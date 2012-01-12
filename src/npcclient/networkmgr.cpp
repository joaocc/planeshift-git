/*
* networkmgr.cpp
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

//=============================================================================
// Crystal Space Includes
//=============================================================================
#include <csutil/csstring.h>
#include <iutil/vfs.h>
#include <iengine/engine.h>
#include <csutil/strhashr.h>


//=============================================================================
// Project Space Includes
//=============================================================================
#include <ibgloader.h>
#include "util/log.h"
#include "util/serverconsole.h"
#include "util/command.h"
#include "util/eventmanager.h"

#include "net/connection.h"
#include "net/messages.h"
#include "net/msghandler.h"
#include "net/npcmessages.h"
#include "engine/linmove.h"
#include "util/strutil.h"

//=============================================================================
// Local Space Includes
//=============================================================================
#include "networkmgr.h"
#include "globals.h"
#include "npcclient.h"
#include "npc.h"
#include "gem.h"
#include "tribe.h"

extern bool running;

NetworkManager::NetworkManager(MsgHandler *mh,psNetConnection* conn, iEngine* engine)
: reconnect(false)
{
    msghandler = mh;
    this->engine = engine;
    ready = false;
    connected = false;
    msghandler->Subscribe(this,MSGTYPE_NPCLIST);
    msghandler->Subscribe(this,MSGTYPE_MAPLIST);
    msghandler->Subscribe(this,MSGTYPE_CELPERSIST);
    msghandler->Subscribe(this,MSGTYPE_ALLENTITYPOS);
    msghandler->Subscribe(this,MSGTYPE_NPCOMMANDLIST);
    msghandler->Subscribe(this,MSGTYPE_PERSIST_ALL_ENTITIES);
    msghandler->Subscribe(this,MSGTYPE_PERSIST_ACTOR);
    msghandler->Subscribe(this,MSGTYPE_PERSIST_ITEM);
    msghandler->Subscribe(this,MSGTYPE_REMOVE_OBJECT);
    msghandler->Subscribe(this,MSGTYPE_DISCONNECT);
    msghandler->Subscribe(this,MSGTYPE_WEATHER);
    msghandler->Subscribe(this,MSGTYPE_MSGSTRINGS);
    msghandler->Subscribe(this,MSGTYPE_NEW_NPC);
    msghandler->Subscribe(this,MSGTYPE_NPC_COMMAND);
    msghandler->Subscribe(this,MSGTYPE_NPCRACELIST);
    msghandler->Subscribe(this,MSGTYPE_NPC_WORKDONE);

    connection= conn;
    connection->SetEngine( engine );

    PrepareCommandMessage();
}

NetworkManager::~NetworkManager()
{
    if (msghandler)
    {
        msghandler->Unsubscribe(this,MSGTYPE_NPCLIST);
        msghandler->Unsubscribe(this,MSGTYPE_MAPLIST);
        msghandler->Unsubscribe(this,MSGTYPE_CELPERSIST);
        msghandler->Unsubscribe(this,MSGTYPE_ALLENTITYPOS);
        msghandler->Unsubscribe(this,MSGTYPE_NPCOMMANDLIST);
        msghandler->Unsubscribe(this,MSGTYPE_PERSIST_ACTOR);
        msghandler->Unsubscribe(this,MSGTYPE_PERSIST_ITEM);
        msghandler->Unsubscribe(this,MSGTYPE_REMOVE_OBJECT);
        msghandler->Unsubscribe(this,MSGTYPE_DISCONNECT);
        msghandler->Unsubscribe(this,MSGTYPE_WEATHER);
        msghandler->Unsubscribe(this,MSGTYPE_MSGSTRINGS);
        msghandler->Unsubscribe(this,MSGTYPE_NPC_COMMAND);
        msghandler->Unsubscribe(this,MSGTYPE_NPCRACELIST);
        msghandler->Unsubscribe(this,MSGTYPE_NPC_WORKDONE);
    }

    delete outbound;
    outbound = NULL;
}

void NetworkManager::Authenticate(csString& host,int port,csString& user,csString& pass)
{
    this->port = port;
    this->host = host;
    this->user = user;
    this->password = pass;

    psNPCAuthenticationMessage login(0,user,pass);
    msghandler->SendMessage(login.msg);
}

void NetworkManager::Disconnect()
{
    psDisconnectMessage discon(0, 0, "");
    msghandler->SendMessage(discon.msg);    
    connection->SendOut(); // Flush the network
    connection->DisConnect();
}

csStringHashReversible * NetworkManager::GetMsgStrings()
{
    return connection->GetAccessPointers()->msgstringshash;
}

const char *NetworkManager::GetCommonString(uint32_t cstr_id)
{
    return connection->GetAccessPointers()->Request(cstr_id);
}

uint32_t NetworkManager::GetCommonStringID(const char* string)
{
    return connection->GetAccessPointers()->Request(string).GetHash();
}


void NetworkManager::HandleMessage(MsgEntry *message)
{
    switch ( message->GetType() )
    {
        case MSGTYPE_MAPLIST:
        {
            connected = true;
            if (ReceiveMapList(message))
            {
                RequestAllObjects();    
            }
            else
            {
                npcclient->Disconnect();
            }
            
            break;
        }
        case MSGTYPE_NPCRACELIST:
        {
            HandleRaceList( message );
            break;
        }
        case MSGTYPE_NPCLIST:
        {
            ReceiveNPCList(message);
            ready = true;
            // Activates NPCs on server side
            psNPCReadyMessage mesg;
            msghandler->SendMessage( mesg.msg );
            npcclient->LoadCompleted();
            break;
        }
        case MSGTYPE_PERSIST_ALL_ENTITIES:
        {
            HandleAllEntities(message);
            break;
        }
        case MSGTYPE_PERSIST_ACTOR:
        {
            HandleActor( message );
            break;
        }        
        
        case MSGTYPE_PERSIST_ITEM:
        {
            HandleItem( message );
            break;
        }        
        
        case MSGTYPE_REMOVE_OBJECT:
        {
            HandleObjectRemoval( message );
            break;
        }

        case MSGTYPE_ALLENTITYPOS:
        {
            HandlePositionUpdates(message);
            break;
        }
        case MSGTYPE_NPCOMMANDLIST:
        {
            HandlePerceptions(message);
            break;
        }
        case MSGTYPE_MSGSTRINGS:
        {
            csRef<iEngine> engine =  csQueryRegistry<iEngine> (npcclient->GetObjectReg());

            // receive message strings hash table
            psMsgStringsMessage msg(message);
            connection->SetMsgStrings(0, msg.msgstrings);
            break;
        }
        case MSGTYPE_DISCONNECT:
        {
            HandleDisconnect(message);
            break;
        }
        case MSGTYPE_WEATHER:
        {
            HandleTimeUpdate(message);
            break;
        }
        case MSGTYPE_NEW_NPC:
        {
            HandleNewNpc(message);
            break;
        }
        case MSGTYPE_NPC_COMMAND:
        {
            HandleConsoleCommand(message);
            break;
        }
        case MSGTYPE_NPC_WORKDONE:
        {
            HandleNPCWorkDone(message);
        }
    }
}

void NetworkManager::HandleConsoleCommand(MsgEntry *me)
{
    csString buffer;

    psServerCommandMessage msg(me);
    printf("Got command: %s\n", msg.command.GetDataSafe() );

    size_t i = msg.command.FindFirst(' ');
    csString word;
    msg.command.SubString(word,0,i);
    const COMMAND *cmd = find_command(word.GetDataSafe());

    if (cmd && cmd->allowRemote)
    {
        int ret = execute_line(msg.command, &buffer);
        if (ret == -1)
            buffer = "Error executing command on the server.";
    }
    else
    {
        buffer = cmd ? "That command is not allowed to be executed remotely"
                     : "No command by that name.  Please try again.";
    }
    printf("%s\n", buffer.GetData());

    /*psServerCommandMessage retn(me->clientnum, buffer);
    retn.SendMessage();*/
}

