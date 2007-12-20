/*
 * combatmanager.cpp
 *
 * Coptright (C) 2001-2002 Atomic Blue (info@planeshift.it, http://www.atomicblue.org) 
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
#include <csutil/xmltiny.h>

#include <physicallayer/entity.h>
#include <propclass/mesh.h>

//=============================================================================
// Project Includes
//=============================================================================
#include "net/msghandler.h"
#include "net/messages.h"

#include "util/eventmanager.h"
#include "util/location.h"
#include "util/mathscript.h"
#include "util/serverconsole.h"

#include "bulkobjects/psitem.h"

//=============================================================================
// Local Includes
//=============================================================================
#include "psserver.h"
#include "playergroup.h"
#include "events.h"
#include "gem.h"
#include "entitymanager.h"
#include "psproxlist.h"
#include "spawnmanager.h"
#include "progressionmanager.h"
#include "groupmanager.h"
#include "npcmanager.h"
#include "combatmanager.h"
#include "netmanager.h"
#include "client.h"
#include "globals.h"

/// This #define determines how far away people will get detailed combat events.
#define MAX_COMBAT_EVENT_RANGE 30

/**
 * When a player or NPC attacks someone, the first combat is queued.  When
 * that event is processed, if appropriate at the end, the next combat event
 * is immediately created and added to the schedule to be triggered at the
 * appropriate time.
 *
 * Note that this event is just a hint that a combat event might occur at this
 * time.  Other events (like equiping items, removing items, spell effects, etc)
 * may alter this time.
 *
 * When a combat event fires, the first task is to check wether the action can actually
 * occur at this time.  If it cannot, the event should not create another event since
 * the action that caused the "delay" or "acceleration" change should have created its 
 * own event.
 *
 * TODO: psGEMEvent makes this depend only on the attacker when in fact
 * this event depends on both attacker and target being present at the time the
 * event fires.
 */
class psCombatGameEvent : public psGameEvent
{
public:

    csWeakRef<gemObject>  attacker;  ///< Entity who instigated this attack
    csWeakRef<gemObject>  target;    ///< Entity who is target of this attack
    psCharacter *attackerdata;
    psCharacter *targetdata;
    int TargetCID;                   ///< ClientID of target
    int AttackerCID;                 ///< ClientID of attacker
    INVENTORY_SLOT_NUMBER WeaponSlot; ///< Identifier of the slot for which this attack event should process
    uint32 WeaponID;                 ///< UID of the weapon used for this attack event
    float AttackValue;               ///< Measure of quality of attack ability
    float AttackRoll;                ///< Randomized attack value.  Used for
    float DefenseRoll;               ///< Randomized defense effectiveness this event
    float DodgeValue;                ///< Measure of quality of dodge ability of target
    float BlockValue;                ///< Quality of blocking ability of all weapons target has
    float CounterBlockValue;         ///< Dificulty of blocking attacking weapon
    float QualityOfHit;              ///< How much of attack remains after shield protection
    float BaseHitDamage;             ///< Hit damage without taking armor into account.
    INVENTORY_SLOT_NUMBER AttackLocation;  ///< Which slot should we check the armor of?
    float ArmorDamageAdjustment;     ///< How much does armor in the struck spot help?
    float FinalBaseDamage;           ///< Resulting damage after armor adjustment
    float DamageMods;                ///< Magnifiers on damage for magic effects, etc.
    float FinalDamage;               ///< Final damage applied to target

    int   AttackResult;              ///< Code indicating the result of the attack attempt
    int   PreviousAttackResult;      ///< The code of the previous result of the attack attempt

    psCombatGameEvent(psCombatManager *mgr,
                      int delayticks,
                      int action,
                      gemObject *attacker,
                      INVENTORY_SLOT_NUMBER weaponslot,
                      uint32 weaponID,
                      gemObject *target,
                      int attackerCID,
                      int targetCID,
                      int previousResult);
    ~psCombatGameEvent();                      

    virtual bool CheckTrigger();
    virtual void Trigger();  // Abstract event processing function

    gemObject* GetTarget()
    {
        return target;
    };
    gemObject* GetAttacker()
    {
        return attacker;
    };
    psCharacter *GetTargetData()
    {
        return targetdata;
    };
    psCharacter *GetAttackerData()
    {
        return attackerdata;
    };
    INVENTORY_SLOT_NUMBER GetWeaponSlot()
    {
        return WeaponSlot;
    };
    
//    virtual void Disconnecting(void * object);
    
    int GetTargetID()               { return TargetCID; };
    int GetAttackerID()             { return AttackerCID; };
    int GetAttackResult()           { return AttackResult; };

protected:
    psCombatManager *combatmanager;
    int action;    
};

