/*
 * cachemanager.h
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

#ifndef __CACHEMANAGER_H__
#define __CACHEMANAGER_H__

//=============================================================================
// Crystal Space Includes
//=============================================================================
#include <csutil/stringarray.h>
#include <csutil/hash.h>
#include <csgeom/vector3.h>

//=============================================================================
// Project Space Includes
//=============================================================================
#include "util/slots.h"
#include "util/gameevent.h"

#include "bulkobjects/pscharacter.h"
#include "bulkobjects/psitemstats.h"

//=============================================================================
// Local Includes
//=============================================================================
#include "icachedobject.h"

class psSectorInfo;
class Client;
class EntityManager;
class psGuildInfo;
class psGuildAlliance;
class psSkillInfo;
class psAccountInfo;
class psQuest;
class psTradePatterns;
class psTradeProcesses;
class psTradeCombinations;
class psTradeTransformations;
class psTradeAutoContainers;
class psItemSet;
class psCommandManager;
class psSpell;
class psItemStats;
class psItem;
class RandomizedOverlay;

struct CraftTransInfo;
struct CombinationConstruction;
struct CombinationSet;
struct CraftComboInfo;
struct psItemAnimation;
struct psItemCategory;
struct psWay;
struct Faction;
struct psTrait;
struct psRaceInfo;
struct Stance;

struct ArmorVsWeapon
{
    int id;
    float c [3][4];
    csString weapontype;
};

/// A list of the flags mapped to their IDs
struct psItemStatFlags
{
    csString string;
    int flag;

    psItemStatFlags(const char *s, int f) : string(s), flag(f) { }
    void Set(const char *s,int f) { string=s; flag=f; }
};

// HACK TO GET AROUND BAD INCLUSION OF PSCHARACTER.H IN DRMESSAGE.H KWF
#ifndef __PSMOVEMENT_H__
/// A character mode and its properties
struct psCharMode
{
    uint32 id;
    csString name;
    csVector3 move_mod;       ///< motion multiplier
    csVector3 rotate_mod;     ///< rotation multiplier
    csString idle_animation;  ///< animation when not moving
};

/// A movement type and its properties
struct psMovement
{
    uint32 id;
    csString name;
    csVector3 base_move;    ///< motion for this
    csVector3 base_rotate;  ///< rotation for this
};
#endif

/**
 * This class allows CacheManager to yield a list of qualifying
 * values given a player's score at something.  The score could
 * be a skill value, a stat value or a mathscript result which
 * combines multiple values.  For example, with sketches, an
 * array of these is used to yield a list of icons the player
 * may use with this character, based on Illustration skill and
 * INT.
 */
struct psCharacterLimitation
{
    uint32 id;              ///< unique id
    csString limit_type;    ///< 'SKETCH' limitations all have this word here
    int      min_score;     ///< What must a player score to earn this ability
    csString value;         ///< What is the name of the ability they earn
};


/** This class rappresents an option tree which comes from the server_options table.
 *  You can iterate the tree starting from the root or get a leaf and start search
 *  from there: useful if a specific class/manager wants to keep only the reference
 *  to the options it needs in place of iterating the entire tree each time. All the
 *  entries in the tree are the same, so you can assign and get a value to/from any.
 *  Right now : is used as path delimiter.
 */
