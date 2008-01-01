/*
 * modehandler.cpp    Keith Fulton <keith@paqrat.com>
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
//=============================================================================
// Crystal Space Includes
//=============================================================================
#include <csutil/objreg.h>
#include <csutil/sysfunc.h>
#include <csutil/cscolor.h>
#include <csutil/flags.h>
#include <csutil/randomgen.h>
#include <csutil/scf.h>
#include <iengine/engine.h>
#include <iengine/sector.h>
#include <iengine/light.h>
#include <iengine/material.h>
#include <iengine/mesh.h>
#include <iengine/movable.h>
#include <iengine/sector.h>
#include <iengine/sharevar.h>
#include <iengine/portal.h>
#include <iengine/portalcontainer.h>
#include <imesh/object.h>
#include <imesh/partsys.h>
#include <imesh/spritecal3d.h>
#include <iutil/vfs.h>
#include <imap/ldrctxt.h>
#include <iutil/object.h>
#include <ivaria/engseq.h>

#include <physicallayer/pl.h>
#include <physicallayer/entity.h>
#include <propclass/mesh.h>
#include <physicallayer/propclas.h>

//=============================================================================
// Project Includes
//=============================================================================
#include "net/messages.h"
#include "net/msghandler.h"

#include "iclient/isoundmngr.h"

#include "effects/pseffectmanager.h"

#include "engine/netpersist.h"

#include "util/psxmlparser.h"
#include "util/psscf.h"
#include "util/psconst.h"
#include "util/log.h"
#include "util/slots.h"

#include "paws/pawsmanager.h"

#include "gui/pawsinfowindow.h"
#include "gui/chatwindow.h"

//=============================================================================
// Local Includes
//=============================================================================
#include "modehandler.h"
#include "entitylabels.h"
#include "pscelclient.h"
#include "pscharcontrol.h"
#include "psclientdr.h"
#include "weather.h"
#include "globals.h"


/// Callback class for the portal traversing
psPortalCallback::psPortalCallback()
    : scfImplementationType(this)
{
}

psPortalCallback::~psPortalCallback()
{
}

bool psPortalCallback::Traverse(iPortal* portal,iBase* base)
{
    // Refresh the weather
    psengine->GetModeHandler()->CreatePortalWeather(portal, 0);

    // Remove us from the list, we have created it now
    portal->RemovePortalCallback(this);

    return true;
}

ModeHandler::ModeHandler(iSoundManager *sm,
                         psCelClient *cc,
                         MsgHandler* mh,
                         iObjectRegistry* obj_reg)
{
    msghandler   = mh;
    soundmanager = sm;
    celclient    = cc;
    entity       = NULL;
    object_reg   = obj_reg;
    engine =  csQueryRegistry<iEngine> (object_reg);

    downfall     = NULL;
    fog          = NULL;

    interpolation_time = 40*1000;
    last_interpolation_ticks = 0;
    last_lightset = 0;
    randomgen     = new csRandomGen();
    interpolation_complete = true;  // don't attempt to update lights until you get a net msg to do so
    
    timeOfDay = TIME_MORNING;
    chatWindow = NULL;
    sound_queued = false;

    last_weather_update = csGetTicks();
    weather_update_time = 100;
}

ModeHandler::~ModeHandler()
{

    RemovePortalWeather();
    RemoveWeather();

    if (msghandler)
    {
        msghandler->Unsubscribe(this,MSGTYPE_MODE);
        msghandler->Unsubscribe(this,MSGTYPE_WEATHER);
        msghandler->Unsubscribe(this,MSGTYPE_NEWSECTOR);
        msghandler->Unsubscribe(this,MSGTYPE_COMBATEVENT);
    }
    if (randomgen)
        delete randomgen;
}

bool ModeHandler::Initialize()
{
    Notify1(LOG_STARTUP,"ModeHandler is initializing..");

    // Net messages
    msghandler->Subscribe(this,MSGTYPE_MODE);
    msghandler->Subscribe(this,MSGTYPE_WEATHER);
    msghandler->Subscribe(this,MSGTYPE_NEWSECTOR);
    msghandler->Subscribe(this,MSGTYPE_COMBATEVENT);

    // Light levels
    if(!LoadLightingLevels())
        return false;

    // Success!
    return true;
}

bool ModeHandler::LoadLightingLevels()
{
    csRef<iVFS> vfs;
    vfs =  csQueryRegistry<iVFS> (object_reg);
    if (!vfs)
        return false;

    csRef<iDocumentSystem> xml = psengine->GetXMLParser ();

    csRef<iDataBuffer> buf (vfs->ReadFile("/planeshift/art/world/lighting.xml"));
    
    if (!buf || !buf->GetSize()) {
        printf("Cannot load /planeshift/art/world/lighting.xml");
        return false;
    }

    csRef<iDocument> doc = xml->CreateDocument();

    const char* error = doc->Parse( buf );
    if ( error )
    {
        printf("Error loading lighting levels, art/world/lighting.xml: %s\n", error);
        return false;
    }

    csRef<iDocumentNodeIterator> iter = doc->GetRoot()->GetNode("lighting")->GetNodes("color");
    
    while (iter->HasNext())
    {
        csRef<iDocumentNode> node = iter->Next();
        LightingSetting *newlight = new LightingSetting;
        double r,g,b,r2,g2,b2;

        newlight->error = false;
        newlight->value = node->GetAttributeValueAsInt("value");
        newlight->object = node->GetAttributeValue("object");
        newlight->type = node->GetAttributeValue("type");
        newlight->density = node->GetAttributeValueAsFloat("density");  // only for fog
        r = node->GetAttributeValueAsFloat("r");
        g = node->GetAttributeValueAsFloat("g");
        b = node->GetAttributeValueAsFloat("b");
        r2 = node->GetAttributeValueAsFloat("weather_r");
        g2 = node->GetAttributeValueAsFloat("weather_g");
        b2 = node->GetAttributeValueAsFloat("weather_b");
        
        newlight->color.Set(r,g,b);
        if (r2 || g2 || b2)
            newlight->raincolor.Set(r2,g2,b2);
        else
            newlight->raincolor.Set(r,g,b);
        
        // Now add to lighting setup        
        if (newlight->value >= lights.GetSize() )
        {
            if ( lights.GetSize() == 0 )
            {
                LightingList *newset = new LightingList;
                lights.Push(newset);            
            }
            else
            {
                // Add as many empty lists as we need.
                for (int i=(int)lights.GetSize()-1; i<(int)newlight->value; i++)
                {
                    LightingList *newset = new LightingList;
                    lights.Push(newset);
                }
            }                
        }

        LightingList *set = lights[newlight->value];
        set->colors.Push(newlight);

        if (newlight->type == "ambient")
            newlight->sector = newlight->object;
        else if (newlight->type == "light")
        {
            iLight* light = psengine->GetEngine()->FindLight(newlight->object);
            if (light)
                newlight->sector = light->GetSector()->QueryObject()->GetName();
        }
    }    
    return true;
}

void ModeHandler::SetEntity(iCelEntity *ent)
{
    entity = ent;
    pcmesh = CEL_QUERY_PROPCLASS_ENT( entity, iPcMesh );
}

void ModeHandler::AddDownfallObject(WeatherObject* weatherobject)
{
    iSector* sector = psengine->GetCelClient()->GetMainPlayer()->GetSector();
    if(weatherobject->GetSector() == sector)
    {
        downfall = weatherobject; // Only need one object, safe to overwrite
    }
}

void ModeHandler::RemoveDownfallObject(WeatherObject* weatherobject)
{
    if (downfall == weatherobject)
    {
        downfall = NULL;
    }
}

LightingSetting *ModeHandler::FindLight(LightingSetting *light,int which)
{
    LightingList *set = lights[which];

    for (size_t j=0; j<set->colors.GetSize(); j++)
    {
        LightingSetting *found = set->colors[j];

        if (found->object == light->object &&
            found->type   == light->type)
            return found;
    }
    return NULL;
}


void ModeHandler::HandleMessage(MsgEntry* me)
{
    switch(me->GetType())
    {
        case MSGTYPE_MODE:
            HandleModeMessage(me);
            return;

        case MSGTYPE_WEATHER:
            HandleWeatherMessage(me);
            return;

        case MSGTYPE_NEWSECTOR:
            HandleNewSectorMessage(me);
            return;

        case MSGTYPE_COMBATEVENT:
            HandleCombatEvent(me);
            return;
    }
}

void ModeHandler::HandleModeMessage(MsgEntry* me)
{
    psModeMessage msg(me);
    
    GEMClientActor* actor = dynamic_cast<GEMClientActor*>(celclient->FindObject(msg.actorID));
    if (!actor)
    {
        Error2("Received psModeMessage for unknown object with ID %u", msg.actorID);
        return;
    }

    actor->SetMode(msg.mode);

    if (msg.actorID == celclient->GetMainActor()->GetID())
    {
        SetModeSounds(msg.mode);
        if (msg.mode == psModeMessage::OVERWEIGHT)
        {
            psSystemMessage sysMsg(0, MSG_ERROR, "You struggle under the weight of your inventory and must drop something." );
            msghandler->Publish( sysMsg.msg );
        }
    }
}

void ModeHandler::SetModeSounds(uint8_t mode)
{
    if ( !celclient->GetMainPlayer() || !psengine->GetSoundStatus() )
        return;

    switch (mode)
    {
        case psModeMessage::PEACE:
            soundmanager->StopOverrideBG();
            soundmanager->ChangeMusicMode(false);
            //const char *sectorname = celclient->GetMainPlayer()->GetSector()->QueryObject()->GetName();
            //SetSectorMusic(sectorname);
            soundmanager->FadeSectorSounds( FADE_UP );
            break;
        case psModeMessage::COMBAT:
            if (soundmanager->PlayingCombatMusic())
            {
                soundmanager->OverrideBGSong("combat",iSoundManager::LOOP_SOUND);
                soundmanager->FadeSectorSounds( FADE_DOWN );
                soundmanager->ChangeMusicMode(true);
            }
            break;
        case psModeMessage::DEAD:
            if (soundmanager->PlayingMusic())
                soundmanager->OverrideBGSong("death",iSoundManager::LOOP_SOUND);
            break;
    }
}

WeatherInfo* ModeHandler::GetWeatherInfo(const char* sector)
{
    WeatherInfo key;
    key.sector = sector;
    WeatherInfo *ri = weatherlist.Find(&key); // Get the weather info
    return ri;
}

WeatherInfo* ModeHandler::CreateWeatherInfo(const char* sector)
{
    WeatherInfo * wi = new WeatherInfo();

    wi->sector = sector;
    wi->downfall_condition = WEATHER_CLEAR;
    wi->downfall_params.value = 0;
    wi->downfall_params.fade_value = 0;
    wi->downfall_params.fade_time = 0;
    wi->fog_params = wi->downfall_params; // Fog and downfall use same type
    wi->fog_condition = WEATHER_CLEAR;
    wi->fog = NULL;
    wi->r = wi->g = wi->b = 0;

    weatherlist.Insert(wi,true); // weatherlist owns data

    return wi;
}


void ModeHandler::DoneLoading(const char* sectorname)
{
    iSector* newsector = psengine->GetEngine()->GetSectors()->FindByName(sectorname);
    // Give us the callback when the portals are working
    CreatePortalWeather(newsector, 0);
}

bool ModeHandler::CreatePortalWeather(iPortal* portal, csTicks delta)
{
    iSector* sect = portal->GetSector();

    if(!sect) // No sector yet assigned? Subscribe to visibility event
    {
        // Check if we already have this subscribed to our class
        bool subbed = false;
        for(int i = 0; i < portal->GetPortalCallbackCount();i++)
        {
            // Test if this is a psPortalCallback object
            psPortalCallback* psCall = dynamic_cast<psPortalCallback*>(portal->GetPortalCallback(i));
            if(psCall) // Found!
            {
                subbed = true;
                break;
            }
        }

        if(!subbed) // Not a psPortalCallback class, subscribe
        {       
            // Set the new callback
            psPortalCallback* psCall = new psPortalCallback();
            portal->SetPortalCallback(psCall);
            psCall->DecRef();
        }

        return true;

    }

    // Begin the work with the weather
    WeatherInfo* ri = GetWeatherInfo(sect->QueryObject()->GetName());
    WeatherPortal* weatherP = GetPortal(portal); // Get the portal


    // No weather to Update if no Weather Info
    if (!ri)
    {
        return true;
    }

    
    if(!weatherP)
    {
        // Create the WeatherPortal entity
        weatherP           = new WeatherPortal;
        weatherP->portal   = portal;
        weatherP->downfall = NULL;
        weatherP->wi       = ri;

        csVector3 min,max;
        csBox3 posCalc; 
        // Create the weathermesh 
        for (int vertidx = 0; vertidx < portal->GetVertexIndicesCount(); vertidx++)
        {
            int vertexidx = portal->GetVertexIndices()[vertidx];
            csVector3 vertex = portal->GetWorldVertices()[vertexidx];
            posCalc.AddBoundingVertex(vertex);
        }
       
        // Store the pos
        weatherP->pos = posCalc.GetCenter();

        // Store the box for later use
        posCalc.SetCenter(csVector3(0,0,0));
        posCalc.AddBoundingVertex(csVector3(1,1,1));
        posCalc.AddBoundingVertex(csVector3(-1,-1,-1));
        weatherP->bbox = posCalc;

        // Push it into the array before loading mesh and stuff
        portals.Push(weatherP);
    }
   
    // Check for removal of downfall
    if (weatherP->downfall && ri->downfall_condition == WEATHER_CLEAR)
    {
        weatherP->downfall->Destroy();
        delete weatherP->downfall;
        weatherP->downfall = NULL;
        Notify2( LOG_WEATHER, "Downfall removed from portal '%s'",
                 portal->GetName());
    }
    // Check if downfall has changed between rain/snow
    else if (weatherP->downfall && weatherP->downfall->GetType() != ri->downfall_condition)
    {
        weatherP->downfall->Destroy();
        delete weatherP->downfall;
        weatherP->downfall = CreateDownfallWeatherObject(ri);
    
        if(!weatherP->downfall->CreateMesh())
        {
            Error1("Failed to create downfall");
            delete weatherP->downfall;
            weatherP->downfall = NULL;
            return false;
        }
        
        // Calculate the drops
        csBox3 def = weatherP->downfall->CreateDefaultBBox();
        csBox3 box = weatherP->bbox;
        float mod = box.Volume() / def.Volume();
        int drops = (int)((float)ri->downfall_params.value * mod);
        
        if(drops < 1)
            drops = 1;
        
        // Setup the mesh
        weatherP->downfall->SetupMesh(weatherP->downfall->CreateDefaultBBox());

        //If the portal is a warp one, then distance is != 0
        csReversibleTransform transform = portal->GetWarp();
        csVector3 distance = transform.GetT2OTranslation();
        
        // move it to right position
        weatherP->downfall->MoveTo(ri,sect);
        weatherP->downfall->MoveTo(weatherP->pos + distance);

        Notify2( LOG_WEATHER, "Downfall changed type for portal '%s'",
                 portal->GetName());
    }
    // Check if downfall need update
    else if (weatherP->downfall && ri->downfall_condition != WEATHER_CLEAR)
    {
        weatherP->downfall->Update(delta);
    }
    // Check if we need to create a downfall object
    else if (!weatherP->downfall && ri->downfall_condition != WEATHER_CLEAR)
    {
        weatherP->downfall = CreateDownfallWeatherObject(ri);
    
        if(!weatherP->downfall->CreateMesh())
        {
            Error1("Failed to create downfall");
            delete weatherP->downfall;
            weatherP->downfall = NULL;
            return false;
        }
        
        // Calculate the drops
        csBox3 def = weatherP->downfall->CreateDefaultBBox();
        csBox3 box = weatherP->bbox;
        float mod = box.Volume() / def.Volume();
        int drops = (int)((float)ri->downfall_params.value * mod);
        
        if(drops < 1)
            drops = 1;
        
        // Setup the mesh
        weatherP->downfall->SetupMesh(weatherP->downfall->CreateDefaultBBox());

        //If the portal is a warp one, then distance is != 0
        csReversibleTransform transform = portal->GetWarp();
        csVector3 distance = transform.GetT2OTranslation();
        
        // move it to right position
        weatherP->downfall->MoveTo(ri,sect);
        weatherP->downfall->MoveTo(weatherP->pos + distance);
        Notify2( LOG_WEATHER, "Downfall created in portal '%s'",
                 portal->GetName());
    }

    // Check if fog need update
    if (ri->fog)
    {
        ri->fog->Update(delta);
    }
    // Check for removal of fog
    if (ri->fog && ri->fog_condition == WEATHER_CLEAR)
    {
        ri->fog->Destroy();
        delete ri->fog;
        ri->fog = NULL;
        Notify2( LOG_WEATHER, "Fog removed from sector '%s'",
                 sect->QueryObject()->GetName());
    }
    // Check for add of fog
    else if (!ri->fog && ri->fog_condition != WEATHER_CLEAR)
    {
        ri->fog = CreateStaticWeatherObject(ri);
        if (ri->fog->CreateMesh())
        {
            Notify2( LOG_WEATHER, "Fog created in sector '%s'",
                     sect->QueryObject()->GetName());
        }
    }

    return true;
}

WeatherPortal* ModeHandler::GetPortal(iPortal* portal)
{
    for(size_t i = 0; i < portals.GetSize(); i++)
    {
        if(portals[i]->portal == portal)
            return portals[i];
    }

    return NULL;
}

void ModeHandler::CreatePortalWeather(iSector* sector, csTicks delta)
{
    if(!sector)
    {
        Error1("CreatePortalWeather received invalid sector!");
        return;
    }

    // Check portals
    csSet<csPtrKey<iMeshWrapper> >::GlobalIterator it = sector->GetPortalMeshes().GetIterator();
    while (it.HasNext())
    {
        csPtrKey<iMeshWrapper> mesh = it.Next();
        iPortalContainer* portalc = mesh->GetPortalContainer();

        for (int pn=0; pn < portalc->GetPortalCount(); pn++)
        {
            // Create weather on each portal, or subscribe to create when first visible
            iPortal * portal = portalc->GetPortal(pn);
            CreatePortalWeather(portal, delta);
        }
    }
}

    
void ModeHandler::RemovePortalWeather()
{
    // Delete the weather meshes
    for(size_t i = 0; i < portals.GetSize();i++)
    {
        WeatherPortal* portal = portals[i];

        if (portal->downfall)
        {
            portal->downfall->Destroy();
            delete portal->downfall;
            portal->downfall = NULL;
        }

        if (portal->wi->fog)
        {
            portal->wi->fog->Destroy();
            delete portal->wi->fog;
            portal->wi->fog = NULL;
        }
        
        portals.DeleteIndex(i);
        i--;
    }
}

void ModeHandler::RemoveWeather()
{
    if (downfall)
    {
        downfall->StopFollow();
        downfall->Destroy();
        delete downfall;
        downfall = NULL;
    }

    if (fog)
    {
        fog->Destroy();
        delete fog;
        fog = NULL;
    }
}


void ModeHandler::HandleNewSectorMessage(MsgEntry* me)
{
    psNewSectorMessage msg(me);

    Debug3(LOG_ANY, 0, "Crossed from sector %s to sector %s.",
               msg.oldSector.GetData(),
               msg.newSector.GetData() );

    RemovePortalWeather();
    RemoveWeather();
    
    WeatherInfo *ri= GetWeatherInfo(msg.newSector);
    if(ri)
    {
        CreateWeather(ri, 0);
    }

    // Create new portals
    iSector* sect = psengine->GetEngine()->FindSector(msg.newSector);
    CreatePortalWeather(sect,0);

    /*
     * Now update bg music if possible.  SoundManager will
     * look for this sound in the soundlib.xml file.
     * Name used is sector name + " background".
     * This will also update weather sounds
     */
   SetSectorMusic(msg.newSector);
}

