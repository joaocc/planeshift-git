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

#include <psconfig.h>

#include <csutil/xmltiny.h>
#include <iutil/stringarray.h>

#include "updaterconfig.h"
#include "updaterengine.h"
#include "md5.h"
#include "binarypatch.h"

#ifndef CS_COMPILER_MSVC
#include <unistd.h>
#endif

iObjectRegistry* psUpdaterEngine::object_reg = NULL;

psUpdaterEngine::psUpdaterEngine(const csArray<csString> args, iObjectRegistry* _object_reg, const char* _appName)
{
    object_reg = _object_reg;
    vfs = csQueryRegistry<iVFS> (object_reg);
    if(!vfs)
    {
        printf("No VFS!\n");
        PS_PAUSEEXIT(1);
    }
    config = new psUpdaterConfig(args, object_reg, vfs);
    fileUtil = new FileUtil(vfs);
    appName = _appName;
}

psUpdaterEngine::~psUpdaterEngine()
{
    delete fileUtil;
    fileUtil = NULL;
}

void psUpdaterEngine::checkForUpdates()
{
    // Make sure the old instance had time to terminate (self-update).
    if(config->IsSelfUpdating() != 0)
        csSleep(500);

    // Check if the last attempt at a general updated ended in failure.
    if(!config->WasCleanUpdate())
    {
        // Restore config file which gives us the last updated position.
        fileUtil->RemoveFile("updaterinfo.xml");
        fileUtil->CopyFile("updaterinfo.xml.bak", "updaterinfo.xml", false, false);
        fileUtil->RemoveFile("updaterinfo.xml.bak");
    }


    // Load current config data.
    csRef<iDocumentNode> root = GetRootNode(UPDATERINFO_FILENAME);
    if(!root)
    {
        printf("Unable to get root node");
        PS_PAUSEEXIT(1);
    }

    csRef<iDocumentNode> confignode = root->GetNode("config");
    if (!confignode)
    {
        printf("Couldn't find config node in configfile!\n");
        PS_PAUSEEXIT(1);
    }

    // Load updater config
    if (!config->GetCurrentConfig()->Initialize(confignode))
    {
        printf("Failed to Initialize mirror config current!\n");
        return;
    }

    // Initialise downloader.
    downloader = new Downloader(GetVFS(), config);

    //Set proxy
    downloader->SetProxy(GetConfig()->GetProxy().host.GetData(),
        GetConfig()->GetProxy().port);

    printf("Checking for updates to the updater!\n");

    // Check for updater updates.
    if(checkUpdater())
    {
        // Begin the self update process.
        selfUpdate(false);
        // Restore config files before terminate.
        fileUtil->RemoveFile("updaterinfo.xml");
        fileUtil->CopyFile("updaterinfo.xml.bak", "updaterinfo.xml", false, false);
        fileUtil->RemoveFile("updaterinfo.xml.bak");
        return;
    }

    printf("Checking for updates to all files!\n");

    // Check for normal updates.
    if(checkGeneral())
    {
        // Mark update as incomplete.
        config->GetConfigFile()->SetBool("Update.Clean", false);
        config->GetConfigFile()->Save();

        // Begin general update.
        generalUpdate();

        // Mark update as complete and clean up.
        config->GetConfigFile()->SetBool("Update.Clean", true);
        config->GetConfigFile()->Save();
    }

    delete downloader;
    downloader = NULL;
    return;
}

bool psUpdaterEngine::checkUpdater()
{
    // Backup old config, download new.
    fileUtil->CopyFile("updaterinfo.xml", "updaterinfo.xml.bak", false, false);
    fileUtil->RemoveFile("updaterinfo.xml");
    downloader->DownloadFile("updaterinfo.xml", "updaterinfo.xml");

    // Load new config data.
    csRef<iDocumentNode> root = GetRootNode(UPDATERINFO_FILENAME);
    if(!root)
    {
        printf("Unable to get root node");
        PS_PAUSEEXIT(1);
    }

    csRef<iDocumentNode> confignode = root->GetNode("config");
    if (!confignode)
    {
        printf("Couldn't find config node in configfile!\n");
        PS_PAUSEEXIT(1);
    }

    if (!config->GetNewConfig()->Initialize(confignode))
    {
        printf("Failed to Initialize mirror config new!\n");
        PS_PAUSEEXIT(1);
    }

    // Compare Versions.
    return(config->GetNewConfig()->GetUpdaterVersionLatest() > UPDATER_VERSION);
}