class optionEntry
{
    private:
    csString value; ///< The value of this option entry.
    csHash<optionEntry,csString> subOptions; ///< array of sub option entries to this.
    public:
    /** Sets the value of this specific option to a newvalue.
     *  @note for now there is no support for db saving so this is only temporary.
     *  @param newValue The new string to assign to this option
     */
    void setValue(const csString newValue) { value = newValue; }
    /** Get the value of this specific option.
     *  @return A csString containing the option value.
     */
    csString getValue() { return value; }
    /** Get the value of this specific option in double format.
     *  @return A double containing the option value converted to double.
     */
    double getValueAsDouble() { return atof(value); }
    /** Get the value of this specific option in int format.
     *  @return An int containing the option value converted to int.
     */
    int getValueAsInt() { return atoi(value); }
    /** Get the value of this specific option in csVector3 format.
     *  This function takes in account the format is x,y,z as text, returns a vector with 0,0,0
     *  in case of error.
     *  @return A csVector3 containing the option value converted to a csvector.
     */
    csVector3 getValueAsVector();
    /** Get the value of this specific option in csVector3 format.
     *  This function takes in account the format is x,y,z as text, this version allows to check
     *  if there was a conversion error.
     *  @param vector A reference to a vector where the value will be stored.
     *  @return A boolean TRUE if the conversion was successfull.
     */
    bool getValueAsVector(csVector3 &vector);
    /** Sets an option in the option tree.
     *  @note for now there is no support for db saving so this is only temporary.
     *  @note If the value was never set the return will be an empty csString
     *        (internal string not initialized see crystal space documentation)
     *  @param path A path relative to this optionEntry. It's the full path only when
     *              using the "root" optionEntry.
     *  @param value The value to set to the option we are adding to the tree.
     */
    bool setOption(const csString path, const csString value);
    /** Gets an option entry in the tree. It doesn't have to be the actual option we want
     *  a value from but also a folder containing the set of options we are interested for.
     *  This is useful to allow managers to keep their reference to a specific part of the
     *  tree and so avoid going through all the branches to reach what they are interested
     *  each time.
     *  @param path A path to the optionEntry we are interested in.
     *  @return A pointer to the optionEntry we are interested in. NULL if the optionEntry
     *          was not found.
     */
    optionEntry *getOption(const csString path);
    /** Does the same of getOption, but, in case the entry is missing, an empty option will
     *  be added to the tree.
     *  @param path A path to the optionEntry we are interested in.
     *  @param fallback A value to associate to the option in case it was not found.
     *  @return A pointer to the optionEntry we are interested in. It still returns NULL if the
     *          data is invalid, but that's a programming error and not a runtime error.
     */
    optionEntry *getOptionSafe(const csString path, const csString fallback);
};

/** This class manages the caching of data that is unchanging during server operation.
* Data stored here is loaded from the database at server startup and retrieved by other code from this cache.
* This allows the database to be used to store configuration data, but we don't have to query the database
* every time we need access.
*
* Meshes, Textures, Images, Parts, and Sectors may be loaded without other lists being loaded first.
* Traits will fail if Textures, Parts and Meshes are not loaded beforehand.
* Races will fail if Meshes, Textures and Sectors are not loaded beforehand.
*
*/
class CacheManager
{
public:
    CacheManager();
    ~CacheManager();

    /** @name Sector Info Table cache
    */
    //@{
    psSectorInfo *GetSectorInfoByName(const char *name);
    psSectorInfo *GetSectorInfoByID(unsigned int id);
    csHash<psSectorInfo *>::GlobalIterator GetSectorIterator();
    //@}

    psCommandManager* GetCommandManager() { return commandManager; }

    /** @name Traits
     */
    //@{
    typedef csPDelArray<psTrait>::Iterator TraitIterator;
    psTrait *GetTraitByID(unsigned int id);
    psTrait *GetTraitByName(const char *name);
    TraitIterator GetTraitIterator();
    //@}

    /** @name Races
     */
    //@{
    size_t         GetRaceInfoCount();
    psRaceInfo *GetRaceInfoByIndex(int idx);
    psRaceInfo *GetRaceInfoByID(unsigned int id);
    psRaceInfo *GetRaceInfoByNameGender(const char *name,PSCHARACTER_GENDER gender);
    psRaceInfo *GetRaceInfoByNameGender(unsigned int id, PSCHARACTER_GENDER gender);
    psRaceInfo *GetRaceInfoByMeshName(const csString & meshname);
    //@}

    /** @name Skills
     */
    //@{
    psSkillInfo *GetSkillByID(unsigned int id);
    size_t GetSkillAmount();
    psSkillInfo *GetSkillByName(const char *name);

    ///Get all skills belonging to a specific category
    void GetSkillsListbyCategory(csArray <psSkillInfo*>& listskill,int category );
    //@}

    /** @name Common Strings
    */
    //@{
    const char *FindCommonString(unsigned int id);
    unsigned int FindCommonStringID(const char *name);
    inline void AddCommonStringID(const char* name)
    {
        FindCommonStringID(name);
    }

    /** Returns the message strings hash table.
     *
     * @return Returns a reference to the message strings hash table.
     */
    csStringSet *GetMsgStrings() { return &msg_strings; }

    /** Returns compressed message strings data.
     *
     * @data Pointer to the compressed data.
     * @size Size of the compressed data in bytes.
     * @num_strings The number of strings encoded.
     * @digest The md5sum digest of the compressed data.
     */
    void GetCompressedMessageStrings(char*& data, unsigned long& size,
        uint32_t& num_strings, csMD5::Digest& digest);
    //@}

