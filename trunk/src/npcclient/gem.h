/*
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
#ifndef PS_GEM_HEADER
#define PS_GEM_HEADER

#include "npcclient.h"
#include "net/messages.h"

struct iPcLinearMovement;
struct iPcMesh;

class gemNPCActor;

class gemNPCObject
{
public:
    static const unsigned int NO_PLAYER_ID = (unsigned int)-1;

    gemNPCObject( psNPCClient* cel, PS_ID id );
    virtual ~gemNPCObject();
    iCelEntity* GetEntity() { return entity; }
    
    bool InitMesh(const char *factname,const char *filename,
                  const csVector3& pos,const float rotangle, const char* sector );
    void Move(const csVector3& pos,float rotangle, const char* room);
    
    int GetID() { return id; }
    csRef<iPcMesh> pcmesh;   
    
    int GetType() { return type; }
    
    csString& GetName() { return name; }
    virtual unsigned int GetPlayerID() { return NO_PLAYER_ID; }

    virtual const char* GetObjectType(){ return "Object"; }
    virtual gemNPCActor *GetActorPtr() { return NULL; }

    virtual bool IsPickable() { return false; }
    virtual bool IsVisible() { return visible; }
    virtual bool IsInvisible() { return !visible; }
    virtual void SetVisible(bool vis) { visible = vis; }

    virtual bool IsInvincible() { return invincible; }
    virtual void SetInvincible(bool inv) { invincible = inv; }

    virtual NPC *GetNPC() { return NULL; }    

protected:
    static psNPCClient *cel;
    
    csRef<iCelEntity> entity;

    csString name;
    int id;
    int type;
    bool visible;
    bool invincible;
};


class gemNPCActor : public gemNPCObject
{
public:

    gemNPCActor( psNPCClient* cel, psPersistActor& mesg);
    virtual ~gemNPCActor();
    
    csRef<iPcLinearMovement> pcmove;
    
    virtual unsigned int GetPlayerID() { return playerID; }
    virtual PS_ID GetOwnerEID() { return ownerEID; }

    csString& GetRace() { return race; };

    virtual const char* GetObjectType(){ return "Actor"; }    
    virtual gemNPCActor *GetActorPtr() { return this; }

    virtual void AttachNPC(NPC * newNPC);
    virtual NPC *GetNPC() { return npc; }

protected:

    bool InitLinMove(const csVector3& pos,float angle, const char* sector,
                     csVector3 top, csVector3 bottom, csVector3 offset);
                     
    bool InitCharData(const char* textures, const char* equipment);        
    
    unsigned int playerID;
    PS_ID ownerEID;
    csString race;
    
    NPC *npc;
};


class gemNPCItem : public gemNPCObject
{
public:
    enum Flags
    {
        NONE           = 0,
        NOPICKUP       = 1 << 0
    };
    
    gemNPCItem( psNPCClient* cel, psPersistItem& mesg);
    virtual ~gemNPCItem();
    
    virtual const char* GetObjectType(){ return "Item"; }

    virtual bool IsPickable();

protected:
    int flags;
};

#endif
