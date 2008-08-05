/*
 * psserver.cpp - author: Matze Braun <matze@braunis.de>
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

#include <psconfig.h>

//=============================================================================
// Crystal Space Includes
//=============================================================================
#include <iutil/vfs.h>
#include <iutil/objreg.h>
#include <iutil/cfgmgr.h>
#include <iutil/cmdline.h>
#include <iutil/object.h>
#include <ivaria/reporter.h>
#include <ivaria/stdrep.h>

//=============================================================================
// Library Include
//=============================================================================
#include "util/serverconsole.h"
#include "util/sleep.h"
#include "util/mathscript.h"
#include "util/psdatabase.h"
#include "util/eventmanager.h"
#include "util/log.h"
#include "util/consoleout.h"

#include "net/msghandler.h"
#include "net/messages.h"

#include "bulkobjects/pscharacterloader.h"
#include "bulkobjects/psitem.h"
#include "bulkobjects/psaccountinfo.h"

//=============================================================================
// Application Includes
//=============================================================================
#include "gem.h"
#include "questionmanager.h"
#include "advicemanager.h"
#include "actionmanager.h"
#include "entitymanager.h"
#include "psserver.h"
#include "chatmanager.h"
#include "netmanager.h"
#include "client.h"
#include "authentserver.h"
#include "guildmanager.h"
#include "groupmanager.h"
#include "playergroup.h"
#include "usermanager.h"
#include "tutorialmanager.h"
#include "psserverchar.h"
#include "spawnmanager.h"
#include "adminmanager.h"
#include "commandmanager.h"
#include "exchangemanager.h"
#include "marriagemanager.h"
#include "combatmanager.h"
#include "spellmanager.h"
#include "weathermanager.h"
#include "npcmanager.h"
#include "serverstatus.h"
#include "progressionmanager.h"
#include "cachemanager.h"
#include "creationmanager.h"
#include "questmanager.h"
#include "economymanager.h"
#include "workmanager.h"
#include "minigamemanager.h"
#include "globals.h"
#include "gmeventmanager.h"
#include "bankmanager.h"
#include "introductionmanager.h"

// Remember to bump this in server_options.sql and add to upgrade_schema.sql!
#define DATABASE_VERSION_STR "1191"


psCharacterLoader psServer::CharacterLoader;

psServer::psServer ()
{
    serverthread        = NULL;
    marriageManager     = NULL;
    entitymanager       = NULL;
    tutorialmanager     = NULL;
    database            = NULL;
    usermanager         = NULL;
    guildmanager        = NULL;
    groupmanager        = NULL;
    charmanager         = NULL;
    eventmanager        = NULL;
    charCreationManager = NULL;
    workmanager         = NULL;
    questionmanager     = NULL;
    advicemanager       = NULL;
    actionmanager       = NULL;
    minigamemanager     = NULL;
	economymanager      = NULL;
    exchangemanager     = NULL;
    spawnmanager        = NULL;
    adminmanager        = NULL;
    combatmanager       = NULL;
    weathermanager      = NULL;
    progression         = NULL;
    npcmanager          = NULL;
    spellmanager        = NULL;
    questmanager        = NULL;
    gmeventManager      = NULL;
    bankmanager         = NULL;
    intromanager        = NULL;
    mathscriptengine    = NULL;
    cachemanager        = NULL;
    logcsv              = NULL;
    vfs                 = NULL;
    
    // Initialize the RNG using current time() as the seed value
    randomGen.Initialize();
}

#define PS_CHECK_REF_COUNT(c) printf("%3d %s\n",c->GetRefCount(),#c)

psServer::~psServer()
{
    // Kick players from server
    if ( serverthread )
    {
        ClientConnectionSet* clients = serverthread->GetConnections();

        Client* p = NULL;
        do
        {       
            // this is needed to not block the RemovePlayer later...
            {
                ClientIterator i(*clients);
                p = i.HasNext() ? i.Next() : NULL;
            }

            if (p)
            {
                psAuthRejectedMessage msgb
                    (p->GetClientNum(),"The server was restarted or shut down.  Please check the website or forums for more news.");

                eventmanager->Broadcast(msgb.msg, NetBase::BC_FINALPACKET);
                RemovePlayer(p->GetClientNum(),"The server was restarted or shut down.  Please check the website or forums for more news.");
            }
        } while (p);
    }
    
    delete economymanager;
    delete tutorialmanager;
    delete charmanager;
    delete entitymanager;
    delete usermanager;
    delete exchangemanager;
    delete serverthread;
    delete database;
    delete marriageManager;
    delete spawnmanager;
    delete adminmanager;
    delete combatmanager;
    delete weathermanager;
    delete charCreationManager;
    delete workmanager;
    delete mathscriptengine;
    delete progression;
    delete npcmanager;
    delete spellmanager;
    delete minigamemanager;
    delete cachemanager;
    delete questmanager;
    delete logcsv;
    delete rng;
    delete gmeventManager;
    delete bankmanager;
    delete intromanager;

    /*
    PS_CHECK_REF_COUNT(guildmanager);
    PS_CHECK_REF_COUNT(questionmanager);
    PS_CHECK_REF_COUNT(groupmanager);
    PS_CHECK_REF_COUNT(authserver);
    PS_CHECK_REF_COUNT(vfs);
    PS_CHECK_REF_COUNT(eventmanager);
    PS_CHECK_REF_COUNT(configmanager);
    PS_CHECK_REF_COUNT(chatmanager);
    PS_CHECK_REF_COUNT(advicemanager);
    PS_CHECK_REF_COUNT(actionmanager);
    */
    guildmanager    = NULL;	 
    questionmanager = NULL;	 
    groupmanager    = NULL;	 
    authserver      = NULL;	 
    chatmanager     = NULL;	 
    advicemanager   = NULL;	 
    actionmanager   = NULL;	 
    minigamemanager = NULL;
}

