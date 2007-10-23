/*
 * adminmessage.cpp
 *
 * Copyright (C) 2002 Atomic Blue (info@planeshift.it, http://www.atomicblue.org) 
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
 * Helper classes for cracking Admin data
 */
#include <psconfig.h>

// COMMON INCLUDES
#include "util/psxmlparser.h"
#include "util/log.h"
#include "util/consoleout.h"
#include "adminmessage.h"
#include "util/psdatabase.h"

#include "../../server/globals.h"

#include <csutil/xmltiny.h>



//--------------------------------------------------------------------------

void psDialogManager::AddTrigger( psTriggerBlock* trigger )
{
    triggers.Push( trigger );
}


void psDialogManager::AddSpecialResponse( psSpecialResponse* response )
{
    special.Push( response );
}


int psDialogManager::GetPriorID( int internalID )
{
    for (int x = 0; x < triggers.Length(); x++ )
    {
        for ( int z = 0; z <  triggers[x]->attitudes.Length(); z++ )
        {
            psAttitudeBlock* currAttitude = triggers[x]->attitudes[z];

            if ( currAttitude->responseSet.givenID == internalID )
                return currAttitude->responseSet.databaseID;
        }
    }
           
   return -1; 
}


void psDialogManager::PrintInfo()
{
    int totalTrigs = triggers.Length();
    
    for ( int z = 0; z < totalTrigs; z++ )
    {
        CPrintf(CON_CMDOUTPUT,"*******************\n");
        CPrintf(CON_CMDOUTPUT,"Trigger Complete  :\n");
        CPrintf(CON_CMDOUTPUT,"*******************\n");

        psTriggerBlock* currTrigger = triggers[z];
   
        printf("Prior responseID: %d\n", currTrigger->priorResponseID );
    int x;
        for ( x = 0; x < currTrigger->phraseList.Length(); x++ )
        {
            CPrintf(CON_CMDOUTPUT,"Phrase: %s\n", currTrigger->phraseList[x]->GetData() );
        }

        int totalAttitudes = currTrigger->attitudes.Length();
        for ( x = 0; x < totalAttitudes; x++ )
        {
            int total = 
                currTrigger->attitudes[x]->responseSet.responses.Length();
            for ( int res = 0; res < total; res++ )
            {
                CPrintf(CON_CMDOUTPUT,"Response: %s\n", 
                currTrigger->attitudes[x]->
                responseSet.responses[res]->GetData() );
            }
        }
    }

}


int psDialogManager::InsertResponse( csString& response )
{
    csString escape;
    db->Escape( escape, response );
    db->Command("INSERT INTO npc_responses (response1,response2,response3,response4,response5,"
                                           "pronoun_him, pronoun_her, pronoun_it, pronoun_them, script, quest_id) "
                "VALUES (\"%s\",'','','','','','','','','',0)",
                escape.GetData());


    int id = db->SelectSingleNumber("SELECT last_insert_id()" );
    return id;
}

int psDialogManager::InsertResponseSet( psResponse &response )
{
    stringList responseSet = response.responses;
    csString script = response.script;
    //Notify1("Inserting the response Set");

    int total = responseSet.Length();
    csString buffer;

    csString command( "INSERT INTO npc_responses (" );
    csString values;

    if ( total > 0 )
    {
        buffer.Format(" response%d", 1 );
        command.Append( buffer );
        buffer.Format(" \"%s\" ", responseSet[0]->GetData() );
        values.Append( buffer );
    }

    for ( int x = 1; x < 5;  x++ )
    {
        buffer.Format( " , response%d ", x+1 );
        command.Append ( buffer );
        if (x<total)
        {
        buffer.Format(" ,\"%s\" ", responseSet[x]->GetData());
        values.Append( buffer );
        } else
            values.Append(" ,\"\" ");

    }

    command.Append ( ", pronoun_him, pronoun_her, pronoun_it, pronoun_them,script"); 
    if(response.questID == -1)
        command.Append(")");
    else
        command.Append(", quest_id)");

    csString escape;
    db->Escape( escape, script.GetDataSafe() );
    buffer.Format( " VALUES ( %s, '%s','%s','%s','%s', '%s' ", 
            (const char*)values,
            response.pronoun_him.GetDataSafe(),
            response.pronoun_her.GetDataSafe(),
            response.pronoun_it.GetDataSafe(),
            response.pronoun_them.GetDataSafe(),
            escape.GetData());
    if (response.questID == -1)
        buffer.Append(")");
    else
    {
        buffer.Append(", ");
        buffer.Append(response.questID);
        buffer.Append(")");
    }
    command += csString(buffer);


   
    //Notify2("Final Command: %s\n", command.GetData()); 
     
    if ( !db->Command( command.GetData() ) )
    {
        Error2("MYSQL ERROR: %s", db->GetLastError());
        Error2("LAST QUERY: %s", db->GetLastQuery());
        CPrintf(CON_ERROR,"%s\n",command.GetData());
   }

    int id = db->SelectSingleNumber("SELECT last_insert_id()" );

    return id;
}


