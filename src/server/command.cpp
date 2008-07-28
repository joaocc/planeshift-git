/*
 * command.cpp
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
 * The commands, the user can type in in the console are defined here
 * if you write a new one, don't forget to add it to the list at the end of
 * the file.
 *
 * Author: Matthias Braun <MatzeBraun@gmx.de>
 */

#include <psconfig.h>

#include <stdio.h>
#include <string.h>
#include <memory.h>
#include <stdlib.h>
#include <ctype.h>

#include <iutil/object.h>
#include <iutil/objreg.h>
#include <iutil/cfgmgr.h>
#include <csutil/csstring.h>
#include <csutil/csmd5.h>
#include <iutil/stringarray.h>
#include <iengine/collection.h>
#include <iengine/engine.h>

#include "util/command.h"
#include "util/serverconsole.h"
#include "net/messages.h"
#include "globals.h"
#include "psserver.h"
#include "cachemanager.h"
#include "playergroup.h"
#include "netmanager.h"
#include "util/strutil.h"
#include "gem.h"
#include "invitemanager.h"
#include "entitymanager.h"
#include "util/psdatabase.h"
#include "spawnmanager.h"
#include "actionmanager.h"
#include "psproxlist.h"
#include "adminmanager.h"
#include "groupmanager.h"
#include "progressionmanager.h"
#include "combatmanager.h"
#include "weathermanager.h"
#include "rpgrules/factions.h"
#include "util/dbprofile.h"
#include "economymanager.h"
#include "questmanager.h"
#include "engine/psworld.h"
#include "bulkobjects/dictionary.h"
#include "bulkobjects/psnpcdialog.h"

#include "bulkobjects/pscharacterloader.h"
#include "bulkobjects/psnpcloader.h"
#include "bulkobjects/psaccountinfo.h"
#include "bulkobjects/pstrainerinfo.h"
#include "bulkobjects/psitem.h"
#include "bulkobjects/pssectorinfo.h"

int com_lock (char *)
{
    EntityManager::GetSingleton().SetReady(false);
    return 0;
}

int com_ready (char *)
{
    if (psserver->IsMapLoaded())
    {
        EntityManager::GetSingleton().SetReady(true);
        CPrintf (CON_CMDOUTPUT, "Server is now ready\n");
    }
    else
    {
        CPrintf (CON_CMDOUTPUT, "Failed to switch server to ready! No map loaded\n");
    }

    return 0;
}

class psQuitEvent : public psGameEvent
{
public:
    psQuitEvent(csTicks msecDelay, psQuitEvent *quit_event, csString message, 
                       bool server_lock, bool server_shutdown)
        : psGameEvent(0,msecDelay,"psDelayedQuitEvent")
    {
       message_quit_event = quit_event;
       mytext = message;
       trigger_server_lock = server_lock;
       trigger_server_shutdown = server_shutdown;
    }
    virtual void Trigger()
    {
        psSystemMessage newmsg(0, MSG_INFO_SERVER, mytext);
        psserver->GetEventManager()->Broadcast(newmsg.msg);
        CPrintf(CON_CMDOUTPUT, "%s\n", (const char*) mytext);
        if(trigger_server_lock) //this is triggering the server lock
            com_lock(NULL);
        if(trigger_server_shutdown) //this is triggering the server shut down
            ServerConsole::Stop();
    }
    virtual bool CheckTrigger()
    {   //if this is the event triggering the server shut down pass it's validity, else check that event if
        //it's still valid
        return message_quit_event == NULL? valid : message_quit_event->CheckTrigger();
    }
    void Invalidate()
    {
        valid = false; //this is used to inavlidate the server shut down event
    }
private:
    csString mytext; //keeps the message which will be sent to clients
    bool trigger_server_lock; //if true this is the event locking the server
    bool trigger_server_shutdown; //if true this is the event which will shut down the server
    psQuitEvent *message_quit_event; //stores a link to the master event which will shut down the server
};

psQuitEvent *server_quit_event = NULL; //used to keep track of the shut down event

/* shut down the server and exit program */
int com_quit(char *arg)
{
    if(strncasecmp(arg,"stop", 4) == 0) //if the user passed 'stop' we will abort the shut down process
    {
        if(server_quit_event != NULL) //there is a quit event let's make it invalid
                server_quit_event->Invalidate();
        com_ready(NULL); //remake the server available if it was locked in the process
        server_quit_event = NULL;  //we don't need it anymore so let's clean ourselves of it
        //Let the user know about the new situation about the server
        csString abort_msg = "Server Admin: The server is no longer restarting."; 
        psSystemMessage newmsg(0, MSG_INFO_SERVER, abort_msg);
        psserver->GetEventManager()->Broadcast(newmsg.msg);
        CPrintf(CON_CMDOUTPUT, "%s\n", (const char*) abort_msg);
    }
    else
    {
        if(server_quit_event == NULL) //check that there isn't a quit event already running...
        {
            uint quit_delay = atoi(arg); //if the user passed a number we will read it
            if (quit_delay) //we have an argument > 0 so let's put an event for server shut down
            {
                if(quit_delay < 5) com_lock(NULL); //we got less than 5 minutes for shut down so let's lock the server
                                                   //immediately
                                                   
                //generates the messages to alert the user and allocates them in the queque
                for(uint i = 3; i > 0; i--) //i = 3 sets up the 0 seconds message and so is the event triggering
                {                           //shutdown
                    csString outtext = "Server Admin: The server will shut down in ";
                             outtext += 60-(i*20);
                             outtext += " seconds.";
                    psQuitEvent *Quit_event = new psQuitEvent(((quit_delay-1)*60+i*20)*1000, server_quit_event, 
                                                                outtext, false, i == 3 ? true : false);
                    psserver->GetEventManager()->Push(Quit_event); 
                    if(!server_quit_event) server_quit_event = Quit_event;
                }
                csString outtext = "Server Admin: The server will shut down in 1 minute.";
                psQuitEvent *Quit_event = new psQuitEvent((quit_delay-1)*60*1000, server_quit_event, outtext,
                                                          false, false);
                psserver->GetEventManager()->Push(Quit_event); 
                
                if(quit_delay == 1) return 0; //if the time we had was 1 minute no reason to go on
                uint quit_time = (quit_delay < 5)? quit_delay : 5; //manage the period 1<x<5 minutes
                while(1)
                {
                    csString outtext = "Server Admin: The server will shut down in ";
                             outtext += quit_time;
                             outtext += " minutes.";
                    psQuitEvent *Quit_event = new psQuitEvent((quit_delay-quit_time)*60*1000, server_quit_event, 
                                                                outtext, quit_time == 5 ? true : false, false);
                    psserver->GetEventManager()->Push(Quit_event); 
                    if(quit_time == quit_delay) { break; } //we have got to the first message saying the server
                                                           //will be shut down let's go out of the loop
                    else if(quit_time+5 > quit_delay) { quit_time = quit_delay; } //we have reached the second message
                                                                                  //saying the server will shut down
                                                                                  //so manage the case of not multiple
                                                                                  //of 5 minutes shut down times
                    else { quit_time +=5; } //we have still a long way so let's go to the next 5 minutes message
                }
            }
            else //we have no arguments or the argument passed is zero let's quit immediately
            {
                 ServerConsole::Stop();
            }
        }
        else //we have found a quit event so we will inform the user about that
        {
            uint planned_shutdown = (server_quit_event->triggerticks-csGetTicks())/1000; //gets the seconds
                                                                                         //to the event
            uint minutes = planned_shutdown/60; //get the minutes to the event
            uint seconds = planned_shutdown%60; //get the seconds to the event when the minutes are subtracted
            csString quitInfo = "Server already shutting down in ";
            if(minutes) //if we don't have minutes (so they are zero) skip them
                quitInfo += minutes; quitInfo += "minutes ";
            if(seconds) //if we don't have seconds (so they are zero) skip them
                quitInfo += seconds; quitInfo += "seconds";
            CPrintf(CON_CMDOUTPUT, "%s\n", (const char*) quitInfo); //send the message to the server console
        }
    }
    return 0;
}

/* Print out help for ARG, or for all of the commands if ARG is
not present. */
int com_help (char *arg)
{
    register int i;
    int printed = 0;

    CPrintf(CON_CMDOUTPUT ,"\n");
    for (i = 0; commands[i].name; i++)
    {
        if (!*arg || (strcmp (arg, commands[i].name) == 0))
        {
            if(strlen(commands[i].name) < 8)
                CPrintf (CON_CMDOUTPUT ,"%s\t\t%s.\n", commands[i].name, commands[i].doc);
            else
                CPrintf (CON_CMDOUTPUT ,"%s\t%s.\n", commands[i].name, commands[i].doc);
            printed++;
        }
    }

    if (!printed)
    {
        CPrintf (CON_CMDOUTPUT ,"No commands match `%s'.  Possibilities are:\n", arg);

        for (i = 0; commands[i].name; i++)
        {
            /* Print in six columns. */
            if (printed == 6)
            {
                printed = 0;
                CPrintf (CON_CMDOUTPUT ,"\n");
            }

            CPrintf (CON_CMDOUTPUT ,"%s\t", commands[i].name);
            printed++;
        }

        if (printed)
            CPrintf(CON_CMDOUTPUT ,"\n");
    }

    CPrintf(CON_CMDOUTPUT ,"\n");

    return (0);
}

static inline csString PS_GetClientStatus(Client* client)
{
    csString status;
    if (client->IsReady())
    {
        if (client->GetConnection()->heartbeat == 0)
        {
            status = "ok";
        }
        else
        {
            status.Format ("connection problem(%d)",
                client->GetConnection()->heartbeat);
        }
    }
    else
    {
        if (client->GetActor())
        {
            status = "loading";
        }
        else
        {
            status = "connecting";
        }
    }

    return status;
}

int com_settime( char* arg )
{
    if (!arg || strlen (arg) == 0)
    {
        CPrintf(CON_CMDOUTPUT,"Please provide a time to use\n");
        return 0;
    }
   
    int hour = atoi( arg );
    int minute = 0;

    if ( hour > 23 || hour < 0 )
    {
        CPrintf( CON_CMDOUTPUT, "Select a time between 0-23\n");
        return 0;
    }
    
    psserver->GetWeatherManager()->SetGameTime(hour,minute);
    CPrintf (CON_CMDOUTPUT, "Current Game Hour set to: %d:%02d\n", hour,minute);
    
    return 0;
}

int com_showtime(char *)
{
    CPrintf(CON_CMDOUTPUT,"Game time is %d:%02d %d-%d-%d\n",
            psserver->GetWeatherManager()->GetGameTODHour(),
            psserver->GetWeatherManager()->GetGameTODMinute(),
            psserver->GetWeatherManager()->GetGameTODYear(),
            psserver->GetWeatherManager()->GetGameTODMonth(),
            psserver->GetWeatherManager()->GetGameTODDay());
    return 0;
}


