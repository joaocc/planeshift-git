#include <psconfig.h>
//=============================================================================
// Crystal Space Includes
//=============================================================================
#include <iengine/engine.h>
#include <iengine/material.h>
#include <iengine/region.h>
#include <iengine/scenenode.h>
#include <imap/loader.h>
#include <imesh/object.h>
#include <iutil/object.h>
#include <ivaria/keyval.h>
#include <ivideo/shader/shader.h>

//=============================================================================
// Project Includes
//=============================================================================
#include "util/log.h"
#include "util/psstring.h"
#include "effects/pseffect.h"
#include "effects/pseffectmanager.h"
#include "engine/materialmanager.h"

//=============================================================================
// Local Includes
//=============================================================================
#include "charapp.h"
#include "psengine.h"
#include "clientcachemanager.h"
#include "pscelclient.h"
#include "psclientchar.h"
#include "globals.h"


psCharAppearance::psCharAppearance(iObjectRegistry* objectReg)
{
    stringSet = csQueryRegistryTagInterface<iStringSet>(objectReg, "crystalspace.shared.stringset");
    engine = csQueryRegistry<iEngine>(objectReg);
    loader = csQueryRegistry<iLoader>(objectReg);
    vfs    = csQueryRegistry<iVFS>(objectReg);
    g3d    = csQueryRegistry<iGraphics3D>(objectReg);
    txtmgr = g3d->GetTextureManager();        
    xmlparser =  csQueryRegistry<iDocumentSystem> (objectReg);
    
    hairMesh = "Hair";
    beardMesh = "Beard";
    hairAttached = true;    
    colorSet = false;   
    
    state = NULL;
    stateFactory = NULL;
}

psCharAppearance::~psCharAppearance()
{
    
}

void psCharAppearance::SetMesh(iMeshWrapper* mesh)
{
    state = scfQueryInterface<iSpriteCal3DState>(mesh->GetMeshObject());
    stateFactory = scfQueryInterface<iSpriteCal3DFactoryState>(mesh->GetMeshObject()->GetFactory());
    
    baseMesh = mesh;
}


csString psCharAppearance::ParseStrings(const char* part, const char* str) const
{
    psString result(str);
    
    const char* factname = baseMesh->GetFactory()->QueryObject()->GetName();
    
    result.ReplaceAllSubString("$F", factname);
    result.ReplaceAllSubString("$P", part);
    
    return result;
}


void psCharAppearance::FaceTexture(csString& faceMaterial, csString& faceTexture)
{
    csString materialParsed = ParseStrings("", faceMaterial);
    csString textureParsed  = ParseStrings("", faceTexture);
    
    iMaterialWrapper* material = MaterialManager::GetSingletonPtr()->MissingMaterial(materialParsed, textureParsed);
    
    if ( !material )
    {
            Notify3(LOG_CHARACTER, "Failed to load texture ( %s, %s )", faceMaterial.GetData(), faceTexture.GetData());
            return;
    }
    else
    {
        if ( state )
        {
            state->SetMaterial("Head", material);        
        }
    }    

}

void psCharAppearance::BeardMesh(csString& subMesh)
{
    beardMesh = subMesh;
    
    if ( beardMesh.Length() == 0 )
    {
        for ( int idx=0; idx < stateFactory->GetMeshCount(); idx++)
        {
            const char* meshName = stateFactory->GetMeshName(idx);
            
            if ( strstr(meshName, "Beard") )
            {
                state->DetachCoreMesh(meshName);
            }
        }
        return;        
    }
    
    csString newPartParsed = ParseStrings("Beard", beardMesh);
    
    int newMeshAvailable = stateFactory->FindMeshName(newPartParsed);
    if ( newMeshAvailable == -1 )
    {
        return;
    }
    else
    {
        for ( int idx=0; idx < stateFactory->GetMeshCount(); idx++)
        {
            const char* meshName = stateFactory->GetMeshName(idx);
            
            if ( strstr(meshName, "Beard") )
            {
                state->DetachCoreMesh(meshName);
            }
        }
        
        state->AttachCoreMesh(newPartParsed);
        beardAttached = true;
        beardMesh = newPartParsed;
    }
    
    if ( colorSet )
        HairColor(hairShader);    
}

