/*
* Author: Andrew Robberts
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

#include <csutil/xmltiny.h>
#include <iengine/engine.h>
#include <iengine/material.h>
#include <iengine/mesh.h>
#include <iengine/movable.h>
#include <imap/loader.h>
#include <iengine/region.h>
#include <imesh/objmodel.h>
#include <csutil/cscolor.h>
#include <csutil/flags.h>

#include "effects/pseffectobjmesh.h"
#include "effects/pseffectanchor.h"
#include "effects/pseffect2drenderer.h"

#include "util/pscssetup.h"
#include "util/log.h"

psEffectObjMesh::psEffectObjMesh(iView * parentView, psEffect2DRenderer * renderer2d)
                :psEffectObj(parentView, renderer2d)
{
}

psEffectObjMesh::~psEffectObjMesh()
{
}

bool psEffectObjMesh::Load(iDocumentNode *node)
{
    // get the attributes
    name.Clear();
    materialName.Clear();
    factName.Clear();
    csRef<iDocumentAttributeIterator> attribIter = node->GetAttributes();
    while (attribIter->HasNext())
    {
        csRef<iDocumentAttribute> attr = attribIter->Next();
        csString attrName = attr->GetName();
        attrName.Downcase();
        if (attrName == "name")
            name = attr->GetValue();
        else if (attrName == "material")
            materialName = attr->GetValue();
        else if (attrName == "fact")
            factName = attr->GetValue();
    }
    if (name.IsEmpty())
    {
        csReport(psCSSetup::object_reg, CS_REPORTER_SEVERITY_ERROR, "planeshift_effects", "Attempting to create an effect obj with no name.\n");
        return false;
    }
    
    if (!psEffectObj::Load(node))
        return false;

    return PostSetup();
}

bool psEffectObjMesh::Render(const csVector3 &up)
{
    static unsigned long nextUniqueID = 0;
    csString effectID = "effect_mesh_";
    effectID += nextUniqueID++;

    // create a mesh wrapper from the factory we just created
    mesh = engine->CreateMeshWrapper(meshFact, effectID.GetData());

    // do the up vector
    objUp = up;
    csReversibleTransform rt;
    rt.LookAt(up, csVector3(1,2,0));
    matUp = rt.GetT2O();
    matBase = matUp;

    // common flags
    mesh->GetFlags().Set(CS_ENTITY_NOHITBEAM);
    mesh->SetZBufMode(zFunc);
    mesh->SetRenderPriority(priority);

    // disable culling
    csRef<iStringSet> globalStringSet = csQueryRegistryTagInterface<iStringSet> 
        (psCSSetup::object_reg, "crystalspace.shared.stringset");
    csStringID viscull_id = globalStringSet->Request ("viscull");
    mesh->GetMeshObject()->GetObjectModel()->SetTriangleData(viscull_id, 0);

    // add the custom material if set
    if (!materialName.IsEmpty())
    {
        csRef<iMaterialWrapper> mat = effectsCollection->FindMaterial(materialName);
        if (mat != 0)
        {
            mesh->GetMeshObject()->SetMaterialWrapper(mat);
        }
    }

    // obj specific    
    sprState =  scfQueryInterface<iSprite3DState> (mesh->GetMeshObject());

    if (sprState)
    {
        sprState->EnableTweening(true);
        sprState->SetAction("default");
        sprState->SetLighting(false);

        if (mixmode != CS_FX_ALPHA)
        {
            sprState->SetMixMode(mixmode);
        }
    }
    
    mesh->GetMeshObject()->SetColor(csColor(1.0f, 1.0f, 1.0f));
    
    return true;
}

bool psEffectObjMesh::Update(csTicks elapsed)
{
    if (!anchor || !anchor->IsReady()) // wait for anchor to be ready
        return true;

    if (!psEffectObj::Update(elapsed))
        return false;

    if (keyFrames->GetSize() == 0)
        return true;
    
    // COLOUR
    csVector3 lerpColour = LERP_VEC_KEY(KA_COLOUR);
    mesh->GetMeshObject()->SetColor(csColor(lerpColour.x, lerpColour.y, lerpColour.z));
        
    // ALPHA
    if (mixmode == CS_FX_ALPHA)
    {
        float lerpAlpha = LERP_KEY(KA_ALPHA);
        sprState->SetMixMode(CS_FX_SETALPHA(lerpAlpha));
    }

    return true;
}

psEffectObj *psEffectObjMesh::Clone() const
{
    psEffectObjMesh *newObj = new psEffectObjMesh(view, renderer2d);
    CloneBase(newObj);

    // mesh specific
    newObj->factName = factName;
    
    return newObj;
}

bool psEffectObjMesh::PostSetup()
{
    static unsigned int uniqueID = 0;
    csString facName = "effect_mesh_fac_";
    facName += uniqueID++;
    
    meshFact = effectsCollection->FindMeshFactory(factName);
    if (!meshFact)
    {
        csReport(psCSSetup::object_reg, CS_REPORTER_SEVERITY_ERROR, "planeshift_effects", "Couldn't find mesh factory %s in effect %s\n", factName.GetData(), name.GetData());
        return false;
    }

    // create the actual sprite3d data
    iMeshObjectFactory* fact = meshFact->GetMeshObjectFactory();
    csRef<iSprite3DFactoryState> facState =  scfQueryInterface<iSprite3DFactoryState> (fact);

    return true;
}
