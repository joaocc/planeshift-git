/*
* tribe.h
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
#ifndef __TRIBE_H__
#define __TRIBE_H__

//=============================================================================
// Crystal Space Includes
//=============================================================================
#include <csutil/array.h>
#include <csutil/list.h>
#include <csgeom/vector3.h>
#include <iengine/sector.h>

//=============================================================================
// Project Includes
//=============================================================================
#include <util/psconst.h>

class iResultRow;
class EventManager;
class NPC;
class TribeNeedSet;
class TribeNeed;
class Perception;

#define TRIBE_UNLIMITED_SIZE   100

class Tribe
{
public:
    struct Resource
    {
        int      id;           ///< Database id
        csString name;
        int      amount;
    };

    struct Memory
    {
        int       id;          ///< Database id
        csString  name;
        csVector3 pos;
        iSector*  sector;
        csString  sectorName;  ///< Keep the sector name until sector is loaded
        float     radius;
        NPC*      npc;         ///< Privat memory if NPC is set

        iSector* GetSector();
    };

    /**
     * Represent each type of NPC Needs
     */
    enum TribeNeedType
    {
        GENERIC,
        RESOURCE_AREA,
        REPRODUCE
    };
    
    static const char *TribeNeedTypeName[];

    /** Construct a new tribe object */
    Tribe();

    /** Destruct a tribe object */
    virtual ~Tribe();

    /** Load the tribe object */
    bool Load(iResultRow& row);

    /** Load and add a new need to the tribe */
    bool LoadNeed(iResultRow& row);

    /** Load and add a new member to the tribe */
    bool LoadMember(iResultRow& row);

    /** Load and add a new resource to the tribe */
    bool LoadResource(iResultRow& row);
    
    /** Adds a new member to the tribe, e.g. from reproduction */
    bool AddMember(PID pid);

    /** Save or update an resource in database */
    void SaveResource(Resource* resource, bool newResource);

    /** Attach a new member to the tribe if the NPC is a member */
    bool CheckAttach(NPC * npc);

    /** Attach a new member to the tribe */
    bool AttachMember(NPC * npc);

    /** Remove members that die */
    bool HandleDeath(NPC * npc);

    /**
     * Count number of alive members
     */
    int AliveCount() const;

    /** Handled a perception given to this tribe */
    void HandlePerception(NPC * npc, Perception * perception);

    /** Add a new resource to the tribe resource table */
    void AddResource(csString resource, int amount);

    /**
     * Return the amount of a given resource
     */
    int CountResource(csString resource) const;
    
    /** Advance the tribe */
    void Advance(csTicks when,EventManager *eventmgr);

    int GetID() { return id; }
    const char* GetName() { return name.GetDataSafe(); }
    size_t GetMemberIDCount() { return membersId.GetSize(); }
    size_t GetMemberCount() { return members.GetSize(); }
    NPC * GetMember(size_t i) { return members[i]; }
    size_t GetResourceCount() { return resources.GetSize(); }
    const Resource& GetResource(size_t n) { return resources[n]; }
    csList<Memory*>::Iterator GetMemoryIterator() { csList<Memory*>::Iterator it(memories); return it; };
    TribeNeedSet* GetNeedSet() { return needSet; };

    /**
     * Calculate the maximum number of members for the tribe.
     */
    int GetMaxSize() const;


    /**
     * Return the reproduction cost for this tribe.
     */
    int GetReproductionCost() const;
    
    /**
     * Get home position for the tribe.
     */
    void GetHome(csVector3& pos, float& radius, iSector* &sector);

    /**
     * Set home position for the tribe.
     */
    void SetHome(const csVector3& pos, float radius, iSector* sector);

    /** Check if the position is within the bounds of the tribe home
     *
     * @param npc    The npc responsible for this checking
     * @param pos    The position to check
     * @param sector The sector to check
     * @return True if position is within bounds of the tribe home
     */
    bool CheckWithinBoundsTribeHome(NPC* npc, const csVector3& pos, const iSector* sector);
    
    /**
     * Get a memorized location for resources
     */
    bool GetResource(NPC* npc, csVector3 startPos, iSector * startSector,
                     csVector3& pos, iSector* &sector, float range, bool random);

    /**
     * Get the most needed resource for this tribe.
     */
    const char* GetNeededResource();

    /**
     * Get the nick for the most needed resource for this tribe.
     */
    const char* GetNeededResourceNick();

    /**
     * Get a area for the most needed resource for this tribe.
     */
    const char* GetNeededResourceAreaType();

    /** Get wealth resource growth rate
     *
     * Get the rate that the wealth will grow when all members are dead.
     *
     * @return Return the wealth growth rate
     */
    float GetWealthResourceGrowth() const;

    /** Get wealth resource growth rate active
     *
     * Get the rate that the wealth will grow when there are alive members.
     *
     * @return Return the wealth growth rate
     */
    float GetWealthResourceGrowthActive() const;

    /** Get wealth resource growth active limit
     *
     * Get the limit that the wealth will be capped at when there are alive members.
     *
     * @return Return the wealth growth limit
     */
    int GetWealthResourceGrowthActiveLimit() const;
    
    
    /**
     * Check if the tribe can grow by checking the tribes wealth
     */
    bool CanGrow() const;

    /**
     * Check if the tribe should grow by checking number of members
     * against max size.
     */
    bool ShouldGrow() const;

    /**
     * Initialize the need set for the tribe brain.
     */
    void InitializeNeedSet();

    /**
     * Memorize a perception. The perception will be marked as
     * personal until NPC return home. Personal perceptions
     * will be deleted if NPC die.
     */
    void Memorize(NPC* npc, Perception * perception);
    
    /**
     * Find a privat memory
     */
    Memory* FindPrivMemory(csString name,const csVector3& pos, iSector* sector, float radius, NPC* npc);

    /**
     * Find a memory
     */
    Memory* FindMemory(csString name,const csVector3& pos, iSector* sector, float radius);

    /**
     * Find a memory
     */
    Memory* FindMemory(csString name);

    /**
     * Add a new memory to the tribe
     */
    void AddMemory(csString name,const csVector3& pos, iSector* sector, float radius, NPC* npc);

    /**
     * Share privat memories with the other npcs. Should be called when npc return to home.
     */
    void ShareMemories(NPC * npc);

    /**
     * Save a memory to the db
     */
    void SaveMemory(Memory * memory);

    /**
     * Load all stored memories from db.
     */
    bool LoadMemory(iResultRow& row);

    /**
     * Forget privat memories. Should be called when npc die.
     */
    void ForgetMemories(NPC * npc);

    /**
     * Find nearest memory to a position.
     */
    Memory* FindNearestMemory(const char* name,const csVector3& pos, const iSector* sector, float range = -1.0, float *foundRange = NULL);

    /**
     * Find a random memory within range to a position.
     */
    Memory* FindRandomMemory(const char* name,const csVector3& pos, const iSector* sector, float range = -1.0, float *foundRange = NULL);

    /** Send a perception to all members of the tribe
     *
     * This will send the perception to all the members of the tribe.
     * If the maxRange has been set to something greater than 0.0
     * a range check will be applied. Only members within the range
     * from the base position and sector will be triggered.
     *
     * @param pcpt       Perception to be sent.
     * @param maxRange   If greater then 0.0 then max range apply
     * @param basePos    The base position for range checks.
     * @param baseSector The base sector for range checks.
     */
    void TriggerEvent(Perception* pcpt, float maxRange=-1.0,
                      csVector3* basePos=NULL, iSector* baseSector=NULL);
    
protected:

    /** Calculate the tribes need from a NPC */
    TribeNeed* Brain(NPC * npc);
    
    int                    id;
    csString               name;
    csArray<PID>           membersId;
    csArray<NPC*>          members;
    csArray<NPC*>          deadMembers;
    csArray<Resource>      resources;

    csVector3              homePos;
    float                  homeRadius;
    csString               homeSectorName;
    iSector*               homeSector;
    int                    maxSize;
    csString               wealthResourceName;
    csString               wealthResourceNick;
    csString               wealthResourceArea;
    float                  wealthResourceGrowth;
    float                  wealthResourceGrowthActive;
    int                    wealthResourceGrowthActiveLimit;
    float                  accWealthGrowth; ///< Accumelated rest of wealth growth.
    int                    reproductionCost;
    csString               wealthGatherNeed;
    TribeNeedSet*          needSet;
    csList<Memory*>        memories;
    
    csTicks                lastGrowth;
};

#endif
