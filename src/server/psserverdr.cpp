/*
  * psserverdr.cpp by Matze Braun <MatzeBraun@gmx.de>
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
#include <iengine/movable.h>
#include <iengine/mesh.h>
#include <iutil/object.h>


//=============================================================================
// Project Includes
//=============================================================================
#include "net/message.h"
#include "net/msghandler.h"

#include "engine/celbase.h"
#include "engine/linmove.h"

#include "util/serverconsole.h"
#include "util/log.h"
#include "util/psconst.h"
#include "util/mathscript.h"
#include "util/eventmanager.h"


//=============================================================================
// Local Includes
//=============================================================================
#include "psserverdr.h"
#include "client.h"
#include "clients.h"
#include "tutorialmanager.h"
#include "events.h"
#include "psserver.h"
#include "cachemanager.h"
#include "playergroup.h"
#include "gem.h"
#include "entitymanager.h"
#include "progressionmanager.h"
#include "adminmanager.h"
#include "paladinjr.h"
#include "psproxlist.h"
#include "globals.h"

psServerDR::psServerDR()
{
    entitymanager = NULL;
    clients = NULL;
    paladin = NULL;

#ifdef USE_THREADED_DR
    dm.AttachNew(new DelayedDRManager(this));
    dmThread.AttachNew(new Thread(dm));
    dmThread->Start();
    dmThread->SetPriority(CS::Threading::THREAD_PRIO_HIGH);
#endif
}

psServerDR::~psServerDR()
{
    if (psserver->GetEventManager())
        psserver->GetEventManager()->Unsubscribe(this,MSGTYPE_DEAD_RECKONING);
    //delete paladin;
#ifdef USE_THREADED_DR
    dm->Stop();
    dmThread->Stop();
#endif
}

bool psServerDR::Initialize(EntityManager* entitymanager,
    ClientConnectionSet* clients)
{
    psServerDR::entitymanager  = entitymanager;
    psServerDR::clients        = clients;

    if (!psserver->GetEventManager()->Subscribe(this,MSGTYPE_DEAD_RECKONING,REQUIRE_READY_CLIENT))
        return false;

    calc_damage   = psserver->GetMathScriptEngine()->FindScript("Calculate Fall Damage");
    if(!calc_damage)
    {
        CPrintf(CON_ERROR, "Cannot find script 'Calculate Fall Damage'!\n");
        return false;
    }


    // Output var bindings here
    var_fall_height = calc_damage->GetVar("FallHeight");
    var_fall_dmg    = calc_damage->GetVar("Damage");

    paladin = new PaladinJr;
    paladin->Initialize(entitymanager);
    
    return true;
}

void psServerDR::SendPersist()
{
    // no server side actions yet

    return;
}

void psServerDR::HandleFallDamage(gemActor *actor,int clientnum, const csVector3& pos, iSector* sector)
{
    float fallHeight = actor->FallEnded(pos,sector);

    // Initialize all variables before call to execute 
    var_fall_height->SetValue(fallHeight);
    var_fall_dmg->SetValue(0.0);
    calc_damage->Execute();
    Debug4(LOG_LINMOVE,actor->GetClientID(), "%s fell %.2fm for damage %.2f",
           actor->GetName(),fallHeight,var_fall_dmg->GetValue() );

    if (var_fall_dmg->GetValue() > 0)
    {
        bool died = (var_fall_dmg->GetValue() > actor->GetCharacterData()->GetHP());

        actor->DoDamage(NULL, var_fall_dmg->GetValue() );
        if (died)
        {
            //Client died
            psserver->SendSystemInfo(clientnum, "You fell down and died.");
        }
    }
}

void psServerDR::ResetPos(gemActor* actor)
{
    psserver->SendSystemInfo(actor->GetClient()->GetClientNum(), "Received out of bounds positional data, resetting your position.");
    iSector* targetSector;
    csVector3 targetPoint;
    csString targetSectorName;
    float yRot = 0;
    actor->GetPosition(targetPoint, yRot, targetSector);
    csRef<iCollection> psRegion =  scfQueryInterface<iCollection> (targetSector->QueryObject()->GetObjectParent());
    if (psRegion)
        targetSectorName = psRegion->QueryObject()->GetName();
    else
        targetSectorName = targetSector->QueryObject()->GetName();
    psserver->GetAdminManager()->GetStartOfMap(actor->GetClient(), targetSectorName, targetSector, targetPoint);
    actor->pcmove->SetOnGround(false);
    actor->pcmove->SetVelocity(csVector3(0,0,0));
    actor->SetPosition(targetPoint,0, targetSector);
    actor->UpdateProxList(true);  // true= force update
    actor->MulticastDRUpdate();
}

void psServerDR::HandleMessage (MsgEntry* me,Client *client)
{
#ifdef USE_THREADED_DR
    me->IncRef();
    dm->Push(me, client);
    return;
#endif
    WorkOnMessage(me, client);
}

void psServerDR::WorkOnMessage (MsgEntry* me,Client *client)
{
    psDRMessage drmsg(me,CacheManager::GetSingleton().GetMsgStrings(),EntityManager::GetSingleton().GetEngine() );
    if (!drmsg.valid)
    {
        Debug2(LOG_NET,me->clientnum,"Received unparsable psDRMessage from client %u.\n",me->clientnum);
        return;
    }

    gemActor *actor = client->GetActor();
    if (actor == NULL)
    {
        Error1("Recieved DR data for NULL actor.");
        return;
    }

    if ( client->IsFrozen() || !actor->IsAllowedToMove())  // Is this movement allowed?
    {
        if (drmsg.worldVel.y > 0)
        {
            client->FlagExploit();  // This DR data may be an exploit but may also be valid from lag.
            actor->pcmove->AddVelocity(csVector3(0,-1,0));
            actor->UpdateDR();
            actor->MulticastDRUpdate();
            return;
        }
    }

    if (drmsg.sector == NULL)
    {
        Error2("Client sent the server DR message with unknown sector \"%s\" !", drmsg.sectorName.GetData());
        psserver->SendSystemInfo(me->clientnum,
                                 "Received unknown sector \"%s\" - moving you to a valid position.",
                                 drmsg.sectorName.GetData() );
        /* FIXME: Strangely, when logging on, the client ends up putting the
         * actor in SectorWhereWeKeepEntitiesResidingInUnloadedMaps, and send
         * a DR packet with (null) sector.  It then relies on the server to
         * bail it out with MoveToValidPos, which at this stage is the login
         * position the server tried to set in the first place. */
        actor->MoveToValidPos(true);
        return;
    }

    // These values must be sane or the proxlist will die.
    // The != test tests for NaN because if it is, the proxlist will mysteriously die (found out the hard way)
    if(drmsg.pos.x != drmsg.pos.x || drmsg.pos.y != drmsg.pos.y || drmsg.pos.z != drmsg.pos.z ||
        fabs(drmsg.pos.x) > 100000 || fabs(drmsg.pos.y) > 1000 || fabs(drmsg.pos.z) > 100000)
    {
        ResetPos(actor);
        return;
    }
    else if(drmsg.vel.x != drmsg.vel.x || drmsg.vel.y != drmsg.vel.y || drmsg.vel.z != drmsg.vel.z ||
        fabs(drmsg.vel.x) > 1000 || fabs(drmsg.vel.y) > 1000 || fabs(drmsg.vel.z) > 1000)
    {
        ResetPos(actor);
        return;
    }
    else if(drmsg.worldVel.x != drmsg.worldVel.x || drmsg.worldVel.y != drmsg.worldVel.y || drmsg.worldVel.z != drmsg.worldVel.z ||
        fabs(drmsg.worldVel.x) > 1000 || fabs(drmsg.worldVel.y) > 1000 || fabs(drmsg.worldVel.z) > 1000)
    {
        ResetPos(actor);
        return;
    }

    paladin->PredictClient(client, drmsg);

    // Go ahead and update the server version
    if (!actor->SetDRData(drmsg)) // out of date message if returns false
        return;

    // Check for Movement Tutorial Required.
    // Usually we don't want to check but DR msgs are so frequent,
    // perf hit is unacceptable otherwise.
    if (actor->GetCharacterData()->NeedsHelpEvent(TutorialManager::MOVEMENT))
    {
        if (!drmsg.vel.IsZero() || drmsg.ang_vel)
        {
            psMovementEvent evt(client->GetClientNum() );
            evt.FireEvent();
        }
    }

    // Update falling status
    if (actor->pcmove->IsOnGround())
    {
        if (actor->IsFalling())
        {
            iSector* sector = actor->GetMeshWrapper()->GetMovable()->GetSectors()->Get(0);

            // GM flag
            if(!actor->safefall)
            {
                HandleFallDamage(actor,me->clientnum, drmsg.pos,sector);
            }
            else
            {
                actor->FallEnded(drmsg.pos,sector);
            }
        }
    }
    else if (!actor->IsFalling())
    {
        iSector* sector = actor->GetMeshWrapper()->GetMovable()->GetSectors()->Get(0);
        actor->FallBegan(drmsg.pos, sector);
    }

    //csTicks time = csGetTicks();
    actor->UpdateProxList();
    /*
    if (csGetTicks() - time > 500)
    {   
        csString status;
        status.Format("Warning: Spent %u time updating proxlist for %s!", csGetTicks() - time, actor->GetName());
        psString out;
        actor->GetProxList()->DebugDumpContents(out);
        out.ReplaceAllSubString("\n", " ");

        status.Append(out);
        psserver->GetLogCSV()->Write(CSV_STATUS, status);
    }
    */

    // Now multicast to other clients
    psserver->GetEventManager()->Multicast(me,
                          actor->GetMulticastClients(),
                          me->clientnum,PROX_LIST_ANY_RANGE);

    paladin->CheckClient(client);

    // Swap lines for easy Death Penalty testing.            
    //if (strcmp(drmsg.sector->QueryObject()->GetName(), "NPCroom1") == 0)
    if (strcmp(drmsg.sector->QueryObject()->GetName(), "DRexit") == 0)
    {
         psserver->GetProgressionManager()->ProcessEvent("death_penalty", actor);

        actor->pcmove->SetOnGround(false);
        actor->MoveToSpawnPos();
    }
}