void NetworkManager::HandleRaceList( MsgEntry* me)
{
    psNPCRaceListMessage mesg( me );

    size_t count = mesg.raceInfo.GetSize();
    for (size_t c = 0; c < count; c++)
    {
        npcclient->AddRaceInfo(mesg.raceInfo[c].name,mesg.raceInfo[c].walkSpeed,mesg.raceInfo[c].runSpeed);
    }
}

void NetworkManager::HandleAllEntities(MsgEntry *message)
{
    psPersistAllEntities allEntities(message);

    printf("Got All Entities message.\n");
    bool done=false;
    int count=0;
    while (!done)
    {
        count++;
        MsgEntry *entity = allEntities.GetEntityMessage();
        if (entity)
        {
            if (entity->GetType() == MSGTYPE_PERSIST_ACTOR)
                HandleActor(entity);
            else if (entity->GetType() == MSGTYPE_PERSIST_ITEM)
                HandleItem(entity);
            else
                Error2("Unhandled type of entity (%d) in AllEntities message.",entity->GetType() );

            delete entity;
        }
        else
            done = true;
    }
    printf("End of All Entities message, with %d entities done.\n", count);
}

void NetworkManager::HandleActor(MsgEntry *me)
{
    psPersistActor mesg( me, connection->GetAccessPointers(), true );

    Debug4(LOG_NET, 0, "Got persistActor message, size %zu, id=%d, name=%s", me->GetSize(),mesg.playerID.Unbox(),mesg.name.GetDataSafe() );

    gemNPCObject * obj = npcclient->FindEntityID(mesg.entityid);

    if(obj && obj->GetPID() == mesg.playerID)
    {
        // We already know this entity so just update the entity.
        CPrintf(CON_ERROR, "Already know about gemNPCActor: %s (%s), %s %s.\n",
                mesg.name.GetData(), obj->GetName(), ShowID(mesg.playerID), ShowID(mesg.entityid));

        obj->Move(mesg.pos, mesg.yrot, mesg.sectorName, mesg.instance );
        obj->SetInvisible( (mesg.flags & psPersistActor::INVISIBLE) ? true : false );
        obj->SetInvincible( (mesg.flags & psPersistActor::INVINCIBLE) ? true : false );
        obj->SetAlive( (mesg.flags & psPersistActor::IS_ALIVE) ? true : false );
        
        return;
    }

    if(!obj && mesg.playerID != 0)
    {
        // Check if we find obj in characters
        obj = npcclient->FindCharacterID(mesg.playerID);
    }

    if(obj)
    {
        // We have a player id, entity id mismatch and we already know this entity
        // so we can only assume a RemoveObject message misorder and we will delete the existing one and recreate.
        CPrintf(CON_ERROR, "Deleting because we already know gemNPCActor: "
                "<%s, %s, %s> as <%s, %s, %s>.\n",
                mesg.name.GetData(), ShowID(mesg.entityid), ShowID(mesg.playerID), 
                obj->GetName(), ShowID(obj->GetEID()), ShowID(obj->GetPID()));

        npcclient->Remove(obj);
        obj = NULL; // Obj isn't valid after remove
    }

    gemNPCActor* actor = new gemNPCActor( npcclient, mesg);
    
    if ( mesg.flags & psPersistActor::NPC )
    {
        npcclient->AttachNPC( actor, mesg.counter, mesg.ownerEID, mesg.masterID );                
    }
    
    npcclient->Add( actor );
}

void NetworkManager::HandleItem( MsgEntry* me )
{
    psPersistItem mesg(me, connection->GetAccessPointers());

    gemNPCObject * obj = npcclient->FindEntityID(mesg.eid);

    Debug4(LOG_NET, 0, "Got persistItem message, size %zu, eid=%d, name=%s\n", me->GetSize(),mesg.eid.Unbox(),mesg.name.GetDataSafe() );

    if(obj && obj->GetPID().IsValid())
    {
        // We have a player/NPC item mismatch.
        CPrintf(CON_ERROR, "Deleting because we already know gemNPCActor: "
                "<%s, %s> as <%s, %s>.\n",
                mesg.name.GetData(), ShowID(mesg.eid), obj->GetName(), ShowID(obj->GetEID()));

        npcclient->Remove(obj);
        obj = NULL; // Obj isn't valid after remove
    }
    

    if(obj)
    {
        // We already know this item so just update the position.
        CPrintf(CON_ERROR, "Deleting because we already know "
                "gemNPCItem: %s (%s), %s.\n", mesg.name.GetData(), 
                obj->GetName(), ShowID(mesg.eid));
        
        npcclient->Remove(obj);
        obj = NULL; // Obj isn't valid after remove
    }

    // Add the new item to the world
    gemNPCItem* item = new gemNPCItem(npcclient, mesg);        
    npcclient->Add(item);

    // Is this a tribe Item?
    if(mesg.tribeID != 0)
    {
        csVector3 where;
        Tribe*    tribe = npcclient->GetTribe(mesg.tribeID);
        if(tribe)
        {
            tribe->HandlePersitItem(item);
        }
    }

}

void NetworkManager::HandleObjectRemoval( MsgEntry* me )
{
    psRemoveObject mesg(me);

    gemNPCObject * object = npcclient->FindEntityID( mesg.objectEID );
    if (object == NULL)
    {
        CPrintf(CON_ERROR, "NPCObject %s cannot be removed - not found\n", ShowID(mesg.objectEID));
        return;
    }

    // If this is a NPC remove any queued dr updates before removing the entity.
    NPC * npc = object->GetNPC();
    if (npc)
    {
        DequeueDRData( npc );
    }

    npcclient->Remove( object ); // Object isn't valid after remove
}

void NetworkManager::HandleTimeUpdate( MsgEntry* me )
{
    psWeatherMessage msg(me);

    if (msg.type == psWeatherMessage::DAYNIGHT)  // time update msg
    {
        npcclient->UpdateTime(msg.minute, msg.hour, msg.day, msg.month, msg.year);
    }
}


