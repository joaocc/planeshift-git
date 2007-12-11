/*
 * Author: Andrew Robberts
 *
 * Copyright (C) 2007 Atomic Blue (info@planeshift.it, http://www.atomicblue.org)
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

#include <csutil/cfgfile.h>

#include "psoptions.h"

const char * psOptions::BuildKey(char * result, const char * className, const char * optionName) const
{
    strcpy(result, className);
    strcat(result, ".");
    strcat(result, optionName);
    return result;
}

void psOptions::EnsureSubscription(const char * name)
{
    if (subscriptions.Contains(name))
        return;

    subscriptions.Put(name, name);
    PawsManager::GetSingleton().Subscribe(name, this);
}

psOptions::psOptions(const char * filename, csRef<iVFS> vfs)
         : filename(filename)
{
    configFile.AttachNew(new csConfigFile(filename, vfs));
    PawsManager::GetSingleton().AddExtraScriptVar(this, "Options");
}

psOptions::~psOptions()
{
    configFile->Save();
}

void psOptions::OnUpdateData(const char * name, PAWSData & data)
{
    switch (data.type)
    {
    case PAWS_DATA_STR:
        configFile->SetStr(name, data.GetStr());
        break;

    case PAWS_DATA_BOOL:
        configFile->SetBool(name, data.GetBool());
        break;

    case PAWS_DATA_INT:
        configFile->SetInt(name, data.GetInt());
        break;
    
    case PAWS_DATA_FLOAT:
        configFile->SetFloat(name, data.GetFloat());
        break;
	
    default:
        break;
    }
}

void psOptions::NewSubscription(const char *name)
{
}

double psOptions::GetProperty(const char *ptr)
{
    if (configFile->KeyExists(ptr))
        return (double)configFile->GetFloat(ptr);

    Error2("psOptions::GetProperty(%s) failed\n", ptr);
    return 0.0;
}

double psOptions::CalcFunction(const char * functionName, const double * params)
{
    if (!strcasecmp(functionName, "Save"))
    {
        Save();
        return 0.0;
    }
    else if (!strcasecmp(functionName, "RefreshOptions"))
    {
        csHash<iOptionsClass *>::GlobalIterator iter = optionsClasses.GetIterator();
        while (iter.HasNext())
            iter.Next()->SaveOptions();
    }

    Error2("psOptions::CalcFunction(%s) failed\n", functionName);
    return 0.0;
}

void psOptions::RegisterOptionsClass(const char * className, iOptionsClass * optionsClass)
{
    unsigned int key = csHashCompute(className);
    optionsClasses.Put(key, optionsClass);
}

void psOptions::SetOption(const char * className, const char * name, const char * value)
{
    char key[64];
    configFile->SetStr(BuildKey(key, className, name), value);
    PawsManager::GetSingleton().Publish(key, value);
}

void psOptions::SetOption(const char * className, const char * name, float value)
{
    char key[64];
    configFile->SetFloat(BuildKey(key, className, name), value);
    PawsManager::GetSingleton().Publish(key, value);
}

void psOptions::SetOption(const char * className, const char * name, int value)
{
    char key[64];
    configFile->SetInt(BuildKey(key, className, name), value);
    PawsManager::GetSingleton().Publish(key, value);
}

void psOptions::SetOption(const char * className, const char * name, bool value)
{
    char key[64];
    configFile->SetBool(BuildKey(key, className, name), value);
    PawsManager::GetSingleton().Publish(key, value);
}

const char * psOptions::GetOption(const char * className, const char * name, const char * defaultValue)
{
    char key[64];
    BuildKey(key, className, name);
    EnsureSubscription(key);

    if (configFile->KeyExists(key))
        return configFile->GetStr(key);

    SetOption(className, name, defaultValue);
    return defaultValue;
}

float psOptions::GetOption(const char * className, const char * name, float defaultValue)
{
    char key[64];
    BuildKey(key, className, name);
    EnsureSubscription(key);

    if (configFile->KeyExists(key))
        return configFile->GetFloat(key);

    SetOption(className, name, defaultValue);
    return defaultValue;
}

int psOptions::GetOption(const char * className, const char * name, int defaultValue)
{
    char key[64];
    BuildKey(key, className, name);
    EnsureSubscription(key);

    if (configFile->KeyExists(key))
        return configFile->GetInt(key);

    SetOption(className, name, defaultValue);
    return defaultValue;
}

bool psOptions::GetOption(const char * className, const char * name, bool defaultValue)
{
    char key[64];
    BuildKey(key, className, name);
    EnsureSubscription(key);

    if (configFile->KeyExists(key))
        return configFile->GetBool(key);

    SetOption(className, name, defaultValue);
    return defaultValue;
}

void psOptions::Save()
{
    csHash<iOptionsClass *>::GlobalIterator iter = optionsClasses.GetIterator();
    while (iter.HasNext())
        iter.Next()->LoadOptions();
    configFile->Save();
}

void psOptions::Load()
{
    configFile->Load(filename);
}
