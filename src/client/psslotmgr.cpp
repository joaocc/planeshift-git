/*
 * psslotmgr.cpp
 *
 * Copyright (C) 2005 Atomic Blue (info@planeshift.it, http://www.atomicblue.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation (version 2
 * of the License).
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 *
 */

#include <psconfig.h>
#include "psslotmgr.h"


//=============================================================================
// PlaneShift Includes
//=============================================================================
#include "iclient/iscenemanipulate.h"
#include "paws/pawsmainwidget.h"
#include "gui/pawsslot.h"
#include "pscamera.h"
#include "globals.h"


//=============================================================================
// Classes
//=============================================================================


psSlotManager::psSlotManager()
{
    isDragging = false;
    isPlacing = false;
    isRotating = false;

    // Initialize event shortcuts
    MouseMove = csevMouseMove (psengine->GetEventNameRegistry(), 0);
    MouseDown = csevMouseDown (psengine->GetEventNameRegistry(), 0);
    MouseUp = csevMouseUp (psengine->GetEventNameRegistry(), 0);
}


psSlotManager::~psSlotManager()
{
}


bool psSlotManager::HandleEvent( iEvent& ev )
{
    if(isDragging)
    {
        uint button = csMouseEventHelper::GetButton(&ev);
        if(ev.Name == MouseMove)
        {
            if(isPlacing)
            {
                // Update item position.
                UpdateItem();
            }
        }
        else if(ev.Name == MouseDown)
        {
            if(button == 0) // Left
            {
                if(isPlacing)
                {
                    // Drop the item at the current position.
                    DropItem();
                    return true;
                }
                else
                {
                    PlaceItem();
                }
            }
            else if(button == 1) // right
            {
                if(!isRotating)
                {
                    isRotating = true;
                    return false;
                }
            }
            else
            {
                CancelDrag();
            }
        }
        else if(ev.Name == MouseUp)
        {
            if(button == 1) // right
            {
                if(isRotating)
                {
                    isRotating = false;
                }
            }
        }
    }

    return false;
}


void psSlotManager::CancelDrag()
{
    isDragging = false;

    if(isPlacing)
    {
        psengine->GetSceneManipulator()->RemoveSelected();
        if(hadInventory)
        {
            PawsManager::GetSingleton().GetMainWidget()->FindWidget("InventoryWindow")->Show();
        }
        isPlacing = false;
        isRotating = false;
        hadInventory = false;
    }

    pawsSlot* dragging = (pawsSlot*)PawsManager::GetSingleton().GetDragDropWidget();
    if ( !dragging )
        return;
        
    draggingSlot.slot->SetPurifyStatus(dragging->GetPurifyStatus());
    int oldStack =  draggingSlot.slot->StackCount();
    oldStack += draggingSlot.stackCount;

    csString res;
    if(dragging->Image())
        res = dragging->Image()->GetName();
    else
        res.Clear();

    draggingSlot.slot->PlaceItem(res, draggingSlot.meshFactName, draggingSlot.materialName, oldStack);
    PawsManager::GetSingleton().SetDragDropWidget( NULL );
}


void psSlotManager::OnNumberEntered(const char *name,int param,int count)
{
    if ( count == -1 )
        return;

    pawsSlot* parent = NULL;
    size_t i = (size_t)param;
    if (i < slotsInUse.GetSize())
    {
        // Get the slot ptr
        parent = slotsInUse[i];
        slotsInUse[i] = NULL;
        
        // Clean up the trailing NULLs  (can't just delete the index, as that would change other indicies)
        for (i=slotsInUse.GetSize()-1; i!=(size_t)-1; i--)
        {
            if (slotsInUse[i] == NULL)
                slotsInUse.DeleteIndex(i);
            else break;
        }
    }

    if (!parent)
        return;

    int purifyStatus = parent->GetPurifyStatus();
    int newStack = parent->StackCount() - count;
            
    pawsSlot* widget = new pawsSlot();
    widget->SetRelativeFrame( 0,0, parent->DefaultFrame().Width(), parent->DefaultFrame().Height() );
    
    if (parent->Image())    
        widget->PlaceItem( parent->Image()->GetName(), parent->GetMeshFactName(), parent->GetMaterialName(), count );
    else        
        widget->PlaceItem( NULL, parent->GetMeshFactName(), parent->GetMaterialName(), count );

    parent->StackCount( newStack );
    widget->SetPurifyStatus( purifyStatus );
    widget->SetBackgroundAlpha(0);
    widget->SetParent( NULL );
           
    SetDragDetails( parent, count );
    isDragging = true;
    PawsManager::GetSingleton().SetDragDropWidget( widget );
}


