/*
 * psengine.cpp
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

//////////////////////////////////////////////////////////////////////////////
// OS Specific Defines
//////////////////////////////////////////////////////////////////////////////
#if defined(CS_COMPILER_GCC) && defined(CS_PLATFORM_WIN32) && defined(LoadImage)
// Somewhere in the mingw includes there is a
// define LoadImage -> LoadImageA and this
// is problematic.
#undef LoadImage
#endif

#if defined(_MSC_VER) && (_MSC_VER >= 1300)
#define USE_WIN32_MINIDUMP
#endif

#ifdef USE_WIN32_MINIDUMP
#include "win32/mdump.h"
// Initialise the crash dumper.
MiniDumper minidumper;
#endif

//////////////////////////////////////////////////////////////////////////////

#define APPNAME              "PlaneShift Steel Blue (0.4.00)"
#define CONFIGFILENAME       "/planeshift/userdata/planeshift.cfg"
#define PSAPP                "planeshift.application.client"


//////////////////////////////////////////////////////////////////////////////
// Macros
//////////////////////////////////////////////////////////////////////////////
#define PS_QUERY_PLUGIN(myref,intf, str)                        \
myref =  csQueryRegistry<intf> (object_reg);                   \
if (!myref) {                                                   \
    csReport (object_reg, CS_REPORTER_SEVERITY_ERROR, PSAPP,    \
       "No " str " plugin!");                                   \
    return false;                                               \
}

#define RegisterFactory(factoryclass)   \
    factory = new factoryclass();

//////////////////////////////////////////////////////////////////////////////

// CS files
#include <iutil/cfgmgr.h>
#include <iutil/event.h>
#include <iutil/eventq.h>
#include <iutil/objreg.h>
#include <iutil/plugin.h>
#include <iutil/virtclk.h>
#include <iutil/vfs.h>
#include <iutil/stringarray.h>

//Sound
#include "iclient/isoundmngr.h"

#include <csver.h>
#include <csutil/cmdline.h>
#include <cstool/collider.h>
#include <cstool/initapp.h>
#include <csutil/event.h>

#include "globals.h"
#include "psengine.h"
#include "pscharcontrol.h"
#include "pscamera.h"
#include "psslotmgr.h"
#include "pscelclient.h"
#include "psnetmanager.h"
#include "psclientdr.h"
#include "psclientchar.h"
#include "modehandler.h"
#include "actionhandler.h"
#include "zonehandler.h"
#include "clientvitals.h"
#include "guihandler.h"

#include "pscal3dcallback.h"

#include "sound/pssoundmngr.h"

#include "net/connection.h"
#include "net/cmdhandler.h"
#include "net/msghandler.h"

#include "effects/pseffectmanager.h"

#include "psoptions.h"

#include "util/localization.h"
#include "util/pscssetup.h"
#include "util/log.h"
#include "util/strutil.h"
#include "engine/psworld.h"
#include "engine/materialmanager.h"
#include "util/psutil.h"
#include "util/consoleout.h"
#include "entitylabels.h"
#include "chatbubbles.h"
#include "clientcachemanager.h"
#include "questionclient.h"

/////////////////////////////////////////////////////////////////////////////
//  PAWS Includes
/////////////////////////////////////////////////////////////////////////////
#include "paws/pawsprogressbar.h"
#include "paws/pawsmenu.h"

#include "gui/pawsloading.h"
#include "gui/pawsglyphwindow.h"
#include "gui/chatwindow.h"
#include "gui/psmainwidget.h"
#include "gui/pawsconfigwindow.h"
#include "gui/pawsconfigmouse.h"
#include "gui/pawsconfigkeys.h"
#include "gui/pawsconfigcamera.h"
#include "gui/pawsconfigdetails.h"
#include "gui/pawsconfigpvp.h"
#include "gui/pawsconfigchat.h"
#include "gui/pawsconfigchattabs.h"
#include "gui/pawsconfigsound.h"
#include "gui/pawsconfigentitylabels.h"
#include "gui/pawsconfigentityinter.h"
#include "gui/inventorywindow.h"
#include "gui/pawsitemdescriptionwindow.h"
#include "gui/pawscontainerdescwindow.h"
#include "gui/pawsinteractwindow.h"
#include "gui/pawsinfowindow.h"
#include "gui/pawscontrolwindow.h"
#include "gui/pawsglyphwindow.h"
#include "gui/pawsgroupwindow.h"
#include "gui/pawsexchangewindow.h"
#include "gui/pawsmerchantwindow.h"
#include "gui/pawspetitionwindow.h"
#include "gui/pawspetitiongmwindow.h"
#include "gui/pawsspellbookwindow.h"
#include "gui/pawssplashwindow.h"
#include "gui/shortcutwindow.h"
#include "gui/pawsloginwindow.h"
#include "gui/pawscharpick.h"
#include "gui/pawsloading.h"
#include "gui/pawsguildwindow.h"
#include "gui/pawslootwindow.h"
#include "gui/pawspetstatwindow.h"
#include "gui/pawsskillwindow.h"
#include "gui/pawsquestwindow.h"
#include "gui/pawsspellcancelwindow.h"
#include "gui/pawscharcreatemain.h"
#include "gui/pawscharbirth.h"
#include "gui/pawscharparents.h"
#include "gui/pawschild.h"
#include "gui/pawslife.h"
#include "gui/pawspath.h"
#include "gui/pawssummary.h"
#include "gui/pawsgmgui.h"
#include "gui/pawsmoney.h"
#include "gui/pawshelp.h"
#include "gui/pawsbuddy.h"
#include "gui/pawsignore.h"
#include "gui/pawsslot.h"
#include "gui/pawsactionlocationwindow.h"
#include "gui/pawsdetailwindow.h"
#include "gui/pawschardescription.h"
#include "gui/pawsquestrewardwindow.h"
#include "gui/pawscreditswindow.h"
#include "gui/pawsinventorydollview.h"
#include "gui/pawsquitinfobox.h"
#include "gui/pawsgmspawn.h"
#include "gui/pawsbookreadingwindow.h"
#include "gui/pawswritingwindow.h"
#include "gui/pawsactivemagicwindow.h"
#include "gui/pawstutorialwindow.h"
#include "gui/pawssmallinventory.h"
#include "gui/pawsconfigchatfilter.h"
#include "gui/pawsgmaction.h"
#include "gui/pawscraft.h"
#include "gui/pawsilluminationwindow.h"
#include "gui/pawsgameboard.h"
#include "gui/pawsbankwindow.h"
#include "gui/pawsconfigchatbubbles.h"
#include "gui/pawsconfigshadows.h"


// ----------------------------------------------------------------------------

CS_IMPLEMENT_APPLICATION

// ----------------------------------------------------------------------------

psEngine::psEngine (iObjectRegistry *objectreg)
{
    object_reg = objectreg;

    // No no, no map loaded
    loadedMap = false;
    gameLoaded = false;
    loadstate = LS_NONE;
    loadError = false;

    cachemanager = NULL;
    charmanager = NULL;
    guiHandler = NULL;
    charController = NULL;
    mouseBinds = NULL;
    camera = NULL;
    slotManager = NULL;
    questionclient = NULL;
    paws = NULL;
    mainWidget = NULL;
    inventoryCache = NULL;

    loadtimeout = 10;  // Default load timeout

    preloadModels = false;
    modelsLoaded = false;
    modelToLoad = (size_t)-1;
    okToLoadModels = false;
    drawScreen = true;

    cal3DCallbackLoader = csPtr<psCal3DCallbackLoader> (new psCal3DCallbackLoader(objectreg));

    KFactor = 0.0f;
    targetPetitioner = "None";
    confirmation = 1;  // Def

    loggedIn = false;
    numOfChars = 0;
    elapsed = 0;
    frameLimit = 0;

    muteSoundsOnFocusLoss = false;


    xmlparser =  csQueryRegistry<iDocumentSystem> (object_reg);
    stringset = csQueryRegistryTagInterface<iStringSet> (object_reg, "crystalspace.shared.stringset");
    nameRegistry = csEventNameRegistry::GetRegistry (object_reg);

    chatBubbles = 0;
    options = 0;
    gfxFeatures = 0;
}

// ----------------------------------------------------------------------------

psEngine::~psEngine ()
{
    printf("psEngine destroyed.\n");
}


void psEngine::Cleanup()
{
    if (loadstate==LS_DONE)
        QuitClient();
    
    delete charmanager;
    delete charController;
    delete camera;
    delete chatBubbles;
    
    if (scfiEventHandler && queue)
        queue->RemoveListener (scfiEventHandler);

    delete paws; // Include delete of mainWidget
    
    delete questionclient;
    delete cachemanager;
    delete slotManager;
    delete mouseBinds;
    delete guiHandler;
    delete inventoryCache;
    

    // Effect manager needs to be destoyed before the soundmanager
    effectManager = NULL;
    
    object_reg->Unregister ((iSoundManager*)soundmanager, "iSoundManager");
                
    delete options;
}

// ----------------------------------------------------------------------------

/**
 *
 * psEngine::Initialize
 * This function initializes all objects necessary for the game. It also creates a
 * suitable environment for the engine.
 *
 **/