bool psServer::Initialize(iObjectRegistry* object_reg)
{
    //Map isn't loaded yet
    MapLoaded=false;

    objreg = object_reg;

    // Start logging asap
    csRef<iCommandLineParser> cmdline =
         csQueryRegistry<iCommandLineParser> (objreg);

    if (cmdline)
    {
        const char* ofile = cmdline->GetOption ("output");
        if (ofile != NULL)
        {
            ConsoleOut::SetOutputFile (ofile, false);
        }
        else
        {
            const char* afile = cmdline->GetOption ("append");
            if (afile != NULL)
            {
                ConsoleOut::SetOutputFile (afile, true);
            }
        }
    }

    rng = new csRandomGen();

    
    vfs =  csQueryRegistry<iVFS> (objreg);
    configmanager =  csQueryRegistry<iConfigManager> (object_reg);

    if (!configmanager || !vfs)
    {
        Error1 ("Couldn't find Configmanager!\n");
        return false;
    }

    // Which plugin is loaded here is specified in psserver.cfg file.

    // Apperantly we need to do this for it to work correctly
    configmanager->Load("psserver.cfg");

    // Load the log settings
    LoadLogSettings();
    
    // Initialise the CSV logger
    logcsv = new LogCSV(configmanager, vfs);

    // Start Database

    database = new psDatabase(object_reg);

    csString db_host, db_user, db_pass, db_name;
    unsigned int db_port;

    db_host = configmanager->GetStr("PlaneShift.Database.host", "localhost");
    db_user = configmanager->GetStr("PlaneShift.Database.userid", "planeshift");
    db_pass = configmanager->GetStr("PlaneShift.Database.password", "planeshift");
    db_name = configmanager->GetStr("PlaneShift.Database.name", "planeshift");
    db_port = configmanager->GetInt("PlaneShift.Database.port");

    Debug5(LOG_STARTUP,0,COL_BLUE "Database Host: '%s' User: '%s' Databasename: '%s' Port: %d\n" COL_NORMAL,
      (const char*) db_host, (const char*) db_user, (const char*) db_name, db_port);

    if (!database->Initialize(db_host, db_port, db_user, db_pass, db_name))
    {
        Error2("Could not create database or connect to it: %s",(const char *) database->GetLastError());
        delete database;
        database = NULL;
        return false;
    }

    csString db_version;
    if ( ! GetServerOption("db_version", db_version) )
    {
        CPrintf (CON_ERROR, "Couldn't determine database version.  Error was %s.\n", db->GetLastError() );
        db = NULL;
        return false;
    }

    if ( strcmp( db_version, DATABASE_VERSION_STR ) )
    {
        CPrintf (CON_ERROR, "Database version mismatch: we have '%s' while we are looking for '%s'. Recreate your database using create_all.sql\n", (const char*)db_version, DATABASE_VERSION_STR);
        db = NULL;
        return false;
    }


    Debug1(LOG_STARTUP,0,"Started Database\n");

    
    cachemanager = new CacheManager();
    
    //Loads the standard motd message from db
    Result result(db->Select("SELECT option_value FROM server_options WHERE option_name = 'standard_motd'"));
    if (result.IsValid()  &&  result.Count()>0)
    {
        motd = result[0][0];
    }
    else
    {
        motd.Clear();
    }

    
    // MathScript Engine
    mathscriptengine = new MathScriptEngine();
    
    
    // Initialize DB settings cache
    if (!cachemanager->PreloadAll())
    {
        CPrintf(CON_ERROR, "Could not initialize database cache.\n");
        delete database;
        database = NULL;
        return false;
    }

    Debug1(LOG_STARTUP,0,"Preloaded mesh names, texture names, part names, image names, race info, sector info, traits, item categories, item stats, ways and spells.\n");

    if (!CharacterLoader.Initialize())
    {
        CPrintf(CON_ERROR, "Could not initialize Character Loader.\n");
        cachemanager->UnloadAll();
        delete database;
        database = NULL;
        return false;
    }

    // Start Network Thread

    serverthread=new NetManager;
    if (!serverthread->Initialize(MSGTYPE_PREAUTHENTICATE,MSGTYPE_NPCAUTHENT))
    {
        Error1 ("Network thread initialization failed!\n");
        Error1 ("Is there already a server running?\n");
        delete serverthread;
        serverthread = NULL;
        return false;
    }

    csString serveraddr =
        configmanager->GetStr("PlaneShift.Server.Addr", "0.0.0.0");
    int port =
    configmanager->GetInt("PlaneShift.Server.Port", 1243);
    Debug3(LOG_STARTUP,0,COL_BLUE "Listening on '%s' Port %d.\n" COL_NORMAL,
            (const char*) serveraddr, port);
    if (!serverthread->Bind (serveraddr, port))
    {
        delete serverthread;
        serverthread = NULL;
        return false;
    }
    Debug1(LOG_STARTUP,0,"Started Network Thread\n");


    // Start Event Manager

    // Create the MAIN GAME thread. The eventhandler handles
    // both messages and events. For backward compablility
    // we still store two points. But they are one object
    // and one thread.
    eventmanager = csPtr<EventManager>( new EventManager );

    // This gives access to msghandler to all message types
    psMessageCracker::msghandler = eventmanager;

    if (!eventmanager->Initialize(serverthread, 1000))
        return false;                // Attach to incoming messages.
    if (!eventmanager->StartThread() )
        return false;

    Debug1(LOG_STARTUP,0,"Started Event Manager Thread\n");


    usermanager = new UserManager(serverthread->GetConnections() );
    Debug1(LOG_STARTUP,0,"Started User Manager\n");

    // Load emotes
    if(!usermanager->LoadEmotes("/planeshift/data/emotes.xml", vfs))
    {
        CPrintf(CON_ERROR, "Could not load emotes from emotes.xml\n");
        return false;
    }

    entitymanager = new EntityManager;
    if (!entitymanager->Initialize(object_reg,
                                   serverthread->GetConnections(),
                                   usermanager))
    {
        Error1("Failed to initialise CEL!\n");
        delete entitymanager;
        entitymanager = NULL;
        return false;
    }
    entitymanager->SetReady(false);
    serverthread->SetEngine(entitymanager->GetEngine());
    Debug1(LOG_STARTUP,0,"Started CEL\n");

    // Start Combat Manager
    combatmanager = new psCombatManager();
    if (!combatmanager->InitializePVP())
    {
        return false;
    }
    Debug1(LOG_STARTUP,0,"Started Combat Manager\n");

    // Start Spell Manager
    spellmanager = new psSpellManager(serverthread->GetConnections(), object_reg);
    Debug1(LOG_STARTUP,0,"Started Spell Manager\n");

    // Start Weather Manager
    weathermanager = new WeatherManager();
    weathermanager->Initialize();
    Debug1(LOG_STARTUP,0,"Started Weather Manager\n");

    marriageManager = new psMarriageManager();

    questmanager = new QuestManager;

    if (!questmanager->Initialize())
        return false;

    chatmanager = csPtr<ChatManager> (new ChatManager);
    Debug1(LOG_STARTUP,0,"Started Chat Manager\n");

    guildmanager = csPtr<GuildManager>(new GuildManager(serverthread->GetConnections(),
                                                        chatmanager));
    Debug1(LOG_STARTUP,0,"Started Guild Manager\n");

    questionmanager = csPtr<QuestionManager>(new QuestionManager() );
    Debug1(LOG_STARTUP,0,"Started Question Manager\n");

    advicemanager = csPtr<AdviceManager>(new AdviceManager( database ) );
    Debug1(LOG_STARTUP,0,"Started Advice Manager\n");

    groupmanager = csPtr<GroupManager>
    (new GroupManager(serverthread->GetConnections(),chatmanager));
    Debug1(LOG_STARTUP,0,"Started Group Manager\n");

    charmanager = new psServerCharManager();
    if ( !charmanager->Initialize(serverthread->GetConnections()) )
        return false;
    Debug1(LOG_STARTUP,0,"Started Character Manager\n");

    spawnmanager = new SpawnManager(database);
    Debug1(LOG_STARTUP,0,"Started NPC Spawn Manager\n");

    adminmanager = new AdminManager;
    Debug1(LOG_STARTUP,0,"Started Admin Manager\n");

    tutorialmanager = new TutorialManager(GetConnections());

    actionmanager = csPtr<ActionManager>(new ActionManager( database));
    Debug1(LOG_STARTUP,0,"Started Action Manager\n");

    authserver = csPtr<psAuthenticationServer>
    (new psAuthenticationServer(serverthread->GetConnections(),
                                usermanager,
                                guildmanager));
    Debug1(LOG_STARTUP,0,"Started Authentication Server\n");

    exchangemanager = new ExchangeManager(serverthread->GetConnections());
    Debug1(LOG_STARTUP,0,"Started Exchange Manager\n");

    npcmanager = new NPCManager(serverthread->GetConnections(),
                                database,
                                eventmanager);
    if ( !npcmanager->Initialize())
    {
        Error1("Failed to start npc manager!");
        return false;

    }
    Debug1(LOG_STARTUP,0,"Started NPC Superclient Manager\n");

    progression = new ProgressionManager(serverthread->GetConnections());
    if ( !progression->Initialize())
    {
        Error1("Failed to start progression manager!");
        return false;
    }
    
    Debug1(LOG_STARTUP,0,"Started Progression Manager\n");

    // Start work manager
    workmanager = new psWorkManager();
    Debug1(LOG_STARTUP,0,"Started Work Manager\n");

    // Start economy manager
    economymanager = new EconomyManager();
    Debug1(LOG_STARTUP,0,"Started Economy Manager\n");
    // Start droping
    economymanager->ScheduleDrop(1 * 60 * 60 * 1000,true); // 1 hour

    // Start minigame manager
    minigamemanager = new psMiniGameManager();
    Debug1(LOG_STARTUP, 0, "Started Minigame Manager\n");

    charCreationManager = new psCharCreationManager();
    if ( !charCreationManager->Initialize() )
    {
        Error1("Failed to load character creation data");
        return false;
    }
    Debug1(LOG_STARTUP,0, "Started Character Creation Manager");

    gmeventManager = new GMEventManager();
    if (!gmeventManager->Initialise())
    {
        Error1("Failed to load GM Events Manager");
        return false;
    }

    // Init Bank Manager.
    bankmanager = new BankManager();

    intromanager = new IntroductionManager();
    Debug1(LOG_STARTUP,0, "Started Introduction Manager");

    if (!ServerStatus::Initialize (object_reg))
    {
        CPrintf (CON_WARNING, "Warning: Couldn't initialize server status reporter.\n");
    }
    else
    {
        Debug1(LOG_STARTUP,0,"Server status reporter initialized.\n");
    }

    weathermanager->StartGameTime();
    return true;    
}

