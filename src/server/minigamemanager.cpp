/*
* minigamemanager.cpp - Author: Enar Vaikene
*
* Copyright (C) 2006 Atomic Blue (info@planeshift.it, http://www.atomicblue.org)
*
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation (version 2 of the License)
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*
*/

#include <psconfig.h>
#include <ctype.h>

//=============================================================================
// Crystal Space Includes
//=============================================================================
#include <csutil/xmltiny.h>

//=============================================================================
// Project Includes
//=============================================================================
#include "util/log.h"
#include "util/eventmanager.h"
#include "util/minigame.h"
#include "util/psxmlparser.h"

#include "bulkobjects/psactionlocationinfo.h"

//=============================================================================
// Local Includes
//=============================================================================
#include "minigamemanager.h"
#include "psserver.h"
#include "netmanager.h"
#include "client.h"
#include "clients.h"
#include "chatmanager.h"
#include "gem.h"
#include "globals.h"

using namespace psMiniGame;


//#define DEBUG_MINIGAMES

/// Tick interval (10 seconds)
#define MINIGAME_TICK_INTERVAL  10*1000

/// Maximum idle time for players (10 minutes)
#define MINIGAME_IDLE_TIME      60
//#define MINIGAME_IDLE_TIME      6


//---------------------------------------------------------------------------

class MiniGameManagerTick : public psGameEvent
{
    public:

        MiniGameManagerTick(int offset, MiniGameManager *m);
        virtual void Trigger();

    protected:

        MiniGameManager *manager;
};

MiniGameManagerTick::MiniGameManagerTick(int offset, MiniGameManager *m)
    : psGameEvent(0, offset, "psMiniGameManagerTick")
{
    manager = m;
}

void MiniGameManagerTick::Trigger()
{
    manager->Idle();
}


//---------------------------------------------------------------------------

MiniGameManager::MiniGameManager()
{
    psserver->GetEventManager()->Subscribe(this, MSGTYPE_MINIGAME_STARTSTOP, REQUIRE_READY_CLIENT);
    psserver->GetEventManager()->Subscribe(this, MSGTYPE_MINIGAME_UPDATE, REQUIRE_READY_CLIENT);

    MiniGameManagerTick *tick = new MiniGameManagerTick(MINIGAME_TICK_INTERVAL, this);
    psserver->GetEventManager()->Push(tick);
}

MiniGameManager::~MiniGameManager()
{
    psserver->GetEventManager()->Unsubscribe(this, MSGTYPE_MINIGAME_STARTSTOP);
    psserver->GetEventManager()->Unsubscribe(this, MSGTYPE_MINIGAME_UPDATE);
}

bool MiniGameManager::Initialise(void)
{
    // Load gameboard definitions
    psMiniGameBoardDef *gameDef;
    Result gameboards(db->Select("SELECT * from gameboards"));
    if (gameboards.IsValid())
    {
        int i, count = gameboards.Count();
        for (i=0; i<count; i++)
        {
            csString name(gameboards[i]["name"]);
            const char *layout = gameboards[i]["layout"];
            const char *pieces = gameboards[i]["pieces"];
            const uint8_t cols = gameboards[i].GetInt("numColumns");
            const uint8_t rows = gameboards[i].GetInt("numRows");
            int8_t players = gameboards[i].GetInt("numPlayers");
            psString optionsStr(gameboards[i]["gameboardOptions"]);
            csString rulesXML(gameboards[i]["gameRules"]); 

            if (name && layout && pieces &&
                cols >= GAMEBOARD_MIN_COLS && cols <= GAMEBOARD_MAX_COLS &&
                rows >= GAMEBOARD_MIN_ROWS && rows <= GAMEBOARD_MAX_ROWS &&
                strlen(layout) == size_t(rows * cols))
            {
                if (players < GAMEBOARD_MIN_PLAYERS || players > GAMEBOARD_MAX_PLAYERS)
                {
                    Error2("Minigames GameBoard definition %d invalid num players.", i+1);
                    players = GAMEBOARD_DEFAULT_PLAYERS;
                }

                // decipher board layout options
                int16_t options = ParseGameboardOptions(optionsStr);

                gameDef = new psMiniGameBoardDef(cols, rows, layout, pieces, players, options);
                gameBoardDef.Put(name.Downcase(), gameDef);

                gameDef->DetermineGameRules(rulesXML, name);
            }
            else
            {
                Error2("Minigames GameBoard definition %d invalid.", i+1);
                return false;
            }
        }

        return true;
    }

    return false;
}