psCombatManager::psCombatManager() : pvp_region(NULL)
{
    randomgen = psserver->rng;
    var_IAH       = NULL;         
    var_AHR       = NULL;         
    var_Blocked   = NULL;
    var_QOH       = NULL;
    var_FinalDmg  = NULL;
  
    script_engine = psserver->GetMathScriptEngine();
    calc_damage   = script_engine->FindScript("Calculate Damage");
    if ( !calc_damage )
    {
        Error1("Calculate Damage Script could not be found. Check rpgrules.xml");            
    }
    else
    {
        // Output var bindings here
        var_IAH       = calc_damage->GetVar("IAH");
        var_AHR       = calc_damage->GetVar("AHR");
        var_Blocked   = calc_damage->GetVar("Blocked");
        var_QOH       = calc_damage->GetVar("QOH");
        var_FinalDmg  = calc_damage->GetVar("FinalDamage");
  
        if ( !(var_IAH && var_AHR && var_Blocked && var_QOH && var_FinalDmg) )
        {
            Error1("One or more combat output binding variables is incorrect");
            Error2("    IAH = %p", var_IAH);
            Error2("    AHR = %p", var_AHR);
            Error2("    Blocked = %p", var_Blocked);
            Error2("    QOH = %p", var_QOH);
            Error2("    FinalDamage = %p", var_FinalDmg);
            Error1("Check rpgrules.xml to make sure these variables exist in 'Calculate Damage' script");
        } 

        // Input var bindings here
        var_AttackWeapon       = calc_damage->GetVar("AttackWeapon");
        //var_AttackWeaponSecondary  = calc_damage->GetVar("AttackWeaponSecondary"); TODO
        var_TargetAttackWeapon      = calc_damage->GetVar("TargetAttackWeapon");
        //var_DefenseWeaponSecondary = calc_damage->GetVar("DefenseWeaponSecondary"); TODO
        var_Target                 = calc_damage->GetVar("Target");
        var_Attacker               = calc_damage->GetVar("Attacker");
        var_AttackLocationItem     = calc_damage->GetVar("AttackLocationItem");

        targetLocations.Push(PSCHARACTER_SLOT_HEAD);
        targetLocations.Push(PSCHARACTER_SLOT_TORSO);
        targetLocations.Push(PSCHARACTER_SLOT_ARMS);
        targetLocations.Push(PSCHARACTER_SLOT_GLOVES);
        targetLocations.Push(PSCHARACTER_SLOT_LEGS);
        targetLocations.Push(PSCHARACTER_SLOT_BOOTS);
    } 

    psserver->GetEventManager()->Subscribe(this,MSGTYPE_DEATH_EVENT,NO_VALIDATION);
}

psCombatManager::~psCombatManager()
{
    psserver->GetEventManager()->Unsubscribe(this,MSGTYPE_DEATH_EVENT);
    if (pvp_region) 
    {
        delete pvp_region;
        pvp_region = NULL;
    }
}

bool psCombatManager::InitializePVP()
{
    Result rs(db->Select("select * from sc_location_type where name = 'pvp_region'"));

    if (!rs.IsValid())
    {
        Error2("Could not load locations from db: %s",db->GetLastError() );
        return false;
    }

    // no PVP defined
    if (rs.Count() == 0)
    {
      return true;
    }

    if (rs.Count() > 1)
    {
        Error1("More than one pvp_region defined!");
        return false;
    }

    LocationType *loctype = new LocationType();
    
    if (loctype->Load(rs[0],NULL,db))
    {
        pvp_region = loctype;
    }
    else
    {
        Error2("Could not load location: %s",db->GetLastError() );            
        delete loctype;
        return false;
    }
    return true;
}

bool psCombatManager::InPVPRegion(csVector3& pos,iSector * sector)
{
    if (pvp_region->CheckWithinBounds(EntityManager::GetSingleton().GetEngine(), pos, sector))
        return true;

    return false;
}

