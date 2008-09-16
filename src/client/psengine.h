/*
 * psengine.h
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
#ifndef __ENGINE_H__
#define __ENGINE_H__

#include <iutil/eventh.h>
#include <csutil/sysfunc.h>
#include <csutil/csstring.h>
#include <csutil/ref.h>
#include <csutil/leakguard.h>
#include <ivaria/profile.h>

#include "paws/pawsmanager.h"
#include "paws/psmousebinds.h"
#include "psclientchar.h"
#include "psinventorycache.h"

#include "util/prb.h"
#include "util/slots.h"

class psCelClient;
class psClientMsgHandler;
class psClientCharManager;
struct iCommandLineParser;
struct iConfigManager;
struct iDialogManager;
struct iEngine;
struct iEvent;
struct iEventQueue;
struct iLoader;
struct iMeshFactoryWrapper;
struct iNetManager;
struct iSoundManager;
struct iVFS;
struct iVirtualClock;
struct iWorld;
class ModeHandler;
class ActionHandler;
class ZoneHandler;
class psCharController;
class psCamera;
class psEffectManager;
class psChatBubbles;
class psCal3DCallbackLoader;
class psMainWidget;
class ClientCacheManager;
class psQuestionClient;
class psOptions;
class MaterialManager;

// Networking classes
class psNetConnection;
class psClientMsgHandler;
class psAuthenticationClient;
class psNetManager;
class psSlotManager;

class GEMClientObject;
class GEMClientActor;
class GUIHandler;

struct DelayedLoader
{
    virtual void CheckMeshLoad() = 0;
    virtual ~DelayedLoader() { };
};

/**
 * psEngine
 * This is the main class that contains all the object. This class responsible
 * for the whole game functionality.
 */
class psEngine
{
public:
    enum LoadState
    {
        LS_NONE,
        LS_LOAD_SCREEN,
        LS_INIT_ENGINE,
        LS_REQUEST_WORLD,
        LS_SETTING_CHARACTERS,
        LS_CREATE_GUI,
        LS_DONE,
        LS_ERROR
    };

    /// Default constructor. It calls csApp constructor.
    psEngine (iObjectRegistry *object_reg);

    /// Destructor
    virtual ~psEngine ();

    /** Creates and loads other interfaces.  There are 2 levels for the setup.
      * @param level The level we want to load  
      * At level 0:
      * <UL>
      * <LI> Various Crystal Space plugins are loaded.
      * <LI> Crash/Dump system intialized. 
      * <LI> Sound manager is loaded.
      * <LI> Log system loaded. 
      * <LI> PAWS windowing system and skins setup.
      * </UL>
      * At level 1:
      * <UL>
      * <LI> The network manager is setup.
      * <LI> SlotManager, ModeManager, ActionHandler, ZoneManager, CacheManager setup.
      * <LI> CEL setup. 
      * <LI> Trigger to start loading models fired.
      * </UL>
      * 
      * @return  True if the level was successful in loading/setting up all components. 
      */
    bool Initialize (int level);

    /**
     * Clean up stuff.
     * Needs to be called before the engine is destroyed, since some objects
     * owned by the engine require the engine during their destruction.
     */
    void Cleanup();

    /// Do anything that needs to happen after the frame is rendered each time.
    void UpdatePerFrame();

    /// Wait to finish drawing the current frame.
    inline void FinishFrame()
    {
        g3d->FinishDraw();
        g3d->Print(NULL);
    }

    /**
     * Everything is event base for csApp based system. This method is called
     * when any event is triggered such as when a button is pushed, mouse move,
     * or keyboard pressed.
     */
    bool HandleEvent (iEvent &Event);

    /// Load game calls LoadWorld and also load characters, cameras, items, etc.
    void LoadGame();

    /// check if the game has been loaded or not
    bool IsGameLoaded() { return gameLoaded; };

