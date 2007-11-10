/*
* pscharacter.cpp
*
* Copyright (C) 2001 Atomic Blue (info@planeshift.it, http://www.atomicblue.org)
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

#include <iutil/virtclk.h>
#include <csutil/databuf.h>
#include <ctype.h>

// Define this so the slot ID to string mapping is compiled into object form exactly once
#define PSCHARACTER_CPP

#include "../psserver.h"
#include "util/psdatabase.h"
#include "../cachemanager.h"
#include "pscharacterloader.h"
#include "../psserverchar.h"
#include "../globals.h"
#include "util/log.h"
#include "../exchangemanager.h"
#include "../spellmanager.h"
#include "../workmanager.h"
#include "../marriagemanager.h"
#include "../npcmanager.h"
#include "psglyph.h"
#include "psquest.h"
#include "dictionary.h"
#include "psraceinfo.h"
#include "psguildinfo.h"
#include "psmerchantinfo.h"
#include "pstrainerinfo.h"
#include "util/psxmlparser.h"
#include "util/serverconsole.h"
#include "util/mathscript.h"
#include "util/log.h"
#include "../playergroup.h"
#include "../events.h"
#include "servervitals.h"
#include "../progressionmanager.h"
#include "../chatmanager.h"
#include "../commandmanager.h"
#include "../gmeventmanager.h"
#include "rpgrules/factions.h"

// The sizes and scripts need balancing.  For now, maxSize is disabled.
#define ENABLE_MAX_CAPACITY 0


const char *psCharacter::characterTypeName[] = { "player", "npc", "pet" };


//-----------------------------------------------------------------------------

// Definition of the itempool for psCharacter(s)
PoolAllocator<psCharacter> psCharacter::characterpool;

const char * psCharacter::player_mode_to_str[] =
 {"unknown","peace","combat","spell casting","working","dead","sitting","carrying too much","exhausted"};

void *psCharacter::operator new(size_t allocSize)
{
// Debug3(LOG_CHARACTER,"%i %i", allocSize,sizeof(psCharacter));
//    CS_ASSERT(allocSize<=sizeof(psCharacter));
    return (void *)characterpool.CallFromNew();
}

void psCharacter::operator delete(void *releasePtr)
{
    characterpool.CallFromDelete((psCharacter *)releasePtr);
}

psCharacter::psCharacter() : inventory(this),
    guildinfo(NULL), attributes(this), modifiers(this),
    skills(this), npc_masterid(0), loaded(false)
{
    characterType = PSCHARACTER_TYPE_UNKNOWN;

    helmGroup = "";
    help_event_flags = 0;
    situation_wum = 0.0f;
    effect_wum = 0.0f;
    memset(advantage_bitfield,0,sizeof(advantage_bitfield));
    accountid = 0;
    characterid = 0;
    familiar_id = 0;
    owner_id = 0;

    animal_affinity = "";

    override_max_hp = 0.0f;
    override_max_mana = 0.0f;

    name = lastname = fullname = " ";
    SetSpouseName( "" );
    isMarried = false;

    raceinfo = NULL;
    combat_stance = getStance("None");
    vitals = new psServerVitals(this);
//    workInfo = new WorkInformation();

    loot_category_id = 0;
    loot_money = 0;

    location.loc_sector = NULL;
    location.loc_x = 0.0f;
    location.loc_y = 0.0f;
    location.loc_z = 0.0f;
    location.loc_yrot = 0.0f;
    spawn_loc = location;

    for (int i=0;i<PSTRAIT_LOCATION_COUNT;i++)
        traits[i] = NULL;

    npc_spawnruleid = -1;

    tradingStopped = false;
    tradingStatus = psCharacter::NOT_TRADING;
    merchant = NULL;
    trainer = NULL;
    actor = NULL;
    merchantInfo = NULL;
    trainerInfo = NULL;

    timeconnected = 0;
    startTimeThisSession = csGetTicks();

    KFactor = 0.0;
    spellCasting = NULL;

//    transformation = NULL;
    work_state = PSCHARACTER_WORKSTATE_HALTED;
    workEvent = NULL;
    kill_exp = 0;
    impervious_to_attack = 0;

    faction_standings = "";
    nextProgressionEventID = 1;

    attackValueModifier = 1;
    defenseValueModifier = 1;
    meleeDefensiveDamageModifier = 1;

    player_mode = PSCHARACTER_MODE_PEACE;
    lastError = "None";

    staminaCalc = psserver->GetMathScriptEngine()->FindScript("StaminaBase");
    if( !staminaCalc)
        Warning1(LOG_CHARACTER, "Can't find math script StaminaBase!");

    lastResponse = -1;

    banker = false;
}

psCharacter::~psCharacter()
{
    if (guildinfo)
        guildinfo->Disconnect(this);

    // First force and update of the DB of all QuestAssignments before deleting
    // every assignment.
    UpdateQuestAssignments(false);
    assigned_quests.DeleteAll();

    delete vitals;
    vitals = NULL;
//    delete workInfo;
}

void psCharacter::SetActor( gemActor* newActor )
{
    actor = newActor;
    if (actor)
    {
        inventory.RunEquipScripts();
        inventory.CalculateLimits();
    }
}

bool psCharacter::Load(iResultRow& row)
{
    // TODO:  Link in account ID?
    SetCharacterID(row.GetInt("id"));
    SetAccount(row.GetInt("account_id"));
    SetCharType( row.GetUInt32("character_type") );

    SetFullName(row["name"], row["lastname"]);
    SetOldLastName( row["old_lastname"] );

    unsigned int raceid = row.GetUInt32("racegender_id");
    psRaceInfo *raceinfo = CacheManager::GetSingleton().GetRaceInfoByID(raceid);
    if (!raceinfo)
    {
        Error3("Character ID %s has unknown race id %s.",row["id"],row["racegender_id"]);
        return false;
    }
    SetRaceInfo(raceinfo);

    //Assign the Helm Group
    Result helmResult(db->Select("SELECT helm FROM race_info WHERE id=%d", raceid));
    helmGroup = helmResult[0]["helm"];

    SetDescription(row["description"]);

    attributes.SetStat(PSITEMSTATS_STAT_STRENGTH,(unsigned int)row.GetFloat("base_strength"), false);
    attributes.SetStat(PSITEMSTATS_STAT_AGILITY,(unsigned int)row.GetFloat("base_agility"), false);
    attributes.SetStat(PSITEMSTATS_STAT_ENDURANCE,(unsigned int)row.GetFloat("base_endurance"), false);
    attributes.SetStat(PSITEMSTATS_STAT_INTELLIGENCE,(unsigned int)row.GetFloat("base_intelligence"), false);
    attributes.SetStat(PSITEMSTATS_STAT_WILL,(unsigned int)row.GetFloat("base_will"), false);
    attributes.SetStat(PSITEMSTATS_STAT_CHARISMA,(unsigned int)row.GetFloat("base_charisma"), false);

    // NPC fields here
    npc_spawnruleid = row.GetUInt32("npc_spawn_rule");
    npc_masterid    = row.GetUInt32("npc_master_id");

    // This substitution allows us to make 100 orcs which are all copies of the stats, traits and equipment
    // from a single master instance.
    uint32_t use_id = (npc_masterid)?npc_masterid:characterid;

    SetHitPointsMax(row.GetFloat("base_hitpoints_max"));
    override_max_hp = GetHitPointsMax();
    SetManaMax(row.GetFloat("base_mana_max"));
    override_max_mana = GetManaMax();

    if (!LoadSkills(use_id))
    {
        Error2("Cannot load skills for Character ID %u.",characterid);
        return false;
    }

    RecalculateStats();

    // If mod_hp or mod_mana are set < 0 in the db, then that means
    // to use whatever is calculated as the max, so npc's spawn at 100%.
    float mod = row.GetFloat("mod_hitpoints");
    SetHitPoints( mod < 0 ? GetHitPointsMax() : mod );
    mod = row.GetFloat("mod_mana");
    SetMana( mod < 0 ? GetManaMax() : mod );
    SetStamina(row.GetFloat("stamina_physical"),true);
    SetStamina(row.GetFloat("stamina_mental"),false);

    vitals->SetOrigVitals(); // This saves them as loaded state for restoring later without hitting db, npc death resurrect.

    lastlogintime = row["last_login"];
    faction_standings = row["faction_standings"];
    progressionEventsText = row["progression_script"];

    // Set on-hand money.
    money.Set(
        row.GetInt("money_circles"),
        row.GetInt("money_octas"),
        row.GetInt("money_hexas"),
        row.GetInt("money_trias"));

    // Set bank money.
    bankMoney.Set(
        row.GetInt("bank_money_circles"),
        row.GetInt("bank_money_octas"),
        row.GetInt("bank_money_hexas"),
        row.GetInt("bank_money_trias"));

    psSectorInfo *sectorinfo=CacheManager::GetSingleton().GetSectorInfoByID(row.GetUInt32("loc_sector_id"));
    if (sectorinfo==NULL)
    {
        Error3("Character ID %u has unresolvable sector id %lu.",characterid,row.GetUInt32("loc_sector_id"));
        return false;
    }

    SetLocationInWorld(row.GetInt("loc_instance"),
                       sectorinfo,
                       row.GetFloat("loc_x"),
                       row.GetFloat("loc_y"),
                       row.GetFloat("loc_z"),
                       row.GetFloat("loc_yrot") );
    spawn_loc = location;

    // Guild fields here
    guildinfo = CacheManager::GetSingleton().FindGuild(row.GetUInt32("guild_member_of"));
    if (guildinfo)
        guildinfo->Connect(this);

    // Loot rule here
    loot_category_id = row.GetInt("npc_addl_loot_category_id");

    impervious_to_attack = (row["npc_impervious_ind"][0]=='Y') ? ALWAYS_IMPERVIOUS : 0;

    // Familiar Fields here
    animal_affinity  = row[ "animal_affinity" ];
    //owner_id         = row.GetUInt32( "owner_id" );
    help_event_flags = row.GetUInt32("help_event_flags");

    if (!LoadTraits(use_id))
    {
        Error2("Cannot load traits for Character ID %u.",characterid);
        return false;
    }

    if (!LoadAdvantages(use_id))
    {
        Error2("Cannot load advantages for Character ID %u.",characterid);
        return false;
    }

    // This data is loaded only if it's a player, not an NPC
    if ( !IsNPC() && !IsPet() )
    {
        if (!LoadQuestAssignments())
        {
            Error2("Cannot load quest assignments for Character ID %u.",characterid);
            return false;
        }

        if (!LoadGMEvents())
        {
            Error2("Cannot load GM Events for Character ID %u.", characterid);
            return false;
        }

    }

    if (npc_masterid && use_id != (uint)npc_masterid )
    {
        // also load character specific items
        if (!inventory.Load(characterid))
        {
            Error2("Cannot load character specific items for Character ID %u.",characterid);
            return false;
        }
    }
    else
    {
        inventory.Load();
    }

    if ( !LoadRelationshipInfo( characterid ) ) // Buddies, Marriage Info, Familiars
    {
        return false;
    }

    // Load merchant info
    csRef<psMerchantInfo> merchant = csPtr<psMerchantInfo>(new psMerchantInfo());
    if (merchant->Load(use_id))
    {
        merchantInfo = merchant;
    }

    // Load trainer info
    csRef<psTrainerInfo> trainer = csPtr<psTrainerInfo>(new psTrainerInfo());
    if (trainer->Load(use_id))
    {
        trainerInfo = trainer;
    }

    if (!LoadSpells(use_id))
    {
        Error2("Cannot load spells for Character ID %u.",characterid);
        return false;
    }

    timeconnected        = row.GetUInt32("time_connected_sec");
    startTimeThisSession = csGetTicks();

    // Load Experience Points W and Progression Points X
    SetExperiencePoints(row.GetUInt32("experience_points"));
    SetProgressionPoints(row.GetUInt32("progression_points"),false);

    // Load the kill exp
    kill_exp = row.GetUInt32("kill_exp");

    // Load the math script
    powerScript = psserver->GetMathScriptEngine()->FindScript("CalculatePowerLevel");
    if ( !powerScript )
    {
        Warning1(LOG_CHARACTER, "Can't find math script CalculatePowerLevel!");
        return false;
    }
    maxRealmScript = psserver->GetMathScriptEngine()->FindScript("MaxRealm");
    if ( !maxRealmScript )
    {
        Warning1(LOG_CHARACTER, "Can't find math script MaxRealm!");
        return false;
    }

    // Load if the character/npc is a banker
    if(row.GetInt("banker") == 1)
        banker = true;

    loaded = true;
    return true;
}

bool psCharacter::QuickLoad(iResultRow& row, bool noInventory)
{
    SetCharacterID(row.GetInt("id"));
    SetFullName(row["name"], row["lastname"]);

    unsigned int raceid = row.GetUInt32("racegender_id");
    psRaceInfo *raceinfo = CacheManager::GetSingleton().GetRaceInfoByID(raceid);
    if (!raceinfo)
    {
        Error3("Character ID %s has unknown race id %s.",row["id"],row["racegender_id"]);
        return false;
    }

    if (!noInventory)
    {
        SetRaceInfo(raceinfo);

        Result result(db->Select("SELECT base_strength,base_agility, base_endurance, base_intelligence, base_will, base_charisma from characters where id=%u LIMIT 1",characterid));

        attributes.SetStat(PSITEMSTATS_STAT_STRENGTH,(unsigned int)result[0].GetFloat("base_strength"), false);

        attributes.SetStat(PSITEMSTATS_STAT_AGILITY,(unsigned int)result[0].GetFloat("base_agility"), false);
        attributes.SetStat(PSITEMSTATS_STAT_ENDURANCE,(unsigned int)result[0].GetFloat("base_endurance"), false);
        attributes.SetStat(PSITEMSTATS_STAT_INTELLIGENCE,(unsigned int)result[0].GetFloat("base_intelligence"), false);
        attributes.SetStat(PSITEMSTATS_STAT_WILL,(unsigned int)result[0].GetFloat("base_will"), false);
        attributes.SetStat(PSITEMSTATS_STAT_CHARISMA,(unsigned int)result[0].GetFloat("base_charisma"), false);

        if (!LoadSkills(characterid))
        {
            Error2("Cannot load skills for Character ID %u.",characterid);
            return false;
        }

        Result helmResult(db->Select("SELECT helm FROM race_info WHERE id=%d", raceid));
        helmGroup = helmResult[0]["helm"];

        if (!LoadTraits(characterid))
        {
            Error2("Cannot load traits for Character ID %u.",characterid);
            return false;
        }

        // Load equipped items
        inventory.QuickLoad(characterid);
    }

    return true;
}

bool psCharacter::LoadRelationshipInfo( unsigned int characterid )
{

    Result has_a( db->Select( "SELECT a.*, b.name AS 'buddy_name' FROM character_relationships a, characters b WHERE a.related_id = b.id AND a.character_id = %u", characterid ) );
    Result of_a( db->Select( "SELECT a.*, b.name AS 'buddy_name' FROM character_relationships a, characters b WHERE a.character_id = b.id AND a.related_id = %u", characterid ) );

    if ( !LoadFamiliar( has_a, of_a ) )
    {
        Error2("Cannot load familiar info for Character ID %u.",characterid);
        return false;
    }

    if ( !LoadMarriageInfo( has_a ) )
    {
      Error2("Cannot load Marriage Info for Character ID %u.",characterid);
      return false;
    }

    if ( !LoadBuddies( has_a, of_a ) )
    {
        Error2("Cannot load buddies for Character ID %u.",characterid);
        return false;
    }
    return true;
}

bool psCharacter::LoadBuddies( Result& myBuddies, Result& buddyOf )
{
    unsigned int x;

    if ( !myBuddies.IsValid() )
      return true;

    for ( x = 0; x < myBuddies.Count(); x++ )
    {

        if ( strcmp( myBuddies[x][ "relationship_type" ], "buddy" ) == 0 )
        {
            Buddy newBud;
            newBud.name = myBuddies[x][ "buddy_name" ];
            newBud.playerID = myBuddies[x].GetUInt32( "related_id" );

            buddyList.Insert( 0, newBud );
        }
    }


    // Load all the people that I am a buddy of. This is used to inform these people
    // of when I log in/out.

    for (x = 0; x < buddyOf.Count(); x++ )
    {
        if ( strcmp( buddyOf[x][ "relationship_type" ], "buddy" ) == 0 )
        {
            buddyOfList.Insert( 0, buddyOf[x].GetUInt32( "character_id" ) );
        }
    }

    return true;
}

bool psCharacter::LoadMarriageInfo( Result& result)
{
    //Result result( db->Select("SELECT * FROM character_marriage_details"
    //                          " WHERE character_id=%d", characterid));
    if ( !result.IsValid() )
    {
        Error3("Could not load marriage info for character %d. Error was: %s", characterid, db->GetLastError() );
        return false;
    }

    for ( unsigned int x = 0; x < result.Count(); x++ )
    {

        if ( strcmp( result[x][ "relationship_type" ], "spouse" ) == 0 )
        {
            const char* spouseName = result[x]["spousename"];
            if ( spouseName == NULL )
                return true;

            SetSpouseName( spouseName );

            Notify2( LOG_MARRIAGE, "Successfully loaded marriage info for %s", name.GetData() );
            break;
        }
    }

    return true;
}

bool psCharacter::LoadFamiliar( Result& pet, Result& owner )
{
    familiar_id = 0;
    owner_id = 0;

    if ( !pet.IsValid() )
    {
        Error3("Could not load pet info for character %d. Error was: %s", characterid, db->GetLastError() );
        return false;
    }

    if ( !owner.IsValid() )
    {
        Error3("Could not load owner info for character %d. Error was: %s", characterid, db->GetLastError() );
        return false;
    }

    unsigned int x;
    for ( x = 0; x < pet.Count(); x++ )
    {

        if ( strcmp( pet[x][ "relationship_type" ], "familiar" ) == 0 )
        {
            familiar_id = pet[x].GetInt( "related_id" );
            Notify2( LOG_MARRIAGE, "Successfully loaded familair for %s", name.GetData() );
            break;
        }
    }

    for ( x = 0; x < owner.Count(); x++ )
    {

        if ( strcmp( owner[x][ "relationship_type" ], "familiar" ) == 0 )
        {
            owner_id = owner[x].GetInt( "character_id" );
            Notify2( LOG_MARRIAGE, "Successfully loaded owner for %s", name.GetData() );
            break;
        }
    }

    return true;
}

/// Load GM events for this player from GMEventManager
bool psCharacter::LoadGMEvents(void)
{
    assigned_events.runningEventID =
        psserver->GetGMEventManager()->GetAllGMEventsForPlayer(characterid,
                                                               assigned_events.completedEventIDs,
                                                               assigned_events.runningEventIDAsGM,
                                                               assigned_events.completedEventIDsAsGM);
    return true;  // cant see how this can fail, but keep convention for now.
}

void psCharacter::SetLastLoginTime(const char *last_login, bool save )
{
    csString timeStr;

    if ( !last_login )
    {
        time_t curr=time(0);
        tm* gmtm = gmtime(&curr);

        timeStr.Format("%d-%02d-%02d %02d:%02d:%02d",
                        gmtm->tm_year+1900,
                        gmtm->tm_mon+1,
                        gmtm->tm_mday,
                        gmtm->tm_hour,
                        gmtm->tm_min,
                        gmtm->tm_sec);
    }
    else
    {
        timeStr = last_login;
    }

    this->lastlogintime = timeStr;

    if ( save )
    {
        //Store in database
        if(!db->CommandPump("UPDATE characters SET last_login='%s' WHERE id='%d'", timeStr.GetData(),
            this->GetCharacterID()))
        {
             Error2( "Last login storage: DB Error: %s\n", db->GetLastError() );
             return;
        }
    }
}

csString psCharacter::GetLastLoginTime()
{
    return this->lastlogintime;
}


bool psCharacter::LoadSpells(unsigned int use_id)
{
    // Load spells in asc since we use push to create the spell list.
    Result spells(db->Select("SELECT * from player_spells where player_id=%u order by spell_slot asc",use_id));
    if (spells.IsValid())
    {
        int i,count=spells.Count();

        for (i=0;i<count;i++)
        {
            psSpell * spell = CacheManager::GetSingleton().GetSpellByID(spells[i].GetInt("spell_id"));
            if (spell != NULL)
                AddSpell(spell);
            else
            {
                Error2("Spell id=%i not found in cachemanager", spells[i].GetInt("spell_id"));
            }
        }
        return true;
    }
    else
        return false;
}

bool psCharacter::LoadAdvantages(unsigned int use_id)
{
    // Load advantages/disadvantages
    Result adv(db->Select("SELECT * from character_advantages where character_id=%u",use_id));
    if (adv.IsValid())
    {
        unsigned int i;
        for (i=0;i<adv.Count();i++)
        {
            AddAdvantage((PSCHARACTER_ADVANTAGE)(adv[i].GetInt("advantage_id")));
        }
       return true;
    }
    else
        return false;
}

bool psCharacter::LoadSkills(unsigned int use_id)
{
    // Load skills
    Result skillResult(db->Select("SELECT * from character_skills where character_id=%u",use_id));

    for ( int z = 0; z < PSSKILL_COUNT; z++ )
    {
        skills.SetSkillInfo( (PSSKILL)z, CacheManager::GetSingleton().GetSkillByID((PSSKILL)z), false );
    }

    if (skillResult.IsValid())
    {
        unsigned int i;
        for (i=0;i<skillResult.Count();i++)
        {
            if (skillResult[i]["skill_id"]!=NULL)
            {
                PSSKILL skill = (PSSKILL)skillResult[i].GetInt("skill_id");
                skills.SetSkillPractice(  skill, skillResult[i].GetInt("skill_Z") );
                skills.SetSkillKnowledge( skill, skillResult[i].GetInt("skill_Y") );
                skills.SetSkillRank(      skill, skillResult[i].GetInt("skill_Rank"),false );
                skills.GetSkill(skill)->dirtyFlag = false;
            }
        }
        skills.Calculate();
    }
    else
        return false;

    // Set stats ranks
    skills.SetSkillRank( PSSKILL_AGI , attributes.GetStat( PSITEMSTATS_STAT_AGILITY        , false), false);
    skills.SetSkillRank( PSSKILL_CHA , attributes.GetStat( PSITEMSTATS_STAT_CHARISMA       , false), false);
    skills.SetSkillRank( PSSKILL_END , attributes.GetStat( PSITEMSTATS_STAT_ENDURANCE      , false), false);
    skills.SetSkillRank( PSSKILL_INT , attributes.GetStat( PSITEMSTATS_STAT_INTELLIGENCE   , false), false);
    skills.SetSkillRank( PSSKILL_WILL ,attributes.GetStat( PSITEMSTATS_STAT_WILL           , false), false);
    skills.SetSkillRank( PSSKILL_STR,  attributes.GetStat( PSITEMSTATS_STAT_STRENGTH       , false), false);

    skills.GetSkill(PSSKILL_AGI)->dirtyFlag = false;
    skills.GetSkill(PSSKILL_CHA)->dirtyFlag = false;
    skills.GetSkill(PSSKILL_END)->dirtyFlag = false;
    skills.GetSkill(PSSKILL_INT)->dirtyFlag = false;
    skills.GetSkill(PSSKILL_WILL)->dirtyFlag = false;
    skills.GetSkill(PSSKILL_STR)->dirtyFlag = false;
    
    return true;
}

bool psCharacter::LoadTraits(unsigned int use_id)
{
    // Load traits
    Result traits(db->Select("SELECT * from character_traits where character_id=%u",use_id));
    if (traits.IsValid())
    {
        unsigned int i;
        for (i=0;i<traits.Count();i++)
        {
            psTrait *trait=CacheManager::GetSingleton().GetTraitByID(traits[i].GetInt("trait_id"));
            if (!trait)
            {
                Error3("Player ID %u has unknown trait id %s.",characterid,traits[i]["trait_id"]);
            }
            else
                SetTraitForLocation(trait->location,trait);
        }
        return true;
    }
    else
        return false;
}

void psCharacter::AddSpell(psSpell * spell)
{
    spellList.Push(spell);
}

void psCharacter::SetFullName(const char* newFirstName, const char* newLastName)
{
    if ( !newFirstName )
    {
        Error1( "Null passed as first name..." );
        return;
    }

    // Error3( "SetFullName( %s, %s ) called...", newFirstName, newLastName );
    // Update fist, last & full name
    if ( strlen(newFirstName) )
    {
        name = newFirstName;
        fullname = name;
    }
    if ( newLastName )
    {
        lastname = newLastName;

        if ( strlen(newLastName) )
        {
            fullname += " ";
            fullname += lastname;
        }
    }

    //Error2( "New fullname is now: %s", fullname.GetData() );
}

void psCharacter::SetRaceInfo(psRaceInfo *rinfo)
{
    raceinfo=rinfo;

    if ( !rinfo )
        return;

    attributes.SetStat(PSITEMSTATS_STAT_STRENGTH,(unsigned int)rinfo->GetBaseAttribute(PSITEMSTATS_STAT_STRENGTH), false );
    attributes.SetStat(PSITEMSTATS_STAT_AGILITY,(unsigned int)rinfo->GetBaseAttribute(PSITEMSTATS_STAT_AGILITY), false);
    attributes.SetStat(PSITEMSTATS_STAT_ENDURANCE,(unsigned int)rinfo->GetBaseAttribute(PSITEMSTATS_STAT_ENDURANCE), false);
    attributes.SetStat(PSITEMSTATS_STAT_INTELLIGENCE,(unsigned int)rinfo->GetBaseAttribute(PSITEMSTATS_STAT_INTELLIGENCE), false);
    attributes.SetStat(PSITEMSTATS_STAT_WILL,(unsigned int)rinfo->GetBaseAttribute(PSITEMSTATS_STAT_WILL), false);
    attributes.SetStat(PSITEMSTATS_STAT_CHARISMA,(unsigned int)rinfo->GetBaseAttribute(PSITEMSTATS_STAT_CHARISMA), false);
}

void psCharacter::SetFamiliarID( int v )
{
    csString sql;

    familiar_id = v;
    sql.Format( "insert into character_relationships values ( %d, %d, 'familiar', '' )", characterid, familiar_id );
    if( !db->Command( sql ) )
    {
        Error3( "Couldn't execute SQL %s!, Character %u's pet relationship is not saved.", sql.GetData(), characterid );
    }

};

void psCharacter::LoadSavedProgressionEvents()
{
    if (progressionEventsText.IsEmpty())
        return;

    csRef<iDocumentSystem> xml = csPtr<iDocumentSystem>(new csTinyDocumentSystem);
    csRef<iDocument> doc = xml->CreateDocument();
    const char *error = doc->Parse(progressionEventsText);
    if (error)
    {
        Error3("Couldn't restore saved progression events: %s\n%s", error, progressionEventsText.GetData());
        progressionEventsText.Clear();
        return;
    }

    csRef<iDocumentNode> evts = doc->GetRoot()->GetNode("evts");
    if (!evts)
    {
        Error2("Couldn't restore saved progression events: no <evts> tag:\n%s", progressionEventsText.GetData());
        progressionEventsText.Clear();
        return;
    }
    
    csRef<iDocumentNodeIterator> iter = evts->GetNodes();

    while (iter->HasNext())
    {
        csRef<iDocumentNode> evt = iter->Next();

        if (evt->GetType() != CS_NODE_ELEMENT)
            continue;

        csString script = GetNodeXML(evt, false);
        printf("Restoring saved script: %s\n", script.GetData());
        psserver->GetProgressionManager()->ProcessScript(script, actor, actor);
    }
    progressionEventsText.Clear();
}

int psCharacter::RegisterProgressionEvent(const csString & script, csTicks ticksElapsed)
{
    SavedProgressionEvent evt;
    evt.id = nextProgressionEventID++;
    evt.registrationTime = csGetTicks();
    evt.ticksElapsed = ticksElapsed;
    evt.script = script;
    progressionEvents.Push(evt);
    return evt.id;
}

void psCharacter::UnregisterProgressionEvent(int id)
{
    for (size_t i = 0; i < progressionEvents.GetSize(); i++)
    {
        if (progressionEvents[i].id == id)
        {
            progressionEvents.DeleteIndex(i);
            return;
        }
    }
}

void psCharacter::UpdateRespawn(csVector3 pos, float yrot, psSectorInfo* sector)
{
    spawn_loc.loc_sector = sector;
    spawn_loc.loc_x      = pos.x;
    spawn_loc.loc_y      = pos.y;
    spawn_loc.loc_z      = pos.z;
    spawn_loc.loc_yrot   = yrot;
}

void psCharacter::AddAdvantage( PSCHARACTER_ADVANTAGE advantage)
{
    if (advantage<0 || advantage>=PSCHARACTER_ADVANTAGE_COUNT)
        return;
    // Get the index into the advantages array.  32 bits per entry.
    int advantage_index=(int)advantage/32;
    // Get the bit offset in the advantage entry, and use it to generate a single bit bitmask.
    unsigned int advantage_bitmask=1 << ((unsigned int)advantage % 32);

    // Set the bit
    advantage_bitfield[advantage_index]|=advantage_bitmask;
}

void psCharacter::RemoveAdvantage( PSCHARACTER_ADVANTAGE advantage)
{
    if (advantage<0 || advantage>=PSCHARACTER_ADVANTAGE_COUNT)
        return;
    // Get the index into the advantages array.  32 bits per entry.
    int advantage_index=(int)advantage/32;
    // Get the bit offset in the advantage entry, and use it to generate an inverse bitmask.
    unsigned int advantage_bitmask=~( 1 << ((unsigned int)advantage % 32));

    // Clear the bit
    advantage_bitfield[advantage_index]&=advantage_bitmask;
}


bool psCharacter::HasAdvantage ( PSCHARACTER_ADVANTAGE advantage)
{
    if (advantage<0 || advantage>=PSCHARACTER_ADVANTAGE_COUNT)
        return false;
    // Get the index into the advantages array.  32 bits per entry.
    int advantage_index=(int)advantage/32;
    // Get the bit offset in the advantage entry, and use it to generate a single bit bitmask.
    unsigned int advantage_bitmask=1 << ((unsigned int)advantage % 32);

    // This will return a value other than 0 if the bit is set.
    return advantage_bitfield[advantage_index] & advantage_bitmask ? true : false;
}


int psCharacter::GetExperiencePoints() // W
{
    return vitals->GetExp();
}

void psCharacter::SetExperiencePoints(int W)
{
    vitals->SetExp(W);
}

/*
* Will adde W to the experience points. While the number
* of experience points are greater than needed points
* for progression points the experience points are transformed
* into  progression points.
* @return Return the number of progression points gained.
*/
int psCharacter::AddExperiencePoints(int W)
{
    int pp = 0;
    int exp = vitals->GetExp();
    int progP = vitals->GetPP();

    exp += W;
    bool updatedPP = false;

    while (exp >= 200)
    {
        exp -= 200;
        progP++;
        pp++;
        updatedPP = true;
    }

    vitals->SetExp(exp);
    if(updatedPP)
        SetProgressionPoints(progP,true);

    return pp;
}

