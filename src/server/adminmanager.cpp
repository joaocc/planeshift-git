/*
* adminmanager.cpp
*
* Copyright (C) 2001-2005 Atomic Blue (info@planeshift.it, http://www.atomicblue.org)
* http://www.atomicblue.org )
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
#include <iutil/object.h>
#include <iutil/stringarray.h>
#include <iengine/campos.h>
#include <iengine/region.h>
#include <propclass/linmove.h>
#include <iengine/mesh.h>
#include <iengine/movable.h>

//=============================================================================
// Library Includes
//=============================================================================
#include "util/psdatabase.h"
#include "util/log.h"
#include "util/serverconsole.h"
#include "util/strutil.h"
#include "util/eventmanager.h"
#include "util/pspathnetwork.h"
#include "util/waypoint.h"
#include "util/pspath.h"

#include "net/msghandler.h"

#include "bulkobjects/psnpcdialog.h"
#include "bulkobjects/psnpcloader.h"
#include "bulkobjects/pscharacterloader.h"
#include "bulkobjects/psraceinfo.h"
#include "bulkobjects/psmerchantinfo.h"
#include "bulkobjects/psactionlocationinfo.h"
#include "bulkobjects/psitem.h"
#include "bulkobjects/pssectorinfo.h"
#include "bulkobjects/pstrait.h"

#include "rpgrules/factions.h"

//=============================================================================
// Application Includes
//=============================================================================
#include "globals.h"
#include "adminmanager.h"
#include "spawnmanager.h"
#include "chatmanager.h"
#include "marriagemanager.h"
#include "gem.h"
#include "clients.h"
#include "playergroup.h"
#include "entitymanager.h"
#include "psserver.h"
#include "usermanager.h"
#include "cachemanager.h"
#include "combatmanager.h"
#include "netmanager.h"
#include "npcmanager.h"
#include "psserverchar.h"
#include "questmanager.h"
#include "creationmanager.h"
#include "guildmanager.h"
#include "weathermanager.h"
#include "commandmanager.h"
#include "authentserver.h"
#include "gmeventmanager.h"
#include "actionmanager.h"
#include "progressionmanager.h"


// Show only items up to this ID when using the item spawn GUI (hide randomly generated items with IDs set above this)
#define SPAWN_ITEM_ID_CEILING 10000
// Define maximum value for awarded experience
#define MAXIMUM_EXP_CHANGE 100
//-----------------------------------------------------------------------------

AdminManager::AdminManager()
{
    clients = psserver->GetNetManager()->GetConnections();

    psserver->GetEventManager()->Subscribe(this,MSGTYPE_ADMINCMD,REQUIRE_READY_CLIENT);
    psserver->GetEventManager()->Subscribe(this,MSGTYPE_PETITION_REQUEST,REQUIRE_READY_CLIENT);
    psserver->GetEventManager()->Subscribe(this,MSGTYPE_GMGUI,REQUIRE_READY_CLIENT);
    psserver->GetEventManager()->Subscribe(this,MSGTYPE_GMSPAWNITEMS,REQUIRE_READY_CLIENT);
    psserver->GetEventManager()->Subscribe(this,MSGTYPE_GMSPAWNITEM,REQUIRE_READY_CLIENT);

    // this makes sure that the player dictionary exists on start up.
    npcdlg = new psNPCDialog(NULL);
    npcdlg->Initialize( db );

    pathNetwork = new psPathNetwork();
    pathNetwork->Load(EntityManager::GetSingleton().GetEngine(),db,
                      EntityManager::GetSingleton().GetWorld());
}


AdminManager::~AdminManager()
{
    psserver->GetEventManager()->Unsubscribe(this,MSGTYPE_ADMINCMD);
    psserver->GetEventManager()->Unsubscribe(this,MSGTYPE_PETITION_REQUEST);
    psserver->GetEventManager()->Unsubscribe(this,MSGTYPE_GMGUI);
    psserver->GetEventManager()->Unsubscribe(this,MSGTYPE_GMSPAWNITEMS);
    psserver->GetEventManager()->Unsubscribe(this,MSGTYPE_GMSPAWNITEM);

    delete npcdlg;
}


void AdminManager::HandleMessage(MsgEntry *me, Client *client)
{
    switch ( me->GetType() )
    {
        case MSGTYPE_ADMINCMD:
        {
            AdminCmdData data;

            psAdminCmdMessage msg(me);

            // Decode the string from the message into a struct with data elements
            if (!data.DecodeAdminCmdMessage(me,msg,client))
            {
                psserver->SendSystemInfo(me->clientnum,"Invalid admin command");
                return;
            }

            HandleAdminCmdMessage(me, msg, data, client);
            break;
        }
        
        case MSGTYPE_PETITION_REQUEST:
        {
            psPetitionRequestMessage msg(me);
            HandlePetitionMessage(me, msg, client);
            break;
        }
        
        case MSGTYPE_GMGUI:
        {
            psGMGuiMessage msg(me);
            HandleGMGuiMessage(me, msg, client);
            break;
        }
        
        case MSGTYPE_GMSPAWNITEMS:
        {
            psGMSpawnItems msg(me);
            SendSpawnItems(me, msg, client);
            break;
        }
        
        case MSGTYPE_GMSPAWNITEM:
        {
            psGMSpawnItem msg(me);
            SpawnItemInv(me, msg, client);
            break;
        }
    }
}



bool AdminManager::IsReseting(const csString& command)
{
    // Grab the first 8 characters after the command and see if we're resetting ourself
    // Everyone is allowed to reset themself via /deputize (should probably use this for other commands, too)
    return command.Slice(command.FindFirst(' ')+1,8) == "me reset";
}

bool AdminManager::AdminCmdData::DecodeAdminCmdMessage(MsgEntry *pMsg, psAdminCmdMessage& msg, Client *client)
{
    WordArray words (msg.cmd, false);

    command = words[0];
    help = false;
    
    if ( command == "/updaterespawn" )
    {
        return true;
    } 
    else if (command == "/deletechar")
    {
        zombie = words[1];
        requestor = words[2];
        return true;
    }
    else if (command == "/banname" ||
             command == "/unbanname" ||
             command == "/freeze" ||
             command == "/thaw" ||
             command == "/mute" ||
             command == "/unmute" ||
             command == "/unban" ||
             command == "/advisor_ban" ||
             command == "/advisor_unban" ||
             command == "/death" ||
             command == "/info" ||
             command == "/charlist" ||
             command == "/inspect" ||
             command == "/npc")
    {
        player = words[1];
        return true;
    }
    else if (command == "/warn" ||
             command == "/kick")
    {
        player = words[1];
        reason = words.GetTail(2);
        return true;
    }
    else if (command == "/ban")
    {
        player = words[1];
        mins   = words.GetInt(2);
        hours  = words.GetInt(3);
        days   = words.GetInt(4);

        if (!mins && !hours && !days)
        {
            reason = words.GetTail(2);
        }
        else
        {
            reason = words.GetTail(5);
        }

        return true;
    }
    else if (command == "/killnpc")
    {
        target = (words[1]=="reload")?"":words[1];
        action = words[words.GetCount()-1];
        return true;
    }
    else if (command == "/loadquest")
    {
        text = words[1];        
        return true;
    }
    else if (command == "/item")
    {
        item = words[1];
        random = 0;
        value = -1;
        
        if (words.GetCount()>2)
        {
            if (words[2]=="random")
            {
                random = 1;
            }
            else
            {
                value = words.GetInt(2);
            }
        }
        if (words.GetCount()>3)
        {
            value = words.GetInt(3);
        }
        return true;
    }
    else if (command == "/key")
    {
        subCmd = words[1];
        return true;
    }
    else if (command == "/crystal")
    {
        interval = words.GetInt(1);
        random   = words.GetInt(2);
        item = words.GetTail(3);
        return true;
    }
    else if (command == "/teleport")
    {
        player = words[1];
        target = words[2];

        instance = 0;
        instanceValid = false;

        if (target == "map")
        {
            if (words.GetCount() == 4)
            {
                map = words[3];
            }
            else if (words.GetCount() >= 7)
            {
                sector = words[3];

                x = words.GetFloat(4);
                y = words.GetFloat(5);
                z = words.GetFloat(6);
                if (words.GetCount() == 8)
                {
                    instance = words.GetInt(7);
                    instanceValid = true;
                }
            }
        } else if (target == "here")
        {
            if (words.GetCount() >= 4)
            {
                instance = words.GetInt(3);
                instanceValid = true;
            }
        }
        
        return true;
    }
    else if (command == "/slide")
    {
        player = words[1];
        direction = words[2];
        amt = words.GetFloat(3);
        return true;
    }
    else if (command == "/changename")
    {
        player = words[1];

        if (words[2] == "no")
            uniqueName = false;
        else
            uniqueName = true;

        newName = words[3];
        newLastName = words[4];

        return true;
    }
    else if (command == "/changeguildname")
    {
        target = words[1];
        newName = words.GetTail(2);
        return true;
    }
    else if (command == "/petition")
    {
        petition = words.GetTail(1);
        return true;
    }
    else if (command == "/impersonate")
    {
        player = words[1];
        commandMod = words[2];
        text = words.GetTail(3);
        return true;
    }
    else if (command == "/deputize")
    {
        player = words[1];
        setting = words[2];
        return true;
    }
    else if (command == "/divorce" ||
             command == "/marriageinfo")
    {
        player = words[1];
        return true;
    }
    else if (command == "/awardexp")
    {
        player = words[1];
        value = words.GetInt(2);
        return true;
    }
    else if (command == "/giveitem" ||
             command == "/takeitem")
    {
        player = words[1];
        value = words.GetInt(2);
        if (value)
        {
            item = words.GetTail(3);
        }
        else if (words[2] == "all")
        {
            value = -1;
            item = words.GetTail(3);
        }
        else
        {
            value = 1;
            item = words.GetTail(2);
        }
        return true;
    }
    else if (command == "/thunder")
    {
        sector = words[1];
        return true;
    }
    else if (command == "/weather")
    {
        if(words.GetCount() == 3)
        {
            sector = words[1];

            if (words[2] == "on")
            {
                interval = -1; // Code used by admin manager to turn on automatic weather
            }
            else if (words[2] == "off")
            {
                interval = -2; // Code used by admin manager to turn off automatic weather
            }
            else
            {
                sector = "";
            }
        }
        return true;
    }
    else if (command == "/rain" ||
             command == "/snow")
    {
        // Defaults
        rainDrops = 4000;   // 50% of max
        interval = 600000;  // 10 min
        fade = 10000;       // 10 sec

        if( words.GetCount() > 1 )
        {
            sector = words[1];
        }
        if ( words.GetCount() == 3 )
        {
            if (words[2] == "stop")
            {
                interval = -3; // Code used for stopping normal weather
            }
            else
            {
                sector = "";
                return true;
            }
        }
        if (words.GetCount() == 5)
        {
           rainDrops = words.GetInt(2);
           interval = words.GetInt(3);
           fade = words.GetInt(4);
        }
        else
        {
            /*If the arguments of the commands are not all written or don't belong to a subcategory (stop/start/off)
              then the sector is reset, so that adminmanager will show the syntax of the command.*/
            if (interval > -1 && words.GetCount() != 2)
            {
               sector = "";
            }
        }

        return true;
    }
    else if (command == "/fog")
    {
        // Defaults
        density = 200;    // Density
        fade = 10000;     // 10 sec
        x = y = z = 200;  // Light gray

        if ( words.GetCount() > 1 )
        {
            sector = words[1];
        }
        if ( words.GetCount() == 3 )
        {
            if ( words[2] == "stop" || words[2] == "-1" ) //This turns off the fog.
            {
                density = -1;
                return true;
            }
            else
            {
                sector = "";
                return true;
            }
        }
        if ( words.GetCount() == 7 )
        {
            density = words.GetInt(2);
            x = (float)words.GetInt(3);
            y = (float)words.GetInt(4);
            z = (float)words.GetInt(5);
            fade = words.GetInt(6);
        }
        else
        {
            /*If the arguments of the commands are not all written or don't belong to a subcategory (off)
              then the sector is reset, so that adminmanager will show the syntax of the command.*/
            if ( words.GetCount() != 2 )
            {
               sector = "";
            }
        }
        return true;
    }
    else if (command == "/modify")
    {
        // Not really a "player", but this is the var used for targeting
        player = words[1];

        action = words[2];
        if (action == "intervals")
        {
            interval = words.GetInt(3);
            random = words.GetInt(4);
            return true;
        }
        else if (action == "move")
        {
            if (words.GetCount() >= 6)
            {
                // If rot wasn't specified (6 words), then words.GetFloat(6) will be 0 (atof behavior).
                x = words.GetFloat(3);
                y = words.GetFloat(4);
                z = words.GetFloat(5);
                rot = words.GetFloat(6);
                return true;
            }
        }
        else
        {
            setting = words[3];
            return true;
        }
    }
    else if (command == "/morph")
    {
        player = words[1];
        mesh = words[2];
        return true;
    }
    else if (command == "/setskill")
    {
        player = words[1];
        skill = words[2];
        if (words.GetCount() >= 4)
            value = words.GetInt(3);
        else
            value = 100;
        return true;
    }
    else if (command == "/set")
    {
        attribute = words[1];
        setting = words[2];
        return true;
    }
    else if (command == "/setlabelcolor")
    {
        player = words[1];
        setting = words[2];
        return true;
    }
    else if (command == "/action")
    {
        subCmd = words[1];
        if (subCmd == "create_entrance")
        {
            sector = words[2];
            name = words[3];
            description = words[4];
        }
        else
        {
            subCmd = "help"; //Unknown command so force help on waypoint.
            help = true;
        }
        return true;
    }
    else if (command == "/path")
    {
        float defaultRadius = 2.0;
        subCmd = words[1];
        if (subCmd == "adjust")
        {
            radius = words.GetFloat(2);
            if (radius == 0.0)
            {
                radius = defaultRadius;
            }
        }
        else if (subCmd == "alias")
        {
            wp1 = words[2];    // Format
            if (wp1 == "")
            {
                help = true;
            }
        }
        else if (subCmd == "display" || subCmd == "show")
        {
            // Show is only an alias so make sure subCmd is display
            subCmd = "display";
            attribute = words[2];
            if (attribute != "" && !(toupper(attribute.GetAt(0))=='P'||toupper(attribute.GetAt(0))=='W'))
            {
                help = true;
            }
        }
        else if (subCmd == "format")
        {
            attribute = words[2];    // Format
            if (attribute == "")
            {
                help = true;
            }
            value = words.GetInt(3); // First
        }
        else if (subCmd == "hide")
        {
            attribute = words[2];
            if (attribute != "" && !(toupper(attribute.GetAt(0))=='P'||toupper(attribute.GetAt(0))=='W'))
            {
                help = true;
            }
        }
        else if (subCmd == "info")
        {
            radius = words.GetFloat(2);
            if (radius == 0.0)
            {
                radius = defaultRadius;
            }
        }
        else if (subCmd == "point")
        {
            // No params
        }
        else if (subCmd == "start")
        {
            radius = words.GetFloat(2);
            if (radius == 0.0)
            {
                radius = defaultRadius;
            }
            attribute = words[3]; // Waypoint flags
            attribute2 = words[4]; // Path flags
        }
        else if (subCmd == "stop" || subCmd == "end")
        {
            subCmd = "stop";
            radius = words.GetFloat(2);
            if (radius == 0.0)
            {
                radius = defaultRadius;
            }
            attribute = words[3]; // Waypoint flags
        }
        else if (subCmd == "select")
        {
            radius = words.GetFloat(2);
            if (radius == 0.0)
            {
                radius = defaultRadius;
            }
        }
        else if (subCmd == "split")
        {
            radius = words.GetFloat(2);
            if (radius == 0.0)
            {
                radius = defaultRadius;
            }
            attribute = words[3]; // Waypoint flags
        }
        else if (subCmd == "help")
        {
            help = true;
            subCmd = words[2]; // This might be help on a specefix command
        }
        else
        {
            subCmd = "help"; //Unknown command so force help on path. 
            help = true;
        }
        return true;
    }
    else if (command == "/location")
    {
        subCmd = words[1];
        if (subCmd == "add" && words.GetCount() == 3)
        {
            type = words[2];
            name = words[3];
        }
        else if (subCmd == "adjust")
        {
            // No params
        }
        else if (subCmd == "display")
        {
            // No params
        }
        else if (subCmd == "show") // Alias for dislay
        {
            subCmd = "display";
        }
        else if (subCmd == "hide")
        {
            // No params
        }
        else
        {
            subCmd = "help"; //Unknown command so force help on cmd. 
            help = true;
        }
        return true;
    }
    else if (command == "/event")
    {
        subCmd = words[1];
        if (subCmd == "create")
        {
            gmeventName = words[2];
            gmeventDesc = words.GetTail(3);
        }
        else if (subCmd == "register")
        {
            /// 'register' expects either 'range' numeric value or a player name.
            if (words[2] == "range")
            {
                player.Empty();
                range = words.GetFloat(3);
                rangeSpecifier = IN_RANGE;
            }
            else
            {
                player = words[2];
                rangeSpecifier = INDIVIDUAL;
            }
        }
        else if (subCmd == "reward")
        {
            // "/event reward [range # | all | [player_name]] <#> item"
            int rewardIndex = 3;
            stackCount = 0;

            if (strspn(words[2].GetDataSafe(), "-0123456789") == words[2].Length())
            {
                commandMod.Empty();
                stackCount = words.GetInt(2);
                rangeSpecifier = INDIVIDUAL;  // expecting a player by target
            }
            else
            {
                commandMod = words[2];    // 'range' or 'all'
                range = 0;
                if (commandMod == "range")
                {
                    rangeSpecifier = IN_RANGE;
                    if (strspn(words[3].GetDataSafe(), "0123456789.") == words[3].Length())
                    {
                        range = words.GetFloat(3);
                        rewardIndex = 4;
                    }
                }
                else if (commandMod == "all")
                {
                    rangeSpecifier = ALL;
                }
                else
                {
                    rangeSpecifier = INDIVIDUAL;
                    player = words[2];
                    commandMod.Empty();
                }

                // next 'word' should be numeric: number of items.
                if (strspn(words[rewardIndex].GetDataSafe(), "-0123456789") == words[rewardIndex].Length())
                {
                    stackCount = words.GetInt(rewardIndex);
                    rewardIndex++;
                }
                else
                {
                    subCmd = "help";
                    help = true;
                    return true;
                }
            }

            // last bit is the item name itself!
            item = words.GetTail(rewardIndex);
        }
        else if (subCmd == "remove")
        {
            player = words[2];
        }
        else if (subCmd == "complete")
        {
            name = words.Get(2);
        }
        else if (subCmd == "list")
        {
            // No params
        }
        else if (subCmd == "control")
        {
            gmeventName = words[2];
        }
        else
        {
            subCmd = "help"; // unknown command so force help on event
            help = true;
        }
        return true;
    }
    else if (command == "/badtext") // syntax is /badtext 1 10 for the last 10 bad text lines
    {
        value = atoi(words[1]);
        interval = atoi(words[2]);
        if (value && interval > value)
        {
            return true;
        }
    }
    else if (command == "/quest")
    {
        if (words.GetCount() == 1)
        {
            value = 2;
        }
        else if (words.GetCount() == 3)
        {
            value = words.GetInt(1);
            text = words[2];
        }
        return true;
    }
    else if (command == "/setquality")
    {
        x = words.GetFloat(1);
        y = words.GetFloat(2);
        return true;
    }
    else if (command == "/settrait")
    {
        player = words.Get(1);
        name = words.Get(2);
        return true;
    }
    else if (command == "/setitemname")
    {
        name = words.Get(1);
        description = words.Get(2);
        return true;
    }
    else if (command == "/reload")
    {
        subCmd = words.Get(1);
        if (subCmd == "item")
            value = words.GetInt(2);
        
        return true;
    }
    else if (command == "/listwarnings")
    {
        target = words.Get(1);
        return true;
    }
    return false;
}


void AdminManager::HandleAdminCmdMessage(MsgEntry *me, psAdminCmdMessage &msg, AdminCmdData &data, Client *client)
{

    // Security check
    if ( me->clientnum != 0 && !IsReseting(msg.cmd) && !Valid(client->GetSecurityLevel(), data.command,  me->clientnum) )
    {
        psserver->SendSystemError(me->clientnum, "You are not allowed to use %s.", data.command.GetData());
        return;       
    }
    
    // Called functions should report all needed errors
    gemObject* targetobject = NULL;
    gemActor* targetactor = NULL;
    Client* targetclient = NULL;

    // Targeting for all commands
    if ( data.player.Length() > 0 )
    {
        if (data.player == "target")
        {
            targetobject = client->GetTargetObject();
            if (!targetobject)
            {
                psserver->SendSystemError(client->GetClientNum(), "You must have a target selected.");
                return;
            }
        }
        else if (data.player.StartsWith("area:",true))
        {
            int range = atoi( data.player.Slice(data.player.FindLast(':')+1).GetDataSafe() );
            CommandArea(me, msg, data, client, range);
            return;  // Done.
        }
        else
        {
            if (data.player == "me")
            {
                targetclient = client; // Self
            }
            else
            {
                targetclient = FindPlayerClient(data.player); // Other player?
            }

            if (targetclient) // Found client
            {
                targetactor = targetclient->GetActor();
                targetobject = (gemObject*)targetactor;
            }
            else // Not found yet
                targetobject = FindObjectByString(data.player); // Find by ID or name
        }
    }
    else // Only command specified; just get the target
    {
        targetobject = client->GetTargetObject();
    }

    if (targetobject && !targetactor) // Get the actor, client, and name for a found object
    {
        targetactor = targetobject->GetActorPtr();
        targetclient = targetobject->GetClient();
        data.player = (targetclient)?targetclient->GetName():targetobject->GetName();
    }

    // Sector finding for all commands
    if ( data.sector == "here" )
    {
        iSector* here = NULL;
        if (client->GetActor())
        {
            here = client->GetActor()->GetSector();
        }

        if (here)
        {
            data.sector = here->QueryObject()->GetName();
        }
        else
        {
            data.sector.Clear();  // Bad sector
        }
    }

    int targetID = 0;
    if (targetobject)
        targetID = targetobject->GetPlayerID();

    if (me->clientnum)
        LogGMCommand( client->GetPlayerID(), targetID, msg.cmd );
    
    if (data.command == "/npc")
    {
        CreateNPC(me,msg,data,client,targetactor);
    }
    else if (data.command == "/killnpc")
    {
        KillNPC(me,msg,data,client);
    }
    else if (data.command == "/item")
    {
        if (data.item.Length())  // If arg, make simple
        {
            CreateItem(me,msg,data,client);
        }
        else  // If no arg, load up the spawn item GUI
        {
            SendSpawnTypes(me,msg,data,client);
        }
    }
    else if (data.command == "/key")
    {
        ModifyKey(me,msg,data,client);
    }
    else if (data.command == "/weather")
    {
        Weather(me,msg,data,client);
    }
    else if (data.command == "/rain")
    {
        Rain(me,msg,data,client);
    }
    else if (data.command == "/snow")
    {
        Snow(me,msg,data,client);
    }
    else if (data.command == "/thunder")
    {
        Thunder(me,msg,data,client);
    }
    else if (data.command == "/fog")
    {
        Fog(me,msg,data,client);
    }
    else if (data.command == "/info")
    {
        GetInfo(me,msg,data,client,targetobject);
    }
    else if (data.command == "/charlist")
    {
        GetSiblingChars(me,msg,data,client);
    }
    else if (data.command == "/crystal")
    {
        CreateHuntLocation(me,msg,data,client);
    }
    else if (data.command == "/mute")
    {
        MutePlayer(me,msg,data,client,targetclient);
    }
    else if (data.command == "/unmute")
    {
        UnmutePlayer(me,msg,data,client,targetclient);
    }
    else if (data.command == "/teleport")
    {
        Teleport(me,msg,data,client,targetobject);
    }
    else if (data.command == "/slide")
    {
        Slide(me,msg,data,client,targetobject);
    }
    else if (data.command == "/petition")
    {
        HandleAddPetition(me, msg, data, client);
    }
    else if (data.command == "/warn")
    {
        WarnMessage(me, msg, data, client, targetclient);
    }
    else if (data.command == "/kick")
    {
        KickPlayer(me, msg, data, client, targetclient);
    }
    else if (data.command == "/death" )
    {
        Death(me, msg, data, client, targetactor);
    }
    else if (data.command == "/impersonate" )
    {
        Impersonate(me, msg, data, client);
    }
    else if (data.command == "/deputize")
    {
        TempSecurityLevel(me, msg, data, client, targetclient);
    }
    else if (data.command == "/deletechar" )
    {
        DeleteCharacter( me, msg, data, client );
    }
    else if (data.command == "/changename" )
    {
        ChangeName( me, msg, data, client );
    }
    else if (data.command == "/changeguildname")
    {
        RenameGuild( me, msg, data, client );
    }
    else if (data.command == "/banname" )
    {
        BanName( me, msg, data, client );
    }
    else if (data.command == "/unbanname" )
    {
        UnBanName( me, msg, data, client );
    }
    else if (data.command == "/ban" )
    {
        BanClient( me, msg, data, client );
    }
    else if (data.command == "/unban" )
    {
        UnbanClient( me, msg, data, client );
    }
    else if (data.command == "/awardexp" )
    {
        AwardExperience(me, msg, data, client, targetclient);
    }
    else if (data.command == "/giveitem" )
    {
        TransferItem(me, msg, data, client, targetclient);
    }
    else if (data.command == "/takeitem" )
    {
        TransferItem(me, msg, data, targetclient, client);
    }
    else if (data.command == "/freeze")
    {
        FreezeClient(me, msg, data, client, targetclient);
    }
    else if (data.command == "/thaw")
    {
        ThawClient(me, msg, data, client, targetclient);
    }
    else if (data.command == "/inspect")
    {
        Inspect(me, msg, data, client, targetactor);
    }
    else if ( data.command == "/updaterespawn")
    {
        UpdateRespawn(client, targetactor);        
    }
    else if (data.command == "/modify")
    {
        ModifyHuntLocation(me, msg, data, client, targetobject);
    }
    else if (data.command == "/morph")
    {
        Morph(me, msg, data, client, targetclient);
    }
    else if (data.command == "/setskill")
    {
        SetSkill(me, msg, data, client, targetclient);
    }
    else if (data.command == "/set")
    {
        SetAttrib(me, msg, data, client);
    }
    else if (data.command == "/setlabelcolor")
    {
        SetLabelColor(me, msg, data, client, targetactor);
    }
    else if (data.command == "/divorce")
    {
        Divorce(me, data);
    }
    else if (data.command == "/marriageinfo")
    {
        ViewMarriage(me, data);
    }
    else if (data.command == "/path")
    {
        HandlePath(me, msg, data, client);
    }
    else if (data.command == "/location")
    {
        HandleLocation(me, msg, data, client);
    }
    else if (data.command == "/action")
    {
        HandleActionLocation(me, msg, data, client);
    }
    else if (data.command == "/event")
    {
        HandleGMEvent(me, msg, data, client, targetclient);
    }
    else if (data.command == "/badtext")
    {
        HandleBadText(msg, data, client, targetobject);
    }
    else if ( data.command == "/loadquest" )
    {
        HandleLoadQuest(msg, data, client);
    }
    else if (data.command == "/quest")
    {
        HandleCompleteQuest(me, msg, data, client, targetclient);
    }
    else if (data.command == "/setquality")
    {
        HandleSetQuality(msg, data, client, targetobject);
    }
    else if (data.command == "/settrait")
    {
        HandleSetTrait(msg, data, client, targetobject);
    }
    else if (data.command == "/setitemname")
    {
        HandleSetItemName(msg, data, client, targetobject);
    }
    else if (data.command == "/reload")
    {
        HandleReload(msg, data, client, targetobject);
    }
    else if (data.command == "/listwarnings")
    {
        HandleListWarnings(msg, data, client, targetobject);
    }
}

