/*
 * pstrade.cpp
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
#include "util/log.h"
#include "util/strutil.h"

#include "../globals.h"
#include "../psserver.h"
#include "../cachemanager.h"

#include "pstrade.h"
#include "psguildinfo.h"


///////////////////////////////////////////////////////////////////////
// Combinations
psTradeCombinations::psTradeCombinations()
{
    id          = 0;
    patternId   = 0;
    resultId    = 0;
    resultQty   = 0;
    itemId      = 0;
    minQty      = 0;
    maxQty      = 0;
}

psTradeCombinations::~psTradeCombinations()
{
}

bool psTradeCombinations::Load(iResultRow& row)
{
    id          = row.GetUInt32("id");
    patternId   = row.GetUInt32("pattern_id");
    resultId    = row.GetUInt32("result_id");
    resultQty   = row.GetInt("result_qty");
    itemId      = row.GetUInt32("item_id");
    minQty      = row.GetInt("min_qty");
    maxQty      = row.GetInt("max_qty");

    return true;
}

///////////////////////////////////////////////////////////////////////
// Transformations
psTradeTransformations::psTradeTransformations()
{
    id              = 0;
    patternId       = 0;
    processId       = 0;
    resultId        = 0;
    resultQty       = 0;
    itemId          = 0;
    itemQty         = 0;
    peniltyPct      = 0.0;
    transPoints     = 0;
    transCached     = true;    
}

// Non-cache constructor
psTradeTransformations::psTradeTransformations(uint32 rId, int rQty, uint32 iId, int iQty, int tPoints)
{
    id              = 0;
    patternId       = 0;
    processId       = 0;
    resultId        = rId;
    resultQty       = rQty;
    itemId          = iId;
    itemQty         = iQty;
    peniltyPct      = 0.0;
    transPoints     = tPoints;
    transCached     = false;    
}

psTradeTransformations::~psTradeTransformations()
{
}

bool psTradeTransformations::Load(iResultRow& row)
{
    id              = row.GetUInt32("id");
    patternId       = row.GetUInt32("pattern_id");
    processId       = row.GetUInt32("process_id");
    resultId        = row.GetUInt32("result_id");
    resultQty       = row.GetInt("result_qty");
    itemId          = row.GetUInt32("item_id");
    itemQty         = row.GetInt("item_qty");
    transPoints     = row.GetInt("trans_points");
    peniltyPct     = row.GetFloat("penilty_pct");
    return true;
}

///////////////////////////////////////////////////////////////////////
// Processes
psTradeProcesses::psTradeProcesses()
{
    processId       = 0;
    subprocess      = 0;
    name            = "";
    animation       = "";
    workItemId      = 0;
    equipmentId     = 0;    
    constraints     = "";
    garbageId       = 0;
    garbageQty       = 0;
    priSkillId      = 0;
    minPriSkill     = 0;
    maxPriSkill     = 0;
    priPracticePts  = 0;
    priQualFactor   = 0;
    secSkillId      = 0;
    minSecSkill     = 0;
    maxSecSkill     = 0;
    secPracticePts  = 0;
    secQualFactor   = 0;
    renderEffect = "";
}


psTradeProcesses::~psTradeProcesses()
{
}

bool psTradeProcesses::Load(iResultRow& row)
{
    processId       = row.GetUInt32("process_id");
    subprocess      = row.GetInt("subprocess_number");
    name            = row["name"];
    animation       = row["animation"];
    workItemId      = row.GetUInt32("workitem_id");
    equipmentId     = row.GetUInt32("equipment_id");
    constraints     = row["constraints"];
    garbageId       = row.GetUInt32("garbage_id");
    garbageQty      = row.GetInt("garbage_qty");
    priSkillId      = row.GetInt("primary_skill_id");
    minPriSkill     = row.GetInt("primary_min_skill");
    maxPriSkill     = row.GetInt("primary_max_skill");
    priPracticePts  = row.GetInt("primary_practice_points");
    priQualFactor   = row.GetInt("primary_quality_factor");
    secSkillId      = row.GetInt("secondary_skill_id");
    minSecSkill     = row.GetInt("secondary_min_skill");
    maxSecSkill     = row.GetInt("secondary_max_skill");
    secPracticePts  = row.GetInt("secondary_practice_points");
    secQualFactor   = row.GetInt("secondary_quality_factor");
    renderEffect    = row["render_effect"];
    return true;
}

///////////////////////////////////////////////////////////////////////
// Patterns
psTradePatterns::psTradePatterns()
{
    id              = 0;
    patternName     = "";
    groupPatternId   = 0;
    designItemId    = 0;
    KFactor         = 0.0;
}

psTradePatterns::~psTradePatterns()
{
}

bool psTradePatterns::Load(iResultRow& row)
{
    id              = row.GetUInt32("id");
    patternName     = row["pattern_name"];
    groupPatternId   = row.GetUInt32("group_id");
    designItemId    = row.GetUInt32("designitem_id");
    KFactor         = row.GetFloat("k_factor");

    return true;
}