void ModeHandler::UpdateWeatherSounds()
{
    if(!soundmanager || !psengine->GetSoundStatus())
        return;

    WeatherSound sound;

    if(downfall)
    {
        sound = downfall->GetWeatherSound();
    }
    else
    {
        sound = WEATHER_SOUND_CLEAR;
    }

    soundmanager->UpdateWeather((int)sound);
}

void ModeHandler::SetSectorMusic(const char *sectorname)
{
    csVector3 pos = psengine->GetCelClient()->GetMainPlayer()->Pos();
   
    // Get the current sound for our main weather object
    WeatherSound sound;
    
    if(downfall && downfall->GetParent()->sector == sectorname)
    {
        sound = downfall->GetWeatherSound();
    }
    else
    {
        sound = WEATHER_SOUND_CLEAR;
    }
   
    csString sector(sectorname);
    if(psengine->GetSoundStatus())
    {
        soundmanager->EnterSector( sector, clock, (int)sound , pos );
    }
}

void ModeHandler::ClearLightFadeSettings()
{
    //Error1("*********************\nCLEARING LIGHT SETTINGS\n******************************\n");
    csTicks current_ticks;
    current_ticks = csGetTicks();

    if (current_ticks >= (interpolation_time + 1000))
        last_interpolation_reset = csGetTicks() - interpolation_time - 1000;
    else
        last_interpolation_reset = 0;

    interpolation_complete = false;
    UpdateLights(last_interpolation_reset);  // 0% sets up everything
}

