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

#ifndef EEDIT_RENDER_TOOLBOX_HEADER
#define EEDIT_RENDER_TOOLBOX_HEADER

#include "eedittoolbox.h"
#include "paws/pawswidget.h"

class pawsButton;

/** This handles the effect render toolbox.
 */
class EEditRenderToolbox : public EEditToolbox, public pawsWidget
{
public:
    SCF_DECLARE_IBASE;

    EEditRenderToolbox();
    virtual ~EEditRenderToolbox();

    // inheritted from EEditToolbox
    virtual void Update(unsigned int elapsed);
    virtual size_t GetType() const;
    virtual const char * GetName() const;
    
    // inheritted from pawsWidget
    virtual bool PostSetup(); 
    virtual bool OnButtonPressed(int mouseButton, int keyModifier, pawsWidget* widget);
    
private:
    pawsButton * renderButton;
    pawsButton * loadButton;
    pawsButton * pauseButton;
    pawsButton * cancelButton;
};

CREATE_PAWS_FACTORY(EEditRenderToolbox);

#endif 