void psCharacter::SetSpouseName( const char* name )
{
    if ( !name )
        return;

    spouseName = name;

    if ( !strcmp(name,"") )
        isMarried = false;
    else
        isMarried = true;

}

unsigned int psCharacter::GetProgressionPoints() // X
{
    return vitals->GetPP();
}

void psCharacter::SetProgressionPoints(unsigned int X,bool save)
{
    int exp = vitals->GetExp();
    if (save)
    {
        Debug3(LOG_SKILLXP,GetCharacterID(), "Updating PP points and Exp to %u and %d\n", X, exp);
        // Update the DB
        csString sql;
        sql.Format("UPDATE characters SET progression_points = '%u', experience_points = '%d' WHERE id ='%u'",X,exp,GetCharacterID());
        if(!db->CommandPump(sql))
        {
            Error3("Couldn't execute SQL %s!, Character %u's PP points are NOT saved",sql.GetData(),GetCharacterID());
        }
    }

    vitals->SetPP( X );
}

void psCharacter::UseProgressionPoints(unsigned int X)
{

    SetProgressionPoints(vitals->GetPP()-X,true);
}

void psCharacter::InterruptSpellCasting()
{
    if (spellCasting != NULL)
        spellCasting->Interrupt();
    SetSpellCasting(NULL);
}