bool psUpdaterEngine::checkGeneral()
{
    /*
    * Compare length of both old and new client version lists.
    * If they're the same, then compare the last lines to be extra sure.
    * If they're not, then we know there's some updates.
    */

    // Start by fetching the configs.
    csPDelArray<ClientVersion>* oldCvs = config->GetCurrentConfig()->GetClientVersions();
    csPDelArray<ClientVersion>* newCvs = config->GetNewConfig()->GetClientVersions();

    // Same size.
    if(oldCvs->GetSize() == newCvs->GetSize())
    {
        // If both are empty then skip the extra name check!
        if(newCvs->GetSize() != 0)
        {
            ClientVersion* oldCv = oldCvs->Get(oldCvs->GetSize()-1);
            ClientVersion* newCv = newCvs->Get(newCvs->GetSize()-1);

            if(!newCv->GetName().Compare(oldCv->GetName()))
            {
                // There's a problem and we can't continue. Throw a boo boo and clean up.
                printf("Local config and server config are incompatible!\n");
                fileUtil->RemoveFile("updaterinfo.xml");
                fileUtil->CopyFile("updaterinfo.xml.bak", "updaterinfo.xml", false, false);
                fileUtil->RemoveFile("updaterinfo.xml.bak");
                PS_PAUSEEXIT(1);
            }
        }
        // Remove the backup of the xml (they're the same).
        fileUtil->RemoveFile("updaterinfo.xml.bak");
        return false;
    }

    // Not the same size, so there's updates.
    return true;
}

csRef<iDocumentNode> psUpdaterEngine::GetRootNode(csString nodeName)
{
    // Load xml.
    csRef<iDocumentSystem> xml = csPtr<iDocumentSystem> (new csTinyDocumentSystem);
    if (!xml)
    {
        printf("Could not load the XML Document System\n");
        return NULL;
    }

    //Try to read file
    csRef<iDataBuffer> buf = vfs->ReadFile (nodeName.GetData());
    if (!buf || !buf->GetSize())
    {
        printf("Couldn't open config file '%s'!\n", nodeName.GetData());
        return NULL;
    }

    //Try to parse file
    configdoc = xml->CreateDocument();
    const char* error = configdoc->Parse(buf);
    if (error)
    {
        printf("XML Parsing error in file '%s': %s.\n", nodeName.GetData(), error);
        return NULL;
    }

    //Try to get root
    csRef<iDocumentNode> root = configdoc->GetRoot ();
    if (!root)
    {
        printf("Couldn't get config file rootnode!");
        return NULL;
    }

    return root;
}

#ifdef CS_PLATFORM_WIN32

bool psUpdaterEngine::selfUpdate(int selfUpdating)
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
            printf("Copying new files!\n");

            // Construct executable names.
            csString tempName = appName;
            appName.AppendFmt(".exe");
            tempName.AppendFmt("2.exe");

            // Delete the old updater file and copy the new in place.
            fileUtil->RemoveFile(appName.GetData());
            fileUtil->CopyFile(tempName.GetData(), appName.GetData(), false, true);

            // Create a new process of the updater.
            CreateProcess(appName.GetData(), "selfUpdateSecond", 0, 0, false, CREATE_DEFAULT_ERROR_MODE, 0, 0, &siStartupInfo, &piProcessInfo);
            return true;
        }
    case 2: // We're now running the new updater in the correct location.
        {
            // Clean up left over files.
            printf("\nCleaning up!\n");

            // Construct zip name.
            csString zip = appName;
            zip.AppendFmt(config->GetCurrentConfig()->GetPlatform());
            zip.AppendFmt(".zip");

            // Remove updater zip.
            fileUtil->RemoveFile(zip); 

            // Remove temp updater file.
            fileUtil->RemoveFile(appName + "2.exe");            

            return false;
        }
    default: // We need to extract the new updater and execute it.
        {
            printf("Beginning self update!\n");

            // Construct zip name.
            csString zip = appName;
            zip.AppendFmt(config->GetCurrentConfig()->GetPlatform());
            zip.AppendFmt(".zip");

            // Download new updater file.
            downloader->DownloadFile(zip, zip);         

            // Check md5sum is correct.
            MD5Sum md5(vfs);
            bool md5status = md5.ReadFile("/this/" + zip);
            if(!md5status)
            {
                printf("Could not get MD5 of updater zip!!\n");
                PS_PAUSEEXIT(1);
            }

            csString md5sum = md5.Get();

            if(!md5sum.Compare(config->GetNewConfig()->GetUpdaterVersionLatestMD5()))
            {
                printf("md5sum of updater zip does not match correct md5sum!!\n");
                PS_PAUSEEXIT(1);
            }

            // md5sum is correct, mount zip and copy file.
            vfs->Mount("/zip", zip.GetData());
            csString from = "zip/";
            from.AppendFmt(appName);
            from.AppendFmt(".exe");
            appName.AppendFmt("2.exe");

            fileUtil->CopyFile(from, "/this/" + appName, true, true);
            vfs->Unmount("/zip", zip.GetData());

            // Create a new process of the updater.
            CreateProcess(appName.GetData(), "selfUpdateFirst", 0, 0, false, CREATE_DEFAULT_ERROR_MODE, 0, 0, &siStartupInfo, &piProcessInfo);
            return true;
        }
    }
}

