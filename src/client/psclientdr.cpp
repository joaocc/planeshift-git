/*
* psclientdr.cpp by Matze Braun <MatzeBraun@gmx.de>
*
* Copyright (C) 2002 Atomic Blue (info@planeshift.it, http://www.atomicblue.org) 
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
#include <iutil/databuff.h>
#include <csutil/sysfunc.h>
#include <iengine/mesh.h>
#include <iengine/sector.h>
#include <imesh/object.h>
#include <imesh/spritecal3d.h>
#include <iutil/object.h>
#include <ivaria/engseq.h>

//=============================================================================
// Project Includes
//=============================================================================
#include "net/message.h"
#include "net/msghandler.h"
#include "net/connection.h"

//=============================================================================
// Local Includes
//=============================================================================
#include "psclientdr.h"
#include "pscelclient.h"
#include "modehandler.h"
#include "clientvitals.h"
#include "psnetmanager.h"
#include "globals.h"



#define LINMOVE_IGNORECHEATS false

////////////////////////////////////////////////////////////////////////////
//  PAWS INCLUDES
////////////////////////////////////////////////////////////////////////////
#include "paws/pawsmanager.h"
#include "gui/pawsgroupwindow.h"
#include "gui/pawsinfowindow.h"
#include "gui/pawscharpick.h"

psClientDR::psClientDR()
{
    celclient = NULL;
    lastupdate = 0;
    lastquery = 0;
    msgstrings = NULL;
    gotStrings = false;
    groupWindow = 0;
}

psClientDR::~psClientDR()
{
    if (msghandler)
    {
        msghandler->Unsubscribe(this,MSGTYPE_DEAD_RECKONING);
        msghandler->Unsubscribe(this,MSGTYPE_STATDRUPDATE);
        msghandler->Unsubscribe(this,MSGTYPE_MSGSTRINGS);
        msghandler->Unsubscribe(this,MSGTYPE_OVERRIDEACTION);
        msghandler->Unsubscribe(this,MSGTYPE_SEQUENCE);
    }

    // allocated by message cracker, but released here
    delete msgstrings;
}

bool psClientDR::Initialize(iObjectRegistry* object_reg, psCelClient* celclient, MsgHandler* msghandler )
{
    psClientDR::object_reg = object_reg;
    psClientDR::celclient = celclient;
    psClientDR::msghandler = msghandler;
    
    msgstrings = 0; // will get it in a MSGTYPE_MSGSTRINGS message

    msghandler->Subscribe(this,MSGTYPE_DEAD_RECKONING);
    msghandler->Subscribe(this,MSGTYPE_STATDRUPDATE);
    msghandler->Subscribe(this,MSGTYPE_MSGSTRINGS);
    msghandler->Subscribe(this,MSGTYPE_OVERRIDEACTION);
    msghandler->Subscribe(this,MSGTYPE_SEQUENCE);

    return true; 
}

// time till next update packet is send
#define UPDATEWAIT  200

void psClientDR::CheckDeadReckoningUpdate()
{
    static float prevVelY = 0;  // vertical velocity of the character when this method was last called
    csVector3 vel;              // current  velocity of the character
    bool beganFalling;

    if ( !celclient->GetMainPlayer() )
    {
        return;
    }            

    lastupdate = csGetTicks();

    if(celclient->IsUnresSector(celclient->GetMainPlayer()->GetSector()))
        return;                 // Main actor still in unresolved sector

    vel = celclient->GetMainPlayer()->GetVelocity();

    beganFalling = (prevVelY>=0 && vel.y<0);
    prevVelY = vel.y;

    uint8_t priority;

    if (! (celclient->GetMainPlayer()->NeedDRUpdate(priority)  ||  beganFalling) )
    {
        // no need to send new packet, real position is as calculated
        return;
    }

    if (!msgstrings)
    {
        Error1("msgstrings not received, cannot handle DR");
        return;
    }

    celclient->GetMainPlayer()->SendDRUpdate(priority,msgstrings);

    return;
}

void psClientDR::CheckSectorCrossing(GEMClientActor *actor)
{
    csString curr = actor->GetSector()->QueryObject()->GetName();

    if (curr != last_sector)
    {
        // crossed sector boundary since last update
        csVector3 pos;
        float yrot;
        iSector* sector;

        actor->GetLastPosition (pos, yrot, sector);
        psNewSectorMessage cross(last_sector, curr, pos);
        msghandler->Publish(cross.msg);  // subscribers to sector messages can process here

        last_sector = curr;
    }
}

void psClientDR::HandleMessage (MsgEntry* me)
{
    if (me->GetType() == MSGTYPE_DEAD_RECKONING)
    {
        HandleDeadReckon( me );
    }
    else if (me->GetType() == MSGTYPE_STATDRUPDATE)
    {
        HandleStatsUpdate( me );
    }
    else if (me->GetType() == MSGTYPE_MSGSTRINGS)
    {
        HandleStrings( me );
    }
    else if (me->GetType() == MSGTYPE_OVERRIDEACTION)
    {
        HandleOverride( me );
    }
    else if (me->GetType() == MSGTYPE_SEQUENCE)
    {
        HandleSequence( me );
    }
}

void psClientDR::ResetMsgStrings()
{
    if(psengine->IsGameLoaded())
        return; // Too late

    delete msgstrings;
    msgstrings = NULL;
    gotStrings = false;
}

void psClientDR::HandleDeadReckon( MsgEntry* me )
{
    psDRMessage drmsg(me,msgstrings,psengine->GetEngine() );
    GEMClientActor* gemActor = (GEMClientActor*)psengine->GetCelClient()->FindObject( drmsg.entityid );
     
    if (!gemActor)
    {
        csTicks currenttime = csGetTicks();
        if (currenttime - lastquery > 750)
        {
            lastquery = currenttime;
        }

        Error2("Got DR message for unknown entity %d.",drmsg.entityid);
        return;
    }

    if (!msgstrings)
    {
        Error1("msgstrings not received, cannot handle DR");
        return;
    }

    // If the object that changed position is our player, check if he crossed sector boundary.
    // If that case the player may have been moved to map that is not currently loaded so
    // we must tell ZoneHandler to: 1) load the needed maps 2) set player's position.
    // If the other case (when the player is in the same sector) we simply set his position immediately.
    if (gemActor == celclient->GetMainPlayer())
    {
        if (last_sector != drmsg.sectorName)
        {
            psNewSectorMessage cross(last_sector, drmsg.sectorName, drmsg.pos);
            msghandler->Publish(cross.msg);
            Error3("Sector crossed from %s to %s after received DR.\n", last_sector.GetData(), drmsg.sectorName.GetData());
        }
        
        psengine->GetModeHandler()->SetModeSounds(drmsg.mode);
    }

    // Set data for this actor
    gemActor->SetDRData(drmsg);
}

void psClientDR::HandleStatsUpdate( MsgEntry* me )
{
    psStatDRMessage statdrmsg(me);
    GEMClientActor* gemObject  = (GEMClientActor*)psengine->GetCelClient()->FindObject( statdrmsg.entityid );
    if (!gemObject)
    {
        Error2("Stat request failed because CelClient not ready for %d",statdrmsg.entityid);
        return;
    }

    // Dirty short cut to allways display 0 HP when dead.
    if (!gemObject->IsAlive() && statdrmsg.hp)
    {
        Error1("Server report HP but object is not alive");
        statdrmsg.hp = 0;
        statdrmsg.hp_rate = 0;
    }
    
    // Check if this client actor was updated
    GEMClientActor* mainActor = celclient->GetMainPlayer();
    
    if (mainActor == gemObject)
    {
        gemObject->GetVitalMgr()->HandleDRData(statdrmsg,"Self");
    }        
    else 
    {   // Publish Vitals data using EntityID
        csString ID;
        ID.Append(gemObject->GetEID());
        gemObject->GetVitalMgr()->HandleDRData(statdrmsg, ID.GetData() );
    }

    //It is not an else if so Target is always published
    if (psengine->GetCharManager()->GetTarget() == gemObject)
        gemObject->GetVitalMgr()->HandleDRData(statdrmsg,"Target"); 
    
    if (mainActor != gemObject && gemObject->IsGroupedWith(celclient->GetMainPlayer()) )
    {
        if (!groupWindow)
        {
            // Get the windowMgr

            pawsWidget* widget = PawsManager::GetSingleton().FindWidget("GroupWindow");
            groupWindow = (pawsGroupWindow*)widget;

            if (!groupWindow) 
            {
                Error1("Group Window Was Not Found. Bad Error");
                return;
            }
        }
        
        groupWindow->SetStats(gemObject);
    }
}

void psClientDR::HandleStrings( MsgEntry* me )
{
    // receive message strings hash table
    psMsgStringsMessage msg(me);
    msgstrings = msg.msgstrings;
    ((psNetManager*)psengine->GetNetManager())->GetConnection()->SetMsgStrings(msgstrings);
    ((psNetManager*)psengine->GetNetManager())->GetConnection()->SetEngine(psengine->GetEngine());
    gotStrings = true;

    pawsCharacterPickerWindow* charPick = (pawsCharacterPickerWindow*)PawsManager::GetSingleton().FindWidget("CharPickerWindow");
    if(charPick) // If it doesn't exist now, it will check the gotStrings flag on show. In other word no need to panic
    {
        charPick->ReceivedStrings();
    }    
}

void psClientDR::HandleOverride( MsgEntry* me )
{
    psOverrideActionMessage msg(me);
    if (!msg.valid)
        return;

    GEMClientActor* gemActor = (GEMClientActor*)psengine->GetCelClient()->FindObject(msg.entity_id);
    if (!gemActor)
        return;

    gemActor->SetAnimation( msg.action.GetData(), msg.duration );
}


void psClientDR::HandleSequence( MsgEntry* me )
{
    psSequenceMessage msg(me);
    if (!msg.valid)
        return;

    csRef<iEngineSequenceManager> seqmgr(
        csQueryRegistry<iEngineSequenceManager> (psengine->GetObjectRegistry()));
    if (seqmgr)
    {
        seqmgr->RunSequenceByName (msg.name,0);
    }
}

void psClientDR::HandleDeath(GEMClientActor * gemObject)
{
    // Check if this client actor was updated    
    if ( gemObject == celclient->GetMainPlayer() )
    {
        gemObject->GetVitalMgr()->HandleDeath("Self");
    }
    else 
    {   // Publish Vitals data using EntityID
        csString ID;
        ID.Append(gemObject->GetEID());
        gemObject->GetVitalMgr()->HandleDeath( ID.GetData() );
    }

    //It is not an else if so Target is always published
    if (psengine->GetCharManager()->GetTarget() == gemObject)
    {
        gemObject->GetVitalMgr()->HandleDeath("Target"); 
    }
}
