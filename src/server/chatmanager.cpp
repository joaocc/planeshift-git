/*
 * chatmanager.cpp
 *
 * Copyright (C) 2003 Atomic Blue (info@planeshift.it, http://www.atomicblue.org)
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
#include <string.h>
#include <memory.h>

//=============================================================================
// Crystal Space Includes
//=============================================================================

//=============================================================================
// Project Space Includes
//=============================================================================
#include "util/serverconsole.h"
#include "util/log.h"
#include "util/pserror.h"
#include "util/eventmanager.h"
#include "util/strutil.h"

#include "net/msghandler.h"

#include "bulkobjects/psnpcdialog.h"
#include "bulkobjects/dictionary.h"
#include "bulkobjects/psguildinfo.h"

//=============================================================================
// Local Space Includes
//=============================================================================
#include "cachemanager.h"
#include "chatmanager.h"
#include "clients.h"
#include "playergroup.h"
#include "gem.h"
#include "globals.h"
#include "psserver.h"
#include "npcmanager.h"
#include "adminmanager.h"


ChatManager::ChatManager()
{
    psserver->GetEventManager()->Subscribe(this,MSGTYPE_CHAT,REQUIRE_ALIVE);
}

ChatManager::~ChatManager()
{
    psserver->GetEventManager()->Unsubscribe(this,MSGTYPE_CHAT);
}

void ChatManager::HandleMessage(MsgEntry *me, Client *client)
{
    psChatMessage msg(me);

    // Dont
    if (!msg.valid)
    {
        Debug2(LOG_NET,me->clientnum,"Received unparsable psChatMessage from client %u.\n",me->clientnum);
        return;
    }

    const char *pType = msg.GetTypeText();

    if (msg.iChatType != CHAT_TELL)
    {
        Debug4(LOG_CHAT, client->GetClientNum(),
                "%s %s: %s\n", client->GetName(),
                pType, (const char *) msg.sText);
    }
    else
    {
        Debug5(LOG_CHAT,client->GetClientNum(), "%s %s %s: %s\n", client->GetName(),
               pType, (const char *)msg.sPerson,(const char *)msg.sText);
    }

    bool saveFlood = true;

    if (!client->IsMute())
    {
      // Send Chat to other players
      switch (msg.iChatType)
      {
          case CHAT_GUILD:
          {
              SendGuild(client, msg);
              break;
          }
          case CHAT_GROUP:
          {
              SendGroup(client, msg);
              break;
           }
          case CHAT_AUCTION:
          case CHAT_SHOUT:
          {
              SendShout(client, msg);
              break;
          }
          case CHAT_PET_ACTION:
          {
              gemNPC *pet = NULL;

              // Check if a specific pet's name was specified, in one of these forms:
              // - /mypet Petname ...
              // - /mypet Petname's ...
              size_t numPets = client->GetNumPets();
              for (size_t i = 0; i < numPets; i++)
              {
                  if ((pet = dynamic_cast <gemNPC*>(client->GetPet(i)))
                      && msg.sText.StartsWith(pet->GetCharacterData()->GetCharName(), true))
                  {
                      size_t n = strlen(pet->GetCharacterData()->GetCharName());
                      if (msg.sText.Length() >= n + 1 && msg.sText.GetAt(n) == ' ')
                      {
                          msg.sText.DeleteAt(0, n);
                          msg.sText.LTrim();
                          break;
                      }
                      else if (msg.sText.Length() >= n + 3 && msg.sText.GetAt(n) == '\''
                               && msg.sText.GetAt(n + 1) == 's' && msg.sText.GetAt(n + 2) == ' ')
                      {
                          msg.sText.DeleteAt(0, n);
                          break;
                      }
                  }
                  else pet = NULL;
              }
              // If no particular pet was specified, assume the default familiar...
              if (!pet)
                  pet = dynamic_cast <gemNPC*>(client->GetFamiliar());

              // Send the message or an appropriate error...
              if (!pet)
                  psserver->SendSystemInfo(me->clientnum, "You have no familiar to command.");
              else
                  SendSay(client->GetClientNum(), pet, msg, pet->GetCharacterData()->GetCharFullName());

              break;
          }
          case CHAT_MY:
          case CHAT_ME:
          case CHAT_SAY:
          {
              // Send to all if there's no NPC response or the response is public
              SendSay(client->GetClientNum(), client->GetActor(), msg, client->GetName());
              break;
          }
          case CHAT_NPC:
          {
              // Only the speaker sees his successful chatting with an npc.
              // This helps quests stay secret.
              psChatMessage newMsg(client->GetClientNum(), client->GetName(), 0,
                  msg.sText, msg.iChatType, msg.translate);
              newMsg.SendMessage();
              saveFlood = false;

              gemObject *target = client->GetTargetObject();
              gemNPC *targetnpc = dynamic_cast<gemNPC*>(target);
              NpcResponse *resp = CheckNPCResponse(msg,client,targetnpc);
              if (resp)
                  resp->ExecuteScript(client, targetnpc);

              break;
          }
          case CHAT_TELL:
          {
              if ( msg.sPerson.Length() == 0 )
              {
                  psserver->SendSystemError(client->GetClientNum(), "You must specify name of player.");
                  break;
              }

              Client *target = FindPlayerClient(msg.sPerson);
              if (target && !target->IsSuperClient())
              {
                  SendTell(msg, client->GetName(), client, target);

                  // Save to chat history
	              client->GetActor()->LogMessage(client->GetActor()->GetName(), msg);
				  if (target->GetActor()) // this can be null if someone sends a tell to a connecting client
	                  target->GetActor()->LogMessage(client->GetActor()->GetName(), msg);
              }
              else
              {
                  psserver->SendSystemError(client->GetClientNum(), "%s is not found online.", msg.sPerson.GetDataSafe());
              }
              break;
          }
          case CHAT_REPORT:
          {
              // First thing to extract the name of the player to log
              csString targetName;
              int index = (int)msg.sText.FindFirst(' ', 0);
              if ( index == -1 )
                 targetName = msg.sText;
              else
                 targetName = msg.sText.Slice(0, index);
              targetName = NormalizeCharacterName(targetName);

              if ( msg.sText.Length() == 0 )
              {
                  psserver->SendSystemError(client->GetClientNum(), "You must specify name of player.");
                  break;
              }

              Client * target = psserver->GetConnections()->Find(targetName);
              if ( !target )
              {
                  psserver->SendSystemError(client->GetClientNum(), "%s is not found online.", targetName.GetData());
                  break;
              }
              if (target->IsSuperClient())
              {
                  psserver->SendSystemError(client->GetClientNum(), "Can't report NPCs.");
                  break;
              }

              if (!client->GetActor()->IsLoggingChat())
              {
                  psserver->SendSystemError(client->GetClientNum(), "%s will be logged for five minutes now.", targetName.GetData());
                  psserver->SendSystemError(target->GetClientNum(), "Your last 5 minutes of chat has been reported to the GMs, logging will now continue.");
              }
              else
              {
                  if (target->GetClientNum() != client->GetActor()->GetReportTargetId())
                  {
                      psserver->SendSystemError(client->GetClientNum(), "Previous logging is still active.");
                      break;
                  }
                  psserver->SendSystemError(client->GetClientNum(), "Logging for another five minutes.");
              }
              client->GetActor()->AddChatReport(target->GetActor());
              psserver->GetEventManager()->Push(new psEndChatLoggingEvent(client->GetClientNum(), 300000));
              break;
         }
         case CHAT_ADVISOR:
         case CHAT_ADVICE:
          {
             break;
         }

         default:
         {
              Error2("Unknown Chat Type: %d\n",msg.iChatType);
              break;
         }
       }
    }
    else
    {
        //User is muted but tries to chat anyway. Remind the user that he/she/it is muted
        psserver->SendSystemInfo(client->GetClientNum(),"You can't send messages because you are muted.");
    }

    if (saveFlood)
        client->FloodControl(msg.iChatType, msg.sText, msg.sPerson);
}

/// TODO: This function is guaranteed not to work atm.-Keith
void ChatManager::SendNotice(psChatMessage& msg)
{
    //SendSay(NULL, msg, "Server");
}


void ChatManager::SendShout(Client *c, psChatMessage& msg)
{
    psChatMessage newMsg(c->GetClientNum(), c->GetName(), 0, msg.sText, msg.iChatType, msg.translate);

    if (c->GetActor()->GetCharacterData()->GetTotalOnlineTime() > 3600 || c->GetActor()->GetSecurityLevel() >= GM_LEVEL_0)
    {
        csArray<PublishDestination>& clients = c->GetActor()->GetMulticastClients();
        newMsg.Multicast(clients, 0, PROX_LIST_ANY_RANGE );

        // The message is saved to the chat history of all the clients around
        for (size_t i = 0; i < clients.GetSize(); i++)
        {
            Client *target = psserver->GetConnections()->Find(clients[i].client);
            if (target && target->IsReady())
                target->GetActor()->LogMessage(c->GetActor()->GetName(), newMsg);
        }
    }
    else
    {
        psserver->SendSystemError(c->GetClientNum(), "You are not allowed to shout or auction until you have been in-game for at least 1 hour.");
        psserver->SendSystemInfo(c->GetClientNum(), "Please use the Help tab or /advisor if you need help.");
    }
}

void ChatManager::SendSay(uint32_t clientNum, gemActor *actor, psChatMessage& msg,const char* who)
{
    psChatMessage newMsg(clientNum, who, 0, msg.sText, msg.iChatType, msg.translate);
    csArray<PublishDestination>& clients = actor->GetMulticastClients();
    newMsg.Multicast(clients, 0, CHAT_SAY_RANGE );

    // The message is saved to the chat history of all the clients around
    for (size_t i = 0; i < clients.GetSize(); i++)
    {
        Client *target = psserver->GetConnections()->Find(clients[i].client);
        if (target && target->IsReady() && clients[i].dist < CHAT_SAY_RANGE)
            target->GetActor()->LogMessage(actor->GetName(), newMsg);
    }
}

void ChatManager::SendGuild(Client *client, psChatMessage& msg)
{
    psGuildInfo * guild;
    psGuildLevel * level;

    guild = client->GetCharacterData()->GetGuild();
    if (guild == NULL)
    {
        psserver->SendSystemInfo(client->GetClientNum(), "You are not in a guild.");
        return;
    }

    level = client->GetCharacterData()->GetGuildLevel();
    if (level && !level->HasRights(RIGHTS_CHAT))
    {
        psserver->SendSystemInfo(client->GetClientNum(), "You are not allowed to use your guild's chat channel.");
        return;
    }

    SendGuild(client->GetName(), guild, msg);
}

void ChatManager::SendGuild(const csString & sender, psGuildInfo * guild, psChatMessage& msg)
{
    ClientIterator iter(*psserver->GetConnections() );
    psGuildLevel * level;

    while(iter.HasNext())
    {
        Client *client = iter.Next();
        if (client->GetGuildID() == guild->id)
        {
            level = client->GetCharacterData()->GetGuildLevel();
            if (level!=NULL  &&  level->HasRights(RIGHTS_VIEW_CHAT))
            {
                psChatMessage newMsg(client->GetClientNum(), sender, 0, msg.sText, msg.iChatType, msg.translate);
                newMsg.SendMessage();
            }
        }
    }
}

void ChatManager::SendGroup(Client * client, psChatMessage& msg)
{
    csRef<PlayerGroup> group = client->GetActor()->GetGroup();
    if (group)
    {
        psChatMessage newMsg(0, client->GetName(), 0, msg.sText, msg.iChatType, msg.translate);
        group->Broadcast(newMsg.msg);
    }
    else
    {
        psserver->SendSystemInfo(client->GetClientNum(), "You are not part of any group.");
    }
}


void ChatManager::SendTell(psChatMessage& msg, const char* who,Client *client,Client *p)
{
    Debug2(LOG_CHAT, client->GetClientNum(), "SendTell: %s!", msg.sText.GetDataSafe());

    // Sanity check that we are sending to correct clientnum!
    csString targetName = msg.sPerson;
    NormalizeCharacterName(targetName);
    CS_ASSERT(strcasecmp(p->GetName(), targetName) == 0);

    // Create a new message and send it to that person if found
    psChatMessage cmsg(p->GetClientNum(), who, 0, msg.sText, msg.iChatType, msg.translate);
    cmsg.SendMessage();

    // Echo the message back to the speaker also
    psChatMessage cmsg2(client->GetClientNum(), msg.sPerson, 0, msg.sText, CHAT_TELLSELF, msg.translate);
    cmsg2.SendMessage();
}

#define MAX_NPC_DIALOG_DIST 5

NpcResponse *ChatManager::CheckNPCEvent(Client *client,csString& triggerText,gemNPC * &target)
{
    gemNPC *npc = target;

    if (npc && npc->IsAlive())
    {
        csString trigger(triggerText);
        trigger.Downcase();

        psNPCDialog *npcdlg = npc->GetNPCDialogPtr();

        if (npcdlg)  // if NULL, then NPC never speaks
        {
            float dist = npc->RangeTo( client->GetActor() );

            if (dist > MAX_NPC_DIALOG_DIST)
                return NULL;

            Debug3(LOG_NPC, client->GetClientNum(),"%s checking trigger %s.\n",target->GetName(),trigger.GetData() );
            return npcdlg->Respond(trigger,client);
        }
        else
            Debug2(LOG_NPC, client->GetClientNum(),"NPC %s cannot speak.\n",npc->GetName() );  // can comment this out later
    }
    return NULL;
}

NpcResponse *ChatManager::CheckNPCResponse(psChatMessage& msg,Client *client,gemNPC * &target)
{
    return CheckNPCEvent(client,msg.sText,target);  // <L MONEY="0,0,0,3"></L>
}



psEndChatLoggingEvent::psEndChatLoggingEvent(uint32_t _clientnum, const int delayticks=5000)
  : psGameEvent(0,delayticks,"psEndChatLoggingEvent")
{
#ifdef _psEndChatLoggingEvent_DEBUG_
CPrintf(CON_DEBUG, "EndOfChatLoggingEvent created for clientnum %i!", _clientnum);
#endif

    clientnum = _clientnum;
}

void psEndChatLoggingEvent::Trigger()
{
#ifdef _psEndChatLoggingEvent_DEBUG_
CPrintf(CON_DEBUG, "EndOfChatLoggingEvent is about to happen on clientnum %i!", clientnum);
#endif

    Client *client = NULL;

    client = psserver->GetConnections()->Find(clientnum);
    if (!client)
    {
#ifdef _psEndChatLoggingEvent_DEBUG_
CPrintf(CON_DEBUG, "EndOfChatLoggingEvent on unknown client!");
#endif
        return;
    }
    client->GetActor()->RemoveChatReport();
}