void NetworkManager::RequestAllObjects()
{
    Notify1(LOG_STARTUP, "Requesting all game objects");    
    
    psRequestAllObjects mesg;
    msghandler->SendMessage( mesg.msg );
}


bool NetworkManager::ReceiveMapList(MsgEntry *msg)
{
    psMapListMessage list(msg);
    CPrintf(CON_CMDOUTPUT,"\n");
    for (size_t i=0; i<list.map.GetSize(); i++)
    {
        CPrintf(CON_CMDOUTPUT,"Loading world '%s'\n",list.map[i].GetDataSafe());
        
        if (!npcclient->LoadMap(list.map[i]))
        {
            CPrintf(CON_ERROR,"Failed to load world '%s'\n",list.map[i].GetDataSafe());
            return false;
        }
    }

    CPrintf(CON_CMDOUTPUT,"Finishing all map loads\n");

    csRef<iBgLoader> loader = csQueryRegistry<iBgLoader>(npcclient->GetObjectReg());
    while(loader->GetLoadingCount() != 0)
        loader->ContinueLoading(true);

    // @@@ RlyDontKnow: Path Networks have to be loaded *after*
    // all required maps are loaded. Else sector Transformations
    // aren't known which will result in broken paths including
    // warp portals.
    if (!npcclient->LoadPathNetwork())
    {
        CPrintf(CON_ERROR, "Couldn't load the path network\n");
        exit(1);
    }

    return true;
}

bool NetworkManager::ReceiveNPCList(MsgEntry *msg)
{
    uint32_t length, pid, eid;

    length = msg->GetUInt32();
    CPrintf(CON_WARNING, "Received list of %i NPCs.\n", length);  
    for (unsigned int x=0; x<length; x++)
    {
        pid = msg->GetUInt32();
        eid = msg->GetUInt32();
    }

    return true;
}



void NetworkManager::HandlePositionUpdates(MsgEntry *msg)
{
    psAllEntityPosMessage updates(msg);

    csRef<iEngine> engine =  csQueryRegistry<iEngine> (npcclient->GetObjectReg());

    for (int x=0; x<updates.count; x++)
    {
        csVector3 pos;
        iSector* sector;
        InstanceID instance;
        bool forced;

        EID id = updates.Get(pos, sector, instance, forced, 0, GetMsgStrings(), engine);
        npcclient->SetEntityPos(id, pos, sector, instance, forced);
    }
}

