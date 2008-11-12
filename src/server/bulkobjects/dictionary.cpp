/*
* dictionary.cpp
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
*
*/

#include <psconfig.h>
//=============================================================================
// Crystal Space Includes
//=============================================================================
#include <iutil/document.h>
#include <csutil/xmltiny.h>

//=============================================================================
// Project Includes
//=============================================================================
#include "net/messages.h"

#include "util/strutil.h"
#include "util/log.h"
#include "util/psdatabase.h"
#include "util/serverconsole.h"
#include "util/mathscript.h"
#include "util/psxmlparser.h"

#include "../playergroup.h"
#include "../client.h"
#include "../gem.h"
#include "../globals.h"
#include "../psserver.h"
#include "../cachemanager.h"
#include "../questmanager.h"
#include "../entitymanager.h"
#include "../progressionmanager.h"
#include "../cachemanager.h"
#include "../questionmanager.h"
#include "../npcmanager.h"
#include "../adminmanager.h"
#include "../netmanager.h"

#include "../iserver/idal.h"
extern "C" {
#include "../tools/wordnet/wn.h"
}
#include "rpgrules/factions.h"

//=============================================================================
// Local Includes
//=============================================================================
#include "dictionary.h"
#include "psnpcdialog.h"
#include "psquestprereqops.h"
#include "pscharacter.h"
#include "pscharacterloader.h"
#include "psitem.h"
#include "psitemstats.h"
#include "psguildinfo.h"
#include "adminmanager.h"

csRef<NPCDialogDict> dict;

NPCDialogDict::NPCDialogDict()
{
    dynamic_id = 1000000;
}

NPCDialogDict::~NPCDialogDict()
{
    wnclose();
    dict = NULL;
}

bool NPCDialogDict::Initialize(iDataConnection *db)
{
    // Initialise WordNet
    if (wninit() != 0)
    {
        Error1("*****************************\nWordNet failed to initialize.\n"
               "******************************\n");
    }

    if (LoadDisallowedWords(db))
    {
        if (LoadSynonyms(db))
        {
            if (LoadTriggerGroups(db))
            {
                if (LoadTriggers(db))
                {
                    if (LoadResponses(db))
                    {
                        return true;
                    }
                    else
                        Error1("*********************************\nFailed to load Responses\n****************************\n");
                }
                else
                    Error1("****************************\nFailed to load Triggers\n****************************\n");
            }
            else
                Error1("****************************\nFailed to load Trigger Groups\n************************\n");
        }
        else
            Error1("****************************\nFailed to load Synonyms\n****************************\n");
    }
    else
        Error1("****************************\nFailed to load Disallowed words\n****************************\n");
    return false;
}

bool NPCDialogDict::LoadDisallowedWords(iDataConnection *db)
{
    Result result(db->Select("select word from npc_disallowed_words"));

    if (!result.IsValid())
    {
        Error1("Cannot load disallowed words into dictionary from database.\n");
        Error1( db->GetLastError() );
        return false;
    }

    for (unsigned int i=0; i<result.Count(); i++)
    {
        csString *newword = new csString(result[i]["word"]);
        if (disallowed_words.Insert(newword,TREE_OWNS_DATA))
        {
            Error2("Found equal disallowed(%s) in disallowed_words\n",newword->GetData());
            delete newword;
        }

    }

    return true;
}

NpcTerm* NPCDialogDict::AddTerm(const char *term)
{
    NpcTerm * npc_term = FindTerm(term);
    if (npc_term) return npc_term;

    NpcTerm *newphrase = new NpcTerm(term);
    if (phrases.Insert(newphrase,TREE_OWNS_DATA))
    {
        delete newphrase;
        return NULL;
    }

    return newphrase;
}

bool NPCDialogDict::LoadSynonyms(iDataConnection *db)
{
    Result result(db->Select("select word,"
        "       synonym_of"
        "  from npc_synonyms"));

    if (!result.IsValid())
    {
        CPrintf(CON_ERROR, "Cannot load terms into dictionary from database.\n");
        CPrintf(CON_ERROR, db->GetLastError());
        return false;
    }

    for (unsigned int i=0; i<result.Count(); i++)
    {
        NpcTerm* term = AddTerm(result[i]["word"]);

        csString synonym_of(result[i]["synonym_of"]);

        if (synonym_of.Length())
        {
            term->synonym = AddTerm(synonym_of);
        }
    }

    return true;
}

void NPCDialogDict::AddWords(csString& trigger)
{
    bool found = false; // Flag to mark if a disallowed word is found
    int wordnum=1;
    csString word("temp");

    // if it's an exchange trigger just skip the following checks
    if (trigger.GetAt(0)=='<')
      return;

    while (word.Length())
    {
        size_t pos = 0;
        word = GetWordNumber(trigger,wordnum++,&pos);
        word.Downcase();
        if (word.Length()==0)
            continue;

        // Check this word in the trigger for being disallowed.  If so, skip it.
        csString *disallowed = disallowed_words.Find(&word);
        if (disallowed)
        {
            // Comment out warning here because disallowed words are actually skipped
            // to make them *allowed* for Settings people to use to make their writing
            // more natural while avoiding recording them in the actual triggers.
            //CPrintf(CON_DEBUG,"Skipping disallowed word '%s' in trigger '%s' at position %d.\n",
            //      word.GetData(), trigger.GetData(), pos);
            trigger.DeleteAt(pos, word.Length() );
            if (strstr(trigger.GetData(),"  "))
                trigger.DeleteAt(strstr(trigger.GetData(),"  ")-trigger.GetData(),1);
            trigger.RTrim();
            trigger.LTrim();
            wordnum--;  // Back up a word since we took one out.
            found = true; // Mark that we found a disallowed word.
            continue;
        }

        NpcTerm *found, key(word);

        found = phrases.Find(&key);
        if (found)
        {
            if (found->synonym )
            {
                CPrintf(CON_WARNING, "Warning: Word %s in trigger '%s' is already a synonym for '%s'.\n",
                    (const char *)found->term,
                    (const char *)trigger,
                    (const char *)found->synonym->term);
            }
        }
        else
        {
            // add word
            found = new NpcTerm(word);
            //            CPrintf(CON_DEBUG, "Adding %s.\n",(const char *)word);
            if (phrases.Insert(found,TREE_OWNS_DATA))
            {
                Error2("Found equal term(%s) in phrases\n",found->term.GetData());
                delete found;
                found = NULL;
            }
        }
    }
}

int NPCDialogDict::AddTriggerGroupEntry(int id,const char *txt, int equivID)
{
    static int NextID=9000000;

    NpcTriggerGroupEntry *parent=NULL;

    if (id==-1)
        id = NextID++;

    if (equivID)
    {
        parent = trigger_groups_by_id.Get(equivID,NULL);
        if (!parent)
        {
            Error4("Trigger group entry id %d (%s) specified bad parent id of %d.  Skipped.",id,txt,equivID );
            return -1;
        }
    }

    NpcTriggerGroupEntry *newtge = new NpcTriggerGroupEntry(id,txt,parent);

    if (trigger_groups.Insert(newtge,TREE_OWNS_DATA))
    {
        Error2("Found equal trigger(%s) in trigger groups\n",newtge->text.GetData());
        delete newtge;
        return -1;
    }
    trigger_groups_by_id.Put(newtge->id,newtge);

    AddWords(newtge->text); // Make sure these trigger words are in known word list.
    Debug2(LOG_STARTUP,0,"Loaded trigger group entry <%s>\n",newtge->text.GetData() );
    return id;
}

bool NPCDialogDict::LoadTriggerGroups(iDataConnection *db)
{
    Debug1(LOG_STARTUP,0,"Loading Trigger Groups...\n");

    // First add all the root entries so we find the parents later.
    Result result(db->Select("select id,"
                             "       trigger_text"
                             "  from npc_trigger_groups"
                             " where equivalent_to_id=0") );

    if (!result.IsValid())
    {
        CPrintf(CON_ERROR, "Cannot load trigger groups into dictionary from database.\n");
        CPrintf(CON_ERROR, db->GetLastError());
        return false;
    }

    for (unsigned int i=0; i<result.Count(); i++)
    {
        AddTriggerGroupEntry(result[i].GetInt("id"),result[i]["trigger_text"],0);
    }

    // Now add all the child entries and attach to parents.
    Result result2(db->Select("select id,"
                              "       trigger_text,"
                              "       equivalent_to_id"
                              "  from npc_trigger_groups"
                              " where equivalent_to_id<>0") );

    if (!result2.IsValid())
    {
        CPrintf(CON_ERROR, "Cannot load trigger groups into dictionary from database.\n");
        CPrintf(CON_ERROR, db->GetLastError());
        return false;
    }

    for (unsigned int i=0; i<result2.Count(); i++)
    {
        AddTriggerGroupEntry(result2[i].GetInt("id"),result2[i]["trigger_text"],result2[i].GetInt("equivalent_to_id"));
    }
    return true;
}