void MiniGameManager::HandleMessage(MsgEntry *me, Client *client)
{
    if (me->GetType() == MSGTYPE_MINIGAME_STARTSTOP)
    {
        psMGStartStopMessage msg(me);
        if (!msg.valid)
        {
            Error2("Failed to parse psMGStartStopMessage from client %u.", me->clientnum);
            return;
        }

        if (msg.msgStart)
            HandleStartGameRequest(client);
        else
            HandleStopGameRequest(client);

    }

    else if (me->GetType() == MSGTYPE_MINIGAME_UPDATE)
    {
        psMGUpdateMessage msg(me);
        if (!msg.valid)
        {
            Error2("Failed to parse psMGUpdateMessage from client %u.", me->clientnum);
            return;
        }

        HandleGameUpdate(client, msg);
    }

}

void MiniGameManager::HandleStartGameRequest(Client *client)
{
    if (!client || client->GetClientNum() == (uint32_t)-1)
    {
        Error1("Invalid client in the start minigame request.");
        return;
    }

    uint32_t clientID = client->GetClientNum();

    // Verify the target
    gemObject *target = client->GetTargetObject();
    if (!target || strcmp(target->GetObjectType(), "ActionLocation"))
    {
        psserver->SendSystemError(clientID, "You don't have a game board targeted!");
        return;
    }

    // Object type was verified before, so static cast should be safe here
    gemActionLocation *action = static_cast<gemActionLocation *>(target);
    psActionLocation *actionLocation = action->GetAction();
    if (!actionLocation || !actionLocation->IsGameBoard())
    {
        psserver->SendSystemError(clientID, "You don't have a game board targeted!");
        return;
    }

    // Check the range
    if (client->GetActor()->RangeTo(action, true) > RANGE_TO_LOOT)
    {
        psserver->SendSystemError(clientID, "You are not in range to play %s!", actionLocation->name.GetData());
        return;
    }

    // Remove from existing game sessions.
    RemovePlayerFromSessions(playerSessions.Get(clientID, 0), client, clientID);

    // Find an existing session or create a new one.
    // If personal minigame, force new one for player.
    psMiniGameSession *session = GetSessionByID((uint32_t)actionLocation->id);
    if (!session ||
        (session && !session->IsSessionPublic()))
    {
        // Create a new minigame session.
        session = new psMiniGameSession(this, action, actionLocation->name);

        // Try to load the game.
        if (session->Load(actionLocation->response))
        {
            sessions.Push(session);

#ifdef DEBUG_MINIGAMES
            Debug2(LOG_ANY, 0, "Created new minigame session %s", session->GetName().GetData());
#endif

        }
        else
        {
            delete session;
            psserver->SendSystemError(clientID, "Failed to load the game %s", actionLocation->name.GetData());
            return;
        }
    }

    // Add to the player/session map for quicker access.
    playerSessions.Put(clientID, session);

    // And finally add to the session
    session->AddPlayer(client);

}

void MiniGameManager::HandleStopGameRequest(Client *client)
{
    if (!client || client->GetClientNum() == (uint32_t)-1)
    {
        Error1("Invalid client in the stop minigame request.");
        return;
    }

    uint32_t clientID = client->GetClientNum();

    // Remove from existing game sessions.
    RemovePlayerFromSessions(playerSessions.Get(clientID, 0), client, clientID);
}