void NetworkManager::HandlePerceptions(MsgEntry *msg)
{
    psNPCCommandsMessage list(msg);

    char cmd = list.msg->GetInt8();
    while (cmd != psNPCCommandsMessage::CMD_TERMINATOR)
    {
        switch(cmd)
        {
            case psNPCCommandsMessage::PCPT_ASSESS:
            {
                // Extract the data
                EID actorEID = EID(msg->GetUInt32());
                EID targetEID = EID(msg->GetUInt32());
                csString physical = msg->GetStr();
                csString magical = msg->GetStr();
                csString overall = msg->GetStr();

                NPC *npc = npcclient->FindNPC(actorEID);
                if (!npc)
                {
                    Debug3(LOG_NPC, actorEID.Unbox(), "Got assess perception for unknown NPC(%s) for %s!\n", ShowID(targetEID), ShowID(targetEID));
                    break;
                }

                gemNPCObject *target = npcclient->FindEntityID(targetEID);
                if (!target)
                {
                    npc->Printf("Got access perception from unknown target(%s)!\n", ShowID(targetEID));
                    break;
                }

                npc->Printf("Got Assess perception for %s(%s) to %s(%s) with "
                            "Physical assessment: '%s' "
                            "Magical assessment: '%s' "
                            "Overall assessment: '%s'",
                            npc->GetName(), ShowID(actorEID),
                            target->GetName(), ShowID(targetEID),
                            physical.GetDataSafe(),magical.GetDataSafe(),overall.GetDataSafe());

                Perception physicalPerception(physical);
                npc->TriggerEvent(&physicalPerception);

                Perception magicalPerception(magical);
                npc->TriggerEvent(&magicalPerception);

                Perception overallPerception(overall);
                npc->TriggerEvent(&overallPerception);
                break;
            }
            case psNPCCommandsMessage::PCPT_SPOKEN_TO:
            {
                EID npcEID    = EID(list.msg->GetUInt32());
                bool spokenTo = list.msg->GetBool();

                NPC *npc = npcclient->FindNPC(npcEID);
                if (!npc)
                {
                    Debug2(LOG_NPC, npcEID.Unbox(), "Got spoken_to perception for unknown NPC(%s)!\n", ShowID(npcEID));
                    break;
                }


                Perception perception("spoken_to",spokenTo?"true":"false");
                npc->Printf("Got spoken_to perception for actor %s(%s) with spoken_to=%s.\n",
                            npc->GetName(), ShowID(npcEID), spokenTo?"true":"false");

                npc->TriggerEvent(&perception);
                break;
            }
            case psNPCCommandsMessage::PCPT_TALK:
            {
                EID speakerEID = EID(list.msg->GetUInt32());
                EID targetEID  = EID(list.msg->GetUInt32());
                int faction    = list.msg->GetInt16();

                NPC *npc = npcclient->FindNPC(targetEID);
                if (!npc)
                {
                    Debug3(LOG_NPC, targetEID.Unbox(), "Got talk perception for unknown NPC(%s) from %s!\n", ShowID(targetEID), ShowID(speakerEID));
                    break;
                }

                gemNPCObject *speaker_ent = npcclient->FindEntityID(speakerEID);
                if (!speaker_ent)
                {
                    npc->Printf("Got talk perception from unknown speaker(%s)!\n", ShowID(speakerEID));
                    break;
                }

                FactionPerception talk("talk",faction,speaker_ent);
                npc->Printf("Got Talk perception for from actor %s(%s), faction diff=%d.\n",
                            speaker_ent->GetName(), ShowID(speakerEID), faction);

                npc->TriggerEvent(&talk);
                break;
            }
            case psNPCCommandsMessage::PCPT_ATTACK:
            {
                EID targetEID   = EID(list.msg->GetUInt32());
                EID attackerEID = EID(list.msg->GetUInt32());

                NPC *npc = npcclient->FindNPC(targetEID);
                gemNPCActor *attacker_ent = (gemNPCActor*)npcclient->FindEntityID(attackerEID);
                
                if (!npc)
                {
                    Debug2(LOG_NPC, targetEID.Unbox(), "Got attack perception for unknown NPC(%s)!\n", ShowID(targetEID));
                    break;
                }
                if (!attacker_ent)
                {
                    npc->Printf("Got attack perception for unknown attacker (%s)!", ShowID(attackerEID));
                    break;
                }

                AttackPerception attack("attack",attacker_ent);
                npc->Printf("Got Attack perception for from actor %s(%s).",
                            attacker_ent->GetName(), ShowID(attackerEID));

                npc->TriggerEvent(&attack);
                break;
            }
            case psNPCCommandsMessage::PCPT_GROUPATTACK:
            {
                EID targetEID = EID(list.msg->GetUInt32());
                NPC *npc = npcclient->FindNPC(targetEID);

                int groupCount = list.msg->GetUInt8();
                csArray<gemNPCObject *> attacker_ents(groupCount);
                csArray<int> bestSkillSlots(groupCount);
                for (int i=0; i<groupCount; i++)
                {
                    attacker_ents.Push(npcclient->FindEntityID(EID(list.msg->GetUInt32())));
                    bestSkillSlots.Push(list.msg->GetInt8());
                    if(!attacker_ents.Top())
                    {
                        if(npc)
                        {
                            npc->Printf("Got group attack perception for unknown group member!",
                                        npc->GetActor()->GetName() );
                        }
                        
                        attacker_ents.Pop();
                        bestSkillSlots.Pop();
                    }
                }


                if (!npc)
                {
                    Debug2(LOG_NPC, targetEID.Unbox(), "Got group attack perception for unknown NPC(%s)!", ShowID(targetEID));
                    break;
                }


                if(attacker_ents.GetSize() == 0)
                {
                    npc->Printf("Got group attack perception and all group members are unknown!");
                    break;
                }

                GroupAttackPerception attack("groupattack",attacker_ents,bestSkillSlots);
                npc->Printf("Got Group Attack perception for recognising %i actors in the group.",
                            attacker_ents.GetSize());

                npc->TriggerEvent(&attack);
                break;
            }

            case psNPCCommandsMessage::PCPT_DMG:
            {
                EID attackerEID = EID(list.msg->GetUInt32());
                EID targetEID   = EID(list.msg->GetUInt32());
                float dmg       = list.msg->GetFloat();

                NPC *npc = npcclient->FindNPC(targetEID);
                if (!npc)
                {
                    Debug2(LOG_NPC, targetEID.Unbox(), "Attack on unknown NPC(%s).", ShowID(targetEID));
                    break;
                }
                gemNPCObject *attacker_ent = npcclient->FindEntityID(attackerEID);
                if (!attacker_ent)
                {
                    CPrintf(CON_ERROR, "%s got attack perception for unknown attacker! (%s)\n",
                            npc->GetName(), ShowID(attackerEID));
                    break;
                }

                DamagePerception damage("damage",attacker_ent,dmg);
                npc->Printf("Got Damage perception for from actor %s(%s) for %1.1f HP.",
                            attacker_ent->GetName(), ShowID(attackerEID), dmg);

                npc->TriggerEvent(&damage);
                break;
            }
            case psNPCCommandsMessage::PCPT_DEATH:
            {
                EID who = EID(list.msg->GetUInt32());
                NPC *npc = npcclient->FindNPC(who);
                if (!npc) // Not managed by us, or a player
                {
                    DeathPerception pcpt(who);
                    npcclient->TriggerEvent(&pcpt); // Broadcast
                    break;
                }
                npc->Printf("Got Death message");
                npcclient->HandleDeath(npc);
                break;
            }
            case psNPCCommandsMessage::PCPT_SPELL:
            {
                EID caster = EID(list.msg->GetUInt32());
                EID target = EID(list.msg->GetUInt32());
                uint32_t strhash = list.msg->GetUInt32();
                float    severity = list.msg->GetInt8() / 10;
                csString type = GetCommonString(strhash);

                gemNPCObject *caster_ent = npcclient->FindEntityID(caster);
                NPC *npc = npcclient->FindNPC(target);
                gemNPCObject *target_ent = (npc) ? npc->GetActor() : npcclient->FindEntityID(target);

                if (npc)
                {
                    npc->Printf("Got Spell Perception %s is casting %s on me.",
                                (caster_ent)?caster_ent->GetName():"(unknown entity)",type.GetDataSafe());
                }

                if (!caster_ent || !target_ent)
                    break;

                iSector *sector;
                csVector3 pos;
                psGameObject::GetPosition(target_ent, pos, sector);
                
                // This will result in spell:self, spell:target, spell:unknown perceptions
                SpellPerception pcpt("spell",caster_ent,target_ent,type,severity);

                npcclient->TriggerEvent(&pcpt, 30, &pos, sector, true); // Broadcast to same sector
                break;
            }
            case psNPCCommandsMessage::PCPT_ANYRANGEPLAYER:
            case psNPCCommandsMessage::PCPT_LONGRANGEPLAYER:
            case psNPCCommandsMessage::PCPT_SHORTRANGEPLAYER:
            case psNPCCommandsMessage::PCPT_VERYSHORTRANGEPLAYER:
            {
                EID npcEID    = EID(list.msg->GetUInt32());
                EID playerEID = EID(list.msg->GetUInt32());
                float faction = list.msg->GetFloat();

                NPC *npc = npcclient->FindNPC(npcEID);
                if (!npc)
                    break;  // This perception is not our problem

                npc->Printf("Range perception: NPC: %s, player: %s, faction: %.0f",
                            ShowID(npcEID), ShowID(playerEID), faction);

                gemNPCObject *npc_ent = (npc) ? npc->GetActor() : npcclient->FindEntityID(npcEID);
                gemNPCObject * player = npcclient->FindEntityID(playerEID);

                if (!player || !npc_ent)
                    break;


                csString pcpt_name;
                if ( npc->GetOwner() == player )
                {
                    pcpt_name.Append("owner ");
                }
                else
                {
                    pcpt_name.Append("player ");
                }
                if (cmd == psNPCCommandsMessage::PCPT_ANYRANGEPLAYER)
                    pcpt_name.Append("anyrange");
                if (cmd == psNPCCommandsMessage::PCPT_LONGRANGEPLAYER)
                    pcpt_name.Append("sensed");
                if (cmd == psNPCCommandsMessage::PCPT_SHORTRANGEPLAYER)
                    pcpt_name.Append("nearby");
                if (cmd == psNPCCommandsMessage::PCPT_VERYSHORTRANGEPLAYER) // PERSONAL_RANGE
                    pcpt_name.Append("adjacent");

                npc->Printf("Got Player %s in Range of %s with the %s Perception, with faction %d",
                            player->GetName(), npc_ent->GetName(), pcpt_name.GetData(), int(faction));

                FactionPerception pcpt(pcpt_name, int (faction), player);

                npc->TriggerEvent(&pcpt);
                break;
            }
            case psNPCCommandsMessage::PCPT_OWNER_CMD:
            {
                psPETCommandMessage::PetCommand_t command = (psPETCommandMessage::PetCommand_t)list.msg->GetUInt32();
                EID owner_id  = EID(list.msg->GetUInt32());
                EID pet_id    = EID(list.msg->GetUInt32());
                EID target_id = EID(list.msg->GetUInt32());

                gemNPCObject *owner = npcclient->FindEntityID(owner_id);
                NPC *npc = npcclient->FindNPC(pet_id);
                
                gemNPCObject *pet = (npc) ? npc->GetActor() : npcclient->FindEntityID( pet_id );

                gemNPCObject *target = npcclient->FindEntityID( target_id );

                if (npc)
                {
                    npc->Printf("Got OwnerCmd %d Perception from %s for %s with target %s",
                                command,(owner)?owner->GetName():"(unknown entity)",
                                (pet)?pet->GetName():"(unknown entity)",
                                (target)?target->GetName():"(none)");
                }

                if (!npc | !owner || !pet)
                    break;

                OwnerCmdPerception pcpt( "OwnerCmdPerception", command, owner, pet, target );

                npc->TriggerEvent(&pcpt);
                break;
            }
            case psNPCCommandsMessage::PCPT_OWNER_ACTION:
            {
                int action = list.msg->GetInt32();
                EID owner_id = EID(list.msg->GetUInt32());
                EID pet_id = EID(list.msg->GetUInt32());

                gemNPCObject *owner = npcclient->FindEntityID(owner_id);
                NPC *npc = npcclient->FindNPC(pet_id);
                gemNPCObject *pet = (npc) ? npc->GetActor() : npcclient->FindEntityID( pet_id );

                if (npc)
                {
                    npc->Printf("Got OwnerAction %d Perception from %s for %s",
                                action,(owner)?owner->GetName():"(unknown entity)",
                                (pet)?pet->GetName():"(unknown entity)");
                }

                if (!npc || !owner || !pet)
                    break;

                OwnerActionPerception pcpt( "OwnerActionPerception", action, owner, pet );

                npc->TriggerEvent(&pcpt);
                break;
            }
            case psNPCCommandsMessage::PCPT_PERCEPT:
            {
                EID npcEID          = EID(list.msg->GetUInt32());
                csString perception = list.msg->GetStr();
                csString type       = list.msg->GetStr();

                NPC *npc = npcclient->FindNPC(npcEID);
                if (npc)
                {
                    npc->Printf("%s got Perception: %s Type: %s ", ShowID(npcEID), perception.GetDataSafe(), type.GetDataSafe());


                    Perception pcpt( perception, type );
                    npc->TriggerEvent(&pcpt);
                }
                break;
            }
            case psNPCCommandsMessage::PCPT_INVENTORY:
            {
                EID owner_id = EID(msg->GetUInt32());
                csString item_name = msg->GetStr();
                bool inserted = msg->GetBool();
                int count = msg->GetInt16();

                gemNPCObject *owner = npcclient->FindEntityID(owner_id);
                NPC *npc = npcclient->FindNPC(owner_id);

                if (!owner || !npc)
                    break;

                npc->Printf("Got Inventory %s Perception from %s for %d %s\n",
                            (inserted?"Add":"Remove"),owner->GetName(),
                            count,item_name.GetData());
                
                iSector *sector;
                csVector3 pos;
                psGameObject::GetPosition(owner, pos, sector);

                /* TODO: Create a inventory for each NPC.
                if (inserted)
                {
                    npc->InventoryAdd(item_name);
                }
                else
                {
                    npc->InventoryRemove(item_name);
                }
                */
                csString str;
                str.Format("inventory:%s",(inserted?"added":"removed"));

                InventoryPerception pcpt( str, item_name, count, pos, sector, 5.0 );

                npc->TriggerEvent(&pcpt);

                // Hack: To get inventory to tribe. Need some more general way of
                //       delivery of perceptions to tribes....
                if (npc->GetTribe())
                {
                    npc->GetTribe()->HandlePerception(npc,&pcpt);
                }
                // ... end of hack.
                break;
            }

            case psNPCCommandsMessage::PCPT_FAILED_TO_ATTACK:
            {
                EID npcEID = EID(msg->GetUInt32());
                EID targetEID = EID(msg->GetUInt32());

                NPC *npc = npcclient->FindNPC( npcEID );
                gemNPCObject *target = npcclient->FindEntityID( targetEID );

                if (!npc || !target)
                    break;

                npc->Printf("Got Failed to Attack perception");

                Perception failedToAttack("failed to attack");
                npc->TriggerEvent(&failedToAttack);

                break;
            }

            case psNPCCommandsMessage::PCPT_FLAG:
            {
                EID owner_id = EID(msg->GetUInt32());
                uint32_t flags = msg->GetUInt32();

                gemNPCObject * obj = npcclient->FindEntityID(owner_id);

                if (!obj)
                    break;

                obj->SetInvisible((flags & psNPCCommandsMessage::INVISIBLE) ? true : false);
                obj->SetInvincible((flags & psNPCCommandsMessage::INVINCIBLE) ? true : false);
                obj->SetAlive((flags & psNPCCommandsMessage::IS_ALIVE));

                break;
            }

            case psNPCCommandsMessage::PCPT_NPCCMD:
            {
                EID owner_id = EID(msg->GetUInt32());
                csString cmd   = msg->GetStr();

                NPC *npc = npcclient->FindNPC(owner_id);

                if (!npc)
                    break;

                NPCCmdPerception pcpt( cmd, npc );

                npcclient->TriggerEvent(&pcpt); // Broadcast

                break;
            }
            
            case psNPCCommandsMessage::PCPT_TRANSFER:
            {
                EID entity_id = EID(msg->GetUInt32());
                csString item = msg->GetStr();
                int count = msg->GetInt8();
                csString target = msg->GetStr();

                NPC *npc = npcclient->FindNPC(entity_id);

                if (!npc)
                    break;

                npc->Printf("Got Transfer Perception from %s for %d %s to %s\n",
                            npc->GetName(),count,
                            item.GetDataSafe(),target.GetDataSafe());

                iSector *sector;
                csVector3 pos;
                psGameObject::GetPosition(npc->GetActor(), pos, sector);

                InventoryPerception pcpt( "transfer", item, count, pos, sector, 5.0 );

                if (target == "tribe" && npc->GetTribe())
                {
                    npc->GetTribe()->HandlePerception(npc,&pcpt);
                }
                break;
            }
            case psNPCCommandsMessage::PCPT_SPAWNED:
            {
                PID spawned_pid = PID(msg->GetUInt32());
                EID spawned_eid = EID(msg->GetUInt32());
                EID spawner_eid = EID(msg->GetUInt32());
                csString tribeMemberType = msg->GetStr();


                NPC *npc = npcclient->FindNPC(spawner_eid);

                if (!npc)
                    break;

                npc->Printf("Got spawn Perception to %u from %u\n",
                            spawned_eid.Unbox(), spawner_eid.Unbox());
                npc->GetTribe()->AddMember(spawned_pid, tribeMemberType);
                NPC *spawned_npc = npcclient->FindNPC(spawned_eid);
                if(spawned_npc)
                {
                   npcclient->CheckAttachTribes(spawned_npc);
                }
                break;
            }
            case psNPCCommandsMessage::PCPT_TELEPORT:
            {
                EID npc_eid = EID(msg->GetUInt32());
                csVector3 pos = msg->GetVector3();
                float yrot = msg->GetFloat();
                iSector* sector = msg->GetSector( 0, GetMsgStrings(), engine );
                InstanceID instance = msg->GetUInt32();

                NPC *npc = npcclient->FindNPC(npc_eid);

                if (!npc)
                    break;

                npc->Printf("Got teleport perception to %s\n",toString(pos,sector).GetDataSafe());

                PositionPerception pcpt("teleported",NULL,instance,sector,pos,yrot,0.0);
                npc->TriggerEvent(&pcpt);
                break;
            }
           
            case psNPCCommandsMessage::PCPT_INFO_REQUEST:
            {
                EID npc_eid = EID(msg->GetUInt32());
                uint32_t clientNum = msg->GetUInt32();
                csString infoRequestSubCmd = msg->GetStr();

                NPC *npc = npcclient->FindNPC(npc_eid);

                if (!npc)
                    break;

                npc->Printf("Got info request.");

                csString reply("NPCClient: ");
                reply.Append(npc->Info());

                QueueInfoReplyCommand(clientNum,reply);

                break;

            }

            case psNPCCommandsMessage::PCPT_CHANGE_BRAIN:
            {
                EID npc_eid = EID(msg->GetUInt32());
                uint32_t clientNum = msg->GetUInt32();
                csString brainType = msg->GetStr();

                NPC *npc = npcclient->FindNPC(npc_eid);

                if (!npc)
                    break;

                npc->Printf("Got change brain request to %s.", brainType.GetDataSafe());

                //search for the requested npc type

                NPCType* type = npcclient->FindNPCType(brainType.GetDataSafe());
                if (!type)
                {
                    Error2("NPC type '%s' is not found",(const char *)brainType);
                    break;
                }

                //if found set it
                npc->SetBrain(type, npcclient->GetEventManager());
                break;
            }

            default:
            {
                CPrintf(CON_ERROR,"************************\nUnknown npc cmd: %d\n*************************\n",cmd);
                abort();
            }
        }
        cmd = list.msg->GetInt8();
    }
}

