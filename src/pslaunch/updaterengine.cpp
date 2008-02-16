/*
* updaterengine.cpp - Author: Mike Gist
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

#include <csutil/csmd5.h>
#include <csutil/xmltiny.h>

#include <iutil/stringarray.h>

#include "updaterconfig.h"
#include "updaterengine.h"
#include "binarypatch.h"

#ifndef CS_COMPILER_MSVC
#include <unistd.h>
#endif

iObjectRegistry* UpdaterEngine::object_reg = NULL;

UpdaterEngine::UpdaterEngine(const csArray<csString> args, iObjectRegistry* object_reg, const char* appName)
{
    bool* a = new bool;
    bool* b = new bool;
    bool* c = new bool;
    *a = true, *b = false, *c = true;
    csArray<csString>* d = new csArray<csString>;
    hasGUI = false;
    Init(args, object_reg, appName, a, b, c, d, NULL);
}

UpdaterEngine::UpdaterEngine(const csArray<csString> args, iObjectRegistry* _object_reg, const char* _appName,
                             bool *_performUpdate, bool *_exitGui, bool *_updateNeeded, csArray<csString> *_consoleOut,  CS::Threading::Mutex *_mutex)
{
    hasGUI = true;
    Init(args, _object_reg, _appName, _performUpdate, _exitGui, _updateNeeded, _consoleOut, _mutex);
}

void UpdaterEngine::Init(const csArray<csString> args, iObjectRegistry* _object_reg, const char* _appName,
                         bool *_performUpdate, bool *_exitGui, bool *_updateNeeded, csArray<csString> *_consoleOut,  CS::Threading::Mutex *_mutex)
{
    object_reg = _object_reg;
    vfs = csQueryRegistry<iVFS> (object_reg);
    if(!vfs)
    {
        printf("No VFS!\r\n");
        exit(1);
    }
    vfs->ChDir("/this/");
    config = new UpdaterConfig(args, object_reg, vfs);
    fileUtil = new FileUtil(vfs);
    appName = _appName;
    exitGUI = _exitGui;
    updateNeeded = _updateNeeded;
    consoleOut = _consoleOut;
    performUpdate = _performUpdate;
    mutex = _mutex;

    if(vfs->Exists("/this/updater.log"))
    {
        fileUtil->RemoveFile("/this/updater.log");
    }
    log = vfs->Open("/this/log.txt", VFS_FILE_APPEND);
}

UpdaterEngine::~UpdaterEngine()
{
    log = NULL;
    delete fileUtil;
    delete config;
    fileUtil = NULL;
    config = NULL;
    if(!hasGUI)
    {
        delete consoleOut;
        delete updateNeeded;
        delete exitGUI;
        delete performUpdate;
    }
}

void UpdaterEngine::printOutput(const char *string, ...)
{
    if ( mutex )
    {
        mutex->Lock();
    }

    csString outputString;
    va_list args;
    va_start (args, string);
    outputString.FormatV (string, args);
    va_end (args);
    consoleOut->Push(outputString);
    printf("%s", outputString.GetData());
    log->Write(outputString.GetData(), outputString.Length());

    if ( mutex )
    {
        mutex->Unlock();
    }        
}

void UpdaterEngine::checkForUpdates()
{

    // Make sure the old instance had time to terminate (self-update).
    if(config->IsSelfUpdating())
        csSleep(500);

    // Check if the last attempt at a general updated ended in failure.
    if(!config->WasCleanUpdate())
    {
        // Restore config file which gives us the last updated position.
        fileUtil->RemoveFile("/this/updaterinfo.xml");
        fileUtil->MoveFile("/this/updaterinfo.xml.bak", "/this/updaterinfo.xml", true, false);
    }


    // Load current config data.
    csRef<iDocumentNode> root = GetRootNode(UPDATERINFO_FILENAME);
    if(!root)
    {
        printOutput("Unable to get root node\r\r\n");
        return;
    }

    csRef<iDocumentNode> confignode = root->GetNode("config");
    if (!confignode)
    {
        printOutput("Couldn't find config node in configfile!\r\r\n");
        return;
    }

    // Load updater config
    if (!config->GetCurrentConfig()->Initialize(confignode))
    {
        printOutput("Failed to Initialize mirror config current!\r\r\n");
        return;
    }

    // Initialise downloader.
    downloader = new Downloader(GetVFS(), config);

    //Set proxy
    downloader->SetProxy(GetConfig()->GetProxy().host.GetData(),
        GetConfig()->GetProxy().port);

    printOutput("Checking for updates to the updater: ");


    if(config->UpdateExecs() && checkUpdater())
    {
        printOutput("Update Available!\r\r\n");

        // If using a GUI, prompt user whether or not to update.
        if(!appName.Compare("psupdater"))
        {
            *updateNeeded = true;            
            while(*performUpdate == false || *exitGUI == false)
            {
                csSleep(500);
                // Make sure we die if we exit the gui as well.
                if(*updateNeeded == false || *exitGUI == true )
                {
                    delete downloader;
                    downloader = NULL;
                    return;
                }

                // If we're going to self-update, close the GUI.
                if(*performUpdate)
                    *exitGUI = true;
            }
        }

        // Begin the self update process.
        selfUpdate(false);
        // Restore config files before terminate.
        fileUtil->RemoveFile("/this/updaterinfo.xml");
        fileUtil->MoveFile("/this/updaterinfo.xml.bak", "/this/updaterinfo.xml", true, false);

        return;
    }

    printOutput("No updates needed!\r\r\nChecking for updates to all files: ");

    // Check for normal updates.
    if(checkGeneral())
    {
        printOutput("Updates Available!\r\r\n");
        // Mark update as incomplete.
        config->GetConfigFile()->SetBool("Update.Clean", false);
        config->GetConfigFile()->Save();

        // If using a GUI, prompt user whether or not to update.
        if(!appName.Compare("psupdater"))
        {
            *updateNeeded = true;
            while(!*performUpdate)
            {
                csSleep(500);
                if(!*updateNeeded)
                {
                    delete downloader;
                    downloader = NULL;
                    return;
                }
            }
        }

        // Begin general update.
        generalUpdate();

        // Mark update as complete and clean up.
        config->GetConfigFile()->SetBool("Update.Clean", true);
        config->GetConfigFile()->Save();
        printOutput("Update finished!\r\r\n");
    }
    else
        printOutput("No updates needed!\r\r\n");


    delete downloader;
    downloader = NULL;
    *updateNeeded = false;

    return;
}

bool UpdaterEngine::checkUpdater()
{
    // Backup old config, download new.
    fileUtil->MoveFile("/this/updaterinfo.xml", "/this/updaterinfo.xml.bak", true, false);
    downloader->DownloadFile("updaterinfo.xml", "updaterinfo.xml", false, true);

    // Load new config data.
    csRef<iDocumentNode> root = GetRootNode(UPDATERINFO_FILENAME);
    if(!root)
    {
        printOutput("Unable to get root node\r\r\n");
        return false;
    }

    csRef<iDocumentNode> confignode = root->GetNode("config");
    if (!confignode)
    {
        printOutput("Couldn't find config node in configfile!\r\r\n");
        return false;
    }

    if (!config->GetNewConfig()->Initialize(confignode))
    {
        printOutput("Failed to Initialize mirror config new!\r\r\n");
        return false;
    }

    // Compare Versions.
    return(config->GetNewConfig()->GetUpdaterVersionLatest() > UPDATER_VERSION);        
}

bool UpdaterEngine::checkGeneral()
{
    /*
    * Compare length of both old and new client version lists.
    * If they're the same, then compare the last lines to be extra sure.
    * If they're not, then we know there's some updates.
    */

    // Start by fetching the configs.
    const csRefArray<ClientVersion>& oldCvs = config->GetCurrentConfig()->GetClientVersions();
    const csRefArray<ClientVersion>& newCvs = config->GetNewConfig()->GetClientVersions();

    // Same size.
    if(oldCvs.GetSize() == newCvs.GetSize())
    {
        // If both are empty then skip the extra name check!
        if(newCvs.GetSize() != 0)
        {
            ClientVersion* oldCv = oldCvs.Get(oldCvs.GetSize()-1);
            ClientVersion* newCv = newCvs.Get(newCvs.GetSize()-1);

            csString name(newCv->GetName());
            if(!name.Compare(oldCv->GetName()))
            {
                // There's a problem and we can't continue. Throw a boo boo and clean up.
                printOutput("Local config and server config are incompatible!\r\r\n");
                fileUtil->RemoveFile("/this/updaterinfo.xml");
                fileUtil->MoveFile("/this/updaterinfo.xml.bak", "/this/updaterinfo.xml", true, false);
                return false;
            }
        }
        // Remove the backup of the xml (they're the same).
        fileUtil->RemoveFile("/this/updaterinfo.xml.bak");
        return false;
    }

    // Not the same size, so there's updates.
    return true;
}

