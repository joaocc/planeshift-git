/*
* chatwindow.cpp - Author: Andrew Craig
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

// chatwindow.cpp: implementation of the pawsChatWindow class.
//
//////////////////////////////////////////////////////////////////////
#include <psconfig.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>

//=============================================================================
// Crystal Space Includes
//=============================================================================
#include <csutil/util.h>
#include <csutil/xmltiny.h>

#include <iutil/evdefs.h>
#include <iutil/databuff.h>

//=============================================================================
// Project Includes
//=============================================================================
#include "iclient/isoundmngr.h"

#include "paws/pawstextbox.h"
#include "paws/pawsprefmanager.h"
#include "paws/pawsmanager.h"
#include "paws/pawsbutton.h"
#include "paws/pawsborder.h"
#include "paws/pawstabwindow.h"

#include "gui/pawscontrolwindow.h"

#include "net/message.h"
#include "net/msghandler.h"
#include "net/cmdhandler.h"

#include "util/strutil.h"
#include "util/fileutil.h"
#include "util/log.h"
#include "util/localization.h"

//=============================================================================
// Local Includes
//=============================================================================
#include "pscelclient.h"
#include "chatwindow.h"

const char *logWidgetName[CHAT_NLOG] = {
    "logAllChat",
    "logSystemChat"
};

const char *logFileName[CHAT_NLOG] = {
    "chat.txt",
    "system.txt"
};


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

pawsChatWindow::pawsChatWindow() 
    : psCmdBase( NULL,NULL,  PawsManager::GetSingleton().GetObjectRegistry() )
{
    systemTriggers.Push("server admin");
    systemTriggers.Push("gm warning");

    havePlayerName = false;

    replyCount = 0;

    chatHistory = new pawsChatHistory();
    awayText.Clear();
    soundmgr = NULL;

    settings.meFilters = 0;
    settings.vicinityFilters = 0;
    settings.echoScreenInSystem = false;

    for (int i = 0; i < CHAT_NLOG; i++)
        logFile[i] = NULL;
}

pawsChatWindow::~pawsChatWindow()
{
    msgqueue->Unsubscribe(this, MSGTYPE_CHAT);
    msgqueue->Unsubscribe(this, MSGTYPE_SYSTEM);

    delete chatHistory;
    for (int i = 0; i < CHAT_NLOG; i++)
        logFile[i].Invalidate();
}

bool pawsChatWindow::PostSetup()
{

    if ( !psCmdBase::Setup( psengine->GetMsgHandler(), psengine->GetCmdHandler()) )
        return false;

    soundmgr = psengine->GetSoundManager();

    msgqueue->Subscribe(this, MSGTYPE_CHAT);
    msgqueue->Subscribe(this, MSGTYPE_SYSTEM);

    SubscribeCommands();

    tabs = (pawsTabWindow*)FindWidget("Chat Tabs");

    systemText  = (pawsMessageTextBox*)FindWidget("SystemText");
    npcText     = (pawsMessageTextBox*)FindWidget("NpcText");
    mainText    = (pawsMessageTextBox*)FindWidget("MainText");
    tellText    = (pawsMessageTextBox*)FindWidget("TellText");
    guildText   = (pawsMessageTextBox*)FindWidget("GuildText");
    groupText   = (pawsMessageTextBox*)FindWidget("GroupText");
    auctionText = (pawsMessageTextBox*)FindWidget("AuctionText");
    helpText    = (pawsMessageTextBox*)FindWidget("HelpText");
    inputText   = (pawsEditTextBox*)   FindWidget("InputText");

    // Load the settings
    LoadChatSettings();

    IgnoredList = (pawsIgnoreWindow*)PawsManager::GetSingleton().FindWidget("IgnoreWindow");
    if ( !IgnoredList )
    {
        Error1("ChatWindow failed because IgnoredList window was not found.");
        return false;
    }
    return true;
}

void pawsChatWindow::LoadChatSettings()
{
    // Get some default colours here and other options.

    // XML parsing time!
    csRef<iDocument> doc;
    csRef<iDocumentNode> root,chatNode, colorNode, optionNode, filtersNode, msgFiltersNode,
                         mainTabNode, flashingNode, flashingOnCharNode;
    csString option;

    csString fileName = CONFIG_CHAT_FILE_NAME;
    if (!psengine->GetVFS()->Exists(fileName))
    {
        fileName = CONFIG_CHAT_FILE_NAME_DEF;
    }

    doc = ParseFile(psengine->GetObjectRegistry(), fileName);
    if (doc == NULL)
    {
        Error2("Failed to parse file %s", fileName.GetData());
        return;
    }
    root = doc->GetRoot();
    if (root == NULL)
    {
        Error1("chat.xml has no XML root");
        return;
    }
    chatNode = root->GetNode("chat");
    if (chatNode == NULL)
    {
        Error1("chat.xml has no <chat> tag");
        return;
    }

    // Load options such as loose after sending
    optionNode = chatNode->GetNode("chatoptions");
    if (optionNode != NULL)
    {
        csRef<iDocumentNodeIterator> oNodes = optionNode->GetNodes();
        while(oNodes->HasNext())
        {
            csRef<iDocumentNode> option = oNodes->Next();
            csString nodeName (option->GetValue());

            if (nodeName == "loose")
                settings.looseFocusOnSend = option->GetAttributeValueAsInt("value") ? true : false;

            else if (nodeName == "selecttabstyle")
                settings.selectTabStyle = (int)option->GetAttributeValueAsInt("value");
            else if (nodeName == "echoscreeninsystem")
                settings.echoScreenInSystem = option->GetAttributeValueAsInt("value") ? true : false;
            else
            {
                for (int i = 0; i < CHAT_NLOG; i++)
                    if (nodeName == logWidgetName[i])   // Take the same name for simplicity
                        settings.logChannel[i] = option->GetAttributeValueAsInt("value") ? true : false;
            }
        }
    }

    // Load flashing options

    settings.mainFlashing = true;
    settings.npcFlashing = true;
    settings.tellFlashing = true;
    settings.guildFlashing = true;
    settings.groupFlashing = true;
    settings.auctionFlashing = true;
    settings.systemFlashing = true;
    settings.helpFlashing = true;

    flashingNode = chatNode->GetNode("flashingoptions");
    if (flashingNode != NULL)
    {
        csRef<iDocumentNodeIterator> nodes = flashingNode->GetNodes();
        while (nodes->HasNext())
        {
            csRef<iDocumentNode> node = nodes->Next();
            csString nodeName(node->GetValue());
            bool value = node->GetAttributeValueAsBool("value");

            if (nodeName == "main")
                settings.mainFlashing = value;
            else if (nodeName == "npc")
                settings.npcFlashing = value;
            else if (nodeName == "tell")
                settings.tellFlashing = value;
            else if (nodeName == "guild")
                settings.guildFlashing = value;
            else if (nodeName == "group")
                settings.groupFlashing = value;
            else if (nodeName == "auction")
                settings.auctionFlashing = value;
            else if (nodeName == "system")
                settings.systemFlashing = value;
            else if (nodeName == "help")
                settings.helpFlashing = value;

        }
    }

	// Load flashing on char name options
    settings.maincFlashing = true;
    settings.npccFlashing = true;
    settings.tellcFlashing = true;
    settings.guildcFlashing = true;
    settings.groupcFlashing = true;
    settings.auctioncFlashing = true;
    settings.systemcFlashing = true;
    settings.helpcFlashing = true;

    flashingNode = chatNode->GetNode("flashingoncharoptions");
    if (flashingNode != NULL)
    {
        csRef<iDocumentNodeIterator> nodes = flashingNode->GetNodes();
        while (nodes->HasNext())
        {
            csRef<iDocumentNode> node = nodes->Next();
            csString nodeName(node->GetValue());
            bool value = node->GetAttributeValueAsBool("value");

            if (nodeName == "main")
			{
                settings.maincFlashing = value;
			}
            else if (nodeName == "npc")
			{
                settings.npccFlashing = value;
			}
            else if (nodeName == "tell")
			{
                settings.tellcFlashing = value;
			}
            else if (nodeName == "guild")
			{
                settings.guildcFlashing = value;
			}
            else if (nodeName == "group")
			{
                settings.groupcFlashing = value;
			}
            else if (nodeName == "auction")
			{
                settings.auctioncFlashing = value;
			}
            else if (nodeName == "system")
			{
                settings.systemcFlashing = value;
			}
            else if (nodeName == "help")
			{
                settings.helpcFlashing = value;
			}

        }
    }



    // Load main tab options (which other channels are included on the main tab)

    settings.npcIncluded = false;
    settings.tellIncluded = false;
    settings.guildIncluded = false;
    settings.groupIncluded = false;
    settings.auctionIncluded = false;
    settings.systemIncluded = false;
    settings.systemBaseIncluded = true;
    settings.helpIncluded = false;

    mainTabNode = chatNode->GetNode("maintabincludes");
    if (mainTabNode != NULL)
    {
        csRef<iDocumentNodeIterator> nodes = mainTabNode->GetNodes();
        while (nodes->HasNext())
        {
            csRef<iDocumentNode> node = nodes->Next();
            csString nodeName(node->GetValue());
            bool value = node->GetAttributeValueAsBool("value");

            if (nodeName == "npc")
                settings.npcIncluded = value;
            else if (nodeName == "tell")
                settings.tellIncluded = value;
            else if (nodeName == "guild")
                settings.guildIncluded = value;
            else if (nodeName == "group")
                settings.groupIncluded = value;
            else if (nodeName == "auction")
                settings.auctionIncluded = value;
            else if (nodeName == "system")
                settings.systemIncluded = value;
            else if (nodeName == "systembase")
                settings.systemBaseIncluded = value;
            else if (nodeName == "help")
                settings.helpIncluded = value;
        }
    }

    // Load colors
    colorNode = chatNode->GetNode("chatcolors");
    if (colorNode != NULL)
    {
        csRef<iDocumentNodeIterator> cNodes = colorNode->GetNodes();
        while(cNodes->HasNext())
        {
            csRef<iDocumentNode> color = cNodes->Next();

            int r = color->GetAttributeValueAsInt( "r" );
            int g = color->GetAttributeValueAsInt( "g" );
            int b = color->GetAttributeValueAsInt( "b" );

            int col = graphics2D->FindRGB( r, g, b );

            csString nodeName ( color->GetValue() );
            if ( nodeName == "systemtext" ) settings.systemColor = col;
            if ( nodeName == "admintext"  ) settings.adminColor = col;
            if ( nodeName == "playernametext" ) settings.playerColor = col;
            if ( nodeName == "chattext") settings.chatColor = col;
            if ( nodeName == "telltext") settings.tellColor = col;
            if ( nodeName == "shouttext") settings.shoutColor = col;
            if ( nodeName == "gmtext") settings.gmColor = col;
            if ( nodeName == "guildtext") settings.guildColor = col;
            if ( nodeName == "yourtext") settings.yourColor = col;
            if ( nodeName == "grouptext") settings.groupColor = col;
            if ( nodeName == "auctiontext") settings.auctionColor = col;
            if ( nodeName == "helptext") settings.helpColor = col;
        }
    }

    // Load filters
    filtersNode = chatNode->GetNode("filters");
    settings.meFilters = 0;
    settings.vicinityFilters = 0;

    if (filtersNode != NULL)
    {
        csRef<iDocumentNode> badWordsNode = filtersNode->GetNode("badwords");
        if (badWordsNode == NULL) {
            settings.enableBadWordsFilterIncoming = settings.enableBadWordsFilterOutgoing = false;
        }
        else
        {
            settings.enableBadWordsFilterIncoming = badWordsNode->GetAttributeValueAsBool("incoming");
            settings.enableBadWordsFilterOutgoing = badWordsNode->GetAttributeValueAsBool("outgoing");
            csRef<iDocumentNodeIterator> oNodes = badWordsNode->GetNodes();
            while(oNodes->HasNext())
            {
                csRef<iDocumentNode> option = oNodes->Next();
                if (option->GetType() == CS_NODE_TEXT)
                {
                    csString s = option->GetValue();
                    for (char *word = strtok((char*)s.GetData(), " \r\n\t"); word; word = strtok(NULL, " \r\n\t"))
                    {
                        if ( settings.badWords.Find(word)==csArrayItemNotFound ) {
                            settings.badWords.Push(word);
                            settings.goodWords.Push("");
                        }
                    }
                }
                else if (option->GetType() == CS_NODE_ELEMENT && csString(option->GetValue()) == "replace")
                {
                    csString bad = option->GetAttributeValue("bad");
                    csString good = option->GetAttributeValue("good");
                    if ( settings.badWords.Find(bad)==csArrayItemNotFound ) {
                        settings.badWords.Push(bad);
                        settings.goodWords.Push(good);
                    }
                }
            }
        }
    }

    if ( settings.badWords.GetSize() != settings.goodWords.GetSize() )
    {
        Error1("Mismatch in the good/bad word chat filter. Different lengths");
        CS_ASSERT( false );
        return;
    }



    // Load message filters
    msgFiltersNode = chatNode->GetNode("msgfilters");
    if (msgFiltersNode != NULL)
    {
        csRef<iDocumentNodeIterator> fNodes = msgFiltersNode->GetNodes();
        while(fNodes->HasNext())
        {
            csRef<iDocumentNode> filter = fNodes->Next();

            // Extract the info
            csString range = filter->GetValue();
            csString type =  filter->GetAttributeValue("type");
            CHAT_COMBAT_FILTERS fType;

            bool value = filter->GetAttributeValueAsBool("value",true);

            if(type == "BLO")
                fType = COMBAT_BLOCKED;
            else if(type == "SUC")
                fType = COMBAT_SUCCEEDED;
            else if(type == "MIS")
                fType = COMBAT_MISSED;
            else if(type == "DOD")
                fType = COMBAT_DODGED;
            else if(type == "FAI")
                fType = COMBAT_FAILED;
            else // if(type == "STA")
                fType = COMBAT_STANCE;

            if(value)
            {
                if(range == "me")
                    settings.meFilters |= fType;
                else
                    settings.vicinityFilters |= fType;
            }
        }
    }
}

const char* pawsChatWindow::HandleCommand( const char* cmd )
{
    WordArray words(cmd);
    csString pPerson;
    csString text;
    int chattype = 0;

    if (words.GetCount()==0)
        return NULL;

    if (words[0].GetAt(0) != '/')
    {
        pPerson.Clear();
        words.GetTail(0,text);
        chattype = CHAT_SAY;
    }
    else
    {
        if (words.GetCount() == 1)
            return PawsManager::GetSingleton().Translate("You must enter the text").Detach();

        if (words[0] == "/say")
        {
            pPerson.Clear();            
            words.GetTail(1, text);
            chattype = CHAT_SAY;
            csArray<csString> allowedTabs;
            allowedTabs.Push("MainText");
            switch (settings.selectTabStyle)
            {
                case 1:
                case 2:
                    AutoselectChatTabIfNeeded(allowedTabs, "Main Button");
                    break;
                default:
                    break;
            }
        }
        else if (words[0] == "/tellnpc")
        {
            pPerson.Clear();
            words.GetTail(1, text);
            chattype = CHAT_NPC;
            csArray<csString> allowedTabs;
            allowedTabs.Push("NpcText");
            if (settings.npcIncluded)
                allowedTabs.Push("MainText");
            switch (settings.selectTabStyle)
            {
                case 1:
                    AutoselectChatTabIfNeeded(allowedTabs, "NPC Button");
                    break;
                case 2:
                    AutoselectChatTabIfNeeded(allowedTabs, settings.npcIncluded ?
                            "Main Button" : "NPC Button");
                    break;
                default:
                    break;
            }
        }
        else if (words[0] == "/report")
        {
            pPerson.Clear();
            words.GetTail(1,text);
            chattype = CHAT_REPORT;
            csArray<csString> allowedTabs;
            allowedTabs.Push("SystemText");
            if (settings.systemIncluded)
                allowedTabs.Push("MainText");
            switch (settings.selectTabStyle)
            {
                case 1:
                    AutoselectChatTabIfNeeded(allowedTabs, "System Button");
                    break;
                case 2:
                    AutoselectChatTabIfNeeded(allowedTabs, settings.systemIncluded ?
                            "Main Button" : "System Button");
                    break;
                default:
                    break;
            }
        }
        else if (words[0] == "/guild")
        {
            pPerson.Clear();
            words.GetTail(1,text);
            chattype = CHAT_GUILD;
            csArray<csString> allowedTabs;
            allowedTabs.Push("GuildText");
            if (settings.guildIncluded)
                allowedTabs.Push("MainText");
            switch (settings.selectTabStyle)
            {
                case 1:
                    AutoselectChatTabIfNeeded(allowedTabs, "Guild Button");
                    break;
                case 2:
                    AutoselectChatTabIfNeeded(allowedTabs, settings.guildIncluded ?
                            "Main Button" : "Guild Button");
                    break;
                default:
                    break;
            }
        }
        else if (words[0] == "/shout")
        {
            pPerson.Clear();
            words.GetTail(1,text);
            chattype = CHAT_SHOUT;
            csArray<csString> allowedTabs;
            allowedTabs.Push("MainText");
            switch (settings.selectTabStyle)
            {
                case 1:
                case 2:
                    AutoselectChatTabIfNeeded(allowedTabs, "Main Button");
                    break;
                default:
                    break;
            }
        }
        else if (words[0] == "/group")
        {
            pPerson.Clear();
            words.GetTail(1,text);
            chattype = CHAT_GROUP;
            csArray<csString> allowedTabs;
            allowedTabs.Push("GroupText");
            if (settings.groupIncluded)
                allowedTabs.Push("MainText");
            switch (settings.selectTabStyle)
            {
                case 1:
                    AutoselectChatTabIfNeeded(allowedTabs, "Group Button");
                    break;
                case 2:
                    AutoselectChatTabIfNeeded(allowedTabs, settings.groupIncluded ?
                            "Main Button" : "Group Button");
                    break;
                default:
                    break;
            }
        }
        else if (words[0] == "/tell")
        {
            pPerson = words[1];
            if (words.GetCount() == 2)
                return PawsManager::GetSingleton().Translate("You must enter the text").Detach();
            words.GetTail(2,text);
            chattype = CHAT_TELL;
            csArray<csString> allowedTabs;
            allowedTabs.Push("TellText");
            if (settings.tellIncluded)
                allowedTabs.Push("MainText");
            switch (settings.selectTabStyle)
            {
                case 1:
                    AutoselectChatTabIfNeeded(allowedTabs, "Tell Button");
                    break;
                case 2:
                    AutoselectChatTabIfNeeded(allowedTabs, settings.tellIncluded ?
                            "Main Button" : "Tell Button");
                    break;
                default:
                    break;
            }
        }
        else if (words[0] == "/auction")
        {
            pPerson.Clear();
            words.GetTail(1,text);
            chattype = CHAT_AUCTION;
            csArray<csString> allowedTabs;
            allowedTabs.Push("AuctionText");
            if (settings.auctionIncluded)
                allowedTabs.Push("MainText");
            switch (settings.selectTabStyle)
            {
                case 1:
                    AutoselectChatTabIfNeeded(allowedTabs, "Auction Button");
                    break;
                case 2:
                    AutoselectChatTabIfNeeded(allowedTabs, settings.auctionIncluded ?
                            "Main Button" : "Auction Button");
                    break;
                default:
                    break;
            }
        }
        else if (words[0] == "/mypet")
        {
            pPerson.Clear();
            chattype = CHAT_PET_ACTION;
            words.GetTail(1,text);
            csArray<csString> allowedTabs;
            allowedTabs.Push("MainText");
            switch (settings.selectTabStyle)
            {
                case 1:
                case 2:
                    AutoselectChatTabIfNeeded(allowedTabs, "Main Button");
                    break;
                default:
                    break;
            }
        }
        else if (words[0] == "/me" || words[0] == "/my")
        {
            pPerson.Clear();
            csString chatType = tabs->GetActiveTab()->GetName();

            csArray<csString> allowedTabs;
            csString defaultButton("Main Button");

            if (chatType == "TellText")
            {
                int i = 0;
                while (!replyList[i].IsEmpty() && i <= 4)
                    i++;
                if (i)
                {
                    if (replyCount >= i)
                        replyCount = 0;
                    pPerson=replyList[replyCount].GetData();
                    replyCount++;
                }
                words.GetTail(0,text);
                chattype = CHAT_TELL;
                csArray<csString> allowedTabs;
                allowedTabs.Push("TellText");
                defaultButton = "Tell Button";
                if (settings.tellIncluded)
                    allowedTabs.Push("MainText");
                switch (settings.selectTabStyle)
                {
                    case 1:
                        AutoselectChatTabIfNeeded(allowedTabs, "Tell Button");
                        break;
                    case 2:
                        AutoselectChatTabIfNeeded(allowedTabs, settings.tellIncluded ?
                                "Main Button" : "Tell Button");
                        break;
                    default:
                        break;
                }
            }
            else if (chatType == "GuildText")
            {
                chattype = CHAT_GUILD;
                words.GetTail(0,text);
                allowedTabs.Push("GuildText");
                defaultButton = "Guild Button";
                if (settings.guildIncluded)
                {
                    allowedTabs.Push("MainText");
                    if (settings.selectTabStyle == 2)
                        defaultButton = "Main Button";
                }
            }
            else if (chatType == "GroupText")
            {
                chattype = CHAT_GROUP;
                words.GetTail(0,text);
                allowedTabs.Push("GroupText");
                defaultButton = "Group Button";
                if (settings.groupIncluded)
                {
                    allowedTabs.Push("MainText");
                    if (settings.selectTabStyle == 2)
                        defaultButton = "Main Button";
                }
            }
            else if (words[0] == "/my")
            {
                chattype = CHAT_MY;
                words.GetTail(1,text);
                allowedTabs.Push("MainText");
            }
            else // when in doubt, use the normal way
            {
                chattype = CHAT_ME;
                words.GetTail(1,text);
                allowedTabs.Push("MainText");
            }
            //pPerson.Clear();
            switch (settings.selectTabStyle)
            {
                case 1:
                case 2:
                    AutoselectChatTabIfNeeded(allowedTabs, defaultButton);
                    break;
                default:
                    break;
            }
        }
    }
    
    if (settings.enableBadWordsFilterOutgoing)
        BadWordsFilter(text);

    psChatMessage chat(0, pPerson.GetDataSafe(), 0, text.GetDataSafe(), chattype, false);
    msgqueue->SendMessage(chat.msg);

    return NULL;
}

void pawsChatWindow::LogMessage(enum E_CHAT_LOG channel, const char* message)
{
    if (settings.logChannel[channel])
    {
        if (!logFile[channel])
        {
	    csString filename;
	    filename.Format("/planeshift/userdata/logs/%s_%s",
                            psengine->GetCelClient()->GetMainPlayer()->GetName(),
			    logFileName[channel]);
            filename.ReplaceAll(" ", "_");

	    logFile[channel] = psengine->GetVFS()->Open(filename, VFS_FILE_APPEND);
            if (logFile[channel])
            {
                time_t aclock;
                struct tm *newtime;
                char buf[32];

                time(&aclock);
                newtime = localtime(&aclock);
                strftime(buf, 32, "%a %d-%b-%Y %H:%M:%S", newtime);
		csString buffer;
		buffer.Format(
                    "================================================\n"
                    "%s %s\n"
                    "------------------------------------------------\n",
                    buf, psengine->GetCelClient()->GetMainPlayer()->GetName()
                    );
                logFile[channel]->Write(buffer.GetData(), buffer.Length());
            }
            else
            {
                // Trouble opening the log file
                Error2("Couldn't create chat log file '%s'.\n", logFileName[channel]);
            }
        }
        if (logFile[channel])
        {
            time_t aclock;
            struct tm *newtime;
            char buf[32];

            time(&aclock);
            newtime = localtime(&aclock);
            strftime(buf, 32, "(%H:%M:%S)", newtime);
	    csString buffer;
	    buffer.Format("%s %s\n", buf, message);
	    logFile[channel]->Write(buffer.GetData(), buffer.Length());
	    logFile[channel]->Flush();
        }
    }
}

void pawsChatWindow::CreateSettingNode(iDocumentNode* mNode,int color,const char* name)
{
    csRef<iDocumentNode> cNode = mNode->CreateNodeBefore(CS_NODE_ELEMENT,0);
    cNode->SetValue(name);
    int r,g,b,alpha;
    graphics2D->GetRGB(color,r,g,b,alpha);
    cNode->SetAttributeAsInt("r",r);
    cNode->SetAttributeAsInt("g",g);
    cNode->SetAttributeAsInt("b",b);
}

void pawsChatWindow::SaveChatSettings()
{
    // Save to file
    csRef<iFile> file;
    file = psengine->GetVFS()->Open(CONFIG_CHAT_FILE_NAME,VFS_FILE_WRITE);

    csRef<iDocumentSystem> docsys = csPtr<iDocumentSystem> (new csTinyDocumentSystem ());

    csRef<iDocument> doc = docsys->CreateDocument();
    csRef<iDocumentNode> root,chatNode, colorNode, optionNode,looseNode,filtersNode,
                         badWordsNode,badWordsTextNode,cNode, logNode, selectTabStyleNode,
                         echoScreenInSystemNode, mainTabNode, flashingNode, flashingOnCharNode, node;

    root = doc->CreateRoot();

    chatNode = root->CreateNodeBefore(CS_NODE_ELEMENT,0);
    chatNode->SetValue("chat");

    optionNode = chatNode->CreateNodeBefore(CS_NODE_ELEMENT,0);
    optionNode->SetValue("chatoptions");

    selectTabStyleNode = optionNode->CreateNodeBefore(CS_NODE_ELEMENT,0);
    selectTabStyleNode->SetValue("selecttabstyle");
    selectTabStyleNode->SetAttributeAsInt("value",(int)settings.selectTabStyle);

    echoScreenInSystemNode = optionNode->CreateNodeBefore(CS_NODE_ELEMENT,0);
    echoScreenInSystemNode->SetValue("echoscreeninsystem");
    echoScreenInSystemNode->SetAttributeAsInt("value",(int)settings.echoScreenInSystem);

    looseNode = optionNode->CreateNodeBefore(CS_NODE_ELEMENT,0);
    looseNode->SetValue("loose");
    looseNode->SetAttributeAsInt("value",(int)settings.looseFocusOnSend);

    for (int i = 0; i < CHAT_NLOG; i++)
    {
        logNode = optionNode->CreateNodeBefore(CS_NODE_ELEMENT,0);
        logNode->SetValue(logWidgetName[i]);
        logNode->SetAttributeAsInt("value",(int)settings.logChannel[i]);
    }

    mainTabNode = chatNode->CreateNodeBefore(CS_NODE_ELEMENT, 0);
    mainTabNode->SetValue("maintabincludes");

    node = mainTabNode->CreateNodeBefore(CS_NODE_ELEMENT, 0);
    node->SetValue("npc");
    node->SetAttribute("value", settings.npcIncluded ? "yes" : "no");

    node = mainTabNode->CreateNodeBefore(CS_NODE_ELEMENT, 0);
    node->SetValue("tell");
    node->SetAttribute("value", settings.tellIncluded ? "yes" : "no");

    node = mainTabNode->CreateNodeBefore(CS_NODE_ELEMENT, 0);
    node->SetValue("guild");
    node->SetAttribute("value", settings.guildIncluded ? "yes" : "no");

    node = mainTabNode->CreateNodeBefore(CS_NODE_ELEMENT, 0);
    node->SetValue("group");
    node->SetAttribute("value", settings.groupIncluded ? "yes" : "no");

    node = mainTabNode->CreateNodeBefore(CS_NODE_ELEMENT, 0);
    node->SetValue("auction");
    node->SetAttribute("value", settings.auctionIncluded ? "yes" : "no");

    node = mainTabNode->CreateNodeBefore(CS_NODE_ELEMENT, 0);
    node->SetValue("system");
    node->SetAttribute("value", settings.systemIncluded ? "yes" : "no");

    node = mainTabNode->CreateNodeBefore(CS_NODE_ELEMENT, 0);
    node->SetValue("systembase");
    node->SetAttribute("value", settings.systemBaseIncluded ? "yes" : "no");

    node = mainTabNode->CreateNodeBefore(CS_NODE_ELEMENT, 0);
    node->SetValue("help");
    node->SetAttribute("value", settings.helpIncluded ? "yes" : "no");

    flashingNode = chatNode->CreateNodeBefore(CS_NODE_ELEMENT, 0);
    flashingNode->SetValue("flashingoptions");

	flashingOnCharNode = chatNode->CreateNodeBefore(CS_NODE_ELEMENT, 0);
    flashingOnCharNode->SetValue("flashingoncharoptions");

    node = flashingNode->CreateNodeBefore(CS_NODE_ELEMENT, 0);
    node->SetValue("main");
    node->SetAttribute("value", settings.mainFlashing ? "yes" : "no");

    node = flashingNode->CreateNodeBefore(CS_NODE_ELEMENT, 0);
    node->SetValue("npc");
    node->SetAttribute("value", settings.npcFlashing ? "yes" : "no");

    node = flashingNode->CreateNodeBefore(CS_NODE_ELEMENT, 0);
    node->SetValue("tell");
    node->SetAttribute("value", settings.tellFlashing ? "yes" : "no");

    node = flashingNode->CreateNodeBefore(CS_NODE_ELEMENT, 0);
    node->SetValue("guild");
    node->SetAttribute("value", settings.guildFlashing ? "yes" : "no");

    node = flashingNode->CreateNodeBefore(CS_NODE_ELEMENT, 0);
    node->SetValue("group");
    node->SetAttribute("value", settings.groupFlashing ? "yes" : "no");

    node = flashingNode->CreateNodeBefore(CS_NODE_ELEMENT, 0);
    node->SetValue("auction");
    node->SetAttribute("value", settings.auctionFlashing ? "yes" : "no");

    node = flashingNode->CreateNodeBefore(CS_NODE_ELEMENT, 0);
    node->SetValue("system");
    node->SetAttribute("value", settings.systemFlashing ? "yes" : "no");

    node = flashingNode->CreateNodeBefore(CS_NODE_ELEMENT, 0);
    node->SetValue("help");
    node->SetAttribute("value", settings.helpFlashing ? "yes" : "no");

    flashingOnCharNode = chatNode->CreateNodeBefore(CS_NODE_ELEMENT, 0);
    flashingOnCharNode->SetValue("flashingoncharoptions");


    filtersNode = chatNode->CreateNodeBefore(CS_NODE_ELEMENT,0);
    filtersNode->SetValue("filters");
    badWordsNode = filtersNode->CreateNodeBefore(CS_NODE_ELEMENT,0);
    badWordsNode->SetValue("badwords");
    badWordsNode->SetAttributeAsInt("incoming",(int)settings.enableBadWordsFilterIncoming);
    badWordsNode->SetAttributeAsInt("outgoing",(int)settings.enableBadWordsFilterOutgoing);

    csString s;
    for (size_t z = 0; z < settings.badWords.GetSize(); z++)
    {
        badWordsTextNode = badWordsNode->CreateNodeBefore(CS_NODE_ELEMENT,0);
        badWordsTextNode->SetValue("replace");
        badWordsTextNode->SetAttribute("bad",settings.badWords[z]);
        if (settings.goodWords[z].Length())
        {
            badWordsTextNode->SetAttribute("good",settings.goodWords[z]);
        }
    }

    // Now for the colors
    colorNode = chatNode->CreateNodeBefore(CS_NODE_ELEMENT,0);
    colorNode->SetValue("chatcolors");

    CreateSettingNode(colorNode,settings.systemColor,"systemtext");
    CreateSettingNode(colorNode,settings.playerColor,"playernametext");
    CreateSettingNode(colorNode,settings.auctionColor,"auctiontext");
    CreateSettingNode(colorNode,settings.helpColor,"helptext");
    CreateSettingNode(colorNode,settings.groupColor,"grouptext");
    CreateSettingNode(colorNode,settings.yourColor,"yourtext");
    CreateSettingNode(colorNode,settings.guildColor,"guildtext");
    CreateSettingNode(colorNode,settings.shoutColor,"shouttext");
    CreateSettingNode(colorNode,settings.tellColor,"telltext");
    CreateSettingNode(colorNode,settings.chatColor,"chattext");
    CreateSettingNode(colorNode,settings.adminColor,"admintext");
    CreateSettingNode(colorNode,settings.gmColor, "gmtext" );

    csRef<iDocumentNode> msgNode = chatNode->CreateNodeBefore(CS_NODE_ELEMENT,0);
    msgNode->SetValue("msgfilters");

    // and the message filters
    for(int a = 0;a < COMBAT_TOTAL_AMOUNT;a++)
    {
        csString type;

        bool value1 = true,
             value2 = true;

        CHAT_COMBAT_FILTERS filter;

        // Check which type it is
        switch(a)
        {
            case 0:
                type = "SUC";
                filter = COMBAT_SUCCEEDED;
                break;
            case 1:
                type = "BLO";
                filter = COMBAT_BLOCKED;
                break;
            case 2:
                type = "DOD";
                filter = COMBAT_DODGED;
                break;
            case 3:
                type = "MIS";
                filter = COMBAT_MISSED;
                break;
            case 4:
                type = "FAI";
                filter = COMBAT_FAILED;
                break;
            case 5:
            default:
                type = "STA";
                filter = COMBAT_STANCE;
                break;
        }


        // Enabled?
        if(!(settings.meFilters & filter))
            value1 = false;

        if(!(settings.vicinityFilters & filter))
            value2 = false;

        // Create the nodes
        csRef<iDocumentNode> cNode = msgNode->CreateNodeBefore(CS_NODE_ELEMENT,0);
        cNode->SetValue("me");
        cNode->SetAttribute("type",type);
        cNode->SetAttribute("value",value1?"true":"false");

        csRef<iDocumentNode> c2Node = msgNode->CreateNodeBefore(CS_NODE_ELEMENT,0);
        c2Node->SetValue("vicinity");
        c2Node->SetAttribute("type",type);
        c2Node->SetAttribute("value",value2?"true":"false");
    }

    doc->Write(file);
}

void pawsChatWindow::HandleSystemMessage(MsgEntry *me)
{
    psSystemMessage msg(me);

    if(!settings.echoScreenInSystem && 
        (msg.type == MSG_OK || msg.type == MSG_ERROR || msg.type == MSG_RESULT || msg.type == MSG_ACK))
        return;

    if ( msg.msgline != "." )
    {
        const char* start = msg.msgline.GetDataSafe();

        char* currentLine = new char[msg.msgline.Length()+1];
        csString header;//Used for storing the header in case of MSG_WHO

        //Handles messages containing the /who content
        if(msg.type == MSG_WHO)
        { 
            csString msgCopy=msg.msgline;               // Make a copy for not modifying the original
            size_t nLocation = msgCopy.FindFirst('\n'); // This get the end of the first line
            csArray<csString> playerLines;              // This is used for storing the data related to the players online 
            msgCopy.SubString(header,0, nLocation);     // The first line is copied, since it is the header and shouldn't be sorted.
            csString playerCount; 

            if (nLocation > 0)
            {
                size_t stringStart = nLocation + 1;
                nLocation = msgCopy.FindFirst ('\n', stringStart);

                csString line;

                while (nLocation != ((size_t)-1))//Until it is possible to find "\n" 
                {
                    // Extract the current string
                    msgCopy.SubString(line, stringStart , nLocation-stringStart);
                    playerLines.Push(line);

                    // Find next substring
                    stringStart = nLocation + 1;
                    nLocation = msgCopy.FindFirst ('\n', stringStart);
                }
                playerCount=playerLines.Pop();//I discard the last element -it is not a player- and copy somewhere.
                playerLines.Sort();//This sorts the list.
            }

            for (size_t i=0; i< playerLines.GetSize();i++)//Now we copy the sorted names in the header msg.
            {
                header.Append('\n');
                header.Append(playerLines[i]);
            }
            header.Append('\n');
            header.Append(playerCount);//We add the line that says how many players there are online
            start= header.GetData();//We copy to start, so it can be displayed like everything else in the chat.            

        }
        const char* workingString = start;
        while(workingString)
        {
            strcpy(currentLine, workingString);
            workingString = strchr(workingString, '\n');
            if(workingString)
            {
                currentLine[workingString - start] = '\0';
                workingString++;
                start = workingString;
            }


            // Handle filtering stances.
            if (msg.type == MSG_COMBAT_STANCE)
            {
                GEMClientActor *gca =
                    (GEMClientActor*) psengine->GetCelClient()->GetMainPlayer();
                bool local = (strstr(currentLine, gca->GetName()) == currentLine);

                if ((local && !(settings.meFilters & COMBAT_STANCE))
                    || (!local && !(settings.vicinityFilters & COMBAT_STANCE)))
                {
                    return;
                }
            }

            csString buff;
            buff.Format(">%s", currentLine);

            int colour = settings.systemColor;

            csString noCaseMsg = currentLine;
            noCaseMsg.Downcase();

            if (psContain(noCaseMsg,systemTriggers))
            {
                colour = settings.adminColor;
            }

            int chatType = CHAT_SYSTEM;
            if (msg.type == MSG_INFO_SERVER)
                chatType = CHAT_SERVER_INFO;
            else if (msg.type == MSG_INFO_BASE ||
                     msg.type == MSG_COMBAT_STANCE ||
                     msg.type == MSG_COMBAT_OWN_DEATH ||
                     msg.type == MSG_COMBAT_DEATH ||
                     msg.type == MSG_COMBAT_VICTORY ||
                     msg.type == MSG_COMBAT_NEARLY_DEAD ||
                     msg.type == MSG_LOOT)
                chatType = CHAT_SYSTEM_BASE;

            WordArray playerName(psengine->GetCelClient()->GetMainPlayer()->GetName());
            bool hasCharName = noCaseMsg.Find(playerName[0].Downcase().GetData()) != (size_t)-1;
            ChatOutput(buff.GetData(), colour, chatType, true, hasCharName);

            if (soundmgr && psengine->GetSoundStatus())
			{
                soundmgr->HandleSoundType(msg.type);
			}

            LogMessage(msg.type == MSG_INFO_BASE ? CHAT_LOG_ALL : CHAT_LOG_SYSTEM, buff.GetData());
        }
        delete[] currentLine;
    }
    return;
}

void pawsChatWindow::HandleMessage (MsgEntry *me)
{
    bool sendAway = false;
    bool flashEnabled = true;

    if ( me->GetType() == MSGTYPE_SYSTEM )
    {
        HandleSystemMessage(me);
        return;
    }

    psChatMessage msg(me);

    if ((msg.iChatType != CHAT_TELLSELF) && (psengine->GetCelClient()->GetActorByName(msg.sPerson, false) == NULL) &&
        (psengine->GetCelClient()->GetActorByName(msg.sPerson, true)))
        msg.sPerson = "Someone";

    if (msg.translate)
        msg.sText = PawsManager::GetSingleton().Translate(msg.sText);

    csString buff;
    int colour = -1;
    const char *pType = msg.GetTypeText();

    if ( !havePlayerName )  // save name for auto-complete later
    {
        noCasePlayerName.Replace(psengine->GetCelClient()->GetMainPlayer()->GetName());
        noCasePlayerName.Downcase(); // Create lowercase string
        noCasePlayerForename = GetWordNumber(noCasePlayerName, 1);
        chatTriggers.Push(noCasePlayerForename);
        havePlayerName = true;
    }

    // Use lowercase strings
    csString noCasePerson = msg.sPerson;
    noCasePerson.Downcase();
    csString noCaseMsg = msg.sText;
    noCaseMsg.Downcase();

    if (IgnoredList->IsIgnored(msg.sPerson) && msg.iChatType != CHAT_TELLSELF && msg.iChatType != CHAT_ADVISOR && msg.iChatType != CHAT_ADVICE)
        return;

    switch( msg.iChatType )
    {
        case CHAT_GROUP:
        {
            // allows /group <person> /me sits down for a private action
            if ( msg.sText.StartsWith("/me ") )
                buff.Format("%s %s", (const char *)msg.sPerson, ((const char *)msg.sText)+4);
            else if ( msg.sText.StartsWith("/my ") )
                buff.Format("%s's %s", (const char *)msg.sPerson, ((const char *)msg.sText)+4);
            else
                buff.Format(PawsManager::GetSingleton().Translate("%s tells group: %s"),
                            (const char *)msg.sPerson, (const char *)msg.sText);
            colour = settings.groupColor;
            break;
        }
        case CHAT_SHOUT:
        {
            buff.Format(PawsManager::GetSingleton().Translate("%s shouts: %s"),
                        (const char *)msg.sPerson, (const char *)msg.sText);
            colour = settings.shoutColor;
            break;
        }
        case CHAT_GM:
        {
            buff.Format(PawsManager::GetSingleton().Translate("%s"), (const char *)msg.sText);
            colour = settings.gmColor;
            break;
        }

        case CHAT_GUILD:
        {
            // allows /guild <person> /me sits down for a private action
            if ( msg.sText.StartsWith("/me ") )
                buff.Format("%s %s", (const char *)msg.sPerson, ((const char *)msg.sText)+4);
            else if ( msg.sText.StartsWith("/my ") )
                buff.Format("%s's %s", (const char *)msg.sPerson, ((const char *)msg.sText)+4);
            else
                buff.Format(PawsManager::GetSingleton().Translate("%s from %s: %s"), pType,
                            (const char *)msg.sPerson,(const char *)msg.sText);
            colour = settings.guildColor;
            break;
        }

        case CHAT_AUCTION:
        {
            buff.Format(PawsManager::GetSingleton().Translate("%s from %s: %s"), pType,
                        (const char *)msg.sPerson, (const char *)msg.sText);
            colour = settings.auctionColor;
            break;
        }

        case CHAT_SAY:
        case CHAT_NPC:
        {
            buff.Format(PawsManager::GetSingleton().Translate("%s says: %s"),
                        (const char *)msg.sPerson, (const char *)msg.sText);
            colour = settings.chatColor;
            break;
        }

        case CHAT_ME:
        case CHAT_NPC_ME:
        {
            buff.Format("%s %s", (const char *)msg.sPerson, ((const char *)msg.sText));
            colour = settings.chatColor;
            break;
        }

        case CHAT_MY:
        case CHAT_NPC_MY:
        {
            buff.Format("%s's %s", (const char *)msg.sPerson, ((const char *)msg.sText));
            colour = settings.chatColor;
            break;
        }
        case CHAT_NPC_NARRATE:
        {
            buff.Format("-%s-", (const char *)msg.sText);
            colour = settings.shoutColor;
            break;
        }
        case CHAT_PET_ACTION:
        {
            if (msg.sText.StartsWith("'s "))
                buff.Format("%s%s", (const char *)msg.sPerson, ((const char *)msg.sText));
            else
                buff.Format("%s %s", (const char *)msg.sPerson, ((const char *)msg.sText));
            colour = settings.chatColor;
            break;
        }

        case CHAT_TELL:

            //Move list of lastreplies down to make room for new lastreply if necessary
            if (replyList[0] != msg.sPerson)
            {
                int i = 0;
                while(!(replyList[i].IsEmpty()) && i<=4)
                    i++;
                for (int j=i-2; j>=0; j--)
                    replyList[j+1]=replyList[j];
                replyList[0] = msg.sPerson;
            }
            replyCount = 0;

            if ( awayText.Length() && !msg.sText.StartsWith("[auto-reply]") )
                sendAway = true;

            // no break (on purpose)

        case CHAT_SERVER_TELL:

            // allows /tell <person> /me sits down for a private action
            if ( msg.sText.StartsWith("/me ") )
                buff.Format("%s %s", (const char *)msg.sPerson, ((const char *)msg.sText)+4);
            else if ( msg.sText.StartsWith("/my ") )
                buff.Format("%s's %s", (const char *)msg.sPerson, ((const char *)msg.sText)+4);
            else
                buff.Format(PawsManager::GetSingleton().Translate("%s tells you: %s"),
                            (const char *)msg.sPerson, (const char *)msg.sText);

            colour = settings.tellColor;
            break;

        case CHAT_TELLSELF:
        {
            if ( msg.sText.StartsWith("/me ") )
            {
                WordArray tName(psengine->GetCelClient()->GetMainPlayer()->GetName());
                buff.Format("%s %s",tName[0].GetData(),
                            ((const char *)msg.sText)+4);
            }
            else if ( msg.sText.StartsWith("/my ") )
            {
                WordArray tName(psengine->GetCelClient()->GetMainPlayer()->GetName());
                buff.Format("%s's %s",tName[0].GetData(),
                            ((const char *)msg.sText)+4);
            }
            else
                buff.Format(PawsManager::GetSingleton().Translate("You tell %s: %s"),
                            (const char *)msg.sPerson, (const char *)msg.sText);

            if (IgnoredList->IsIgnored(msg.sPerson))
                ChatOutput(PawsManager::GetSingleton().Translate("You are ignoring all replies from that person."));

            colour = settings.yourColor;
            flashEnabled = false;
            break;
        }

        case CHAT_ADVISOR:
        {
            if (strstr(noCasePlayerName,noCasePerson))
            {
                buff.Format(PawsManager::GetSingleton().Translate("You advise %s with this suggestion: %s"),
                            msg.sOther.GetDataSafe(), msg.sText.GetDataSafe());
            }
            else
            {
                csString noCaseOtherName = msg.sOther;
                noCaseOtherName.Downcase();

                if (strstr(noCasePlayerName, noCaseOtherName))
                    buff.Format(PawsManager::GetSingleton().Translate("The advisor suggests: %s"), msg.sText.GetData());
                else
                    buff.Format(PawsManager::GetSingleton().Translate("%s advises %s with this suggestion: %s"),
                                msg.sPerson.GetData(), msg.sOther.GetDataSafe(), msg.sText.GetData());
            }
            colour = settings.helpColor;
            break;
        }

        case CHAT_ADVICE:
        {
            if (strstr(noCasePlayerName, noCasePerson))
            {
                buff.Format(PawsManager::GetSingleton().Translate("You ask: %s"), msg.sText.GetData());
            }
            else
            {
                buff.Format(PawsManager::GetSingleton().Translate("%s asks: %s"),
                            (const char *)msg.sPerson, (const char *)msg.sText);
            }
            colour = settings.helpColor;
            break;
        }

        case CHAT_ADVICE_LIST:
        {
            buff.Format(PawsManager::GetSingleton().Translate("%s previously asked: %s"),
                        (const char *)msg.sPerson, (const char *)msg.sText);
            colour = settings.helpColor;
            break;
        }

        default:
        {
            buff.Format(PawsManager::GetSingleton().Translate("Unknown Chat Type: %d"), msg.iChatType);
            colour = settings.chatColor;
            break;
        }
    }

    // Add the player to our auto-complete list
    AddAutoCompleteName(msg.sPerson);

    if (msg.iChatType != CHAT_TELL &&
        msg.iChatType != CHAT_TELLSELF)
    {
        if ( noCasePlayerForename == noCasePerson )
        {
            colour = settings.yourColor;
            flashEnabled = false;
        }
        else if ( psContain(noCaseMsg,chatTriggers) )
        {
            colour = settings.playerColor;
        }
    }

    WordArray playerName(psengine->GetCelClient()->GetMainPlayer()->GetName());
    bool hasCharName = msg.sText.Downcase().Find(playerName[0].Downcase().GetData()) != (size_t)-1;

    if (!buff.IsEmpty())
	{
        ChatOutput(buff.GetData(), colour, msg.iChatType, flashEnabled, hasCharName);
	}

    LogMessage(CHAT_LOG_ALL, buff.GetDataSafe());

    if ( sendAway )
    {
        csString autoResponse, clientmsg(awayText);
        if ( clientmsg.StartsWith("/me ") )
            clientmsg.Format("%s %s", psengine->GetCelClient()->GetMainPlayer()->GetName(), ((const char *)awayText)+4);
        else if ( clientmsg.StartsWith("/my ") )
            clientmsg.Format("%s's %s",psengine->GetCelClient()->GetMainPlayer()->GetName(), ((const char *)awayText)+4);
 
        autoResponse.Format("/tell %s [auto-reply] %s", (const char*)msg.sPerson, clientmsg.GetData());
		const char* errorMessage = cmdsource->Publish(autoResponse.GetData());
        if ( errorMessage )
            ChatOutput( errorMessage );
    }
}


void pawsChatWindow::SubscribeCommands()
{
    cmdsource->Subscribe("/say",this);
    cmdsource->Subscribe("/shout",this);
    cmdsource->Subscribe("/tell",this);
    cmdsource->Subscribe("/guild",this);
    cmdsource->Subscribe("/group",this);
    cmdsource->Subscribe("/tellnpc",this);

    cmdsource->Subscribe("/me",this);
    cmdsource->Subscribe("/my",this);
    cmdsource->Subscribe("/mypet",this);
    cmdsource->Subscribe("/auction",this);
    cmdsource->Subscribe("/report",this);
}

bool pawsChatWindow::InputActive()
{
    return inputText->HasFocus();
}

bool pawsChatWindow::OnMouseDown( int button, int modifiers, int x , int y )
{
    pawsWidget::OnMouseDown( button, modifiers, x, y );

    PawsManager::GetSingleton().SetCurrentFocusedWidget( inputText );

    for (size_t z = 0; z < children.GetSize(); z++ )
    {
        //children[z]->Show();
    }

    return true;
}

void pawsChatWindow::Show()
{
    pawsControlledWindow::Show();

    for (size_t x = 0; x < children.GetSize(); x++ )
    {
        children[x]->Show();
    }
    BringToTop(inputText);
}


void pawsChatWindow::OnLostFocus()
{
    hasFocus = false;

    for (size_t x = 0; x < children.GetSize(); x++ )
    {
        //children[x]->Hide();
    }

    //chatText->Show();
    //chatText->GetBorder()->Hide();

    //systemText->Show();
    //systemText->GetBorder()->Hide();


}


bool pawsChatWindow::OnKeyDown(int keyCode, int key, int modifiers )
{
    switch ( key )
    {
        case CSKEY_ENTER:
        {
            SendChatLine();
            
            if (settings.looseFocusOnSend || !strcmp(inputText->GetText(), ""))
            {
                pawsWidget *topParent;
                for (topParent=this; topParent->GetParent(); topParent = topParent->GetParent());
                    PawsManager::GetSingleton().SetCurrentFocusedWidget(topParent);
            }

            inputText->Clear();

            break;
        }

        // History access - Previous
        case CSKEY_UP:
        {
            if (strlen(inputText->GetText()) == 0 && currLine.GetData() != NULL)
            {
                // Restore cleared line from buffer
                inputText->SetText(currLine);
                currLine.Free(); // Set to NULL
            }
            else  // Browse history
            {
                csString* cmd = chatHistory->GetPrev();
                if (cmd)
                {
                    // Save current line in buffer
                    if (currLine.GetData() == NULL)
                        currLine = inputText->GetText();

                    inputText->SetText(cmd->GetData());
                }
            }

            break;
        }

        // History access - Next
        case CSKEY_DOWN:
        {
            csString* cmd = chatHistory->GetNext();
            if (cmd)  // Browse history
            {
                inputText->SetText(cmd->GetData());
            }
            else if (currLine.GetData() != NULL)
            {
                // End of history; restore current line from buffer
                inputText->SetText(currLine);
                currLine.Free(); // Set to NULL
                chatHistory->SetGetLoc(0);
            }
            else if (strlen(inputText->GetText()) > 0)
            {
                // Pressing down at bottom clears
                currLine = inputText->GetText();
                inputText->SetText("");
            }

            break;
        }

        // Tab Completion
        case CSKEY_TAB:
        {
            // What's the command we're about to tab-complete?
            const char *cmd = inputText->GetText();

            if (!cmd)
                return true;

            // What kind of completion are we doing?
            if (cmd[0] == '/' && !strchr(cmd,' '))
            {
                // Tab complete the started command
                TabCompleteCommand(cmd);
            }
            else
            {
                // Tab complete the name
                TabCompleteName(cmd);
            }
        }

        default:
        {
            // This statement should always be true since the printable keys should be handled
            // by the child (edittextbox)
            // if ( !isprint( (char)key ))
            //{
            if (parent)
                return parent->OnKeyDown( keyCode, key, modifiers );
            else
                return false;
            //}
            break;
        }
    }

    return true;
}

void pawsChatWindow::SendChatLine()
{
    csString textToSend = inputText->GetText();

    if ( textToSend.Length() )
    {
        if ( textToSend.GetAt(0) != '/' )
        {
            csString chatType = tabs->GetActiveTab()->GetName();

            if (chatType == "TellText")
            {
                int i = 0;
                csString buf;
                while (!replyList[i].IsEmpty() && i <= 4)
                    i++;
                if (!i)
                {
                    textToSend.Insert(0, "/tell ");
                }else{
                    if (replyCount >= i)
                        replyCount = 0;
                    buf.Format("/tell %s ", replyList[replyCount].GetData());
                    textToSend.Insert(0, buf.GetData());
                    replyCount++;
                }
            }
            else if (chatType == "GuildText")
                textToSend.Insert(0, "/guild ");
            else if (chatType == "GroupText")
                textToSend.Insert(0, "/group ");
            else if (chatType == "AuctionText")
                textToSend.Insert(0, "/auction ");
            else if (chatType == "HelpText")
                textToSend.Insert(0, "/advisor ");
            else if (chatType == "NpcText")
                textToSend.Insert(0, "/tellnpc ");
            else
                textToSend.Insert(0, "/say ");

        }

        const char* errorMessage = cmdsource->Publish(textToSend);
        if (errorMessage)
            ChatOutput(errorMessage);

        // Insert the command into the history
        chatHistory->Insert(textToSend);
        currLine.Free(); // Set to NULL
    }
}

bool pawsChatWindow::OnMenuAction(pawsWidget * widget, const pawsMenuAction & action)
{
    if (action.name == "TranslatedChat")
    {
        if (action.params.GetSize() > 0)
        {
            psChatMessage chat(0, "", 0, action.params[0], CHAT_SAY, true);
            msgqueue->SendMessage(chat.msg);
        }
        else
            Error2("Menu action \"TranslatedChat\" must have one parameter (widget name is %s)", widget->GetName());
    }

    return pawsWidget::OnMenuAction(widget, action);
}

void pawsChatWindow::AutoselectChatTabIfNeeded(const csArray<csString> &allowedTabs, const char * defaultTab)
{
    pawsWidget * currentTab;
    csString currentTabName;
    size_t allowedTab;

    currentTab = tabs->GetActiveTab();
    if (currentTab != NULL)
    {
        currentTabName = currentTab->GetName();
        allowedTab = 0;
        while (allowedTab < allowedTabs.GetSize() && currentTabName != allowedTabs[allowedTab])
            allowedTab++;
        if (allowedTab >= allowedTabs.GetSize())
        {
            tabs->SetTab(defaultTab);
            PawsManager::GetSingleton().SetCurrentFocusedWidget( inputText );
            BringToTop( inputText );
        }
    }
    else
    {
        tabs->SetTab(defaultTab);
        PawsManager::GetSingleton().SetCurrentFocusedWidget( inputText );
        BringToTop( inputText );
    }
}

bool pawsChatWindow::OnButtonPressed( int mouseButton, int keyModifier, pawsWidget* widget )
{
    // We know that the calling widget is a button.
    csString name = widget->GetName();

    if ( name=="inputback" )
    {
        PawsManager::GetSingleton().SetCurrentFocusedWidget( inputText );
        BringToTop( inputText );
    }

    return true;
}


void pawsChatWindow::PerformAction( const char* action )
{
    csString temp(action);
    if ( temp == "togglevisibility" )
    {
        pawsWidget::PerformAction( action );
        PawsManager::GetSingleton().SetCurrentFocusedWidget( NULL );
    }
}

void pawsChatWindow::RefreshCommandList()
{
    cmdsource->GetSubscribedCommands(commandList);
}

/// Tab completion
void pawsChatWindow::TabCompleteCommand(const char *cmd)
{
    // Sanity
    if (!cmd)
        return;

    // Make sure we have our command list
    if (!commandList.Count())
        RefreshCommandList();

    psString partial(cmd);
    //psString *found = commandList.FindPartial(&partial);
    //if (found)
    //{
    //    inputText->SetText(*found);  // unique partial can be supplied
    //    return;
    //}

    //// display list of partial matches if non-unique
    //found = commandList.FindPartial(&partial,false);
    int count = 0;

    // valid but not unique
    psString list, *last=NULL;
    size_t max_common = 50; // big number gets pulled in
    BinaryRBIterator<psString> loop(&commandList);
    for (psString* found = loop.First(); found; found = ++loop)
    {
        if (!found->PartialEquals(partial))
        {
            count++;
            if(!list.IsEmpty())
                list.Append(" ");
            list.Append(*found);
            if (!last)
                last = found;
            size_t common = found->FindCommonLength(*last);

            if (common < max_common)
            {
                max_common = common;
                last = found;
            }
        }
        else if (list.Length())
            break;
    }
    if (count == 1)
    {
        list.Append(" ");
        inputText->SetText(list.GetData());  // unique partial can be supplied
        return;
    }
    else if(count > 1)
    {
        list.Insert(0,PawsManager::GetSingleton().Translate("Ambiguous command: "));
        ChatOutput(list);
        psString complete;
        last->GetSubString(complete,0,max_common);
        inputText->SetText(complete);
    }

    return;
}

void pawsChatWindow::TabCompleteName(const char *cmdstr)
{
    // Sanity
    if (!cmdstr || *cmdstr == 0)
        return;

    // Set cmd to point to the start of the name.
    char *cmd = (char *)(cmdstr + strlen(cmdstr) - 1);
    while (cmd != cmdstr && isalpha(*cmd))
        --cmd;
    if (!isalpha(*cmd))
        cmd++;

    // Make sure we have our auto-complete list
    if (!autoCompleteNames.GetSize() )
    {
        return;
    }

    psString partial(cmd);
    if (partial.Length () == 0)
        return;
    partial.SetAt(0, toupper(partial.GetAt(0)));

    // valid but not unique
    psString list, last;
    size_t max_common = 50; // big number gets pulled in
    int matches = 0;
    csArray<csString>::Iterator iter = autoCompleteNames.GetIterator();
    while (iter.HasNext())
    {
        psString found = iter.Next();
        if (!found.PartialEquals(partial))
        {
            list.Append(" ");
            list.Append(found);
            if (!last)
                last = found;

            matches++;

            size_t common = found.FindCommonLength(last);

            if (common < max_common)
            {
                max_common = common;
                last = found;
            }
        }
    }

    if (matches > 1)
    {
        list.Insert(0,PawsManager::GetSingleton().Translate("Ambiguous name:"));
        ChatOutput(list);
        
        psString line, partial;
        
        last.GetSubString(partial,0,max_common);
        line = cmdstr;
        line.DeleteAt(cmd-cmdstr,line.Length()-(cmd-cmdstr) );
        line += partial;
        inputText->SetText(line);
    }
    else if (matches == 1)
    {
        psString complete(cmdstr);
        complete.DeleteAt(cmd-cmdstr,complete.Length()-(cmd-cmdstr) );
        complete.Append(last);
        complete.Append(" ");
        inputText->SetText(complete);
    }

    return;
}


void pawsChatWindow::AutoReply(void)
{
    int i = 0;
    csString buf;

    while (!replyList[i].IsEmpty() && i <= 4)
        i++;

    if (!i)
    {
        inputText->SetText("/tell ");
        return;
    }

    if (replyCount >= i)
        replyCount = 0;

    buf.Format("/tell %s ", replyList[replyCount].GetData());
    inputText->SetText(buf.GetData());

    replyCount++;
}

void pawsChatWindow::SetAway(const char* text)
{
	if ( !strcmp(text, "off") || strlen(text) == 0)
	{
        awayText.Clear();
        ChatOutput( "Auto-reply has been turned OFF" );
    }
    else
    {
        awayText = text;
        ChatOutput( "Auto-reply SET");
    }
}

void pawsChatWindow::Clear()
{
    mainText->Clear();
    npcText->Clear();
    systemText->Clear();
    tellText->Clear();
    guildText->Clear();
    groupText->Clear();
    auctionText->Clear();
    helpText->Clear();
}


void pawsChatWindow::BadWordsFilter(csString& s)
{
    csString mask = "$@!";
    csString lowercase;
    size_t badwordPos;

    lowercase = s;
    lowercase.Downcase();

    for (size_t i = 0; i < settings.badWords.GetSize(); i++)
    {
        size_t pos = 0;
        while (true)
        {
            csString badWord = settings.badWords[i];
            csString replace = settings.goodWords[i];

            badwordPos = lowercase.FindStr(badWord.GetDataSafe(),pos);

            // LOOP EXIT:
            if (badwordPos == SIZET_NOT_FOUND)
                break;

            // Check for whole word boundaries.  First confirm beginning of word
            if (badwordPos>0 && isalpha(lowercase[badwordPos-1]))
            {
                pos++;
                break;
            }
            // now verify end of word
            if (badwordPos+badWord.Length() < lowercase.Length() && isalpha(lowercase[badwordPos+badWord.Length()]))
            {
                pos++;
                break;
            }

            if (replace.Length() == 0)
            {
                for (size_t k = badwordPos; k < badwordPos + badWord.Length(); k++)
                    s[k] = mask[ (k-badwordPos) % mask.Length() ];
                pos = badwordPos + badWord.Length();
            }
            else
            {
                s.DeleteAt( badwordPos, badWord.Length());
                s.Insert(badwordPos,replace);
                pos = badwordPos + replace.Length();
            }
            lowercase = s;
            lowercase.Downcase();
        }
    }
}

void pawsChatWindow::ChatOutput(const char* data, int colour, int type, bool flashEnabled, bool hasCharName)
{
    csString s = data;
    if (settings.enableBadWordsFilterIncoming)
    {
        BadWordsFilter(s);
        data = s.GetDataSafe();
    }

    pawsWidget *currentTab = tabs->GetActiveTab();

    // Indicates that the text is also shown on the main tab
    bool toMain = false;

    // Indicates that the main tab should be flashing
    bool flashMain = flashEnabled;

    switch (type)
    {
        case CHAT_SERVER_INFO:
            // We are posting this everywhere, no need to flash.
            ChatOutput(systemText, data, colour, false, "System Button");
            ChatOutput(tellText, data, colour, false, "Tell Button");
            ChatOutput(guildText, data, colour, false, "Guild Button");
            ChatOutput(groupText, data, colour, false, "Group Button");
            ChatOutput(auctionText, data, colour, false, "Auction Button");
            ChatOutput(helpText, data, colour, false, "Help Button");
            ChatOutput(npcText, data, colour, false, "NPC Button");
            toMain = true;
            flashMain = false; // Since it goes to all tabs, no reason to flash the main tab
            break;

        case CHAT_SYSTEM:
            toMain = settings.systemIncluded;
            flashMain &= currentTab != systemText;
            ChatOutput(systemText, data, colour, flashEnabled && !toMain && (settings.systemFlashing || (hasCharName && settings.systemcFlashing)) , "System Button");
            break;

        case CHAT_TELL:
        case CHAT_TELLSELF:
            toMain = settings.tellIncluded;
            flashMain &= currentTab != tellText;
            ChatOutput(tellText, data, colour, flashEnabled && !toMain && (settings.tellFlashing || (hasCharName && settings.tellcFlashing)), "Tell Button");
            break;

        case CHAT_GUILD:
            toMain = settings.guildIncluded;
            flashMain &= currentTab != guildText;
            ChatOutput(guildText, data, colour, flashEnabled && !toMain && (settings.guildFlashing || (hasCharName && settings.guildcFlashing)), "Guild Button");
            break;

        case CHAT_GROUP:
            toMain = settings.groupIncluded;
            flashMain &= currentTab != groupText;
            ChatOutput(groupText, data, colour, flashEnabled && !toMain && (settings.groupFlashing || (hasCharName && settings.groupcFlashing)), "Group Button");
            break;

        case CHAT_AUCTION:
            toMain = settings.auctionIncluded;
            flashMain &= currentTab != auctionText;
            ChatOutput(auctionText, data, colour, flashEnabled && !toMain && (settings.auctionFlashing || (hasCharName && settings.auctioncFlashing)), "Auction Button");
            break;

        case CHAT_ADVISOR:
        case CHAT_ADVICE:
        case CHAT_ADVICE_LIST:
            toMain = settings.helpIncluded;
            flashMain &= currentTab != helpText;
            ChatOutput(helpText, data, colour, flashEnabled && !toMain && (settings.helpFlashing || (hasCharName && settings.helpcFlashing)), "Help Button");
            break;

        case CHAT_NPC:
        case CHAT_NPC_ME:
        case CHAT_NPC_MY:
        case CHAT_NPC_NARRATE:
            toMain = settings.npcIncluded;
            flashMain &= currentTab != npcText;
            ChatOutput(npcText, data, colour, flashEnabled && !toMain && (settings.npcFlashing || (hasCharName && settings.npccFlashing)), "NPC Button");
            break;

        case CHAT_SYSTEM_BASE:
            // System base messages go either to the main tab or to the system tab
            if (settings.systemBaseIncluded)
                toMain = true;
            else
            {
                // There is another chance that it still would be shown on the main tab as well
                toMain = settings.systemIncluded;
                flashMain &= currentTab != systemText;
                ChatOutput(systemText, data, colour, flashEnabled && !toMain && (settings.systemFlashing || (hasCharName && settings.systemcFlashing)), "System Button");
            }
            break;

        default:
            // Anything else goes to the Main tab (say, shout, system base)
            toMain = true;
            break;
    }

    if (toMain)
        ChatOutput(mainText, data, colour, flashMain && (settings.mainFlashing || (hasCharName && settings.maincFlashing)), "Main Button");
}

void pawsChatWindow::ChatOutput(pawsMessageTextBox *pmtb, const char *data,
                                int colour, bool flashEnabled,
                                const char *buttonName)
{
    pmtb->AddMessage(data, colour);

    if (flashEnabled && buttonName != NULL && FindWidget(buttonName))
        ((pawsButton *) FindWidget(buttonName))->Flash(true);
}

void pawsChatWindow::AddAutoCompleteName(const char *name)
{
    for (size_t i = 0; i < autoCompleteNames.GetSize(); i++)
    {
        if (autoCompleteNames[i].CompareNoCase(name))
        {
            return;
        }
    }

    autoCompleteNames.Push(name);
}

void pawsChatWindow::RemoveAutoCompleteName(const char *name)
{
    for (size_t i = 0; i < autoCompleteNames.GetSize(); i++)
    {
        if (autoCompleteNames[i].CompareNoCase(name))
        {
            autoCompleteNames.DeleteIndex(i);
            Debug2(LOG_CHAT,0, "Removed %s from autoComplete list.\n",name);
        }
    }
}

void pawsChatWindow::NpcChat()
{
    csArray<csString> allowedTabs;
    allowedTabs.Push("NpcText");

    AutoselectChatTabIfNeeded(allowedTabs, "NPC Button");
}

//------------------------------------------------------------------------------

pawsChatHistory::pawsChatHistory()
{
    getLoc = 0;
}

pawsChatHistory::~pawsChatHistory()
{

}

// insert a string at the end of the buffer
// returns: true on success
void pawsChatHistory::Insert(const char *str)
{
    bool previous = false;
    
    if ( buffer.GetSize() > 0 )
    {        
        // Check to see if it's the same as the last one before adding.
        if ( *buffer[ buffer.GetSize()-1] == str )
        {
            previous = true;
        }    
    }
    
    if ( !previous )
    {
        csString * newStr = new csString( str );
        buffer.Push( newStr );    
    }
    getLoc = 0;
}    


// returns a copy of the stored string from 'n' commands back.
// if n is invalid (ie, more than the number of stored commands)
// then a null pointer is returned.
csString *pawsChatHistory::GetCommand(int n)
{
    // n == 0 means "this command" according to our notation
    if (n <= 0 || (size_t)n > buffer.GetSize() )
    {
        return NULL;
    }

    getLoc = n;
    csString* retval = buffer[buffer.GetSize() - (getLoc)];
    if ( retval )
    {
        return retval;
    }

    return NULL;
}

// return the next (temporally) command in history
csString *pawsChatHistory::GetNext()
{
    return GetCommand(getLoc - 1);
}

// return the prev (temporally) command from history
csString *pawsChatHistory::GetPrev()
{
    return GetCommand(getLoc + 1);
}

//------------------------------------------------------------------------------

void pawsChatMenuItem::LoadAction(iDocumentNode * node)
{
    csRef<iDocumentAttribute> attr;
    csString label;

    // If there is no action definition in the XML, it will make its own description
    attr = node->GetAttribute("action");
    if (attr != NULL)
        pawsMenuItem::LoadAction(node);
    else
    {
        attr = node->GetAttribute("label");
        if (attr != NULL)
            label = attr->GetValue();
        else
            label = "<missing label in menu>";
        action.name = "TranslatedChat";
        action.params.SetSize(1);
        action.params[0] = label;
    }
}

void pawsChatWindow::SetSettings(ChatSettings& newSets)
{
    settings = newSets;
}