void AdminManager::HandleLoadQuest(psAdminCmdMessage& msg, AdminCmdData& data, Client* client)
{
    uint32 questID = (uint32)-1;
    
    Result result(db->Select("select * from quests where name='%s'", data.text.GetData()));    
    if (!result.IsValid() || result.Count() == 0)
    {
        psserver->SendSystemError(client->GetClientNum(), "Quest <%s> not found", data.text.GetData());        
        return;
    }
    else
    {
        questID = result[0].GetInt("id");
    }
        
    if(!CacheManager::GetSingleton().UnloadQuest(questID))
        psserver->SendSystemError(client->GetClientNum(), "Quest <%s> Could not be unloaded", data.text.GetData());                
    else
        psserver->SendSystemError(client->GetClientNum(), "Quest <%s> unloaded", data.text.GetData());        
        
    if(!CacheManager::GetSingleton().LoadQuest(questID))
    {
        psserver->SendSystemError(client->GetClientNum(), "Quest <%s> Could not be loaded", data.text.GetData());                
        psserver->SendSystemError(client->GetClientNum(), psserver->questmanager->LastError());
    }        
    else
    {
        psserver->SendSystemError(client->GetClientNum(), "Quest <%s> loaded", data.text.GetData());                    
    }        
}


gemObject* AdminManager::FindObjectByString(const csString& str)
{
    gemObject* found = NULL;
    GEMSupervisor *gem = GEMSupervisor::GetSingletonPtr();
    if (!gem) 
    {
        return NULL;
    }        

    if ( str.StartsWith("pid:",true) ) // Find by player ID
    {
        csString pid_str = str.Slice(4);
        int pID = atoi( pid_str.GetDataSafe() );
        if (pID)
            found = gem->FindPlayerEntity( pID );
    }
    else if ( str.StartsWith("eid:",true) ) // Find by entity ID
    {
        csString eid_str = str.Slice(4);
        PS_ID eID = (PS_ID)atoi( eid_str.GetDataSafe() );
        if (eID)
            found = gem->FindObject( eID );
    }
    else // Try finding an entity by name
    {
        found = gem->FindObject(str);
    }        

    return found;
}

void AdminManager::CommandArea(MsgEntry *me, psAdminCmdMessage& msg, AdminCmdData& data, Client *client, int range)
{
    if (!Valid(client->GetSecurityLevel(), "command area", me->clientnum))
    {
        psserver->SendSystemInfo(me->clientnum,"You are not allowed to use area");
        return;
    }

    if (range)
    {
        gemActor* self = client->GetActor();
        if (!self)
        {
            psserver->SendSystemError(client->GetClientNum(), "You do not exist...");
            return;
        }

        int mode = 0;
        if (data.player.StartsWith("area:players",true))
            mode = 0;
        else if (data.player.StartsWith("area:actors",true))
            mode = 1;
        else if (data.player.StartsWith("area:items",true))
            mode = 2;
        else if (data.player.StartsWith("area:npcs",true))
            mode = 3;
        else if (data.player.StartsWith("area:entities",true))
            mode = 4;

        csVector3 pos;
        iSector* sector;
        self->GetPosition(pos,sector);
                
        GEMSupervisor* gem = GEMSupervisor::GetSingletonPtr();
        if (!gem)
            return;

        csRef<iCelEntityList> nearlist = gem->pl->FindNearbyEntities(sector,pos,range);
        size_t count = nearlist->GetCount();
        size_t handled_entities = 0;
        

        for (size_t i=0; i<count; i++)
        {
            gemObject *nearobj = gem->GetObjectFromEntityList(nearlist,i);
            if (!nearobj)
                continue;

            switch (mode)
            {
                case 0:  // Target players
                {
                    if (nearobj->GetClientID())
                    {
                        data.player.Format("pid:%d",nearobj->GetPlayerID());
                        break;
                    }
                    else
                        continue;
                }
                case 1:  // Target actors
                {
                    if (nearobj->GetPlayerID())
                    {
                        data.player.Format("pid:%d",nearobj->GetPlayerID());
                        break;
                    }
                    else
                        continue;
                }
                case 2:  // Target items
                {
                    if (nearobj->GetItem())
                    {
                        data.player.Format("eid:%u",nearobj->GetEntity()->GetID());
                        break;
                    }
                    else
                        continue;
                }
                case 3:  // Target NPCs
                {
                    if (nearobj->GetNPCPtr())
                    {
                        data.player.Format("pid:%d",nearobj->GetPlayerID());
                        break;
                    }
                    else
                        continue;
                }
                case 4:  // Target everything
                {
                    data.player.Format("eid:%u",nearobj->GetEntity()->GetID());
                    break;
                }
            }

            // Run this once for every target in range  (each one will be verified and logged seperately)
            HandleAdminCmdMessage(me, msg, data, client);
            handled_entities++;
        }

        if (!handled_entities)
        {
            psserver->SendSystemError(client->GetClientNum(), "Nothing of specified type in range.");
            return;
        }
    }
    else
    {
        psserver->SendSystemError(client->GetClientNum(), "You must specify a range.");
    }
}

void AdminManager::GetSiblingChars(MsgEntry* me,psAdminCmdMessage& msg, AdminCmdData& data,Client *client)
{
    if (!data.player || data.player.Length() == 0)
    {
        psserver->SendSystemError(me->clientnum, "You must specify a character name.");
        return;
    }

    size_t accountId = 0;
    Result result(db->Select("SELECT account_id FROM characters WHERE name = '%s'", data.player.GetData()));
    if (result.IsValid() && result.Count())
    {
        iResultRow& accountRow = result[0];
        accountId = accountRow.GetUInt32("account_id");

        psserver->SendSystemInfo(client->GetClientNum(), "Account ID of player %s is %d.",
                                 data.player.GetData(), accountId);
    }
    else
    {
        psserver->SendSystemError(me->clientnum, "No player with name %s found in database.", data.player.GetData());
        return;
    }

    if (accountId != 0)
    {
        Result result2(db->Select("SELECT id, name, lastname, last_login FROM characters WHERE account_id = %d", accountId));

        if (result2.IsValid() && result2.Count())
        {

            psserver->SendSystemInfo( client->GetClientNum(), "Characters on this account:" );
            for (int i = 0; i < (int)result2.Count(); i++)
            {
                iResultRow& row = result2[i];
                psserver->SendSystemInfo(client->GetClientNum(), "Player ID: %d, %s %s, last login: %s",
                                         row.GetUInt32("id"), row["name"], row["lastname"], row["last_login"] );
            }
        }
        else if ( !result2.Count() )
        {
            psserver->SendSystemInfo(me->clientnum, "There are no characters on this account.");
        }
        else
        {
            psserver->SendSystemError(me->clientnum, "Error executing SQL-statement to retrieve characters on the account of player %s.", data.player.GetData());
            return;
        }
    }
    else
    {
        psserver->SendSystemInfo(me->clientnum, "The Account ID [%d] of player %s is not valid.", accountId, data.player.GetData());
    }
}


bool AdminManager::Valid( int level, const char* command, int clientnum )
{
    csString errorStr;
    
    if ( !CacheManager::GetSingleton().GetCommandManager()->Validate( level, command, errorStr ) )
    {
        psserver->SendSystemError(clientnum, errorStr);
        return false;
    }
    return true;
}


void AdminManager::GetInfo(MsgEntry* me,psAdminCmdMessage& msg, AdminCmdData& data,Client *client, gemObject* target)
{    
    PS_ID entityId = 0;
    if ( target && target->GetEntity() )
        entityId = target->GetEntity()->GetID();

    if (target && strcmp(target->GetObjectType(), "ActionLocation") == 0) // Action location
    {
        gemActionLocation *item = dynamic_cast<gemActionLocation *>(target);
        if (!item)
        {
            psserver->SendSystemError(client->GetClientNum(), "Error! Target is not a valid gemActionLocation object.");
            return;
        }
        psActionLocation *action = item->GetAction();

        csString info;
        info.Format("ActionLocation: %s with ", item->GetName());
        if (action)
            info.AppendFmt("ID %u, and instance ID of the container %u.", action->id, action->GetInstanceID());
        else
            info.Append("no action location information.");

        psserver->SendSystemInfo(client->GetClientNum(), info);

        return;
    }

    if ( target && target->GetItem() && target->GetItem()->GetBaseStats() ) // Item
    {
          psItem* item = target->GetItem();

          csString info;
          info.Format("Item: %s ", item->GetName() );
          
          if ( item->GetStackCount() > 1 )
              info.AppendFmt("(x%d) ", item->GetStackCount() );

          info.AppendFmt("with item ID %u, instance ID %u, and entity ID %u",
                      item->GetBaseStats()->GetUID(),
                      item->GetUID(),
                      entityId );

          if ( item->GetScheduledItem() )
              info.AppendFmt(", spawns with interval %d + %d max modifier",
                             item->GetScheduledItem()->GetInterval(),
                             item->GetScheduledItem()->GetMaxModifier() );

          // Get all flags on this item
          int flags = item->GetFlags();
          if (flags)
          {
              info += ", has flags:";

              if ( flags & PSITEM_FLAG_CRAFTER_ID_IS_VALID )
                  info += " 'vlaid crafter id'";
              if ( flags & PSITEM_FLAG_GUILD_ID_IS_VALID )
                  info += " 'vlaid guild id'";
              if ( flags & PSITEM_FLAG_UNIQUE_ITEM )
                  info += " 'unique'";
              if ( flags & PSITEM_FLAG_USES_BASIC_ITEM )
                  info += " 'uses basic item'";
              if ( flags & PSITEM_FLAG_PURIFIED )
                  info += " 'purified'";
              if ( flags & PSITEM_FLAG_PURIFYING )
                  info += " 'purifying'";
              if ( flags & PSITEM_FLAG_LOCKED )
                  info += " 'locked'";
              if ( flags & PSITEM_FLAG_LOCKABLE )
                  info += " 'lockable'";
              if ( flags & PSITEM_FLAG_SECURITYLOCK )
                  info += " 'lockable'";
              if ( flags & PSITEM_FLAG_UNPICKABLE )
                  info += " 'unpickable'";
              if ( flags & PSITEM_FLAG_NOPICKUP )
                  info += " 'no pickup'";
              if ( flags & PSITEM_FLAG_KEY )
                  info += " 'key'";
              if ( flags & PSITEM_FLAG_MASTERKEY )
                  info += " 'masterkey'";
              if ( flags & PSITEM_FLAG_TRANSIENT )
                  info += " 'transient'";
              if ( flags & PSITEM_FLAG_USE_CD)
                  info += " 'collide'";
          }

          psserver->SendSystemInfo(client->GetClientNum(),info);
          return; // Done
    }

    char ipaddr[20] = {0};
    csString name, ipAddress, securityLevel;
    int playerId = 0, accountId = 0;
    float timeConnected = 0.0f;

    bool banned = false;
    time_t banTimeLeft;
    int daysLeft = 0, hoursLeft = 0, minsLeft = 0;
    
    if (target) // Online
    {
        Client* targetclient = target->GetClient();

        playerId = target->GetPlayerID();
        if (target->GetCharacterData())
            timeConnected = target->GetCharacterData()->GetTimeConnected() / 3600;

        if (targetclient) // Player
        {
            name = targetclient->GetName();
            targetclient->GetIPAddress(ipaddr);
            ipAddress = ipaddr;
            accountId = targetclient->GetAccountID();

            // Because of /deputize we'll need to get the real SL from DB
            int currSL = targetclient->GetSecurityLevel();
            int trueSL = GetTrueSecurityLevel(accountId);
            
            if (currSL != trueSL)
                securityLevel.Format("%d(%d)",currSL,trueSL);
            else
                securityLevel.Format("%d",currSL);
        }
        else // NPC
        {
            name = target->GetName();
            psserver->SendSystemInfo(client->GetClientNum(),
                "NPC: %s has a player ID %d, entity ID %u, and has been active for %1.1f hours",
                name.GetData(),
                playerId,
                entityId,
                timeConnected );
            return; // Done
        }
    }
    else // Offline
    {
        Result result(db->Select("SELECT id, name, lastname, account_id, time_connected_sec from characters where name='%s'", data.player.GetData()));

        if (!result.IsValid() || result.Count() == 0)
        {
             psserver->SendSystemError(client->GetClientNum(), "Cannot find player %s",data.player.GetData() );
             return;
        }
        else
        {
             iResultRow& row = result[0];
             name = row["name"];
             if (row["lastname"] && strcmp(row["lastname"],""))
             {
                  name.Append(" ");
                  name.Append(row["lastname"]);
             }
             playerId = row.GetUInt32("id");
             accountId = row.GetUInt32("account_id");
             ipAddress = "(offline)";
             timeConnected = row.GetFloat("time_connected_sec") / 3600;
             securityLevel.Format("%d",GetTrueSecurityLevel(accountId));
        }
    }
    BanEntry* ban = psserver->GetAuthServer()->GetBanManager()->GetBanByAccount(accountId);
    if(ban)
    {
        time_t now = time(0);
        if(ban->end > now)
        {
            banTimeLeft = ban->end - now;
            banned = true;
    		
            banTimeLeft = banTimeLeft / 60; // don't care about seconds
            minsLeft = banTimeLeft % 60;
            banTimeLeft = banTimeLeft / 60;
            hoursLeft = banTimeLeft % 24;
            banTimeLeft = banTimeLeft / 24;
            daysLeft = banTimeLeft;
        }
    }

    if (playerId != 0)
    {
        csString info;
        info.Format("Player: %s has ", name.GetData() );
        
        if (securityLevel != "0")
            info.AppendFmt("security level %s, ", securityLevel.GetData() );

        info.AppendFmt("account ID %d, player ID %d, ", accountId, playerId );
        
        if (ipAddress != "(offline)")
            info.AppendFmt("entity ID %u, IP is %s, ", entityId, ipAddress.GetData() );
        else
            info.Append("is offline, ");
            
        info.AppendFmt("total time connected is %1.1f hours", timeConnected );

        info.AppendFmt(" has had %d exploits flagged.", client->GetFlagCount());

        if(banned)
        {
            info.AppendFmt(" The player's account is banned! Time left: %d days, %d hours, %d minutes.", daysLeft, hoursLeft, minsLeft);
        }

        psserver->SendSystemInfo(client->GetClientNum(),info);
    }
    else
    {
        psserver->SendSystemError(client->GetClientNum(), "Error!  Object is not an item, player, or NPC." );
    }
}

void AdminManager::HandlePetitionMessage(MsgEntry *me, psPetitionRequestMessage& msg,Client *client)
{
    // Check which message and if this is a GM message or user message
    if (msg.request == "query")
    {
        if (msg.isGM)
            GMListPetitions(me, msg,client);
        else
            ListPetitions(me, msg,client);
    }
    else if (msg.request == "cancel" && !msg.isGM)
    {
        CancelPetition(me, msg,client);
    }
    else if (msg.isGM)
    {
        GMHandlePetition(me, msg,client);
    }
}

void AdminManager::HandleGMGuiMessage(MsgEntry *me, psGMGuiMessage& msg,Client *client)
{
    if (msg.type == psGMGuiMessage::TYPE_QUERYPLAYERLIST)
    {
        if (client->GetSecurityLevel() >= GM_LEVEL_0) 
            SendGMPlayerList(me, msg,client);
        else
            psserver->SendSystemError(me->clientnum, "You are not a GM.");
    }
    else if (msg.type == psGMGuiMessage::TYPE_GETGMSETTINGS)
    {
        int gmSettings = 0;
        if (client->GetActor()->GetVisibility())
            gmSettings |= 1;
        if (client->GetActor()->GetInvincibility())
            gmSettings |= (1 << 1);
        if (client->GetActor()->GetViewAllObjects())
            gmSettings |= (1 << 2);
        if (client->GetActor()->nevertired)
            gmSettings |= (1 << 3);
        if (client->GetActor()->questtester)
            gmSettings |= (1 << 4);
        if (client->GetActor()->infinitemana)
            gmSettings |= (1 << 5);
        if (client->GetActor()->GetFiniteInventory())
            gmSettings |= (1 << 6);
        if (client->GetActor()->safefall)
            gmSettings |= (1 << 7);

        psGMGuiMessage gmMsg(client->GetClientNum(), gmSettings);
        gmMsg.SendMessage();
    }
}

void AdminManager::CreateHuntLocation(MsgEntry* me,psAdminCmdMessage& msg, AdminCmdData& data,Client *client)
{    
    if (data.item.IsEmpty())
    {
        psserver->SendSystemError(me->clientnum, "Insufficent parameters. Use /crystal <interval> <random> <itemname>");
        return;
    }

    if (data.interval != data.interval || data.random != data.random)
    {
        psserver->SendSystemError(me->clientnum, "Invalid interval(s)");
        return;
    }
    if (data.interval < 1 || data.random < 1)
    {
        psserver->SendSystemError(me->clientnum, "Intervals need to be greater than 0");
        return;
    }

    // In seconds
    int interval = 1000*data.interval;
    int random   = 1000*data.random;

    psItemStats* rawitem = CacheManager::GetSingleton().GetBasicItemStatsByName(data.item);
    if (!rawitem)
    {
        psserver->SendSystemError(me->clientnum, "Invalid item to spawn");
        return;
    }

    // Find the location
    csVector3 pos;
    float angle;
    iSector* sector = 0;
    int instance;

    client->GetActor()->GetPosition(pos, angle, sector);
    instance = client->GetActor()->GetInstance();

    if (!sector)
    {
        psserver->SendSystemError(me->clientnum, "Invalid sector");
        return;
    }

    psSectorInfo *spawnsector = CacheManager::GetSingleton().GetSectorInfoByName(sector->QueryObject()->GetName());
    if(!spawnsector)
    {
        CPrintf(CON_ERROR,"Player is in invaild sector %s!",sector->QueryObject()->GetName());
        return;
    }
 
    // to db
    db->Command(
        "INSERT INTO hunt_locations"
        "(`x`,`y`,`z`,`itemid`,`sector`,`interval`,`max_random`)"
        "VALUES ('%f','%f','%f','%u','%s','%d','%d')",
        pos.x,pos.y,pos.z, rawitem->GetUID(),sector->QueryObject()->GetName(),interval,random);

    psScheduledItem* schedule = new psScheduledItem(db->GetLastInsertID(),rawitem->GetUID(),pos,spawnsector,instance,interval,random);
    psItemSpawnEvent* event = new psItemSpawnEvent(schedule);
    psserver->GetEventManager()->Push(event);

    // Done!
    psserver->SendSystemInfo(me->clientnum,"New hunt location created!");
}

void AdminManager::SetAttrib(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data, Client *client)
{
    gemActor * actor = client->GetActor();

    bool onoff = false;
    bool toggle = false;
    bool already = false;

    if (data.setting == "on")
        onoff = true;
    else if (data.setting == "off")
        onoff = false;
    else
        toggle = true;

    if (data.attribute == "list")
    {
        psserver->SendSystemInfo(me->clientnum, "invincible = %s\n"
                                                "invisible = %s\n"
                                                "viewall = %s\n"
                                                "nevertired = %s\n"
                                                "nofalldamage = %s\n"
                                                "infiniteinventory = %s\n"
                                                "questtester = %s\n"
                                                "infinitemana = %s",
                                                (actor->GetInvincibility())?"on":"off",
                                                (!actor->GetVisibility())?"on":"off",
                                                (actor->GetViewAllObjects())?"on":"off",
                                                (actor->nevertired)?"on":"off",
                                                (actor->safefall)?"on":"off",
                                                (!actor->GetFiniteInventory())?"on":"off",
                                                (actor->questtester)?"on":"off",
                                                actor->infinitemana?"on":"off");
        return;
    }
    else if (data.attribute == "invinciblity" || data.attribute == "invincible")
    {
        if (toggle)
        {
            actor->SetInvincibility(!actor->GetInvincibility());
            onoff = actor->GetInvincibility();
        }
        else if (actor->GetInvincibility() == onoff)
            already = true;
        else
            actor->SetInvincibility(onoff);
    }
    else if (data.attribute == "invisiblity" || data.attribute == "invisible")
    {
        if (toggle)
        {
            actor->SetVisibility(!actor->GetVisibility());
            onoff = !actor->GetVisibility();
        }
        else if (actor->GetVisibility() == !onoff)
            already = true;
        else
            actor->SetVisibility(!onoff);
    }
    else if (data.attribute == "viewall")
    {
        if (toggle)
        {
            actor->SetViewAllObjects(!actor->GetViewAllObjects());
            onoff = actor->GetViewAllObjects();
        }
        else if (actor->GetViewAllObjects() == onoff)
            already = true;
        else
            actor->SetViewAllObjects(onoff);
    }
    else if (data.attribute == "nevertired")
    {
        if (toggle)
        {
            actor->nevertired = !actor->nevertired;
            onoff = actor->nevertired;
        }
        else if (actor->nevertired == onoff)
            already = true;
        else
            actor->nevertired = onoff;
    }
    else if (data.attribute == "infinitemana")
    {
        if (toggle)
        {
            actor->infinitemana = !actor->infinitemana;
            onoff = actor->infinitemana;
        }
        else if (actor->infinitemana == onoff)
            already = true;
        else
            actor->infinitemana = onoff;
    }
    else if (data.attribute == "nofalldamage")
    {
        if (toggle)
        {
            actor->safefall = !actor->safefall;
            onoff = actor->safefall;
        }
        else if (actor->safefall == onoff)
            already = true;
        else
            actor->safefall = onoff;
    }
    else if (data.attribute == "infiniteinventory")
    {
        if (toggle)
        {
            actor->SetFiniteInventory(!actor->GetFiniteInventory());
            onoff = !actor->GetFiniteInventory();
        }
        else if (actor->GetFiniteInventory() == !onoff)
            already = true;
        else
            actor->SetFiniteInventory(!onoff);
    }
    else if (data.attribute == "questtester")
    {
        if (toggle)
        {
            actor->questtester = !actor->questtester;
            onoff = actor->questtester;
        }
        else if (actor->questtester == onoff)
            already = true;
        else
            actor->questtester = onoff;
    }
    else if (!data.attribute.IsEmpty())
    {
        psserver->SendSystemInfo(me->clientnum, "%s is not a supported attribute", data.attribute.GetData() );
        return;
    }
    else
    {
        psserver->SendSystemInfo(me->clientnum, "Correct syntax is: \"/set [attribute] [on|off]\"");
        return;
    }

    psserver->SendSystemInfo(me->clientnum, "%s %s %s",
                                            data.attribute.GetData(),
                                            (already)?"is already":"has been",
                                            (onoff)?"enabled":"disabled" );
}

void AdminManager::SetLabelColor(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data, Client *client, gemActor * subject)
{
    int mask = 0;

    if(!subject)
    {
        psserver->SendSystemInfo( me->clientnum, "The target was not found online\"");
        return;
    }

    if (data.setting == "normal")
    {
        mask = subject->GetSecurityLevel();
    }
    else if (data.setting == "npc")
    {
        mask = -1;
    }
    else if (data.setting == "player")
    {
        mask = 0;
    }
    else if (data.setting == "gm1")
    {
        mask = 21;
    }
    else if (data.setting == "gm")
    {
        mask = 22;
    }
    else
    {
        psserver->SendSystemInfo(me->clientnum,
            "Correct syntax is: \"/setlabelcolor [target] [npc|player|gm1|gm|normal]\"");
        return;
    }
    subject->SetMasqueradeLevel(mask);
    psserver->SendSystemInfo(me->clientnum, "Label color of %s set to %s",
                             data.player.GetData(), data.setting.GetData() );
}