    /** @name Guilds
    */
    //@{
    csWeakRef<psGuildInfo> FindGuild(unsigned int id);
    csWeakRef<psGuildInfo> FindGuild(const csString & name);
    bool CreateGuild(const char *guildname, Client *client);
    void RemoveGuild(psGuildInfo *which);
    //@}

    /** @name Guild alliances
    */
    //@{
    psGuildAlliance * FindAlliance(unsigned int id);
    bool CreateAlliance(const csString & name, psGuildInfo * founder, Client *client);
    bool RemoveAlliance(psGuildAlliance *which);
    //@}

    /** @name Quests
    */
    //@{
    psQuest *GetQuestByID(unsigned int id);
    psQuest *GetQuestByName(const char *name);
    psQuest *AddDynamicQuest(const char *name, psQuest *parentQuest, int step);

    /// Unloads a quest and its scripts
    bool UnloadQuest(int id);

    /// Loads a quest and its scripts
    bool LoadQuest(int id);
    csHash<psQuest *>::GlobalIterator GetQuestIterator();
    //@}

    /** @name Accounts
    */
    //@{
    /** Retrieves account information given an accountid.
     *
     *  This is not the usual entry method.  The username will be used more often.
     *
     *  @param accountid - The unique id associated with the account.
     *  @return NULL if no account information was found for the given ID. The returned pointer must be deleted when no longer needed.
     */
    psAccountInfo *GetAccountInfoByID(AccountID accountid);

    /** Retrieves account information given an character ID.
     *
     *  @param charid - The unique id associated with the character.
     *  @return NULL if no account information was found for the given ID. The returned pointer must be deleted when no longer needed.
     */
    psAccountInfo *GetAccountInfoByCharID(PID charid);

    /** Retrieves account information given a username.
     *
     *  This is the usual method for looking up account information.
     *
     *  @param username - The unique username associated with the account.
     *  @return NULL if no account information was found for the given ID.  The returned pointer must be deleted when no longer needed.
     */
    psAccountInfo *GetAccountInfoByUsername(const char *username);

    /** Call to store modified account information back to the database. Updates IP, security level, last login time, os, graphics card and graphics driver version.
     *
     * @param ainfo - A pointer to account data to store.
     * @return true - success  false - failed
     */
    bool UpdateAccountInfo(psAccountInfo *ainfo);

    /** Call to create a new account in the database.
     *
     *  @param ainfo - Should be initialized with all needed account info.
     *  @return The accountid assigned to this account (also stored in the appropriate field in ainfo)
     */
    unsigned int NewAccountInfo(psAccountInfo *ainfo);
    //@}

    /// Convenience function to preload all of the above in an appropriate order
    bool PreloadAll(EntityManager* entitymanager);
    void UnloadAll();

    // Item categories
    psItemCategory *GetItemCategoryByID(unsigned int id);
    psItemCategory *GetItemCategoryByName(const csString & name);

    /** Gets an item category by its position in the array. Useful to iterate it.
     *  @param pos The position in the array from where to extract the category.
     *  @return A pointer to the category in the position.
     */
    psItemCategory *GetItemCategoryByPos(size_t pos) { return itemCategoryList.Get(pos); }
    /** Gets the size of the item category array. Useful to iterate it.
     *  @return The size of the array.
     */
    size_t GetItemCategoryAmount() { return itemCategoryList.GetSize(); }

    // Item Animations
    csPDelArray<psItemAnimation> *FindAnimationList(int id);

    // Ways
    psWay *GetWayByID(unsigned int id);
    psWay *GetWayByName(const csString & name);

    // Factions
    Faction * GetFaction(const char *name);
    Faction * GetFaction(int id);
    Faction * GetFactionByName(const char *name);
    csHash<Faction*, int> &GetFactionHash() { return factions_by_id; }

    // Progression Scripts
    ProgressionScript *GetProgressionScript(const char *name);

    // Spells
    typedef csPDelArray<psSpell>::Iterator SpellIterator;
    psSpell *GetSpellByID(unsigned int id);
    psSpell *GetSpellByName(const csString & name);
    SpellIterator GetSpellIterator();

    /** @name Trades
    */
    //@{
    /// Get set of transformations for that pattern
    csPDelArray<CombinationConstruction>* FindCombinationsList(uint32 patternid);

