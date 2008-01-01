/*
* npcbehave.cpp by Keith Fulton <keith@paqrat.com>
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
#include <csgeom/transfrm.h>
#include <iutil/document.h>
#include <iutil/vfs.h>
#include <iengine/mesh.h>
#include <iengine/movable.h>

#include <propclass/mesh.h>
#include <physicallayer/entity.h>
#include <physicallayer/propclas.h>
#include <propclass/linmove.h>

#include "net/msghandler.h"
#include "net/npcmessages.h"
#include "npcbehave.h"
#include "perceptions.h"
#include "npc.h"
#include "npcclient.h"
#include "networkmgr.h"
#include "globals.h"
#include "util/log.h"
#include "util/location.h"
#include "util/strutil.h"
#include "util/psutil.h"
#include "gem.h"



Reaction::Reaction()
{
    delta_desire  = 0;
    affected      = NULL;
    range         = 0;
    active_only   = false;
    inactive_only = false;
}

bool Reaction::Load(iDocumentNode *node,BehaviorSet& behaviors)
{
    delta_desire = node->GetAttributeValueAsFloat("delta");

    // Handle hooking up to the right behavior
    csString name = node->GetAttributeValue("behavior");
    affected = behaviors.Find(name);
    if (!affected)
    {
        Error2("Reaction specified unknown behavior of '%s'. Error in XML.\n",(const char *)name);
        return false;
    }

    // Handle hooking up to the perception
    event_type             = node->GetAttributeValue("event");
    range                  = node->GetAttributeValueAsFloat("range");
    weight                 = node->GetAttributeValueAsFloat("weight");
    faction_diff           = node->GetAttributeValueAsInt("faction_diff");
    oper                   = node->GetAttributeValue("oper");

    // Decode the value field, It is in the form value="1,2,,4"
    csString valueAttr = node->GetAttributeValue("value");
    csArray<csString> valueStr = psSplit( valueAttr , ',');
    for (size_t ii=0; ii < valueStr.GetSize(); ii++)
    {
        if (valueStr[ii] != "")
        {
            values.Push(atoi(valueStr[ii]));
            valuesValid.Push(true);
        } else
        {
            values.Push(0);
            valuesValid.Push(false);
        }
    }
    // Decode the random field, It is in the form random="1,2,,4"
    csString randomAttr = node->GetAttributeValue("random");
    csArray<csString> randomStr = psSplit( randomAttr, ',');
    for (size_t ii=0; ii < randomStr.GetSize(); ii++)
    {
        if (randomStr[ii] != "")
        {
            randoms.Push(atoi(randomStr[ii]));
            randomsValid.Push(true);
        } else
        {
            randoms.Push(0);
            randomsValid.Push(false);
        }
    }

    type                   = node->GetAttributeValue("type");
    active_only            = node->GetAttributeValueAsBool("active_only");
    inactive_only          = node->GetAttributeValueAsBool("inactive_only");
    react_when_dead        = node->GetAttributeValueAsBool("when_dead",false);
    react_when_invisible   = node->GetAttributeValueAsBool("when_invisible",false);
    react_when_invincible  = node->GetAttributeValueAsBool("when_invincible",false);
    only_interrupt         = node->GetAttributeValue("only_interrupt");

    return true;
}

void Reaction::DeepCopy(Reaction& other,BehaviorSet& behaviors)
{
    delta_desire           = other.delta_desire;
    affected               = behaviors.Find(other.affected->GetName());
    event_type             = other.event_type;
    range                  = other.range;
    faction_diff           = other.faction_diff;
    oper                   = other.oper;
    weight                 = other.weight;
    values                 = other.values;
    valuesValid            = other.valuesValid;
    randoms                = other.randoms;
    randomsValid           = other.randomsValid;
    type                   = other.type;
    active_only            = other.active_only;
    inactive_only          = other.inactive_only;
    react_when_dead        = other.react_when_dead;
    react_when_invisible   = other.react_when_invisible;
    react_when_invincible  = other.react_when_invincible;
    only_interrupt         = other.only_interrupt;

    // For now depend on that each npc do a deep copy to create its instance of the reaction
    for (uint ii=0; ii < values.GetSize(); ii++)
    {
        if (GetRandomValid((int)ii))
        {
            values[ii] += psGetRandom(GetRandom((int)ii));
        }
    }
}

void Reaction::React(NPC *who,EventManager *eventmgr,Perception *pcpt)
{
    CS_ASSERT(who);

    // When active_only flag is set we should do nothing
    // if the affected behaviour is inactive.
    if (active_only && !affected->GetActive() )
        return;

    // When inactive_only flag is set we should do nothing
    // if the affected behaviour is active.
    if (inactive_only && affected->GetActive() )
        return;

    // If dead we should not react unless react_when_dead is set
    if (!(who->IsAlive() || react_when_dead))
        return;

    if (only_interrupt)
    {
        bool found = false;
        csArray<csString> strarr = psSplit( only_interrupt, ':');
        for (size_t i = 0; i < strarr.GetSize(); i++)
        {
            if (who->GetCurrentBehavior() && strarr[i] == who->GetCurrentBehavior()->GetName())
            {
                found = true;
                break;
            }
        }
        if (!found) 
            return;
    }

    if (!pcpt->ShouldReact(this,who))
    {
        if (who->IsDebugging(12))
        {
            who->Printf(12, "Skipping perception %s",pcpt->ToString().GetDataSafe());
        }
        return;
    }

    who->Printf(2, "Reaction '%s' reacting to perception %s", GetEventType(), pcpt->ToString().GetDataSafe());
    who->Printf(10, "Adding %1.1f need to behavior %s for npc %s(EID: %u).",
                delta_desire, affected->GetName(), who->GetName(),
                (who->GetEntity()?who->GetEntity()->GetID():0));
        
    if (delta_desire>0 || delta_desire<-1)
    {
        affected->ApplyNeedDelta(delta_desire);
    }
    else if (fabs(delta_desire+1)>SMALL_EPSILON)  // -1 in delta means "don't react"
    {
        // zero delta means guarantee that this affected
        // need becomes the highest (and thus active) one.

        float highest = 0;

        if (who->GetCurrentBehavior())
        {
            highest = who->GetCurrentBehavior()->CurrentNeed();
        }
        
        affected->ApplyNeedDelta( highest - affected->CurrentNeed() + 25);
        affected->SetCompletionDecay(-1);
    }
    
    pcpt->ExecutePerception(who,weight);

    Perception *p = pcpt->MakeCopy();
    who->SetLastPerception(p);
}

bool Reaction::ShouldReact(iCelEntity* entity, Perception *pcpt)
{
    gemNPCObject * actor = npcclient->FindEntityID(entity->GetID());

    if (!actor) return false;

    if (!(actor->IsVisible() || react_when_invisible))
    {
        return false;
    }
    
    if (!(!actor->IsInvincible() || react_when_invincible))
    {
        return false;
    }
    return true;
}

int Reaction::GetValue(int i)
{
    if (i < (int)values.GetSize())
    {
        return values[i];
        
    }
    return 0;
}

bool Reaction::GetValueValid(int i)
{
    if (i < (int)valuesValid.GetSize())
    {
        return valuesValid[i];
        
    }
    return false;
}

int Reaction::GetRandom(int i)
{
    if (i < (int)randoms.GetSize())
    {
        return randoms[i];
        
    }
    return 0;
}

bool Reaction::GetRandomValid(int i)
{
    if (i < (int)randomsValid.GetSize())
    {
        return randomsValid[i];
        
    }
    return false;
}


/*----------------------------------------------------------------------------*/