void psCharacter::SetTradeWork(psWorkGameEvent * event)
{
    workEvent = event;
}

void psCharacter::SetKFactor(float K)
{
    if (K < 0.0)
        K = 0.0;
    else if (K > 100.0)
        K = 100.0;

    KFactor = K;
}

float psCharacter::GetPowerLevel( PSSKILL skill )
{
    float waySkillRank = (float)skills.GetSkillRank(skill);
    MathScriptVar*  waySkillVar = powerScript->GetVar("WaySkill");
    MathScriptVar*  kVar        = powerScript->GetVar("KFactor");
    MathScriptVar*  powerLevel  = powerScript->GetVar("PowerLevel");

    if(!kVar)
    {
        Warning1(LOG_SPELLS,"Couldn't find the KFactor var in powerlevel script!\n");
        return 0.0f;
    }

    if(!waySkillVar)
    {
        Warning1(LOG_SPELLS,"Couldn't find the WaySkill var in powerlevel script!\n");
        return 0.0f;
    }

    if(!powerLevel)
    {
        Warning1(LOG_SPELLS,"Couldn't find the PowerLevel var in powerlevel script!\n");
        return 0.0f;
    }

    kVar->SetValue((double)KFactor);
    waySkillVar->SetValue((double)waySkillRank);

    powerScript->Execute();
    return (float)powerLevel->GetValue();
}

int psCharacter::GetMaxAllowedRealm( PSSKILL skill )
{
    int waySkillRank = skills.GetSkillRank(skill);

    if (waySkillRank == 0)  // zero skill = no casting
        return 0;

    if (!maxRealmScript)
    {
        Error1("No \"MaxRealm\" script!");
        return 0;
    }

    MathScriptVar*  waySkillVar = maxRealmScript->GetVar("WaySkill");
    MathScriptVar*  maxRealmVar = maxRealmScript->GetVar("MaxRealm");

    if(!waySkillVar)
    {
        Warning1(LOG_SPELLS,"Couldn't find the WaySkill var in MaxRealm script!");
        return 0;
    }

    if(!maxRealmVar)
    {
        Warning1(LOG_SPELLS,"Couldn't find the MaxRealm var in MaxRealm script!");
        return 0;
    }

    waySkillVar->SetValue((double)waySkillRank);
    maxRealmScript->Execute();
    return (int)maxRealmVar->GetValue();
}

bool psCharacter::CheckMagicKnowledge( PSSKILL skill, int realm )
{
    Skill * skillRec = skills.GetSkill(skill);
    if (!skillRec)
        return false;

    if (GetMaxAllowedRealm(skill) >= realm)
        return true;

    // Special case for rank 0 people just starting.
    if (skillRec->rank==0 && !skillRec->CanTrain() && realm==1)
        return true;
    else
        return false;
}

float psCharacter::GetPowerLevel()
{
    // When casting a spell the spell casting event is stored in spellCasting.
    // This function is only legal to call when a spell is casted.
    if (spellCasting == NULL)
    {
        Error2("Character %s isn't casting a spell and GetPowerLevel is called",GetCharName());
        return 0.0;
    }

    return GetPowerLevel(spellCasting->spell->GetSkill());
}

const char * psCharacter::GetModeStr()
{
    return player_mode_to_str[player_mode];
}

bool psCharacter::CanSwitchMode(PSCHARACTER_MODE from, PSCHARACTER_MODE to)
{
    switch (to)
    {
        case PSCHARACTER_MODE_DEFEATED:
            return from != PSCHARACTER_MODE_DEAD;
        case PSCHARACTER_MODE_OVERWEIGHT:
            return (from != PSCHARACTER_MODE_DEAD &&
                    from != PSCHARACTER_MODE_DEFEATED);
        case PSCHARACTER_MODE_EXHAUSTED:
        case PSCHARACTER_MODE_COMBAT:
        case PSCHARACTER_MODE_SPELL_CASTING:
        case PSCHARACTER_MODE_WORK:
            return (from != PSCHARACTER_MODE_OVERWEIGHT &&
                    from != PSCHARACTER_MODE_DEAD &&
                    from != PSCHARACTER_MODE_DEFEATED);
        case PSCHARACTER_MODE_SIT:
        case PSCHARACTER_MODE_PEACE:
        case PSCHARACTER_MODE_DEAD:
        default:
            return true;
    }
}

void psCharacter::SetMode(PSCHARACTER_MODE newmode, uint32_t clientnum)
{
    // Assume combat messages need to be sent anyway, because the stance may have changed.
    if (player_mode == newmode && newmode != PSCHARACTER_MODE_COMBAT)
        return;

    if (newmode < PSCHARACTER_MODE_PEACE || newmode >= PSCHARACTER_MODE_COUNT)
    {
        Error3("Unhandled mode: %d switching to %d", player_mode, newmode);
        return;
    }

    if (!CanSwitchMode(player_mode, newmode))
        return;

    // Force mode to be OVERWEIGHT if encumbered and the proposed mode isn't
    // more important.
    if (!inventory.HasEnoughUnusedWeight(0) && CanSwitchMode(newmode, PSCHARACTER_MODE_OVERWEIGHT))
        newmode = PSCHARACTER_MODE_OVERWEIGHT;

    if (newmode != PSCHARACTER_MODE_COMBAT)
        SetCombatStance(getStance("None"));

    psModeMessage msg(clientnum, actor->GetEntity()->GetID(), (uint8_t) newmode, combat_stance.stance_id);
    msg.Multicast(actor->GetMulticastClients(), 0, PROX_LIST_ANY_RANGE);

    actor->SetAllowedToMove(newmode != PSCHARACTER_MODE_DEAD &&
                            newmode != PSCHARACTER_MODE_DEFEATED &&
                            newmode != PSCHARACTER_MODE_SIT &&
                            newmode != PSCHARACTER_MODE_EXHAUSTED &&
                            newmode != PSCHARACTER_MODE_OVERWEIGHT);

    actor->SetAlive(newmode != PSCHARACTER_MODE_DEAD);

    //cancel ongoing work
    if (player_mode == PSCHARACTER_MODE_WORK && workEvent)
    {
        workEvent->Interrupt();
        workEvent = NULL;
    }
    switch( newmode )
    {
        case PSCHARACTER_MODE_PEACE:
        case PSCHARACTER_MODE_EXHAUSTED:
        case PSCHARACTER_MODE_SPELL_CASTING:
        case PSCHARACTER_MODE_COMBAT:
            SetStaminaRegenerationStill();  // start stamina regen
            break;

        case PSCHARACTER_MODE_SIT:
            SetStaminaRegenerationSitting();
            break;
        
        case PSCHARACTER_MODE_DEAD:
        case PSCHARACTER_MODE_OVERWEIGHT:
            SetStaminaRegenerationNone();  // no stamina regen while dead or overweight
            break;
        case PSCHARACTER_MODE_DEFEATED:
            SetHitPointsRate(HP_REGEN_RATE);
            SetStaminaRegenerationNone();
            break;

        case PSCHARACTER_MODE_WORK:
            break;  // work manager sets it's own rates
        default:
            break;
    }

    player_mode = newmode;
}

void psCharacter::SetCombatStance(const Stance& stance)
{
    // NPCs don't have stances yet
    if (actor->GetClientID()==0)
        return;  // Stance never changes from PSCHARACTER_STANCE_NORMAL for NPCs

    CS_ASSERT(stance.stance_id >= 0 && stance.stance_id <= CacheManager::GetSingleton().stances.GetSize());

    if (combat_stance.stance_id == stance.stance_id)
        return;

    combat_stance = stance;
    Debug3(LOG_COMBAT,GetCharacterID(),"Setting stance to %s for %s",stance.stance_name.GetData(),actor->GetName());
}

const Stance& psCharacter::getStance(csString name)
{
    name.Downcase();
    size_t ID = CacheManager::GetSingleton().stanceID.Find(name);
    if(ID == csArrayItemNotFound)
    {
        name = "normal"; // Default to Normal stance.
        ID = CacheManager::GetSingleton().stanceID.Find(name);
    }
    return CacheManager::GetSingleton().stances.Get(ID);
}

void psCharacter::DropItem(psItem *&item, csVector3 suggestedPos, bool transient)
{
    if (!item)
        return;

    if (item->IsInUse())
    {
        psserver->SendSystemError(actor->GetClientID(),"You cannot drop an item while using it.");
        return;
    }

    // Handle position...
    if (suggestedPos != 0)
    {
        // User-specified position...check if it's close enough to the character.

        csVector3 delta;
        delta.x = location.loc_x - suggestedPos.x;
        delta.y = location.loc_y - suggestedPos.y;
        delta.z = location.loc_z - suggestedPos.z;

        float dist = delta.Norm();

        // Future: Could make it drop in the direction specified, if not at the
        //         exact location...
        if (dist > 2) // max drop distance is 15m
            suggestedPos = 0;
    }

    if (suggestedPos == 0)
    {
        // No position specified or it was invalid.
        suggestedPos.x = location.loc_x - (DROP_DISTANCE * sinf(location.loc_yrot));
        suggestedPos.y = location.loc_y;
        suggestedPos.z = location.loc_z - (DROP_DISTANCE * cosf(location.loc_yrot));
    }

    // Play the drop item sound for this item
    psserver->GetCharManager()->SendOutPlaySoundMessage(actor->GetClientID(), item->GetSound(), "drop");

    // Announce drop (in the future, there should be a drop animation)
    psSystemMessage newmsg(actor->GetClientID(), MSG_INFO_BASE, "%s dropped %s.", fullname.GetData(), item->GetQuantityName().GetData());
    newmsg.Multicast(actor->GetMulticastClients(), 0, RANGE_TO_SELECT);

    // If we're dropping from inventory, we should properly remove it.
    // No need to check the return value: we're removing the whole item and
    // already have a pointer.  Plus, well get NULL and crash if it isn't...
    inventory.RemoveItemID(item->GetUID());

    gemObject* obj = EntityManager::GetSingleton().MoveItemToWorld(item,
                             location.worldInstance, location.loc_sector,
                             suggestedPos.x, suggestedPos.y, suggestedPos.z,
                             location.loc_yrot, this, transient);

    if (obj)
    {
        // Assign new object to replace the original object
        item = obj->GetItem();
        item->SetGuardingCharacterID(GetCharacterID());
    }

    // If a container, move its contents as well...
    gemContainer *cont = dynamic_cast<gemContainer*> (obj);
    if (cont)
    {
        for (size_t i=0; i < Inventory().GetInventoryIndexCount(); i++)
        {
            psItem *item = Inventory().GetInventoryIndexItem(i);
            if (item->GetContainerID() == cont->GetItem()->GetUID())
            {
                // This item is in the dropped container
                size_t slot = item->GetLocInParent() - PSCHARACTER_SLOT_BULK1;
                Inventory().RemoveItemIndex(i);
                if (!cont->AddToContainer(item, actor->GetClient(), (int)slot))
                {
                    Error2("Cannot add item into container slot %zu.\n", slot);
                    return;
                }
                item->SetGuardingCharacterID(GetCharacterID());
                i--; // indexes shift when we remove one.
                item->Save(false);
            }
        }
    }
}

void psCharacter::CalculateEquipmentModifiers()
{
    modifiers.Clear();

    // Loop through every holding item
    for(int i = 0; i < PSCHARACTER_SLOT_BULK1; i++)
    {
        psItem *currentitem = NULL;
        currentitem=inventory.GetInventoryItem((INVENTORY_SLOT_NUMBER)i);
        if(!currentitem)
            continue;

        // Check for attr bonuses
        for(int z = 0; z < PSITEMSTATS_STAT_BONUS_INDEX_COUNT; z++)
        {
            PSITEMSTATS_STAT_BONUS_INDEX stat;
            if(z == 0)
                stat = PSITEMSTATS_STAT_BONUS_INDEX_0;
            else if( z == 1)
                stat = PSITEMSTATS_STAT_BONUS_INDEX_1;
            else if( z == 2)
                stat = PSITEMSTATS_STAT_BONUS_INDEX_2;

            float bonus = currentitem->GetWeaponAttributeBonusMax(stat);

            // Add to right var
            modifiers.AddToStat(currentitem->GetWeaponAttributeBonusType(stat), (int)bonus);
        }
    }
}

void psCharacter::AddLootItem(psItemStats *item)
{
    if(!item){
	    Error2("Attempted to add 'null' loot item to character %s, ignored.",fullname.GetDataSafe());
	} else {
        loot_pending.Push(item);
	}
}

size_t psCharacter::GetLootItems(psLootMessage& msg,int entity,int cnum)
{
    // adds inventory to loot. TEMPORARLY REMOVED. see KillNPC()
    //if ( loot_pending.GetSize() == 0 )
    //    AddInventoryToLoot();

    if (loot_pending.GetSize() )
    {
        csString loot;
        loot.Append("<loot>");

        for (size_t i=0; i<loot_pending.GetSize(); i++)
        {
            if (!loot_pending[i]) {
              printf("Potential ERROR: why this happens?");
              continue;
            }
            csString item;
            csString escpxml_imagename = EscpXML(loot_pending[i]->GetImageName());
            csString escpxml_name = EscpXML(loot_pending[i]->GetName());
            item.Format("<li><image icon=\"%s\" count=\"1\" /><desc text=\"%s\" /><id text=\"%u\" /></li>",
                  escpxml_imagename.GetData(),
                  escpxml_name.GetData(),
                  loot_pending[i]->GetUID());
            loot.Append(item);
        }
        loot.Append("</loot>");
        Debug3(LOG_COMBAT, GetCharacterID(), "Loot was %s for %s\n",loot.GetData(), name.GetData());
        msg.Populate(entity,loot,cnum);
    }
    return loot_pending.GetSize();
}

bool psCharacter::RemoveLootItem(int id)
{
    size_t x;
    for (x=0; x<loot_pending.GetSize(); x++)
    {
        if (!loot_pending[x]) {
          printf("Potential ERROR: why this happens?");
          loot_pending.DeleteIndex(x);
          continue;
        }

        if (loot_pending[x]->GetUID() == (uint32) id)
        {
            loot_pending.DeleteIndex(x);
            return true;
        }
    }
    return false;
}

void psCharacter::ClearLoot()
{
    loot_pending.DeleteAll();
    loot_money = 0;
}

void psCharacter::SetMoney(psMoney m)
{
    money          = m;
    SaveMoney(false);
}