void psCharAppearance::HairMesh(csString& subMesh)
{   
    hairMesh = subMesh;
    
    if ( hairMesh.Length() == 0 )
    {
        hairMesh = "Hair";
    }
    
    csString newPartParsed = ParseStrings("Hair", hairMesh);
    
    int newMeshAvailable = stateFactory->FindMeshName(newPartParsed);
    if ( newMeshAvailable == -1 )
    {
        return;
    }
    else
    {
        for ( int idx=0; idx < stateFactory->GetMeshCount(); idx++)
        {
            const char* meshName = stateFactory->GetMeshName(idx);
            
            if ( strstr(meshName, "Hair") )
            {
                state->DetachCoreMesh(meshName);
            }
        }
        
        state->AttachCoreMesh(newPartParsed);
        hairAttached = true;
        hairMesh = newPartParsed;
    }
    
    if ( colorSet )
        HairColor(hairShader);    
}


void psCharAppearance::HairColor(csVector3& color)
{
    if ( hairMesh.Length() == 0 )
    {
        return;
    }
    else
    {        
        hairShader = color;
        iShaderVariableContext* context_hair = state->GetCoreMeshShaderVarContext(hairMesh);
        iShaderVariableContext* context_beard = state->GetCoreMeshShaderVarContext(beardMesh);
    
        if ( context_hair )
        {
            csStringID varName = stringSet->Request("colorize");
            csShaderVariable* var = context_hair->GetVariableAdd(varName);
        
            if ( var )
            {
                var->SetValue(hairShader);
            }
        }
        
        if ( context_beard )
        {
            csStringID varName = stringSet->Request("colorize");
            csShaderVariable* var = context_beard->GetVariableAdd(varName);
        
            if ( var )
            {
                var->SetValue(hairShader);
            }
        }
        colorSet = true;
    }        
}


void psCharAppearance::ShowHair(bool show)
{
    if ( show && hairAttached )
    {
        return;
    }
    else if ( show == false )
    {
        state->DetachCoreMesh(hairMesh);
        hairAttached = false;
    }
    else if ( show == true )
    {            
        state->AttachCoreMesh(hairMesh);        
        
        if (colorSet)
            HairColor(hairShader);
            
        hairAttached = true;
    }
}

void psCharAppearance::SetSkinTone(csString& part, csString& material, csString& texture)
{
    if (!baseMesh || !part || !material || !texture)
    {
        return;
    }        
    else
    {
        SkinToneSet s;
        s.part = part;
        s.material = material;
        s.texture = texture;
    
        skinToneSet.Push(s);
       
        csString materialNameParsed    = ParseStrings(part, material);
        csString textureNameParsed     = ParseStrings(part, texture);

        iMaterialWrapper* material = MaterialManager::GetSingletonPtr()->MissingMaterial(materialNameParsed, textureNameParsed );
        if ( !material )
        {
            // Not necisarily an error; this texture may just not exist for this character, yet
            Notify3(LOG_CHARACTER,"Failed to load texture \"%s\" for part \"%s\"",textureNameParsed.GetData(),part.GetData());
            return;
        }

        if ( !state->SetMaterial(part,material) )
        {
            csString left,right;
            left.Format("Left %s",part.GetData());
            right.Format("Right %s",part.GetData());
    
            // Try mirroring
            if ( !state->SetMaterial(left,material) || !state->SetMaterial(right,material) )
            {
                Error3("Failed to set material \"%s\" on part \"%s\"",materialNameParsed.GetData(),part.GetData());
                return;
            }
        }
    }    
}


void psCharAppearance::ApplyEquipment(csString& equipment)
{
    if ( equipment.Length() == 0 )
    {
        return;
    }
    
    csRef<iDocument> doc = xmlparser->CreateDocument();

    const char* error = doc->Parse(equipment);
    if ( error )
    {
        Error2("Error in XML: %s", error );
        return;
    }
    
    // Do the helm check.
    csRef<iDocumentNode> helmNode = doc->GetRoot()->GetNode("equiplist")->GetNode("helm");    
    csString helmGroup(helmNode->GetContentsValue());
    if ( helmGroup.Length() == 0 )
        helmGroup = baseMesh->GetFactory()->QueryObject()->GetName();
    
    csRef<iDocumentNodeIterator> equipIter = doc->GetRoot()->GetNode("equiplist")->GetNodes("equip");
          
    while (equipIter->HasNext())
    {
        csRef<iDocumentNode> equipNode = equipIter->Next();
        csString slot = equipNode->GetAttributeValue( "slot" );
        csString mesh = equipNode->GetAttributeValue( "mesh" );
        csString part = equipNode->GetAttributeValue( "part" );
        csString partMesh = equipNode->GetAttributeValue("partMesh");
        csString texture = equipNode->GetAttributeValue( "texture" );
        
        // If this is a head item check for helm replacement
        if ( slot == "head" )
        {
            psString result(mesh);                        
            result.ReplaceAllSubString("$H",helmGroup);                                                    
            mesh = result;        
        }            
       
        Equip(slot, mesh, part, partMesh, texture);                        
    }
    
    return;
}