bool NPCDialogDict::LoadTriggers(iDataConnection *db)
{
    Debug1(LOG_STARTUP,0,"Loading Triggers...\n");

    Result result(db->Select("select * from npc_triggers") );

    if (!result.IsValid())
    {
        CPrintf(CON_ERROR, "Cannot load triggers into dictionary from database.\n");
        CPrintf(CON_ERROR, db->GetLastError());
        return false;
    }

    for (unsigned int i=0; i<result.Count(); i++)
    {
        NpcTrigger *newtrig = new NpcTrigger;

        if (!newtrig->Load(result[i]))
        {
            Error2("Could not load trigger %s!\n",result[i]["id"]);
            delete newtrig;
            continue;
        }

        ParseMultiTrigger(newtrig);

        if(newtrig->trigger.Length() == 0)
        {
            Error3("Found bad trigger %d of trigger length 0 in triggers, area %s\n", newtrig->id, newtrig->area.GetDataSafe());
            delete newtrig;
            continue;
        }

        AddWords(newtrig->trigger); // Make sure these trigger words are in known word list.

        if (triggers.Insert(newtrig,TREE_OWNS_DATA))
        {
            Error4("Found equal trigger %d (%s) in triggers, area %s\n", newtrig->id, newtrig->trigger.GetDataSafe(), newtrig->area.GetDataSafe());
            delete newtrig;
            continue;
        }

    }
    return true;
}

bool NPCDialogDict::LoadResponses(iDataConnection *db)
{
    Result result(db->Select("SELECT * FROM npc_responses"));

    if (!result.IsValid())
    {
        CPrintf(CON_ERROR, "Cannot load responses into dictionary from database.\n");
        CPrintf(CON_ERROR, db->GetLastError());
        return false;
    }

    for (unsigned int i=0; i<result.Count(); i++)
    {
        NpcResponse *newresp = new NpcResponse;

        if (!newresp->Load(result[i]))
        {
            delete newresp;
            return false;
        }

        int trigger_id = result[i].GetInt("trigger_id");
        if (trigger_id == 0)
        {
            Error1("Response with null trigger.\n");
            delete newresp;
            return false;
        }
        if (!AddTrigger(db,trigger_id,newresp->id))
        {
            Error2("Failed to load trigger for resp: %d\n",newresp->id);
            return false;
        }



        if (responses.Insert(newresp,TREE_OWNS_DATA))
        {
            Error2("Found equal response(%s) in responses\n",newresp->response[0].GetData());
            delete newresp;
            return false;
        }
    }
    return true;
}


NpcTerm * NPCDialogDict::FindTerm(const char *term)
{
    NpcTerm key(term);
    key.term.Downcase();
    return phrases.Find(&key);
}

NpcTerm * NPCDialogDict::FindTermOrSynonym(const csString & term)
{
    NpcTerm * termRec = FindTerm(term);

    if (termRec && termRec->synonym)
        return termRec->synonym;
    else
        return termRec;
}

NpcResponse *NPCDialogDict::FindResponse(gemNPC * npc,
                                         const char *area,
                                         const char *trigger,
                                         int faction,
                                         int priorresponse,
                                         Client *client)
{
    Debug7(LOG_NPC, client->GetClientNum(),"Entering NPCDialogDict::FindResponse(%s,%s,%s,%d,%d,%s)",
            npc->GetName(),area,trigger,faction,priorresponse,client->GetName());
    NpcTrigger *trig;
    NpcTrigger key;

    key.area            = area;
    key.trigger         = trigger;
    key.priorresponseID = priorresponse;

    trig = triggers.Find(&key);

    if (trig)
    { 
        Debug3(LOG_NPC, client->GetClientNum(),"NPCDialogDict::FindResponse consider trig(%d): '%s'",
                trig->id,trig->trigger.GetDataSafe());
    }
    else
    {
        Debug1(LOG_NPC, client->GetClientNum(),"NPCDialogDict::FindResponse no trigger found");
        return NULL;
    }


    csArray<int> availableResponseList;

    // Check if not all responses is blocked(Not available in quests, Prequests not fullfitted,...)
    if (trig && !trig->HaveAvailableResponses(client,npc,this,&availableResponseList))
    {
        Debug1(LOG_NPC, client->GetClientNum(),"NPCDialogDict::FindResponse no available responses found");
        return NULL;
    }

    NpcResponse *resp;

    resp = FindResponse(trig->GetRandomResponse(availableResponseList)); // find one of the specified responses

    return resp;
}

NpcResponse *NPCDialogDict::FindResponse(int responseID)
{
    NpcResponse key;
    key.id = responseID;
    return responses.Find(&key);
}


bool NPCDialogDict::CheckForTriggerGroup(csString& trigger)
{
    NpcTriggerGroupEntry key(0,trigger);
    NpcTriggerGroupEntry *found = trigger_groups.Find(&key);

    if (found && found->parent)
    {
        // Trigger is child, so substitute parent
        trigger = found->parent->text;
        return true;
    }
    return false;
}

void NPCDialogDict::ParseMultiTrigger(NpcTrigger *parsetrig)
{
    psString trig(parsetrig->trigger);
    csStringArray list;

    trig.Split(list,'.');
    if (list.GetSize() <= 1)
        return;

    parsetrig->trigger = list.Get(0);
    int id = AddTriggerGroupEntry(-1,list.Get(0),0);
    if (id < 0)
    {
        Error3("Error in MultiTrigger %d (%s)",parsetrig->id, parsetrig->trigger.GetDataSafe() );
        return;
    }
    for (size_t i=1; i<list.GetSize(); i++)
    {
        AddTriggerGroupEntry(-1,list.Get(i),id);
    }
}


bool NPCDialogDict::AddTrigger( iDataConnection* db, int triggerID , int responseID )
{
    Result result(db->Select("SELECT * from npc_triggers WHERE id=%d", triggerID ));

    if (!result.IsValid() || result.Count()!=1)
    {
        Error2("Invalid trigger id %d in npc_triggers table.\n",triggerID);
        return false;
    }

    NpcTrigger* newtrig = new NpcTrigger;

    if (!newtrig->Load(result[0]))
    {
        delete newtrig;
        return false;
    }

    ParseMultiTrigger(newtrig);

    CS_ASSERT(responseID != -1);
    newtrig->responseIDlist.Push(responseID);

    NpcTrigger* trig;

    AddWords( newtrig->trigger );

    if ( (trig = triggers.Insert( newtrig, TREE_OWNS_DATA )) )
    {
        // There are already a trigger with this combination of
        // triggertext, KA, and prior respose so pushing the trigger
        // response on the same trigger.
        CS_ASSERT(responseID != -1);
        trig->responseIDlist.Push(responseID);

        delete newtrig;
    }

    return true;
}


void NPCDialogDict::AddResponse( iDataConnection* db, int databaseID )
{
    Result result(db->Select("SELECT * from npc_responses WHERE id=%d",databaseID ) );

    if (!result.IsValid() || result.Count() != 1)
    {
        Error2("Invalid response id %d specified for npc_responses table.\n",databaseID);
        return;
    }

    NpcResponse *newresp = new NpcResponse;

    if (!newresp->Load(result[0]))
    {
        delete newresp;
        return;

    }
    if (responses.Insert(newresp,TREE_OWNS_DATA))
    {
        Error2("Found equal response(%s) in responses\n",newresp->response[0].GetData());
        delete newresp;
    }
}