void psCharacter::SetMoney( psItem *& itemdata )
{
    /// Check to see if the item is a money item and treat as a special case.
    if ( itemdata->GetBaseStats()->GetFlags() & PSITEMSTATS_FLAG_TRIA )
        money.AdjustTrias( itemdata->GetStackCount() );

    if ( itemdata->GetBaseStats()->GetFlags() & PSITEMSTATS_FLAG_HEXA )
        money.AdjustHexas( itemdata->GetStackCount() );

    if ( itemdata->GetBaseStats()->GetFlags() & PSITEMSTATS_FLAG_OCTA )
        money.AdjustOctas( itemdata->GetStackCount() );

    if ( itemdata->GetBaseStats()->GetFlags() & PSITEMSTATS_FLAG_CIRCLE )
        money.AdjustCircles( itemdata->GetStackCount() );

    CacheManager::GetSingleton().RemoveInstance(itemdata);
    SaveMoney(false);
}



void psCharacter::AdjustMoney(psMoney m, bool bank)
{
    psMoney *mon;
    if(bank)
        mon = &bankMoney;
    else
        mon = &money;
    mon->Adjust( MONEY_TRIAS, m.GetTrias() );
    mon->Adjust( MONEY_HEXAS, m.GetHexas() );
    mon->Adjust( MONEY_OCTAS, m.GetOctas() );
    mon->Adjust( MONEY_CIRCLES, m.GetCircles() );
    SaveMoney(bank);
}

void psCharacter::SaveMoney(bool bank)
{
    if(!loaded)
        return;

    psString sql;

    if(bank)
    {
        sql.AppendFmt("update characters set bank_money_circles=%u, bank_money_trias=%u, bank_money_hexas=%u, bank_money_octas=%u where id=%u",
                      bankMoney.GetCircles(), bankMoney.GetTrias(), bankMoney.GetHexas(), bankMoney.GetOctas(), GetCharacterID());
    }
    else
    {
        sql.AppendFmt("update characters set money_circles=%u, money_trias=%u, money_hexas=%u, money_octas=%u where id=%u",
                      money.GetCircles(), money.GetTrias(), money.GetHexas(), money.GetOctas(), GetCharacterID());
    }

    if (db->CommandPump(sql) != 1)
    {
        Error3 ("Couldn't save character's money to database.\nCommand was "
            "<%s>.\nError returned was <%s>\n",db->GetLastQuery(),db->GetLastError());
    }
}

void psCharacter::ResetStats()
{
    vitals->ResetVitals();
    inventory.CalculateLimits();
}

void psCharacter::CombatDrain(int slot)
{
    float mntdrain = 1.0f;
    float phydrain = 1.0f;

    static MathScript *costScript = NULL;

    if (!costScript)
        costScript = psserver->GetMathScriptEngine()->FindScript("StaminaCombat");

    if ( costScript )
    {
        // Output
        MathScriptVar* PhyDrain   = costScript->GetVar("PhyDrain");
        MathScriptVar* MntDrain   = costScript->GetVar("MntDrain");

        // Input
        MathScriptVar* actor    = costScript->GetOrCreateVar("Actor");
        MathScriptVar* weapon   = costScript->GetOrCreateVar("Weapon");

        if(!PhyDrain || !MntDrain)
        {
            Error1("Couldn't find the PhyDrain output var in StaminaCombat script!");
            return;
        }

        // Input the data
        actor->SetObject(this);
        weapon->SetObject(inventory.GetItem(NULL,(INVENTORY_SLOT_NUMBER) slot));

        costScript->Execute();

        // Get the output
        phydrain = PhyDrain->GetValue();
        mntdrain = MntDrain->GetValue();
    }

    AdjustStamina(-phydrain, true);
    AdjustStamina(-mntdrain, false);
}

float psCharacter::GetHitPointsMax()
{
    return vitals->GetVital(VITAL_HITPOINTS).max;
}

float psCharacter::GetHitPointsMaxModifier()
{
    return vitals->GetVital(VITAL_HITPOINTS).maxModifier;
}


float psCharacter::GetManaMax()
{
    return vitals->GetVital(VITAL_MANA).max;
}

float psCharacter::GetManaMaxModifier()
{
    return vitals->GetVital(VITAL_MANA).maxModifier;
}


float psCharacter::GetStaminaMax(bool pys)
{
    if(pys)
	{
        return vitals->GetVital(VITAL_PYSSTAMINA).max;
	}
    else
	{
        return vitals->GetVital(VITAL_MENSTAMINA).max;
	}
}

float psCharacter::GetStaminaMaxModifier(bool pys)
{
	if(pys)
	{
		return vitals->GetVital(VITAL_PYSSTAMINA).maxModifier;
	}
	else
	{
		return vitals->GetVital(VITAL_MENSTAMINA).maxModifier;
	}
}


float psCharacter::AdjustHitPoints(float adjust)
{
    return AdjustVital(VITAL_HITPOINTS, DIRTY_VITAL_HP,adjust);
}


void psCharacter::SetHitPoints(float v)
{
    SetVital(VITAL_HITPOINTS, DIRTY_VITAL_HP,v);
}

float psCharacter::AdjustHitPointsMaxModifier(float adjust)
{
    vitals->DirtyVital(VITAL_HITPOINTS, DIRTY_VITAL_HP_MAX).maxModifier += adjust;
    return vitals->GetVital(VITAL_HITPOINTS).maxModifier;
}

void psCharacter::SetHitPointsMaxModifier (float v)
{
	vitals->DirtyVital(VITAL_HITPOINTS, DIRTY_VITAL_HP_MAX).maxModifier = v;
}


float psCharacter::AdjustHitPointsMax(float adjust)
{
    vitals->DirtyVital(VITAL_HITPOINTS, DIRTY_VITAL_HP_MAX).max += adjust;
    return vitals->GetVital(VITAL_HITPOINTS).max;
}
void psCharacter::SetHitPointsMax(float v)
{
    CS_ASSERT_MSG("Negative Max HP!", v > -0.01f);
    vitals->DirtyVital(VITAL_HITPOINTS, DIRTY_VITAL_HP_MAX).max = v;
}
float psCharacter::GetHP()
{
    return vitals->GetHP();
}

float psCharacter::GetMana()
{
    return vitals->GetMana();
}
float psCharacter::GetStamina(bool pys)
{
    return vitals->GetStamina(pys);
}

float psCharacter::AdjustVital( int vitalName, int dirtyFlag, float adjust )
{
    vitals->DirtyVital(vitalName, dirtyFlag).value += adjust;

    if (vitals->GetVital(vitalName).value < 0)
        vitals->GetVital(vitalName).value = 0;

    if (vitals->GetVital(vitalName).value > vitals->GetVital(vitalName).max)
        vitals->GetVital(vitalName).value = vitals->GetVital(vitalName).max;

    return vitals->GetVital(vitalName).value;
}

float psCharacter::SetVital( int vitalName, int dirtyFlag, float value )
{
    vitals->DirtyVital(vitalName, dirtyFlag).value = value;

    if (vitals->GetVital(vitalName).value < 0)
        vitals->GetVital(vitalName).value = 0;

    if (vitals->GetVital(vitalName).value > vitals->GetVital(vitalName).max)
        vitals->GetVital(vitalName).value = vitals->GetVital(vitalName).max;

    return vitals->GetVital(vitalName).value;
}

bool psCharacter::UpdateStatDRData(csTicks now)
{
    bool res = vitals->Update(now);

    // if HP dropped to zero, provoke the killing process
    if (GetHP() == 0   &&   actor != NULL   &&   actor->IsAlive())
        actor->Kill(NULL);
    return res;
}

bool psCharacter::SendStatDRMessage(uint32_t clientnum, PS_ID eid, int flags, csRef<PlayerGroup> group)
{
    return vitals->SendStatDRMessage(clientnum, eid, flags, group);
}

float psCharacter::AdjustHitPointsRate(float adjust)
{
    vitals->DirtyVital(VITAL_HITPOINTS, DIRTY_VITAL_HP_RATE).drRate += adjust;
    return vitals->GetVital(VITAL_HITPOINTS).drRate;
}
void psCharacter::SetHitPointsRate(float v)
{
    vitals->DirtyVital(VITAL_HITPOINTS, DIRTY_VITAL_HP).drRate = v;
}


float psCharacter::AdjustMana(float adjust)
{
    return AdjustVital(VITAL_MANA, DIRTY_VITAL_MANA,adjust);
}
void psCharacter::SetMana(float v)
{
    SetVital(VITAL_MANA, DIRTY_VITAL_MANA,v);
}

float psCharacter::AdjustManaMaxModifier(float adjust)
{
    vitals->DirtyVital(VITAL_MANA, DIRTY_VITAL_MANA_MAX).maxModifier += adjust;
    return vitals->GetVital(VITAL_MANA).maxModifier;
}

void psCharacter::SetManaMaxModifier(float v)
{
    vitals->DirtyVital(VITAL_MANA, DIRTY_VITAL_MANA_MAX).maxModifier = v;
}

float psCharacter::AdjustManaMax(float adjust)
{
    vitals->DirtyVital(VITAL_MANA, DIRTY_VITAL_MANA_MAX).max += adjust;
    return vitals->GetVital(VITAL_MANA).max;
}
void psCharacter::SetManaMax(float v)
{
    CS_ASSERT_MSG("Negative Max MP!", v > -0.01f);
    vitals->DirtyVital(VITAL_MANA, DIRTY_VITAL_MANA_MAX).max = v;
}

float psCharacter::AdjustManaRate(float adjust)
{
    vitals->DirtyVital(VITAL_MANA, DIRTY_VITAL_MANA_RATE).drRate += adjust;
    return vitals->GetVital(VITAL_MANA).drRate;
}

void psCharacter::SetManaRate(float v)
{
    vitals->DirtyVital(VITAL_MANA, DIRTY_VITAL_MANA_RATE).drRate = v;
}


float psCharacter::AdjustStamina(float adjust, bool pys)
{
    if(pys)
        return AdjustVital(VITAL_PYSSTAMINA, DIRTY_VITAL_PYSSTAMINA,adjust);
    else
        return AdjustVital(VITAL_MENSTAMINA, DIRTY_VITAL_MENSTAMINA,adjust);
}
float psCharacter::AdjustStaminaMaxModifier (float adjust, bool pys)
{
	if(pys)
	{
		vitals->DirtyVital(VITAL_PYSSTAMINA, DIRTY_VITAL_PYSSTAMINA_MAX).maxModifier += adjust;
		return vitals->GetVital(VITAL_PYSSTAMINA).maxModifier;
	}
	else
	{
		vitals->DirtyVital(VITAL_MENSTAMINA, DIRTY_VITAL_MENSTAMINA_MAX).maxModifier += adjust;
		return vitals->GetVital(VITAL_MENSTAMINA).maxModifier;
	}
}

void psCharacter::SetStaminaMaxModifier(float v, bool pys)
{
	if(pys)
	{
		vitals->DirtyVital(VITAL_PYSSTAMINA, DIRTY_VITAL_PYSSTAMINA_MAX).maxModifier = v;
	}
	else
	{
		vitals->DirtyVital(VITAL_MENSTAMINA, DIRTY_VITAL_MENSTAMINA_MAX).maxModifier = v;
	}
}


void psCharacter::SetStamina(float v,bool pys)
{
    if(pys)
        SetVital(VITAL_PYSSTAMINA, DIRTY_VITAL_PYSSTAMINA,v);
    else
        SetVital(VITAL_MENSTAMINA, DIRTY_VITAL_MENSTAMINA,v);
}

float psCharacter::AdjustStaminaMax(float adjust,bool pys)
{
    if(pys)
    {
        vitals->DirtyVital(VITAL_PYSSTAMINA, DIRTY_VITAL_PYSSTAMINA_MAX).max += adjust;
        return vitals->GetVital(VITAL_PYSSTAMINA).max;
    }
    else
    {
        vitals->DirtyVital(VITAL_MENSTAMINA, DIRTY_VITAL_MENSTAMINA_MAX).max += adjust;
        return vitals->GetVital(VITAL_MENSTAMINA).max;
    }
}

void psCharacter::SetStaminaRegenerationNone(bool physical,bool mental)
{
    if(physical) SetStaminaRate(0.0f,true);
    if(mental)   SetStaminaRate(0.0f,false);
}

void psCharacter::SetStaminaRegenerationWalk(bool physical,bool mental)
{
    if(physical) SetStaminaRate(GetStaminaMax(true)/100 * GetRaceInfo()->baseRegen[PSRACEINFO_STAMINA_PHYSICAL_WALK],true);
    if(mental)   SetStaminaRate(GetStaminaMax(false)/100 * GetRaceInfo()->baseRegen[PSRACEINFO_STAMINA_MENTAL_WALK],false);
}

void psCharacter::SetStaminaRegenerationSitting()
{
    SetStaminaRate(GetStaminaMax(true) * 0.015 * GetRaceInfo()->baseRegen[PSRACEINFO_STAMINA_PHYSICAL_STILL],true);
    SetStaminaRate(GetStaminaMax(false) * 0.015 * GetRaceInfo()->baseRegen[PSRACEINFO_STAMINA_MENTAL_STILL],false);
}

void psCharacter::SetStaminaRegenerationStill(bool physical,bool mental)
{
    if(physical) SetStaminaRate(GetStaminaMax(true)/100 * GetRaceInfo()->baseRegen[PSRACEINFO_STAMINA_PHYSICAL_STILL],true);
    if(mental)   SetStaminaRate(GetStaminaMax(false)/100 * GetRaceInfo()->baseRegen[PSRACEINFO_STAMINA_MENTAL_STILL],false);
}

void psCharacter::SetStaminaRegenerationWork(int skill)
{
    //Gms don't want to lose stamina when testing
    if (actor->nevertired)
        return;

    // Need real formula for this. Shouldn't be hard coded anyway.
    // Stamina drain needs to be set depending on the complexity of the task.
    int factor = CacheManager::GetSingleton().GetSkillByID(skill)->mental_factor;
    AdjustStaminaRate(-6.0*(100-factor)/100, true);
    AdjustStaminaRate(-6.0*(100-factor)/100, false);
}
void psCharacter::CalculateMaxStamina()
{
    if(!staminaCalc)
    {
        CPrintf(CON_ERROR,"Called CalculateMaxStamina without mathscript!!!!");
        return;
    }
    // Set all the skills vars
    staminaCalc->GetOrCreateVar("STR") ->SetValue(attributes.GetStat(PSITEMSTATS_STAT_STRENGTH));
    staminaCalc->GetOrCreateVar("END") ->SetValue(attributes.GetStat(PSITEMSTATS_STAT_ENDURANCE));
    staminaCalc->GetOrCreateVar("AGI") ->SetValue(attributes.GetStat(PSITEMSTATS_STAT_AGILITY));
    staminaCalc->GetOrCreateVar("INT") ->SetValue(attributes.GetStat(PSITEMSTATS_STAT_INTELLIGENCE));
    staminaCalc->GetOrCreateVar("WILL")->SetValue(attributes.GetStat(PSITEMSTATS_STAT_WILL));
    staminaCalc->GetOrCreateVar("CHA") ->SetValue(attributes.GetStat(PSITEMSTATS_STAT_CHARISMA));

    // Calculate
    staminaCalc->Execute();

    // Set the max values
    SetStaminaMax((float)staminaCalc->GetOrCreateVar("BasePhy")->GetValue(),true);
    SetStaminaMax((float)staminaCalc->GetOrCreateVar("BaseMen")->GetValue(),false);
}

void psCharacter::SetStaminaMax(float v,bool pys)
{
    if (v < 0.0)
        v =0.1f;
    if(pys)
        vitals->DirtyVital(VITAL_PYSSTAMINA, DIRTY_VITAL_PYSSTAMINA_MAX).max = v;
    else
        vitals->DirtyVital(VITAL_MENSTAMINA, DIRTY_VITAL_MENSTAMINA_MAX).max = v;
}

