/*
 * pawsobjectview.h - Author: Andrew Craig
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

#ifndef PAWS_OBJECT_VIEW_HEADER
#define PAWS_OBJECT_VIEW_HEADER

#include <iengine/camera.h>
#include <iengine/engine.h>
#include <iengine/light.h>
#include <iengine/material.h>
#include <iengine/mesh.h>
#include <iengine/movable.h>
#include <iengine/sector.h>

#include <imesh/object.h>

#include <cstool/csview.h>
#include <csutil/csstring.h>
#include <csutil/leakguard.h>

#include "pawswidget.h"

class psCharAppearance;

/** This widget is used to view a mesh in it's own seperate world.
 */
class pawsObjectView : public pawsWidget
{
public:
    pawsObjectView();
    ~pawsObjectView();

    /** Make a copy of this mesh to view.
     * @param wrapper  Will use the factory of this mesh to create a copy of it.
     */    
    void View( iMeshWrapper* wrapper );

    /** Use the specified mesh factory to create the mesh
     * @param wrapper  Will use this factory to create the mesh.
     */    
    void View( iMeshFactoryWrapper* wrapper );
    
    /** View a mesh. 
     * @param factName The name of the factory to use.
     * @param fileName The name of the file of the mesh file if the factory 
                       is not found.
     */
    void View( const char* factName, const char* fileName );
    
    /** Creates the room ( world ) for the mesh to be placed.
     */
    bool Setup(iDocumentNode* node);    
    
    /** Loads a map to use as the backdrop.
      *
      * @param map The full path name of the map to load
      * 
      * @return True if the map was loaded correctly. False otherwise.
      */
    bool LoadMap ( const char* map, const char* sector );
    
    /** Creates a default map. Creates a simple room to place object.
      * 
      * @return True if the simple map is created.
      */
    bool CreateMap();
    
    void Clear();
    
    void Draw();

    iMeshWrapper* GetObject() { return mesh; }
                                                
    bool OnMouseDown(int button,int mod, int x, int y);
    bool OnMouseUp(int button,int mod, int x, int y);
    bool OnMouseExit();

    void Rotate(int speed,float radians); // Starts a rotate each {SPEED} ms, taking {RADIANS} radians steps
    void Rotate(float radians); // Rotate the object "staticly"

    void EnableMouseControl(bool v) { mouseControlled = v; }

    void SetCameraPosModifier(csVector3& mod) { cameraMod = mod; }
    csVector3& GetCameraPosModifier() { return cameraMod; }

    void LockCamera(csVector3 where, csVector3 at, bool mouseDownUnlock = false );
    void UnlockCamera();
    void DrawRotate();
    void DrawNoRotate();

    /// Assign this view an ID
    void SetID(unsigned int id) { ID = id; }
    unsigned int GetID() { return ID; }

    void SetCharApp(psCharAppearance* cApp) { charApp = cApp; }
    psCharAppearance* GetCharApp() { return charApp; }

private:

    /**
     * Filters the world file to remove features which have been marked
     * as disabled by the user (post proc effects for example).
     */
    csRef<iDocumentNode> Filter(csRef<iDocumentNode> worldNode);

    bool doRotate;
    bool mouseDownUnlock;   ///< Checks to see if a mouse down will break camera lock.
    csVector3 cameraPosition;
    csVector3 lookingAt;
    
    csVector3 oldPosition;  ///< The old camera position before a lock.
    csVector3 oldLookAt;     ///< The old look at position before a lock.
        
    bool CreateArea();

    bool spinMouse;
    csVector2 downPos;
    float orgRadians;
    int orgTime;
    int downTime;
    bool mouseControlled;

    csRef<iSector> stage;
    csRef<iView>   view;
    csRef<iEngine> engine;
    csRef<iMeshWrapper> mesh;
    csRef<iCollection> col;
    csRef<iSector> meshSector;
    csRef<iView>   meshView;
    psCharAppearance* charApp;

    csVector3 objectPos;
    csVector3 cameraMod;
    
    void RotateDef(); // Used to reset to the values given by the controlling widget
    void RotateTemp(int speed,float radians); // Used to for example stop the rotate but not write the def values

    float distance;
    
    int rotateTime;
    float rotateRadians;
    float camRotate;

    bool loadedMap;
    
    unsigned int ID;
    bool needToFilter;
};
CREATE_PAWS_FACTORY( pawsObjectView );


#endif