int com_setmaxout(char* arg)
{
    if (!arg || strlen (arg) == 0)
    {
        CPrintf (CON_CMDOUTPUT, "Use one of the following values to control output on standard output:\n");
        CPrintf (CON_CMDOUTPUT, "  0: no output at all\n");
        CPrintf (CON_CMDOUTPUT, "  1: only output of server commands\n");
        CPrintf (CON_CMDOUTPUT, "  2: 1 + errors\n");
        CPrintf (CON_CMDOUTPUT, "  3: 2 + warnings\n");
        CPrintf (CON_CMDOUTPUT, "  4: 3 + notifications\n");
        CPrintf (CON_CMDOUTPUT, "  5: 4 + debug messages\n");
        CPrintf (CON_CMDOUTPUT, "  6: 5 + spam\n");
        return 0;
    }
    int msg = CON_SPAM;;
    sscanf (arg, "%d", &msg);
    ConsoleOut::SetMaximumOutputClassStdout ((ConsoleOutMsgClass)msg);
    return 0;
}

int com_setmaxfile(char* arg)
{
    if (!arg || strlen (arg) == 0)
    {
        CPrintf (CON_CMDOUTPUT, "Use one of the following values to control output on output file:\n");
        CPrintf (CON_CMDOUTPUT, "  0: no output at all\n");
        CPrintf (CON_CMDOUTPUT, "  1: only output of server commands\n");
        CPrintf (CON_CMDOUTPUT, "  2: 1 + errors\n");
        CPrintf (CON_CMDOUTPUT, "  3: 2 + warnings\n");
        CPrintf (CON_CMDOUTPUT, "  4: 3 + notifications\n");
        CPrintf (CON_CMDOUTPUT, "  5: 4 + debug messages\n");
        CPrintf (CON_CMDOUTPUT, "  6: 5 + spam\n");
        return 0;
    }
    int msg = CON_SPAM;;
    sscanf (arg, "%d", &msg);
    ConsoleOut::SetMaximumOutputClassFile ((ConsoleOutMsgClass)msg);
    return 0;
}

int com_netprofile(char *)
{
    psNetMsgProfiles * profs = psserver->GetNetManager()->GetProfs();
    csString dumpstr = profs->Dump ();
    csRef<iFile> file = psserver->vfs->Open("/this/netprofile.txt",VFS_FILE_WRITE);
    file->Write(dumpstr, dumpstr.Length());
    CPrintf (CON_CMDOUTPUT, "Net profile dumped to netprofile.txt\n");
    profs->Reset();
    return 0;
}

int com_dbprofile(char *)
{
    csString dumpstr = db->DumpProfile();
    csRef<iFile> file = psserver->vfs->Open("/this/dbprofile.txt",VFS_FILE_WRITE);
    file->Write(dumpstr, dumpstr.Length());
    CPrintf (CON_CMDOUTPUT, "DB profile dumped to dbprofile.txt\n");
    db->ResetProfile();
    return 0;
}

/* print out server status */
int com_status(char *)
{
    bool ready = psserver->IsReady();
    bool hasBeenReady = psserver->HasBeenReady();

    CPrintf (CON_CMDOUTPUT ,"Server           : " COL_CYAN "%s\n" COL_NORMAL,
        (ready ^ hasBeenReady) ? "is shutting down" : "is running.");
    CPrintf (CON_CMDOUTPUT ,"World            : " COL_CYAN "%s\n" COL_NORMAL,
        hasBeenReady ? "loaded and running." : "not loaded.");
    CPrintf (CON_CMDOUTPUT ,"Connection Count : " COL_CYAN "%d\n" COL_NORMAL,
        psserver->GetNetManager()->GetConnections()->Count());
    CPrintf (CON_CMDOUTPUT ,COL_GREEN "%-5s %-5s %-25s %14s %10s %9s\n" COL_NORMAL,"EID","PID","Name","CNum","Ready","Time con.");

    ClientConnectionSet* clients = psserver->GetNetManager()->GetConnections();
    if (psserver->GetNetManager()->GetConnections()->Count() == 0)
    {
        CPrintf (CON_CMDOUTPUT ,"  *** List Empty ***\n");
        return 0;
    }

    ClientIterator i(*clients);
    while(i.HasNext())
    {
        Client *client = i.Next();
        int eid = 0;
        if (client->GetActor())
        {
            eid = client->GetActor()->GetEntityID();
        }
        CPrintf (CON_CMDOUTPUT ,"%5d %5d %-25s %14d %10s",
            eid,
            client->GetPlayerID(),
            client->GetName(),
            client->GetClientNum(),
            (const char*) PS_GetClientStatus(client));
        if (client->GetCharacterData())
        {
            psCharacter * character = client->GetCharacterData();
            unsigned int time = character->GetOnlineTimeThisSession();
            unsigned int hour,min,sec;
            sec = time%60;
            time = time/60;
            min = time%60;
            hour = time/60;
            CPrintf(CON_CMDOUTPUT ," %3u:%02u:%02u",hour,min,sec);
        }
        CPrintf (CON_CMDOUTPUT ,"\n");
    }

    return 0;
}

int com_say(char* text)
{
    csString outtext = "Server Admin: ";
    outtext += text;

    psSystemMessage newmsg(0, MSG_INFO_SERVER, outtext);
    psserver->GetEventManager()->Broadcast(newmsg.msg);
    CPrintf(CON_CMDOUTPUT, "%s\n", (const char*) outtext);

    return 0;
}

int com_kick(char* player)
{
    int playernum = atoi(player);
    if (playernum<=0)
    {
        CPrintf (CON_CMDOUTPUT ,"Please Specify the number of the client!\n");
        CPrintf (CON_CMDOUTPUT ,"  (you can use 'status' to see a list of clients and their"
            "numbers.)\n");
        return 0;
    }

    uint32_t cnum = (uint32_t) playernum;
    Client* client = psserver->GetNetManager()->GetConnections()->Find(cnum);
    if (!client)
    {
        CPrintf (CON_CMDOUTPUT ,COL_RED "Client with number %u not found!\n" COL_NORMAL,
            cnum);
        return 0;
    }

    psserver->RemovePlayer (cnum,"You were kicked from the server by a GM.");
    return 0;
}

int com_delete( char* name )
{
    if ( strlen(name) == 0 )
    {
        CPrintf(CON_CMDOUTPUT ,"Usuage:\nPS Server: delete charactername\n");
        return 0;
    }

    //Check to see if this player needs to be kicked first
    Client* client = psserver->GetNetManager()->GetConnections()->Find(name);
    if (client)
    {
        psserver->RemovePlayer (client->GetClientNum(),"Your character is being deleted so you are being kicked.");
    }

    unsigned int characteruid=psserver->CharacterLoader.FindCharacterID(name);
    if (characteruid==0)
    {
        CPrintf(CON_CMDOUTPUT ,"Character <%s> was not found in the database.\n", name);
        return 0;
    }

    csString error;
    if (!psserver->CharacterLoader.DeleteCharacterData(characteruid,error))
    {
        if(!error.Length())
            CPrintf(CON_CMDOUTPUT ,"Character <%s> was not found in the database.\n", name);
        else
            CPrintf(CON_CMDOUTPUT ,"Character <%s> error: %s .\n", name,error.GetData());
        return 0;
    }


    CPrintf(CON_CMDOUTPUT ,"Character <%s> was removed from the database.\n", name);

    return 0;
}


int com_set(char *args)
{
    iConfigManager* cfgmgr = psserver->GetConfig();

    if (!strcmp(args, ""))
    {
        CPrintf (CON_CMDOUTPUT ,"Please give the name of the var you want to change:\n");

        csRef<iConfigIterator> ci = cfgmgr->Enumerate("PlaneShift.Server.User.");
        if (!ci)
        {
            return 0;
        }

        while (ci->HasNext())
        {
            ci->Next();
            
            if (ci->GetKey(true))
            {
                if (ci->GetComment())
                {
                    CPrintf (CON_CMDOUTPUT ,"%s",ci->GetComment());
                }
                
                CPrintf (CON_CMDOUTPUT ,COL_GREEN "%s = '%s'\n" COL_NORMAL,
                         ci->GetKey(true), ci->GetStr());
            }
        }
    }
    else
    {
        WordArray words(args);
        csString a1 = "PlaneShift.Server.User.";
        a1 += words[0];
        csString a2 = words[1];
        if (a2.IsEmpty())
        {
            CPrintf (CON_CMDOUTPUT ,COL_GREEN " %s = '%s'\n" COL_NORMAL,
                     (const char*) a1, cfgmgr->GetStr(a1, ""));
        }
        else
        {
            cfgmgr->SetStr(a1, a2);
            CPrintf (CON_CMDOUTPUT ,COL_GREEN " SET %s = '%s'\n" COL_NORMAL,
                     (const char*) a1, cfgmgr->GetStr(a1, ""));
        }
    }

    return 0;
}

int com_maplist(char *)
{
    csRef<iVFS> vfs =  csQueryRegistry<iVFS> (psserver->GetObjectReg());
    if (!vfs)
        return 0;

    csRef<iDataBuffer> xpath = vfs->ExpandPath("/planeshift/world/");
    const char* dir = **xpath;
    csRef<iStringArray> files = vfs->FindFiles(dir);

    if (!files)
        return 0;
    for (size_t i=0; i < files->GetSize(); i++)
    {
        char * name = csStrNew(files->Get(i));
        char * onlyname = name + strlen("/planeshift/world/");
        onlyname[strlen(onlyname) - 1] = '\0';
        CPrintf (CON_CMDOUTPUT ,"%s\n",onlyname);
        delete [] name;
    }
    return 0;
}

int com_dumpwarpspace(char *)
{
    EntityManager::GetSingleton().GetWorld()->DumpWarpCache();
    return 0;
}


int com_loadmap(char *mapname)
{
    if (!strcmp(mapname, ""))
    {
        CPrintf (CON_CMDOUTPUT ,COL_RED "Please specify any zip file in your /art/world"
            " directory:\n" COL_NORMAL);
                com_maplist(NULL);
        return 0;
    }

    if (!psserver->LoadMap(mapname))
    {
        CPrintf (CON_CMDOUTPUT ,"Couldn't load map!\n");
        return 0;
    }

    return 0;
}

int com_spawn(char* sector = 0)
{
    // After world is loaded, repop NPCs--only the first time.
    static bool already_spawned = false;
    psSectorInfo *sectorinfo = NULL;
    if ( sector )
        sectorinfo = CacheManager::GetSingleton().GetSectorInfoByName(sector);
    
    if (!already_spawned)
    {
        psserver->GetSpawnManager()->RepopulateLive(sectorinfo);
        psserver->GetSpawnManager()->RepopulateItems(sectorinfo);
        psserver->GetActionManager()->RepopulateActionLocations(sectorinfo);
        psserver->GetSpawnManager()->LoadHuntLocations(sectorinfo); // Start spawning
        already_spawned = true;
    }
    else
    {
        CPrintf(CON_CMDOUTPUT ,"Already respawned everything.\n");
    }
    return 0;
}