void AdminManager::Divorce(MsgEntry* me, AdminCmdData& data)
{
    if (!data.player.Length())
    {
        psserver->SendSystemInfo(me->clientnum, "Usage: \"/divorce [character]\"");
        return;
    }

    Client* divorcer = clients->Find( data.player );
    
    // If the player that wishes to divorce is not online, we can't proceed.
    if (!divorcer)
    {
        psserver->SendSystemInfo(me->clientnum, "The player that wishes to divorce must be online." );
        return;
    }

    psCharacter* divorcerChar = divorcer->GetCharacterData();

    // If the player is not married, we can't divorce.
    if(!divorcerChar->GetIsMarried())
    {
        psserver->SendSystemInfo(me->clientnum, "You can't divorce people who aren't married!");
        return;
    }

    // Divorce the players.
    psMarriageManager* marriageMgr = psserver->GetMarriageManager();
    if (!marriageMgr)
    {
        psserver->SendSystemError(me->clientnum, "Can't load MarriageManager.");
        Error1("MarriageManager failed to load.");
        return;
    }

    // Delete entries of character's from DB
    csString spouseFullName = divorcerChar->GetSpouseName();
    csString spouseName = spouseFullName.Slice( 0, spouseFullName.FindFirst(' '));
    marriageMgr->DeleteMarriageInfo(divorcerChar);
    psserver->SendSystemInfo(me->clientnum, "You have divorced %s from %s.", data.player.GetData(), spouseName.GetData());
    Debug3(LOG_MARRIAGE, me->clientnum, "%s divorced from %s.", data.player.GetData(), spouseName.GetData());
}

void AdminManager::ViewMarriage(MsgEntry* me, AdminCmdData& data)
{
    if (!data.player.Length())
    {
        psserver->SendSystemInfo(me->clientnum, "Usage: \"/marriageinfo [character]\"");
        return;
    }

    Client* player = clients->Find( data.player );
    
    // If the player is not online, we can't proceed.
    if (!player)
    {
        psserver->SendSystemInfo(me->clientnum, "The player who's marriage info you wish to check must be online." );
        return;
    }

    psCharacter* playerData = player->GetCharacterData();

    // If the player is not married, there's no info.
    if(!playerData->GetIsMarried())
    {
        psserver->SendSystemInfo(me->clientnum, "This player is not married.");
        return;
    }

    csString spouseFullName = playerData->GetSpouseName();
    csString spouseName = spouseFullName.Slice( 0, spouseFullName.FindFirst(' '));

    if(psserver->GetCharManager()->HasConnected(spouseName))
    {
        psserver->SendSystemInfo(me->clientnum, "%s is married to %s, who was last online less than two months ago.", data.player.GetData(), spouseName.GetData());
    }
    else
    {
        psserver->SendSystemInfo(me->clientnum, "%s is married to %s, who was last online more than two months ago.", data.player.GetData(), spouseName.GetData());
    }
}

void AdminManager::Teleport(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data, Client *client, gemObject* subject)
{
    if (data.target == "")
    {
        psserver->SendSystemInfo(client->GetClientNum(), "Use: /teleport <subject> <destination>\n"
                                 "Subject    : me/target/<object name>/<NPC name>/<player name>/eid:<EID>/pid:<PID>\n"
                                 "Destination: here [<instance>]/last/spawn/restore/map [<map name>|here] <x> <y> <z> [<instance>]\n");
        return;
    }
    
    // If player is offline and the special argument is called
    if (subject == NULL && data.target == "restore")
    {
        psString sql;
        iSector * mySector;
        csVector3 myPoint;
        float yRot = 0.0;
        client->GetActor()->GetPosition(myPoint, yRot, mySector);

        psSectorInfo * mysectorinfo = NULL;
        if (mySector != NULL)
            mysectorinfo = CacheManager::GetSingleton().GetSectorInfoByName(mySector->QueryObject()->GetName());
        if (mysectorinfo == NULL)
        {
            psserver->SendSystemError(client->GetClientNum(), "Sector not found!");
            return;
        }
        
        sql.AppendFmt("update characters set loc_x=%10.2f, loc_y=%10.2f, loc_z=%10.2f, loc_yrot=%10.2f, loc_sector_id=%u, loc_instance=%d where name='%s'",
            myPoint.x, myPoint.y, myPoint.z, yRot, mysectorinfo->uid, client->GetActor()->GetInstance(), data.player.GetData());
        
        if (db->CommandPump(sql) != 1)
        {
            Error3 ("Couldn't save character's position to database.\nCommand was "
                    "<%s>.\nError returned was <%s>\n",db->GetLastQuery(),db->GetLastError());

            psserver->SendSystemError(client->GetClientNum(), "Offline character %s could not be moved!", data.player.GetData());
        }
        else
            psserver->SendSystemResult(client->GetClientNum(), "%s will next log in at your current location", data.player.GetData());
        
        return;
    }
    else if (subject == NULL)
    {
        psserver->SendSystemError(client->GetClientNum(), "Cannot teleport target");
        return;
    }

    csVector3 targetPoint;
    float yRot = 0.0;
    iSector *targetSector;
    int targetInstance;
    if ( !GetTargetOfTeleport(client, msg, data, targetSector, targetPoint, yRot, subject, targetInstance) )
    {
        psserver->SendSystemError(client->GetClientNum(), "Cannot teleport %s to %s", data.player.GetData(), data.target.GetData() );
        return;
    }
    
    //Error6("tele %s to %s %f %f %f",subject->GetName(), targetSector->QueryObject()->GetName(), targetPoint.x, targetPoint.y, targetPoint.z);
    
    csVector3 oldpos;
    float oldyrot;
    iSector *oldsector;
    subject->GetPosition(oldpos,oldyrot,oldsector);

    if ( (oldsector == targetSector) && (oldpos == targetPoint) && (subject->GetInstance() == targetInstance) )
    {
        psserver->SendSystemError(client->GetClientNum(), "What's the point?");
        return;
    }

    Client* superclient = clients->FindAccount( subject->GetSuperclientID() );
    if(superclient)
    {
        psserver->SendSystemError(client->GetClientNum(), "This enity is controlled by superclient and can't be teleported.");        
        return;
    }

    if ( !MoveObject(client,subject,targetPoint,yRot,targetSector,targetInstance) )
        return;

    csString destName;
    if (data.map.Length())
    {
        destName.Format("map %s", data.map.GetData() );
    }
    else
    {
        destName.Format("sector %s", targetSector->QueryObject()->GetName() );
    }

    // Update ProxList on sector crossing
    if (oldsector != targetSector)
    {
        subject->UpdateProxList(true);
        psserver->SendSystemOK(subject->GetClientID(), "Welcome to " + destName);
    }
    else
    {
        subject->UpdateProxList(false); // Update ProxList if needed
    }
    

    if ( dynamic_cast<gemActor*>(subject) ) // Record old location of actor, for undo
        ((gemActor*)subject)->UpdateValidLocation(oldpos, 0.0f, oldyrot, oldsector, true);

    // Send explanations
    if (subject->GetClientID() != client->GetClientNum())
    {
        psserver->SendSystemResult(client->GetClientNum(), "Teleported %s to %s", subject->GetName(), ((data.target=="map")?destName:data.target).GetData() );
        psserver->SendSystemResult(subject->GetClientID(), "You were moved by a GM");
    }
    
    if (data.player == "me"  &&  data.target != "map"  &&  data.target != "here")
    {
        psGUITargetUpdateMessage updateMessage( client->GetClientNum(), subject->GetEntity()->GetID() );
        updateMessage.SendMessage();
    }
}

void AdminManager::HandleActionLocation(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data, Client *client)
{
    if ( !data.subCmd.Length() || data.subCmd == "help" || data.help )
    {
        psserver->SendSystemInfo( me->clientnum, "Usage: \"/action create_entrance sector guildname description\"");
        return;
    }

    if (data.subCmd == "create_entrance")
    {
        // Create sign
        csString doorLock = "Claymore";
        psItemStats *itemstats=CacheManager::GetSingleton().GetBasicItemStatsByName(doorLock.GetData());
        if (!itemstats)
        {
            Error2("Error: Action entrance failed to get item stats for item %s.\n",doorLock.GetData());
            return;
        }

        // Make item
        psItem *lockItem = itemstats->InstantiateBasicItem();
        if (!lockItem)
        {
            Error2("Error: Action entrance failed to create item %s.\n",doorLock.GetData());
            return;
        }

        // Get client location for exit
        csVector3 pos;
        iSector* sector = 0;
        csString name = data.name;
        float angle;
        gemObject *object = client->GetActor();
        if (!object)
        {
            Error2("Error: Action entrance failed to get client actor pointer for client %s.\n",client->GetName());
            return;
        }

        // Get client position and sector
        object->GetPosition(pos, angle, sector);
        csString sector_name = (sector) ? sector->QueryObject()->GetName() : "(null)";
        psSectorInfo* sectorInfo = CacheManager::GetSingleton().GetSectorInfoByName( sector_name.GetData() );
        if (!sectorInfo)
        {
            Error2("Error: Action entrance failed to get sector using name %s.\n",sector_name.GetData());
            return;
        }

        // Setup the sign
        lockItem->SetStackCount(1);
        lockItem->SetLocationInWorld(0,sectorInfo,pos.x,pos.y,pos.z,angle);
        lockItem->SetOwningCharacter(NULL);
        lockItem->SetMaxItemQuality(50.0);

        // Assign the lock attributes and save to create ID
        lockItem->SetFlags(PSITEM_FLAG_UNPICKABLE | PSITEM_FLAG_SECURITYLOCK | PSITEM_FLAG_LOCKED | PSITEM_FLAG_LOCKABLE);
        lockItem->SetIsPickupable(false);
        lockItem->SetLoaded();
        lockItem->SetName(name);
        lockItem->Save(false);

        // Create lock in world
        if (!EntityManager::GetSingleton().CreateItem(lockItem,false))
        {
            delete lockItem;
            Error1("Error: Action entrance failed to create lock item.\n");
            return;
        }


        //-------------

        // Create new lock for entrance
        doorLock = "Simple Lock";
        itemstats=CacheManager::GetSingleton().GetBasicItemStatsByName(doorLock.GetData());
        if (!itemstats)
        {
            Error2("Error: Action entrance failed to get item stats for item %s.\n",doorLock.GetData());
            return;
        }

        // Make item
        lockItem = itemstats->InstantiateBasicItem();
        if (!lockItem)
        {
            Error2("Error: Action entrance failed to create item %s.\n",doorLock.GetData());
            return;
        }

        // Setup the lock item in instance 1
        lockItem->SetStackCount(1);
        lockItem->SetLocationInWorld(1,sectorInfo,pos.x,pos.y,pos.z,angle);
        lockItem->SetOwningCharacter(NULL);
        lockItem->SetMaxItemQuality(50.0);

        // Assign the lock attributes and save to create ID
        lockItem->SetFlags(PSITEM_FLAG_UNPICKABLE | PSITEM_FLAG_SECURITYLOCK | PSITEM_FLAG_LOCKED | PSITEM_FLAG_LOCKABLE);
        lockItem->SetIsPickupable(false);
        lockItem->SetIsSecurityLocked(true);
        lockItem->SetIsLocked(true);
        lockItem->SetLoaded();
        lockItem->SetName(name);
        lockItem->Save(false);

        // Create lock in world
        if (!EntityManager::GetSingleton().CreateItem(lockItem,false))
        {
            delete lockItem;
            Error1("Error: Action entrance failed to create lock item.\n");
            return;
        }

        // Get lock ID for response string
        uint32 lockID = lockItem->GetUID();

        // Get last targeted mesh
        csString meshTarget = client->GetMesh();

        // Create entrance name
        name.Format("Enter %s",data.name.GetData());

        // Create entrance response string
        csString resp = "<Examine><Entrance Type='ActionID' ";
        resp.AppendFmt("LockID='%u' ",lockID);
        resp.Append("X='0' Y='0' Z='0' Rot='3.14' ");
        resp.AppendFmt("Sector=\'%s\' />",data.sector.GetData());

        // Create return response string
        resp.AppendFmt("<Return X='%f' Y='%f' Z='%f' Rot='%f' Sector='%s' />",
            pos.x,pos.y,pos.z,angle,sector_name.GetData());

        // Add on description
        resp.AppendFmt("<Description>%s</Description></Examine>",data.description.GetData());

        // Create entrance action location w/ possition info since there will be many of these
        psActionLocation* actionLocation = new psActionLocation();
        actionLocation->SetName(name);
        actionLocation->SetSectorName(sector_name.GetData());
        actionLocation->SetMeshName(meshTarget);
        actionLocation->SetRadius(2.0);
        actionLocation->SetPosition(pos);
        actionLocation->SetTriggerType("SELECT");
        actionLocation->SetResponseType("EXAMINE");
        actionLocation->SetResponse(resp);
        actionLocation->SetIsEntrance(true);
        actionLocation->SetIsLockable(true);
        actionLocation->SetActive(false);
        actionLocation->Save();

        // Update Cache
        if (!psserver->GetActionManager()->CacheActionLocation(actionLocation))
        {
            Error2("Failed to create action %s.\n", actionLocation->name.GetData());
            delete actionLocation;
        }
        psserver->SendSystemInfo( me->clientnum, "Action location entrance created for %s.",data.sector.GetData());
    }
    else
    {
        Error2("Unknow action command: %s",data.subCmd.GetDataSafe());
    }
}

int AdminManager::PathPointCreate(int pathID, int prevPointId, csVector3& pos, csString& sectorName)
{
    const char *fieldnames[]=
        {
            "path_id",
            "prev_point",
            "x",
            "y",
            "z",
            "loc_sector_id"
        };

    psSectorInfo * si = CacheManager::GetSingleton().GetSectorInfoByName(sectorName);
    if (!si)
    {
        Error2("No sector info for %s",sectorName.GetDataSafe());
        return -1;
    }
    
    psStringArray values;
    values.FormatPush("%u", pathID );
    values.FormatPush("%u", prevPointId );
    values.FormatPush("%10.2f",pos.x);
    values.FormatPush("%10.2f",pos.y);
    values.FormatPush("%10.2f",pos.z);
    values.FormatPush("%u",si->uid);
    
    unsigned int id = db->GenericInsertWithID("sc_path_points",fieldnames,values);
    if (id==0)
    {
        Error2("Failed to create new Path Point Error %s",db->GetLastError());
        return -1;
    }
    
    return id;
}

void AdminManager::HandlePath(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data, Client *client)
{
    const char* usage         = "/path adjust|alias|display|format|help|hide|info|point|start|stop [options]";
    const char* usage_alias   = "/path alias <alias>";
    const char* usage_adjust  = "/path adjust [<radius>]";
    const char* usage_display = "/path display|show ['points'|'waypoints']";
    const char* usage_format  = "/path format <format> [first]";
    const char* usage_help    = "/path help [sub command]";
    const char* usage_hide    = "/path hide ['points'|'waypoints']";
    const char* usage_info    = "/path info";
    const char* usage_point   = "/path point";
    const char* usage_select  = "/path select <radius>";
    const char* usage_split   = "/path split <radius> [wp flags]";
    const char* usage_start   = "/path start <radius> [wp flags] [path flags]";
    const char* usage_stop    = "/path stop|end <radius> [wp flags]";

    // Some variables needed by most functions
    csVector3 myPos;
    float myRotY;
    iSector* mySector = 0;
    csString mySectorName;
    
    client->GetActor()->GetPosition(myPos, myRotY, mySector);
    mySectorName = mySector->QueryObject()->GetName();
    
    // First check if some help is needed
    if (data.help)
    {
        if (data.subCmd == "help")
        {
            psserver->SendSystemInfo( me->clientnum,"Usage: %s",usage);
        } else if (data.subCmd == "")
        {
            psserver->SendSystemInfo( me->clientnum,"Help on /point\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s",
                                      usage_adjust,usage_alias,usage_display,usage_format,
                                      usage_help,usage_hide,usage_info,usage_point,
                                      usage_select,usage_split,usage_start,usage_stop);
        }
        else if (data.subCmd == "adjust")
        {
            psserver->SendSystemInfo( me->clientnum,"Usage: %s",usage_adjust);
        }
        else if (data.subCmd == "alias")
        {
            psserver->SendSystemInfo( me->clientnum,"Usage: %s",usage_alias);
        }
        else if (data.subCmd == "display")
        {
            psserver->SendSystemInfo( me->clientnum,"Usage: %s",usage_display);
        }
        else if (data.subCmd == "format")
        {
            psserver->SendSystemInfo( me->clientnum,"Usage: %s",usage_format);
        }
        else if (data.subCmd == "help")
        {
            psserver->SendSystemInfo( me->clientnum,"Usage: %s",usage_help);
        }
        else if (data.subCmd == "hide")
        {
            psserver->SendSystemInfo( me->clientnum,"Usage: %s",usage_hide);
        }
        else if (data.subCmd == "info")
        {
            psserver->SendSystemInfo( me->clientnum,"Usage: %s",usage_info);
        }
        else if (data.subCmd == "point")
        {
            psserver->SendSystemInfo( me->clientnum,"Usage: %s",usage_point);
        }
        else if (data.subCmd == "split")
        {
            psserver->SendSystemInfo( me->clientnum,"Usage: %s",usage_split);
        }
        else if (data.subCmd == "select")
        {
            psserver->SendSystemInfo( me->clientnum,"Usage: %s",usage_select);
        }
        else if (data.subCmd == "start")
        {
            psserver->SendSystemInfo( me->clientnum,"Usage: %s",usage_start);
        }
        else if (data.subCmd == "stop")
        {
            psserver->SendSystemInfo( me->clientnum,"Usage: %s",usage_stop);
        }
        else
        {
            psserver->SendSystemInfo( me->clientnum,"Usage not implemented for %s",data.subCmd.GetDataSafe());
        }
    }
    else if (data.subCmd == "adjust")
    {
        float rangeWP,rangePath;
        int index;
        

        Waypoint * wp = pathNetwork->FindNearestWaypoint(myPos,mySector,data.radius,&rangeWP);
        psPath * path = pathNetwork->FindNearestPath(myPos,mySector,data.radius,&rangePath,&index);
        if (!wp && !path)
        {
            psserver->SendSystemInfo(me->clientnum, "No path point or waypoint in range of %.2f.",data.radius);
            return;
        }

        if (path && (!wp || rangePath < rangeWP))
        {
            if (path->Adjust(db,index,myPos,mySectorName))
            {
                if (client->PathIsDisplaying())
                {
                    psEffectMessage msg(me->clientnum,"admin_path_point",myPos,0,0,client->PathGetEffectID());
                    msg.SendMessage();
                }
                
                psserver->SendSystemInfo(me->clientnum,
                                         "Adjusted point %d of path %s(%s) at range %.2f",
                                         index,path->GetName(),path->GetID(),rangePath);
            }
        }
        else
        {
            if (wp->Adjust(db,myPos,mySectorName))
            {
                if (client->WaypointIsDisplaying())
                {
                    psEffectMessage msg(me->clientnum,"admin_waypoint",myPos,0,0,client->PathGetEffectID());
                    msg.SendMessage();
                }
                
                psserver->SendSystemInfo(me->clientnum, 
                                         "Adjusted waypoint %s(%d) at range %.2f",
                                         wp->GetName(), wp->GetID(), rangeWP);
            }
        }
    }
    else if (data.subCmd == "alias")
    {
        // Find waypoint to alias
        Waypoint * wp = pathNetwork->FindNearestWaypoint(myPos,mySector,2.0);
        if (!wp)
        {
            psserver->SendSystemError( me->clientnum, "No waypoint nearby.");
            return;
        }

        // Check if alias is used before
        Waypoint * existing = pathNetwork->FindWaypoint(data.wp1.GetDataSafe());
        if (existing)
        {
            psserver->SendSystemError( me->clientnum, "Waypoint already exists with the name %s", data.wp1.GetDataSafe());
            return;
        }

        // Create the alias in db
        wp->CreateAlias(db, data.wp1);
        psserver->SendSystemInfo( me->clientnum, "Added alias %s to waypoint %s(%d)",
                                  data.wp1.GetDataSafe(),wp->GetName(),wp->GetID());
    }
    else if (data.subCmd == "format")
    {
        client->WaypointSetPath(data.attribute,data.value);
        csString wp;
        wp.Format(client->WaypointGetPathName(),client->WaypointGetPathIndex());
        psserver->SendSystemInfo( me->clientnum, "New path format, first new WP will be: '%s'",wp.GetDataSafe());
    }
    else if (data.subCmd == "point")
    {
        psPath * path = client->PathGetPath();
        if (!path)
        {
            psserver->SendSystemError(me->clientnum, "You have no path. Please start/select one.");
            return;
        }
        
        path->AddPoint(db, myPos, mySectorName);
        
        if (client->PathIsDisplaying())
        {
            psEffectMessage msg(me->clientnum,"admin_path_point",myPos,0,0,client->PathGetEffectID());
            msg.SendMessage();
        }
        
        psserver->SendSystemInfo( me->clientnum, "Added point.");
        
    }
    else if (data.subCmd == "start")
    {
        float range;
        
        psPath * path = client->PathGetPath();

        if (path)
        {
            if (path->GetID() == -1) // No ID yet -> Just started not ended.
            {
                psserver->SendSystemError( me->clientnum, "You already have a path started.");
                return;
            }
            else
            {
                psserver->SendSystemError( me->clientnum, "Stoping selected path.");
                client->PathSetPath(NULL);
            }
        }

        if (client->WaypointGetPathName() == "")
        {
            psserver->SendSystemError( me->clientnum, "No path format set yet.");
            return;
        }
        
        Waypoint * wp = pathNetwork->FindNearestWaypoint(myPos,mySector,2.0,&range);
        if (wp)
        {
            psserver->SendSystemInfo( me->clientnum, "Starting path, using exsisting waypoint %s(%d) at range %.2f",
                                      wp->GetName(), wp->GetID(), range);
        } else
        {
            csString wpName;
            wpName.Format(client->WaypointGetPathName(),client->WaypointGetNewPathIndex());
            
            Waypoint * existing = pathNetwork->FindWaypoint(wpName);
            if (existing)
            {
                psserver->SendSystemError( me->clientnum, "Waypoint already exists with the name %s", wpName.GetDataSafe());
                return;
            }

            wp = pathNetwork->CreateWaypoint(wpName,myPos,mySectorName,data.radius,data.attribute);

            if (client->WaypointIsDisplaying())
            {
                psEffectMessage msg(me->clientnum,"admin_waypoint",myPos,0,0,client->WaypointGetEffectID());
                msg.SendMessage();
            }
            psserver->SendSystemInfo( me->clientnum, "Starting path, using new waypoint %s(%d)",
                                      wp->GetName(), wp->GetID());
        }
        path = new psLinearPath(-1,"",data.attribute2);
        path->SetStart(wp);
        client->PathSetPath(path);
    }
    else if (data.subCmd == "stop")
    {
        float range;
        psPath * path = client->PathGetPath();
        
        if (!path)
        {
            psserver->SendSystemError( me->clientnum, "You have no path started.");
            return;
        }
        
        Waypoint * wp = pathNetwork->FindNearestWaypoint(myPos,mySector,2.0,&range);
        if (wp)
        {
            psserver->SendSystemInfo( me->clientnum, "Stoping path using exsisting waypoint %s(%d) at range %.2f",
                                      wp->GetName(), wp->GetID(), range);
        } else
        {
            csString wpName;
            wpName.Format(client->WaypointGetPathName(),client->WaypointGetNewPathIndex());
            
            Waypoint * existing = pathNetwork->FindWaypoint(wpName);
            if (existing)
            {
                psserver->SendSystemError( me->clientnum, "Waypoint already exists with the name %s", wpName.GetDataSafe());
                return;
            }

            wp = pathNetwork->CreateWaypoint(wpName,myPos,mySectorName,data.radius,data.attribute);

            if (client->WaypointIsDisplaying())
            {
                psEffectMessage msg(me->clientnum,"admin_waypoint",myPos,0,0,client->WaypointGetEffectID());
                msg.SendMessage();
            }
        }

        client->PathSetPath(NULL);
        path->SetEnd(wp);
        path = pathNetwork->CreatePath(path);
        if (!path)
        {
            psserver->SendSystemError( me->clientnum, "Failed to create path");
        } else
        {
            psserver->SendSystemInfo( me->clientnum, "New path %s(%d) created between %s(%d) and %s(%d)",
                                      path->GetName(),path->GetID(),path->start->GetName(),path->start->GetID(),
                                      path->end->GetName(),path->end->GetID());
        }
    }
    else if (data.subCmd == "display")
    {
        if (data.attribute == "" || toupper(data.attribute.GetAt(0)) == 'P')
        {
            Result rs(db->Select("select pp.* from sc_path_points pp, sectors s where pp.loc_sector_id = s.id and s.name ='%s'",mySectorName.GetDataSafe()));
            
            if (!rs.IsValid())
            {
                Error2("Could not load path points from db: %s",db->GetLastError() );
                return ;
            }
            
            for (int i=0; i<(int)rs.Count(); i++)
            {
                
                csVector3 pos(rs[i].GetFloat("x"),rs[i].GetFloat("y"),rs[i].GetFloat("z"));
                psEffectMessage msg(me->clientnum,"admin_path_point",pos,0,0,client->PathGetEffectID());
                msg.SendMessage();
            }
            
            client->PathSetIsDisplaying(true);
            psserver->SendSystemInfo(me->clientnum, "Displaying all path points in sector %s",mySectorName.GetDataSafe());
        }
        if (data.attribute == "" || toupper(data.attribute.GetAt(0)) == 'W')
        {
            Result rs(db->Select("select wp.* from sc_waypoints wp, sectors s where wp.loc_sector_id = s.id and s.name ='%s'",mySectorName.GetDataSafe()));

            if (!rs.IsValid())
            {
                Error2("Could not load waypoints from db: %s",db->GetLastError() );
                return ;
            }
            
            for (int i=0; i<(int)rs.Count(); i++)
            {
                
                csVector3 pos(rs[i].GetFloat("x"),rs[i].GetFloat("y"),rs[i].GetFloat("z"));
                psEffectMessage msg(me->clientnum,"admin_waypoint",pos,0,0,client->WaypointGetEffectID());
                msg.SendMessage();
            }
            client->WaypointSetIsDisplaying(true);
            psserver->SendSystemInfo(me->clientnum, "Displaying all waypoints in sector %s",mySectorName.GetDataSafe());
        }
    }
    else if (data.subCmd == "hide")
    {
        if (data.attribute == "" || toupper(data.attribute.GetAt(0)) == 'P')
        {
            psStopEffectMessage msg(me->clientnum, client->PathGetEffectID());
            msg.SendMessage();
            client->PathSetIsDisplaying(false);
            psserver->SendSystemInfo(me->clientnum, "All path points hidden");
        }
        if (data.attribute == "" || toupper(data.attribute.GetAt(0)) == 'W')
        {
            psStopEffectMessage msg(me->clientnum, client->WaypointGetEffectID());
            msg.SendMessage();
            client->WaypointSetIsDisplaying(false);
            psserver->SendSystemInfo(me->clientnum, "All waypoints hidden");
        }
        
    }
    else if (data.subCmd == "info")
    {
        float rangeWP,rangePath;
        int index;
        
        Waypoint * wp = pathNetwork->FindNearestWaypoint(myPos,mySector,data.radius,&rangeWP);
        psPath * path = pathNetwork->FindNearestPath(myPos,mySector,data.radius,&rangePath,&index);
        if (!wp && !path)
        {
            psserver->SendSystemInfo(me->clientnum, "No path point or waypoint in range of %.2f.",data.radius);
            return;
        }

        // Faviorize waypoints, since it would be difficult to find them if we should use the distance
        // since you in most cases is standing closer to a path than the wp.
        if (wp)
        {
            csString links;
            for (size_t i = 0; i < wp->links.GetSize(); i++)
            {
                if (i!=0)
                {
                    links.Append(", ");
                }
                links.AppendFmt("%s(%d)",wp->links[i]->GetName(),wp->links[i]->GetID());
            }
            
            psserver->SendSystemInfo(me->clientnum, 
                                     "Found waypoint %s(%d) at range %.2f\n"
                                     "Radius: %.2f\n"
                                     "Flags: %s\n"
                                     "Aliases: %s\n"
                                     "Links: %s",
                                     wp->GetName(),wp->GetID(),rangeWP,
                                     wp->loc.radius,wp->GetFlags().GetDataSafe(),
                                     wp->GetAliases().GetDataSafe(),
                                     links.GetDataSafe());
        }
        else
        {
            psserver->SendSystemInfo(me->clientnum,
                                     "Found path: %s(%d) at range %.2f to point %d\n"
                                     "Start WP: %s(%d)\n"
                                     "End WP: %s(%d)\n"
                                     "Flags: %s",
                                     path->GetName(),path->GetID(),rangePath,index+1,
                                     path->start->GetName(),path->start->GetID(),
                                     path->end->GetName(),path->end->GetID(),
                                     path->GetFlags().GetDataSafe());
        }
        
    }
    else if (data.subCmd == "select")
    {
        float range;

        psPath * path = pathNetwork->FindNearestPath(myPos,mySector,100.0,&range);
        if (!path)
        {
            client->PathSetPath(NULL);
            psserver->SendSystemError( me->clientnum, "Didn't find any path close by");
            return;
        }
        client->PathSetPath(path);
        psserver->SendSystemInfo( me->clientnum, "Selected path %s(%d) from %s(%d) to %s(%d) at range %.1f",
                                  path->GetName(),path->GetID(),path->start->GetName(),path->start->GetID(),
                                  path->end->GetName(),path->end->GetID(),range);
    }
    else if (data.subCmd == "split")
    {
        float range;

        psPath * path = pathNetwork->FindNearestPath(myPos,mySector,100.0,&range);
        if (!path)
        {
            psserver->SendSystemError( me->clientnum, "Didn't find any path close by");
            return;
        }

        if (client->WaypointGetPathName() == "")
        {
            psserver->SendSystemError( me->clientnum, "No path format set yet.");
            return;
        }

        csString wpName;
        wpName.Format(client->WaypointGetPathName(),client->WaypointGetNewPathIndex());
            
        Waypoint * existing = pathNetwork->FindWaypoint(wpName);
        if (existing)
        {
            psserver->SendSystemError( me->clientnum, "Waypoint already exists with the name %s", wpName.GetDataSafe());
            return;
        }

        Waypoint * wp = pathNetwork->CreateWaypoint(wpName,myPos,mySectorName,data.radius,data.attribute);
        if (!wp)
        {
            return;
        }
        if (client->WaypointIsDisplaying())
        {
            psEffectMessage msg(me->clientnum,"admin_waypoint",myPos,0,0,client->WaypointGetEffectID());
            msg.SendMessage();
        }

        psPath * path1 = pathNetwork->CreatePath("",path->start,wp, "" );
        psPath * path2 = pathNetwork->CreatePath("",wp,path->end, "" );
        
        // Warning: This will delete all path points. So they has to be rebuild for the new segments.
        pathNetwork->Delete(path);

        psserver->SendSystemInfo( me->clientnum, "Splitted %s(%d) into %s(%d) and %s(%d)",
                                  path->GetName(),path->GetID(),
                                  path1->GetName(),path1->GetID(),
                                  path2->GetName(),path2->GetID());
    }
}