void psServer::MainLoop ()
{
    // Eventually load an autoexec file
    csRef<iCommandLineParser> cmdline =
         csQueryRegistry<iCommandLineParser> (objreg);

    if (cmdline)
    {
        const char* autofile;
        for (int i = 0; (autofile = cmdline->GetOption("run", i)); i++)
        {
            char toexec[1000];
            strcpy(toexec, "exec ");
            strcat(toexec, autofile);
            ServerConsole::ExecuteScript(toexec);
        }
    }

    csString status("Server initialized");
    logcsv->Write(CSV_STATUS, status);

    ServerConsole::MainLoop ();

    status = "Server shutdown";
    logcsv->Write(CSV_STATUS, status);

    // Save log settings
    SaveLogSettings();

    // Shut things down
    eventmanager->StopThread();
}

void psServer::RemovePlayer (uint32_t clientnum,const char *reason)
{
    Client* client = serverthread->GetConnections()->FindAny(clientnum);
    if (!client)
    {
        CPrintf (CON_WARNING, "Tried to remove non-existent client: %d\n", clientnum);
        return;
    }

    char ipaddr[20];
    client->GetIPAddress(ipaddr);

    csString status;
    status.Format("%s, %u, Client (%s) removed", ipaddr, client->GetClientNum(), client->GetName());

    psserver->GetLogCSV()->Write(CSV_AUTHENT, status);

    Notify3(LOG_CHARACTER, "Remove player '%s' (%d)\n", client->GetName(),client->GetClientNum() );

    client->Disconnect();

    authserver->SendDisconnect(client,reason);

    entitymanager->DeletePlayer(client);

    if (client->IsSuperClient())
    {
        npcmanager->Disconnect(client);
    }

    serverthread->GetConnections()->MarkDelete(client);
}