#else

bool psUpdaterEngine::selfUpdate(int selfUpdating)
{
    // Check what stage of the update we're in.
    switch(selfUpdating)
    {
    case 1: // We've downloaded the new file and executed it.
        {
            printf("Copying new files!\n");

            // Construct executable names.
            csString appName2 = appName;
            appName2.AppendFmt("2");


            // Delete the old updater file and copy the new in place.
            fileUtil->RemoveFile(appName.GetData());
            fileUtil->CopyFile(appName2.GetData(), appName.GetData(), false, true);

            // Create a new process of the updater.
            if(fork() == 0)
                execl(appName, appName, "selfUpdateSecond", NULL);
            return true;
        }
    case 2: // We're now running the new updater in the correct location.
        {
            // Clean up left over files.
            printf("\nCleaning up!\n");

            // Construct zip name.
            csString zip = appName;
            zip.AppendFmt(config->GetCurrentConfig()->GetPlatform());
            zip.AppendFmt(".zip");

            // Remove updater zip.
            fileUtil->RemoveFile(zip); 

            // Remove temp updater file.
            fileUtil->RemoveFile(appName + "2");            

            return false;
        }
    default: // We need to extract the new updater and execute it.
        {
            printf("Beginning self update!\n");

            // Construct zip name.
            csString zip = appName;
            zip.AppendFmt(config->GetCurrentConfig()->GetPlatform());
            zip.AppendFmt(".zip");

            // Download new updater file.
            downloader->DownloadFile(zip, zip);         

            // Check md5sum is correct.
            MD5Sum md5(vfs);
            bool md5status = md5.ReadFile("/this/" + zip);
            if(!md5status)
            {
                printf("Could not get MD5 of updater zip!!\n");
                PS_PAUSEEXIT(1);
            }

            csString md5sum = md5.Get();

            if(!md5sum.Compare(config->GetNewConfig()->GetUpdaterVersionLatestMD5()))
            {
                printf("md5sum of updater zip does not match correct md5sum!!\n");
                PS_PAUSEEXIT(1);
            }

            // md5sum is correct, mount zip and copy file.
            vfs->Mount("/zip", zip.GetData());
            csString from = "zip/";
            from.AppendFmt(appName);
            appName.AppendFmt("2");

            fileUtil->CopyFile(from, "/this/" + appName, true, true);
            vfs->Unmount("/zip", zip.GetData());

            // Create a new process of the updater.
            if(fork() == 0)
                execl(appName, appName, "selfUpdateFirst", NULL);
            return true;
        }
    }
}

#endif