    // References to plugins
    iObjectRegistry*      GetObjectRegistry()     { return object_reg; }
    iEventNameRegistry*   GetEventNameRegistry()  { return nameRegistry; }
    iEngine*              GetEngine()             { return engine; }
    iGraphics3D*          GetG3D()                { return g3d; }
    iGraphics2D*          GetG2D()                { return g2d; }
    iTextureManager*      GetTextureManager()     { return txtmgr; }
    iVFS*                 GetVFS()                { return vfs; }
    iVirtualClock*        GetVirtualClock()       { return vc; }
    iDocumentSystem*      GetXMLParser()          { return xmlparser; }
    iLoader*              GetLoader()             { return loader; }
    iSoundManager*        GetSoundManager()       { return soundmanager; }
    iNetManager*          GetNetManager();
    iConfigManager*       GetConfig()             { return cfgmgr; };  ///< config file

    csRandomGen& GetRandomGen() { return random; }
    float GetRandom() { return random.Get(); }

    MsgHandler*            GetMsgHandler();
    CmdHandler*            GetCmdHandler();
    psSlotManager*         GetSlotManager()    { return slotManager;}
    ClientCacheManager*    GetCacheManager()   { return cachemanager; }
    psClientCharManager*   GetCharManager()    { return charmanager; }
    psCelClient*           GetCelClient()      { return celclient; };
    psMainWidget*          GetMainWidget()     { return mainWidget; }
    psEffectManager*       GetEffectManager()  { return effectManager; }
    psChatBubbles*         GetChatBubbles()    { return chatBubbles; }
    psOptions*             GetOptions()        { return options; }
    ModeHandler*           GetModeHandler()    { return modehandler; }
    ActionHandler*         GetActionHandler()  { return actionhandler; }
    psCharController*      GetCharControl()    { return charController; }
    psMouseBinds*          GetMouseBinds();
    psCamera*              GetPSCamera()       { return camera; }

    /// Access the player's petitioner target
    void SetTargetPetitioner(const char * pet) { targetPetitioner = pet; }
    const char * GetTargetPetitioner() { return targetPetitioner; }

    /// Quits client
    void QuitClient();

    /**
     * Formally disconnects a client and allows time for an info box to be shown
     * @param final Set to true if this is the final quit.
     */
    void Disconnect(bool final);

    /// Tell the engine to start the load proceedure.
    void StartLoad() { loadstate = LS_LOAD_SCREEN; }
    LoadState loadstate;

    /// Main event handler for psEngine

    class EventHandler : public scfImplementation1< EventHandler, iEventHandler >
    {
    private:
        psEngine* parent;

    public:
        EventHandler(psEngine* p)
            : scfImplementationType(this), parent(p) {}

        virtual ~EventHandler() {}

        virtual bool HandleEvent(iEvent& ev)
        {
            return parent->HandleEvent(ev);
        }

        CS_EVENTHANDLER_NAMES ("planeshift.engine.int")
        virtual const csHandlerID * GenericPrec(
            csRef<iEventHandlerRegistry>&,
            csRef<iEventNameRegistry>&,
            csEventID) const;
        virtual const csHandlerID * GenericSucc(
            csRef<iEventHandlerRegistry>&,
            csRef<iEventNameRegistry>&,
            csEventID) const;
        CS_EVENTHANDLER_DEFAULT_INSTANCE_CONSTRAINTS;
    };

    csRef<EventHandler> scfiEventHandler;
    csHandlerID EHConstraintsFramePrec[2];

    size_t GetTime();

    float GetBrightnessCorrection() { return BrightnessCorrection; }
    void SetBrightnessCorrection(float B) { BrightnessCorrection = B; }
    void UpdateLights();

    float GetKFactor() { return KFactor; }
    void SetKFactor(float K) { KFactor = K; }

    iEvent* GetLastEvent() { return lastEvent; }

    // Functions to set and get frameLimit converted into a value of FPS for users
    void setLimitFPS(int);
    int getLimitFPS() { return maxFPS; }

    /// Sets the duel confirmation type
    void SetDuelConfirm(int confirmType);
    /// Loads the duel confirmation type
    bool LoadDuelConfirm();
    /// Gets the duel confirmation type
    int GetDuelConfirm() { return confirmation; }

    /** Loads and applies the sound settings
     * @param True if you want to load the default settings
     * @return true if it was successfully done
     */
    bool LoadSoundSettings(bool forceDef);

    /// Checks if the client has loaded its map
    bool HasLoadedMap() { return loadedMap; }

    /// Sets the HasLoadedMap value
    void SetLoadedMap(bool value) { loadedMap = value; }