NpcResponse *NPCDialogDict::AddResponse(const char *response_text,
                                        const char *pronoun_him,
                                        const char *pronoun_her,
                                        const char *pronoun_it,
                                        const char *pronoun_them,
                                        const char *npc_name,
                                        int &new_id,
                                        psQuest * quest,
										const char *audio_path)
{
    NpcResponse *newresp = new NpcResponse;

    if (!new_id)
        new_id = dynamic_id++;

    newresp->id = new_id;

    csString opStr;

    if (strchr(response_text,'['))
    {
        size_t start=0,end=0;
        opStr = "<response>";
        csString resp(response_text);

        while (end != resp.Length() )
        {
            if (resp.GetAt(start) == ']')
                start++;
            end = (int)resp.Find("[", start);  // action delimiter
            if (end == SIZET_NOT_FOUND)
                end = resp.Length();

            if (end-start > 0)
            {
                csString saySegment;
                resp.SubString(saySegment,start,end-start); // pull out the part before the [ ]
                opStr.AppendFmt("<say text=\"%s\"/>", saySegment.GetDataSafe() );
            }
            if (end == resp.Length())
                break;

            // Now get the part in brackets
            start = end;
            end = resp.Find("]", start); // find end of action
            if (end == SIZET_NOT_FOUND)
            {
                Error2("Unmatched open '[' in npc response %s.", response_text);
                delete newresp;
                return NULL;
            }

            if (end - start > 1)
            {
                csString actionSegment;
                resp.SubString(actionSegment,start+1,end-start-1);
                 // If action does not start with npc's name, it is a 3rd person statement, not /me
                if (strncasecmp(actionSegment,npc_name,strlen(npc_name)))
                {
                    opStr.AppendFmt("<narrate text=\"%s\"/>", actionSegment.GetDataSafe() );
                }
                else // now look for /me or /my because the npc name matches
                {
                    size_t spc = actionSegment.FindFirst(" ");
                    if (resp[strlen(npc_name)] == '\'') // apostrophe after name means /my
                    {
                        actionSegment.DeleteAt(0,spc+1);
                        opStr.AppendFmt("<actionmy text=\"%s\"/>", actionSegment.GetDataSafe() );
                    }
                    else // this is a /me command
                    {
                        actionSegment.DeleteAt(0,spc+1);
                        opStr.AppendFmt("<action text=\"%s\"/>", actionSegment.GetDataSafe() );
                    }
                }
            }
            start = end;
        }
    }
    else // simple case doesn't have /me actions in brackets [ ]
    {
        newresp->response[0] = response_text;
    }
    newresp->him  = pronoun_him;
    newresp->her  = pronoun_her;
    newresp->it   = pronoun_it;
    newresp->them = pronoun_them;
	newresp->voiceAudioPath = audio_path;

    newresp->type = NpcResponse::VALID_RESPONSE;

    newresp->quest = quest;
    if (quest && quest->GetPrerequisite())
        newresp->prerequisite = quest->GetPrerequisite()->Copy();
    else
        newresp->prerequisite = NULL;

    if (opStr.Length() > 8)
    {
        // printf("Got resulting opStr of: %s\n", opStr.GetDataSafe() );
        opStr.Append("</response>");
    }
    else
        opStr.Clear();
    newresp->ParseResponseScript(opStr.GetDataSafe() );

    if (responses.Insert(newresp,TREE_OWNS_DATA))
    {
        Error2("Found equal response(%s) in responses\n",newresp->response[0].GetData());
        delete newresp;
        return NULL;
    }


    return newresp;  // Make response available for script additions
}

void NPCDialogDict::DeleteTriggerResponse(NpcTrigger * trigger, int responseId)
{
    NpcResponse dummy;
    dummy.id = responseId;

    responses.Delete(&dummy);

    if(trigger)
    {
        trigger->responseIDlist.Delete(responseId);
        if(trigger->responseIDlist.GetSize() == 0)
            triggers.Delete(trigger);
    }
}

NpcTrigger *NPCDialogDict::AddTrigger(const char *k_area,const char *mytrigger,int prior_response, int trigger_response)
{
    NpcTrigger *trig;
    NpcTrigger key;
    // Both 0 and -1 can be used for no precodition, make sure we use -1
    if (prior_response == 0) prior_response = -1;

    // If the trigger to be added is already present, then the trigger response
    // is just added as an alternative to the existing responses specified for
    // this trigger.  These are chosen from randomly when triggering later.
    csString temp_k_area(k_area);
    key.area            = temp_k_area.Downcase();
    key.trigger         = mytrigger;
    key.priorresponseID = prior_response;

    trig = triggers.Find(&key);

    if (trig)
    {
        Debug2(LOG_QUESTS,0,"Found existing trigger so adding response %d as an alternative response to it.\n",trigger_response);
        CS_ASSERT(trigger_response != -1);
        trig->responseIDlist.Push(trigger_response);
        return trig;
    }
    else
    {
        NpcTrigger *newtrig = new NpcTrigger;

        newtrig->id              = 0;
        newtrig->area            = temp_k_area.Downcase();
        newtrig->trigger         = mytrigger;
        newtrig->priorresponseID = prior_response;
        CS_ASSERT(trigger_response != -1);
        newtrig->responseIDlist.Push(trigger_response);

        AddWords( newtrig->trigger );

        NpcTrigger* oldtrig;
        if ( (oldtrig = triggers.Insert( newtrig, TREE_OWNS_DATA )) )
        {
            // There are already a trigger with this combination of
            // triggertext, KA, and prior respose so pushing the trigger
            // response on the same trigger.
            int respID = newtrig->responseIDlist.Top();
            CS_ASSERT(respID != -1);
            oldtrig->responseIDlist.Push(respID);

            delete newtrig;
            return oldtrig;
        }


        return newtrig;
    }
}

void PrintTrigger(NpcTrigger * trig)
{
    CPrintf(CON_CMDOUTPUT ,"Trigger [%d] %s : \"%-60.60s\" %8d /",
            trig->id,trig->area.GetData(),trig->trigger.GetDataSafe(),trig->priorresponseID);
}

void PrintResponse(NpcResponse * resp)
{
    csString script = resp->GetResponseScript();
    CPrintf(CON_CMDOUTPUT ,"Response [%8d] Script : %s\n",resp->id,script.GetData());
    if (resp->quest)
    {
        CPrintf(CON_CMDOUTPUT ,"                    Quest  : %s\n",resp->quest->GetName());
    }
    if (resp->prerequisite)
    {
        csString prereq = resp->prerequisite->GetScript();
        if (!prereq.IsEmpty())
        {
            CPrintf(CON_CMDOUTPUT ,"                    Prereq : %s\n",prereq.GetDataSafe());
        }
    }
    if (resp->him.Length() || resp->her.Length() || resp->it.Length() || resp->them.Length())
    {
        CPrintf(CON_CMDOUTPUT,"                    Pronoun: him='%s' her='%s' it='%s' them='%s'\n",
                resp->him.GetDataSafe(),resp->her.GetDataSafe(),
                resp->it.GetDataSafe(),resp->them.GetDataSafe());
    }
    for (int n = 0; n < MAX_RESP; n++)
    {
        if (resp->response[n].Length())
        {
            CPrintf(CON_CMDOUTPUT ,"%21d) %s\n",n+1,resp->response[n].GetData());
        }
    }
}

void NpcTerm::Print()
{
    CPrintf(CON_CMDOUTPUT ,"%-30s",term.GetDataSafe());
    if (synonym)
    {
        CPrintf(CON_CMDOUTPUT ," %-30s :",synonym->term.GetDataSafe());
    }
    else
    {
        CPrintf(CON_CMDOUTPUT ," %-30s :","");
    }

    if(hypernymSynNet == NULL)
        BuildHypernymList();

    if (hypernyms.GetSize())
    {
        for (size_t i=0; i < hypernyms.GetSize(); i++)
        {
            if (i!=0)
            {
                CPrintf(CON_CMDOUTPUT ,", ");
            }
            CPrintf(CON_CMDOUTPUT ,"%s",hypernyms[i]);
        }
    }

    CPrintf(CON_CMDOUTPUT ,"\n");
}



void NPCDialogDict::Print(const char *area)
{
    CPrintf(CON_CMDOUTPUT ,"\n");
    CPrintf(CON_CMDOUTPUT ,"NPC Dictionary\n");
    CPrintf(CON_CMDOUTPUT ,"\n");

    if (area!=NULL && strlen(area))
    {
        CPrintf(CON_CMDOUTPUT ,"----------- Triggers/Responses of area %s----------\n",area);
        BinaryRBIterator<NpcTrigger> trig_iter(&triggers);
        NpcTrigger * trig;
        for (trig = trig_iter.First(); trig; trig = ++trig_iter)
        {
            // filter on given area
            if (area!=NULL && strcasecmp(trig->area.GetDataSafe(),area)!=0)
                continue;

            PrintTrigger(trig);
            CPrintf(CON_CMDOUTPUT ,"\n");

            for (size_t i = 0; i < trig->responseIDlist.GetSize(); i++)
            {
                NpcResponse * resp = dict->FindResponse(trig->responseIDlist[i]);
                if (resp)
                {
                    PrintResponse(resp);
                }
                else
                    CPrintf(CON_CMDOUTPUT ,"Response [%d]: Error. Response not found!!!\n",trig->responseIDlist[i]);
            }
            CPrintf(CON_CMDOUTPUT ,"\n");
        }
        return;
    }

    CPrintf(CON_CMDOUTPUT ,"----------- All Triggers ----------\n");
    BinaryRBIterator<NpcTrigger> trig_iter(&triggers);
    NpcTrigger * trig;
    for (trig = trig_iter.First(); trig; trig = ++trig_iter)
    {
        PrintTrigger(trig);
        for (size_t i = 0; i < trig->responseIDlist.GetSize(); i++)
        {
            CPrintf(CON_CMDOUTPUT ," %d",trig->responseIDlist[i]);
        }
        CPrintf(CON_CMDOUTPUT ,"\n");
    }

    CPrintf(CON_CMDOUTPUT ,"----------- All Responses ---------\n");
    BinaryRBIterator<NpcResponse> resp_iter(&responses);
    NpcResponse * resp;
    for (resp = resp_iter.First(); resp; resp = ++resp_iter)
    {
        PrintResponse(resp);
    }

    CPrintf(CON_CMDOUTPUT ,"\n");

    CPrintf(CON_CMDOUTPUT ,"----------- All Terms ---------\n");
    BinaryRBIterator<NpcTerm> term_iter(&phrases);
    NpcTerm * term;
    for (term = term_iter.First(); term; term = ++term_iter)
    {
        term->Print();
    }

    CPrintf(CON_CMDOUTPUT ,"\n");
}