int com_rain(char* arg)
{
    const char * syntax = "rain <sector> <drops> <fade> <duration>";
    if (strlen(arg) == 0)
    {
        CPrintf(CON_CMDOUTPUT ,"Please specify sector and number of drops (minimum 2000 drops for lightning, 0 drops to stop rain) and duration (0 for default duration).\nSyntax: %s\n",syntax);
        return 0;
    }
    WordArray words(arg);
    csString sector(words[0]);
    int drops = atoi(words[1]);
    int fade = atoi(words[2]);
    int length = atoi(words[3]);
    psSectorInfo *sectorinfo = CacheManager::GetSingleton().GetSectorInfoByName(sector.GetData());
    if (!sectorinfo)
    {
        CPrintf(CON_CMDOUTPUT ,"Could not find that sector.\nSyntax: %s\n",syntax);
        return 0;
    }
    if (drops < 0 || (length <= 0 && drops > 0))
    {
        CPrintf(CON_CMDOUTPUT ,"Drops must be >= 0. Length must be > 0 if drops > 0.\nSyntax: %s\n",syntax);
        return 0;
    }
    if (drops == 0)
    {
        CPrintf(CON_CMDOUTPUT ,"Stopping rain in sector %s.\n", sector.GetData());
    }
    else
    {
        CPrintf(CON_CMDOUTPUT ,"Starting rain in sector %s with %d drops for %d ticks with fade %d.\n", 
                sector.GetData(), drops, length, fade);
    }
    
    psserver->GetWeatherManager()->QueueNextEvent(0, psWeatherMessage::RAIN, drops, length, fade,
                                                  sector.GetData(), sectorinfo);
    return 0;
}

int com_dict(char* arg)
{
    WordArray words(arg);
    if (words.GetCount()>1) 
	{
        csString fullname = words[0]+" "+words[1];
        dict->Print(fullname.GetDataSafe());
    } 
	else
        dict->Print(words[0].GetDataSafe());
    return 0;
}

int com_filtermsg(char* arg)
{
    CPrintf(CON_CMDOUTPUT ,"%s\n",psserver->GetNetManager()->LogMessageFilter(arg).GetDataSafe());
    
    return 0;
}

int com_loadnpc(char* npcName)
{

    //TODO Rewrite
    CPrintf(CON_CMDOUTPUT, "Disabled at the moment");



//    AdminManager * adminmgr = psserver->GetAdminManager();
//
//    csString name = npcName;
//
    // Call this thread save function to initiate loading of NPC.
//    adminmgr->AdminCreateNewNPC(name);

    return 0;
}

int com_loadquest(char* stringId)
{
    int id = atoi(stringId);
    CPrintf(CON_CMDOUTPUT, "Reloading quest id %d\n", id);

    if(!CacheManager::GetSingleton().UnloadQuest(id))
        CPrintf(CON_CMDOUTPUT, "Could not remove quest %d\n", id);
    else
        CPrintf(CON_CMDOUTPUT, "Existing quest removed.\n");

    if(!CacheManager::GetSingleton().LoadQuest(id))
        CPrintf(CON_CMDOUTPUT, "Could not load quest %d\n", id);
    else
        CPrintf(CON_CMDOUTPUT, "Quest %d loaded.\n", id);
    return 0;
}


int com_importnpc(char* filename)
{
    psNPCLoader npcloader;

    csString file;
    file.Format("/this/%s", filename);

    csRef<iVFS> vfs =  csQueryRegistry<iVFS> (psserver->GetObjectReg());

    csString newDir(file);
    newDir.Append("/");

    csRef<iStringArray> filenames = vfs->FindFiles(newDir);
    if(filenames->GetSize() > 0)
    {
        csString currentFile;
        size_t failed = 0;
        size_t count = filenames->GetSize();

        while(filenames->GetSize() > 0)
        {

            currentFile = filenames->Pop();

            // skip non xml files
            if (currentFile.Find(".xml") == (size_t)-1)
                continue;

            if(npcloader.LoadFromFile(currentFile))
                CPrintf(CON_CMDOUTPUT ,"Succesfully imported NPC from file %s.\n", currentFile.GetData());
            else
            {
                failed++;
                CPrintf(CON_WARNING ,"Failed to import NPC from file %s.\n\n", currentFile.GetData());
            }
        }
        CPrintf(CON_CMDOUTPUT, "Successfully imported %u NPCs.\n", count - failed);
        return 0;
    };


    if (npcloader.LoadFromFile(file))
    {
        CPrintf(CON_CMDOUTPUT ,"Succesfully imported NPC.\n");
    } else
    {
        CPrintf(CON_CMDOUTPUT ,"Failed to import NPC.\n");
    }

    return 0;
}


int com_exportnpc(char *args)
{
    WordArray words(args);
    csArray<int> npcids;
    bool all = false;

    if(words.GetCount() == 1 && !strcasecmp(words.Get(0),"all"))
        all = true;

    if (words.GetCount()!=2 && !all)
    {
        CPrintf(CON_CMDOUTPUT ,"Usage: /exportnpc <id> <filename>\n");
        CPrintf(CON_CMDOUTPUT ,"       /exportnpc all\n");
        return -1;
    }

    psNPCLoader npcloader;

    if(all)
    {
        Result result(db->Select("SELECT id from characters where npc_master_id !=0"));
        if (!result.IsValid())
        {
            CPrintf(CON_ERROR, "Cannot load character ids from database.\n");
            CPrintf(CON_ERROR, db->GetLastError());
            return -1;
        }
        if(result.Count() == 0)
        {
            CPrintf(CON_ERROR, "No NPCs to export.\n");
            return -1;
        }

        csString filename;
        int npcid;
        unsigned long failed = 0;

        for (unsigned long i=0; i<result.Count(); i++)
        {
            npcid = result[i].GetInt(0);
            filename.Format("npc-id%i.xml", npcid);
            if(!npcloader.SaveToFile(result[i].GetInt(0), filename))
            {
                failed++;
                CPrintf(CON_WARNING, "Failed to export npc %i\n\n", npcid);
            }
        }
        CPrintf(CON_CMDOUTPUT, "Successfully exported %u NPCs.\n", result.Count() - failed);
        return 0;
    }

    int id = atoi(words.Get(0));
    csString fileName = words.Get(1);

    if (npcloader.SaveToFile(id, fileName))
    {
        CPrintf(CON_CMDOUTPUT ,"Succesfully exported NPC.\n");
    } else
    {
        CPrintf(CON_CMDOUTPUT ,"Failed to export NPC.\n");
    }

    return 0;
}


int com_importdialogs(char *filename)
{
    csString file;

    if (filename==NULL || filename[0] == '\0') {
      CPrintf(CON_WARNING ,"Please speficy a filename, pattern or 'all' (for all files in /this/).\n\n");
      return 0;
    }

    printf ("-%s-",filename);

    if (!strcmp(filename,"all"))
      file="/this/";
    else
      file.Format("/this/%s", filename);

    csRef<iVFS> vfs =  csQueryRegistry<iVFS> (psserver->GetObjectReg());

    csString newDir(file);
    newDir.Append("/");

    csRef<iStringArray> filenames = vfs->FindFiles(newDir);
    if(filenames->GetSize() > 0)
    {
        csString currentFile;
        size_t failed = 0;
        size_t count = filenames->GetSize();
        psNPCLoader npcloader;

        while(filenames->GetSize() > 0)
        {
            currentFile = filenames->Pop();

            // skip non xml files
            if (currentFile.Find(".xml") == (size_t)-1)
                continue;

            if(npcloader.LoadDialogsFromFile(currentFile))
                CPrintf(CON_CMDOUTPUT ,"Succesfully imported dialogs from file %s.\n", currentFile.GetData());
            else
            {
                failed++;
                CPrintf(CON_WARNING ,"Failed to import dialogs from file %s.\n\n", currentFile.GetData());
            }
        }
        CPrintf(CON_CMDOUTPUT, "Successfully imported %u dialogs.\n", count - failed);
        return 0;
    };

    psNPCLoader npcloader;
    if (npcloader.LoadDialogsFromFile(file))
    {
        CPrintf(CON_CMDOUTPUT ,"Succesfully imported NPC dialogs.\n");
    } else
    {
        CPrintf(CON_CMDOUTPUT ,"Failed to import NPC dialogs.\n");
    }

    return 0;
}


int com_exportdialogs(char *args)
{
    bool allquests = false;
    WordArray words(args);

    if (words.GetCount() == 1 && !strcasecmp(words.Get(0),"allquests"))
        allquests = true;

    if (words.GetCount()!=3 && !allquests)
    {
        CPrintf(CON_CMDOUTPUT ,"Usage: /exportdialogs area <areaname> <filename>\n");
        //CPrintf(CON_CMDOUTPUT ,"       /exportdialogs queststep <questid> <filename>\n");
        CPrintf(CON_CMDOUTPUT ,"       /exportdialogs quest <questid> <filename>\n");
        CPrintf(CON_CMDOUTPUT ,"       /exportdialogs allquests\n");
        return -1;
    }

    if(allquests)
    {
        Result questids(db->Select("SELECT id FROM quests WHERE master_quest_id = 0"));
        if(questids.Count() == 0)
        {
            CPrintf(CON_ERROR, "No complete quests found.\n");
            return -1;
        }

        psNPCLoader npcloader;
        csString filename;
        csString areaname("");
        int questid;
        unsigned long failed = 0;

        for(unsigned long i=0;i<questids.Count();i++)
        {
            questid = questids[i].GetInt(0);
            filename.Format("quest-id%i.xml",questid);
            if (!npcloader.SaveDialogsToFile(areaname, filename, questid, true))
            {
                failed++;
                CPrintf(CON_WARNING, "Failed to export quest %i\n\n", questid);
            }
        }
        CPrintf(CON_CMDOUTPUT, "Successfully exported %u quests.\n", questids.Count() - failed);
        return 0;
    }

    csString type = words.Get(0);
    csString areaname = words.Get(1);
    csString filename = words.Get(2);
    bool quest = type.CompareNoCase("quest");

    int questid = -1;

    if (quest)
    {
        questid = atoi(areaname.GetData());
        areaname.Clear();
        Result masterQuest(db->Select("SELECT master_quest_id FROM quests WHERE id = '%i'", questid));
        if (masterQuest.Count() < 1)
        {
            CPrintf(CON_CMDOUTPUT, "No quest with that id found in the quests table.\n");
            return -1;
        }
        if (masterQuest[0].GetInt(0) != 0)
        {
            CPrintf(CON_CMDOUTPUT, "This quest is not a complete quest.\n");
            return -1;
        }
    }
    else if (!type.CompareNoCase("area"))
    {
        CPrintf(CON_CMDOUTPUT ,"Usage: /exportdialogs area <areaname> <filename>\n");
        //CPrintf(CON_CMDOUTPUT ,"       /exportdialogs queststep <questid> <filename>\n");
        CPrintf(CON_CMDOUTPUT ,"       /exportdialogs quest <questid> <filename>\n");
        return -1;
    }

    psNPCLoader npcloader;
    // trick to allow export of areas with spaces, just use $ instead, example Pet$Groffel$1
    areaname.ReplaceAll("$"," ");
    if (npcloader.SaveDialogsToFile(areaname, filename, questid, quest))
    {
        CPrintf(CON_CMDOUTPUT ,"Succesfully exported NPC dialogs\n");
    } else
    {
        CPrintf(CON_CMDOUTPUT ,"Failed to export NPC dialogs\n");
    }

    return 0;
}