csRef<iDocumentNode> UpdaterEngine::GetRootNode(const char* nodeName)
{
    // Load xml.
    csRef<iDocumentSystem> xml = csPtr<iDocumentSystem> (new csTinyDocumentSystem);
    if (!xml)
    {
        printOutput("Could not load the XML Document System\r\r\n");
        return NULL;
    }

    //Try to read file
    csRef<iDataBuffer> buf = vfs->ReadFile(nodeName);
    if (!buf || !buf->GetSize())
    {
        printOutput("Couldn't open xml file '%s'!\r\n", nodeName);
        return NULL;
    }

    //Try to parse file
    configdoc = xml->CreateDocument();
    const char* error = configdoc->Parse(buf);
    if (error)
    {
        printOutput("XML Parsing error in file '%s': %s.\r\n", nodeName, error);
        return NULL;
    }

    //Try to get root
    csRef<iDocumentNode> root = configdoc->GetRoot ();
    if (!root)
    {
        printOutput("Couldn't get config file rootnode!");
        return NULL;
    }

    return root;
}

#ifdef CS_PLATFORM_WIN32

bool UpdaterEngine::selfUpdate(int selfUpdating)
{
    // Info for CreateProcess.
    STARTUPINFO siStartupInfo;
    PROCESS_INFORMATION piProcessInfo;
    memset(&siStartupInfo, 0, sizeof(siStartupInfo));
    memset(&piProcessInfo, 0, sizeof(piProcessInfo));
    siStartupInfo.cb = sizeof(siStartupInfo);

    // Check what stage of the update we're in.
    switch(selfUpdating)
    {
    case 1: // We've downloaded the new file and executed it.
        {
            printOutput("Copying new files!\r\n");

            // Construct executable names.
            csString tempName = appName;
            appName.AppendFmt(".exe");
            tempName.AppendFmt("2.exe");

            // Delete the old updater file and copy the new in place.
            fileUtil->RemoveFile("/this/" + appName);
            fileUtil->CopyFile("/this/" + tempName, "/this/" + appName, true, true);

            // Copy any art and data.
            if(appName.Compare("pslaunch"))
            {
              csString zip = appName;
              zip.AppendFmt(config->GetCurrentConfig()->GetPlatform());
              zip.AppendFmt(".zip");
              vfs->Mount("/zip", zip);

              csString artPath = "/art/";
              artPath.AppendFmt("%s.zip", appName.GetData());
              fileUtil->CopyFile("/zip" + artPath, artPath, true, false, true);

              csString dataPath = "/data/gui/";
              dataPath.AppendFmt("%s.xml", appName.GetData());
              fileUtil->CopyFile("/zip" + dataPath, dataPath, true, false, true);

              vfs->Unmount("/zip", zip);
            }

            // Create a new process of the updater.
            CreateProcess(appName.GetData(), "selfUpdateSecond", 0, 0, false, CREATE_DEFAULT_ERROR_MODE, 0, 0, &siStartupInfo, &piProcessInfo);
            return true;
        }
    case 2: // We're now running the new updater in the correct location.
        {
            // Clean up left over files.
            printOutput("\r\nCleaning up!\r\n");

            // Construct zip name.
            csString zip = appName;
            zip.AppendFmt(config->GetCurrentConfig()->GetPlatform());
            zip.AppendFmt(".zip");

            // Remove updater zip.
            fileUtil->RemoveFile("/this/" + zip); 

            // Remove temp updater file.
            fileUtil->RemoveFile("/this/" + appName + "2.exe");            

            return false;
        }
    default: // We need to extract the new updater and execute it.
        {
            printOutput("Beginning self update!\r\n");

            // Construct zip name.
            csString zip = appName;
            zip.AppendFmt(config->GetCurrentConfig()->GetPlatform());
            zip.AppendFmt(".zip");

            // Download new updater file.
            downloader->DownloadFile(zip, zip, false, true);         

            // Check md5sum is correct.
            csRef<iDataBuffer> buffer = vfs->ReadFile("/this/" + zip, true);
            if (!buffer)
            {
                printOutput("Could not get MD5 of updater zip!!\r\n");
                return false;
            }

            csMD5::Digest md5 = csMD5::Encode(buffer->GetData(), buffer->GetSize());
            csString md5sum = md5.HexString();

            if(!md5sum.Compare(config->GetNewConfig()->GetUpdaterVersionLatestMD5()))
            {
                printOutput("md5sum of updater zip does not match correct md5sum!!\r\n");
                return false;
            }

            // md5sum is correct, mount zip and copy file.
            vfs->Mount("/zip", zip);
            csString from = "/zip/";
            from.AppendFmt(appName);
            from.AppendFmt(".exe");
            appName.AppendFmt("2.exe");

            fileUtil->CopyFile(from, "/this/" + appName, true, true);
            vfs->Unmount("/zip", zip);

            // Create a new process of the updater.
            CreateProcess(appName.GetData(), "selfUpdateFirst", 0, 0, false, CREATE_DEFAULT_ERROR_MODE, 0, 0, &siStartupInfo, &piProcessInfo);
            return true;
        }
    }
}