void NetworkManager::HandleDisconnect(MsgEntry *msg)
{
    psDisconnectMessage disconnect(msg);

    // Special case to drop login failure reply from server immediately after we have already logged in.
    if (connected && disconnect.msgReason.CompareNoCase("Already Logged In."))
        return;

    CPrintf(CON_WARNING, "Disconnected: %s\n",disconnect.msgReason.GetData());

    // Reconnect is disabled right now because the CEL entity registry is not flushed upon
    // reconnect which will cause BIG problems.
    Disconnect();
    CPrintf(CON_WARNING, "Reconnect disabled...\n");
    exit(-1);
    if(!reconnect && false)
    {
        connected = ready = false;

        // reconnect
        connection->DisConnect();
        npcclient->RemoveAll();

        reconnect = true;
        // 60 secs to allow any connections to go linkdead.
        psNPCReconnect *recon = new psNPCReconnect(60000, this, false);
        npcclient->GetEventManager()->Push(recon);
    }
}

void NetworkManager::HandleNPCWorkDone(MsgEntry *me)
{
    psNPCWorkDoneMessage msg(me);

    NPC* npc = npcclient->FindNPC(msg.npcEID);

    if (!npc)
    {
        Error2("Could not find npc(%s) for Got work done.", ShowID(msg.npcEID));
        return;
    }

    // If he just prospected a mine and its a tribe member
    if(npc->GetTribe())
    {
        npc->GetTribe()->ProspectMine(npc,msg.resource,msg.nick);
    }
}