int AdminManager::LocationCreate(int typeID, csVector3& pos, csString& sectorName, csString & name)
{
    const char *fieldnames[]=
        {
            "type_id",
            "id_prev_loc_in_region",
            "name",
            "x",
            "y",
            "z",
            "radius",
            "angle",
            "flags",
            "loc_sector_id",
            
        };

    psSectorInfo * si = CacheManager::GetSingleton().GetSectorInfoByName(sectorName);
    
    psStringArray values;
    values.FormatPush("%u", typeID );
    values.FormatPush("%d", -1 );
    values.FormatPush("%s", name.GetDataSafe() );
    values.FormatPush("%10.2f",pos.x);
    values.FormatPush("%10.2f",pos.y);
    values.FormatPush("%10.2f",pos.z);
    values.FormatPush("%d", 0 );
    values.FormatPush("%.2f", 0.0 );
    values.FormatPush("%s", "" );
    values.FormatPush("%u",si->uid);
    
    unsigned int id = db->GenericInsertWithID("sc_locations",fieldnames,values);
    if (id==0)
    {
        Error2("Failed to create new Location Error %s",db->GetLastError());
        return -1;
    }
    
    return id;
}

void AdminManager::HandleLocation(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data, Client *client)
{
    if ( !data.subCmd.Length() || data.subCmd == "help")
    {
        psserver->SendSystemInfo( me->clientnum, "/location help\n"
                                  "/location add <type> <name>\n"
                                  "/location adjust\n"
                                  "/location display\n"
                                  "/location hide");
    }
    else if (data.subCmd == "add")
    {
        csVector3 myPos;
        float myRotY;
        iSector* mySector = 0;
        int loc_id;
        int typeID = 0;
        
        Result rs(db->Select("SELECT id from sc_location_type where name = '%s'",data.type.GetDataSafe()));
        if (!rs.IsValid())
        {
            Error2("Could not load location type from db: %s",db->GetLastError() );
            return ;
        }
        if (rs.Count() != 1)
        {
            psserver->SendSystemInfo( me->clientnum, "Location type not found");
            return;
        }
        
        typeID = rs[0].GetInt("id");
        
        client->GetActor()->GetPosition(myPos, myRotY, mySector);
        csString sectorName = mySector->QueryObject()->GetName();
        
        loc_id = LocationCreate(typeID,myPos,sectorName,data.name);

        psserver->SendSystemInfo( me->clientnum, "Created new Location %u",loc_id);
    }
    else if (data.subCmd == "adjust")
    {
        csVector3 myPos;
        float myRotY,distance=10000.0;
        iSector* mySector = 0;
        int loc_id = -1;
        
        client->GetActor()->GetPosition(myPos, myRotY, mySector);
        csString sectorName = mySector->QueryObject()->GetName();
        
        Result rs(db->Select("select loc.* from sc_locations loc, sectors s where loc.loc_sector_id = s.id and s.name ='%s'",sectorName.GetDataSafe()));

        if (!rs.IsValid())
        {
            Error2("Could not load location from db: %s",db->GetLastError() );
            return ;
        }
        
        for (int i=0; i<(int)rs.Count(); i++)
        {
            
            csVector3 pos(rs[i].GetFloat("x"),rs[i].GetFloat("y"),rs[i].GetFloat("z"));
            if ((pos-myPos).SquaredNorm() < distance)
            {
                distance = (pos-myPos).SquaredNorm();
                loc_id = rs[i].GetInt("id");
            }
        }
        
        if (distance >= 10.0)
        {
            psserver->SendSystemInfo(me->clientnum, "To far from any locations to adjust.");
            return;
        }
        
        db->CommandPump("UPDATE sc_locations SET x=%.2f,y=%.2f,z=%.2f WHERE id=%d",
                    myPos.x,myPos.y,myPos.z,loc_id);
        
        if (client->LocationIsDisplaying())
        {
            psEffectMessage msg(me->clientnum,"admin_location",myPos,0,0,client->LocationGetEffectID());
            msg.SendMessage();
        }
        
        psserver->SendSystemInfo(me->clientnum, "Adjusted location %d",loc_id);
    }
    else if (data.subCmd == "display")
    {
        csVector3 myPos;
        float myRotY;
        iSector* mySector = 0;
        
        client->GetActor()->GetPosition(myPos, myRotY, mySector);
        csString sectorName = mySector->QueryObject()->GetName();

        Result rs(db->Select("select loc.* from sc_locations loc, sectors s where loc.loc_sector_id = s.id and s.name ='%s'",sectorName.GetDataSafe()));

        if (!rs.IsValid())
        {
            Error2("Could not load locations from db: %s",db->GetLastError() );
            return ;
        }
        
        for (int i=0; i<(int)rs.Count(); i++)
        {
            
            csVector3 pos(rs[i].GetFloat("x"),rs[i].GetFloat("y"),rs[i].GetFloat("z"));
            psEffectMessage msg(me->clientnum,"admin_location",pos,0,0,client->LocationGetEffectID());
            msg.SendMessage();
        }
        
        client->LocationSetIsDisplaying(true);
        psserver->SendSystemInfo(me->clientnum, "Displaying all Locations in sector");
    }
    else if (data.subCmd == "hide")
    {
        psStopEffectMessage msg(me->clientnum,client->LocationGetEffectID());
        msg.SendMessage();
        client->LocationSetIsDisplaying(false);
        psserver->SendSystemInfo(me->clientnum, "All Locations hidden");
    }
}


bool AdminManager::GetTargetOfTeleport(Client *client, psAdminCmdMessage& msg, AdminCmdData& data, iSector * & targetSector,  csVector3 & targetPoint, float &yRot, gemObject *subject, int &instance)
{
    instance = 0;

    // when teleporting to a map
    if (data.target == "map")
    {
        if (data.sector.Length())
        {
            // Verify the location first. Invalid values can crash the proxlist.
            if (fabs(data.x) > 100000 || fabs(data.y) > 100000 || fabs(data.z) > 100000)
            {
                psserver->SendSystemError(client->GetClientNum(), "Invalid location for teleporting");
                return false;
            }

            targetSector = EntityManager::GetSingleton().GetEngine()->FindSector(data.sector);
            if (!targetSector)
            {
                psserver->SendSystemError(client->GetClientNum(), "Cannot find sector " + data.sector);
                return false;
            }
            targetPoint = csVector3(data.x, data.y, data.z);
            if (data.instanceValid)
            {
                instance = data.instance;
            }
            else
            {
                instance = client->GetActor()->GetInstance();
            }
        }
        else
        {
             return GetStartOfMap(client, data.map, targetSector, targetPoint);
        }
    }
    // when teleporting to the place where we are standing at
    else if (data.target == "here")
    {
        client->GetActor()->GetPosition(targetPoint, yRot, targetSector);
        if (data.instanceValid)
        {
            instance = data.instance;
        }
        else
        {
            instance = client->GetActor()->GetInstance();
        }
    }
    // Teleport to last valid location (force unstick/teleport undo)
    else if (data.target == "last")
    {
        if ( dynamic_cast<gemActor*>(subject) )
        {
            ((gemActor*)subject)->GetValidPos(targetPoint, yRot, targetSector);
        }
        else
        {
            return false; // Actors only
        }
    }
    // Teleport to spawn point
    else if (data.target == "spawn")
    {
        if ( dynamic_cast<gemActor*>(subject) )
        {
           ((gemActor*)subject)->GetSpawnPos(targetPoint, yRot, targetSector);
        }
        else
        {
            return false; // Actors only
        }
    }
    // Teleport to target
    else if (data.target == "target")
    {
        gemObject* obj = client->GetTargetObject();
        if (!obj)
        {
            return false;
        }
        
        obj->GetPosition(targetPoint, yRot, targetSector);
        instance = obj->GetInstance();
    }
    // when teleporting to a player/npc
    else
    {
        Client * player = FindPlayerClient(data.target);
        if (player)
        {
            player->GetActor()->GetPosition(targetPoint, yRot, targetSector);
            instance = player->GetActor()->GetInstance();
        }
        else
        {
            gemObject* obj = FindObjectByString(data.target); // Find by ID or name
            if (!obj) // Didn't find
            {
                return false;
            }

            obj->GetPosition(targetPoint, yRot, targetSector);
            instance = obj->GetInstance();
        }
    }
    return true;
}

bool AdminManager::GetStartOfMap(Client * client, const csString & map, iSector * & targetSector,  csVector3 & targetPoint)
{
    iEngine* engine = EntityManager::GetSingleton().GetEngine();
    iRegionList* regions = engine->GetRegions();

    assert(regions);

    if ( map.Length() == 0 )
    {
        psserver->SendSystemError( client->GetClientNum(), "Map name not given");
        return false;
    }

    iRegion* region = regions->FindByName(map.GetData());
    if (region == NULL)
    {
        psserver->SendSystemError(client->GetClientNum(), "Map not found.");
        return false;
    }

    iCameraPosition* loc = region->FindCameraPosition("Camera01");
    if (loc == NULL)
    {
        psserver->SendSystemError(client->GetClientNum(), "Starting location not found in map.");
        return false;
    }

    targetSector = engine->FindSector(loc->GetSector());
    targetPoint  = loc->GetPosition();
    return true;
}

void AdminManager::Slide(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data, Client *client, gemObject *target)
{    
    if (!target)
    {
        psserver->SendSystemError(me->clientnum, "Invalid target");
        return;
    }

    if (data.direction.IsEmpty())
    {
        psserver->SendSystemError(me->clientnum, "Syntax: /slide [name|'target'] [direction] [distance]\nAllowed directions: U D L R F B T");
        return;
    }

    float slideAmount = (data.amt == 0)?1:data.amt; // default to 1

    if (slideAmount > 1000 || slideAmount < -1000 || slideAmount != slideAmount) // Check bounds and NaN
    {
        psserver->SendSystemError(me->clientnum, "Invalid slide amount");
        return;
    }

    csVector3 pos;
    float yrot;
    iSector* sector = 0;

    target->GetPosition(pos, yrot, sector);

    if (sector)
    {
        switch (toupper(data.direction.GetAt(0)))
        {
            case 'U':
                pos.y += slideAmount;
                break;
            case 'D':
                pos.y -= slideAmount;
                break;
            case 'L':
                pos.x += slideAmount*cosf(yrot);
                pos.z -= slideAmount*sinf(yrot);
                break;
            case 'R':
                pos.x -= slideAmount*cosf(yrot);
                pos.z += slideAmount*sinf(yrot);
                break;
            case 'F':
                pos.x -= slideAmount*sinf(yrot);
                pos.z -= slideAmount*cosf(yrot);
                break;
            case 'B':
                pos.x += slideAmount*sinf(yrot);
                pos.z += slideAmount*cosf(yrot);
                break;
            case 'T':
                slideAmount = (data.amt == 0)?90:data.amt; // defualt to 90 deg
                yrot += slideAmount*PI/180.0; // Rotation units are degrees
                break;
            default:
                psserver->SendSystemError(me->clientnum, "Invalid direction given (Use one of: U D L R F B T)");
                return;
        }

        // Update the object
        if ( !MoveObject(client,target,pos,yrot,sector,client->GetActor()->GetInstance()) )
            return;

        target->UpdateProxList(false); // Update ProxList if needed

        if (target->GetActorPtr() && client->GetActor() != target->GetActorPtr())
            psserver->SendSystemInfo(me->clientnum, "Sliding %s...", target->GetName());
    }
    else
    {
        psserver->SendSystemError(me->clientnum,
            "Invalid sector; cannot slide.  Please contact Planeshift support.");
    }
}

bool AdminManager::MoveObject(Client *client, gemObject *target, csVector3& pos, float yrot, iSector* sector, int instance)
{
    // This is a powerful feature; not everyone is allowed to use all of it
    csString response;
    bool allowedToMoveOthers = CacheManager::GetSingleton().GetCommandManager()->Validate(client->GetSecurityLevel(), "move others", response);
    if ( client->GetActor() != (gemActor*)target && !allowedToMoveOthers )
    {
        psserver->SendSystemError(client->GetClientNum(),response);
        return false;
    }

    if ( dynamic_cast<gemItem*>(target) ) // Item?
    {
        gemItem* item = (gemItem*)target;

        // Check to see if this client has the admin level to move this particular item
        bool extras = CacheManager::GetSingleton().GetCommandManager()->Validate(client->GetSecurityLevel(), "move unpickupables/spawns", response);

        if ( !item->IsPickable() && !extras )
        {
            psserver->SendSystemError(client->GetClientNum(),response);
            return false;
        }

        // Move the item
        item->GetMeshWrapper()->GetMovable()->SetPosition(pos);
        // Rotation
        csMatrix3 matrix = (csMatrix3) csYRotMatrix3 (yrot);
        item->GetMeshWrapper()->GetMovable ()->GetTransform ().SetO2T (matrix);

        item->SetPosition(pos,yrot,sector,instance);

        // Check to see if this client has the admin level to move this spawn point
        if ( item->GetItem()->GetScheduledItem() && extras )
        {
            psserver->SendSystemInfo(client->GetClientNum(), "Moving spawn point for %s", item->GetName());

            // Update spawn pos
            item->GetItem()->GetScheduledItem()->UpdatePosition(pos,sector->QueryObject()->GetName());
        }
    }
    else if ( dynamic_cast<gemActor*>(target) ) // Actor? (Player/NPC)
    {
        gemActor* actor = (gemActor*)target;
        actor->pcmove->SetVelocity(csVector3(0.0f,0.0f,0.0f)); // Halt actor
        actor->SetInstance(instance);
        actor->SetPosition(pos,yrot,sector);
        actor->GetCharacterData()->SaveLocationInWorld(); // force save the pos
        actor->MulticastDRUpdate();
    }
    else
    {
        psserver->SendSystemError(client->GetClientNum(),"Unknown target type");
        return false;
    }

    return true;
}

void AdminManager::CreateNPC(MsgEntry* me,psAdminCmdMessage& msg, AdminCmdData& data,Client *client, gemActor* basis)
{        
    if (!basis || !basis->GetCharacterData())
    {
        psserver->SendSystemError(me->clientnum, "Invalid target");
        return;
    }

    unsigned int masterNPCID = 0;
    gemNPC *masternpc = basis->GetNPCPtr();
    if (masternpc)
    {
        masterNPCID = masternpc->GetCharacterData()->GetCharacterID();
    }
    if (masterNPCID == 0)
    {
        psserver->SendSystemError(me->clientnum, "%s was not found as a valid master NPC", basis->GetName() );
        return;
    }

    if ( !psserver->GetConnections()->FindAccount(masternpc->GetSuperclientID()) )
    {
        psserver->SendSystemError(me->clientnum, "%s's superclient is not online", basis->GetName() );
        return;
    }

    csVector3 pos;
    float angle;
    psSectorInfo* sectorInfo = NULL;
    int instance;
    client->GetActor()->GetCharacterData()->GetLocationInWorld(instance,sectorInfo, pos.x, pos.y, pos.z, angle );

    iSector* sector = NULL;
    if (sectorInfo != NULL)
        sector = EntityManager::GetSingleton().FindSector(sectorInfo->name);
    if (sector == NULL)
    {
        psserver->SendSystemError(me->clientnum, "Invalid sector");
        return;
    }

    // Copy the master NPC into a new player record, with all child tables also
    unsigned int newNPCID = CopyNPCFromDatabase(masterNPCID, pos.x, pos.y, pos.z, angle, sectorInfo->name, instance );
    if (newNPCID == 0)
    {
        psserver->SendSystemError(me->clientnum, "Could not copy the master NPC");
        return;
    }

    psserver->npcmanager->NewNPCNotify(newNPCID, masterNPCID,-1);

    // Make new entity
    PS_ID eid = EntityManager::GetSingleton().CreateNPC(newNPCID, false);
        
    // Get gemNPC for new entity
    gemNPC* npc = GEMSupervisor::GetSingleton().FindNPCEntity(newNPCID);
    if (npc == NULL)
    {
        psserver->SendSystemError(client->GetClientNum(), "Could not find GEM and set its location");
        return;
    }

    npc->GetCharacterData()->SetLocationInWorld(instance,sectorInfo, pos.x, pos.y, pos.z, angle);
    npc->SetPosition(pos, angle, sector);

    psserver->npcmanager->ControlNPC(npc);

    npc->UpdateProxList(true);

    psserver->SendSystemInfo(me->clientnum, "New %s with PID %u and EID %u at (%1.2f,%1.2f,%1.2f) in %s.",
                                            npc->GetName(), newNPCID, eid,
                                            pos.x, pos.y, pos.z, sectorInfo->name.GetData() );
    psserver->SendSystemOK(me->clientnum, "New NPC created!");
}


int AdminManager::CopyNPCFromDatabase(int master_id, float x, float y, float z, float angle, const csString & sector, int instance)
{
    psCharacter* npc = NULL;
    int new_id;

    npc = psServer::CharacterLoader.LoadCharacterData(master_id,false);
    if (npc == NULL)
        return 0;

    psSectorInfo* sectorInfo = CacheManager::GetSingleton().GetSectorInfoByName( sector );
    if (sectorInfo != NULL)
        npc->SetLocationInWorld(instance,sectorInfo,x,y,z,angle);

    if (psServer::CharacterLoader.NewNPCCharacterData(0, npc))
    {
        new_id = npc->GetCharacterID();
        db->CommandPump("update characters set npc_master_id=%i where id=%i", master_id, new_id);
    }
    else
        new_id = 0;

    delete npc;

    return new_id;
}

void AdminManager::CreateItem(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data,Client *client)
{
    csVector3 pos;
    iSector*  sector = 0;
    float angle;
    int instance;

    client->GetActor()->GetPosition(pos, angle, sector);
    instance = client->GetActor()->GetInstance();

    if (data.item == "help")
    {
        psserver->SendSystemError(me->clientnum, "Syntax: /item or /item <name>|[help] [random] [<quality>]");
        return;
    }

    Debug4(LOG_ADMIN,me->clientnum,  "Created item %s %s with quality %d\n",data.item.GetDataSafe(),data.random?"random":"",data.value )

    // TODO: Get number of items to create from client
    int stackCount = 1;
    if (CreateItem((const char*)data.item,pos.x,pos.y,pos.z,angle,sector->QueryObject()->GetName(),instance,stackCount,data.random,data.value))
    {
        psserver->SendSystemInfo(me->clientnum, "New item %s added!",data.item.GetData());
    }
    else
    {
        psserver->SendSystemError(me->clientnum, "Can't create item %s!",data.item.GetData());
    }
}