bool NpcTerm::IsNoun()
{
    char* baseform = morphstr(const_cast<char *>(term.GetData()), NOUN);

    if(!baseform)
        baseform = const_cast<char *>(term.GetData());

    return in_wn(baseform, NOUN) != 0;
}

const char* NpcTerm::GetInterleavedHypernym(size_t which)
{
    if(hypernymSynNet == NULL)
        BuildHypernymList();

    if(which < hypernyms.GetSize())
        return hypernyms.Get(which);
    else
        return NULL;
}

void NpcTerm::BuildHypernymList()
{
    hypernymSynNet = findtheinfo_ds(const_cast<char *>(term.GetData()), NOUN, -HYPERPTR, ALLSENSES);

    // Hypernym 0 is the original word
    hypernyms.Put(0, term.GetData());

    bool hit;

    SynsetPtr sense[50];
    SynsetPtr current = sense[0] = hypernymSynNet;

    if(current == NULL)
        return;

    // We interleave hypernyms through the senses
    size_t sensecount = 0;
    while(current->nextss)
    {
        sensecount++;
        current = current->nextss;
        sense[sensecount] = current;
    }

    // Perform breadth-first search of hypernyms/word senses
    do
    {
        // Each iteration goes one level deeper
        hit = false;
        for(size_t j = 0; j <= sensecount; j++)
        {
            if(sense[j] == NULL)
                continue;

            sense[j] = sense[j]->ptrlist;

            // We have found a hypernym
            if(sense[j])
            {
                hit = true;
                // Check if we have this word already before we add it.
                bool found = false;
                for (size_t i = 0; i < hypernyms.GetSize() && !found; i++)
                {
                    if (strcmp(*sense[j]->words,hypernyms[i])==0)
                    {
                        found = true;
                    }
                }
                if (!found)
                {
                    hypernyms.Push(*sense[j]->words);
                }

                // Check if we have new senses.
                current = sense[j];
                while(current->nextss)
                {
                    sensecount++;
                    current = current->nextss;
                    sense[sensecount] = current;
                }

            }
        }
    } while (hit);
}

bool NpcTrigger::Load(iResultRow& row)
{
    id              = row.GetInt("id");
    csString temp_area(row["area"]);
    area            = temp_area.Downcase();
    trigger         = row["trigger_text"];
    priorresponseID = row.GetInt("prior_response_required");
    // Both 0 and -1 can be used for no precodition, make sure we use -1
    if (priorresponseID == 0) priorresponseID = -1;

    return true;
}

bool NpcTrigger::HaveAvailableResponses(Client * client, gemNPC * npc, NPCDialogDict * dict, csArray<int> *availableResponseList)
{
    bool haveAvail = false;

    for (size_t n = 0; n < responseIDlist.GetSize(); n++)
    {
        NpcResponse * resp = dict->FindResponse(responseIDlist[n]);
        if (resp)
        {
            if (resp->quest || resp->prerequisite)
            {
                // Check if all prerequisites are true, and available(no lockout)
                if ((!resp->prerequisite || client->GetCharacterData()->CheckResponsePrerequisite(resp)) &&
                    (!resp->quest || (resp->quest->Active() && client->GetCharacterData()->CheckQuestAvailable(resp->quest,npc->GetPID()))))
                {
                    Debug2(LOG_QUESTS,client->GetClientNum(),"Pushing quest response: %d\n",resp->id);
                    // This is a available response that is connected to a available quest
                    haveAvail = true;
                    if (availableResponseList)
                        availableResponseList->Push(resp->id);
                }
            }
            else
            {
                Debug2(LOG_QUESTS,client->GetClientNum(),"Pushing non quest response: %d\n",resp->id);
                // This is a available responses that isn't connected to a quest
                haveAvail = true;
                if (availableResponseList) availableResponseList->Push(resp->id);
            }
        }
    }

    return haveAvail;
}

int NpcTrigger::GetRandomResponse( const csArray<int> &availableResponseList )
{
    if (availableResponseList.GetSize() > 1)
        return availableResponseList[ psserver->rng->Get( (uint32) availableResponseList.GetSize() ) ];
    else
        return availableResponseList[0];
}

bool NpcTrigger::operator==(NpcTrigger& other) const
{
    return (area==other.area &&
            trigger==other.trigger &&
            priorresponseID==other.priorresponseID);
};

bool NpcTrigger::operator<(NpcTrigger& other) const
{
    if (strcmp(area,other.area)<0)
        return true;
    if (strcmp(area,other.area)>0)
        return false;

    if (strcmp(trigger,other.trigger)<0)
        return true;
    if (strcmp(trigger,other.trigger)>0)
        return false;

    if (priorresponseID<other.priorresponseID)
        return true;

    return false;
};


NpcResponse::NpcResponse()
{
    quest = NULL;
	menu = NULL;
    active_quest = -1;
}

bool NpcResponse::Load(iResultRow& row)
{
    id             = row.GetInt("id");

    csString respName = "response ";
    for (int i=0; i < MAX_RESP; i++)
    {
        respName[respName.Length()-1] = '1'+i;
        response[i] = row[respName];
    }

    him  = row["pronoun_him"];
    her  = row["pronoun_her"];
    it   = row["pronoun_it"];
    them = row["pronoun_them"];


	voiceAudioPath = row["audio_path"];
	if (voiceAudioPath.Length() > 0)
		printf("Got audio file '%s' on response %d.\n", voiceAudioPath.GetDataSafe(), id);


    type = NpcResponse::VALID_RESPONSE;

    // if a quest_id is specified in this response,
    // auto-generate a script op to make sure this
    // quest is active for the player before responding
    int quest_id = row.GetInt("quest_id");
    if (quest_id)
    {
        csString scripttext(row["script"]);
        if (scripttext.Find("<assign") == SIZET_NOT_FOUND)  // can't verify assigned if this step is what assigns
        {
            VerifyQuestAssignedResponseOp *op = new VerifyQuestAssignedResponseOp(quest_id);
            script.Push(op);
        }
    }

    // Parse prerequisite, we reuse the quest prerequiste as preprequisite for triggers as well
    csString prereq = row["prerequisite"];
    if (!ParsePrerequisiteScript(prereq,true))
    {
        Error3("Failed to decode response %d prerequisite: '%s'",id,prereq.GetDataSafe());

        return false;
    }

    return ParseResponseScript(row["script"]);
}

void NpcResponse::SetActiveQuest(int max)
{
    active_quest = psserver->rng->Get(max);
}

const char *NpcResponse::GetResponse()
{
    if (active_quest == -1)
    {
        int i=100;
        while (i--)
        {
            int which = psserver->rng->Get(MAX_RESP);

            if (i < MAX_RESP) which = i; // Loop through on the last 5 attempts
                                         // just to be sure we find one.

            if (response[which].Length())
                return response[which];
        }
        return "5 blank responses!";
    }
    else
    {
        return response[active_quest];
    }
}