void psCharAppearance::Equip( csString& slotname,
                              csString& mesh,
                              csString& part,
                              csString& subMesh,
                              csString& texture
                             )
{ 
    
    if ( slotname == "head" )
    {
        ShowHair(false);
    }
    
    // If it's a new mesh attach that mesh.
    if ( mesh.Length() )
    {
        Attach(slotname, mesh);
    }

    // Set up item effect if there is one.
    if(state->FindSocket(slotname))
    {
        psengine->GetCelClient()->HandleItemEffect(mesh, state->FindSocket(slotname)->GetMeshWrapper(), false, slotname, &effectids);
    }
    
    // This is a subMesh on the model change so change the mesh for that part.
    if ( subMesh.Length() )
    {
        // Change the mesh on the part of the model.
        ChangeMesh(part, subMesh);
        
        // If there is also a new material ( texture ) then place that on as well.
        if ( texture.Length() )
        {
            ChangeMaterial( ParseStrings(part,subMesh),texture, texture);
        }
    }
    else
    {
        if ( part.Length() )
        {
            ChangeMaterial(part, texture, texture);
        }
    }          
}


bool psCharAppearance::Dequip(csString& slotname,
                              csString& mesh,
                              csString& part,
                              csString& subMesh,
                              csString& texture)
{  
    if ( slotname == "head" )
    {
         ShowHair(true);
    }

    if ( mesh.Length() )
    {
        Detach(slotname);
    }

    // This is a part mesh (ie Mesh) set default mesh for that part.

    if ( subMesh.Length() )
    {
        DefaultMesh(part); 
    }

    if ( part.Length() )
    {
        if ( texture.Length() )
        {
            ChangeMaterial(part, texture, texture);
        }
        else            
        {
            DefaultMaterial(part);
        }            
        DefaultMaterial(part);
    }

    ClearEquipment(slotname);

    return true;
}


void psCharAppearance::DefaultMesh(const char* part)
{
    const char * defaultPart = NULL;
    /* First we detach every mesh that match the partPattern */
    for (int idx=0; idx < stateFactory->GetMeshCount(); idx++)
    {
        const char * meshName = stateFactory->GetMeshName( idx );
        if (strstr(meshName, part))
        {
            state->DetachCoreMesh( meshName );
            if (stateFactory->IsMeshDefault(idx))
            {
                defaultPart = meshName;
            }
        }
    }

    if (!defaultPart) 
    {
        return;
    }
    
    state->AttachCoreMesh( defaultPart );
}


bool psCharAppearance::ChangeMaterial(const char* part, const char* meshName, const char* textureName )
{
    if ( !part || !meshName || !textureName)
        return false;
    
    csString meshNameParsed    = ParseStrings(part, meshName);
    csString textureNameParsed = ParseStrings(part, textureName);

    iMaterialWrapper* material = MaterialManager::GetSingletonPtr()->MissingMaterial( meshNameParsed, textureNameParsed );
    if ( !material )
    {
        // Not necisarily an error; this texture may just not exist for this character, yet
        Notify3(LOG_CHARACTER,"Failed to load texture \"%s\" for part \"%s\"",textureNameParsed.GetData(),part);
        return false;
    }

    if ( !state->SetMaterial(part,material) )
    {
        csString left,right;
        left.Format("Left %s",part);
        right.Format("Right %s",part);

        // Try mirroring
        if ( !state->SetMaterial(left,material) || !state->SetMaterial(right,material) )
        {
             Error3("Failed to set material \"%s\" on part \"%s\"",meshNameParsed.GetData(),part);
             return false;
        }
    }

    return true;
}