void ModeHandler::PublishTime( int newTime )
{
    Debug2(LOG_WEATHER,0, "*New time of the Day: %d\n", newTime );
    if(psengine->GetSoundStatus())
    {
        psengine->GetSoundManager()->ChangeTimeOfDay( newTime );
    }

    // Publish raw time first
    PawsManager::GetSingleton().Publish("TimeOfDay",newTime);

    char buf[100];
    char time[3];
    time[0] = 'A'; time[1]='M'; time[2]='\0';
    
    if ( newTime >= 12 ) 
    {
        newTime-= 12;
        time[0] = 'P';
    }
    // "0 o'clock" is really "12 o'clock" for both AM and PM
    if (newTime == 0)
      newTime = 12;
    
    csString translation = PawsManager::GetSingleton().Translate("o'clock");
    sprintf(buf, "%d %s(%s)", newTime, translation.GetData(), time );
    PawsManager::GetSingleton().Publish("TimeOfDayStr",buf);    
}



void ModeHandler::HandleWeatherMessage(MsgEntry* me)
{
    psWeatherMessage msg(me);

    switch(msg.type)
    {
        case psWeatherMessage::WEATHER:
        {    
            if (msg.weather.has_lightning)
            {
                ProcessLighting(msg.weather);
            }
            if (msg.weather.has_downfall)
            {
                ProcessDownfall(msg.weather);
            }
            if (msg.weather.has_fog)
            {
                ProcessFog(msg.weather);
            }
            break;
        }
            
        case psWeatherMessage::DAYNIGHT:
        {
            clock = msg.hour;
            Notify2(LOG_WEATHER, "The time is now %d o'clock.\n",clock);
            if ( clock >= 22 || clock <= 6 )
                timeOfDay = TIME_NIGHT;
            else if ( clock >=7 && clock <=12 )
                timeOfDay = TIME_MORNING;
            else if ( clock >=13 && clock <=18 )    
                timeOfDay = TIME_AFTERNOON;
            else if ( clock >=19 && clock < 22 )
                timeOfDay = TIME_EVENING;    
            
            if (abs(clock) > (int)lights.GetSize() )
            {
                Bug1("Illegal value for time.\n");
                break;
            }

            PublishTime(abs(clock));
            
            // Reset the time basis for interpolation
            if (clock >= 0)
            {
                last_interpolation_reset = csGetTicks();
                last_lightset = clock;
                interpolation_complete = false;
                // Update lighting setup for this time period
                UpdateLights(last_interpolation_reset);
            }
            else // if a negative hour is passed, just jump to it--don't interpolate.
            {
                ClearLightFadeSettings();
                last_lightset = -clock;
                UpdateLights( csGetTicks() );         // 100% sets the light values
            }

            break;
        }
    }
}

