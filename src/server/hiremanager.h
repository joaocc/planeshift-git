/*
 * hiremanager.h  creator <andersr@pvv.org>
 *
 * Copyright (C) 2013 Atomic Blue (info@planeshift.it, http://www.atomicblue.org) 
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
#ifndef HIRE_MANAGER_HEADER
#define HIRE_MANAGER_HEADER
//====================================================================================
// Crystal Space Includes
//====================================================================================
#include <csutil/list.h>

//====================================================================================
// Project Includes
//====================================================================================
 
//====================================================================================
// Local Includes
//====================================================================================
#include "msgmanager.h"   // Subscriber class
#include "hiresession.h"

//------------------------------------------------------------------------------------
// Forward Declarations
//------------------------------------------------------------------------------------


/** The Hire Manager will manage all aspects related to hiring of NPCs.
 *
 *  Players is allowed to Hire NPCs to do tasks for them. This can include
 *  selling of items, standing guard for the guild, etc. This manager handle
 *  the business logic related to this. For each hire a HireSession object
 *  is created to manage the details of each hire.
 */
class HireManager : public MessageManager<HireManager>
{
public:
    /** Constructor.
     */
    HireManager();

    /** Destructor.
     */
    virtual ~HireManager();
    
    /** Initialize the Hire Manager.
     *
     *  Handle all initialization that can go wrong.
     *
     *  @return True if load suceeded. False if error is fatale and server should not start.
     */
    bool Initialize();

    /** Start a new Hire Session.
     *
     *  Establish the hire session for modification in the hiring process.
     *
     *  @param owner The actor that start the process of hiring a NPC.
     *  @return True if actor was allowed to start a new hire process.
     */
    bool StartHire(gemActor* owner);

    /** Set the type of NPC to hire for a pending hire.
     *
     *  @param owner   An actor that is in progress of hiring.
     *  @param name The name of the type of NPC to hire.
     *  @param npcType The NPC Type of the type of NPC to hire.
     *  @return True if the type was set for a pending hire.
     */
    bool SetHireType(gemActor* owner, const csString& name, const csString& npcType);

    /** Set the PID of the master NPC to use when hiring for a pending hire.
     *
     *  @param owner     An actor that is in progress of hiring.
     *  @param masterNPC The master NPC PID.
     */
    bool SetHireMasterPID(gemActor* owner, PID masterPID);

    /** Confirm the hire.
     *
     *  The Hired NPC will be created and given to player.
     */
    gemActor* ConfirmHire(gemActor* owner);

    /** Release the hire.
     *
     *  Release a NPC from hire if the hiredNPC is hired by the owner.
     *  The hired NPC will be deleted. 
     *
     *  @param owner    The actor that has hired the NPC.
     *  @param hiredNPC The NPC that is to be released from hire.
     *  @return True if the NPC was released.
     */
    bool ReleaseHire(gemActor* owner, gemNPC* hiredNPC);
    
protected:
private:
    /** Check if requirments to hire is ok.
     *
     *  @param owner An actor that would like to start hire.
     *  @return True if the actor is allowed to hire.
     */
    bool AllowedToHire(gemActor* owner);

    /** Create a new hire Session.
     *
     *  @param owner The actor that start the process of hiring a NPC.
     */
    HireSession* CreateHireSession(gemActor* owner);

    /** Get a pending hire session for an actor.
     *
     *  @param owner An actor that has a pending hire.
     */
    HireSession* GetPendingHire(gemActor* owner);

    /** Remove all pending hire sessions for actor.
     */
    void RemovePendingHire(gemActor* owner);

    // Private data
    
    csList<HireSession*> hires; // List of all hire sessions in the manager.

    csHash<HireSession*,PID> pendingHires; // List of all pending hires by owner PID.
    
};

#endif