bool psEngine::Initialize (int level)
{
    // load basic stuff that is enough to display the splash screen
    if (level == 0)
    {
        // print out some version information
        csReport (object_reg, CS_REPORTER_SEVERITY_NOTIFY, PSAPP, APPNAME);
        csReport (object_reg, CS_REPORTER_SEVERITY_NOTIFY, PSAPP,
            "This game uses Crystal Space Engine created by Jorrit and others");
        csReport (object_reg, CS_REPORTER_SEVERITY_NOTIFY, PSAPP, CS_VERSION);


        // Query for plugins
        PS_QUERY_PLUGIN (queue,  iEventQueue,    "iEventQueue");
        PS_QUERY_PLUGIN (vfs,    iVFS,           "iVFS");
        PS_QUERY_PLUGIN (engine, iEngine,        "iEngine");
        PS_QUERY_PLUGIN (cfgmgr, iConfigManager, "iConfigManager");
        PS_QUERY_PLUGIN (g3d,    iGraphics3D,    "iGraphics3D");
        PS_QUERY_PLUGIN (loader, iLoader,        "iLoader");
        PS_QUERY_PLUGIN (vc,     iVirtualClock,  "iVirtualClock");
        PS_QUERY_PLUGIN (cmdline,iCommandLineParser, "iCommandLineParser");
        
        g2d = g3d->GetDriver2D();
        
        g2d->AllowResize(false);
        
        // Check for configuration values for crash dump action and mode
        #ifdef USE_WIN32_MINIDUMP
            PS_CRASHACTION_TYPE crashaction=PSCrashActionPrompt;
            csString CrashActionStr(cfgmgr->GetStr("PlaneShift.Crash.Action","prompt"));
            if (CrashActionStr.CompareNoCase("off"))
                crashaction=PSCrashActionOff;
            if (CrashActionStr.CompareNoCase("prompt"))
                crashaction=PSCrashActionPrompt;
            if (CrashActionStr.CompareNoCase("always"))
                crashaction=PSCrashActionAlways;

            minidumper.SetCrashAction(crashaction);

            PS_MINIDUMP_TYPE dumptype=PSMiniDumpNormal;
            csString DumpTypeStr(cfgmgr->GetStr("PlaneShift.Crash.DumpType","normal"));
            if (DumpTypeStr.CompareNoCase("normal")) // The normal stack and back information with no data
                dumptype=PSMiniDumpNormal;
            if (DumpTypeStr.CompareNoCase("detailed")) // This includes data segments associated with modules - global variables and static members
                dumptype=PSMiniDumpWithDataSegs;
            if (DumpTypeStr.CompareNoCase("full")) // This is the entire process memory, this dump will be huge, but will include heap data as well
                dumptype=PSMiniDumpWithFullMemory;
            if (DumpTypeStr.CompareNoCase("NThandles")) // NT/2k/xp only, include system handle information in addition to normal
                dumptype=PSMiniDumpWithHandleData;
            if (DumpTypeStr.CompareNoCase("filter")) // This can trim down the dump even more than normal
                dumptype=PSMiniDumpFilterMemory;
            if (DumpTypeStr.CompareNoCase("scan")) // This may be able to help make stack corruption crashes somewhat readable
                dumptype=PSMiniDumpScanMemory;

            minidumper.SetDumpType(dumptype);

            // Report the current type
            csReport (object_reg, CS_REPORTER_SEVERITY_NOTIFY, PSAPP, minidumper.GetDumpTypeString() );
        #endif // #ifdef USE_WIN32_MINIDUMP


        // Initialize and tweak the Texture Manager
        txtmgr = g3d->GetTextureManager ();
        if (!txtmgr)
        {
            return false;
        }            

        // Check if we're preloading models.
        preloadModels = (cmdline->GetBoolOption("preload_models", false) || GetConfig()->GetBool("PlaneShift.Client.Loading.PreloadModels", false));

        // Check if we're using post proc effects of any kind.
        if(cmdline->GetBoolOption("use_normal_maps", false))
        {
            gfxFeatures |= useNormalMaps;
        }

        if(cmdline->GetBoolOption("use_meshgen", true))
        {
            gfxFeatures |= useMeshGen;
        }

        //Check if sound is on or off in psclient.cfg
        csString soundPlugin;
      
        soundOn = cfgmgr->KeyExists("System.PlugIns.iSndSysRenderer");
        
        if (soundOn)
        {
            psSoundManager* pssound = new psSoundManager(0);
            
            pssound->Initialize(object_reg);
            soundmanager.AttachNew(pssound);
            object_reg->Register ((iSoundManager*) soundmanager, "iSoundManager");
    
            if (!soundmanager->Setup())
            {
                csReport(object_reg, CS_REPORTER_SEVERITY_NOTIFY, PSAPP,
                    "Warning: Cannot initialize SoundManager");
            }
        }

        LoadLogSettings();

        // Mount the selected gui first to allow overwriting of certain elements
        csString skinPath;
        skinPath += cfgmgr->GetStr("PlaneShift.GUI.Skin.Dir","/planeshift/art/skins/");
        skinPath += cfgmgr->GetStr("PlaneShift.GUI.Skin.Selected","default");
        skinPath += ".zip";

        // This .zip could be a file or a dir
        csString slash(CS_PATH_SEPARATOR);
        if ( vfs->Exists(skinPath + slash) )
        {
            skinPath += slash;
        }            

        
        // Create the PAWS window manager
        csString skinPathBase = cfgmgr->GetStr("PlaneShift.GUI.Skin.Base","/planeshift/art/skins/base/client_base.zip");
        paws = new PawsManager( object_reg, skinPath, skinPathBase, "/planeshift/userdata/planeshift.cfg", gfxFeatures );
        
        options = new psOptions("/planeshift/userdata/options.cfg", vfs);
        
        // Default to maximum 1000/(14)fps (71.4 fps)
        // Actual fps get be up to 10 fps less so set a reasonably high limit
        frameLimit = cfgmgr->GetInt("Video.FrameLimit", 14);
        
        paws->SetSoundStatus(soundOn);
        mainWidget = new psMainWidget();
        paws->SetMainWidget( mainWidget );
        
        paws->GetMouse()->Hide(true);

        DeclareExtraFactories();
        
        // Register default PAWS sounds
        if (soundmanager.IsValid() && soundOn)
        {
            paws->RegisterSound("sound.standardButtonClick",soundmanager->GetSoundResource("sound.standardButtonClick"));
            // Standard GUI stuff
            paws->RegisterSound("gui.toolbar",      soundmanager->GetSoundResource("gui.toolbar"));
            paws->RegisterSound("gui.cancel",       soundmanager->GetSoundResource("gui.cancel"));
            paws->RegisterSound("gui.ok",           soundmanager->GetSoundResource("gui.ok"));
            paws->RegisterSound("gui.scrolldown",   soundmanager->GetSoundResource("gui.scrolldown"));
            paws->RegisterSound("gui.scrollup",     soundmanager->GetSoundResource("gui.scrollup"));
            paws->RegisterSound("gui.shortcut",     soundmanager->GetSoundResource("gui.shortcut"));
            paws->RegisterSound("gui.quit",         soundmanager->GetSoundResource("gui.quit"));
            // Load sound settings
             if(!LoadSoundSettings(false))
             {
                return false;
             }
        }

        
        if ( ! paws->LoadWidget("data/gui/splash.xml") )
            return false;
        if ( ! paws->LoadWidget("data/gui/ok.xml") )
            return false;
        if ( ! paws->LoadWidget("data/gui/quitinfo.xml") )
            return false;

        //Load confirmation information for duels
        if (!LoadDuelConfirm())
            return false;

        // Register our event handler
        scfiEventHandler = csPtr<EventHandler> (new EventHandler (this));
        csEventID esub[] = {
              csevPreProcess (object_reg),
              csevProcess (object_reg),
              csevPostProcess (object_reg),
              csevFinalProcess (object_reg),
              csevFrame (object_reg),
              csevMouseEvent (object_reg),
              csevKeyboardEvent (object_reg),
              csevCanvasExposed (object_reg,g2d), 
              csevCanvasHidden (object_reg, g2d),
              csevFocusGained (object_reg),
              csevFocusLost (object_reg),
              csevQuit (object_reg),
              CS_EVENTLIST_END
        };
        queue->RegisterListener(scfiEventHandler, esub);

        event_preprocess = csevPreProcess(object_reg);
        event_process = csevProcess(object_reg);
        event_finalprocess = csevFinalProcess(object_reg);
        event_frame = csevFrame (object_reg);
        event_postprocess = csevPostProcess(object_reg);
        event_canvashidden = csevCanvasHidden(object_reg,g2d);
        event_canvasexposed = csevCanvasExposed(object_reg,g2d);
        event_focusgained = csevFocusGained(object_reg);
        event_focuslost = csevFocusLost(object_reg);
        event_quit = csevQuit(object_reg);

        // Inform debug that everything initialized succesfully
        csReport (object_reg, CS_REPORTER_SEVERITY_NOTIFY, PSAPP,
            "psEngine initialized.");

        
        return true;
    }
    else if (level==1)
    {
        
        // Initialize Networking
        if (!netmanager)
        {
            netmanager = csPtr<psNetManager> (new psNetManager);
            
            if (!netmanager->Initialize(object_reg))
            {
                lasterror = "Couldn't init Network Manager.";
                return false;
            }
            psMessageCracker::msghandler = netmanager->GetMsgHandler();
        }

        inventoryCache = new psInventoryCache();
        guiHandler = new GUIHandler();
        celclient = csPtr<psCelClient> (new psCelClient());
        slotManager = new psSlotManager();
        modehandler = csPtr<ModeHandler> (new ModeHandler (soundmanager, celclient,netmanager->GetMsgHandler(),object_reg));
        actionhandler = csPtr<ActionHandler> ( new ActionHandler ( netmanager->GetMsgHandler(), object_reg ) );
        zonehandler = csPtr<ZoneHandler> (new ZoneHandler(netmanager->GetMsgHandler(),object_reg,celclient));
        cachemanager = new ClientCacheManager();
        questionclient = new psQuestionClient(GetMsgHandler(), object_reg);
        
        if (cmdline)
        {
            celclient->IgnoreOthers(cmdline->GetBoolOption("ignore_others"));
        }

        zonehandler->SetLoadAllMaps(GetConfig()->GetBool("PlaneShift.Client.Loading.AllMaps",false));
        zonehandler->SetKeepMapsLoaded(GetConfig()->GetBool("PlaneShift.Client.Loading.KeepMaps",false));

        unloadLast = GetConfig()->GetBool("PlaneShift.Client.Loading.UnloadLast", true);

        materialmanager.AttachNew(new MaterialManager(object_reg, preloadModels));
        
        if(preloadModels)
        {
            materialmanager->PreloadTextures();
        }

        if (!celclient->Initialize(object_reg, GetMsgHandler(), zonehandler))
        {
            lasterror = "Couldn't init Cel Manager.";
            Error2("FATAL ERROR: %s",lasterror.GetData());
            return false;
        }

        
        if(!modehandler->Initialize())
        {
            lasterror = "ModeHandler failed init.";
            Error2("FATAL ERROR: %s",lasterror.GetData());
            return false;
        }

        // Init the main widget
        mainWidget->SetupMain();

        if (!camera)
        {
            camera = new psCamera();            
        }          

        if (!charmanager)
        {
            charmanager = new psClientCharManager(object_reg);

            if (!charmanager->Initialize( GetMsgHandler(), celclient))
            {
                lasterror = "Couldn't init Character Manager.";
                return false;
            }
        }

        
        // This widget requires NetManager to exist so must be in this stage
        if ( ! paws->LoadWidget("data/gui/charpick.xml") )
            return false;

        // Load effects now, before we have actors
        if (!effectManager)
        {
            effectManager.AttachNew(new psEffectManager());
        }

        if (!effectManager->LoadFromDirectory("/this/data/effects", true, camera->GetView()))
        {
            FatalError("Failed to load effects!");
            return 1;
        }
        
        okToLoadModels = true;
        
        return true;
    }

    return true;
}