void psCombatManager::AttackSomeone(gemActor *attacker,gemObject *target,Stance stance)
{
    psCharacter *attacker_character = attacker->GetCharacterData();

    if (attacker_character->GetMode() == PSCHARACTER_MODE_DEFEATED)
        return;

    if (attacker_character->GetMode() == PSCHARACTER_MODE_COMBAT)  // Already fighting
    {
        SetCombat(attacker,stance);  // switch stance from Bloody to Defensive, etc.
        return;
    } else {
        attacker_character->ResetSwings(csGetTicks());
    }

    // Indicator of whether any weapons are available to attack with
    bool startedAttacking=false;
    bool haveWeapon=false;

    // Step through each current slot and queue events for all that can attack
    for (int slot=0; slot<PSCHARACTER_SLOT_BULK1; slot++)
    {
        // See if this slot is able to attack
        if (attacker_character->Inventory().CanItemAttack((INVENTORY_SLOT_NUMBER) slot))
        {
            INVENTORY_SLOT_NUMBER weaponSlot = (INVENTORY_SLOT_NUMBER) slot;
            // Get the data for the "weapon" that is used in this slot
            psItem *weapon=attacker_character->Inventory().GetEffectiveWeaponInSlot(weaponSlot);

            csString response;
            if (weapon!=NULL && weapon->CheckRequirements(attacker_character,response) )
            {
              haveWeapon = true;
              Debug5(LOG_COMBAT,attacker->GetClientID(),"%s tries to attack with %s weapon %s at %.2f range",
                attacker->GetName(),(weapon->GetIsRangeWeapon()?"range":"melee"),weapon->GetName(),attacker->RangeTo(target,false));
              Debug3(LOG_COMBAT,attacker->GetClientID(),"%s started attacking with %s",attacker->GetName(),weapon->GetName())

                // start the ball rolling
                QueueNextEvent(attacker,weaponSlot,target,attacker->GetClientID(),target->GetClientID());  

              startedAttacking=true;
            }
            else
            {
                if( weapon  && attacker_character->GetActor())
                {
                    Debug3(LOG_COMBAT,attacker->GetClientID(),"%s tried attacking with %s but can't use it.",attacker->GetName(),weapon->GetName())
                    psserver->SendSystemError(attacker_character->GetActor()->GetClientID(), response);
                } 
            }
        }
    }

    /* Only notify the target if any attacks were able to start.  Otherwise there are
     * no available weapons with which to attack.
     */
    if (haveWeapon)
    {
        if (startedAttacking)
        {
            // The attacker should now enter combat mode
            if (attacker->GetMode() != PSCHARACTER_MODE_COMBAT)
            {
                SetCombat(attacker,stance);
            }
        }
        else
        {
            psserver->SendSystemError(attacker->GetClientID(),"You are too far away to attack!");
            return;
        }
    }
    else
    {
        psserver->SendSystemError(attacker->GetClientID(),"You have no weapons equipped!");
        return;
    }
}

void psCombatManager::SetCombat(gemActor *combatant, Stance stance)
{
    // Sanity check
    if (!combatant || !combatant->GetCharacterData() || !combatant->IsAlive())
        return;

    // Change stance if new and for a player (NPCs don't have different stances)
    if (combatant->GetClientID() && combatant->GetCharacterData()->GetCombatStance().stance_id != stance.stance_id)
    {
        psSystemMessage msg(combatant->GetClientID(), MSG_COMBAT_STANCE, "%s changes to a %s stance", combatant->GetName(), stance.stance_name.GetData() );
        msg.Multicast(combatant->GetMulticastClients(),0,MAX_COMBAT_EVENT_RANGE);

        combatant->GetCharacterData()->SetCombatStance(stance);
    }

    combatant->SetMode(PSCHARACTER_MODE_COMBAT); // Set mode and multicast new mode and/or stance
    Debug3(LOG_COMBAT,combatant->GetClientID(), "%s starts attacking with stance %s", combatant->GetName(), stance.stance_name.GetData());
}

void psCombatManager::StopAttack(gemActor *attacker)
{
    if (!attacker)
        return;

    // TODO: I'm not sure this is a wise idea after all...spells may not be offensive...
    switch (attacker->GetMode())
    {
        case PSCHARACTER_MODE_SPELL_CASTING:
            attacker->GetCharacterData()->InterruptSpellCasting();
            break;
        case PSCHARACTER_MODE_COMBAT:
            attacker->SetMode(PSCHARACTER_MODE_PEACE);
            break;
        default:
            return;
    }

    Debug2(LOG_COMBAT, attacker->GetClientID(), "%s stops attacking", attacker->GetName());
}

void psCombatManager::NotifyTarget(gemActor *attacker,gemObject *target)
{
    // Queue Attack percetion to npc clients
    gemNPC *targetnpc = dynamic_cast<gemNPC *>(target);
    if (targetnpc)
        psserver->npcmanager->QueueAttackPerception(attacker,targetnpc);

    // Interrupt spell casting
    //gemActor *targetactor = dynamic_cast<gemActor*>(target);
    //if (targetactor)
    //    targetactor->GetCharacterData()->InterruptSpellCasting();
}

void psCombatManager::QueueNextEvent(psCombatGameEvent *event)
{
    QueueNextEvent(event->GetAttacker(),
                   event->GetWeaponSlot(),
                   event->GetTarget(),
                   event->GetAttackerID(),
                   event->GetTargetID(),
                   event->GetAttackResult());
}