void NetworkManager::HandleNewNpc(MsgEntry *me)
{
    psNewNPCCreatedMessage msg(me);

    NPC *npc = npcclient->FindNPCByPID(msg.master_id);
    if (npc)
    {
        npc->Printf("Got new NPC notification for %s", ShowID(msg.new_npc_id));
        
        // Insert a row in the db for this guy next.  
        // We will get an entity msg in a second to make him come alive.
        npc->InsertCopy(msg.new_npc_id,msg.owner_id);

        // Now requery so we have the new guy on our list when we get the entity msg.
        if (!npcclient->ReadSingleNPC(msg.new_npc_id))
        {
            Error3("Error creating copy of master %s as id %s.", ShowID(msg.master_id), ShowID(msg.new_npc_id));
            return;
        }
    }
    else
    {
        // Ignore it here.  We don't manage the master.
    }
}

void NetworkManager::PrepareCommandMessage()
{
    outbound = new psNPCCommandsMessage(0,30000);
    cmd_count = 0;
}

void NetworkManager::QueueDRData(NPC * npc )
{
    // When a NPC is dead, this may still be called by Behavior::Interrupt
    if(!npc->IsAlive())
        return;
    cmd_dr_outbound.PutUnique(npc->GetPID(), npc);
}

void NetworkManager::DequeueDRData(NPC * npc )
{
    npc->Printf(15, "Dequeuing DR Data...");
    cmd_dr_outbound.DeleteAll(npc->GetPID());
}

void NetworkManager::CheckCommandsOverrun(size_t neededSize)
{
	if (outbound->msg->current + neededSize > outbound->msg->bytes->GetSize())
		SendAllCommands();
}

void NetworkManager::QueueDRDataCommand(gemNPCActor *entity, psLinearMovement *linmove, uint8_t counter)
{
    if(!entity) return; //TODO: This shouldn't happen but it happens so this is just a quick patch and
                        //      the real problem should be fixed (entities being removed from the game
                        //      world while moving will end up here with entity being null)
                        //      DequeueDRData should be supposed to remove all queued data but the iterator
                        //      in SendAllCommands still "catches them"

    CheckCommandsOverrun(100);
   
    psDRMessage drmsg(0,entity->GetEID(),counter,connection->GetAccessPointers(),linmove);

    outbound->msg->Add( (int8_t) psNPCCommandsMessage::CMD_DRDATA);
    outbound->msg->Add( drmsg.msg->bytes->payload,(uint32_t)drmsg.msg->bytes->GetTotalSize() );

    if ( outbound->msg->overrun )
    {
        CS_ASSERT(!"NetworkManager::QueueDRData put message in overrun state!\n");
    }
    cmd_count++;
}

void NetworkManager::QueueAttackCommand(gemNPCActor *attacker, gemNPCActor *target, const char* stance)
{

    CheckCommandsOverrun(100);

    outbound->msg->Add( (int8_t) psNPCCommandsMessage::CMD_ATTACK);
    outbound->msg->Add(attacker->GetEID().Unbox());
    
    if (target)
    {
        outbound->msg->Add(target->GetEID().Unbox());
    }
    else
    {
        outbound->msg->Add( (uint32_t) 0 );  // 0 target means stop attack
    }

    outbound->msg->Add( GetCommonStringID(stance) );

    if ( outbound->msg->overrun )
    {
        CS_ASSERT(!"NetworkManager::QueueAttackCommand put message in overrun state!\n");
    }

    cmd_count++;
}

void NetworkManager::QueueSitCommand(gemNPCActor *npc, gemNPCObject* target, bool sit)
{
    CheckCommandsOverrun(100);

    outbound->msg->Add( (int8_t) psNPCCommandsMessage::CMD_SIT);
    outbound->msg->Add( npc->GetEID().Unbox() );
    if (target)
    {
        outbound->msg->Add( target->GetEID().Unbox() );
    }
    else
    {
        outbound->msg->Add( (uint32_t)0 );
    }
    outbound->msg->Add( sit );

    if ( outbound->msg->overrun )
    {
        CS_ASSERT(!"NetworkManager::QueueSitCommand put message in overrun state!\n");
    }
    cmd_count++;
}