#else

bool UpdaterEngine::selfUpdate(int selfUpdating)
{
    // Check what stage of the update we're in.
    switch(selfUpdating)
    {
    case 2: // We're now running the new updater in the correct location.
        {
            // Clean up left over files.
            printOutput("\r\nCleaning up!\r\n");

            // Construct zip name.
            csString zip = appName;
            zip.AppendFmt(config->GetCurrentConfig()->GetPlatform());
            zip.AppendFmt(".zip");

            // Remove updater zip.
            fileUtil->RemoveFile("/this/" + zip); 

            return false;
        }
    default: // We need to extract the new updater and execute it.
        {
            printOutput("Beginning self update!\r\n");

            // Construct zip name.
            csString zip = appName;
            zip.AppendFmt(config->GetCurrentConfig()->GetPlatform());
            zip.AppendFmt(".zip");

            // Download new updater file.
            downloader->DownloadFile(zip, zip, false, true);         

            // Check md5sum is correct.
            csRef<iDataBuffer> buffer = vfs->ReadFile("/this/" + zip, true);
            if (!buffer)
            {
                printOutput("Could not get MD5 of updater zip!!\r\n");
                return false;
            }

            csMD5::Digest md5 = csMD5::Encode(buffer->GetData(), buffer->GetSize());

            csString md5sum = md5.HexString();

            if(!md5sum.Compare(config->GetNewConfig()->GetUpdaterVersionLatestMD5()))
            {
                printOutput("md5sum of updater zip does not match correct md5sum!!\r\n");
                return false;
            }

            csRef<iDataBuffer> realZipPath = vfs->GetRealPath("/this/" + zip);

            // Mount zip
            vfs->Mount("/zip", realZipPath->GetData());

            csString realName = appName;
#if defined(CS_PLATFORM_MACOSX)
            realName.Append(".app");
#else
            realName.Append(".bin");
#endif
            // Remove old updater.
            csString cmd;
            cmd.AppendFmt("rm -Rf %s", realName.GetData());
            system(cmd.GetData());

            // Copy new one into place.
            cmd.Clear();
            cmd.AppendFmt("cp -Rf zip/* .");
            system(cmd.GetData());

            // Unmount zip.
            vfs->Unmount("/zip", realZipPath->GetData());

            // Create a new process of the updater and exit.
#if defined(CS_PLATFORM_MACOSX)
            cmd.Clear();
            cmd.AppendFmt("open -a ./%s selfUpdateSecond", realName.GetData());
            system(cmd);
#else
            if(fork() == 0)
                execl(appName, appName, "selfUpdateSecond", NULL);
#endif
            return true;
        }
    }
}

