/*
 *  loader.cpp - Author: Mike Gist
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

#include <cstool/collider.h>
#include <csutil/scanstr.h>
#include <iengine/movable.h>
#include <iengine/portal.h>
#include <imesh/object.h>
#include <iutil/stringarray.h>
#include <iutil/object.h>
#include <ivaria/collider.h>
#include <ivideo/material.h>

#include "loader.h"
#include "util/strutil.h"
#include "globals.h"

void Loader::Init(iObjectRegistry* object_reg, bool keepModels, uint gfxFeatures, float loadRange)
{
    this->object_reg = object_reg;
    this->keepModels = keepModels;
    this->gfxFeatures = gfxFeatures;
    this->loadRange = loadRange;
    
    engine = csQueryRegistry<iEngine> (object_reg);
    tloader = csQueryRegistry<iThreadedLoader> (object_reg);
    vfs = csQueryRegistry<iVFS> (object_reg);
    svstrings = csQueryRegistryTagInterface<iShaderVarStringSet>(object_reg, "crystalspace.shader.variablenameset");
    strings = csQueryRegistryTagInterface<iStringSet>(object_reg, "crystalspace.shared.stringset");
    cdsys = csQueryRegistry<iCollideSystem> (object_reg);

    csRef<iGraphics3D> g3d = csQueryRegistry<iGraphics3D> (object_reg);
    txtmgr = g3d->GetTextureManager();

    engine->SetClearZBuf(true);

    if(keepModels)
    {
        PreloadTextures();
    }
}

void Loader::PrecacheData(const char* path)
{
    if(vfs->Exists(path))
    {
        csRef<iDocumentSystem> docsys = csQueryRegistry<iDocumentSystem>(object_reg);
        csRef<iDocument> doc = docsys->CreateDocument();
        csRef<iDataBuffer> data = vfs->ReadFile(path);

        doc->Parse(data, true);

        csRef<iDocumentNode> root = doc->GetRoot()->GetNode("library");
        if(!root.IsValid())
        {
            root = doc->GetRoot()->GetNode("world");
        }

        if(root.IsValid())
        {
            csRef<iDocumentNode> node;
            csRef<iDocumentNodeIterator> nodeItr;

            nodeItr = root->GetNodes("library");
            while(nodeItr->HasNext())
            {
                node = nodeItr->Next();
                PrecacheData(node->GetContentsValue());
            }

            node = root->GetNode("plugins");
            if(node.IsValid())
            {
                csRef<iThreadReturn> itr = tloader->LoadNode(node);
                itr->Wait();
            }

            node = root->GetNode("shaders");
            if(node.IsValid())
            {
                nodeItr = root->GetNodes("shader");
                while(nodeItr->HasNext())
                {
                    node = nodeItr->Next();
                    node = node->GetNode("file");
                    tloader->LoadShader(node->GetContentsValue());
                }
            }

            node = root->GetNode("textures");
            if(node.IsValid())
            {
                nodeItr = node->GetNodes("texture");
                while(nodeItr->HasNext())
                {
                    node = nodeItr->Next();
                    csRef<Texture> t = csPtr<Texture>(new Texture());
                    textures.Push(t);
                    t->name = node->GetAttributeValue("name");
                    t->data = node;
                }
            }

            node = root->GetNode("materials");
            if(node.IsValid())
            {
                nodeItr = node->GetNodes("material");
                while(nodeItr->HasNext())
                {
                    node = nodeItr->Next();
                    csRef<Material> m = csPtr<Material>(new Material(node->GetAttributeValue("name")));
                    materials.Push(m);
                    
                    if(node->GetNode("texture"))
                    {
                        node = node->GetNode("texture");
                        ShaderVar sv("tex diffuse", csShaderVariable::TEXTURE);
                        sv.value = node->GetContentsValue();
                        m->shadervars.Push(sv);

                        for(size_t i=0; i<textures.GetSize(); i++)
                        {
                            if(textures[i]->name.Compare(node->GetContentsValue()))
                            {
                                m->textures.Push(textures[i]);
                            }
                        }

                        node = node->GetParent();
                    }

                    csRef<iDocumentNodeIterator> nodeItr2 = node->GetNodes("shader");
                    while(nodeItr2->HasNext())
                    {
                        node = nodeItr2->Next();
                        m->shaders.Push(Shader(node->GetAttributeValue("type"), node->GetContentsValue()));
                        node = node->GetParent();
                    }

                    nodeItr2 = node->GetNodes("shadervar");
                    while(nodeItr2->HasNext())
                    {
                        node = nodeItr2->Next();
                        if(csString("texture").Compare(node->GetAttributeValue("type")))
                        {
                            ShaderVar sv(node->GetAttributeValue("name"), csShaderVariable::TEXTURE);
                            sv.value = node->GetContentsValue();
                            m->shadervars.Push(sv);
                            for(size_t i=0; i<textures.GetSize(); i++)
                            {
                                if(textures[i]->name.Compare(node->GetContentsValue()))
                                {
                                    m->textures.Push(textures[i]);
                                }
                            }
                        }
                        else if(csString("vector2").Compare(node->GetAttributeValue("type")))
                        {
                            ShaderVar sv(node->GetAttributeValue("name"), csShaderVariable::VECTOR2);
                            csScanStr (node->GetContentsValue(), "%f,%f", &sv.vec2.x, &sv.vec2.y);
                            m->shadervars.Push(sv);
                        }
                        node = node->GetParent();
                    }
                }
            }

            nodeItr = root->GetNodes("meshfact");
            while(nodeItr->HasNext())
            {
                node = nodeItr->Next();
                csRef<MeshFact> mf = csPtr<MeshFact>(new MeshFact(node->GetAttributeValue("name"), node));
                csRef<iDocumentNodeIterator> nodeItr3 = node->GetNode("params")->GetNodes("submesh");
                while(nodeItr3->HasNext())
                {
                    csRef<iDocumentNode> node2 = nodeItr3->Next();
                    if(node2->GetNode("material"))
                    {
                        for(size_t i=0; i<materials.GetSize(); i++)
                        {
                            if(materials[i]->name.Compare(node2->GetNode("material")->GetContentsValue()))
                            {
                                mf->materials.PushSmart(materials[i]);
                            }
                        }
                    }
                }

                if(node->GetNode("params")->GetNode("cells"))
                {
                    node = node->GetNode("params")->GetNode("cells")->GetNode("celldefault")->GetNode("basematerial");
                    for(size_t i=0; i<materials.GetSize(); i++)
                    {
                        if(materials[i]->name.Compare(node->GetContentsValue()))
                        {
                            mf->materials.PushSmart(materials[i]);
                        }
                    }
                    node = node->GetParent()->GetParent();

                    nodeItr3 = node->GetNodes("cell");
                    while(nodeItr3->HasNext())
                    {
                        node = nodeItr3->Next();
                        node = node->GetNode("feederproperties");
                        if(node)
                        {
                            csRef<iDocumentNodeIterator> nodeItr4 = node->GetNodes("alphamap");
                            while(nodeItr4->HasNext())
                            {
                                csRef<iDocumentNode> node2 = nodeItr4->Next();
                                for(size_t i=0; i<materials.GetSize(); i++)
                                {
                                    if(materials[i]->name.Compare(node2->GetAttributeValue("material")))
                                    {
                                        mf->materials.PushSmart(materials[i]);
                                    }
                                }
                            }
                        }
                    }
                }

                meshfacts.Push(mf);
            }

            nodeItr = root->GetNodes("sector");
            while(nodeItr->HasNext())
            {
                node = nodeItr->Next();

                csRef<Sector> s;
                csString sectorName = node->GetAttributeValue("name");
                for(size_t i=0; i<sectors.GetSize(); i++)
                {
                    if(sectors[i]->name.Compare(sectorName))
                    {
                        s = sectors[i];
                        break;
                    }
                }

                if(!s.IsValid())
                {
                    s = csPtr<Sector>(new Sector(sectorName));
                    sectors.Push(s);
                }

                s->culler = node->GetNode("cullerp")->GetContentsValue();
                if(node->GetNode("ambient"))
                {
                    node = node->GetNode("ambient");
                    s->ambient = csColor(node->GetAttributeValueAsFloat("red"),
                        node->GetAttributeValueAsFloat("green"), node->GetAttributeValueAsFloat("blue"));
                    node = node->GetParent();
                }

                csRef<iDocumentNodeIterator> nodeItr2 = node->GetNodes("meshobj");
                while(nodeItr2->HasNext())
                {
                    csRef<iDocumentNode> node2 = nodeItr2->Next();
                    csRef<MeshObj> m = csPtr<MeshObj>(new MeshObj(node2->GetAttributeValue("name"), node2));
                    m->sector = s;

                    if(node2->GetNode("move"))
                    {
                        node2 = node2->GetNode("move")->GetNode("v");
                        m->pos = csVector3(node2->GetAttributeValueAsFloat("x"),
                            node2->GetAttributeValueAsFloat("y"), node2->GetAttributeValueAsFloat("z"));
                        node2 = node2->GetParent()->GetParent();
                    }

                    if(node2->GetNode("paramsfile"))
                    {
                        csRef<iDocument> pdoc = docsys->CreateDocument();
                        csRef<iDataBuffer> pdata = vfs->ReadFile(node2->GetNode("paramsfile")->GetContentsValue());
                        pdoc->Parse(pdata, true);
                        node2 = pdoc->GetRoot();
                    }

                    csRef<iDocumentNodeIterator> nodeItr3 = node2->GetNode("params")->GetNodes("submesh");
                    while(nodeItr3->HasNext())
                    {
                        csRef<iDocumentNode> node3 = nodeItr3->Next();
                        if(node3->GetNode("material"))
                        {
                            for(size_t i=0; i<materials.GetSize(); i++)
                            {
                                if(materials[i]->name.Compare(node3->GetNode("material")->GetContentsValue()))
                                {
                                    m->materials.PushSmart(materials[i]);
                                }
                            }
                        }

                        csRef<iDocumentNodeIterator> nodeItr4 = node3->GetNodes("shadervar");
                        while(nodeItr4->HasNext())
                        {
                            node3 = nodeItr4->Next();
                            if(csString("texture").Compare(node3->GetAttributeValue("type")))
                            {
                                for(size_t i=0; i<textures.GetSize(); i++)
                                {
                                    if(textures[i]->name.Compare(node3->GetContentsValue()))
                                    {
                                        m->textures.PushSmart(textures[i]);
                                    }
                                }
                            }
                        }
                    }

                    node2 = node2->GetNode("params")->GetNode("factory");
                    for(size_t i=0; i<meshfacts.GetSize(); i++)
                    {
                        if(meshfacts[i]->name.Compare(node2->GetContentsValue()))
                        {
                            m->meshfacts.PushSmart(meshfacts[i]);
                        }
                    }
                    node2 = node2->GetParent();

                    if(node2->GetNode("material"))
                    {
                        node2 = node2->GetNode("material");
                        for(size_t i=0; i<materials.GetSize(); i++)
                        {
                            if(materials[i]->name.Compare(node2->GetContentsValue()))
                            {
                                m->materials.PushSmart(materials[i]);
                            }
                        }
                        node2 = node2->GetParent();
                    }

                    
                    if(node2->GetNode("materialpalette"))
                    {
                        nodeItr3 = node2->GetNode("materialpalette")->GetNodes("material");
                        while(nodeItr3->HasNext())
                        {
                            csRef<iDocumentNode> node3 = nodeItr3->Next();
                            for(size_t i=0; i<materials.GetSize(); i++)
                            {
                                if(materials[i]->name.Compare(node3->GetContentsValue()))
                                {
                                    m->materials.PushSmart(materials[i]);
                                }
                            }
                        }
                    }

                    if(node2->GetNode("cells"))
                    {
                        nodeItr3 = node2->GetNode("cells")->GetNodes("cell");
                        while(nodeItr3->HasNext())
                        {
                            node2 = nodeItr3->Next();
                            if(node2->GetNode("renderproperties"))
                            {
                                csRef<iDocumentNodeIterator> nodeItr4 = node2->GetNode("renderproperties")->GetNodes("shadervar");
                                while(nodeItr4->HasNext())
                                {
                                    csRef<iDocumentNode> node3 = nodeItr4->Next();
                                    if(csString("texture").Compare(node3->GetAttributeValue("type")))
                                    {
                                        for(size_t i=0; i<textures.GetSize(); i++)
                                        {
                                            if(textures[i]->name.Compare(node3->GetContentsValue()))
                                            {
                                                m->textures.PushSmart(textures[i]);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    s->meshes.Push(m);
                }

                nodeItr2 = node->GetNodes("portals");
                while(nodeItr2->HasNext())
                {
                    csRef<iDocumentNodeIterator> nodeItr3 = nodeItr2->Next()->GetNodes("portal");
                    while(nodeItr3->HasNext())
                    {
                        csRef<iDocumentNode> node2 = nodeItr3->Next();
                        csRef<Portal> p = csPtr<Portal>(new Portal(node2->GetAttributeValue("name")));

                        if(node2->GetNode("ww"))
                        {
                            node2 = node2->GetNode("ww");
                            p->ww = csVector3(node2->GetAttributeValueAsFloat("x"),
                                node2->GetAttributeValueAsFloat("y"), node2->GetAttributeValueAsFloat("z"));
                            node2 = node2->GetParent();
                        }

                        csRef<iDocumentNodeIterator> nodeItr3 = node2->GetNodes("v");
                        p->num_vertices = (int)nodeItr3->GetEndPosition()-2;
                        p->vertices = new csVector3[p->num_vertices];
                        int i = 0;
                        while(nodeItr3->HasNext())
                        {
                            node2 = nodeItr3->Next();
                            p->vertices[i++] = csVector3(node2->GetAttributeValueAsFloat("x"),
                                node2->GetAttributeValueAsFloat("y"), node2->GetAttributeValueAsFloat("z"));
                            node2 = node2->GetParent();
                        }

                        csString targetSector = node2->GetNode("sector")->GetContentsValue();
                        for(size_t i=0; i<sectors.GetSize(); i++)
                        {
                            if(targetSector == sectors[i]->name)
                            {
                                p->targetSector = sectors[i];
                                break;
                            }
                        }

                        if(!p->targetSector.IsValid())
                        {
                            p->targetSector = csPtr<Sector>(new Sector(targetSector));
                            sectors.Push(p->targetSector);
                        }

                        s->portals.Push(p);
                    }
                }

                nodeItr2 = node->GetNodes("light");
                while(nodeItr2->HasNext())
                {
                    node = nodeItr2->Next();
                    csRef<Light> l = csPtr<Light>(new Light(node->GetAttributeValue("name")));

                    if(node->GetNode("attenuation"))
                    {
                        node = node->GetNode("attenuation");
                        if(csString("none").Compare(node->GetContentsValue()))
                        {
                            l->attenuation = CS_ATTN_NONE;
                        }
                        else if(csString("linear").Compare(node->GetContentsValue()))
                        {
                            l->attenuation = CS_ATTN_LINEAR;
                        }
                        else if(csString("inverse").Compare(node->GetContentsValue()))
                        {
                            l->attenuation = CS_ATTN_INVERSE;
                        }
                        else if(csString("realistic").Compare(node->GetContentsValue()))
                        {
                            l->attenuation = CS_ATTN_REALISTIC;
                        }
                        else if(csString("clq").Compare(node->GetContentsValue()))
                        {
                            l->attenuation = CS_ATTN_CLQ;
                        }

                        node = node->GetParent();
                    }
                    else
                    {
                        l->attenuation = CS_ATTN_LINEAR;
                    }

                    if(node->GetNode("dynamic"))
                    {
                        l->dynamic = CS_LIGHT_DYNAMICTYPE_PSEUDO;
                    }
                    else
                    {
                        l->dynamic = CS_LIGHT_DYNAMICTYPE_STATIC;
                    }

                    l->type = CS_LIGHT_POINTLIGHT;

                    node = node->GetNode("center");
                    l->pos = csVector3(node->GetAttributeValueAsFloat("x"),
                        node->GetAttributeValueAsFloat("y"), node->GetAttributeValueAsFloat("z"));
                    node = node->GetParent();

                    l->radius = node->GetNode("radius")->GetContentsValueAsFloat();

                    node = node->GetNode("color");
                    l->colour = csColor(node->GetAttributeValueAsFloat("red"),
                            node->GetAttributeValueAsFloat("green"), node->GetAttributeValueAsFloat("blue"));
                    node = node->GetParent();

                    s->lights.Push(l);
                    node = node->GetParent();
                }
            }
        }
    }
}

void Loader::UpdatePosition(const csVector3& pos, const char* sectorName, bool force)
{
    // Check already loading meshes.
    for(size_t i=0; i<loadingMeshes.GetSize(); i++)
    {
        LoadMesh(loadingMeshes[i]);
    }

    if(!force)
    {
        // Check if we've moved.
        if(lastSector.IsValid() && lastSector->name.Compare(sectorName) && csVector3(lastPos - pos).Norm() < loadRange/10)
        {
            return;
        }
    }

    csRef<Sector> sector;
    // Hack to work around the weird sector stuff we do.
    if(csString("SectorWhereWeKeepEntitiesResidingInUnloadedMaps").Compare(sectorName))
    {
        sector = lastSector;
    }

    for(size_t i=0; !sector.IsValid() && i<sectors.GetSize(); i++)
    {
        if(sectors[i]->name.Compare(sectorName))
        {
            sector = sectors[i];
            break;
        }
    }

    if(sector.IsValid())
    {
        LoadSector(pos, sector);
        if(lastSector != sector)
        {
            CleanDisconnectedSectors(sector);
        }
        lastPos = pos;
        lastSector = sector;
    }
}

void Loader::CleanDisconnectedSectors(Sector* sector)
{
    // Create a list of connectedSectors;
    csRefArray<Sector> connectedSectors;
    FindConnectedSectors(connectedSectors, sector);

    // Check for disconnected sectors.
    for(size_t i=0; i<sectors.GetSize(); i++)
    {
        if(sectors[i]->object.IsValid() && connectedSectors.Find(sectors[i]) == csArrayItemNotFound)
        {
            CleanSector(sectors[i]);
        }
    }
}

void Loader::FindConnectedSectors(csRefArray<Sector>& connectedSectors, Sector* sector)
{
    if(connectedSectors.Find(sector) != csArrayItemNotFound)
    {
        return;
    }

    connectedSectors.Push(sector);

    for(size_t i=0; i<sector->activePortals.GetSize(); i++)
    {
        FindConnectedSectors(connectedSectors, sector->activePortals[i]->targetSector);
    }
}

void Loader::CleanSector(Sector* sector)
{
    for(size_t i=0; i<sector->meshes.GetSize(); i++)
    {
        if(sector->meshes[i]->object.IsValid())
        {
            sector->meshes[i]->object->GetMovable()->ClearSectors();
            sector->meshes[i]->object->GetMovable()->UpdateMove();
            engine->GetMeshes()->Remove(sector->meshes[i]->object);
            sector->meshes[i]->object.Invalidate();
            --sector->objectCount;
        }
    }

    for(size_t i=0; i<sector->portals.GetSize(); i++)
    {
        if(sector->portals[i]->mObject.IsValid())
        {
            engine->GetMeshes()->Remove(sector->portals[i]->mObject);
            sector->portals[i]->pObject = NULL;
            sector->portals[i]->mObject.Invalidate();
            sector->activePortals.Delete(sector->portals[i]);
            --sector->objectCount;
        }
    }

    for(size_t i=0; i<sector->lights.GetSize(); i++)
    {
        if(sector->lights[i]->object.IsValid())
        {
            engine->RemoveLight(sector->lights[i]->object);
            sector->lights[i]->object.Invalidate();
            --sector->objectCount;
        }
    }

    CS_ASSERT_MSG("Error cleaning sector. Sector still has objects!", sector->objectCount == 0);
    CS_ASSERT_MSG("Error cleaning sector. Sector is invalid!", sector->object.IsValid());

    engine->GetSectors()->Remove(sector->object);
    sector->object.Invalidate();
}

void Loader::LoadSector(const csVector3& pos, Sector* sector)
{
    sector->isLoading = true;

    if(!sector->object.IsValid())
    {
        sector->object = engine->CreateSector(sector->name);
        sector->object->SetDynamicAmbientLight(sector->ambient);
        sector->object->SetVisibilityCullerPlugin(sector->culler);
    }

    // Check other sectors linked to by active portals.
    for(size_t i=0; i<sector->activePortals.GetSize(); i++)
    {
        if(!sector->activePortals[i]->targetSector->isLoading)
            LoadSector(pos, sector->activePortals[i]->targetSector);
    }

    for(size_t i=0; i<sector->meshes.GetSize(); i++)
    {
        if(!sector->meshes[i]->loading)
        {
            if(!sector->meshes[i]->object.IsValid() && csVector3(sector->meshes[i]->pos - pos).Norm() <= loadRange)
            {
                sector->meshes[i]->loading = true;
                loadingMeshes.Push(sector->meshes[i]);
                LoadMesh(sector->meshes[i]);
                ++sector->objectCount;
            }
            else if(sector->meshes[i]->object.IsValid() && csVector3(sector->meshes[i]->pos - pos).Norm() > loadRange*1.5)
            {
                sector->meshes[i]->object->GetMovable()->ClearSectors();
                sector->meshes[i]->object->GetMovable()->UpdateMove();
                engine->GetMeshes()->Remove(sector->meshes[i]->object);
                sector->meshes[i]->object.Invalidate();
                --sector->objectCount;
            }
        }
    }

    for(size_t i=0; i<sector->portals.GetSize(); i++)
    {
        if(!sector->portals[i]->mObject.IsValid() && sector->portals[i]->InRange(pos))
        {
            if(!sector->portals[i]->targetSector->isLoading)
            {
                LoadSector(pos, sector->portals[i]->targetSector);
            }

            sector->portals[i]->mObject = engine->CreatePortal(sector->portals[i]->name, sector->object,
                csVector3(0), sector->portals[i]->targetSector->object, sector->portals[i]->vertices,
                sector->portals[i]->num_vertices, sector->portals[i]->pObject);
            sector->activePortals.Push(sector->portals[i]);
            ++sector->objectCount;
        }
        else if(sector->portals[i]->mObject.IsValid() && sector->portals[i]->OutOfRange(pos))
        {
            if(!sector->portals[i]->targetSector->isLoading)
            {
                LoadSector(pos, sector->portals[i]->targetSector);
            }

            engine->GetMeshes()->Remove(sector->portals[i]->mObject);
            sector->portals[i]->pObject = NULL;
            sector->portals[i]->mObject.Invalidate();
            sector->activePortals.Delete(sector->portals[i]);
            --sector->objectCount;
        }
    }

    for(size_t i=0; i<sector->lights.GetSize(); i++)
    {
        if(!sector->lights[i]->object.IsValid() && csVector3(sector->lights[i]->pos - pos).Norm() <= loadRange)
        {
            sector->lights[i]->object = engine->CreateLight(sector->lights[i]->name, sector->lights[i]->pos,
                sector->lights[i]->radius, sector->lights[i]->colour, sector->lights[i]->dynamic);
            sector->lights[i]->object->SetAttenuationMode(sector->lights[i]->attenuation);
            sector->lights[i]->object->SetType(sector->lights[i]->type);
            sector->object->AddLight(sector->lights[i]->object);
            ++sector->objectCount;
        }
        else if(sector->lights[i]->object.IsValid() && csVector3(sector->lights[i]->pos - pos).Norm() > loadRange*1.5)
        {
            engine->RemoveLight(sector->lights[i]->object);
            sector->lights[i]->object.Invalidate();
            --sector->objectCount;
        }
    }

    if(sector->objectCount == 0 && sector->object.IsValid())
    {
        engine->GetSectors()->Remove(sector->object);
        sector->object.Invalidate();
    }

    sector->isLoading = false;
}

void Loader::LoadMesh(MeshObj* mesh)
{
    bool ready = true;
    for(size_t i=0; i<mesh->meshfacts.GetSize(); i++)
    {
        ready &= LoadMeshFact(mesh->meshfacts[i]);
    }

    for(size_t i=0; i<mesh->materials.GetSize(); i++)
    {
        ready &= LoadMaterial(mesh->materials[i]);
    }

    for(size_t i=0; i<mesh->textures.GetSize(); i++)
    {
        ready &= LoadTexture(mesh->textures[i]);
    }

    if(ready && !mesh->status)
    {
        mesh->status = tloader->LoadNode(mesh->data);
    }

    if(mesh->status && mesh->status->IsFinished())
    {
        vfs->ChDir("/planeshift/maps/");
        mesh->object = scfQueryInterface<iMeshWrapper>(mesh->status->GetResultRefPtr());
        engine->SyncEngineListsNow(tloader);
        mesh->object->GetMovable()->SetSector(mesh->sector->object);
        mesh->object->GetMovable()->UpdateMove();
        engine->PrecacheMesh(mesh->object);
        csColliderHelper::InitializeCollisionWrapper(cdsys, mesh->object);
        loadingMeshes.Delete(mesh);
        mesh->loading = false;
    }
}

bool Loader::LoadMeshFact(MeshFact* meshfact)
{
    if(meshfact->loaded)
    {
        return true;
    }

    bool ready = true;
    for(size_t i=0; i<meshfact->materials.GetSize(); i++)
    {
        ready &= LoadMaterial(meshfact->materials[i]);
    }

    if(ready && !meshfact->status)
    {
        meshfact->status = tloader->LoadNode(meshfact->data);
        return false;
    }

    if(meshfact->status && meshfact->status->IsFinished())
    {
        meshfact->loaded = true;
        return true;
    }

    return false;
}

bool Loader::LoadMaterial(Material* material)
{
    if(material->loaded)
    {
        return true;
    }

    bool ready = true;
    for(size_t i=0; i<material->textures.GetSize(); i++)
    {
        ready &= LoadTexture(material->textures[i]);
    }

    if(ready)
    {
        csArray<csStringID> shadertypes;
        csArray<iShader*> shaderptrs;
        csRefArray<csShaderVariable> shadervars;

        csRef<iMaterial> mat (engine->CreateBaseMaterial(0));
        iMaterialWrapper* mw = engine->GetMaterialList()->NewMaterial(mat, material->name);

        for(size_t i=0; i<material->shaders.GetSize(); i++)
        {
            csRef<iShaderManager> shaderMgr = csQueryRegistry<iShaderManager> (object_reg);
            iShader* shader = shaderMgr->GetShader(material->shaders[i].name);
            csStringID type = strings->Request(material->shaders[i].type);
            mat->SetShader(type, shader);
        }

        for(size_t i=0; i<material->shadervars.GetSize(); i++)
        {
            if(material->shadervars[i].type == csShaderVariable::TEXTURE)
            {
                for(size_t j=0; j<material->textures.GetSize(); j++)
                {
                    if(material->textures[j]->name.Compare(material->shadervars[i].value))
                    {
                        csShaderVariable* var = mat->GetVariableAdd(svstrings->Request(material->shadervars[i].name));
                        var->SetType(material->shadervars[i].type);
                        csRef<iTextureWrapper> tex = scfQueryInterface<iTextureWrapper>(material->textures[j]->status->GetResultRefPtr());
                        var->SetValue(tex);
                        break;
                    }
                }
            }
            else if(material->shadervars[i].type == csShaderVariable::VECTOR2)
            {
                csShaderVariable* var = mat->GetVariableAdd(svstrings->Request(material->shadervars[i].name));
                var->SetType(material->shadervars[i].type);
                var->SetValue(material->shadervars[i].vec2);
            }
        }

        material->loaded = true;
        return true;
    }

    return false;
}

bool Loader::LoadTexture(Texture* texture)
{
    if(texture->loaded)
    {
        return true;
    }

    if(!texture->status.IsValid())
    {
        texture->status = tloader->LoadNode(texture->data);
        return false;
    }

    if(!texture->status->IsFinished())
    {
        return false;
    }

    texture->loaded = true;
    return true;
}

iMaterialWrapper* Loader::LoadMaterial(const char *name, const char *filename)
{
    iMaterialWrapper* materialWrap = engine->GetMaterialList()->FindByName(name);
    if(!materialWrap)
    {
        // Check that the texture exists.
        if(!vfs->Exists(filename))
            return NULL;

        // Load base texture.
        iTextureWrapper* texture = LoadTexture(name, filename);

        // Load base material.
        csRef<iMaterial> material (engine->CreateBaseMaterial(texture));
        materialWrap = engine->GetMaterialList()->NewMaterial(material, name);

        // Check for shader maps.
        if(gfxFeatures & useAdvancedShaders)
        {
            csString shadermapBase = filename;
            shadermapBase.Truncate(shadermapBase.Length()-4);

            // Normal map
            csString shadermap = shadermapBase;
            shadermap.Append("_n.dds");
            if(vfs->Exists(shadermap))
            {
                iTextureWrapper* t = LoadTexture(shadermap, shadermap, "normalmap");
                csShaderVariable* shadervar = new csShaderVariable();
                shadervar->SetName(svstrings->Request("tex normal compressed"));
                shadervar->SetValue(t);
                material->AddVariable(shadervar);
            }

            // Height map
            shadermap = shadermapBase;
            shadermap.Append("_h.dds");
            if(vfs->Exists(shadermap))
            {
                iTextureWrapper* t = LoadTexture(shadermap, shadermap);
                csShaderVariable* shadervar = new csShaderVariable();
                shadervar->SetName(svstrings->Request("tex height"));
                shadervar->SetValue(t);
                material->AddVariable(shadervar);
            }

            // Spec map
            shadermap = shadermapBase;
            shadermap.Append("_s.dds");
            if(vfs->Exists(shadermap))
            {
                iTextureWrapper* t = LoadTexture(shadermap, shadermap);
                csShaderVariable* shadervar = new csShaderVariable();
                shadervar->SetName(svstrings->Request("tex specular"));
                shadervar->SetValue(t);
                material->AddVariable(shadervar);
            }

            // Gloss map
            shadermap = shadermapBase;
            shadermap.Append("_sgdds");
            if(vfs->Exists(shadermap))
            {
                iTextureWrapper* t = LoadTexture(shadermap, shadermap);
                csShaderVariable* shadervar = new csShaderVariable();
                shadervar->SetName(svstrings->Request("tex gloss"));
                shadervar->SetValue(t);
                material->AddVariable(shadervar);
            }

            // AO map
            shadermap = shadermapBase;
            shadermap.Append("_ao.dds");
            if(vfs->Exists(shadermap))
            {
                iTextureWrapper* t = LoadTexture(shadermap, shadermap);
                csShaderVariable* shadervar = new csShaderVariable();
                shadervar->SetName(svstrings->Request("tex ambient occlusion"));
                shadervar->SetValue(t);
                material->AddVariable(shadervar);
            }
        }
    }
    return materialWrap;
}

iTextureWrapper* Loader::LoadTexture(const char *name, const char *filename, const char* className)
{
    // name is the material name; blah.dds
    // filename will be /planeshift/blah/blah.dds
    csString tempName;
    if(!name)
    {
        tempName = filename;
        size_t last = tempName.FindLast('/');
        tempName.DeleteAt(0, last+1);
        name = tempName.GetData();
    }

    csRef<iTextureWrapper> texture = engine->GetTextureList()->FindByName(name);

    if(!texture)
    {
        csRef<iThreadReturn> itr = tloader->LoadTexture(name, filename, CS_TEXTURE_3D, txtmgr, true, false);
        itr->Wait();
        texture = scfQueryInterfaceSafe<iTextureWrapper>(itr->GetResultRefPtr());
        if(className)
        {
          texture->SetTextureClass(className);
        }
        engine->SyncEngineListsNow(tloader);
    }

    if (!texture)
    {
        csReport (object_reg, CS_REPORTER_SEVERITY_ERROR,
            "planeshift.engine.celbase",
            "Error loading texture '%s'!",
            name);
        return false;
    }
    return texture;
}

bool Loader::LoadTextureDir(const char *dir)
{
    csRef<iDataBuffer> xpath = vfs->ExpandPath(dir);
    csRef<iStringArray> files = vfs->FindFiles( **xpath );

    if (!files)
        return false;

    for (size_t i=0; i < files->GetSize(); i++)
    {
        const char* filename = files->Get(i);
        if (strcmp (filename + strlen(filename) - 4, ".png") && 
            strcmp (filename + strlen(filename) - 4, ".tga") && 
            strcmp (filename + strlen(filename) - 4, ".gif") && 
            strcmp (filename + strlen(filename) - 4, ".bmp") && 
            strcmp (filename + strlen(filename) - 4, ".jpg") &&
            strcmp (filename + strlen(filename) - 4, ".dds"))
            continue;

        // If this is an icon or shader map texture then we don't load as a material.
        if(strstr(filename, "_icon" ) || strstr(filename, "_n." ) || strstr(filename, "_h." ) ||
          strstr(filename, "_s." ) || strstr(filename, "_g." ) || strstr(filename, "_ao." ))
            continue;
        
        const char* name = csStrNew(filename);
        const char* onlyname = PS_GetFileName(name);

        if (!LoadMaterial(onlyname,filename))
        {
            delete[] name;
            return false;
        }
        delete[] name;
    }
    return true;
}

bool Loader::PreloadTextures()
{
    // characters
    if (!LoadTextureDir("/planeshift/models/"))
        return false;

    // Load the textures for the weapons.
    if (!LoadTextureDir("/planeshift/weapons/"))
        return false;

    if (!LoadTextureDir("/planeshift/shields/"))
        return false;
        
    // Load the textures for the items.
    if (!LoadTextureDir("/planeshift/items/"))
        return false;

    // Load the textures for the spell effects
    if (!LoadTextureDir("/planeshift/art/effects/"))
        return false;

    // Load the textures for the resources items
    if (!LoadTextureDir("/planeshift/naturalres/"))
        return false;

    // Load the textures for the tools items
    if (!LoadTextureDir("/planeshift/tools/"))
        return false;

    // Load the textures for the food items
    if (!LoadTextureDir("/planeshift/food/"))
        return false;

    engine->SyncEngineListsNow(tloader);

    return true;
}