bool NpcResponse::ParseResponseScript(const char *xmlstr,bool insertBeginning)
{
    if (!xmlstr || strcmp(xmlstr,"")==0)
    {
        SayResponseOp *op = new SayResponseOp(false);
        if (insertBeginning)
            script.Insert(0,op);
        else
            script.Push(op);
        return true;
    }

    int where=0;
    csRef<iDocumentSystem> xml = csPtr<iDocumentSystem>(new csTinyDocumentSystem);
    csRef<iDocument> doc = xml->CreateDocument();
    const char* error = doc->Parse( xmlstr );
    if ( error )
    {
        Error3("Error: %s . In XML: %s", error, xmlstr );
        return false;
    }
    csRef<iDocumentNode> root    = doc->GetRoot();
    if(!root)
    {
        Error2("No xml root in %s", xmlstr);
        return false;
    }

    csRef<iDocumentNode> topNode = root->GetNode("response");
    if(!topNode)
    {
        CPrintf(CON_WARNING,"The npc_response ID %d doesn't have a valid script: %s\n",id,xmlstr);
        return true; // Return true to add the response, but without a vaild script
    }

    csRef<iDocumentNodeIterator> iter = topNode->GetNodes();

    while ( iter->HasNext() )
    {
        csRef<iDocumentNode> node = iter->Next();

        if ( node->GetType() != CS_NODE_ELEMENT )
            continue;

        // Some Responses need post load functions.
        bool postLoadAssignQuest = false;


        ResponseOperation * op = NULL;

        if ( strcmp( node->GetValue(), "respond" ) == 0 ||
             strcmp( node->GetValue(), "respondpublic" ) == 0 ||
             strcmp( node->GetValue(), "say") == 0)
        {
            op = new SayResponseOp(strcmp( node->GetValue(), "respondpublic" ) == 0);  // true for public, false for private
        }
        else if ( strcmp( node->GetValue(), "action" ) == 0 ||
                  strcmp( node->GetValue(), "actionmy" ) == 0 ||
                  strcmp( node->GetValue(), "narrate") == 0)
        {
            op = new ActionResponseOp(strcmp( node->GetValue(), "actionmy" ) == 0, // true for actionmy, false for action
                                      strcmp( node->GetValue(), "narrate" )  == 0);
        }
        else if ( strcmp( node->GetValue(), "npccmd" ) == 0 )
        {
            op = new NPCCmdResponseOp;
        }
        else if ( strcmp( node->GetValue(), "verifyquestcompleted") == 0)
        {
            op = new VerifyQuestCompletedResponseOp;
        }
        else if ( strcmp( node->GetValue(), "verifyquestassigned") == 0)
        {
            op = new VerifyQuestAssignedResponseOp;
        }
        else if ( strcmp( node->GetValue(), "verifyquestnotassigned") == 0)
        {
            op = new VerifyQuestNotAssignedResponseOp;
        }
        else if ( strcmp( node->GetValue(), "assign" ) == 0 )
        {
            op = new AssignQuestResponseOp;
            postLoadAssignQuest = true;
        }
        else if ( strcmp( node->GetValue(), "complete" ) == 0 )
        {
            op = new CompleteQuestResponseOp;
        }
        else if ( strcmp( node->GetValue(), "give" ) == 0 )
        {
            op = new GiveItemResponseOp;
        }
        else if ( strcmp( node->GetValue(), "faction" ) == 0 )
        {
            op = new FactionResponseOp;
        }
        else if ( strcmp( node->GetValue(), "run" ) == 0 )
        {
            op = new RunScriptResponseOp;
        }
        else if ( strcmp( node->GetValue(), "train" ) == 0 )
        {
            op = new TrainResponseOp;
        }
        else if ( strcmp( node->GetValue(), "guild_award" ) == 0 )
        {
            op = new GuildAwardResponseOp;
        }
        else if ( strcmp( node->GetValue(), "offer" ) == 0 )
        {
            op = new OfferRewardResponseOp;
        }
        else if ( strcmp( node->GetValue(), "money" ) == 0 )
        {
            op = new MoneyResponseOp;
        }
        else if ( strcmp( node->GetValue(), "introduce" ) == 0 )
        {
            op = new IntroduceResponseOp;
        }
        else if ( strcmp( node->GetValue(), "doadmincmd" ) == 0 )
        {
            op = new DoAdminCommandResponseOp;
        }
        else if (strcmp(node->GetValue(), "fire_event") == 0)
        {
            op = new FireEventResponseOp;
        }
        else
        {
            Error2("undefined operation specified in response script %d.",id);
            return false;
        }

        if (!op->Load(node))
        {
            Error3("Could not load <%s> operation in script %d. Error in XML",op->GetName(),id);
            delete op;
            return false;
        }
        if (insertBeginning)
            script.Insert(where++,op);
        else
            script.Push(op);

        // Execute any outstanding post load operations.
        if (postLoadAssignQuest)
        {
            AssignQuestResponseOp *aqr_op = dynamic_cast<AssignQuestResponseOp*>(op);
            if (aqr_op->GetTimeoutMsg())
            {
                CheckQuestTimeoutOp *op2 = new CheckQuestTimeoutOp(aqr_op);
                script.Insert(0,op2);
                where++;
            }
            AssignQuestSelectOp *op3 = new AssignQuestSelectOp(aqr_op);
            script.Insert(0,op3);
            where++;
        }
    }
    return true;
}

bool NpcResponse::HasPublicResponse()
{
    for (size_t i=0; i<script.GetSize(); i++)
    {
        SayResponseOp *op = dynamic_cast<SayResponseOp*>(script[i]);
        if (op && op->saypublic)
            return true;
    }
    return false;
}

bool NpcResponse::ExecuteScript(Client *client, gemNPC* target)
{
    timeDelay = 0; // Say commands, etc. should be delayed by 2-3 seconds to simulate typing

    active_quest = -1;  // not used by default
    for (size_t i=0; i<script.GetSize(); i++)
    {
        if (!script[i]->Run(target,client,this,timeDelay))
        {
            csString resp = script[i]->GetResponseScript();
            Error3("Error running script in %s operation for client %s.",
                   resp.GetData(),client->GetName() );
            return false;
        }
    }
    return true;
}

csString NpcResponse::GetResponseScript()
{
    csString respScript = "<response>";
    for (size_t i=0; i<script.GetSize(); i++)
    {
        csString op;
        op.Format("<%s/>",script[i]->GetResponseScript().GetData());
        respScript.Append(op);
    }
    respScript.Append("</response>");
    return respScript;
}

bool NpcResponse::ParsePrerequisiteScript(const char *xmlstr,bool insertBeginning)
{
    if (!xmlstr || strcmp(xmlstr,"")==0 || strcmp(xmlstr,"0")==0)
    {
        return true;
    }

    csRef<psQuestPrereqOp> op;
    if (!LoadPrerequisiteXML(op,NULL,xmlstr))
    {
        return false;
    }

    if (op)
    {
        return AddPrerequisite(op,insertBeginning);
    }

    return false;
}

bool NpcResponse::AddPrerequisite(csRef<psQuestPrereqOp> op, bool insertBeginning)
{
    // Make sure that the first op is an AND list if there are an
    // prerequisite from before.
    if (prerequisite)
    {
        // Check if first op is an and list.
        psQuestPrereqOp* cast = prerequisite;
        csRef<psQuestPrereqOpAnd> list = dynamic_cast<psQuestPrereqOpAnd*>(cast);
        if (list == NULL)
        {
            // If not insert an and list.
            list.AttachNew(new psQuestPrereqOpAnd());
            list->Push(prerequisite);
            prerequisite = list;
        }

        if (insertBeginning)
            list->Insert(0,op);
        else
            list->Push(op);
    }
    else
    {
        // No prerequisite from before so just set this.
        prerequisite = op;
    }

    return true;
}


bool NpcResponse::CheckPrerequisite(psCharacter * character)
{
    if (prerequisite)
        return prerequisite->Check(character);

    return true; // No prerequisite so its ok to do this quest
}


////////////////////////////////
/////////// Training ///////////
////////////////////////////////

/** Checks if training is possible (enough money to pay etc.)
  * If not, tells the user why */
bool CheckTraining(gemNPC *who, Client *target, psSkillInfo* skill)
{
    csTicks timeDelay=0;

    if (!who->GetCharacterData()->IsTrainer())
    {
        CPrintf(CON_ERROR, "%s isn't a trainer, but have train in dialog!!",who->GetCharacterData()->GetCharName());
        return false;
    }

    psCharacter * character = target->GetCharacterData();

    CPrintf(CON_DEBUG, "    PP available: %u\n", character->GetProgressionPoints() );

    // Test for progression points
    if (character->GetProgressionPoints() <= 0)
    {
        csString str;
        csString downcase = skill->name.Downcase();
        str.Format("You don't have any progression points to be trained in %s, Sorry",downcase.GetData());
        who->Say(str,target,false,timeDelay);
        return false;
    }

    // Test for money
    if (skill->price > character->Money())
    {
        csString str;
        csString downcase = skill->name.Downcase();
        str.Format("Sorry, but I see that you don't have enough money to be trained in %s",downcase.GetData());
        who->Say(str,target,false,timeDelay);
        return false;
    }

    if ( !character->CanTrain( skill->id ) )
    {
        csString str;
        csString downcase = skill->name.Downcase();
        str.Format("You can't train %s higher yet",downcase.GetData());
        who->Say(str,target,false,timeDelay);
        return false;
    }
    return true;
}

/** This class asks user to confirm that he really wants to pay for training */
class TrainingConfirm : public PendingQuestion
{
public:
    TrainingConfirm(const csString & question,
                    gemNPC *who, Client *target, psSkillInfo* skill)
        : PendingQuestion(target->GetClientNum(), question,
                          psQuestionMessage::generalConfirm)
    {
        this->who     =   who;
        this->target  =   target;
        this->skill   =   skill;
    }

    virtual void HandleAnswer(const csString & answer)
    {
        if (answer != "yes")
            return;

        // We better check again, if everything is still ok
        if (!CheckTraining(who, target, skill))
            return;

        psCharacter * character = target->GetCharacterData();

        character->UseProgressionPoints(1);
        character->SetMoney(character->Money()-skill->price);
        character->Train(skill->id,1);

    csString downcase = skill->name.Downcase();
        psserver->SendSystemInfo(target->GetClientNum(), "You've received some %s training", downcase.GetData());
    }

protected:
    gemNPC *who;
    Client *target;
    psSkillInfo* skill;
};