void psServer::MutePlayer (uint32_t clientnum,const char *reason)
{
    Client* client = serverthread->GetConnections()->Find(clientnum);
    if (!client)
    {
        CPrintf (CON_WARNING, "Tried to mute non-existent client: %d\n", clientnum);
        return;
    }

    CPrintf (CON_DEBUG, "Mute player '%s' (%d)\n", client->GetName(),
        client->GetClientNum() );

    client->SetMute(true);

    psserver->SendSystemInfo(client->GetClientNum(),reason);
}

void psServer::UnmutePlayer (uint32_t clientnum,const char *reason)
{
    Client* client = serverthread->GetConnections()->Find(clientnum);
    if (!client)
    {
        CPrintf (CON_WARNING, "Tried to unmute non-existent client: %d\n", clientnum);
        return;
    }

    CPrintf (CON_DEBUG, "Unmute player '%s' (%d)\n", client->GetName(),
        client->GetClientNum() );

    client->SetMute(false);
    psserver->SendSystemInfo(client->GetClientNum(),reason);
}

bool psServer::LoadMap(char* mapname)
{
    if (entitymanager->LoadMap(mapname))
    {
        MapLoaded=true;
    }
    return MapLoaded;
}

bool psServer::IsReady()
{
    if (!entitymanager)
        return false;

    return entitymanager->IsReady();
}