bool Perception::ShouldReact(Reaction *reaction, NPC *npc)
{
    if (name == reaction->GetEventType())
    {
        return true;
    }
    return false;
}

Perception *Perception::MakeCopy()
{
    Perception *p = new Perception(name,type);
    return p;
}

csString Perception::ToString()
{
    csString result;
    result.Format("Name: '%s' Type: '%s'",name.GetDataSafe(), type.GetDataSafe());
    return result;
}


/*----------------------------------------------------------------------------*/

bool RangePerception::ShouldReact(Reaction *reaction, NPC *npc)
{
    if (name == reaction->GetEventType() && range < reaction->GetRange())
    {
        return true;
    }
    else
    {
        return false;
    }
}

Perception *RangePerception::MakeCopy()
{
    RangePerception *p = new RangePerception(name,range);
    return p;
}

csString RangePerception::ToString()
{
    csString result;
    result.Format("Name: '%s' Range: '%.2f'",name.GetDataSafe(), range );
    return result;
}

//---------------------------------------------------------------------------------


bool FactionPerception::ShouldReact(Reaction *reaction,NPC *npc)
{
    if (name == reaction->GetEventType())
    {
        if (player)
        {
            if (!reaction->ShouldReact(player,this))
            {
                return false;
            }
        }
        
        if (reaction->GetOp() == '>' )
        {
            npc->Printf(15, "Checking %d > %d.",faction_delta,reaction->GetFactionDiff() );
            if (faction_delta > reaction->GetFactionDiff() )
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
            npc->Printf(15, "Checking %d < %d.",faction_delta,reaction->GetFactionDiff() );
            if (faction_delta < reaction->GetFactionDiff() )
            {
                return true;
            }
            else
            {
                return false;
            }
        }
    }
    return false;
}