    /// Get transformation array for pattern and target item
    csPDelArray<psTradeTransformations>* FindTransformationsList(uint32 patternid, uint32 targetid);
    bool PreloadUniqueTradeTransformations();
    csArray<uint32>* GetTradeTransUniqueByID(uint32 id);
    bool PreloadTradeProcesses();
    csArray<psTradeProcesses*>* GetTradeProcessesByID(uint32 id);
    bool PreloadTradePatterns();
    csArray<psTradePatterns*> GetTradePatternByItemID(uint32 id);
    psTradePatterns* GetTradePatternByName(csString name);
    csString CreateTransCraftDescription(psTradeTransformations* tran, psTradeProcesses* proc);
    csString CreateComboCraftDescription(CombinationConstruction* combArray);
    csString CreateComboCraftDescription(Result *combArray);
    csArray<CraftTransInfo*>* GetTradeTransInfoByItemID(uint32 id);
    csArray<CraftComboInfo*>* GetTradeComboInfoByItemID(uint32 id);
    //@}

    /** @name Items
    */
    //@{
    /// Get item basic stats by hashed table
    psItemStats *GetBasicItemStatsByName(csString name);

    /// Get item basic stats by hashed table
    psItemStats *GetBasicItemStatsByID(uint32 id);
    psItemStats *CopyItemStats(uint32 id, csString newName);

    /// return id of item if 'name' exists already
    uint32 BasicItemStatsByNameExist(csString name);
    size_t ItemStatsSize(void) { return itemStats_IDHash.GetSize(); }

    /// If an item changes name (eg book title) keep cache up to date
    void CacheNameChange(csString oldName, csString newName);

    bool LoadWorldItems(psSectorInfo *sector, csArray<psItem*> & items);

    float GetArmorVSWeaponResistance(const char* armor_type, const char* weapon_type);
    float GetArmorVSWeaponResistance(psItemStats* armor, psItemStats* weapon);

    void RemoveInstance( psItem * & item );
    void RemoveItemStats (psItemStats *&itemStats);

    PSITEMSTATS_STAT ConvertAttributeString(const char *attributestring);

    /// Converts the stat enum to a string.
    const char* Attribute2String( PSITEMSTATS_STAT s );
    //@}

    PSSKILL               ConvertSkillString(const char *skillstring);
    PSSKILL               ConvertSkill(int skill_id);
    PSCHARACTER_GENDER    ConvertGenderString(const char *genderstring);
    PSITEMSTATS_SLOTLIST  slotMap[PSCHARACTER_SLOT_BULK1];

    //Tips
    void GetTipByID(int id, csString& tip);
    unsigned int GetTipLength(); ///< Returns how many tips there is

    SlotNameHash slotNameHash;

    // Bad names
    size_t GetBadNamesCount();
    const char* GetBadName(int pos);
    void AddBadName(const char*);
    void DelBadName(const char*);

    /// Translation table for Flag strings (from db) into bit codes
    csArray<psItemStatFlags> ItemStatFlagArray;

    const char *MakeCacheName(const char *prefix, uint32 id);
    void AddToCache(iCachedObject *obj, const char *name, int max_cache_time_seconds);
    iCachedObject *RemoveFromCache(const char *name);

    const csPDelArray<psCharMode>& GetCharModes() const { return char_modes; }
    const csPDelArray<psMovement>& GetMovements() const { return movements; }
    const psCharMode* GetCharMode(size_t id) const { return char_modes[id]; }
    const psMovement* GetMovement(size_t id) const { return movements[id]; }
    uint8_t GetCharModeID(const char* name);
    uint8_t GetMovementID(const char* name);

    /// Used to track effect IDs
    uint32_t NextEffectUID() { return ++effectID; }

    /// This allows psServerChar to build a list of limitations for each player on demand.
    const psCharacterLimitation *GetLimitation(size_t index);

    /// List of stances.
    csArray<Stance> stances;

    /// Map of locations.
    csArray<csString> stanceID;

    void AddItemStatsToHashTable(psItemStats* newitem);

    /** Wrapper for the getOption method of the root optionEntry stored in cachemanager.
     *  @param A path to the requested option starting from the root optionEntry
     *  @return An optionEntry in the requested position. NULL if the path was not found.
     *  @see optionEntry
     */
    optionEntry* getOption(const csString path) { return rootOptionEntry.getOption(path); }

