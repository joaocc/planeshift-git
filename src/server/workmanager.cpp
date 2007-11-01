/*
* workmanager.cpp
*
* Copyright (C) 2001-2003 Atomic Blue (info@planeshift.it, http://www.atomicblue.org)
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
#include <math.h>
#include <iutil/object.h>

#include "gem.h"
#include "globals.h"
#include "netmanager.h"
#include "util/psdatabase.h"
#include "psserver.h"
#include "psserverchar.h"
#include "playergroup.h"
#include "net/messages.h"
#include "util/eventmanager.h"
#include "util/serverconsole.h"
#include "entitymanager.h"
#include "weathermanager.h"

#include "bulkobjects/psactionlocationinfo.h"
#include "bulkobjects/pscharacterloader.h"
#include "bulkobjects/pscharacter.h"
#include "bulkobjects/psglyph.h"
#include "bulkobjects/psitem.h"
#include "bulkobjects/psmerchantinfo.h"
#include "bulkobjects/psraceinfo.h"
#include "bulkobjects/pstrade.h"
#include "cachemanager.h"
#include "workmanager.h"
#include "globals.h"
#include "util/mathscript.h"

//#define DEBUG_WORKMANAGER         // debugging only

/*
 *  There are four types of work that can be done:
 *  1. Manufacturing work harvests raw materials.
 *  2. Production work creates new items from raw materials.
 *  3. Lock picking opens locked containers.
 *  4. Cleanup work removes the ownership of abandoned items.
 *
 *  Go to each section of the code for a full description of each process.
**/


//  To add a new constraint you need to:
//    put a reference to the new constraint function in this struct with constraint string and player message
//    declare a new constraint function in the header like: static bool constraintNew();
//    code the constaint function elsewhere in this module
const constraint constraints[] =
{
    // Time of day.
    // Parameter: hh; where hh is hour of 24 hour clock.
    // Example: TIME(12) is noon.
    {psWorkManager::constraintTime, "TIME", "You can not do this work at this time of the day!"},

    // People in area.
    // Parameter: n,r; where n is number of people and r is the range.
    // Example: FRIENDS(6,4) is six people within 4.
    {psWorkManager::constraintFriends, "FRIENDS", "You need more people for this work!"},

    // Location of player.
    // Parameter: x,y,z,r; where x is x-coord, y is y-coord, z is z-coord, and r is rotation.
    // Example: LOCATION(-10.53,176.36,,) is at [-10.53,176.36] any hight and any direction.
    {psWorkManager::constraintLocation, "LOCATION","You can not do this work here!"},

    // Player mode.
    // Parameter: mode; where mode is psCharacter mode string.
    // Example: MODE(sitting) is player needs to be sitting as work is started.
    {psWorkManager::constraintMode, "MODE","You are not in the right position to complete this work!"},

    // Array end.
    {NULL, "", ""}
    

};

//-----------------------------------------------------------------------------

psWorkManager::psWorkManager()
{
    currentQuality = 1.00;

    psserver->GetEventManager()->Subscribe(this,MSGTYPE_WORKCMD,REQUIRE_READY_CLIENT|REQUIRE_ALIVE);
    psserver->GetEventManager()->Subscribe(this,MSGTYPE_LOCKPICK,REQUIRE_READY_CLIENT|REQUIRE_ALIVE|REQUIRE_TARGET);

    script_engine = psserver->GetMathScriptEngine();
    calc_repair_time   = script_engine->FindScript("Calculate Repair Time");
    calc_repair_result = script_engine->FindScript("Calculate Repair Result");
    calc_mining_chance = script_engine->FindScript("Calculate Mining Odds");

    if (!calc_repair_time)
    {
        Error1("Could not find mathscript 'Calculate Repair Time' in rpgrules.xml");
    }
    else
    {
        var_time_Worker = calc_repair_time->GetVar("Worker");
        var_time_Object = calc_repair_time->GetVar("Object");
        var_time_Result = calc_repair_time->GetVar("Result");

        if (!var_time_Worker || !var_time_Object)
        {
            Error1("Either var 'Worker', 'Object' or 'Result' is missing from Calculate Repair Time script in rpgrules.xml.");
        }
    }

    if (!calc_repair_result)
    {
        Error1("Could not find mathscript 'Calculate Repair Result' in rpgrules.xml");
    }
    else
    {
        var_result_Worker = calc_repair_result->GetVar("Worker");
        var_result_Object = calc_repair_result->GetVar("Object");
        var_result_Result = calc_repair_result->GetVar("Result");

        if (!var_result_Worker || !var_result_Object || !var_result_Result)
        {
            Error1("Either var 'Worker', 'Object' or 'Result' is missing from Calculate Repair Result script in rpgrules.xml.");
        }
    }

	if (!calc_mining_chance)
	{
		Error1("Could not find mathscript 'Calculate Mining Odds' in rpgrules");
	}
	else
	{
		var_mining_distance = calc_mining_chance->GetOrCreateVar("Distance");
		var_mining_probability = calc_mining_chance->GetOrCreateVar("Probability");
		var_mining_quality = calc_mining_chance->GetOrCreateVar("Quality");
		var_mining_skill = calc_mining_chance->GetOrCreateVar("Skill");
		var_mining_total = calc_mining_chance->GetVar("Total");
	}

    Initialize();
};


psWorkManager::~psWorkManager()
{
    if (psserver->GetEventManager())
    {
        psserver->GetEventManager()->Unsubscribe(this,MSGTYPE_WORKCMD);
        psserver->GetEventManager()->Unsubscribe(this,MSGTYPE_LOCKPICK);
    }
}

void psWorkManager::Initialize()
{
    Result res(db->Select("select * from natural_resources"));

    if (res.IsValid())
    {
        for (unsigned int i=0; i<res.Count(); i++)
        {
            NaturalResource *nr = new NaturalResource;

            nr->sector = res[i].GetInt("loc_sector_id");
            nr->loc.x  = res[i].GetFloat("loc_x");
            nr->loc.y  = res[i].GetFloat("loc_y");
            nr->loc.z  = res[i].GetFloat("loc_z");
            nr->radius = res[i].GetFloat("radius");
            nr->visible_radius = res[i].GetFloat("visible_radius");
            nr->probability = res[i].GetFloat("probability");
            nr->skill = CacheManager::GetSingleton().GetSkillByID( res[i].GetInt("skill") );
            nr->skill_level = res[i].GetInt("skill_level");
            nr->item_cat_id = res[i].GetUInt32("item_cat_id");
            nr->item_quality = res[i].GetFloat("item_quality");
            nr->anim = res[i]["animation"];
            nr->anim_duration_seconds = res[i].GetInt("anim_duration_seconds");
            nr->reward = res[i].GetInt("item_id_reward");
            nr->reward_nickname = res[i]["reward_nickname"];
            nr->action = res[i]["action"];

            resources.Push(nr);
        }
    }
    else
    {
        Error2("Database error loading natural_resources: %s\n",
               db->GetLastError() );
    }
}


void psWorkManager::HandleMessage(MsgEntry* me,Client *client)
{
    switch ( me->GetType() )
    {
        case MSGTYPE_WORKCMD:
        {
            psWorkCmdMessage msg(me);

            if (!msg.valid)
            {
                psserver->SendSystemError(me->clientnum,"Invalid work command.");
                return;
            }

            if (msg.command == "/use" )
            {
                HandleUse(client);
            }
            else if (msg.command == "/combine")
            {
                HandleCombine(client);
            }
            else if (msg.command == "/dig")
            {
                HandleProduction(client,"dig",msg.filter);
            }
			else if (msg.command == "/fish")
			{
				HandleProduction(client, "fish", msg.filter);
			}
            else if (msg.command == "/repair")
            {
                HandleRepair(client, msg);
            }
            break;
        }

        case MSGTYPE_LOCKPICK:
        {
            gemObject* target = client->GetTargetObject();

            // Check if target is action item
            gemActionLocation* gemAction = dynamic_cast<gemActionLocation*>(target);
            if(gemAction) {
                psActionLocation *action = gemAction->GetAction();

                // Check if the actionlocation is linked to real item
                uint32 instance_id = action->GetInstanceID();
                if (instance_id!=(uint32)-1)
                {
                    target = GEMSupervisor::GetSingleton().FindItemEntity( instance_id );
                }
            }

            // Check target gem
            if (!target)
            {
                    Error1("No gemItem target!\n");
                    return;
            }

            // Check range ignoring Y co-ordinate
            if (client->GetActor()->RangeTo(target, true) > RANGE_TO_USE)
            {
                psserver->SendSystemInfo(client->GetClientNum(),"You need to be closer to the lock to try this.");
                return;
            }

            // Get item
            psItem* item = target->GetItem();
            if ( !item )
            {
                Error1("Found gemItem but no psItem was attached!\n");
                return;
            }

            StartLockpick(client,item);
            break;
        }
    }

}

//-----------------------------------------------------------------------------
// Repair
//-----------------------------------------------------------------------------

/**
* This function handles commands like "/repair" using
* the following sequence of steps.
*
* 1) Make sure client isn't already busy digging, etc.
* 2) Check for repairable item in right hand slot
* 3) Check for required repair kit item in any inventory slot
* 4) Calculate time required for repair based on item and skill level
* 5) Calculate result after repair
* 6) Queue time event to trigger when repair is complete, if not canceled.
*
*/
void psWorkManager::HandleRepair(Client *client, psWorkCmdMessage &msg)
{
    // Make sure client isn't already busy digging, etc.
    if ( client->GetActor()->GetMode() != PSCHARACTER_MODE_PEACE )
    {
        psserver->SendSystemError(client->GetClientNum(),"You cannot repair anything because you are already busy.");
        return;
    }

    // No stamina checking yet for repairs.  Never discussed.

    // Check for repairable item in right hand slot
    int slotTarget;
    if ( msg.repairSlotName == "" )
        slotTarget = PSCHARACTER_SLOT_RIGHTHAND;
    else
        slotTarget = CacheManager::GetSingleton().slotNameHash.GetID(msg.repairSlotName);

    psItem *repairTarget = client->GetCharacterData()->Inventory().GetInventoryItem((INVENTORY_SLOT_NUMBER)slotTarget);
    if (repairTarget==NULL)
    {
        if(slotTarget == -1)
        {
            psserver->SendSystemError(client->GetClientNum(),"The Slot %s doesn't exists.", msg.repairSlotName.GetData() );
        }
        else if(msg.repairSlotName == "")
        {
            psserver->SendSystemError(client->GetClientNum(),"The Default Slot (Right Hand) is empty.", msg.repairSlotName.GetData() );
        }
        else
        {
            psserver->SendSystemError(client->GetClientNum(),"The Slot %s is empty.", msg.repairSlotName.GetData() );
        }        
        return;
    }
    if (repairTarget->GetItemQuality() >= repairTarget->GetMaxItemQuality() )
    {
        psserver->SendSystemError(client->GetClientNum(),"Your %s is in perfect condition.", repairTarget->GetName() );
        return;
    }

    // Check for required repair kit item in any inventory slot
    int toolstatid     = repairTarget->GetRequiredRepairTool();
    psItemStats *tool  = CacheManager::GetSingleton().GetBasicItemStatsByID(toolstatid);
    psItem *repairTool = NULL;
    if (tool)
    {
        size_t index = client->GetCharacterData()->Inventory().FindItemStatIndex(tool);
        if (index == SIZET_NOT_FOUND)
        {
            psserver->SendSystemError(client->GetClientNum(),"You must have a %s to repair your %s.", tool->GetName(), repairTarget->GetName() );
            return;
        }
        repairTool = client->GetCharacterData()->Inventory().GetInventoryIndexItem(index);
    }
    else
    {
         psserver->SendSystemError(client->GetClientNum(),"The %s cannot be repaired!", repairTarget->GetName() );
         return;
    }

    // Calculate if current skill is enough to repair the item
    int rankneeded = repairTarget->GetPrice().GetTotal() / 150;
    // if the item is low cost, allow people to try repair anyway
    if (repairTarget->GetPrice().GetTotal()<300)
        rankneeded = 0;
    int skillid = repairTarget->GetBaseStats()->GetCategory()->repair_skill_id;
    int repairskillrank = client->GetCharacterData()->GetSkills()->GetSkillRank(PSSKILL(skillid));
    if (repairskillrank<rankneeded)
    {
        psserver->SendSystemError(client->GetClientNum(),"This item is too complex for your current repair skill. You cannot repair it." );
        return;
    }

    // Calculate time required for repair based on item and skill level
    var_time_Object->SetObject(repairTarget);
    var_time_Worker->SetObject(client->GetCharacterData());
    calc_repair_time->Execute();
    // If time is less than 20 seconds, cap it to 20 seconds
    int repair_time = (int)var_time_Result->GetValue();
    if (repair_time < 20)
    {
        repair_time = 20;
    }
    csTicks repairDuration = (csTicks)(repair_time * 1000); // convert secs to msec

    // Calculate result after repair
    var_result_Object->SetObject(repairTarget);
    var_result_Worker->SetObject(client->GetCharacterData());
    calc_repair_result->Execute();
    float repairResult = var_result_Result->GetValue();

    // Queue time event to trigger when repair is complete, if not canceled.
    csVector3 dummy = csVector3(0,0,0);

    psWorkGameEvent *evt = new psWorkGameEvent(this, client->GetActor(),repairDuration,REPAIR,dummy,NULL,client,repairTarget,repairResult);
    psserver->GetEventManager()->Push(evt);  // wake me up when repair is done

    client->GetActor()->GetCharacterData()->SetTradeWork(evt);

    repairTarget->SetInUse(true);

    psserver->SendSystemInfo(client->GetClientNum(),"You start repairing the %s and continue for %d seconds.",repairTarget->GetName(),repairDuration/1000);
	client->GetActor()->SetMode(PSCHARACTER_MODE_WORK);
}

/**
 * This function handles the conclusion timer of when a repair is completed.
 * It is not called if the event is cancelled.  It follows the following
 * sequence of steps.
 *
 * 1) The values are all pre-calculated, so just adjust the quality of the item directly.
 * 2) Consume the repair required item, if flagged to do so.
 * 3) Notify the user.
 */