void psCombatManager::QueueNextEvent(gemObject *attacker,INVENTORY_SLOT_NUMBER weaponslot,
                                     gemObject *target,
                                     int attackerCID,
                                     int targetCID, int previousResult)
{

    /* We should check the combat queue here if a viable combat queue method is
     *  ever presented.  As things stand the combat queue is not workable with 
     *  multiple weapons being used.
     */
// Get next action from QueueOfAttacks
//    int action = GetQueuedAction(attacker);

// If no next action, create default action based on mode.
//    if (!action)
//    {
//        action = GetDefaultModeAction(attacker);
//    }

    int action=0;

    psCharacter *Character=attacker->GetCharacterData();
    psItem *Weapon=Character->Inventory().GetEffectiveWeaponInSlot(weaponslot);
    uint32 weaponID = Weapon->GetUID();

    float latency = Weapon->GetLatency();
    int delay = (int)(latency*1000);

    // Create first Combat Event and queue it.
    psCombatGameEvent *event;
    
    event = new psCombatGameEvent(this,
                                  delay,
                                  action,
                                  attacker,
                                  weaponslot,
                                  weaponID,
                                  target,
                                  attackerCID,
                                  targetCID,
                                  previousResult);

    psserver->GetEventManager()->Push(event);
}

/* ----------------------- NOT IMPLEMENTED YET -------------------------
int psCombatManager::GetQueuedAction(gemActor *attacker)
{
    (void) attacker;
    // TODO: This will eventually query the prop class for the next action
    // in the queue.

    return 0;
}

int psCombatManager::GetDefaultModeAction(gemActor *attacker)
{
    (void) attacker;
    // TODO: This will eventually query the prop class for the mode
    // if nothing is in the queue.

    return 1;
}*/


/**
 * This is the meat and potatoes of the combat engine here.
 */
int psCombatManager::CalculateAttack(psCombatGameEvent *event)
{
    var_Attacker->SetObject(event->GetAttackerData() );
    var_Target->SetObject(event->GetTargetData() );

    if (var_AttackWeapon)
        var_AttackWeapon->SetObject(event->GetAttackerData()->Inventory().GetEffectiveWeaponInSlot(event->GetWeaponSlot() ) );

    if (var_TargetAttackWeapon)
        var_TargetAttackWeapon->SetObject(event->GetTargetData()->Inventory().GetEffectiveWeaponInSlot(event->GetWeaponSlot() ) );

    if (var_AttackLocationItem)
    {
        int idx = randomgen->Get((int)targetLocations.GetSize());        
        int attack_location = targetLocations[idx];
        var_AttackLocationItem->SetObject(event->GetTargetData()->Inventory().GetEffectiveArmorInSlot((INVENTORY_SLOT_NUMBER)attack_location) );
        event->AttackLocation = (INVENTORY_SLOT_NUMBER)attack_location;
    }

    calc_damage->Execute();

    if (DoLogDebug(LOG_COMBAT))
    {
        calc_damage->DumpAllVars();
    }

    if (var_IAH->GetValue() < 0.0F)
        return ATTACK_MISSED;

    if (var_AHR->GetValue() < 0.0F)
        return ATTACK_DODGED;

    if (var_Blocked->GetValue() < 0.0F)
       return ATTACK_BLOCKED;

    event->FinalDamage=var_FinalDmg->GetValue();

    return ATTACK_DAMAGE;
}

