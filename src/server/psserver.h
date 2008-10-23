/*
 * psserver.h
 *
 * Copyright (C) 2001 Atomic Blue (info@planeshift.it, http://www.atomicblue.org)
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

#ifndef __PSSERVER_H__
#define __PSSERVER_H__
#include "psstdint.h"

//=============================================================================
// Crystal Space Includes
//=============================================================================
#include <csutil/ref.h>
#include <csutil/randomgen.h>
#include <csutil/csstring.h>

//=============================================================================
// Project Includes
//=============================================================================
#include "util/psconst.h"

//=============================================================================
// Local Includes
//=============================================================================


struct iObjectRegistry;
struct iConfigManager;
class  NetManager;
struct iChat;
class  EntityManager;
class  MsgHandler;          // Receive and distributes incoming messages.
class  psAuthenticationServer;
class  ChatManager;
class  GuildManager;
class  GroupManager;
class  AdminManager;
class  UserManager;
class  WeatherManager;
class  psTimerThread;
class  psDatabase;
class  psServerCharManager;
class  SpawnManager;
class  EventManager;
class  TutorialManager;
class  ServerStatus;
class  psMarriageManager;
class  psCombatManager;
class  psSpellManager;
class  ExchangeManager;
class  ProgressionManager;
class  NPCManager;
class  CacheManager;
class  psCharacterLoader;
class  csRandomGen;
class  psCharCreationManager;
class  QuestManager;
class  EconomyManager;
class  psWorkManager;
class  QuestionManager;
class  ClientConnectionSet;
class  Client;
class  MathScriptEngine;
class  AdviceManager;
class  ActionManager;
class  IntroductionManager;
class  psMiniGameManager;
class  LogCSV;
class  iResultSet;
class  csVector3;
struct iVFS;
class  GMEventManager;
class  BankManager;
class  ServerConsole;

/** The main server class.
 * The main server class holding references to important server objects.
 */
class psServer
{
public:

    /** Default constructor.
     * Just sets all the pointers to NULL.
     */
    psServer();
    /** Destructor.
     * Cleans up all allocated memory and removes all the players from the
     * world.
     */
    ~psServer();
    /** Initialize the server.
     * Initializes the following modules:
     *     - marriage Manager ( psMarriageManager )
     *     - database ( psDatabase )
     *     - combat manager ( psCombatManager )
     *     - weather manager ( WeatherManager )
     *     - user manager ( UserManager )
     *     - chat manager ( ChatManager )
     *     - advice manager ( AdviceManager )
     *     - guild manager ( GuildManager )
     *     - group manager ( GroupManager )
     *     - character manager ( psServerCharManager )
     *     - spawn manager ( psSpawnManager )
     *     - admin manager ( AdminManager )
     *     - CEL Server ( EntityManager )
     *     - exchange manager ( ExchangeManager )
     *     - minigame manager ( MiniGameManager )
     *     .
     * Starts a thread for:
     *     - Network ( NetManager )
     *         Binds the socket for the server also.
     *     - Message Handler ( MsgHandler )
     *
     * @param objreg: Is a reference to the object registry.
     * @return Returns success or failure.
     */
    bool Initialize(iObjectRegistry* objreg);
    /** Runs a script file to set up server and goes to the consoles main loop.
     * Load and starts an optional script file whose name was given on the
     * command line using the "run script_filename" option. This script sets
     * up the server by executing console commands. After that the function
     * jumps into the server console's main loop, accepting commands from the
     * user.
     *
     * @see ServerConsole::MainLoop()
     */
    void MainLoop ();

    /** Checks to see if a world is loaded
     *
     * @return Returns a true if a world has been loaded.
     */
    bool IsMapLoaded() { return MapLoaded; }

    /** Loads a map and changes the MapLoaded
     *
     * @return Returns a true if the world loaded.
     */
    bool LoadMap(char* mapname);

