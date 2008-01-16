/*
 * spellmanager.cpp by Anders Reggestad <andersr@pvv.org>
 *
 * Copyright (C) 2001-2002 Atomic Blue (info@planeshift.it, http://www.atomicblue.org) 
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
#include "globals.h"

//=============================================================================
// Crystal Space Includes
//=============================================================================
#include <iutil/document.h>
#include <csutil/xmltiny.h>
#include <physicallayer/entity.h>
#include <propclass/mesh.h>

//=============================================================================
// Library Includes
//=============================================================================
#include "net/msghandler.h"
#include "net/messages.h"
#include "util/eventmanager.h"
#include "util/psxmlparser.h"
#include "util/mathscript.h"
#include "bulkobjects/pscharacterloader.h"
#include "bulkobjects/psspell.h"
#include "bulkobjects/psglyph.h"

//=============================================================================
// Server Includes
//=============================================================================
#include "spellmanager.h"
#include "clients.h"
#include "playergroup.h"
#include "gem.h"
#include "psserver.h"
#include "psserverchar.h"
#include "cachemanager.h"
#include "progressionmanager.h"
#include "commandmanager.h"


psSpellManager::psSpellManager(ClientConnectionSet *ccs,
                               iObjectRegistry * object_reg)
{
    clients      = ccs;
    this->object_reg = object_reg;

    randomgen = psserver->rng;

    psserver->GetEventManager()->Subscribe(this,MSGTYPE_GLPYH_REQUEST,REQUIRE_READY_CLIENT|REQUIRE_ALIVE);  
    psserver->GetEventManager()->Subscribe(this,MSGTYPE_GLYPH_ASSEMBLE,REQUIRE_READY_CLIENT|REQUIRE_ALIVE);  
    psserver->GetEventManager()->Subscribe(this,MSGTYPE_SPELL_CAST,REQUIRE_READY_CLIENT|REQUIRE_ALIVE);  
    psserver->GetEventManager()->Subscribe(this,MSGTYPE_PURIFY_GLYPH,REQUIRE_READY_CLIENT|REQUIRE_ALIVE);  
    psserver->GetEventManager()->Subscribe(this,MSGTYPE_SPELL_BOOK,REQUIRE_READY_CLIENT|REQUIRE_ALIVE);  
    psserver->GetEventManager()->Subscribe(this,MSGTYPE_SPELL_CANCEL,REQUIRE_READY_CLIENT|REQUIRE_ALIVE);      

    researchSpellScript = psserver->GetMathScriptEngine()->FindScript("CalculateChanceOfResearchSuccess");
    if ( researchSpellScript )
    {
        varCaster = researchSpellScript->GetOrCreateVar( "Actor" );
        varSpell = researchSpellScript->GetOrCreateVar( "Spell" );
        varSuccess = researchSpellScript->GetOrCreateVar( "ChanceOfSuccess" );
    }
    else
    {
        Warning1( LOG_SPELLS, "Can't find math script 'CalculateChanceOfResearchSuccess'." );
    }
}

psSpellManager::~psSpellManager()
{
    if (psserver->GetEventManager())
    {
        psserver->GetEventManager()->Unsubscribe(this,MSGTYPE_GLPYH_REQUEST);
        psserver->GetEventManager()->Unsubscribe(this,MSGTYPE_GLYPH_ASSEMBLE);
        psserver->GetEventManager()->Unsubscribe(this,MSGTYPE_SPELL_CAST);  
        psserver->GetEventManager()->Unsubscribe(this,MSGTYPE_PURIFY_GLYPH);  
        psserver->GetEventManager()->Unsubscribe(this,MSGTYPE_SPELL_BOOK);
        psserver->GetEventManager()->Unsubscribe(this,MSGTYPE_SPELL_CANCEL);
    }
}

void psSpellManager::HandleMessage(MsgEntry *me,Client *client)
{
    switch( me->GetType())
    {
        case MSGTYPE_SPELL_CAST:
        {
            psSpellCastMessage mesg(me);            
            Cast(client,mesg.spell,mesg.kFactor);
            break;
        }
        case MSGTYPE_PURIFY_GLYPH:
        {
            psPurifyGlyphMessage mesg(me);
            StartPurifying(client, mesg.glyph);
            break;
        }        
        case MSGTYPE_SPELL_BOOK:
        {
            SendSpellBook( client );
            break;
        }
        case MSGTYPE_GLPYH_REQUEST:
        {
            SendGlyphs( client );
            break; 
        }
        case MSGTYPE_GLYPH_ASSEMBLE:
        {
            HandleAssembler( client, me );
            break;
        }       
        
        case MSGTYPE_SPELL_CANCEL:
        {
            CancelSpellCasting(client->GetActor());
            break;
        }            
    }
}

//debug:
void DumpAssembler(Client * client, glyphList_t & assembler)
{
    psString msg;
    msg="";
    for (size_t i=0; i < assembler.GetSize(); i++)
        msg.AppendFmt("S%u ",assembler[i]->GetUID());
    psserver->SendSystemInfo(client->GetClientNum(), msg.GetData());
}

void psSpellManager::HandleAssembler( Client* client, MsgEntry* me )
{
    psGlyphAssembleMessage mesg;
    mesg.FromClient( me );
    
    glyphList_t assembler;
    psSpell * spell;
    psItemStats * stats;
    
    assembler.SetSize(GLYPH_ASSEMBLER_SLOTS);   // set to maximum length so the glyphs will fit in

    size_t numGlyphs = 0;
    for (size_t slotNum=0; slotNum < GLYPH_ASSEMBLER_SLOTS; slotNum++)
    {        
        int statID = mesg.glyphs[slotNum];
        stats = CacheManager::GetSingleton().GetBasicItemStatsByID(statID);
        if (stats != NULL)
        {
            numGlyphs++;
            assembler[numGlyphs-1] = stats;
        }
    }
    assembler.SetSize(numGlyphs);   // set assembler to actual length
    
    //DumpAssembler(client, assembler);

    if (!client->GetCharacterData()->Inventory().HasPurifiedGlyphs(assembler))
    {
        Error2( "Client %i tried to research spell with glyphs he actually doesn't have", client->GetClientNum() );
        SendGlyphs( client );
        return;
    }

    csString name( "You failed to discover a spell." );
    csString image( " " );
    csString description(" ");
    ProgressionEvent *progEvent;
    
    // Default to nasty progression script.
    csString eventName = "ResearchSpellFailure";
    float result = 0;

    // Is the Glyph Sequence a Valid one?
    spell = FindSpell(client, assembler);

    if ( spell )
    {
        // Is this spell already in our spellbook?
        psSpell * knownSpell = client->GetCharacterData()->GetSpellByName( spell->GetName() );
        if ( knownSpell )
        {
            if(mesg.info)
            {
                psGlyphAssembleMessage newmsginfo(client->GetClientNum(), knownSpell->GetName(), knownSpell->GetImage(), knownSpell->GetDescription());
                newmsginfo.SendMessage();
            }
            else
            {
                psserver->SendSystemInfo( client->GetClientNum(), "You know this spell already." );
            }
            return;
        }
    }
    if(mesg.info)
    {
        return;
    }

    // If the spell exists and the player has high enough skill, determine if the player successfully researches the spell.
    if ( researchSpellScript && spell && client->GetCharacterData()->CheckMagicKnowledge( spell->GetSkill(), spell->GetRealm() ))
    {
        varCaster->SetObject( client->GetCharacterData() );
        varSpell->SetObject( spell );
        researchSpellScript->Execute();
        float chanceOfSuccess = varSuccess->GetValue();

        // If success, set to nice progression script
        float dieRoll = psserver->GetRandom() * 100.0;
        if (  dieRoll < chanceOfSuccess )
        {
            eventName = "ResearchSpellSuccess";
        }
    }

    // Run progression script.
    progEvent = psserver->GetProgressionManager()->FindEvent( eventName.GetDataSafe() );
    if(progEvent)
    {
        progEvent->CopyVariables( researchSpellScript );
        result = progEvent->Run(client->GetActor(), client->GetActor(), NULL);
    }
    else
    {
        Error2( "Failed to find progression event %s. ", eventName.GetDataSafe() );
    }
    
    // If successfully researched, add to spellbook!
    if ( eventName == "ResearchSpellSuccess" )        
    {
        description = spell->GetDescription();
        name = spell->GetName();
        image = spell->GetImage();

        SaveSpell( client, name );
    }

    // Clear the description, if this is not valid glyph sequence for our player:
    psGlyphAssembleMessage newmsg(client->GetClientNum(), name, image, description);
    newmsg.SendMessage();
}


void psSpellManager::SaveSpell(Client * client, csString spellName)
{
    psSpell * spell = client->GetCharacterData()->GetSpellByName(spellName);
    if (spell)
    {
        psserver->SendSystemInfo(client->GetClientNum(),
                                 "You know the %s spell already!",spellName.GetData());
        return;
    }

    spell = CacheManager::GetSingleton().GetSpellByName(spellName);
    if (!spell)
    {
        psserver->SendSystemInfo(client->GetClientNum(),
                                 "%s isn't a defined spell!",spellName.GetData());
        return;
    }


    client->GetCharacterData()->AddSpell(spell);

    psServer::CharacterLoader.SaveCharacterData(client->GetCharacterData(),client->GetActor());

    SendSpellBook(client);
    psserver->SendSystemInfo(client->GetClientNum(),
                           "%s added to your spell book!",spellName.GetData());
}

void psSpellManager::Cast(Client * client, csString spellName, float kFactor)
{
    psSpell *spell = NULL;

    // Allow developers to cast any spell, even if unknown to the character.
    if (CacheManager::GetSingleton().GetCommandManager()->Validate(client->GetSecurityLevel(), "cast all spells"))
    {
        spell = CacheManager::GetSingleton().GetSpellByName(spellName);
    }        
    else
    {
        spell = client->GetCharacterData()->GetSpellByName(spellName); 
    }        

    if (!spell)
    {
        psserver->SendSystemInfo(client->GetClientNum(),
                                 "%s is a unknown spell for you!",spellName.GetData());
        return;
    }

    client->GetCharacterData()->SetKFactor(kFactor);

    csString  effectName;
    csVector3 offset;
    PS_ID     anchorID;
    PS_ID     targetID;
    unsigned int  castingDuration;
    csString  castingText;

    psSpellCastGameEvent *event = spell->Cast(this, client, effectName, offset, anchorID, targetID, castingDuration, &castingText);
    if ( event )
    {
        event->QueueEvent();
    
        //targetID = event->target->GetEntity()->GetID();
        if (effectName && effectName != "")
        {
            psEffectMessage newmsg(0, effectName, offset, anchorID, targetID, castingDuration, 0);
            if (newmsg.valid)
            {
                newmsg.Multicast(event->caster->GetActor()->GetMulticastClients(),0,PROX_LIST_ANY_RANGE);
            }                
            else
            {
                Bug1("Could not create valid psEffectMessage for broadcast.\n");
            }
        }
    }

    // Inform player
    if (castingText != "")
    {
        psserver->SendSystemInfo(client->GetClientNum(),castingText.GetData());
    }
}

void psSpellManager::CancelSpellCasting(gemActor * caster)
{
    caster->GetCharacterData()->InterruptSpellCasting();
}

void psSpellManager::SendSpellBook(Client * client)
{
    psSpellBookMessage mesg( client->GetClientNum() );
    csArray<psSpell*> spells = client->GetCharacterData()->GetSpellList();
    
    for ( size_t i = 0; i < spells.GetSize(); i++ )
    {
        csString glyph0("");
        csString glyph1("");
        csString glyph2("");
        csString glyph3("");
        csString name( spells[i]->GetName() );
        csString description( spells[i]->GetDescription() );
        csString way(spells[i]->GetWay()->name);
        int realm = spells[i]->GetRealm();
        csArray<psItemStats*> glyphs = spells[i]->GetGlyphList();
        
        if ( glyphs.GetSize() > 0 )
        {
            glyph0 = glyphs[0]->GetImageName();
        }            
        if ( glyphs.GetSize() > 1 )
        {
            glyph1 = glyphs[1]->GetImageName();
        }                    
        if ( glyphs.GetSize() > 2 )
        {
            glyph1 = glyphs[2]->GetImageName();
        }            
        if ( glyphs.GetSize() > 3 )
        {
            glyph1 = glyphs[3]->GetImageName();
        }            
            
        mesg.AddSpell(name,description,way,realm,
                 glyph0,glyph1,glyph2,glyph3 );
    }

    mesg.Construct();
    mesg.SendMessage();
}

void psSpellManager::SendGlyphs( Client * client)
{
    psCharacter * character = client->GetCharacterData();
    csArray <glyphSlotInfo> slots;
    size_t slotNum;
    int wayNum = 0;
    
    character->Inventory().CreateGlyphList(slots);
    
    psRequestGlyphsMessage outMessage( client->GetClientNum() );     
    for (slotNum=0; slotNum < slots.GetSize(); slotNum++)
    {
        csString way;
        PSITEMSTATS_SLOTLIST validSlots;
        int statID;

        validSlots = slots[slotNum].glyphType->GetValidSlots();
        statID = slots[slotNum].glyphType->GetUID();

        if (validSlots & PSITEMSTATS_SLOT_CRYSTAL)
        {
            wayNum = 0;
        }
        else if (validSlots & PSITEMSTATS_SLOT_BLUE) 
        {
            wayNum = 1;
        }
        else if (validSlots & PSITEMSTATS_SLOT_AZURE) 
        {
            wayNum = 2;
        }
        else if (validSlots & PSITEMSTATS_SLOT_BROWN)
        {
            wayNum = 3;
        }
        else if (validSlots & PSITEMSTATS_SLOT_RED)
        {
            wayNum = 4;
        }
        else if (validSlots & PSITEMSTATS_SLOT_DARK)
        {
            wayNum = 5;
        }

        outMessage.AddGlyph( slots[slotNum].glyphType->GetName(),
                             slots[slotNum].glyphType->GetImageName(),
                             slots[slotNum].purifyStatus,
                             wayNum, 
                             statID );
                             
    }
    
    outMessage.Construct();
    outMessage.SendMessage();    
}

psGlyph * FindUnpurifiedGlyph(psCharacter * character, unsigned int statID)
{
    size_t index;
    for (index=0; index < character->Inventory().GetInventoryIndexCount(); index++)
    {
        psItem *item = character->Inventory().GetInventoryIndexItem(index);

        psGlyph *glyph = dynamic_cast <psGlyph*> (item);
        if (glyph != NULL)
        {
            if (glyph->GetBaseStats()->GetUID()==statID   &&   glyph->GetPurifyStatus()==0)
            {
                return glyph;
            }
        }
    }
    return NULL;
}

void psSpellManager::StartPurifying(Client * client, int statID)
{
    psCharacter* character = client->GetCharacterData();

    psGlyph* glyph = FindUnpurifiedGlyph(character, statID);

    if (glyph == NULL)
    {
        return;
    }        

    if (glyph->GetStackCount() > 1)
    {
        if ( !character->Inventory().HowManyCanFit(glyph) )
        {
            psserver->SendSystemError(client->GetClientNum(), "Your inventory is full!" );
            SendGlyphs(client);
            return;
        }

        psGlyph* stackOfGlyphs = glyph;

        glyph = dynamic_cast <psGlyph*> (stackOfGlyphs->SplitStack(1));
        if (glyph == NULL)
        {
            return;
        }            
        
        psItem *glyphItem = glyph; // Needed for reference in next function
        if (!character->Inventory().Add(glyphItem,false,false))
        {
            Error2("Failed to move purifying glyph to bulk slot for %s", client->GetName() );
            CacheManager::GetSingleton().RemoveInstance(glyphItem);

            // Reset the stack count to account for the one that was destroyed.
            stackOfGlyphs->SetStackCount(stackOfGlyphs->GetStackCount() + 1);
            return;
        }
        stackOfGlyphs->Save(false);
        glyph->Save(false);
    }

    glyph->PurifyingStarted();
    
    // If the glyph has no ID, we must save it now because we need to have an ID for the purification script
    glyph->ForceSaveIfNew();

    // Use the progression manager to purify so that the
    // purify event will continue when player reconnect if
    // not finished before disconnect.
    psserver->SendSystemInfo(client->GetClientNum(), "You start to purify %s", glyph->GetName() );
    csString event;
    event.Format("<evt><script delay=\"%d\" persistent=\"yes\"><purify glyph=\"%u\"/></script></evt>",20000,glyph->GetUID());
    psserver->GetProgressionManager()->ProcessScript(event.GetData(),client->GetActor(),client->GetActor());

    SendGlyphs(client);
    psserver->GetCharManager()->SendInventory(client->GetClientNum());
}

void psSpellManager::EndPurifying(psCharacter * character, uint32 glyphUID)
{
    Client * client = clients->FindPlayer(character->GetCharacterID());
    if (!client)
    {
        Error1("No purifyer!");
        return;
    }

    psItem *item = character->Inventory().FindItemID(glyphUID);
    if (item)
    {
        psGlyph * glyph = dynamic_cast <psGlyph*> (item);
        if (glyph)
        {
            glyph->PurifyingFinished();
            glyph->Save(false);
            psserver->SendSystemInfo(client->GetClientNum(), "The glyph %s is now purified", glyph->GetName());
            SendGlyphs(client);
            psserver->GetCharManager()->SendInventory(client->GetClientNum());
            return;
        }
    }
}

psSpell* psSpellManager::FindSpell(Client * client, const glyphList_t & assembler)
{
    CacheManager::SpellIterator loop = CacheManager::GetSingleton().GetSpellIterator();
    
    psSpell *p;
    
    while (loop.HasNext())
    {
        p = loop.Next();

        if (p->MatchGlyphs(assembler))
        {
            // Combination of glyphs correct
            return p;
        }
    }

    return NULL;
}

psSpell* psSpellManager::FindSpell(csString& name)
{
    return CacheManager::GetSingleton().GetSpellByName(name);
}

psSpell* psSpellManager::FindSpell(int spellID)
{
    return CacheManager::GetSingleton().GetSpellByID(spellID);
}

/**
 * This is the meat and potatoes of the spell engine here.
 */