void psUpdaterEngine::generalUpdate()
{
    /*
    * This function updates our non-updater files to the latest versions,
    * writes new files and deletes old files.
    * This may take several iterations of patching.
    * After each iteration we need to update updaterinfo.xml.bak as well as the array.
    */

    // Start by fetching the configs.
    csPDelArray<ClientVersion>* oldCvs = config->GetCurrentConfig()->GetClientVersions();
    csPDelArray<ClientVersion>* newCvs = config->GetNewConfig()->GetClientVersions();
    csRef<iDocumentNode> rootnode = GetRootNode(UPDATERINFO_OLD_FILENAME);
    csRef<iDocumentNode> confignode = rootnode->GetNode("config");

    if (!confignode)
    {
        printf("Couldn't find config node in configfile!\n");
        PS_PAUSEEXIT(1);
    }

    // Main loop.
    while(checkGeneral())
    {
        // Find starting point in newCvs from oldCvs.
        size_t index = oldCvs->GetSize();

        // Use index to find the first update version in newCvs.
        ClientVersion* newCv = newCvs->Get(index);

        // Construct zip name.
        csString zip = config->GetCurrentConfig()->GetPlatform();
        zip.AppendFmt(newCv->GetName());
        zip.AppendFmt(".zip");

        // Download update zip.
        downloader->DownloadFile(zip, zip);

        // Check md5sum is correct.
        MD5Sum md5(vfs);
        bool md5status = md5.ReadFile("/this/" + zip);
        if(!md5status)
        {
            printf("Could not get MD5 of client zip!!\n");
            PS_PAUSEEXIT(1);
        }

        csString md5sum = md5.Get();

        if(!md5sum.Compare(newCv->GetMD5Sum()))
        {
            printf("md5sum of client zip does not match correct md5sum!!\n");
            PS_PAUSEEXIT(1);
        }

        // Mount zip
        vfs->Mount("/zip", zip.GetData());

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
        csRef<iDocumentNode> newrootnode = GetRootNode("/zip/newfiles.xml");
        if(newrootnode)
        {
            csRef<iDocumentNode> newnode = newrootnode->GetNode("newfiles");
            csRef<iDocumentNodeIterator> nodeItr = newnode->GetNodes();
            while(nodeItr->HasNext())
            {
                newList.PushSmart(nodeItr->Next()->GetAttributeValue("name"));
            }
        }

        // Copy all those files to our real dir.
        for(uint i=0; i<newList.GetSize(); i++)
        {
            fileUtil->CopyFile("/zip/" + newList.Get(i), "/this/" + newList.Get(i), true, false);
        }


        // Remove new files from virtual dir.
        for(uint i=0; i<newList.GetSize(); i++)
        {
            fileUtil->RemoveFile("/zip/" + newList.Get(i));
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
                csString diff = next->GetAttributeValue("diff");
                csString oldFilePath = newFilePath;
                oldFilePath.AppendFmt("_temp");

                // Move old file to a temp location ready for input.
                fileUtil->CopyFile(newFilePath, oldFilePath, false, false);
                fileUtil->RemoveFile("/this/" + newFilePath);

                // Move diff to a real location ready for input.
                fileUtil->CopyFile("/zip/" + diff, "/this/" + newFilePath + ".vcdiff", true, false);
                fileUtil->RemoveFile("/zip/" + diff);
                diff = newFilePath + ".vcdiff";

                // Binary patch.
                if(!PatchFile(oldFilePath, diff, newFilePath))
                {
                    printf("Patching failed for file: %s. Reverting file!\n", newFilePath.GetData());
                    fileUtil->CopyFile(oldFilePath, newFilePath, false, false);
                }
                else
                {

                    // Check md5sum is correct.
                    printf("Checking for correct md5sum: ");
                    MD5Sum md5(vfs);
                    bool md5status = md5.ReadFile("/this/" + newFilePath);
                    if(!md5status)
                    {
                        printf("Could not get MD5 of patched file %s! Reverting file!\n", newFilePath.GetData());
                        fileUtil->RemoveFile("/this/" + newFilePath);
                        fileUtil->CopyFile(oldFilePath, newFilePath, false, false);
                    }
                    else
                    {

                        csString md5sum = md5.Get();
                        csString fileMD5 = next->GetAttributeValue("md5sum");

                        if(!md5sum.Compare(fileMD5))
                        {
                            printf("md5sum of file %s does not match correct md5sum! Reverting file!\n", newFilePath.GetData());
                            fileUtil->RemoveFile("/this/" + newFilePath);
                            fileUtil->CopyFile(oldFilePath, newFilePath, false, false);
                        }
                        else
                            printf("Success!\n");
                    }
                }

                // Clean up temp files.
                fileUtil->RemoveFile("/this/" + oldFilePath);
                fileUtil->RemoveFile("/this/" + diff);
            }
        }       

        // Unmount zip and delete.
        vfs->Unmount("/zip", zip.GetData());
        fileUtil->RemoveFile("/this/" + zip);

        // Add version info to updaterinfo.xml.bak and oldCvs.
        confignode->GetNode("client")->CreateNodeBefore(CS_NODE_TEXT)->SetValue("<version name=\"" + newCv->GetName() + "\" />");
        oldCvs->Push(newCv);
    }
}