bool FactionPerception::GetLocation(csVector3& pos, iSector*& sector)
{
    if (player)
    {
        float rot;
        psGameObject::GetPosition(player,pos,rot,sector);
        return true;
    }
    return false;
}

Perception *FactionPerception::MakeCopy()
{
    FactionPerception *p = new FactionPerception(name,faction_delta,player);
    return p;
}

void FactionPerception::ExecutePerception(NPC *npc,float weight)
{
    npc->AddToHateList(player,weight*-faction_delta);
}

//---------------------------------------------------------------------------------


bool ItemPerception::GetLocation(csVector3& pos, iSector*& sector)
{
    if (item)
    {
        float rot;
        psGameObject::GetPosition(item,pos,rot,sector);
        return true;
    }
    return false;
}

Perception *ItemPerception::MakeCopy()
{
    ItemPerception *p = new ItemPerception(name,item);
    return p;
}

//---------------------------------------------------------------------------------


bool LocationPerception::ShouldReact(Reaction *reaction,NPC *npc)
{
    if (name == reaction->GetEventType() && (reaction->GetType() == "" || type == reaction->GetType()))
    {
        return true;
    }
    return false;
}

bool LocationPerception::GetLocation(csVector3& pos, iSector*& sector)
{
    if (location)
    {
        pos = location->pos;
        sector = location->GetSector(npcclient->GetEngine());
        return true;
    }
    return false;
}

Perception *LocationPerception::MakeCopy()
{
    LocationPerception *p = new LocationPerception(name,type,location);
    return p;
}

float LocationPerception::GetRadius() const
{
    return location->radius; 
}

//---------------------------------------------------------------------------------

Perception *AttackPerception::MakeCopy()
{
    AttackPerception *p = new AttackPerception(name,attacker);
    return p;
}

void AttackPerception::ExecutePerception(NPC *npc,float weight)
{
    npc->AddToHateList(attacker,weight);
}
//---------------------------------------------------------------------------------

Perception *GroupAttackPerception::MakeCopy()
{
    GroupAttackPerception *p = new GroupAttackPerception(name,attacker_ents,bestSkillSlots);
    return p;
}

void GroupAttackPerception::ExecutePerception(NPC *npc,float weight)
{
    for(size_t i=0;i<attacker_ents.GetSize();i++)
        npc->AddToHateList(attacker_ents[i],bestSkillSlots[i]*weight);
}

//---------------------------------------------------------------------------------

Perception *DamagePerception::MakeCopy()
{
    DamagePerception *p = new DamagePerception(name,attacker,damage);
    return p;
}

void DamagePerception::ExecutePerception(NPC *npc,float weight)
{
    npc->AddToHateList(attacker,damage*weight);
}

//---------------------------------------------------------------------------------

SpellPerception::SpellPerception(const char *name,
                                 iCelEntity *caster,iCelEntity *target, 
                                 const char *type,float severity)
                                 : Perception(name)
{
    this->caster = caster;
    this->target = target;
    this->spell_severity = severity;
    this->type = type;
}