////////////////////////////////////////////////////////////////////
// Responses

bool SayResponseOp::Load(iDocumentNode *node)
{
    sayWhat = NULL;
    if (node->GetAttributeValue("text"))
    {
        sayWhat = new csString(node->GetAttributeValue("text"));
    }

    return true;
}

csString SayResponseOp::GetResponseScript()
{
    psString resp;
    resp = GetName();
    return resp;
}

bool SayResponseOp::Run(gemNPC *who, Client *target,NpcResponse *owner,csTicks& timeDelay)
{
    psString response;

    if (sayWhat)
        response = *sayWhat;
    else
        response = owner->GetResponse();

    who->GetNPCDialogPtr()->SubstituteKeywords(target,response);

    if (target->GetSecurityLevel() >= GM_DEVELOPER)
        response.AppendFmt(" (%s)",owner->triggerText.GetDataSafe() );

    who->Say(response,target,saypublic,timeDelay);

    return true;
}

bool ActionResponseOp::Load(iDocumentNode *node)
{
    anim = node->GetAttributeValue("anim");
    if (node->GetAttributeValue("text"))
        actWhat = new csString(node->GetAttributeValue("text"));
    else
        actWhat = NULL;
    return true;
}

csString ActionResponseOp::GetResponseScript()
{
    psString resp;
    resp = GetName();
    resp.AppendFmt(" anim=\"%s\"",anim.GetData());
    return resp;
}

bool ActionResponseOp::Run(gemNPC *who, Client *target,NpcResponse *owner,csTicks& timeDelay)
{
    if (!anim.IsEmpty())
        who->SetAction(anim,timeDelay);

    if (actWhat)
    {
        csString response(*actWhat);
        who->GetNPCDialogPtr()->SubstituteKeywords(target,response);
        who->ActionCommand(actionMy, actionNarrate, response.GetDataSafe(), target->GetClientNum(),timeDelay );
    }

    return true;
}

bool NPCCmdResponseOp::Load(iDocumentNode *node)
{
    cmd = node->GetAttributeValue("cmd");
    return true;
}

csString NPCCmdResponseOp::GetResponseScript()
{
    psString resp;
    resp = GetName();
    resp.AppendFmt(" cmd=\"%s\"",cmd.GetData());
    return resp;
}

bool NPCCmdResponseOp::Run(gemNPC *who, Client *target,NpcResponse *owner,csTicks& timeDelay)
{
    psserver->GetNPCManager()->QueueNPCCmdPerception(who,cmd);
    return true;
}


bool VerifyQuestCompletedResponseOp::Load(iDocumentNode *node)
{
    quest = CacheManager::GetSingleton().GetQuestByName( node->GetAttributeValue("quest") );
    error_msg = node->GetAttributeValue("error_msg");
    if (error_msg=="(null)") error_msg="";

    if (!quest)
    {
        Error2("Quest %s was not found in VerifyQuestCompleted script op! You must have at least one!",node->GetAttributeValue("quest") );
        return false;
    }
    return true;
}

csString VerifyQuestCompletedResponseOp::GetResponseScript()
{
    psString resp = GetName();
    resp.AppendFmt(" quest=\"%s\"",quest->GetName());
    if (!error_msg.IsEmpty())
    {
        resp.AppendFmt(" error_msg=\"%s\"",error_msg.GetDataSafe());
    }
    return resp;
}

bool VerifyQuestCompletedResponseOp::Run(gemNPC *who, Client *target,NpcResponse *owner,csTicks& timeDelay)
{
    bool avail = target->GetCharacterData()->CheckQuestCompleted(quest);
    if (!avail)
    {
        who->GetNPCDialogPtr()->SubstituteKeywords(target,error_msg);
        who->Say(error_msg,target,false,timeDelay);
        return false;
    }
    return true;  // and don't say anything
}

VerifyQuestAssignedResponseOp::VerifyQuestAssignedResponseOp(int quest_id)
{
    name="verifyquestassigned";
    quest = CacheManager::GetSingleton().GetQuestByID(quest_id);
    if (!quest)
    {
        Error2("Quest %d was not found in VerifyQuestAssigned script op!",quest_id);
    }
}

bool VerifyQuestAssignedResponseOp::Load(iDocumentNode *node)
{
    quest = CacheManager::GetSingleton().GetQuestByName( node->GetAttributeValue("quest") );
    error_msg = node->GetAttributeValue("error_msg");
    if (error_msg=="(null)") error_msg="";
    if (!quest && node->GetAttributeValue("quest") )
    {
        Error2("Quest <%s> not found!",node->GetAttributeValue("quest"));
    }
    else if (!quest)
    {
        Error1("Quest name must be specified in VerifyQuestAssigned script op!");
        return false;
    }
    return true;
}

csString VerifyQuestAssignedResponseOp::GetResponseScript()
{
    psString resp = GetName();
    resp.AppendFmt(" quest=\"%s\"",quest->GetName());
    if (!error_msg.IsEmpty())
    {
        resp.AppendFmt(" error_msg=\"%s\"",error_msg.GetDataSafe());
    }
    return resp;
}

bool VerifyQuestAssignedResponseOp::Run(gemNPC *who, Client *target,NpcResponse *owner,csTicks& timeDelay)
{
    bool avail = target->GetCharacterData()->CheckQuestAssigned(quest);
    if (!avail)
    {
        if (error_msg.IsEmpty() || error_msg.Length() == 0)
        {
            error_msg = "I don't know what you are talking about.";  // TODO: use the standard error response for that NPC
        }
        who->GetNPCDialogPtr()->SubstituteKeywords(target,error_msg);
        who->Say(error_msg,target,false,timeDelay);
        return false;
    }
    return true;  // and don't say anything
}

VerifyQuestNotAssignedResponseOp::VerifyQuestNotAssignedResponseOp(int quest_id)
{
    name="verifyquestnotassigned";
    quest = CacheManager::GetSingleton().GetQuestByID(quest_id);
    if (!quest)
    {
        Error2("Quest %d was not found in VerifyQuestNotAssigned script op!",quest_id);
    }
}

bool VerifyQuestNotAssignedResponseOp::Load(iDocumentNode *node)
{
    quest = CacheManager::GetSingleton().GetQuestByName( node->GetAttributeValue("quest") );
    error_msg = node->GetAttributeValue("error_msg");
    if (error_msg=="(null)") error_msg="";
    if (!quest && node->GetAttributeValue("quest") )
    {
        Error2("Quest <%s> not found!",node->GetAttributeValue("quest"));
    }
    else if (!quest)
    {
        Error1("Quest name must be specified in VerifyQuestNotAssigned script op!");
        return false;
    }
    return true;
}

csString VerifyQuestNotAssignedResponseOp::GetResponseScript()
{
    psString resp = GetName();
    resp.AppendFmt(" quest=\"%s\"",quest->GetName());
    if (!error_msg.IsEmpty())
    {
        resp.AppendFmt(" error_msg=\"%s\"",error_msg.GetDataSafe());
    }
    return resp;
}

bool VerifyQuestNotAssignedResponseOp::Run(gemNPC *who, Client *target,NpcResponse *owner,csTicks& timeDelay)
{
    bool avail = target->GetCharacterData()->CheckQuestAssigned(quest);
    if (avail)
    {
        if (error_msg.IsEmpty() || error_msg.Length() == 0)
        {
            error_msg = "I don't know what you are talking about.";  // TODO: use the standard error response for that NPC
        }
        who->GetNPCDialogPtr()->SubstituteKeywords(target,error_msg);
        who->Say(error_msg,target,false,timeDelay);
        return false;
    }
    return true;  // and don't say anything
}


bool AssignQuestResponseOp::Load(iDocumentNode *node)
{
    quest[0] = CacheManager::GetSingleton().GetQuestByName( node->GetAttributeValue("q1") );
    quest[1] = CacheManager::GetSingleton().GetQuestByName( node->GetAttributeValue("q2") );
    quest[2] = CacheManager::GetSingleton().GetQuestByName( node->GetAttributeValue("q3") );
    quest[3] = CacheManager::GetSingleton().GetQuestByName( node->GetAttributeValue("q4") );
    quest[4] = CacheManager::GetSingleton().GetQuestByName( node->GetAttributeValue("q5") );

    if (!quest[0])
    {
        Error2("Quest %s was not found in Assign Quest script op! You must have at least one!",node->GetAttributeValue("q1") );
        return false;
    }
    if (node->GetAttributeValue("timeout_msg"))
    {
        timeout_msg = node->GetAttributeValue("timeout_msg");
        if (timeout_msg=="(null)") timeout_msg="";
    }
    if (quest[4])
        num_quests = 5;
    else if (quest[3])
        num_quests = 4;
    else if (quest[2])
        num_quests = 3;
    else if (quest[1])
        num_quests = 2;
    else if (quest[0])
        num_quests = 1;

    return true;
}