int com_newacct(char *userpass)
{
    char *password;
    if (!userpass)
    {
        CPrintf(CON_CMDOUTPUT ,"Please specify username/password.\n");
        return 0;
    }
    char *slash = strchr(userpass,'/');
    if (!slash)
    {
        CPrintf(CON_CMDOUTPUT ,"Please specify username/password with slashes (/) between them.\n");
        return 0;
    }
    *slash = 0;  // term the user str
    password=slash+1;

    psAccountInfo accountinfo;

    accountinfo.username = userpass;
    accountinfo.password = csMD5::Encode(password).HexString();

    if (CacheManager::GetSingleton().NewAccountInfo(&accountinfo)==0)
    {
        CPrintf(CON_CMDOUTPUT ,"Could not create account.\n");
        return 0;
    }
    CPrintf(CON_CMDOUTPUT ,"Account created successfully.\n");

    return 0;
}

/*****
int com_newguild(char *nameleader)
{
    char *leadername;
    if (!nameleader)
    {
        CPrintf(CON_CMDOUTPUT ,"Please specify guildname/leader.\n");
        return 0;
    }
    char *slash = strchr(nameleader,'/');
    if (!slash)
    {
        CPrintf(CON_CMDOUTPUT ,"Please specify guildname/leader with a slash (/) between them.\n");
        return 0;
    }
    *slash = 0;
    leadername=slash+1;

    unsigned int leaderuid=psserver->CharacterLoader.FindCharacterID(leadername);
    if (leaderuid==0)
    {
        CPrintf(CON_CMDOUTPUT ,"That leader name was not found.\n");
        return 0;
    }


    int rc = psserver->GetDatabase()->CreateGuild(nameleader,leaderuid);

    switch(rc)
    {
    case 0: CPrintf(CON_CMDOUTPUT ,"Guild created successfully.\n");
        break;
    case 1: CPrintf(CON_CMDOUTPUT ,"That guildname already exists.\n");
        break;
    case 2: CPrintf(CON_CMDOUTPUT ,"That leader name was not found.\n");
        break;
    default: CPrintf(CON_CMDOUTPUT ,"SQL Error: %s\n",psserver->GetDatabase()->GetLastError());
        break;
    }

    return 0;
}

int com_joinguild(char *guild_member)
{
    char *membername;
    if (!guild_member)
    {
        CPrintf(CON_CMDOUTPUT ,"Please specify guildname/newmember.\n");
        return 0;
    }
    char *slash = strchr(guild_member,'/');
    if (!slash)
    {
        CPrintf(CON_CMDOUTPUT ,"Please specify guildname/newmember with a slash (/) between them.\n");
        return 0;
    }
    *slash = 0;
    membername=slash+1;

    int guild = psserver->GetDatabase()->GetGuildID(guild_member);
    unsigned int memberuid = psserver->CharacterLoader.FindCharacterID(membername);

    if (guild==-1 || memberuid==0)
    {
        CPrintf(CON_CMDOUTPUT ,"guild or member name is not found.\n");
        return 0;
    }

    int rc = psserver->GetDatabase()->JoinGuild(guild,memberuid);

    switch(rc)
    {
    case 0:  CPrintf(CON_CMDOUTPUT ,"Guild joined successfully.\n");
        break;
    case -2: CPrintf(CON_CMDOUTPUT ,"Character is already a member of another guild.\n");
        break;
    default: CPrintf(CON_CMDOUTPUT ,"SQL Error: %s\n",psserver->GetDatabase()->GetLastError());
        break;
    }

    return 0;
}


int com_quitguild(char *name)
{
    if (!name)
    {
        CPrintf(CON_CMDOUTPUT ,"Please specify member who is leaving.\n");
        return 0;
    }

        unsigned int memberuid=psserver->CharacterLoader.FindCharacterID(name);

    if (memberuid==0)
    {
        CPrintf(CON_CMDOUTPUT ,"Character name is not found.  Sorry.\n");
        return 0;
    }

    int rc = psserver->GetDatabase()->LeaveGuild(memberuid);

    switch(rc)
    {
    case 0:  CPrintf(CON_CMDOUTPUT ,"Guild left successfully.\n");
        break;
    case -3: CPrintf(CON_CMDOUTPUT ," is not a member of a guild right now.\n");
        break;
    default: CPrintf(CON_CMDOUTPUT ,"SQL Error: %s\n",psserver->GetDatabase()->GetLastError());
        break;
    }

    return 0;
}
******************************/

int com_addinv(char *line)
{
    bool temploaded=false;

    if (!line)
    {
        CPrintf(CON_CMDOUTPUT ,"Please specify Character Name and Basic Item Template Name and optionally a stack count.\n");
        return 0;
    }

    WordArray words(line);

    csString charactername = words[0];
    csString item   = words[1];
    //int stack_count = words.GetInt(2);

    if (!charactername.Length() || !item.Length())
    {
        CPrintf(CON_CMDOUTPUT ,"Please specify Character Name and Basic Item Template ID and optionally a stack count.\n");
        return 0;
    }

    // Get the UID of this character based on the provided name.  This ensures the name is accurate.
    unsigned int characteruid=psserver->CharacterLoader.FindCharacterID((char*)charactername.GetData());
    if (characteruid==0)
    {
        CPrintf(CON_CMDOUTPUT ,"Character name not found.\n");
        return 0;
    }

    // Get the ItemStats based on the name provided.
    psItemStats *itemstats=CacheManager::GetSingleton().GetBasicItemStatsByID(atoi(item.GetData()) );
    if (itemstats==NULL)
    {
        CPrintf(CON_CMDOUTPUT ,"No Basic Item Template with that id was found.\n");
        return 0;
    }

    // If the character is online, update the stats live.  Otherwise we need to load the character data to add this item to
    //  an appropriate inventory slot.
    psCharacter *chardata=NULL;
    Client* client = psserver->GetNetManager()->GetConnections()->Find(charactername.GetData());
    if (!client)
    {
        // Character is not online
        chardata=psserver->CharacterLoader.LoadCharacterData(characteruid,false);
        temploaded=true;
    }
    else
        chardata=client->GetCharacterData();

    if (chardata==NULL)
    {
        CPrintf(CON_CMDOUTPUT ,"Could not get character data for specified character.\n");
        return 0;
    }


    psItem *iteminstance = itemstats->InstantiateBasicItem();
    if (iteminstance==NULL)
    {
        CPrintf(CON_CMDOUTPUT ,"Could not instantiate item based on basic properties.\n");
        return 0;
    }

    iteminstance->SetLoaded();  // Item is fully created
    if (!chardata->Inventory().Add(iteminstance, false, false))
    {
        CPrintf(CON_CMDOUTPUT ,"The item did not fit into the character's inventory.\n");
        CacheManager::GetSingleton().RemoveInstance(iteminstance);
        return 0;
    }

    // If we temporarily loaded the character for this add, unload them now
    if (temploaded)
        delete chardata;


    CPrintf(CON_CMDOUTPUT ,"Added item '%s' to character '%s'.\n",item.GetData(),charactername.GetData());

    return 0;
}

csString get_item_modifiers(psItem *item)
{
    csString modifiers;
    modifiers = "(Modifiers:";
    bool use_comma = false;
    int i;
    for (i = 0 ; i < PSITEM_MAX_MODIFIERS ; i++)
    {
        psItemStats* stats = item->GetModifier(i);
    if (stats)
    {
        if (use_comma) modifiers += ", ";
        use_comma = true;
        modifiers += stats->GetName ();
    }
    }
    if (!use_comma) modifiers.Clear();
    else modifiers += ")";
    return modifiers;
}

csString get_item_stats(psItem *item)
{
    csString stats;
    stats.Format ("Qual:%g Guild:%u Crafter:%u DecayResist:%g SumWeight:%g Uni:%c",
        item->GetItemQuality(),
    item->GetGuildID(),
    item->GetCrafterID(),
    item->GetDecayResistance(),
    0.0, ////// TODO: Hardcoded zero weight
    //////////////////////////item->GetSumWeight(),
    item->GetIsUnique() ? 'Y' : 'N');
    return stats;
}

static void indent(int depth)
{
    for (int i=0;i<depth;i++) CPrintf(CON_CMDOUTPUT ,"    ");
}

void show_itemstat_stats(const char* prefix,psItemStats *itemstats,int depth)
{
    indent(depth);
    csString prog = itemstats->GetProgressionEventEquip();
    csString progun = itemstats->GetProgressionEventUnEquip();
    CPrintf(CON_CMDOUTPUT ,"%s Progression Event Equip:%s Progression Event UnEquip:%s\n", prefix, prog.GetData(), progun.GetData());
    PSITEM_FLAGS flags = itemstats->GetFlags();
    csString flags_string;
    if (flags & PSITEMSTATS_FLAG_IS_A_MELEE_WEAPON) flags_string += "MELEE ";
    if (flags & PSITEMSTATS_FLAG_IS_A_RANGED_WEAPON) flags_string += "RANGED ";
    if (flags & PSITEMSTATS_FLAG_IS_A_SHIELD) flags_string += "SHIELD ";
    if (flags & PSITEMSTATS_FLAG_IS_AMMO) flags_string += "AMMO ";
    if (flags & PSITEMSTATS_FLAG_IS_A_CONTAINER) flags_string += "CONTAINER ";
    if (flags & PSITEMSTATS_FLAG_USES_AMMO) flags_string += "USESAMMO ";
    if (flags & PSITEMSTATS_FLAG_IS_STACKABLE) flags_string += "STACKABLE ";
    if (flags & PSITEMSTATS_FLAG_IS_GLYPH) flags_string += "GLYPH ";
    if (flags & PSITEMSTATS_FLAG_CAN_TRANSFORM) flags_string += "TRANSFORM ";
    if (flags & PSITEMSTATS_FLAG_NOPICKUP) flags_string += "NOPICKUP ";
    if (flags & PSITEMSTATS_FLAG_TRIA) flags_string += "TRIA ";
    if (flags & PSITEMSTATS_FLAG_HEXA) flags_string += "HEXA ";
    if (flags & PSITEMSTATS_FLAG_OCTA) flags_string += "OCTA ";
    if (flags & PSITEMSTATS_FLAG_CIRCLE) flags_string += "CIRCLE ";
    if (flags & PSITEMSTATS_FLAG_CONSUMABLE) flags_string += "CONSUMABLE ";
    {
        indent(depth);
        CPrintf(CON_CMDOUTPUT ,"%s Flags: %s\n", prefix, flags_string.GetData());
    }
}

