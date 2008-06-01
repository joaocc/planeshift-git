/*
 * psregion.cpp
 *
 * Copyright (C) 2008 Atomic Blue (info@planeshift.it, http://www.atomicblue.org) 
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
#include <cstool/collider.h>
#include <csutil/sysfunc.h>
#include <iengine/engine.h>
#include <iengine/collection.h>
#include <iengine/mesh.h>
#include <imap/loader.h>
#include <iutil/document.h>
#include <iutil/object.h>
#include <ivaria/collider.h>
#include <ivideo/graph3d.h>

//=============================================================================
// Project Includes
//=============================================================================
#include "util/log.h"
#include "util/psconst.h"

//=============================================================================
// Local Includes
//=============================================================================
#include "psregion.h"

psRegion::psRegion(iObjectRegistry *obj_reg, const char *file, uint _gfxFeatures)
{
    object_reg = obj_reg;

    worlddir.Format("/planeshift/world/%s", file);
    colldetworlddir.Format("/planeshift/world/cd_%s", file);
    worldfile  = "world";
    regionName = file;
    loaded     = false;
    gfxFeatures = _gfxFeatures;
    needToFilter = gfxFeatures != useAll;

    engine = csQueryRegistry<iEngine> (object_reg);
    vfs = csQueryRegistry<iVFS>(object_reg);
    xml = csQueryRegistry<iDocumentSystem>(object_reg);
    loader = csQueryRegistry<iLoader>(object_reg);
}

psRegion::~psRegion()
{
    Unload();
}


bool psRegion::Load(bool loadMeshes)
{
    if (loaded)
        return true;

    bool using3D;

    // Find out if we are ever going to render 3D
    csRef<iGraphics3D> g3d =  csQueryRegistry<iGraphics3D> (object_reg);
    csRef<iFactory> factory =  scfQueryInterface<iFactory> (g3d);

    using3D = (strcmp("crystalspace.graphics3d.null", factory->QueryClassID()) ? true : false);

    csString target;
    target.Format("%s/world", worlddir.GetData());

    csRef<iDataBuffer> buf (vfs->ReadFile (target.GetData()));
    if (!buf || !buf->GetSize ())
    {
        Error2("Error loading world file. %s\n", target.GetData());
        return false;
    }

    csRef<iDocument> doc = xml->CreateDocument();

    const char* error = doc->Parse( buf );

    if( error )
    {
        Error3("Error %s loading file to be cleaned. %s\n",error, target.GetData());
        return false;
    }

    csRef<iDocumentNode> worldNode = doc->GetRoot()->GetNode("world");

    if(!loadMeshes)
    {
        // Clean the world file to remove all textures/meshes/models
        Debug1(LOG_LOAD, 0,"Cleaning map file.");
        worldNode = Clean(worldNode);
    }
    else if(needToFilter)
    {
        // Filter the world file to get the correct settings.
        worldNode = Filter(worldNode, using3D);
    }

    // Create a new region with the given name, or select it if already there
    collection = engine->CreateCollection (regionName);

    // Now load the map into the selected region
    vfs->ChDir (worlddir);

    csTicks start = csGetTicks();
    Debug2(LOG_LOAD, 0,"Loading map file %s", worlddir.GetData());

    if (!loader->LoadMap(worldNode, CS_LOADER_KEEP_WORLD, collection, CS_LOADER_ACROSS_REGIONS, true, 0, 0, KEEP_USED))
    {
        Error3("LoadMap failed: %s, %s.",worlddir.GetData(),worldfile.GetData() );
        Error2("Region name was: %s", regionName.GetData());
        return false;
    }

    Debug2(LOG_LOAD, 0,"After LoadMapFile, %dms elapsed", csGetTicks()-start);

    // Successfully loaded.  Now get textures ready, etc. and return.
    if (using3D)
    {
        engine->ShineLights(collection);
        engine->PrecacheDraw (collection);
        Debug2(LOG_LOAD, 0,"After Precache, %dms elapsed", csGetTicks()-start);
    }

    if (loadMeshes)
    {
        csRef<iCollideSystem> cdsys = csQueryRegistry<iCollideSystem> (object_reg);

        csString target;
        target.Format("%s/%s", colldetworlddir.GetData(), worldfile.GetData());

        csRef<iDataBuffer> buf = vfs->ReadFile(target.GetData());
        if (!buf || !buf->GetSize())
        {
            SetupWorldColliders(engine);
        }
        else
        {
            const char* error = doc->Parse(buf);
            if(error)
            {
                Error3("Error %s while loading colldet world file: %s.\nFalling back to normal colldet, please report this error.\n", error, target.GetData());
                SetupWorldColliders(engine);
            }
            else
            {

                csRef<iDocumentNode> worldNodeCD = doc->GetRoot()->GetNode("world");

                iCollection* colldetCol = engine->CreateCollection("colldetPS");

                vfs->ChDir(colldetworlddir);

                if(!loader->LoadMap(worldNodeCD, CS_LOADER_KEEP_WORLD, colldetCol, CS_LOADER_ACROSS_REGIONS, false))
                {
                    Error3("LoadMap failed: %s, %s.\n", colldetworlddir.GetData(), worldfile.GetData() );
                    Error2("Region name was: %s\nFalling back to normal colldet, please report this error.\n", regionName.GetData());
                    SetupWorldColliders(engine);
                }
                else
                {
                    SetupWorldCollidersCD(engine, colldetCol);
                }

                engine->RemoveCollection("colldetPS");
            }
        }
        Debug2(LOG_LOAD, 0,"After SetupWorldColliders, %dms elapsed\n", csGetTicks()-start);
    }

    loaded = true;

    vfs->ChDir("/planeshift");
    engine->SetVFSCacheManager();

    printf("Map %s loaded successfully in %dms\n", regionName.GetData(), csGetTicks()-start);
    return true;
}

csRef<iDocumentNode> psRegion::Clean(csRef<iDocumentNode> world)
{
    csRef<iDocumentSystem> xml (
        csQueryRegistry<iDocumentSystem> (object_reg));

    csRef<iDocument> doc = xml->CreateDocument();
    csRef<iDocumentNode> node = doc->CreateRoot();

    // Copy the world node
    csRef<iDocumentNode> cleanedWorld = node->CreateNodeBefore(CS_NODE_ELEMENT);

    cleanedWorld->SetValue("world");

    // Copy the sector node
    csRef<iDocumentNodeIterator> sectors = world->GetNodes("sector");
    while ( sectors->HasNext() )
    {
        csRef<iDocumentNode> sector = sectors->Next();

        csRef<iDocumentNode> cleanedSector = cleanedWorld->CreateNodeBefore(CS_NODE_ELEMENT);
        cleanedSector->SetValue(sector->GetValue());

        // Copy the sector attributes
        csRef<iDocumentAttributeIterator> attrs = sector->GetAttributes();
        while(attrs->HasNext())
        {
            csRef<iDocumentAttribute> attr = attrs->Next();
            cleanedSector->SetAttribute(attr->GetName(), attr->GetValue());
        }

        // Copy the portal
        csRef<iDocumentNodeIterator> nodes = sector->GetNodes("portal");

        while(nodes->HasNext())
        {
            csRef<iDocumentNode> portal = nodes->Next();
            csRef<iDocumentNode> cleanedportal = cleanedSector->CreateNodeBefore(CS_NODE_ELEMENT);
            CloneNode(portal, cleanedportal);
        }

        // Copy the portals
        csRef<iDocumentNodeIterator> portalsItr = sector->GetNodes("portals");

        while(portalsItr->HasNext())
        {
            csRef<iDocumentNode> portals = portalsItr->Next();
            csRef<iDocumentNode> cleanedportals = cleanedSector->CreateNodeBefore(CS_NODE_ELEMENT);
            CloneNode(portals, cleanedportals);
        }


    }

    // Copy the start node
    csRef<iDocumentNodeIterator> startLocations = world->GetNodes("start");
    while (startLocations->HasNext())
    {
        csRef<iDocumentNode> start = startLocations->Next();
        csRef<iDocumentNode> cleanedStart = cleanedWorld->CreateNodeBefore(CS_NODE_ELEMENT);
        CloneNode(start, cleanedStart);
    }

    return cleanedWorld;
}

csRef<iDocumentNode> psRegion::Filter(csRef<iDocumentNode> world, bool using3D)
{
    if(!using3D)
    {
        if(world->GetNode("shaders"))
            world->RemoveNode(world->GetNode("shaders"));

        if(world->GetNode("textures"))
            world->RemoveNode(world->GetNode("textures"));

        if(world->GetNode("materials"))
            world->RemoveNode(world->GetNode("materials"));

        csRef<iDocumentNodeIterator> meshfacts = world->GetNodes("meshfact");
        while(meshfacts->HasNext())
        {
            csRef<iDocumentNode> meshfact = meshfacts->Next();
            csRef<iDocumentNode> params = meshfact->GetNode("params");
            if(params)
            {
                params->RemoveNodes(params->GetNodes("n"));
                csRef<iDocumentNodeIterator> submeshes = params->GetNodes("submesh");
                while(submeshes->HasNext())
                {
                    csRef<iDocumentNode> submesh = submeshes->Next();
                    if(submesh->GetNode("material"))
                    {
                        submesh->RemoveNode(submesh->GetNode("material"));
                    }
                }
            }
        }

        csRef<iDocumentNodeIterator> sectors = world->GetNodes("sector");
        while(sectors->HasNext())
        {
            csRef<iDocumentNode> sector = sectors->Next();
            csRef<iDocumentNodeIterator> meshobjs = sector->GetNodes("meshobj");
            while(meshobjs->HasNext())
            {
                csRef<iDocumentNode> meshobj = meshobjs->Next();
                csRef<iDocumentNode> params = meshobj->GetNode("params");
                if(params)
                {
                    if(params->GetNode("material"))
                    {
                        params->RemoveNode(params->GetNode("material"));
                    }
                    if(params->GetNode("materialpalette"))
                    {
                        params->RemoveNode(params->GetNode("materialpalette"));
                    }
                    csRef<iDocumentNodeIterator> submeshes = params->GetNodes("submesh");
                    while(submeshes->HasNext())
                    {
                        csRef<iDocumentNode> submesh = submeshes->Next();
                        if(submesh->GetNode("material"))
                        {
                            submesh->RemoveNode(submesh->GetNode("material"));
                        }

                    }
                }
            }
        }
    }
    else
    {
        if(!(gfxFeatures & useNormalMaps))
        {
            csRef<iDocumentNodeIterator> sectors = world->GetNodes("sector");
            while(sectors->HasNext())
            {
                csRef<iDocumentNode> sector = sectors->Next();
                csRef<iDocumentNode> rloop = sector->GetNode("renderloop");
                if(rloop.IsValid())
                {
                    csString value = rloop->GetContentsValue();
                    if(value.Compare("std_rloop_diffuse"))
                    {
                        sector->RemoveNode(rloop);
                    }
                }
            }
        }

        if(!(gfxFeatures & useMeshGen))
        {
            csRef<iDocumentNodeIterator> sectors = world->GetNodes("sector");
            while(sectors->HasNext())
            {
                csRef<iDocumentNode> sector = sectors->Next();
                csRef<iDocumentNodeIterator> meshgen = sector->GetNodes("meshgen");
                sector->RemoveNodes(meshgen);
            }
        }
    }

    return world;
}

void psRegion::CloneNode (iDocumentNode* from, iDocumentNode* to)
{
    to->SetValue (from->GetValue ());
    csRef<iDocumentNodeIterator> it = from->GetNodes ();
    while (it->HasNext ())
    {
        csRef<iDocumentNode> child = it->Next ();
        csRef<iDocumentNode> child_clone = to->CreateNodeBefore (
            child->GetType (), 0);
        CloneNode (child, child_clone);
    }
    csRef<iDocumentAttributeIterator> atit = from->GetAttributes ();
    while (atit->HasNext ())
    {
        csRef<iDocumentAttribute> attr = atit->Next ();
        to->SetAttribute (attr->GetName (), attr->GetValue ());
    }
}

void psRegion::Unload()
{
    if(loaded)
    {
        loaded = false;

        // Remove all objects from the region.
#ifndef CS_DEBUG
        engine->RemoveCollection(collection);
#else
        printf("Unloading %s\n", regionName.GetData());
        engine->RemoveCollection(collection);
#endif
    }
}

void psRegion::SetupWorldColliders(iEngine *engine)
{
    csRef<iCollideSystem> cdsys =
        csQueryRegistry<iCollideSystem> (object_reg);
    csRef<iObjectIterator> iter = collection->QueryObject()->GetIterator();

    iObject *curr;
    while ( iter->HasNext() )
    {
        curr = iter->Next();
        // regions hold many objects, but only meshes are collide-able
        csRef<iMeshWrapper> sp = scfQueryInterface<iMeshWrapper> (curr);
        if (sp && sp->GetMeshObject() )
        {
            csColliderHelper::InitializeCollisionWrapper(cdsys, sp);
        }
    }
}

void psRegion::SetupWorldCollidersCD(iEngine *engine, iCollection *cd_col)
{
    csRef<iCollideSystem> cdsys =
        csQueryRegistry<iCollideSystem> (object_reg);
    csRef<iObjectIterator> iter = cd_col->QueryObject()->GetIterator();

    iObject *curr;
    while (iter->HasNext())
    {
        curr = iter->Next();
        // regions hold many objects, but only meshes are collide-able
        csRef<iMeshWrapper> sp = scfQueryInterface<iMeshWrapper> (curr);
        if (sp && sp->GetMeshObject())
        {
            csRef<csColliderWrapper> cw = csColliderHelper::InitializeCollisionWrapper(cdsys, sp);
            if(cw)
            {
              csRef<iMeshWrapper> mw = collection->FindMeshObject(cw->GetObjectParent()->GetName());
              if(mw)
              {
                  mw->QueryObject()->ObjAdd(cw);
                  cw->SetObjectParent(mw->QueryObject());
              }
              else
              {
                  printf("Mesh Wrapper %s doesn't exist for collision!!\n", cw->GetObjectParent()->GetName());
              }
            }
        }
    }
}