void psSlotManager::SetDragDetails( pawsSlot* slot, int count ) 
{ 
    draggingSlot.containerID    = slot->ContainerID();
    draggingSlot.slotID         = slot->ID();
    draggingSlot.stackCount     = count;
    draggingSlot.slot           = slot;
    draggingSlot.meshFactName   = slot->GetMeshFactName();
    draggingSlot.materialName   = slot->GetMaterialName();
}


void psSlotManager::PlaceItem()
{
    // Get WS position.
    psPoint p = PawsManager::GetSingleton().GetMouse()->GetPosition();

    // Create mesh.
    outline = psengine->GetSceneManipulator()->CreateAndSelectMesh(draggingSlot.meshFactName,
        draggingSlot.materialName, psengine->GetPSCamera()->GetICamera()->GetCamera(), csVector2(p.x, p.y));

    // If created mesh is valid.
    if(outline)
    {
        // Hide the inventory so we can see where we're dropping.
        pawsWidget* inventory = PawsManager::GetSingleton().GetMainWidget()->FindWidget("InventoryWindow");
        hadInventory = inventory->IsVisible();
        inventory->Hide();

        // Get rid of icon.
        PawsManager::GetSingleton().SetDragDropWidget( NULL );

        // Mark placing.
        isPlacing = true;
    }
}


void psSlotManager::UpdateItem()
{
    // Get new position.
    psPoint p = PawsManager::GetSingleton().GetMouse()->GetPosition();

    // If we're rotating then we use mouse movement to determine rotation.
    if(isRotating)
    {
        psengine->GetSceneManipulator()->RotateSelected(csVector2(p.x, p.y));
    }
    else
    {
        // Else we use it to determine item position.
        psengine->GetSceneManipulator()->TranslateSelected(false,
            psengine->GetPSCamera()->GetICamera()->GetCamera(), csVector2(p.x, p.y));
    }
}


void psSlotManager::DropItem()
{
    // get final position and rotation
    psPoint p = PawsManager::GetSingleton().GetMouse()->GetPosition();
    csVector3 pos;
    csVector3 rot;
    psengine->GetSceneManipulator()->GetPosition(pos, rot, csVector2(p.x, p.y));

    // Send drop message.
    psSlotMovementMsg msg( draggingSlot.containerID, draggingSlot.slotID,
      CONTAINER_WORLD, 0, draggingSlot.stackCount, &pos,
      &rot);
    msg.SendMessage();

    // Remove outline mesh.
    psengine->GetSceneManipulator()->RemoveSelected();

    // Show inventory window again.
    if(hadInventory)
    {
        PawsManager::GetSingleton().GetMainWidget()->FindWidget("InventoryWindow")->Show();
    }

    // Reset flags.
    isDragging = false;
    isPlacing = false;
    isRotating = false;
    hadInventory = false;
}


void psSlotManager::Handle( pawsSlot* slot, bool grabOne, bool grabAll )
{
    //printf("In psSlotManager::Handle()\n");

    if ( !isDragging )
    {
        // Make sure other code isn't drag-and-dropping a different object.
        pawsWidget *dndWidget = PawsManager::GetSingleton().GetDragDropWidget();
        if (dndWidget)
            return; 

        //printf("Starting a drag/drop action\n");

        int stackCount = slot->StackCount();
        if ( stackCount > 0 )
        {          
            int tmpID = (int)slotsInUse.Push(slot);

            if ( stackCount == 1 || grabOne )
            {
                OnNumberEntered("StackCount",tmpID, 1);
            }
            else if ( grabAll )
            {
                OnNumberEntered("StackCount",tmpID, stackCount);
            }
            else // Ask for the number of items to grab
            {
                csString max;
                max.Format("Max %d", stackCount );
                
                pawsNumberPromptWindow::Create(max,
                                               -1, 1, stackCount, this, "StackCount", tmpID);
            }
        }
    }
    else
    {
        //printf("Sending slot movement message\n");
        psSlotMovementMsg msg( draggingSlot.containerID, draggingSlot.slotID,
                               slot->ContainerID(), slot->ID(),
                               draggingSlot.stackCount );
        msg.SendMessage();

        // Reset widgets/objects/status.
        PawsManager::GetSingleton().SetDragDropWidget( NULL );
        isDragging = false;
        if(isPlacing)
        {
            psengine->GetSceneManipulator()->RemoveSelected();
            if(hadInventory)
            {
                PawsManager::GetSingleton().GetMainWidget()->FindWidget("InventoryWindow")->Show();
            }
            isPlacing = false;
            isRotating = false;
            hadInventory = false;
        }
    }    
}