void show_item_stats(psItem *item,int depth)
{
    depth++;
    if (item->GetDescription())
    {
    indent(depth);
        CPrintf(CON_CMDOUTPUT ,"Desc:%s\n", item->GetDescription());
    }

    indent(depth);
    CPrintf(CON_CMDOUTPUT ,"Weight:%g Size:%d ContainerMaxSize:%d VisDistance:%g DecayResist:%g\n",
                            item->GetWeight(),
                            item->GetItemSize(),
                            item->GetContainerMaxSize(),    
                            item->GetVisibleDistance(),
                            item->GetDecayResistance());

    if (item->GetMeshName())
    {
        indent(depth);
        CPrintf(CON_CMDOUTPUT ,"MeshName:%s\n", item->GetMeshName());
    }
    if (item->GetTextureName())
    {
        indent(depth);
        CPrintf(CON_CMDOUTPUT ,"TextureName:%s\n", item->GetTextureName());
    }
    if (item->GetPartName())
    {
        indent(depth);
        CPrintf(CON_CMDOUTPUT ,"PartName:%s\n", item->GetPartName());
    }
    if (item->GetImageName())
    {
        indent(depth);
        CPrintf(CON_CMDOUTPUT ,"ImageName:%s\n", item->GetImageName());
    }

    psMoney price = item->GetPrice();
    if (price.GetTotal() > 0)
    {
    indent(depth);
        CPrintf (CON_CMDOUTPUT ,"Price Circles:%d Octas:%d Hexas:%d Trias:%d Total:%d\n",
        price.GetCircles(), price.GetOctas(), price.GetHexas(), price.GetTrias(),
        price.GetTotal());
    }

    PSITEM_FLAGS flags = item->GetFlags();
    csString flags_string;
    if (flags & PSITEM_FLAG_UNIQUE_ITEM) flags_string += "UNIQUE ";
    if (flags & PSITEM_FLAG_PURIFIED) flags_string += "PURIFIED ";
    if (flags & PSITEM_FLAG_PURIFYING) flags_string += "PURIFYING ";
    if (flags & PSITEM_FLAG_LOCKED) flags_string += "LOCKED ";
    if (flags & PSITEM_FLAG_LOCKABLE) flags_string += "LOCKABLE ";
    if (flags & PSITEM_FLAG_SECURITYLOCK) flags_string += "SECURITYLOCK ";
    if (flags & PSITEM_FLAG_UNPICKABLE) flags_string += "UNPICKABLE ";
    if (flags & PSITEM_FLAG_KEY) flags_string += "KEY ";
    if (flags & PSITEM_FLAG_MASTERKEY) flags_string += "MASTERKEY ";
    if (flags_string.Length () > 0)
    {
    indent(depth);
        CPrintf (CON_CMDOUTPUT ,"Flags: %s\n", flags_string.GetData());
    }

    //psItemStats* basestats = item->GetBaseStats();
    //if (basestats)
    //show_itemstat_stats("Base",basestats,depth);
    psItemStats* curstats = item->GetCurrentStats();
    if (curstats)
    show_itemstat_stats("Current",curstats,depth);

    PSITEMSTATS_ARMORTYPE armortype = item->GetArmorType();
    if (armortype != PSITEMSTATS_ARMORTYPE_NONE)
    {
        indent(depth);
    switch (armortype)
    {
        case PSITEMSTATS_ARMORTYPE_LIGHT: CPrintf(CON_CMDOUTPUT ,"Armor:Light"); break;
        case PSITEMSTATS_ARMORTYPE_MEDIUM: CPrintf(CON_CMDOUTPUT ,"Armor:Medium"); break;
        case PSITEMSTATS_ARMORTYPE_HEAVY: CPrintf(CON_CMDOUTPUT ,"Armor:Heavy"); break;
        default:;
    }
    CPrintf(CON_CMDOUTPUT ," Hardness:%g\n",
        item->GetHardness());
    }
    PSITEMSTATS_WEAPONTYPE weapontype = item->GetWeaponType();
    if (weapontype != PSITEMSTATS_WEAPONTYPE_NONE)
    {
        indent(depth);
        switch (weapontype)
        {
            case PSITEMSTATS_WEAPONTYPE_SWORD: CPrintf(CON_CMDOUTPUT ,"WType:Sword"); break;
            case PSITEMSTATS_WEAPONTYPE_AXE: CPrintf(CON_CMDOUTPUT ,"WType:Axe"); break;
            case PSITEMSTATS_WEAPONTYPE_DAGGER: CPrintf(CON_CMDOUTPUT ,"WType:Dagger"); break;
            case PSITEMSTATS_WEAPONTYPE_HAMMER: CPrintf(CON_CMDOUTPUT ,"WType:Hammer"); break;
        default:;
        }
    CPrintf(CON_CMDOUTPUT ," Latency:%g Penetration:%g untgtblock:%g tgtblock=%g cntblock=%g\n",
        item->GetLatency(),
        item->GetPenetration(),
        item->GetUntargetedBlockValue(),
        item->GetTargetedBlockValue(),
        item->GetCounterBlockValue());
    }
    PSITEMSTATS_AMMOTYPE ammotype = item->GetAmmoType();
    if (ammotype != PSITEMSTATS_AMMOTYPE_NONE)
    {
        indent(depth);
    switch (ammotype)
    {
        case PSITEMSTATS_AMMOTYPE_ARROWS: CPrintf(CON_CMDOUTPUT ,"Ammo:Arrows\n"); break;
        case PSITEMSTATS_AMMOTYPE_BOLTS: CPrintf(CON_CMDOUTPUT ,"Ammo:Bolts\n"); break;
        case PSITEMSTATS_AMMOTYPE_ROCKS: CPrintf(CON_CMDOUTPUT ,"Ammo:Rocks\n"); break;
        default:;
    }
    }
}

csString com_showinv_itemextra(bool moreiteminfo, psItem *currentitem)
{
    if (moreiteminfo)
        return get_item_modifiers(currentitem) + " " + get_item_stats(currentitem);
    else
        return csString("");
}

void com_showinv_item(bool moreiteminfo, psItem *currentitem, const char *slotname, unsigned int slotnumber)
{
//    psItem *workingset[5];
//    unsigned int positionset[5];
    csString output;

    if (currentitem!=NULL)
    { 
        if(moreiteminfo)
        {
            output.AppendFmt("%s\tCount: %d\tID:%u Instance ID:%u \n",currentitem->GetName(),
                currentitem->GetStackCount(),currentitem->GetBaseStats()->GetUID(), currentitem->GetUID());
        }
        else
        {
            output.AppendFmt("%s\tCount: %d\t", currentitem->GetName(),currentitem->GetStackCount() );
        }
        csString siextra = com_showinv_itemextra(moreiteminfo,currentitem);
        if (slotnumber != (unsigned int)-1)
        {
            output.AppendFmt("(%s%u) Count:%u %s\n",slotname,slotnumber,currentitem->GetStackCount(), siextra.GetData());
         //   CPrintf(CON_CMDOUTPUT ,"%s (%s%u) %s\n",currentitem->GetName(),slotname,slotnumber,
         //   siextra.GetData());
        }
        else
        {
            output.AppendFmt("(%s) %s\n",slotname, siextra.GetData());
         //   CPrintf(CON_CMDOUTPUT ,"%s (%s) %s\n",currentitem->GetName(),slotname,
         //   siextra.GetData());
        }
        CPrintf(CON_CMDOUTPUT, output);
        if (moreiteminfo)
                show_item_stats(currentitem,0);

        /*******************************************************************************************
        if (currentitem->GetIsContainer())
        {
            workingset[0]=currentitem;
            positionset[0]=0;
            depth=1;
            while (depth>1 || positionset[depth-1]<PSITEM_MAX_CONTAINER_SLOTS)
            {
                currentitem=workingset[depth-1]->GetItemInSlot(positionset[depth-1]);
                if (currentitem!=NULL)
                {
                    for (int i=0;i<depth;i++)
                        CPrintf(CON_CMDOUTPUT ,"   -");
            csString siextra = com_showinv_itemextra(moreiteminfo,currentitem);
                    CPrintf(CON_CMDOUTPUT ,"%s %s\n",currentitem->GetName(), siextra.GetData());
            if (moreiteminfo)
            show_item_stats(currentitem,depth);

                    // Check recursion
                    if (currentitem->GetIsContainer())
                    {
                        if (depth>=5)
                        {
                            for (int i=0;i<depth;i++)
                                CPrintf(CON_CMDOUTPUT ,"   -");
                            CPrintf(CON_CMDOUTPUT ,"Items deeper than max depth of 5.\n");
                        }
                        else
                        {
                            workingset[depth]=currentitem;
                            positionset[depth]=0;
                            depth++;
                            continue;
                        }
                    }
                }
                // Next item at this level
                positionset[depth-1]++;

                // Drop to previous level
                while (depth>1 && positionset[depth-1]>=PSITEM_MAX_CONTAINER_SLOTS)
                {
                    depth--;
                    positionset[depth-1]++;
                }
            }
        }
        *********************************************************/
    }
}