void psEngine::LoadLogSettings()
{
    int count=0;
    for (int i=0; i< MAX_FLAGS; i++)
    {
        if (pslog::GetName(i))
        {
            pslog::SetFlag(pslog::GetName(i),cfgmgr->GetBool(pslog::GetSettingName(i)), 0);
            if ((cfgmgr->GetBool(pslog::GetSettingName(i))))
            {
                count++;
            }

        }
    }
    if (count==0)
    {
        CPrintf(CON_CMDOUTPUT,"All LOGS are off.\n");
    }

}

void psEngine::DeclareExtraFactories()
{
    pawsWidgetFactory* factory;

    RegisterFactory (pawsInventoryDollViewFactory);
    RegisterFactory (pawsGlyphSlotFactory);
    RegisterFactory (pawsInfoWindowFactory);
    RegisterFactory (pawsSplashWindowFactory);
    RegisterFactory (pawsLoadWindowFactory);
    RegisterFactory (pawsChatWindowFactory);
    RegisterFactory (pawsInventoryWindowFactory);
    RegisterFactory (pawsItemDescriptionWindowFactory);
    RegisterFactory (pawsContainerDescWindowFactory);
    RegisterFactory (pawsInteractWindowFactory);
    RegisterFactory (pawsControlWindowFactory);
    RegisterFactory (pawsGroupWindowFactory);
    RegisterFactory (pawsExchangeWindowFactory);
    RegisterFactory (pawsSpellBookWindowFactory);
    RegisterFactory (pawsGlyphWindowFactory);
    RegisterFactory (pawsMerchantWindowFactory);
    RegisterFactory (pawsConfigWindowFactory);
    RegisterFactory (pawsConfigKeysFactory);
    RegisterFactory (pawsConfigPvPFactory);
    RegisterFactory (pawsFingeringWindowFactory);
    RegisterFactory (pawsConfigDetailsFactory);
    RegisterFactory (pawsConfigMouseFactory);
    RegisterFactory (pawsConfigCameraFactory);
    RegisterFactory (pawsConfigChatFactory);
    RegisterFactory (pawsConfigSoundFactory);
    RegisterFactory (pawsConfigEntityLabelsFactory);
    RegisterFactory (pawsConfigEntityInteractionFactory);
    RegisterFactory (pawsPetitionWindowFactory);
    RegisterFactory (pawsPetitionGMWindowFactory);
    RegisterFactory (pawsShortcutWindowFactory);
    RegisterFactory (pawsLoginWindowFactory);
    RegisterFactory (pawsCharacterPickerWindowFactory);
    RegisterFactory (pawsGuildWindowFactory);
    RegisterFactory (pawsLootWindowFactory);
    RegisterFactory (pawsCreationMainFactory);
    RegisterFactory (pawsCharBirthFactory);
    RegisterFactory (pawsCharParentsFactory);
    RegisterFactory (pawsChildhoodWindowFactory);
    RegisterFactory (pawsLifeEventWindowFactory);
    RegisterFactory (pawsPathWindowFactory);
    RegisterFactory (pawsSummaryWindowFactory);
    RegisterFactory (pawsChatMenuItemFactory);
    RegisterFactory (pawsSkillWindowFactory);
    RegisterFactory (pawsQuestListWindowFactory);
    RegisterFactory (pawsSpellCancelWindowFactory);
    RegisterFactory (pawsGmGUIWindowFactory);
    RegisterFactory (pawsMoneyFactory);
    RegisterFactory (pawsHelpFactory);
    RegisterFactory (pawsBuddyWindowFactory);
    RegisterFactory (pawsIgnoreWindowFactory);
    RegisterFactory (pawsSlotFactory);
    RegisterFactory (pawsActionLocationWindowFactory);
    RegisterFactory (pawsDetailWindowFactory);
    RegisterFactory (pawsCharDescriptionFactory);
    RegisterFactory (pawsQuestRewardWindowFactory);
    RegisterFactory (pawsCreditsWindowFactory);
    RegisterFactory (pawsQuitInfoBoxFactory);
    RegisterFactory (pawsGMSpawnWindowFactory);
    RegisterFactory (pawsSkillIndicatorFactory);
    RegisterFactory (pawsBookReadingWindowFactory);
    RegisterFactory (pawsWritingWindowFactory);
    RegisterFactory (pawsActiveMagicWindowFactory);
    RegisterFactory (pawsSmallInventoryWindowFactory);
    RegisterFactory (pawsConfigChatFilterFactory);
    RegisterFactory (pawsConfigChatTabsFactory);
    RegisterFactory (pawsGMActionWindowFactory);
    RegisterFactory (pawsCraftWindowFactory);
    RegisterFactory (pawsPetStatWindowFactory);
    RegisterFactory (pawsTutorialWindowFactory);
    RegisterFactory (pawsTutorialNotifyWindowFactory);
    RegisterFactory (pawsSketchWindowFactory);
    RegisterFactory (pawsGameBoardFactory);
    RegisterFactory (pawsGameTileFactory);
    RegisterFactory (pawsBankWindowFactory);
	RegisterFactory (pawsConfigChatBubblesFactory);
    RegisterFactory (pawsConfigShadowsFactory);
}