void psWorkManager::HandleRepairEvent(psWorkGameEvent* workEvent)
{
    psItem *repairTarget = workEvent->object;

	// We're done work, clear the mode
	workEvent->client->GetActor()->SetMode(PSCHARACTER_MODE_PEACE);
	repairTarget->SetInUse(false);
    // Check for presence of required tool
    int toolstatid     = repairTarget->GetRequiredRepairTool();
    psItemStats *tool  = CacheManager::GetSingleton().GetBasicItemStatsByID(toolstatid);
    psItem *repairTool = NULL;
    if (tool)
    {
        size_t index = workEvent->client->GetCharacterData()->Inventory().FindItemStatIndex(tool);
        if (index == SIZET_NOT_FOUND)
        {
            psserver->SendSystemError(workEvent->client->GetClientNum(),"You must have a %s to repair your %s.", tool->GetName(), repairTarget->GetName() );
            return;
        }
        repairTool = workEvent->client->GetCharacterData()->Inventory().GetInventoryIndexItem(index);
        if (repairTool == repairTarget)
        {
            psserver->SendSystemError(workEvent->client->GetClientNum(), "You can't use your repair tool on itself.");
            return;
        }
    }

    // Now consume the item if we need to
    if (repairTarget->GetRequiredRepairToolConsumed() && repairTool)
    {
        // Always assume 1 item is consumed, not stacked
        psItem *item = workEvent->client->GetCharacterData()->Inventory().RemoveItemID(repairTool->GetUID(),1);
        if (item)
        {
            // This message must come first because item is about to be deleted.
            psserver->SendSystemResult(workEvent->client->GetClientNum(),
                                       "You used a %s in your repair work.",
                                       item->GetName());

            CacheManager::GetSingleton().RemoveInstance(item);
            psserver->GetCharManager()->UpdateItemViews( workEvent->client->GetClientNum() );
        }
    }
    else
    {
        // TODO: Implement decay of quality of repair tool here if not consumed.
    }

    // Adjust the quality of the item
    repairTarget->SetItemQuality( repairTarget->GetItemQuality() + workEvent->repairAmount);

    // Lower the maximum quality based on the repair amount
    float newmax = repairTarget->GetMaxItemQuality() - (workEvent->repairAmount * 0.02);
    newmax = (newmax<0) ? 0 : newmax;
    repairTarget->SetMaxItemQuality(newmax);

    // If new maximum is greater then current quality reduce current quality
    if (repairTarget->GetItemQuality() > newmax)
    {
        repairTarget->SetItemQuality(newmax);
    }

    psserver->SendSystemResult(workEvent->client->GetClientNum(),
                               "You have repaired your %s to %.0f out of %.0f",
                               repairTarget->GetName(),
                               repairTarget->GetItemQuality(),
                               repairTarget->GetMaxItemQuality());

//    printf("Item quality now %1.2f\n", repairTarget->GetItemQuality() );

  // assign practice points
    int skillid = repairTarget->GetBaseStats()->GetCategory()->repair_skill_id;
    psSkillInfo *skill = CacheManager::GetSingleton().GetSkillByID((PSSKILL)skillid);
    if (skill)
    {
        workEvent->client->GetCharacterData()->GetSkills()->AddSkillPractice((PSSKILL)skillid,1);
    }
    repairTarget->Save(false);

}



//-----------------------------------------------------------------------------
// Production
//-----------------------------------------------------------------------------

/**
 * This function handles commands like "/dig for gold" using
 * the following sequence of steps:
 *
 *   Make sure client isn't already busy digging, etc.
 *   Find closest natural resource
 *   Validate category of equipped item
 *   Calculate time required
 *   Send anim and confirmation message to client
 *   Queue up game event for success
 */
void psWorkManager::HandleProduction(Client *client,const char *type,const char *reward)
{
    if ( !LoadLocalVars(client) )
    {
        return;
    }

    int mode = client->GetActor()->GetMode();

    // Make sure client isn't already busy digging, etc.
    if ( mode != PSCHARACTER_MODE_PEACE )
    {
        psserver->SendSystemError(client->GetClientNum(),"You cannot %s because you are already busy.",type);
        return;
    }

    // check stamina
    if ( !CheckStamina( client->GetCharacterData() ) )
    {
        psserver->SendSystemError(client->GetClientNum(),"You cannot %s because you are too tired.",type);
        return;
    }

    // Make sure they specified a resource type
    if (reward == NULL || !strcmp(reward,""))
    {
        psserver->SendSystemError(client->GetClientNum(),"Please specify which resource you want to %s for. (/%s <some resource>)",type,type);
        return;
    }

    csVector3 pos;

    // Make sure they are not in the same loc as the last dig.
    client->GetActor()->GetLastProductionPos(pos);
    if (SameProductionPosition(client->GetActor(), pos))
    {
        psserver->SendSystemError(client->GetClientNum(),
                                  "You cannot %s in the same place twice in a"
                                  " row.", type);
        return;
    }

    iSector *sector;

    client->GetActor()->GetPosition(pos, sector);

    NaturalResource *nr = FindNearestResource(reward,sector,pos,type);
    if (!nr)
    {
        psserver->SendSystemInfo(client->GetClientNum(),"You don't see a good place to %s.",type);
        return;
    }

    // Validate the skill
    float cur_skill = client->GetCharacterData()->GetSkills()->GetSkillRank((PSSKILL)nr->skill->id);

    // If skill=0, check if it has at least theoretical training in that skill
    if (cur_skill==0) {
        bool fullTrainingReceived = !client->GetCharacterData()->GetSkills()->GetSkill((PSSKILL)nr->skill->id)->CanTrain();
        if (fullTrainingReceived)
           cur_skill=0.7F;
    }

    if (cur_skill <= 0.0)
    {
        psserver->SendSystemInfo(client->GetClientNum(),"You don't have the skill to %s for %s.",type,reward);
        return;
    }

    // Validate category of equipped item
    psItem *item = owner->Inventory().GetInventoryItem(PSCHARACTER_SLOT_RIGHTHAND);
    if (!item || nr->item_cat_id != item->GetCategory()->id)
    {
        psserver->SendSystemError(client->GetClientNum(),"You don't have a good tool to %s with, equipped in your right hand.",type);
        return;
    }

    // Calculate time required
    int time_req = nr->anim_duration_seconds;

    // Send anim and confirmation message to client and nearby people
    psOverrideActionMessage msg(client->GetClientNum(),client->GetActor()->GetEntity()->GetID(),nr->anim,nr->anim_duration_seconds);
    psserver->GetEventManager()->Multicast(msg.msg, client->GetActor()->GetMulticastClients(),0,PROX_LIST_ANY_RANGE);
    client->GetActor()->SetMode(PSCHARACTER_MODE_WORK);

    client->GetCharacterData()->SetStaminaRegenerationWork(nr->skill->id);
    client->GetActor()->SetProductionStartPos(pos);

    // Queue up game event for success
    psWorkGameEvent *ev = new psWorkGameEvent(this,client->GetActor(),time_req*1000,PRODUCTION,pos,nr,client);
    psserver->GetEventManager()->Push(ev);  // wake me up when digging is done

    client->GetActor()->GetCharacterData()->SetTradeWork(ev);

    psserver->SendSystemInfo(client->GetClientNum(),"You start to %s.",type);
}

// Function used by super client
void psWorkManager::HandleProduction(gemActor *actor,const char *type,const char *reward)
{
    int mode = actor->GetMode();

    // Make sure client isn't already busy digging, etc.
    if ( mode != PSCHARACTER_MODE_PEACE )
    {
        // psserver->SendSystemError(client->GetClientNum(),"You cannot %s because you are already busy.",type);
        Warning3(LOG_SUPERCLIENT,"%s cannot %s because you are already busy.",actor->GetName(),type);
        return;
    }

    // check stamina
    if ( !CheckStamina( actor->GetCharacterData() ) )
    {
        // psserver->SendSystemError(client->GetClientNum(),"You cannot %s because you are too tired.",type);
        Warning3(LOG_SUPERCLIENT,"%s cannot %s because you are too tired.",actor->GetName(),type);
        return;
    }


    csVector3 pos;
    iSector *sector;

    actor->GetPosition(pos, sector);

    NaturalResource *nr = FindNearestResource(reward,sector,pos,type);
    if (!nr)
    {
        // psserver->SendSystemInfo(client->GetClientNum(),"You don't see a good place to %s.",type);
        Warning4(LOG_SUPERCLIENT,"%s don't see a good place to %s for %s.",actor->GetName(),type,reward);
        return;
    }

    // Validate the skill
    float cur_skill = actor->GetCharacterData()->GetSkills()->GetSkillRank((PSSKILL)nr->skill->id);

    // If skill=0, check if it has at least theoretical training in that skill
    if (cur_skill==0) {
        bool fullTrainingReceived = !actor->GetCharacterData()->GetSkills()->GetSkill((PSSKILL)nr->skill->id)->CanTrain();
        if (fullTrainingReceived)
           cur_skill=0.7F;
    }

    if (cur_skill <= 0.0)
    {
        //psserver->SendSystemInfo(client->GetClientNum(),"You don't have the skill to %s for %s.",type,reward);
        Warning6(LOG_SUPERCLIENT,"%s(%d) don't have the skill(%d) to %s for %s.",
                 actor->GetName(),actor->GetEntity()->GetID(),nr->skill->id,type,reward);
        return;
    }

    // Validate category of equipped item
    psItem *item = actor->GetCharacterData()->Inventory().GetInventoryItem(PSCHARACTER_SLOT_RIGHTHAND);
    if (!item || nr->item_cat_id != item->GetCategory()->id)
    {
        // psserver->SendSystemError(client->GetClientNum(),"You don't have a good tool to %s with, equipped in your right hand.",type);
        Warning3(LOG_SUPERCLIENT,"%s don't have a good tool to %s with, equipped in right hand.",actor->GetName(),type);
        return;
    }

    // Calculate time required
    int time_req = nr->anim_duration_seconds;

    // Send anim and confirmation message to client and nearby people
    psOverrideActionMessage msg(0,actor->GetEntity()->GetID(),nr->anim,nr->anim_duration_seconds);
    psserver->GetEventManager()->Multicast(msg.msg, actor->GetMulticastClients(),0,PROX_LIST_ANY_RANGE);

    actor->SetMode(PSCHARACTER_MODE_WORK);

    actor->GetCharacterData()->SetStaminaRegenerationWork(nr->skill->id);
    actor->SetProductionStartPos(pos);

    // Queue up game event for success
    psWorkGameEvent *ev = new psWorkGameEvent(this,actor,time_req*1000,PRODUCTION,pos,nr,NULL);
    psserver->GetEventManager()->Push(ev);  // wake me up when digging is done

    actor->GetCharacterData()->SetTradeWork(ev);

    //psserver->SendSystemInfo(client->GetClientNum(),"You start to %s.",type);
    Debug4(LOG_SUPERCLIENT,0,"%s start to %s for %s",actor->GetName(),type,reward);
}


bool psWorkManager::SameProductionPosition(gemActor *actor,
                                           const csVector3& startPos)
{
    csVector3 pos;
    iSector *sector;

    actor->GetPosition(pos, sector);

    return ((startPos - pos).SquaredNorm() < 1);
}

NaturalResource *psWorkManager::FindNearestResource(const char *reward,iSector *sector, csVector3& pos, const char *action)
{
    NaturalResource *nr=NULL;

    psSectorInfo *playersector= CacheManager::GetSingleton().GetSectorInfoByName(sector->QueryObject()->GetName());
    int sectorid = playersector->uid;

    Debug2(LOG_TRADE,0, "Finding nearest resource for %s\n", reward);

    float mindist = 100000;
    for (size_t i=0; i<resources.GetSize(); i++)
    {
        NaturalResource *curr=resources[i];
        if (curr->sector==sectorid)
        {
            if (reward && curr->reward_nickname.CompareNoCase( reward ) &&
                curr->action.CompareNoCase(action))
            {
                csVector3 diff = curr->loc - pos;
                float dist = diff.Norm();
                // Update nr if dist is less than radius and closer than previus nr or nr isn't found yet
                if (dist < curr->visible_radius && (dist < mindist || nr == NULL))
                {
                    mindist = dist;
                    nr = curr;
                }
            }
        }
    }

    if (nr == NULL)
        Debug2(LOG_TRADE,0, "No resource found for %s\n", reward);

    return nr;
}