csString AssignQuestResponseOp::GetResponseScript()
{
    psString resp = GetName();
    for (int n = 0; n < 5; n++)
    {
        if (quest[n])
        {
            resp.AppendFmt(" q%d=\"%s\"",n+1,quest[n]->GetName());
        }

    }
    if (!timeout_msg.IsEmpty())
    {
        resp.AppendFmt(" timeout_msg=\"%s\"",timeout_msg.GetDataSafe());
    }
    return resp;
}

bool AssignQuestResponseOp::Run(gemNPC *who, Client *target,NpcResponse *owner,csTicks& timeDelay)
{
    if (owner->GetActiveQuest() == -1)
    {
        owner->SetActiveQuest(GetMaxQuests());
        Debug4(LOG_QUESTS, target->GetClientNum(),"Selected quest %d out of %d for %s",owner->GetActiveQuest()+1,
               GetMaxQuests(),target->GetCharacterData()->GetCharName());
    }

    if (target->GetCharacterData()->CheckQuestAssigned(quest[owner->GetActiveQuest()]))
    {
        Debug3(LOG_QUESTS, target->GetClientNum(),"Quest(%d) is already assigned for %s",owner->GetActiveQuest()+1,
               target->GetCharacterData()->GetCharName());
        return false;
    }

    psserver->questmanager->Assign(quest[owner->GetActiveQuest()],target,who);
    return true;
}

bool AssignQuestSelectOp::Run(gemNPC *who, Client *target,NpcResponse *owner,csTicks& timeDelay)
{
    if (owner->GetActiveQuest() == -1)
    {
        owner->SetActiveQuest(quest_op->GetMaxQuests());
        Debug4(LOG_QUESTS, target->GetClientNum(), "Selected quest %d out of %d for %s",owner->GetActiveQuest()+1,
               quest_op->GetMaxQuests(),target->GetCharacterData()->GetCharName());
    }

    if (target->GetCharacterData()->CheckQuestAssigned(quest_op->GetQuest(owner->GetActiveQuest())))
    {
        Debug3(LOG_QUESTS, target->GetClientNum(), "Quest(%d) is already assignd for %s",quest_op->GetQuest(owner->GetActiveQuest())->GetID(),
               target->GetCharacterData()->GetCharName());
        return false;
    }

    return true;
}

csString AssignQuestSelectOp::GetResponseScript()
{
    psString resp = GetName();
    resp.AppendFmt(" max_quest=\"%d\"", quest_op->GetMaxQuests());
    return resp;
}


bool FireEventResponseOp::Load(iDocumentNode *node)
{
    event = node->GetAttributeValue("name");
    return true;
}

csString FireEventResponseOp::GetResponseScript()
{
    psString resp = GetName();
    return resp;
}

bool FireEventResponseOp::Run(gemNPC *who, Client *target,NpcResponse *owner,csTicks& timeDelay)
{
    psCharacter *character = target->GetActor()->GetCharacterData();
    character->FireEvent(event);

    return true;
}

bool CheckQuestTimeoutOp::Run(gemNPC *who, Client *target,NpcResponse *owner,csTicks& timeDelay)
{
    if (owner->GetActiveQuest() == -1)
    {
        owner->SetActiveQuest(quest_op->GetMaxQuests());
        Debug4(LOG_QUESTS, target->GetClientNum(), "Selected quest %d out of %d for %s",owner->GetActiveQuest()+1,
               quest_op->GetMaxQuests(),target->GetCharacterData()->GetCharName());
    }

    bool avail = target->GetCharacterData()->CheckQuestAvailable(quest_op->GetQuest(owner->GetActiveQuest()),
                                                                 who->GetPID());
    if (!avail)
    {
        psString timeOutMsg = quest_op->GetTimeoutMsg();
        who->GetNPCDialogPtr()->SubstituteKeywords(target,timeOutMsg);
        who->Say(timeOutMsg,target,false,timeDelay);
        return false;
    }
    return true;
}

csString CheckQuestTimeoutOp::GetResponseScript()
{
    psString resp = GetName();
    return resp;
}

bool CompleteQuestResponseOp::Load(iDocumentNode *node)
{
    quest = CacheManager::GetSingleton().GetQuestByName( node->GetAttributeValue("quest_id") );
    if (!quest)
    {
        Error2("Quest '%s' was not found in Complete Quest script op!",node->GetAttributeValue("quest_id") );
        return false;
    }
    error_msg = node->GetAttributeValue("error_msg");
    if (error_msg=="(null)") error_msg="";

    return true;
}

csString CompleteQuestResponseOp::GetResponseScript()
{
    psString resp = GetName();
    resp.AppendFmt(" quest_id=\"%s\"", quest->GetName());
    if (!error_msg.IsEmpty())
    {
        resp.AppendFmt(" error_msg=\"%s\"",error_msg.GetDataSafe());
    }
    return resp;
}

bool CompleteQuestResponseOp::Run(gemNPC *who, Client *target,NpcResponse *owner,csTicks& timeDelay)
{
    if (!psserver->questmanager->Complete(quest,target))
    {
        who->GetNPCDialogPtr()->SubstituteKeywords(target,error_msg);
        who->Say(error_msg,target,false,timeDelay);
        return false;
    }
    else
        return true;
}

bool GiveItemResponseOp::Load(iDocumentNode *node)
{
    itemstat = CacheManager::GetSingleton().GetBasicItemStatsByID( node->GetAttributeValueAsInt("item") );
    if (!itemstat)
        itemstat = CacheManager::GetSingleton().GetBasicItemStatsByName(node->GetAttributeValue("item") );

    if (!itemstat)
    {
        Error2("ItemStat '%s' was not found in GiveItem script op!",node->GetAttributeValue("item") );
        return false;
    }

    // Check for count attribute
    if (node->GetAttribute("count"))
    {
        count = node->GetAttributeValueAsInt("count");
        if (count < 1)
        {
            Error1("Try to give negative or zero count in GiveItem script op!");
            count = 1;
        }

        if (count > 1 && !itemstat->GetIsStackable())
        {
            Error2("ItemStat '%s' isn't stackable in GiveItem script op!",node->GetAttributeValue("item") );
            return false;
        }
    }

    return true;
}

csString GiveItemResponseOp::GetResponseScript()
{
    psString resp = GetName();
    resp.AppendFmt(" item_id=\"%s\"",itemstat->GetName());
    if (count != 1)
    {
        resp.AppendFmt(" count=\"%d\"",count);
    }

    return resp;
}

bool GiveItemResponseOp::Run(gemNPC *who, Client *target,NpcResponse *owner,csTicks& timeDelay)
{
    psCharacter *character = target->GetActor()->GetCharacterData();
    if (!character)
        return false;

    psItem *item = itemstat->InstantiateBasicItem(false); // Not a transient item

    if (!item)
    {
        Error3("Couldn't give item %u to player %s!\n",itemstat->GetUID(), target->GetName());
        return false;
    }

    item->SetStackCount(count);
    item->SetLoaded();  // Item is fully created


    csString itemName = item->GetQuantityName();
    psserver->SendSystemInfo(target->GetClientNum(), "You have received %s.", itemName.GetData());

    if (!character->Inventory().AddOrDrop(item))
        psserver->SendSystemError(target->GetClientNum(), "You received %s, but dropped it because you can't carry any more.", itemName.GetData());

    return true;
}

bool FactionResponseOp::Load(iDocumentNode *node)
{
    faction = psserver->GetProgressionManager()->FindFaction(node->GetAttributeValue("name"));
    if (!faction)
    {
        Error2("Error: FactionOp faction(%s) not found\n",node->GetAttributeValue("name"));
        return false;
    }
    value = node->GetAttributeValueAsInt("value");
    return true;
}

csString FactionResponseOp::GetResponseScript()
{
    psString resp = GetName();
    csString escpxml = EscpXML(faction->name);
    resp.AppendFmt(" name=\"%s\"",escpxml.GetData());
    resp.AppendFmt(" value=\"%d\"",value);
    return resp;
}

bool FactionResponseOp::Run(gemNPC *who, Client *target,NpcResponse *owner,csTicks& timeDelay)
{
    psCharacter *player = target->GetActor()->GetCharacterData();
    if (!player)
    {
        Error1("Error: FactionResponceOp found no character");
        return false;
    }

    player->UpdateFaction(faction,value);

    return true;
}

bool RunScriptResponseOp::Load(iDocumentNode *node)
{
    scriptname = node->GetAttributeValue("scr");
    if (!scriptname.Length())
    {
        Error1("Progression script name was not specified in Run script op!");
        return false;
    }
    p0 = node->GetAttributeValueAsFloat("param0");
    p1 = node->GetAttributeValueAsFloat("param1");
    p2 = node->GetAttributeValueAsFloat("param2");
    return true;
}

