/*
 * pawsbutton.cpp - Author: Andrew Craig
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
// pawsbutton.cpp: implementation of the pawsButton class.
//
//////////////////////////////////////////////////////////////////////

#include <psconfig.h>
#include <ivideo/fontserv.h>
#include <iutil/evdefs.h>


#include "pawsmanager.h"
#include "pawsbutton.h"
#include "pawstexturemanager.h"
#include "pawsprefmanager.h"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

pawsButton::pawsButton()
          : enabled(true), upTextOffsetX(0), upTextOffsetY(0), downTextOffsetX(0), downTextOffsetY(0)
{
    down = false;
    notify = NULL;
    toggle = false;
    flash = 0;
    flashtype = FLASH_REGULAR;
    keybinding = 0;
}


bool pawsButton::Setup( iDocumentNode* node )
{    
    // Check for toggle 
    csRef<iDocumentAttribute> toggleAttribute = node->GetAttribute("toggle");
    if ( toggleAttribute )
    {
        csString value( toggleAttribute->GetValue() );
        if ( value == "yes" )  toggle = true;
        else                   toggle = false;

    }

    // Check for keyboard shortcut for this button
    const char *key = node->GetAttributeValue("key");
    if (key)
    {
        if (!strcasecmp(key,"Enter"))
            keybinding = 10;
        else
            keybinding = *key;
    }
    // Check for sound to be associated to Buttondown
    csRef<iDocumentAttribute> soundAttribute = node->GetAttribute( "sound" );
    if ( soundAttribute )
    {
        csString soundName = node->GetAttributeValue("sound");
        SetSound(soundName);
    }
    else
    {
        csString name2;

        csRef<iDocumentNode> buttonLabelNode = node->GetNode( "label" );
        if ( buttonLabelNode )
            name2 = buttonLabelNode->GetAttributeValue("text");

        name2.Downcase();

        if(name2 == "ok")
            SetSound("gui.ok");
        else if(name2 == "quit")
            SetSound("gui.quit");
        else if(name2 == "cancel")
            SetSound("gui.cancel");
        else
            SetSound("sound.standardButtonClick");
    }

    // Check for notify widget
    csRef<iDocumentAttribute> notifyAttribute = node->GetAttribute( "notify" );
    if ( notifyAttribute )
        notify = PawsManager::GetSingleton().FindWidget(notifyAttribute->GetValue());

    // Check for mouse over
    changeOnMouseOver = node->GetAttributeValueAsBool("changeonmouseover", false);

    // Get the down button image name.
    csRef<iDocumentNode> buttonDownImage = node->GetNode( "buttondown" );
    if ( buttonDownImage )
    {
        csString downImageName = buttonDownImage->GetAttributeValue("resource");
        SetDownImage(downImageName);
		downTextOffsetX = buttonDownImage->GetAttributeValueAsInt("textoffsetx");
		downTextOffsetY = buttonDownImage->GetAttributeValueAsInt("textoffsety");
    }

    // Get the up button image name.
    csRef<iDocumentNode> buttonUpImage = node->GetNode( "buttonup" );
    if ( buttonUpImage )
    {
        csString upImageName = buttonUpImage->GetAttributeValue("resource");
        SetUpImage(upImageName);
		upTextOffsetX = buttonUpImage->GetAttributeValueAsInt("textoffsetx");
		upTextOffsetY = buttonUpImage->GetAttributeValueAsInt("textoffsety");
    }

	// Get the "on char name flash" button image name.
    csRef<iDocumentNode> buttonSpecialImage = node->GetNode( "buttonspecial" );
    if ( buttonSpecialImage )
    {
        csString onSpecialImageName = buttonSpecialImage->GetAttributeValue("resource");
        SetOnSpecialImage(onSpecialImageName);
    }


    // Get the button label
    csRef<iDocumentNode> buttonLabelNode = node->GetNode( "label" );
    if ( buttonLabelNode )
    {
        buttonLabel = PawsManager::GetSingleton().Translate(buttonLabelNode->GetAttributeValue("text"));
    }

    return true;
}

bool pawsButton::SelfPopulate( iDocumentNode *node)
{
    if (node->GetAttributeValue("text"))
    {
        SetText (node->GetAttributeValue("text"));
    }

    if (node->GetAttributeValue("down"))
    {
        SetState(strcmp(node->GetAttributeValue("down"),"true")==0);
    }
    
    return true;
}


void pawsButton::SetDownImage(const csString & image)
{
    pressedImage = PawsManager::GetSingleton().GetTextureManager()->GetDrawable(image);
}

void pawsButton::SetUpImage(const csString & image)
{
    releasedImage = PawsManager::GetSingleton().GetTextureManager()->GetDrawable(image);
}

void pawsButton::SetGreyUpImage(const char * greyUpImage)
{
	this->greyUpImage = PawsManager::GetSingleton().GetTextureManager()->GetDrawable(greyUpImage);
}

void pawsButton::SetGreyDownImage(const char * greyDownImage)
{
	this->greyDownImage = PawsManager::GetSingleton().GetTextureManager()->GetDrawable(greyDownImage);
}

void pawsButton::SetOnSpecialImage( const csString & image )
{
    specialFlashImage = PawsManager::GetSingleton().GetTextureManager()->GetDrawable(image);
}


void pawsButton::SetSound(const csString & soundName)
{
    if(PawsManager::GetSingleton().GetSoundStatus())
    {
        sound_click = PawsManager::GetSingleton().LoadSound(soundName);

        if(sound_click == NULL)
        {
            sound_click = PawsManager::GetSingleton().LoadSound("sound.standardButtonClick");
        }
    }
}

void pawsButton::SetText(const char* text)
{
    buttonLabel = text;

    // Try to parse new sound if standard is active
    if(PawsManager::GetSingleton().GetSoundStatus() && 
        sound_click == PawsManager::GetSingleton().LoadSound("sound.standardButtonClick"))
    {
       if(buttonLabel == "ok")
            SetSound("gui.ok");
        else if(buttonLabel == "quit")
            SetSound("gui.quit");
        else if(buttonLabel == "cancel")
            SetSound("gui.cancel");
    }
}


pawsButton::~pawsButton()
{
}

void pawsButton::Draw()
{   
    pawsWidget::Draw();        
    int drawAlpha = -1;
    if (parent && parent->GetMaxAlpha() >= 0)
    {
        fadeVal = parent->GetFadeVal();
        alpha = parent->GetMaxAlpha();
        alphaMin = parent->GetMinAlpha();
        drawAlpha = (int)(alphaMin + (alpha-alphaMin) * fadeVal * 0.010);
    }
    if ( down )
    {
		if (!enabled && greyDownImage)
			greyDownImage->Draw(screenFrame, drawAlpha);
        else if (pressedImage)
			pressedImage->Draw(screenFrame, drawAlpha);
    }
    else if ( flash==0 )
    {
		if (!enabled && greyUpImage)
			greyUpImage->Draw(screenFrame, drawAlpha);
		else if (releasedImage) 
			releasedImage->Draw(screenFrame, drawAlpha);
    }
    else // Flash the button if it's not depressed.
    {
        if (flash <= 10 )
        {
            flash++;
            switch (flashtype)
            {
            case FLASH_REGULAR:
                if ( pressedImage )
					pressedImage->Draw( screenFrame );
                break;
            case FLASH_SPECIAL:
                if ( specialFlashImage ) 
    				specialFlashImage->Draw( screenFrame );
                break;
			}
        }
        else
        {
            if (flash == 30)
                flash = 1;
            else flash++;
            if ( releasedImage ) releasedImage->Draw( screenFrame, drawAlpha );
        }
    }
    if (!(buttonLabel.IsEmpty()))
    {
        int drawX=0;
        int drawY=0;
        int width=0;
        int height=0;

        GetFont()->GetDimensions( buttonLabel , width, height );  

        int midX = screenFrame.Width() / 2;
        int midY = screenFrame.Height() / 2;

        drawX = screenFrame.xmin + midX - width/2;
        drawY = screenFrame.ymin + midY - height/2;
        drawY -= 2; // correction

		if (down)
			DrawWidgetText(buttonLabel, drawX + downTextOffsetX, drawY + downTextOffsetY);
		else
			DrawWidgetText(buttonLabel, drawX + upTextOffsetX, drawY + upTextOffsetY);
    }
}

bool pawsButton::OnMouseEnter()
{
    if(changeOnMouseOver)
    {
        SetState(true, false);
    }

    return true;
}

bool pawsButton::OnMouseExit()
{
    if(changeOnMouseOver)
    {
        SetState(false, false);
    }

    return true;
}

bool pawsButton::OnMouseDown( int button, int modifiers, int x, int y )
{  
    if (!enabled)
        return true;

    // plays a sound
    PawsManager::GetSingleton().PlaySound(sound_click);
    
    if ( toggle ) 
        SetState(!IsDown());
    else
        SetState(true, false);

    if ( flash )
        flash = 0;

    if (notify != NULL)
        return notify->CheckButtonPressed( button, modifiers, this );
    else if ( parent )
        return parent->CheckButtonPressed( button, modifiers, this );

    return false;
}

bool pawsButton::CheckKeyHandled(int keyCode)
{
    if (keybinding && keyCode == keybinding)
    {
        OnMouseDown(0,0,0,0);
        return true;
    }
    return false;
}
bool pawsButton::OnMouseUp( int button, int modifiers, int x, int y )
{
    if (!enabled)
    {
        return false;
    }        

    if (!toggle)
        SetState(false, false);

    if (notify != NULL)
    {
        notify->OnButtonReleased( button, this );
    }        
    else if ( parent )
    {
        return parent->OnButtonReleased( button, this );
    }

    return false;
}

bool pawsButton::OnKeyDown( int keyCode, int key, int modifiers )
{
    if (enabled && key == CSKEY_ENTER)
    {
        OnMouseDown(csmbLeft,modifiers,screenFrame.xmin,screenFrame.ymin);
        return true;
    }
    return pawsWidget::OnKeyDown(keyCode, key, modifiers);
}

void pawsButton::SetNotify( pawsWidget* widget )
{
    notify = widget;
}

void pawsButton::SetEnabled(bool enabled)
{
	this->enabled = enabled;
}

bool pawsButton::IsEnabled() const
{
	return enabled;
}

void pawsButton::SetState(bool isDown, bool publish)
{
    down = isDown;
	if (!toggle)
		return;

	if (notify)
		notify->RunScriptEvent(PW_SCRIPT_EVENT_VALUECHANGED);
	else
		RunScriptEvent(PW_SCRIPT_EVENT_VALUECHANGED);

    if (!publish)
        return;

    for (size_t a=0; a<publishList.GetSize(); ++a)
        PawsManager::GetSingleton().Publish(publishList[a], isDown);
}