void MiniGameManager::RemovePlayerFromSessions(psMiniGameSession *session, Client *client, uint32_t clientID)
{
    if (session)
    {
        session->RemovePlayer(client);
        playerSessions.DeleteAll(clientID);

        if (session->GetSessionReset())
            ResetGameSession(session);

        // if session is personal, delete it.
        if (!session->IsSessionPublic())
        {
            if (session->GameSessionActive())
            {
                Error1("Cannot remove personal minigame session as there are still participants."); // should never happen
            }
            else
            {
                sessions.Delete(session);
            }
        }
    }
}

void MiniGameManager::HandleGameUpdate(Client *client, psMGUpdateMessage &msg)
{
    if (!client || client->GetClientNum() == (uint32_t)-1)
    {
        Error1("Invalid client in the minigame update message.");
        return;
    }

    uint32_t clientID = client->GetClientNum();

    // Find the session
    psMiniGameSession *session = playerSessions.Get(clientID, 0);
    if (!session)
    {
        Error2("Received minigame update from client %u without an active session.", clientID);
        return;
    }

    // Verify the session
    if (!session->IsValidToUpdate(client))
    {
        session->Send(clientID, ReadOnly);
        return;
    }

    // Apply updates
    session->Update(client, msg);
}

void MiniGameManager::ResetAllGameSessions(void)
{
    for (size_t i = 0; i < sessions.GetSize(); i++)
    {
        ResetGameSession(sessions.Get(i));
    }
}

psMiniGameSession *MiniGameManager::GetSessionByID(uint32_t id)
{
    for (size_t i = 0; i < sessions.GetSize(); i++)
    {
        if (sessions.Get(i)->GetID() == id)
            return sessions.Get(i);
    }
    return 0;
}

void MiniGameManager::Idle()
{
    for (size_t i = 0; i < sessions.GetSize(); i++)
    {
        sessions.Get(i)->Idle();
    }

    MiniGameManagerTick *tick = new MiniGameManagerTick(MINIGAME_TICK_INTERVAL, this);
    psserver->GetEventManager()->Push(tick);
}

psMiniGameBoardDef *MiniGameManager::FindGameDef(csString gameName)
{
    return gameBoardDef.Get(gameName.Downcase(), NULL);
}

void MiniGameManager::ResetGameSession(psMiniGameSession *sessionToReset)
{
    if (!sessionToReset)
    {
        Error1("Attempt to reset NULL minigame session");
        return;
    }

    if (sessionToReset->GameSessionActive())
    {
        sessionToReset->SetSessionReset();
    }
    else
    {
        if (!sessions.Delete(sessionToReset))
        {
            Error1("Could not remove minigame session");
        }
    }
}

int16_t MiniGameManager::ParseGameboardOptions(psString optionsStr)
{
    uint16_t localOptions = 0;

    // look for black/white. White by default.
    if (optionsStr.FindSubString("black",0,true) != -1)
    {
        localOptions |= BlackSquare;
    }

    // look for checked/plain. Checked by default.
    if (optionsStr.FindSubString("plain",0,true) != -1)
    {
        localOptions |= PlainSquares;
    }

    // parse the options string
    return localOptions;
}

//---------------------------------------------------------------------------

psMiniGameSession::psMiniGameSession(MiniGameManager *mng, gemActionLocation *obj, const char *name)
{
    manager = mng;
    actionObject = obj;
    id = (uint32_t)obj->GetAction()->id;
    this->name = name;
    currentCounter = 0;
    whitePlayerID = (uint32_t)-1;
    blackPlayerID = (uint32_t)-1;
    toReset = false;
    nextPlayerToMove = 0;
}

psMiniGameSession::~psMiniGameSession()
{

    // Unregister pending call-backs. Right now clients are disconnected before deleting game
    // sessions, but it might be changed in the future.
    ClientConnectionSet *clients = psserver->GetConnections();
    if (clients)
    {
        if (whitePlayerID != (uint32_t)-1)
        {
            Client *client = clients->Find(whitePlayerID);
            if (client && client->GetActor())
                client->GetActor()->UnregisterCallback(this);
        }
        if (blackPlayerID != (uint32_t)-1)
        {
            Client *client = clients->Find(blackPlayerID);
            if (client && client->GetActor())
                client->GetActor()->UnregisterCallback(this);
        }
        csArray<uint32_t>::Iterator iter = watchers.GetIterator();
        while (iter.HasNext())
        {
            Client *client = clients->Find(iter.Next());
            if (client && client->GetActor())
                client->GetActor()->UnregisterCallback(this);
        }
    }
}