    /// Checks if the client had any errors during the loading state
    bool LoadingError() { return loadError; }

    /// Sets the LoadError value
    void LoadError(bool value) { loadError = value; }

    /** Loads a widget
      * @param Title to show in the logs if an error occurs
      * @param XML file
      * @return Append this to a var and check it after all loading is done
      */
    bool LoadPawsWidget(const char* title, const char* filename);

    /** Loads custom paws widgets that are specified as a list in an xml file.
     *   \param filename The xml file that holds the list of custom widgets.
     */
    bool LoadCustomPawsWidgets(const char * filename);

    SlotNameHash slotName;

    void AddLoadingWindowMsg(const csString & msg);

    /** Shortcut to pawsQuitInfoBox
      * @param Message to display before quiting
      */
    void FatalError(const char* msg);

    /// Logged in?
    bool IsLoggedIn() { return loggedIn; }
    void SetLoggedIn(bool v) { loggedIn = v; }

    /** Set the number of characters this player has.
      * Used to wait for full loading of characters in selection screen.
      */
    void SetNumChars(int chars) { numOfChars = chars; }

    /// Get the number of characters this player should have.
    int GetNumChars() { return numOfChars; }

    /// Force the next frame to get drawn. Used when updating LoadWindow.
    void ForceRefresh() { elapsed = 0; }

    /// Set Guild Name here for use with Entity labels
    void SetGuildName(const char *name) { guildname = name; }

    /// Get GuildName
    const char *GetGuildName() { return guildname; }

    /** Gets whether sounds should be muted when the application loses focus.
     * @return true if sounds should be muted, false otherwise
     */
    bool GetMuteSoundsOnFocusLoss(void) const { return muteSoundsOnFocusLoss; }
    /** Sets whether sounds should be muted when the application loses focus.
     * @param value true if sounds should be muted, false otherwise
     */
    void SetMuteSoundsOnFocusLoss(bool value) { muteSoundsOnFocusLoss = value; }

    /// Mute all sounds.
    void MuteAllSounds(void);
    /// Unmute all sounds.
    void UnmuteAllSounds(void);

    /** FindCommonString
     * @param cstr_id The id of the common string to find.
     */
    const char* FindCommonString(unsigned int cstr_id);

    /**
     * FindCommonStringId
     * @param[in] str The string we are looking for
     * @return The id of the common string or csInvalidStringID if not found
     */
    csStringID FindCommonStringId(const char *str);


    ///Get the status of the sound plugin, if available or not.
    bool GetSoundStatus() {return soundOn;}

    /// get the inventory cache
    psInventoryCache* GetInventoryCache(void) { return inventoryCache; }

    /// Are we preloading models?
    bool PreloadingModels() { return preloadModels; }

    /// Unload order.
    bool UnloadingLast() { return unloadLast; }

    /// The graphics features that are enabled/disabled.
    uint GetGFXFeatures() { return gfxFeatures; }

    void RegisterDelayedLoader(DelayedLoader* obj) { delayedLoaders.Push(obj); }
    void UnregisterDelayedLoader(DelayedLoader* obj) { delayedLoaders.Delete(obj); }

private:
    // Load the log report settings from the config file.
    void LoadLogSettings();

    /// queries all needed plugins
    bool QueryPlugins();

    /// This adds more factories to the standard PAWS ones.
    void DeclareExtraFactories();

    void HideWindow(const csString & widgetName);

    /// Limits the frame rate either by returning false or sleeping
    bool FrameLimit();

    /* plugins we're using... */
    csRef<iObjectRegistry>    object_reg;   ///< The Object Registry
    csRef<iEventNameRegistry> nameRegistry; ///< The name registry.
    csRef<iEngine>            engine;       ///< Engine plug-in handle.
    csRef<iConfigManager>     cfgmgr;       ///< Config Manager
    csRef<iTextureManager>    txtmgr;       ///< Texture Manager
    csRef<iVFS>               vfs;          ///< Virtual File System
    csRef<iGraphics2D>        g2d;          ///< 2d canvas
    csRef<iGraphics3D>        g3d;          ///< 3d canvas
    csRef<iSoundManager>      soundmanager; ///< PS Sound manager
    csRef<iEventQueue>        queue;        ///< Event Queue
    csRef<iVirtualClock>      vc;           ///< Clock
    csRef<iDocumentSystem>    xmlparser;    ///< XML Parser.
    csRef<iLoader>            loader;       ///< Loader
    csRef<iCommandLineParser> cmdline;      ///< Command line parser
    csRef<iStringSet>         stringset;
    csRandomGen               random;