    /** Wrapper for the getOptionSafe method of the root optionEntry stored in cachemanager.
     *  @param A path to the requested option starting from the root optionEntry
     *  @return An optionEntry in the requested position. NULL if the path was invalid.
     *  @see optionEntry
     */
    optionEntry* getOptionSafe(const csString path, const csString value) { return rootOptionEntry.getOptionSafe(path,value); }

    /** Returns a pointer to the loot randomizer which is hosted within the cache manager.
     *  @return A pointer to the lootrandomizer.
     */
    LootRandomizer* getLootRandomizer() { return lootRandomizer; }

    /** Applies on a randomized overlay structure, starting with the base item stats passed, the modifier ids
     *  passed. This is an accessor to the lootrandomizer own function
     *  @param baseItem The basic item stats of the item we are creating an overlay for.
     *  @param overlay The overlay which we will fill with the modifiers data.
     *  @param modifierIds An array of ids referencing loot_modifiers rules.
     */
    void ApplyItemModifiers(psItemStats* baseItem, RandomizedOverlay* overlay, csArray<uint32_t>& modifierIds)
         { lootRandomizer->ApplyModifier(baseItem, overlay, modifierIds); }

    /** Randomizes the passed item.
     *  @param item The item which we are going to randomize.
     *  @param maxCost The maximum cost allowed to the randomized item
     *  @param numModifiers The maximum amount of modifiers to apply.
     */
    void RandomizeItem(psItem* item, float maxCost, size_t numModifiers)
         { lootRandomizer->RandomizeItem( item, maxCost, false, numModifiers); }

    /** Reloads the server_options.
     *  @return FALSE if the reload failed
     */
    bool ReloadOptions() { return PreloadOptions(); }

protected:
    uint32_t effectID;
    char CacheNameBuffer[15];

    bool PreloadSectors();
    bool PreloadRaceInfo();
    bool PreloadSkills();
    /** Preloads the server_options and stores them in an easily accessible structure.
     *  @return FALSE if the load failed
     */
    bool PreloadOptions();
    bool PreloadLimitations();
    bool PreloadTraits();
    bool PreloadItemCategories();
    bool PreloadWays();
    ///preloads the character events script for a faction
    void PreloadFactionCharacterEvents(const char* script, Faction* faction);
    bool PreloadFactions();
    bool PreloadScripts(EntityManager* entitymanager);
    bool PreloadSpells();
    bool PreloadItemStatsDatabase();
    bool PreloadItemAnimList();
    bool PreloadQuests();
    bool PreloadTradeCombinations();
    bool PreloadTradeTransformations();
    bool PreloadTips();
    bool PreloadBadNames();
    bool PreloadArmorVsWeapon();
    bool PreloadMovement();
    bool PreloadStances();
    void PreloadCommandGroups();


    /**Loads the loot modifiers from the loot modifiers table
     * @return TRUE if there were no issues during loading, else FALSE.
     */
    bool PreloadLootModifiers();

    /// Cache in the crafting messages.
    bool PreloadCraftMessages();



	/////
	//
	// Craft Book revision - JAL 20121105-17
	bool DescribeTransformation( csPDelArray<psTradeTransformations>&, csArray<CraftTransInfo*> *newArray );
	int Contains(  csArray<uint32> *list, uint32 id );
	int ResultsIn(   Result*, int id );

	bool loadTradeTransformationsByPatternAndGroup( Result *result,
		csHash<csHash<csPDelArray<psTradeTransformations> *,uint32> *,uint32>* txResultHash,
		csHash<csHash<csPDelArray<psTradeTransformations> *,uint32> *,uint32>* txItemHash );
	bool freeTradeTransformationsByPatternAndGroup( csHash<csHash<csPDelArray<psTradeTransformations> *,uint32> *,uint32>* txItemHash, csHash<csHash<csPDelArray<psTradeTransformations> *,uint32> *,uint32>* txResultHash );
	bool FindFinalItem( csHash<csHash<csPDelArray<psTradeTransformations>*, uint32> *,uint32>* txItemHash, csArray<uint32> *finalItems, uint32 itemID, csArray<uint32> *itemStack );
	bool UniqueInsertIntoItemArray( csArray<uint32> *finalItems, uint32 itemID );
	bool ListProductionSteps( csArray<CraftTransInfo*> *newArray, csHash<csHash<csPDelArray<psTradeTransformations>*, uint32> *,uint32>* txResultHash, csArray<uint32> *finalItems, uint32 itemID, uint32 patternID, uint32 groupID, csArray<uint32> *itemStack   );