void ModeHandler::UpdateLights()
{
    last_interpolation_reset = csGetTicks();
    interpolation_complete = false;

    // Update lighting setup for this time period
    UpdateLights(last_interpolation_reset);
}

float ModeHandler::GetDensity(WeatherInfo* wi)
{
    switch(wi->downfall_condition)
    {
        case WEATHER_RAIN:
            return RainWeatherObject::GetDensity(wi->downfall_params.value);
        case WEATHER_SNOW:
            return SnowWeatherObject::GetDensity(wi->downfall_params.value);
        default:
        {
            switch (wi->fog_condition)
            {
                case WEATHER_FOG:
                    return FogWeatherObject::GetDensity(wi->fog_params.value);
                default:
                    return 0.0f;
            }
        }
    }

    return 0.0f;
}


void ModeHandler::ProcessLighting(psWeatherMessage::NetWeatherInfo& info)
{
    Debug2( LOG_WEATHER, 0, "Lightning in sector %s",info.sector.GetData());
    // Get the sequence manager
    csRef<iEngineSequenceManager> seqmgr =  csQueryRegistry<iEngineSequenceManager> (object_reg);
    if(!seqmgr)
    {
        Error2("Couldn't apply thunder in sector %s! No SequenceManager!",info.sector.GetData());
        psSystemMessage sysMsg( 0, MSG_INFO, "Error while trying to create a lightning." );
        msghandler->Publish( sysMsg.msg );
        return;
    }

    // Make sure the sequence uses the right ambient light value to return to
    iSector *sector = engine->FindSector(info.sector);
    if (sector)
    {
        csColor bg = sector->GetDynamicAmbientLight();
        iSharedVariable *var = engine->GetVariableList()->FindByName("lightning reset");
        if (var)
        {
            var->SetColor(bg);
        }
    }

    // Run the lightning sequence.
    csString name(info.sector);
    name.Append(" lightning");
    //We search if the sector has the support for this sequence (lightning)
    if(!seqmgr->FindSequenceByName(name))
    {   
        Notify2(LOG_WEATHER,"Couldn't apply thunder in sector %s! No sequence for "
                "this sector!",info.sector.GetData());
        return;
    }
    else
    {
        seqmgr->RunSequenceByName(name,0);
    }

    // Only play thunder if lightning is in current sector
    csVector3 pos;
    if (CheckCurrentSector(entity,info.sector,pos,sector))
    {
        // Queue sound to be played later (actually by UpdateLights below)
        int which = randomgen->Get(5);
        sound_name.Format("amb.weather.thunder[%d]",which);
        sound_when = csGetTicks() + randomgen->Get(4000) + 300;
        sound_queued = true;
    }
}

void ModeHandler::ProcessFog(psWeatherMessage::NetWeatherInfo& info)
{
    WeatherInfo * wi = GetWeatherInfo(info.sector);

    if(info.fog_density)
    {
        Notify2( LOG_WEATHER, "Fog is coming in %s...",(const char *)info.sector);

        if (!wi)
        {
            // We need a wether info struct for the fog
            wi = CreateWeatherInfo(info.sector);
        }

        wi->fog_condition = WEATHER_FOG;
        wi->r = info.r;
        wi->g = info.g;
        wi->b = info.b;
        
        if(info.fog_fade)
        {
            wi->fog_params.fade_time  = info.fog_fade;
            wi->fog_params.fade_value = info.fog_density;
        }
        else
        {
            wi->fog_params.fade_time = 0;
            wi->fog_params.value = wi->fog_params.fade_value = info.fog_density;
        }
    }
    else
    {
        // No weather to disable if there are no weather info
        if (!wi)
        {
            return;
        }
        Notify2( LOG_WEATHER, "Fog is fading away in %s...",(const char *)info.sector);

        if (info.fog_fade)
        {
            // Update weatherinfo
            wi->fog_params.fade_value = 0;         // Set no fade
            wi->fog_params.fade_time = info.fog_fade;
        }
        else
        {
            wi->fog_params.value = 0;         // Set no drops
            wi->fog_params.fade_value = 0;    // Set no fade
            wi->fog_params.fade_time = 0;
        }
    }
}

void ModeHandler::ProcessDownfall(psWeatherMessage::NetWeatherInfo& info)
{
    //    iSector* current = psengine->GetCelClient()->GetMainPlayer()->GetSector();
    WeatherInfo *ri = GetWeatherInfo(info.sector);

    if (info.downfall_drops) //We are creating rain/snow.
    {
        Notify2(LOG_WEATHER, "It starts raining in %s...",(const char *)info.sector);

        // If no previous weather info create one
        if(!ri)
        {
            ri = CreateWeatherInfo(info.sector);
        }

        ri->downfall_condition  = (info.downfall_is_snow?WEATHER_SNOW:WEATHER_RAIN);

        if (info.downfall_fade)
        {
            ri->downfall_params.fade_time  = info.downfall_fade;
            ri->downfall_params.fade_value = info.downfall_drops;
        }
        else
        {
            ri->downfall_params.fade_time  = 0;
            ri->downfall_params.value = ri->downfall_params.fade_value = info.downfall_drops;
        }
    }
    else
    { //We are removing rain/snow

        // No weather to disable if there are no weather info
        if (!ri)
        {
            return;
        }

        Notify2( LOG_WEATHER, "It stops raining in %s...",(const char *)info.sector);

        if (info.downfall_fade)
        {
            ri->downfall_params.fade_value = 0;         // Set no drops
            ri->downfall_params.fade_time  = info.downfall_fade;
        }
        else
        {
            ri->downfall_params.value      = 0;         // Set no drops
            ri->downfall_params.fade_value = 0;         // Set no fade
            ri->downfall_params.fade_time  = 0;
        }
    }
}


void ModeHandler::PreProcess()
{
    UpdateLights(csGetTicks());
    UpdateWeather(csGetTicks());
}

/**
 * This function is called periodically by psclient.  It
 * handles the smooth interpolation of lights to the new
 * values.  It simply calculates a new pct complete and
 * goes through all the light settings of the current time
 * to update them.
 */
void ModeHandler::UpdateLights(csTicks when)
{
    if (sound_queued)
    {
        if (when >= sound_when)
        {
            if(psengine->GetSoundStatus())
            {
                csRef<iSndSysSource> toPlay = soundmanager->StartAmbientSound(sound_name,iSoundManager::NO_LOOP);
            }
            sound_queued = false;
        }
    }

    if (interpolation_complete)
    {
        return;
    }

    // Calculate how close to stated values we should be by now
    float pct = when-last_interpolation_reset;
    pct /= interpolation_time;

    // This keeps the interpolation from happening every single frame
    // because that is too slow.  Now it will divide up the interpolation
    // into a maximum of 20 steps.
    if ((when!=last_interpolation_reset) && (when - last_interpolation_ticks < interpolation_time/20))
    {
        return;
    }

    if (when > last_interpolation_reset + interpolation_time)
    {
        pct = 1;
        interpolation_complete = true;  // do 1 past the end, but then skip in the future
    }

    if (last_lightset >= lights.GetSize())
    {
        return;
    }
    LightingList *list = lights[last_lightset];

    int count = 0;
    if (list)
    {
        for (size_t i=0; i<list->colors.GetSize(); i++)
        {
            if (ProcessLighting(list->colors[i],pct))
                count++;
        }
    }
    
    last_interpolation_ticks = when;
}

/**
 * This updates 1 light
 */