bool SpellPerception::ShouldReact(Reaction *reaction,NPC *npc)
{
    csString event(type);
    event.Append(':');

    if (npc->GetEntityHate(caster) || npc->GetEntityHate(target))
    {
        event.Append("target");
    }
    else if (target == npc->GetEntity())
    {
        event.Append("self");
    }
    else
    {
        event.Append("unknown");
    }

    if (event == reaction->GetEventType())
    {
        npc->Printf(15, "%s spell cast by %s on %s, severity %1.1f.\n",
            event.GetData(), (caster)?caster->GetName():"(Null caster)", (target)?target->GetName():"(Null target)", spell_severity);

        return true;
    }
    return false;
}

Perception *SpellPerception::MakeCopy()
{
    SpellPerception *p = new SpellPerception(name,caster,target,type,spell_severity);
    return p;
}

void SpellPerception::ExecutePerception(NPC *npc,float weight)
{
    npc->AddToHateList(caster,spell_severity*weight);
}

//---------------------------------------------------------------------------------

bool TimePerception::ShouldReact(Reaction *reaction,NPC *npc)
{
    if (name == reaction->GetEventType() )
    {
        if (npc->IsDebugging(15))
        {
            csString dbgOut;
            dbgOut.AppendFmt("Time is now %d:%02d %d-%d-%d and I need ",
                          gameHour,gameMinute,gameYear,gameMonth,gameDay);
            // Hours
            if (reaction->GetValueValid(0))
            {
                dbgOut.AppendFmt("%d:",reaction->GetValue(0));
            }
            else
            {
                dbgOut.Append("*:");
            }
            // Minutes
            if (reaction->GetValueValid(1))
            {
                dbgOut.AppendFmt("%02d ",reaction->GetValue(1));
            }
            else
            {
                dbgOut.Append("* ");
            }
            // Year
            if (reaction->GetValueValid(2))
            {
                dbgOut.AppendFmt("%d-",reaction->GetValue(2));
            }
            else
            {
                dbgOut.Append("*-");
            }
            // Month
            if (reaction->GetValueValid(3))
            {
                dbgOut.AppendFmt("%d-",reaction->GetValue(3));
            }
            else
            {
                dbgOut.Append("*-");
            }
            // Day
            if (reaction->GetValueValid(4))
            {
                dbgOut.AppendFmt("%d",reaction->GetValue(4));
            }
            else
            {
                dbgOut.Append("*");
            }

            npc->Printf(15,dbgOut);
        }
        
        if ((!reaction->GetValueValid(0) || reaction->GetValue(0) == gameHour) &&
            (!reaction->GetValueValid(1) || reaction->GetValue(1) == gameMinute) &&
            (!reaction->GetValueValid(2) || reaction->GetValue(2) == gameYear) &&
            (!reaction->GetValueValid(3) || reaction->GetValue(3) == gameMonth) &&
            (!reaction->GetValueValid(4) || reaction->GetValue(4) == gameDay))
        {
            return true;
        }
        else
        {
            return false;
        }
    }
    return false;
}

Perception *TimePerception::MakeCopy()
{
    TimePerception *p = new TimePerception(gameHour,gameMinute,gameYear,gameMonth,gameDay);
    return p;
}

//---------------------------------------------------------------------------------

Perception *DeathPerception::MakeCopy()
{
    DeathPerception *p = new DeathPerception(who);
    return p;
}

void DeathPerception::ExecutePerception(NPC *npc,float weight)
{
    npc->RemoveFromHateList(who);
}

//---------------------------------------------------------------------------------
Perception *InventoryPerception::MakeCopy()
{
    InventoryPerception *p = new InventoryPerception(name,type,count,pos,sector,radius);
    return p;
}


/// NPC Pet Perceptions ===========================================================
//---------------------------------------------------------------------------------

OwnerCmdPerception::OwnerCmdPerception( const char *name,
                                        int command,
                                        iCelEntity *owner,
                                        iCelEntity *pet,
                                        iCelEntity *target ) : Perception(name)
{
    this->command = command;
    this->owner = owner;
    this->pet = pet;
    this->target = target;
}