void psCombatManager::ApplyCombatEvent(psCombatGameEvent *event, int attack_result)
{
    psCharacter *attacker_data,*target_data;

    attacker_data=event->GetAttackerData();
    target_data=event->GetTargetData();

    psItem *weapon         = attacker_data->Inventory().GetEffectiveWeaponInSlot(event->GetWeaponSlot());
    psItem *blockingWeapon = target_data->Inventory().GetEffectiveWeaponInSlot(event->GetWeaponSlot());
    psItem *struckArmor    = target_data->Inventory().GetEffectiveArmorInSlot(event->AttackLocation);

    // if no armor, then ArmorVsWeapon = 1
    float ArmorVsWeapon = 1;
    if (struckArmor)
      ArmorVsWeapon = weapon->GetArmorVSWeaponResistance(struckArmor->GetBaseStats());
    // clamp values due to bad data
    ArmorVsWeapon = ArmorVsWeapon > 1.0F ? 1.0F : ArmorVsWeapon;
    ArmorVsWeapon = ArmorVsWeapon < 0.0F ? 0.0F : ArmorVsWeapon;

    gemActor *gemAttacker = dynamic_cast<gemActor*> ((gemObject *) event->attacker);
    gemActor *gemTarget   = dynamic_cast<gemActor*> ((gemObject *) event->target);

    switch (attack_result)
    {
        case ATTACK_DAMAGE:
        {
            bool isNearlyDead = false;
            if (target_data->GetHitPointsMax() > 0.0 && target_data->GetHP()/target_data->GetHitPointsMax() > 0.2)
            {
                if ((target_data->GetHP() - event->FinalDamage) / target_data->GetHitPointsMax() <= 0.2)
                    isNearlyDead = true;
            }

            psCombatEventMessage ev(event->AttackerCID,
                isNearlyDead ? psCombatEventMessage::COMBAT_DAMAGE_NEARLY_DEAD : psCombatEventMessage::COMBAT_DAMAGE,
                gemAttacker->GetEntity()->GetID(),
                gemTarget->GetEntity()->GetID(),
                event->AttackLocation,
                event->FinalDamage,
                weapon->GetAttackAnimID(gemAttacker->GetCharacterData(),gemAttacker->GetEntity() ),
                gemTarget->FindAnimIndex("hit"));

            ev.Multicast(gemTarget->GetMulticastClients(),0,MAX_COMBAT_EVENT_RANGE);

            // Apply final damage
            if (target_data!=NULL)
            {
                gemTarget->DoDamage(gemAttacker,event->FinalDamage);
                
                if (gemAttacker)
                    gemAttacker->InvokeAttackScripts(gemTarget);

                if (gemTarget)
                    gemTarget->InvokeDamageScripts(gemAttacker);

                if (gemTarget->GetClientID() == 0 && !gemTarget->GetCharacterData()->IsPet())
                {
                    // Successful attack of NPC, train skill.
                    Debug1(LOG_COMBAT, gemAttacker->GetClientID(), "Training Weapon Skills On Attack\n");
                    gemAttacker->GetCharacterData()->PracticeWeaponSkills(weapon,1);
                }
            }
            
            // If the target wasn't in combat, it is now...
            // Note that other modes shouldn't be interrupted automatically
            if (gemTarget->GetMode() == PSCHARACTER_MODE_PEACE)
            {
                if (gemTarget->GetClient())  // Set reciprocal target
                    gemTarget->GetClient()->SetTargetObject(gemAttacker,true);

                // The default stance is 'Fully Defensive'.
                Stance initialStance = gemAttacker->GetCharacterData()->getStance("FullyDefensive");
                AttackSomeone(gemTarget,gemAttacker,initialStance);
            }

            if (weapon)
            {
                weapon->AddDecay(1.0F - ArmorVsWeapon);
            }
            if (struckArmor)
            {
                struckArmor->AddDecay(ArmorVsWeapon);
            }
            NotifyTarget(gemAttacker,gemTarget);

            break;
        }
        case ATTACK_DODGED:
        {
            psCombatEventMessage ev(event->AttackerCID,
                psCombatEventMessage::COMBAT_DODGE,
                gemAttacker->GetEntity()->GetID(),
                gemTarget->GetEntity()->GetID(),
                event->AttackLocation,
                0, // no dmg on a dodge
                weapon->GetAttackAnimID( gemAttacker->GetCharacterData(),gemAttacker->GetEntity() ),
                (unsigned int)-1); // no defense anims yet

            ev.Multicast(gemTarget->GetMulticastClients(),0,MAX_COMBAT_EVENT_RANGE);

            if (gemAttacker->GetClientID() == 0 && !gemAttacker->GetCharacterData()->IsPet())
            {
                // Successful dodged by target, train skill.
                Debug1(LOG_COMBAT, gemAttacker->GetClientID(), "Training Armour Skills On Dodge\n");
                gemTarget->GetCharacterData()->PracticeArmorSkills(1, event->AttackLocation);
            }
            NotifyTarget(gemAttacker,gemTarget);
            break;
        }
        case ATTACK_BLOCKED:
        {
            psCombatEventMessage ev(event->AttackerCID,
                psCombatEventMessage::COMBAT_BLOCK,
                gemAttacker->GetEntity()->GetID(),
                gemTarget->GetEntity()->GetID(),
                event->AttackLocation,
                0, // no dmg on a block
                weapon->GetAttackAnimID( gemAttacker->GetCharacterData(),gemAttacker->GetEntity() ),
                (unsigned int)-1); // no defense anims yet

            ev.Multicast(gemTarget->GetMulticastClients(),0,MAX_COMBAT_EVENT_RANGE);

            if (gemAttacker->GetClientID() == 0 && !gemAttacker->GetCharacterData()->IsPet())
            {
                // Successful blocked by target, train skill.
                Debug1(LOG_COMBAT, gemAttacker->GetClientID(), "Training Armour Skills On Block\n");
                gemTarget->GetCharacterData()->PracticeArmorSkills(1, event->AttackLocation);
            }

            if (weapon)
            {
                weapon->AddDecay(ITEM_DECAY_FACTOR_BLOCKED);  
            }
            if (blockingWeapon)
            {
                blockingWeapon->AddDecay(ITEM_DECAY_FACTOR_PARRY);
            }
            NotifyTarget(gemAttacker,gemTarget);

            break;
        }
        case ATTACK_MISSED:
        {
            psCombatEventMessage ev(event->AttackerCID,
                psCombatEventMessage::COMBAT_MISS,
                gemAttacker->GetEntity()->GetID(),
                gemTarget->GetEntity()->GetID(),
                event->AttackLocation,
                0, // no dmg on a miss
                weapon->GetAttackAnimID( gemAttacker->GetCharacterData(),gemAttacker->GetEntity() ),
                (unsigned int)-1); // no defense anims yet

            ev.Multicast(gemTarget->GetMulticastClients(),0,MAX_COMBAT_EVENT_RANGE);
            NotifyTarget(gemAttacker,gemTarget);
            break;
        }
        case ATTACK_OUTOFRANGE:
        {
            if (event->AttackerCID)
            {
                psserver->SendSystemError(event->AttackerCID,"You are too far away to attack!");

                // Auto-stop attack is commented out below, when out of range to prevent npc kiting by jumping in and out of range
                //if (event->attacker && event->attacker.IsValid())
                //    StopAttack(dynamic_cast<gemActor*>((gemObject *) event->attacker));  // if you run away, you exit attack mode
            }
            break;
        }
        case ATTACK_BADANGLE:
        {
            if (event->AttackerCID)  // if human player
            {
                psserver->SendSystemError(event->AttackerCID,"You must face the enemy to attack!");

                // Auto-stop attack is commented out below, when out of range to prevent npc kiting by jumping in and out of range
                //if (event->attacker && event->attacker.IsValid())
                //    StopAttack(dynamic_cast<gemActor*>((gemObject *) event->attacker));  // if you run away, you exit attack mode
            }
            break;
        }
    }

    // Notify that the attack took place
    attacker_data->NotifyAttackPerformed(event->GetWeaponSlot(),csGetTicks());
}