bool psCharAppearance::ChangeMesh(const char* partPattern, const char* newPart)
{   
    csString newPartParsed = ParseStrings(partPattern, newPart);

    // If the new mesh cannot be found then do nothing.   
    int newMeshAvailable = stateFactory->FindMeshName(newPartParsed);
    if ( newMeshAvailable == -1 )
        return false;
    
    /* First we detach every mesh that match the partPattern */
    for (int idx=0; idx < stateFactory->GetMeshCount(); idx++)
    {
        const char * meshName = stateFactory->GetMeshName( idx );
        if (strstr(meshName,partPattern))
        {     
            state->DetachCoreMesh( meshName );
        }
    }
    
    state->AttachCoreMesh( newPartParsed.GetData() );
    return true;
}


bool psCharAppearance::Attach(const char* socketName, const char* meshFactName )
{
    if (!socketName || !meshFactName)
        return false;

    
    csRef<iSpriteCal3DSocket> socket = state->FindSocket( socketName );
    if ( !socket )
    {
        Notify2(LOG_CHARACTER, "Socket %s not found.", socketName );
        return false;
    }

    csRef<iMeshFactoryWrapper> factory = engine->GetMeshFactories()->FindByName (meshFactName);
    if ( !factory )
    {
        // Try loading the mesh again
        csString filename;
        if (!psengine->GetFileNameByFact(meshFactName, filename))
        {
            Error2("Mesh Factory %s not found", meshFactName );            
            return false;
        }
        psengine->GetCacheManager()->LoadNewFactory(filename);
        factory = psengine->GetEngine()->GetMeshFactories()->FindByName (meshFactName);      
        if (!factory)
        {
            Error2("Mesh Factory %s not found", meshFactName );
            return false;
        }
    }

    csRef<iMeshWrapper> meshWrap = engine->CreateMeshWrapper( factory, meshFactName );

    // Given a socket name of "righthand", we're looking for a key in the form of "socket_righthand"
    char * keyName = (char *)malloc(strlen(socketName)+strlen("socket_")+1);
    strcpy(keyName,"socket_");
    strcat(keyName,socketName);

    // Variables for transform to be specified
    float trans_x = 0, trans_y = 0.0, trans_z = 0, rot_x = -PI/2, rot_y = 0, rot_z = 0;
    csRef<iObjectIterator> it = factory->QueryObject()->GetIterator();

    while ( it->HasNext() )
    {
        csRef<iKeyValuePair> key ( scfQueryInterface<iKeyValuePair> (it->Next()));
        if (key && strcmp(key->GetKey(),keyName) == 0)
        {
            sscanf(key->GetValue(),"%f,%f,%f,%f,%f,%f",&trans_x,&trans_y,&trans_z,&rot_x,&rot_y,&rot_z);
        }
    }

    free(keyName);
    keyName = NULL;

    meshWrap->QuerySceneNode()->SetParent( baseMesh->QuerySceneNode ());
    socket->SetMeshWrapper( meshWrap );
    socket->SetTransform( csTransform(csZRotMatrix3(rot_z)*csYRotMatrix3(rot_y)*csXRotMatrix3(rot_x), csVector3(trans_x,trans_y,trans_z)) );

    usedSlots.PushSmart(socketName);
    return true;
}



void psCharAppearance::ApplyTraits(csString& traitString)
{
    if ( traitString.Length() == 0 )
    {
        return;
    }
    
    csRef<iDocument> doc = xmlparser->CreateDocument();

    const char* traitError = doc->Parse(traitString);
    if ( traitError )
    {
        Error2("Error in XML: %s", traitError );
        return;

    }

    csRef<iDocumentNodeIterator> traitIter = doc->GetRoot()->GetNode("traits")->GetNodes("trait");

    csPDelArray<Trait> traits;

    // Build traits table
    while ( traitIter->HasNext() )
    {
        csRef<iDocumentNode> traitNode = traitIter->Next();

        Trait * trait = new Trait;
        trait->Load(traitNode);
        traits.Push(trait);
    }

    // Build next and prev pointers for trait sets
    csPDelArray<Trait>::Iterator iter = traits.GetIterator();
    while (iter.HasNext())
    {
        Trait * trait = iter.Next();

        csPDelArray<Trait>::Iterator iter2 = traits.GetIterator();
        while (iter2.HasNext())
        {
            Trait * trait2 = iter2.Next();
            if (trait->next_trait_uid == trait2->uid)
            {
                trait->next_trait = trait2;
                trait2->prev_trait = trait;
            }
        }
    }

    // Find top traits and set them on mesh
    csPDelArray<Trait>::Iterator iter3 = traits.GetIterator();
    while (iter3.HasNext())
    {
        Trait * trait = iter3.Next();
        if (trait->prev_trait == NULL)
        {                    
            if (!SetTrait(trait))
            {
                Error2("Failed to set trait %s for mesh.", traitString.GetData());
            }
        }
    }
    return;

}