void psWorkManager::HandleProductionEvent(psWorkGameEvent* workEvent)
{
    if (!workEvent->worker.IsValid()) // Worker has disconnected
        return;

    // Make sure clients are in the same loc as they started to dig.
    if (workEvent->client && !SameProductionPosition(workEvent->client->GetActor(),
                                workEvent->client->GetActor()->GetProductionStartPos()))
    {
        psserver->SendSystemError(workEvent->client->GetClientNum(),
                                  "You were unsuccessful since you moved away "
                                  "from where you started.");

        // Actor isn't working anymore.
        workEvent->worker->SetMode(PSCHARACTER_MODE_PEACE);
        return;
    }

    psCharacter *workerchar = workEvent->worker->GetCharacterData();
    psItem* tool = workerchar->Inventory().GetInventoryItem(PSCHARACTER_SLOT_RIGHTHAND);
    if (!tool || workEvent->nr->item_cat_id != tool->GetCategory()->id)
    {
        if (workEvent->client)
        {
            psserver->SendSystemInfo(workEvent->worker->GetClientID(),"You were unsuccessful since you no longer have the tool.");
        }

        workEvent->worker->SetMode(PSCHARACTER_MODE_PEACE); // Actor isn't working anymore
        return;
    }

    float roll = psserver->GetRandom();

    // Get player skill value
    float cur_skill = workerchar->GetSkills()->GetSkillRank((PSSKILL)workEvent->nr->skill->id);

    // If skill=0, check if it has at least theoretical training in that skill
    if (cur_skill==0) {
        bool fullTrainingReceived = !workerchar->GetSkills()->GetSkill((PSSKILL)workEvent->nr->skill->id)->CanTrain();
        if (fullTrainingReceived)
            cur_skill = 0.7F; // consider the skill somewhat usable
    }

    // Calculate complexity factor for skill
    float f1 = cur_skill / workEvent->nr->skill_level;
    if (f1 > 1.0) f1 = 1.0; // Clamp value 0..1

    // Calculate factor for tool quality
    float f2 = tool->GetItemQuality() / workEvent->nr->item_quality;
    if (f2 > 1.0) f2 = 1.0; // Clamp value 0..1

    // Calculate factor for distance from center of resource
    csVector3 diff = workEvent->nr->loc - workEvent->position;
    float dist = diff.Norm();
    float f3 = 1 - (dist / workEvent->nr->radius);
    if (f3 < 0.0) f3 = 0.0f; // Clamp value 0..1

	var_mining_distance->SetValue(f3);
	var_mining_probability->SetValue(workEvent->nr->probability);
	var_mining_quality->SetValue(f2);
	var_mining_skill->SetValue(f1);

	calc_mining_chance->Execute();

	float total = var_mining_total->GetValue();

    psString debug;
    debug.AppendFmt( "Probability:     %1.3f\n",workEvent->nr->probability);
    debug.AppendFmt( "Skill Factor:    %1.3f\n",f1);
    debug.AppendFmt( "Quality Factor:  %1.3f\n",f2);
    debug.AppendFmt( "Distance Factor: %1.3f\n",f3);
    debug.AppendFmt( "Total Factor:    %1.3f\n",total);
    debug.AppendFmt( "Roll:            %1.3f\n",roll);
    if (workEvent->client)
    {
        Debug1(LOG_TRADE, workEvent->client->GetClientNum(),debug.GetData());
    }
    else
    {
       Debug1(LOG_TRADE, 0 ,debug.GetData()); 
    }
    

    psCharacter *worke = workEvent->worker->GetCharacterData();
    if (roll < total)  // successful!
    {
        int ppGained =  worke->AddExperiencePoints(25);

        if (workEvent->client)
        {
            if ( ppGained > 0 )
                psserver->SendSystemInfo(workEvent->client->GetClientNum(),"You gained some experience points and a progression point!");
            else
                psserver->SendSystemInfo(workEvent->client->GetClientNum(),"You gained some experience points");
        }

        psItemStats *newitem = CacheManager::GetSingleton().GetBasicItemStatsByID(workEvent->nr->reward);
        if (!newitem)
        {
            Bug2("Natural Resource reward item #%d not found!\n",workEvent->nr->reward);
        }
        else
        {
            psItem *item = newitem->InstantiateBasicItem();

            if (!worke->Inventory().AddOrDrop(item))
            {
                Debug5(LOG_ANY,worke->GetCharacterID(),"HandleProductionEvent() could not give item of stat %u (%s) to character %u [%s])",
                    newitem->GetUID(),newitem->GetName(),worke->GetCharacterID(),worke->GetCharName());

                if (workEvent->client)
                {
                    // Assume it's full
                    psserver->SendSystemInfo(workEvent->client->GetClientNum(),"You found %s, but you can't carry anymore of it so you dropped it", newitem->GetName() );
                }
                else
                {
                    Debug5(LOG_SUPERCLIENT,0,"%s(EID: %u) found %s, but dropped it: %s",workEvent->worker->GetName(),
                           workEvent->worker->GetEntity()->GetID(), newitem->GetName(), worke->Inventory().lastError.GetDataSafe() );
                }


                worke->DropItem( item );

            }
            {
                if (workEvent->client)
                {
                    psserver->GetCharManager()->UpdateItemViews(workEvent->client->GetClientNum());
                    psserver->SendSystemResult(workEvent->client->GetClientNum(),"You got some %s!", newitem->GetName() );
                }
                else
                {
                    Debug4(LOG_SUPERCLIENT,0,"%s(EID: %u) got some %s.",workEvent->worker->GetName(),
                           workEvent->worker->GetEntity()->GetID(),newitem->GetName() );
                }
            }

            item->SetLoaded();  // Item is fully created
            item->Save(false);    // First save

            // No matter if the item could be moved to inventory reset the last position and
            // give out skills.
            csVector3 pos;
            iSector* sector;

            workEvent->worker->GetPosition(pos, sector);

            // Store the mining position for the next check.
            if (workEvent->client)
            {
                workEvent->client->GetActor()->SetLastProductionPos(pos);
            }

            // increase practice in that skill
            psSkillInfo *skill = CacheManager::GetSingleton().GetSkillByID((PSSKILL)workEvent->nr->skill->id);
            if (skill)
            {
                workerchar->GetSkills()->AddSkillPractice(PSSKILL_MINING,1);
            }
        }
    }
    else
    {
        int ppGained =  worke->AddExperiencePoints(2);

        if (workEvent->client)
        {
            if ( ppGained > 0 )
                psserver->SendSystemInfo(workEvent->worker->GetClientID(),"You gained a little experience and a progression point!");
            else
                psserver->SendSystemInfo(workEvent->worker->GetClientID(),"You gained a little experience");


            psserver->SendSystemInfo(workEvent->worker->GetClientID(),"You were not successful.");
        } else
        {
            Debug2(LOG_SUPERCLIENT,0,"%s where not successful.",workEvent->worker->GetName());
        }

    }
    workEvent->worker->SetMode(PSCHARACTER_MODE_PEACE); // Actor isn't working anymore
}


//-----------------------------------------------------------------------------
// Manufacture/cleanup
//-----------------------------------------------------------------------------

