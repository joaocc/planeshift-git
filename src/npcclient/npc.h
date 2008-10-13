/*
* npc.h
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

/* This file holds definitions for ALL global variables in the planeshift
* server, normally you should move global variables into the psServer class
*/
#ifndef __NPC_H__
#define __NPC_H__
//=============================================================================
// Crystal Space Includes
//=============================================================================
#include <csutil/hash.h>

struct iMovable;

//=============================================================================
// Local Includes
//=============================================================================
#include "npcbehave.h"

class EventManager;
class  HateList;
struct HateListEntry;
class iResultRow;
class psTribe;
class Waypoint;
class gemNPCObject;
class gemNPCActor;
struct RaceInfo_t;
class LocationType;

/**
* This object represents the entities which have attacked or 
* hurt the NPC and prioritizes them.
*/
class HateList
{
protected:
    csHash<HateListEntry*> hatelist;

public:
    HateList() {}

    void AddHate(int entity_id,float delta);
    gemNPCActor *GetMostHated(iSector *sector, csVector3& pos, float range, LocationType * region, bool include_invisible, bool include_invincible);
    bool Remove(int entity_id);
    void DumpHateList(const csVector3& myPos, iSector *mySector);
    void Clear();
    float GetHate(int ent);
};


/**
* This object represents each NPC managed by this superclient.
*/
class NPC
{
protected:
    NPCType           *brain;
    csString           type;
    unsigned int       pid;
    PS_ID              oldID;
    csString           name;
    csTicks            last_update;
    gemNPCActor       *npcActor;
    iMovable          *movable;
    uint8_t            DRcounter;

    csVector3          active_locate_pos;
    iSector*           active_locate_sector;
    float              active_locate_angle;
    Waypoint*          active_locate_wp;
    float              ang_vel,vel;
    float              walkVelocity,runVelocity;
    LocationType      *region;
    csString           region_name;
    Perception        *last_perception;
    int                debugging;       /// The current debugging level for this npc
    bool               alive;
    int                owner_id;
    uint32_t           target_id;
    psTribe           *tribe;

    RaceInfo_t        *raceInfo;

    // Initial position checks
    csVector3          checkedPos;
    iSector*           checkedSector;
    bool               checked;
    bool               checkedResult;
    bool               disabled;

public:
    HateList           hatelist;
    
    NPC();
    ~NPC();

    unsigned int          GetPID() { return pid; }
    /**
     * Return the entity ID if an entity exist else 0.
     */
    PS_ID                 GetEID();
    iMovable             *GetMovable()   { return movable; }
    psLinearMovement     *GetLinMove();
    uint8_t               GetDRCounter() { return ++DRcounter;}
    void                  SetDRCounter(uint8_t counter) { DRcounter = counter;}

    bool Load(iResultRow& row,BinaryRBTree<NPCType>& npctypes);
    bool InsertCopy(int use_char_id,int ownerPID);

    void SetActor(gemNPCActor * actor);
    gemNPCActor * GetActor() { return npcActor; }
    const char* GetName() {return name.GetDataSafe();}
    void SetAlive(bool a) { alive = a; }
    bool IsAlive() const { return alive; }
    void Disable();
    bool IsDisabled() { return disabled; }

    Behavior *GetCurrentBehavior() { return brain->GetCurrentBehavior(); }
    NPCType  *GetBrain() { return brain; }

    /**
     * Dump all state information for npc.
     */
    void DumpState();
    /**
     * Dump all behaviors for npc.
     */
    void DumpBehaviorList();
    /**
     * Dump all reactions for npc.
     */
    void DumpReactionList();
    /**
     * Dump all hated entities for npc.
     */
    void DumpHateList();

    void ClearState();

    void Advance(csTicks when,EventManager *eventmgr);
    void ResumeScript(EventManager *eventmgr,Behavior *which);

    void TriggerEvent(Perception *pcpt,EventManager *eventmgr);
    void SetLastPerception(Perception *pcpt);
    Perception *GetLastPerception() { return last_perception; }

    gemNPCActor *GetMostHated(float range, bool include_invisible, bool include_invincible);
    float       GetEntityHate(gemNPCActor *ent);
    void AddToHateList(gemNPCActor *attacker,float delta);
    void RemoveFromHateList(PS_ID who);

    void SetActiveLocate(csVector3& pos, iSector* sector, float rot, Waypoint * wp)
    { active_locate_pos=pos; active_locate_sector = sector;
      active_locate_angle=rot; active_locate_wp = wp; }

    void GetActiveLocate(csVector3& pos,iSector*& sector, float& rot)
    { pos=active_locate_pos; sector = active_locate_sector; rot=active_locate_angle; }

    void GetActiveLocate(Waypoint*& wp) { wp = active_locate_wp; }
    
    bool SwitchDebugging()
    {
        debugging = !debugging;
        return IsDebugging();
    }
    
    void SetDebugging(int debug)
    {
        debugging = debug;
    }
    
    float GetAngularVelocity()
    {
        if (ang_vel == 999)
            return brain->GetAngularVelocity(this);
        else
            return ang_vel;
    }

    float GetVelocity()
    {
        if (vel == 999)
            return brain->GetVelocity(this);
        else
            return vel;
    }

    float GetWalkVelocity();
    float GetRunVelocity();

    LocationType *GetRegion();
    csString& GetRegionName() { return region_name; }

    void GetNearestEntity(uint32_t& target_id,csVector3& dest,csString& name,float range);

    gemNPCActor * GetNearestVisibleFriend(float range);

    void Printf(const char *msg,...);
    void Printf(int debug, const char *msg,...);
    void VPrintf(int debug, const char *msg,va_list arg);

    gemNPCObject *GetTarget();
    void SetTarget(gemNPCObject *t);

    gemNPCObject *GetOwner();
    const char* GetOwnerName();

    /** Set a new tribe for this npc */
    void SetTribe(psTribe * new_tribe);
    /** Get the tribe this npc belongs to. Null if not part of a tribe */
    psTribe * GetTribe();

    RaceInfo_t * GetRaceInfo();
    

    bool IsDebugging() { return (debugging > 0);};
    bool IsDebugging(int debug) { return (debugging > 0 && debug <= debugging);};

    void CheckPosition();
};

struct HateListEntry
{
    int   entity_id;
    float hate_amount;
};


#endif