bool psCharAppearance::SetTrait(Trait * trait)
{
    bool result = true;

    while (trait)
    {
        switch (trait->location)
        {
            case PSTRAIT_LOCATION_SKIN_TONE:
            {
                SetSkinTone(trait->mesh, trait->material, trait->texture);
                break;
            }
        
            case PSTRAIT_LOCATION_FACE:
            {
                FaceTexture(trait->material, trait->texture );
                break;            
            }
            
                        
            case PSTRAIT_LOCATION_HAIR_STYLE:
            {
                  HairMesh(trait->mesh);
                  break;
            }
            
            
            case PSTRAIT_LOCATION_BEARD_STYLE:
            {
                BeardMesh(trait->mesh);
                break;
            } 
                           

            case PSTRAIT_LOCATION_HAIR_COLOR:
            {
                HairColor(trait->shader);            
                break;
            }                
        

            default:
            {
                Error3("Trait(%d) unkown trait location %d",trait->uid,trait->location);
                result = false;
                break;
            }
        }
        trait = trait->next_trait;
    }

    return true;
}


void psCharAppearance::DefaultMaterial(csString& part)
{    
    bool torsoFound = false;
    
    for ( size_t z = 0; z < skinToneSet.GetSize(); z++ )
    {
        if ( skinToneSet[z].part == "Torso" )
        {
            torsoFound = true;
        }
        
        if ( part == skinToneSet[z].part )
        {
            ChangeMaterial(part, skinToneSet[z].material, skinToneSet[z].texture);
        }
    }
        
    if ( part == "Torso" && torsoFound == false)
    {                                
        ChangeMaterial(part, stateFactory->GetDefaultMaterial(part), stateFactory->GetDefaultMaterial(part));
    }        
}


void psCharAppearance::ClearEquipment(const char* slot)
{
    if(slot)
    {
        psengine->GetEffectManager()->DeleteEffect(effectids.Get(slot, 0));
        return;
    }

    csArray<csString> deleteList = usedSlots;
    
    for ( size_t z = 0; z < deleteList.GetSize(); z++ )    
    {
        Detach(deleteList[z]);
    }

    csHash<int, csString>::GlobalIterator itr = effectids.GetIterator();
    while(itr.HasNext())
    {
        psengine->GetEffectManager()->DeleteEffect(itr.Next());
        //effectids.DeleteElement(itr); -- Bugged in CS! :x
    }
}


bool psCharAppearance::Detach(const char* socketName )
{
    if (!socketName)
    {
        return false;
    }        

    
    csRef<iSpriteCal3DSocket> socket = state->FindSocket( socketName );
    if ( !socket )
    {
        Notify2(LOG_CHARACTER, "Socket %s not found.", socketName );
        return false;
    }

    csRef<iMeshWrapper> meshWrap = socket->GetMeshWrapper();
    if ( !meshWrap )
    {
        Notify2(LOG_CHARACTER, "No mesh in socket: %s.", socketName );
    }
    else
    {
        meshWrap->QuerySceneNode ()->SetParent (0);
        socket->SetMeshWrapper( NULL );
        engine->RemoveObject( meshWrap );
    }

    usedSlots.Delete(socketName);    
    return true;
}


void psCharAppearance::Clone(psCharAppearance* clone)
{
    this->hairMesh      = clone->hairMesh;
    this->beardMesh     = clone->beardMesh;
    
    this->hairShader    = clone->hairShader;    
    this->faceMaterial  = clone->faceMaterial;
    this->skinToneSet   = clone->skinToneSet;   
    this->hairAttached  = clone->hairAttached;
    this->colorSet      = clone->colorSet;
    this->effectids     = clone->effectids;
}