void psCombatManager::HandleCombatEvent(psCombatGameEvent *event)
{
    psCharacter *attacker_data,*target_data;
    int attack_result;
    bool skipThisRound = false;

    if (!event->GetAttacker() || !event->GetTarget()) // disconnected and deleted
        return;

    gemActor *gemAttacker = dynamic_cast<gemActor*> ((gemObject *) event->attacker);
    gemActor *gemTarget   = dynamic_cast<gemActor*> ((gemObject *) event->target);

    attacker_data=event->GetAttackerData();
    target_data=event->GetTargetData();

    // If the attacker is no longer in attack mode or target is dead, abort.
    if (attacker_data->GetMode() != PSCHARACTER_MODE_COMBAT || !gemTarget->IsAlive() )
        return;
    
    // If the slot is no longer attackable, abort
    if (!attacker_data->Inventory().CanItemAttack(event->GetWeaponSlot()))
        return;

    // If the slot next attack time is not yet up, abort (another event sequence should have been started)
    if (attacker_data->GetSlotNextAttackTime(event->GetWeaponSlot()) > csGetTicks())
        return;

    psItem* weapon = attacker_data->Inventory().GetEffectiveWeaponInSlot(event->GetWeaponSlot());

    // weapon became unwieldable
    csString response;
    if(weapon!=NULL && !weapon->CheckRequirements(attacker_data,response))
    {
        Debug2(LOG_COMBAT, gemAttacker->GetClientID(),"%s has lost use of weapon", gemAttacker->GetName() );
        psserver->SendSystemError(event->AttackerCID, "You can't use your %s any more.", weapon->GetName() );
        return;
    }

    // If the weapon in the slot has been changed, skip a turn (latency for this slot may also have changed)
    if (event->WeaponID != weapon->GetUID())
    {
        Debug2(LOG_COMBAT, gemAttacker->GetClientID(),"%s has changed weapons mid battle", gemAttacker->GetName() );
        skipThisRound = true;
    }

    Client * attacker_client = psserver->GetNetManager()->GetClient(event->AttackerCID);
    if (attacker_client)
    {
        // if the player is too tired, stop fighting. We stop if we don't have enough stamina to make an attack with the current stance.
        MathScript* staminacombat = psserver->GetMathScriptEngine()->FindScript("StaminaCombat");

        // Output
        MathScriptVar* PhyDrain   = staminacombat->GetVar("PhyDrain");
        MathScriptVar* MntDrain   = staminacombat->GetVar("MntDrain");

        // Input
        MathScriptVar* actorVar    = staminacombat->GetOrCreateVar("Actor");
        MathScriptVar* weaponVar   = staminacombat->GetOrCreateVar("Weapon");

        if(!PhyDrain || !MntDrain)
        {
            Error1("Couldn't find the PhyDrain output var in StaminaCombat script!");
            return;
        }

        // Input the data
        actorVar->SetObject(gemAttacker->GetCharacterData());
        weaponVar->SetObject(weapon);

        staminacombat->Execute();

        if ( (attacker_client->GetCharacterData()->GetStamina(true) < PhyDrain->GetValue())
            || (attacker_client->GetCharacterData()->GetStamina(false) < MntDrain->GetValue()) )
        {
           StopAttack(attacker_data->GetActor());
           psserver->SendSystemError(event->AttackerCID, "You are too tired to attack.");
           return;
        }

        // If the target has become impervious, abort and give up attacking
        if (!attacker_client->IsAllowedToAttack(gemTarget))
        {
           StopAttack(attacker_data->GetActor());
           return;
        }

        // If the target has changed, abort (assume another combat event has started since we are still in attack mode)
        if (gemTarget != attacker_client->GetTargetObject())
            return;
    }
    else
    {
        // Check if the npc's target has changed (if it has, then assume another combat event has started.)
        gemNPC* npcAttacker = dynamic_cast<gemNPC*>(gemAttacker);
        if (npcAttacker && npcAttacker->GetTarget() != gemTarget)
            return;
    }

    if (attacker_data->IsSpellCasting())
    {
        psserver->SendSystemInfo(event->AttackerCID, "You can't attack while casting spells.");
        skipThisRound = true;
    }

    if (!skipThisRound)
    {
        // If the target is out of range, skip this attack round, send a warning
        if ( !ValidDistance(gemAttacker, gemTarget, weapon) )
        {
            // Attacker is a npc so does not realise it's attack is cancelled
            attack_result=ATTACK_OUTOFRANGE;

        }
        // If attacker is not pointing at target, skip this attack round, send a warning
        else if ( !ValidCombatAngle(gemAttacker, gemTarget, weapon) )
        {
            // Attacker is a npc so does not realise it's attack is cancelled
            attack_result=ATTACK_BADANGLE;

        }
        else
        {
            // If we didn't attack last time target might have forgotten about us by now
            // so we should remind him.
            if(event->PreviousAttackResult == ATTACK_OUTOFRANGE ||
               event->PreviousAttackResult == ATTACK_BADANGLE)
                NotifyTarget(gemAttacker,gemTarget);

            attack_result=CalculateAttack(event);
        }

        event->AttackResult=attack_result;

        //#ifdef DEBUGCOMBAT
            DebugOutput(event);
        //#endif

        ApplyCombatEvent(event, attack_result);
    }

    // Queue next event to continue combat if this is an auto attack slot
    if (attacker_data->Inventory().IsItemAutoAttack(event->GetWeaponSlot()))
    {
//      CPrintf(CON_DEBUG, "Queueing Slot %d for %s's next combat event.\n",event->GetWeaponSlot(), event->attacker->GetName() );
        QueueNextEvent(event);
    }
//    else
//        CPrintf(CON_DEBUG, "Slot %d for %s not an auto-attack slot.\n",event->GetWeaponSlot(), event->attacker->GetName() );
}