int psDialogManager::InsertTrigger( const char* trigger, const char* area,
                              int maxAttitude, int minAttitude,
                              int responseID, int priorID, int questID )
{
    csString command;

    csString lower(trigger);
    lower.Downcase();
 
    csString escLower;
    csString escArea;
    db->Escape( escLower, lower );
    db->Escape( escArea, area );
    
    if(questID == -1)
    db->Command( "INSERT INTO npc_triggers ( trigger, "
                                              " response_id, "
                                              " prior_response_required, "
                                              " min_attitude_required, "
                                              " max_attitude_required, "
                                              " area )"
                                     " VALUES ( '%s', "
                                                " %d , "
                                                " %d , "
                                                " %d , "
                                                " %d , "
                                                " '%s' ) ",
                                            escLower.GetData() , 
                                            responseID,
                                            priorID,
                                            minAttitude,
                                            maxAttitude,
                                            escArea.GetData());
    else
        db->Command( "INSERT INTO npc_triggers ( trigger, "
                                              " response_id, "
                                              " prior_response_required, "
                                              " min_attitude_required, "
                                              " max_attitude_required, "
                                              " area, quest_id )"
                                     " VALUES ( '%s', "
                                                " %d , "
                                                " %d , "
                                                " %d , "
                                                " %d , "
                                                " '%s', %i) ",
                                            escLower.GetData(), 
                                            responseID,
                                            priorID,
                                            minAttitude,
                                            maxAttitude,
                                            escArea.GetData(), questID);

    int id = db->SelectSingleNumber("SELECT last_insert_id()" );

    return id;
}


bool psDialogManager::WriteToDatabase()
{
    // Add in all the responses
    int totalTriggers = triggers.Length();
  
    int x;
    for ( x = 0; x < totalTriggers; x++ )
    {
        psTriggerBlock* currTrig = triggers[x];
        
        // Insert all the responses for this trigger. Each 
        // attitude block represents a different response set.
        for ( int att = 0; att < currTrig->attitudes.Length(); att++ )
        {
            psAttitudeBlock* currAttitude = currTrig->attitudes[att];
            
            int id = InsertResponseSet( currAttitude->responseSet );

            // Store the database ID for later use by triggers. 
            currAttitude->responseSet.databaseID = id;

            responseIDs.Push( id );
        }
    }


    // Add all the triggers
    for ( x = 0; x < totalTriggers; x++ )
    {
        psTriggerBlock* currTrig = triggers[x];
 
        // Figure out the correct prior id to use for the database
        int goodPriorResponseID = 0;
        if ( currTrig->priorResponseID != 0 )
        {
            goodPriorResponseID = GetPriorID(currTrig->priorResponseID );
        }

        CS_ASSERT( goodPriorResponseID != -1 );
            
        for ( int phi = 0; phi < currTrig->phraseList.Length(); phi++ )
        {
            csString phrase(currTrig->phraseList[phi]->GetData() );

            for ( int att = 0; att < currTrig->attitudes.Length(); att++ )
            {
                int attMax = currTrig->attitudes[att]->maxAttitude;
                int attMin = currTrig->attitudes[att]->minAttitude;
                int respID = currTrig->attitudes[att]->responseSet.databaseID; 

                csString trigArea = currTrig->area;
                if (trigArea.IsEmpty())
                    trigArea = area;

                int databaseID = InsertTrigger( phrase, trigArea.GetDataSafe(), 
                                                attMax, attMin, 
                                                respID, goodPriorResponseID, currTrig->questID );

                triggerIDs.Push( databaseID );
            }
        }
    }

    // Add the special responses
    int totalSpecial = special.Length();
    for ( int sp = 0; sp < totalSpecial; sp++ )
    {
        psSpecialResponse *spResp = special[sp];
        int responseID = InsertResponse( spResp->response );
        int triggerID = InsertTrigger( spResp->type, 
                                       area,
                                       spResp->attMax,
                                       spResp->attMin,
                                       responseID,
                                       0, spResp->questID );

        triggerIDs.Push( triggerID );
        responseIDs.Push( responseID );
    }

    return false;
}