bool AdminManager::CreateItem(const char * name, double xPos, double yPos, double zPos, float angle, const char * sector, int instance, int stackCount, int random, int value)
{
    psSectorInfo *sectorinfo = CacheManager::GetSingleton().GetSectorInfoByName(sector);
    if (sectorinfo==NULL)
    {
        Error2("'%s' was not found as a valid sector.",sector);
        return false;
    }

    if ( name == NULL )
    {
        Error1( "No item name was given" );
        return false;
    }
    
    // retrieve base stats item
    psItemStats *basestats=CacheManager::GetSingleton().GetBasicItemStatsByName(name);
    if (basestats==NULL)
    {
        Error2("'%s' was not found as a valid base item.",name);
        return false;
    }

    psItem *newitem = NULL;

    // randomize if requested
    if (random) 
    {
        LootRandomizer* lootRandomizer = psserver->GetSpawnManager()->GetLootRandomizer();
        psItemStats *newstats = lootRandomizer->RandomizeItem( basestats, value );
        newitem = newstats->InstantiateBasicItem(true);
    } 
    else
    {
        newitem = basestats->InstantiateBasicItem(true);
    }

    if (value > 0)
    {
        newitem->SetItemQuality((float)value);
        // Setting craftet quality as well if quality given by user
        newitem->SetMaxItemQuality((float)value);
    }
    else
    {
        newitem->SetItemQuality(basestats->GetQuality());
    }
        

    if (newitem==NULL)
    {
        Error2("Could not instanciate from base item '%s'.",name);
        return false;
    }

    newitem->SetStackCount(stackCount);
    newitem->SetLocationInWorld(instance,sectorinfo,xPos,yPos,zPos,angle);

    if (!EntityManager::GetSingleton().CreateItem(newitem, true))
    {
        delete newitem;
        return false;
    }

    newitem->SetLoaded();  // Item is fully created
    newitem->Save(false);    // First save

    return true;
}

void AdminManager::ModifyKey(MsgEntry *me, psAdminCmdMessage& msg, AdminCmdData& data,Client *client)
{
    // Give syntax
    if(data.subCmd.Length() == 0)
    {
        psserver->SendSystemError(me->clientnum, "Syntax: /key [changelock|makeunlockable|securitylockable|make|makemaster|copy|clearlocks|addlock|removelock|skel]");
        return;
    }

    // Exchange lock on targeted item
    //  this actually removes the ability to unlock this lock from all the keys
    if ( data.subCmd == "changelock" )
    {
        ChangeLock(me, msg, data, client);
        return;
    }

    // Change lock to allow it to be unlocked
    if (data.subCmd == "makeunlockable")
    {
        MakeUnlockable(me, msg, data, client);
        return;
    }
    
    // Change lock to allow it to be unlocked
    if (data.subCmd == "securitylockable")
    {
        MakeSecurity(me, msg, data, client);
        return;
    }
    
    // Make a key out of item in right hand
    if (data.subCmd == "make")
    {
        MakeKey(me, msg, data, client, false);
        return;
    }
    
    // Make a master key out of item in right hand
    if (data.subCmd == "makemaster")
    {
        MakeKey(me, msg, data, client, true);
        return;
    }

    // Find key item from hands
    psItem* key = client->GetCharacterData()->Inventory().GetInventoryItem(PSCHARACTER_SLOT_RIGHTHAND);
    if ( !key || !key->GetIsKey() )
    {
        key = client->GetCharacterData()->Inventory().GetInventoryItem(PSCHARACTER_SLOT_LEFTHAND);
        if ( !key || !key->GetIsKey() )
        {
            psserver->SendSystemError(me->clientnum,"You need to be holding the key you want to work on");
            return;
        }
    }

    // Make or unmake key a skeleton key that will open any lock
    if ( data.subCmd == "skel" )
    {
        bool b = key->GetIsSkeleton();
        key->MakeSkeleton(!b);
        if (b)
            psserver->SendSystemInfo(me->clientnum, "Your %s is no longer a skeleton key", key->GetName());
        else
            psserver->SendSystemInfo(me->clientnum, "Your %s is now a skeleton key", key->GetName());
        key->Save(false);
        return;
    }

    // Copy key item
    if ( data.subCmd == "copy" )
    {
        CopyKey(me, msg, data, client, key);
    }

    // Clear all locks that key can open
    if ( data.subCmd == "clearlocks" )
    {
        key->ClearOpenableLocks();
        key->Save(false);
        psserver->SendSystemInfo(me->clientnum, "Your %s can no longer unlock anything", key->GetName());
        return;
    }

    // Add or remove keys ability to lock targeted lock 
    if ( data.subCmd == "addlock" || data.subCmd == "removelock")
    {
        AddRemoveLock(me, msg, data, client, key);
        return;                
    }
}

void AdminManager::CopyKey(MsgEntry *me, psAdminCmdMessage& msg, AdminCmdData& data, Client *client, psItem* key )
{
    // check if item is master key
    if (!key->GetIsMasterKey())
    {
        psserver->SendSystemInfo(me->clientnum, "Only a master key can be copied.");
        return;
    }

    // get stats
    psItemStats* keyStats = key->GetBaseStats();
    if (!keyStats)
    {
        Error2("Could not get base stats for item (%s).", key->GetName() );
        psserver->SendSystemError(me->clientnum,"Could not get base stats for key!");
        return;
    }

    // make a perminent new item
    psItem* newKey = keyStats->InstantiateBasicItem();
    if (!newKey)
    {
        Error2("Could not create item (%s).", keyStats->GetName() );
        psserver->SendSystemError(me->clientnum,"Could not create key!");
        return;
    }

    // copy item characteristics
    newKey->SetItemQuality(key->GetItemQuality());
    newKey->SetStackCount(key->GetStackCount());

    // copy over key characteristics
    newKey->SetIsKey(true);
    newKey->SetLockpickSkill(key->GetLockpickSkill());
    newKey->CopyOpenableLock(key);

    // get client info
    if (!client)
    {
        Error1("Bad client pointer for key copy.");
        psserver->SendSystemError(me->clientnum,"Bad client pointer for key copy!");
        return;
    }
    psCharacter* charData = client->GetCharacterData();
    if (!charData)
    {
        Error2("Could not get character data for (%s).", client->GetName());
        psserver->SendSystemError(me->clientnum,"Could not get character data!");
        return;
    }

    // put into inventory
    newKey->SetLoaded();
    if (charData->Inventory().Add(newKey))
    {
        psserver->SendSystemInfo(me->clientnum, "A copy of %s has been spawned into your inventory", key->GetName());
    }
    else
    {
        psserver->SendSystemInfo(me->clientnum, "Couldn't spawn %s to your inventory, maybe it's full?", key->GetName());
        CacheManager::GetSingleton().RemoveInstance(newKey);
    }
    psserver->GetCharManager()->UpdateItemViews(me->clientnum);
}

void AdminManager::MakeUnlockable(MsgEntry *me, psAdminCmdMessage& msg, AdminCmdData& data, Client *client)
{
    // check if player has something targeted
    gemObject* target = client->GetTargetObject();
    if (!target)
    {
        psserver->SendSystemError(me->clientnum,"You need to target the lock you want to make unlockable.");
    }

    // Check if target is action item
    gemActionLocation* gemAction = dynamic_cast<gemActionLocation*>(target);
    if(gemAction) {
        psActionLocation *action = gemAction->GetAction();

        // check if the actionlocation is linked to real item
        uint32 instance_id = action->GetInstanceID();
        if (instance_id== (uint32)-1)
        {
            instance_id = action->GetGemObject()->GetEntity()->GetID();
        }
        target = GEMSupervisor::GetSingleton().FindItemEntity( instance_id );
        if (!target)
        {
            psserver->SendSystemError(me->clientnum,"There is no item assoviated with this action location.");
            return;
        }
    }

    // Get targeted item
    psItem* item = target->GetItem();
    if ( !item )
    {
        Error1("Found gemItem but no psItem was attached!\n");
        psserver->SendSystemError(me->clientnum,"Found gemItem but no psItem was attached!");
        return;
    }
        
    // Flip the lockability
    if (item->GetIsLockable())
    {
        item->SetIsLockable(false);
        psserver->SendSystemInfo(me->clientnum, "The lock was set to be non unlockable.", item->GetName());
    }
    else
    {
        item->SetIsLockable(true);
        psserver->SendSystemInfo(me->clientnum, "The lock was set to be unlockable.", item->GetName());
    }
}

void AdminManager::MakeSecurity(MsgEntry *me, psAdminCmdMessage& msg, AdminCmdData& data, Client *client)
{
    // check if player has something targeted
    gemObject* target = client->GetTargetObject();
    if (!target)
    {
        psserver->SendSystemError(me->clientnum,"You need to target the lock you want to make a security lock.");
    }

    // Check if target is action item
    gemActionLocation* gemAction = dynamic_cast<gemActionLocation*>(target);
    if(gemAction) {
        psActionLocation *action = gemAction->GetAction();

        // check if the actionlocation is linked to real item
        uint32 instance_id = action->GetInstanceID();
        if (instance_id== (uint32)-1)
        {
            instance_id = action->GetGemObject()->GetEntity()->GetID();
        }
        target = GEMSupervisor::GetSingleton().FindItemEntity( instance_id );
        if (!target)
        {
            psserver->SendSystemError(me->clientnum,"There is no item assoviated with this action location.");
            return;
        }
    }

    // Get targeted item
    psItem* item = target->GetItem();
    if ( !item )
    {
        Error1("Found gemItem but no psItem was attached!\n");
        psserver->SendSystemError(me->clientnum,"Found gemItem but no psItem was attached!");
        return;
    }
        
    // Flip the security lockability
    if (item->GetIsSecurityLocked())
    {
        item->SetIsSecurityLocked(false);
        psserver->SendSystemInfo(me->clientnum, "The lock was set to be non security lock.", item->GetName());
    }
    else
    {
        item->SetIsSecurityLocked(true);
        psserver->SendSystemInfo(me->clientnum, "The lock was set to be security lock.", item->GetName());
    }
}

void AdminManager::MakeKey(MsgEntry *me, psAdminCmdMessage& msg, AdminCmdData& data, Client *client, bool masterkey)
{
    psItem* key = client->GetCharacterData()->Inventory().GetInventoryItem(PSCHARACTER_SLOT_RIGHTHAND);
    if ( !key )
    {
        psserver->SendSystemError(me->clientnum,"You need to hold the item you want to make into a key in your right hand.");
        return;
    }
    if (masterkey)
    {
        if ( key->GetIsMasterKey() )
        {
            psserver->SendSystemError(me->clientnum,"Your %s is already a master key.", key->GetName());
            return;
        }
        key->SetIsKey(true);
        key->SetIsMasterKey(true);
        psserver->SendSystemOK(me->clientnum,"Your %s is now a master key.", key->GetName());
    }
    else
    {
        if ( key->GetIsKey() )
        {
            psserver->SendSystemError(me->clientnum,"Your %s is already a key.", key->GetName());
            return;
        }
        key->SetIsKey(true);
        psserver->SendSystemOK(me->clientnum,"Your %s is now a key.", key->GetName());
    }

    key->Save(false);
}

void AdminManager::AddRemoveLock(MsgEntry *me, psAdminCmdMessage& msg, AdminCmdData& data, Client *client, psItem* key )
{
    // check if player has something targeted
    gemObject* target = client->GetTargetObject();
    if (!target)
    {
        if ( data.subCmd == "addlock" )
            psserver->SendSystemError(me->clientnum,"You need to target the item you want to encode the key to unlock");
        else
            psserver->SendSystemError(me->clientnum,"You need to target the item you want to stop the key from unlocking");
        return;
    }

    // Check if target is action item
    gemActionLocation* gemAction = dynamic_cast<gemActionLocation*>(target);
    if(gemAction) {
        psActionLocation *action = gemAction->GetAction();

        // check if the actionlocation is linked to real item
        uint32 instance_id = action->GetInstanceID();
        if (instance_id == (uint32)-1)
        {
            instance_id = action->GetGemObject()->GetEntity()->GetID();
        }
        target = GEMSupervisor::GetSingleton().FindItemEntity( instance_id );
        if (!target)
        {
            psserver->SendSystemError(me->clientnum,"There is no item assoviated with this action location.");
            return;
        }
    }

    // Get targeted item
    psItem* item = target->GetItem();
    if ( !item )
    {
        Error1("Found gemItem but no psItem was attached!\n");
        psserver->SendSystemError(me->clientnum,"Found gemItem but no psItem was attached!");
        return;
    }
        
    if(!item->GetIsLockable())
    {
        psserver->SendSystemError(me->clientnum,"This object isn't lockable");
        return;
    }

    if ( data.subCmd == "addlock" )
    {
        key->AddOpenableLock(item->GetUID());
        key->Save(false);
        psserver->SendSystemInfo(me->clientnum, "You encoded %s to unlock %s", key->GetName(), item->GetName());
    }
    else
    {
        key->RemoveOpenableLock(item->GetUID());
        key->Save(false);
        psserver->SendSystemInfo(me->clientnum, "Your %s can no longer unlock %s", key->GetName(), item->GetName());
    }
}

void AdminManager::ChangeLock(MsgEntry *me, psAdminCmdMessage& msg, AdminCmdData& data, Client *client)
{
    // check if player has something targeted
    gemObject* target = client->GetTargetObject();
    if (!target)
    {
        psserver->SendSystemError(me->clientnum,"You need to target the item for which you want to change the lock");
        return;
    }

    // check for action location
    gemActionLocation* gemAction = dynamic_cast<gemActionLocation*>(target);
    if(gemAction) {
        psActionLocation *action = gemAction->GetAction();

        // check if the actionlocation is linked to real item
        uint32 instance_id = action->GetInstanceID();
        if (instance_id == (uint32)-1)
        {
            instance_id = action->GetGemObject()->GetEntity()->GetID();
        }
        target = GEMSupervisor::GetSingleton().FindItemEntity( instance_id );
        if (!target)
        {
            psserver->SendSystemError(me->clientnum,"There is no item assoviated with this action location.");
            return;
        }
    }

    // get the old item
    psItem* oldLock = target->GetItem();
    if ( !oldLock )
    {
        Error1("Found gemItem but no psItem was attached!\n");
        psserver->SendSystemError(me->clientnum,"Found gemItem but no psItem was attached!");
        return;
    }                

    // get instance ID
    psString buff;
    uint32 lockID = oldLock->GetUID();
    buff.Format("%u", lockID);

    // Get psItem array of keys to check
    Result items(db->Select("SELECT * from item_instances where flags like '%KEY%'"));
    if ( items.IsValid() )
    {
        for ( int i=0; i < (int)items.Count(); i++ )
        {
            // load openableLocks except for specific lock
            psString word;
            psString lstr = "";
            uint32 keyID = items[i].GetUInt32("id");
            psString olstr(items[i]["openable_locks"]);
            olstr.GetWordNumber(1, word);
            for (int n = 2; word.Length(); olstr.GetWordNumber(n++, word))
            {
                // check for matching lock
                if (word != buff)
                {
                    // add space to sparate since GetWordNumber is used to decode
                    if (lstr != "")
                        lstr.Append(" "); 
                    lstr.Append(word);

                    // write back to database
                    int result = db->CommandPump("UPDATE item_instances SET openable_locks='%s' WHERE id=%d", 
                        lstr.GetData(), keyID);
                    if (result == -1)
                    {
                        Error4("Couldn't update item instance lockchange with lockID=%d keyID=%d openable_locks <%s>.",lockID, keyID, lstr.GetData());
                        return;
                    }

                    // reload inventory if key belongs to this character
                    uint32 ownerID = items[i].GetUInt32("char_id_owner");
                    psCharacter* character = client->GetCharacterData();
                    if( character->GetCharacterID() == ownerID )
                    {
                        character->Inventory().Load();
                    }
                    break;
                }
            }
        }
    }
    psserver->SendSystemInfo(me->clientnum, "You changed the lock on %s", oldLock->GetName());
}

void AdminManager::KillNPC (MsgEntry *me, psAdminCmdMessage& msg, AdminCmdData& data, Client *client )
{
    gemObject* obj = NULL;

    if(data.target.Length() != 0)
        if (data.target.FindFirst(':')!=(size_t)-1) 
            obj = GEMSupervisor::GetSingleton().FindNPCEntity(atoi(data.target.Slice(3).GetData()));

    if ( !obj )
        obj = client->GetTargetObject();

    if ( obj )
    {
        gemActor *target = obj->GetActorPtr();
        if (target && target->GetClientID() == 0)
        {
            if (data.action != "reload")
                target->Kill(client->GetActor());
            else
            {
                unsigned int npcid = target->GetCharacterData()->GetCharacterID();
                
                psCharacter * npcdata = psServer::CharacterLoader.LoadCharacterData(npcid,true);
                EntityManager::GetSingleton().RemoveActor(obj);
                EntityManager::GetSingleton().CreateNPC(npcdata);
                psserver->SendSystemResult(me->clientnum, "NPC (id %d) has been reloaded.",npcid);
            }
            return;
        }
    }
    psserver->SendSystemError(me->clientnum, "No NPC was targeted.");
}


void AdminManager::Admin ( int playerID, int clientnum,Client *client )
{    
    // Set client security level in case security level have
    // changed in database.
    csString commandList;
    int type = client->GetSecurityLevel();

    // for now consider all levels > 30 as level 30.
    if (type>30) type=30;

    CacheManager::GetSingleton().GetCommandManager()->BuildXML( type, commandList );
    
    psAdminCmdMessage admin(commandList.GetDataSafe(), clientnum);
    admin.SendMessage();
}




void AdminManager::WarnMessage(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data,Client *client,Client *target)
{
    if (!target)
    {
        psserver->SendSystemError(me->clientnum, "Invalid target to warn");
        return;
    }
    
    if (data.reason.Length() == 0)
    {
        psserver->SendSystemError(me->clientnum, "Please enter a warn message");
        return;
    }

    // This message will be shown in adminColor (red) in all chat tabs for this player
    psSystemMessage newmsg(target->GetClientNum(), MSG_INFO_SERVER, "GM warning from %s: " + data.reason, client->GetName());
    newmsg.SendMessage();

    // This message will be in big red letters on their screen
    psserver->SendSystemError(target->GetClientNum(), data.reason);
    db->CommandPump("insert into warnings values(%d, '%s', NOW(), '%s')", target->GetAccountID(), client->GetName(), data.reason.GetData());

    psserver->SendSystemInfo(client->GetClientNum(), "You warned '%s': " + data.reason, target->GetName());
}


void AdminManager::KickPlayer(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data,Client *client,Client *target)
{
    if (!target)
    {
        psserver->SendSystemError(me->clientnum, "Invalid target to kick");
        return;
    }

    if (data.reason.Length() < 5)
    {
        psserver->SendSystemError(me->clientnum, "You must specify a reason to kick");
        return;
    }

    // Remove from server and show the reason message
    psserver->RemovePlayer(target->GetClientNum(),"You were kicked from the server by a GM. Reason: " + data.reason);

    psserver->SendSystemInfo(me->clientnum,"You kicked '%s' off the server.",(const char*)data.player);
}

void AdminManager::Death( MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data, Client *client, gemActor* target)
{    
    if (!target)
    {
        psserver->SendSystemError(me->clientnum,"You can't kill things that are not alive!");
        return;
    }
    
    target->Kill(NULL);  // Have a nice day ;)

    if (target->GetClientID() != 0)
        psserver->SendSystemError(target->GetClientID(), "You were killed by a GM");
}


void AdminManager::Impersonate( MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data, Client *client)
{        if (data.player.IsEmpty() || data.text.IsEmpty() || data.commandMod.IsEmpty())
    {
        psserver->SendSystemError(me->clientnum, "Invalid parameters");
        return;
    }
    
    // If no commandMod is given, default to say
    if (data.commandMod != "say" && data.commandMod != "shout" && data.commandMod != "worldshout")
    {
        data.text = data.commandMod + " " + data.text;
        data.commandMod = "say";
    }

    csString sendText; // We need specialised say/shout as it is a special GM chat message

    if (data.player == "text")
        sendText = data.text;
    else
        sendText.Format("%s %ss: %s", data.player.GetData(), data.commandMod.GetData(), data.text.GetData() );

    psChatMessage newMsg(client->GetClientNum(), data.player, 0, sendText, CHAT_GM, false);

    gemObject* source = (gemObject*)client->GetActor();

    // Invisible; multicastclients list is empty
    if (!source->GetVisibility() && data.commandMod != "worldshout")
    {
        // Try to use target as source
        source = client->GetTargetObject();

        if (source == NULL || source->GetClientID() == client->GetClientNum())
        {
            psserver->SendSystemError(me->clientnum, "Invisible; select a target to use as source");
            return;
        }
    }

    if (data.commandMod == "say")
        newMsg.Multicast(source->GetMulticastClients(), 0, CHAT_SAY_RANGE);
    else if (data.commandMod == "shout")
        newMsg.Multicast(source->GetMulticastClients(), 0, PROX_LIST_ANY_RANGE);
    else if (data.commandMod == "worldshout")
        psserver->GetEventManager()->Broadcast(newMsg.msg, NetBase::BC_EVERYONE);
    else
        psserver->SendSystemInfo(me->clientnum, "Syntax: /impersonate name command text\nCommand can be one of say, shout, or worldshout.\nIf name is \"text\" the given text will be the by itself.");
}

void AdminManager::MutePlayer(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data, Client *client, Client *target)
{
    if (!target)
    {
        psserver->SendSystemError(me->clientnum, "Invalid target to mute");
        return;
    }

    psserver->MutePlayer(target->GetClientNum(),"You were muted by a GM, until log off.");

    // Finally, notify the GM that the client was successfully muted
    psserver->SendSystemInfo(me->clientnum, "You muted '%s' until he/she/it logs back in.",(const char*)data.player);
}


void AdminManager::UnmutePlayer(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data, Client *client, Client *target)
{
    if (!target)
    {
        psserver->SendSystemError(me->clientnum, "Invalid target to unmute");
        return;
    }

    psserver->UnmutePlayer(target->GetClientNum(),"You were unmuted by a GM.");

    // Finally, notify the GM that the client was successfully unmuted
    psserver->SendSystemInfo(me->clientnum, "You unmuted '%s'.",(const char*)data.player);
}


void AdminManager::HandleAddPetition(MsgEntry *me, psAdminCmdMessage& msg, AdminCmdData& data,Client *client)
{
    if (data.petition.Length() == 0)
    {
        psserver->SendSystemError(me->clientnum,"You must enter a petition question/description after '/petition '");
        return;
    }
    
    // Try and add the petition to the database:
    if (!AddPetition(client->GetPlayerID(), (const char*)data.petition))
    {
        psserver->SendSystemError(me->clientnum,"SQL Error: %s", db->GetLastError());
        return;
    }

    // Tell client the petition was added:
    psserver->SendSystemInfo(me->clientnum, "Your petition was successfully submitted!");

    BroadcastDirtyPetitions(me->clientnum, true);
}

void AdminManager::BroadcastDirtyPetitions(int clientNum, bool includeSelf)
{
    psPetitionMessage dirty(clientNum, NULL, "", true, PETITION_DIRTY, true);
    if (dirty.valid)
    {
        if (includeSelf)
            psserver->GetEventManager()->Broadcast(dirty.msg, NetBase::BC_EVERYONE);
        else
            psserver->GetEventManager()->Broadcast(dirty.msg, NetBase::BC_EVERYONEBUTSELF);
    }
}

void AdminManager::ListPetitions(MsgEntry *me, psPetitionRequestMessage& msg,Client *client)
{
    // Try and grab the result set from the database:
    iResultSet *rs = GetPetitions(client->GetPlayerID());
    if (rs)
    {
        // Send list to client:
        csArray<psPetitionInfo> petitions;
        psPetitionInfo info;
        for (unsigned int i=0; i<rs->Count(); i++)
        {
            // Set info
            info.id = atoi((*rs)[i][0]);
            info.petition = (*rs)[i][1];
            info.status = (*rs)[i][2];
            info.created = csString((*rs)[i][3]).Slice(0, 16);
            info.assignedgm = (*rs)[i][4];
            if (info.assignedgm.Length() == 0)
			{
                info.assignedgm = "No GM Assigned";
			}
			info.resolution = (*rs)[i][5];
			if (info.resolution.Length() == 0)
			{
				info.resolution = "No Resolution";
			}

            // Append to the message:
            petitions.Push(info);
        }

        psPetitionMessage message(me->clientnum, &petitions, "List retrieved successfully.", true, PETITION_LIST);
        message.SendMessage();
        rs->Release();
    }
    else
    {
        // Return no succeed message to client
        csString error;
        error.Format("SQL Error: %s", db->GetLastError());
        psPetitionMessage message(me->clientnum, NULL, error, false, PETITION_LIST);
        message.SendMessage();
    }
}

void AdminManager::CancelPetition(MsgEntry *me, psPetitionRequestMessage& msg,Client *client)
{
    // Tell the database to change the status of this petition:
    if (!CancelPetition(client->GetPlayerID(), msg.id))
    {
        psPetitionMessage error(me->clientnum, NULL, db->GetLastError(), false, PETITION_CANCEL);
        error.SendMessage();
        return;
    }

    // Try and grab the result set from the database:
    iResultSet *rs = GetPetitions(client->GetPlayerID());
    if (rs)
    {
        // Send list to client:
        csArray<psPetitionInfo> petitions;
        psPetitionInfo info;
        for (unsigned int i=0; i<rs->Count(); i++)
        {
            // Set info
            info.id = atoi((*rs)[i][0]);
            info.petition = (*rs)[i][1];
            info.status = (*rs)[i][2];
            info.created = (*rs)[i][3];
            info.assignedgm = (*rs)[i][4];
            if (info.assignedgm.Length() == 0)
			{
                info.assignedgm = "No GM Assigned";
			}
			if (info.resolution.Length() == 0)
			{
				info.resolution = "No Resolution";
			}

            // Append to the message:
            petitions.Push(info);
        }

        psPetitionMessage message(me->clientnum, &petitions, "Cancel was successful.", true, PETITION_CANCEL);
        message.SendMessage();
        rs->Release();
    }
    else
    {
        // Tell client deletion was successful:
        psPetitionMessage message(me->clientnum, NULL, "Cancel was successful.", true, PETITION_CANCEL);
        message.SendMessage();
    }
    BroadcastDirtyPetitions(me->clientnum);
}