float psCharacter::AdjustStaminaRate(float adjust,bool pys)
{
    if(pys)
    {
        vitals->DirtyVital(VITAL_PYSSTAMINA, DIRTY_VITAL_PYSSTAMINA_RATE).drRate += adjust;
        return vitals->GetVital(VITAL_PYSSTAMINA).drRate;
    }
    else
    {
        vitals->DirtyVital(VITAL_MENSTAMINA, DIRTY_VITAL_MENSTAMINA_RATE).drRate += adjust;
        return vitals->GetVital(VITAL_MENSTAMINA).drRate;
    }

}

void psCharacter::SetStaminaRate(float v,bool pys)
{
    if(pys)
        vitals->DirtyVital(VITAL_PYSSTAMINA, DIRTY_VITAL_PYSSTAMINA_RATE).drRate = v;
    else
        vitals->DirtyVital(VITAL_MENSTAMINA, DIRTY_VITAL_MENSTAMINA_RATE).drRate = v;
}






void psCharacter::NotifyAttackPerformed(INVENTORY_SLOT_NUMBER slot,csTicks timeofattack)
{
    psItem *Weapon;

    // Slot out of range
    if (slot<0 || slot>=PSCHARACTER_SLOT_BULK1)
        return;

    // TODO: Reduce ammo if this is an ammunition using weapon

    // Reset next attack time
    Weapon=Inventory().GetEffectiveWeaponInSlot(slot);

    if (Weapon!=NULL)
        inventory.GetEquipmentObject(slot).NextSwingTime=csTicks(timeofattack+ (Weapon->GetLatency() * 1000.0f));

    //drain stamina on player attacks
    if(actor->GetClientID() && !actor->nevertired)
        CombatDrain(slot);
}

csTicks psCharacter::GetSlotNextAttackTime(INVENTORY_SLOT_NUMBER slot)
{
    // Slot out of range
    if (slot<0 || slot>=PSCHARACTER_SLOT_BULK1)
        return (csTicks)0;

    return inventory.GetEquipmentObject(slot).NextSwingTime;
}


// AVPRO= attack Value progression (this is to change the progression of all the calculation in AV for now it is equal to 1)
#define AVPRO 1

float psCharacter::GetAttackValueModifier()
{
    return attackValueModifier;
}

void  psCharacter::AdjustAttackValueModifier(float mul)
{
    attackValueModifier *= mul;
}

float psCharacter::GetDefenseValueModifier()
{
    return defenseValueModifier;
}

void  psCharacter::AdjustDefenseValueModifier(float mul)
{
    defenseValueModifier *= mul;
}

float psCharacter::GetMeleeDefensiveDamageModifier()
{
    return meleeDefensiveDamageModifier;
}

void  psCharacter::AdjustMeleeDefensiveDamageModifier(float mul)
{
    meleeDefensiveDamageModifier *= mul;
}

float psCharacter::GetTargetedBlockValueForWeaponInSlot(INVENTORY_SLOT_NUMBER slot)
{
    psItem *weapon=Inventory().GetEffectiveWeaponInSlot(slot);
    if (weapon==NULL)
        return 0.0f;

    return weapon->GetTargetedBlockValue();
}

float psCharacter::GetUntargetedBlockValueForWeaponInSlot(INVENTORY_SLOT_NUMBER slot)
{
    psItem *weapon=Inventory().GetEffectiveWeaponInSlot(slot);
    if (weapon==NULL)
        return 0.0f;

    return weapon->GetUntargetedBlockValue();
}


float psCharacter::GetTotalTargetedBlockValue()
{
    float blockval=0.0f;
    int slot;

    for (slot=0;slot<PSCHARACTER_SLOT_BULK1;slot++)
        blockval+=GetTargetedBlockValueForWeaponInSlot((INVENTORY_SLOT_NUMBER)slot);

    return blockval;
}

float psCharacter::GetTotalUntargetedBlockValue()
{
    float blockval=0.0f;
    int slot;

    for (slot=0;slot<PSCHARACTER_SLOT_BULK1;slot++)
        blockval+=GetUntargetedBlockValueForWeaponInSlot((INVENTORY_SLOT_NUMBER)slot);

    return blockval;
}


float psCharacter::GetCounterBlockValueForWeaponInSlot(INVENTORY_SLOT_NUMBER slot)
{
    psItem *weapon=inventory.GetEffectiveWeaponInSlot(slot);
    if (weapon==NULL)
        return 0.0f;

    return weapon->GetCounterBlockValue();
}

#define ARMOR_USES_SKILL(slot,skill) inventory.GetInventoryItem(slot)==NULL?inventory.GetEquipmentObject(slot).default_if_empty->GetArmorType()==skill:inventory.GetInventoryItem(slot)->GetArmorType()==skill

#define CALCULATE_ARMOR_FOR_SLOT(slot) \
    if (ARMOR_USES_SKILL(slot,PSITEMSTATS_ARMORTYPE_LIGHT)) light_p+=1.0f/6.0f; \
    if (ARMOR_USES_SKILL(slot,PSITEMSTATS_ARMORTYPE_MEDIUM)) med_p+=1.0f/6.0f; \
    if (ARMOR_USES_SKILL(slot,PSITEMSTATS_ARMORTYPE_HEAVY)) heavy_p+=1.0f/6.0f;


float psCharacter::GetDodgeValue()
{
    float heavy_p,med_p,light_p;
    float asdm;

    // hold the % of each type of armor worn
    heavy_p=med_p=light_p=0.0f;

    CALCULATE_ARMOR_FOR_SLOT(PSCHARACTER_SLOT_HEAD);
    CALCULATE_ARMOR_FOR_SLOT(PSCHARACTER_SLOT_TORSO);
    CALCULATE_ARMOR_FOR_SLOT(PSCHARACTER_SLOT_ARMS);
    CALCULATE_ARMOR_FOR_SLOT(PSCHARACTER_SLOT_GLOVES);
    CALCULATE_ARMOR_FOR_SLOT(PSCHARACTER_SLOT_LEGS);
    CALCULATE_ARMOR_FOR_SLOT(PSCHARACTER_SLOT_BOOTS);

    // multiplies for skill
    heavy_p *= skills.GetSkillRank(PSSKILL_HEAVYARMOR);
    med_p   *= skills.GetSkillRank(PSSKILL_MEDIUMARMOR);
    light_p *= skills.GetSkillRank(PSSKILL_LIGHTARMOR);

    // armor skill defense mod
    asdm=heavy_p+med_p+light_p;

    // if total skill is 0, give a little chance anyway to defend himself
    if (asdm==0)
        asdm=0.2F;

    // for now return just asdm
    return asdm;

    /*
    // MADM= Martial Arts Defense Mod=martial arts skill-(weight carried+ AGI malus of the armor +DEX malus of the armor) min 0
    // TODO: fix this to use armor agi malus
    madm=GetSkillRank(PSSKILL_MARTIALARTS)-inventory.weight;
    if (madm<0.0f)
        madm=0.0f;

    // Active Dodge Value =ADV = (ASDM + agiDmod + MADM)*ADVP)^0.4
    // Out of Melee Defense Value=OMDV = (agiDmod+(ASDM\10)*PDVP)^0.4

    // TODO:  Use passive dv here when the conditions are finalized that it should be used (archery, casting, more?)

    dv=asdm + AgiMod  + madm;
    dv=pow(dv,(float)0.6);

    return dv;
    */
}


void psCharacter::PracticeArmorSkills(unsigned int practice, INVENTORY_SLOT_NUMBER attackLocation)
{

    psItem *armor = inventory.GetEffectiveArmorInSlot(attackLocation);

    switch (armor->GetArmorType())
    {
        case PSITEMSTATS_ARMORTYPE_LIGHT:
            skills.AddSkillPractice(PSSKILL_LIGHTARMOR,practice);
            break;
        case PSITEMSTATS_ARMORTYPE_MEDIUM:
            skills.AddSkillPractice(PSSKILL_MEDIUMARMOR,practice);
            break;
        case PSITEMSTATS_ARMORTYPE_HEAVY:
            skills.AddSkillPractice(PSSKILL_HEAVYARMOR,practice);
            break;
        default:
            break;
    }

}

void psCharacter::PracticeWeaponSkills(unsigned int practice)
{
    int slot;

    for (slot=0;slot<PSCHARACTER_SLOT_BULK1;slot++)
    {
        psItem *weapon=inventory.GetEffectiveWeaponInSlot((INVENTORY_SLOT_NUMBER)slot);
        if (weapon!=NULL)
            PracticeWeaponSkills(weapon,practice);
    }

}

void psCharacter::PracticeWeaponSkills(psItem * weapon, unsigned int practice)
{
    for (int index = 0; index < PSITEMSTATS_WEAPONSKILL_INDEX_COUNT; index++)
    {
        PSSKILL skill = weapon->GetWeaponSkill((PSITEMSTATS_WEAPONSKILL_INDEX)index);
        if (skill != PSSKILL_NONE)
            skills.AddSkillPractice(skill,practice);
    }
}

void psCharacter::SetTraitForLocation(PSTRAIT_LOCATION location,psTrait *trait)
{
    if (location<0 || location>=PSTRAIT_LOCATION_COUNT)
        return;

    traits[location]=trait;
}

psTrait *psCharacter::GetTraitForLocation(PSTRAIT_LOCATION location)
{
    if (location<0 || location>=PSTRAIT_LOCATION_COUNT)
        return NULL;

    return traits[location];
}


void psCharacter::GetLocationInWorld(int &instance,psSectorInfo *&sectorinfo,float &loc_x,float &loc_y,float &loc_z,float &loc_yrot)
{
    sectorinfo=location.loc_sector;
    loc_x=location.loc_x;
    loc_y=location.loc_y;
    loc_z=location.loc_z;
    loc_yrot=location.loc_yrot;
    instance = location.worldInstance;
}

void psCharacter::SetLocationInWorld(int instance, psSectorInfo *sectorinfo,float loc_x,float loc_y,float loc_z,float loc_yrot)
{
    psSectorInfo *oldsector = location.loc_sector;

    location.loc_sector=sectorinfo;
    location.loc_x=loc_x;
    location.loc_y=loc_y;
    location.loc_z=loc_z;
    location.loc_yrot=loc_yrot;
    location.worldInstance = instance;

    if (oldsector!=NULL  &&  oldsector!=sectorinfo)
    {
        if ( dynamic_cast<gemNPC*>(actor) == NULL ) // NOT an NPC so it's ok to save location info
            SaveLocationInWorld();
    }
}

void psCharacter::SaveLocationInWorld()
{
    if(!loaded)
        return;

    st_location & l = location;
    psString sql;

    sql.AppendFmt("update characters set loc_x=%10.2f, loc_y=%10.2f, loc_z=%10.2f, loc_yrot=%10.2f, loc_sector_id=%u, loc_instance=%u where id=%u",
                     l.loc_x, l.loc_y, l.loc_z, l.loc_yrot, l.loc_sector->uid, l.worldInstance, characterid);
    if (db->CommandPump(sql) != 1)
    {
        Error3 ("Couldn't save character's position to database.\nCommand was "
            "<%s>.\nError returned was <%s>\n",db->GetLastQuery(),db->GetLastError());
    }
}




psSpell * psCharacter::GetSpellByName(const csString& spellName)
{
    for (size_t i=0; i < spellList.GetSize(); i++)
    {
        if (spellList[i]->GetName().CompareNoCase(spellName)) return spellList[i];
    }
    return NULL;
}

psSpell * psCharacter::GetSpellByIdx(int index)
{
    if (index < 0 || (size_t)index >= spellList.GetSize())
        return NULL;
    return spellList[index];
}

csString psCharacter::GetXMLSpellList()
{
    csString buff = "<L>";
    for (size_t i=0; i < spellList.GetSize(); i++)
    {
        buff.Append(spellList[i]->SpellToXML());
    }
    buff.Append("</L>");
    return buff;
}

bool psCharacter::SetTradingStopped(bool stopped)
{
    bool old = tradingStopped;
    tradingStopped=stopped;
    return old;
}

// Check if player and target is ready to do a exchange
//       - Not fighting
//       - Not casting spell
//       - Not exchanging with a third player
//       - Not player stopped trading
//       - Not trading with a merchant
bool psCharacter::ReadyToExchange()
{
    return (//TODO: Test for fighting &&
        //TODO: Test for casting spell
       //  !exchangeMgr.IsValid() &&
        !tradingStopped &&
        tradingStatus == NOT_TRADING);

}

void psCharacter::MakeTextureString( csString& traits)
{
    // initialize string
    traits = "<traits>";

    // cycle through and add entries for each part
    for (unsigned int i=0;i<PSTRAIT_LOCATION_COUNT;i++)
    {
        psTrait *trait;
        trait = GetTraitForLocation((PSTRAIT_LOCATION)i);
        while (trait != NULL)
        {
            csString buff = trait->ToXML(true);
            traits.Append(buff);
            trait = trait->next_trait;
        }
    }

    // terminate string
    traits.Append("</traits>");

    Notify2( LOG_CHARACTER, "Traits string: %s", (const char*)traits );
}

void psCharacter::MakeEquipmentString( csString& equipment )
{
    equipment = "<equiplist>";
    equipment.AppendFmt("<helm>%s</helm>", EscpXML(helmGroup).GetData());

    for (int i=0; i<PSCHARACTER_SLOT_BULK1; i++)
    {        
        psItem* item = inventory.GetInventoryItem((INVENTORY_SLOT_NUMBER)i);
        if (item == NULL)
            continue;
    
        csString slot = EscpXML( CacheManager::GetSingleton().slotNameHash.GetName(i) );
        csString mesh = EscpXML( item->GetMeshName() );
        csString part = EscpXML( item->GetPartName() );
        csString texture = EscpXML( item->GetTextureName() );
        csString partMesh = EscpXML( item->GetPartMeshName() );
        
        equipment.AppendFmt("<equip slot=\"%s\" mesh=\"%s\" part=\"%s\" texture=\"%s\" partMesh=\"%s\"  />",
                              slot.GetData(), mesh.GetData(), part.GetData(), texture.GetData(), partMesh.GetData() );
    }

    equipment.Append("</equiplist>");

    Notify2( LOG_CHARACTER, "Equipment string: %s", equipment.GetData() );
}


bool psCharacter::AppendCharacterSelectData(psAuthApprovedMessage& auth)
{
    csString traits;
    csString equipment;

    MakeTextureString( traits );
    MakeEquipmentString( equipment );

    auth.AddCharacter(fullname, raceinfo->name, raceinfo->mesh_name, traits, equipment);
    return true;
}

QuestAssignment *psCharacter::IsQuestAssigned(int id)
{
    for (size_t i=0; i<assigned_quests.GetSize(); i++)
    {
        if (assigned_quests[i]->GetQuest().IsValid() && assigned_quests[i]->GetQuest()->GetID() == id &&
            assigned_quests[i]->status != PSQUEST_DELETE)
            return assigned_quests[i];
    }

    return NULL;
}

int psCharacter::GetAssignedQuestLastResponse(int i)
{
    if ((size_t) i<assigned_quests.GetSize())
    {
        return assigned_quests[i]->last_response;
    }
    else
    {
        //could use error message
        return -1; //return no response
    }
}


//if (parent of) quest id is an assigned quest, set its last response
bool psCharacter::SetAssignedQuestLastResponse(psQuest *quest, int response)
{   
    int id = 0;

    if (quest)
    {
        while (quest->GetParentQuest()) //get highest parent
            quest= quest->GetParentQuest();
        id = quest->GetID();
    }
    else
        return false;


    for (size_t i=0; i<assigned_quests.GetSize(); i++)
    {
        if (assigned_quests[i]->GetQuest().IsValid() && assigned_quests[i]->GetQuest()->GetID() == id &&
            assigned_quests[i]->status == PSQUEST_ASSIGNED)
        {
            assigned_quests[i]->last_response = response;
            assigned_quests[i]->dirty = true;
            UpdateQuestAssignments();
            return true;
        }
    }
    return false;
}