void psCombatManager::DebugOutput(psCombatGameEvent *event)
{
    psItem* item = event->GetAttackerData()->Inventory().GetEffectiveWeaponInSlot(event->GetWeaponSlot() );
    psString debug;
    debug.Append( "-----Debug Combat Summary--------\n");
    debug.AppendFmt( "%s attacks %s with slot %d , weapon %s, quality %1.2f, basedmg %1.2f/%1.2f/%1.2f\n",
      event->attacker->GetName(),event->target->GetName(), event->GetWeaponSlot(),item->GetName(),item->GetItemQuality(),
      item->GetDamage(PSITEMSTATS_DAMAGETYPE_SLASH),item->GetDamage(PSITEMSTATS_DAMAGETYPE_BLUNT),item->GetDamage(PSITEMSTATS_DAMAGETYPE_PIERCE));
    debug.AppendFmt( "IAH: %1.6f AHR: %1.6f Blocked: %1.6f",var_IAH->GetValue(),var_AHR->GetValue(),var_Blocked->GetValue());
    debug.AppendFmt( "QOH: %1.6f Damage: %1.1f\n",var_QOH->GetValue(),var_FinalDmg->GetValue());
    Debug1(LOG_COMBAT, event->attacker->GetClientID(),debug.GetData());

}





bool psCombatManager::ValidDistance(gemObject *attacker,gemObject *target,psItem *Weapon)
{
    if (Weapon==NULL)
        return false;

    if (Weapon->GetIsRangeWeapon())
    {
        return false; // TODO: Range weapons to be added later
    }
    else
    {
        if(!attacker->GetNPCPtr())
        {
            return attacker->IsNear(target,2.0F);  // within 2.0m for melee - for PCs
        }
        else
        {
            return attacker->IsNear(target,3.0F);  // within 3.0m for melee - for NPCs
        }
    }
}

bool psCombatManager::ValidCombatAngle(gemObject *attacker,gemObject *target,psItem *Weapon)
{
    csVector3 attackPos, targetPos;
    iSector *sector;

    if (attacker->GetNPCPtr())
        return true;  // We don't check this for npc's because they are too stupid

    attacker->GetPosition(attackPos,sector);
    target->GetPosition(targetPos,sector);

    csVector3 diff = targetPos - attackPos;
    if (!diff.x)
        diff.x = 0.00001F; // div/0 protect

    float angle = atan2(-diff.x,-diff.z);  // Incident angle to npc
    float attackFacing = attacker->GetAngle();
    angle = attackFacing - angle;  // Where is user facing vs. incident angle?
    if (angle > 3.14159F)
        angle -= TWO_PI;
    else if (angle < -3.14159F)
        angle += TWO_PI;

    float dist = diff.SquaredNorm();

    // Use a slightly tighter angle if the player is farther away
    if (dist > 1.5)
        return ( fabs(angle) < 3.14159F * .30);
    else
        return ( fabs(angle) < 3.14159F * .40);
}