bool psServer::HasBeenReady()
{
    if (!entitymanager)
        return false;

    return entitymanager->HasBeenReady();
}

bool psServer::IsFull(size_t numclients, Client * client)
{
    unsigned int maxclients = GetConfig()->GetInt("PlaneShift.Server.User.connectionlimit", 20);

    if (client)
    {
        return numclients > maxclients &&
               !CacheManager::GetSingleton().GetCommandManager()->Validate(client->GetSecurityLevel(), "always login");
    }
    else
    {
        return numclients > maxclients;
    }
}


void psServer::SendSystemInfo(int clientnum, const char *fmt, ... )
{
    if ( clientnum == 0 || fmt == NULL )
        return;

    va_list args;
    va_start(args, fmt);
    psSystemMessage newmsg(clientnum ,MSG_INFO, fmt, args);
    va_end(args);

    if (newmsg.valid)
        eventmanager->SendMessage(newmsg.msg);
    else
    {
        Bug2("Could not create valid psSystemMessage for client %u.\n",clientnum);
    }
}

void psServer::SendSystemBaseInfo(int clientnum, const char *fmt, ...)
{
    if ( clientnum == 0 || fmt == NULL )
        return;

    va_list args;
    va_start(args, fmt);
    psSystemMessage newmsg(clientnum ,MSG_INFO_BASE, fmt, args);
    va_end(args);

    if (newmsg.valid)
        eventmanager->SendMessage(newmsg.msg);
    else
    {
        Bug2("Could not create valid psSystemMessage for client %u.\n",clientnum);
    }
}