int com_showinv(char *line, bool moreiteminfo)
{
    bool temploaded=false;

    if (!line)
    {
        CPrintf(CON_CMDOUTPUT ,"Please specify a character name.\n");
        return 0;
    }

    // If the character is online use the active stats.  Otherwise we need to load the character data.
    psCharacter *chardata=NULL;
    unsigned int characteruid=psserver->CharacterLoader.FindCharacterID(line,false);
    if (characteruid==0)
    {
        CPrintf(CON_CMDOUTPUT ,"Character name is not found.\n");
        return 0;
    }

    gemObject *obj = GEMSupervisor::GetSingleton().FindPlayerEntity(characteruid);
    if (!obj)
    {
        obj = GEMSupervisor::GetSingleton().FindNPCEntity(characteruid);
    }
    // If the character is online use the active stats.  Otherwise we need to load the character data.
    if (obj)
    {
        chardata = obj->GetCharacterData();
    }
    else
    {
        // Character is not online
        CPrintf(CON_CMDOUTPUT,"Loading inventory from database because character is not found online.\n");
        chardata=psserver->CharacterLoader.LoadCharacterData(characteruid,true);
        temploaded=true;
    }

    if (chardata==NULL)
    {
        CPrintf(CON_CMDOUTPUT ,"Could not get character data for specified character.\n");
        return 0;
    }

    /*  Iterating through a character's items is not standard enough to have an iterator yet.
     *   So we implement the logic here.
     */
    psItem *currentitem;
    unsigned int charslot;

///KWF    CPrintf(CON_CMDOUTPUT ,"Weight: %.2f MaxWeight: %.2f Capacity: %.2f MaxCapacity: %.2f\n",
///            chardata->Inventory().Weight(),chardata->Inventory().MaxWeight(),
///            chardata->Inventory().Capacity(), chardata->Inventory().MaxCapacity());

    if (moreiteminfo)
        CPrintf(CON_CMDOUTPUT ,"%d trias, %d hexas, %d octas, %d circles\n",
                chardata->Money().GetTrias(),
                chardata->Money().GetHexas(),
                chardata->Money().GetOctas(),
                chardata->Money().GetCircles() );

    CPrintf(CON_CMDOUTPUT ,"Total money: %d\n", chardata->Money().GetTotal() );

    // Inventory indexes start at 1.  0 is reserved for the "NULL" item.
    for (charslot=1;charslot<chardata->Inventory().GetInventoryIndexCount(); charslot++)
    {
        currentitem=chardata->Inventory().GetInventoryIndexItem(charslot);
        const char *name = CacheManager::GetSingleton().slotNameHash.GetName( currentitem->GetLocInParent() );
        char buff[20];
        if (!name)
        {
            cs_snprintf(buff,19,"Bulk %d",currentitem->GetLocInParent(true) );
        }
        com_showinv_item(moreiteminfo,currentitem, name ? name : buff,(unsigned int)-1);
    }

    if (temploaded)
        delete chardata;

    return 0;
}

int com_showinv(char *line)
{
    return com_showinv(line, false);
}

int com_showinvf(char *line)
{
    return com_showinv(line, true);
}


int com_exec(char *line)
{
    if (!line)
    {
        CPrintf(CON_CMDOUTPUT ,"Please specify a script file.\n");
        return 0;
    }
    csRef<iVFS> vfs =  csQueryRegistry<iVFS> (psserver->GetObjectReg());

    if (!vfs->Exists(line))
    {
        CPrintf(CON_CMDOUTPUT ,"The specified file doesn't exist.\n");
        return 0;
    }
    if (line[strlen(line)-1] == '/')
    {
        CPrintf(CON_CMDOUTPUT ,"The specified file doesn't exist.\n");
        return 0;
    }

    csRef<iDataBuffer> script = vfs->ReadFile(line);
    ServerConsole::ExecuteScript(*(*script));

    return 0;
}

int com_print(char *line)
{
    unsigned int num;
    if (!line || !atoi(line) )
    {
        CPrintf(CON_CMDOUTPUT ,"Please specify an entity #.\n");
        return 0;
    }
    num = atoi(line);
    gemObject* obj;

    obj = GEMSupervisor::GetSingleton().FindObject(num);

    if (obj)
    {
        obj->Dump();

        gemNPC * npc = obj->GetNPCPtr();
        if (npc)
        {
            CPrintf(CON_CMDOUTPUT ,"--------- NPC Information -------\n");
            psNPCDialog *npcdlg = npc->GetNPCDialogPtr();

            if (npcdlg)
            {
                npcdlg->DumpDialog();
            }
            

        }
        

        return 0;
    }

    CPrintf(CON_CMDOUTPUT ,"Entity %d was not found.\n",num);
    return 0;
}

int com_entlist(char *)
{
    csHash<gemObject *>& gems = GEMSupervisor::GetSingleton().GetAllGEMS();

    csHash<gemObject *>::GlobalIterator i(gems.GetIterator());
    gemObject* obj;

    CPrintf(CON_CMDOUTPUT ,"%-5s %-15s %-20s Position\n","EID","Type","Name");
    while ( i.HasNext() )
    {
        obj = i.Next();
        if (obj)
        {
            csVector3 pos;
            float     rot;
            iSector  *sector;
            obj->GetPosition(pos,rot,sector);
            const char *sector_name =
                (sector) ? sector->QueryObject()->GetName():"(null)";

            CPrintf(CON_CMDOUTPUT ,"%5d %-15s %-20s (%9.3f,%9.3f,%9.3f, %s)\n",
                    obj->GetEntityID(),
                    obj->GetObjectType(),
                    obj->GetName(),
                    pos.x,pos.y,pos.z,sector_name );
        }
    }

    return 0;
}

int com_charlist(char *)
{
    csHash<gemObject *>& gems = GEMSupervisor::GetSingleton().GetAllGEMS();

    csHash<gemObject *>::GlobalIterator i(gems.GetIterator());
    gemObject* obj;

    CPrintf(CON_CMDOUTPUT ,"%-9s %-5s %-9s %-10s %-20s\n","PID","EID","CNUM","Type","Name");
    while ( i.HasNext() )
    {
        obj = i.Next();
        gemActor* actor = dynamic_cast<gemActor*>(obj);
        if (actor)
        {
            CPrintf(CON_CMDOUTPUT ,"%9u %5u %9u %-10s %-20s\n",
                    actor->GetCharacterData()->GetCharacterID(),
                    actor->GetEntityID(),
                    actor->GetClientID(),
                    actor->GetObjectType(),
                    actor->GetName());
        }
    }

    return 0;
}

int com_factions(char *)
{

    csHash<gemObject *>& gems = GEMSupervisor::GetSingleton().GetAllGEMS();

    csHash<gemObject *>::GlobalIterator itr(gems.GetIterator());
    gemObject* obj;

    csArray<gemActor*> actors;
    size_t i;
    size_t j;
    while ( itr.HasNext() )
    {
        obj = itr.Next();

        if (obj && obj->GetActorPtr())
        {
            gemActor * actor = obj->GetActorPtr();
            if (actor)
                actors.Push(actor);
        }
    }

    size_t num = actors.GetSize();
    CPrintf(CON_CMDOUTPUT ,"                     ");
    for (i = 0; i < num; i++)
    {
        CPrintf(CON_CMDOUTPUT ,"%*s ",MAX(7,strlen(actors[i]->GetName())),actors[i]->GetName());
    }
    CPrintf(CON_CMDOUTPUT ,"\n");
    for (i = 0; i < num; i++)
    {
        CPrintf(CON_CMDOUTPUT ,"%20s ",actors[i]->GetName());
        for (j = 0; j < num; j++)
        {
            CPrintf(CON_CMDOUTPUT ,"%*.2f ",MAX(7,strlen(actors[j]->GetName())),actors[i]->GetRelativeFaction(actors[j]));
        }
        CPrintf(CON_CMDOUTPUT ,"\n");
    }

    csHash<Faction*,int> factions_by_id = CacheManager::GetSingleton().GetFactionHash();
    CPrintf(CON_CMDOUTPUT ,"                     ");
    csHash<Faction*, int>::GlobalIterator iter = factions_by_id.GetIterator();
    while (iter.HasNext())
    {
        Faction * faction = iter.Next();
        CPrintf(CON_CMDOUTPUT ,"%15s ",faction->name.GetData());
    }
    CPrintf(CON_CMDOUTPUT ,"\n");

    for (j = 0; j < num; j++)
    {
        CPrintf(CON_CMDOUTPUT ,"%20s",actors[j]->GetName());
        csHash<Faction*, int>::GlobalIterator iter = factions_by_id.GetIterator();
        while (iter.HasNext())
        {
            Faction * faction = iter.Next();
            FactionSet * factionSet = actors[j]->GetFactions();
            int standing = 0;
            float weight = 0.0;
            factionSet->GetFactionStanding(faction->id,standing,weight);
            CPrintf(CON_CMDOUTPUT ," %7d %7.2f",standing,weight);
        }
        CPrintf(CON_CMDOUTPUT ,"\n");
    }


    return 0;
}


int com_sectors(char *)
{
    csRef<iEngine> engine = csQueryRegistry<iEngine> (psserver->GetObjectReg());
    csRef<iSectorList> sectorList = engine->GetSectors();
    csRef<iCollectionArray> collections = engine->GetCollections();
    for (int i = 0; i < sectorList->GetCount(); i++){
        iSector * sector = sectorList->Get(i);
        csString sectorName = sector->QueryObject()->GetName();
        psSectorInfo * si = CacheManager::GetSingleton().GetSectorInfoByName(sectorName);


        CPrintf(CON_CMDOUTPUT ,"%4i %4u %s",i,si?si->uid:0,sectorName.GetDataSafe());

        for (size_t r = 0; r < collections->GetSize(); r++)
        {
            if (collections->Get(r)->FindSector(sector->QueryObject()->GetName()))
            {

                CPrintf(CON_CMDOUTPUT ," %s", collections->Get(r)->QueryObject()->GetName());
            }

        }
        CPrintf(CON_CMDOUTPUT ,"\n");
        
    }
    return 0;
}

int com_showlogs(char *line)
{
    pslog::DisplayFlags(*line?line:NULL);
    return 0;
}

int com_setlog(char *line)
{
    if (!*line)
    {
        CPrintf(CON_CMDOUTPUT ,"Please specify: <log> <true/false> <filter_id>\n");
        CPrintf(CON_CMDOUTPUT, "            or: all <true/false> \n");
        return 0;
    }
    WordArray words(line);
    csString log(words[0]);
    csString flagword(words[1]);
    csString filter(words[2]);

    bool flag;
    if (tolower(flagword.GetAt(0)) == 't' || tolower(flagword.GetAt(0)) == 'y' || flagword.GetAt(0) == '1')
    {
        flag=true;
    }
    else
    {
        flag=false;
    }

    uint32 filter_id=0;
    if(filter && !filter.IsEmpty())
    {
        filter_id=atoi(filter.GetDataSafe());
    }

    pslog::SetFlag(log, flag, filter_id);

    return 0;
}