#ifdef USE_THREADED_DR

DelayedDRManager::DelayedDRManager(psServerDR* pDR)
{
    m_Close = false;
    start=end=0;
    arr.SetSize(100);
    arrClients.SetSize(3000);
    serverdr = pDR;
}

void DelayedDRManager::Stop()
{
    m_Close = true;
    datacondition.NotifyOne();
    CS::Threading::Thread::Yield();
}

void DelayedDRManager::Run()
{
    while(!m_Close)
    {
        CS::Threading::MutexScopedLock lock(mutex);
        datacondition.Wait(mutex);
        while (start != end)
        {
            MsgEntry* me;
            Client* c;
            {
                CS::Threading::RecursiveMutexScopedLock lock(mutexArray);
                me = arr[end];
                c = arrClients[end];
                end = (end+1) % arr.GetSize();
            }
            Debug2(LOG_NET, 0, "Processing DR from client %d", c->GetClientNum());
            serverdr->WorkOnMessage(me, c);
            me->DecRef();
        }
    }
}

void DelayedDRManager::Push(MsgEntry* msg, Client* c) 
{ 
    Debug2(LOG_NET, 0, "Queueing new DR from client %d", c->GetClientNum());
    {
        CS::Threading::RecursiveMutexScopedLock lock(mutexArray);
        size_t tstart = (start+1) % arr.GetSize();
        while (tstart == end)
        {
            csSleep(100);
        }
        arr[start] = msg;
        arrClients[start] = c;
        start = tstart;
    }
    datacondition.NotifyOne();
}
#endif