// ----------------------------------------------------------------------------

// access funtions
MsgHandler* psEngine::GetMsgHandler()
{
    return netmanager->GetMsgHandler();
}
CmdHandler * psEngine::GetCmdHandler()
{
    return netmanager->GetCmdHandler();
}
iNetManager* psEngine::GetNetManager()
{
    return (iNetManager*)netmanager;
}

//-----------------------------------------------------------------------------

/**
 *
 * psEngine::HandleEvent
 * This function receives all event that occured when the game runs. Event is
 * classified by CS as anything that occured whether be it mouse move, mouse
 * click, messages, etc. These events are processed by this function.
 */
bool psEngine::HandleEvent (iEvent &ev)
{
    lastEvent = &ev;
 
    if ( paws->HandleEvent( ev ) )
    {
        if (charController && paws->GetFocusOverridesControls())
        {
            charController->GetMovementManager()->StopControlledMovement();
        }

        return true;
    }
    
    if ( charController && charController->HandleEvent( ev ) )
    {
        if (paws->GetFocusOverridesControls())
        {
            charController->GetMovementManager()->StopControlledMovement();
        }

        return true;
    }
    
    static bool drawFrame;

    if (ev.Name == event_preprocess)
    {
        for(size_t i=0; i<delayedLoaders.GetSize(); i++)
        {
            delayedLoaders.Get(i)->CheckMeshLoad();
        }

        if (gameLoaded)
        {
            modehandler->PreProcess();
        }

        // If any objects or actors are enqueued to be created, create the next one this frame.
        if (celclient)
        {
            celclient->CheckEntityQueues();
        }

        // Loading the game
        if (loadstate != LS_DONE)
        {
            if (!modelsLoaded)
            {
                if (preloadModels)
                {
                    // Preload models in spare time.
                    PreloadModels();
                }
                else
                {
                    if (okToLoadModels)
                    {
                        PreloadModels();
                        BuildFactoryList();
                        Debug1(LOG_ADMIN,0, "Preloading complete");

                        delete paws->FindWidget("SplashWindow");
                        paws->LoadWidget("data/gui/loginwindow.xml");

                        paws->GetMouse()->ChangeImage("Standard Mouse Pointer");
                        paws->GetMouse()->Hide(false);
                    
                        modelsLoaded = true;
                    }
                }
            }

            /* Try forwarding loading every 10th frame... not that
            * nice... but our guy doesn't produce idle events at the
            * moment :(
            */
            static unsigned int count = 0;
            if (++count%10 == 0)
                LoadGame();
        }
        // Update the sound system
        else if (GetSoundStatus())
        {
            soundmanager->Update( camera->GetView() );
            soundmanager->Update( celclient->GetMainPlayer()->Pos() );
        }

        if (celclient)
            celclient->Update();

        if (effectManager)
            effectManager->Update();
    }
    else if (ev.Name == event_process)
    {
        if (drawScreen)
        {
            // FPS limits
            drawFrame = FrameLimit();

            if (drawFrame)
            {
                if (camera)
                    camera->Draw();

                g3d->BeginDraw(CSDRAW_2DGRAPHICS);
                if (effectManager)
                    effectManager->Render2D(g3d, g2d);
                paws->Draw();
            }
        }
        else
        {
            csSleep(150);
        }
        return true;
    }
    else if (ev.Name == event_finalprocess)
    {
        if(drawFrame)
            FinishFrame ();

        // We need to call this after drawing was finished so
        // LoadingScreen had a chance to be displayed when loading
        // maps in-game
        if (zonehandler)
            zonehandler->OnDrawingFinished();
        return true;
    }
    else if (ev.Name == event_postprocess)
    {
        UpdatePerFrame();
    }
    else if (ev.Name == event_canvashidden)
    {
        drawScreen = false;
        if(soundOn)
        {
            MuteAllSounds();
        }
    }
    else if (ev.Name == event_canvasexposed)
    {
        //camera->ResetActualCameraData();
        drawScreen = true;
        if(soundOn)
        {
            UnmuteAllSounds();
        }
        // RS: this causes a freeze switching back to the client window
        // for up to a few seconds, and seems very much unnecessary
        //if (IsGameLoaded())
        //    celclient->GetEntityLabels()->RepaintAllLabels();
    }    
    else if (ev.Name == event_focusgained)
    {
        if(GetMuteSoundsOnFocusLoss() && soundOn)
        {
            UnmuteAllSounds();
        }
    }
    else if (ev.Name == event_focuslost)
    {
        if(GetMuteSoundsOnFocusLoss() && soundOn)
        {
            MuteAllSounds();
        }
    }
    else if (ev.Name == event_quit)
    {
        // Disconnect and quit, if this event wasn't made here
        QuitClient();
    }

    return false;
}