    ClientCacheManager*       cachemanager; ///< Cache manager
    csRef<psNetManager>       netmanager;   ///< Network manager
    csRef<psCelClient>        celclient;    ///< CEL client
    csRef<ModeHandler>        modehandler;  ///< Handling background audio sent from server, etc.
    csRef<ActionHandler>      actionhandler;
    csRef<ZoneHandler>        zonehandler;  ///< Region/map file memory manager.
    csRef<psCal3DCallbackLoader> cal3DCallbackLoader;
    csRef<MaterialManager>    materialmanager; ///< Handles loading of materials/textures.
    psClientCharManager*      charmanager;  ///< Holds the charactermanager
    GUIHandler*               guiHandler;
    psCharController*         charController;
    psMouseBinds*             mouseBinds;
    psCamera*                 camera;
    csRef<psEffectManager>    effectManager;
    psChatBubbles*            chatBubbles;
    psOptions*                options;
    psSlotManager*            slotManager;
    psQuestionClient*         questionclient;
    PawsManager*              paws;         ///< Hold the ps AWS manager
    psMainWidget*             mainWidget;   ///< Hold the ps overridden version of the desktop
    psInventoryCache*	      inventoryCache;///< inventory cache for client

    /* status, misc. vars */
    bool gameLoaded; ///< determines if the game is loaded or not
    bool modalMenu;
    bool loggedIn;

    csTicks loadtimeout;

    csString lasterror;

    csString guildname;

    /// Used to stop rendering when the window is minimized
    bool drawScreen;

    void BuildFactoryList();
    void PreloadModels();
    void PreloadItemsDir();
    void PreloadSubDir(const char* dirname);
    bool preloadModels;
    uint gfxFeatures;
    bool modelsLoaded;  ///< Tells if the models are finished loading yet.
    size_t modelToLoad; ///< Keeps a count of the models loaded so far.
    bool modelsInit;    ///< True if we've begun the process of loading models.
    csStringArray modelnames;
    csHash<csString, csString> factfilenames;

    public:
        bool GetFileNameByFact(csString factName, csString& fileName);
    private:

    csString targetPetitioner;

    bool okToLoadModels;     ///< Make sure that it is allowed to start preloading
    float BrightnessCorrection;

    float KFactor;           ///< Hold the K factor used for casting spells.

    iEvent* lastEvent;

    /// Confirmation type on duels
    int confirmation;

    bool loadedMap;
    bool loadError; ///< If something happend during loading, it will show
    csArray<csString> wdgProblems; ///< Widget load errors

    /// Stores the number of chars available to this player. Used in loading.
    int numOfChars;

    /// Time at which last frame was drawn.
    csTicks elapsed;

    /// msec to wait between drawing frames
    csTicks frameLimit;

    /// Maximum number of frames per second to draw.
    // Maximum settable value in client/gui/pawsconfigdetails.cpp is 200, so
    // it's more than enough to use an unsigned char to store this value
    unsigned char maxFPS;

    /// Whether sounds should be muted when the application loses focus.
    bool muteSoundsOnFocusLoss;

    /// Define if the sound is on or off from psclient.cfg.
    bool soundOn;

    /// Define what kind of loading we want to do; unload first or unload last.
    bool unloadLast;

    /// Whether or not we're using a threaded loader (will break things if you don't and you set to true).
    bool threadedLoad;
    
    // Event ID cache
    csEventID event_preprocess;
    csEventID event_process;
    csEventID event_finalprocess;
    csEventID event_frame;
    csEventID event_postprocess;
    csEventID event_canvashidden;
    csEventID event_canvasexposed;
    csEventID event_focusgained;
    csEventID event_focuslost;
    csEventID event_quit;

    CS_EVENTHANDLER_NAMES ("planeshift.engine")
    CS_EVENTHANDLER_NIL_CONSTRAINTS

    csArray<DelayedLoader*> delayedLoaders;
};

#endif