	//
	/////



    /** Caches in the crafting transforms.
      * @param tradePattern  The current pattern message we want to construct.
      * @param patternID    The pattern ID of the pattern we are cacheing.
      */
    void CacheCraftTransforms( psMsgCraftingInfo* tradePattern, int patternID, int group );

    /** Caches in the crafting combinations..
      * @param tradePattern  The current pattern message we want to construct.
      * @param patternID    The pattern ID of the pattern we are cacheing.
      */
    void CacheCraftCombos( psMsgCraftingInfo* tradePattern, int patternID, int group );

    PSTRAIT_LOCATION ConvertTraitLocationString(const char *locationstring);

    class psCacheExpireEvent; // forward decl for next decl

    struct CachedObject
    {
        csString name;
        iCachedObject *object;
        psCacheExpireEvent *event;
    };

    /**
    * The generic cache sets timers for each object added to the cache,
    * then if the object is not removed by the expiration time, it
    * calls the iCachedObject interface and deletes the object.
    */
    class psCacheExpireEvent : public psGameEvent
    {
    protected:
        bool valid;
        CachedObject *myObject;

    public:
        psCacheExpireEvent(int delayticks,CachedObject *object);
        void CancelEvent() { valid = false;}
        virtual void Trigger();  ///< Abstract event processing function
    };

    /** This cache is intended to keep database-loaded objects
     *  in memory for a time after we are done with them to
     *  avoid reloading in case they are reused soon.
     */
    csHash<CachedObject *, csString> generic_object_cache;

    // Common strings data.
    csStringSet msg_strings;
    char* compressed_msg_strings;
    unsigned long compressed_msg_strings_size;
    uint32_t num_compressed_strings;
    csMD5::Digest compressed_msg_strings_digest;

    csHash<psSectorInfo *> sectorinfo_by_id;   ///< Sector info list hashed by sector id
    csHash<psSectorInfo *> sectorinfo_by_name; ///< Sector info list hashed by sector name
    csPDelArray<psTrait > traitlist;
    csPDelArray<psRaceInfo > raceinfolist;

    csHash<psSkillInfo*, int> skillinfo_IDHash;
    csHash<psSkillInfo *, csString> skillinfo_NameHash;
    csHash<psSkillInfo *, int> skillinfo_CategoryHash;

    csPDelArray<psItemCategory > itemCategoryList;
    csPDelArray<psWay > wayList;
    csHash<Faction*, int> factions_by_id;
    csHash<Faction*, csString> factions;
    csHash<ProgressionScript*,csString> scripts;
    csPDelArray<psSpell > spellList;
    //csArray<psItemStats *> basicitemstatslist;
    csHash<psItemStats *,uint32> itemStats_IDHash;
    csHash<psItemStats *,csString> itemStats_NameHash;
    csPDelArray<csPDelArray<psItemAnimation > > item_anim_list;
    csHash<csRef<psGuildInfo> > guildinfo_by_id;
    csHash<psGuildAlliance*> alliance_by_id;
    csHash<psQuest *> quests_by_id;
    csHash<psTradePatterns *,uint32> tradePatterns_IDHash;
    csHash<psTradePatterns *,csString> tradePatterns_NameHash;
    csHash<csArray<psTradeProcesses*> *,uint32> tradeProcesses_IDHash;
    csHash<csPDelArray<CombinationConstruction> *,uint32> tradeCombinations_IDHash;
    csHash<csHash<csPDelArray<psTradeTransformations> *,uint32> *,uint32> tradeTransformations_IDHash;
    csHash<csArray<uint32> *,uint32> tradeTransUnique_IDHash;
    csHash<csArray<CraftTransInfo*> *,uint32> tradeCraftTransInfo_IDHash;
    csHash<csArray<CraftComboInfo*> *,uint32> tradeCraftComboInfo_IDHash;
    csArray<csString> tips_list;                                            ///< List for the tips
    csStringArray bad_names;
    csPDelArray<ArmorVsWeapon> armor_vs_weapon;
    csPDelArray<psCharMode> char_modes;
    csPDelArray<psMovement> movements;
    csPDelArray<psCharacterLimitation> limits;  ///< All the limitations based on scores for characters.
    psCommandManager* commandManager;
    optionEntry rootOptionEntry;

    LootRandomizer *lootRandomizer; ///< A pointer to the lootrandomizer mantained by the cachemanager.

};


#endif

