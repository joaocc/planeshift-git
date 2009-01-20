/*
* pawsimagedrawable.cpp
*
* Copyright (C) 2001 Atomic Blue (info@planeshift.it, http://www.atomicblue.org)
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
#include <iutil/databuff.h>
#include <igraphic/imageio.h>
#include <igraphic/image.h>

#include <ivideo/txtmgr.h>
#include <ivideo/graph2d.h>
#include <csgeom/math.h>


#include "util/log.h"

#include "pawstexturemanager.h"
#include "pawsimagedrawable.h"
#include "pawsmanager.h"

bool pawsImageDrawable::PreparePixmap()
{
    if (imageFileLocation.Length() == 0) // tileable background
        return true;

    csRef<iVFS> vfs = csQueryRegistry<iVFS>(PawsManager::GetSingleton().GetObjectRegistry());
    csRef<iImageIO> imageLoader = csQueryRegistry<iImageIO>(PawsManager::GetSingleton().GetObjectRegistry());
    graphics3D  =  PawsManager::GetSingleton().GetGraphics3D();

    csRef<iTextureManager> textureManager = graphics3D->GetTextureManager();

    int textureFormat = textureManager->GetTextureFormat();

    csRef<iImage> ifile;
    csRef<iDataBuffer> buf( vfs->ReadFile( imageFileLocation, false ) );

    if (!buf.IsValid())
    {
        Error2( "Could not open image: >%s<", (const char*)imageFileLocation );
        return false;
    }

    ifile = imageLoader->Load( buf, textureFormat );


    if ( !ifile )
    {
        Error2( "Image >%s< could not be loaded by the iImageID", 
            (const char*)imageFileLocation );
        return false;
    }

    width = ifile->GetWidth();
    height = ifile->GetHeight();


    textureHandle = textureManager->RegisterTexture( ifile, 
        CS_TEXTURE_2D |
        CS_TEXTURE_3D |
        CS_TEXTURE_NOMIPMAPS |
        //  This doesn't seem to have an effect, and crashes some Macs.
        CS_TEXTURE_CLAMP |
        CS_TEXTURE_NPOTS);

    if (!textureHandle)
    {
        Error1("Failed to Register Texture");
        return false;
    }

    // If colour key exists.
    if ( defaultTransparentColourBlue  != -1 &&
        defaultTransparentColourGreen != -1 &&
        defaultTransparentColourRed   != -1 )
    {
        textureHandle->SetKeyColor( defaultTransparentColourRed, 
            defaultTransparentColourGreen, 
            defaultTransparentColourBlue );
    }

    if ( textureRectangle.Width() == 0 || textureRectangle.Height() == 0 )
    {
        textureRectangle.xmax = width;
        textureRectangle.ymax = height;        
    }

    return true;     
}

pawsImageDrawable::pawsImageDrawable(csRef<iDocumentNode> node)
                 : scfImplementationType (this)
{
    defaultTransparentColourBlue  = -1;
    defaultTransparentColourGreen = -1;
    defaultTransparentColourRed   = -1;

    defaultAlphaValue = 0;

    // Read off the image and file vars
    imageFileLocation = node->GetAttributeValue( "file" );
    resourceName = node->GetAttributeValue( "resource" );

    tiled = node->GetAttributeValueAsBool("tiled");

    csRef<iDocumentNodeIterator> iter = node->GetNodes();
    while ( iter->HasNext() )
    {
        csRef<iDocumentNode> childNode = iter->Next();       

        // Read the texture rectangle for this image.
        if ( strcmp( childNode->GetValue(), "texturerect" ) == 0 )
        {
            textureRectangle.xmin = childNode->GetAttributeValueAsInt("x");
            textureRectangle.ymin = childNode->GetAttributeValueAsInt("y");

            int width = childNode->GetAttributeValueAsInt("width");
            int height = childNode->GetAttributeValueAsInt("height");

            textureRectangle.SetSize(width, height);
        }

        // Read the default alpha value.
        if ( strcmp( childNode->GetValue(), "alpha" ) == 0 )
        {
            defaultAlphaValue = childNode->GetAttributeValueAsInt("level");            
        }

        // Read the default transparent colour.
        if ( strcmp( childNode->GetValue(), "trans" ) == 0 )
        {
            defaultTransparentColourRed   = childNode->GetAttributeValueAsInt("r");            
            defaultTransparentColourGreen = childNode->GetAttributeValueAsInt("g");            
            defaultTransparentColourBlue  = childNode->GetAttributeValueAsInt("b");                                    
        }
    }

    PreparePixmap();
}

pawsImageDrawable::pawsImageDrawable(const char * file, const char * resource, bool tiled, const csRect & textureRect, int alpha, int transR, int transG, int transB)
                 : scfImplementationType (this)
{
    imageFileLocation = file;
    resourceName = resource;
    this->tiled = tiled;
    textureRectangle = textureRect;
    defaultAlphaValue = alpha;
    defaultTransparentColourRed   = transR;
    defaultTransparentColourGreen = transG;
    defaultTransparentColourBlue  = transB;
    PreparePixmap();
}

pawsImageDrawable::~pawsImageDrawable()
{
}

const char * pawsImageDrawable::GetName() const
{
    return resourceName;
}

void pawsImageDrawable::Draw(int x, int y, int alpha)
{
    if (!textureHandle)
        return;
    if (alpha < 0)
        alpha = defaultAlphaValue;
    int w = textureRectangle.Width();
    int h = textureRectangle.Height();
    graphics3D->DrawPixmap(textureHandle, x, y, w, h, textureRectangle.xmin, textureRectangle.ymin, w, h, alpha);
}

void pawsImageDrawable::Draw(csRect rect, int alpha)
{
    Draw(rect.xmin, rect.ymin, rect.Width(), rect.Height(), alpha);
}

void pawsImageDrawable::Draw(int x, int y, int newWidth, int newHeight, int alpha)
{   
    int w = textureRectangle.Width();
    int h = textureRectangle.Height();

    if (!textureHandle)
        return;

    if (alpha < 0)
        alpha = defaultAlphaValue;
    if ( newWidth == 0 ) 
        newWidth = width;
    if ( newHeight == 0 )
        newHeight = height;

    if (!tiled)
        graphics3D->DrawPixmap(textureHandle, x, y, newWidth, newHeight, textureRectangle.xmin, textureRectangle.ymin, w, h, alpha);
    else
    {
        int left = x;
        int top = y;
        int right = x + newWidth;
        int bottom = y + newHeight;
        for (x=left; x<right;  x+=w)
        {
            for (y=top; y<bottom; y+=h)
            {
                int dw = csMin<int>(w, right - x);
                int dh = csMin<int>(h, bottom - y);
                graphics3D->DrawPixmap(textureHandle, x, y, dw, dh, textureRectangle.xmin, textureRectangle.ymin, dw, dh, alpha);
            }
        }
    }
}

int pawsImageDrawable::GetWidth() const
{
    return textureRectangle.Width();
}

int pawsImageDrawable::GetHeight() const
{
    return textureRectangle.Height();
}

int pawsImageDrawable::GetDefaultAlpha() const
{
    return defaultAlphaValue;
}

iImage * pawsImageDrawable::GetImage()
{
    if (image)
        return image;

    csRef<iVFS> vfs = csQueryRegistry<iVFS>(PawsManager::GetSingleton().GetObjectRegistry());
    csRef<iImageIO> imageLoader =  csQueryRegistry<iImageIO >( PawsManager::GetSingleton().GetObjectRegistry());

    csRef<iDataBuffer> buf(vfs->ReadFile(imageFileLocation, false));

    if (!buf || !buf->GetSize())
    {
        Error2("Could not open image: '%s'", imageFileLocation.GetData());
        return 0;
    }

    image = imageLoader->Load(buf, CS_IMGFMT_ANY | CS_IMGFMT_ALPHA);

    if (!image)
    {
        Error2( "Could not load image: '%s'", imageFileLocation.GetData());
        return 0;
    }

    image->SetName(imageFileLocation);
    return image;
}

int pawsImageDrawable::GetTransparentRed() const
{
    return defaultTransparentColourRed;
}

int pawsImageDrawable::GetTransparentGreen() const
{
    return defaultTransparentColourGreen;
}

int pawsImageDrawable::GetTransparentBlue() const
{
    return defaultTransparentColourBlue;
}