// ----------------------------------------------------------------------------

const csHandlerID * psEngine::EventHandler::GenericPrec(csRef<iEventHandlerRegistry>&handler_reg,
                                                        csRef<iEventNameRegistry>&name_reg,
                                                        csEventID e) const
{
    if (name_reg->IsKindOf(e, csevFrame (name_reg)))
    {
        // Make sure psClientMsgHandler::EventHandler is called before psEngine
        parent->EHConstraintsFramePrec[0] = handler_reg->GetGenericID("planeshift.clientmsghandler");
        parent->EHConstraintsFramePrec[1] = CS_HANDLERLIST_END;
        return parent->EHConstraintsFramePrec;
    }
    else
    {
        return NULL;
    }
}

const csHandlerID * psEngine::EventHandler::GenericSucc(csRef<iEventHandlerRegistry> &,
                                                        csRef<iEventNameRegistry> &,
                                                        csEventID) const
{
    return NULL;
}

// ----------------------------------------------------------------------------

inline bool psEngine::FrameLimit()
{
    csTicks sleeptime;
    csTicks elapsedTime;

    // Find the time taken since we left this function
    elapsedTime = csGetTicks() - elapsed;

    static pawsWidget* loading = NULL;
    if(!loading)
        loading = paws->FindWidget("LoadWindow", false);

    // Loading competes with drawing in THIS thread so we can't sleep
    if(loading && loading->IsVisible())
    {
        sleeptime = 1000;

        // Here we sacrifice drawing time for loading time
        if(elapsedTime < sleeptime)
            return false;
        else
        {
            elapsedTime = csGetTicks();
            return true;
        }

    }

    // Define sleeptimes
    if(!camera)
    {
        // Get the window
        static pawsWidget* credits = NULL;
        if(!credits)
            credits = paws->FindWidget("CreditsWindow", false);

        if(credits && credits->IsVisible())
            sleeptime = 10;
        else
            sleeptime = 30;
    }
    else
        sleeptime = frameLimit;


    // Here we sacrifice drawing AND loading time
    if(elapsedTime < sleeptime)
        csSleep(sleeptime - elapsedTime);

    elapsed = csGetTicks();

    return true;
}

void psEngine::MuteAllSounds(void)
{
    GetSoundManager()->ToggleSounds(false);
    GetSoundManager()->ToggleMusic(false);
    GetSoundManager()->ToggleActions(false);
    GetSoundManager()->ToggleGUI(false);
    paws->ToggleSounds(false);
}

void psEngine::UnmuteAllSounds(void)
{

    LoadSoundSettings(false);

    //If we are in the login/loading/credits/etc. screen we have to force the music to start again.
    if(loadstate != LS_DONE) 
    {
        const char* bgMusic = NULL;
        bgMusic = GetSoundManager()->GetSongName();
        if (bgMusic == NULL)
            return;

        GetSoundManager()->OverrideBGSong(bgMusic);
    }
}

// ----------------------------------------------------------------------------

const char* psEngine::FindCommonString(unsigned int cstr_id)
{
   if (!celclient)
        return "";

   psClientDR * clientDR = celclient->GetClientDR();
   if (!clientDR)
        return "";

   return clientDR->GetMsgStrings()->Request(cstr_id);
}

csStringID psEngine::FindCommonStringId(const char *str)
{
    if (!celclient)
        return csInvalidStringID;

    psClientDR * clientDR = celclient->GetClientDR();
    if (!clientDR)
        return csInvalidStringID;

    return clientDR->GetMsgStrings()->Request(str);
}

// ----------------------------------------------------------------------------

inline void psEngine::UpdatePerFrame()
{
    if (!celclient)
        return;

    // Must be in PostProcess for accurate DR updates
    if (celclient->GetClientDR())
        celclient->GetClientDR()->CheckDeadReckoningUpdate();

    if (celclient->GetMainPlayer())
    {
        celclient->GetClientDR()->CheckSectorCrossing(celclient->GetMainPlayer());
        celclient->PruneEntities();    // Prune CD-intensive entities by disabling CD

        // Update Stats for Player
        celclient->GetMainPlayer()->GetVitalMgr()->Predict( csGetTicks(),"Self" );

        // Update stats for Target if Target is there and has stats
        GEMClientObject * target = psengine->GetCharManager()->GetTarget();
        if (target && target->GetType() != -2 )
        {
            GEMClientActor * actor = dynamic_cast<GEMClientActor*>(target);
            if (actor)
                actor->GetVitalMgr()->Predict(csGetTicks(),"Target");
        }
    }
}

// ----------------------------------------------------------------------------

void psEngine::QuitClient()
{
    // Only run through the shut down procedure once
    static bool alreadyQuitting = false;
    if (alreadyQuitting)
    { 
        return;
    }
    
    alreadyQuitting = true;

    loadstate = LS_NONE;

    #if defined(USE_WIN32_MINIDUMP) && !defined(CS_DEBUG)
        // If we're in release mode, there's no sense in bothering with exit crashes...
        minidumper.SetCrashAction(PSCrashActionIgnore);
    #endif

    csRef<iConfigManager> cfg( csQueryRegistry<iConfigManager> (object_reg) );
    if (cfg)
    {
        cfg->Save();
    }        

    Disconnect(true);

    queue->GetEventOutlet()->Broadcast(event_quit);
}

void psEngine::Disconnect(bool final)
{
    netmanager->SendDisconnect(final);
    netmanager->Disconnect();
}

// ----------------------------------------------------------------------------

void psEngine::AddLoadingWindowMsg(const csString & msg)
{
    pawsLoadWindow* window = static_cast <pawsLoadWindow*> (paws->FindWidget("LoadWindow"));
    if (window != NULL)
    {
        window->AddText( paws->Translate(msg) );
        ForceRefresh();
    }
}

/*
 * When loading this is called several times whenever there is a spare moment
 * in the event queue.  When it is called it does one of the cases and breaks out.
 */