bool psMiniGameSession::Load(csString &responseString)
{
#ifdef DEBUG_MINIGAMES
    Debug2(LOG_ANY, 0, "Loading minigame: %s", responseString.GetData());
#endif

    options = 0;

    if (!responseString.StartsWith("<Examine>", false))
    {
        Error2("Invalid response string for minigame %s.", name.GetData());
        return false;
    }

    csRef<iDocument> doc = ParseString(responseString);
    if (!doc)
    {
        Error2("Parse error in minigame %s response string.", name.GetData());
        return false;
    }

    csRef<iDocumentNode> root = doc->GetRoot();
    if (!root)
    {
        Error2("No XML root in minigame %s response string.", name.GetData());
        return false;
    }

    csRef<iDocumentNode> topNode = root->GetNode("Examine");
    if (!topNode)
    {
        Error2("No <Examine> tag in minigame %s response string.", name.GetData());
        return false;
    }

    csRef<iDocumentNode> boardNode = topNode->GetNode("GameBoard");
    if (!boardNode )
    {
        Error2("No <Board> tag in minigame %s response string.", name.GetData());
        return false;
    }

    const char *gameName = boardNode->GetAttributeValue("Name");
    psMiniGameBoardDef *gameBoardDef = manager->FindGameDef(gameName);
    if (!gameBoardDef)
    {
        Error2("Game board \"%s\" not defined.", gameName);
        return false;
    }

    // Get the optional layout string; will override the default layout
    const char *layoutStr = boardNode->GetAttributeValue("Layout");
    uint8_t *layout = NULL;
    if (layoutStr && strlen(layoutStr) != size_t(gameBoardDef->GetRows() * gameBoardDef->GetCols()))
    {
        Error2("Layout string in action_location minigame %s response string invalid. Using default.", gameName);
    }
    else if (layoutStr)
    {
        layout = new uint8_t[gameBoardDef->GetLayoutSize()];
        gameBoardDef->PackLayoutString(layoutStr, layout);
    }

    // get the gameboard def options
    options |= gameBoardDef->GetGameboardOptions();

    // get the session type. "Personal" means a minigame session per player; each player gets their
    // own personal session (i.e. no watchers, and restricted to single player games). Expected application
    // for personal minigames is in-game puzzles, including quest steps maybe.
    // "Public" is the more traditional, two player leisure games, such as Groffels Toe.
    csString sessionStr(boardNode->GetAttributeValue("Session"));
    if (sessionStr.Downcase() == "personal")
    {
        options |= PersonalGame;
    }
    else if (sessionStr.Length() > 0 && sessionStr.Downcase() != "public")
    {
        Error2("Session setting for minigame %s invalid. Defaulting to \'Public\'", gameName);
    }
    
    // Setup the game board
    gameBoard.Setup(gameBoardDef, layout);

    // if session is personal, check its a 1-player game
    if ((options & PersonalGame) && gameBoard.GetNumPlayers() > 1)
    {
        Error2("Personal game %s has 2 or more players, which is untenable.", gameName);
        return false;
    }

    // intialise player to move first, if appropriate
    if (gameBoard.GetPlayerTurnRule() == ORDERED)
        nextPlayerToMove = 1;

    return true;
}

void psMiniGameSession::Restart()
{

}