int com_adjuststat(char *line)
{
    if (!line || !strcmp(line,"") || !strcmp((const char*)line,"help"))
    {
        CPrintf(CON_CMDOUTPUT ,"Please specify: PlayerName StatValue AdjustMent.\n"
            "Where StatValue can be one of following:\n"
            "HP: HP HP_max HP_rate\n"
            "Mana: Mana: Mana_max Mana_rate\n"
            "Physical Stamina: Pstamina(Psta) Pstamina(Psta)_max Pstamina(Psta)_rate\n"
            "Mental Stamina: Mstamina(Msta) Mstamina(Msta)_max Mstamina(Msta)_rate");
        return 0;
    }

    WordArray words(line);
    csString charname(words[0]);
    csString statValue(words[1]);
    float adjust = atof(words[2]);
    int clientnum = atoi(words[0]);

    csHash<gemObject *>& gems = GEMSupervisor::GetSingleton().GetAllGEMS();

    csHash<gemObject *>::GlobalIterator i(gems.GetIterator());
    gemActor * actor = NULL;
    bool found = false;

    if (clientnum != 0)
    {
        Client* client = psserver->GetNetManager()->GetClient(clientnum);
        if (!client)
        {
            CPrintf(CON_CMDOUTPUT ,"Couldn't find client %d!\n",clientnum);
            return 0;
        }
        actor = client->GetActor();
        if (!actor)
        {
            CPrintf(CON_CMDOUTPUT ,"Found client, but not an actor!\n");
            return 0;
        }

        found = true;

    }
    else
    {
        while ( i.HasNext() )
        {
            gemObject* obj = i.Next();

            actor = dynamic_cast<gemActor*>(obj);
            if (
                actor &&
                actor->GetCharacterData() &&
                !strcmp( actor->GetCharacterData()->GetCharName(),charname.GetData() )
                )
            {
                found = true;
                break;
            }
        }
    }

    // Fail safe
    if (!actor)
        return 0;

    psCharacter *charData = actor->GetCharacterData();
    if (charData)
    {
        statValue.Downcase();
        float newValue = 0.0;
        if (statValue == "hp" || statValue=="hitpoints")
        {
            newValue = charData->AdjustHitPoints(adjust);
        }
        else if (statValue == "hp_max" || statValue=="hitpoints_max")
        {
            newValue = charData->AdjustHitPointsMax(adjust);
        }
        else if (statValue == "hp_rate" || statValue=="hitpoints_rate")
        {
            newValue = charData->AdjustHitPointsRate(adjust);
        }
        else if (statValue == "mana")
        {
            newValue = charData->AdjustMana(adjust);
        }
        else if (statValue == "mana_max")
        {
            newValue = charData->AdjustManaMax(adjust);
        }
        else if (statValue == "mana_rate")
        {
            newValue = charData->AdjustManaRate(adjust);
        }
        else if (statValue == "pstamina" || statValue == "psta")
        {
            newValue = charData->AdjustStamina(adjust,true);
        }
        else if (statValue == "pstamina_max" || statValue == "psta_max")
        {
            newValue = charData->AdjustStaminaMax(adjust,true);
        }
        else if (statValue == "pstamina_rate" || statValue == "psta_rate")
        {
            newValue = charData->AdjustStaminaRate(adjust,true);
        }
        else if (statValue == "mstamina" || statValue == "msta")
        {
            newValue = charData->AdjustStamina(adjust,false);
        }
        else if (statValue == "mstamina_max" || statValue == "msta_max")
        {
            newValue = charData->AdjustStaminaMax(adjust,false);
        }
        else if (statValue == "mstamina_rate" || statValue == "msta_rate")
        {
            newValue = charData->AdjustStaminaRate(adjust,false);
        }
        else
        {
            CPrintf(CON_CMDOUTPUT ,"Unkown statValue %s\n",statValue.GetData());
            return 0;
        }
        CPrintf(CON_CMDOUTPUT ,"Adjusted %s: %s with %.2f to %.2f\n",
            charname.GetData(),statValue.GetData(),adjust,newValue);
        return 0;
    }

    return 0;
}

int com_liststats(char *line)
{
    if (!line)
    {
        CPrintf(CON_CMDOUTPUT ,"Please specify: PlayerName");
        return 0;
    }
    unsigned int characteruid=psserver->CharacterLoader.FindCharacterID(line,false);
    if (characteruid==0)
    {
        CPrintf(CON_CMDOUTPUT ,"Character name is not found.\n");
        return 0;
    }

    // If the character is online use the active stats.  Otherwise we need to load the character data.
    Client* client = psserver->GetNetManager()->GetConnections()->Find(line);

    // When the instance of this struct is removed as soon as function
    // exits it will delete the temporary chardata.
    struct AutoRemove
    {
        bool temploaded;
        psCharacter *chardata;
        AutoRemove() : temploaded(false), chardata(NULL) { }
        ~AutoRemove() { if (temploaded) delete chardata; }
    };
    AutoRemove chardata_keeper;

    if (!client)
    {
        // Character is not online
        chardata_keeper.chardata=psserver->CharacterLoader.LoadCharacterData(characteruid,true);
        chardata_keeper.temploaded=true;
    }
    else
        chardata_keeper.chardata=client->GetCharacterData();

    if (chardata_keeper.chardata==NULL)
    {
        CPrintf(CON_CMDOUTPUT ,"Could not get character data for specified character.\n");
        return 0;
    }

    psCharacter* charData = chardata_keeper.chardata;
    CPrintf(CON_CMDOUTPUT ,"\nStats for player %s\n",charData->GetCharName());
    CPrintf(CON_CMDOUTPUT     ,"Stat            Current     Max    Rate\n");
    {
        // Only show these stats if character is really loaded.
        CPrintf(CON_CMDOUTPUT ,"HP              %7.1f %7.1f %7.1f\n",charData->AdjustHitPoints(0.0),
        charData->AdjustHitPointsMax(0.0),charData->AdjustHitPointsRate(0.0));
        CPrintf(CON_CMDOUTPUT ,"Mana            %7.1f %7.1f %7.1f\n",charData->AdjustMana(0.0),
        charData->AdjustManaMax(0.0),charData->AdjustManaRate(0.0));
        CPrintf(CON_CMDOUTPUT ,"Physical Stamina%7.1f %7.1f %7.1f\n",charData->GetStamina(true),
        charData->GetStaminaMax(true),charData->AdjustStaminaRate(0.0,true));
        CPrintf(CON_CMDOUTPUT ,"Mental Stamina  %7.1f %7.1f %7.1f\n",charData->GetStamina(false),
        charData->GetStaminaMax(false),charData->AdjustStaminaRate(0.0,false));
    }

    CPrintf(CON_CMDOUTPUT ,"Stat        Base        Buff\n");
    {
    CPrintf(CON_CMDOUTPUT ,"STR        %7.1f\t%7.1f\n",(float)charData->GetAttributes()->GetStat(PSITEMSTATS_STAT_STRENGTH, false), (float)charData->GetAttributes()->GetBuffVal(PSITEMSTATS_STAT_STRENGTH) );
    CPrintf(CON_CMDOUTPUT ,"AGI        %7.1f\t%7.1f\n",(float)charData->GetAttributes()->GetStat(PSITEMSTATS_STAT_AGILITY, false), (float)charData->GetAttributes()->GetBuffVal(PSITEMSTATS_STAT_AGILITY));
    CPrintf(CON_CMDOUTPUT ,"END        %7.1f\t%7.1f\n",(float)charData->GetAttributes()->GetStat(PSITEMSTATS_STAT_ENDURANCE, false), (float)charData->GetAttributes()->GetBuffVal(PSITEMSTATS_STAT_ENDURANCE));
    CPrintf(CON_CMDOUTPUT ,"INT        %7.1f\t%7.1f\n",(float)charData->GetAttributes()->GetStat(PSITEMSTATS_STAT_INTELLIGENCE,false), (float)charData->GetAttributes()->GetBuffVal(PSITEMSTATS_STAT_INTELLIGENCE));
    CPrintf(CON_CMDOUTPUT ,"WIL        %7.1f\t%7.1f\n",(float)charData->GetAttributes()->GetStat(PSITEMSTATS_STAT_WILL,false), (float)charData->GetAttributes()->GetBuffVal(PSITEMSTATS_STAT_WILL));
    CPrintf(CON_CMDOUTPUT ,"CHA        %7.1f\t%7.1f\n",(float)charData->GetAttributes()->GetStat(PSITEMSTATS_STAT_CHARISMA, false),(float)charData->GetAttributes()->GetBuffVal(PSITEMSTATS_STAT_CHARISMA) );
    }

    CPrintf(CON_CMDOUTPUT ,"Experience points(W)  %7d\n",charData->GetExperiencePoints());
    CPrintf(CON_CMDOUTPUT ,"Progression points(X) %7d\n",charData->GetProgressionPoints());
    CPrintf(CON_CMDOUTPUT ,"%-20s %12s %12s %12s\n","Skill","Practice(Z)","Knowledge(Y)","Rank(R)");
    for (int skillID = 0; skillID < (int)PSSKILL_COUNT; skillID++)
    {
        psSkillInfo * info = CacheManager::GetSingleton().GetSkillByID(skillID);
        if (!info)
        {
            Error2("Can't find skill %d",skillID);
            continue;
        }

        unsigned int z = charData->GetSkills()->GetSkillPractice(info->id);
        unsigned int y = charData->GetSkills()->GetSkillKnowledge(info->id);
        unsigned int rank = charData->GetSkills()->GetSkillRank(info->id);


        if ( z == 0 && y == 0 && rank == 0 )
            continue;

        CPrintf(CON_CMDOUTPUT ,"%-20s %12u %12u %12u \n",info->name.GetData(),
            z, y, rank );
    }
    return 0;
}


int com_progress(char * line)
{
    if (!line)
    {
        CPrintf(CON_CMDOUTPUT ,"Please specify: <player>, <event>\n");

        return 0;
    }

    char * event = strchr(line,',');
    if (!event)
    {
        CPrintf(CON_CMDOUTPUT ,"Please specify: <player>, <event>\n");
        return 0;
    }

    *event = '\0';
    event++;
    char * charname = line;

    // Convert to int, if possible
    int clientnum = atoi(charname);

    csHash<gemObject *>& gems = GEMSupervisor::GetSingleton().GetAllGEMS();

    csHash<gemObject *>::GlobalIterator i(gems.GetIterator());
    gemObject* obj;
    gemActor* actor = NULL;
    bool found = false;

    if (clientnum == 0)
    {
        while ( i.HasNext() )
        {
            obj = i.Next();

            if (!strcasecmp(obj->GetName(),charname))
            {
                actor = dynamic_cast<gemActor*>(obj);
                found = true;
                break;
            }
        }
    }
    else
    {
        Client* client = psserver->GetNetManager()->GetClient(clientnum);
        if (!client)
        {
            CPrintf(CON_CMDOUTPUT ,"Player %d not found!\n",clientnum);
            return 0;
        }

        actor = client->GetActor();
        if (!actor)
        {
            CPrintf(CON_CMDOUTPUT ,"Player %s found, but without actor object!\n",client->GetName());
            return 0;
        }

        found = true;
    }

    // Did we find the player?
    if (!found)
    {
        CPrintf(CON_CMDOUTPUT ,"Player %s not found!\n",charname);
        return 0;
    }


    if (actor!=NULL)
    {
        float result;
        // Check if this is a script
        if (event[0] == '<')
        {
            result = psserver->GetProgressionManager()->ProcessScript(event,actor,actor);
        }
        else
        {
            result = psserver->GetProgressionManager()->ProcessEvent(event,actor,actor);
        }
        CPrintf(CON_CMDOUTPUT ,"Result = %.3f\n",result);
        return 0;
    }

    return 0;
}

