/*
 * Author: Andrew Robberts
 *
 * Copyright (C) 2003 PlaneShift Team (info@planeshift.it,
 * http://www.planeshift.it)
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

#ifndef EEDIT_SELECT_EDIT_ANCHOR_KEYFRAME_HEADER
#define EEDIT_SELECT_EDIT_ANCHOR_KEYFRAME_HEADER

#include "eeditinputboxmanager.h"
#include "paws/pawswidget.h"

class pawsButton;
class pawsSpinBox;
class pawsCheckBox;

/** A dialog window to edit an anchor keyframe.
 */
class EEditSelectEditAnchorKeyFrame : public pawsWidget, public scfImplementation0<EEditSelectEditAnchorKeyFrame>
{
public:
    EEditSelectEditAnchorKeyFrame();
    virtual ~EEditSelectEditAnchorKeyFrame();

    /** Pops up the select dialog.
     *   @param time the default time of the select box.
     *   @param callback a pointer to the callback that should be called on selection.
     */
    void Select(float time, EEditInputboxManager::iSelectEditAnchorKeyFrame * callback, const csVector2 & pos);
    
    // inheritted from pawsWidget
    virtual bool PostSetup(); 
    virtual bool OnButtonPressed(int mouseButton, int keyModifier, pawsWidget* widget);
    
private:
    pawsSpinBox  * newTime;
    pawsCheckBox * hasPosX;
    pawsCheckBox * hasPosY;
    pawsCheckBox * hasPosZ;
    pawsCheckBox * hasToTarget;
    pawsButton   * ok;

    EEditInputboxManager::iSelectEditAnchorKeyFrame * selectCallback;
};

CREATE_PAWS_FACTORY(EEditSelectEditAnchorKeyFrame);

#endif