void psServer::SendSystemResult(int clientnum, const char *fmt, ... )
{
    if ( clientnum == 0 || fmt == NULL )
        return;

    va_list args;
    va_start(args, fmt);
    psSystemMessage newmsg(clientnum ,MSG_RESULT, fmt, args);
    va_end(args);

    if (newmsg.valid)
        eventmanager->SendMessage(newmsg.msg);
    else
    {
        Bug2("Could not create valid psSystemMessage for client %u.\n",clientnum);
    }
}

void psServer::SendSystemOK(int clientnum, const char *fmt, ... )
{
    if ( clientnum == 0 || fmt == NULL )
        return;

    va_list args;
    va_start(args, fmt);
    psSystemMessage newmsg(clientnum ,MSG_OK, fmt, args);
    va_end(args);

    if (newmsg.valid)
        eventmanager->SendMessage(newmsg.msg);
    else
    {
        Bug2("Could not create valid psSystemMessage for client %u.\n",clientnum);
    }
}

void psServer::SendSystemError(int clientnum, const char *fmt, ... )
{
    if ( clientnum == 0 || fmt == NULL )
        return;

    va_list args;
    va_start(args, fmt);
    psSystemMessage newmsg(clientnum ,MSG_ERROR, fmt, args);
    va_end(args);

    if (newmsg.valid)
        eventmanager->SendMessage(newmsg.msg);
    else
    {
        Bug2("Could not create valid psSystemMessage for client %u.\n",clientnum);
    }
}