size_t  psCharacter::GetAssignedQuests(psQuestListMessage& questmsg,int cnum)
{
    if (assigned_quests.GetSize() )
    {
        csString quests;
        quests.Append("<quests>");
        csArray<QuestAssignment*>::Iterator iter = assigned_quests.GetIterator();

        while(iter.HasNext())
        {
            QuestAssignment* assigned_quest = iter.Next();
            // exclude deleted
            if (assigned_quest->status == PSQUEST_DELETE || !assigned_quest->GetQuest().IsValid())
                continue;
            // exclude substeps
            if (assigned_quest->GetQuest()->GetParentQuest())
                continue;

            csString item;
            csString escpxml_image = EscpXML(assigned_quest->GetQuest()->GetImage());
            csString escpxml_name = EscpXML(assigned_quest->GetQuest()->GetName());
            item.Format("<q><image icon=\"%s\" /><desc text=\"%s\" /><id text=\"%d\" /><status text=\"%c\" /></q>",
                        escpxml_image.GetData(),
                        escpxml_name.GetData(),
                        assigned_quest->GetQuest()->GetID(),
                        assigned_quest->status );
            quests.Append(item);
        }
        quests.Append("</quests>");
        Debug2(LOG_QUESTS, GetCharacterID(), "QuestMsg was %s\n",quests.GetData() );
        questmsg.Populate(quests,cnum);
    }
    return assigned_quests.GetSize();
}

QuestAssignment *psCharacter::AssignQuest(psQuest *quest, int assigner_id)
{
    CS_ASSERT( quest );  // Must not be NULL

    //first check if there is not another assigned quest with the same NPC
    for (size_t i=0; i<assigned_quests.GetSize(); i++)
    {
        if (assigned_quests[i]->GetQuest().IsValid() && assigned_quests[i]->assigner_id == assigner_id &&
            assigned_quests[i]->GetQuest()->GetID() != quest->GetID() &&
            assigned_quests[i]->GetQuest()->GetParentQuest() == NULL &&
            assigned_quests[i]->status == PSQUEST_ASSIGNED)
        {
            Debug3(LOG_QUESTS, GetCharacterID(), "Did not assign %s quest to %s because (s)he already has a quest assigned with this npc.\n",quest->GetName(),GetCharName() );
            return false; // Cannot have multiple quests from the same guy
        }
    }

    QuestAssignment *q = IsQuestAssigned(quest->GetID() );
    if (!q)  // make new entry if needed, reuse if old
    {
        q = new QuestAssignment;
        q->SetQuest(quest);
        q->status = PSQUEST_DELETE;
        
        assigned_quests.Push(q);
    }

    if (q->status != PSQUEST_ASSIGNED)
    {
        q->dirty  = true;
        q->status = PSQUEST_ASSIGNED;
        q->lockout_end = 0;
        q->assigner_id = assigner_id;
        q->last_response = this->lastResponse; //This should be the response given when starting this quest
        
        // assign any skipped parent quests
        if (quest->GetParentQuest() && !IsQuestAssigned(quest->GetParentQuest()->GetID()))
                AssignQuest(quest->GetParentQuest(),assigner_id );

        // assign any skipped sub quests
        csHash<psQuest*>::GlobalIterator it = CacheManager::GetSingleton().GetQuestIterator();
        while (it.HasNext())
        {
            psQuest * q = it.Next();
            if (q->GetParentQuest())
            {
                if (q->GetParentQuest()->GetID() == quest->GetID())
                    AssignQuest(q,assigner_id);
            }
        }

        q->GetQuest()->SetQuestLastActivatedTime( csGetTicks() );

        Debug3(LOG_QUESTS, GetCharacterID(), "Assigned quest '%s' to player '%s'\n",quest->GetName(),GetCharName() );
        UpdateQuestAssignments();
    }
    else
    {
        Debug3(LOG_QUESTS, GetCharacterID(), "Did not assign %s quest to %s because it was already assigned.\n",quest->GetName(),GetCharName() );
    }

    return q;
}

bool psCharacter::CompleteQuest(psQuest *quest)
{
    CS_ASSERT( quest );  // Must not be NULL

    QuestAssignment *q = IsQuestAssigned( quest->GetID() );
    QuestAssignment *parent = NULL;

    // substeps are not assigned, so the above check fails for substeps.
    // in this case we check if the parent quest is assigned
    if (!q && quest->GetParentQuest()) {
      parent = IsQuestAssigned(quest->GetParentQuest()->GetID() );
    }

    // create an assignment for the substep if parent is valid
    if (parent) {
      q = AssignQuest(quest,parent->assigner_id);
    }

    if (q)
    {
        if (q->status == PSQUEST_DELETE || q->status == PSQUEST_COMPLETE)
        {
            Debug3(LOG_QUESTS, GetCharacterID(), "Player '%s' has already completed quest '%s'.  No credit.\n",GetCharName(),quest->GetName() );
            return false;  // already completed, so no credit here
        }

        q->dirty  = true;
        q->status = PSQUEST_COMPLETE; // completed
        q->lockout_end = csGetTicks() + q->GetQuest()->GetPlayerLockoutTime();
        q->last_response = -1; //reset last response for this quest in case it is restarted

        // Complete all substeps if this is the parent quest
        if (!q->GetQuest()->GetParentQuest())
        {
            // assign any skipped sub quests
            csHash<psQuest*>::GlobalIterator it = CacheManager::GetSingleton().GetQuestIterator();
            while (it.HasNext())
            {
                psQuest * q = it.Next();
                if (q->GetParentQuest())
                {
                    if (q->GetParentQuest()->GetID() == quest->GetID())
                        CompleteQuest(q);
                }
            }
        }

        Debug3(LOG_QUESTS, GetCharacterID(), "Player '%s' just completed quest '%s'.\n",GetCharName(),quest->GetName() );
        UpdateQuestAssignments();
        return true;
    }

    return false;
}

void psCharacter::DiscardQuest(QuestAssignment *q, bool force)
{
    CS_ASSERT( q );  // Must not be NULL

    if (force || (q->status != PSQUEST_DELETE && !q->GetQuest()->HasInfinitePlayerLockout()) )
    {
        q->dirty = true;
        q->status = PSQUEST_DELETE;  // discarded
        if (q->GetQuest()->HasInfinitePlayerLockout())
            q->lockout_end = 0;
        else
            q->lockout_end = csGetTicks() + q->GetQuest()->GetPlayerLockoutTime();  // assignment entry will be deleted after expiration

        Debug3(LOG_QUESTS, GetCharacterID(), "Player '%s' just discarded quest '%s'.\n",
               GetCharName(),q->GetQuest()->GetName() );

        UpdateQuestAssignments();
    }
    else
    {
        Debug3(LOG_QUESTS, GetCharacterID(),
               "Did not discard %s quest for player %s because it was already discarded or was a one-time quest.\n",
               q->GetQuest()->GetName(),GetCharName() );
		// Notify the player that he can't discard one-time quests
        psserver->SendSystemError(GetActor()->GetClient()->GetClientNum(),
			"You can't discard this quest, since it can be done just once!");
    }
}

bool psCharacter::CheckQuestAssigned(psQuest *quest)
{
    CS_ASSERT( quest );  // Must not be NULL
    QuestAssignment* questAssignment = IsQuestAssigned( quest->GetID() );
    if ( questAssignment )
    {
        if ( questAssignment->status == PSQUEST_ASSIGNED)
            return true;
    }
    return false;
}

bool psCharacter::CheckQuestCompleted(psQuest *quest)
{
    CS_ASSERT( quest );  // Must not be NULL
    QuestAssignment* questAssignment = IsQuestAssigned( quest->GetID());
    if ( questAssignment )
    {
        if ( questAssignment->status == PSQUEST_COMPLETE)
            return true;
    }
    return false;
}

//This incorrectly named function checks if the npc (assigner_id) is supposed to answer
// in the (parent)quest at this moment.
bool psCharacter::CheckQuestAvailable(psQuest *quest,int assigner_id)
{
    CS_ASSERT( quest );  // Must not be NULL

    csTicks now = csGetTicks();

    if (quest->GetParentQuest()) 
    {
        quest = quest->GetParentQuest();
    }

    bool notify = false;
    if (GetActor()->GetClient())
    {
        notify = CacheManager::GetSingleton().GetCommandManager()->Validate(GetActor()->GetClient()->GetSecurityLevel(), "quest notify");
    }

    //NPC should always answer, if the quest is assigned, no matter who started the quest. 
    QuestAssignment *q = IsQuestAssigned(quest->GetID());
    if (q && q->status == PSQUEST_ASSIGNED)
    {
        return true;
    }

    //Since the quest is not assigned, this conversation will lead to starting the quest.
    //Check all assigned quests, to make sure there is no other quest already started by this NPC
    for (size_t i=0; i<assigned_quests.GetSize(); i++)
    {
        if (assigned_quests[i]->GetQuest().IsValid() && assigned_quests[i]->assigner_id == assigner_id &&
            assigned_quests[i]->GetQuest()->GetID() != quest->GetID() &&
            assigned_quests[i]->GetQuest()->GetParentQuest() == NULL &&
            assigned_quests[i]->status == PSQUEST_ASSIGNED)
        {
            if (notify)
            {
                psserver->SendSystemInfo(GetActor()->GetClientID(),
                                         "GM NOTICE: Quest found, but you already have one assigned from same NPC");
            }

            return false; // Cannot have multiple quests from the same guy
        }
    }

    if (q) //then quest in assigned list, but not PSQUEST_ASSIGNED
    {
        // Character has this quest in completed list. Check if still in lockout
        if ( q->GetQuest()->HasInfinitePlayerLockout() ||
             q->lockout_end > now)
        {
            if (notify)
            {
                if (GetActor()->questtester) // GM flag
                {
                    psserver->SendSystemInfo(GetActor()->GetClientID(),
                        "GM NOTICE: Quest (%s) found and player lockout time has been overridden.",
                        quest->GetName());
                    return true; // Quest is available for GM
                }
                else
                {
                    if (q->GetQuest()->HasInfinitePlayerLockout())
                    {
                        psserver->SendSystemInfo(GetActor()->GetClientID(),
                            "GM NOTICE: Quest (%s) found but quest has infinite player lockout.",
                            quest->GetName());
                    }
                    else
                    {
                        psserver->SendSystemInfo(GetActor()->GetClientID(),
                            "GM NOTICE: Quest (%s) found but player lockout time hasn't elapsed yet. %d seconds remaining.",
                            quest->GetName(), (q->lockout_end - now)/1000 );
                    }

                }

            }

            return false; // Cannot have the same quest while in player lockout time.
        }
    }

    // If here, quest is not in assigned_quests, or it is completed and not in player lockout time
    // Player is allowed to start this quest, now check if quest has a lockout
    if (quest->GetQuestLastActivatedTime() &&
        (quest->GetQuestLastActivatedTime() + quest->GetQuestLockoutTime() > now))
    {
        if (notify)
        {
            if (GetActor()->questtester) // GM flag
            {
                psserver->SendSystemInfo(GetActor()->GetClientID(),
                                         "GM NOTICE: Quest(%s) found; quest lockout time has been overrided",
                                         quest->GetName());
                return true; // Quest is available for GM
            }
            else
                psserver->SendSystemInfo(GetActor()->GetClientID(),
                                         "GM NOTICE: Quest(%s) found, but quest lockout time hasn't elapsed yet. %d seconds remaining.",
                                         quest->GetName(),(quest->GetQuestLastActivatedTime()+quest->GetQuestLockoutTime()-now)/1000);
        }

        return false; // Cannot start this quest while in quest lockout time.
    }

    return true; // Quest is available
}

bool psCharacter::CheckResponsePrerequisite(NpcResponse *resp)
{
    CS_ASSERT( resp );  // Must not be NULL

    return resp->CheckPrerequisite(this);
}

int psCharacter::NumberOfQuestsCompleted(csString category)
{
    int count=0;
    for (size_t i=0; i<assigned_quests.GetSize(); i++)
    {
        // Character have this quest
        if (assigned_quests[i]->GetQuest().IsValid() && assigned_quests[i]->GetQuest()->GetParentQuest() == NULL &&
            assigned_quests[i]->status == PSQUEST_COMPLETE &&
            assigned_quests[i]->GetQuest()->GetCategory() == category)
        {
            count++;
        }
    }
    return count;
}

bool psCharacter::UpdateQuestAssignments(bool force_update)
{
    csTicks now = csGetTicks();

    for (size_t i=0; i<assigned_quests.GetSize(); i++)
    {
        QuestAssignment *q = assigned_quests[i];
        if (q->GetQuest().IsValid() && (q->dirty || force_update))
        {
            int r;

            // will delete the quest only after the expiration time, so the player cannot get it again immediately
            if (q->status == PSQUEST_DELETE &&
                !q->GetQuest()->HasInfinitePlayerLockout() &&
                (!q->GetQuest()->GetPlayerLockoutTime() || !q->lockout_end ||
                 (q->lockout_end < now))) // delete
            {
                r = db->CommandPump("delete from character_quests"
                                " where player_id=%d"
                                "   and quest_id=%d",
                                characterid,q->GetQuest()->GetID() );

                delete assigned_quests[i];
                assigned_quests.DeleteIndex(i);
                i--;  // reincremented in loop
                continue;
            }

            // Update or create a new entry in DB

            // Store remaining time to DB. Than remaining time has to be added to current time
            // at load from DB.
            csTicks remaining_time = 0;
            if (q->lockout_end && q->lockout_end > now)
                remaining_time = q->lockout_end - now;

            r = db->CommandPump("update character_quests "
                            "set status='%c',"
                            "remaininglockout=%ld, "
                            "last_response=%ld "
                            " where player_id=%d"
                            "   and quest_id=%d",
                            q->status,
                            remaining_time,
                            q->last_response,
                            characterid,
                            q->GetQuest()->GetID() );
            if (!r)  // no update done
            {
                r = db->CommandPump("insert into character_quests"
                                "(player_id, assigner_id, quest_id, status, remaininglockout, last_response) "
                                "values (%d, %d, %d, '%c', %d, %d)",
                                characterid,
                                q->assigner_id,
                                q->GetQuest()->GetID(),
                                q->status,
                                remaining_time,
                                q->last_response);

                if (r == -1)
                {
                    Error2("Could not insert character_quest row.  Error was <%s>.",db->GetLastError() );
                }
                else
                {
                    Debug3(LOG_QUESTS, GetCharacterID(), "Inserted quest info for player %d, quest %d.\n",characterid,assigned_quests[i]->GetQuest()->GetID() );
                }
            }
            else
            {
                Debug3(LOG_QUESTS, GetCharacterID(), "Updated quest info for player %d, quest %d.\n",characterid,assigned_quests[i]->GetQuest()->GetID() );
            }
            assigned_quests[i]->dirty = false;
        }
    }
    return true;
}


bool psCharacter::LoadQuestAssignments()
{
    Result result(db->Select("select * from character_quests"
                             " where player_id=%d", characterid));
    if (!result.IsValid())
    {
        Error3("Could not load quest assignments for character %d. Error was: %s",characterid, db->GetLastError() );
        return false;
    }

    csTicks now = csGetTicks();

    for (unsigned int i=0; i<result.Count(); i++)
    {
        QuestAssignment *q = new QuestAssignment;
        q->dirty = false;
        q->SetQuest(CacheManager::GetSingleton().GetQuestByID( result[i].GetInt("quest_id") ) );
        q->status = result[i]["status"][0];
        q->lockout_end = now + result[i].GetInt("remaininglockout");
        q->assigner_id = result[i].GetInt("assigner_id");
        q->last_response = result[i].GetInt("last_response");

        if (!q->GetQuest())
        {
            Error3("Quest %d for player %d not found!", result[i].GetInt("quest_id"), characterid );
            delete q;
            return false;
        }

        // Sanity check to see if time for completion is withing
        // lockout time.
        if (q->lockout_end > now + q->GetQuest()->GetPlayerLockoutTime())
            q->lockout_end = now + q->GetQuest()->GetPlayerLockoutTime();

        Debug6(LOG_QUESTS, characterid, "Loaded quest %-40.40s, status %c, lockout %lu, last_response %d, for player %s.\n",
               q->GetQuest()->GetName(),q->status,
               ( q->lockout_end > now ? q->lockout_end-now:0),q->last_response, GetCharFullName());
        assigned_quests.Push(q);
    }
    return true;
}