bool ModeHandler::ProcessLighting(LightingSetting *setting,float pct)
{
    csColor interpolate_color;
    csColor target_color;
    
    target_color.red = 0.0f;
    target_color.blue = 0.0f;
    target_color.green = 0.0f;

    // set up target color and differentials if the interpolation is just starting.
    if (pct==0)
    {
        // determine whether to interpolate using weathering color or daylight color
        WeatherInfo *ri = NULL;
        if ((const char *)setting->sector)
        {
            ri = GetWeatherInfo(setting->sector);
        }

        // then weathering in sector in which light is located
        if (ri && downfall && (setting->raincolor.red || setting->raincolor.green || setting->raincolor.blue)) 
        {
            // Interpolate between daylight color and full weather color depending on weather intensity
            target_color.red   = setting->color.red   + ((setting->raincolor.red   - setting->color.red)  * GetDensity(ri));
            target_color.green = setting->color.green + ((setting->raincolor.green - setting->color.green)* GetDensity(ri));
            target_color.blue  = setting->color.blue  + ((setting->raincolor.blue  - setting->color.blue) * GetDensity(ri));
        }
        else
        {
            target_color = setting->color;
        }
    }
    
    if (setting->type == "light")
    {
        iLight* light = setting->light_cache;
        if (!light)
        {
            // Light is not in the cache. Try to find it and update cache.
            setting->light_cache = psengine->GetEngine()->FindLight(setting->object);
            light = setting->light_cache;
        }
        if (light)
        {
            if ( light->GetDynamicType() != CS_LIGHT_DYNAMICTYPE_PSEUDO )
            {
                Warning2(LOG_WEATHER, "Light '%s' is not marked as <dynamic /> in the world map!", (const char *)setting->object);
                setting->error = true;
                return false;
            }
            csColor existing = light->GetColor(); // only update the light if it is in fact different.
            if (pct == 0) // save starting color of light for interpolation
            {
                setting->start_color = existing;
                // precalculate diff to target color so only percentages have to be applied later
                setting->diff.red   = target_color.red   - setting->start_color.red;
                setting->diff.green = target_color.green - setting->start_color.green;
                setting->diff.blue  = target_color.blue  - setting->start_color.blue;
            }
    
            interpolate_color.red   = setting->start_color.red   + (setting->diff.red*pct);
            interpolate_color.green = setting->start_color.green + (setting->diff.green*pct);
            interpolate_color.blue  = setting->start_color.blue  + (setting->diff.blue*pct);

            if (existing.red   != interpolate_color.red ||
                existing.green != interpolate_color.green ||
                existing.blue  != interpolate_color.blue)
                {
                    light->SetColor(interpolate_color);
                }
            
            return true;
        }
        else
        {
            // Warning3( LOG_WEATHER, "Light '%s' was not found in lighting setup %d.",(const char*)setting->object, setting->value);
            setting->error = true;
            return false;
        }
    }
    else if (setting->type == "ambient")
    {
        iSector* sector = setting->sector_cache;
        if (!sector)
        {
            // Sector is not in the cache. Try to find it and update cache.
            setting->sector_cache = psengine->GetEngine()->FindSector(setting->object);
            sector = setting->sector_cache;
        }
        if (sector)
        {
            if (pct == 0)
            {
                setting->start_color = sector->GetDynamicAmbientLight();

                // precalculate diff to target color so only percentages have to be applied later
                setting->diff.red   = target_color.red   - setting->start_color.red;
                setting->diff.green = target_color.green - setting->start_color.green;
                setting->diff.blue  = target_color.blue  - setting->start_color.blue;
            }
            
            interpolate_color.red   = setting->start_color.red   + (setting->diff.red*pct);
            interpolate_color.green = setting->start_color.green + (setting->diff.green*pct);
            interpolate_color.blue  = setting->start_color.blue  + (setting->diff.blue*pct);

            sector->SetDynamicAmbientLight(interpolate_color);
            return true;
        }
        else
        {
//            Warning3( LOG_WEATHER, "Sector '%s' for ambient light was not found in lighting setup %d.\n", (const char *)setting->object, setting->value);
            setting->error = true;
            
            return false;
        }
    }
    else if (setting->type == "fog")
    {
        iSector* sector = setting->sector_cache;
        if (!sector)
        {
            // Sector is not in the cache. Try to find it and update cache.
            setting->sector_cache = psengine->GetEngine()->FindSector(setting->object);
            sector = setting->sector_cache;
        }
        if (sector)
        {
            if (setting->density)
            {
                if (pct == 0)
                {
                    if (sector->HasFog())
                    {
                        csColor fogColor = sector->GetFog().color;
                        
                        setting->start_color.Set(fogColor.red,fogColor.green,fogColor.blue);
                    }
                    else
                        setting->start_color.Set(0,0,0);

                    // precalculate diff to target color so only percentages have to be applied later
                    setting->diff.red   = target_color.red   - setting->start_color.red;
                    setting->diff.green = target_color.green - setting->start_color.green;
                    setting->diff.blue  = target_color.blue  - setting->start_color.blue;
                }
                
                interpolate_color.red   = setting->start_color.red   + (setting->diff.red*pct);
                interpolate_color.green = setting->start_color.green + (setting->diff.green*pct);
                interpolate_color.blue  = setting->start_color.blue  + (setting->diff.blue*pct);
                sector->SetFog(setting->density,interpolate_color);
            }
            else
            {
                sector->DisableFog();
            }
            
            return true;
        }
        else
        {
            Warning3( LOG_WEATHER, "Sector '%s' for fog was not found in lighting setup %i.\n",(const char *)setting->object, (int)setting->value);
            setting->error = true;

            return false;
        }
    }
    else
    {
#ifdef DEBUG_LIGHTING
        printf("Lighting setups do not currently support objects of type '%s'.\n",
               (const char *)setting->type);
#endif
        return false;
    }
}


void ModeHandler::UpdateWeather(csTicks when)
{
    // Only update weather if more than updat_time have elapsed
    // since last update
    if (when - last_weather_update < weather_update_time)
    {
        return ;
    }

    int delta = when - last_weather_update;

    // Set a flag to indicate if we are ready to update the gfx yet
    int update_gfx = psengine->GetCelClient()->GetMainPlayer() &&
        psengine->GetCelClient()->GetMainPlayer()->GetSector();

    csString current_sector;

    if (update_gfx)
    {
        current_sector = psengine->GetCelClient()->GetMainPlayer()->GetSector()->QueryObject()->GetName();
    }

    /*
     * Update fade in weather info so 
     * that rain etc are at the right level when
     * entering a new sector.
     */
    BinaryRBIterator<WeatherInfo> loop(&weatherlist);
    WeatherInfo *wi, *current_wi = NULL;
    
    for ( wi = loop.First(); wi; wi = ++loop)
    {

        wi->Fade(&wi->downfall_params,delta);
        if (wi->downfall_params.value <= 0)
        {
            wi->downfall_params.value = 0;
            wi->downfall_condition = WEATHER_CLEAR;
        }
        
        wi->Fade(&wi->fog_params,delta);
        if (wi->fog_params.value <= 0)
        {
            wi->fog_params.value = 0;
            wi->fog_condition = WEATHER_CLEAR;
        }
        
        // Remember wi for current sector for use later
        if (wi->sector == current_sector)
        {
            current_wi = wi;
        }
    }
    wi = current_wi; // Use wi for rest of code, easier to read wi than current_wi

    // Create/Update/Remove weather gfx
    if (update_gfx)
    {
        // Update for current sector if wi
        if (wi)
        {
            CreateWeather(wi,delta);
        }

        // Refresh the portals
        iSector* current = psengine->GetCelClient()->GetMainPlayer()->GetSector();
        CreatePortalWeather(current,delta);
        
        // Update sounds
        UpdateWeatherSounds();
    }


    last_weather_update = when;
}


void ModeHandler::StartFollowWeather()
{
    downfall->StartFollow();
}

void ModeHandler::StopFollowWeather()
{
    downfall->StopFollow();
    downfall->Destroy();
}

