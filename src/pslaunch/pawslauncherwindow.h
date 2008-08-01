/*
* pawslauncherwindow.h - Author: Mike Gist
*
* Copyright (C) 2007 Atomic Blue (info@planeshift.it, http://www.atomicblue.org)
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

#ifndef __PAWS_LAUNCHER_WINDOW_H__
#define __PAWS_LAUNCHER_WINDOW_H__

#include "pslaunch.h"

class pawsWidget;
class pawsYesNoBox;

class pawsLauncherWindow : public pawsWidget
{
public:
    pawsLauncherWindow();
    bool OnButtonPressed(int mouseButton, int keyModifier, pawsWidget* widget );
    bool PostSetup();
private:
    pawsWidget* launcherMain;
    pawsWidget* launcherUpdater;
    pawsWidget* launcherSettings;
    pawsYesNoBox* updateAvailable;
    csRef<iConfigFile> configFile;
    csRef<iConfigFile> configUser;
    csRef<Thread> newsUpdater;
    csString mountedPath;
    csString currentSkin;

    static void HandleUpdateButton(bool choice, void *thisptr);
    void UpdateNews();
    void LoadSettings();
    void SaveSettings();
    void LoadSkin(const char* name);
    bool LoadResource(const char* resource,const char* resname, const char* mountPath);
    void OnListAction(pawsListBox* widget, int status);

    class NewsUpdater : public Runnable
    {
    public:
        NewsUpdater(pawsLauncherWindow* plw)
        {
            lw = plw;
        }

        void Run()
        {
            lw->UpdateNews();
        }
    private:
        pawsLauncherWindow* lw;
    };

    enum WidgetID
    {
        LAUNCHER = 1,
        LAUNCHER_MAIN = 11,
        LAUNCHER_UPDATER,
        LAUNCHER_SETTINGS,
        UPDATE_AVAILABLE,
        SERVER_NEWS = 111,
        QUIT_BUTTON,
        REPAIR_BUTTON,
        SETTINGS_BUTTON,
        PLAY_BUTTON,
        UPDATER_OUTPUT = 121,
        UPDATER_YES_BUTTON,
        UPDATER_NO_BUTTON,
        UPDATER_OK_BUTTON,
        UPDATER_CANCEL_BUTTON,
        SETTINGS_OK_BUTTON = 131,
        SETTINGS_CANCEL_BUTTON,
        SETTINGS_AUDIO_BUTTON,
        SETTINGS_CONTROLS_BUTTON,
        SETTINGS_GENERAL_BUTTON,
        SETTINGS_GRAPHICS_BUTTON,
        UPDATE_MESSAGE_BOX = 141,
        UPDATE_YES_BUTTON,
        UPDATE_NO_BUTTON
    };

    enum GraphicsPresets
    {
        HIGHEST = 0,
        HIGH,
        MEDIUM,
        LOW,
        LOWEST,
        CUSTOM
    };

    pawsButton* FindButton(WidgetID id);
};

CREATE_PAWS_FACTORY( pawsLauncherWindow );

#endif // __PAWS_LAUNCHER_WINDOW_H__