psGuildLevel * psCharacter::GetGuildLevel()
{
    if (guildinfo == NULL)
        return 0;

    psGuildMember * membership = guildinfo->FindMember((unsigned int)characterid);
    if (membership == NULL)
        return 0;

    return membership->guildlevel;
}

void psCharacter::RemoveBuddy( unsigned int buddyID )
{
    for ( size_t x = 0; x < buddyList.GetSize(); x++ )
    {
        if ( buddyList[x].playerID == buddyID )
        {
            buddyList.DeleteIndex(x);
            return;
        }
    }
}

void psCharacter::BuddyOf( unsigned int buddyID )
{
    if ( buddyOfList.Find( buddyID )  == csArrayItemNotFound )
    {
        buddyOfList.Push( buddyID );
    }
}

void psCharacter::NotBuddyOf( unsigned int buddyID )
{
    buddyOfList.Delete( buddyID );
}

bool psCharacter::AddBuddy( unsigned int buddyID, csString& buddyName )
{
    // Cannot addself to buddy list
    if ( buddyID == characterid )
        return false;

    for ( size_t x = 0; x < buddyList.GetSize(); x++ )
    {
        if ( buddyList[x].playerID == buddyID )
        {
            return true;
        }
    }

    Buddy b;
    b.name = buddyName;
    b.playerID = buddyID;

    buddyList.Push( b );

    return true;
}



double psCharacter::GetProperty(const char *ptr)
{
    if (!strcasecmp(ptr,"AttackerTargeted"))
    {
        return true;
        // return (attacker_targeted) ? 1 : 0;
    }
    else if (!strcasecmp(ptr,"TotalTargetedBlockValue"))
    {
        return GetTotalTargetedBlockValue();
    }
    else if (!strcasecmp(ptr,"TotalUntargetedBlockValue"))
    {
        return GetTotalUntargetedBlockValue();
    }
    else if (!strcasecmp(ptr,"DodgeValue"))
    {
        return GetDodgeValue();
    }
    else if (!strcasecmp(ptr,"KillExp"))
    {
        return kill_exp;
    }
    else if (!strcasecmp(ptr,"getAttackValueModifier"))
    {
        return attackValueModifier;
    }
    else if (!strcasecmp(ptr,"getDefenseValueModifier"))
    {
        return defenseValueModifier;
    }
    else if (!strcasecmp(ptr,"Strength"))
    {
        return attributes.GetStat(PSITEMSTATS_STAT_STRENGTH,true);
    }
    else if (!strcasecmp(ptr,"Agility"))
    {
        return attributes.GetStat(PSITEMSTATS_STAT_AGILITY,true);
    }
    else if (!strcasecmp(ptr,"Endurance"))
    {
        return attributes.GetStat(PSITEMSTATS_STAT_ENDURANCE,true);
    }
    else if (!strcasecmp(ptr,"Intelligence"))
    {
        return attributes.GetStat(PSITEMSTATS_STAT_INTELLIGENCE,true);
    }
    else if (!strcasecmp(ptr,"Will"))
    {
        return attributes.GetStat(PSITEMSTATS_STAT_WILL,true);
    }
    else if (!strcasecmp(ptr,"Charisma"))
    {
        return attributes.GetStat(PSITEMSTATS_STAT_CHARISMA,true);
    }
    else if (!strcasecmp(ptr,"BaseStrength"))
    {
        return attributes.GetStat(PSITEMSTATS_STAT_STRENGTH,false);
    }
    else if (!strcasecmp(ptr,"BaseAgility"))
    {
        return attributes.GetStat(PSITEMSTATS_STAT_AGILITY,false);
    }
    else if (!strcasecmp(ptr,"BaseEndurance"))
    {
        return attributes.GetStat(PSITEMSTATS_STAT_ENDURANCE,false);
    }
    else if (!strcasecmp(ptr,"BaseIntelligence"))
    {
        return attributes.GetStat(PSITEMSTATS_STAT_INTELLIGENCE,false);
    }
    else if (!strcasecmp(ptr,"BaseWill"))
    {
        return attributes.GetStat(PSITEMSTATS_STAT_WILL,false);
    }
    else if (!strcasecmp(ptr,"BaseCharisma"))
    {
        return attributes.GetStat(PSITEMSTATS_STAT_CHARISMA,false);
    }
    else if (!strcasecmp(ptr,"AllArmorStrMalus"))
    {
        return modifiers.GetStat(PSITEMSTATS_STAT_STRENGTH);
    }
    else if (!strcasecmp(ptr,"AllArmorAgiMalus"))
    {
        return modifiers.GetStat(PSITEMSTATS_STAT_AGILITY);
    }
    else if (!strcasecmp(ptr,"CombatStance"))
    {
        // Backwards compatibility.
        return GetCombatStance().stance_id;
    }
    Error2("Requested psCharacter property not found '%s'", ptr);
    return 0;
}

double psCharacter::CalcFunction(const char * functionName, const double * params)
{
    if (!strcasecmp(functionName, "GetStatValue"))
    {
        psCharacter *my_char = this;
        PSITEMSTATS_STAT stat = (PSITEMSTATS_STAT)(int)params[0];

        return (double)my_char->GetAttributes()->GetStat(stat);
    }
    else if (!strcasecmp(functionName, "GetAverageSkillValue"))
    {
        psCharacter *my_char = this;
        PSSKILL skill1 = (PSSKILL)(int)params[0];
        PSSKILL skill2 = (PSSKILL)(int)params[1];
        PSSKILL skill3 = (PSSKILL)(int)params[2];

        double v1 = my_char->GetSkills()->GetSkillRank(skill1);

        if (skill2!=PSSKILL_NONE) {
            double v2 = my_char->GetSkills()->GetSkillRank(skill2);
            v1 = (v1+v2)/2;
        }

        if (skill3!=PSSKILL_NONE) {
            double v3 = my_char->GetSkills()->GetSkillRank(skill3);
            v1 = (v1+v3)/2;
        }

        // always give a small % of combat skill, or players will never be able to get the first exp
        if (v1==0)
            v1 = 0.7;

        return v1;
    }
    else if (!strcasecmp(functionName, "GetSkillValue"))
    {
        psCharacter *my_char = this;
        PSSKILL skill = (PSSKILL)(int)params[0];

        double value = my_char->GetSkills()->GetSkillRank(skill);

        // always give a small % of melee (unharmed) skill
        if (skill==PSSKILL_MARTIALARTS && value==0)
            value = 0.2;

        return value;
    }

    CPrintf(CON_ERROR, "psItem::CalcFunction(%s) failed\n", functionName);
    return 0;
}

/** A skill can only be trained if the player requires points for it.
  */
bool psCharacter::CanTrain( PSSKILL skill )
{
    return skills.CanTrain( skill );
}

void psCharacter::Train( PSSKILL skill, int yIncrease )
{
    // Did we train stats?
    if( skill == PSSKILL_AGI ||
        skill == PSSKILL_CHA ||
        skill == PSSKILL_END ||
        skill == PSSKILL_INT ||
        skill == PSSKILL_WILL ||
        skill == PSSKILL_STR )
    {
        Skill* cskill = skills.GetSkill(skill);

        skills.Train( skill, yIncrease );
        int know = cskill->y;
        int cost = cskill->yCost;

        // We ranked up
        if(know >= cost)
        {
            cskill->rank++;
            cskill->y = 0;
            int after = cskill->rank;
            cskill->CalculateCosts(this);

            // Insert into the stats
            switch(skill)
            {
                case PSSKILL_AGI:
                    attributes.SetStat(PSITEMSTATS_STAT_AGILITY,after); break;
                case PSSKILL_CHA:
                    attributes.SetStat(PSITEMSTATS_STAT_CHARISMA,after); break;
                case PSSKILL_END:
                    attributes.SetStat(PSITEMSTATS_STAT_ENDURANCE,after); break;
                case PSSKILL_INT:
                    attributes.SetStat(PSITEMSTATS_STAT_INTELLIGENCE,after); break;
                case PSSKILL_WILL:
                    attributes.SetStat(PSITEMSTATS_STAT_WILL,after); break;
                case PSSKILL_STR:
                    attributes.SetStat(PSITEMSTATS_STAT_STRENGTH,after); break;
                default:
                    break;
            }

            // Save stats
            if(GetActor()->GetClientID() != 0)
            {
                const char *fieldnames[]= {
                   "base_strength",
                   "base_agility",
                   "base_endurance",
                   "base_intelligence",
                   "base_will",
                   "base_charisma"
                    };
                psStringArray fieldvalues;
                fieldvalues.FormatPush("%d",attributes.GetStat(PSITEMSTATS_STAT_STRENGTH, false));
                fieldvalues.FormatPush("%d",attributes.GetStat(PSITEMSTATS_STAT_AGILITY, false));
                fieldvalues.FormatPush("%d",attributes.GetStat(PSITEMSTATS_STAT_ENDURANCE, false));
                fieldvalues.FormatPush("%d",attributes.GetStat(PSITEMSTATS_STAT_INTELLIGENCE, false));
                fieldvalues.FormatPush("%d",attributes.GetStat(PSITEMSTATS_STAT_WILL, false));
                fieldvalues.FormatPush("%d",attributes.GetStat(PSITEMSTATS_STAT_CHARISMA, false));

                csString id;
                id = GetCharacterID();

                if(!db->GenericUpdateWithID("characters","id",id,fieldnames,fieldvalues))
                {
                    Error2("Couldn't save stats for character %u!\n",GetCharacterID());
                }
            }

            // When a stat is ranked up, hp, mana and stamina are recalculated
            RecalculateStats();
            inventory.CalculateLimits();
        }
    }
    else
    {
        skills.Train( skill, yIncrease ); // Normal training
        psServer::CharacterLoader.UpdateCharacterSkill(
                GetCharacterID(),
                skill,
                skills.GetSkillPractice((PSSKILL)skill),
                skills.GetSkillKnowledge((PSSKILL)skill),
                skills.GetSkillRank((PSSKILL)skill, false)
                );
        RecalculateStats();
        inventory.CalculateLimits();
    }
}



/*-----------------------------------------------------------------*/


void Skill::CalculateCosts(psCharacter* user)
{
    if(!info || !user)
        return;

    // Calc the new Y/Z cost
    MathScript* costScript;
    csString scriptName;

    if(info->id < PSSKILL_AGI || info->id > PSSKILL_WILL)
        scriptName = "CalculateSkillCosts";
    else
        scriptName = "CalculateStatCosts";

    costScript = psserver->GetMathScriptEngine()->FindScript(scriptName);
    if (!costScript)
    {
        Error2("Couldn't find script %s!",scriptName.GetData());
        return;
    }

    // Output
    MathScriptVar*  yCostVar        = costScript->GetVar("YCost");
    MathScriptVar*  zCostVar        = costScript->GetVar("ZCost");

    // Input
    MathScriptVar*  baseCostVar  = costScript->GetOrCreateVar("BaseCost");
    MathScriptVar*  skillRankVar = costScript->GetOrCreateVar("SkillRank");
    MathScriptVar*  skillIDVar   = costScript->GetOrCreateVar("SkillID");
    MathScriptVar*  practiceVar  = costScript->GetOrCreateVar("PracticeFactor");
    MathScriptVar*  mentalVar    = costScript->GetOrCreateVar("MentalFactor");
    MathScriptVar*  actorVar     = costScript->GetOrCreateVar("Actor");

    if(!yCostVar)
    {
        Warning2(LOG_SKILLXP,"Couldn't find the YCost var in %s script!\n",scriptName.GetData());
        return;
    }
    if(!zCostVar)
    {
        Warning2(LOG_SKILLXP,"Couldn't find the ZCost var in %s script!\n",scriptName.GetData());
        return;
    }

    // Input the data
    skillRankVar->SetValue((double)rank);
    skillIDVar->SetValue((double)info->id);
    practiceVar->SetValue((double)info->practice_factor);
    mentalVar->SetValue((double)info->mental_factor);
    baseCostVar->SetValue((double)info->baseCost);
    actorVar->SetObject(user);

    // Execute
    costScript->Execute();

    // Get the output
    yCost = (int)yCostVar->GetValue();
    zCost = (int)zCostVar->GetValue();
    // Make sure the y values is clamped to the cost.  Otherwise Practice may always
    // fail.
    if  (y > yCost)
    {
        dirtyFlag = true;
        y = yCost;
    }
    if ( z > zCost )
    {
        dirtyFlag = true;
        z = zCost;        
    }
}

void Skill::Train( int yIncrease )
{
    y+=yIncrease;
    if ( y > yCost )
        y = yCost;

    dirtyFlag = true;        
}


bool Skill::Practice( unsigned int amount, unsigned int& actuallyAdded,psCharacter* user )
{
    bool rankup = false;
    // Practice can take place
    if ( yCost == y )
    {
        z+=amount;
        if ( z >= zCost )
        {
            rank++;
            z = 0;
            y = 0;
            actuallyAdded = z - zCost;
            rankup = true;
            // Reset the costs for Y/Z
            CalculateCosts(user);
        }
        else
        {
            actuallyAdded = amount;
        }
    }
    else
    {
        actuallyAdded = 0;
    }

    dirtyFlag = true;
    return rankup;
}

void psCharacter::SetSkillRank( PSSKILL which, int rank)
{
    if (rank < 0)
        rank = 0;

    skills.SetSkillRank(which, rank);

    if (which == PSSKILL_AGI )
        attributes.SetStat(PSITEMSTATS_STAT_AGILITY, rank);
    else if( which == PSSKILL_CHA )
        attributes.SetStat(PSITEMSTATS_STAT_CHARISMA, rank);
    else if( which == PSSKILL_END )
        attributes.SetStat(PSITEMSTATS_STAT_ENDURANCE, rank);
    else if( which == PSSKILL_INT )
        attributes.SetStat(PSITEMSTATS_STAT_INTELLIGENCE, rank);
    else if( which == PSSKILL_STR )
        attributes.SetStat(PSITEMSTATS_STAT_STRENGTH, rank);
    else if( which == PSSKILL_WILL)
        attributes.SetStat(PSITEMSTATS_STAT_WILL,rank);

    inventory.CalculateLimits();    
}

unsigned int psCharacter::GetCharLevel()
{
    return ( attributes.GetStat(PSITEMSTATS_STAT_STRENGTH) +
             attributes.GetStat(PSITEMSTATS_STAT_ENDURANCE) +
             attributes.GetStat(PSITEMSTATS_STAT_AGILITY) +
             attributes.GetStat(PSITEMSTATS_STAT_INTELLIGENCE) +
             attributes.GetStat(PSITEMSTATS_STAT_WILL) +
             attributes.GetStat(PSITEMSTATS_STAT_CHARISMA) ) / 6;
}