void psSpellManager::HandleSpellCastEvent(psSpellCastGameEvent *event)
{
    csString  responseEffectName;
    csVector3 offset;
    PS_ID     anchorID = 0;
    PS_ID     targetID = 0;
    csString  affectText;
             
    // Start the effect if spell is successfully cast
    gemActor * caster = event->caster->GetActor();
    const psSpell * spell = event->spell;

    // Check for spell failure
    float chanceOfSuccess = spell->ChanceOfSuccess( caster->GetCharacterData()->GetKFactor(), caster->GetCharacterData()->GetSkills()->GetSkillRank( spell->GetSkill() ), caster->GetCharacterData()->GetSkills()->GetSkillRank( spell->GetRelatedStat() ) );

    Notify4(LOG_SPELLS, "%s Casting %s with a chance of success = %.2f\n",caster->GetName(), spell->GetName().GetData(), chanceOfSuccess);

    if ( psserver->GetRandom() * 100.0 > chanceOfSuccess )
    {
        // Spell casting failed
        affectText.Format( "You failed to cast the spell %s" , spell->GetName().GetData() );

        psEffectMessage newmsg(0, "spell_failure", offset, anchorID, targetID, 0, 0);
        if (newmsg.valid)
        {
            newmsg.Multicast(caster->GetMulticastClients(),0,PROX_LIST_ANY_RANGE);
        }            
        else
        {
            Bug1("Could not create valid psEffectMessage for broadcast.\n");
        }

        // Only drain 10% of mana.
        caster->DrainMana(-(spell->ManaCost(caster->GetCharacterData()->GetKFactor())/10),false);
    }
    else
    {
        // Drain full mana amount.
        caster->DrainMana(-(spell->ManaCost(caster->GetCharacterData()->GetKFactor())),false);

        // Spell casting succeeded, find out what targets are affected.
        if ( spell->AffectTargets( this, event, responseEffectName, offset, anchorID, targetID, &affectText ) )
        {
            // Only gain practice if the spell was effective
            caster->GetCharacterData()->GetSkills()->AddSkillPractice( spell->GetSkill(), 1 );
        }

        // If there is some sort of visual/particle/audio effect for the target then fire it out.
        if (responseEffectName && responseEffectName != "")
        {
            psEffectMessage newmsg( 0, responseEffectName, offset, anchorID, targetID, 0, 0 );
            if ( newmsg.valid )
            {
                newmsg.Multicast( event->target->GetMulticastClients(), 0, PROX_LIST_ANY_RANGE );
            }                
            else
            {
                Bug1( "Could not create valid psEffectMessage for broadcast.\n" );
            }
        }
    }

    // Spell Casting complete, we are now in Peace mode again.
    caster->SetMode( PSCHARACTER_MODE_PEACE );
    event->caster->GetCharacterData()->SetSpellCasting( NULL );

    // Inform player
    if ( affectText != "" )
    {
        psserver->SendSystemInfo( event->caster->GetActor()->GetClientID(), affectText.GetData() );
    }
}