void NetworkManager::QueueSpawnCommand(gemNPCActor *mother, gemNPCActor *father, const csString& tribeMemberType)
{
    CheckCommandsOverrun(100);

    outbound->msg->Add( (int8_t) psNPCCommandsMessage::CMD_SPAWN);
    outbound->msg->Add(mother->GetEID().Unbox());
    outbound->msg->Add(father->GetEID().Unbox());
    outbound->msg->Add(tribeMemberType);

    if ( outbound->msg->overrun )
    {
        CS_ASSERT(!"NetworkManager::QueueSpawnCommand put message in overrun state!\n");
    }
    cmd_count++;
}

void NetworkManager::QueueSpawnBuildingCommand(gemNPCActor *spawner, csVector3 where, iSector* sector, const char* buildingName, int tribeID)
{
    CheckCommandsOverrun(100);

    outbound->msg->Add((int8_t) psNPCCommandsMessage::CMD_SPAWN_BUILDING);
    outbound->msg->Add(spawner->GetEID().Unbox());
    outbound->msg->Add(where);
    outbound->msg->Add(sector->QueryObject()->GetName());
    outbound->msg->Add(buildingName);

    outbound->msg->Add(tribeID);

    if(outbound->msg->overrun)
    {
        CS_ASSERT(!"NetworkManager::QueueSpawnBuildingCommand put message in overrun state!\n");
    }
    cmd_count++;
}

void NetworkManager::QueueTalkCommand(gemNPCActor *speaker, gemNPCActor* target, psNPCCommandsMessage::PerceptionTalkType talkType, bool publicTalk, const char* text)
{
    CheckCommandsOverrun(100);

    outbound->msg->Add( (int8_t) psNPCCommandsMessage::CMD_TALK );
    outbound->msg->Add( speaker->GetEID().Unbox() );
    if (target)
    {
        outbound->msg->Add( target->GetEID().Unbox() );
    }
    else
    {
        outbound->msg->Add( (uint32_t)0 );
    }
    outbound->msg->Add( (uint32_t) talkType );
    outbound->msg->Add( publicTalk );
    outbound->msg->Add(text);

    if ( outbound->msg->overrun )
    {
        CS_ASSERT(!"NetworkManager::QueueTalkCommand put message in overrun state!\n");
    }

    cmd_count++;
}

void NetworkManager::QueueVisibilityCommand(gemNPCActor *entity, bool status)
{
    CheckCommandsOverrun(100);

    outbound->msg->Add( (int8_t) psNPCCommandsMessage::CMD_VISIBILITY);
    outbound->msg->Add(entity->GetEID().Unbox());
    
    outbound->msg->Add(status);
    if ( outbound->msg->overrun )
    {
        CS_ASSERT(!"NetworkManager::QueueVisibleCommand put message in overrun state!\n");
    }

    cmd_count++;
}

void NetworkManager::QueuePickupCommand(gemNPCActor *entity, gemNPCObject *item, int count)
{
    CheckCommandsOverrun(100);

    outbound->msg->Add( (int8_t) psNPCCommandsMessage::CMD_PICKUP);
    outbound->msg->Add(entity->GetEID().Unbox());
    outbound->msg->Add(item->GetEID().Unbox());
    outbound->msg->Add( (int16_t) count );

    if ( outbound->msg->overrun )
    {
        CS_ASSERT(!"NetworkManager::QueuePickupCommand put message in overrun state!\n");
    }

    cmd_count++;
}

void NetworkManager::QueueEmoteCommand(gemNPCActor *npc, gemNPCObject* target,  const csString& cmd)
{
    CheckCommandsOverrun(100);

    outbound->msg->Add( (int8_t) psNPCCommandsMessage::CMD_EMOTE);
    outbound->msg->Add( npc->GetEID().Unbox() );
    if (target)
    {
        outbound->msg->Add( target->GetEID().Unbox() );
    }
    else
    {
        outbound->msg->Add( (uint32_t)0 );
    }
    outbound->msg->Add( cmd );

    if ( outbound->msg->overrun )
    {
        CS_ASSERT(!"NetworkManager::QueueEmoteCommand put message in overrun state!\n");
    }

    cmd_count++;
}


void NetworkManager::QueueEquipCommand(gemNPCActor *entity, csString item, csString slot, int count)
{
    CheckCommandsOverrun(100);

    outbound->msg->Add( (int8_t) psNPCCommandsMessage::CMD_EQUIP);
    outbound->msg->Add(entity->GetEID().Unbox());
    outbound->msg->Add( item );
    outbound->msg->Add( slot );
    outbound->msg->Add( (int16_t) count );

    if ( outbound->msg->overrun )
    {
        CS_ASSERT(!"NetworkManager::QueueEquipCommand put message in overrun state!\n");
    }

    cmd_count++;
}

void NetworkManager::QueueDequipCommand(gemNPCActor *entity, csString slot)
{
    CheckCommandsOverrun(100);

    outbound->msg->Add( (int8_t) psNPCCommandsMessage::CMD_DEQUIP);
    outbound->msg->Add(entity->GetEID().Unbox());
    outbound->msg->Add( slot );

    if ( outbound->msg->overrun )
    {
        CS_ASSERT(!"NetworkManager::QueueDequipCommand put message in overrun state!\n");
    }

    cmd_count++;
}

void NetworkManager::QueueWorkCommand(gemNPCActor *entity, const csString& type, const csString& resource)
{
    CheckCommandsOverrun( sizeof(uint8_t) + sizeof(uint32_t) + (type.Length()+1) + (resource.Length()+1));

    outbound->msg->Add( (int8_t) psNPCCommandsMessage::CMD_WORK);
    outbound->msg->Add(entity->GetEID().Unbox());
    outbound->msg->Add( type );
    outbound->msg->Add( resource );

    if ( outbound->msg->overrun )
    {
        CS_ASSERT(!"NetworkManager::QueueWorkCommand put message in overrun state!\n");
    }

    cmd_count++;
}

void NetworkManager::QueueTransferCommand(gemNPCActor *entity, csString item, int count, csString target)
{
    CheckCommandsOverrun(100);

    outbound->msg->Add( (int8_t) psNPCCommandsMessage::CMD_TRANSFER);
    outbound->msg->Add(entity->GetEID().Unbox());
    outbound->msg->Add( item );
    outbound->msg->Add( (int8_t)count );
    outbound->msg->Add( target );

    if ( outbound->msg->overrun )
    {
        CS_ASSERT(!"NetworkManager::QueueTransferCommand put message in overrun state!\n");
    }

    cmd_count++;
}

void NetworkManager::QueueDropCommand(gemNPCActor *entity, csString slot)
{
    CheckCommandsOverrun(100);

    outbound->msg->Add( (int8_t) psNPCCommandsMessage::CMD_DROP);
    outbound->msg->Add(entity->GetEID().Unbox());
    outbound->msg->Add( slot );

    if ( outbound->msg->overrun )
    {
        CS_ASSERT(!"NetworkManager::QueueDropCommand put message in overrun state!\n");
    }

    cmd_count++;
}