void AdminManager::GMListPetitions(MsgEntry *me, psPetitionRequestMessage& msg,Client *client)
{
    // Check to see if this client has GM level access
    if ( client->GetSecurityLevel() < GM_LEVEL_0 )
    {
        psserver->SendSystemError(me->clientnum, "Access denied. Only GMs can manage petitions.");
        return;
    }

    // Try and grab the result set from the database:
    
    // Show the player all petitions.
    iResultSet *rs = GetPetitions(-1, client->GetPlayerID(), GM_DEVELOPER );
    if (rs)
    {
        // Send list to GM:
        csArray<psPetitionInfo> petitions;
        psPetitionInfo info;
        for (unsigned int i=0; i<rs->Count(); i++)
        {
            // Set info
            info.id = atoi((*rs)[i][0]);
            info.petition = (*rs)[i][1];
            info.status = (*rs)[i][2];
            info.escalation = atoi((*rs)[i][3]);
            info.created = csString((*rs)[i][4]).Slice(0, 16);
            info.player = (*rs)[i][5];

            // Append to the message:
            petitions.Push(info);
        }

        psPetitionMessage message(me->clientnum, &petitions, "List retrieved successfully.", true, PETITION_LIST, true);
        message.SendMessage();
        rs->Release();
    }
    else
    {
        // Return no succeed message to GM
        csString error;
        error.Format("SQL Error: %s", db->GetLastError());
        psPetitionMessage message(me->clientnum, NULL, error, false, PETITION_LIST, true);
        message.SendMessage();
    }
}

void AdminManager::GMHandlePetition(MsgEntry *me, psPetitionRequestMessage& msg,Client *client)
{
    // Check to see if this client has GM level access
    if ( client->GetSecurityLevel() < GM_LEVEL_0 )
    {
        psserver->SendSystemError(me->clientnum, "Access denied. Only GMs can manage petitions.");
        return;
    }

    // Check what operation we are executing based on the request:
    int type = -1;
    bool result = false;
    if (msg.request == "cancel")
    {
        // Cancellation:
        type = PETITION_CANCEL;
        result = CancelPetition(-1, msg.id);
    }
    else if (msg.request == "close")
    {
        // Closing petition:
        type = PETITION_CLOSE;
        result = ClosePetition(client->GetPlayerID(), msg.id, msg.desc);
    }
    else if (msg.request == "assign")
    {
        // Assigning petition:
        type = PETITION_ASSIGN;
        result = AssignPetition(client->GetPlayerID(), msg.id);
    }
    else if (msg.request == "escalate")
    {
        // Escalate petition:
        type = PETITION_ESCALATE;
        result = EscalatePetition(client->GetPlayerID(), client->GetSecurityLevel(), msg.id);
    }

    else if (msg.request == "descalate")
    {
        // Descalate petition:
        type = PETITION_DESCALATE;
        result = DescalatePetition(client->GetPlayerID(), client->GetSecurityLevel(), msg.id);
    }

    // Check result of operation
    if (!result)
    {
        psPetitionMessage error(me->clientnum, NULL, db->GetLastError(), false, type, true);
        error.SendMessage();
        return;
    }

    // Try and grab the result set from the database:
    iResultSet *rs = GetPetitions(-1, client->GetPlayerID(), GM_DEVELOPER);
    if (rs)
    {
        // Send list to GM:
        csArray<psPetitionInfo> petitions;
        psPetitionInfo info;
        for (unsigned int i=0; i<rs->Count(); i++)
        {
            // Set info
            info.id = atoi((*rs)[i][0]);
            info.petition = (*rs)[i][1];
            info.status = (*rs)[i][2];
            info.escalation = atoi((*rs)[i][3]);
            info.created = (*rs)[i][4];
            info.player = (*rs)[i][5];

            // Append to the message:
            petitions.Push(info);
        }        
        // Tell GM operation was successful
        psPetitionMessage message(me->clientnum, &petitions, "Successful", true, type, true);
        message.SendMessage();
        rs->Release();
    }
    else
    {
        // Tell GM operation was successful eventhough we don't have a list of petitions
        psPetitionMessage message(me->clientnum, NULL, "Successful", true, type, true);
        message.SendMessage();
    }
    BroadcastDirtyPetitions(me->clientnum);
}

void AdminManager::SendGMPlayerList(MsgEntry* me, psGMGuiMessage& msg,Client *client)
{
    if ( client->GetSecurityLevel() < GM_LEVEL_1  &&
         client->GetSecurityLevel() > GM_LEVEL_9  && !client->IsSuperClient())
    {
        psserver->SendSystemError(me->clientnum,"You don't have access to GM functions!");
        CPrintf(CON_ERROR, "Client %d tried to get GM player list, but hasn't got GM access!\n");
        return;
    }
    
    csArray<psGMGuiMessage::PlayerInfo> playerList;

    // build the list of players
    Client *curr;
    ClientIterator i(*clients);
    for (curr = i.First(); curr; curr = i.Next())
    {
        if (curr->IsSuperClient() || !curr->GetActor()) continue;

        psGMGuiMessage::PlayerInfo playerInfo;
        
        playerInfo.name = curr->GetName();
        playerInfo.lastName = curr->GetCharacterData()->lastname;
        playerInfo.gender = curr->GetCharacterData()->GetRaceInfo()->gender;

        psGuildInfo *guild = curr->GetCharacterData()->GetGuild();
        if (guild)
            playerInfo.guild = guild->GetName();
        else
            playerInfo.guild = "";

        //Get sector name
        csVector3 vpos;
        float yrot;
        iSector* sector;

        curr->GetActor()->GetPosition(vpos,yrot,sector);

        playerInfo.sector = sector->QueryObject()->GetName();

        playerList.Push(playerInfo);
    }

    // send the list of players
    psGMGuiMessage message(me->clientnum, &playerList, psGMGuiMessage::TYPE_PLAYERLIST);
    message.SendMessage();
}

bool AdminManager::EscalatePetition(int gmID, int gmLevel, int petitionID)
{
    int result = db->CommandPump("UPDATE petitions SET status='Open',assigned_gm=-1,"
                            "escalation_level=(escalation_level+1) "
                            "WHERE id=%d AND escalation_level<=%d AND escalation_level<%d "
                            "AND (assigned_gm=%d OR status='Open')", petitionID, gmLevel, GM_DEVELOPER-20, gmID);
    // If this failed if means that there is a serious error
    if (result == -1)
    {
        lasterror.Format("Couldn't escalate petition #%d.",
            petitionID);
        return false;
    }
    return (result != -1);
}

bool AdminManager::DescalatePetition(int gmID, int gmLevel, int petitionID)
{
    
    int result = db->CommandPump("UPDATE petitions SET status='Open',assigned_gm=-1,"
                            "escalation_level=(escalation_level-1)"
                            "WHERE id=%d AND escalation_level<=%d AND (assigned_gm=%d OR status='Open' AND escalation_level != 0)", petitionID, gmLevel, gmID);
    // If this failed if means that there is a serious error
    if (result == -1)
    {
        lasterror.Format("Couldn't descalate petition #%d.",
            petitionID);
        return false;
    }
    return (result != -1);
}

bool AdminManager::AddPetition(int playerID, const char* petition)
{
    /* The columns in the table NOT included in this command
     * have default values and thus we do not need to put them in
     * the INSERT statement
     */
    csString escape;
    db->Escape( escape, petition );
    int result = db->Command("INSERT INTO petitions "
                             "(player,petition,created_date,status,resolution) "
                             "VALUES (%d,\"%s\",Now(),\"Open\",\"Not Resolved\")",playerID, escape.GetData());

    return (result != -1);
}

iResultSet *AdminManager::GetPetitions(int playerID, int gmID, int gmLevel)
{
    iResultSet *rs;

    // Check player ID (if ID is -1, get a complete list for the GM):
    if (playerID == -1)
    {
        rs = db->Select("SELECT pet.id,pet.petition,pet.status,pet.escalation_level,pet.created_date,pl.name FROM petitions pet, "
                    "characters pl WHERE (pet.player!=%d AND ((pet.status=\"Open\" AND pet.escalation_level<=%d) "
                    "OR (pet.assigned_gm=%d AND pet.status=\"In Progress\"))) "
                    "AND pet.player=pl.id "
                    "ORDER BY pet.status ASC,pet.escalation_level DESC,pet.created_date ASC", gmID, gmLevel, gmID);
    }
    else
    {
        rs = db->Select("SELECT pet.id,pet.petition,pet.status,pet.created_date,pl.name,pet.resolution "
                    "FROM petitions pet LEFT JOIN characters pl "
                    "ON pet.assigned_gm=pl.id "
                    "WHERE pet.player=%d "
                    "AND pet.status!=\"Cancelled\" "
                    "ORDER BY pet.status ASC,pet.escalation_level DESC", playerID);
    }

    if (!rs)
    {
        lasterror = GetLastSQLError();
    }

    return rs;
}

bool AdminManager::CancelPetition(int playerID, int petitionID)
{
    // If player ID is -1, just cancel the petition (a GM is requesting the change)
    if (playerID == -1)
    {
        int result = db->CommandPump("UPDATE petitions SET status='Cancelled' WHERE id=%d", petitionID);
        return (result != -1);
    }

    // Attempt to select this petition; two things can go wrong: it doesn't exist or the player didn't create it
    int result = db->SelectSingleNumber("SELECT id FROM petitions WHERE id=%d AND player=%d", petitionID, playerID);
    if (!result || result <= -1)
    {
        // Failure was due to nonexistant petition or ownership rights:
        lasterror.Format("Couldn't cancel the petition. Either it does not exist, or you did not "
            "create the petition.");
        return false;
    }

    // Update the petition status
    result = db->CommandPump("UPDATE petitions SET status='Cancelled' WHERE id=%d AND player=%d", petitionID, playerID);

    return (result != -1);
}

bool AdminManager::ClosePetition(int gmID, int petitionID, const char* desc)
{
    csString escape;
    db->Escape( escape, desc );
    int result = db->CommandPump("UPDATE petitions SET status='Closed',closed_date=Now(),resolution='%s' "
                             "WHERE id=%d AND assigned_gm=%d", escape.GetData(), petitionID, gmID);

    // If this failed if means that there is a serious error, or the GM was not assigned
    if (result == -1)
    {
        lasterror.Format("Couldn't close petition #%d.  You must be assigned to the petition before you close it.",
            petitionID);
        return false;
    }

    return (result != -1);
}

bool AdminManager::AssignPetition(int gmID, int petitionID)
{
    int result = db->CommandPump("UPDATE petitions SET assigned_gm=%d,status=\"In Progress\" WHERE id=%d AND assigned_gm=-1",gmID, petitionID);

    // If this failed if means that there is a serious error, or another GM was already assigned
    if (result == -1)
    {
        lasterror.Format("Couldn't assign you to petition #%d.  Another GM is already assigned to that petition.",
            petitionID);
        return false;
    }

    return true;
}

bool AdminManager::LogGMCommand(int gmID, int playerID, const char* cmd)
{
    if (!strncmp(cmd,"/slide",6)) // don't log all these.  spamming the GM log table.
        return true;

    csString escape;
    db->Escape( escape, cmd );
    int result = db->Command("INSERT INTO gm_command_log "
                             "(gm,command,player,ex_time) "
                             "VALUES (%d,\"%s\",%d,Now())",gmID, escape.GetData(), playerID);
    return (result != -1);
}

const char *AdminManager::GetLastSQLError()
{
    if (!db)
        return "";

    return db->GetLastError();
}

void AdminManager::DeleteCharacter(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data,Client *client)
{
    unsigned int zombieID = 0;
    
    if ( data.zombie.StartsWith("pid:",true) ) // Find by player ID
    {
        zombieID = atoi( data.zombie.Slice(4).GetData() );
        if (!zombieID)
        {
            psserver->SendSystemError(me->clientnum,"Error, bad PID");
            return;
        }
    }

    if (zombieID == 0)  // Deleting by name; verify the petitioner gave us one of their characters
    {
        if (data.zombie == "" || data.requestor == "")
        {
            psserver->SendSystemInfo(me->clientnum,"Syntax: \"/deletechar CharacterName RequestorName\" OR \"/deletechar pid:[id]\"");
            return;
        }
    
        csString escape;
        db->Escape( escape, data.zombie );
       
        // Check account
        unsigned int zombieAccount = db->SelectSingleNumber( "SELECT account_id FROM characters WHERE name='%s'\n", escape.GetData() );
        if ( zombieAccount == QUERY_FAILED )
        {
            psserver->SendSystemInfo(me->clientnum,"Character %s has no account.", data.zombie.GetData());
            return;
        }
        zombieID = (unsigned int)db->SelectSingleNumber( "SELECT id FROM characters WHERE name='%s'\n", escape.GetData() );

        db->Escape( escape, data.requestor );
        unsigned int requestorAccount = db->SelectSingleNumber( "SELECT account_id FROM characters WHERE name='%s'\n", escape.GetData() );
        if ( requestorAccount == QUERY_FAILED )
        {
            psserver->SendSystemInfo(me->clientnum,"Requestor %s has no account.", data.requestor.GetData());
            return;
        }

        if ( zombieAccount != requestorAccount )
        {
            psserver->SendSystemInfo(me->clientnum,"Zombie/Requestor Mismatch, no deletion.");
            return;
        }
    }
    else  // Deleting by PID; make sure this isn't a unique or master NPC
    {
        Result result(db->Select("SELECT name, character_type, npc_master_id FROM characters WHERE id='%u'",zombieID));
        if (!result.IsValid() || result.Count() != 1)
        {
            psserver->SendSystemError(me->clientnum,"No character found with PID %u!",zombieID);
            return;
        }
        
        iResultRow& row = result[0];
        data.zombie = row["name"];
        unsigned int charType = row.GetUInt32("character_type");
        unsigned int masterID = row.GetUInt32("npc_master_id");

        if (charType == PSCHARACTER_TYPE_NPC)
        {
            if (masterID == 0)
            {
                psserver->SendSystemError(me->clientnum,"%s is a unique NPC, and may not be deleted", data.zombie.GetData() );
                return;
            }

            if (masterID == zombieID)
            {
                psserver->SendSystemError(me->clientnum,"%s is a master NPC, and may not be deleted", data.zombie.GetData() );
                return;
            }
        }
    }
        
    csString error;
    if ( psserver->CharacterLoader.DeleteCharacterData(zombieID,error) )
        psserver->SendSystemInfo(me->clientnum,"Character %s (PID %u) has been deleted.", data.zombie.GetData(), zombieID );
    else
    {
        if ( error.Length() )
            psserver->SendSystemError(me->clientnum,"Deletion error: %s", error.GetData() );
        else
            psserver->SendSystemError(me->clientnum,"Character deletion got unkown error!", error.GetData() );
    }
}



void AdminManager::ChangeName(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data,Client *client)
{    
    WordArray words (msg.cmd, false);
    csString pid_str = words[1]; // Player is changed to string so no point in testing that if multiple entites with same name.

    if (!data.player.Length() || !data.newName.Length())
    {
        psserver->SendSystemInfo(me->clientnum,"Syntax: \"/changename <OldName|pid:[id]> <yes|no> <NewName> [NewLastName|No]\"");
        return;
    }

    unsigned int pid = 0;
    
    if (pid_str.StartsWith("pid:",true) && pid_str.Length() > 4) // Find by player ID
    {
        pid = atoi( pid_str.Slice(4).GetData() );
        if (!pid)
        {
            psserver->SendSystemError(me->clientnum,"Error, bad PID");
            return;
        }

    }

    // Fix names
    data.newName = NormalizeCharacterName(data.newName);
    data.newLastName = NormalizeCharacterName(data.newLastName);
    csString name = NormalizeCharacterName(data.player);
    
    bool online;
    unsigned int id = 0;
    unsigned int type = 0;

    Client* target = NULL;

    if (pid)
    {
        target = clients->FindPlayer(pid);
    }
    else
    {
        target = clients->Find(data.player);
    }
    online = (target != NULL);

    csString prevFirstName,prevLastName;

    // Check the DB if the player isn't online
    if(!online)
    {
        csString query;
        if (pid)
        {
            query.Format("SELECT id,name,lastname,character_type FROM characters WHERE id=%u",pid);
        }
        else
        {
            query.Format("SELECT id,name,lastname,character_type FROM characters WHERE name='%s'",name.GetData());
        }

        Result result(db->Select(query));
        if (!result.IsValid() || result.Count() == 0)
        {
            psserver->SendSystemError(me->clientnum,"No online or offline player found with the name %s!",name.GetData());               
            return;
        }
        else if (result.Count() != 1)
        {
            psserver->SendSystemError(me->clientnum,"Multiple characters with same name '%s' use pid.",name.GetData());               
            return; 
        }
        else
        {
            iResultRow& row = result[0];
            prevFirstName = row["name"];
            prevLastName = row["lastname"];
            id = row.GetUInt32("id");

            type = row.GetUInt32("character_type");
            if (type == PSCHARACTER_TYPE_NPC)
            {
                if (!Valid(client->GetSecurityLevel(), "change NPC names", me->clientnum))
                    return;
            }
        }
    }
    else
    {
        prevFirstName = target->GetCharacterData()->GetCharName();
        prevLastName = target->GetCharacterData()->GetCharLastName();
        id = target->GetCharacterData()->GetCharacterID();

        type = target->GetCharacterData()->GetCharType();
    }

    bool checkFirst=true; //If firstname is same as before, skip DB check
    bool checkLast=true; //If we make the newLastName var the current value, we need to skip the db check on that    

    if(data.newLastName.CompareNoCase("no"))
    {
        data.newLastName = "";
        checkLast = false;
    }
    else if (data.newLastName.Length() == 0 || data.newLastName == prevLastName)
    {
        data.newLastName = prevLastName;
        checkLast = false;
    }

    if (data.player == data.newName)
        checkFirst = false;

    if (!checkFirst && !checkLast && data.newLastName.Length() != 0)
        return;

    if(checkFirst)
    {   
        if (!psCharCreationManager::FilterName(data.newName))   
        {   
            psserver->SendSystemError(me->clientnum,"The name %s is invalid!",data.newName.GetData());   
            return;   
        }   
    }
    if(checkLast)   
    {   
        if (!psCharCreationManager::FilterName(data.newLastName))   
        {
            psserver->SendSystemError(me->clientnum,"The last name %s is invalid!",data.newLastName.GetData());   
            return;
        }   
    }

    // If the first name should be unique, check it
    if (checkFirst && type == PSCHARACTER_TYPE_PLAYER)
    {
        if (!psCharCreationManager::IsUnique(data.newName))
        {
            psserver->SendSystemError(me->clientnum,"The name %s is not unique!",data.newName.GetData());               
            return;
        }
    }

    // If the last name should be unique, check it
    if (data.uniqueName && checkLast && data.newLastName.Length())
    {
        if (!psCharCreationManager::IsLastNameUnique(data.newLastName))
        {
            psserver->SendSystemError(me->clientnum,"The last name %s is not unique!",data.newLastName.GetData());               
            return;
        }
    }

    // Apply
    csString fullName;
    PS_ID actorId = 0;
    if(online)
    {
        target->GetCharacterData()->SetFullName(data.newName, data.newLastName);
        fullName = target->GetCharacterData()->GetCharFullName();
        target->SetName(data.newName);
        target->GetActor()->SetName(fullName);
        actorId = target->GetActor()->GetEntity()->GetID();

    }
    else if (type == PSCHARACTER_TYPE_NPC || type == PSCHARACTER_TYPE_PET)
    {
        gemNPC *npc = GEMSupervisor::GetSingleton().FindNPCEntity( id );
        if (!npc)
        {
            psserver->SendSystemError(me->clientnum,"Unable to find NPC %s!", 
                name.GetData());
            return;
        }
        npc->GetCharacterData()->SetFullName(data.newName, data.newLastName);
        fullName = npc->GetCharacterData()->GetCharFullName();
        actorId = npc->GetEntity()->GetID();
    }

    // Inform
    if(online)
    {
        psserver->SendSystemInfo(
                        target->GetClientNum(),
                        "Your name has been changed to %s %s by GM %s",
                        data.newName.GetData(),
                        data.newLastName.GetData(),
                        client->GetName()
                        );
    }

    psserver->SendSystemInfo(me->clientnum,
                             "%s %s is now known as %s %s",
                             prevFirstName.GetDataSafe(),
                             prevLastName.GetDataSafe(),
                             data.newName.GetDataSafe(),
                             data.newLastName.GetDataSafe()
                             );

    // Update
    if (online || type == PSCHARACTER_TYPE_NPC || type == PSCHARACTER_TYPE_PET)
    {
        psUpdateObjectNameMessage newNameMsg(0, actorId, fullName);
        psserver->GetEventManager()->Broadcast(
            newNameMsg.msg, NetBase::BC_EVERYONE);
    }

    // Need instant DB update if we should be able to change the same persons name twice
    db->CommandPump("UPDATE characters SET name='%s', lastname='%s' WHERE id='%u'",data.newName.GetData(),data.newLastName.GetDataSafe(), id );

    // Resend group list
    if(online)
    {
        csRef<PlayerGroup> group = target->GetActor()->GetGroup();
        if(group)
            group->BroadcastMemberList();

        // Handle guild update
        psGuildInfo* guild = target->GetActor()->GetGuild();
        if(guild)
        {
            psGuildMember* mem = guild->FindMember((unsigned int)target->GetActor()->GetCharacterData()->GetCharacterID());
            if(mem)
                mem->name = data.newName;
        }
    }
    
    Client* buddy;
    /*We update the buddy list of the people who have the target in their own buddy list and 
      they are online. */
    if(online)
    {
        csArray<int> buddyOfList=target->GetCharacterData()->buddyOfList;
    
        for (size_t i=0; i<buddyOfList.GetSize(); i++)
        {
            buddy = clients->FindPlayer(buddyOfList[i]);
            if (buddy && buddy->IsReady())
            {
                buddy->GetCharacterData()->RemoveBuddy(id);
                buddy->GetCharacterData()->AddBuddy(id, data.newName);
                //We refresh the buddy list
                psserver->usermanager->BuddyList(buddy, buddy->GetClientNum(), true);
           }
        }
    }
    else
    {
        unsigned int buddyid;

        //If the target is offline then we select all the players online that have him in the buddylist
        Result result2(db->Select("SELECT player_id FROM buddy_list WHERE player_buddy='%u'",id));

        if (result2.IsValid())
        {
            for(unsigned long j=0; j<result2.Count();j++)
            {
                iResultRow& row = result2[j];
                buddyid = row.GetUInt32("player_id");
                buddy = clients->FindPlayer(buddyid);
                if (buddy && buddy->IsReady())
                {
                    buddy->GetCharacterData()->RemoveBuddy(id);
                    buddy->GetCharacterData()->AddBuddy(id, data.newName);
                    //We refresh the buddy list
                    psserver->usermanager->BuddyList(buddy, buddy->GetClientNum(), true);
                }
           }
        }
     }
}

void AdminManager::BanName(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data, Client *client)
{
    if (!data.player.Length())
    {
        psserver->SendSystemError(me->clientnum,"You have to specify a name to ban");
        return;
    }

    if (psserver->GetCharManager()->IsBanned(data.player))
    {
        psserver->SendSystemError(me->clientnum,"That name is already banned");
        return;
    }
    
    CacheManager::GetSingleton().AddBadName(data.player);
    psserver->SendSystemInfo(me->clientnum,"You banned the name '%s'",data.player.GetDataSafe());
}

void AdminManager::UnBanName(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data, Client *client)
{
    if (!data.player.Length())
    {
        psserver->SendSystemError(me->clientnum,"You have to specify a name to unban");
        return;
    }

    if (!psserver->GetCharManager()->IsBanned(data.player))
    {
        psserver->SendSystemError(me->clientnum,"That name is not banned");
        return;
    }
    
    CacheManager::GetSingleton().DelBadName(data.player);
    psserver->SendSystemInfo(me->clientnum,"You unbanned the name '%s'",data.player.GetDataSafe());
}

void AdminManager::BanClient(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data,Client *client)
{
    const time_t year = 31536000UL; //one year should be enough
    time_t secs = (data.mins * 60) + (data.hours * 60 * 60) + (data.days * 24 * 60 * 60);
    if ((secs > year) || (secs == 0))
        secs = year; //some errors if time was too high 

    if (data.player.Length() == 0)
    {
        psserver->SendSystemError(me->clientnum, "You must specify a player name or an account name or number.");
        return;
    }
    
    if (data.reason.Length() < 5)
    {
        psserver->SendSystemError(me->clientnum, "You must specify a reason to ban");
        return;
    }

    Result result;
    unsigned int account = atoi( data.player.GetDataSafe() );  // See if we're going by character name or account ID

    if (account == 0)
    {
        if ( !GetAccount(data.player,result) )
        {
            // not found
            psserver->SendSystemError(me->clientnum, "Couldn't find account with the name %s",data.player.GetData());
            return;
        }
        account = result[0].GetUInt32("id");
    }
    else
    {
        result = db->Select("SELECT * FROM accounts WHERE id = '%u' LIMIT 1",account);
        if ( !result.IsValid() || !result.Count() )
        {
            psserver->SendSystemError(me->clientnum, "Couldn't find account with id %u",account);
            return;
        }
    }

    csString user = result[0]["username"];

    // Ban by IP range, as well as account
    csString ip_range = Client::GetIPRange(result[0]["last_login_ip"]);

    if ( !psserver->GetAuthServer()->GetBanManager()->AddBan(account,ip_range,secs,data.reason) )
    {
        // Error adding; entry must already exist
        psserver->SendSystemError(me->clientnum, "%s is already banned", user.GetData() );
        return;
    }

    // Find client to get target
    Client *target = clients->Find(NormalizeCharacterName(data.player));

    // Now we have a valid player target, so remove from server
    if(target)
    {
        if (secs < year)
        {
            csString reason;
            reason.Format("You were banned from the server by a GM for %d minutes, %d hours and %d days. Reason: %s",
                          data.mins, data.hours, data.days, data.reason.GetData() );

            psserver->RemovePlayer(target->GetClientNum(),reason);
        }
        else
            psserver->RemovePlayer(target->GetClientNum(),"You were banned from the server by a GM. Reason: " + data.reason);
    }
    
    csString notify;
    notify.Format("You%s banned '%s' off the server for ", (target)?" kicked and":"", user.GetData() );
    if (secs == year)
        notify.Append("a year. ");
    else
        notify.AppendFmt("%d minutes, %d hours and %d days. ", data.mins, data.hours, data.days );
    notify.AppendFmt("They will also be banned by IP range%s.", (secs > 60*60*24*2)?" for the first 2 days":"" );

    // Finally, notify the client who kicked the target
    psserver->SendSystemInfo(me->clientnum,notify);
}