void psEngine::LoadGame()
{
    switch(loadstate)
    {
    case LS_ERROR:
    case LS_NONE:
        return;

    case LS_LOAD_SCREEN:
    {
        paws->LoadWidget("data/gui/loadwindow.xml");
        LoadPawsWidget( "Active Magic window",     "data/gui/activemagicwindow.xml" );
        HideWindow("ActiveMagicWindow");
        
        pawsLoadWindow* window = dynamic_cast <pawsLoadWindow*> (paws->FindWidget("LoadWindow"));
        if (!window)
        {
            FatalError("Widget: LoadWindow could not be found or is not a pawsLoadWindow widget.");
            return;
        }

        AddLoadingWindowMsg( "Loading game" );
        ForceRefresh();

        // Request MOTD
        psMOTDRequestMessage motdRe;
        GetMsgHandler()->SendMessage(motdRe.msg);

        loadstate = LS_INIT_ENGINE;
        break;
    }
    
    case LS_INIT_ENGINE:
    {
        if (charController == NULL)
        {
            charController = new psCharController(nameRegistry);
            if (!charController->Initialize())
            {
                FatalError("Couldn't initialize the character controller!");
                return;
            }
        } 

        // load chat bubbles
        if (!chatBubbles)
        {
            chatBubbles = new psChatBubbles();
            chatBubbles->Initialize(this);
        }

        loadstate = LS_REQUEST_WORLD;
        break;
    }

    case LS_REQUEST_WORLD:
    {
        if (!charController->IsReady())
            return;  // Wait for character modes

        celclient->RequestServerWorld();

        loadtimeout = csGetTicks () + cfgmgr->GetInt("PlaneShift.Client.User.Persisttimeout", 60) * 1000;

        if (GetSoundStatus())
        {
            soundmanager->StartMapSoundSystem();
        }

        AddLoadingWindowMsg( "Requesting connection" );
        ForceRefresh();

        loadstate = LS_SETTING_CHARACTERS;
        break;
    }

    case LS_SETTING_CHARACTERS:
    {
        if ( !celclient->IsReady() )
        {
            if (celclient->GetRequestStatus() != 0 && csGetTicks() > loadtimeout)
            {
                csReport (object_reg, CS_REPORTER_SEVERITY_NOTIFY, PSAPP,
                    "PSLoader: timeout!");

                FatalError("Timeout waiting for the world to load");
            }

            // We don't have the main actor or world yet
            return;
        }

        // Wait for the map to be loaded
        if (!HasLoadedMap())
            return;

        // Set controlled actor and map controls
        charController->GetMovementManager()->SetActor();

        // Init camera with controlled actor
        camera->InitializeView( celclient->GetMainPlayer() );

        // Get stats
        psStatsMessage statmsg;
        GetMsgHandler()->SendMessage(statmsg.msg);

        AddLoadingWindowMsg( "Getting entities" );
        AddLoadingWindowMsg( "Loading GUI" );
        ForceRefresh();

        loadstate = LS_CREATE_GUI;
        break;
    }

    case LS_CREATE_GUI:
    {
        // Must be first!!!
        if (!paws->LoadWidget( "data/gui/control.xml" ))
        {
            GetMainWidget()->LockPlayer();
            FatalError("The toolbar couldn't be loaded\nPlease check your logs");
            return;
        }

        LoadPawsWidget( "Status window",           "data/gui/infowindow.xml" );
        LoadPawsWidget( "Ignore window",           "data/gui/ignorewindow.xml" );
        LoadPawsWidget( "Communications window",   "data/gui/chat.xml" );
        LoadPawsWidget( "Inventory window",        "data/gui/inventory.xml" );
        LoadPawsWidget( "Item description window", "data/gui/itemdesc.xml" );
        LoadPawsWidget( "Container description window",    "data/gui/containerdesc.xml" );
        LoadPawsWidget( "Book Reading window", "data/gui/readbook.xml" );
        LoadPawsWidget( "Interact menu",           "data/gui/interact.xml" );
        LoadPawsWidget( "Yes / No dialog",         "data/gui/yesno.xml" );
        LoadPawsWidget( "Group status window",     "data/gui/group.xml" );
        LoadPawsWidget( "Exchange window",         "data/gui/exchange.xml" );
        LoadPawsWidget( "Glyph window",            "data/gui/glyph.xml" );
        LoadPawsWidget( "Merchant window",         "data/gui/merchant.xml" );
        LoadPawsWidget( "Petition window",         "data/gui/petition.xml" );
        LoadPawsWidget( "Petititon GM window",     "data/gui/petitiongm.xml" );
        LoadPawsWidget( "Spellbook window",        "data/gui/spellwindow.xml" );
        LoadPawsWidget( "Shortcut window",         "data/gui/shortcutwindow.xml" );
        LoadPawsWidget( "GM GUI window",           "data/gui/gmguiwindow.xml" );
        LoadPawsWidget( "Control panel",           "data/gui/configwindow.xml" );
        LoadPawsWidget( "Fingering window",        "data/gui/fingering.xml" );
        LoadPawsWidget( "Guild information window","data/gui/guildwindow.xml" );
        LoadPawsWidget( "Loot window",             "data/gui/loot.xml" );
        LoadPawsWidget( "Skills window",           "data/gui/skillwindow.xml" );
        LoadPawsWidget( "PetStats window",         "data/gui/petstatwindow.xml" );
        LoadPawsWidget( "Quest notebook window",   "data/gui/questnotebook.xml" );
        LoadPawsWidget( "Spell cast status window","data/gui/spellcancelwindow.xml" );
        LoadPawsWidget( "Help window",             "data/gui/helpwindow.xml" );
        LoadPawsWidget( "Buddy window",            "data/gui/buddy.xml" );
        LoadPawsWidget( "Action Location window",  "data/gui/actionlocation.xml" );
        LoadPawsWidget( "Details window",          "data/gui/detailwindow.xml" );
        LoadPawsWidget( "Character description window",    "data/gui/chardescwindow.xml" );
        LoadPawsWidget( "Quest reward window",     "data/gui/questrewardwindow.xml" );
        LoadPawsWidget( "GM Spawn interface",      "data/gui/gmspawn.xml" );
        //LoadPawsWidget( "Active Magic window",     "data/gui/activemagicwindow.xml" );
        LoadPawsWidget( "Small Inventory Window",  "data/gui/smallinventory.xml" );
        LoadPawsWidget( "GM Action Location Edit", "data/gui/gmaddeditaction.xml" );
        LoadPawsWidget( "Crafting",                "data/gui/craft.xml");
        LoadPawsWidget( "Tutorial",                "data/gui/tutorial.xml");
        LoadPawsWidget( "Sketch",                  "data/gui/illumination.xml");
        LoadPawsWidget( "GameBoard",               "data/gui/gameboard.xml");
        LoadPawsWidget( "Writing window",          "data/gui/bookwriting.xml");

        LoadCustomPawsWidgets("/this/data/gui/customwidgetslist.xml");

        HideWindow("DescriptionEdit");
        HideWindow("ItemDescWindow");
        HideWindow("BookReadingWindow");
        HideWindow("ContainerDescWindow");
        HideWindow("GroupWindow");
        HideWindow("InteractWindow");
        HideWindow("ExchangeWindow");
        HideWindow("MerchantWindow");
        HideWindow("ShortcutEdit");
        HideWindow("FingeringWindow");
        HideWindow("QuestEdit");
        HideWindow("LootWindow");
        HideWindow("ActionLocationWindow");
        HideWindow("DetailWindow");
        HideWindow("QuestRewardWindow");
        HideWindow("SpellCancelWindow");
        HideWindow("GMSpawnWindow");
        HideWindow("SmallInventoryWindow");
        HideWindow("AddEditActionWindow");
        HideWindow("GmGUI");
        HideWindow("PetStatWindow");
        HideWindow("WritingWindow");

        paws->GetMouse()->ChangeImage("Skins Normal Mouse Pointer");

        // If we had any problems, show them
        if ( wdgProblems.GetSize() > 0 )
        {
            printf("Following widgets failed to load:\n");

            // Loop through the array
            for (size_t i = 0;i < wdgProblems.GetSize(); i++)
            {
                csString str = wdgProblems.Get(i);
                if (str.Length() > 0)
                    printf("%s\n",str.GetData());
            }

            GetMainWidget()->LockPlayer();
            FatalError("One or more widgets failed to load\nPlease check your logs");
            return;
        }

        celclient->SetPlayerReady(true);

        psClientStatusMessage statusmsg(true);
        statusmsg.SendMessage();

        //GetCmdHandler()->Execute("/who");
        GetCmdHandler()->Execute("/tip");

        // load the mouse options if not loaded allready. The GetMouseBinds
        // function will load them if they are requested before this code
        // is executed.
        if (!mouseBinds)
        {
            mouseBinds = GetMouseBinds();
        }

        // Set the focus to the main widget
        paws->SetCurrentFocusedWidget(GetMainWidget());

        loadstate = LS_DONE;
        break;
    }


    default:
        loadstate = LS_NONE;
    }

    csReport(object_reg, CS_REPORTER_SEVERITY_NOTIFY, PSAPP,
        "PSLoader: step %d: success", loadstate);

    if (loadstate==LS_DONE)
    {
        gameLoaded = true;
        paws->FindWidget("LoadWindow")->Hide();
    }
}