void psSpellManager::HandleSpellAffectEvent( psSpellAffectGameEvent *event )
{ 
    // Since we just came in from an event, make sure target is still alive.
    if ( event->target->IsAlive() || event->inverse )
    {
        event->spell->PerformResult( event->caster->GetActor(), event->target, event->max_range, event->saved, event->powerLevel, event->inverse);
    }

    // We may have just killed the target, lets check to make sure it's still alive.
    /*
    if ( event->target->IsAlive() )
    {
        // If this is a DoT spell then set up the next event.
        if ( event->interval != 0 )
        {
            csTicks duration, interval;
            if ( event->duration > event->interval )
            {
                interval = event->interval;
                duration = event->duration - event->interval;
            }
            else
            {
                interval = event->duration;
                duration = 0;
            }

            if ( ( interval > 0 ) && ( duration > 0 ) )
            {
                psSpellAffectGameEvent *e = new psSpellAffectGameEvent( psserver->GetSpellManager(), event->spell, 
                                                                        event->caster, event->target,
                                                                        event->interval, event->min_range,
                                                                        event->max_range, event->saved, event->powerLevel,
                                                                        interval, duration);
                e->QueueEvent();
            }
        }
    }
    */
}

/*-------------------------------------------------------------*/

psSpellCastGameEvent::psSpellCastGameEvent(psSpellManager *mgr,
                                   const psSpell * spell,
                                   Client *caster,
                                   gemObject *target,
                                   csTicks castingDuration,
                                   float max_range,
                                   float powerLevel,
                                   csTicks duration )
    : psGameEvent(0,castingDuration,"psSpellCastGameEvent")
{
    spellmanager     = mgr;
    this->spell      = spell;
    this->caster     = caster;
    this->target     = target;
    valid            = true;
    this->max_range = max_range;
    this->powerLevel= powerLevel;
    this->duration = duration;

    target->RegisterCallback( this );
    caster->GetActor()->RegisterCallback( this );
    caster->GetCharacterData()->SetSpellCasting(this);
}