void AdminManager::UnbanClient(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data,Client *gm)
{
    if (data.player.Length() == 0)
    {
        psserver->SendSystemError(me->clientnum, "You must specify a player name or an account name or number.");
        return;
    }
    
    Client* target = clients->Find(data.player);
    // Check if the target is online, if he/she/it is he/she/it can't be unbanned (No logic in it).
    if (target)
    {
        psserver->SendSystemError(me->clientnum, "The player is active and is playing.");
        return;
    }

    Result result;
    unsigned int account = atoi( data.player.GetDataSafe() );  // See if we're going by character name or account ID

    if (account == 0)
    {
        if ( !GetAccount(data.player,result) )
        {
            // not found
            psserver->SendSystemError(me->clientnum, "Couldn't find account with the name %s",data.player.GetDataSafe());
            return;
        }
        account = result[0].GetUInt32("id");
    }
    else
    {
        result = db->Select("SELECT * FROM accounts WHERE id = '%u' LIMIT 1",account);
        if ( !result.IsValid() || !result.Count() )
        {
            psserver->SendSystemError(me->clientnum, "Couldn't find account with id %u",account);
            return;
        }
    }

    csString user = result[0]["username"];

    if ( psserver->GetAuthServer()->GetBanManager()->RemoveBan(account) )
        psserver->SendSystemResult(me->clientnum, "%s has been unbanned", user.GetData() );
    else
        psserver->SendSystemError(me->clientnum, "%s is not banned", user.GetData() );
}

bool AdminManager::GetAccount(csString useroracc,Result& resultre )
{
    unsigned int id = 0;
    bool character = false;
    csString usr;

    // Check if it's a character
    // Uppercase in names
    usr = NormalizeCharacterName(useroracc);
    resultre = db->Select("SELECT * FROM characters WHERE name = '%s' LIMIT 1",usr.GetData());
    if (resultre.IsValid() && resultre.Count() == 1)
    {
        id = resultre[0].GetUInt32("account_id"); // store id
        character = true;
    }
    
    if (character)
        resultre = db->Select("SELECT * FROM accounts WHERE id = '%u' LIMIT 1",id);
    else
    {
        // account uses lowercase
        usr.Downcase();
        resultre = db->Select("SELECT * FROM accounts WHERE username = '%s' LIMIT 1",usr.GetData());
    }

    if ( !resultre.IsValid() || !resultre.Count() )
        return false;

    return true;
}

void AdminManager::SendSpawnTypes(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data,Client *client)
{
    csArray<csString> itemCat;
    unsigned int size = 0;

    for(int i = 1;; i++)
    {
        psItemCategory* cat = CacheManager::GetSingleton().GetItemCategoryByID(i);
        if(!cat)
            break;

        size += (int)strlen(cat->name)+1;
        itemCat.Push(cat->name);
    }
    
    itemCat.Sort();
    psGMSpawnTypes msg2(me->clientnum,size);

    // Add the numbers of types
    msg2.msg->Add((uint32_t)itemCat.GetSize());

    for(size_t i = 0;i < itemCat.GetSize(); i++)
    {
        msg2.msg->Add(itemCat.Get(i));
    }

    msg2.SendMessage();
}

void AdminManager::SendSpawnItems (MsgEntry* me, psGMSpawnItems& msg,Client *client)
{
    csArray<psItemStats*> items;
    unsigned int size = 0;
    if (!Valid(client->GetSecurityLevel(), "/item", client->GetClientNum()))
    {
        return;
    }

    psItemCategory * category = CacheManager::GetSingleton().GetItemCategoryByName(msg.type);    
    if ( !category )
    {
        psserver->SendSystemError(me->clientnum, "Category %s is not valid.", msg.type.GetData() );
        return;    
    }

    // Database hit.  
    // Justification:  This is a rare event and it is quicker than us doing a sort.  
    //                 Is also a read only event.     
    Result result(db->Select("SELECT id FROM item_stats WHERE category_id=%d AND flags NOT LIKE '%%BUY_PERSONALISE%%' AND id < %d ORDER BY Name ", category->id, SPAWN_ITEM_ID_CEILING));
    if (!result.IsValid() || result.Count() == 0)
    {
        psserver->SendSystemError(me->clientnum, "Could not query database for category %s.", msg.type.GetData() );
        return;
    }
    
    for ( unsigned int i=0; i < result.Count(); i++ )
    {
        unsigned id = result[i].GetUInt32(0);
        psItemStats* item = CacheManager::GetSingleton().GetBasicItemStatsByID(id);
        if(item)
        {
            csString name(item->GetName());
            csString mesh(item->GetMeshName());
            csString icon(item->GetImageName());
            size += (int)name.Length()+(int)mesh.Length()+(int)icon.Length()+3;
            items.Push(item);           
        }            
    }

    psGMSpawnItems msg2(me->clientnum,msg.type,size);

    // Add the numbers of types
    msg2.msg->Add((uint32_t)items.GetSize());

    for(size_t i = 0;i < items.GetSize(); i++)
    {
        psItemStats* item = items.Get(i);
        msg2.msg->Add(item->GetName());
        msg2.msg->Add(item->GetMeshName());
        msg2.msg->Add(item->GetImageName());
    }

    Debug4(LOG_ADMIN, me->clientnum, "Sending %zu items from the %s category to client %s\n", items.GetSize(), msg.type.GetData(), client->GetName());

    msg2.SendMessage();
}

void AdminManager::SpawnItemInv(MsgEntry* me, psGMSpawnItem& msg,Client *client)
{
    if (!Valid(client->GetSecurityLevel(), "/item", client->GetClientNum()))
    {
        return;
    }

    psCharacter* charData = client->GetCharacterData();
    if (!charData)
    {
        psserver->SendSystemError(me->clientnum, "Couldn't find your character data!");
        return;
    }

    // Get the basic stats
    psItemStats* stats = CacheManager::GetSingleton().GetBasicItemStatsByName(msg.item);
    if (!stats)
    {
        psserver->SendSystemError(me->clientnum, "Couldn't find basic stats for that item!");
        return;
    }

    // Check skill
    PSSKILL skill = CacheManager::GetSingleton().ConvertSkillString(msg.lskill);
    if (skill == PSSKILL_NONE && msg.lockable)
    {
        psserver->SendSystemError(me->clientnum, "Couldn't find the lock skill!");
        return;
    }

    if (msg.count < 1 || msg.count > MAX_STACK_COUNT)
    {
        psserver->SendSystemError(me->clientnum, "Invalid stack count!");
        return;
    }

    // if the item is 'personalised' by the owner, then the item is in fact just a
    // template which is best not to instantiate it itself. Eg blank book, map.
    if (stats->GetBuyPersonalise())
    {
        psserver->SendSystemError(me->clientnum, "Cannot spawn personalised item!");
        return;
    }

    // Create the new item
    psItem *item = stats->InstantiateBasicItem();

    item->SetStackCount(msg.count);
    item->SetIsLockable(msg.lockable);
    item->SetIsLocked(msg.locked);
    item->SetIsPickupable(msg.pickupable);
    item->SetIsCD(msg.collidable);

    if (msg.lockable)
    {        
        item->SetLockpickSkill(skill);
        item->SetLockStrength(msg.lstr);
    }
    
    // Place the new item in the GM's inventory
    csString text;
    item->SetLoaded();  // Item is fully created
    if (charData->Inventory().Add(item))
    {
        text.Format("You spawned %s to your inventory",msg.item.GetData());
    }            
    else
    {
        text.Format("Couldn't spawn %s to your inventory, maybe it's full?",msg.item.GetData());
        CacheManager::GetSingleton().RemoveInstance(item);
    }

    psserver->SendSystemInfo(me->clientnum,text);
    psserver->GetCharManager()->UpdateItemViews(me->clientnum);
}

void AdminManager::AwardExperience(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data, Client* client, Client* target)
{    
    if (!target || !target->GetCharacterData())
    {
        psserver->SendSystemError(me->clientnum, "Invalid target to award experience to");
        return;
    }
    
    if (data.value == 0)
    {
        psserver->SendSystemError(me->clientnum, "Invalid experience specified");
        return;
    }

    AwardExperienceToTarget(me->clientnum, target, data.player, data.value);
}

void AdminManager::AwardExperienceToTarget(int gmClientnum, Client* target, csString recipient, int ppAward)
{
    int pp = target->GetCharacterData()->GetProgressionPoints();

    if (pp == 0 && ppAward < 0)
    {
        psserver->SendSystemError(gmClientnum, "Target has no experience to penalize");
        return;
    }
    //Limiting the amount of awarded pp.
    if (abs(ppAward) > MAXIMUM_EXP_CHANGE)
    {
        ppAward = (ppAward > 0 ? MAXIMUM_EXP_CHANGE : -MAXIMUM_EXP_CHANGE);
        psserver->SendSystemError(gmClientnum, "The experience awarded is too large. Limited to %d", ppAward);
    }

    pp += ppAward; // Negative changes are allowed
    if (pp < 0) // Negative values are not
    {
        ppAward += -pp;
        pp = 0;
    }

    target->GetCharacterData()->SetProgressionPoints(pp,true);

    if (ppAward > 0)
    {
        psserver->SendSystemOK(target->GetClientNum(),"You have been awarded experience by a GM");
        psserver->SendSystemInfo(target->GetClientNum(),"You gained %d progression points.", ppAward);
    }
    else if (ppAward < 0)
    {
        psserver->SendSystemError(target->GetClientNum(),"You have been penalized experience by a GM");
        psserver->SendSystemInfo(target->GetClientNum(),"You lost %d progression points.", -ppAward);
    }

    psserver->SendSystemInfo(gmClientnum, "You awarded %s %d progression points.", recipient.GetData(), ppAward);
}

void AdminManager::AdjustFactionStandingOfTarget(int gmClientnum, Client* target, csString factionName, int standingDelta)
{
    Faction *faction = CacheManager::GetSingleton().GetFaction(factionName.GetData());
    if (!faction)
    {
        psserver->SendSystemInfo(gmClientnum, "\'%s\' Unrecognised faction.", factionName.GetData());
        return;
    }

    if (target->GetCharacterData()->UpdateFaction(faction, standingDelta))
        psserver->SendSystemInfo(gmClientnum, "%s\'s standing on \'%s\' faction has been adjusted.", 
                               target->GetName(), faction->name.GetData());
    else
        psserver->SendSystemError(gmClientnum, "%s\'s standing on \'%s\' faction failed.", 
                               target->GetName(), faction->name.GetData());
}

void AdminManager::TransferItem(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data, Client* source, Client* target)
{
    if (!target || !target->GetCharacterData())
    {
        psserver->SendSystemError(me->clientnum, "Invalid character to give to");
        return;
    }
    
    if (!source || !source->GetCharacterData())
    {
        psserver->SendSystemError(me->clientnum, "Invalid character to take from");
        return;
    }
    
    if (source == target)
    {
        psserver->SendSystemError(me->clientnum, "Source and target must be different");
        return;
    }
    
    if (data.value == 0 || data.item.IsEmpty())
    {
        psserver->SendSystemError(me->clientnum, "Syntax: \"/[giveitem|takeitem] [target] [quantity|'all'|''] [item]\"");
        return;
    }

    psItemStats* itemstats = CacheManager::GetSingleton().GetBasicItemStatsByName(data.item);
    if (!itemstats)
    {
        psserver->SendSystemError(me->clientnum, "Invalid item name");
        return;
    }

    psCharacter* targetchar = target->GetCharacterData();
    psCharacter* sourcechar = source->GetCharacterData();

    InventoryTransaction srcTran( &sourcechar->Inventory() );

    //int slot = sourcechar->Inventory().FindItemInTopLevelBulkWithStats(itemstats);
    size_t slot = sourcechar->Inventory().FindItemStatIndex(itemstats);
    if (slot == SIZET_NOT_FOUND)
    {
        psserver->SendSystemError(me->clientnum, "Cannot find any %s in %s's inventory.",
                                                 data.item.GetData(), source->GetActor()->GetName() );
        return;
    }

    psItem* item;
    item = sourcechar->Inventory().RemoveItemIndex(slot,data.value);  // data.value is the stack count to move, or -1
    if (!item)
    {
        Error2("Cannot RemoveItemIndex on slot %zu.\n", slot);
        psserver->SendSystemError(me->clientnum, "Cannot remove %s from %s's inventory.",
                                  data.item.GetData(), source->GetActor()->GetName() );
        return;
    }
    psserver->GetCharManager()->UpdateItemViews(source->GetClientNum());

    if (item->GetStackCount() < data.value)
    {
        psserver->SendSystemError(me->clientnum, "There are only %d, not %d in the stack.", item->GetStackCount(), data.value );
        return;
    }

    bool wasEquipped = item->IsEquipped();

    // Now here we handle the target machine
    InventoryTransaction trgtTran( &targetchar->Inventory() );

    if (!targetchar->Inventory().Add(item))
    {
        psserver->SendSystemError(me->clientnum, "Target inventory is too full to accept item transfer.");
        return;
    }
    psserver->GetCharManager()->UpdateItemViews(target->GetClientNum());

    // Inform the GM doing the transfer
    psserver->SendSystemOK(me->clientnum, "%s transferred from %s's %s to %s",
                                          item->GetName(),
                                          source->GetActor()->GetName(),
                                          wasEquipped?"equipment":"inventory",
                                          target->GetActor()->GetName() );

    // If we're giving to someone else, notify them
    if (target->GetClientNum() != me->clientnum)
    {
         psserver->SendSystemOK(target->GetClientNum(), "You were given %s by GM %s.",
                                                        item->GetName(),
                                                        source->GetActor()->GetName() );
    }

    // If we're taking from someone else, notify them
    if (source->GetClientNum() != me->clientnum)
    {
         psserver->SendSystemResult(source->GetClientNum(), "%s was taken by GM %s.",
                                                            item->GetName(),
                                                         target->GetActor()->GetName() );
    }

    trgtTran.Commit();
    srcTran.Commit();
}

void AdminManager::FreezeClient(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data, Client* client, Client* target)
{
    if (!target)
    {
        psserver->SendSystemError(me->clientnum,"Invalid target for freeze");
        return;
    }
    
    if (target->IsFrozen())
    {
        psserver->SendSystemError(me->clientnum,"The player is alreday frozen");
        return;
    }

	target->GetActor()->SetAllowedToMove(false);
    target->SetFrozen(true);
    target->GetActor()->SetMode(PSCHARACTER_MODE_SIT);
    psserver->SendSystemError(target->GetClientNum(), "You have been frozen in place by a GM.");
    psserver->SendSystemInfo(me->clientnum, "You froze '%s'.",(const char*)data.player);
}

void AdminManager::ThawClient(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data, Client* client, Client* target)
{
    if (!target)
    {
        psserver->SendSystemError(me->clientnum,"Invalid target for thaw");
        return;
    }

    if (!target->IsFrozen())
    {
        psserver->SendSystemError(me->clientnum,"The player is not frozen");
        return;
    }   
    
	target->GetActor()->SetAllowedToMove(true);
    target->SetFrozen(false);
    target->GetActor()->SetMode(PSCHARACTER_MODE_PEACE);
    psserver->SendSystemOK(target->GetClientNum(), "You have been released by a GM.");
    psserver->SendSystemInfo(me->clientnum, "You released '%s'.",(const char*)data.player);
}

void AdminManager::SetSkill(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data, Client* client, Client *target)
{
    if (data.skill.IsEmpty())
    {
        psserver->SendSystemError(me->clientnum, "Syntax: /setskill [target] [skill|'all'] [value|-1]");
        return;
    }

    if (target == NULL)
    {
        psserver->SendSystemError(me->clientnum, "Invalid target for setting skills");
        return;
    }

    // Check the permission to set skills for other characters
    if (target != client && !Valid(client->GetSecurityLevel(), "setskill others", me->clientnum))
        return;

    psCharacter * pchar = target->GetCharacterData();
    if (!pchar)
    {
        psserver->SendSystemError(me->clientnum, "No character data!");
        return;
    }

    if (data.skill == "all")
    {
        for (int i=0; i<PSSKILL_COUNT; i++)
        {
            psSkillInfo * skill = CacheManager::GetSingleton().GetSkillByID(i);
            if (skill == NULL) continue;

            pchar->SetSkillRank(skill->id, data.value);
        }
        
        psserver->SendSystemInfo(me->clientnum, "Fine");
    }
    else
    {
        psSkillInfo * skill = CacheManager::GetSingleton().GetSkillByName(data.skill);
        if (skill == NULL)
        {
            psserver->SendSystemError(me->clientnum, "Skill not found");
            return;
        }

        unsigned int old_value = pchar->GetSkills()->GetSkillRank(skill->id);
        if (data.value == -1)
        {
            psserver->SendSystemInfo(me->clientnum, "Current '%s' is %d",skill->name.GetDataSafe(),old_value);
            return;
        } else
    
        pchar->SetSkillRank(skill->id, data.value);
        psserver->SendSystemInfo(me->clientnum, "Changed '%s' from %d to %d",skill->name.GetDataSafe(),old_value,data.value);
    }

    psserver->GetProgressionManager()->SendSkillList(target, false);

    if (target != client)
    {
        // Inform the other player.
        psserver->SendSystemOK(target->GetClientNum(), "Your '%s' level was set to %d by a GM", data.skill.GetDataSafe(), data.value);
    }
}

void AdminManager::UpdateRespawn(Client* client, gemActor* target)
{
    if (!target)
    {
        psserver->SendSystemError(client->GetClientNum(),"You need to specify or target a player or NPC");
        return;
    }

    if (!target->GetCharacterData())
    {
        psserver->SendSystemError(client->GetClientNum(),"Critical error! The entity hasn't got any character data!");
        return;
    }
    
    csVector3 pos;
    float yrot;
    iSector* sec;
    target->GetPosition(pos, yrot, sec);
    csString sector = sec->QueryObject()->GetName();

    psSectorInfo* sectorinfo = CacheManager::GetSingleton().GetSectorInfoByName(sector);
    
    target->GetCharacterData()->UpdateRespawn(pos, yrot, sectorinfo);
    
    csString buffer;
    buffer.Format("%s now respawning (%.2f,%.2f,%.2f) <%s>", target->GetName(), pos.x, pos.y, pos.z, sector.GetData());
    psserver->SendSystemOK(client->GetClientNum(), buffer);
}


void AdminManager::Inspect(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data, Client* client, gemActor* target)
{    
    if (!target)
    {
        psserver->SendSystemError(me->clientnum,"You need to specify or target a player or NPC");
        return;
    }

    if (!target->GetCharacterData())
    {
        psserver->SendSystemError(me->clientnum,"Critical error! The entity hasn't got any character data!");
        return;
    }

    // We got our target, now let's print it's inventory
    csString message; // Dump all data formated in this
    bool npc = (target->GetClientID() == 0);

    message.Format("Inventory for %s %s:\n",
                   npc?"NPC":"player",
                   target->GetName() );

    message.AppendFmt("Total weight is %d / %d\nTotal money is %d\n",
                      (int)target->GetCharacterData()->Inventory().GetCurrentTotalWeight(),
                      (int)target->GetCharacterData()->Inventory().MaxWeight(),
                      target->GetCharacterData()->Money().GetTotal() );

    bool found = false;
    // Inventory indexes start at 1.  0 is reserved for the "NULL" item.
    for (size_t i = 1; i < target->GetCharacterData()->Inventory().GetInventoryIndexCount(); i++)
    {
        psItem* item = target->GetCharacterData()->Inventory().GetInventoryIndexItem(i);
        if (item)
        {
            found = true;
            message += item->GetName();
            message.AppendFmt(" (%d/%d)", (int)item->GetItemQuality(), (int)item->GetMaxItemQuality());
            if (item->GetStackCount() > 1)
                message.AppendFmt(" (x%u)", item->GetStackCount());

            message.Append(" - ");

            const char *slotname = CacheManager::GetSingleton().slotNameHash.GetName(item->GetLocInParent());
            if (slotname)
                message.Append(slotname);
            else
                message.AppendFmt("Bulk %d", item->GetLocInParent(true));
            message += "\n";
        }
    }
    if (!found)
        message += "(none)\n";

    message.Truncate(message.Length() -1);

    psserver->SendSystemInfo(me->clientnum,message);
}

void AdminManager::RenameGuild(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data, Client* client)
{
    if(data.target.IsEmpty() || data.newName.IsEmpty())
    {
        psserver->SendSystemError(me->clientnum,"Syntax: /changeguildname guildname newguildname");
        return;
    }

    psGuildInfo* guild = CacheManager::GetSingleton().FindGuild(data.target);
    if(!guild)
    {
        psserver->SendSystemError(me->clientnum,"No guild with that name");
        return;
    }

    guild->SetName(data.newName);
    psserver->GetGuildManager()->ResendGuildData(guild->id);

    // Notify the guild leader if he is online
    psGuildMember* gleader = guild->FindLeader();
    if(gleader)
    {
        if(gleader->actor && gleader->actor->GetActor())
        {
            psserver->SendSystemInfo(gleader->actor->GetActor()->GetClientID(),
                "Your guild has been renamed to %s by a GM",
                data.newName.GetData()
                );
        }
    }

    psserver->SendSystemOK(me->clientnum,"Guild renamed to '%s'",data.newName.GetData());
    
    // Get all connected guild members
    csArray<uint32_t> array;
    for (size_t i = 0; i < guild->members.GetSize();i++)
    {
        psGuildMember* member = guild->members[i];
        if(member->actor)
            array.Push(member->actor->GetActor()->GetEntity()->GetID());
    }   

    // Update the labels
    int length = (int)array.GetSize();
    psUpdatePlayerGuildMessage newNameMsg(0,length,data.newName,false);

    // Copy array
    for(size_t i = 0; i < array.GetSize();i++)
    {
        newNameMsg.AddPlayer(array[i]);
    }

    // Broadcast to everyone
    psserver->GetEventManager()->Broadcast(newNameMsg.msg,NetBase::BC_EVERYONE);
}

void AdminManager::Thunder(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data, Client *client)
{   
    // Find the sector
    psSectorInfo *sectorinfo = NULL;

    if (!data.sector.IsEmpty())
        sectorinfo = CacheManager::GetSingleton().GetSectorInfoByName(data.sector);
    else
    {
        csVector3 pos;
        iSector* sect;
        // Get the current sector
        client->GetActor()->GetPosition(pos,sect);
        if(!sect)
        {
            psserver->SendSystemError(me->clientnum,"Invalid sector");
            return;
        }

        sectorinfo = CacheManager::GetSingleton().GetSectorInfoByName(sect->QueryObject()->GetName());
    }

    if (!sectorinfo)
    {
        psserver->SendSystemError(me->clientnum,"Sector not found!");
        return;
    }
    
    if (sectorinfo->lightning_max_gap == 0)
    {
        psserver->SendSystemError(me->clientnum, "Lightning not defined for this sector!");
        return;
    }

    if (!sectorinfo->is_raining)
    {
        psserver->SendSystemError(me->clientnum, "You cannot create a lightning "
                                  "if no rain or rain is fading out!");
        return;
    }


    // Queue thunder
    psserver->GetWeatherManager()->QueueNextEvent(0, psWeatherMessage::LIGHTNING, 0, 0, 0, 
                                                  sectorinfo->name, sectorinfo, 
                                                  client->GetActor()->GetEntity()->GetID());

}

void AdminManager::Fog(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data, Client *client)
{
    if( !data.sector.Length() )
    {
        psserver->SendSystemError(me->clientnum, "Syntax: /fog sector [density [[r g b] fade]|stop]");
        return;
    }

    // Find the sector
    psSectorInfo *sectorinfo = CacheManager::GetSingleton().GetSectorInfoByName(data.sector);
    if(!sectorinfo)
    {
        psserver->SendSystemError(me->clientnum,"Sector not found!");
        return;
    }

    // Queue fog
    if(data.density == -1)
    {
        if ( !sectorinfo->fog_density )
        {
            psserver->SendSystemInfo( me->clientnum, "You need to have fog in this sector for turning it off." );
            return;
        }
        psserver->SendSystemInfo( me->clientnum, "You have turned off the fog." );
        // Reset fog
        psserver->GetWeatherManager()->QueueNextEvent(0, psWeatherMessage::FOG, 0, 0, 0,
                                                      sectorinfo->name, sectorinfo,0,0,0,0);
    }
    else
    {
        // Set fog
        psserver->GetWeatherManager()->QueueNextEvent(0, psWeatherMessage::FOG, 
                                                      data.density, 0, data.fade, 
                                                      sectorinfo->name, sectorinfo,0,
                                                      (int)data.x,(int)data.y,(int)data.z); //rgb
    }
    
}

