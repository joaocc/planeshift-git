/*
* pawsquestwindow.cpp - Author: Keith Fulton
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
//////////////////////////////////////////////////////////////////////

// STANDARD INCLUDE
#include <psconfig.h>
#include <csutil/xmltiny.h>
#include "globals.h"

// COMMON/NET INCLUDES
#include "net/messages.h"
#include "net/msghandler.h"
#include "net/cmdhandler.h"
#include "util/strutil.h"
#include "util/psxmlparser.h"
#include <ivideo/fontserv.h>

#include "pscelclient.h"

// PAWS INCLUDES
#include "pawsquestwindow.h"
#include "paws/pawsprefmanager.h"
#include "inventorywindow.h"
#include "gui/pawscontrolwindow.h"
#include "paws/pawstextbox.h"
#include "paws/pawsyesnobox.h"

#define DISCARD_BUTTON                    1201
#define VIEW_BUTTON                       1200
#define SAVE_BUTTON                       1203
#define CANCEL_BUTTON                     1204
#define TAB_UNCOMPLETED_QUESTS_OR_EVENTS  1000
#define TAB_COMPLETED_QUESTS_OR_EVENTS    1001
#define FLIP_TAB_BUTTON                   1300

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

pawsQuestListWindow::pawsQuestListWindow()
    : psCmdBase( NULL,NULL,  PawsManager::GetSingleton().GetObjectRegistry() )
{
    vfs =  csQueryRegistry<iVFS > ( PawsManager::GetSingleton().GetObjectRegistry());
    xml = psengine->GetXMLParser();

    questList = NULL;
    questTab = NULL;
    completedQuestList = NULL;
    uncompletedQuestList = NULL;
    eventList = NULL;
    eventTab = NULL;
    completedEventList = NULL;
    uncompletedEventList = NULL;
    description = NULL;
    notes = NULL;

    populateGMEventLists = false;
    populateQuestLists = false;

    questID = -1;

    filename = "/planeshift/userdata/questnotes_";
    filename.Append(psengine->GetCelClient()->GetMainActor()->GetName());
    filename.Append(".xml");
    filename.ReplaceAllSubString(" ", "_");

    // Load the quest notes file (if it doesn't exist, it will be made on first save)
    if ( vfs->Exists(filename) )
        LoadNotes(filename);
}

pawsQuestListWindow::~pawsQuestListWindow()
{
    if (msgqueue)
    {
        msgqueue->Unsubscribe(this, MSGTYPE_QUESTLIST);
        msgqueue->Unsubscribe(this, MSGTYPE_QUESTINFO);

        msgqueue->Unsubscribe(this, MSGTYPE_GMEVENT_LIST);
        msgqueue->Unsubscribe(this, MSGTYPE_GMEVENT_INFO);
    }

    while (quest_notes.GetSize() )
        delete quest_notes.Pop();
}

void pawsQuestListWindow::Show(void)
{
    eventList->Clear();
    uncompletedEventList->Clear();
    description->Clear();
    notes->Clear();
 
    psUserCmdMessage msg("/quests");
    psengine->GetMsgHandler()->SendMessage(msg.msg);
 
    pawsControlledWindow::Show();
}

bool pawsQuestListWindow::PostSetup()
{
    // Setup this widget to receive messages and commands
    if ( !psCmdBase::Setup( psengine->GetMsgHandler(), 
        psengine->GetCmdHandler()) )
        return false;

    // Subscribe to certain types of messages (those we want to handle)
    msgqueue->Subscribe(this, MSGTYPE_QUESTLIST);
    msgqueue->Subscribe(this, MSGTYPE_QUESTINFO);

    msgqueue->Subscribe(this, MSGTYPE_GMEVENT_LIST);
    msgqueue->Subscribe(this, MSGTYPE_GMEVENT_INFO);
	
    // Subscribe certain petition commands with this widget
    // cmdsource->Subscribe("/petition_list", PawsManager::GetSingleton().Translate("List all of the petitions you have submitted."),
    //                     CmdHandler::VISIBLE_TO_USER,this);

    questTab             = (pawsTabWindow*)FindWidget("QuestTabs");
    completedQuestList   = (pawsListBox*  )FindWidget("CompletedQuestList");
    uncompletedQuestList = (pawsListBox*  )FindWidget("UncompletedQuestList");

    // get pointer to active listbox:
    questList  = (pawsListBox*)questTab->GetActiveTab();
    
    eventTab             = (pawsTabWindow*)FindWidget("EventTabs");
    completedEventList   = (pawsListBox*  )FindWidget("CompletedEventList");
    uncompletedEventList = (pawsListBox*  )FindWidget("UncompletedEventList");

    // get pointer to active listbox:
    eventList  = (pawsListBox*)eventTab->GetActiveTab();

    topTab = NULL;

    description = (pawsMessageTextBox*)FindWidget("Description");
    notes = (pawsMultilineEditTextBox*)FindWidget("Notes");

    return true;
}

//////////////////////////////////////////////////////////////////////
// Command and Message Handling
//////////////////////////////////////////////////////////////////////

const char* pawsQuestListWindow::HandleCommand( const char* cmd )
{
    return NULL;
}

void pawsQuestListWindow::HandleMessage ( MsgEntry* me )
{
    switch (me->GetType())
    {
        case MSGTYPE_QUESTLIST:
        {
            // Get the petition message data from the server
            psQuestListMessage message(me);

            // The quest xml string contains all the quests for the
            // current user. The quests that have been completed
            // will be added to a different listbox than the quests that
            // still need to be done. So here we'll split the quest xml
            // string into two parts according to the quest status.
            psXMLString questxml = psXMLString(message.questxml);
            psXMLString section;

            size_t start = questxml.FindTag("q>");
            size_t end   = questxml.FindTag("/quest");

            completedQuests   = "<quests>";
            uncompletedQuests = "<quests>";

            if (start != SIZET_NOT_FOUND)
            {
                do
                {
                    // a quest looks like this:
                    // <q><image icon="%s" /><desc text="%s" /><id text="%d" /><status text="%c" /></q>
                    start += questxml.GetTagSection((int)start,"q",section);
                    csString str = "status text=\"";
                    char status = section.GetAt(section.FindSubString(str) + str.Length());
                
                    if (status == 'A')
                        uncompletedQuests += section;
                    else if (status == 'C')
                        completedQuests += section;
                } while (section.Length()!=0 && start<end);
            }

            completedQuests   += "</quests>";
            uncompletedQuests += "</quests>";

            completedQuestList->Clear();
            uncompletedQuestList->Clear();
            description->Clear();
            notes->Clear();
            populateQuestLists = true;	    

            // Reset window
            if (!topTab || topTab == questTab)
            {
                PopulateQuestTab();
            }
            break;
	}

	case MSGTYPE_QUESTINFO:
	{
            psQuestInfoMessage message(me);
            SelfPopulateXML(message.xml);
            break;
	}

	case MSGTYPE_GMEVENT_LIST:
	{
            // get the GM events data
            psGMEventListMessage message(me);

            // The event xml string contains all the GM events for the
            // current user. The events that have been completed
            // will be added to a different listbox than the events that
            // still need to be done. So here we'll split the event xml
            // string into two parts according to the event status.
            psXMLString gmeventxml = psXMLString(message.gmEventsXML);
            psXMLString section;

            size_t start = gmeventxml.FindTag("event>");
            size_t end   = gmeventxml.FindTag("/gmevents");

            completedEvents   = "<gmevents>";
            uncompletedEvents = "<gmevents>";
	    
            if (start != SIZET_NOT_FOUND)
            {
                do
                {
                    // a gm event looks like this:
                    // <event><name text="%s" /><role text="%c" /><status text="%c" /><id text="%d" /></event>
                    start += gmeventxml.GetTagSection((int)start,"event",section);
                    csString str = "status text=\"";
                    char status = section.GetAt(section.FindSubString(str) + str.Length());
                
                    if (status == 'R')
                        uncompletedEvents += section;
                    else if (status == 'C')
                        completedEvents += section;
                } while (section.Length()!=0 && start<end);
            }
            
            completedEvents   += "</gmevents>";
            uncompletedEvents += "</gmevents>";

            completedEventList->Clear();
            uncompletedEventList->Clear();
            description->Clear();
            notes->Clear();
            populateGMEventLists = true;

            if (!topTab || topTab == eventTab)
            {
                PopulateGMEventTab();
            }
	    break;
        }

        case MSGTYPE_GMEVENT_INFO:
        {
            psGMEventInfoMessage message(me);
            SelfPopulateXML(message.xml);
            break;
        }
    }
}

bool pawsQuestListWindow::OnButtonPressed( int mouseButton, int keyModifier, pawsWidget* widget )
{
    // We know that the calling widget is a button.
    int button = widget->GetID();
    
    questList = (pawsListBox*)questTab->GetActiveTab();
    eventList = (pawsListBox*)eventTab->GetActiveTab();
    
    switch( button )
    {
        case TAB_COMPLETED_QUESTS_OR_EVENTS:
        case TAB_UNCOMPLETED_QUESTS_OR_EVENTS:
        {
	    if (topTab == questTab)
            {
                questID = -1;
                completedQuestList->Select(NULL);
                uncompletedQuestList->Select(NULL);
            }
            else
	    {
                questID = -1;
                completedEventList->Select(NULL);
                uncompletedEventList->Select(NULL);
	    }
            description->Clear();
            notes->Clear();
            break;
        }

        // These cases are for the discard funciton, and it's confirmation prompt
        case DISCARD_BUTTON:
        {
            if (topTab == eventTab && questID != -1)
            {
                PawsManager::GetSingleton().CreateYesNoBox(
                    "Are you sure you want to discard the selected event?", this);
                questIDBuffer = questID;
            }
            else if (topTab == questTab && questID != -1)
            {
                PawsManager::GetSingleton().CreateYesNoBox(
                    "Are you sure you want to discard the selected quest?", this );
                questIDBuffer = questID;
            }
            break;
        }
        case CONFIRM_YES:
        {
            if (topTab == questTab)
                DiscardQuest(questIDBuffer);
            else if (topTab == eventTab)
                DiscardGMEvent(questIDBuffer);
            questID = -1;
            
            // Refresh window
            notes->Clear();
            description->Clear();
            eventList->Clear();
            questList->Clear();

            psUserCmdMessage msg("/quests");
            psengine->GetMsgHandler()->SendMessage(msg.msg);
            break;
        }

        // These cases are for the edit window child of this
        case SAVE_BUTTON:
        {
            if (topTab == eventTab)
            {
                PawsManager::GetSingleton().CreateWarningBox(
                    "Notes for GM-Events not yet implemented. Sorry.", this);
            }
            else if (topTab == questTab && questID != -1)
            {
                bool found = false;
                for (size_t i=0; i < quest_notes.GetSize(); i++)
                {
                    if (quest_notes[i]->id == questID) // Change notes
                    {
                        quest_notes[i]->notes = notes->GetText();
                        found = true;
                        break;
                    }
                }
                    
                if (!found) // Add notes
                {
                    QuestNote *qn = new QuestNote;
                    qn->id = questID;
                    qn->notes = notes->GetText();
                    quest_notes.Push(qn);
                }

                SaveNotes(filename);
            }
            break;
        }
        case CANCEL_BUTTON:
        {
            if (topTab == questTab)
            {
                ShowNotes();
            }
            break;
        }

        case FLIP_TAB_BUTTON:
        {
            if (topTab)
                topTab->Hide();

            if (!topTab || topTab == eventTab)
            {
                PopulateQuestTab();
            }
            else
            {
                PopulateGMEventTab();
            }

	    description->Clear();
            notes->Clear();

	    break;
        }
    }
    return true;
}

inline void pawsQuestListWindow::RequestQuestData(int id)
{
    psQuestInfoMessage info(0,psQuestInfoMessage::CMD_QUERY,id,NULL,NULL);
    msgqueue->SendMessage(info.msg);
}

inline void pawsQuestListWindow::RequestGMEventData(int id)
{
    psGMEventInfoMessage info(0,psGMEventInfoMessage::CMD_QUERY,id,NULL,NULL);
    msgqueue->SendMessage(info.msg);
}

inline void pawsQuestListWindow::DiscardQuest(int id)
{
    psQuestInfoMessage info(0,psQuestInfoMessage::CMD_DISCARD,id,NULL,NULL);
    msgqueue->SendMessage(info.msg);
}

inline void pawsQuestListWindow::DiscardGMEvent(int id)
{
    psGMEventInfoMessage info(0,psGMEventInfoMessage::CMD_DISCARD,id,NULL,NULL);
    msgqueue->SendMessage(info.msg);
}

void pawsQuestListWindow::OnListAction( pawsListBox* selected, int status )
{
    int previousQuestID = questID;
    size_t numOfQuests;
    unsigned int topLine = 0;
    int idColumn;

    if (topTab == questTab)
        idColumn = QCOL_ID;
    else if (topTab == eventTab)
        idColumn = EVCOL_ID;
    else
        return;

    pawsListBoxRow *row = selected->GetSelectedRow();
    if (row)
    {
        pawsTextBox *field = (pawsTextBox*)row->GetColumn(idColumn);
        questID = atoi(field->GetText());

        if (topTab == questTab)       // GM Events dont support notes, yet!
        {
            numOfQuests = quest_notes.GetSize();
            for (size_t i=0; i < numOfQuests; i++)
            {
                // if no previous quest selected, then 0 all toplines
                if (previousQuestID < 0)
                {
                    quest_notes[i]->topLine = 0;
                } 
                // store topline of 'old' quest notes
                else if (quest_notes[i]->id == previousQuestID)
                {
                    quest_notes[i]->topLine = notes->GetTopLine();
                }
            }
            for (size_t i=0; i < numOfQuests; i++)
            {
                // set new quest notes topline
                if (quest_notes[i]->id == questID)
                {
                   topLine = quest_notes[i]->topLine;
                }
            }
            notes->SetTopLine(topLine);

            RequestQuestData(questID);
            ShowNotes();
	}
        else
        {
            RequestGMEventData(questID);
        }
    }
}

void pawsQuestListWindow::SaveNotes(const char * fileName)
{
    // Save quest notes to a local file
    char temp[20];
    csRef<iDocumentSystem> xml = csPtr<iDocumentSystem>(new csTinyDocumentSystem);
    csRef<iDocument> doc = xml->CreateDocument();
    csRef<iDocumentNode> root = doc->CreateRoot();
    csRef<iDocumentNode> parentMain = root->CreateNodeBefore(CS_NODE_ELEMENT);
    parentMain->SetValue("questnotes");
    csRef<iDocumentNode> parent;
    csRef<iDocumentNode> text;

    for (size_t i=0; i < quest_notes.GetSize(); i++)
    {
        QuestNote *qn = quest_notes[i];
        
        if ( !(qn->notes).IsEmpty() )
        {
            parent = parentMain->CreateNodeBefore(CS_NODE_ELEMENT);
            sprintf(temp, "quest%d", qn->id);
            parent->SetValue(temp);

            text = parent->CreateNodeBefore(CS_NODE_TEXT);
            text->SetValue(qn->notes);
        }
    }
    doc->Write(vfs, fileName);
}

void pawsQuestListWindow::LoadNotes(const char * fileName)
{
    int id;
    csRef<iDocument> doc = xml->CreateDocument();

    csRef<iDataBuffer> buf (vfs->ReadFile (fileName));
    if (!buf || !buf->GetSize())
    {
        return;
    }
    const char* error = doc->Parse( buf );
    if (error)
    {
        printf("Error loading quest notes: %s\n", error);
        return;
    }

    csRef<iDocumentNodeIterator> iter = doc->GetRoot()->GetNode("questnotes")->GetNodes();

    while ( iter->HasNext() )
    {
        csRef<iDocumentNode> child = iter->Next();
        if ( child->GetType() != CS_NODE_ELEMENT )
            continue;

        sscanf(child->GetValue(), "quest%d", &id);

        QuestNote *qn = new QuestNote;
        qn->id = id;
        qn->notes = child->GetContentsValue();
        quest_notes.Push(qn);
    }
}

void pawsQuestListWindow::ShowNotes()
{
    for (size_t i=0; i < quest_notes.GetSize(); i++)
    {
        if (quest_notes[i]->id == questID)
        {
            notes->SetText(quest_notes[i]->notes);
            return;
        }
    }

    notes->Clear(); // If didn't find notes, then blank
}

void pawsQuestListWindow::PopulateQuestTab(void)
{
    if (populateQuestLists)
    {
        // add quests to the (un)completedQuestList if new
        completedQuestList->SelfPopulateXML(completedQuests);
        uncompletedQuestList->SelfPopulateXML(uncompletedQuests);
        populateQuestLists = false;
    }
    topTab = questTab;
    questTab->SetTab(TAB_UNCOMPLETED_QUESTS_OR_EVENTS);
    completedQuestList->Select(NULL);
    uncompletedQuestList->Select(NULL);

    topTab->Show();
}

void pawsQuestListWindow::PopulateGMEventTab(void)
{
    if (populateGMEventLists)
    {
        // add quests to the (un)completedQuestList if new
        completedEventList->SelfPopulateXML(completedEvents);
        uncompletedEventList->SelfPopulateXML(uncompletedEvents);
        populateGMEventLists = false;
    }
    topTab = eventTab;
    eventTab->SetTab(TAB_UNCOMPLETED_QUESTS_OR_EVENTS);
    completedEventList->Select(NULL);
    uncompletedEventList->Select(NULL);

    topTab->Show();
}