bool OwnerCmdPerception::ShouldReact( Reaction *reaction, NPC *npc )
{
    csString event("ownercmd");
    event.Append(':');

    switch ( this->command )
    {
    case psPETCommandMessage::CMD_FOLLOW: 
        event.Append( "follow" );
        break;
    case psPETCommandMessage::CMD_STAY :
        event.Append( "stay" );
        break;
    case psPETCommandMessage::CMD_SUMMON :
        event.Append( "summon" );
        break;
    case psPETCommandMessage::CMD_DISMISS :
        event.Append( "dismiss" );
        break;
    case psPETCommandMessage::CMD_ATTACK :
        event.Append( "attack" );
        break;
    case psPETCommandMessage::CMD_STOPATTACK :
        event.Append( "stopattack" );
        break;
    default:
        event.Append("unknown");
        break;
    }

    if (event == reaction->GetEventType())
    {
        return true;
    }
    return false;
}

Perception *OwnerCmdPerception::MakeCopy()
{
    OwnerCmdPerception *p = new OwnerCmdPerception( name, command, owner, pet, target );
    return p;
}

void OwnerCmdPerception::ExecutePerception( NPC *pet, float weight )
{
    switch ( this->command )
    {
    case psPETCommandMessage::CMD_SUMMON : // Summon
        break;
    case psPETCommandMessage::CMD_DISMISS : // Dismiss
        break;
    case psPETCommandMessage::CMD_ATTACK : // Attack
        if (pet->GetTarget())
        {
            pet->AddToHateList(pet->GetTarget(), 1 * weight );
        }
        else
        {
            pet->Printf("No target to add to hate list");
        }
        
        break;

    case psPETCommandMessage::CMD_STOPATTACK : // StopAttack
        break;
    }
}

//---------------------------------------------------------------------------------

OwnerActionPerception::OwnerActionPerception( const char *name  ,
                                              int action,
                                              iCelEntity *owner ,
                                              iCelEntity *pet   ) : Perception(name)
{
    this->action = action;
    this->owner = owner;
    this->pet = pet;
}

bool OwnerActionPerception::ShouldReact( Reaction *reaction, NPC *npc )
{
    csString event("ownercmd");
    event.Append(':');

    switch ( this->action )
    {
    case 1: 
        event.Append("attack");
        break;
    case 2: 
        event.Append("damage");
        break;
    case 3: 
        event.Append("logon");
        break;
    case 4: 
        event.Append("logoff");
        break;
    default:
        event.Append("unknown");
        break;
    }

    if (event == reaction->GetEventType())
    {
        return true;
    }
    return false;
}

Perception *OwnerActionPerception::MakeCopy()
{
    OwnerActionPerception *p = new OwnerActionPerception( name, action, owner, pet );
    return p;
}

void OwnerActionPerception::ExecutePerception( NPC *pet, float weight )
{
    switch ( this->action )
    {
    case 1: // Find and Set owner
        break;
    case 2: // Clear Owner and return to astral plane
        break;
    }
}

//---------------------------------------------------------------------------------
/// NPC Pet Perceptions ===========================================================


//---------------------------------------------------------------------------------

NPCCmdPerception::NPCCmdPerception( const char *command, NPC * self ) : Perception("npccmd")
{
    this->cmd = command;
    this->self = self;
}

bool NPCCmdPerception::ShouldReact( Reaction *reaction, NPC *npc )
{
    csString global_event("npccmd:global:");
    global_event.Append(cmd);

    if (strcasecmp(global_event,reaction->GetEventType()) == 0)
    {
        npc->Printf(15,"Matched reaction '%s' to perception '%s'.\n",reaction->GetEventType(), global_event.GetData() );
        return true;
    }
    else
    {
        npc->Printf(16,"No matched reaction '%s' to perception '%s'.\n",reaction->GetEventType(), global_event.GetData() );
    }
    

    csString self_event("npccmd:self:");
    self_event.Append(cmd);

    if (strcasecmp(self_event,reaction->GetEventType())==0 && npc == self)
    {
        npc->Printf(15,"Matched reaction '%s' to perception '%s'.\n",reaction->GetEventType(), self_event.GetData() );
        return true;
    }
    else
    {
        npc->Printf(16,"No matched reaction '%s' to perception '%s' for self(%s) with npc(%s).\n",
                    reaction->GetEventType(), self_event.GetData(), 
                    self->GetName(), npc->GetName() );
    }
    
    return false;
}

Perception *NPCCmdPerception::MakeCopy()
{
    NPCCmdPerception *p = new NPCCmdPerception( cmd, self );
    return p;
}

//---------------------------------------------------------------------------------
