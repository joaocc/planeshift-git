/*
* tutorialmanager.cpp
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
#include <string.h>
#include <memory.h>

//=============================================================================
// Crystal Space Includes
//=============================================================================
#include <csutil/xmltiny.h>
#include <physicallayer/entity.h>
#include <propclass/mesh.h>

//=============================================================================
// Project Includes
//=============================================================================
#include "util/eventmanager.h"
#include "util/psdatabase.h"

#include "bulkobjects/pscharacter.h"
#include "bulkobjects/pscharacterloader.h"

//=============================================================================
// Local Includes
//=============================================================================
#include "tutorialmanager.h"
#include "client.h"
#include "clients.h"
#include "events.h"
#include "gem.h"
#include "netmanager.h"
#include "globals.h"


TutorialManager::TutorialManager(ClientConnectionSet *pCCS)
{
    clients = pCCS;
    psserver->GetEventManager()->Subscribe(this,MSGTYPE_CONNECT_EVENT, REQUIRE_ANY_CLIENT);
    psserver->GetEventManager()->Subscribe(this,MSGTYPE_MOVEMENT_EVENT,REQUIRE_ANY_CLIENT);
    psserver->GetEventManager()->Subscribe(this,MSGTYPE_TARGET_EVENT,  NO_VALIDATION);
    psserver->GetEventManager()->Subscribe(this,MSGTYPE_GENERIC_EVENT, REQUIRE_ANY_CLIENT);
    psserver->GetEventManager()->Subscribe(this,MSGTYPE_DAMAGE_EVENT,  NO_VALIDATION);
    psserver->GetEventManager()->Subscribe(this,MSGTYPE_DEATH_EVENT,  NO_VALIDATION);
    LoadTutorialStrings();
}

TutorialManager::~TutorialManager()
{
    psserver->GetEventManager()->Unsubscribe(this,MSGTYPE_CONNECT_EVENT);
    psserver->GetEventManager()->Unsubscribe(this,MSGTYPE_MOVEMENT_EVENT);
    psserver->GetEventManager()->Unsubscribe(this,MSGTYPE_TARGET_EVENT);
    psserver->GetEventManager()->Unsubscribe(this,MSGTYPE_GENERIC_EVENT);
    psserver->GetEventManager()->Unsubscribe(this,MSGTYPE_DAMAGE_EVENT);
    psserver->GetEventManager()->Unsubscribe(this,MSGTYPE_DEATH_EVENT);
}

bool TutorialManager::LoadTutorialStrings()
{
    Result result(db->Select("select * from tips where id>1000 and id<1033"));
    if (!result.IsValid() )
    {
        Error2("Could not load tutorial strings due to database error: %s\n",
               db->GetLastError());
        return false;
    }

    for ( unsigned long i=0; i < result.Count(); i++ )
    {
        int id = result[i].GetInt("id") - 1001;
        tutorialMsg[id] = result[i]["tip"];
    }
    return true;
}

void TutorialManager::SendTutorialMessage(int which,Client *client,const char *instrs)
{
//    printf("Send tutorial msg %d\n",which);
    if (!instrs)
    {
        csString error;
        error.Format("TutorialEventMessage %d is missing instructions.  Please file a bug.", which);
        instrs = error;
    }
    psTutorialMessage msg(client->GetClientNum(),which,instrs);
    msg.SendMessage();
}

void TutorialManager::HandleMessage(MsgEntry *pMsg,Client *client)
{
    switch (pMsg->GetType())
    {
        case MSGTYPE_CONNECT_EVENT:   HandleConnect(pMsg,client);   break;
        case MSGTYPE_MOVEMENT_EVENT:  HandleMovement(pMsg,client);  break;
        case MSGTYPE_TARGET_EVENT:    HandleTarget(pMsg,client);    break;
        case MSGTYPE_DAMAGE_EVENT:    HandleDamage(pMsg,client);    break;
        case MSGTYPE_DEATH_EVENT:     HandleDeath(pMsg,client);     break;
        case MSGTYPE_GENERIC_EVENT:   HandleGeneric(pMsg,client);   break;
        default: break;
    }
}

void TutorialManager::HandleConnect(MsgEntry *pMsg,Client *client)
{
//    printf("Got psConnectEvent\n");
    psConnectEvent evt(pMsg);
    if (client)
    {
        psCharacter *ch = client->GetCharacterData();
        if (ch->NeedsHelpEvent(CONNECT))
        {
            SendTutorialMessage(CONNECT,client,tutorialMsg[CONNECT]);
            ch->CompleteHelpEvent(CONNECT);
        }
    }
}

void TutorialManager::HandleMovement(MsgEntry *pMsg,Client *client)
{
    // printf("Got psMovementEvent\n");
    psMovementEvent evt(pMsg);
    if (client)
    {
        psCharacter *ch = client->GetCharacterData();
        if (ch->NeedsHelpEvent(MOVEMENT))
        {
            SendTutorialMessage(MOVEMENT,client,tutorialMsg[MOVEMENT]);
            ch->CompleteHelpEvent(MOVEMENT);
        }
    }
}

// psTargetEvent already published so intercepting this takes zero code
void TutorialManager::HandleTarget(MsgEntry *pMsg,Client *client)
{
    // printf("Got psTargetEvent\n");
    psTargetChangeEvent evt(pMsg);
    if (evt.character)
    {
        client = evt.character->GetClient();
        psCharacter *ch = evt.character->GetCharacterData();
        if (ch->NeedsHelpEvent(NPC_SELECT))
        {
            SendTutorialMessage(NPC_SELECT,client,tutorialMsg[NPC_SELECT]);
            ch->CompleteHelpEvent(NPC_SELECT);
        }

        if (ch->NeedsHelpEvent(NPC_TALK))
        {
            SendTutorialMessage(NPC_TALK,client,tutorialMsg[NPC_TALK]);
            ch->CompleteHelpEvent(NPC_TALK);
        }
    }
}

/// Specifically handle the Damage event in the tutorial
void TutorialManager::HandleDamage(MsgEntry *pMsg,Client *client)
{
    //printf("Got psDamageEvent\n");
    psDamageEvent evt(pMsg);
    if (evt.target && evt.attacker)  // someone hurt us
    {
        client = evt.target->GetClient();
        if (client)
        {
            psCharacter *ch = evt.target->GetCharacterData();
            if (ch->NeedsHelpEvent(DAMAGE_SELF))
            {
                SendTutorialMessage(DAMAGE_SELF,client,tutorialMsg[DAMAGE_SELF]);
                ch->CompleteHelpEvent(DAMAGE_SELF);
            }
        }
    }
    else if (evt.target && !evt.attacker) // no one hurt us, so fall damage
    {
        client = evt.target->GetClient();
        if (client)
        {
            psCharacter *ch = evt.target->GetCharacterData();
            if (ch->NeedsHelpEvent(DAMAGE_FALL))
            {
                SendTutorialMessage(DAMAGE_FALL,client,tutorialMsg[DAMAGE_FALL]);
                ch->CompleteHelpEvent(DAMAGE_FALL);
            }
        }
    }
}

/// Specifically handle the Damage event in the tutorial
void TutorialManager::HandleDeath(MsgEntry *pMsg,Client *client)
{
    //printf("Got psDeathEvent\n");
    psDeathEvent evt(pMsg);
    if (evt.deadActor)  // We're dead
    {
        client = evt.deadActor->GetClient();
        if (client)
        {
            psCharacter *ch = evt.deadActor->GetCharacterData();
            if (ch->NeedsHelpEvent(DEATH_SELF))
            {
                SendTutorialMessage(DEATH_SELF,client,tutorialMsg[DEATH_SELF]);
                ch->CompleteHelpEvent(DEATH_SELF);
            }
        }
    }
}

void TutorialManager::HandleGeneric(MsgEntry *pMsg,Client *client)
{
    // printf("Got psGenericEvent\n");
    psGenericEvent evt(pMsg);
    if (evt.client_id)
    {
        switch (evt.eventType)
        {
            case psGenericEvent::QUEST_ASSIGN:
            {
                psCharacter *ch = client->GetCharacterData();
                if (ch->NeedsHelpEvent(QUEST_ASSIGN))
                {
                    SendTutorialMessage(QUEST_ASSIGN,client,tutorialMsg[QUEST_ASSIGN]);
                    ch->CompleteHelpEvent(QUEST_ASSIGN);
                }
                break;
            }
            default:
                // printf("Unknown generic event received in Tutorial Manager.\n");
                break;
        }
    }
}