bool ModeHandler::CheckCurrentSector(iCelEntity *entity, 
                                     const char *sectorname,
                                     csVector3& pos,
                                     iSector*&  sector)
{
    if (!pcmesh)
    {
        pcmesh = CEL_QUERY_PROPCLASS_ENT( entity, iPcMesh );
        if (!pcmesh)
        {
            Bug1("No Mesh found on entity!\n");
            return false;
        }
    }
    
    iMovable* movable = pcmesh->GetMesh()->GetMovable();

    sector = movable->GetSectors()->Get(0);

    // Skip the weather if sector is named and not the same as the player sector
    if (sectorname && strlen(sectorname) && strcmp(sectorname,sector->QueryObject()->GetName()) )
    {
        return false;
    }

    pos = movable->GetPosition();
    
    return true;
}

bool ModeHandler::CreateWeather(WeatherInfo* ri, csTicks delta)
{
    if (!entity)
    {
        Bug1("No player entity has been assigned so weather cannot be created.");
        return false;
    }

    // Check that player is in same sector that the weather and 
    // get sector pointer and position.
    csVector3 pos;
    iSector *sector;
    if (!CheckCurrentSector(entity,ri->sector,pos,sector))
        return false;


    // Check for removal of downfall
    if (downfall && ri->downfall_condition == WEATHER_CLEAR)
    {
        downfall->StopFollow();
        downfall->Destroy();
        delete downfall;
        downfall = NULL;
        Notify2( LOG_WEATHER, "Downfall removed from sector '%s'",
                 sector->QueryObject()->GetName());
    }
    // Check if downfall has changed between rain/snow
    else if (downfall && downfall->GetType() != ri->downfall_condition)
    {
        downfall->StopFollow();
        downfall->Destroy();
        delete downfall;
        downfall = CreateDownfallWeatherObject(ri);
        if (downfall->CreateMesh())
        {
            downfall->SetupMesh(downfall->CreateDefaultBBox());
            downfall->StartFollow();
            Notify2( LOG_WEATHER, "Downfall created in sector '%s'",
                     sector->QueryObject()->GetName());
        } else
        {
            Error1("Failed to create downfall");
            delete downfall;
            downfall = NULL;
        }
        Notify2( LOG_WEATHER, "Downfall changed type in sector '%s'",
                 sector->QueryObject()->GetName());
    }
    // Check if downfall need update
    else if (downfall && ri->downfall_condition != WEATHER_CLEAR)
    {
        downfall->Update(delta);
    }
    // Check for add of downfall
    else if (!downfall && ri->downfall_condition != WEATHER_CLEAR)
    {
        downfall = CreateDownfallWeatherObject(ri);
        if (downfall->CreateMesh())
        {
            downfall->SetupMesh(downfall->CreateDefaultBBox());
            downfall->StartFollow();
            Notify2( LOG_WEATHER, "Downfall created in sector '%s'",
                     sector->QueryObject()->GetName());
        } else
        {
            Error1("Failed to create downfall");
            delete downfall;
            downfall = NULL;
        }
    }


    // Check if fog need update
    if (fog)
    {
        fog->Update(delta);
    }
    // Check for removal of fog
    if (fog && ri->fog_condition == WEATHER_CLEAR)
    {
        fog->Destroy();
        delete fog;
        fog = NULL;
        Notify2( LOG_WEATHER, "Fog removed from sector '%s'",
                 sector->QueryObject()->GetName());
    }
    // Check for add of fog
    else if (!fog && ri->fog_condition != WEATHER_CLEAR)
    {
        fog = CreateStaticWeatherObject(ri);
        if (fog->CreateMesh())
        {
            Notify2( LOG_WEATHER, "Fog created in sector '%s'",
                     sector->QueryObject()->GetName());
        }
    }
    

    return true;
}

WeatherObject* ModeHandler::CreateDownfallWeatherObject(WeatherInfo* ri)
{
    WeatherObject* object = NULL;
    
    switch(ri->downfall_condition)
    {
        case WEATHER_CLEAR:
            return NULL;
            
        case WEATHER_RAIN:
        {
            object = new RainWeatherObject(ri);
            break;
        }
        case WEATHER_SNOW:
        {
            object = new SnowWeatherObject(ri);
            break;
        }
        default:
        {
            CS_ASSERT(false); // Should not be used to create anything else
                              // than downfall
            break;
        }
    }

    return object;

}

WeatherObject* ModeHandler::CreateStaticWeatherObject(WeatherInfo* ri)
{
    WeatherObject* object = NULL;
    
    switch(ri->fog_condition)
    {
        case WEATHER_CLEAR:
            return NULL;
            
        case WEATHER_FOG:
        {
            object = new FogWeatherObject(ri);
            break;
        }
        default:
        {
            CS_ASSERT(false); // Should not be used to create anything else
                              // than fog
            break;
        }
    }

    return object;

}

void ModeHandler::HandleCombatEvent(MsgEntry* me)
{
    psCombatEventMessage event(me);

    //printf("Got Combat event...\n");
    if(!psengine->IsGameLoaded())
        return; // Drop if we haven't loaded

    // Get the relevant entities
    iCelEntity *attacker, *target;
    GEMClientActor* atObject =  (GEMClientActor*)celclient->FindObject(event.attacker_id);
    GEMClientActor* tarObject = (GEMClientActor*)celclient->FindObject(event.target_id);

    csString location = psengine->slotName.GetSecondaryName( event.target_location );
    if (!atObject || !tarObject )
    {
        Bug1("NULL Attacker or Target combat event sent to client!");
        return;
    }

    attacker = atObject->GetEntity();
    target   = tarObject->GetEntity();

    SetCombatAnim( atObject, event.attack_anim );
    
    if (event.event_type == event.COMBAT_DEATH)
        tarObject->StopMoving();
    else
        SetCombatAnim( tarObject, event.defense_anim );

 
    if ( !chatWindow )
    {    
        chatWindow = (pawsChatWindow*)PawsManager::GetSingleton().FindWidget("ChatWindow");
        if(!chatWindow)
        {
            Error1("Couldn't find the communications window (ChatWindow), won't print combat.");
            return;
        }       
    }        

    
    // Display the text that goes with it
    if (attacker == celclient->GetMainActor() ) // we're attacking here
    {
        Attack( event.event_type, event.damage, atObject, tarObject, location );                
    }
    else if (target == celclient->GetMainActor() )  // we're being attacked
    {
        Defend( event.event_type, event.damage, atObject, tarObject, location );
    }        
    else // 3rd party msg
    {
        Other(  event.event_type, event.damage, atObject, tarObject, location );
    }
}



void ModeHandler::AttackBlock(GEMClientActor* atObject, GEMClientActor* tarObject, csString& location )
{
    psengine->GetEffectManager()->RenderEffect("combatBlock", csVector3(0, 0, 0), tarObject->Mesh(), atObject->Mesh());
    if(!(chatWindow->GetSettings().meFilters & COMBAT_BLOCKED))
        return;

    psSystemMessage ev(0,MSG_COMBAT_BLOCK,"You attack %s on the %s but are blocked", tarObject->GetEntity()->GetName(), location.GetData() );
    msghandler->Publish(ev.msg);
}

void ModeHandler::AttackDamage(float damage, GEMClientActor* atObject, GEMClientActor* tarObject, csString& location )
{
    if (damage)
    {
        psengine->GetEffectManager()->RenderEffect("combatYourHit", csVector3(0, 0, 0), tarObject->Mesh(), atObject->Mesh());

        if(!(chatWindow->GetSettings().meFilters & COMBAT_SUCCEEDED))
            return;
            
        psSystemMessage ev(0,MSG_COMBAT_YOURHIT,"You hit %s on the %s for %1.2f damage!", tarObject->GetEntity()->GetName(), location.GetData(), damage );
        msghandler->Publish(ev.msg);
    }
    else
    {
        psengine->GetEffectManager()->RenderEffect("combatYourHitFail", csVector3(0, 0, 0), tarObject->Mesh(), atObject->Mesh());
        if(!(chatWindow->GetSettings().meFilters & COMBAT_FAILED))
            return;
            
        psSystemMessage ev(0,MSG_COMBAT_YOURHIT,"You hit %s on the %s but fail to do any damage!", tarObject->GetEntity()->GetName(), location.GetData());
        msghandler->Publish(ev.msg);
    }

}