    /** Checks to see if a world is loaded ready to go.
     *
     * @return Returns a true if a world has been loaded and is ready to go.
     * @see EntityManager::IsReady()
     */
    bool IsReady();
    /** Checks to see if a world has loaded in the past.
     *
     * @return Returns a true if a world has been loaded or is still loaded.
     * @see EntityManager::HasBeenReady()
     */
    bool HasBeenReady();
    /** Checks if the server is full
     *
     * @param numclients Current number of connected clients
     * @param client Will say no if client is GM
     */
    bool IsFull(size_t numclients, Client * client);
    /** Removes a player from the world.
     * It will save the players state information and remove them from the
     * world.
     *
     * @param clientnum: Is the number of the client whose player we wish to
     *     remove from the world.
     * @param reason: Is a string representing the reason we are removing the
     *     player from the world.
     */
    void RemovePlayer (uint32_t clientnum,const char *reason);

    /** Mutes a player from the world.
     * It will prevent them from being heard, until they log back in
     * @param clientnum: Is the number of the client whose player we wish to
     *     remove from the world.
     * @param reason: Is a string representing the reason we are removing the
     *     player from the world.
     */
    void MutePlayer (uint32_t clientnum,const char *reason);
    //Unmutes player
    void UnmutePlayer (uint32_t clientnum,const char *reason);

    /** Gets the MOTD string */
    const char * GetMOTD(){return (const char*)motd.GetData();}
    /** Sets the MOTD string */
    void SetMOTD(const char* str){motd=str;}

    /** Returns the server's network thread.
     *
     * @return Returns the NetManager object for the current networking thread.
     */
    NetManager* GetNetManager() { return netmanager; }
    /** Returns the Chat manager.
     *
     * @return Returns a reference to the current chat manager server.
     */
    ChatManager* GetChatManager() { return chatmanager; }
    /** Returns the Advice manager.
     *
     * @return Returns a reference to the current advice manager.
     */
    AdviceManager* GetAdviceManager() { return advicemanager; }
    /** Returns the Advice manager.
     *
     * @return Returns a reference to the current advice manager.
     */
    ActionManager* GetActionManager() { return actionmanager; }
    /** Returns the NPC Superclient manager.
     *
     * @return Returns a reference to the current npc manager.
     */
    NPCManager* GetNPCManager() { return npcmanager; }
    /** Returns the database manager.
     *
     * @return Returns a reference to the current database manager.
     */
    psDatabase* GetDatabase() { return database; }
    /** Returns the configuration manager.
     *
     * @return Returns a reference to the current configuration manager object.
     */
    iConfigManager* GetConfig() { return configmanager; }
    /** Returns the object registry.
     *
     * @return Returns a reference to the current object registry.
     */
    iObjectRegistry* GetObjectReg() { return objreg; }
    /** Returns the spawn manager.
     *
     * @return Returns a reference to the current spawn manager.
     */
    SpawnManager* GetSpawnManager() { return spawnmanager; }
    /** Returns the event manager.
     *
     * @return Returns a reference to the current event manager.
     */
    EventManager* GetEventManager() { return eventmanager; }

    /** Returns the Weather manager.
     *
     * @return Returns a reference to the current weather manager.
     */
    WeatherManager*  GetWeatherManager() { return weathermanager; }

    /** Returns the Math Scripting Engine.
     *
     * @return Returns a ptr to the math script engine.
     */
    MathScriptEngine*  GetMathScriptEngine() { return mathscriptengine; }

    /** Returns the Administration Manager.
     *
     * @return Returns a reference to the current administration manager.
     */
    AdminManager* GetAdminManager() { return adminmanager; }
    /** Returns the character manager.
     *
     * @return Returns a reference to the current character manager for the
     *     server.
     */
    psServerCharManager* GetCharManager() { return charmanager; }

    /** Returns the Progression Manager.
     *
     * @return Returns a reference to the current administration manager.
     */
    ProgressionManager* GetProgressionManager() { return progression; }

    /** Returns the Marriage Manager */
    psMarriageManager* GetMarriageManager() { return marriageManager; }

    /** Returns the Exchange Manager */
    ExchangeManager* GetExchangeManager() { return exchangemanager; }

    /** Returns the Combat Manager.
     *
     * @return Returns a reference to the current combat manager.
     */
    psCombatManager* GetCombatManager() { return combatmanager; }