//--------------------------------------------------------------------------

void psTriggerBlock::Create( csRef<iDocumentNode> triggerNode, int questID )
{
    area = triggerNode->GetAttributeValue("area");
    this->questID = questID;
    //=================
    // Read in phrases
    //=================
    csRef<iDocumentNodeIterator> iter = triggerNode->GetNodes("phrase");
    while ( iter->HasNext() )
    {
        csRef<iDocumentNode> phrasenode = iter->Next();
        psString* newPhrase = new psString;
        *newPhrase = phrasenode->GetAttributeValue("value");

        phraseList.Push( newPhrase );
    }


    //========================
    // Read in attitude blocks
    //========================
    iter = triggerNode->GetNodes("attitude");
    while ( iter->HasNext() )
    {
        csRef<iDocumentNode> attitudenode = iter->Next();
        psAttitudeBlock* attitudeBlock = new psAttitudeBlock( manager );

        attitudeBlock->Create( attitudenode, questID );

        attitudes.Push( attitudeBlock );
    }
}

//--------------------------------------------------------------------------

int psAttitudeBlock::responseID = 0;

void psAttitudeBlock::Create( csRef<iDocumentNode> attitudeNode, int questID )
{
    //=========================
    // Read the attitude values
    //=========================
    csRef<iDocumentAttribute> maxattr = attitudeNode->GetAttribute( "max");
    csRef<iDocumentAttribute> minattr = attitudeNode->GetAttribute("min");

    if  ( !minattr || !maxattr )
    {
        maxAttitude = 100;
        minAttitude = -100;
    }
    else
    {
        maxAttitude = maxattr->GetValueAsInt();
        minAttitude = minattr->GetValueAsInt();
    }

    //==========================
    // Read in the response tags
    //==========================
    csRef<iDocumentNodeIterator> iter = attitudeNode->GetNodes("response");
    while ( iter->HasNext() )
    {
        psString* newResponse = new psString;
        csRef<iDocumentNode> responseNode = iter->Next();
        *newResponse = responseNode->GetAttributeValue("say");
        responseSet.responses.Push( newResponse );
    }

    iter = attitudeNode->GetNodes("script");

    responseSet.script="";
    while (iter->HasNext())
    {
        csRef<iDocumentNode> scriptNode = iter->Next();
        csString script = scriptNode->GetContentsValue();

        // remove \n and \r chars from the string
        for (unsigned int i=0;i < script.Length();i++)
            if (script.GetAt(i)!='\r'&&script.GetAt(i)!='\n')
                responseSet.script+=script.GetAt(i);
    }

    csRef<iDocumentNode> pronounNode = attitudeNode->GetNode("pronoun");

    if (pronounNode)
    {
        responseSet.pronoun_him = pronounNode->GetAttributeValue("him");
        responseSet.pronoun_her = pronounNode->GetAttributeValue("her");
        responseSet.pronoun_it = pronounNode->GetAttributeValue("it");
        responseSet.pronoun_them = pronounNode->GetAttributeValue("them");
    }

    responseSet.questID = questID;


    //=======================================================
    // Read in the sub triggers that could be in the attitude
    //=======================================================
    iter = attitudeNode->GetNodes("trigger");

    while( iter->HasNext() )
    {
        csRef<iDocumentNode> triggerNode = iter->Next();
        psTriggerBlock* trigger = new psTriggerBlock( manager );

        trigger->Create( triggerNode, questID );

        // The prior id for a sub trigger is the one for THIS attitude.
        trigger->SetPrior( responseSet.givenID );
        manager->AddTrigger( trigger );
    }
}

//--------------------------------------------------------------------------

psSpecialResponse::psSpecialResponse( csRef<iDocumentNode> responseNode, int questID )
{
    attMax = responseNode->GetAttributeValueAsInt("reactionMax");
    attMin = responseNode->GetAttributeValueAsInt("reactionMin");

    type = responseNode->GetAttributeValue("type");
    response = responseNode->GetAttributeValue("value");
    this->questID = questID;
}