void psEngine::HideWindow(const csString & widgetName)
{
    pawsWidget * wnd = paws->FindWidget(widgetName);
    if (wnd != NULL)
        wnd->Hide();
}

psMouseBinds* psEngine::GetMouseBinds()
{
    // If not loaded load the mouse binds. They should normaly be loaded
    // from the load GUI step, but this function have been observerd called
    // before.
    if (!mouseBinds)
    {
        mouseBinds = new psMouseBinds();

        csString fileName = "/planeshift/userdata/options/mouse.xml";
        if (!vfs->Exists(fileName))
        {
            fileName = "/planeshift/data/options/mouse_def.xml";
        }

        if ( !mouseBinds->LoadFromFile( object_reg, fileName))
            Error1("Failed to load mouse options");
    }
    return mouseBinds;
}

bool psEngine::GetFileNameByFact(csString factName, csString& fileName)
{
    csString notfound = "MESH_NOT_FOUND", tempfilename;
    tempfilename = factfilenames.Get(factName, notfound);
    if (tempfilename == notfound)
        return false;
    fileName = tempfilename;
    return true;
}

inline void psEngine::PreloadModels()
{
    /// Models are already finished loaded
    if ( modelsLoaded || !okToLoadModels )
    {
        return;
    }

    static pawsProgressBar* bar = NULL;
    if (!bar)
        bar = (pawsProgressBar*)paws->FindWidget("SplashWindow")->FindWidget("Progress");
    bar->Show();

    /// Load the models
    if (modelToLoad == (size_t)-1)
    {
        // Load the cal3d models
        PreloadSubDir("");
        // Load the items
        PreloadItemsDir();
        modelToLoad = 0;

        bar->SetTotalValue( modelnames.GetSize() );
    }
    else
    {
        if (modelnames.GetSize()-1 < modelToLoad)
        {
            Debug1(LOG_ADMIN,0, "Preloading complete");

            delete paws->FindWidget("SplashWindow");
            paws->LoadWidget("data/gui/loginwindow.xml");

            paws->GetMouse()->ChangeImage("Standard Mouse Pointer");
            paws->GetMouse()->Hide(false);
            
            modelsLoaded = true;
            return;
        }

        // Now build the index, wait for each model to load (timeout after 20 sec).
        size_t counter = 0;
        while(!cachemanager->GetFactoryEntry(modelnames.Get(modelToLoad))->factory && counter < 200)
        {
            counter++;
            csSleep(100);
        }

        if(counter == 200)
        {
            Error2("Model %s didn't seem to load!", modelnames.Get(modelToLoad));
        }

        modelToLoad++;
        bar->SetCurrentValue( modelToLoad );
    }
}

void psEngine::BuildFactoryList()
{
    for (size_t i = 0 ; i < modelnames.GetSize() ; i++)
    {
        csString filename = modelnames.Get(i);

        // Check if the file exists
        if (!GetVFS()->Exists(filename))
        {
            Error2("Couldn't find file %s",filename.GetData());
            continue;
        }

        csRef<iDocument> doc = ParseFile(GetObjectRegistry(),filename);
        if (!doc)
        {
            Error2("Couldn't parse file %s",filename.GetData());
            continue;
        }

        csRef<iDocumentNode> root = doc->GetRoot();
        if (!root)
        {
            Error2("The file(%s) doesn't have a root",filename.GetData());
            continue;
        }

        csRef<iDocumentNode> meshNode;
        csRef<iDocumentNode> libNode = root->GetNode("library");
        if (libNode)
            meshNode = libNode->GetNode("meshfact");
        else
            meshNode = root->GetNode("meshfact");
        if (!meshNode)
        {
            Error2("The file(%s) doesn't have a meshfact or library node", filename.GetData());
            continue;
        }

        csString factname = meshNode->GetAttributeValue("name");
        factfilenames.PutUnique(factname, filename);
    }
}

void psEngine::PreloadItemsDir()
{
    /// Models are already finished loaded
    if ( modelsLoaded )
        return;

    if (okToLoadModels)
    {
        csRef<iDataBuffer> xpath = vfs->ExpandPath("/planeshift/");
        csRef<iStringArray> files = vfs->FindFiles( **xpath );

        if (!files)
            return;

        for (size_t i=0; i < files->GetSize(); i++)
        {
            csString filename = files->Get(i);

            // Do it this way because if we do it by recursing (whatever) it will loop through a lot of
            // stuff we don't want to load
            if (filename.GetAt(filename.Length()-1) == '/')
            {
                csRef<iDataBuffer> xpath2 = vfs->ExpandPath(filename);
                csRef<iStringArray> files2 = vfs->FindFiles( **xpath2 );

                if (files2)
                {
                    for (size_t i=0; i < files2->GetSize(); i++)
                    {
                        csString localName= files2->Get(i);
                        if (localName.Slice(localName.Length()-4,4) != ".spr")
                            continue;

                        modelnames.Push(localName);
                    }
                }
                continue;
            }

            if (filename.Slice(filename.Length()-4,4) != ".spr")
                continue;

            modelnames.Push(filename);
        }
    }
}

void psEngine::PreloadSubDir(const char* dirname)
{
    /// Models are already finished loaded
    if ( modelsLoaded )
        return;

    if (okToLoadModels && modelToLoad == (size_t)-1)
    {
        csRef<iDataBuffer> xpath = vfs->ExpandPath("/planeshift/models/" + csString(dirname));
        csRef<iStringArray> files = vfs->FindFiles( **xpath );

        if (!files)
            return;

        for (size_t i=0; i < files->GetSize(); i++)
        {
            csString filename = files->Get(i);
            size_t length = strlen("/planeshift/models/");
            csString localName = filename.Slice(length,filename.Length() - length);

            if (localName.GetAt(localName.Length()-1) == '/')
            {
                PreloadSubDir(localName);
                continue;
            }

            if (localName.Slice(localName.Length()-6,6) != ".cal3d")
                continue;

            modelnames.Push("/planeshift/models/"+localName.Slice(0,localName.Length()));
        }
    }
}

size_t psEngine::GetTime()
{
    return modehandler->GetTime();
}


void psEngine::SetDuelConfirm(int confirmType)
{
    confirmation = confirmType;

    csString xml;
    xml = "<PvP>\n";
    xml += "    <Confirmation value=\"";
    xml.Append(confirmation);
    xml += "\"/>\n";
    xml += "</PvP>\n";

    vfs->WriteFile("/planeshift/userdata/options/pvp.xml", xml.GetData(), xml.Length());
}

bool psEngine::LoadDuelConfirm()
{
    csRef<iDocument> doc;
    csRef<iDocumentNode> root, mainNode, optionNode;

    csString fileName = "/planeshift/userdata/options/pvp.xml";
    if (!psengine->GetVFS()->Exists(fileName))
    {
        fileName = "/planeshift/data/options/pvp_def.xml";
    }

    doc = ParseFile(object_reg, fileName);
    if (doc == NULL)
    {
        Error2("Failed to parse file %s", fileName.GetData());
        return false;
    }
    root = doc->GetRoot();
    if (root == NULL)
    {
        Error1("pvp.xml has no XML root");
        return false;
    }
    mainNode = root->GetNode("PvP");
    if (mainNode == NULL)
    {
        Error1("pvp.xml has no <PvP> tag");
        return false;
    }

    optionNode = mainNode->GetNode("Confirmation");
    if (optionNode != NULL)
    {
        confirmation = optionNode->GetAttributeValueAsInt("value");
    }

    return true;
}