void psServer::LoadLogSettings()
{
    int count=0;
    for (int i=0; i< MAX_FLAGS; i++)
    {
        if (pslog::GetName(i))
        {
            pslog::SetFlag(pslog::GetName(i),configmanager->GetBool(pslog::GetSettingName(i)),0);
            if(configmanager->GetBool(pslog::GetSettingName(i)))
                count++;
        }
    }
    if (count==0)
    {
        CPrintf(CON_CMDOUTPUT,"All LOGS are off.\n");
    }

    csString debugFile =  configmanager->GetStr("PlaneShift.DebugFile");
    if ( debugFile.Length() > 0 )
    {
        csRef<iStandardReporterListener> reporter =  csQueryRegistry<iStandardReporterListener > ( objreg);


        reporter->SetMessageDestination (CS_REPORTER_SEVERITY_DEBUG, true, false ,false, false, true, false);
        reporter->SetMessageDestination (CS_REPORTER_SEVERITY_ERROR, true, false ,false, false, true, false);
        reporter->SetMessageDestination (CS_REPORTER_SEVERITY_BUG, true, false ,false, false, true, false);

        time_t curr=time(0);
        tm* gmtm = gmtime(&curr);

        csString timeStr;
        timeStr.Format("-%d-%02d-%02d-%02d:%02d:%02d",
            gmtm->tm_year+1900,
            gmtm->tm_mon+1,
            gmtm->tm_mday,
            gmtm->tm_hour,
            gmtm->tm_min,
            gmtm->tm_sec);

        debugFile.Append( timeStr );
        reporter->SetDebugFile( debugFile, true );

        Debug2(LOG_STARTUP,0,"PlaneShift Server Log Opened............. %s", timeStr.GetData());

    }
}

void psServer::SaveLogSettings()
{
    for (int i=0; i< MAX_FLAGS; i++)
    {
        if (pslog::GetName(i))
        {
            configmanager->SetBool(pslog::GetSettingName(i),pslog::GetValue(pslog::GetName(i)));
        }
    }

    configmanager->Save();
}

ClientConnectionSet* psServer::GetConnections()
{
    return serverthread->GetConnections();
}

/*-----------------Buddy List Management Functions-------------------------*/


bool psServer::IsBuddy(int self,int buddy)
{
    Result result(db->Select("SELECT player_id"
                                "  from buddy_list"
                                " where player_buddy=%d "
                                "   and player_id=%d",buddy,self));
    if (!result.IsValid())
    {
        database->SetLastError(database->GetLastSQLError());
        return false;
    }

    if (result.Count() < 1)
    {
        database->SetLastError("No buddy found");
        return false;
    }

    return true;
}

bool psServer::AddBuddy(int self,int buddy)
{
    //int rows=db->Command("insert into buddy_list values (%d, %d)",
    int rows = db->Command( "insert into character_relationships ( character_id, related_id, relationship_type ) values ( %d, %d, 'buddy' )",
                            self, buddy);

    if (rows != 1)
    {
        database->SetLastError(database->GetLastSQLError());
        return false;
    }

    return true;
}

bool psServer::RemoveBuddy(int self,int buddy)
{
    //int rows=db->Command("delete"
    //                        "  from buddy_list"
    //                        " where player_id=%d"
    //                        "   and player_buddy=%d",
    int rows=db->Command("delete from character_relationships where character_id=%d and related_id=%d and relationship_type='buddy'",
    self, buddy);

    if (rows != 1)
    {
        database->SetLastError(database->GetLastSQLError());
        return false;
    }

    return true;
}

void psServer::UpdateDialog( const char* area, const char* trigger,
                               const char* response, int num )
{
    csString escTrigger;
    csString escArea;
    csString escResp;

    db->Escape( escTrigger, trigger );
    db->Escape( escArea, area );
    db->Escape( escResp, response );

    // Find the response id:
    int id = db->SelectSingleNumber("SELECT response_id FROM npc_triggers "
                                       "WHERE trigger=\"%s\" AND area=\"%s\"",
                                       escTrigger.GetData(), escArea.GetData());


    db->Command(  "UPDATE npc_responses SET response%d=\"%s\" WHERE id=%d",
            num, escResp.GetData(), id );
}