csString RunScriptResponseOp::GetResponseScript()
{
    psString resp = GetName();
    resp.AppendFmt(" scr=\"%s\"",scriptname.GetData());
    if (p0 != 0)
        resp.AppendFmt(" param0=\"%f\"",p0);
    if (p1 != 0)
        resp.AppendFmt(" param1=\"%f\"",p1);
    if (p2 != 0)
        resp.AppendFmt(" param2=\"%f\"",p2);

    return resp;
}

bool RunScriptResponseOp::Run(gemNPC *who, Client *target, NpcResponse *owner, csTicks& timeDelay)
{
    ProgressionEvent *event;

    if ((scriptname.GetDataSafe())[0] == '<')
    {
        event = psserver->GetProgressionManager()->CreateEvent(who->GetName(),scriptname);
        if (!event)
        {
            Error2("Progression script '%s' could not be created in the Progression Manager!",scriptname.GetData());
            return true;
        }
    }
    else
    {
        event = psserver->GetProgressionManager()->FindEvent(scriptname);
        if (!event)
        {
            Error2("Progression script '%s' was not found in the Progression Manager!",scriptname.GetData());
            return true;
        }
    }

    MathScriptVar *var;
    var = event->FindOrCreateVariable("Param0");
    if (var)
        var->SetValue(p0);
    var = event->FindOrCreateVariable("Param1");
    if (var)
        var->SetValue(p1);
    var = event->FindOrCreateVariable("Param2");
    if (var)
        var->SetValue(p2);

    event->Run(target->GetActor(), who, target->GetCharacterData()->Inventory().GetItemHeld());

    return true;
}


bool TrainResponseOp::Load(iDocumentNode *node)
{
    if (node->GetAttributeValue("skill"))
        skill = CacheManager::GetSingleton().GetSkillByName(node->GetAttributeValue("skill"));
    else
        skill = NULL;

    if(!skill)
    {
        CPrintf(CON_ERROR, "Couldn't find skill '%s'",node->GetAttributeValue("skill")? node->GetAttributeValue("skill"):"Not Specified");
        return false;
    }

    return true;
}

csString TrainResponseOp::GetResponseScript()
{
    psString resp = GetName();
    return resp;
}

bool TrainResponseOp::Run(gemNPC *who, Client *target,NpcResponse *owner,csTicks& timeDelay)
{
    if (CheckTraining(who, target, skill))
    {
        csString question;
        question.Format("Do you really want to train %s ? You will be charged %d trias.",
        skill->name.GetData(), skill->price.GetTotal());
        psserver->questionmanager->SendQuestion(
            new TrainingConfirm(question, who, target, skill));
    }
    return true;
}


bool GuildAwardResponseOp::Load(iDocumentNode *node)
{
    karma = node->GetAttributeValueAsInt("karma");
    return true;
}

csString GuildAwardResponseOp::GetResponseScript()
{
    psString resp = GetName();
    return resp;
}

bool GuildAwardResponseOp::Run(gemNPC *who, Client *target,NpcResponse *owner,csTicks& timeDelay)
{
    psGuildInfo * guild = CacheManager::GetSingleton().FindGuild(target->GetGuildID());
    if (!guild)
    {
        CPrintf(CON_ERROR, "Couldn't find guild (%d). Guild karma points not added\n",target->GetGuildID());
        return false;
    }
    guild->karma_points += karma;
    db->Command("UPDATE guilds SET karma_points = '%d' WHERE id = '%d'",guild->karma_points,guild->id);
    // TODO: Notify player and guild of what happened here.
    return true;
}

bool OfferRewardResponseOp::Load(iDocumentNode *node)
{
    // <offer>
    //      <item id="9"/>
    //      <item id="10"/>
    // </offer>

    csRef<iDocumentNodeIterator> iter = node->GetNodes();

    // for each item in the offer list
    while (iter->HasNext())
    {
        csRef<iDocumentNode> node = iter->Next();
        psItemStats* itemstat;
        // get item
        uint32 itemID = (uint32)node->GetAttributeValueAsInt("id");
        if (itemID)
            itemstat = CacheManager::GetSingleton().GetBasicItemStatsByID(itemID);
        else
            itemstat = CacheManager::GetSingleton().GetBasicItemStatsByName(node->GetAttributeValue("name"));

        // make sure that the item exists
        if (!itemstat)
        {
            Error3("ItemStat #%u/%s was not found in OfferReward script op!",itemID,node->GetAttributeValue("name"));
            return false;
        }
        // add this item to the list
        offer.Push(itemstat);
    }
    return true;
}

csString OfferRewardResponseOp::GetResponseScript()
{
    psString resp = GetName();
    for (size_t n = 0; n < offer.GetSize();n++)
    {
        resp.AppendFmt("><item id=\"%s\"/",offer[n]->GetName());
    }
    resp.AppendFmt("></offer");
    return resp;
}

bool OfferRewardResponseOp::Run(gemNPC *who, Client *target, NpcResponse *owner,csTicks& timeDelay)
{
    psserver->questmanager->OfferRewardsToPlayer(target, offer, timeDelay);
    return true;
}

bool MoneyResponseOp::Load(iDocumentNode *node)
{
    if (node->GetAttributeValue("value"))
        money = psMoney(node->GetAttributeValue("value"));
    else
        money = psMoney();

    if(money.GetTotal()==0)
    {
        CPrintf(CON_ERROR, "Couldn't load money '%s' or money = 0",node->GetAttributeValue("value"));
        return false;
    }

    return true;
}

csString MoneyResponseOp::GetResponseScript()
{
    psString resp;
    resp = GetName();
    resp.AppendFmt(" value=\"%s\"",money.ToString().GetData());
    return resp;
}

bool MoneyResponseOp::Run(gemNPC *who, Client *target,NpcResponse *owner,csTicks& timeDelay)
{
    psCharacter * character = target->GetCharacterData();

    character->SetMoney(character->Money()+money);

    psserver->SendSystemInfo(target->GetClientNum(), "You've received %s",money.ToUserString().GetData());
    return true;
}

bool IntroduceResponseOp::Load(iDocumentNode *node)
{
    if (node->GetAttributeValue("name"))
        targetName = node->GetAttributeValue("name");
    else
        targetName = "Me";

    return true;
}

csString IntroduceResponseOp::GetResponseScript()
{
    psString resp;
    resp = GetName();
    resp.AppendFmt(" name=\"%s\"",targetName.GetData());
    return resp;
}

bool IntroduceResponseOp::Run(gemNPC *who, Client *target,NpcResponse *owner,csTicks& timeDelay)
{
    psCharacter * character = target->GetCharacterData();
    psCharacter * npcChar = who->GetCharacterData();

    if (targetName != "Me")
    {
        gemObject* obj = psserver->GetAdminManager()->FindObjectByString(targetName,who);
        if (obj)
        {
            character->Introduce(((gemNPC*)obj)->GetCharacterData());
            obj->Send(target->GetClientNum(), false, false);
            psserver->SendSystemInfo(target->GetClientNum(), "You now know %s",((gemNPC*)obj)->GetName());
        }
    }
    else
    {
        character->Introduce(npcChar);
        who->Send(target->GetClientNum(), false, false);
        psserver->SendSystemInfo(target->GetClientNum(), "You now know %s",who->GetName());
    }

    return true;
}

bool DoAdminCommandResponseOp::Load(iDocumentNode *node)
{
    origCommandString = node->GetAttributeValue("command");
    return true;
}

csString DoAdminCommandResponseOp::GetResponseScript()
{
    psString resp;
    return resp;
}

bool DoAdminCommandResponseOp::Run(gemNPC *who, Client *target,NpcResponse *owner,csTicks& timeDelay)
{
    modifiedCommandString = origCommandString;
    csString format;
    format.Format("\"%s\"", target->GetCharacterData()->GetCharFullName());
    modifiedCommandString.ReplaceAll("targetchar", format);
    format.Format("\"%s\"", who->GetNPCPtr()->GetCharacterData()->GetCharFullName());
    modifiedCommandString.ReplaceAll("sourcenpc", format);
    psAdminCmdMessage msg(modifiedCommandString, 0);
    msg.msg->current=0;
    psserver->GetAdminManager()->HandleMessage(msg.msg, target);
    return true;
}

NpcDialogMenu::NpcDialogMenu()
{
	counter = 0;
}

void NpcDialogMenu::AddTrigger(const csString &formatted, const csString &trigger)
{
	NpcDialogMenu::DialogTrigger new_trigger;

	new_trigger.formatted = formatted;
	new_trigger.trigger = trigger;
	new_trigger.triggerID = counter++;

	this->triggers.Push( new_trigger );
}

void NpcDialogMenu::ShowMenu( Client *client )
{
	if( client == NULL )
		return;

	psDialogMenuMessage menu;

	for( size_t i = 0; i < counter; i++ )
		menu.AddResponse((uint32_t) i, this->triggers[ i ].formatted, this->triggers[i].trigger );

	menu.BuildMsg(client->GetClientNum());
	
	menu.SendMessage();
}