bool psEngine::LoadSoundSettings(bool forceDef)
{
    csRef<iDocument> doc;
    csRef<iDocumentNode> root, mainNode, optionNode;

    csString fileName;
    if(!forceDef)
        fileName = "/planeshift/userdata/options/sound.xml";

    if (forceDef || !psengine->GetVFS()->Exists(fileName))
    {
        fileName = "/planeshift/data/options/sound_def.xml";
    }

    doc = ParseFile(object_reg, fileName);
    if (doc == NULL)
    {
        Error2("Failed to parse file %s", fileName.GetData());
        return false;
    }
    root = doc->GetRoot();
    if (root == NULL)
    {
        Error2("%s has no XML root",fileName.GetData());
        return false;
    }
    mainNode = root->GetNode("sound");
    if (mainNode == NULL)
    {
        Error2("%s has no <sound> tag",fileName.GetData());
        return false;
    }

    // load and apply the settings
    optionNode = mainNode->GetNode("ambient");
    if (optionNode != NULL)
        GetSoundManager()->ToggleSounds(optionNode->GetAttributeValueAsBool("on",true));

    optionNode = mainNode->GetNode("actions");
    if (optionNode != NULL)
        GetSoundManager()->ToggleActions(optionNode->GetAttributeValueAsBool("on",true));

    optionNode = mainNode->GetNode("music");
    if (optionNode != NULL)
        GetSoundManager()->ToggleMusic(optionNode->GetAttributeValueAsBool("on",true));

    optionNode = mainNode->GetNode("gui");
    if (optionNode != NULL)
        paws->ToggleSounds(optionNode->GetAttributeValueAsBool("on",true));

    optionNode = mainNode->GetNode("volume");
    if (optionNode != NULL)
    {
        // Failsafe
        int volume = optionNode->GetAttributeValueAsInt("value");
        if(volume == 0)
        {
            printf("Invalid sound setting, setting to 100%%\n");
            volume = 100;
        }

        GetSoundManager()->SetVolume(float(volume)/100);
    }

    optionNode = mainNode->GetNode("musicvolume");
    if (optionNode != NULL)
    {
        // Failsafe
        int volume = optionNode->GetAttributeValueAsInt("value");
        if(volume == 0)
        {
            printf("Invalid sound setting, setting to 100%%\n");
            volume = 100;
        }

        GetSoundManager()->SetMusicVolume(float(volume)/100);
    }

    optionNode = mainNode->GetNode("ambientvolume");
    if (optionNode != NULL)
    {
        // Failsafe
        int volume = optionNode->GetAttributeValueAsInt("value");
        if(volume == 0)
        {
            printf("Invalid sound setting, setting to 100%%\n");
            volume = 100;
        }

        GetSoundManager()->SetAmbientVolume(float(volume)/100);
    }

    optionNode = mainNode->GetNode("actionsvolume");
    if (optionNode != NULL)
    {
        // Failsafe
        int volume = optionNode->GetAttributeValueAsInt("value");
        if(volume == 0)
        {
            printf("Invalid sound setting, setting to 100%%\n");
            volume = 100;
        }

        GetSoundManager()->SetActionsVolume(float(volume)/100);
    }

    optionNode = mainNode->GetNode("guivolume");
    if (optionNode != NULL)
    {
        // Failsafe
        int volume = optionNode->GetAttributeValueAsInt("value");
        if(volume == 0)
        {
            printf("Invalid sound setting, setting to 100%%\n");
            volume = 100;
        }

        paws->SetVolume(float(volume)/100);
    }

    optionNode = mainNode->GetNode("muteonfocusloss");
    if (optionNode != NULL)
        SetMuteSoundsOnFocusLoss(optionNode->GetAttributeValueAsBool("on", false));

    optionNode = mainNode->GetNode("loopbgm");
    if (optionNode)
        GetSoundManager()->ToggleLoop(optionNode->GetAttributeValueAsBool("on", false));

    optionNode = mainNode->GetNode("combatmusic");
    if (optionNode)
        GetSoundManager()->ToggleCombatMusic(optionNode->GetAttributeValueAsBool("on", true));

    return true;
}

bool psEngine::LoadPawsWidget(const char* title, const char* filename)
{
    bool loaded = paws->LoadWidget(filename);

    if (!loaded)
    {
        wdgProblems.Push(title);
    }

    return loaded;
}

bool psEngine::LoadCustomPawsWidgets(const char * filename)
{
    csRef<iDocument> doc;
    csRef<iDocumentNode> root;
    csRef<iDocumentSystem>  xml;
    const char* error;

    csRef<iDataBuffer> buff = vfs->ReadFile(filename);
    if (buff == NULL)
    {
        Error2("Could not find file: %s", filename);
        return false;
    }
    xml = psengine->GetXMLParser ();
    doc = xml->CreateDocument();
    assert(doc);
    error = doc->Parse( buff );
    if ( error )
    {
        Error3("Parse error in %s: %s", filename, error);
        return false;
    }
    if (doc == NULL)
        return false;

    root = doc->GetRoot();
    if (root == NULL)
    {
        Error2("No root in XML %s", filename);
        return false;
    }

    csRef<iDocumentNode> customWidgetsNode = root->GetNode("custom_widgets");
    if (!customWidgetsNode)
    {
        Error2("No custom_widgets node in %s", filename);
        return false;
    }

    csRef<iDocumentNodeIterator> nodes = customWidgetsNode->GetNodes("widget");
    while (nodes->HasNext())
    {
        csRef<iDocumentNode> widgetNode = nodes->Next();

        csString name = widgetNode->GetAttributeValue("name");
        csString file = widgetNode->GetAttributeValue("file");

        LoadPawsWidget(name, file);
    }
    return true;
}

void psEngine::FatalError(const char* msg)
{
    loadstate = LS_ERROR;
    Bug1(msg);
    
    pawsQuitInfoBox* quitinfo = (pawsQuitInfoBox*)(paws->FindWidget("QuitInfoWindow"));
    if (quitinfo)
    {
        quitinfo->SetBox(msg);
        quitinfo->Show();
    }
    else
    {
        // Force crash to give dump here
        int* crash = NULL;
        *crash=0;
        QuitClient();
    }
}

void psEngine::setLimitFPS(int a)
{
    frameLimit = ( 1000 / a );
    cfgmgr->SetInt("Video.frameLimit", frameLimit);
    cfgmgr->Save();// need to save this every time
}

// ----------------------------------------------------------------------------

/**
 *
 * main()
 * Main function. Like any other applications, this is the entry point of the
 * program. The SysSystemDriver is created and initialized here. It also
 * creates psEngine and initialize it. Then it calls the main loop.
 *
 **/

psEngine * psengine;


int main (int argc, char *argv[])
{
    psCSSetup* CSSetup = new psCSSetup( argc, argv, "/this/psclient.cfg", CONFIGFILENAME );
    iObjectRegistry* object_reg = CSSetup->InitCS();

    pslog::Initialize (object_reg);
    pslog::disp_flag[LOG_LOAD] = true;

    // Create our application object
    psengine = new psEngine(object_reg);

    
    // Initialize engine
    if (!psengine->Initialize(0))
    {
        csReport (object_reg, CS_REPORTER_SEVERITY_ERROR,
                  PSAPP, "Failed to init app!");
        PS_PAUSEEXIT(1);
    }

    // start the main event loop
    csDefaultRunLoop(object_reg);

    psengine->Cleanup();
    

    Notify1(LOG_ANY,"Removing engine...");

    // remove engine before destroying Application
    delete psengine;
    psengine = NULL;

    delete CSSetup;

    Notify1(LOG_ANY,"Destroying application...");

    csInitializer::DestroyApplication(object_reg);

    return 0;
}