void psServer::UpdateDialog( const char* area, const char* trigger,
                   const char* prohim, const char* proher,
                   const char* proit,     const char* prothem )
{
    csString escTrigger;
    csString escArea;
    csString escHim,escHer,escIt,escThem;

    db->Escape( escTrigger, trigger );
    db->Escape( escArea, area );
    db->Escape( escHim, prohim );
    db->Escape( escHer, proher );
    db->Escape( escIt, proit );
    db->Escape( escThem, prothem );


    // Find the response id:
    int id = db->SelectSingleNumber("SELECT response_id FROM npc_triggers "
                                       "WHERE trigger=\"%s\" AND area=\"%s\"",
                                       escTrigger.GetData(), escArea.GetData());


    db->Command(     "UPDATE npc_responses SET "
                     "pronoun_him=\"%s\", "
                     "pronoun_her=\"%s\", "
                     "pronoun_it=\"%s\", "
                     "pronoun_them=\"%s\" "
                     "WHERE id=%d",
                     escHim.GetData(), escHer.GetData(),
                     escIt.GetData(), escThem.GetData(), id);
}

iResultSet* psServer::GetAllTriggersInArea(csString data)
{
    iResultSet* rs;

    csString escape;
    db->Escape( escape, data );
    rs = db->Select("SELECT trigger FROM npc_triggers "
                       "WHERE area='%s'", escape.GetData());
    if ( !rs )
    {
        Error2("db ERROR: %s", db->GetLastError());
        Error2("LAST QUERY: %s", db->GetLastQuery());
        return NULL;
    }

    return rs;
}

iResultSet* psServer::GetAllResponses( csString& trigger )
{
    iResultSet* rs;
    csString escTrigger;
    db->Escape( escTrigger, trigger );


    // Get the response ID:
    int id = db->SelectSingleNumber("SELECT response_id FROM npc_triggers "
                                       "WHERE trigger=\"%s\"",
                                       escTrigger.GetData());

    if ( !id )
    {
        Error2("db ERROR: %s", db->GetLastError());
        Error2("LAST QUERY: %s", db->GetLastQuery());
        return NULL;
    }


    rs = db->Select("SELECT * FROM npc_responses "
                       "WHERE id=%d",id);
    if ( !rs )
    {
        Error2("db ERROR: %s", db->GetLastError());
        Error2("LAST QUERY: %s", db->GetLastQuery());
        return NULL;
    }

    return rs;
}


iResultSet *psServer::GetSuperclientNPCs(int superclientID)
{
    iResultSet *rs;

    rs = db->Select("SELECT id"
                    "  FROM characters"
                    " WHERE account_id='%d'",
                    superclientID);
    if (!rs)
    {
        database->SetLastError(database->GetLastSQLError());
    }

    return rs;
}


bool psServer::GetServerOption(const char *option_name,csString& value)
{
    csString escape;
    db->Escape( escape, option_name );
    Result result (db->Select("select option_value from server_options where "
                    "option_name='%s'", escape.GetData()));

    if (!result.IsValid())
    {
        csString temp;
        temp.Format("Couldn't execute query.\nCommand was "
                    "<%s>.\nError returned was <%s>\n",
                    db->GetLastQuery(),db->GetLastError());
        database->SetLastError(temp);
        return false;
    }

    if (result.Count() == 1)
    {
        value = result[0]["option_value"];
        return true;
    }

    return false;
}

bool psServer::SetServerOption(const char *option_name,const csString& value)
{
    csString escape, dummy;
    bool bExists = GetServerOption(option_name, dummy);
    unsigned long result;

    db->Escape( escape, option_name );

    if (bExists)
        result = db->Command("update server_options set option_value='%s' where option_name='%s'", value.GetData(), option_name);
    else
        result = db->Command("insert into server_options(option_name, option_value) values('%s','%s')", option_name, value.GetData());

    return result==1;
}