    /** Returns the spell manager.
     *
     * @return Returns a reference to the current spell manager for the
     *     server.
     */
    psSpellManager* GetSpellManager() { return spellmanager; }

    /** Returns the work manager.
     *
     * @return Returns a reference to the current work manager for the
     *     server.
     */
    psWorkManager* GetWorkManager() { return workmanager; }

    /** Returns the guild manager.
     *
     * @return Returns a reference to the current guild manager for the
     *     server.
     */
    GuildManager* GetGuildManager() { return guildmanager; }

    /** Returns the economy manager.
     *
     * @return Returns a reference to the current economy manager for the
     *     server.
     */
    EconomyManager* GetEconomyManager() { return economymanager; }

    /** Returns the minigame manager
     *
     * @return returns a reference to the current minigame manager for the
     * server.
     */
    psMiniGameManager *GetMiniGameManager() { return minigamemanager; }

    /** Returns the GM Event Manager
     *
     * @return returns a reference to the GM event manager for the
     * server.
     */
    GMEventManager *GetGMEventManager() { return gmeventManager; }

    /** Returns the Bank Manager
     *
     * @return returns a reference to the Bank manager for the server.
     */
    BankManager *GetBankManager() { return bankmanager; }

    /** Returns the Introduction Manager
     *
     * @return returns a reference to the introduction manager for the server.
     */
    IntroductionManager *GetIntroductionManager() { return intromanager; }

    /// Returns a pointer to the psAuthenticationServer
    psAuthenticationServer* GetAuthServer() { return authserver; }

    /** Gets a list of all connected clients.
     *
     * @return Returns a pointer to the list of all the clients.
     */
    ClientConnectionSet* GetConnections();

    /** Returns a random number.
     *
     * @return Returns a random number between 0.0 and 1.0.
     */
    float GetRandom() { return randomGen.Get(); }
    /** Returns a random number with a limit.
     *
     * @return Returns a random number between 0 and limit.
     */
    uint32 GetRandom(uint32 limit) { return randomGen.Get(limit); }

    /**
     * Convenience command to send a client a psSystemMessage.
     */
    void SendSystemInfo(int clientnum, const char *fmt, ... );

    /**
     * Similar to SendSystemInfo(), but the message is shown also on the "Main" tab.
     */
    void SendSystemBaseInfo(int clientnum, const char *fmt, ...);

    /**
     * Convenience command to send a client a psSystemMessage with the MSG_RESULT type
     */
    void SendSystemResult(int clientnum,const char *fmt, ... );

    /**
     * Convenience command to send a client a psSystemMessage with the MSG_OK type
     */
    void SendSystemOK(int clientnum,const char *fmt, ... );

    /**
     * Convenience command to send a client an error psSystemMessage.
     */
    void SendSystemError(int clientnum, const char *fmt, ... );

    LogCSV* GetLogCSV() { return logcsv; }

    /**
     * Used to load the log settings
     */
    void LoadLogSettings();

    /**
     * Used to save the log settings
     */
    void SaveLogSettings();

    /** Adds a buddy to this players list of buddies.
     * Takes the
     * player ID of us and the player ID of the buddy.
     * When you have a buddy, you will be able to see when they
     * come online and leave.
     *
     * @param self: Is the player ID for the current player we are adding buddy to.
     * @param buddy: Is the player ID for the buddy you wish to add.
     * @return Returns either success or failure.
     */
    bool AddBuddy(PID self, PID buddy);

    /** Removes a buddy from this character's list of buddies.
     * Takes the
     * unique player ID of us and the player ID of the buddy.
     * Returns false if buddy diddnt exist.
     *
     * @param self: Is the player ID for the current player we are removing the buddy from.
     * @param buddy: Is the player ID for the buddy you wish to remove.
     * @return Returns either success or failure.
     */
    bool RemoveBuddy(PID self, PID buddy);