void psMiniGameSession::AddPlayer(Client *client)
{
    if (!client || client->GetClientNum() == (uint32_t)-1)
        return;

    uint32_t clientID = client->GetClientNum();

    // If there is no player with white pieces, give to this player white pieces
    if (whitePlayerID == (uint32_t)-1)
    {
        whitePlayerID = clientID;

        whiteIdleCounter = MINIGAME_IDLE_TIME;

        // If this is a managed game, reset the game board
        if (options & ManagedGame)
            Restart();

        // Send notifications
        psSystemMessage newmsg(clientID, MSG_INFO, "%s started playing %s with white pieces.",
                               client->GetName(), name.GetData());
        newmsg.Multicast(client->GetActor()->GetMulticastClients(), 0, CHAT_SAY_RANGE);
    }

    // Or if there is no player with black pieces, and its a 2-player game, give black pieces
    else if (blackPlayerID == (uint32_t)-1 && gameBoard.GetNumPlayers() >= 2)
    {
        blackPlayerID = clientID;

        blackIdleCounter = MINIGAME_IDLE_TIME;

        // Send notifications
        psSystemMessage newmsg(clientID, MSG_INFO, "%s started playing %s with black pieces.",
                               client->GetName(), name.GetData());
        newmsg.Multicast(client->GetActor()->GetMulticastClients(), 0, CHAT_SAY_RANGE);

    }

    // Otherwise add to the list of watchers
    else
    {
        watchers.Push(clientID);
    }

    // Register our call-back function to detect disconnected clients
    client->GetActor()->RegisterCallback(this);

    // Broadcast the game board
    Broadcast();

#ifdef DEBUG_MINIGAMES
    Debug3(LOG_ANY, 0, "Added player %u to the game session \"%s\"\n", clientID, name.GetData());
#endif

}

void psMiniGameSession::RemovePlayer(Client *client)
{
    if (!client || client->GetClientNum() == (uint32_t)-1)
        return;

    client->GetActor()->UnregisterCallback(this);

    uint32_t clientID = client->GetClientNum();

    // Is it the player with white pieces?
    if (whitePlayerID == clientID)
    {
        whitePlayerID = (uint32_t)-1;

        // Send notifications
        psSystemMessage newmsg(clientID, MSG_INFO, "%s stopped playing %s.",
                               client->GetName(), name.GetData());
        newmsg.Multicast(client->GetActor()->GetMulticastClients(), 0, CHAT_SAY_RANGE);

    }

    // Or the player with black pieces?
    else if (blackPlayerID == clientID)
    {
        blackPlayerID = (uint32_t)-1;

        // Send notifications
        psSystemMessage newmsg(clientID, MSG_INFO, "%s stopped playing %s.",
                               client->GetName(), name.GetData());
        newmsg.Multicast(client->GetActor()->GetMulticastClients(), 0, CHAT_SAY_RANGE);
    }

    // Otherwise it is one of the watchers
    else
    {
        watchers.Delete(clientID);
    }

    // Send the STOP game message
    psMGStartStopMessage msg(clientID, false);
    msg.SendMessage();


#ifdef DEBUG_MINIGAMES
    Debug3(LOG_ANY, 0, "Removed player %u from the game session \"%s\"\n", clientID, name.GetData());
#endif
}

void psMiniGameSession::Send(uint32_t clientID, uint32_t modOptions)
{
    psMGBoardMessage msg(clientID, currentCounter, id, options | modOptions,
                         gameBoard.GetCols(), gameBoard.GetRows(), gameBoard.GetLayout(),
                         gameBoard.GetNumPieces(), gameBoard.GetPieces());
    msg.SendMessage();
}

void psMiniGameSession::Broadcast()
{

    if (whitePlayerID != (uint32_t)-1)
    {
        Send(whitePlayerID, 0);
    }

    if (blackPlayerID != (uint32_t)-1)
    {
        Send(blackPlayerID, BlackPieces);
    }

    csArray<uint32_t>::Iterator iter = watchers.GetIterator();
    while (iter.HasNext())
    {
        uint32_t id = iter.Next();
        if (id != (uint32_t)-1)
            Send(id, ReadOnly);
    }
}