/** Kills a player right away
 */
int com_kill(char* player)
{
    int clientNum = atoi(player);
    Client* client = psserver->GetNetManager()->GetConnections()->Find(clientNum);
    if (!client)
    {
        CPrintf(CON_CMDOUTPUT ,"Client %d not found!\n",clientNum);
        return 0;
    }
    int playerNum = client->GetActor()->GetEntityID();
    gemActor* object = (gemActor*)GEMSupervisor::GetSingleton().FindObject(playerNum);
    object->Kill(NULL);
    return 0;
}

int com_motd(char* str)
{
    if (!strcmp(str,""))    {
        CPrintf(CON_CMDOUTPUT ,"MOTD: %s\n",psserver->GetMOTD());
    } else {
        psserver->SetMOTD((const char*)str);
    }
    return 0;
}

int com_questreward( char* str )
{
    csString cmd(str);
    WordArray words(cmd);
    
    csString charactername = words[0];
    csString item   = words[1];

    if (charactername.IsEmpty() || item.IsEmpty()) {
        CPrintf(CON_CMDOUTPUT ,"Both char name and item number should be specified.\n");
        return 0;
    }

    unsigned int characteruid=psserver->CharacterLoader.FindCharacterID((char*)charactername.GetData());
    if (characteruid==0)
    {
        CPrintf(CON_CMDOUTPUT ,"Character name not found.\n");
        return 0;
    }

    // Get the ItemStats based on the name provided.
    psItemStats *itemstats=CacheManager::GetSingleton().GetBasicItemStatsByID(atoi(item.GetData()) );
    if (itemstats==NULL)
    {
        CPrintf(CON_CMDOUTPUT ,"No Basic Item Template with that id was found.\n");
        return 0;
    }

    // If the character is online, update the stats live.  Otherwise we need to load the character data to add this item to
    //  an appropriate inventory slot.
    psCharacter *chardata=NULL;
    Client* client = psserver->GetNetManager()->GetConnections()->Find(charactername.GetData());
    csArray<psItemStats*> items;
    items.Push( itemstats );
    
    if (!client)
    {
        // Character is not online
        chardata=psserver->CharacterLoader.LoadCharacterData(characteruid,true);        
        return 0;
    }
    else
        chardata=client->GetCharacterData();

    if (chardata==NULL)
    {
        CPrintf(CON_CMDOUTPUT ,"Could not get character data for specified character.\n");
        return 0;
    }

    csTicks timeDelay=0;
    psserver->questmanager->OfferRewardsToPlayer(client, items, timeDelay);
    return 0;
}


int com_transactions(char* str)
{
    csString cmd(str);

    if(!cmd.Length())
    {
        CPrintf(CON_CMDOUTPUT,"Transaction actions:\n");
        CPrintf(CON_CMDOUTPUT,"DUMP <TIMESTAMP FROM> <TIMESTAMP TO> - Dumps the transaction history to the console\n");
        CPrintf(CON_CMDOUTPUT,"ERASE - Empties the transaction history\n");
        return 0;
    }

    cmd.Upcase();
    WordArray words(cmd);
    EconomyManager* economy = psserver->GetEconomyManager();

    if(words[0] == "DUMP")
    {
        for(unsigned int i = 0; i< economy->GetTotalTransactions();i++)
        {
            TransactionEntity* trans = economy->GetTransaction(i);
            if(trans)
            {
                // Dump it
                CPrintf(
                    CON_CMDOUTPUT,
                    "%s transaction for %d %d (Quality %d) (%d => %d) with price %u/ (%d)\n",
                    trans->moneyIn?"Selling":" Buying",
                    trans->count,
                    trans->item,
                    trans->quality,
                    trans->from,
                    trans->to,
                    trans->price,
                    trans->stamp);
            }
        }
    }
    else if(words[0] == "ERASE")
    {
        economy->ClearTransactions();
        CPrintf(CON_CMDOUTPUT,"Cleared transactions\n");
    }
    else
        CPrintf(CON_CMDOUTPUT,"Unknown action\n");

    return 0;
}

int com_allocations(char* str)
{
    CS::Debug::DumpAllocateMemoryBlocks();
    CPrintf(CON_CMDOUTPUT,"Dumped.\n");
    
    return 0;
}

int com_randomloot( char* loot )
{
    if (strlen(loot) == 0)
    {
        CPrintf(CON_CMDOUTPUT, "Error. Syntax = randomloot \"<item name>\" <#modifiers: 0-3>\n");
        return 0;
    }

    WordArray words(loot, false);
    csString baseItemName = words[0];
    int numModifiers = atoi(words[1]);

    if (numModifiers < 0 || numModifiers > 3)
    {
        numModifiers = 0;
        CPrintf(CON_CMDOUTPUT, "Number of modifiers out of range 0-3. Default = 0\n");
    }
    
    LootEntrySet* testLootEntrySet = new LootEntrySet(1);
    LootEntry* testEntry = new LootEntry;
    if (testLootEntrySet && testEntry)
    {
        // get the base item stats
        testEntry->item = CacheManager::GetSingleton().GetBasicItemStatsByName(baseItemName);
        if (testEntry->item)
        {
            // make copy of item to randomize
            testEntry->item = new psItemStats(*testEntry->item);
            testEntry->probability = 1.0;   // want a dead cert for testing!
            testEntry->min_money = 0;       // ignore money
            testEntry->max_money = 0;
            testEntry->randomize = true;

            // add loot entry into set
            testLootEntrySet->AddLootEntry(testEntry);

            // generate loot from base item 
            testLootEntrySet->SetRandomizer(psserver->GetSpawnManager()->GetLootRandomizer());
            testLootEntrySet->CreateLoot(NULL, numModifiers);

            delete testEntry->item;
        }
        else
            CPrintf(CON_CMDOUTPUT, "\'%s\' not found.\n",
                    baseItemName.GetDataSafe());
	
        delete testLootEntrySet;
    }
    else
        CPrintf(CON_CMDOUTPUT, "Could not create LootEntrySet/LootEntry instance.\n");

    return 0;
}

/* add all new commands here */
const COMMAND commands[] = {

  // Server commands
    { "-- Server commands",  true, NULL, "------------------------------------------------" },
    { "dbprofile",  true, com_dbprofile, "shows database profile info" },
    { "exec",      true, com_exec,      "Executes a script file" },
    { "help",      true, com_help,      "Show help information" },
    { "kick",      true, com_kick,      "Kick player from the server"},
    { "loadmap",   true, com_loadmap,   "Loads a map into the server"},
    { "lock",      false, com_lock,      "Tells server to stop accepting connections"},
    { "maplist",   true, com_maplist,   "List all mounted maps"},
    { "dumpwarpspace",   true, com_dumpwarpspace,   "Dump the warp space table"},
    { "netprofile", true, com_netprofile, "shows network profile info" },
    { "quit",      true, com_quit,      "[minutes] Makes the server exit immediately or after the specified amount of minutes"},
    { "ready",     false, com_ready,     "Tells server to start accepting connections"},
    { "sectors",   true, com_sectors,   "Display all sectors" },
    { "set",       true, com_set,       "Sets a server variable"},
    { "setlog",    true, com_setlog,    "Set server log" },
    { "setmaxfile",false, com_setmaxfile,"Set maximum message class for output file"},
    { "setmaxout", false, com_setmaxout, "Set maximum message class for standard output"},
    { "settime",   true, com_settime,    "Sets the current server hour using a 24 hour clock" },
    { "showtime",  true, com_showtime,   "Show the current time" },
    { "showlogs",  true, com_showlogs,  "Show server logs" },
    { "spawn",     false, com_spawn,     "Loads a map into the server"},
    { "status",    true, com_status,    "Show server status"},
    { "transactions", false, com_transactions, "Performs an action on the transaction history (run without parameters for options)" },
    { "dumpallocations", true, com_allocations, "Dump all allocations to allocations.txt if CS extensive memdebug is enabled" },

    // npc commands
    { "-- NPC commands",  true, NULL, "------------------------------------------------" },
    { "exportdialogs", true, com_exportdialogs, "Loads NPC dialogs from the DB and stores them in a XML file"},
    { "exportnpc", true, com_exportnpc, "Loads NPC data from the DB and stores it in a XML file"},
    { "importdialogs", true, com_importdialogs, "Loads NPC dialogs from a XML file or a directory and inserts them into the DB"},
    { "importnpc", true, com_importnpc, "Loads NPC data from a XML file or a directory and inserts it into the DB"},
    { "loadnpc",   true, com_loadnpc,   "Loads/Reloads an NPC from the DB into the world"},
    { "loadquest", true, com_loadquest, "Loads/Reloads a quest from the DB into the world"},
    { "newacct",   true, com_newacct,   "Create a new account: newacct <user/passwd>" },
//  { "newguild",  com_newguild,  "Create a new guild: newguild <name/leader>" },
//  { "joinguild", com_joinguild, "Attach player to guild: joinguild <guild/player>" },
//  { "quitguild", com_quitguild, "Detach player from guild: quitguild <player>" },

    // player-entities commands

    { "-- Player commands",  true, NULL, "------------------------------------------------" },
    { "addinv",    true, com_addinv,    "Add an item to a player's inventory" },
    { "adjuststat",true, com_adjuststat,"Adjust player stat [HP|HP_max|HP_rate|Mana|Mana_max|Mana_rate|Stamina|Stamina_max|Stamina_rate]" },
    { "charlist",  true, com_charlist,  "List all known characters" },
    { "delete",    false, com_delete,    "Delete a player from the database"},
    { "dict",      true, com_dict,      "Dump the NPC dictionary"},
    { "kill",      true, com_kill,      "kill <playerID> Kills a player" },
    { "progress",  true, com_progress,  "progress <player>,<event/script>" },
    { "questreward", true, com_questreward, "Preforms the same action as when a player gets a quest reward" },
    { "say",       true, com_say,       "Tell something to all players connected"},
    { "showinv",   true, com_showinv,   "Show items in a player's inventory" },
    { "showinvf",  true, com_showinvf,  "Show items in a player's inventory (more item information)" },

    // various commands
    { "-- Various commands",  true, NULL, "------------------------------------------------" },
    { "entlist",   true, com_entlist,   "List all known entities" },
    { "factions",  true, com_factions,  "Display factions" },
    { "filtermsg", true, com_filtermsg, "Add or remove messages from the LOG_MESSAGE log"},
    { "liststats", true, com_liststats, "List player stats" },
    { "motd",      true, com_motd,      "motd <msg> Sets the MOTD" },
    { "print",     true, com_print,     "Displays debug data about a specified entity" },
    { "rain",      true, com_rain,      "Forces it to start or stop raining in a sector"},
    { "randomloot",false,com_randomloot,"Generates random loot"},
    { 0, 0, 0, 0 }
};