    /** Change the dialog responses for that trigger in the area.
     * Given the area, trigger and response number; change the
     * reponse to be the new value. There are seperate tables
     * for the triggers and the responses. If something has
     * been said then it is checked against the triggers. If
     * it matches a trigger then the response ID is retrieved
     * for that trigger. That response ID is looked up and
     * for each ID there are a number of responses (5 to be exact)
     * One of these responses is chosen and that response
     * is used from the trigger.
     *
     * @param area: Is the name of the area associated with the trigger.
     * @param trigger: Is the trigger we want to modify a response for.
     * @param response: Is the new response for the trigger.
     * @param num: Is the response number to change. This can be a value from 1 - 5.
     */
    void UpdateDialog( const char* area, const char* trigger,
                       const char* response, int num );

    /** Changes the pronoun set for that trigger in that area.
     * //@@@Brendon have to find info on these args. What are the pronoun fields for?
     *
     * @param area: Is the name of the area associated with the trigger.
     * @param trigger: Is the trigger we want to modify a response for.
     * @param prohim:
     * @param proher:
     * @param it:
     * @param them:
     */
    void UpdateDialog( const char* area, const char* trigger,
                       const char* prohim, const char* proher,
                       const char* it,     const char* them );

    /** Gets all the triggers from a particular knowledge area.
     *
     * @param data: Is a string containing the name of the data area we want the triggers for.
     * @return Returns an iResultSet containing a list of all the triggers for a particular knowledge area.
     * Result set contains the following members:
     *     - trigger
     */
    iResultSet* GetAllTriggersInArea(csString data);

    /** Return a list of NPCs (with their info) managed by a particular superclient.
     *
     * @param superclientID is the playerID of the superclient
     * @return Returns a iResultSet which contains the set of all npcs managed by the superclient.
     *  <UL>
     *      <LI>id
     *      <LI>name
     *      <LI>master (npc master id)
     *      <LI>x,y,z coords of positions
     *      <LI>rotational angle (y-axis)
     *  </UL>
     */
    iResultSet *GetSuperclientNPCs(int superclientID);

    /** Check for options in table 'server_options'
     * @param option_name: the desired option
     * @param value: Value of the found option will be placed in this string.
     * @return Return false is option not found or if an error happened. True otherwise.
     */
    bool GetServerOption(const char *option_name,csString& value);

    /** Set an option in table 'server_options'
     * @param option_name: the desired option
     * @param value: Value of the found option will be replaced with this string.
     * @return Return false is option not found or if an error happened. True otherwise.
     */
    bool SetServerOption(const char *option_name,const csString& value);

    iResultSet* GetAllResponses( csString& trigger );

    static psCharacterLoader        CharacterLoader;

    NPCManager*                     npcmanager;
    psMarriageManager*              marriageManager;
    psCombatManager*                combatmanager;
    csRandomGen *                   rng;
    QuestManager *                  questmanager;
    psCharCreationManager*          charCreationManager;
    csRef<GuildManager>             guildmanager;
    csRef<QuestionManager>          questionmanager;
    csRef<GroupManager>             groupmanager;
    UserManager*                    usermanager;
    ExchangeManager*                exchangemanager;
    csRef<iVFS>                     vfs;

protected:

    ServerConsole*                  serverconsole;
    CacheManager*                   cachemanager;
    NetManager*                     netmanager;
    EntityManager*                  entitymanager;
    AdminManager*                   adminmanager;
    psDatabase*                     database;
    psServerCharManager*            charmanager;
    SpawnManager*                   spawnmanager;
    csRef<EventManager>             eventmanager;
    WeatherManager*                 weathermanager;
    psSpellManager*                 spellmanager;
    ProgressionManager*             progression;
    psWorkManager*                  workmanager;
    EconomyManager*                 economymanager;
    TutorialManager*                tutorialmanager;
    psMiniGameManager*              minigamemanager;
    IntroductionManager*            intromanager;
    csRandomGen                     randomGen;
    MathScriptEngine*               mathscriptengine;
    iObjectRegistry*                objreg;
    csRef<iConfigManager>           configmanager;
    csRef<ChatManager>              chatmanager;
    csRef<AdviceManager>            advicemanager;
    csRef<ActionManager>            actionmanager;
    csRef<psAuthenticationServer>   authserver;
    LogCSV*                         logcsv;
    bool                            MapLoaded;
    csString                        motd;
    GMEventManager*                 gmeventManager;
    BankManager*                    bankmanager;
};

#endif