void NetworkManager::QueueResurrectCommand(csVector3 where, float rot, iSector* sector, PID character_id)
{
    CheckCommandsOverrun(100);

    outbound->msg->Add( (int8_t) psNPCCommandsMessage::CMD_RESURRECT );
    outbound->msg->Add( character_id.Unbox() );
    outbound->msg->Add( (float)rot );
    outbound->msg->Add( where );
    outbound->msg->Add( sector, 0, GetMsgStrings() );

    if ( outbound->msg->overrun )
    {
        CS_ASSERT(!"NetworkManager::QueueResurrectCommand put message in overrun state!\n");
    }

    cmd_count++;
}

void NetworkManager::QueueSequenceCommand(csString name, int cmd, int count)
{
    CheckCommandsOverrun(100);

    outbound->msg->Add( (int8_t) psNPCCommandsMessage::CMD_SEQUENCE);
    outbound->msg->Add( name );
    outbound->msg->Add( (int8_t) cmd );
    outbound->msg->Add( (int32_t) count );

    if ( outbound->msg->overrun )
    {
        CS_ASSERT(!"NetworkManager::QueueSequenceCommand put message in overrun state!\n");
    }

    cmd_count++;
}

void NetworkManager::QueueImperviousCommand(gemNPCActor * entity, bool impervious)
{
    CheckCommandsOverrun(100);

    outbound->msg->Add( (int8_t) psNPCCommandsMessage::CMD_IMPERVIOUS);
    outbound->msg->Add(entity->GetEID().Unbox());
    outbound->msg->Add( (bool) impervious );

    if ( outbound->msg->overrun )
    {
        CS_ASSERT(!"NetworkManager::QueueImperviousCommand put message in overrun state!\n");
    }

    cmd_count++;
}

void NetworkManager::QueueInfoReplyCommand(uint32_t clientNum,const char* reply)
{
    CheckCommandsOverrun(sizeof(int8_t)+sizeof(uint32_t)+(strlen(reply)+1));

    outbound->msg->Add( (int8_t) psNPCCommandsMessage::CMD_INFO_REPLY);
    outbound->msg->Add( clientNum );
    outbound->msg->Add( reply );

    if ( outbound->msg->overrun )
    {
        CS_ASSERT(!"NetworkManager::QueueInfoReplyCommand put message in overrun state!\n");
    }

    cmd_count++;
}

void NetworkManager::QueueAssessCommand(gemNPCActor* entity, gemNPCObject* target, const csString& physicalAssessmentPerception,
                                            const csString& magicalAssessmentPerception,  const csString& overallAssessmentPerception)
{
    CheckCommandsOverrun(sizeof(int8_t)+2*sizeof(uint32_t)+(physicalAssessmentPerception.Length()+1)+
                         (magicalAssessmentPerception.Length()+1)+(overallAssessmentPerception.Length()+1));

    outbound->msg->Add( (int8_t) psNPCCommandsMessage::CMD_ASSESS);
    outbound->msg->Add( entity->GetEID().Unbox() );
    outbound->msg->Add( target->GetEID().Unbox() );
    outbound->msg->Add( physicalAssessmentPerception );
    outbound->msg->Add( magicalAssessmentPerception );
    outbound->msg->Add( overallAssessmentPerception );

    if ( outbound->msg->overrun )
    {
        CS_ASSERT(!"NetworkManager::QueueAssessCommand put message in overrun state!\n");
    }

    cmd_count++;    
}

void NetworkManager::QueueCastCommand(gemNPCActor* entity, gemNPCObject* target, const csString& spell, float kFactor)
{
    CheckCommandsOverrun(sizeof(int8_t)+2*sizeof(uint32_t)+(spell.Length()+1)+sizeof(float));

    outbound->msg->Add( (int8_t) psNPCCommandsMessage::CMD_CAST);
    outbound->msg->Add( entity->GetEID().Unbox() );
    outbound->msg->Add( target->GetEID().Unbox() );
    outbound->msg->Add( spell );
    outbound->msg->Add( kFactor );

    if ( outbound->msg->overrun )
    {
        CS_ASSERT(!"NetworkManager::QueueCastCommand put message in overrun state!\n");
    }

    cmd_count++;    
}


void NetworkManager::SendAllCommands(bool final)
{
    // If this is the final send all for a tick we need to check if any NPCs has been queued for sending of DR data.
    if (final)
    {
        csHash<NPC*,PID>::GlobalIterator it(cmd_dr_outbound.GetIterator());
        while ( it.HasNext() )
        {
            NPC * npc = it.Next();
            if (npc->GetActor())
            {
                if (npc->GetLinMove()->GetSector())
                {
                    QueueDRDataCommand(npc->GetActor(),npc->GetLinMove(),npc->GetDRCounter());
                }
                else
                {
                    CPrintf(CON_WARNING,"NPC %s is trying to send DR data with no sector\n",npc->GetName());
                    npc->Disable(); // Disable the NPC.
                }
                
            }
            else
            {
                //TODO: probably here the npc should be destroyed?
                CPrintf(CON_WARNING,"NPC %s is trying to send DR data with no actor\n",npc->GetName());
                npc->Disable(); // Disable the NPC.
            }
        }
        cmd_dr_outbound.DeleteAll();
    }
    
    if (cmd_count)
    {
        outbound->msg->Add( (int8_t) psNPCCommandsMessage::CMD_TERMINATOR);
        outbound->msg->ClipToCurrentSize();

        msghandler->SendMessage(outbound->msg);

        delete outbound;
        PrepareCommandMessage();
    }
}


void NetworkManager::ReAuthenticate()
{
    Authenticate(host,port,user,password);
}

void NetworkManager::ReConnect()
{
    if (!connection->Connect(host,port))
    {
        CPrintf(CON_ERROR, "Couldn't resolve hostname %s on port %d.\n",(const char *)host,port);
        return;
    }
    // 2 seconds to allow linkdead messages to be processed
    psNPCReconnect *recon = new psNPCReconnect(2000, this, true);
    npcclient->GetEventManager()->Push(recon);
}

void NetworkManager::SendConsoleCommand(const char *cmd)
{
    psServerCommandMessage msg(0,cmd);
    msg.SendMessage();
}

/*------------------------------------------------------------------*/

psNPCReconnect::psNPCReconnect(int offsetticks, NetworkManager *mgr, bool authent)
: psGameEvent(0,offsetticks,"psNPCReconnect"), networkMgr(mgr), authent(authent)
{

}

void psNPCReconnect::Trigger()
{
    if(!running)
        return;
    if(!authent)
    {
        networkMgr->ReConnect();
        return;
    }
    if(!networkMgr->IsReady())
    {
        CPrintf(CON_WARNING, "Reconnecting...\n");
        networkMgr->ReAuthenticate();
    }
    networkMgr->reconnect = false;
}