void psCombatManager::HandleMessage(MsgEntry *me,Client *client)
{
    if (me->GetType() == MSGTYPE_DEATH_EVENT)
    {
        HandleDeathEvent(me);
        return;
    }
}

void psCombatManager::HandleDeathEvent(MsgEntry *me)
{
    psDeathEvent death(me);

    Debug1(LOG_COMBAT,death.deadActor->GetClientID(),"Combat Manager handling Death Event\n");

    // Stop any duels.
    if (death.deadActor->GetClient())
        death.deadActor->GetClient()->ClearAllDuelClients();

    // Stop actor moving.
    death.deadActor->StopMoving();

    // Send out the notification of death, which plays the anim, etc.
    psCombatEventMessage die(death.deadActor->GetClientID(),
                                psCombatEventMessage::COMBAT_DEATH,
                                (death.killer)?death.killer->GetEntity()->GetID():0,
                                death.deadActor->GetEntity()->GetID(),
                                -1, // no target location
                                0,  // no dmg on a death
                                (unsigned int)-1,  // TODO: "killing blow" matrix of mob-types vs. weapon types
                                (unsigned int)-1 ); // Death anim on client side is handled by the death mode message

    die.Multicast(death.deadActor->GetMulticastClients(),0,MAX_COMBAT_EVENT_RANGE);
}


/*-------------------------------------------------------------*/

psSpareDefeatedEvent::psSpareDefeatedEvent(gemActor *losr) : psGameEvent(0, SECONDS_BEFORE_SPARING_DEFEATED * 1000, "psSpareDefeatedEvent")
{
    loser = losr->GetClient();
}

void psSpareDefeatedEvent::Trigger()
{
    // Ignore stale events: perhaps the character was already killed and resurrected...
    if (!loser.IsValid() || !loser->GetActor() || loser->GetActor()->GetMode() != PSCHARACTER_MODE_DEFEATED)
        return;

    psserver->SendSystemInfo(loser->GetClientNum(), "Your opponent has spared your life.");
    loser->ClearAllDuelClients();
    loser->GetActor()->SetMode(PSCHARACTER_MODE_PEACE);
}

/*-------------------------------------------------------------*/

psCombatGameEvent::psCombatGameEvent(psCombatManager *mgr,
                                     int delayticks,
                                     int act,
                                     gemObject *attacker,
                                     INVENTORY_SLOT_NUMBER weaponslot,
                                     uint32 weapon,
                                     gemObject *target,
                                     int attackerCID,
                                     int targetCID,
                                     int previousResult)
  : psGameEvent(0,delayticks,"psCombatGameEvent")
{
    combatmanager  = mgr;
    action         = act;
    this->attacker = attacker;
    this->WeaponSlot = weaponslot;
    this->WeaponID = weapon;
    this->target   = target;
    this->AttackerCID = attackerCID;
    this->TargetCID   = targetCID;

    // Also register the target as a disconnector
//    target->Register(this);
//    attacker->Register( this ); 
    
    attackerdata = attacker->GetCharacterData();
    targetdata   = target->GetCharacterData();

    if (!attackerdata || !targetdata)
        return;

    AttackValue=-1;      
    AttackRoll=-1;
    DefenseRoll=-1;
    DodgeValue=-1;
    BlockValue=-1;       
    QualityOfHit=-1;     
    BaseHitDamage=-1;    
    AttackLocation=PSCHARACTER_SLOT_NONE;   
    ArmorDamageAdjustment=-1;
    FinalBaseDamage=-1;  
    DamageMods=-1;       
    FinalDamage=-1;
    AttackResult=ATTACK_NOTCALCULATED;
    PreviousAttackResult=previousResult;
}


psCombatGameEvent::~psCombatGameEvent()
{
//    if (!valid)
//        return;

/***
    if ( target && target->IsValid() )
    {        
        target->Unregister(this);
        target = NULL;
    }        
    if ( attacker && attacker->IsValid() )
    {
        attacker->Unregister(this);
        target = NULL;
    }
 ***/
}

bool psCombatGameEvent::CheckTrigger()
{
    if ( attacker.IsValid() && target.IsValid())
    {
        if ( attacker->IsAlive() && target->IsAlive() )
        {
            return true;
        }
        else
        {
            return false;
        }                    
    }        
    else
    {
        return false;
    }        
}

void psCombatGameEvent::Trigger()
{
    if (!attacker.IsValid() || !target.IsValid())
        return;

    combatmanager->HandleCombatEvent(this);
}

/************
void psCombatGameEvent::Disconnecting(void * object)
{
    psGEMEvent::Disconnecting(object);
    valid = false;
    if (attacker)
    {
        attacker->Unregister(this);
        attacker = NULL;
    }
    if ( target )
    {
        target->Unregister(this);
        target = NULL;
    }
}
*****/