void ModeHandler::AttackDeath( GEMClientActor* atObject, GEMClientActor* tarObject )
{
    if (atObject != tarObject) //not killing self
    {
        if (psengine->GetSoundStatus() && soundmanager->PlayingCombatMusic())
            psengine->GetEffectManager()->RenderEffect("combatVictory", csVector3(0, 0, 0), atObject->Mesh());

        psSystemMessage ev(0,MSG_COMBAT_VICTORY,"You have killed %s!", tarObject->GetEntity()->GetName() );
        msghandler->Publish(ev.msg);
    }
    else //killing self
    {
        psSystemMessage ev(0,MSG_COMBAT_DEATH,"You have killed yourself!");
        msghandler->Publish(ev.msg);
    }
}

void ModeHandler::AttackDodge(GEMClientActor* atObject, GEMClientActor* tarObject )
{
    psengine->GetEffectManager()->RenderEffect("combatDodge", csVector3(0, 0, 0), tarObject->Mesh(), atObject->Mesh());
    if(!(chatWindow->GetSettings().meFilters & COMBAT_DODGED))
        return;
        
    psSystemMessage ev(0,MSG_COMBAT_DODGE,"%s has dodged your attack!", tarObject->GetEntity()->GetName() );
    msghandler->Publish(ev.msg);

}

void ModeHandler::AttackMiss(GEMClientActor* atObject, GEMClientActor* tarObject, csString& location )
{
    psengine->GetEffectManager()->RenderEffect("combatMiss", csVector3(0, 0, 0), atObject->Mesh(), tarObject->Mesh());
    if(!(chatWindow->GetSettings().meFilters & COMBAT_MISSED))
        return;
        
    psSystemMessage ev(0,MSG_COMBAT_MISS,"You attack %s but missed the %s.", tarObject->GetEntity()->GetName(), location.GetData() );
    msghandler->Publish(ev.msg);
}

void ModeHandler::AttackOutOfRange( GEMClientActor* atObject, GEMClientActor* tarObject )
{
    psengine->GetEffectManager()->RenderEffect("combatMiss", csVector3(0, 0, 0), atObject->Mesh(), tarObject->Mesh());
    psSystemMessage ev(0,MSG_COMBAT_MISS,"You are too far away to attack %s.", tarObject->GetEntity()->GetName() );
    msghandler->Publish(ev.msg);
}



void ModeHandler::Attack( int type, float damage, GEMClientActor* atObject, GEMClientActor* tarObject, csString& location )
{
    switch (type)
    {
        case psCombatEventMessage::COMBAT_BLOCK:
        {
            AttackBlock( atObject, tarObject, location );
            break;
        }
        case psCombatEventMessage::COMBAT_DAMAGE_NEARLY_DEAD:
        {
            OtherNearlyDead(tarObject);
            // no break by intention
        }
        case psCombatEventMessage::COMBAT_DAMAGE:
        {
            AttackDamage( damage, atObject, tarObject, location );
            break;
        }
        case psCombatEventMessage::COMBAT_DEATH:
        {
            AttackDeath( atObject, tarObject );
            break;
        }
        case psCombatEventMessage::COMBAT_DODGE:
        {
            AttackDodge( atObject, tarObject );                
            break;
        }
        case psCombatEventMessage::COMBAT_MISS:
        {
            AttackMiss( atObject, tarObject, location );                
            break;
        }
        case psCombatEventMessage::COMBAT_OUTOFRANGE:
        {
            AttackOutOfRange( atObject, tarObject );                
            break;
        }
    }
}


void ModeHandler::DefendBlock(GEMClientActor* atObject, GEMClientActor* tarObject, csString& location )
{
    psengine->GetEffectManager()->RenderEffect("combatBlock", csVector3(0, 0, 0), tarObject->Mesh(), atObject->Mesh());

    if(!(chatWindow->GetSettings().meFilters & COMBAT_BLOCKED))
        return;
        
    psSystemMessage ev(0,MSG_COMBAT_BLOCK,"%s attacks you but your %s blocks it.", atObject->GetEntity()->GetName(), location.GetData() );
    msghandler->Publish(ev.msg);
}

void ModeHandler::DefendDamage( float damage, GEMClientActor* atObject, GEMClientActor* tarObject, csString& location )
{
    if(damage)
    {
        psengine->GetEffectManager()->RenderEffect("combatHitYou", csVector3(0, 0, 0), tarObject->Mesh(), atObject->Mesh());
        if(!(chatWindow->GetSettings().meFilters & COMBAT_SUCCEEDED))
            return;
            
        psSystemMessage ev(0,MSG_COMBAT_HITYOU,"%s hits you on the %s for %1.2f damage!",  atObject->GetName(), location.GetData(), damage );
        msghandler->Publish(ev.msg);
    }
    else
    {
        psengine->GetEffectManager()->RenderEffect("combatHitYouFail", csVector3(0, 0, 0), tarObject->Mesh(), atObject->Mesh());
        if(!(chatWindow->GetSettings().meFilters & COMBAT_FAILED))
            return;
            
        psSystemMessage ev(0,MSG_COMBAT_HITYOU,"%s hits you on the %s but fails to do any damage!",  atObject->GetName(), location.GetData());
        msghandler->Publish(ev.msg);
    }

}

void ModeHandler::DefendDeath( GEMClientActor* atObject )
{
    //atObject->
    psengine->GetEffectManager()->RenderEffect("combatDeath", csVector3(0,0,0), atObject->Mesh());
    psSystemMessage ev(0,MSG_COMBAT_OWN_DEATH,"You have been killed by %s!", atObject->GetEntity()->GetName() );
    msghandler->Publish(ev.msg);
}

void ModeHandler::DefendDodge( GEMClientActor* atObject, GEMClientActor* tarObject )
{
    psengine->GetEffectManager()->RenderEffect("combatDodge", csVector3(0, 0, 0), tarObject->Mesh(), atObject->Mesh());
    if(!(chatWindow->GetSettings().meFilters & COMBAT_DODGED))
        return;
        
    psSystemMessage ev(0,MSG_COMBAT_DODGE,"%s attacks you but you dodge.", atObject->GetEntity()->GetName() );
    msghandler->Publish(ev.msg);
}

void ModeHandler::DefendMiss( GEMClientActor* atObject, GEMClientActor* tarObject )
{
    psengine->GetEffectManager()->RenderEffect("combatMiss", csVector3(0, 0, 0), atObject->Mesh(), tarObject->Mesh());
    if(!(chatWindow->GetSettings().meFilters & COMBAT_MISSED))
        return;
        
    psSystemMessage ev(0,MSG_COMBAT_MISS,"%s attacks you but misses.", atObject->GetEntity()->GetName() );
    msghandler->Publish(ev.msg);
}

void ModeHandler::DefendOutOfRange( GEMClientActor* atObject, GEMClientActor* tarObject )
{
    psengine->GetEffectManager()->RenderEffect("combatMiss", csVector3(0, 0, 0), atObject->Mesh(), tarObject->Mesh());
    psSystemMessage ev(0,MSG_COMBAT_MISS,"%s attacks but is too far away to reach you.", atObject->GetEntity()->GetName() );
    msghandler->Publish(ev.msg);
}    

void ModeHandler::DefendNearlyDead()
{
    if (!(chatWindow->GetSettings().meFilters & COMBAT_SUCCEEDED))
        return;
    psSystemMessage ev(0, MSG_COMBAT_NEARLY_DEAD, "You are nearly dead!");
    msghandler->Publish(ev.msg);
}