/*
*    There are four trade DB tables; trade_patterns, trade_processes, trade_transofrmations, and trade_combinations.
* The pattern data represents the intent of the work.  The patterns are divided into group patterns and the specific
* patterns that go with the design items that are placed in the mind slot.  The group patterns allow work to be associated
* with a group of patterns; This saves a lot of effort in creating individual transformations and combinations
* for similar patterns.  The process table holds all the common transform data, such as equipment, animations and skills.
* The transform table holds a list of transforms.  These are the starting item, quantity, and the result item and
* quantity. It also hold the duration of the trasform.  The combination table contains all the one to many items
* that make up a single combination.
*
*    As items are moved into and out of containers the entry functions are called to check
* if work can be done on the items.  Other entry functions are called when /combine or /use
* commands are issued.  Here are the different types of work that can be done:
*
* 1.	Command /use (targeted item) Specific types of items can be targeted and used to change items held
*       in the hand into other items.  Example:  Steel stock can be transformed into shield pieces by holding
*       hot stock in one hand and a hammer in the other while targeting an anvil.
* 2.	Command /use (targeted container) Containers can be targeted and used to change items contained into
*       other items.  Example:  Mechanical mixers can be targeted and used to mix large amounts of batter into dough.
* 3.	Command /combine (targeted container) Containers can be targeted and used to combine items into other items.
*       Example: Coal and iron ore can be combined in a foundry to make super sharp steel.
* 4.	Drag and drop Single items that are put into special auto-transformation containers can be changed into other
*       items.  Example: Heating iron ore in a forge creates iron stock.
*
*
* A set of member variables is used to guide the work process.  These are usually loaded at the start of the
*    entry functions, before validations functions are called to make sure the player meets the work requirements.
* There are four calls to IsTransformable().  The first is checking for a single transform that matches all the player
* item, tool, and skill criteria.  The second checks the same criteria looking for any possible group transform.  The
* next check is checking equipment and items for patternless transforms.  These transforms are possible without
* any item in the mind slot.  The transforms that allow any item to be transformed come next.  These are used in the
* oven for example where any item can be changed into dust.  Next the any item group transforms are checked.
* transform.  Finally the patternless any item transforms are checked.
*
* The IsTransformable() function will then loop through all the transforms associated with the current design item
*    and the current work item looking for a match.  It will check for required equipment, skills, quantities,
*    training, as well as any specific constraints made on doing the work.
* If a valid transformation is found StartTransformationEvent() is called that creates a work event and
*    returns control back.  A combination transformation causes the set of combining items into a temporary item
*    and then creates starts the transformation for the temporary item.
* Depending on the type of work being done more calls IsTransformable() might be made.  For example, when
*    issuing a /use command on an item both the right and left hand are checked for transformations.
* If no transformations are found an appropriate message is displayed to the user.
* Once the work event is fired skill points are collected and some calculations are done to determine any random
*    changes to the planned result.
* At this point the item is transformed into a different item and the user is informed of the change.
* If the transformation is caused by an auto-transform container then IsTransformable() is called again to
*    see if a second transformation needs to be started.
*
* Practice points are awarded only if skills are indicated in the transformation processes.  Experience 
*    is awarded based on the difference of the original quality of the item and the resulting item's quality.
*
*/

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Stop doing work
void psWorkManager::StopWork(Client* client, psItem* item)
{
    // Check client pointer
    if (!client)
    {
        Error1("Bad client pointer in StopWork");
        return;
    }
    // Check for bad item pointer
    if (!item)
    {
        Error1("Bad item pointer in StopWork");
        return;
    }

    // if no event for item then return
    psWorkGameEvent* curEvent = item->GetTransformationEvent();
    if( !curEvent )
    {
        return;
    }

    // Check for ending clean up event
    if (curEvent->category == CLEANUP)
    {
      Debug2(LOG_TRADE,client->GetClientNum(), "Stopping cleaning up %s item.\n", item->GetName());
      StopCleanupWork(client, item);
    }
    else
    {
        Debug3(LOG_TRADE,client->GetClientNum(), "Player id #%d stopped working on %s item.\n", worker->GetPlayerID(), item->GetName());

        // Handle all the different transformation types
        if( curEvent->GetTransformationType() == TRANSFORMTYPE_AUTO_CONTAINER )
        {
            StopAutoWork(client, item);
        }
        else
        {
            StopUseWork(client);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle /use command
void psWorkManager::HandleUse(Client *client)
{
    // Assign the memeber vars
    if ( !LoadLocalVars(client) )
    {
        return;
    }

    //Check if we are starting or stopping use
    if ( owner->GetMode() != PSCHARACTER_MODE_WORK )
    {
        StartUseWork(client);
    }
    else
    {
        StopUseWork(client);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check if possible to do some use work
void psWorkManager::StartUseWork(Client* client)
{
    // Check to see if we have everything we need to do any trade work
    if (!ValidateWork())
    {
        return;
    }

    // Check to see if we have stamina
    if (!CheckStamina( client->GetCharacterData() ))
    {
        return;
    }

    // Check to see if we have pattern
    if (!ValidateMind())
    {
        return;
    }

    // Check to see if player has stamina for work
    if (!ValidateStamina(client))
    {
        return;
    }

    // Check for any targeted item or container in hand
    if (!ValidateTarget(client))
    {
        return;
    }

    // Check if the target is container
    if ( workItem->GetIsContainer() )
    {
        // cast a gem container to iterate thru
        gemContainer *container = dynamic_cast<gemContainer*> (workItem->GetGemObject());
        if (!container)
        {
            Error1("Could not instantiate gemContainer");
            return;
        }

        // Load item array from the conatiner
        csArray<psItem*> itemArray;
        gemContainer::psContainerIterator it(container);
        while (it.HasNext())
        {
            // Only check the stuff that player owns
            psItem* item = it.Next();
            if (item->GetGuardingCharacterID() == owner->GetCharacterID())
            {
                itemArray.Push(item);
            }
        }

        // Check for too many items
        if (itemArray.GetSize() > 1 )
        {
            // Tell the player they are trying to work on too many items
            SendTransformError( clientNum, TRANSFORM_TOO_MANY_ITEMS );
            return;
        }

        // Only one item from container can be transformed at once
        if (itemArray.GetSize() == 1)
        {
            // Check to see if item can be transformed
            uint32 itemID = itemArray[0]->GetBaseStats()->GetUID();
            int count = itemArray[0]->GetStackCount();

            // Verify there is a valad transformation for the item that was dropped
            unsigned int transMatch = AnyTransform( patternId, groupPatternId, itemID, count );
            if ( transMatch == TRANSFORM_MATCH )
            {
                // Set up event for transformation
                StartTransformationEvent(
                    TRANSFORMTYPE_CONTAINER, PSCHARACTER_SLOT_NONE, count, itemArray[0]->GetItemQuality(), itemArray[0]);
                psserver->SendSystemOK(clientNum,"You start work on %d %s.", itemArray[0]->GetStackCount(), itemArray[0]->GetName());
                return;
            }
            else
            {
                // The transform could not be created so send the reason back to the user.
                SendTransformError( clientNum, transMatch, itemID, count );
                return;
            }
        }
        // nothing (owned by the player) in the container to use
        else
        {
            SendTransformError( clientNum, TRANSFORM_BAD_USE );
        }
    }

    // check for in hand use
    else
    {
        // Check if player has any transformation items in right hand
        psItem* rhand = owner->Inventory().GetInventoryItem(PSCHARACTER_SLOT_RIGHTHAND);
        if ( rhand != NULL )
        {
            // Find out if we can do a transformation on items in hand
            uint32 handId = rhand->GetBaseStats()->GetUID();
            int handCount = rhand->GetStackCount();

            // Verify there is a valad transformation for the item that was dropped
            unsigned int transMatch = AnyTransform( patternId, groupPatternId, handId, handCount );
            if ( transMatch == TRANSFORM_MATCH )
            {
                // Set up event for transformation
                StartTransformationEvent(
                    TRANSFORMTYPE_SLOT, PSCHARACTER_SLOT_RIGHTHAND, handCount, rhand->GetItemQuality(), rhand);
                psserver->SendSystemOK(clientNum,"You start work on %d %s.", rhand->GetStackCount(), rhand->GetName());
                return;
            }
            else
            {
                // The transform could not be created so send the reason back to the user.
                SendTransformError( clientNum, transMatch, handId, handCount );
                return;
            }
        }

        // Check if player has any transformation items in left hand
        psItem* lhand = owner->Inventory().GetInventoryItem(PSCHARACTER_SLOT_LEFTHAND);
        if ( lhand != NULL )
        {
            // Find out if we can do a transformation on items in hand
            uint32 handId = lhand->GetBaseStats()->GetUID();
            int handCount = lhand->GetStackCount();

            // Verify there is a valad transformation for the item that was dropped
            unsigned int transMatch = AnyTransform( patternId, groupPatternId, handId, handCount );
            if ( transMatch == TRANSFORM_MATCH )
            {
                // Set up event for transformation
                StartTransformationEvent(
                    TRANSFORMTYPE_SLOT, PSCHARACTER_SLOT_LEFTHAND, handCount, lhand->GetItemQuality(), lhand);
                psserver->SendSystemOK(clientNum,"You start work on %d %s.", lhand->GetStackCount(), lhand->GetName());
                return;
            }
            else
            {
                // The transform could not be created so send the reason back to the user.
                SendTransformError( clientNum, transMatch, handId, handCount );
            }
        }

        // Since either hand can fail to transform normally send general message if we get to here
        SendTransformError( clientNum, TRANSFORM_MISSING_ITEM );

    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Stop doing use work
void psWorkManager::StopUseWork(Client* client)
{
    // Assign the memeber vars
    if ( !LoadLocalVars(client) )
    {
        Error1("Could not load variables for client");
        return;
    }

    // Check for any targeted item or container in hand
    if ( !ValidateTarget(client))
    {
        return;
    }

    // Kill the work event if it exists
    if( worker->GetMode() == PSCHARACTER_MODE_WORK )
    {
        worker->SetMode(PSCHARACTER_MODE_PEACE);
        psserver->SendSystemOK(clientNum,"You stop working.");
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle /combine command
void psWorkManager::HandleCombine(Client *client)
{
    // Assign the memeber vars
    if ( !LoadLocalVars(client) )
    {
        return;
    }

    //Check if we are starting or stopping use
    if ( owner->GetMode() != PSCHARACTER_MODE_WORK )
    {
        StartCombineWork(client);
    }
    else
    {
        StopCombineWork(client);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check if possible to do some use work
void psWorkManager::StartCombineWork(Client* client)
{
    // Check to see if we have everything we need to do any trade work
    if (!ValidateWork())
    {
        return;
    }

    // Check to see if we have pattern
    if (!ValidateMind())
    {
        return;
    }

    // Check for any targeted item or container in hand
    if (!ValidateTarget(client))
    {
        return;
    }

    // Check if targeted item is container
    if ( workItem->GetIsContainer() )
    {
        // Find out if anything can be combined in container
        uint32 combinationId = 0;
        int combinationQty = 0;
        if ( IsContainerCombinable( combinationId, combinationQty ) )
        {
            // Transform all items in container into the combination item
            psItem* newItem = CombineContainedItem( combinationId, combinationQty, currentQuality, workItem );
            if (newItem)
            {
                // Find out if we can do a combination transformation
                unsigned int transMatch = AnyTransform( patternId, groupPatternId, combinationId, combinationQty );
                if ( transMatch == TRANSFORM_MATCH )
                {
                    // Set up event for transformation
                    if (workItem->GetCanTransform())
                    {
                        StartTransformationEvent(
                            TRANSFORMTYPE_AUTO_CONTAINER, PSCHARACTER_SLOT_NONE,
                            combinationQty, currentQuality, newItem);
                    }
                    else
                    {
                        StartTransformationEvent(
                            TRANSFORMTYPE_CONTAINER, PSCHARACTER_SLOT_NONE,
                            combinationQty, currentQuality, newItem);
                    }
                    psserver->SendSystemOK(clientNum,"You start to work on combining items.");
                }
                else
                {
                    // The transform could not be created so send "wrong" message and the reason back to the user.
                    psserver->SendSystemError(clientNum, "Something has obviously gone wrong.");
                    SendTransformError( clientNum, transMatch, combinationId, combinationQty );
                }
            }
            return;
        }
/*************
 * Note: Until equipped containers are properly supported this code will need to be left commented out.
 *
        // check for in hand containers for combinations
        else
        {
            // Find out if anything can be combined in hands
            uint32 combinationId = 0;
            int combinationQty = 0;
            if ( IsContainerCombinable( combinationId, combinationQty ) )
            {
                // Find out if we can do a combination transformation
                TradePatternMatch personal = IsTransformable( patternId, combinationId, combinationQty );
                TradePatternMatch group    = IsTransformable( groupPatternId, combinationId, combinationQty );
                if ( personal == TRANSFORM_MATCH || (groupPatternId > 0 && group==TRANSFORM_MATCH) )
                {
                    // Transform all items in container into the combination item
                    psItem* newItem = NULL;
                    CombineContainedItem( combinationId, combinationQty, currentQuality, newItem );

                    // Set up event for transformation
                    StartTransformationEvent(
                        TRANSFORMTYPE_CONTAINER, PSCHARACTER_SLOT_NONE, combinationQty, currentQuality, newItem);
                    psserver->SendSystemOK(clientNum,"You start work on %d %s!", newItem->GetStackCount(), newItem->GetName());
                    return;
                }
                else
                {
                    // The transform could not be created so send the reason back to the user.
                    if ( group !=  TRANSFORM_UNKNOWN_ITEM )
                        SendTransformError( clientNum, group );
                    else
                        SendTransformError( clientNum, personal );
                }
                return;
            }
        } */

        // otherwise send no combine message to client.
        SendTransformError( clientNum, TRANSFORM_BAD_COMBINATION );
    }

    return;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Stop doing combine work
void psWorkManager::StopCombineWork(Client* client)
{
    // Assign the memeber vars
    if ( !LoadLocalVars(client) )
    {
        Error1("Could not load variables for client");
        return;
    }

    // Check for any targeted item or container in hand
    if ( !ValidateTarget(client))
    {
        return;
    }

    // Kill the work event if it exists
    if( worker->GetMode() == PSCHARACTER_MODE_WORK )
    {
        worker->SetMode(PSCHARACTER_MODE_PEACE);
    }

    // Tell the user
    psserver->SendSystemOK(clientNum,"You stop working.");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check if possible to do some automatic work
void psWorkManager::StartAutoWork(Client* client, gemContainer* container, psItem* droppedItem, int count)
{
    // Assign the memeber vars
    if ( !LoadLocalVars(client) )
    {
        return;
    }

    if ( !droppedItem )
    {
        Error1("Bad item passed to start AutoWork.");
        return;
    }

    // Interrupt the first event of the dropped item if any to handle stacking
    psWorkGameEvent* stackEvent = droppedItem->GetTransformationEvent();
    if( stackEvent )
    {
        droppedItem->SetTransformationEvent(NULL);
        stackEvent->Interrupt();
    }

    // We know it is a container so check if it's a transform container
    if ( container->GetCanTransform() )
    {
        // Check to see if we have everything we need to do any trade work
        if (!ValidateWork())
        {
            return;
        }

        // Work item is container into which items were dropped
        workItem = container->GetItem();
        autoItem = droppedItem;
        uint32 autoID = autoItem->GetBaseStats()->GetUID();

        // Check to see if we have pattern
        if (ValidateMind())
        {
            // Verify there is a valad transformation for the item that was dropped
            unsigned int transMatch = AnyTransform( patternId, groupPatternId, autoID, count );
            switch( transMatch )
            {
                case TRANSFORM_MATCH:
                {
                    // Set up event for auto transformation
                    StartTransformationEvent(
                        TRANSFORMTYPE_AUTO_CONTAINER, PSCHARACTER_SLOT_NONE, count, autoItem->GetItemQuality(), autoItem);
                    psserver->SendSystemOK(clientNum,"You start work on %d %s.", autoItem->GetStackCount(), autoItem->GetName());
                    return;
                }
                case TRANSFORM_GARBAGE:
                {
                    // Set up event for auto transformation
                    StartTransformationEvent(
                        TRANSFORMTYPE_AUTO_CONTAINER, PSCHARACTER_SLOT_NONE, count, autoItem->GetItemQuality(), autoItem);
                    psserver->SendSystemOK(clientNum,"You are not sure what is going to happen to %d %s.", autoItem->GetStackCount(), autoItem->GetName());
                    return;
                }
                default:
                {
                    // The transform could not be created so send the reason back to the user.
                    SendTransformError( clientNum, transMatch, autoID, count );
                    break;
                }
            }
        }
        // If no transformations started go ahead and start a cleanup event
        StartCleanupEvent(TRANSFORMTYPE_AUTO_CONTAINER, client, droppedItem, client->GetActor());
        return;
    }
    // If this is a non-autotransform container go ahead and start a cleanup event
    StartCleanupEvent(TRANSFORMTYPE_CONTAINER, client, droppedItem, client->GetActor());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Stop doing automatic work
void psWorkManager::StopAutoWork(Client* client, psItem* autoItem)
{
    // Assign the memeber vars
    if ( !LoadLocalVars(client) )
    {
        Error1("Could not load variables in stopautowork");
        return;
    }

    // Check for proper autoItem
    if ( !autoItem )
    {
        Error1("StopAutoWork does not have a good autoItem pointer.");
        return;
    }

    // Stop the auto item's work event
    psWorkGameEvent* workEvent = autoItem->GetTransformationEvent();
    if (workEvent)
    {
        if( !workEvent->GetTransformation())
        {
            Error1("StopAutoWork does not have a good transformation pointer.");
        }

        // Unless it's some garbage item let players know
        if( workEvent->GetTransformation()->GetResultId() > 0)
        {
            psserver->SendSystemOK(clientNum,"You stop working on %s.", autoItem->GetName());
        }
        autoItem->SetTransformationEvent(NULL);
        workEvent->Interrupt();
    }

    return;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Returns with the result ID and quantity of the combination
//  if work item container has the correct items in the correct amounts
// Note:  This assumes that the combination items array is sorted by
//  resultId and then itemId
bool psWorkManager::IsContainerCombinable(uint32 &resultId, int &resultQty)
{
    // cast a gem container to iterate thru
    gemContainer *container = dynamic_cast<gemContainer*> (workItem->GetGemObject());
    if (!container)
    {
        Error1("Could not instantiate gemContainer");
        return false;
    }

    // Load item array from the conatiner
    csArray<psItem*> itemArray;
    gemContainer::psContainerIterator it(container);
    currentQuality = 1.00;
    while (it.HasNext())
    {
        // Only check the stuff that player owns
        psItem* item = it.Next();
        if (item->GetGuardingCharacterID() == owner->GetCharacterID())
        { 
            itemArray.Push(item);

            // While we are here find the item with the least quality
            float quality = item->GetItemQuality();
            if (currentQuality < quality)
            {
                currentQuality = quality;
            }
        }
    }

    // Check if specific combination is possible
    if( ValidateCombination(itemArray, resultId, resultQty))
        return true;

    // Check if any combination is possible
    return AnyCombination(itemArray, resultId, resultQty);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Returns with the result ID and quantity of the combination
//  if player has the correct items in the correct amounts in hand
// Note:  This assumes that the combination items array is sorted by
//  resultId and then itemId
bool psWorkManager::IsHandCombinable(uint32 &resultId, int &resultQty)
{
    // Load item array from the hand slots
    csArray<psItem*> itemArray;

    // Check if player has any items in right hand
    psItem* rhand = owner->Inventory().GetInventoryItem(PSCHARACTER_SLOT_RIGHTHAND);
    if ( rhand )
    {
        // add item to list
        itemArray.Push(rhand);
    }

    // Check if player has any containers in left hand
    psItem* lhand = owner->Inventory().GetInventoryItem(PSCHARACTER_SLOT_LEFTHAND);
    if ( lhand )
    {
        // add item to list
        itemArray.Push(lhand);
    }

    //Check if a combination is possible
    if (ValidateCombination(itemArray, resultId, resultQty))
        return true;
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Checks to see if every item in list matches every item in a valid combination
bool psWorkManager::ValidateCombination(csArray<psItem*> itemArray, uint32 &resultId, int &resultQty)
{
    // check if player owns anything in conatiner
    size_t itemCount = itemArray.GetSize();
    if (itemCount != 0)
    {
        // Get all possible combinations for that pattern
        csPDelArray<CombinationConstruction>* combArray = CacheManager::GetSingleton().FindCombinationsList(patternId);
        if (combArray == NULL)
        {
            // Check for group pattern combinations
            combArray = CacheManager::GetSingleton().FindCombinationsList(groupPatternId);
            if (combArray == NULL)
            {
                Debug3( LOG_TRADE, 0,"Failed to find any combinations in patterns %d and %d", patternId, groupPatternId );
                return false;
            }
        }

        // Go through all of the combinations looking for exact match
        resultId = 0;
        resultQty = 0;

        // Check all the possible combination in this data set
        for (size_t i=0; i<combArray->GetSize(); i++)
        {
            // Check for matching lists
            CombinationConstruction* current = combArray->Get(i);
            if( MatchCombinations(itemArray,current) )
            {
                resultId = current->resultItem;
                resultQty = current->resultQuantity;
                return true;
            }
        }
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Checks to see if every item in list is in the set of ingredients for this pattern
bool psWorkManager::AnyCombination(csArray<psItem*> itemArray, uint32 &resultId, int &resultQty)
{
    // check if player owns anything in conatiner
    size_t itemCount = itemArray.GetSize();
    if (itemCount != 0)
    {
        // Get list of unique ingredients for this pattern
        uint32 activePattern = patternId;
        csArray<uint32>* uniqueArray = CacheManager::GetSingleton().GetTradeTransUniqueByID(activePattern);
        if (uniqueArray == NULL)
        {
            // Try the group pattern
            activePattern = groupPatternId;
            csArray<uint32>* uniqueArray = CacheManager::GetSingleton().GetTradeTransUniqueByID(activePattern);
            if (uniqueArray == NULL)
            {
                return false;
            }
        }

        // Go through all of the items making sure they are ingredients
        for (size_t i=0; i<itemArray.GetSize(); i++)
        {
            psItem* item = itemArray.Get(i);
            if ( uniqueArray->Find(item->GetBaseStats()->GetUID()) == csArrayItemNotFound)
            {
                return false;
            }
        }

        // Now find the patternless transform to get results
        resultId = 0;
        resultQty = 0;

        // Get all unknow item transforms for this pattern
        csPDelArray<psTradeTransformations>* transArray = 
            CacheManager::GetSingleton().FindTransformationsList(activePattern, 0);
        if (transArray == NULL)
        {
            return false;
        }

        // Go thru list of transforms
        for (size_t j=0; j<transArray->GetSize(); j++)
        {
            // Get first transform with a 0 process ID this indicates processless any ingredient transform
            psTradeTransformations* trans = transArray->Get(j);
            if( trans->GetProcessId() == 0)
            {
                resultId = trans->GetResultId();
                resultQty = trans->GetResultQty();
                return true;
            }
        }
    }
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Checks for matching combination list to item array
bool psWorkManager::MatchCombinations(csArray<psItem*> itemArray, CombinationConstruction* current)
{
    // If the items count match then this is a possible valid combination.
    if ( itemArray.GetSize() == current->combinations.GetSize() )
    {
        // Setup two arrays for comparison
        csArray<psItem*> itemsMatched;
        csArray<psItem*> itemsLeft = itemArray;

        // Iterate over the items in the construction set looking for matches.
        for( size_t j = 0; j < current->combinations.GetSize(); j++ )
        {
            uint32 combId  = current->combinations[j]->GetItemId();
            int combMinQty = current->combinations[j]->GetMinQty();
            int combMaxQty = current->combinations[j]->GetMaxQty();

            // Iterate again over all items left in match set.
            for ( size_t z = 0; z < itemsLeft.GetSize(); z++ )
            {
                uint32 id =  itemsLeft[z]->GetCurrentStats()->GetUID();
                int stackQty = itemsLeft[z]->GetStackCount();
                if ( id ==  combId && stackQty >= combMinQty && stackQty <= combMaxQty )
                {
                    // We have matched the ids and the stack counts are in range.
                    itemsMatched.Push(itemsLeft[z]);
                    itemsLeft.DeleteIndexFast(z);
                    break;
                }
            }
        }

        // If all items matched up then we have a valid combination
        if( (itemsLeft.GetSize() == 0) && (itemsMatched.GetSize() == current->combinations.GetSize()) )
        {
            return true;
        }
    }
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Checks if any transformation is possible
//   We check the more specific transforms for matches first
unsigned int psWorkManager::AnyTransform(uint32 singlePatternId, uint32 groupPatternId, uint32 targetId, int targetQty)
{
    Debug4(LOG_TRADE, 0, "******* Checking single transforms: %u, %u, %u \n", singlePatternId,targetId,targetQty );

    // First check for specific single transform match
    unsigned int singleMatch = IsTransformable( singlePatternId, targetId, targetQty );
    if (singleMatch == TRANSFORM_MATCH)
        return TRANSFORM_MATCH;

    Debug4(LOG_TRADE, 0, "******* Checking group transforms: %u, %u, %u \n",groupPatternId, targetId, targetQty);

    // Then check for a group transform match
    unsigned int groupMatch = IsTransformable( groupPatternId, targetId, targetQty );
    if (groupMatch == TRANSFORM_MATCH)
        return TRANSFORM_MATCH;

    Debug3(LOG_TRADE, 0, "******* Checking patternless transforms: 0, %u, %u\n", targetId, targetQty );

    // Check for patternless transforms
    unsigned int lessMatch = IsTransformable( 0, targetId, targetQty );
    if (lessMatch == TRANSFORM_MATCH)
        return TRANSFORM_MATCH;

    Debug2(LOG_TRADE, 0, "******* Checking any ingredient transforms pattern: %u\n", singlePatternId );

    // Check for transforms of any ingredients
    if (IsIngredient( singlePatternId, groupPatternId, targetId ))
        return TRANSFORM_MATCH;

    Debug3(LOG_TRADE, 0, "******* Checking single any item transforms : %u, 0, %u\n",singlePatternId, targetQty);

    // Check for specific single any item transform match
    unsigned int singleGarbMatch = IsTransformable( singlePatternId, 0, targetQty );
    if (singleGarbMatch == TRANSFORM_MATCH)
        return TRANSFORM_GARBAGE;

    Debug3(LOG_TRADE, 0, "******* Checking group any item transforms : %u, 0, %u\n", groupPatternId, targetQty);

    // Then check for a group any item transform match
    unsigned int groupGarbMatch = IsTransformable( groupPatternId, 0, targetQty );
    if (groupGarbMatch == TRANSFORM_MATCH)
        return TRANSFORM_GARBAGE;

    Debug1(LOG_TRADE, 0, "******* Checking patternless any item tranforms\n");

    // If no other non-garbage failures then check for pattern-less any item transforms
    if (singleMatch == TRANSFORM_GARBAGE && groupMatch == TRANSFORM_GARBAGE && lessMatch == TRANSFORM_GARBAGE)
    {
        unsigned int lessGarbMatch = IsTransformable( 0, 0, 0 );
        if (lessGarbMatch == TRANSFORM_MATCH)
            return TRANSFORM_GARBAGE;
    }

    // Otherwise return a combination of all of them except garbage
    return (singleMatch | groupMatch | lessMatch);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Checks if a transformation is possible
unsigned int psWorkManager::IsTransformable(uint32 patternId, uint32 targetId, int targetQty)
{
    unsigned int match = TRANSFORM_GARBAGE;

    Debug4(LOG_TRADE, 0, "*** Checking tranforms for pattern(%u) and item(%u) quantity(%d) \n",patternId, targetId, targetQty);

    // Only get those with correct target item and pattern
    csPDelArray<psTradeTransformations>* transArray =
        CacheManager::GetSingleton().FindTransformationsList(patternId, targetId);
    if (transArray == NULL)
    {
        return TRANSFORM_UNKNOWN_ITEM;
    }

    // Go thru all the trasnformations and check if one is possible
    Debug2( LOG_TRADE, 0,"Found %zu transformations\n", transArray->GetSize() );
    psItemStats* workStats = workItem->GetCurrentStats();
    psTradeTransformations* transCandidate;
    psTradeProcesses* procCandidate;
    for (size_t i=0; i<transArray->GetSize(); i++)
    {
        transCandidate = transArray->Get(i);

        // Go thru all the possable processes and check if one is possible
        csArray<psTradeProcesses*>* procArray = CacheManager::GetSingleton().GetTradeProcessesByID(
            transCandidate->GetProcessId());

        // If no process for this transform then just continue on
        if (!procArray)
        {
            continue;
        }

        for (size_t j=0; j<procArray->GetSize(); j++)
        {
            procCandidate = procArray->Get(j);

#ifdef DEBUG_WORKMANAGER
            CPrintf(CON_DEBUG, "trans match attempts ID (%u):\n",transCandidate->GetId());
            uint32 a = procCandidate->GetWorkItemId();
            uint32 b = workStats->GetUID();
            CPrintf(CON_DEBUG, "\tcandidate WorkItemID (%u) == target WorkItemID (%u)\n", a,b);
            CPrintf(CON_DEBUG, "\tcandidate Quantity (%d) == target Quantity(%d)\n",
                transCandidate->GetItemQty(), targetQty);
            uint32 c = procCandidate->GetEquipementId();
            uint32 d = (owner->Inventory().GetInventoryItem(PSCHARACTER_SLOT_RIGHTHAND)) ?
                owner->Inventory().GetInventoryItem(PSCHARACTER_SLOT_RIGHTHAND)->GetCurrentStats()->GetUID() : 0;
            uint32 e = (owner->Inventory().GetInventoryItem(PSCHARACTER_SLOT_LEFTHAND)) ?
                owner->Inventory().GetInventoryItem(PSCHARACTER_SLOT_LEFTHAND)->GetCurrentStats()->GetUID() : 0;
            CPrintf(CON_DEBUG, "\tcandidate EquipID(%u) == righthandID(%u) == lefthandID(%u)\n", c,d,e);
#endif

            // Check the work item does not match
            if( procCandidate->GetWorkItemId() != workStats->GetUID() )
            {
                    match |= TRANSFORM_MISSING_ITEM;
                    continue;
            }

            // Check if any equipement is required
            uint32 equipmentId = procCandidate->GetEquipementId();
            if ( equipmentId > 0 )
            {
                // Check if required equipment is on hand
                if ( !IsOnHand( equipmentId ) )
                {
                    Debug3(LOG_TRADE, 0, "transformation bad equipment id=%u equipmentId=%u.\n", transCandidate->GetId(), equipmentId);
                    match |= TRANSFORM_MISSING_EQUIPMENT;
                    continue;
                }
            }

            // Check if we do not have the correct quantity
            int itemQty = transCandidate->GetItemQty();
            if ( itemQty != 0 && itemQty != targetQty )
            {
                match |= TRANSFORM_BAD_QUANTITY;
                continue;
            }

            // Check if the player has the sufficient training
            if ( !ValidateTraining(transCandidate, procCandidate) )
            {
                match |= TRANSFORM_BAD_TRAINING;
                continue;
            }

            // Check if the player has the correct skills
            if ( !ValidateSkills(transCandidate, procCandidate) )
            {
                match |= TRANSFORM_BAD_SKILLS;
                continue;
            }

            // Check if the transformation constraint is meet
            if ( !ValidateConstraints(transCandidate, procCandidate) )
            {
                // Player message comes from database
                match |= TRANSFORM_FAILED_CONSTRAINTS;
                continue;
            }

            // Good match
            Debug2(LOG_TRADE, 0, "Good match: id=(%u)\n", transCandidate->GetId());

            trans = transCandidate;
            process = procCandidate;
            return TRANSFORM_MATCH;
        }
    }

    // Return all the errors
    return match;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Checks if any ingredient transformation is possible
bool psWorkManager::IsIngredient(uint32 patternId, uint32 groupPatternId, uint32 targetId)
{
    Debug2(LOG_TRADE, 0, "*** Checking ingredient tranforms for pattern(%u) \n",patternId);

    // Check if ingredient is on list of unique ingredients for this pattern
    csArray<uint32>* itemArray = CacheManager::GetSingleton().GetTradeTransUniqueByID(patternId);
    if (itemArray == NULL)
    {
        csArray<uint32>* itemArray = CacheManager::GetSingleton().GetTradeTransUniqueByID(groupPatternId);
        if (itemArray == NULL)
        {
            return false;
        }
    }

    // Go thru list
    for (size_t i=0; i<itemArray->GetSize(); i++)
    {
        // Check item on ingredient list
        if( itemArray->Get(i) == targetId )
        {
            // Get all unknow item transforms for this pattern
            csPDelArray<psTradeTransformations>* transArray =
                CacheManager::GetSingleton().FindTransformationsList(patternId, 0);
            if (transArray == NULL)
            {
                return false;
            }

            // Go thru list of transforms
            for (size_t j=0; j<transArray->GetSize(); j++)
            {
                // Get first transform with a 0 process ID this indicates processless any ingredient transform
                trans = transArray->Get(j);
                process = NULL;
                if( trans->GetProcessId() == 0)
                {
                    return true;
                }
            }
        }
    }
    return false;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Do transformation
// This is called once all the transformation requirements have been verified.
void psWorkManager::StartTransformationEvent(int transType, INVENTORY_SLOT_NUMBER transSlot, int resultQty, 
                                             float resultQuality, psItem* item)
{
    // Get transformation delay
    int delay = CalculateEventDuration( trans->GetTransPoints() );

    // Let the server admin know
    Debug3(LOG_TRADE, 0, "Scheduled player id #%d to finish work in %1.1f seconds.\n", worker->GetPlayerID(), (float)delay);

    // Make sure we have an item
    if (!item)
    {
        Error1("Bad item in StartTransformationEvent().");
        return;
    }

    // kill old event - just in case
    psWorkGameEvent* oldEvent = item->GetTransformationEvent();
    if (oldEvent)
    {
        Error1("Had to kill old event in StartTransformationEvent.");
        item->SetTransformationEvent(NULL);
        oldEvent->Interrupt();
    }

    // Set event
    csVector3 pos(0,0,0);
    psWorkGameEvent* workEvent = new psWorkGameEvent(
        this, owner->GetActor(), delay*1000, MANUFACTURE, pos, NULL, worker->GetClient() );
    workEvent->SetTransformationType(transType);
    workEvent->SetTransformationSlot(transSlot);
    workEvent->SetResultQuality(resultQuality);
    workEvent->SetTransformationItem(item);
    workEvent->SetWorkItem(workItem);
    workEvent->SetResultQuantity(resultQty);
    workEvent->SetTransformation(trans);
    workEvent->SetProcess(process);
    workEvent->SetKFactor(patternKFactor);
    item->SetTransformationEvent(workEvent);

    // Send render effect to client and nearby people
    if ( process && process->GetRenderEffect().Length() > 0 )
    {
        csVector3 offset(0,0,0);
        workEvent->effectID =  CacheManager::GetSingleton().NextEffectUID();
        psEffectMessage newmsg( 0, process->GetRenderEffect(), offset, owner->GetActor()->GetEntity()->GetID(),0 ,workEvent->effectID );
        newmsg.Multicast(workEvent->multi,0,PROX_LIST_ANY_RANGE);
    }

    // If it's not an autotransformation event run the animation and setup the owner with the event
    if( transType != TRANSFORMTYPE_AUTO_CONTAINER)
    {
        // Send anim and confirmation message to client and nearby people
        if( process && process->GetAnimation() != "")
        {
            psOverrideActionMessage msg(clientNum, worker->GetEntity()->GetID(), process->GetAnimation(), delay);
            psserver->GetEventManager()->Multicast(msg.msg, worker->GetMulticastClients(), 0, PROX_LIST_ANY_RANGE);
        }

        // Setup work owner
        worker->SetMode(PSCHARACTER_MODE_WORK);
        owner->SetTradeWork(workEvent);
        if (process)
        {
            owner->SetStaminaRegenerationWork(process->GetPrimarySkillId());
        }
    }

    // Start event
    psserver->GetEventManager()->Push(workEvent);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Loads up local memeber variables
bool psWorkManager::LoadLocalVars(Client* client)
{
    if ( client == NULL )
    {
        Error1("Bad client pointer.");
        return false;
    }

    clientNum = client->GetClientNum();
    if ( clientNum == 0 )
    {
        Error1("Bad client number.");
        return false;
    }

    worker = client->GetActor();
    if ( !worker.IsValid())
    {
        Error1("Bad client GetActor pointer.");
        return false;
    }

    owner = worker->GetCharacterData();
    if ( owner == NULL )
    {
        Error1("Bad client actor character data pointer.");
        return false;
    }

    // Setup mode string pointer
    preworkModeString = owner->GetModeStr();

    // Null out everything else
    workItem = NULL;
    patternId = 0;
    groupPatternId = 0;
    patternKFactor = 0.0;
    currentQuality = 0.0;
    trans = NULL;

    return true;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check to see if client can do any trade work
bool psWorkManager::ValidateWork()
{
    // Check if not in normal mode
    if ( worker->GetMode() == PSCHARACTER_MODE_COMBAT)
    {
        psserver->SendSystemInfo(clientNum,"You can not practice your trade during combat." );
        return false;
    }
    return true;
}



/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check to see if client can do any trade work and
// that we have everything we need to do any trade work
bool psWorkManager::ValidateMind()
{
    // Check for the existance of a design item
    psItem* designitem = owner->Inventory().GetInventoryItem( PSCHARACTER_SLOT_MIND );
    if ( !designitem )
    {
        patternId = 0;
        groupPatternId = 0;
        patternKFactor = 1.0;
        return true;
    }

    // Check if there is a pattern for that design item
    psItemStats* itemStats = designitem->GetCurrentStats();
    psTradePatterns* pattern = CacheManager::GetSingleton().GetTradePatternByItemID( itemStats->GetUID() );
    if ( pattern == NULL )
    {
        Error3("ValidateWork() could not find pattern ID for item %u (%s) in mind slot",
            itemStats->GetUID(),itemStats->GetName());
        return false;
    }

    // Setup patterns
    patternId = pattern->GetId();
    groupPatternId = pattern->GetGroupPatternId();
    patternKFactor = pattern->GetKFactor();

    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check for any targeted item or container in hand
bool psWorkManager::ValidateTarget(Client* client)
{
    // Check if player has something targeted
    gemObject* target = client->GetTargetObject();

    gemActionLocation* gemAction = dynamic_cast<gemActionLocation*>(target);
    if(gemAction) {
        psActionLocation *action = gemAction->GetAction();

      // check if the actionlocation is linked to real item
      uint32 instance_id = action->GetInstanceID();
      if (instance_id==(uint32)-1)
      {
          instance_id = action->GetGemObject()->GetEntity()->GetID();
      }
      target = GEMSupervisor::GetSingleton().FindItemEntity( instance_id );
    }

    if (target)
    {
        // Make sure it's not character
        if (target->GetActorPtr())
        {
            psserver->SendSystemInfo(clientNum,"Only items can be targeted for use.");
            return false;
        }

        // Check range ignoring Y co-ordinate
        if (worker->RangeTo(target, true) > RANGE_TO_USE)
        {
            psserver->SendSystemError(clientNum,"You are not in range to use %s.",target->GetItem()->GetName());
            return false;
        }

        // Only legit items
        if (!target->GetItem())
        {
            psserver->SendSystemInfo(clientNum,"That item can not be used in this way.");
            return false;
        }

        // Otherwise assign item
        workItem = target->GetItem();
        return true;
    }

    // if nothing targeted check for containers in hand
    else
    {
        // Check if player has any containers in right hand
        psItem* rhand = owner->Inventory().GetInventoryItem(PSCHARACTER_SLOT_RIGHTHAND);
        if ( rhand )
        {
            // If it's a container it's our target
            if ( rhand->GetIsContainer() )
            {
                workItem = rhand;
                return true;
            }
        }

        // Check if player has any containers in left hand
        psItem* lhand = owner->Inventory().GetInventoryItem(PSCHARACTER_SLOT_LEFTHAND);
        if ( lhand )
        {
            // If it's a container it's our target
            if ( lhand->GetIsContainer() )
            {
                workItem = lhand;
                return true;
            }
        }

        // Nothing to do it to
        SendTransformError( clientNum, TRANSFORM_UNKNOWN_WORKITEM );
        return false;
    }
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check if client is too tired
bool psWorkManager::ValidateStamina(Client* client)
{
    // check stamina
    if ( (client->GetActor()->GetCharacterData()->GetStamina(true) <=
        ( client->GetActor()->GetCharacterData()->GetStaminaMax(true)*.1 ) )
     || (client->GetActor()->GetCharacterData()->GetStamina(false) <= 
        ( client->GetActor()->GetCharacterData()->GetStaminaMax(false)*.1 )) )
    {
        SendTransformError( clientNum, TRANSFORM_NO_STAMINA );
        return false;
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Checks if equipment is in hand
bool psWorkManager::IsOnHand( uint32 equipId )
{
    // Check right hand
    psItem* rhand = owner->Inventory().GetInventoryItem(PSCHARACTER_SLOT_RIGHTHAND);
    if ( rhand )
    {
        if ( equipId == rhand->GetCurrentStats()->GetUID() )
        {
            return true;
        }
    }

    // Check left hand
    psItem* lhand = owner->Inventory().GetInventoryItem(PSCHARACTER_SLOT_LEFTHAND);
    if ( lhand )
    {
        if ( equipId == lhand->GetCurrentStats()->GetUID() )
        {
            return true;
        }
    }
    return false;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Validates player has enough training
bool psWorkManager::ValidateTraining(psTradeTransformations* transCandidate, psTradeProcesses* processCandidate)
{
    // Check primary skill training if any skill required
    int priSkill = processCandidate->GetPrimarySkillId();
    if ( priSkill > 0 )
    {
        // If primary skill is zero, check if this skill should be trained first
        int basePriSkill = owner->GetSkills()->GetSkillRank((PSSKILL)priSkill);
        if ( basePriSkill == 0 )
        {
            if( owner->GetSkills()->GetSkill((PSSKILL)priSkill)->CanTrain() )
                return false;
        }
    }

    // Check secondary skill training if any skill required
    int secSkill = processCandidate->GetSecondarySkillId();
    if ( secSkill > 0 )
    {
        // If secondary skill is zero, check if this skill should be trained first
        int baseSecSkill = owner->GetSkills()->GetSkillRank((PSSKILL)secSkill);
        if ( baseSecSkill == 0 )
        {
            if( owner->GetSkills()->GetSkill((PSSKILL)secSkill)->CanTrain() )
                return false;
        }
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Validates player has the correct skills
bool psWorkManager::ValidateSkills(psTradeTransformations* transCandidate, psTradeProcesses* processCandidate)
{
    // Check if players primary skill levels are less then minimum for that skill
    int priSkill = processCandidate->GetPrimarySkillId();
    if ( priSkill > 0 )
    {
        int minPriSkill = processCandidate->GetMinPrimarySkill();
        int basePriSkill = owner->GetSkills()->GetSkillRank((PSSKILL)priSkill);
        if ( minPriSkill > basePriSkill )
        {
            return false;
        }
    }

    // Check if players secondary skill levels are less then minimum for that skill
    int secSkill = processCandidate->GetSecondarySkillId();
    if ( secSkill > 0 )
    {
        int minSecSkill = processCandidate->GetMinSecondarySkill();
        int baseSecSkill = owner->GetSkills()->GetSkillRank((PSSKILL)secSkill);
        if ( minSecSkill > baseSecSkill )
        {
            return false;
        }
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool psWorkManager::CheckStamina(psCharacter * owner) const
{
//todo- use factors based on the work to determine required stamina
    return ((owner->GetStamina(true) >= ( owner->GetStaminaMax(true)*.1 ) ) // physical
         && (owner->GetStamina(false) >= ( owner->GetStaminaMax(false)*.1 )) //mental
        );
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Validates player is not over skilled
bool psWorkManager::ValidateNotOverSkilled(psTradeTransformations* transCandidate, psTradeProcesses* processCandidate)
{
    // Check if players primary skill levels are less then minimum for that skill
    int priSkill = processCandidate->GetPrimarySkillId();
    if ( priSkill > 0 )
    {
        int maxPriSkill = processCandidate->GetMaxPrimarySkill();
        int basePriSkill = owner->GetSkills()->GetSkillRank((PSSKILL)priSkill);
        if ( maxPriSkill < basePriSkill )
        {
            return false;
        }
    }

    // Check if players secondary skill levels are less then minimum for that skill
    int secSkill = processCandidate->GetSecondarySkillId();
    if ( secSkill > 0 )
    {
        int maxSecSkill = processCandidate->GetMaxSecondarySkill();
        int baseSecSkill = owner->GetSkills()->GetSkillRank((PSSKILL)secSkill);
        if ( maxSecSkill < baseSecSkill )
        {
            return false;
        }
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Validate if all the transformation constraints are meet
// Note: This assumes that each of the constraints have paramaters
bool psWorkManager::ValidateConstraints(psTradeTransformations* transCandidate, psTradeProcesses* processCandidate)
{
    // Set up to go through the constraint string picking out the functions and parameters
    const char constraintSeperators[] = " \t(),";
    char constraintPtr[256];
    char* param;

    // Get a work copy of the constraint string to token up
    const char* constraintStr = processCandidate->GetConstraintString();
    strcpy(constraintPtr,constraintStr);

    // Loop through all the constraint strings
    char* funct = strtok( constraintPtr, constraintSeperators);
    while( funct != NULL)
    {
        // Save the pointer to the constraint and get the parameter string
        param = strtok( NULL, constraintSeperators);

        // Check all the constraints
        int constraintId = -1;
        while( constraints[++constraintId].constraintFunction != NULL )
        {
            // Check if the transformation constraint matches
            if( strcmp(constraints[constraintId].name,funct) == 0 )
            {

                // Call the associated function
                if ( !constraints[constraintId].constraintFunction( this, param) )
                {
                    // Send constraint specific message to client
                    psserver->SendSystemError(clientNum, constraints[constraintId].message );
                    return false;
                }
                break;
            }
        }

        // Get the next constraint
        funct = strtok( NULL, constraintSeperators);
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Calculate how long it will take to complete event
//  based on the transformation's point quanity
int psWorkManager::CalculateEventDuration(int pointQty)
{
    // Translate the points into seconds
    // ToDo: For now the point quantity is the duration
    return pointQty;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Combine transform all items in container into a new item
psItem* psWorkManager::CombineContainedItem(uint32 newId, int newQty, float itemQuality, psItem* containerItem)
{
#ifdef DEBUG_WORKMANAGER
    CPrintf(CON_DEBUG, "deleting items from container...\n");
#endif

    // cast a gem container to iterate thru
    gemContainer *container = dynamic_cast<gemContainer*> (containerItem->GetGemObject());
    if (!container)
    {
        Error1("Could not instantiate gemContainer");
        return NULL;
    }

    // Remove all items in container and destroy them
    gemContainer::psContainerIterator it(container);
    while (it.HasNext())
    {
        // Remove all items from container that player owns
        psItem* item = it.Next();
        if (item->GetGuardingCharacterID() == owner->GetCharacterID() )
        {
            // Kill any trade work started
            psWorkGameEvent* workEvent = item->GetTransformationEvent();
            if (workEvent)
            {
                item->SetTransformationEvent(NULL);
                workEvent->Interrupt();
            }

            // Remove items and delete
            it.RemoveCurrent(owner->GetActor()->GetClient() );
            if (!item->Destroy())
            {
                Error2("CombineContainedItem() could not remove old item ID #%u from database.", item->GetUID());
                return NULL;
            }
            delete item;
        }
    }

    // Create combined item
    psItem* newItem = CreateTradeItem(newId, newQty, itemQuality, containerItem, PSCHARACTER_SLOT_NONE);
    if(!newItem)
    {
        Error2("StartCombineWork() could not create new item ID #%u", newId);
        return NULL;
    }

#ifdef DEBUG_WORKMANAGER
    CPrintf(CON_DEBUG, "creating new item ok\n");
#endif

    return newItem;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Transform the one items in container into a new item
psItem* psWorkManager::TransformContainedItem(psItem* oldItem, uint32 newId, int newQty, float itemQuality)
{
    if (!oldItem)
    {
        Error1("TransformAutoContainedItem() had no item to remove." );
        return NULL;
    }

#ifdef DEBUG_WORKMANAGER
    CPrintf(CON_DEBUG, "deleting item from container...\n");
#endif

    gemContainer *container = dynamic_cast<gemContainer*> (workItem->GetGemObject());
    if (!container)
    {
        Error1("Could not instantiate gemContainer");
        return NULL;
    }

    // Remove items from container and destroy it
    unsigned int transformSlot = oldItem->GetLocInParent();
    container->RemoveFromContainer(oldItem,owner->GetActor()->GetClient() );
    if (!oldItem->Destroy())
    {
        Error2("TransformContainedItem() could not remove old item ID #%u from database", oldItem->GetUID());
        return NULL;
    }
    delete oldItem;

    // Check for consumed item
    if (newId <= 0)
        return NULL;

    // Create item and save it to item instances
    psItem* newItem = CreateTradeItem(newId, newQty, itemQuality, workItem, (INVENTORY_SLOT_NUMBER)transformSlot);
    if(!newItem)
    {
        Error2("TransformContainedItem() could not create new item ID #%u", newId);
        return NULL;
    }

#ifdef DEBUG_WORKMANAGER
    CPrintf(CON_DEBUG, "creating new item ok\n");
#endif
    return newItem;
}

/*
/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Transform all items in players hands into a new item
bool psWorkManager::TransformHandItem(uint32 newId, int newQty, float itemQuality)
{
    // Remove all items from both hands and destroy them
    psItem* rhand = owner->Inventory().GetInventoryItem(PSCHARACTER_SLOT_RIGHTHAND);
    if (!rhand)
    {
        Error1("TransformHandItem() could not find item in right hand to remove for combination");
    }
    // Remove items from slot and destroy them
    psItem *oldItem = owner->Inventory().RemoveEquipment(PSCHARACTER_SLOT_RIGHTHAND);
    oldItem->Delete();
    delete oldItem;

    psItem* lhand = owner->Inventory().GetInventoryItem(PSCHARACTER_SLOT_LEFTHAND);
    if (!lhand)
    {
        Error1("TransformHandItem() could not find item in left hand to remove for combination");
    }
    // Remove items from slot and destroy them
    oldItem = owner->Inventory().RemoveEquipment(PSCHARACTER_SLOT_LEFTHAND);
    oldItem->Delete();
    delete oldItem;

    // Create item and save it to item instances
    psItem* newItem = CreateTradeItem(newId, newQty, itemQuality);

    // Add the new item to the right hand slot
    if(newItem)
    {
        owner->Inventory().EquipIn(PSCHARACTER_SLOT_RIGHTHAND, newItem);
    }
    return true;
}
*/

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Transform all items in equipment into a new item
psItem* psWorkManager::TransformSlotItem(INVENTORY_SLOT_NUMBER slot, uint32 newId, int newQty, float itemQuality)
{
    // Remove items from slot and destroy it
    psItem *oldItem = owner->Inventory().RemoveItem(NULL,slot);
    if (!oldItem)
    {
        Error2("TransformSlotItem() could not remove item in slot #%i", slot );
        return NULL;
    }

    // Delete the old item
    if (!oldItem->Destroy())
    {
        Error2("TransformSlotItem() could not remove old item ID #%u from database", oldItem->GetUID());
        return NULL;
    }
    delete oldItem;

    // Check for consumed item
    if (newId <= 0)
        return NULL;

    // Create item
    psItem* newItem = CreateTradeItem(newId, newQty, itemQuality, NULL, slot);
    if(!newItem)
    {
        Error2("CreateTradeItem() could not create new item ID #%u", newId);
        return NULL;
    }

    // Send equip message to client
    psserver->GetCharManager()->SendInventory(clientNum);

#ifdef DEBUG_WORKMANAGER
    CPrintf(CON_DEBUG, "putting new item %s id=%u into slot number=%i\n",newItem->GetName(),newId,slot);
#endif

    return newItem;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Create the new item and return newly created item pointer
//  if no psItem* parrent specified then item is created in players equipment slot and not in container
psItem* psWorkManager::CreateTradeItem(uint32 newId, int newQty, float itemQuality, psItem* parent, INVENTORY_SLOT_NUMBER slot)
{
    Debug3( LOG_TRADE, 0,"Creating new item id(%u) quantity(%d)\n", newId, newQty );

#ifdef DEBUG_WORKMANAGER
        CPrintf(CON_DEBUG, "creating item id=%u qty=%i quality=%f\n",
            newId, newQty, itemQuality);
#endif

    // Check to make sure it's not a trasnformation that just destroys items
    if (newId > 0)
    {
        // Create a new item including stack count and the calculated quality
        psItemStats* baseStats = CacheManager::GetSingleton().GetBasicItemStatsByID( newId );
        if (!baseStats)
        {
            Error2("CreateTradeItem() could not get base states for item #%iu", newId );
            return NULL;
        }

        // Make a perminent new item
        psItem* newItem = baseStats->InstantiateBasicItem( true );
        if (!newItem)
        {
            Error3("CreateTradeItem() could not create item (%s) id #%u",
                baseStats->GetName(), newId );
            return NULL;
        }

        // Set stuff
        newItem->SetItemQuality(itemQuality);
        newItem->SetMaxItemQuality(itemQuality);            // Set the max quality the same as the inital quality.
        newItem->SetStackCount(newQty);
        newItem->SetOwningCharacter(owner);
        newItem->SetDecayResistance(0.5);

        // Put new item away in parent container
        if ( parent )
        {
            gemContainer *container = dynamic_cast<gemContainer*> (parent->GetGemObject());
            if (!container)
            {
                Error1("Could not instantiate gemContainer");
                return NULL;
            }
            if (!container->AddToContainer(newItem,owner->GetActor()->GetClient(), slot))
            {
                Error3("Bad container slot %i when trying to add item instance #%u.", slot, newItem->GetUID());
                return NULL;
            }
            parent->Save(true);
        }

        // Put new item away into equipped slot
        else if (slot != -1)
        {
            owner->Inventory().Add(newItem);
            if (!owner->Inventory().EquipItem(newItem,slot))
            {
                psSectorInfo *sectorinfo;
                float loc_x,loc_y,loc_z,loc_yrot;
                int instance;

                // Drop item into world
                owner->GetLocationInWorld(instance,sectorinfo,loc_x,loc_y,loc_z,loc_yrot);
                psserver->SendSystemError(owner->GetActor()->GetClientID(),
                                          "Item %s was dropped because your inventory is full.",
                                          newItem->GetName() );
                EntityManager::GetSingleton().MoveItemToWorld(newItem, instance, sectorinfo, 
                    loc_x, loc_y, loc_z, loc_yrot, owner, true);
                return NULL;
            }
        }

        // Set current player ID creator mark
        newItem->SetCrafterID(worker->GetPlayerID());

        // Set current player guild to creator mark
        if (owner->GetGuild())
            newItem->SetGuildID(owner->GetGuild()->id);
        else
            newItem->SetGuildID(0);

        // Get sector of container to set locations in world
        float xpos,ypos,zpos,yrot;
        psSectorInfo* sectorinfo;
        int instance;
        workItem->GetLocationInWorld(instance, &sectorinfo, xpos, ypos, zpos, yrot );
        newItem->SetLocationInWorld(instance,sectorinfo, 0.00, 0.00, 0.00, 0.00 );

#ifdef DEBUG_WORKMANAGER
        CPrintf(CON_DEBUG, "done creating item crafterID=%i guildID=%i name=%s owner=%p\n",
            worker->GetPlayerID(), worker->GetGuildID(), owner->GetCharName(), owner);
#endif
        newItem->SetLoaded();  // Item is fully created

        // TO REMOVE, JUST A TEMP PATCH TO STOP SERVER CRASHES
        // assert taken from newItem->Save(true);
        if (newItem->GetLocInParent(false) == -1 && newItem->GetOwningCharacter() && newItem->GetContainerID()==0) {
            CPrintf(CON_DEBUG, "PROBLEM ON ITEM: item UID=%i crafterID=%i guildID=%i name=%s owner=%p\n",
            newItem->GetBaseStats()->GetUID(),worker->GetPlayerID(), worker->GetGuildID(), owner->GetCharName(), owner);
            if (workItem && workItem->GetBaseStats())
                printf ("workitem=%i \n",workItem->GetBaseStats()->GetUID());
            printf ("ITEM WILL NOT BE SAVED. PLEASE CHECK THIS BUG!!!!\n");
            return NULL;
        }

        newItem->Save(true);
        return newItem;

    }
    return NULL;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constraint functions
#define PSABS(x)    ((x) < 0 ? -(x) : (x))

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constraint function to check hour of day
bool psWorkManager::constraintTime(psWorkManager* that,char* param)
{
    // Check paramater pointer
    if(!param)
        return false;

    // Get game hour in 24 hours cycle
    int curTime = psserver->GetWeatherManager()->GetCurrentTime();
    int targetTime = atoi( param );

    if ( curTime == targetTime )
    {
        return true;
    }
    return false;
}

#define FRIEND_RANGE 5
/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constraint function to check if players are near worker
//  Note: Constraint distance is limited to proximiy list.
bool psWorkManager::constraintFriends(psWorkManager* that, char* param)
{
    // Check paramater pointer
    if(!param)
        return false;

    // Count proximity player objects
    csArray< gemObject *> *targetsInRange  = that->worker->GetObjectsInRange( FRIEND_RANGE );

    size_t count = (size_t)atoi(param);
    if ( targetsInRange->GetSize() == count )
    {
        return true;
    }
    return false;
}


#define MAXDISTANCE 1
#define MAXANGLE 0.2
/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constraint function to check location
bool psWorkManager::constraintLocation(psWorkManager* that, char* param)
{
    // Get the current position of client
    csVector3 pos;
    float yrot;
    iSector* sect;
    that->worker->GetPosition( pos, yrot, sect );

    // Parse through the constraint parameters
    char* pdest;
    int ch = ',';
    pdest = strchr( param, ch );
    *pdest = '\0';
    char* xloc = param;

    // go to the next parameter
    param = pdest + 1;
    pdest = strchr( param, ch );
    *pdest = '\0';
    char* yloc = param;

    // go to the next parameter
    param = pdest + 1;
    pdest = strchr( param, ch );
    *pdest = '\0';
    char* zloc = param;

    // go to the next parameter
    param = pdest + 1;
    pdest = strchr( param, ch );
    *pdest = '\0';
    char* yrotation = param;

    // go to the next parameter
    param = pdest + 1;
    pdest = strchr( param, ch );
    *pdest = '\0';
    char* sector = param;

    // Skip if no X constraint co-ord specified
    if ( strlen(xloc) != 0 )
    {
        // Check X location
        float x = atof( xloc );
        if ( PSABS(pos.x - x) > MAXDISTANCE )
            return false;
    }

    // Skip if no Y constraint co-ord specified
    if ( strlen(yloc) != 0 )
    {
        // Check Y location
        float y = atof( yloc );
        if ( PSABS(pos.y - y) > MAXDISTANCE )
            return false;
    }

    // Skip if no Z constraint co-ord specified
    if ( strlen(zloc) != 0 )
    {
        // Check Z location
        float z = atof( zloc );
        if ( PSABS(pos.z - z) > MAXDISTANCE )
            return false;
    }

    // Skip if no Yrot constraint co-ord specified
    if ( strlen(yrotation) != 0 )
    {
        // Check Y rotation
        float r = atof( yrotation );
        if ( PSABS(yrot - r) > MAXANGLE )
            return false;
    }

    // Skip if no sector constraint name specified
    if ( strlen(sector) != 0 )
    {
        // Check sector
        if ( strcmp( sector, sect->QueryObject()->GetName()) != 0)
            return false;
    }

    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constraint function to check client mode
bool psWorkManager::constraintMode(psWorkManager* that, char* param)
{
    // Check mode string pointer
    if ( !that->preworkModeString )
        return false;

    // Check constraint mode to mode before work started
    if ( strcmp( that->preworkModeString, param) != 0 )
        return false;

    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This handles trade work transformation events
void psWorkManager::HandleWorkEvent(psWorkGameEvent* workEvent)
{
#ifdef DEBUG_WORKMANAGER
    CPrintf(CON_DEBUG, "handling work event...\n");
#endif

    // Load vars
    if ( !LoadLocalVars(workEvent->client) )
    {
        Error1("Could not load local variables in HandleWorkEvent().\n");
        owner->SetTradeWork(NULL);
        worker->SetMode(PSCHARACTER_MODE_PEACE);
        return;
    }

    // Get active transformation and then do it
    trans = workEvent->GetTransformation();
    if (!trans)
    {
        Error1("No valid transformation in HandleWorkEvent().\n");
        owner->SetTradeWork(NULL);
        worker->SetMode(PSCHARACTER_MODE_PEACE);
        return;
    }

    // Get active process
    process = workEvent->GetProcess();
    if (!process)
    {
            Error1("No valid process pointer for transaction.\n");
            owner->SetTradeWork(NULL);
            worker->SetMode(PSCHARACTER_MODE_PEACE);
            return;
    }

    // Get event data
    workItem = workEvent->GetWorkItem();
    if (!workItem)
    {
            Error1("No valid transformation item in HandleWorkEvent().\n");
            owner->SetTradeWork(NULL);
            worker->SetMode(PSCHARACTER_MODE_PEACE);
            return;
    }

    // Check for exploits before starting
    int transType = workEvent->GetTransformationType();
    if (transType != TRANSFORMTYPE_AUTO_CONTAINER)
    {
        // Check to see if player walked away from non-auto container
        gemObject* target = workItem->GetGemObject();
        if (worker->RangeTo(target, true) > RANGE_TO_USE)
        {
            psserver->SendSystemOK(clientNum,"You interrupted your work when you moved away.");
            owner->SetTradeWork(NULL);
            worker->SetMode(PSCHARACTER_MODE_PEACE);
            return;
        }
    }

    // Set and load
    workItem->SetTransformationEvent(NULL);
    currentQuality = workEvent->GetResultQuality();
    uint32 result = trans->GetResultId();
    const int originalQty = workEvent->GetResultQuantity();
    int resultQty = trans->GetResultQty();
    int itemQty = trans->GetItemQty();
    psItem* transItem = workEvent->GetTranformationItem();
    if (!transItem)
    {
        Error1("Bad transformation item in HandleWorkEvent().\n");
        owner->SetTradeWork(NULL);
        worker->SetMode(PSCHARACTER_MODE_PEACE);
        return;
    }

    // Check for item being moved from hand slots - container movement is handled by slot manager
    if (transType == TRANSFORMTYPE_SLOT)
    {
        INVENTORY_SLOT_NUMBER slot = workEvent->GetTransformationSlot();
        psItem *oldItem = owner->Inventory().GetInventoryItem(slot);
        if (oldItem != workEvent->GetTranformationItem() || (oldItem && oldItem->GetStackCount() != originalQty))
        {
            psserver->SendSystemOK(clientNum,"You interrupted your work when you moved item.");
            owner->SetTradeWork(NULL);
            worker->SetMode(PSCHARACTER_MODE_PEACE);
            return;
        }
    }

    // Check and apply the skills to quality and practice points
    float startQuality = transItem->GetItemQuality();
    if ( process )
    {
	    if ( !ApplySkills(workEvent->GetKFactor(), workEvent->GetTranformationItem()) && process->GetGarbageId() != 0 )
        {
            result = process->GetGarbageId();
            resultQty = process->GetGarbageQty();
        }
    }

    //  Check for 0 quantity transformations 
    if ( itemQty == 0 )
    {
        resultQty = workEvent->GetResultQuantity();
        itemQty = resultQty;
    }

    // Handle all the different transformation types
    switch( transType )
    {
        case TRANSFORMTYPE_SLOT:
        {
            INVENTORY_SLOT_NUMBER slot = workEvent->GetTransformationSlot();
            psItem* newItem = TransformSlotItem(slot, result, resultQty, currentQuality );
            if (!newItem)
            {
                Error1("TransformSlotItem did not create new item in HandleWorkEvent().\n");
                owner->SetTradeWork(NULL);
                worker->SetMode(PSCHARACTER_MODE_PEACE);
                return;
            }
            workEvent->SetTransformationItem(newItem);
            break;
        }

        case TRANSFORMTYPE_AUTO_CONTAINER:
        case TRANSFORMTYPE_SLOT_CONTAINER:
        case TRANSFORMTYPE_CONTAINER:
        {
            psItem* newItem = TransformContainedItem(workEvent->GetTranformationItem(), result, resultQty, currentQuality );
            if (!newItem)
            {
                Error1("TransformContainedItem did not create new item in HandleWorkEvent().\n");
                owner->SetTradeWork(NULL);
                worker->SetMode(PSCHARACTER_MODE_PEACE);
                return;
            }
            workEvent->SetTransformationItem(newItem);
            break;
        }

        case TRANSFORMTYPE_UNKNOWN:
        default:
        {
            break;
        }
    }

    // Calculate and apply experience points
    if (result > 0 && startQuality < currentQuality)
    {
        int ppGained =  owner->AddExperiencePoints(2*(int)(currentQuality-startQuality));
        if ( ppGained > 0 )
            psserver->SendSystemInfo(clientNum,"You gained some experience points and a progression point!");
        else
            psserver->SendSystemInfo(clientNum,"You gained some experience points");
        Debug2(LOG_TRADE, clientNum, "Giving experience points %f \n",currentQuality-startQuality);
    }
   
    // Let the user know the we have done something
    if (result <= 0 )
    {
        psItemStats* itemStats = CacheManager::GetSingleton().GetBasicItemStatsByID( trans->GetItemId() );
        if (itemStats)
        {
            psserver->SendSystemOK(clientNum," %i %s is gone.", itemQty, itemStats->GetName());
        }
        else
        {
            Error2("HandleWorkEvent() could not get item stats for item ID #%u.", trans->GetItemId());
            return;
        }
    }
    else
    {
        psItemStats* resultStats = CacheManager::GetSingleton().GetBasicItemStatsByID( result );
        if (resultStats)
        {
            psserver->SendSystemOK(clientNum,"You made %i %s.", resultQty, resultStats->GetName() );
        }
        else
        {
            Error2("HandleWorkEvent() could not get result item stats for item ID #%u.", result);
            return;
        }
    }

    // If there is something left lets see what other damage we can do
    if( (result > 0) )
    {
        // If it's an auto transformation keep going
        if( (transType == TRANSFORMTYPE_AUTO_CONTAINER) )
        {
            // Check to see if we have pattern
            if ( ValidateMind() )
            {
                // Check if there is another transformation possible for the item just created
                unsigned int transMatch = AnyTransform( patternId, groupPatternId, result, resultQty );
                if ( (transMatch == TRANSFORM_MATCH ) || (transMatch == TRANSFORM_GARBAGE ) )
                {
                    // Set up event for new transformation with the same setting as the old
                    StartTransformationEvent(
                        workEvent->GetTransformationType(), workEvent->GetTransformationSlot(),
                        workEvent->GetResultQuantity(), workEvent->GetResultQuality(),
                        workEvent->GetTranformationItem());
                    return;
                }
            }
        }

        // If item in any container clean it up
        if( (transType == TRANSFORMTYPE_AUTO_CONTAINER) ||
            (transType == TRANSFORMTYPE_CONTAINER) )
        {
            // If no transformations started go ahead and start a cleanup event
            psItem* newItem = workEvent->GetTranformationItem();
            if (newItem) {
                StartCleanupEvent(transType,workEvent->client, newItem, workEvent->client->GetActor());
            }
        }
    }

    // Make sure we clear out the old event and go to peace
    owner->SetTradeWork(NULL);
    worker->SetMode(PSCHARACTER_MODE_PEACE);
}

// Apply skills if any to quality and practice points
bool psWorkManager::ApplySkills(float factor, psItem* transItem)
{
    // just return for processless transforms
    if (!process)
        return true;

	// Check for a primary skill
    float startingQuality = currentQuality;
    int priSkill = process->GetPrimarySkillId();
    if ( priSkill > 0)
    {
        // Increase quality for crafted item based on if the starting quality was less then the normal quality
        float baseQuality = transItem->GetBaseStats()->GetQuality();
        if (currentQuality > baseQuality)
        {
        	// Add the transfromation items base quality to the current quality as a crafting bonus
            currentQuality = currentQuality + baseQuality;
        }
        else
        {
        	// Double the current quality as a crafting bonus
            currentQuality = currentQuality * 2;
        }

		// Get the players skill level using the transformations primary skill
		int basePriSkill = owner->GetSkills()->GetSkillRank((PSSKILL)priSkill);
        int maxPriSkill = process->GetMaxPrimarySkill();

        // Get the quality factor for this primary skill
        //  and only use it if in range.
		// This value represents what percentage of the effect of skills should be
		//  applied to the quality calculation for this transformation
        float priQualFactor = (process->GetPrimaryQualFactor())/100.00;
        if ((priQualFactor > 0.00) && (priQualFactor < 1.00))
        {
            // For quality considerations cap the base skill
            //  at the max skill for this transformation.
            int capPriSkill = ( basePriSkill > maxPriSkill ) ? maxPriSkill : basePriSkill;

            // Calculate the lack of skill as a percentage of the capped skill over the skill range.
            // Since this is a lack of skill percentage subtract it from 1.
            int minPriSkill = process->GetMinPrimarySkill();
            float priSkillLessPercent = 0;
            if((maxPriSkill-minPriSkill)>0)
            {
                priSkillLessPercent = 1 - ((float)(capPriSkill-minPriSkill)/(float)(maxPriSkill-minPriSkill));
            }

            // Calculate the effect of the quality factor for this skill by the skill level
			// Subtract it as a percentage from the current ingredient quality
			currentQuality -= startingQuality * priSkillLessPercent * priQualFactor;
        }

        // Only give primary experience to those under the max
        if ( basePriSkill < maxPriSkill )
        {
            // Get some practice in
            int priPoints = process->GetPrimaryPracticePts();
            owner->GetSkills()->AddSkillPractice( (PSSKILL)priSkill, priPoints );
            Debug3(LOG_TRADE, clientNum, "Giving practice points %d to skill %d \n",priPoints, priSkill);
        }

        // Apply the secondary skill if any
        int secSkill = process->GetSecondarySkillId();
        if ( secSkill > 0)
        {
            int baseSecSkill = owner->GetSkills()->GetSkillRank((PSSKILL)secSkill);
            int maxSecSkill = process->GetMaxSecondarySkill();

            // Get the quality factor for this secmary skill
            //  and only use it if in range.
            float secQualFactor = (process->GetSecondaryQualFactor())/100.00;
            if ((secQualFactor > 0.00) && (secQualFactor < 1.00))
            {
                // For quality considerations cap the base skill
                //  at the max skill for this transformation.
                int capSecSkill = ( baseSecSkill > maxSecSkill ) ? maxSecSkill : baseSecSkill;

                // Calculate the lack of skill as a percentage of the capped skill over the skill range.
                // Since this is a lack of skill percentage subtract it from 1.
                int minSecSkill = process->GetMinSecondarySkill();
                float secSkillLessPercent = 0;
			    if((maxSecSkill-minSecSkill) > 0)
			    {
 				    secSkillLessPercent = 1 - ((float)(capSecSkill-minSecSkill)/(float)(maxSecSkill-minSecSkill));
			    }

                // Calculate the effect of the quality factor for this skill by the skill level
                currentQuality -= startingQuality * secSkillLessPercent * secQualFactor;
            }

            // Only give secondary experience to those under the max
            if ( baseSecSkill < maxSecSkill )
            {
                // Get some practice in
                int secPoints = process->GetSecondaryPracticePts();
                owner->GetSkills()->AddSkillPractice( (PSSKILL)secSkill, secPoints );
            Debug3(LOG_TRADE, clientNum, "Giving practice points %d to skill %d \n",secPoints, secSkill);
            }
        }

	    // Randomize the final quality results
	    // We are using a logrithmic calculation so that normally there is little quality change
	    //  except at the edges of the random distribution.
	    // Use a pattern specific factor to determine the curve at the edges
        float roll = psserver->rng->Get();
        float expFactor = factor*log((1/roll)-1);
        currentQuality = currentQuality -((currentQuality*expFactor)/100);
    }

    // Adjust the final quality with the transformation quality factor
    currentQuality = currentQuality * trans->GetItemQualityPeniltyPercent();

    // Check for range
    if ( currentQuality > 999.99)
    {
        currentQuality = 999.99F;
    }

    // Fail if it's worst then worse
    else if (currentQuality < 1.0)
    {
        currentQuality = 0;
        return false;
    }
    return true;
}

void psWorkManager::SendTransformError( uint32_t clientNum, unsigned int result, uint32 curItemId, int curItemQty )
{
    csString error("");

    // There is some hierachy to the error codes
    //  for example, bad quantity is only important if no other error is found
    if (result & TRANSFORM_UNKNOWN_WORKITEM)
    {
        psserver->SendSystemError(clientNum, "Nothing has been chosen to use.");
        return;
    }
    if (result & TRANSFORM_FAILED_CONSTRAINTS)
    {
        // Message is sent by constraint logic.
        // error = "The conditions are not right for this to work.";
        return;
    }
    if (result & TRANSFORM_UNKNOWN_PATTERN)
    {
        psserver->SendSystemError(clientNum, "You are unable to think about what to do with this.");
        return;
    }
    if (result & TRANSFORM_BAD_TRAINING)
    {
        psserver->SendSystemError(clientNum, "You need some more training before you can do this sort of work.");
        return;
    }
    if (result & TRANSFORM_BAD_SKILLS)
    {
        psserver->SendSystemError(clientNum, "This is a bit beyond your skills at this moment.");
        return;
    }
    if (result & TRANSFORM_OVER_SKILLED)
    {
        psserver->SendSystemError(clientNum, "You loose interest in these simple tasks and cannot complete the work.");
        return;
    }
    if (result & TRANSFORM_NO_STAMINA)
    {
        psserver->SendSystemError(clientNum, "You are too tired to do this now.");
        return;
    }
    if (result & TRANSFORM_MISSING_EQUIPMENT)
    {
        psserver->SendSystemError(clientNum, "You do not have the correct tools for this sort of work.");
        return;
    }
    if (result & TRANSFORM_BAD_QUANTITY)
    {
        psserver->SendSystemError(clientNum, "This doesn't seem to be the right amount to do anything useful.");
        return;
    }
    if (result & TRANSFORM_BAD_USE)
    {
        psserver->SendSystemError(clientNum, "There is nothing in this container that you own.");
        return;
    }
    if (result & TRANSFORM_TOO_MANY_ITEMS)
    {
        psserver->SendSystemError(clientNum, "You can only work on one item at a time this way.");
        return;
    }
    if (result & TRANSFORM_BAD_COMBINATION)
    {
        psserver->SendSystemError(clientNum, "You do not have the right amount of the right items to do anything useful.");
        return;
    }
    if (result & TRANSFORM_MISSING_ITEM)
    {
        psserver->SendSystemError(clientNum, "You are not sure what to do with this.");
        return;
    }
    if (result & TRANSFORM_UNKNOWN_ITEM)
    {
        psItemStats* curStats = CacheManager::GetSingleton().GetBasicItemStatsByID( curItemId );
        if(curStats)
        {
            psserver->SendSystemError(clientNum, "You are not sure what to do with %d %s.", curItemQty, curStats->GetName());
        }
        return;
    }
    if (result & TRANSFORM_GONE_WRONG)
    {
        psserver->SendSystemError(clientNum, "Something has obviously gone wrong.");
        return;
    }
    if (result & TRANSFORM_GARBAGE)
    {
        psItemStats* curStats = CacheManager::GetSingleton().GetBasicItemStatsByID( curItemId );
        if(curStats)
        {
            psserver->SendSystemError(clientNum, "You are not sure what to do with this.");
        }
        return;
    }
    Error4("SendTransformError() got unknown result type #%d from item ID #%u (%d).", result, curItemId, curItemQty);
    return;
}

//-----------------------------------------------------------------------------
// Cleaning up discarded items from public containers
//-----------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Stop doing cleanup work
void psWorkManager::StopCleanupWork(Client* client, psItem* cleanItem)
{
    // Check for proper autoItem
    if ( !cleanItem )
    {
        Error1("StopCleanupWork does not have a good cleanItem pointer.");
        return;
    }

    // Stop the cleanup item's work event
    psWorkGameEvent* workEvent = cleanItem->GetTransformationEvent();
    if (workEvent)
    {
        cleanItem->SetTransformationEvent(NULL);
        workEvent->Interrupt();
    }
    return;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Start doing cleanup
void psWorkManager::StartCleanupEvent(int transType, Client* client, psItem* item, gemActor* worker)
{
    if (!item)
    {
        Error1("No valid transformation item in StartCleanupEvent().\n");
        return;
    }

#ifdef DEBUG_WORKMANAGER
    // Let the server admin know
    CPrintf(CON_DEBUG, "Scheduled item id #%d to be cleaned up in %d seconds.\n", item->GetUID(), CLEANUP_DELAY);
#endif

    // Set event
    csVector3 pos(0,0,0);
    psWorkGameEvent* workEvent = new psWorkGameEvent(
        this, worker, CLEANUP_DELAY*1000, CLEANUP, pos, NULL, client );
    workEvent->SetTransformationType(transType);
    workEvent->SetTransformationItem(item);
    item->SetTransformationEvent(workEvent);
    psserver->GetEventManager()->Push(workEvent);

    psserver->SendSystemOK(clientNum,"You probably should not leave %d %s here.", item->GetStackCount(), item->GetName());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Remove ownership of item making item free game.
void psWorkManager::HandleCleanupEvent(psWorkGameEvent* workEvent)
{
#ifdef DEBUG_WORKMANAGER
    CPrintf(CON_DEBUG, "handling cleanup workEvent...\n");
#endif

    // Get event item
    psItem* cleanItem = workEvent->GetTranformationItem();
    if ( cleanItem )
    {
        cleanItem->SetGuardingCharacterID(0);
        cleanItem->Save(true);
        cleanItem->SetTransformationEvent(NULL);
    }
}

//-----------------------------------------------------------------------------
// Lock picking
//-----------------------------------------------------------------------------

void psWorkManager::StartLockpick(Client* client,psItem* item)
{
    // Check if its a lock
    if(!item->GetIsLockable())
    {
        psserver->SendSystemInfo(client->GetClientNum(),"This object isn't lockable");
        return;
    }

    // Check if player has a key
    if (client->GetCharacterData()->Inventory().HaveKeyForLock(item->GetUID()))
    {
        bool locked = item->GetIsLocked();
        psserver->SendSystemInfo(client->GetClientNum(),locked ? "You unlocked %s" : "You locked %s", item->GetName());
        item->SetIsLocked(!locked);
        item->Save(false);
        return;
    }

    if (item->GetIsUnpickable())
    {
        psserver->SendSystemInfo(client->GetClientNum(),"This lock is impossible to open by lockpicking.");
        return;
    }

    if (!CheckStamina( client->GetCharacterData() ))
    {
        psserver->SendSystemInfo(client->GetClientNum(),"You are too tired to lockpick");
        return;
    }

    client->GetCharacterData()->SetStaminaRegenerationWork(PSSKILL_LOCKPICKING);

    psserver->SendSystemInfo(client->GetClientNum(),"You started lockpicking %s",item->GetName());
    client->GetActor()->SetMode(PSCHARACTER_MODE_WORK);

    // Execute mathscript to get lockpicking time
    MathScript* pickTime = psserver->GetMathScriptEngine()->FindScript("Lockpicking Time");
    MathScriptVar* quality = pickTime->GetVar("LockQuality");
    MathScriptVar* time = pickTime->GetVar("Time");

    quality->SetValue(item->GetItemQuality());
    pickTime->Execute();

    // Add new event
    csVector3 emptyV = csVector3(0,0,0);
    psWorkGameEvent *ev = new psWorkGameEvent(
        this,
        client->GetActor(),
        (int) time->GetValue(),
        LOCKPICKING,
        emptyV,
        0,
        client,
        item);
    psserver->GetEventManager()->Push(ev);
}

void psWorkManager::LockpickComplete(psWorkGameEvent* workEvent)
{
    psCharacter* character = workEvent->client->GetCharacterData();
    PSSKILL skill = workEvent->object->GetLockpickSkill();

    // Check if the user has the right skills
    if(character->GetSkills()->GetSkillRank(skill) >= workEvent->object->GetLockStrength())
    {
        bool locked = workEvent->object->GetIsLocked();
        psserver->SendSystemInfo(workEvent->client->GetClientNum(),locked ? "You unlocked %s!" : "You locked %s!", workEvent->object->GetName());
        workEvent->object->SetIsLocked(!locked);
        workEvent->object->Save(false);
    }
    else
    {
        // Send denied message
        psserver->SendSystemInfo(workEvent->client->GetClientNum(),"You failed your lockpicking attempt.");
    }
    workEvent->client->GetActor()->SetMode(PSCHARACTER_MODE_PEACE);
}


//-----------------------------------------------------------------------------
// Events
//-----------------------------------------------------------------------------

//////////////////////////////////////////////////////////////
//  Primary work object
psWorkGameEvent::psWorkGameEvent(psWorkManager* mgr,
                                 gemActor* worker,
                                 int delayticks,
                                 int cat,
                                 csVector3& pos,
                                 NaturalResource *natres,
                                 Client *c,
                                 psItem* object,
                                 float repairAmt)
                                 : psGameEvent(0,delayticks,"psWorkGameEvent"), effectID(0)
{
    workmanager = mgr;
    nr          = natres;
    client      = c;
    position    = pos;
    category    = cat;
    this->object = object;
    this->worker = worker;
    worker->RegisterCallback(this);
    repairAmount = repairAmt;
}


void psWorkGameEvent::Interrupt()
{
    // Setting the character mode ends up calling this function again, so we have to check for reentrancy here
    if (workmanager)
    {
        workmanager = NULL;

        // TODO: get tick count so far before killing event
        //  for partial trade work - store it in psItem
        //  leave work state alone so it can be restarted

        // Stop event from being executed when triggered.
	    if(category == REPAIR && client->GetActor() && (client->GetActor()->GetMode() == PSCHARACTER_MODE_WORK))
        {
	       client->GetActor()->SetMode(PSCHARACTER_MODE_PEACE);
           
            psItem *repairTarget = client->GetCharacterData()->Inventory().GetInventoryItem(PSCHARACTER_SLOT_RIGHTHAND);
            if (repairTarget)
                repairTarget->SetInUse(false);
        }
    }
}


// Event trigger
void psWorkGameEvent::Trigger()
{
    if(workmanager && worker.IsValid())
    {

        if ( effectID != 0 )
        {
            psStopEffectMessage newmsg(effectID);
            newmsg.Multicast(multi,0,PROX_LIST_ANY_RANGE);
        }


        // Work done for now
        switch(category)
        {
            case(REPAIR):
            {
                workmanager->HandleRepairEvent(this);
                break;
            }
            case(MANUFACTURE):
            {
                workmanager->HandleWorkEvent(this);
                break;
            }
            case(PRODUCTION):
            {
                workmanager->HandleProductionEvent(this);
                break;
            }
            case(LOCKPICKING):
            {
                workmanager->LockpickComplete(this);
                break;
            }
            case(CLEANUP):
            {
                workmanager->HandleCleanupEvent(this);
                break;
            }
            default:
                Error1("Unknown transformation type!");
                break;
        }
    }
}

void psWorkGameEvent::DeleteObjectCallback(iDeleteNotificationObject * object)
{
    if(workmanager && worker.IsValid())
    {
        worker->UnregisterCallback(this);

        // Cleanup any autoitem associated with event
        if( transType == TRANSFORMTYPE_AUTO_CONTAINER )
        {
            if( item )
            {

#ifdef DEBUG_WORKMANAGER
CPrintf(CON_DEBUG, "Cleaning up item from auto-transform container...\n");
#endif
                // clear out event
                item->SetTransformationEvent(NULL);

            }
            Interrupt();
        }
        else
        {
            // Setting character to peace will interrupt work event
            worker->GetCharacterData()->SetTradeWork(NULL);
            worker->SetMode(PSCHARACTER_MODE_PEACE);
        }
    }
}

psWorkGameEvent::~psWorkGameEvent()
{
    if(worker.IsValid())
    {
        worker->UnregisterCallback(this);
        if (worker->GetCharacterData()->GetTradeWork() == this)
            worker->GetCharacterData()->SetTradeWork(NULL);
    }

    // For manufactuing events only
    if( category == MANUFACTURE )
    {
        // Remove any item reference to this  event
        psItem* transItem = GetTranformationItem();
        if( transItem && transItem->GetTransformationEvent() == this)
            transItem->SetTransformationEvent(NULL);

        // The garbase transformation is not cached and needs to be cleaned up
        if(transformation && !transformation->GetTransformationCacheFlag() )
        {
            delete transformation;
        }
    }
}