void AdminManager::Weather(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data, Client *client)
{
    if(data.sector.Length() == 0)
    {
        psserver->SendSystemError(me->clientnum, "Syntax: /weather sector [on|off]");
        return;
    }

    // Find the sector
    psSectorInfo *sectorinfo = CacheManager::GetSingleton().GetSectorInfoByName(data.sector);
    if(!sectorinfo)
    {
        psserver->SendSystemError(me->clientnum,"Sector not found!");
        return;
    }

    // Start automatic weather - only rain supported for now.
    if (data.interval == -1)
    {
        if (!sectorinfo->rain_enabled)
        {
            sectorinfo->rain_enabled = true;
            psserver->GetWeatherManager()->StartWeather(sectorinfo);
            psserver->SendSystemInfo(me->clientnum,"Automatic weather started in sector %s",
                                     data.sector.GetDataSafe());
        }
        else
        {
            psserver->SendSystemInfo(me->clientnum,"The weather is already automatic in sector %s",
                                     data.sector.GetDataSafe());
        }
        
    }
    // Stop automatic weather
    else if (data.interval == -2)
    {
        if (sectorinfo->rain_enabled)
        {
            sectorinfo->rain_enabled = false;
            psserver->SendSystemInfo(me->clientnum,"Automatic weather stopped in sector %s",
                                 data.sector.GetDataSafe());
        }
        else
        {
            psserver->SendSystemInfo(me->clientnum,"The automatic weather is already off in sector %s",
                                     data.sector.GetDataSafe());
        }       
    }
}

void AdminManager::Rain(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data, Client *client)
{    
    if(data.sector.Length() == 0)
    {
        psserver->SendSystemError(me->clientnum, "Syntax: /rain sector [[drops length fade]|stop]");
        return;
    }

    if (data.rainDrops < 0 || data.rainDrops > WEATHER_MAX_RAIN_DROPS)
    {
        psserver->SendSystemError(me->clientnum, "Rain drops should be between %d and %d",
                                  0,WEATHER_MAX_RAIN_DROPS);
        return;
    }

    // Find the sector
    psSectorInfo *sectorinfo = CacheManager::GetSingleton().GetSectorInfoByName(data.sector);
    if(!sectorinfo)
    {
        psserver->SendSystemError(me->clientnum,"Sector not found!");
        return;
    }

    // Stop raining
    if (data.interval == -3 || (data.interval == 0 && data.rainDrops == 0 && data.fade == 0 ))
    {
        if( !sectorinfo->is_raining) //If it is not raining already then you don't stop anything.
        { 
            psserver->SendSystemInfo( me->clientnum, "You need some weather, first." );
            return;
        }
        else
        {
            psserver->SendSystemInfo( me->clientnum, "The weather was stopped." );
           
            // queue the event
            psserver->GetWeatherManager()->QueueNextEvent(0, psWeatherMessage::RAIN, 
                                                      0, 0,
                                                      0, data.sector, sectorinfo);
         }
    }
    else
    {
        // queue the event
        psserver->GetWeatherManager()->QueueNextEvent(0, psWeatherMessage::RAIN, 
                                                      data.rainDrops, data.interval,
                                                      data.fade, data.sector, sectorinfo);
    }
}

void AdminManager::Snow(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data, Client *client)
{
    if(data.sector.Length() == 0)
    {
        psserver->SendSystemError(me->clientnum, "Syntax: /snow sector [flakes length fade]|stop]");
        return;
    }

    if (data.rainDrops < 0 || data.rainDrops > WEATHER_MAX_SNOW_FALKES)
    {
        psserver->SendSystemError(me->clientnum, "Snow flakes should be between %d and %d",
                                  0,WEATHER_MAX_SNOW_FALKES);
        return;
    }

    // Find the sector
    psSectorInfo *sectorinfo = CacheManager::GetSingleton().GetSectorInfoByName(data.sector);
    if(!sectorinfo)
    {
        psserver->SendSystemError(me->clientnum,"Sector not found!");
        return;
    }
   
    // Stop snowing
    if (data.interval == -3 || (data.interval == 0 && data.fade == 0 && data.rainDrops == 0 ))
    {
        if( !sectorinfo->is_snowing) //If it is not snowing already then you don't stop anything.
        { 
            psserver->SendSystemInfo( me->clientnum, "You need some snow, first." );
            return;
        }
        else
        {
            psserver->SendSystemInfo( me->clientnum, "The snow was stopped." );
           
            // queue the event
            psserver->GetWeatherManager()->QueueNextEvent(0, psWeatherMessage::SNOW, 
                                                      0, 0,
                                                      0, data.sector, sectorinfo);
         }
    }
    else
    {
        // queue the event
        psserver->GetWeatherManager()->QueueNextEvent(0, psWeatherMessage::SNOW, data.rainDrops, 
                                                    data.interval, data.fade, data.sector, sectorinfo);
    }
}


void AdminManager::ModifyHuntLocation(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data, Client* client, gemObject* object)
{    
    if (!object)
    {
        psserver->SendSystemError(me->clientnum,"You need to specify an item in the world with 'target' or 'eid:#'");
        return;
    }

    psItem* item = object->GetItem();
    if (!item)
    {
        psserver->SendSystemError(me->clientnum,"You can only use modify on items");
        return;
    }

    if (data.action == "remove")
    {
        if (item->GetScheduledItem())
        {
            item->GetScheduledItem()->Remove();
            psserver->SendSystemInfo(me->clientnum,"Spawn point deleted for %s",item->GetName());
        }

        EntityManager::GetSingleton().RemoveActor(object); // Remove from world
        psserver->SendSystemInfo(me->clientnum,"%s was removed from the world",item->GetName());
        item->Destroy(); // Remove from db
        delete item;
        item = NULL;
    }
    else if (data.action == "intervals")
    {
        if (data.interval < 0 || data.random < 0 || data.interval != data.interval || data.random != data.random)
        {
            psserver->SendSystemError(me->clientnum,"Invalid intervals specified");
            return;
        }

        // In seconds
        int interval = 1000*data.interval;
        int random   = 1000*data.random;

        if (item->GetScheduledItem())
        {
            item->GetScheduledItem()->ChangeIntervals(interval,random);
            psserver->SendSystemInfo(me->clientnum,"Intervals for %s set to %d base + %d max modifier",item->GetName(),data.interval,data.random);
        }
        else
            psserver->SendSystemError(me->clientnum,"This item does not spawn; no intervals");
    }
    else if (data.action == "move")
    {
        gemItem* gItem = dynamic_cast<gemItem*>(object);
        if (gItem)
        {
            int instance = object->GetInstance();
            iSector* sector = object->GetSector();

            csVector3 pos(data.x, data.y, data.z);
            gItem->SetPosition(pos, data.rot, sector, instance);
        }
    }
    else
    {
        bool onoff;
        if (data.setting == "true")
            onoff = true;
        else if (data.setting == "false")
            onoff = false;
        else
        {
            psserver->SendSystemError(me->clientnum,"Invalid settings");
            return;
        }

        if (data.action == "pickupable")
        {
            item->SetIsPickupable(onoff);
            psserver->SendSystemInfo(me->clientnum,"%s is now %s",item->GetName(),(onoff)?"pickupable":"un-pickupable");
        }
        else if (data.action == "transient")
        {
            item->SetIsTransient(onoff);
            psserver->SendSystemInfo(me->clientnum,"%s is now %s",item->GetName(),(onoff)?"transient":"non-transient");
        }
        else if (data.action == "npcowned")
        {
            item->SetIsNpcOwned(onoff);
            psserver->SendSystemInfo(me->clientnum, "%s is now %s",
                                    item->GetName(), onoff ? "npc owned" : "not npc owned");
        }
        else if (data.action == "collide")
        {
            item->SetIsCD(onoff);
            psserver->SendSystemInfo(me->clientnum, "%s is now %s",
                                    item->GetName(), onoff ? "using collision detection" : "not using collision detection");
            item->GetGemObject()->Send(me->clientnum, false, false);
            item->GetGemObject()->Broadcast(me->clientnum, false);
        }
        // TODO: Add more flags
        else
        {
            psserver->SendSystemError(me->clientnum,"Invalid action");
        }
        item->Save(false);
    }
}

void AdminManager::Morph(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data, Client *client, Client *targetclient)
{
    if (data.player == "list" && data.mesh.IsEmpty())
    {
        static csString list;
        if (list.IsEmpty())  // Construct list once
        {
            // Get array of mounted model directories
            const char* modelsPath = "/planeshift/models/";
            size_t modelsPathLength = strlen(modelsPath);
            csRef<iVFS> vfs = csQueryRegistry<iVFS> (psserver->GetObjectReg());
            csRef<iStringArray> dirPaths = vfs->FindFiles(modelsPath);
            csStringArray dirNames;
            for (size_t i=0; i < dirPaths->GetSize(); i++)
            {
                csString path = dirPaths->Get(i);
                csString name = path.Slice( modelsPathLength, path.Length()-modelsPathLength-1 );
                if (name.Length() && name.GetAt(0) != '.')
                {
                    if ( vfs->Exists(path+name+".cal3d") )
                        dirNames.Push(name);
                    else
                        Error2("Model dir %s lacks a valid cal3d file!", name.GetData() );
                }
            }

            // Make alphabetized list
            dirNames.Sort();
            list = "Available models:  ";
            for (size_t i=0; i<dirNames.GetSize(); i++)
            {
                list += dirNames[i];
                if (i < dirNames.GetSize()-1)
                    list += ", ";
            }
        }

        psserver->SendSystemInfo(me->clientnum, "%s", list.GetData() );
        return;
    }

    if (!targetclient || !targetclient->GetActor())
    {
        psserver->SendSystemError(me->clientnum,"Invalid target for morph");
        return;
    }
    
    gemActor* target = targetclient->GetActor();

    if (data.mesh == "reset")
    {
        if ( target->ResetMesh() )
            psserver->SendSystemInfo(me->clientnum,"Resetting mesh for %s", targetclient->GetName() );
        else
            psserver->SendSystemError(me->clientnum,"Error resetting mesh for %s!", targetclient->GetName() );

    }
    else
    {
        if ( target->SetMesh(data.mesh) )
            psserver->SendSystemInfo(me->clientnum,"Setting mesh for %s to %s", targetclient->GetName(), data.mesh.GetData() );
        else
            psserver->SendSystemError(me->clientnum,"Error setting mesh %s!", data.mesh.GetData() );
    }
}

void AdminManager::TempSecurityLevel(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data, Client *client, Client *target)
{
    if (!target || !target->GetActor())
    {
        psserver->SendSystemError(me->clientnum,"Invalid target");
        return;
    }
    
    // Can only set others to a max of 3 levels below own level (ex: GM4 can set someone to GM1)
    int maxleveltoset = client->GetSecurityLevel() - 3;

    int value;

    data.setting.Downcase();
    if (data.setting == "reset")
    {
        int trueSL = GetTrueSecurityLevel( target->GetAccountID() );
        if (trueSL < 0)
        {
            psserver->SendSystemError(client->GetClientNum(), "Cannot reset access level for %s!", target->GetName() );
            return;
        }

        target->SetSecurityLevel(trueSL);
        target->GetActor()->SetSecurityLevel(trueSL);

		 // Refresh the label
		target->GetActor()->UpdateProxList(true);

        psserver->SendSystemOK(target->GetClientNum(),"Your access level was reset");
        if (target != client)
            psserver->SendSystemOK(me->clientnum,"Access level for %s was reset",target->GetName());
            
        if (trueSL)
            psserver->SendSystemInfo(target->GetClientNum(),"Your access level has been reset to %d",trueSL);

        return;
    }
    else if (data.setting == "player")
        value = 0;
    else if (data.setting == "tester")
        value = GM_TESTER;
    else if (data.setting == "gm")
        value = GM_LEVEL_1;
    else if (data.setting.StartsWith("gm",true) && data.setting.Length() == 3)
        value = atoi(data.setting.Slice(2,1)) + GM_LEVEL_0;
    else
    {
        psserver->SendSystemError(me->clientnum,"Valid settings are:  player, tester, GM, or reset.  GM levels may be specified:  GM1, ... GM5");
        return;
    }

    if (!CacheManager::GetSingleton().GetCommandManager()->GroupExists(value) )
    {
        psserver->SendSystemError(me->clientnum,"Specified access level does not exist!");
        return;
    }

    if ( target == client && value > GetTrueSecurityLevel(target->GetAccountID()) )
    {
        psserver->SendSystemError(me->clientnum,"You cannot upgrade your own level!");
        return;
    }
    else if ( target != client && value > maxleveltoset )
    {
        psserver->SendSystemError(me->clientnum,"Max access level you may set is %d", maxleveltoset);
        return;
    }

    if (target->GetSecurityLevel() == value)
    {
        psserver->SendSystemError(me->clientnum,"%s is already at that access level", target->GetName());
        return;
    }

    if (value == 0)
    {
        psserver->SendSystemInfo(target->GetClientNum(),"Your access level has been disabled for this session.");
    }
    else  // Notify of added/removed commands
    {
        psserver->SendSystemInfo(target->GetClientNum(),"Your access level has been changed for this session. "
                                 " Use \"/admin\" to enable and list available GM commands.");
    }

    if (value < GM_LEVEL_4) // Cannot access this command, but may still reset
    {
        psserver->SendSystemInfo(target->GetClientNum(),"You may do \"/deputize me reset\" at any time to reset yourself. "
                                 " The temporary access level will also expire on logout.");
    }
    

    // Set temporary security level (not saved to DB)
    target->SetSecurityLevel(value);
    target->GetActor()->SetSecurityLevel(value);
    
    // Refresh the label
    target->GetActor()->UpdateProxList(true);

    psserver->SendSystemOK(me->clientnum,"Access level for %s set to %s",target->GetName(),data.setting.GetData());
    if (target != client)
    {
        psserver->SendSystemOK(target->GetClientNum(),"Your access level was set to %s by a GM",data.setting.GetData());
    }
    
}

int AdminManager::GetTrueSecurityLevel(int accountID)
{
    Result result(db->Select("SELECT security_level FROM accounts WHERE id='%d'", accountID ));

    if (!result.IsValid() || result.Count() != 1)
        return -99;
    else
        return result[0].GetUInt32("security_level");
}

void AdminManager::HandleGMEvent(MsgEntry* me, psAdminCmdMessage& msg, AdminCmdData& data, Client* client, Client* target)
{
    bool gmeventResult;
    GMEventManager* gmeventManager = psserver->GetGMEventManager();
    
    // bit more vetting of the /event command - if in doubt, give help
    if ((data.subCmd == "create" && 
        (data.gmeventName.Length() == 0 || data.gmeventDesc.Length() == 0)) ||
        (data.subCmd == "register" && data.player.Length() == 0 && data.rangeSpecifier == INDIVIDUAL) ||
        (data.subCmd == "remove" && data.player.Length() == 0) ||
        (data.subCmd == "reward" && data.item.Length() == 0 && data.stackCount == 0) ||
        (data.subCmd == "control" && data.gmeventName.Length() == 0))
    {
        data.subCmd = "help";
    }

    // HELP!
    if (data.subCmd == "help")
    {
        psserver->SendSystemInfo( me->clientnum, "/event help\n"
                                  "/event create <name> <description>\n"
                                  "/event register [range <range> | <player>]\n"
                                  "/event reward [all | range <range> | <player>] # <item>\n"
                                  "/event remove <player>\n"
                                  "/event complete [name]\n"
                                  "/event list\n"
                                  "/event control <name>\n");
        return;
    }

    // add new event
    if (data.subCmd == "create")
    {
        gmeventResult = gmeventManager->AddNewGMEvent(client, data.gmeventName, data.gmeventDesc);
        return;
    }

    // register player(s) with the event
    if (data.subCmd == "register")
    {
        /// this looks odd, because the range value is in the 'player' parameter.
        if (data.rangeSpecifier == IN_RANGE)
        {
            gmeventResult = gmeventManager->RegisterPlayersInRangeInGMEvent(client, data.range);
        }
        else
        {
            gmeventResult = gmeventManager->RegisterPlayerInGMEvent(client, target);
        }
        return;
    }
    
    // player completed event
    if (data.subCmd == "complete")
    {
        if (data.name == "")
            gmeventResult = gmeventManager->CompleteGMEvent(client,
                                                            client->GetPlayerID());
        else
            gmeventResult = gmeventManager->CompleteGMEvent(client,
                                                            data.name);
        return;
    }

    //remove player
    if (data.subCmd == "remove")
    {
        gmeventResult = gmeventManager->RemovePlayerFromGMEvent(client,
                                                                target);
        return;
    }

    // reward player(s)
    if (data.subCmd == "reward")
    {
        gmeventResult = gmeventManager->RewardPlayersInGMEvent(client,
                                                               data.rangeSpecifier,
                                                               data.range,
                                                               target,
                                                               data.stackCount,
                                                               data.item);
        return;
    }

    if (data.subCmd == "list")
    {
        gmeventResult = gmeventManager->ListGMEvents(client);
        return;
    }

    if (data.subCmd == "control")
    {
        gmeventResult = gmeventManager->AssumeControlOfGMEvent(client, data.gmeventName);
        return;
    }
}

void AdminManager::HandleBadText(psAdminCmdMessage& msg, AdminCmdData& data, Client *client, gemObject *targetobject)
{
    if (!targetobject)
    {
        psserver->SendSystemError(client->GetClientNum(), "You must select an npc first.");
        return;
    }
    gemNPC *npc = targetobject->GetNPCPtr();
    if (!npc)
    {
        psserver->SendSystemError(client->GetClientNum(), "You must select an npc first.");
        return;
    }

    csStringArray saidArray;
    csStringArray trigArray;

    npc->GetBadText(data.value, data.interval, saidArray, trigArray);
    psserver->SendSystemInfo(client->GetClientNum(), "Bad Text for %s", npc->GetName() );
    psserver->SendSystemInfo(client->GetClientNum(), "--------------------------------------");

    for (size_t i=0; i<saidArray.GetSize(); i++)
    {
        psserver->SendSystemInfo(client->GetClientNum(), "%s -> %s", saidArray[i], trigArray[i]);
    }
    psserver->SendSystemInfo(client->GetClientNum(), "--------------------------------------");
}

void AdminManager::HandleCompleteQuest(MsgEntry* me,psAdminCmdMessage& msg, AdminCmdData& data, Client *client, Client *subject)
{
    Client* target = client;
    if (subject && CacheManager::GetSingleton().GetCommandManager()->Validate(client->GetSecurityLevel(), "quest others"))
        target = subject;    

    switch (data.value)
    {
        case 1:
        {
            psQuest* quest = CacheManager::GetSingleton().GetQuestByName(data.text);
            if (!quest)
            {
                psserver->SendSystemError(me->clientnum, "Quest not found!");
                return;
            }
            target->GetActor()->GetCharacterData()->AssignQuest(quest, 0);
            if (target->GetActor()->GetCharacterData()->CompleteQuest(quest))
            {
                psserver->SendSystemInfo(me->clientnum, "Quest %s completed!", data.text.GetData());
            }
            break;
        }
        case 0:
        {
            psQuest* quest = CacheManager::GetSingleton().GetQuestByName(data.text);
            if (!quest)
            {
                psserver->SendSystemError(me->clientnum, "Quest not found!");
                return;
            }
            QuestAssignment* questassignment = target->GetActor()->GetCharacterData()->IsQuestAssigned(quest->GetID());
            if (!questassignment)
            {
                psserver->SendSystemError(me->clientnum, "Quest was never started!");
                return;
            }
            target->GetActor()->GetCharacterData()->DiscardQuest(questassignment, true);
            psserver->SendSystemInfo(me->clientnum, "Quest %s discarded!", data.text.GetData());
            break;
        }
        case 2:
        {
            csArray<QuestAssignment*> quests = target->GetCharacterData()->GetAssignedQuests();
            size_t len = quests.GetSize();
            for (size_t i = 0 ; i < len ; i++)
            {
                QuestAssignment* currassignment = quests.Get(i);
                psserver->SendSystemInfo(me->clientnum, "Quest name: %s. Status: %c", currassignment->GetQuest()->GetName(), currassignment->status);
            }
            break;
        }
    }
}

void AdminManager::HandleSetQuality(psAdminCmdMessage& msg, AdminCmdData& data, Client *client, gemObject* object )
{
    if (!object)
    {
        psserver->SendSystemError(client->GetClientNum(), "No target selected");
        return;
    }

    psItem *item = object->GetItem();
    if (!item)
    {
        psserver->SendSystemError(client->GetClientNum(), "Not an item");
        return;
    }
    
    item->SetItemQuality(data.x);
    if (data.y)
        item->SetMaxItemQuality(data.y);

    item->Save(false);

    psserver->SendSystemOK(client->GetClientNum(), "Quality changed successfully");
}

void AdminManager::HandleSetTrait(psAdminCmdMessage& msg, AdminCmdData& data, Client *client, gemObject* object )
{
    if (data.name.IsEmpty())
    {
        psserver->SendSystemError(client->GetClientNum(), "Syntax: /settrait [target] [trait]");
        return;
    }

    psCharacter* target;
    if (object && object->GetCharacterData())
    {
        target = object->GetCharacterData();
    }
    else
    {
        psserver->SendSystemError(client->GetClientNum(), "Invalid target for setting traits");
        return;
    }

    CacheManager::TraitIterator ti = CacheManager::GetSingleton().GetTraitIterator();
    while(ti.HasNext())
    {
        psTrait* currTrait = ti.Next();
        if (currTrait->gender == target->GetRaceInfo()->gender &&
            currTrait->race == target->GetRaceInfo()->race &&
            currTrait->name.CompareNoCase(data.name))
        {
            target->SetTraitForLocation(currTrait->location, currTrait);
            csString str( "<traits>" );
            str.Append(currTrait->ToXML() );
            str.Append("</traits>");        
            psTraitChangeMessage message( client->GetClientNum(), (uint32_t)target->GetActor()->GetEntity()->GetID(), str );
            message.Multicast( target->GetActor()->GetMulticastClients(), 0, PROX_LIST_ANY_RANGE );     
            psserver->SendSystemOK(client->GetClientNum(), "Trait successfully changed");
            return;
        }
    }
    psserver->SendSystemError(client->GetClientNum(), "Trait not found");
}

void AdminManager::HandleSetItemName(psAdminCmdMessage& msg, AdminCmdData& data, Client *client, gemObject* object )
{
    if (!object)
    {
        psserver->SendSystemError(client->GetClientNum(), "No target selected");
        return;
    }

    psItem *item = object->GetItem();
    if (!item)
    {
        psserver->SendSystemError(client->GetClientNum(), "Not an item");
        return;
    }

    item->SetName(data.name);
    if (data.description != "")
        item->SetDescription(data.description);

    item->Save(false);

    psserver->SendSystemOK(client->GetClientNum(), "Name changed successfully");
}

void AdminManager::HandleReload(psAdminCmdMessage& msg, AdminCmdData& data, Client *client, gemObject* object )
{
    if (data.subCmd == "item")
    {
        bool bCreatingNew = false;
        psItemStats* itemStats = CacheManager::GetSingleton().GetBasicItemStatsByID(data.value);
        iResultSet* rs = db->Select("select * from item_stats where id = %d", data.value);
        if (!rs || rs->Count() == 0)
        {
            psserver->SendSystemError(client->GetClientNum(), "Item stats for %d not found", data.value);
            return;
        }
        if (itemStats == NULL)
        {
            bCreatingNew = true;
            itemStats = new psItemStats();
        }

        if (!itemStats->ReadItemStats((*rs)[0]))
        {
            psserver->SendSystemError(client->GetClientNum(), "Couldn't load new item stats", data.value);
            if (bCreatingNew)
                delete itemStats;
            return;
        }
        
        if (bCreatingNew)
        {
            CacheManager::GetSingleton().AddItemStatsToHashTable(itemStats);
            psserver->SendSystemOK(client->GetClientNum(), "Successfully created new item", data.value);
        }
        else
            psserver->SendSystemOK(client->GetClientNum(), "Successfully modified item", data.value);
    }
}

void AdminManager::HandleListWarnings(psAdminCmdMessage& msg, AdminCmdData& data, Client *client, gemObject* object )
{
    Client* target = NULL;
    if (data.target != "" && data.target != "target")
    {
        target = psserver->GetCharManager()->FindPlayerClient(data.target);
    }
    else if(object)
    {
        gemActor* targetActor = dynamic_cast<gemActor*>(object);
        if (targetActor)
            target = targetActor->GetClient();
    }

    if (target)
    {
        Result rs(db->Select("select warningGM, timeOfWarn, warnMessage from warnings where accountid = %d", client->GetAccountID()));
        if (rs.IsValid())
        {
            csString newLine;
            unsigned long i = 0;
            for (i = 0 ; i < rs.Count() ; i++)
            {
                newLine.Format("%s - %s - %s", rs[i]["warningGM"], rs[i]["timeOfWarn"], rs[i]["warnMessage"]);
                psserver->SendSystemInfo(client->GetClientNum(), newLine.GetData());
            }
            if (i == 0)
                psserver->SendSystemInfo(client->GetClientNum(), "No warnings found");
        }
    }
    else
        psserver->SendSystemError(client->GetClientNum(), "Target wasn't found");
}