psSpellCastGameEvent::~psSpellCastGameEvent()
{
    if ( target )
    {
        target->UnregisterCallback(this);
    }        
    if ( caster )
    {
        caster->GetActor()->UnregisterCallback(this);    
    }
}

void psSpellCastGameEvent::DeleteObjectCallback(iDeleteNotificationObject * object)
{
    if ( target )
    {
        target->UnregisterCallback(this);
    }        
    if ( caster )        
    {
        caster->GetActor()->UnregisterCallback(this);
    }        

    Interrupt();   

    target = NULL;
    caster = NULL;    
}

void psSpellCastGameEvent::Interrupt()
{
    // Check if this event have been stoped before
    if (!IsValid())
    {
        return;
    }
    
    psserver->SendSystemInfo(caster->GetClientNum(),"Your spell (%s) has been interrupted!",spell->GetName().GetData());

    caster->GetActor()->SetMode( PSCHARACTER_MODE_PEACE );
    caster->GetCharacterData()->SetSpellCasting(NULL);

    // Stop event from beeing executet when trigged.
    SetValid(false);
}

void psSpellCastGameEvent::Trigger()
{
    spellmanager->HandleSpellCastEvent(this);
}

/*-------------------------------------------------------------*/

psSpellAffectGameEvent::psSpellAffectGameEvent(psSpellManager *mgr,
                                   const psSpell *spell,
                                   Client  *caster, 
                                   gemObject *target, 
                                   csTicks progression_delay,
                                   float max_range,
                                   bool saved,
                                   float powerLevel,
                                   csTicks duration,
                                   bool inverse)
    : psGameEvent(0, progression_delay,"psSpellAffectGameEvent")
{
    spellmanager     = mgr;
    this->spell      = spell;
    this->caster     = caster;
    this->target     = target;
    this->max_range  = max_range;
    this->saved      = saved;
    this->powerLevel = powerLevel;
    this->duration   = duration;
    this->inverse    = inverse;

    target->RegisterCallback( dynamic_cast< iDeleteObjectCallback *>(this) );
    caster->GetActor()->RegisterCallback( dynamic_cast< iDeleteObjectCallback *>(this) );
    gemActor * targetActor = dynamic_cast<gemActor*>(target);
    if ( targetActor)
    {
        targetActor->RegisterCallback( dynamic_cast< iDeathCallback *>(this) );
    }
            
    caster->GetActor()->RegisterCallback( dynamic_cast< iDeathCallback *>(this) );

    /*CPrintf( CON_DEBUG, "Created Spell Effect [%s, %s, %s, %.2f, %u, %.2f, %u, %u, %u ].\n", 
            spell?spell->GetName().GetData():"NULL",
            caster?caster->GetActor()->GetName():"NULL",
            target?target->GetName():"NULL",
            max_range,
            saved,
            powerLevel,
            progression_delay,
            duration,
            inverse
            ); */

}


