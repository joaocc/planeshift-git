/*
 * serversongmngr.cpp, Author: Andrea Rizzi <88whacko@gmail.com>
 *
 * Copyright (C) 2001-2011 Atomic Blue (info@planeshift.it, http://www.atomicblue.org) 
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
#include "serversongmngr.h"


//====================================================================================
// Project Includes
//====================================================================================
#include <net/messages.h>
#include <util/music.h>

//====================================================================================
// Local Includes
//====================================================================================
#include "globals.h"
#include "client.h"
#include "gem.h"
#include "psproxlist.h"

#include "bulkobjects/pscharacter.h"
#include "bulkobjects/psmerchantinfo.h"


psEndSongEvent::psEndSongEvent(gemActor* actor, int songLength)
    : psGameEvent(0, songLength, "psEndSongEvent")
{
    charActor = actor;
}

psEndSongEvent::~psEndSongEvent()
{
    // obviously charActor must not be deleted :P
}

bool psEndSongEvent::CheckTrigger()
{
    return charActor->GetMode() == PSCHARACTER_MODE_PLAY;
}

void psEndSongEvent::Trigger()
{
    ServerSongManager::GetSingleton().OnStopSong(charActor, true);
}


//----------------------------------------------------------------------------


ServerSongManager::ServerSongManager()
{
    isProcessedSongEnded = false;

    Subscribe(&ServerSongManager::HandlePlaySongMessage, MSGTYPE_MUSICAL_SHEET, REQUIRE_READY_CLIENT);
    Subscribe(&ServerSongManager::HandleStopSongMessage, MSGTYPE_STOP_SONG, REQUIRE_READY_CLIENT);

    calcSongPar  = psserver->GetMathScriptEngine()->FindScript("Calculate Song Parameters");
    calcSongExp  = psserver->GetMathScriptEngine()->FindScript("Calculate Song Experience");

    CS_ASSERT_MSG("Could not load mathscript 'Calculate Song Parameters'", calcSongPar);
    CS_ASSERT_MSG("Could not load mathscript 'Calculate Song Experience'", calcSongExp);
}

ServerSongManager::~ServerSongManager()
{
    Unsubscribe(MSGTYPE_MUSICAL_SHEET);
    Unsubscribe(MSGTYPE_STOP_SONG);
}

bool ServerSongManager::Initialize()
{
    csString instrCatStr;

    if(psserver->GetServerOption("instruments_category", instrCatStr))
    {
        instrumentsCategory = atoi(instrCatStr.GetData());
        return true;
    }
    else
    {
        return false;
    }
}

void ServerSongManager::HandlePlaySongMessage(MsgEntry* me, Client* client)
{
    psMusicalSheetMessage musicMsg(me);

    if(musicMsg.valid && musicMsg.play)
    {
        psItem *item = client->GetCharacterData()->Inventory().FindItemID(musicMsg.itemID);

        // playing
        if(item != 0)
        {
            uint32 actorEID;
            int songLength;
            float errorRate;
            psItem* instrItem;
            const char* instrName;

            MathEnvironment mathEnv;
            csArray<PublishDestination> proxList;

            psCharacter* charData = client->GetCharacterData();
            gemActor* charActor = charData->GetActor();

            // checking if the client's player is already playing something
            if(charActor->GetMode() == PSCHARACTER_MODE_PLAY)
            {
                return;
            }

            // checking if the score is valid
            csRef<iDocumentSystem> docSys = csQueryRegistry<iDocumentSystem>(psserver->GetObjectReg());;
            csRef<iDocument> scoreDoc = docSys->CreateDocument();
            scoreDoc->Parse(musicMsg.musicalSheet, true);
            if(!psMusic::GetStatistics(scoreDoc, songLength))
            {
                // sending an error message
                psStopSongMessage stopMsg(client->GetClientNum(), 0, true);
                stopMsg.SendMessage();
                return;
            }

            // getting equipped instrument
            instrItem = GetEquippedInstrument(charData);
            if(instrItem == 0)
            {
                // sending an error message
                psStopSongMessage stopMsg(client->GetClientNum(), 0, true);
                stopMsg.SendMessage();
                return;
            }

            instrName = instrItem->GetName();
            actorEID = charActor->GetEID().Unbox();

            // calculating song parameters
            if(calcSongPar == 0)
            {
                errorRate = 0;
            }
            else
            {
                mathEnv.Define("Player", client->GetActor());
                mathEnv.Define("Instrument", instrItem);
                calcSongPar->Evaluate(&mathEnv);

                errorRate = mathEnv.Lookup("ErrorRate")->GetValue();
            }

            // sending message to requester, it's useless to send the musical sheet again
            psPlaySongMessage sendedPlayMsg(client->GetClientNum(), actorEID, true, errorRate, instrName, 0, "");
            sendedPlayMsg.SendMessage();

            // preparing compressed musical sheet
            csString compressedScore;
            psMusic::ZCompressSong(musicMsg.musicalSheet, compressedScore);

            // sending play messages to proximity list
            proxList = charActor->GetProxList()->GetClients();
            for(size_t i = 0; i < proxList.GetSize(); i++)
            {
                if(client->GetClientNum() == proxList[i].client)
                {
                    continue;
                }
                psPlaySongMessage sendedPlayMsg(proxList[i].client, actorEID, false, errorRate, instrName, compressedScore.Length(), compressedScore);
                sendedPlayMsg.SendMessage();
            }

            // updating character mode and item status
            charActor->SetMode(PSCHARACTER_MODE_PLAY);
            instrItem->SetInUse(true);

            // keeping track of the song's time
            psEndSongEvent* event = new psEndSongEvent(charActor, songLength);
            psserver->GetEventManager()->Push(event);
        }
        else
        {
            Error3("Item %u not found in musical sheet message from client %s.", musicMsg.itemID, client->GetName());
        }
    }
}

void ServerSongManager::HandleStopSongMessage(MsgEntry* me, Client* client)
{
    psStopSongMessage receivedStopMsg(me);

    if(receivedStopMsg.valid)
    {
        OnStopSong(client->GetActor(), false);
    }
}

void ServerSongManager::OnStopSong(gemActor* charActor, bool isEnded)
{
    // checking that the client's player is actually playing
    if(charActor->GetMode() != PSCHARACTER_MODE_PLAY)
    {
        return;
    }

    isProcessedSongEnded = isEnded;

    // updating character mode, this will call StopSong
    charActor->SetMode(PSCHARACTER_MODE_PEACE);
}

void ServerSongManager::StopSong(gemActor* charActor, bool skillRanking)
{
    psItem* instrItem;
    uint32 actorEID = charActor->GetEID().Unbox();
    psCharacter* charData = charActor->GetCharacterData();
    int charClientID = charActor->GetProxList()->GetClientID();

    // forwarding the message to the whole proximity list
    if(!isProcessedSongEnded) // no need to send it
    {
        csArray<PublishDestination> proxList = charActor->GetProxList()->GetClients();

        for(size_t i = 0; i < proxList.GetSize(); i++)
        {
            if(charClientID != proxList[i].client)
            {
                psStopSongMessage sendedStopMsg(proxList[i].client, actorEID, false);
                sendedStopMsg.SendMessage();
            }
        }
    }
    else // client needs to be notified if sounds are disabled
    {
        // resetting flag
        isProcessedSongEnded = false;

        // notifying the client
        psStopSongMessage sendedStopMsg(charClientID, actorEID, true);
        sendedStopMsg.SendMessage();
    }

    // handling skill ranking
    instrItem = GetEquippedInstrument(charData);
    if(instrItem != 0)
    {
        // unlocking instrument
        instrItem->SetInUse(false);

        if(skillRanking && calcSongPar != 0 && calcSongExp != 0)
        {
            MathEnvironment mathEnv;
            int practicePoints;
            float modifier;
            PSSKILL instrSkill;

            mathEnv.Define("Player", charActor);
            mathEnv.Define("Instrument", instrItem);
            mathEnv.Define("SongTime", charData->GetPlayingTime() / 1000);
            mathEnv.Define("AverageDuration", 1);   // TODO to be implemented
            mathEnv.Define("AveragePolyphony", 1);  // TODO to be implemented

            calcSongPar->Evaluate(&mathEnv);
            calcSongExp->Evaluate(&mathEnv);

            practicePoints = mathEnv.Lookup("PracticePoints")->GetValue();
            modifier = mathEnv.Lookup("Modifier")->GetValue();
            instrSkill = (PSSKILL)(mathEnv.Lookup("InstrSkill")->GetRoundValue());

            charData->CalculateAddExperience(instrSkill, practicePoints, modifier);
        }
    }
}

psItem* ServerSongManager::GetEquippedInstrument(psCharacter* charData) const
{
    psItem* instrItem;

    instrItem = charData->Inventory().GetItem(0, PSCHARACTER_SLOT_RIGHTHAND);
    if(instrItem == 0 || instrItem->GetCategory()->id != instrumentsCategory)
    {
        instrItem = charData->Inventory().GetItem(0, PSCHARACTER_SLOT_LEFTHAND);
        if(instrItem != 0 && instrItem->GetCategory()->id != instrumentsCategory)
        {
            instrItem = 0;
        }
    }

    return instrItem;
}