bool psMiniGameSession::IsValidToUpdate(Client *client) const
{

    if (!client || client->GetClientNum() == (uint32_t)-1)
         return false;

    uint32_t clientID = client->GetClientNum();

    if (!actionObject.IsValid())
    {
        Error2("gemActionLocation for the minigame %s was deleted.", name.GetData());
        psserver->SendSystemError(clientID, "Oops! Game was deleted by the server!");
        return false;
    }

    // TODO: Implement more complex verification for managed games

    if (clientID != whitePlayerID && clientID != blackPlayerID)
    {
        psserver->SendSystemError(clientID, "You are not playing %s!", name.GetData());
        return false;
    }

    return true;
}

void psMiniGameSession::Update(Client *client, psMGUpdateMessage &msg)
{
    uint32_t clientnum = client->GetClientNum();

    // Verify the message version
    if (!msg.IsNewerThan(currentCounter))
    {
        Error3("Ignoring minigame update message with version %d because our version is %d.",
               msg.msgCounter, currentCounter);
        return;
    }
    currentCounter = msg.msgCounter;

    // Verify range
    if (client->GetActor()->RangeTo(actionObject, true) > RANGE_TO_LOOT)
    {
        psserver->SendSystemError(clientnum, "You are not in range to play %s!",
                                  name.GetData());
        // Reset the client's game board
        ResendBoardLayout(clientnum);
        return;
    }

    // valid move?
    if ((nextPlayerToMove == 1 && clientnum == blackPlayerID) ||
        (nextPlayerToMove == 2 && clientnum == whitePlayerID))
    {
        psserver->SendSystemError(clientnum, "It is not your turn to move.");
        // Reset the client's game board
        ResendBoardLayout(clientnum);
        return;
    }

    // Check moves and apply updates if rules passed
    bool moveAccepted;
    if (msg.msgNumUpdates == 1)
    {
        // 1 update means a new piece is added to the board
        moveAccepted = GameMovePassesRules(clientnum,
                                           (msg.msgUpdates[0] & 0xF0) >> 4,
                                           msg.msgUpdates[0] & 0x0F,
                                           msg.msgUpdates[1]);
    }
    else if (msg.msgNumUpdates == 2)
    {
        // 2 updates means a piece has been moved from one tile to another
        moveAccepted = GameMovePassesRules(clientnum,
                                           (msg.msgUpdates[0] & 0xF0) >> 4,
                                           msg.msgUpdates[0] & 0x0F,
                                           msg.msgUpdates[1],
                                           (msg.msgUpdates[2] & 0xF0) >> 4,
                                           msg.msgUpdates[2] & 0x0F,
                                           msg.msgUpdates[3]);
    }

    if (!moveAccepted)
    {
        psserver->SendSystemError(clientnum, "Illegal move.");
        // Reset the client's game board
        ResendBoardLayout(clientnum);
        return;
    }

    // Apply updates
    for (int i = 0; i < msg.msgNumUpdates; i++)
    {
        gameBoard.Set((msg.msgUpdates[2*i] & 0xF0) >> 4,
                      msg.msgUpdates[2*i] & 0x0F,
                      msg.msgUpdates[2*i+1]);
    }

    // Update idle counters
    if (clientnum == whitePlayerID)
        whiteIdleCounter = MINIGAME_IDLE_TIME;
    else if (clientnum == blackPlayerID)
        blackIdleCounter = MINIGAME_IDLE_TIME;

    // Broadcast the new game board layout
    Broadcast();

    // broadcast the move info too
    csString movedText = GetName() + csString(": ") + csString(client->GetName()) + 
                         csString((clientnum==whitePlayerID)?" (white)":" (black)") + csString(" has moved.");
    if (clientnum == whitePlayerID && blackPlayerID != (uint32_t)-1)
    {
        psserver->SendSystemInfo(blackPlayerID, movedText);
    }
    else if (clientnum == blackPlayerID && whitePlayerID != (uint32_t)-1)
    {
        psserver->SendSystemInfo(whitePlayerID, movedText);
    }
    csArray<uint32_t>::Iterator iter = watchers.GetIterator();
    while (iter.HasNext())
    {
        uint32_t id = iter.Next();
        if (movedText && id != (uint32_t)-1)
            psserver->SendSystemInfo(id, movedText);
    }

    // whos move next?
    if (nextPlayerToMove > 0)
    {
        if (nextPlayerToMove < gameBoard.GetNumPlayers())
            nextPlayerToMove++;
        else
            nextPlayerToMove = 1;
    }
}