psSpellAffectGameEvent::~psSpellAffectGameEvent()
{
    if ( target )
    {
        target->UnregisterCallback( dynamic_cast<iDeleteObjectCallback *>(this));
        gemActor * targetActor = dynamic_cast<gemActor*>(target);
        if ( targetActor)
        {
            targetActor->UnregisterCallback( dynamic_cast<iDeathCallback *>(this));
        }            
    }
    if ( caster )
    {
        caster->GetActor()->UnregisterCallback(dynamic_cast<iDeleteObjectCallback*>(this));    
        caster->GetActor()->UnregisterCallback(dynamic_cast<iDeathCallback*>(this));    
    }
}

void psSpellAffectGameEvent::DeleteObjectCallback(iDeleteNotificationObject * object)
{
    SetValid(false); // Prevent the Trigger from being called.

    if ( target )
    {
        target->UnregisterCallback( dynamic_cast<iDeleteObjectCallback *>(this));
        target->UnregisterCallback( dynamic_cast<iDeleteObjectCallback *>(this));
        gemActor * targetActor = dynamic_cast<gemActor*>(target);
        if ( targetActor)
            targetActor->UnregisterCallback( dynamic_cast<iDeathCallback *>(this));
    }
    if ( caster )
    {
        caster->GetActor()->UnregisterCallback(dynamic_cast<iDeleteObjectCallback*>(this));    
        caster->GetActor()->UnregisterCallback(dynamic_cast<iDeathCallback*>(this));    
    }

    target = NULL;
    caster = NULL;    
}

/** 
 * Called when the actor or target object dies.
 */
void psSpellAffectGameEvent::DeathCallback(iDeathNotificationObject * object)
{
    this->Trigger();
    this->SetValid(false);
}


void psSpellAffectGameEvent::Trigger() 
{
    if ( IsValid() )
    {
        spellmanager->HandleSpellAffectEvent(this);
    }        
}