//This function recalculates Hp, Mana, and Stamina when needed (char creation, combats, training sessions)
void psCharacter::RecalculateStats()
{
    // Calculate current Max Mana level:
    static MathScript *maxManaScript;

    if (!maxManaScript)
    {
        // Max Mana Script isn't loaded, so load it
        maxManaScript = psserver->GetMathScriptEngine()->FindScript("CalculateMaxMana");
        CS_ASSERT(maxManaScript != NULL);
    }


    if ( maxManaScript )
    {
        MathScriptVar* actorvar  = maxManaScript->GetOrCreateVar("Actor");
        actorvar->SetObject(this);

        if (override_max_mana)
        {
            SetManaMax(override_max_mana);
        }
        else
        {
            maxManaScript->Execute();
            MathScriptVar* manaValue =  maxManaScript->GetVar("MaxMana");
            SetManaMax(manaValue->GetValue() + GetManaMaxModifier());
        }
    }

    // Calculate current Max HP level:
    static MathScript *maxHPScript;

    if (!maxHPScript)
    {
        // Max HP Script isn't loaded, so load it
        maxHPScript = psserver->GetMathScriptEngine()->FindScript("CalculateMaxHP");
        CS_ASSERT(maxHPScript != NULL);
    }


    if ( maxHPScript )
    {
        MathScriptVar* actorvar  = maxHPScript->GetOrCreateVar("Actor");
        actorvar->SetObject(this);

        if (override_max_hp)
        {
            SetHitPointsMax(override_max_hp);
        }
        else
        {
            maxHPScript->Execute();
            MathScriptVar* HPValue =  maxHPScript->GetVar("MaxHP");
            SetHitPointsMax(HPValue->GetValue() + GetHitPointsMaxModifier());
        }
    }

    // The max weight that a player can carry
    inventory.CalculateLimits();

    // Stamina
    CalculateMaxStamina();

    // Speed
//    if (GetActor())
//        GetActor()->UpdateAllSpeedModifiers();
}

size_t psCharacter::GetAssignedGMEvents(psGMEventListMessage& gmeventsMsg, int clientnum)
{
    // GM events consist of events ran by the GM (running & completed) and
    // participated in (running & completed).
    size_t numberOfEvents = 0;
    csString gmeventsStr, event, name, desc;
    gmeventsStr.Append("<gmevents>");
    GMEventStatus eventStatus;

    // XML: <event><name text><role text><status text><id text></event>
    if (assigned_events.runningEventIDAsGM >= 0)
    {
        eventStatus = psserver->GetGMEventManager()->GetGMEventDetailsByID(assigned_events.runningEventIDAsGM,
                                                                           name,
                                                                           desc);
        event.Format("<event><name text=\"%s\" /><role text=\"*\" /><status text=\"R\" /><id text=\"%d\" /></event>",
                     name.GetData(), assigned_events.runningEventIDAsGM);
        gmeventsStr.Append(event);
        numberOfEvents++;
    }
    if (assigned_events.runningEventID >= 0)
    {
        eventStatus = psserver->GetGMEventManager()->GetGMEventDetailsByID(assigned_events.runningEventID,
                                                                           name,
									   desc);
        event.Format("<event><name text=\"%s\" /><role text=\" \" /><status text=\"R\" /><id text=\"%d\" /></event>",
                     name.GetData(), assigned_events.runningEventID);
        gmeventsStr.Append(event);
        numberOfEvents++;
    }

    csArray<int>::Iterator iter = assigned_events.completedEventIDsAsGM.GetIterator();
    while(iter.HasNext())
    {
        int gmEventIDAsGM = iter.Next();
        eventStatus = psserver->GetGMEventManager()->GetGMEventDetailsByID(gmEventIDAsGM,
                                                                           name,
                                                                           desc);
        event.Format("<event><name text=\"%s\" /><role text=\"*\" /><status text=\"C\" /><id text=\"%d\" /></event>",
                     name.GetData(), gmEventIDAsGM);
        gmeventsStr.Append(event);
        numberOfEvents++;
    }

    csArray<int>::Iterator iter2 = assigned_events.completedEventIDs.GetIterator();
    while(iter2.HasNext())
    {
        int gmEventID = iter2.Next();
        eventStatus = psserver->GetGMEventManager()->GetGMEventDetailsByID(gmEventID,
                                                                           name,
                                                                           desc);
        event.Format("<event><name text=\"%s\" /><role text=\" \" /><status text=\"C\" /><id text=\"%d\" /></event>",
                     name.GetData(), gmEventID);
        gmeventsStr.Append(event);
        numberOfEvents++;
    }

    gmeventsStr.Append("</gmevents>");

    if (numberOfEvents)
	gmeventsMsg.Populate(gmeventsStr, clientnum);

    return numberOfEvents;
}

void psCharacter::AssignGMEvent(int id, bool playerIsGM)
{
    if (playerIsGM)
        assigned_events.runningEventIDAsGM = id;
    else
        assigned_events.runningEventID = id;
}

void psCharacter::CompleteGMEvent(bool playerIsGM)
{
    if (playerIsGM)
    {
        assigned_events.completedEventIDsAsGM.Push(assigned_events.runningEventIDAsGM);
	assigned_events.runningEventIDAsGM = -1;
    }
    else
    {
        assigned_events.completedEventIDs.Push(assigned_events.runningEventID);
        assigned_events.runningEventID = -1;
    }
}

void psCharacter::RemoveGMEvent(int id)
{
    if (assigned_events.runningEventID == id)
        assigned_events.runningEventID = -1;
    else
        assigned_events.completedEventIDs.Delete(id);
}


bool psCharacter::UpdateFaction(Faction * faction, int delta)
{
    if (!GetActor()) 
    {
        return false;
    }        

    GetActor()->GetFactions()->UpdateFactionStanding(faction->id,delta);
    if (delta > 0)
    {
        psserver->SendSystemInfo(GetActor()->GetClientID(),"Your faction with %s has improved.",faction->name.GetData());
    }
    else
    {
        psserver->SendSystemInfo(GetActor()->GetClientID(),"Your faction with %s has worsened.",faction->name.GetData());
    }
    
    
    psFactionMessage factUpdate( GetActor()->GetClientID(), psFactionMessage::MSG_UPDATE);
    int standing;
    float weight;
    GetActor()->GetFactions()->GetFactionStanding(faction->id, standing ,weight);
    
    factUpdate.AddFaction(faction->name, standing);
    factUpdate.BuildMsg();
    factUpdate.SendMessage();
    
    return true;
}

bool psCharacter::CheckFaction(Faction * faction, int value)
{
    if (!GetActor()) return false;

    return GetActor()->GetFactions()->CheckFaction(faction,value);
}

const char* psCharacter::GetDescription()
{
    return description; 
}

void psCharacter::SetDescription(const char* newValue) 
{
    description = newValue;
    bool bChanged = false;
    while (description.Find("\n\n\n\n") != (size_t)-1)
    {
        bChanged = true;
        description.ReplaceAll("\n\n\n\n", "\n\n\n");
    }

    if (bChanged && GetActor() && GetActor()->GetClient())
        psserver->SendSystemError(GetActor()->GetClient()->GetClientNum(), "Warning! Description trimmed.");
}


//TODO: Make this not return a temp csString, but fix in place
csString NormalizeCharacterName(const csString & name)
{
    csString normName = name;
    normName.Downcase();
    normName.Trim();
    if (normName.Length() > 0)
        normName.SetAt(0,toupper(normName.GetAt(0)));
    return normName;
}



unsigned int StatSet::GetStat(PSITEMSTATS_STAT attrib, bool withBuff)
{
    if (attrib<0 || attrib>=PSITEMSTATS_STAT_COUNT)
        return 0;

    int buff   = withBuff?stats[attrib].rankBuff:0;
    int result = (int)stats[attrib].rank + buff;
    return result;
}

int StatSet::GetBuffVal( PSITEMSTATS_STAT attrib )
{
    if (attrib<0 || attrib>=PSITEMSTATS_STAT_COUNT)
        return 0;

    return stats[attrib].rankBuff;
}


void StatSet::BuffStat( PSITEMSTATS_STAT attrib, int buffAmount )
{
    if (attrib<0 || attrib>=PSITEMSTATS_STAT_COUNT)
        return;

    // should buffed stat go negative, we set it to zero
    if (int(stats[attrib].rank) + stats[attrib].rankBuff + buffAmount < 0)
        stats[attrib].rankBuff = -(int)stats[attrib].rank;
    else
        stats[attrib].rankBuff+=buffAmount;

    self->RecalculateStats();
}

void StatSet::SetStat(PSITEMSTATS_STAT attrib, unsigned int val, bool recalculatestats)
{
    if (attrib<0 || attrib>=PSITEMSTATS_STAT_COUNT)
        return;

    // Clamp values to prevent huge numbers resulting from bugs.
    if (val > MAX_STAT)
        val = MAX_STAT;

    stats[attrib].rank=val;
    if (recalculatestats)
      self->RecalculateStats();
}

void StatSet::AddToStat(PSITEMSTATS_STAT attrib, unsigned int delta)
{
    if (attrib<0 || attrib>=PSITEMSTATS_STAT_COUNT)
        return;

    stats[attrib].rank += delta;

    self->RecalculateStats();
}



int SkillSet::AddSkillPractice(PSSKILL skill, unsigned int val)
{
    unsigned int added;
    bool rankUp;
    csString name = "";

    rankUp = AddToSkillPractice(skill,val, added);

    psSkillInfo* skillInfo = CacheManager::GetSingleton().GetSkillByID(skill);

    if ( skillInfo )
    {
        // Save skill and practice only when the level reached a new rank
        // this is done to avoid saving to db each time a player hits
        // an opponent
        if(rankUp && self->GetActor()->GetClientID() != 0)
        {
            psServer::CharacterLoader.UpdateCharacterSkill(
                self->GetCharacterID(),
                skill,
                GetSkillPractice((PSSKILL)skill),
                GetSkillKnowledge((PSSKILL)skill),
                GetSkillRank((PSSKILL)skill)
                );
        }


        name = skillInfo->name;
        Debug5(LOG_SKILLXP,self->GetActor()->GetClientID(),"Adding %d points to skill %s to character %s (%d)\n",val,skillInfo->name.GetData(),
            self->GetCharFullName(),
            self->GetActor()->GetClientID());
    }
    else
    {
        Debug4(LOG_SKILLXP,self->GetActor()->GetClientID(),"WARNING! Skill practise to unknown skill(%d) for character %s (%d)\n",
            (int)skill,
            self->GetCharFullName(),
            self->GetActor()->GetClientID());
    }

    if ( added > 0 )
    {
        psZPointsGainedEvent evt( self->GetActor(), skillInfo->name, added, rankUp );
        evt.FireEvent();
    }

    return added;
}

Skill * SkillSet::GetSkill( PSSKILL which )
{
    if (which<0 || which>=PSSKILL_COUNT)
        return NULL;
    else
        return & skills[which];
}

unsigned int SkillSet::GetBestSkillValue( bool withBuffer )
{
    unsigned int max=0;
    for (int i=0; i<PSSKILL_COUNT; i++)
    {
        PSSKILL skill = skills[i].info->id;
        if(     skill == PSSKILL_AGI ||
                skill == PSSKILL_CHA ||
                skill == PSSKILL_END ||
                skill == PSSKILL_INT ||
                skill == PSSKILL_WILL ||
                skill == PSSKILL_STR )
            continue; // Jump past the stats, we only want the skills

        unsigned int rank = skills[i].rank + (withBuffer?skills[i].rankBuff:0);
        if (rank > max)
            max = rank;
    }
    return max;
}

unsigned int SkillSet::GetBestSkillSlot( bool withBuffer )
{
    unsigned int max = 0;
    unsigned int i = 0;
    for (; i<PSSKILL_COUNT; i++)
    {
        unsigned int rank = skills[i].rank + withBuffer ? skills[i].rankBuff : 0;
        if (rank > max)
            max = rank;
    }

    if (i == PSSKILL_COUNT)
        return (unsigned int)~0;
    else
        return i;
}

void SkillSet::Calculate()
{
    for ( int z = 0; z < PSSKILL_COUNT; z++ )
    {
        skills[z].CalculateCosts(self);
    }
    self->RecalculateStats();
}


bool SkillSet::CanTrain( PSSKILL skill )
{
    if (skill<0 || skill>=PSSKILL_COUNT)
        return false;
    else
    {
        return skills[skill].CanTrain();
    }
}

void SkillSet::Train( PSSKILL skill, int yIncrease )
{

    if (skill<0 ||skill>=PSSKILL_COUNT)
        return;
    else
    {
        skills[skill].Train( yIncrease );
    }
}


void SkillSet::SetSkillInfo( PSSKILL which, psSkillInfo* info, bool recalculatestats )
{
    if (which<0 || which>=PSSKILL_COUNT)
        return;
    else
    {
        skills[which].info = info;
        skills[which].CalculateCosts(self);
    }

    if (recalculatestats)
      self->RecalculateStats();
}

void SkillSet::SetSkillRank( PSSKILL which, int rank, bool recalculatestats )
{
    if (which<0 || which>=PSSKILL_COUNT)
        return;

    // Clamp rank to stay within sane values, even if given something totally outrageous.
    bool isStat = which >= PSSKILL_AGI && which <= PSSKILL_WILL;
    if (rank < 0)
        rank = 0;
    else if (isStat && rank > MAX_STAT)
        rank = MAX_STAT;
    else if (!isStat && rank > MAX_SKILL)
        rank = MAX_SKILL;

    skills[which].rank = rank;
    skills[which].CalculateCosts(self);
    skills[which].dirtyFlag = true;

    if (recalculatestats)
      self->RecalculateStats();
}

void SkillSet::BuffSkillRank( PSSKILL which, int buff )
{
    if (which<0 || which>=PSSKILL_COUNT)
        return;
    skills[which].Buff( buff );

    self->RecalculateStats();
}

void SkillSet::SetSkillKnowledge( PSSKILL which, int y_value )
{
    if (which<0 || which>=PSSKILL_COUNT)
        return;
    if (y_value < 0)
        y_value = 0;
    skills[which].y = y_value;
    skills[which].dirtyFlag = true;
}


void SkillSet::SetSkillPractice(PSSKILL which,int z_value)
{
    if (which<0 || which>=PSSKILL_COUNT)
        return;
    if (z_value < 0)
        z_value = 0;

    skills[which].z = z_value;
    skills[which].dirtyFlag = true;
}


bool SkillSet::AddToSkillPractice(PSSKILL skill, unsigned int val, unsigned int& added )
{
    if (skill<0 || skill>=PSSKILL_COUNT)
        return 0;

    bool rankup = false;
    rankup = skills[skill].Practice( val, added, self );
    return rankup;
}


unsigned int SkillSet::GetSkillPractice(PSSKILL skill)
{

    if (skill<0 || skill>=PSSKILL_COUNT)
        return 0;
    return skills[skill].z;
}


unsigned int SkillSet::GetSkillKnowledge(PSSKILL skill)
{

    if (skill<0 || skill>=PSSKILL_COUNT)
        return 0;
    return skills[skill].y;
}

unsigned int SkillSet::GetSkillRank( PSSKILL skill, bool withBuffer )
{
    if (skill<0 || skill>=PSSKILL_COUNT)
        return 0;

    int buff   = withBuffer?skills[skill].rankBuff:0;
    int result = (int)skills[skill].rank + buff;

    if (result < 0)
        return 0;

    return (unsigned int)result;
}


Skill& SkillSet::Get(PSSKILL skill)
{
    return skills[skill];
}


//////////////////////////////////////////////////////////////////////////
// QuestAssignment accessor functions to accommodate removal of csWeakRef better
//////////////////////////////////////////////////////////////////////////

csWeakRef<psQuest>& QuestAssignment::GetQuest()
{
    if (!quest.IsValid())
    {
        psQuest *q = CacheManager::GetSingleton().GetQuestByID(quest_id);
        SetQuest(q);
    }
    return quest;
}

void QuestAssignment::SetQuest(psQuest *q)
{
    if (q)
    {
        quest = q;
        quest_id = q->GetID();
    }
}