void psMiniGameSession::DeleteObjectCallback(iDeleteNotificationObject * object)
{
    gemActor *actor = (gemActor *)object;
    if (!actor)
        return;

    Client *client = actor->GetClient();

    if (client)
        RemovePlayer(client);
}

void psMiniGameSession::Idle()
{

    // Idle counters

    if (whiteIdleCounter > 0 && whitePlayerID != (uint32_t)-1)
    {
#ifdef DEBUG_MINIGAMES
        Debug2(LOG_ANY, 0, "White player idle counter %d", whiteIdleCounter);
#endif

        if ((--whiteIdleCounter) == 0)
        {
            Client *client = psserver->GetNetManager()->GetClient(whitePlayerID);
            if (client)
                RemovePlayer(client);
        }
    }

    if (blackIdleCounter > 0 && blackPlayerID != (uint32_t)-1)
    {

#ifdef DEBUG_MINIGAMES
        Debug2(LOG_ANY, 0, "Black player idle counter %d", blackIdleCounter);
#endif

        if ((--blackIdleCounter) == 0)
        {
            Client *client = psserver->GetNetManager()->GetClient(blackPlayerID);
            if (client)
                RemovePlayer(client);
        }
    }

    // Range check for watchers

    size_t i = 0;
    while (i < watchers.GetSize())
    {
        Client *client = psserver->GetNetManager()->GetClient(watchers.Get(i));
        if (!client)
        {
            ++i;
            continue;
        }

        if (client->GetActor()->RangeTo(actionObject, true) > RANGE_TO_SELECT)
        {
            RemovePlayer(client);
            continue;
        }

        ++i;
    }
}

bool psMiniGameSession::GameSessionActive(void)
{
    return ((whitePlayerID!=(uint32_t)-1) || (blackPlayerID!=(uint32_t)-1) || !watchers.IsEmpty());
}

bool psMiniGameSession::IsSessionPublic(void)
{
    return !(options & PersonalGame);
}

void psMiniGameSession::ResendBoardLayout(uint32_t clientnum)
{
    if (clientnum == whitePlayerID)
        Send(whitePlayerID, DisallowedMove);
    else if (clientnum == blackPlayerID)
        Send(blackPlayerID, BlackPieces | DisallowedMove);
    else
        Send(clientnum, ReadOnly | DisallowedMove);

    // after a disallowed move, the other clients will be out of sync
    // with server & disallowed client, so resync.
    currentCounter--;
}

// checks move passes rules. If col/row/state-2 are -1 then a new piece is played,
// otherwise an existing piece is moved
bool psMiniGameSession::GameMovePassesRules(uint32_t movingClient,
                                            int8_t col1, int8_t row1, int8_t state1,
                                            int8_t col2, int8_t row2, int8_t state2)
{
    bool newPiecePlayed = (state2 == -1);

    if ((newPiecePlayed && gameBoard.GetMovePieceTypeRule() == MOVE_ONLY) ||
        (!newPiecePlayed && gameBoard.GetMovePieceTypeRule() == PLACE_ONLY))
    {
        return false;
    }

    if (gameBoard.GetMoveablePiecesRule() == OWN_PIECES_ONLY)
    {
        TileStates movingPiece = (TileStates) gameBoard.Get(col1, row1);
        if (movingClient == whitePlayerID && (movingPiece < White1 || movingPiece > White7))
        {
            return false;
        }
        else if (movingClient == blackPlayerID && (movingPiece < Black1 || movingPiece > Black7))
        {
            return false;
        }
    }

    if (!newPiecePlayed && 
        gameBoard.GetMovePiecesToRule() == VACANCY_ONLY && gameBoard.Get(col2, row2) != EmptyTile)
    {
        return false;
    }

    return true;
}