void ModeHandler::Defend( int type, float damage, GEMClientActor* atObject, GEMClientActor* tarObject, csString& location )
{
    switch (type)
    {
        case psCombatEventMessage::COMBAT_BLOCK:
        {
            DefendBlock( atObject, tarObject, location ); 
            break;
        }

        case psCombatEventMessage::COMBAT_DAMAGE_NEARLY_DEAD:
        {
            DefendNearlyDead();
            // no break by intention
        }
        case psCombatEventMessage::COMBAT_DAMAGE:
        {
            DefendDamage( damage, atObject, tarObject, location ); 
            break;
        }
        
        case psCombatEventMessage::COMBAT_DEATH:
        {
            DefendDeath( atObject );
            break;
        }
        
        case psCombatEventMessage::COMBAT_DODGE:
        {
            DefendDodge( atObject, tarObject );             
            break;
        }
        
        case psCombatEventMessage::COMBAT_MISS:
        {
            DefendMiss( atObject, tarObject );             
            break;
        }
        
        case psCombatEventMessage::COMBAT_OUTOFRANGE:
        {
            DefendOutOfRange( atObject, tarObject );
            break;
        }
    }
}

void ModeHandler::OtherBlock( GEMClientActor* atObject, GEMClientActor* tarObject )
{
    bool isGrouped = celclient->GetMainPlayer()->IsGroupedWith(atObject) ||
            celclient->GetMainPlayer()->IsGroupedWith(tarObject);
    int level = celclient->GetMainPlayer()->GetType();

    psengine->GetEffectManager()->RenderEffect("combatBlock", csVector3(0, 0, 0), tarObject->Mesh(), atObject->Mesh());

    if(!((level > 0 || isGrouped) && (chatWindow->GetSettings().vicinityFilters & COMBAT_BLOCKED)))
        return;
        
    psSystemMessage ev(0,MSG_COMBAT_BLOCK,"%s attacks %s but they are blocked.", atObject->GetEntity()->GetName(), tarObject->GetEntity()->GetName() );
    msghandler->Publish(ev.msg);
}

void ModeHandler::OtherDamage( float damage, GEMClientActor* atObject, GEMClientActor* tarObject )
{
    bool isGrouped = celclient->GetMainPlayer()->IsGroupedWith(atObject) ||
            celclient->GetMainPlayer()->IsGroupedWith(tarObject);
    int level = celclient->GetMainPlayer()->GetType();

    if(damage)
    {
        psengine->GetEffectManager()->RenderEffect("combatHitOther", csVector3(0, 0, 0), tarObject->Mesh(), atObject->Mesh());
        if(!((level > 0 || isGrouped) && (chatWindow->GetSettings().vicinityFilters & COMBAT_SUCCEEDED)))
            return;
            
        psSystemMessage ev(0,MSG_COMBAT_HITOTHER,"%s hits %s for %1.2f damage!", atObject->GetEntity()->GetName(), tarObject->GetEntity()->GetName(), damage );
        msghandler->Publish(ev.msg);
    }
    else
    {
        psengine->GetEffectManager()->RenderEffect("combatHitOtherFail", csVector3(0, 0, 0), tarObject->Mesh(), atObject->Mesh());
        if(!((level > 0 || isGrouped) && (chatWindow->GetSettings().vicinityFilters & COMBAT_FAILED)))
            return;
        psSystemMessage ev(0,MSG_COMBAT_HITOTHER,"%s hits %s, but fails to do any damage!", atObject->GetEntity()->GetName(), tarObject->GetEntity()->GetName());
        msghandler->Publish(ev.msg);
    }

}

void ModeHandler::OtherDeath( GEMClientActor* atObject, GEMClientActor* tarObject )
{
    if (atObject != tarObject) //not killing self
    {
        psSystemMessage ev(0,MSG_COMBAT_DEATH,"%s has been killed by %s!", tarObject->GetEntity()->GetName(), atObject->GetEntity()->GetName() );
        msghandler->Publish(ev.msg);
    }
    else //killing self
    {
        psSystemMessage ev(0,MSG_COMBAT_DEATH,"%s has died!", tarObject->GetEntity()->GetName());
        msghandler->Publish(ev.msg);
    }
}

void ModeHandler::OtherDodge( GEMClientActor* atObject, GEMClientActor* tarObject )
{
    bool isGrouped = celclient->GetMainPlayer()->IsGroupedWith(atObject) ||
            celclient->GetMainPlayer()->IsGroupedWith(tarObject);
    int level = celclient->GetMainPlayer()->GetType();

    psengine->GetEffectManager()->RenderEffect("combatDodge", csVector3(0, 0, 0), tarObject->Mesh(), atObject->Mesh());
    if(!((level > 0 || isGrouped) && (chatWindow->GetSettings().vicinityFilters & COMBAT_DODGED)))
        return;
    psSystemMessage ev(0,MSG_COMBAT_DODGE,"%s attacks %s but %s dodges.", atObject->GetEntity()->GetName(),tarObject->GetEntity()->GetName(),tarObject->GetEntity()->GetName() );
    msghandler->Publish(ev.msg);
}

void ModeHandler::OtherMiss( GEMClientActor* atObject, GEMClientActor* tarObject ) 
{
    bool isGrouped = celclient->GetMainPlayer()->IsGroupedWith(atObject) ||
            celclient->GetMainPlayer()->IsGroupedWith(tarObject);
    int level = celclient->GetMainPlayer()->GetType();

    psengine->GetEffectManager()->RenderEffect("combatMiss", csVector3(0, 0, 0), atObject->Mesh(), tarObject->Mesh());
    if(!((level > 0 || isGrouped) && (chatWindow->GetSettings().vicinityFilters & COMBAT_MISSED)))
        return;
    psSystemMessage ev(0,MSG_COMBAT_MISS,"%s attacks %s but misses.", atObject->GetEntity()->GetName(),tarObject->GetEntity()->GetName() );
    msghandler->Publish(ev.msg);
}

void ModeHandler::OtherOutOfRange( GEMClientActor* atObject, GEMClientActor* tarObject ) 
{
    psengine->GetEffectManager()->RenderEffect("combatMiss", csVector3(0, 0, 0), atObject->Mesh(), tarObject->Mesh());
    psSystemMessage ev(0,MSG_COMBAT_MISS,"%s attacks but is too far away to reach %s.", atObject->GetEntity()->GetName(), tarObject->GetEntity()->GetName() );
    msghandler->Publish(ev.msg);
}    

void ModeHandler::OtherNearlyDead(GEMClientActor *tarObject)
{
    if(!(chatWindow->GetSettings().vicinityFilters & COMBAT_SUCCEEDED))
        return;
    psSystemMessage ev(0, MSG_COMBAT_NEARLY_DEAD, "%s is nearly dead!", tarObject->GetEntity()->GetName());
    msghandler->Publish(ev.msg);
}

void ModeHandler::Other(  int type, float damage, GEMClientActor* atObject, GEMClientActor* tarObject, csString& location )
{
    switch (type)
    {
        case psCombatEventMessage::COMBAT_BLOCK:
        {
            OtherBlock( atObject, tarObject );
            break;
        }
        case psCombatEventMessage::COMBAT_DAMAGE_NEARLY_DEAD:
        {
            OtherNearlyDead(tarObject);
            // no break by intention
        }
        case psCombatEventMessage::COMBAT_DAMAGE:
        {
            OtherDamage( damage, atObject, tarObject );
            break;
        }
        case psCombatEventMessage::COMBAT_DEATH:
        {
            OtherDeath( atObject, tarObject );
            break;
        }
        case psCombatEventMessage::COMBAT_DODGE:
        {
            OtherDodge( atObject, tarObject );
            break;
        }
        case psCombatEventMessage::COMBAT_MISS:
        {
            OtherMiss( atObject, tarObject );
            break;
        }
        case psCombatEventMessage::COMBAT_OUTOFRANGE:
        {
            OtherOutOfRange( atObject, tarObject );
            break;
        }
    }
}

void ModeHandler::SetCombatAnim( GEMClientActor* atObject, csStringID anim )
{
    // Set the relevant anims
    if (anim != csStringID(uint32_t(csInvalidStringID)))
    {
        iSpriteCal3DState* state = atObject->cal3dstate;
        if (state)
        {
            int idx = atObject->GetAnimIndex (celclient->GetClientDR ()->GetMsgStrings (), anim);
            state->SetAnimAction(idx,0.5F,0.5F);     
        }
    }    
}
