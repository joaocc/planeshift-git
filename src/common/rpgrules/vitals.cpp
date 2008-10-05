/*
 * vitals.cpp
 *
 * Copyright (C) 2005 Atomic Blue (info@planeshift.it, http://www.atomicblue.org)
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
#include "vitals.h"


psVitalManager::psVitalManager()
{
    for ( int x = 0; x < VITAL_COUNT; x++ )
    {
        orig_vitals[x].value = 0.0f;
        orig_vitals[x].max = 0.0f;
        orig_vitals[x].maxModifier = 0.0f;
    }

    experiencePoints = 0;
    progressionPoints = 0;
    orig_vitals[VITAL_HITPOINTS].drRate  = HP_REGEN_RATE;
    orig_vitals[VITAL_MANA].drRate       = MANA_REGEN_RATE;

    orig_vitals[VITAL_PYSSTAMINA].drRate    = 0;
    orig_vitals[VITAL_MENSTAMINA].drRate    = 0;

    experiencePoints = 0;
    progressionPoints = 0;
    lastDRUpdate = 0;

    ResetVitals();
}


psVitalManager::~psVitalManager()
{

}

void psVitalManager::ResetVitals()
{
    for (int i=0; i<VITAL_COUNT; i++)
        vitals[i] = orig_vitals[i];
}

void psVitalManager::SetOrigVitals()
{
    for (int i=0; i<VITAL_COUNT; i++)
        orig_vitals[i] = vitals[i];
}

psCharVital& psVitalManager::GetVital( int vital )
{
    if ( vital >=0 && vital < VITAL_COUNT )
    {
        return vitals[vital];
    }
    else
    {
        static psCharVital dummy;
        assert(false);
        return dummy;
    }
}

float psVitalManager::GetValue( int vital )
{
    if ( vital >=0 && vital < VITAL_COUNT )
    {
        return vitals[vital].value;
    }
    else
    {
        return 0.0;
    }
}
float psVitalManager::GetStamina(bool pys)
{
    if(pys)
        return vitals[VITAL_PYSSTAMINA].value;
    else
        return vitals[VITAL_MENSTAMINA].value;
}