#endif


void UpdaterEngine::generalUpdate()
{
    /*
    * This function updates our non-updater files to the latest versions,
    * writes new files and deletes old files.
    * This may take several iterations of patching.
    * After each iteration we need to update updaterinfo.xml.bak as well as the array.
    */

    // Start by fetching the configs.
    csRefArray<ClientVersion>& oldCvs = config->GetCurrentConfig()->GetClientVersions();
    const csRefArray<ClientVersion>& newCvs = config->GetNewConfig()->GetClientVersions();
    csRef<iDocumentNode> rootnode = GetRootNode(UPDATERINFO_OLD_FILENAME);
    csRef<iDocumentNode> confignode = rootnode->GetNode("config");

    if (!confignode)
    {
        printOutput("Couldn't find config node in configfile!\r\n");
        return;
    }

    // Main loop.
    while(checkGeneral())
    {
        // Find starting point in newCvs from oldCvs.
        size_t index = oldCvs.GetSize();

        // Use index to find the first update version in newCvs.
        ClientVersion* newCv = newCvs.Get(index);

        // Construct zip name.
        csString zip = config->GetCurrentConfig()->GetPlatform();
        zip.AppendFmt("-%s.zip", newCv->GetName());

        // Download update zip.
        printOutput("Downloading update file..\r\n");
        downloader->DownloadFile(zip, zip, false, true);

        // Check md5sum is correct.
        csRef<iDataBuffer> buffer = vfs->ReadFile("/this/" + zip, true);
        if (!buffer)
        {
            printOutput("Could not get MD5 of updater zip!!\r\n");
            return;
        }

        csMD5::Digest md5 = csMD5::Encode(buffer->GetData(), buffer->GetSize());

        csString md5sum = md5.HexString();

        if(!md5sum.Compare(newCv->GetMD5Sum()))
        {
            printOutput("md5sum of client zip does not match correct md5sum!!\r\n");
            return;
        }

        csRef<iDataBuffer> realZipPath = vfs->GetRealPath("/this/" + zip);

        // Mount zip
        vfs->Mount("/zip", realZipPath->GetData());

        // Parse deleted files xml, make a list.
        csArray<csString> deletedList;
        csRef<iDocumentNode> deletedrootnode = GetRootNode("/zip/deletedfiles.xml");
        if(deletedrootnode)
        {
            csRef<iDocumentNode> deletednode = deletedrootnode->GetNode("deletedfiles");
            csRef<iDocumentNodeIterator> nodeItr = deletednode->GetNodes();
            while(nodeItr->HasNext())
            {
                deletedList.PushSmart(nodeItr->Next()->GetAttributeValue("name"));
            }
        }

        // Remove all those files from our real dir.
        for(uint i=0; i<deletedList.GetSize(); i++)
        {
            fileUtil->RemoveFile("/this/" + deletedList.Get(i));
        }

        // Parse new files xml, make a list.
        csArray<csString> newList;
        csArray<bool> newListType;
        csRef<iDocumentNode> newrootnode = GetRootNode("/zip/newfiles.xml");
        if(newrootnode)
        {
            csRef<iDocumentNode> newnode = newrootnode->GetNode("newfiles");
            csRef<iDocumentNodeIterator> nodeItr = newnode->GetNodes();
            while(nodeItr->HasNext())
            {
                csRef<iDocumentNode> node = nodeItr->Next();
                newList.PushSmart(node->GetAttributeValue("name"));
                newListType.Push(node->GetAttributeValueAsBool("exec"));
            }
        }

        // Copy all those files to our real dir.
        for(uint i=0; i<newList.GetSize(); i++)
        {
            // Skip if it's an executable and we're not updating those.
            if(newListType.Get(i) && !config->UpdateExecs())
                continue;

            fileUtil->CopyFile("/zip/" + newList.Get(i), "/this/" + newList.Get(i), true, false);
        }

        // Parse changed files xml, binary patch each file.
        csRef<iDocumentNode> changedrootnode = GetRootNode("/zip/changedfiles.xml");
        if(changedrootnode)
        {
            csRef<iDocumentNode> changednode = changedrootnode->GetNode("changedfiles");
            csRef<iDocumentNodeIterator> nodeItr = changednode->GetNodes();
            while(nodeItr->HasNext())
            {
                csRef<iDocumentNode> next = nodeItr->Next();

                csString newFilePath = next->GetAttributeValue("filepath");
                if(!config->UpdateExecs() && fileUtil->isExecutable(newFilePath))
                    continue;

                csString diff = next->GetAttributeValue("diff");
                csString oldFilePath = newFilePath;
                oldFilePath.AppendFmt(".old");

                // Move old file to a temp location ready for input.
                fileUtil->MoveFile("/this/" + newFilePath, "/this/" + oldFilePath, true, false, true);

                // Move diff to a real location ready for input.
                fileUtil->CopyFile("/zip/" + diff, "/this/" + newFilePath + ".vcdiff", true, false, true);
                diff = newFilePath + ".vcdiff";

                // Binary patch.
                printOutput("Patching file %s: ", newFilePath.GetData());
                if(!PatchFile(oldFilePath, diff, newFilePath))
                {
                    printOutput("Failed!\r\n");
                    printOutput("Attempting to download full version of %s: ", newFilePath.GetData());

                    // Get the 'backup' mirror, should always be the first in the list.
                    csString baseurl = config->GetNewConfig()->GetMirror(0)->GetBaseURL();
                    baseurl.Append("backup/");

                    // Try path from base URL.
                    csString url = baseurl;
                    if(!downloader->DownloadFile(url.Append(newFilePath.GetData()), newFilePath.GetData(), true, true))
                    {
                        // Maybe it's in a platform specific subdirectory. Try that next.
                        url = baseurl;
                        url.AppendFmt("%s/", config->GetNewConfig()->GetPlatform());
                        if(!downloader->DownloadFile(url.Append(newFilePath.GetData()), newFilePath.GetData(), true, true))
                        {
                            printOutput("\r\nUnable to update file: %s. Reverting file!\r\n", newFilePath.GetData());
                            fileUtil->CopyFile("/this/" + oldFilePath, "/this/" + newFilePath, true, false);
                        }
                        else
                            printOutput("Done!\r\n");
                    }
                    else
                        printOutput("Done!\r\n");
                }
                else
                {
                    printOutput("Done!\r\n");

                    // Check md5sum is correct.
                    printOutput("Checking for correct md5sum: ");
                    csRef<iDataBuffer> buffer = vfs->ReadFile("/this/" + newFilePath, true);
                    if(!buffer)
                    {
                        printOutput("Could not get MD5 of patched file %s! Reverting file!\r\n", newFilePath.GetData());
                        fileUtil->RemoveFile("/this/" + newFilePath);
                        fileUtil->CopyFile("/this/" + oldFilePath, "/this/" + newFilePath, true, false);
                    }
                    else
                    {
                        csMD5::Digest md5 = csMD5::Encode(buffer->GetData(), buffer->GetSize());
                        csString md5sum = md5.HexString();

                        csString fileMD5 = next->GetAttributeValue("md5sum");

                        if(!md5sum.Compare(fileMD5))
                        {
                            printOutput("md5sum of file %s does not match correct md5sum! Reverting file!\r\n", newFilePath.GetData());
                            fileUtil->RemoveFile("/this/" + newFilePath);
                            fileUtil->CopyFile("/this/" + oldFilePath, "/this/" + newFilePath, true, false);
                        }
                        else
                            printOutput("Success!\r\n");
                    }
                    // Clean up temp files.
                    fileUtil->RemoveFile("/this/" + oldFilePath);
                }
                fileUtil->RemoveFile("/this/" + diff, false);
            }
        }

        // Unmount zip and delete.
        if(vfs->Unmount("/zip", realZipPath->GetData()))
        {
            vfs->Sync();
            fileUtil->RemoveFile("/this/" + zip);
        }
        else
        {
            printf("Failed to unmount file %s\r\n", zip.GetData());
        }

        // Add version info to updaterinfo.xml.bak and oldCvs.
        csString value("<version name=\"");
        value.AppendFmt("%s\" />", newCv->GetName());
        confignode->GetNode("client")->CreateNodeBefore(CS_NODE_TEXT)->SetValue(value);
        oldCvs.PushSmart(newCv);
     }
}
