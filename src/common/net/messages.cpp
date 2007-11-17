/*
 * messages.cpp by Keith Fulton <keith@paqrat.com>
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

#include <memory.h>
#include <string.h>
#include <ctype.h>
#include <csutil/databuf.h>
#include <csutil/zip.h>
#include <iengine/engine.h>
#include <iengine/sector.h>
#include <imesh/spritecal3d.h>
#include <propclass/linmove.h>
#include <iutil/object.h>

#include "util/psconst.h"
#include "util/strutil.h"
#include "util/psxmlparser.h"
#include "net/netbase.h"
#include "net/messages.h"
#include "util/skillcache.h"
#include "net/npcmessages.h"
#include "util/log.h"
#include "net/msghandler.h"
#include "util/slots.h"
#include "rpgrules/vitals.h"

// uncomment this to produce full debug dumping in <class>::ToString functions
#define FULL_DEBUG_DUMP
//

MsgHandler *psMessageCracker::msghandler;
CS::Threading::RecursiveMutex csSyncRefCount::mutex;

void psMessageCracker::SendMessage()
{
    CS_ASSERT(valid);
    CS_ASSERT(msg);
    msghandler->SendMessage(msg);
}


void psMessageCracker::Multicast(csArray<PublishDestination>& multi, int except, float range)
{
    CS_ASSERT(valid);
    CS_ASSERT(msg);
    msghandler->Multicast(msg,multi,except,range);
}

void psMessageCracker::FireEvent()
{
    CS_ASSERT(valid);
    CS_ASSERT(msg);
    msghandler->Publish(msg);
}

// ---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psCharDeleteMessage,MSGTYPE_CHAR_DELETE);

psCharDeleteMessage::psCharDeleteMessage( const char* name, uint32_t client )
{
    if(!name)
        return;

    msg = new MsgEntry( strlen(name) + 1 );

    msg->SetType( MSGTYPE_CHAR_DELETE );
    msg->clientnum = client;
    msg->Add( name );

    valid = !(msg->overrun );
}

psCharDeleteMessage::psCharDeleteMessage( MsgEntry* message )
{
    if(!message)
        return;
    charName = message->GetStr();
}

csString psCharDeleteMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("CharName: '%s'", charName.GetDataSafe());

    return msgtext;
}


// ---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psPreAuthenticationMessage,MSGTYPE_PREAUTHENTICATE);

psPreAuthenticationMessage::psPreAuthenticationMessage(uint32_t clientnum,
    uint32_t version)
{
    msg  = new MsgEntry(sizeof(uint32_t));

    msg->SetType(MSGTYPE_PREAUTHENTICATE);
    msg->clientnum      = clientnum;

    msg->Add(version);

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psPreAuthenticationMessage::psPreAuthenticationMessage(MsgEntry *message)
{
    if (!message)
        return;

    netversion = message->GetUInt32();

    // Sets valid flag based on message overrun state
    valid=!(message->overrun);
}

bool psPreAuthenticationMessage::NetVersionOk()
{
    return netversion == PS_NETVERSION;
}

csString psPreAuthenticationMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("NetVersion: %d", netversion);

    return msgtext;
}

// ---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psAuthenticationMessage,MSGTYPE_AUTHENTICATE);

psAuthenticationMessage::psAuthenticationMessage(uint32_t clientnum,
    const char *userid,const char *password, uint32_t version)
{

    if (!userid || !password)
    {
        msg = NULL;
        return;
    }


    msg  = new MsgEntry(strlen(userid)+1+strlen(password)+1+sizeof(uint32_t),PRIORITY_LOW);

    msg->SetType(MSGTYPE_AUTHENTICATE);
    msg->clientnum      = clientnum;

    msg->Add(version);
    msg->Add(userid);
    msg->Add(password);

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psAuthenticationMessage::psAuthenticationMessage(MsgEntry *message)
{
    if (!message)
        return;

    netversion = message->GetUInt32();
    sUser = message->GetStr();
    sPassword = message->GetStr();

    // Sets valid flag based on message overrun state
    valid=!(message->overrun);
}

bool psAuthenticationMessage::NetVersionOk()
{
    return netversion == PS_NETVERSION;
}

csString psAuthenticationMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("NetVersion: %d User: '%s' Passwd: '%s'",
                      netversion,sUser.GetDataSafe(),sPassword.GetDataSafe());

    return msgtext;
}

// ---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psAuthApprovedMessage,MSGTYPE_AUTHAPPROVED);

psAuthApprovedMessage::psAuthApprovedMessage (uint32_t clientnum,
    uint32_t playerID, uint8_t numChars)
{
    msgClientValidToken = clientnum;
    msgPlayerID = playerID;
    msgNumOfChars = numChars;
}

psAuthApprovedMessage::psAuthApprovedMessage(MsgEntry *message)
{
    if (!message)
        return;

    msgClientValidToken = message->GetUInt32();
    msgPlayerID         = message->GetUInt32();
    msgNumOfChars       = message->GetUInt8();
}

void psAuthApprovedMessage::AddCharacter(const char *fullname, const char *race,
                                         const char *mesh, const char *traits,
                                         const char *equipment)
{
    contents.Push(fullname);
    contents.Push(race);
    contents.Push(mesh);
    contents.Push(traits);
    contents.Push(equipment);
}

void psAuthApprovedMessage::GetCharacter(MsgEntry *message,
                                         csString& fullname, csString& race,
                                         csString& mesh, csString& traits,
                                         csString& equipment)
{
    fullname  = message->GetStr();
    race      = message->GetStr();
    mesh      = message->GetStr();
    traits    = message->GetStr();
    equipment = message->GetStr();
}

void psAuthApprovedMessage::ConstructMsg()
{
    size_t msgSize = sizeof(uint32_t)*2 + sizeof(uint8_t);

    for (size_t i = 0; i < contents.GetSize(); ++i)
        msgSize += strlen(contents[i]) + 1;

    msg = new MsgEntry(msgSize);

    msg->SetType(MSGTYPE_AUTHAPPROVED);
    msg->clientnum      = msgClientValidToken;

    msg->Add(msgClientValidToken);
    msg->Add(msgPlayerID);
    msg->Add(msgNumOfChars);

    for (size_t i = 0; i < contents.GetSize(); ++i)
        msg->Add(contents[i]);
}

csString psAuthApprovedMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("ClientValidToken: %d PlayerID: %d NumOfChars: %d",
                      msgClientValidToken,msgPlayerID,msgNumOfChars);

    return msgtext;
}

// ---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psPreAuthApprovedMessage,MSGTYPE_PREAUTHAPPROVED);

psPreAuthApprovedMessage::psPreAuthApprovedMessage (uint32_t clientnum)
{
    msg  = new MsgEntry(sizeof(uint32_t));

    msg->SetType(MSGTYPE_PREAUTHAPPROVED);
    msg->clientnum      = clientnum;

    msg->Add(clientnum);

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psPreAuthApprovedMessage::psPreAuthApprovedMessage(MsgEntry *message)
{
    if (!message)
        return;

    ClientNum = message->GetUInt32();

    // Sets valid flag based on message overrun state
    valid=!(message->overrun);
}

csString psPreAuthApprovedMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("CNUM: %d",ClientNum);

    return msgtext;
}

// ---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psAuthRejectedMessage,MSGTYPE_AUTHREJECTED);

psAuthRejectedMessage::psAuthRejectedMessage(uint32_t clientnum,
    const char *reason)
{
    if (!reason)
        return;

    msg = new MsgEntry( strlen(reason)+1 );

    msg->SetType(MSGTYPE_AUTHREJECTED);
    msg->clientnum      = clientnum;

    msg->Add(reason);

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psAuthRejectedMessage::psAuthRejectedMessage(MsgEntry *message)
{
    if (!message)
        return;

    msgReason = message->GetStr();

    // Sets valid flag based on message overrun state
    valid=!(message->overrun);
}

csString psAuthRejectedMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Reason: '%s'",msgReason.GetDataSafe());

    return msgtext;
}

// ---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psCharacterPickerMessage,MSGTYPE_AUTHCHARACTER);

psCharacterPickerMessage::psCharacterPickerMessage( const char* characterName )
{
    msg = new MsgEntry( strlen(characterName)+1 );

    msg->SetType(MSGTYPE_AUTHCHARACTER);
    msg->clientnum      = 0;

    msg->Add(characterName);

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}


psCharacterPickerMessage::psCharacterPickerMessage( MsgEntry* message )
{
    characterName = message->GetStr();

    // Sets valid flag based on message overrun state
    valid=!(message->overrun);
}

csString psCharacterPickerMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("CharacterName: '%s'",characterName.GetDataSafe());

    return msgtext;
}

// ---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psCharacterApprovedMessage,MSGTYPE_AUTHCHARACTERAPPROVED);

psCharacterApprovedMessage::psCharacterApprovedMessage(uint32_t clientnum)
{
    msg = new MsgEntry( );

    msg->SetType(MSGTYPE_AUTHCHARACTERAPPROVED);
    msg->clientnum      = clientnum;

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}


psCharacterApprovedMessage::psCharacterApprovedMessage( MsgEntry* message )
{
   // No data, always valid
}

csString psCharacterApprovedMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Approved");

    return msgtext;
}


// ---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psChatMessage,MSGTYPE_CHAT);

psChatMessage::psChatMessage(uint32_t cnum, const char *person, const char * other,
                 const char *chatMessage, uint8_t type, bool translate)
{
    if (!chatMessage || !person)
        return;

    iChatType = type;
    sPerson   = person;
    sOther    = other;
    sText     = chatMessage;
    this->translate = translate;

    bool includeOther = iChatType == CHAT_ADVISOR;
    size_t sz = strlen(person) + 1 + strlen(chatMessage) + 1 + sizeof(uint8_t)*2;
    if (includeOther)
        sz += strlen(other) + 1;

    msg = new MsgEntry(sz);

    msg->SetType(MSGTYPE_CHAT);
    msg->clientnum      = cnum;

    msg->Add(iChatType);
    msg->Add(person);
    msg->Add(chatMessage);
    msg->Add(translate);
    if (includeOther)
        msg->Add(other);

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psChatMessage::psChatMessage(MsgEntry *message)
{
    if (!message)
        return;

    iChatType = message->GetUInt8();
    bool includeOther = iChatType == CHAT_ADVISOR;

    sPerson   = message->GetStr();
    sText     = message->GetStr();
    translate = message->GetBool();
    if (includeOther && !message->IsEmpty())
        sOther = message->GetStr();

    // Sets valid flag based on message overrun state
    valid=!(message->overrun);
    valid = valid && !(sText.IsEmpty());
}


csString psChatMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("ChatType: %s",GetTypeText());
    msgtext.AppendFmt(" Person: %s",sPerson.GetDataSafe());
    msgtext.AppendFmt(" Text: %s",sText.GetDataSafe());
    msgtext.AppendFmt(" Translate: %s",(translate?"true":"false"));

    return msgtext;
}

const char *psChatMessage::GetTypeText()
{
    switch (iChatType)
    {
        case CHAT_SAY:      return "Say";
        case CHAT_TELL:     return "Tell";
        case CHAT_NPC:      return "TellNPC";
        case CHAT_TELLSELF: return "TellSelf";
        case CHAT_GROUP:    return "GroupMsg";
        case CHAT_SHOUT:    return "Shout";
        case CHAT_GM:       return "GM";
        case CHAT_GUILD:    return "GuildChat";
        case CHAT_AUCTION:  return "Auction";
        case CHAT_MY: case CHAT_ME: case CHAT_PET_ACTION: return "Action";
        case CHAT_REPORT:   return "Report";
        case CHAT_ADVISOR:  return "Advisor";
        case CHAT_ADVICE:   return "Advice";
        default:            return "Unknown";
    }
}

// ---------------------------------------------------------------------------

psSystemMessageSafe::psSystemMessageSafe(uint32_t clientnum, uint32_t msgtype,
                 const char *text)
{
    char str[1024];

    strncpy(str, text, 1023);
    str[1023]=0x00;

    msg = new MsgEntry( strlen(str) + 1 + sizeof(uint32_t) );

    msg->SetType(MSGTYPE_SYSTEM);
    msg->clientnum      = clientnum;

    msg->Add(msgtype);
    msg->Add(str);

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

// ---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psSystemMessage,MSGTYPE_SYSTEM);

psSystemMessage::psSystemMessage(uint32_t clientnum, uint32_t msgtype, const char *fmt, ... )
{
    char str[1024];
    va_list args;

    va_start(args, fmt);

    /* This appears to work now on MSVC with vsnprintf() defined as _vsnprintf()
     *  If this is not the case please report exactly which version of MSVC this has
     *  a problem on.
     */
    vsnprintf(str,1023, fmt, args);
    str[1023]=0x00;
    va_end(args);

    msg = new MsgEntry( strlen(str) + 1 + sizeof(uint32_t) );

    msg->SetType(MSGTYPE_SYSTEM);
    msg->clientnum      = clientnum;

    msg->Add(msgtype);
    msg->Add(str);

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psSystemMessage::psSystemMessage(uint32_t clientnum, uint32_t msgtype, const char *fmt, va_list args )
{
    char str[1024];

    vsnprintf(str,1023, fmt, args);
    str[1023]=0x00;

    msg = new MsgEntry( strlen(str) + 1 + sizeof(uint32_t) );

    msg->SetType(MSGTYPE_SYSTEM);
    msg->clientnum      = clientnum;

    msg->Add(msgtype);
    msg->Add(str);

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psSystemMessage::psSystemMessage(MsgEntry *message)
{
    type    = message->GetUInt32();
    msgline = message->GetStr();

    // Sets valid flag based on message overrun state
    valid=!(message->overrun);
}

csString psSystemMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Type: %d Msg: %s",type,msgline.GetDataSafe());

    return msgtext;
}

// ---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psPetitionMessage,MSGTYPE_PETITION);

psPetitionMessage::psPetitionMessage(uint32_t clientnum, csArray<psPetitionInfo> *petition, const char* errMsg,
                     bool succeed, int type, bool gm)
{
    size_t petitionLen = (petition != NULL ? petition->GetSize():0);
    size_t maxPetitionTextLen = 0;
    for (size_t i = 0; i < petitionLen; i++)
    {
        size_t len = (&petition->Get(i))->petition.Length();
        if (len > (size_t)maxPetitionTextLen)
            maxPetitionTextLen = len;
    }

    msg = new MsgEntry(sizeof(int32_t) + (maxPetitionTextLen + 250)* (petitionLen+1) + sizeof(errMsg) +
            sizeof(succeed) + sizeof(int32_t) + sizeof(gm));

    msg->SetType(MSGTYPE_PETITION);
    msg->clientnum      = clientnum;
    msg->priority       = PRIORITY_HIGH;

    msg->Add((int32_t)petitionLen);
    msg->Add(gm);
    psPetitionInfo *current;
    for (size_t i = 0; i < petitionLen; i++)
    {
        current = &petition->Get(i);
        msg->Add((int32_t)current->id);
        msg->Add(current->petition.GetData());
        msg->Add(current->status.GetData());

        // Add specified fields based upon GM status or not
        if (!gm)
        {
            msg->Add(current->created.GetData());
            msg->Add(current->assignedgm.GetData());
			msg->Add(current->resolution.GetData());
        }
        else
        {
            msg->Add((int32_t)current->escalation);
            msg->Add(current->created.GetData());
            msg->Add(current->player.GetData());
        }
    }
    msg->Add(errMsg);
    msg->Add(succeed);
    msg->Add((int32_t)type);

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);

    if (valid)
        msg->ClipToCurrentSize();
}

psPetitionMessage::psPetitionMessage(MsgEntry *message)
{
    // Get the petitions
    int count = message->GetInt32();
    isGM = message->GetBool();
    psPetitionInfo current;

    for (int i = 0; i < count; i++)
    {
        // Set each property in psPetitionInfo:
        current.id = message->GetInt32();
        current.petition = message->GetStr();
        current.status = message->GetStr();

        // Check GM fields or user fields:
        if (!isGM)
        {
            current.created = message->GetStr();
            current.assignedgm = message->GetStr();
			current.resolution = message->GetStr();
        }
        else
        {
            current.escalation = message->GetInt32();
            current.created = message->GetStr();
            current.player = message->GetStr();
        }

        petitions.Push(current);
    }

    // Get the rest of the data:
    error = message->GetStr();
    success = message->GetBool();
    msgType = message->GetInt32();

    // Sets valid flag based on message overrun state
    valid=!(message->overrun);
}

csString psPetitionMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("IsGM: %s",(isGM?"true":"false"));

    for (size_t i = 0; i < petitions.GetSize(); i++)
    {
        psPetitionInfo current = petitions[i];

        msgtext.AppendFmt(" %zu(ID: %d Petition: '%s' Status: '%s'",
                          i,current.id,current.petition.GetDataSafe(),
                          current.status.GetDataSafe());

        // Check GM fields or user fields:
        if (!isGM)
        {
            msgtext.AppendFmt(" Created: '%s' AssignedGM: '%s')",
                              current.created.GetDataSafe(),
                              current.assignedgm.GetDataSafe());
        }
        else
        {
            msgtext.AppendFmt(" Escalation: %d Created: '%s' Player: '%s'",
                              current.escalation,
                              current.created.GetDataSafe(),
                              current.player.GetDataSafe());
        }
    }
    msgtext.AppendFmt(" Error: '%s' Success: %s MsgType: %d",
                      error.GetDataSafe(),(success?"true":"false"),msgType);

    return msgtext;
}

// ---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psPetitionRequestMessage,MSGTYPE_PETITION_REQUEST);

psPetitionRequestMessage::psPetitionRequestMessage(bool gm, const char* requestCmd, int petitionID, const char* petDesc)
{
    msg = new MsgEntry((strlen(requestCmd) + 1) + (strlen(petDesc)+1) + sizeof(gm) + sizeof(int32_t));

    msg->SetType(MSGTYPE_PETITION_REQUEST);
    msg->clientnum      = 0;

    msg->Add(gm);
    msg->Add(requestCmd);
    msg->Add((int32_t)petitionID);
    msg->Add(petDesc);

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);

    if (valid)
        msg->ClipToCurrentSize();
}

psPetitionRequestMessage::psPetitionRequestMessage(MsgEntry *message)
{
    isGM = message->GetBool();
    request = message->GetStr();
    id = message->GetInt32();
    desc = message->GetStr();

    // Sets valid flag based on message overrun state
    valid=!(message->overrun);
}

csString psPetitionRequestMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("IsGM: %s Request: '%s' Id: %d Desc: '%s'",
                      (isGM?"true":"false"),request.GetDataSafe(),id,
                      desc.GetDataSafe());

    return msgtext;
}

// ---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psGMGuiMessage,MSGTYPE_GMGUI);

psGMGuiMessage::psGMGuiMessage(uint32_t clientnum, csArray<PlayerInfo> *playerArray, int type)
{
    size_t playerCount = (playerArray == NULL ? 0 : playerArray->GetSize());
    msg = new MsgEntry(sizeof(playerCount) + 250 * (playerCount+1) + sizeof(type));

    msg->SetType(MSGTYPE_GMGUI);
    msg->clientnum = clientnum;

    msg->Add((uint32_t)playerCount);
    for (size_t i=0; i<playerCount; i++)
    {
        PlayerInfo playerInfo = playerArray->Get(i);

        msg->Add(playerInfo.name.GetData());
        msg->Add(playerInfo.lastName.GetData());
        msg->Add((int32_t)playerInfo.gender);
        msg->Add(playerInfo.guild.GetData());
        msg->Add(playerInfo.sector.GetData());
    }
    msg->Add((int32_t)type);

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
    if (valid)
        msg->ClipToCurrentSize();
}


psGMGuiMessage::psGMGuiMessage(MsgEntry *message)
{
    size_t playerCount = message->GetUInt32();
    for (size_t i=0; i<playerCount; i++)
    {
        PlayerInfo playerInfo;
        playerInfo.name = message->GetStr();
        playerInfo.lastName = message->GetStr();
        playerInfo.gender = message->GetInt32();
        playerInfo.guild = message->GetStr();
        playerInfo.sector = message->GetStr();

        players.Push(playerInfo);
    }
    type = message->GetInt32();

    // Sets valid flag based on message overrun state
    valid=!(message->overrun);
}

csString psGMGuiMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    for (size_t i=0; i < players.GetSize(); i++)
    {
        PlayerInfo p = players[i];
        msgtext.AppendFmt(" %zu(Name: '%s' Last: '%s'"
                          " Gender: %d Guild: '%s'"
                          " Sector: '%s')",i,
                          p.name.GetDataSafe(),p.lastName.GetDataSafe(),
                          p.gender,p.guild.GetDataSafe(),
                          p.sector.GetDataSafe());
    }

    msgtext.AppendFmt(" Type: %d",type);

    return msgtext;
}

// ---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psGuildCmdMessage,MSGTYPE_GUILDCMD);

psGuildCmdMessage::psGuildCmdMessage(const char *cmd)
{
    msg = new MsgEntry(strlen(cmd) + 1);

    msg->SetType(MSGTYPE_GUILDCMD);
    msg->clientnum      = 0;

    msg->Add(cmd);

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psGuildCmdMessage::psGuildCmdMessage(MsgEntry *message)
{
    valid = true;
    level = 0;

    WordArray words(message->GetStr());

    command = words[0];

    if (command == "/newguild" || command == "/endguild" || command == "/guildname" || command == "/guildpoints" )
    {
        guildname = words.GetTail(1);
        return;
    }
    if (command == "/guildinvite" || command == "/guildremove" || command == "/allianceinvite")
    {
        player = words[1];
        return;
    }
    if (command == "/confirmguildjoin")
    {
        accept = words[1];  // yes or no
        return;
    }
    if (command == "/guildlevel")
    {
        level = words.GetInt(1);
        levelname = words.GetTail(2);
        return;
    }
    if (command == "/guildmembers")
    {
        level = words.GetInt(1);
        return;
    }
    if (command == "/guildpromote")
    {
        player = words[1];
        level = words.GetInt(2);
        return;
    }
    if (command == "/guildsecret")
    {
        secret = words[1];
        return;
    }
    if (command == "/guildweb")
    {
        web_page = words.GetTail(1);
        return;
    }
    if (command == "/guildmotd")
    {
        motd = words.GetTail(1);
        return;
    }
    if (command == "/newalliance" || command == "/allianceremove" || command == "/allianceleader")
    {
        alliancename = words.GetTail(1);
        return;
    }
    if (command == "/endalliance" ||
             command == "/allianceleave" ||
             command == "/guildinfo" ||
             command == "/guildwar"    ||
             command == "/guildyield" )
    {
        return;
    }

    valid = false;
}

csString psGuildCmdMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Command: '%s'",command.GetDataSafe());

    if (command == "/newguild" || command == "/endguild" || command == "/guildname" || command == "/guildpoints" )
    {
        msgtext.AppendFmt("GuildName: '%s'",guildname.GetDataSafe());
        return msgtext;
    }
    if (command == "/guildinvite" || command == "/guildremove" || command == "/allianceinvite")
    {
        msgtext.AppendFmt("Player: '%s'",player.GetDataSafe());
        return msgtext;
    }
    if (command == "/confirmguildjoin")
    {
        msgtext.AppendFmt("Accept: '%s'",accept.GetDataSafe());
        return msgtext;
    }
    if (command == "/guildlevel")
    {
        msgtext.AppendFmt("Level: %d LevelName: '%s'",level,levelname.GetDataSafe());
        return msgtext;
    }
    if (command == "/guildmembers")
    {
        msgtext.AppendFmt("Level: %d",level);
        return msgtext;
    }
    if (command == "/guildpromote")
    {
        msgtext.AppendFmt("Player: '%s' Level: %d",player.GetDataSafe(),level);
        return msgtext;
    }
    if (command == "/guildsecret")
    {
        msgtext.AppendFmt("Secret: '%s'",secret.GetDataSafe());
        return msgtext;
    }
    if (command == "/guildweb")
    {
        msgtext.AppendFmt("Web page: '%s'",web_page.GetDataSafe());
        return msgtext;
    }
    if (command == "/guildmotd")
    {
        msgtext.AppendFmt("Motd: '%s'",motd.GetDataSafe());
        return msgtext;
    }
    if (command == "/newalliance" || command == "/allianceremove" || command == "/allianceleader")
    {
        msgtext.AppendFmt("AlianceName: '%s'",alliancename.GetDataSafe());
        return msgtext;
    }
    if (command == "/endalliance" ||
             command == "/allianceleave" ||
             command == "/guildinfo" ||
             command == "/guildwar"    ||
             command == "/guildyield" )
    {
        return msgtext;
    }

    msgtext.AppendFmt("ERROR: Not decoded.");

    return msgtext;
}

//--------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psGUIGuildMessage,MSGTYPE_GUIGUILD);

psGUIGuildMessage::psGUIGuildMessage( uint32_t command,
                                      csString commandData)
{
    msg = new MsgEntry( sizeof(command) +
                        commandData.Length() +
                        1);

    msg->SetType(MSGTYPE_GUIGUILD);
    msg->clientnum  = 0;

    msg->Add( command );
    msg->Add( commandData );

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psGUIGuildMessage::psGUIGuildMessage( uint32_t clientNum,
                                      uint32_t command,
                                      csString commandData)
{
    msg = new MsgEntry( sizeof(command) +
                        commandData.Length() +
                        1);

    msg->SetType(MSGTYPE_GUIGUILD);
    msg->clientnum  = clientNum;

    msg->Add( command );
    msg->Add( commandData );

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psGUIGuildMessage::psGUIGuildMessage( MsgEntry* message )
{
    if ( !message )
        return;

    command   = message->GetUInt32();
    commandData = message->GetStr();

    // Sets valid flag based on message overrun state
    valid=!(message->overrun);
}

csString psGUIGuildMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Command: %d Data: '%s'",
                      command,commandData.GetDataSafe());

    return msgtext;
}

//--------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psGroupCmdMessage,MSGTYPE_GROUPCMD);

psGroupCmdMessage::psGroupCmdMessage(const char *cmd)
{
    msg = new MsgEntry(strlen(cmd) + 1);

    msg->SetType(MSGTYPE_GROUPCMD);
    msg->clientnum      = 0;

    msg->Add(cmd);

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psGroupCmdMessage::psGroupCmdMessage(uint32_t clientnum,const char *cmd)
{
    msg = new MsgEntry( strlen(cmd) + 1);

    msg->SetType(MSGTYPE_GROUPCMD);
    msg->clientnum      = clientnum;
    msg->priority       = PRIORITY_HIGH;

    msg->Add(cmd);

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}


psGroupCmdMessage::psGroupCmdMessage(MsgEntry *message)
{
    valid = true;

    WordArray words(message->GetStr());
    command = words[0];

    if (command == "/invite" || command == "/groupremove")
    {
        player = words[1];
        return;
    }
    if (command == "/confirmgroupjoin")
    {
        accept = words[1];  // yes or no
        return;
    }
    if (command == "/disband" ||
             command == "/leavegroup" ||
             command == "/groupmembers")
    {
        return;
    }

    valid = false;
}

csString psGroupCmdMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

	msgtext.AppendFmt("Command: '%s'", command.GetDataSafe());
	if (command == "/invite" || command == "/groupremove")
	{
		msgtext.AppendFmt("Player: '%s'", player.GetDataSafe());
		return msgtext;
	}
	if (command == "/confirmgoupjoin")
	{
		msgtext.AppendFmt("Accept: '%s'", accept.GetDataSafe());
		return msgtext;
	}
	if (command == "/disband" ||
			command == "/leavegroup" ||
			command == "/groupmembers")
	{
		return msgtext;
	}

	msgtext.AppendFmt("Error: Not Decoded");

    return msgtext;
}

// ---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psUserCmdMessage,MSGTYPE_USERCMD);

psUserCmdMessage::psUserCmdMessage(const char *cmd)
{
    msg = new MsgEntry(strlen(cmd) + 1);

    msg->SetType(MSGTYPE_USERCMD);
    msg->clientnum      = 0;

    msg->Add(cmd);

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psUserCmdMessage::psUserCmdMessage(MsgEntry *message)
{
    valid = true;
    level = 0;

    WordArray words(message->GetStr());

    command = words[0];
        
    if (command == "/who" || command == "/buddylist")
    {
        filter = words[1];
        return;
    }
    if (command == "/buddy" || command == "/notbuddy")
    {
        player = words[1];
        return;
    }
    if ( command == "/pos" )
    {
        player = words.GetTail(1);
        return;
    }
    if ( command == "/spawn" ||
         command == "/unstick" ||
         command == "/die" ||
         command == "/loot" ||
         command == "/train" ||
         command == "/use" ||
         command == "/stopattack" ||
         command == "/starttrading" ||
         command == "/stoptrading" ||
         command == "/quests" ||
         command == "/tip" ||
         command == "/motd" ||
         command == "/challenge" ||
         command == "/yield" ||
         command == "/admin" ||
         command == "/listemotes" ||
         command == "/duelpoints" ||
         command == "/sit" ||
         command == "/stand")
    {
        return;
    }
    //if (command == "/advisor" || command == "/advice")
    if (command == "/advisormode")
    {
        filter = words.GetTail(1);
        return;
    }
    if (command == "/attack")
    {
        stance = words.Get(1);
        return;
    }
    if (command == "/roll")
    {
        if (words.GetCount() == 1)
        {
            dice  = 1;
            sides = 6;
        }
        else if (words.GetCount() == 2)
        {
            dice  = 1;
            sides = words.GetInt(1);
        }
        else
        {
            dice = words.GetInt(1);
            sides = words.GetInt(2);
        }
        return;
    }
    if ( command == "/assist" )
    {
        player = words[1];
        return;
    }
    if ( command == "/marriage" )
    {
        action = words.Get(1);
        if ( action == "propose" )
        {
            player = words.Get(2);
            text = words.GetTail(3);
        }
        else if ( action == "divorce" )
        {
            text = words.GetTail(2);
        }
        return;
    }
    if ( command == "/bank" )
    {
        action = words.Get(1);
        return;
    }

    valid = false;
}

csString psUserCmdMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

	msgtext.AppendFmt("Command: '%s'", command.GetDataSafe());
	if (command == "/who" || command == "/buddylist" ||
			command == "/advisormode")
	{
		msgtext.AppendFmt("Filter: '%s'", filter.GetDataSafe());
		return msgtext;
	}
    if (command == "/buddy" || command == "/notbuddy" ||
			command == "/pos" || command == "/assist")
	{
		msgtext.AppendFmt("Player: '%s'", player.GetDataSafe());
		return msgtext;
	}
	if (command == "/attack")
	{
		msgtext.AppendFmt("Stance: '%s'", stance.GetDataSafe());
		return msgtext;
	}
	if (command == "/roll")
	{
		msgtext.AppendFmt("Rolled '%d' '%d' sided dice", dice, sides);
		return msgtext;
	}
	if (command == "/marriage")
	{
		msgtext.AppendFmt("Action: %s ", action.GetData());
		if (player.Length())
		    msgtext.AppendFmt("Player: %s ", player.GetData());
        msgtext.AppendFmt("Message: %s", text.GetData());
		return msgtext;
	}
	if (command == "/spawn" || command == "/unstick" ||
         command == "/die" || command == "/loot" ||
         command == "/train" || command == "/use" ||
         command == "/stopattack" || command == "/starttrading" ||
         command == "/stoptrading" || command == "/quests" ||
         command == "/tip" || command == "/motd" ||
         command == "/challenge" || command == "/yield" ||
         command == "/admin" || command == "/duelpoints" ||
		 command == "/list" || command == "/listemotes" ||
		 command == "/sit" || command == "/stand" ||
         command == "/bank")
	{
		return msgtext;
	}

	msgtext.AppendFmt("Error: Not Decoded");

    return msgtext;
}

// ---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psWorkCmdMessage,MSGTYPE_WORKCMD);

psWorkCmdMessage::psWorkCmdMessage(const char *cmd)
{
    msg = new MsgEntry(strlen(cmd) + 1);

    msg->SetType(MSGTYPE_WORKCMD);
    msg->clientnum      = 0;
    msg->priority       = PRIORITY_HIGH;

    msg->Add(cmd);

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}


psWorkCmdMessage::psWorkCmdMessage(MsgEntry *message)
{
    valid = true;

    WordArray words(message->GetStr());

    command = words[0];

    if (command == "/use"||
        command == "/combine")
    {
        return;
    }
    if (command == "/dig" || command == "/fish")
    {
        filter = words[1];
        if (filter == "for")
        {
            filter = words[2];
        }
        return;
    }
    
    if (command == "/repair")
    {
        repairSlotName = words[1];
        return;
    }

    valid = false;
}

csString psWorkCmdMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Command: '%s'",command.GetDataSafe());
    if (command == "/dig")
    {
        msgtext.AppendFmt(" Filter: '%s'",filter.GetDataSafe());
    }

    return msgtext;
}

// ---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psAdminCmdMessage,MSGTYPE_ADMINCMD);

psAdminCmdMessage::psAdminCmdMessage(const char *cmd)
{
    msg = new MsgEntry(strlen(cmd) + 1);

    msg->SetType(MSGTYPE_ADMINCMD);
    msg->clientnum      = 0;
    msg->priority       = PRIORITY_HIGH;

    msg->Add(cmd);

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psAdminCmdMessage::psAdminCmdMessage(const char *cmd, uint32_t client)
{
    msg = new MsgEntry(strlen(cmd) + 1);

    msg->SetType(MSGTYPE_ADMINCMD);
    msg->clientnum      = client;
    msg->priority       = PRIORITY_HIGH;

    msg->Add(cmd);

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psAdminCmdMessage::psAdminCmdMessage(MsgEntry *message)
{
    valid = true;
    cmd = message->GetStr();
}

csString psAdminCmdMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Cmd: '%s'",cmd.GetDataSafe());

    return msgtext;
}


// ---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psDisconnectMessage,MSGTYPE_DISCONNECT);

psDisconnectMessage::psDisconnectMessage(uint32_t clientnum,PS_ID actorid, const char *reason)
{
    msg = new MsgEntry( sizeof(PS_ID) + strlen(reason) + 1 );

    msg->SetType(MSGTYPE_DISCONNECT);
    msg->clientnum      = clientnum;

    msg->Add((uint32_t) actorid);
    msg->Add(reason);

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psDisconnectMessage::psDisconnectMessage(MsgEntry *message)
{
    // Is this valid?  Do any psDisconnectMessage messages get created with no data?
    if (!message)
        return;

    actor     = message->GetUInt32();
    msgReason = message->GetStr();

    // Sets valid flag based on message overrun state
    valid=!(message->overrun);
}

csString psDisconnectMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Actor: %d Reason: '%s'",
                      actor,msgReason.GetDataSafe());

    return msgtext;
}

// ---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psUserActionMessage,MSGTYPE_USERACTION);

psUserActionMessage::psUserActionMessage(uint32_t clientnum,PS_ID target,const char *action, const char *dfltBehaviors)
{
    msg = new MsgEntry( strlen(action) + 1 + strlen(dfltBehaviors) + 1 + sizeof(PS_ID) );

    msg->SetType(MSGTYPE_USERACTION);
    msg->clientnum      = clientnum;

    msg->Add((uint32_t) target);
    msg->Add(action);
    msg->Add(dfltBehaviors);

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psUserActionMessage::psUserActionMessage(MsgEntry *message)
{
    if (!message)
        return;

    target = message->GetUInt32();
    action = message->GetStr();
    dfltBehaviors = message->GetStr();

    // Sets valid flag based on message overrun state
    valid=!(message->overrun);
}

csString psUserActionMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Target: %d Action: %s DfltBehaviors: %s",
                      target,action.GetDataSafe(),dfltBehaviors.GetDataSafe());

    return msgtext;
}

//---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psGUIInteractMessage,MSGTYPE_GUIINTERACT);

psGUIInteractMessage::psGUIInteractMessage (uint32_t client, uint32_t options)
{
    msg = new MsgEntry(sizeof(uint32_t));

    msg->SetType(MSGTYPE_GUIINTERACT);
    msg->clientnum      = client;

    msg->Add(options);

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}


psGUIInteractMessage::psGUIInteractMessage( MsgEntry *message )
{
    if ( !message )
        return;

    options = message->GetUInt32();

    // Sets valid flag based on message overrun state
    valid=!(message->overrun);
}

csString psGUIInteractMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.Append("Options:");
    if (options & psGUIInteractMessage::PICKUP)
        msgtext.Append(" PICKUP");
    if (options & psGUIInteractMessage::EXAMINE)
        msgtext.Append(" EXAMINE");
    if (options & psGUIInteractMessage::UNLOCK)
        msgtext.Append(" UNLOCK");
    if (options & psGUIInteractMessage::LOCK)
        msgtext.Append(" LOCK");
    if (options & psGUIInteractMessage::LOOT)
        msgtext.Append(" LOOT");
    if (options & psGUIInteractMessage::BUYSELL)
        msgtext.Append(" BUYSELL");
    if (options & psGUIInteractMessage::GIVE)
        msgtext.Append(" GIVE");
    if (options & psGUIInteractMessage::CLOSE)
        msgtext.Append(" CLOSE");
    if (options & psGUIInteractMessage::USE)
        msgtext.Append(" USE");
    if (options & psGUIInteractMessage::PLAYERDESC)
        msgtext.Append(" PLAYERDESC");
    if (options & psGUIInteractMessage::ATTACK)
        msgtext.Append(" ATTACK");
    if (options & psGUIInteractMessage::COMBINE)
        msgtext.Append(" COMBINE");
    if (options & psGUIInteractMessage::EXCHANGE)
        msgtext.Append(" EXCHANGE");
    if (options & psGUIInteractMessage::BANK)
        msgtext.Append(" BANK");
    if (options & psGUIInteractMessage::TRAIN)
        msgtext.Append(" TRAIN");
    if (options & psGUIInteractMessage::NPCTALK)
        msgtext.Append(" NPCTALK");
    if (options & psGUIInteractMessage::VIEWSTATS)
        msgtext.Append(" VIEWSTATS");
    if (options & psGUIInteractMessage::DISMISS)
        msgtext.Append(" DISMISS");
    if (options & psGUIInteractMessage::MARRIAGE)
        msgtext.Append(" MARRIAGE");
    if (options & psGUIInteractMessage::DIVORCE)
        msgtext.Append(" DIVORCE");
    if (options & psGUIInteractMessage::ENTER)
        msgtext.Append(" ENTER");
    if (options & psGUIInteractMessage::ENTERLOCKED)
        msgtext.Append(" ENTERLOCKED");

    return msgtext;
}


//---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psMapActionMessage,MSGTYPE_MAPACTION);

psMapActionMessage::psMapActionMessage( uint32_t clientnum, uint32_t cmd, const char *xml )
{
    msg = new MsgEntry(sizeof(uint32_t) + strlen( xml ) + 1);

    msg->SetType( MSGTYPE_MAPACTION );
    msg->clientnum      = clientnum;

    msg->Add( cmd );
    msg->Add( xml );

    // Sets valid flag based on message overrun state
    valid = !(msg->overrun);
}

psMapActionMessage::psMapActionMessage( MsgEntry *message )
{
    if ( !message )
        return;

    command   = message->GetUInt32();
    actionXML = message->GetStr();

    // Sets valid flag based on message overrun state
    valid = !(message->overrun);
}

csString psMapActionMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext,cmd;
    cmd = "UNKOWN";
    switch (command)
    {
    case QUERY:         cmd = "QUERY"; break;
    case NOT_HANDLED:   cmd = "NOT_HANDLED"; break;
    case SAVE:          cmd = "SAVE"; break;
    case LIST:          cmd = "LIST"; break;
    case LIST_QUERY:    cmd = "LIST_QUERY"; break;
    case DELETE_ACTION: cmd = "DELETE_ACTION"; break;
    case RELOAD_CACHE:  cmd = "RELOAD_CACHE"; break;
    }


    msgtext.AppendFmt("Cmd: %s XML: '%s'",cmd.GetData(), actionXML.GetDataSafe());

    return msgtext;
}

//---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psModeMessage,MSGTYPE_MODE);

psModeMessage::psModeMessage (uint32_t client, uint32_t actorID, uint8_t mode, uint8_t stance)
{
    msg = new MsgEntry(sizeof(uint32_t) + 2*sizeof(uint8_t));

    msg->SetType(MSGTYPE_MODE);
    msg->clientnum      = client;

    msg->Add(actorID);
    msg->Add(mode);
    msg->Add(stance);

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}


psModeMessage::psModeMessage(MsgEntry *message)
{
    if ( !message )
        return;

    actorID = message->GetUInt32();
    mode    = message->GetUInt8();
    stance  = message->GetUInt8();

    // Sets valid flag based on message overrun state
    valid=!(message->overrun);
}

csString psModeMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("ActorID: %u Mode: %u Stance: %u", actorID, mode, stance);

    return msgtext;
}


//---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psMoveLockMessage,MSGTYPE_MOVELOCK);

psMoveLockMessage::psMoveLockMessage (uint32_t client, bool lock)
{
    msg = new MsgEntry(sizeof(uint8_t));

    msg->SetType(MSGTYPE_MOVELOCK);
    msg->clientnum      = client;

    msg->Add(lock);

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}


psMoveLockMessage::psMoveLockMessage(MsgEntry *message )
{
    if ( !message )
        return;

    locked = message->GetBool();

    // Sets valid flag based on message overrun state
    valid=!(message->overrun);
}

csString psMoveLockMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Locked: %s",locked?"true":"false");

    return msgtext;
}


//---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psWeatherMessage,MSGTYPE_WEATHER);

psWeatherMessage::psWeatherMessage(uint32_t client, int time )
{
    msg = new MsgEntry( sizeof(uint32_t) + sizeof(uint8_t) );

    msg->SetType(MSGTYPE_WEATHER);
    msg->clientnum      = client;

    msg->Add((uint8_t)DAYNIGHT);
    msg->Add((uint32_t)time);

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psWeatherMessage::psWeatherMessage(uint32_t client, psWeatherMessage::NetWeatherInfo info, uint clientnum )
{
    size_t size =
        sizeof(uint8_t) +        // type
        strlen(info.sector) + 1; // sector

    uint8_t type = (uint8_t)WEATHER;
    if (info.has_downfall)
    {
        if (info.downfall_is_snow)
        {
            type |= (uint8_t)SNOW;
        }
        else
        {
            type |= (uint8_t)RAIN;
        }
        size += sizeof(uint32_t) * 2;
    }
    if (info.has_fog)
    {
        type |= (uint8_t)FOG;
        size += sizeof(uint32_t) * 5;
    }
    if (info.has_lightning)
    {
        type |= (uint8_t)LIGHTNING;
    }

    msg = new MsgEntry( size );

    msg->SetType(MSGTYPE_WEATHER);
    msg->clientnum      = client;

    msg->Add(type);
    msg->Add(info.sector);
    if (info.has_downfall)
    {
        msg->Add((uint32_t)info.downfall_drops);
        msg->Add((uint32_t)info.downfall_fade);
    }
    if (info.has_fog)
    {
        msg->Add((uint32_t)info.fog_density);
        msg->Add((uint32_t)info.fog_fade);
        msg->Add((uint32_t)info.r);
        msg->Add((uint32_t)info.g);
        msg->Add((uint32_t)info.b);
    }

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psWeatherMessage::psWeatherMessage(MsgEntry *message )
{
    if ( !message )
        return;

    type   = message->GetUInt8();
    if (type == DAYNIGHT)
        time = (int)message->GetUInt32();
    else
    {
        if (type & (uint8_t)SNOW)
        {
            weather.has_downfall = true;
            weather.downfall_is_snow = true;
        } else if (type & (uint8_t)RAIN)
        {
            weather.has_downfall = true;
            weather.downfall_is_snow = false;
        } else
        {
            weather.has_downfall = false;
            weather.downfall_is_snow = false;
        }
        if (type & (uint8_t)FOG)
        {
            weather.has_fog = true;
        }
        else
        {
            weather.has_fog = false;
        }
        if (type & (uint8_t)LIGHTNING)
        {
            weather.has_lightning = true;
        }
        else
        {
            weather.has_lightning = false;
        }

        type = WEATHER;

        weather.sector  = message->GetStr();
        if (weather.has_downfall)
        {
            weather.downfall_drops   = (int)message->GetUInt32();
            weather.downfall_fade    = (int)message->GetUInt32();
        }
        else
        {
            weather.downfall_drops = 0;
            weather.downfall_fade = 0;
        }
        if (weather.has_fog)
        {
            weather.fog_density = (int)message->GetUInt32();
            weather.fog_fade    = (int)message->GetUInt32();
            weather.r           = (int)message->GetUInt32();
            weather.g           = (int)message->GetUInt32();
            weather.b           = (int)message->GetUInt32();
        }
        else
        {
            weather.fog_density = 0;
            weather.fog_fade = 0;
            weather.r = weather.g = weather.b = 0;
        }

     }

    // Sets valid flag based on message overrun state
    valid=!(message->overrun);
}

csString psWeatherMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    if (type == DAYNIGHT)
    {
        msgtext.AppendFmt("Type: DAYNIGHT Time: %u",time);
    }
    else
    {
        msgtext.AppendFmt("Type: WEATHER( ");
        if (weather.has_downfall)
        {
            msgtext.AppendFmt("DOWNFALL");
        }
        if (weather.downfall_is_snow)
        {
            msgtext.AppendFmt(" SNOW");
        }
        if (weather.has_fog)
        {
            msgtext.AppendFmt(" FOG");
        }
        if (weather.has_lightning)
        {
            msgtext.AppendFmt(" LIGHTNING");
        }

        msgtext.AppendFmt(" ) Sector: '%s'",weather.sector.GetDataSafe());

        if (weather.has_downfall)
        {
            msgtext.AppendFmt(" DownfallDrops: %d DownfallFade: %d",
                              weather.downfall_drops,weather.downfall_fade);
        }
        if (weather.has_fog)
        {
            msgtext.AppendFmt(" FogDensity: %d FogFade: %d RGB: (%d,%d,%d)",
                              weather.fog_density,weather.fog_fade,
                              weather.r,weather.g,weather.b);
        }
    }

    return msgtext;
}


//---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psGUIInventoryMessage,MSGTYPE_GUIINVENTORY);

psGUIInventoryMessage::psGUIInventoryMessage(uint8_t command, uint32_t size )
{
    msg = new MsgEntry( sizeof(uint8_t)  + size );

    msg->SetType(MSGTYPE_GUIINVENTORY);
    msg->clientnum      = 0;

    msg->Add(command);

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}


psGUIInventoryMessage::psGUIInventoryMessage(MsgEntry *message)
{
    if (!message)
        return;

    command = message->GetUInt8();

    switch ( command )
    {
        case LIST:
        case UPDATE_LIST:
        {
            totalItems = message->GetUInt32();
            if (command == UPDATE_LIST)
                totalEmptiedSlots = message->GetUInt32();
            maxWeight = message->GetFloat();
            for ( size_t x = 0; x < totalItems; x++ )
            {
                ItemDescription item;
                item.name          = message->GetStr();
                item.container     = message->GetUInt32();
                item.slot          = message->GetUInt32();
                item.stackcount    = message->GetUInt32();
                item.weight        = message->GetFloat();
                item.size          = message->GetFloat();
                item.iconImage     = message->GetStr();
                item.purifyStatus  = message->GetUInt8();
                items.Push(item);
            }
            if (command == UPDATE_LIST)
            {
                for ( size_t x = 0; x < totalEmptiedSlots; x++ )
                {
                   ItemDescription emptied;
                   emptied.container = message->GetUInt32();
                   emptied.slot = message->GetUInt32();
                   items.Push(emptied);
                }
            }

            money = psMoney(message->GetStr());
            break;
        }
    }
    // Sets valid flag based on message overrun state
    valid=!(message->overrun);
}


psGUIInventoryMessage::psGUIInventoryMessage(uint32_t clientnum,
                                             uint8_t command,
                                             uint32_t totalItems,
                                             uint32_t totalEmptiedSlots,
                                             float maxWeight  )
{
    msg = new MsgEntry( 10000 );
    msg->SetType(MSGTYPE_GUIINVENTORY);
    msg->clientnum      = clientnum;

    msg->Add(command);
    msg->Add(totalItems );
    if (command == UPDATE_LIST)
        msg->Add(totalEmptiedSlots);
    msg->Add( maxWeight );

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

void psGUIInventoryMessage::AddItem( const char* name,
                                     int containerID,
                                     int slotID,
                                     int stackcount,
                                     float weight,
                                     float size,
                                     const char* icon,
                                     int purifyStatus )
{
    msg->Add(name);
    msg->Add((uint32_t)containerID);
    msg->Add((uint32_t)slotID);

    msg->Add((uint32_t)stackcount);
    msg->Add(weight);
    msg->Add(size);

    msg->Add(icon);

    msg->Add((uint8_t)purifyStatus);

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

void psGUIInventoryMessage::AddMoney( const psMoney & money)
{
    msg->Add(money.ToString());

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

void psGUIInventoryMessage::AddEmptySlot( int containerID, int slotID )
{
    msg->Add((uint32_t)containerID);
    msg->Add((uint32_t)slotID);
}

csString psGUIInventoryMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Command: %d", command);
    if (command == LIST || command == UPDATE_LIST)
    {
        msgtext.AppendFmt(" Total Items: %zu Max Weight: %.3f", totalItems, maxWeight);

        msgtext.AppendFmt(" List: ");
        for ( size_t x = 0; x < totalItems; x++ )
        {
            msgtext.AppendFmt("%d x '%s' ", items[x].stackcount, items[x].name.GetDataSafe());

#ifdef FULL_DEBUG_DUMP
            msgtext.AppendFmt("(Container: %d Slot: %d Weight: %.2f Size: %.2f Icon: '%s' Purified: %d), ",
            items[x].container,
            items[x].slot,
            items[x].weight,
            items[x].size,
            items[x].iconImage.GetDataSafe(),
            items[x].purifyStatus);
#endif
        }
        if (command == UPDATE_LIST)
        {
            msgtext.AppendFmt(" Total Emptied Slots: %zu", totalEmptiedSlots);

#ifdef FULL_DEBUG_DUMP
            for ( size_t x = 0; x < totalEmptiedSlots; x++ )
            {
                msgtext.AppendFmt(" (Container: %d Slot: %d),",
                items[totalItems+x].container, items[totalItems+x].slot);
            }
#endif
        }
	msgtext.AppendFmt(" Money: %s", money.ToUserString().GetDataSafe());
    }

    return msgtext;
}

//---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psNewSectorMessage,MSGTYPE_NEWSECTOR);

// Leaving this one marshalled the old way.  This message is NEVER sent on
// the network.
psNewSectorMessage::psNewSectorMessage(const csString & oldSector, const csString & newSector, csVector3 pos)
{
    msg = new MsgEntry( 1024 );

    msg->SetType(MSGTYPE_NEWSECTOR);
    msg->clientnum      = 0; // msg never sent on network. client only

    msg->Add(oldSector);
    msg->Add(newSector);
    msg->Add(pos.x);
    msg->Add(pos.y);
    msg->Add(pos.z);

    // Since this message is never sent, we don't adjust the valid flag
}


psNewSectorMessage::psNewSectorMessage( MsgEntry *message )
{
    if ( !message )
        return;

    oldSector = message->GetStr();
    newSector = message->GetStr();
    pos.x = message->GetFloat();
    pos.y = message->GetFloat();
    pos.z = message->GetFloat();

    // Since this message is never sent, we don't adjust the valid flag
}

csString psNewSectorMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Old sector: '%s' New sector: '%s' Pos: (%.3f,%.3f,%.3f)",
            oldSector.GetDataSafe(), newSector.GetDataSafe(), pos.x, pos.y, pos.z);

    return msgtext;
}

//--------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psLootItemMessage,MSGTYPE_LOOTITEM);

psLootItemMessage::psLootItemMessage(int client,int entity,int item,int action)
{
    msg = new MsgEntry(2*sizeof(int32_t) + sizeof(uint8_t) );

    msg->SetType(MSGTYPE_LOOTITEM);
    msg->clientnum      = client;

    msg->Add( (int32_t) entity);
    msg->Add( (int32_t) item);
    msg->Add( (uint8_t) action);
    valid=!(msg->overrun);
}

psLootItemMessage::psLootItemMessage(MsgEntry* message)
{
    entity   = message->GetInt32();
    lootitem = message->GetInt32();
    lootaction = message->GetUInt8();
    valid=!(message->overrun);
}

csString psLootItemMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Entity: %d Item: %d Action: %d", entity, lootitem, lootaction);

    return msgtext;
}

//--------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psLootMessage,MSGTYPE_LOOT);

psLootMessage::psLootMessage()
{
    msg = NULL;
}

psLootMessage::psLootMessage(MsgEntry* msg)
{
    entity_id = msg->GetInt32();
    lootxml = msg->GetStr();
    valid=!(msg->overrun);
}

void psLootMessage::Populate(int entity,csString& lootstr, int cnum)
{
    msg = new MsgEntry(sizeof(int32_t) + lootstr.Length() + 1);

    msg->SetType(MSGTYPE_LOOT);
    msg->clientnum      = cnum;

    msg->Add( (int32_t) entity);
    msg->Add( lootstr );
    valid=!(msg->overrun);
}

csString psLootMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Entity: %d ", entity_id);
#ifdef FULL_DEBUG_DUMP
    msgtext.AppendFmt("XML: '%s'", lootxml.GetDataSafe());
#endif

    return msgtext;
}

//--------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psQuestListMessage,MSGTYPE_QUESTLIST);

psQuestListMessage::psQuestListMessage()
{
    msg = NULL;
    valid=false;
}

psQuestListMessage::psQuestListMessage(MsgEntry* msg)
{
    questxml = msg->GetStr();
    valid=!(msg->overrun);
}

void psQuestListMessage::Populate(csString& queststr, int cnum)
{
    msg = new MsgEntry(sizeof(int) + queststr.Length() + 1);

    msg->SetType(MSGTYPE_QUESTLIST);
    msg->clientnum      = cnum;

    msg->Add( queststr );
    valid=!(msg->overrun);
}

csString psQuestListMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("XML: '%s'", questxml.GetDataSafe());

    return msgtext;
}

//--------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psQuestInfoMessage,MSGTYPE_QUESTINFO);

psQuestInfoMessage::psQuestInfoMessage(int cnum, int cmd, int id, const char *name, const char *info)
{
    if (name)
    {
        csString escpxml_info = EscpXML(info);
        xml.Format("<QuestNotebook><Description text=\"%s\"/></QuestNotebook>",escpxml_info.GetData());
    }
    else
        xml = "";

    msg = new MsgEntry(sizeof(int32_t) + sizeof(uint8_t) + xml.Length() + 1);

    msg->SetType(MSGTYPE_QUESTINFO);
    msg->clientnum = cnum;

    msg->Add( (uint8_t) cmd );

    if (cmd == CMD_QUERY || cmd == CMD_DISCARD)
    {
        msg->Add( (int32_t) id );
    }
    else if (cmd == CMD_INFO)
    {
        msg->Add( xml );
    }
    msg->ClipToCurrentSize();

    valid=!(msg->overrun);
}

psQuestInfoMessage::psQuestInfoMessage(MsgEntry* msg)
{
    command = msg->GetUInt8();
    if (command == CMD_QUERY || command == CMD_DISCARD)
        id = msg->GetInt32();
    else if (command == CMD_INFO)
        xml = msg->GetStr();
    valid=!(msg->overrun);
}


csString psQuestInfoMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Command: %d", command);
    if (command == CMD_QUERY || command == CMD_DISCARD)
        msgtext.AppendFmt(" Id: %d", id);
    else if (command == CMD_INFO)
        msgtext.AppendFmt(" XML: '%s'", xml.GetDataSafe());

    return msgtext;
}

//--------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psOverrideActionMessage,MSGTYPE_OVERRIDEACTION);

psOverrideActionMessage::psOverrideActionMessage(int client,int entity,const char *action, int duration)
{
    size_t strlength = 0;
    if (action)
        strlength = strlen(action) + 1;

    msg = new MsgEntry( sizeof(entity) +
                        strlength +
                        sizeof(duration) );

    msg->SetType(MSGTYPE_OVERRIDEACTION);
    msg->clientnum  = client;

    msg->Add( (uint32_t)entity );
    msg->Add( action );
    msg->Add( (uint32_t)duration );

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psOverrideActionMessage::psOverrideActionMessage(MsgEntry* message)
{
    if ( !message )
        return;

    entity_id = message->GetUInt32();
    action    = message->GetStr();
    duration  = message->GetUInt32();

    // Sets valid flag based on message overrun state
    valid=!(message->overrun);
}

csString psOverrideActionMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Entity: %d Action: '%s' Duration: %d", entity_id, action.GetDataSafe(), duration);

    return msgtext;
}

//--------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psEquipmentMessage,MSGTYPE_EQUIPMENT);

psEquipmentMessage::psEquipmentMessage( uint32_t clientNum,
                                        PS_ID actorid,
                                        uint8_t type,
                                        int slot,
                                        csString& meshName,
                                        csString& part,
                                        csString& texture,
                                        csString& partMesh)
{
    msg = new MsgEntry( sizeof(uint32_t)*2 +
                        sizeof(uint8_t) +
                        meshName.Length()+1 +
                        part.Length()+1 +
                        texture.Length()+1 +
                        partMesh.Length()+1);

    msg->SetType(MSGTYPE_EQUIPMENT);
    msg->clientnum  = clientNum;

    msg->Add( type );
    msg->Add( (uint32_t)actorid );
    msg->Add( (uint32_t)slot );
    msg->Add( meshName );
    msg->Add( part );
    msg->Add( texture );
    msg->Add( partMesh );

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psEquipmentMessage::psEquipmentMessage( MsgEntry* message )
{
    if ( !message )
        return;

    type   = message->GetUInt8();
    player = message->GetUInt32();
    slot   = message->GetUInt32();
    mesh   = message->GetStr();
    part   = message->GetStr();
    texture = message->GetStr();
    partMesh = message->GetStr();

    // Sets valid flag based on message overrun state
    valid=!(message->overrun);
}

csString psEquipmentMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Type: %d",type);
    msgtext.AppendFmt(" Player: %d",player);
    msgtext.AppendFmt(" Slot: %d",slot);
    msgtext.AppendFmt(" Mesh: '%s'",mesh.GetDataSafe());
    msgtext.AppendFmt(" Part: '%s'",part.GetDataSafe());
    msgtext.AppendFmt(" Texture: '%s'",texture.GetDataSafe());
    msgtext.AppendFmt(" PartMesh: '%s'",partMesh.GetDataSafe());

    return msgtext;
}

//--------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psGUIMerchantMessage,MSGTYPE_GUIMERCHANT);

psGUIMerchantMessage::psGUIMerchantMessage( uint8_t command,
                                            csString commandData)
{
    msg = new MsgEntry( sizeof(uint8_t) +
                        commandData.Length() +
                        1);

    msg->SetType(MSGTYPE_GUIMERCHANT);
    msg->clientnum  = 0;

    msg->Add( command );
    msg->Add( commandData );

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psGUIMerchantMessage::psGUIMerchantMessage( uint32_t clientNum,
                                            uint8_t command,
                                            csString commandData)
{
    msg = new MsgEntry( sizeof(uint8_t) +
                        commandData.Length() +
                        1);

    msg->SetType(MSGTYPE_GUIMERCHANT);
    msg->clientnum  = clientNum;

    msg->Add( command );
    msg->Add( commandData );

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psGUIMerchantMessage::psGUIMerchantMessage( MsgEntry* message )
{
    if ( !message )
        return;

    command   = message->GetUInt8();
    commandData = message->GetStr();

    // Sets valid flag based on message overrun state
    valid=!(message->overrun);
}

csString psGUIMerchantMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;


    msgtext.AppendFmt("Command: %d", command);
#ifdef FULL_DEBUG_DUMP
    msgtext.AppendFmt(" Data: '%s'", commandData.GetDataSafe());
#endif

    return msgtext;
}

//--------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psGUIGroupMessage,MSGTYPE_GUIGROUP);

psGUIGroupMessage::psGUIGroupMessage( uint8_t command,
                                      csString commandData)
{
    msg = new MsgEntry( sizeof(uint8_t) +
                        commandData.Length() +
                        1);

    msg->SetType(MSGTYPE_GUIGROUP);
    msg->clientnum  = 0;

    msg->Add( command );
    msg->Add( commandData );

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psGUIGroupMessage::psGUIGroupMessage( uint32_t clientNum,
                                      uint8_t command,
                                      csString commandData)
{
    msg = new MsgEntry( sizeof(uint8_t) +
                        commandData.Length() +
                        1);

    msg->SetType(MSGTYPE_GUIGROUP);
    msg->clientnum  = clientNum;

    msg->Add( command );
    msg->Add( commandData );

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psGUIGroupMessage::psGUIGroupMessage( MsgEntry* message )
{
    if ( !message )
        return;

    command   = message->GetUInt8();
    commandData = message->GetStr();

    // Sets valid flag based on message overrun state
    valid=!(message->overrun);
}

csString psGUIGroupMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Command: %d", command);
#ifdef FULL_DEBUG_DUMP
    msgtext.AppendFmt(" Data: '%s'", commandData.GetDataSafe());
#endif

    return msgtext;
}

//--------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psSpellCancelMessage,MSGTYPE_SPELL_CANCEL);

csString psSpellCancelMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Spell cancel");

    return msgtext;
}

//--------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psSpellBookMessage,MSGTYPE_SPELL_BOOK);

psSpellBookMessage::psSpellBookMessage()
{
    msg = new MsgEntry();
    msg->clientnum = 0;
    msg->SetType(MSGTYPE_SPELL_BOOK);
    size = 0;
}

psSpellBookMessage::psSpellBookMessage( uint32_t client )
{
    this->client = client;
    size = 0;
}

psSpellBookMessage::psSpellBookMessage( MsgEntry* me )
{
    size_t length = me->GetUInt32();

    for ( size_t x = 0; x < length; x++ )
    {
        psSpellBookMessage::NetworkSpell ns;
        ns.name = me->GetStr();
        ns.description = me->GetStr();
        ns.way = me->GetStr();
        ns.realm = me->GetUInt32();
        ns.glyphs[0] = me->GetStr();
        ns.glyphs[1] = me->GetStr();
        ns.glyphs[2] = me->GetStr();
        ns.glyphs[3] = me->GetStr();

        spells.Push( ns );
    }

}

void psSpellBookMessage::AddSpell( csString& name, csString& description, csString& way, int realm,
                                   csString& glyph0, csString& glyph1, csString& glyph2, csString& glyph3 )
{
    size+=(uint32_t)(name.Length() + description.Length() + way.Length()+ sizeof(int)+7 +
    glyph0.Length()+glyph1.Length()+glyph2.Length()+glyph3.Length());

    psSpellBookMessage::NetworkSpell ns;
    ns.name = name;
    ns.description = description;
    ns.way = way;
    ns.realm = realm;
    ns.glyphs[0] = glyph0;
    ns.glyphs[1] = glyph1;
    ns.glyphs[2] = glyph2;
    ns.glyphs[3] = glyph3;

    spells.Push( ns );
}

void psSpellBookMessage::Construct()
{
    msg = new MsgEntry( size + sizeof(int) );
    msg->SetType(MSGTYPE_SPELL_BOOK);
    msg->clientnum = client;

    msg->Add( (uint32_t)spells.GetSize() );
    for ( size_t x = 0; x < spells.GetSize(); x++ )
    {
        msg->Add( spells[x].name );
        msg->Add( spells[x].description );
        msg->Add( spells[x].way );
        msg->Add( (uint32_t)spells[x].realm);
        msg->Add( spells[x].glyphs[0] );
        msg->Add( spells[x].glyphs[1] );
        msg->Add( spells[x].glyphs[2] );
        msg->Add( spells[x].glyphs[3] );
    }
}



csString psSpellBookMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    for ( size_t x = 0; x < spells.GetSize(); x++ )
    {
        msgtext.AppendFmt("Spell: '%s' Way: '%s' Realm: %d ",
            spells[x].name.GetDataSafe(),
            spells[x].way.GetDataSafe(),
            spells[x].realm);
        msgtext.AppendFmt("Glyphs: '%s', '%s', '%s', '%s' ",
            spells[x].glyphs[0].GetDataSafe(),
            spells[x].glyphs[1].GetDataSafe(),
            spells[x].glyphs[2].GetDataSafe(),
            spells[x].glyphs[3].GetDataSafe());
#ifdef FULL_DEBUG_DUMP
        msgtext.AppendFmt("Description: '%s'",
            spells[x].description.GetDataSafe());
#endif
    }

    return msgtext;
}

//--------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psPurifyGlyphMessage,MSGTYPE_PURIFY_GLYPH);

psPurifyGlyphMessage::psPurifyGlyphMessage( uint32_t glyphID )
{
    msg = new MsgEntry( sizeof(uint32_t) );
    msg->clientnum = 0;
    msg->SetType(MSGTYPE_PURIFY_GLYPH);
    msg->Add( glyphID );
}

psPurifyGlyphMessage::psPurifyGlyphMessage( MsgEntry* me )
{
    glyph = me->GetUInt32();
}

csString psPurifyGlyphMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Glyph: %d", glyph);

    return msgtext;
}

//--------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psSpellCastMessage,MSGTYPE_SPELL_CAST);

psSpellCastMessage::psSpellCastMessage( csString &spellName, float kFactor )
{
    msg = new MsgEntry( spellName.Length() + 1 + sizeof(float) );
    msg->clientnum = 0;
    msg->SetType(MSGTYPE_SPELL_CAST);
    msg->Add( spellName );
    msg->Add( kFactor );
}

psSpellCastMessage::psSpellCastMessage( MsgEntry* me )
{
    spell = me->GetStr();
    kFactor = me->GetFloat();
}

csString psSpellCastMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Spell: '%s' kFactor: %.3f", spell.GetDataSafe(), kFactor);

    return msgtext;
}

//--------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psGlyphAssembleMessage,MSGTYPE_GLYPH_ASSEMBLE);

psGlyphAssembleMessage::psGlyphAssembleMessage(  int slot0, int slot1, int slot2, int slot3, bool info)
{
    msg = new MsgEntry( sizeof(uint32_t) * 4 + sizeof(uint8_t) );
    msg->SetType(MSGTYPE_GLYPH_ASSEMBLE);
    msg->clientnum = 0;
    msg->Add( (uint32_t)slot0 );
    msg->Add( (uint32_t)slot1 );
    msg->Add( (uint32_t)slot2 );
    msg->Add( (uint32_t)slot3 );
	msg->Add( (uint8_t)info );
}

psGlyphAssembleMessage::psGlyphAssembleMessage( uint32_t client,
                                                csString name,
                                                csString image,
                                                csString description )
{
    msg = new MsgEntry( name.Length() + description.Length() + image.Length() + 3 );
    msg->SetType(MSGTYPE_GLYPH_ASSEMBLE);
    msg->clientnum = client;
    msg->Add( name );
    msg->Add( image );
    msg->Add( description );

}

psGlyphAssembleMessage::psGlyphAssembleMessage( MsgEntry* me )
{
    if (me->GetSize() == sizeof(uint32_t) * 4 + sizeof(uint8_t))
    {
        FromClient(me);
        msgFromServer = false;
    }
    else
    {
        FromServer(me);
        msgFromServer = true;
    }
}

void psGlyphAssembleMessage::FromClient( MsgEntry* me )
{
    glyphs[0] = me->GetUInt32();
    glyphs[1] = me->GetUInt32();
    glyphs[2] = me->GetUInt32();
    glyphs[3] = me->GetUInt32();
	info = me->GetBool();
}

void psGlyphAssembleMessage::FromServer( MsgEntry* me )
{
    name = me->GetStr();
    image = me->GetStr();
    description = me->GetStr();
}

csString psGlyphAssembleMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    if (msgFromServer)
    {
        msgtext.AppendFmt("Name: '%s' Image: '%s' Description: '%s'",
                name.GetDataSafe(), image.GetDataSafe(), description.GetDataSafe());
    }
    else
    {
        msgtext.AppendFmt("Glyphs: %d, %d, %d, %d", glyphs[0], glyphs[1], glyphs[2], glyphs[3]);
    }

    return msgtext;
}

//--------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psRequestGlyphsMessage,MSGTYPE_GLPYH_REQUEST);

psRequestGlyphsMessage::psRequestGlyphsMessage( uint32_t client )
{
    this->client = client;
    size = 0;
    if ( client == 0 )
    {
        msg = new MsgEntry();
        msg->SetType(MSGTYPE_GLPYH_REQUEST);
        msg->clientnum = client;
    }
}

psRequestGlyphsMessage::~psRequestGlyphsMessage()
{
}

void psRequestGlyphsMessage::AddGlyph( csString name, csString image, int purifiedStatus,
                                       int way, int statID )
{
    size+=name.Length() + image.Length() + sizeof(int)*4+2;

    psRequestGlyphsMessage::NetworkGlyph ng;
    ng.name = name;
    ng.image = image;
    ng.purifiedStatus = purifiedStatus;
    ng.way = way;
    ng.statID = statID;

    glyphs.Push( ng );
}


void psRequestGlyphsMessage::Construct()
{
    msg = new MsgEntry( size + sizeof(int) );
    msg->SetType(MSGTYPE_GLPYH_REQUEST);
    msg->clientnum = client;

    msg->Add( (uint32_t)glyphs.GetSize() );
    for ( size_t x = 0; x < glyphs.GetSize(); x++ )
    {
        msg->Add( glyphs[x].name );
        msg->Add( glyphs[x].image );
        msg->Add( glyphs[x].purifiedStatus );
        msg->Add( glyphs[x].way );
        msg->Add( glyphs[x].statID );
    }
}

psRequestGlyphsMessage::psRequestGlyphsMessage( MsgEntry* me )
{
    size_t length = me->GetUInt32();

    for ( size_t x = 0; x < length; x++ )
    {
        psRequestGlyphsMessage::NetworkGlyph ng;
        ng.name = me->GetStr();
        ng.image = me->GetStr();
        ng.purifiedStatus = me->GetUInt32();
        ng.way = me->GetUInt32();
        ng.statID = me->GetUInt32();

        glyphs.Push( ng );
    }
}

csString psRequestGlyphsMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    for ( size_t x = 0; x < glyphs.GetSize(); x++ )
    {
        msgtext.AppendFmt("Name: '%s' Image: '%s' Purified Status: %d Way: %d Stat Id: %d; ",
            glyphs[x].name.GetDataSafe(),
            glyphs[x].image.GetDataSafe(),
            glyphs[x].purifiedStatus,
            glyphs[x].way,
            glyphs[x].statID);
    }

    return msgtext;
}

//--------------------------------------------------------------------------
PSF_IMPLEMENT_MSG_FACTORY(psStopEffectMessage,MSGTYPE_EFFECT_STOP);

PSF_IMPLEMENT_MSG_FACTORY(psEffectMessage,MSGTYPE_EFFECT);

psEffectMessage::psEffectMessage(uint32_t clientNum, const csString &effectName,
                                 const csVector3 &effectOffset, uint32_t anchorID,
                                 uint32_t targetID, uint32_t uid)
{
    msg = new MsgEntry(effectName.Length() + sizeof(csVector3)
                + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) + 1);

    msg->SetType(MSGTYPE_EFFECT);
    msg->clientnum = clientNum;

    msg->Add(effectName);
    msg->Add(effectOffset.x);
    msg->Add(effectOffset.y);
    msg->Add(effectOffset.z);
    msg->Add(anchorID);
    msg->Add(targetID);
    msg->Add((uint32_t)0);
    msg->Add((uint32_t)uid);

    valid = !(msg->overrun);
}

psEffectMessage::psEffectMessage(uint32_t clientNum, const csString &effectName,
                                 const csVector3 &effectOffset, uint32_t anchorID,
                                 uint32_t targetID, uint32_t castDuration,uint32_t uid)
{
    msg = new MsgEntry(effectName.Length() + sizeof(csVector3)
                + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) + 1);

    msg->SetType(MSGTYPE_EFFECT);
    msg->clientnum = clientNum;

    msg->Add(effectName);
    msg->Add(effectOffset.x);
    msg->Add(effectOffset.y);
    msg->Add(effectOffset.z);
    msg->Add(anchorID);
    msg->Add(targetID);
    msg->Add(castDuration);
    msg->Add(uid);

    valid = !(msg->overrun);
}

psEffectMessage::psEffectMessage(uint32_t clientNum, const csString &effectName,
                                 const csVector3 &effectOffset, uint32_t anchorID,
                                 uint32_t targetID, csString &effectText, uint32_t uid)
{
    msg = new MsgEntry(effectName.Length() + sizeof(csVector3)
                + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(effectText) + sizeof(uint32_t) + 1);

    msg->SetType(MSGTYPE_EFFECT);
    msg->clientnum = clientNum;

    msg->Add(effectName);
    msg->Add(effectOffset.x);
    msg->Add(effectOffset.y);
    msg->Add(effectOffset.z);
    msg->Add(anchorID);
    msg->Add(targetID);
    msg->Add(effectText);
    msg->Add(uid);

    valid = !(msg->overrun);
}

psEffectMessage::psEffectMessage(MsgEntry* message)
{
    if (!message)
        return;

    name = message->GetStr();
    offset.x = message->GetFloat();
    offset.y = message->GetFloat();
    offset.z = message->GetFloat();
    anchorID = message->GetUInt32();
    targetID = message->GetUInt32();
    castDuration = message->GetUInt32();
    uid = message->GetUInt32();

    valid = !(message->overrun);
}

csString psEffectMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Name: '%s' Offset: (%.3f, %.3f, %.3f) Anchor: %d Target: %d Duration: %d UID: %d",
            name.GetDataSafe(),
            offset.x, offset.y, offset.z,
            anchorID,
            targetID,
            castDuration,
            uid);

    return msgtext;
}

//---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psGUITargetUpdateMessage,MSGTYPE_GUITARGETUPDATE);

psGUITargetUpdateMessage::psGUITargetUpdateMessage(uint32_t client_num,
                                                   PS_ID target_id)
{
    msg = new MsgEntry(sizeof(uint32_t) + sizeof(PS_ID));

    msg->SetType(MSGTYPE_GUITARGETUPDATE);
    msg->clientnum      = client_num;

    msg->Add((uint32_t)target_id);

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psGUITargetUpdateMessage::psGUITargetUpdateMessage(MsgEntry *message)
{
    if ( !message )
        return;

    targetID = message->GetUInt32();

    // Sets valid flag based on message overrun state
    valid=!(message->overrun);
}

csString psGUITargetUpdateMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Target ID: %d", targetID);

    return msgtext;
}

//---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psMsgStringsMessage,MSGTYPE_MSGSTRINGS);

#define COMPRESSION_BUFFSIZE  MAX_MESSAGE_SIZE/2
#define PACKING_BUFFSIZE  COMPRESSION_BUFFSIZE*3

psMsgStringsMessage::psMsgStringsMessage(uint32_t clientnum, csStringHash *strings)
{
    msgstrings = 0;

    msg = new MsgEntry(MAX_MESSAGE_SIZE);

    char buff1[PACKING_BUFFSIZE];      // Holds packed strings
    char buff2[COMPRESSION_BUFFSIZE];  // Holds compressed data
    size_t length = 0;

    msg->SetType(MSGTYPE_MSGSTRINGS);
    msg->clientnum      = clientnum;

    msg->Add((uint32_t)strings->GetSize());

    csStringHash::GlobalIterator it = strings->GetIterator();
    while (it.HasNext())
    {
        const char* string;
        csStringID id = it.Next(string);

        // Pack ID
        uint32 *p = (uint32*)(buff1+length);
        *p = csLittleEndian::UInt32(id);
        length += sizeof(uint32);

        // Pack string
        strcpy(buff1+length, string);
        length += strlen(string)+1;
    }

    // Ready
    z_stream z;
    z.zalloc = NULL;
    z.zfree = NULL;
    z.opaque = NULL;
    z.next_in = (z_Byte*)buff1;
    z.avail_in = (uInt)length;
    z.next_out = (z_Byte*)buff2;
    z.avail_out = COMPRESSION_BUFFSIZE;

    // Set
	int err = deflateInit(&z,Z_BEST_COMPRESSION);
    CS_ASSERT(err == Z_OK);

    // Go
	err = deflate(&z,Z_FINISH);
    CS_ASSERT(err == Z_STREAM_END);
    deflateEnd(&z);

    // Write the data
    msg->Add(buff2, z.total_out);

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);

    if (valid)
        msg->ClipToCurrentSize();
/*
    printf("%u -> %u\n%d(%f%%) saved\n",
           length, z.total_out,
           int(length)-int(z.total_out),
           1.0f-(float(z.total_out)/float(length)));
*/
}

psMsgStringsMessage::psMsgStringsMessage(MsgEntry *message)
{
    if (!message)
        return;

    nstrings = message->GetUInt32();

    /* Somewhat arbitrary max size - currently the maximum message size of 2048
     *  So the maximum number of strings that could be in a message is 2048-sizeof(lengthtype)
     *  We cannot allow this to be any value, a malicious client could sent maxuint
     *  and cause the new call to fail below, or just an obscene value and cause memory
     *  to thrash.
     */
    if (nstrings<1 || nstrings>2044)
    {
        printf("Threw away strings message, invalid number of strings %d\n",(int)nstrings);
        valid=false;
        return;
    }

    // Read the data
    uint32_t length = 0;
    const void* data = message->GetData(&length);

    if (message->overrun)
    {
        valid=false;
        return;
    }

    char buff[PACKING_BUFFSIZE];  // Holds packed strings after decompression

    // Ready
    z_stream z;
    z.zalloc = NULL;
    z.zfree = NULL;
    z.opaque = NULL;
    z.next_in = (z_Byte*)data;
    z.avail_in = (uInt)length;
    z.next_out = (z_Byte*)buff;
    z.avail_out = PACKING_BUFFSIZE;

    // Set
    if (inflateInit(&z) != Z_OK)
    {
        valid=false;
        return;
    }

    // Go
    int res = inflate(&z,Z_FINISH);
    inflateEnd(&z);

    if (res != Z_STREAM_END)
    {
        valid=false;
        return;
    }

    size_t pos = 0;
    msgstrings = new csStringHash(nstrings);
    for (uint32_t i=0; i<nstrings; i++)
    {
        // Unpack ID
        uint32 *p = (uint32*)(buff+pos);
        uint32 id = csLittleEndian::UInt32(*p);
        pos += sizeof(uint32);

        // Unpack string
        const char* string = buff+pos;
        pos += strlen(string)+1;

        // csStringHash::Register() cannot handle NULL pointers
        if (string[0] == '\0')
            msgstrings->Register("",id);
        else
            msgstrings->Register(string,id);

        if (pos > z.total_out)
        {
            delete msgstrings;
            msgstrings=NULL;
            valid=false;
            return;
        }
    }
}

csString psMsgStringsMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("No. Strings: %d ", nstrings);

#ifdef FULL_DEBUG_DUMP
    uint32_t s = 1;

    csStringHash::GlobalIterator it = msgstrings->GetIterator();
    while (it.HasNext())
    {
        const char* string;
        it.Next(string);
        msgtext.AppendFmt("String %d: '%s', ", s++, string);
    }
#endif

    return msgtext;
}

//---------------------------------------------------------------------------
#if 0
PSF_IMPLEMENT_MSG_FACTORY(psCharacterDataMessage,MSGTYPE_CHARACTERDATA);

psCharacterDataMessage::psCharacterDataMessage(uint32_t clientnum,
                                               csString fullname,
                                               csString race_name,
                                               csString mesh_name,
                                               csString traits,
                                               csString equipment)
{
    msg = new MsgEntry(fullname.Length() + race_name.Length()
                       + mesh_name.Length() + traits.Length()
                       + equipment.Length() + 5 );

    msg->SetType(MSGTYPE_CHARACTERDATA);
    msg->clientnum      = clientnum;

    msg->Add( (const char*) fullname);
    msg->Add( (const char*) race_name);
    msg->Add( (const char*) mesh_name);
    msg->Add( (const char*) traits);
    msg->Add( (const char*) equipment);

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psCharacterDataMessage::psCharacterDataMessage(MsgEntry *message)
{
    fullname = message->GetStr();
    race_name = message->GetStr();
    mesh_name = message->GetStr();
    traits = message->GetStr();
    equipment = message->GetStr();

    // Sets valid flag based on message overrun state
    valid=!(message->overrun);
}

csString psCharacterDataMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Name: '%s'",fullname.GetData());
    msgtext.AppendFmt(" Race: '%s'",race_name.GetData());
    msgtext.AppendFmt(" Mesh: '%s'",mesh_name.GetData());
    msgtext.AppendFmt(" Traits: '%s'",traits.GetData());
    msgtext.AppendFmt(" Equipment: '%s'",equipment.GetData());

    return msgtext;
}
#endif

//---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psCombatEventMessage,MSGTYPE_COMBATEVENT);

psCombatEventMessage::psCombatEventMessage(uint32_t clientnum,
                                           int event_type,
                                           uint32_t attacker,
                                           uint32_t target,
                                           int targetLocation,
                                           float damage,
                                           csStringID attack_anim,
                                           csStringID defense_anim)
{
    switch (event_type)
    {
        case COMBAT_DODGE:
        case COMBAT_BLOCK:
        case COMBAT_MISS:
        case COMBAT_DEATH:

            msg = new MsgEntry(sizeof(uint8_t) + 4 * sizeof(uint32_t) + sizeof(int8_t) );

            msg->SetType(MSGTYPE_COMBATEVENT);
            msg->clientnum      = clientnum;

            msg->Add( (uint8_t)  event_type);
            msg->Add( (uint32_t) attack_anim );
            msg->Add( (uint32_t) defense_anim );

            msg->Add( (uint32_t) attacker );
            msg->Add( (uint32_t) target );
            msg->Add( (int8_t)   targetLocation );

            // Sets valid flag based on message overrun state
            valid=!(msg->overrun);
            break;

        case COMBAT_DAMAGE:
        case COMBAT_DAMAGE_NEARLY_DEAD:

            msg = new MsgEntry(sizeof(uint8_t) + 4 * sizeof(uint32_t) + sizeof(int8_t) + sizeof(float) );

            msg->SetType(MSGTYPE_COMBATEVENT);
            msg->clientnum      = clientnum;

            msg->Add( (uint8_t)  event_type);
            msg->Add( (uint32_t) attack_anim );
            msg->Add( (uint32_t) defense_anim );

            msg->Add( (uint32_t) attacker );
            msg->Add( (uint32_t) target );
            msg->Add( (int8_t)   targetLocation );
            msg->Add( (float)    damage );

            // Sets valid flag based on message overrun state
            valid=!(msg->overrun);
            break;
        default:
            Warning2(LOG_NET,"Attempted to construct a psCombatEventMessage with unknown event type %d.\n",event_type);
            valid=false;
            break;
    }

}

void psCombatEventMessage::SetClientNum(int cnum)
{
    msg->clientnum = cnum;
}

psCombatEventMessage::psCombatEventMessage(MsgEntry *message)
{
    event_type  = message->GetUInt8();
    attack_anim = message->GetUInt32();
    defense_anim = message->GetUInt32();
    attacker_id = message->GetUInt32();
    target_id = message->GetUInt32();
    target_location = message->GetInt8();

    if (event_type == COMBAT_DAMAGE || event_type == COMBAT_DAMAGE_NEARLY_DEAD)
    {
        damage = message->GetFloat();
    }
    else
        damage = 0;

    // Sets valid flag based on message overrun state
    valid=!(message->overrun);
}


csString psCombatEventMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Event Type: %d Attack Anim: %lu Defense Anim: %lu Attacker: %d Target: %d Location: %d",
            event_type, attack_anim, defense_anim, attacker_id, target_id, target_location);
    if (event_type == COMBAT_DAMAGE)
    {
        msgtext.AppendFmt(" Damage: %.3f", damage);
    }

    return msgtext;
}

//---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psSoundEventMessage,MSGTYPE_SOUND_EVENT);

psSoundEventMessage::psSoundEventMessage(uint32_t clientnum, uint32_t type)
{
    msg = new MsgEntry(1 * sizeof(int) );
    msg->SetType(MSGTYPE_SOUND_EVENT);
    msg->clientnum = clientnum;
    msg->Add((uint32_t)type);
    valid=!(msg->overrun);
}

psSoundEventMessage::psSoundEventMessage(MsgEntry* msg)
{
    type = msg->GetInt32();
    valid=!(msg->overrun);
}

csString psSoundEventMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Type: %d", type);

    return msgtext;
}

//---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psStatDRMessage,MSGTYPE_STATDRUPDATE);

psStatDRMessage::psStatDRMessage(uint32_t clientnum, PS_ID eid, csArray<float> fVitals, csArray<int32_t> iVitals, uint8_t version, int flags)
{
    msg = new MsgEntry(2*sizeof(uint32_t) + sizeof(float) * fVitals.GetSize() + sizeof(int32_t) * iVitals.GetSize() + sizeof(uint8_t), PRIORITY_LOW); 

    msg->clientnum = clientnum;
    msg->SetType(MSGTYPE_STATDRUPDATE);

    msg->Add((uint32_t) eid);
    msg->Add((uint32_t) flags);
    
    for (size_t i = 0; i < fVitals.GetSize(); i++)
        msg->Add(fVitals[i]);
    for (size_t i = 0; i < iVitals.GetSize(); i++)
        msg->Add(iVitals[i]);
    
    msg->Add((uint8_t) version);
    
    msg->ClipToCurrentSize();

    valid = !(msg->overrun);
}

/** Send a request to the server for a full stat update. */
psStatDRMessage::psStatDRMessage()
{
    msg = new MsgEntry(PRIORITY_HIGH);

    msg->clientnum = 0;
    msg->SetType(MSGTYPE_STATDRUPDATE);
    valid = !(msg->overrun);
}

psStatDRMessage::psStatDRMessage(MsgEntry* me)
{
    /* We handle PS_ID as a uint32 - which it is at this time.  If it ever changes we 
     *  will need to adjust.
     */
    CS_ASSERT(sizeof(PS_ID) == sizeof(uint32_t));

    entityid=me->GetUInt32();
    statsDirty = me->GetUInt32();

    if (statsDirty & DIRTY_VITAL_HP)
        hp = me->GetFloat();

    if (statsDirty & DIRTY_VITAL_HP_RATE)
        hp_rate = me->GetFloat();

    if (statsDirty & DIRTY_VITAL_MANA)
        mana = me->GetFloat();

    if (statsDirty & DIRTY_VITAL_MANA_RATE)
        mana_rate = me->GetFloat();

    if (statsDirty & DIRTY_VITAL_PYSSTAMINA)
        pstam = me->GetFloat();

    if (statsDirty & DIRTY_VITAL_PYSSTAMINA_RATE)
        pstam_rate = me->GetFloat();

    if (statsDirty & DIRTY_VITAL_MENSTAMINA)
        mstam = me->GetFloat();

    if (statsDirty & DIRTY_VITAL_MENSTAMINA_RATE)
        mstam_rate = me->GetFloat();

    if (statsDirty & DIRTY_VITAL_EXPERIENCE)
        exp = me->GetInt32();

    if (statsDirty & DIRTY_VITAL_PROGRESSION)
        prog = me->GetInt32();

    counter = me->GetUInt8();

    valid=!(me->overrun);
}

csString psStatDRMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Ent: %d",entityid);

    msgtext.AppendFmt(" Dirty: %d ",statsDirty);

    if (statsDirty & DIRTY_VITAL_HP)
        msgtext.AppendFmt(" HP: %.2f",hp);

    if (statsDirty & DIRTY_VITAL_HP_RATE)
        msgtext.AppendFmt(" HP RATE: %.2f",hp_rate);

    if (statsDirty & DIRTY_VITAL_MANA)
        msgtext.AppendFmt(" MANA: %.2f",mana);

    if (statsDirty & DIRTY_VITAL_MANA_RATE)
        msgtext.AppendFmt(" MANA RATE: %.2f",mana_rate);

    if (statsDirty & DIRTY_VITAL_PYSSTAMINA)
        msgtext.AppendFmt(" PYSSTA: %.2f",pstam);

    if (statsDirty & DIRTY_VITAL_PYSSTAMINA_RATE)
        msgtext.AppendFmt(" PYSSTA RATE: %.2f",pstam_rate);

    if (statsDirty & DIRTY_VITAL_MENSTAMINA)
        msgtext.AppendFmt(" MENSTA: %.2f",mstam);

    if (statsDirty & DIRTY_VITAL_MENSTAMINA_RATE)
        msgtext.AppendFmt(" MENSTA RATE: %.2f",mstam_rate);

    if (statsDirty & DIRTY_VITAL_EXPERIENCE)
        msgtext.AppendFmt(" EXP: %.2f",exp);

    if (statsDirty & DIRTY_VITAL_PROGRESSION)
        msgtext.AppendFmt(" PP: %.2f",prog);

    msgtext.AppendFmt(" C: %d",counter);

    return msgtext;
}

//---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psStatsMessage,MSGTYPE_STATS);

psStatsMessage::psStatsMessage( uint32_t client, float maxHP, float maxMana, float maxWeight, float maxCapacity )
{
    msg = new MsgEntry( sizeof(float)*4 );
    msg->Add( maxHP );
    msg->Add( maxMana );
    msg->Add( maxWeight );
    msg->Add( maxCapacity );
    msg->SetType( MSGTYPE_STATS );
    msg->clientnum = client;

    valid=!(msg->overrun);
}

psStatsMessage::psStatsMessage()
{
    msg = new MsgEntry();
    msg->SetType( MSGTYPE_STATS );
    msg->clientnum = 0;
    valid=!(msg->overrun);
}


psStatsMessage::psStatsMessage( MsgEntry* me )
{
    if ( !me )
        return;

    hp = me->GetFloat();
    mana = me->GetFloat();
    weight = me->GetFloat();
    capacity = me->GetFloat();
}


csString psStatsMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("HP: %.3f Mana: %.3f Weight: %.3f Capacity: %.3f",
            hp, mana, weight, capacity );

    return msgtext;
}

//--------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psGUISkillMessage, MSGTYPE_GUISKILL);

psGUISkillMessage::psGUISkillMessage( uint8_t command,
                                      csString commandData)
{
    msg = new MsgEntry( sizeof(bool) + sizeof(uint8_t) +
                        commandData.Length() + 1 +
                        skillCache.size());

    msg->SetType(MSGTYPE_GUISKILL);
    msg->clientnum  = 0;

    msg->Add( false ); //We didn't add stats
    msg->Add( command );
    msg->Add( commandData );
    skillCache.write(msg);

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psGUISkillMessage::psGUISkillMessage( uint32_t clientNum,
                                      uint8_t command,
                                      csString commandData,
                                      psSkillCache *skills,
                                      uint32_t str,
                                      uint32_t end,
                                      uint32_t agi,
                                      uint32_t inl,
                                      uint32_t wil,
                                      uint32_t chr,
                                      uint32_t hp,
                                      uint32_t man,
                                      uint32_t physSta,
                                      uint32_t menSta,
                                      uint32_t hpMax,
                                      uint32_t manMax,
                                      uint32_t physStaMax,
                                      uint32_t menStaMax,
                                      bool openWin,
                                      int32_t focus,
                                      int32_t selSkillCat,
                                      bool isTraining)
{
    //Function begins
    msg = new MsgEntry( sizeof(bool) + sizeof(uint8_t) +
                        commandData.Length() + 1+
                        (skills ? skills->size() : skillCache.size()) +
                        sizeof(str)+
                        sizeof(end)+
                        sizeof(agi)+
                        sizeof(inl)+
                        sizeof(wil)+
                        sizeof(chr)+
                        sizeof(hp)+
                        sizeof(man)+
                        sizeof(physSta)+
                        sizeof(menSta)+
                        sizeof(hpMax)+
                        sizeof(manMax)+
                        sizeof(physStaMax)+
                        sizeof(menStaMax)+
                        sizeof(bool) +
                        sizeof(focus)+
                        sizeof(selSkillCat)+
                        sizeof(bool)
                        );

    msg->SetType(MSGTYPE_GUISKILL);
    msg->clientnum  = clientNum;

    msg->Add( true ); //We added stats
    msg->Add( command );
    msg->Add( commandData );
    if (skills)
        skills->write(msg);
    else
        skillCache.write(msg);
    msg->Add( str );
    msg->Add( end );
    msg->Add( agi );
    msg->Add( inl );
    msg->Add( wil );
    msg->Add( chr );
    msg->Add( hp );
    msg->Add( man );
    msg->Add( physSta );
    msg->Add( menSta);
    msg->Add( hpMax );
    msg->Add( manMax );
    msg->Add( physStaMax );
    msg->Add( menStaMax);
    msg->Add( openWin );
    msg->Add( focus );
    msg->Add( selSkillCat );
    msg->Add( isTraining );

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psGUISkillMessage::psGUISkillMessage( MsgEntry* message )
{
    if ( !message )
        return;

    includeStats = message->GetBool(); //First added value indicates if we added stats or not
    command   = message->GetUInt8();
    commandData = message->GetStr();
    skillCache.read(message);

    if (includeStats)
    {
        strength = message->GetUInt32();
        endurance = message->GetUInt32();
        agility = message->GetUInt32();
        intelligence = message->GetUInt32();
        will = message->GetUInt32();
        charisma = message->GetUInt32();
        hitpoints = message->GetUInt32();
        mana = message->GetUInt32();
        physStamina = message->GetUInt32();
        menStamina = message->GetUInt32();

        hitpointsMax = message->GetUInt32();
        manaMax = message->GetUInt32();
        physStaminaMax = message->GetUInt32();
        menStaminaMax = message->GetUInt32();
        openWindow = message->GetBool();
        focusSkill = message->GetInt32();
        skillCat = message->GetInt32();
        trainingWindow = message->GetBool();
    }
    // Sets valid flag based on message overrun state
    valid=!(message->overrun);
}

csString psGUISkillMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Command: %d ", command);
    if (includeStats)
    {
        msgtext.AppendFmt("Str: %d End: %d Agi: %d Int: %d Wil: %d Cha: %d ",
            strength, endurance, agility, intelligence, will, charisma);
        msgtext.AppendFmt("HP: %d (Max: %d) Mana: %d (Max: %d) ", hitpoints, hitpointsMax, mana, manaMax);
        msgtext.AppendFmt("Physical Stamina: %d (Max: %d) Mental Stamina: %d (Max %d) ",
            physStamina, physStaminaMax, menStamina, menStaminaMax);
        msgtext.AppendFmt("Focus Skill: %d Skill Cat: %d ", focusSkill, skillCat);
        msgtext.AppendFmt("Window '%s' Training '%s'",
            (openWindow ? "open" : "closed"), (trainingWindow ? "open" : "closed"));
    }
#ifdef FULL_DEBUG_DUMP
    msgtext.AppendFmt(" Data: '%s' ", commandData.GetDataSafe());
#endif

    return msgtext;
}

//--------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psGUIBankingMessage, MSGTYPE_BANKING);

psGUIBankingMessage::psGUIBankingMessage(uint32_t clientNum, uint8_t command, bool guild,
                                         int circles, int octas, int hexas, int trias, 
                                         int circlesBanked, int octasBanked, int hexasBanked,
                                         int triasBanked, int maxCircles, int maxOctas, int maxHexas,
                                         int maxTrias, float exchangeFee, bool forceOpen)
{
    msg = new MsgEntry(sizeof(bool) +
                       sizeof(bool) +
                       sizeof(uint8_t) +
                       sizeof(bool) +
                       sizeof(int) +
                       sizeof(int) +
                       sizeof(int) +
                       sizeof(int) +
                       sizeof(int) +
                       sizeof(int) +
                       sizeof(int) +
                       sizeof(int) +
                       sizeof(int) +
                       sizeof(int) +
                       sizeof(int) +
                       sizeof(int) +
                       sizeof(float) +
                       sizeof(bool));
        
    msg->SetType(MSGTYPE_BANKING);
    msg->clientnum  = clientNum;
    msg->Add( true );
    msg->Add( false );
    msg->Add( command );
    msg->Add( guild );
    msg->Add( circles );
    msg->Add( octas );
    msg->Add( hexas );
    msg->Add( trias );
    msg->Add( circlesBanked );
    msg->Add( octasBanked );
    msg->Add( hexasBanked );
    msg->Add( triasBanked );
    msg->Add( maxCircles );
    msg->Add( maxOctas );
    msg->Add( maxHexas );
    msg->Add( maxTrias );
    msg->Add( exchangeFee );
    msg->Add( forceOpen );

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psGUIBankingMessage::psGUIBankingMessage(uint8_t command, bool guild,
                                         int circles, int octas, int hexas,int trias)
{
    msg = new MsgEntry(sizeof(bool) +
                       sizeof(bool) +
                       sizeof(uint8_t) +
                       sizeof(bool) +
                       sizeof(int) +
                       sizeof(int) +
                       sizeof(int) +
                       sizeof(int));
     
    msg->clientnum = 0;
    msg->SetType(MSGTYPE_BANKING);
    msg->Add( false );
    msg->Add( false );
    msg->Add( command );
    msg->Add( guild );
    msg->Add( circles );
    msg->Add( octas );
    msg->Add( hexas );
    msg->Add( trias );

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psGUIBankingMessage::psGUIBankingMessage(uint8_t command, bool guild,
                                         int coins, int coin)
{
    msg = new MsgEntry(sizeof(bool) +
                       sizeof(bool) +
                       sizeof(uint8_t) +
                       sizeof(bool) +
                       sizeof(int) +
                       sizeof(int));
     
    msg->clientnum = 0;
    msg->SetType(MSGTYPE_BANKING);
    msg->Add( false );
    msg->Add( true );
    msg->Add( command );
    msg->Add( guild );
    msg->Add( coins );
    msg->Add( coin );

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psGUIBankingMessage::psGUIBankingMessage(MsgEntry *message)
{
    if ( !message )
        return;

    sendingFull = message->GetBool();
    sendingExchange = message->GetBool();
    command = message->GetUInt8();
    guild = message->GetBool();

    if(sendingExchange)
    {
        coins = message->GetInt32();
        coin = message->GetInt32();
    }
    else
    {
        circles = message->GetInt32();
        octas = message->GetInt32();
        hexas = message->GetInt32();
        trias = message->GetInt32();
    }

    if(sendingFull)
    {
        circlesBanked = message->GetInt32();
        octasBanked = message->GetInt32();
        hexasBanked = message->GetInt32();
        triasBanked = message->GetInt32();
        maxCircles = message->GetInt32();
        maxOctas = message->GetInt32();
        maxHexas = message->GetInt32();
        maxTrias = message->GetInt32();
        exchangeFee = message->GetFloat();
        openWindow = message->GetBool();
    }

    // Sets valid flag based on message overrun state
    valid=!(message->overrun);
}

csString psGUIBankingMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Command: %d ", command);
    msgtext.AppendFmt("Guild Bank: %s", (guild ? "Yes" : "No"));
    msgtext.AppendFmt("Circles Banked: %i, OctasBanked: %i, HexasBanked: %i, TriasBanked: %i", circlesBanked, octasBanked, hexasBanked, triasBanked);
    msgtext.AppendFmt("Circles: %i, Octas: %i, Hexas: %i, Trias: %i", circles, octas, hexas, trias);
    msgtext.AppendFmt("Max Circles: %i, Max Octas: %i, Max Hexas: %i, Max Trias: %i", maxCircles, maxOctas, maxHexas, maxTrias);
    msgtext.AppendFmt("Exchange Fee: %f%%", exchangeFee);
    msgtext.AppendFmt("Bank Window '%s'", (openWindow ? "open" : "closed"));

    return msgtext;
}

//--------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psPetSkillMessage,MSGTYPE_PET_SKILL);

psPetSkillMessage::psPetSkillMessage( uint8_t command,
                                      csString commandData)
{
    msg = new MsgEntry( sizeof(bool) + sizeof(uint8_t) +
                        commandData.Length() +
                        1);

    msg->SetType(MSGTYPE_PET_SKILL);
    msg->clientnum  = 0;

    msg->Add( false ); //We didn't add stats
    msg->Add( command );
    msg->Add( commandData );

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psPetSkillMessage::psPetSkillMessage( uint32_t clientNum,
                                      uint8_t command,
                                      csString commandData,
                                      uint32_t str,
                                      uint32_t end,
                                      uint32_t agi,
                                      uint32_t inl,
                                      uint32_t wil,
                                      uint32_t chr,
                                      uint32_t hp,
                                      uint32_t man,
                                      uint32_t physSta,
                                      uint32_t menSta,
                                      uint32_t hpMax,
                                      uint32_t manMax,
                                      uint32_t physStaMax,
                                      uint32_t menStaMax,
                                      bool openWin,
                                      int32_t focus)
{
    //Function begins
    msg = new MsgEntry( sizeof(bool) + sizeof(uint8_t) +
                        commandData.Length() + 1+
                        sizeof(str)+
                        sizeof(end)+
                        sizeof(agi)+
                        sizeof(inl)+
                        sizeof(wil)+
                        sizeof(chr)+
                        sizeof(hp)+
                        sizeof(man)+
                        sizeof(physSta)+
                        sizeof(menSta)+
                        sizeof(hpMax)+
                        sizeof(manMax)+
                        sizeof(physStaMax)+
                        sizeof(menStaMax)+
                        sizeof(bool) +
                        sizeof(focus)
                        );

    msg->SetType(MSGTYPE_PET_SKILL);
    msg->clientnum  = clientNum;

    msg->Add( true ); //We added stats
    msg->Add( command );
    msg->Add( commandData );
    msg->Add( str );
    msg->Add( end );
    msg->Add( agi );
    msg->Add( inl );
    msg->Add( wil );
    msg->Add( chr );
    msg->Add( hp );
    msg->Add( man );
    msg->Add( physSta );
    msg->Add( menSta);
    msg->Add( hpMax );
    msg->Add( manMax );
    msg->Add( physStaMax );
    msg->Add( menStaMax);
    msg->Add( openWin );
    msg->Add( focus );

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psPetSkillMessage::psPetSkillMessage( MsgEntry* message )
{
    if ( !message )
        return;

    includeStats = message->GetBool(); //First added value indicates if we added stats or not
    command   = message->GetUInt8();
    commandData = message->GetStr();

    if (includeStats)
    {
        strength = message->GetUInt32();
        endurance = message->GetUInt32();
        agility = message->GetUInt32();
        intelligence = message->GetUInt32();
        will = message->GetUInt32();
        charisma = message->GetUInt32();
        hitpoints = message->GetUInt32();
        mana = message->GetUInt32();
        physStamina = message->GetUInt32();
        menStamina = message->GetUInt32();

        hitpointsMax = message->GetUInt32();
        manaMax = message->GetUInt32();
        physStaminaMax = message->GetUInt32();
        menStaminaMax = message->GetUInt32();
        openWindow = message->GetBool();
        focusSkill = message->GetInt32();
    }
    // Sets valid flag based on message overrun state
    valid=!(message->overrun);
}

csString psPetSkillMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Command: %d ", command );

    if (includeStats)
    {
        msgtext.AppendFmt("Str: %d End: %d Agi: %d Int: %d Wil: %d Cha: %d ",
                strength, endurance, agility, intelligence, will, charisma);
        msgtext.AppendFmt("HP: %d (Max: %d) Mana: %d (Max: %d) ", hitpoints, hitpointsMax, mana, manaMax);
        msgtext.AppendFmt("Physical Stamina: %d (Max: %d) Mental Stamina: %d (Max %d) ",
                physStamina, physStaminaMax, menStamina, menStaminaMax);
        msgtext.AppendFmt("Focus Skill: %d ", focusSkill);
        msgtext.AppendFmt("Window '%s'", (openWindow ? "open" : "closed"));
    }
#ifdef FULL_DEBUG_DUMP
    msgtext.AppendFmt(" Data: '%s' ", commandData.GetDataSafe());
#endif

    return msgtext;
}

//--------------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY2(psDRMessage,MSGTYPE_DEAD_RECKONING);

void psDRMessage::CreateMsgEntry(uint32_t client, csStringHash* msgstrings, iSector *sector)
{
    const char* sectorName = sector->QueryObject()->GetName();
    csStringID sectorNameStrId = msgstrings ? msgstrings->Request(sectorName) : csInvalidStringID;
    int sectorNameLen = (sectorNameStrId == csInvalidStringID) ? (int) strlen (sectorName) : 0;

    msg = new MsgEntry( sizeof(uint32)*12 + sizeof(uint8)*4 + (sectorNameLen?sectorNameLen+1:0) );

    msg->SetType(MSGTYPE_DEAD_RECKONING);
    msg->clientnum = client;
}

psDRMessage::psDRMessage(uint32_t client, PS_ID mappedid, uint8_t counter,
                         csStringHash* msgstrings, iPcLinearMovement *linmove, uint8_t mode)
{
    float speed;
    linmove->GetDRData(on_ground,speed,pos,yrot,sector,vel,worldVel,ang_vel);

    CreateMsgEntry(client, msgstrings, sector);

    WriteDRInfo(client, mappedid,
         on_ground, mode, counter, pos, yrot, sector,
         vel,worldVel, ang_vel, msgstrings);

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psDRMessage::psDRMessage(uint32_t client,PS_ID mappedid,
                         bool on_ground, uint8_t mode, uint8_t counter,
                         const csVector3& pos, float yrot,iSector *sector,
                         const csVector3& vel, csVector3& worldVel, float ang_vel,
                         csStringHash* msgstrings)
{
    CreateMsgEntry(client, msgstrings, sector);

    WriteDRInfo(client, mappedid,
         on_ground, mode, counter, pos, yrot, sector,
         vel,worldVel, ang_vel ,msgstrings);

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

#define ISNONZERO(x) (fabsf(x) > SMALL_EPSILON)

uint8_t psDRMessage::GetDataFlags(const csVector3& v, const csVector3& wv, float yrv, uint8_t mode)
{
    uint8_t flags = NOT_MOVING;
    if ( mode != ON_GOUND )
        flags |= ACTOR_MODE;
    if ( ISNONZERO(yrv) )
        flags |= ANG_VELOCITY;
    if ( ISNONZERO(v.x) )
        flags |= X_VELOCITY;
    if ( ISNONZERO(v.y) )
        flags |= Y_VELOCITY;
    if ( ISNONZERO(v.z) )
        flags |= Z_VELOCITY;
    if ( ISNONZERO(wv.x) )
        flags |= X_WORLDVELOCITY;
    if ( ISNONZERO(wv.y) )
        flags |= Y_WORLDVELOCITY;
    if ( ISNONZERO(wv.z) )
        flags |= Z_WORLDVELOCITY;
    return flags;
}

void psDRMessage::WriteDRInfo(uint32_t client,PS_ID mappedid,
                        bool on_ground, uint8_t mode, uint8_t counter,
                        const csVector3& pos, float yrot, iSector *sector,
                        const csVector3& vel, csVector3& worldVel, float ang_vel,
                        csStringHash* msgstrings, bool donewriting)
{
    const char* sectorName = sector->QueryObject()->GetName();
    csStringID sectorNameStrId = msgstrings ? msgstrings->Request(sectorName) : csInvalidStringID;

    msg->Add( (uint32_t) mappedid );
    msg->Add( counter );

    if (on_ground)
        mode |= ON_GOUND;  // Pack falling status with mode

    // Store packing information
    uint8_t dataflags = GetDataFlags(vel, worldVel, ang_vel, mode);
    msg->Add( dataflags );

    if (dataflags & ACTOR_MODE)
        msg->Add( mode );
    if (dataflags & ANG_VELOCITY)
        msg->Add( ang_vel );
    if (dataflags & X_VELOCITY)
        msg->Add( vel.x );
    if (dataflags & Y_VELOCITY)
        msg->Add( vel.y );
    if (dataflags & Z_VELOCITY)
        msg->Add( vel.z );
    if (dataflags & X_WORLDVELOCITY)
        msg->Add( worldVel.x );
    if (dataflags & Y_WORLDVELOCITY)
        msg->Add( worldVel.y );
    if (dataflags & Z_WORLDVELOCITY)
        msg->Add( worldVel.z );

    msg->Add( pos.x );
    msg->Add( pos.y );
    msg->Add( pos.z );

    msg->Add( (uint8_t) (yrot * 256 / TWO_PI) ); // Quantize radians to 0-255

    msg->Add( (uint32_t) sectorNameStrId );

    if (sectorNameStrId == csInvalidStringID)
        msg->Add(sectorName);

    if (donewriting)  // If we're not writing anymore data after this, shrink to fit
        msg->ClipToCurrentSize();
}

psDRMessage::psDRMessage( void *data, int size,csStringHash* msgstrings, iEngine *engine)
{
    msg = new MsgEntry(size,PRIORITY_HIGH);
    memcpy(msg->bytes->payload,data,size);
    ReadDRInfo(msg,msgstrings,engine);
}

psDRMessage::psDRMessage( MsgEntry* me, csStringHash* msgstrings, iEngine *engine)
{
    msg = NULL;
    ReadDRInfo(me,msgstrings,engine);
}

void psDRMessage::operator=(psDRMessage& other)
{
    entityid   = other.entityid;
    counter    = other.counter;
    ang_vel    = other.ang_vel;
    vel        = other.vel;
    pos        = other.pos;
    on_ground  = other.on_ground;
    yrot       = other.yrot;
    sector     = other.sector;
    sectorName = other.sectorName;
}

void psDRMessage::ReadDRInfo(MsgEntry* me, csStringHash* msgstrings, iEngine *engine)
{
    entityid = me->GetUInt32();
    counter  = me->GetUInt8();

    // Find out what's packed here
    uint8_t dataflags = me->GetUInt8();

    if (dataflags & ACTOR_MODE)
    {
        mode = me->GetInt8();
        on_ground = (mode & ON_GOUND) != 0;
        mode &= ~ON_GOUND;  // Unpack
    }
    else  // Normal
    {
        mode = 0;
        on_ground = true;
    }

    ang_vel = (dataflags & ANG_VELOCITY) ? me->GetFloat() : 0.0f;
    vel.x = (dataflags & X_VELOCITY) ? me->GetFloat() : 0.0f;
    vel.y = (dataflags & Y_VELOCITY) ? me->GetFloat() : 0.0f;
    vel.z = (dataflags & Z_VELOCITY) ? me->GetFloat() : 0.0f;
    worldVel.x = (dataflags & X_WORLDVELOCITY) ? me->GetFloat() : 0.0f;
    worldVel.y = (dataflags & Y_WORLDVELOCITY) ? me->GetFloat() : 0.0f;
    worldVel.z = (dataflags & Z_WORLDVELOCITY) ? me->GetFloat() : 0.0f;

    pos.x = me->GetFloat();
    pos.y = me->GetFloat();
    pos.z = me->GetFloat();

    yrot = me->GetInt8();
    yrot *= TWO_PI/256;

    csStringID sectorNameStrId = (csStringID)me->GetUInt32();
    sectorName = (sectorNameStrId != csInvalidStringID) ? msgstrings->Request(sectorNameStrId) : me->GetStr() ;
    sector = (sectorName.Length()) ? engine->GetSectors()->FindByName(sectorName) : NULL ;
}

bool psDRMessage::IsNewerThan(uint8_t oldCounter)
{
    /** The value of the DR counter goes from 0-255 and then loops back around.
     *  For this message to be newer, we test that the distance from us back to
     *  the given value is no more than half-way back around the loop.
     *  (if the difference is negative, the cast will loop it back to the top)
     */
    return (uint8_t)(counter-oldCounter) <= 127;
}

csString psDRMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("EID: %d C: %d ",entityid,counter);
    msgtext.AppendFmt("Sector: %s ",sectorName.GetDataSafe());
    msgtext.AppendFmt("Pos(%.2f,%.2f,%.2f) ",pos.x,pos.y,pos.z);

#ifdef FULL_DEBUG_DUMP
    msgtext.AppendFmt("Vel(%.2f,%.2f,%.2f) ",vel.x,vel.y,vel.z);
    msgtext.AppendFmt("WVel(%.2f,%.2f,%.2f) ",worldVel.x,worldVel.y,worldVel.z);
    if (on_ground)
        msgtext.Append("OnGround ");
    else
        msgtext.Append("Flying ");
    msgtext.AppendFmt("yrot: %.2f ",yrot);
    msgtext.AppendFmt("sector: %s ",sectorName.GetData());
#endif

    return msgtext;
}

//--------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psPersistWorldRequest,MSGTYPE_PERSIST_WORLD);

psPersistWorldRequest::psPersistWorldRequest()
{
    msg = new MsgEntry();

    msg->SetType(MSGTYPE_PERSIST_WORLD);
    msg->clientnum  = 0;
}

psPersistWorldRequest::psPersistWorldRequest(MsgEntry* me)
{
}

csString psPersistWorldRequest::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Request world");

    return msgtext;
}

//--------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psRequestAllObjects,MSGTYPE_PERSIST_ALL);

psRequestAllObjects::psRequestAllObjects()
{
    msg = new MsgEntry( );

    msg->SetType(MSGTYPE_PERSIST_ALL);
    msg->clientnum  = 0;

    valid=!(msg->overrun);
}

psRequestAllObjects::psRequestAllObjects(MsgEntry* me)
{
}

csString psRequestAllObjects::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Request All objects");

    return msgtext;
}

//--------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psPersistWorld,MSGTYPE_PERSIST_WORLD);

psPersistWorld::psPersistWorld( uint32_t clientNum, const char* sector )
{
    msg = new MsgEntry( strlen( sector ) + 1 );

    msg->SetType(MSGTYPE_PERSIST_WORLD);
    msg->clientnum  = clientNum;

    msg->Add( sector );
}

psPersistWorld::psPersistWorld( MsgEntry* me )
{
    sector = csString(me->GetStr());
}

csString psPersistWorld::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Sector: '%s'", sector.GetDataSafe());

    return msgtext;
}

//--------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psPersistActorRequest,MSGTYPE_PERSIST_ACTOR_REQUEST);

psPersistActorRequest::psPersistActorRequest( MsgEntry* me )
{
}

csString psPersistActorRequest::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Request actors");

    return msgtext;
}

//--------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY2(psPersistActor,MSGTYPE_PERSIST_ACTOR);

psPersistActor::psPersistActor( uint32_t clientNum,
                                int type,
                                int masqueradeType,
                                bool control,
                                const char* name,
                                const char* guild,
                                const char* factname,
                                const char* filename,
                                const char* race,
                                const char* helmGroup,
                                csVector3 collTop, csVector3 collBottom, csVector3 collOffSet,
                                const char* texParts,
                                const char* equipmentParts,
                                uint8_t counter,
                                PS_ID mappedid,csStringHash* msgstrings, iPcLinearMovement *linmove,
                                uint8_t movementMode,
                                uint8_t serverMode,
                                uint32_t playerID,
                                uint32_t groupID,
                                PS_ID ownerEID,
                                uint32_t flags)
{
    msg = new MsgEntry( 5000 );

    msg->SetType(MSGTYPE_PERSIST_ACTOR);
    msg->clientnum  = clientNum;

    float speed;
    linmove->GetDRData(on_ground,speed,pos,yrot,sector,vel, worldVel, ang_vel);

    WriteDRInfo(clientNum, mappedid, on_ground, movementMode, counter, pos, yrot, sector, vel, worldVel, ang_vel, msgstrings, false);

    msg->Add( (uint32_t) type );
    msg->Add( (uint32_t) masqueradeType );
    msg->Add( control );
    msg->Add( name );

    if ( guild == 0 )
        msg->Add( " " );
    else
        msg->Add( guild );

    msg->Add( factname );
    msg->Add( filename );
    msg->Add( race );
    msg->Add( helmGroup );
    msg->Add( collTop );
    msg->Add( collBottom );
    msg->Add( collOffSet );
    msg->Add( texParts );
    msg->Add( equipmentParts );
    msg->Add( serverMode );
    posPlayerID = (int) msg->current;
    msg->Add( playerID );
    msg->Add( groupID );
    msg->Add( (uint32_t)ownerEID );
    if (flags) // No point sending 0, has to be at the end
    {
        msg->Add( flags );
    }

    msg->ClipToCurrentSize();
}

psPersistActor::psPersistActor( MsgEntry* me, csStringHash* msgstrings, iEngine *engine )
{
    ReadDRInfo(me, msgstrings, engine);

    type        = me->GetUInt32();
    masqueradeType = me->GetUInt32();
    control     = me->GetBool();
    name        = csString ( me->GetStr() );
    guild       = csString ( me->GetStr() );
    if ( guild == " " )
        guild.Clear();

    factname    = csString ( me->GetStr() );
    filename    = csString ( me->GetStr() );
    race        = csString ( me->GetStr() );
    helmGroup   = csString ( me->GetStr() );

    top         = me->GetVector();
    bottom      = me->GetVector();
    offset      = me->GetVector();

    texParts    = csString ( me->GetStr() );
    equipment   = csString ( me->GetStr() );

    serverMode = me->GetUInt8();
    playerID   = me->GetUInt32();
    groupID    = me->GetUInt32();
    ownerEID   = me->GetUInt32();
    if (!me->IsEmpty())
    {
        flags   = me->GetUInt32();
    } else
    {
        flags   = 0;
    }
}

csString psPersistActor::ToString(AccessPointers * access_ptrs)
{
    csString msgtext;

    msgtext.AppendFmt("DR: %s ", psDRMessage::ToString(access_ptrs).GetData() );
    msgtext.AppendFmt(" Type: %d",type);
    msgtext.AppendFmt(" MaskType: %d",masqueradeType);
    msgtext.AppendFmt(" Control: %s",(control?"true":"false"));
    msgtext.AppendFmt(" Name: '%s'",name.GetDataSafe());
    msgtext.AppendFmt(" Guild: '%s'",guild.GetDataSafe());
    msgtext.AppendFmt(" Factname: '%s'",factname.GetDataSafe());
    msgtext.AppendFmt(" Filename: '%s'",filename.GetDataSafe());
    msgtext.AppendFmt(" Race: '%s'",race.GetDataSafe());
    msgtext.AppendFmt(" Top: (%.3f,%.3f,%.3f)",top.x,top.y,top.z);
    msgtext.AppendFmt(" Bottom: (%.3f,%.3f,%.3f)",bottom.x,bottom.y,bottom.z);
    msgtext.AppendFmt(" Offset: (%.3f,%.3f,%.3f)",offset.x,offset.y,offset.z);
    msgtext.AppendFmt(" TexParts: '%s'",texParts.GetDataSafe());
    msgtext.AppendFmt(" Equipment: '%s'",equipment.GetDataSafe());
    msgtext.AppendFmt(" Mode: %d",serverMode);
    msgtext.AppendFmt(" PlayerID: %d",playerID);
    msgtext.AppendFmt(" GroupID: %d",groupID);
    msgtext.AppendFmt(" OwnerEID: %d",ownerEID);
    msgtext.AppendFmt(" Flags:");
    if (flags & INVISIBLE) msgtext.AppendFmt(" INVISIBLE");
    if (flags & INVINCIBLE) msgtext.AppendFmt(" INVINCIBLE");
    if (flags & NPC) msgtext.AppendFmt(" NPC");

    return msgtext;
}

void psPersistActor::SetPlayerID(uint32_t playerID)
{
    msg->Reset(posPlayerID);
    msg->Add(playerID);
}

//------------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psPersistItem,MSGTYPE_PERSIST_ITEM);

psPersistItem::psPersistItem( uint32_t clientNum,
                              uint32_t id,
                              int type,
                              const char* name,
                              const char* factname,
                              const char* filename,
                              const char* sector,
                              csVector3 pos,
                              float yRot,
                              uint32_t flags)
{
    msg = new MsgEntry( 5000 );

    msg->SetType(MSGTYPE_PERSIST_ITEM);
    msg->clientnum  = clientNum;

    msg->Add( (uint32_t) id );
    msg->Add( (uint32_t) type );
    msg->Add( name );
    msg->Add( factname );
    msg->Add( filename );
    msg->Add( sector );
    msg->Add( pos );
    msg->Add( yRot );
    if (flags) // No point sending 0, has to be at the end
    {
        msg->Add( flags );
    }

    msg->ClipToCurrentSize();
}


psPersistItem::psPersistItem( MsgEntry* me )
{
    id          = me->GetUInt32();
    type        = me->GetUInt32();
    name        = csString ( me->GetStr() );
    factname    = csString ( me->GetStr() );
    filename    = csString ( me->GetStr() );
    sector      = csString ( me->GetStr() );
    pos         = me->GetVector();
    yRot        = me->GetFloat();
    if (!me->IsEmpty())
    {
        flags   = me->GetUInt32();
    } else
    {
        flags   = 0;
    }
}

csString psPersistItem::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("ID: %d type: %d Name: %s",id,type,name.GetData());
    msgtext.AppendFmt(" Factname: %s Filename %s ",factname.GetData(),filename.GetData());
    msgtext.AppendFmt("Sector: %s ",sector.GetDataSafe());
    msgtext.AppendFmt("Pos(%.2f,%.2f,%.2f) ",pos.x,pos.y,pos.z);
    msgtext.AppendFmt("yrot: %.2f Flags:",yRot);
    if (flags & NOPICKUP) msgtext.AppendFmt(" NOPICKUP");

    return msgtext;
}

//------------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psPersistActionLocation,MSGTYPE_PERSIST_ACTIONLOCATION);

psPersistActionLocation::psPersistActionLocation( uint32_t clientNum,
                                uint32_t id,
                                int type,
                                const char* name,
                                const char* sector,
                                const char* mesh
                               )
{
    msg = new MsgEntry( 5000 );

    msg->SetType(MSGTYPE_PERSIST_ACTIONLOCATION);
    msg->clientnum  = clientNum;

    msg->Add( (uint32_t) id );
    msg->Add( (uint32_t) type );
    msg->Add( name );
    msg->Add( sector );
    msg->Add( mesh );

    msg->ClipToCurrentSize();
}


psPersistActionLocation::psPersistActionLocation( MsgEntry* me )
{
    id          = me->GetUInt32();
    type        = me->GetUInt32();
    name        = me->GetStr();
    sector      = me->GetStr();
    mesh        = me->GetStr();
}

csString psPersistActionLocation::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("ID: %d Type: %d Name: '%s' Sector: '%s' Mesh: '%s'",
                      id,type,name.GetDataSafe(),sector.GetDataSafe(),mesh.GetDataSafe());

    return msgtext;
}

//------------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psRemoveObject,MSGTYPE_REMOVE_OBJECT);

psRemoveObject::psRemoveObject( uint32_t clientNum, uint32_t objectEID )
{
    msg = new MsgEntry( sizeof( uint32_t) );

    msg->SetType(MSGTYPE_REMOVE_OBJECT);
    msg->clientnum  = clientNum;

    msg->Add( (uint32_t) objectEID );
    valid=!(msg->overrun);
}

psRemoveObject::psRemoveObject( MsgEntry* me )
{
    objectEID = me->GetUInt32();
}

csString psRemoveObject::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("ObjectEID: %d", objectEID);

    return msgtext;
}

//------------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psBuddyListMsg,MSGTYPE_BUDDY_LIST);

psBuddyListMsg::psBuddyListMsg( uint32_t client, int totalBuddies )
{
    // Possible overflow here!
    msg = new MsgEntry( totalBuddies*100+10 );
    msg->SetType(MSGTYPE_BUDDY_LIST);
    msg->clientnum = client;

    buddies.SetSize( totalBuddies );
}

psBuddyListMsg::psBuddyListMsg( MsgEntry* me )
{
    int totalBuddies = me->GetUInt32();
    buddies.SetSize( totalBuddies );

    for ( int x = 0; x < totalBuddies; x++ )
    {
        buddies[x].name = me->GetStr();
        buddies[x].online = me->GetBool();
    }
}

void psBuddyListMsg::AddBuddy( int num, const char* name, bool onlineStatus )
{
    buddies[num].name = name;
    buddies[num].online = onlineStatus;
}

void psBuddyListMsg::Build()
{
    msg->Add( (uint32_t)buddies.GetSize() );

    for ( size_t x = 0; x < buddies.GetSize(); x++ )
    {
        msg->Add( buddies[x].name );
        msg->Add( buddies[x].online );
    }
    msg->ClipToCurrentSize();

    valid=!(msg->overrun);
}

csString psBuddyListMsg::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    for ( size_t x = 0; x < buddies.GetSize(); x++ )
    {
        msgtext.AppendFmt("Name: '%s' %s ",
                buddies[x].name.GetDataSafe(), (buddies[x].online ? "Online" : "Offline"));
    }

    return msgtext;
}

//------------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psBuddyStatus,MSGTYPE_BUDDY_STATUS);

csString psBuddyStatus::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Buddy: '%s' %s ", buddy.GetDataSafe(), (onlineStatus ? "online" : "offline"));

    return msgtext;
}

//------------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psMOTDMessage,MSGTYPE_MOTD);

csString psMOTDMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Tip: '%s' MOTD: '%s' Guild: '%s' Guild MOTD: '%s'",
            tip.GetDataSafe(), motd.GetDataSafe(), guild.GetDataSafe(), guildmotd.GetDataSafe()  );

    return msgtext;
}

//------------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psMOTDRequestMessage,MSGTYPE_MOTDREQUEST);

csString psMOTDRequestMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("MOTD Request");

    return msgtext;
}

//------------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psQuestionResponseMsg,MSGTYPE_QUESTIONRESPONSE);

csString psQuestionResponseMsg::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Question ID: %d Answer: '%s'", questionID, answer.GetDataSafe());

    return msgtext;
}

//------------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psQuestionMessage,MSGTYPE_QUESTION);

csString psQuestionMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Question ID: %d Question: '%s' Type: %d", questionID, question.GetDataSafe(), type);

    return msgtext;
}

//------------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psAdviceMessage,MSGTYPE_ADVICE);

csString psAdviceMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Command: '%s' Target: '%s' Message: '%s'",
            sCommand.GetDataSafe(), sTarget.GetDataSafe(), sMessage.GetDataSafe());

    return msgtext;
}

//------------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psGUIActiveMagicMessage,MSGTYPE_ACTIVEMAGIC);

csString psGUIActiveMagicMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;
    size_t numCategories = categoryList.GetSize();

    msgtext.AppendFmt("Window: %s Command: %d Categories: %zu ",
            (openWindow ? "open" : "closed"), command, numCategories);
    for (size_t i = 0; i < numCategories; i++)
    {
        msgtext.AppendFmt("'%s', ", categoryList[i].GetDataSafe());
    }

    return msgtext;
}

//------------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psSlotMovementMsg,MSGTYPE_SLOT_MOVEMENT);

csString psSlotMovementMsg::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("FROM Container: %d Slot: %d ", fromContainer, fromSlot);
    msgtext.AppendFmt("TO Container: %d Slot: %d ", toContainer, toSlot);
    msgtext.AppendFmt("Stack Count: %d", stackCount);

    return msgtext;
}

//------------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psQuestionCancelMessage,MSGTYPE_QUESTIONCANCEL);

csString psQuestionCancelMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Question ID: %d", questionID);

    return msgtext;
}

//------------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psGuildMOTDSetMessage,MSGTYPE_GUILDMOTDSET);

csString psGuildMOTDSetMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Guild '%s' MOTD: '%s'", guild.GetDataSafe(), guildmotd.GetDataSafe());

    return msgtext;
}

//------------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psCharacterDetailsMessage,MSGTYPE_CHARACTERDETAILS);

psCharacterDetailsMessage::psCharacterDetailsMessage( int clientnum,
                                                      const csString& name2s,
                                                      unsigned short int gender2s,
                                                      const csString& race2s,
                                                      const csString& desc2s,
                                                      const csArray<NetworkDetailSkill>& skills2s,
                                                      const csString& requestor)
{
    size_t size = sizeof(uint32_t);
    for ( size_t x = 0; x < skills2s.GetSize(); x++ )
    {
        size += sizeof(uint32_t) + skills2s[x].text.Length()+1;
    }

    msg = new MsgEntry( desc2s.Length()+1 + sizeof(gender2s) + name2s.Length() +1 + race2s.Length() +1  +requestor.Length()+1 + size);

    msg->SetType(MSGTYPE_CHARACTERDETAILS);
    msg->clientnum = clientnum;

    msg->Add(name2s);
    msg->Add(gender2s);
    msg->Add(race2s);
    msg->Add(desc2s);
    msg->Add(requestor);

    msg->Add( (uint32_t)skills2s.GetSize() );
    for (size_t x = 0; x < skills2s.GetSize(); x++)
    {
        msg->Add( (uint32_t)skills2s[x].category );
        msg->Add( skills2s[x].text );
    }
}

psCharacterDetailsMessage::psCharacterDetailsMessage( MsgEntry* me )
{
    name       = me->GetStr();
    gender     = me->GetUInt16();
    race       = me->GetStr();
    desc       = me->GetStr();
    requestor  = me->GetStr();
    uint32_t len = me->GetUInt32();
    for (uint32_t x = 0; x < len; x++)
    {
        NetworkDetailSkill s;

        s.category = me->GetUInt32();
        s.text = me->GetStr();
        skills.Push(s);
    }
}

csString psCharacterDetailsMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Name: '%s' Gender: %d Race: '%s' Description: '%s' Requestor: '%s'",
        name.GetDataSafe(), gender, race.GetDataSafe(), desc.GetDataSafe(), requestor.GetDataSafe());
    for ( size_t x = 0; x < skills.GetSize(); x++ )
    {
        msgtext.AppendFmt(" Skill: '%s' Category: '%d'",
            skills[x].text.GetDataSafe(), skills[x].category );
    }

    return msgtext;
}

//------------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psCharacterDetailsRequestMessage,MSGTYPE_CHARDETAILSREQUEST);

csString psCharacterDetailsRequestMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Is Me? %s Is Simple? %s Requestor: '%s'",
            (isMe?"True":"False"), (isSimple?"True":"False"), requestor.GetDataSafe());

    return msgtext;
}

//------------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psCharacterDescriptionUpdateMessage,MSGTYPE_CHARDESCUPDATE);

csString psCharacterDescriptionUpdateMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("New Description: '%s'", newValue.GetDataSafe());

    return msgtext;
}

//------------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psViewActionLocationMessage,MSGTYPE_VIEW_ACTION_LOCATION);

psViewActionLocationMessage::psViewActionLocationMessage(uint32_t clientnum, const char* name, const char* description)
{
    msg = new MsgEntry(strlen(name)+1 + strlen(description)+1);

    msg->SetType(MSGTYPE_VIEW_ACTION_LOCATION);
    msg->clientnum = clientnum;

    msg->Add(name);
    msg->Add(description);
}

psViewActionLocationMessage::psViewActionLocationMessage(MsgEntry* me)
{
    name        = me->GetStr();
    description = me->GetStr();
}

csString psViewActionLocationMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Name: '%s' Description: '%s'", name, description);

    return msgtext;
}

//------------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psViewItemDescription,MSGTYPE_VIEW_ITEM);

psViewItemDescription::psViewItemDescription(int containerID, int slotID)
{
    msg = new MsgEntry( sizeof(int32_t)*2 + sizeof(uint8_t));
    msg->SetType(MSGTYPE_VIEW_ITEM);
    msg->clientnum = 0;

    msg->Add( (uint8_t)REQUEST );
    msg->Add( (int32_t)containerID );

    msg->Add( (uint32_t)slotID );
}

psViewItemDescription::psViewItemDescription(uint32_t to, const char *itemName, const char *description, const char *icon, uint32_t stackCount, bool isContainer)
{
    csString name(itemName);
    csString desc(description);
    csString iconName(icon);

    if ( !isContainer )
    {
        msg = new MsgEntry(sizeof(uint8_t) + sizeof(bool) + name.Length() + desc.Length() + iconName.Length() + 3 + sizeof(uint32_t));
        msg->SetType(MSGTYPE_VIEW_ITEM);
        msg->clientnum = to;

        msg->Add( (uint8_t)DESCR );
        msg->Add( !IS_CONTAINER );
        msg->Add( itemName );
        msg->Add( description );
        msg->Add( icon );
        msg->Add( stackCount );
    }
    else
    {
        this->itemName = itemName;
        this->itemDescription = description;
        this->itemIcon = icon;
        this->to = to;
        msgSize = (int) (sizeof(uint8_t) + sizeof(bool) + name.Length() + desc.Length() + iconName.Length() + 3 + sizeof(int32_t) + sizeof(uint32_t));
    }
}

void psViewItemDescription::AddContents( const char *name, const char *icon, int purifyStatus, int slot, int stack )
{
    ContainerContents item;
    item.name = name;
    item.icon = icon;
	item.purifyStatus = purifyStatus; 
    item.slotID = slot;
    item.stackCount = stack;

    contents.Push( item );
    int namesize = name?(int)strlen(name):0;
    int iconsize = icon?(int)strlen(icon):0;
    msgSize += (int)(namesize + iconsize + 3 + sizeof(int)*3);
}

void psViewItemDescription::ConstructMsg()
{
    msg = new MsgEntry( msgSize );
    msg->SetType(MSGTYPE_VIEW_CONTAINER);
    msg->clientnum = to;

    msg->Add( (uint8_t)DESCR );
    msg->Add( IS_CONTAINER );
    msg->Add( itemName );
    msg->Add( itemDescription );
    msg->Add( itemIcon );
    msg->Add( (int32_t)containerID );
    msg->Add( (uint32_t)contents.GetSize() );
    for ( size_t n = 0; n < contents.GetSize(); n++ )
    {
        msg->Add( contents[n].name );
        msg->Add( contents[n].icon );
		msg->Add( contents[n].purifyStatus );
        msg->Add( (uint32_t)contents[n].slotID );
        msg->Add( (uint32_t)contents[n].stackCount );
    }
}

psViewItemDescription::psViewItemDescription( MsgEntry* me )
{
    format = me->GetUInt8();

    if ( format == REQUEST )
    {
        containerID = me->GetInt32();
        slotID      = me->GetUInt32();

        if (containerID == CONTAINER_INVENTORY_BULK) // must adjust slot number up
            slotID += PSCHARACTER_SLOT_BULK1;
    }
    else if ( format == DESCR )
    {
        hasContents = me->GetBool();

        itemName = me->GetStr();
        itemDescription = me->GetStr();
        itemIcon = me->GetStr();

        if (me->GetType() == MSGTYPE_VIEW_ITEM)
            stackCount = me->GetUInt32();
        else
            stackCount = 0;

        if ( hasContents )
        {
            containerID = me->GetInt32();
            size_t length = me->GetUInt32();

            for ( size_t n = 0; n < length; n++ )
            {
                ContainerContents item;
                item.name = me->GetStr();
                item.icon = me->GetStr();
				item.purifyStatus = me->GetUInt32();
                item.slotID = me->GetUInt32();
                item.stackCount = me->GetUInt32();
                contents.Push( item );
            }
        }
    }
}

csString psViewItemDescription::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    if ( format == REQUEST )
    {
        msgtext.AppendFmt("Container ID: %d Slot ID: %d", containerID, slotID);
    }
    else if ( format == DESCR )
    {
        msgtext.AppendFmt("Name: '%s' Description: '%s' Icon: '%s' Stack Count: %d ",
                itemName,
                itemDescription,
                itemIcon,
                stackCount);

        if ( hasContents )
        {
            msgtext.AppendFmt("Container ID: %d Contains ", containerID);

            for (size_t n = 0; n < contents.GetSize(); n++)
            {
				msgtext.AppendFmt("Name: '%s' Icon: '%s' Purify Status: %d Slot ID: %d Stack Count: %d ",
                        contents[n].name.GetData(),
                        contents[n].icon.GetData(),
						contents[n].purifyStatus,
                        contents[n].slotID,
                        contents[n].stackCount);
            }
        }
    }

    return msgtext;
}

//------------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psViewItemUpdate,MSGTYPE_UPDATE_ITEM);

psViewItemUpdate::psViewItemUpdate( uint32_t to,  uint32_t containerID, uint32_t slotID, bool clearSlot, 
                                    const char *itemName, const char *icon, uint32_t stackCount, uint32_t ownerID)
{
    msg = new MsgEntry( sizeof(containerID)+1 + sizeof(slotID) + sizeof(clearSlot) + strlen(itemName)+1 + strlen(icon)+1 + sizeof(stackCount) + sizeof(ownerID) );
    msg->SetType(MSGTYPE_UPDATE_ITEM);
    msg->clientnum = to;
    msg->Add(containerID);
    msg->Add(slotID);
    msg->Add(clearSlot);
    msg->Add(itemName);
    msg->Add(icon);
    msg->Add(stackCount);
    msg->Add(ownerID);
}

//void psViewItemUpdate::ConstructMsg()
//{
//    msg = new MsgEntry( sizeof(containerID) + itemName.Length() + description.Length() + icon.Length() + sizeof(stackCount) );
//    msg->data->type = MSGTYPE_UPDATE_ITEM;
//    msg->clientnum = to;

//    msg->Add( containerID );
//    msg->Add( itemName );
//    msg->Add( icon );
//    msg->Add( (uint32_t)contents[slotID].slotID );
//    msg->Add( stackCount );
//}

psViewItemUpdate::psViewItemUpdate( MsgEntry* me )
{
    containerID = me->GetUInt32();
    slotID = me->GetUInt32();
    clearSlot = me->GetBool();
    name = me->GetStr();
    icon = me->GetStr();
    stackCount = me->GetUInt32();
    ownerID = me->GetUInt32();
}

csString psViewItemUpdate::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Container ID: %d Slot ID: %d Clear Slot? %s Name: '%s' Icon: '%s' Stack Count: %d",
            containerID,
            slotID,
            (clearSlot?"True":"False"),
            name.GetDataSafe(),
            icon.GetDataSafe(),
            stackCount);

    return msgtext;
}

//------------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psReadBookTextMessage,MSGTYPE_READ_BOOK);

psReadBookTextMessage::psReadBookTextMessage(uint32_t clientNum, csString& itemName, csString& bookText, bool canWrite, int slotID, int containerID)
{
    msg = new MsgEntry(itemName.Length()+1 + bookText.Length()+1+1+3*sizeof(uint32_t));
    msg->SetType(MSGTYPE_READ_BOOK);
    msg->clientnum = clientNum;
    msg->Add(itemName);
    msg->Add(bookText);
    msg->Add((uint8_t) canWrite);
    msg->Add(slotID);
    msg->Add(containerID);
}

psReadBookTextMessage::psReadBookTextMessage(MsgEntry* me )
{
  name=me->GetStr();
  text=me->GetStr();
  canWrite = me->GetUInt8() ? true : false;
  slotID = me->GetUInt32();
  containerID = me->GetUInt32();
}

csString psReadBookTextMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Name: '%s' Text: '%s'", name.GetDataSafe(), text.GetDataSafe());

    return msgtext;
}

//------------------------------------------------------------------------------
PSF_IMPLEMENT_MSG_FACTORY(psWriteBookMessage, MSGTYPE_WRITE_BOOK);
psWriteBookMessage::psWriteBookMessage(uint32_t clientNum, csString& title, csString& content, bool success, int slotID, int containerID)
{
  //uint8_t for the flag, a bool, 2 uints for the item reference, content length + 1 for the content
   msg = new MsgEntry(sizeof(uint8_t)+sizeof(bool)+2*sizeof(uint32_t)+title.Length()+1+content.Length()+1);
   msg->SetType(MSGTYPE_WRITE_BOOK);
   msg->clientnum = clientNum;
   msg->Add((uint8_t)RESPONSE);
   msg->Add(success);
   msg->Add(slotID);
   msg->Add(containerID);
   msg->Add(title);
   msg->Add(content);
}

//Request to write on this book
psWriteBookMessage::psWriteBookMessage(int slotID, int containerID)
{
    msg = new MsgEntry(sizeof(uint8_t)+2*sizeof(uint32_t));
    msg->SetType(MSGTYPE_WRITE_BOOK);
    msg->clientnum = 0;
    msg->Add((uint8_t)REQUEST);
    msg->Add(slotID);
    msg->Add(containerID);
}

psWriteBookMessage::psWriteBookMessage(int slotID, int containerID, csString& title, csString& content)
{
    msg = new MsgEntry(sizeof(uint8_t)+2*sizeof(uint32_t)+title.Length()+1+content.Length()+1);
    msg->SetType(MSGTYPE_WRITE_BOOK);
    msg->clientnum = 0;
    msg->Add((uint8_t)SAVE);
    msg->Add(slotID);
    msg->Add(containerID);
    msg->Add(title);
    msg->Add(content);
}

psWriteBookMessage::psWriteBookMessage(MsgEntry* me)
{
  messagetype = me->GetUInt8();
  switch (messagetype)
  {
      case REQUEST:
          slotID = me->GetUInt32();
          containerID = me->GetUInt32();
          break;
      case RESPONSE:
          success = me->GetBool();
          slotID = me->GetUInt32();
          containerID = me->GetUInt32();
          title = me->GetStr();
          content = me->GetStr();
          break;
      case SAVE:
          slotID = me->GetUInt32();
          containerID = me->GetUInt32();
          title = me->GetStr();
          content = me->GetStr();
          break;
  }
}

csString psWriteBookMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;
    switch(messagetype)
    {
       case REQUEST: 
            msgtext.AppendFmt("Write Book REQUEST for slot %d, container %d ", slotID, containerID);
            break;
       case RESPONSE:
            msgtext.AppendFmt("Write Book RESPONSE for slot %d, container %d.  Successful? %s  Title: \"%s\" Content: \"%s\" \n ", 
                              slotID, containerID, success?"true":"false", title.GetDataSafe(), content.GetDataSafe());
            break;
       case SAVE:
            msgtext.AppendFmt("Write Book SAVE for slot %d, container %d. Title: \"%s\" Content: \"%s\" \n ", 
                              slotID, containerID, title.GetDataSafe(), content.GetDataSafe());
            break;
    }

    return msgtext;
}

//------------------------------------------------------------------------------
PSF_IMPLEMENT_MSG_FACTORY(psQuestRewardMessage,MSGTYPE_QUESTREWARD);

csString psQuestRewardMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Quest Reward: '%s' Type: %d", newValue.GetDataSafe(), msgType);

    return msgtext;
}

//------------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psExchangeMoneyMsg,MSGTYPE_EXCHANGE_MONEY);

psExchangeMoneyMsg::psExchangeMoneyMsg( uint32_t client, int container,
                        int trias, int hexas, int circles,int octas )
{
    msg = new MsgEntry( sizeof(int) * 5 );
    msg->SetType(MSGTYPE_EXCHANGE_MONEY);
    msg->clientnum = client;

    msg->Add( (uint32_t)container );
    msg->Add( (uint32_t)trias );
    msg->Add( (uint32_t)hexas );
    msg->Add( (uint32_t)circles );
    msg->Add( (uint32_t)octas );
}


psExchangeMoneyMsg::psExchangeMoneyMsg( MsgEntry* me )
{
    container = me->GetUInt32();
    trias = me->GetUInt32();
    hexas = me->GetUInt32();
    circles = me->GetUInt32();
    octas = me->GetUInt32();
}

csString psExchangeMoneyMsg::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Container: %d %d trias, %d hexas, %d circles, %d octas",
            container, trias, hexas, circles, octas);

    return msgtext;
}

//------------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psExchangeRequestMsg,MSGTYPE_EXCHANGE_REQUEST);

psExchangeRequestMsg::psExchangeRequestMsg( bool withPlayer )
{
    msg = new MsgEntry(1 + 1);
    msg->SetType(MSGTYPE_EXCHANGE_REQUEST);
    msg->clientnum = 0;

    msg->Add("");
    msg->Add(withPlayer);
}

psExchangeRequestMsg::psExchangeRequestMsg(uint32_t client, csString& playerName, bool withPlayer)
{
    msg = new MsgEntry( playerName.Length() + 1 + 1);
    msg->SetType(MSGTYPE_EXCHANGE_REQUEST);
    msg->clientnum = client;

    msg->Add( playerName );
    msg->Add( withPlayer );
}


psExchangeRequestMsg::psExchangeRequestMsg( MsgEntry* me )
{
    player = me->GetStr();
    withPlayer = me->GetBool();
}

csString psExchangeRequestMsg::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Player: '%s' WithPlayer: %s", player.GetDataSafe(), (withPlayer?"True":"False"));

    return msgtext;
}

//------------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psExchangeAddItemMsg,MSGTYPE_EXCHANGE_ADD_ITEM);

psExchangeAddItemMsg::psExchangeAddItemMsg( uint32_t clientNum,
                                            const csString& name,
                                            int containerID,
                                            int slot,
                                            int stackcount,
                                            const csString& icon )
{
    msg = new MsgEntry(1000);
    msg->SetType(MSGTYPE_EXCHANGE_ADD_ITEM);
    msg->clientnum = clientNum;

    msg->Add( name );
    msg->Add( (uint32_t) containerID );
    msg->Add( (uint32_t) slot );
    msg->Add( (uint32_t) stackcount );
    msg->Add( icon );
    msg->ClipToCurrentSize();
}

psExchangeAddItemMsg::psExchangeAddItemMsg( MsgEntry* me )
{
    name        = me->GetStr();
    container   = me->GetUInt32();
    slot        = me->GetUInt32();
    stackCount  = me->GetUInt32();
    icon        = me->GetStr();
}

csString psExchangeAddItemMsg::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Name: '%s' Container: %d Slot: %d Stack Count: %d Icon: '%s'",
            name.GetDataSafe(), container, slot, stackCount, icon.GetDataSafe());

    return msgtext;
}

//------------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psExchangeRemoveItemMsg,MSGTYPE_EXCHANGE_REMOVE_ITEM);

psExchangeRemoveItemMsg::psExchangeRemoveItemMsg( uint32_t client, int container, int slot, int newStack )
{
    msg = new MsgEntry( sizeof( int ) * 3 );
    msg->SetType(MSGTYPE_EXCHANGE_REMOVE_ITEM);
    msg->clientnum = client;

    msg->Add( (uint32_t)container );
    msg->Add( (uint32_t)slot );
    msg->Add( (uint32_t)newStack );
}

psExchangeRemoveItemMsg::psExchangeRemoveItemMsg( MsgEntry* msg )
{
    container       = msg->GetUInt32();
    slot            = msg->GetUInt32();
    newStackCount   = msg->GetUInt32();
}


csString psExchangeRemoveItemMsg::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Container: %d Slot %d Stack Count: %d", container, slot, newStackCount);

    return msgtext;
}

//------------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psExchangeAcceptMsg,MSGTYPE_EXCHANGE_ACCEPT);

psExchangeAcceptMsg::psExchangeAcceptMsg( uint32_t client )
{
    msg = new MsgEntry();
    msg->SetType(MSGTYPE_EXCHANGE_ACCEPT);
    msg->clientnum = client;
}

psExchangeAcceptMsg::psExchangeAcceptMsg( MsgEntry* me )
{
}

csString psExchangeAcceptMsg::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Exchange Accept");

    return msgtext;
}

//------------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psExchangeStatusMsg,MSGTYPE_EXCHANGE_STATUS);

psExchangeStatusMsg::psExchangeStatusMsg( uint32_t client, bool playerAccept, bool targetAccept )
{
    msg = new MsgEntry( sizeof(bool)*2 );
    msg->SetType(MSGTYPE_EXCHANGE_STATUS);
    msg->clientnum = client;
    msg->Add( playerAccept );
    msg->Add( targetAccept );

}

psExchangeStatusMsg::psExchangeStatusMsg( MsgEntry* me )
{
    playerAccept = me->GetBool();
    otherAccept = me->GetBool();
}

csString psExchangeStatusMsg::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Player %s, Other Player %s",
            (playerAccept?"accepted":"rejected"),
        (otherAccept?"accepted":"rejected"));

    return msgtext;
}

//------------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psExchangeEndMsg,MSGTYPE_EXCHANGE_END);

psExchangeEndMsg::psExchangeEndMsg( uint32_t client )
{
    msg = new MsgEntry();
    msg->SetType(MSGTYPE_EXCHANGE_END);
    msg->clientnum = client;
}

psExchangeEndMsg::psExchangeEndMsg( MsgEntry* me )
{
}

csString psExchangeEndMsg::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Exchange End");

    return msgtext;
}

//------------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psUpdateObjectNameMessage,MSGTYPE_NAMECHANGE);

psUpdateObjectNameMessage::psUpdateObjectNameMessage( uint32_t client,uint32_t ID, const char* newName )
{
    msg = new MsgEntry( strlen(newName)+1 + sizeof(uint32_t));

    msg->SetType(MSGTYPE_NAMECHANGE);
    msg->clientnum = client;

    msg->Add( ID );
    msg->Add( newName );

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psUpdateObjectNameMessage::psUpdateObjectNameMessage( MsgEntry* me )
{
    objectID      = me->GetUInt32();
    newObjName    = me->GetStr();
}

csString psUpdateObjectNameMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Object ID: %d New Name: '%s'", objectID, newObjName.GetDataSafe());

    return msgtext;
}

//------------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psUpdatePlayerGuildMessage,MSGTYPE_GUILDCHANGE);

psUpdatePlayerGuildMessage::psUpdatePlayerGuildMessage( uint32_t client,int tot, const char* newGuild,bool totalIsClient )
{
    int total = 1;
    if(!totalIsClient)
        total = tot;

    msg = new MsgEntry( strlen(newGuild)+1 + (sizeof(uint32_t) * (total+1) ));

    msg->SetType(MSGTYPE_GUILDCHANGE);
    msg->clientnum = client;

    msg->Add((uint32_t)total);
    msg->Add( newGuild );

    valid = false; // need to add first

    if(totalIsClient)
        AddPlayer(tot);
}

psUpdatePlayerGuildMessage::psUpdatePlayerGuildMessage( MsgEntry* me )
{
    int total     = (int)me->GetUInt32();
    newGuildName  = me->GetStr();

    for(int i = 0; i < total; i++)
        objectID.Push(me->GetUInt32());
}

void psUpdatePlayerGuildMessage::AddPlayer(uint32_t id)
{
    msg->Add(id);

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

csString psUpdatePlayerGuildMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("New Guild Name: '%s' Player IDs: ", newGuildName.GetDataSafe());

    for (size_t i = 0; i < objectID.GetSize(); i++)
    {
        msgtext.AppendFmt("%d, ", objectID[i]);
    }

    return msgtext;
}

//------------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psUpdatePlayerGroupMessage,MSGTYPE_GROUPCHANGE);

psUpdatePlayerGroupMessage::psUpdatePlayerGroupMessage( int clientnum, uint32_t objectID, uint32_t groupID)
{
    msg = new MsgEntry(sizeof(uint32_t)*2);

    msg->SetType(MSGTYPE_GROUPCHANGE);
    msg->clientnum = clientnum;

    msg->Add( objectID );
    msg->Add( groupID );

    // Sets valid flag based on message overrun state
    valid=!(msg->overrun);
}

psUpdatePlayerGroupMessage::psUpdatePlayerGroupMessage( MsgEntry* me )
{
    objectID      = me->GetUInt32();
    groupID       = me->GetUInt32();
}

csString psUpdatePlayerGroupMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Object ID: %d Group ID: %d", objectID, groupID);

    return msgtext;
}

//------------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psNameCheckMessage,MSGTYPE_CHAR_CREATE_NAME);

psNameCheckMessage::psNameCheckMessage( const char* newname )
{
    csString name(newname);
    msg = new MsgEntry( strlen(newname)+2);

    msg->SetType(MSGTYPE_CHAR_CREATE_NAME);

    csString lastname,firstname(name.Slice(0,name.FindFirst(' ')));
    if (name.FindFirst(' ') != SIZET_NOT_FOUND)
        lastname = name.Slice(name.FindFirst(' ')+1,name.Length());

    msg->Add( firstname );
    msg->Add( lastname );
}


psNameCheckMessage::psNameCheckMessage( uint32_t client, bool accept, const char* reason )
{
    msg = new MsgEntry( sizeof(bool) + strlen(reason)+1 );

    msg->SetType(MSGTYPE_CHAR_CREATE_NAME);
    msg->clientnum = client;
    msg->Add( accept );
    msg->Add( reason );
}

psNameCheckMessage::psNameCheckMessage( MsgEntry* me )
{
    /*
      TODO: Check if this is this way or the other way around.
    if (me->clientnum)
        FromClient(me);
    msgFromServer = false;
    else
        FromServer(me);
    msgFromServer = true;
    */
}


void psNameCheckMessage::FromClient( MsgEntry* me )
{
    firstName = me->GetStr();
    lastName = me->GetStr();
}

void psNameCheckMessage::FromServer( MsgEntry* me )
{
    accepted = me->GetBool();
    reason = me->GetStr();
}

csString psNameCheckMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("TODO");
/* When the FromClient/FromServer issue is sorted in psNameCheckMessage(MsgEntry *) function...
    if (msgFromServer)
    {
        msgtext.AppendFmt("Name Check: %s Reason: '%s'",
                (accepted?"Accepted":"Rejected"), reason.GetDataSafe);
    }
    else
    {
       msgText.AppendFmt("Name: '%s %s'", firstName.GetDataSafe(), lastName.GetDataSafe());
    }
*/
    return msgtext;
}

// ---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psPingMsg,MSGTYPE_PING);

psPingMsg::psPingMsg( MsgEntry* me )
{
    id = me->GetInt32();
    flags = me->GetUInt8();
}

psPingMsg::psPingMsg( uint32_t client, uint32_t id, uint8_t flags )
{
    msg = new MsgEntry( sizeof(uint32_t) + sizeof(uint8_t) ,PRIORITY_LOW );

    msg->SetType(MSGTYPE_PING);
    msg->clientnum = client;
    msg->Add( id );
    msg->Add( flags );
}

csString psPingMsg::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("ID: %d flags:", id);
    if (flags & PINGFLAG_REQUESTFLAGS)
        msgtext.Append(" REQUESTFLAGS");
    if (flags & PINGFLAG_READY)
        msgtext.Append(" READY");
    if (flags & PINGFLAG_HASBEENREADY)
        msgtext.Append(" HASBEENREADY");
    if (flags & PINGFLAG_SERVERFULL)
        msgtext.Append(" SERVERFULL");

    return msgtext;
}

// ---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psHeartBeatMsg,MSGTYPE_HEART_BEAT);

psHeartBeatMsg::psHeartBeatMsg( MsgEntry* me )
{
}

psHeartBeatMsg::psHeartBeatMsg( uint32_t client )
{
    msg = new MsgEntry( 0 ,PRIORITY_HIGH );

    msg->SetType(MSGTYPE_HEART_BEAT);
    msg->clientnum = client;
}

csString psHeartBeatMsg::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Alive?");

    return msgtext;
}

// ---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psLockpickMessage,MSGTYPE_LOCKPICK);

psLockpickMessage::psLockpickMessage(const char* password)
{
    msg = new MsgEntry( strlen(password) +1 );

    msg->SetType(MSGTYPE_LOCKPICK);
    msg->clientnum = 0;
    msg->Add( password );
}

psLockpickMessage::psLockpickMessage( MsgEntry* me )
{
    password = me->GetStr();
}

csString psLockpickMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Password: '%s'", password.GetDataSafe());

    return msgtext;
}

// ---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psGMSpawnItems,MSGTYPE_GMSPAWNITEMS);

psGMSpawnItems::psGMSpawnItems(uint32_t client,const char* type,unsigned int size)
{
    msg = new MsgEntry(
                        strlen(type) +1 +
                        sizeof(bool) + size + sizeof(uint32_t)
                        );

    msg->SetType(MSGTYPE_GMSPAWNITEMS);
    msg->clientnum = client;
    msg->Add( type );
    msg->Add( false );
}

psGMSpawnItems::psGMSpawnItems(const char* type)
{
    msg = new MsgEntry(
                        strlen(type) +1 +
                        sizeof(bool)
                        );

    msg->SetType(MSGTYPE_GMSPAWNITEMS);
    msg->clientnum = 0;
    msg->Add( type );
    msg->Add( true );
}

psGMSpawnItems::psGMSpawnItems(MsgEntry* me)
{
    type = me->GetStr();
    request = me->GetBool();

    if(!request)
    {
        unsigned int length = me->GetUInt32();
        for(unsigned int i = 0;i < length;i++)
        {
            Item item;
            item.name = me->GetStr();
            item.mesh = me->GetStr();
            item.icon = me->GetStr();

            items.Push(item);
        }
    }
}

csString psGMSpawnItems::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Type: '%s' ", type.GetDataSafe());

#ifdef FULL_DEBUG_DUMP
    if (!request)
    {
        for (size_t i = 0; i < items.GetSize(); i++)
        {
            msgtext.AppendFmt("Name: '%s' Mesh: '%s', ",
                    items[i].name.GetDataSafe(),
                    items[i].mesh.GetDataSafe());
        }
    }
#endif

    return msgtext;
}

//---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psGMSpawnTypes,MSGTYPE_GMSPAWNTYPES);

psGMSpawnTypes::psGMSpawnTypes(uint32_t client,unsigned int size)
{
    msg = new MsgEntry(size + sizeof(uint32_t));

    msg->SetType(MSGTYPE_GMSPAWNTYPES);
    msg->clientnum = client;
}

psGMSpawnTypes::psGMSpawnTypes(MsgEntry* me)
{
    unsigned int length = me->GetUInt32();
    for(unsigned int i = 0;i < length;i++)
    {
        types.Push(me->GetStr());
    }
}

csString psGMSpawnTypes::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Types: ");

    for (size_t i = 0; i < types.GetSize(); i++)
    {
        msgtext.AppendFmt("'%s', ", types[i].GetDataSafe());
    }

    return msgtext;
}

//---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psGMSpawnItem,MSGTYPE_GMSPAWNITEM);

psGMSpawnItem::psGMSpawnItem(const char* item,
                             unsigned int count,
                             bool lockable,
                             bool locked,
                             const char* lskill,
                             int lstr,
                             bool pickupable
                             )
{
    msg = new MsgEntry(
                        strlen(item) +1
                        + sizeof(uint32_t)
                        + (sizeof(bool) *2)
                        + strlen(lskill)+1
                        + sizeof(int32_t)
                        + sizeof(bool)
                      );

    msg->SetType(MSGTYPE_GMSPAWNITEM);
    msg->clientnum = 0;
    msg->Add( item );
    msg->Add( (uint32_t)count );
    msg->Add( lockable);
    msg->Add( locked );
    msg->Add( lskill );
    msg->Add( (int32_t)lstr );
    msg->Add( pickupable );
}

psGMSpawnItem::psGMSpawnItem(MsgEntry *me)
{
    item = me->GetStr();
    count = me->GetUInt32();
    lockable = me->GetBool();
    locked = me->GetBool();
    lskill = me->GetStr();
    lstr = me->GetInt32();
    pickupable = me->GetBool();
}

csString psGMSpawnItem::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Item: '%s' Count: %d Lockable: %s, Is %s, Skill: '%s' Str: %d Pickupable: %s",
            item.GetDataSafe(),
        count,
        (lockable ? "True" : "False"),
        (locked ? "Locked" : "Unlocked"),
        lskill.GetDataSafe(),
        lstr,
        (pickupable ? "True" : "False"));

    return msgtext;
}

//---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psLootRemoveMessage,MSGTYPE_LOOTREMOVE);

psLootRemoveMessage::psLootRemoveMessage( uint32_t client,int item )
{
    msg = new MsgEntry(
                        sizeof(int)
                        );

    msg->SetType(MSGTYPE_LOOTREMOVE);
    msg->clientnum = client;
    msg->Add( (int32_t)item );
}

psLootRemoveMessage::psLootRemoveMessage(MsgEntry *me)
{
    id = me->GetInt32();
}

csString psLootRemoveMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("ID: %d", id);

    return msgtext;
}

//---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psCharCreateTraitsMessage,MSGTYPE_CHAR_CREATE_TRAITS);

csString psCharCreateTraitsMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Data: '%s'",string.GetDataSafe());

    return msgtext;
}

//---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psClientStatusMessage,MSGTYPE_CLIENTSTATUS);

psClientStatusMessage::psClientStatusMessage(bool ready)
{
    msg  = new MsgEntry(sizeof(uint8_t));

    msg->SetType(MSGTYPE_CLIENTSTATUS);
    msg->clientnum      = 0; // To server only

    msg->Add((uint8_t) READY);

    valid=true;
}

psClientStatusMessage::psClientStatusMessage(MsgEntry *message)
{
    uint8_t status = message->GetUInt8();

    ready = status & READY;

    // Sets valid flag based on message overrun state
    valid=!(message->overrun);
}

csString psClientStatusMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Ready: %s",(ready?"true":"false"));

    return msgtext;
}

//---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psMoveModMsg,MSGTYPE_MOVEMOD);

psMoveModMsg::psMoveModMsg(uint32_t client, ModType type, const csVector3& move, float Yrot)
{
    msg = new MsgEntry(1 + 4*sizeof(uint32));
    msg->SetType(MSGTYPE_MOVEMOD);
    msg->clientnum = client;

    msg->Add((uint8_t)type);

    if (type != NONE)
    {
        msg->Add(move);
        msg->Add(Yrot);
    }
    else
    {
        msg->ClipToCurrentSize();
    }

    valid = !(msg->overrun);
}

psMoveModMsg::psMoveModMsg(MsgEntry* me)
{
    type = (ModType)me->GetUInt8();

    if (type != NONE)
    {
        movementMod = me->GetVector();
        rotationMod = me->GetFloat();
    }
    else
    {
        movementMod = 0.0f;
        rotationMod = 0.0f;
    }
}

csString psMoveModMsg::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("type %d move(%.2f,%.2f,%.2f) rot(%.2f)",
                      (int)type,
                      movementMod.x, movementMod.y, movementMod.z,
                      rotationMod );

    return msgtext;
}

//---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psMsgRequestMovement,MSGTYPE_REQUESTMOVEMENTS);

psMsgRequestMovement::psMsgRequestMovement()
{
    msg = new MsgEntry(10);
    msg->SetType(MSGTYPE_REQUESTMOVEMENTS);
    msg->clientnum = 0;
    msg->ClipToCurrentSize();
}

psMsgRequestMovement::psMsgRequestMovement(MsgEntry * /*me*/)
{
}

csString psMsgRequestMovement::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;
    msgtext.Format("Requesting movements");
    return msgtext;
}

//---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psMovementInfoMessage,MSGTYPE_MOVEINFO);

psMovementInfoMessage::psMovementInfoMessage(size_t modes, size_t moves)
{
    msg = new MsgEntry(10000);
    msg->SetType(MSGTYPE_MOVEINFO);
    msg->clientnum = 0;

    this->modes = modes;
    this->moves = moves;

    msg->Add((uint32_t)modes);
    msg->Add((uint32_t)moves);
}

psMovementInfoMessage::psMovementInfoMessage(MsgEntry * me)
{
    msg = me;
    msg->IncRef();

    modes = msg->GetUInt32();
    moves = msg->GetUInt32();
}

void psMovementInfoMessage::AddMode(uint32 id, const char* name, csVector3 move_mod, csVector3 rotate_mod, const char* idle_anim)
{
    msg->Add(id);
    msg->Add(name);
    msg->Add(move_mod);
    msg->Add(rotate_mod);
    msg->Add(idle_anim);
}

void psMovementInfoMessage::AddMove(uint32 id, const char* name, csVector3 base_move, csVector3 base_rotate)
{
    msg->Add(id);
    msg->Add(name);
    msg->Add(base_move);
    msg->Add(base_rotate);
}

void psMovementInfoMessage::GetMode(uint32 &id, const char* &name, csVector3 &move_mod, csVector3 &rotate_mod, const char* &idle_anim)
{
    id = msg->GetUInt32();
    name = msg->GetStr();
    move_mod = msg->GetVector();
    rotate_mod = msg->GetVector();
    idle_anim = msg->GetStr();
}

void psMovementInfoMessage::GetMove(uint32 &id, const char* &name, csVector3 &base_move, csVector3 &base_rotate)
{
    id = msg->GetUInt32();
    name = msg->GetStr();
    base_move = msg->GetVector();
    base_rotate = msg->GetVector();
}

csString psMovementInfoMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;
    msgtext.Format("%zu modes and %zu moves",modes,moves);
    return msgtext;
}

//---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psMsgCraftingInfo,MSGTYPE_CRAFT_INFO);


csString psMsgCraftingInfo::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("CraftInfo: %s", craftInfo.GetData());

    return msgtext;
}

//---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psTraitChangeMessage,MSGTYPE_CHANGE_TRAIT);

csString psTraitChangeMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Target: %d String: '%s'", target, string.GetDataSafe());

    return msgtext;
}

//---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psUpdateInfo,MSGTYPE_UPDATE_CHECK);

csString psUpdateInfo::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Timestamp: %d", timestamp);

    return msgtext;
}

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psTutorialMessage,MSGTYPE_TUTORIAL);

csString psTutorialMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Which Message: %d Instructions: '%s'", whichMessage, instrs.GetDataSafe());

    return msgtext;
}

PSF_IMPLEMENT_MSG_FACTORY(psSketchMessage,MSGTYPE_VIEW_SKETCH);

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psMGStartStopMessage, MSGTYPE_MINIGAME_STARTSTOP);

psMGStartStopMessage::psMGStartStopMessage(uint32_t client, bool start)
{
    msg = new MsgEntry(sizeof(bool));

    msg->SetType(MSGTYPE_MINIGAME_STARTSTOP);
    msg->clientnum = client;

    msg->Add(start);
}

psMGStartStopMessage::psMGStartStopMessage(MsgEntry *me)
{
    msgStart = me->GetBool();
}

csString psMGStartStopMessage::ToString(AccessPointers * /* access_ptrs */)
{
    csString msgText;
    msgText.AppendFmt("Start: %s", msgStart ? "Yes" : "No");
    return msgText;
}

//---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psMGBoardMessage, MSGTYPE_MINIGAME_BOARD);

psMGBoardMessage::psMGBoardMessage(uint32_t client, uint8_t counter,
                                   uint32_t gameID, uint16_t options, int8_t cols, int8_t rows, uint8_t *layout,
                                   uint8_t numOfPieces, uint8_t *pieces)
    : msgLayout(0)
{
    // We need tiles/2 number of bytes and one extra byte for odd number of tiles
    int layoutSize = cols * rows / 2;
    if (cols * rows % 2 != 0)
        layoutSize++;

    int piecesSize = numOfPieces / 2;
    if (numOfPieces % 2 != 0)
        piecesSize++;

    msg = new MsgEntry(
            sizeof(uint8_t) +       // counter
            sizeof(uint32_t) +      // game ID
            sizeof(uint16_t) +      // options
            sizeof(int8_t) +        // cols
            sizeof(int8_t) +        // rows
            sizeof(uint32_t) +      // number of bytes in layout
            layoutSize +            // layout
            sizeof(uint8_t) +       // number of available pieces
            sizeof(uint32_t) +      // number of bytes in the pieces array
            piecesSize);            // available pieces

    msg->SetType(MSGTYPE_MINIGAME_BOARD);
    msg->clientnum = client;

    msg->Add(counter);
    msg->Add(gameID);
    msg->Add(options);
    msg->Add(cols);
    msg->Add(rows);
    msg->Add(layout, layoutSize);
    msg->Add(numOfPieces);
    msg->Add(pieces, piecesSize);

}

psMGBoardMessage::psMGBoardMessage(MsgEntry *me)
    : msgLayout(0)
{
    msg = me;
    msg->IncRef();

    msgCounter = msg->GetUInt8();
    msgGameID = msg->GetUInt32();
    msgOptions = msg->GetUInt16();
    msgCols = msg->GetInt8();
    msgRows = msg->GetInt8();

    uint32_t tmp = 0;
    msgLayout = (uint8_t *)msg->GetData(&tmp);

    msgNumOfPieces = msg->GetInt8();
    msgPieces = (uint8_t *)msg->GetData(&tmp);
}

bool psMGBoardMessage::IsNewerThan(uint8_t oldCounter)
{
    return (uint8_t)(msgCounter-oldCounter) <= 127;
}

csString psMGBoardMessage::ToString(AccessPointers * /* access_ptrs */)
{
    csString msgText;
    msgText.AppendFmt("GameID: %u", msgGameID);
    msgText.AppendFmt(" Options: 0x%04X", msgOptions);
    msgText.AppendFmt(" Rows: %d", msgRows);
    msgText.AppendFmt(" Cols: %d", msgCols);
    msgText.Append(" Layout: ");
    if (msgLayout)
    {
        int layoutSize = msgCols * msgRows / 2;
        if (msgCols * msgRows % 2 != 0)
            layoutSize++;
        for (int i = 0; i < layoutSize; i++)
            msgText.AppendFmt("%02X", msgLayout[i]);
    }
    else
    {
        msgText.Append("<default>");
    }
    msgText.Append(" Pieces: %d ", msgNumOfPieces);
    if (msgNumOfPieces > 0)
    {
        for (int i = 0; i < msgNumOfPieces; i++)
            msgText.AppendFmt("%02X", msgPieces[i]);
    }
    else
    {
        msgText.Append("<default>");
    }
    return msgText;
}


//---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psEntranceMessage, MSGTYPE_ENTRANCE);

psEntranceMessage::psEntranceMessage(uint32_t entranceID)
{
    msg = new MsgEntry( sizeof(int32_t));
    msg->SetType(MSGTYPE_ENTRANCE);
    msg->clientnum = 0;

    msg->Add( (int32_t)entranceID );
}

psEntranceMessage::psEntranceMessage( MsgEntry* me )
{
    entranceID = me->GetUInt32();
}

csString psEntranceMessage::ToString(AccessPointers * /* access_ptrs */)
{
    csString msgText;
    msgText.AppendFmt("EntranceID: %u", entranceID);
    return msgText;
}


//---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psMGUpdateMessage, MSGTYPE_MINIGAME_UPDATE);

psMGUpdateMessage::psMGUpdateMessage(uint32_t client, uint8_t counter,
                                     uint32_t gameID, uint8_t numUpdates, uint8_t *updates)
    : msgUpdates(0)
{
    msg = new MsgEntry(
            sizeof(uint8_t) +       // counter
            sizeof(uint32_t) +      // game iD
            sizeof(uint8_t) +       // numUpdates
            sizeof(uint32_t) +      // number of bytes in updates
            2*numUpdates);          // updates

    msg->SetType(MSGTYPE_MINIGAME_UPDATE);
    msg->clientnum = client;

    msg->Add(counter);
    msg->Add(gameID);
    msg->Add(numUpdates);
    msg->Add(updates, 2*numUpdates);

}

psMGUpdateMessage::psMGUpdateMessage(MsgEntry *me)
    : msgUpdates(0)
{
    msg = me;
    msg->IncRef();

    msgCounter = msg->GetUInt8();
    msgGameID = msg->GetUInt32();
    msgNumUpdates = msg->GetUInt8();

    uint32_t tmp = 0;
    msgUpdates = (uint8_t *)msg->GetData(&tmp);
}

bool psMGUpdateMessage::IsNewerThan(uint8_t oldCounter)
{
    return (uint8_t)(msgCounter-oldCounter) <= 127;
}

csString psMGUpdateMessage::ToString(AccessPointers * /* access_ptrs */)
{
    csString msgText;
    msgText.AppendFmt("GameID: %u", msgGameID);
    msgText.AppendFmt(" NumUpdates: %u", msgNumUpdates);
    msgText.Append(" Updates: ");
    if (msgUpdates)
    {
        for (uint8_t i = 0; i < msgNumUpdates; i++)
        {
            int col = (int)((msgUpdates[2*i] & 0xF0) >> 4);
            int row = (int)(msgUpdates[2*i] & 0x0F);
            msgText.AppendFmt("(%d,%d)=%X", col, row, msgUpdates[2*i + 1]);
        }
    }
    else
    {
        msgText.Append("<null>");
    }
    return msgText;
}

//---------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psGMEventListMessage, MSGTYPE_GMEVENT_LIST);

psGMEventListMessage::psGMEventListMessage()
{
    msg = NULL;
    valid = false;
}

psGMEventListMessage::psGMEventListMessage(MsgEntry* msg)
{
    if (!msg)
        return;
    
    gmEventsXML = msg->GetStr();
    valid =! (msg->overrun);
}

void psGMEventListMessage::Populate(csString& gmeventStr, int clientnum)
{
    msg = new MsgEntry(sizeof(int)+gmeventStr.Length()+1);

    msg->SetType(MSGTYPE_GMEVENT_LIST);
    msg->clientnum = clientnum;
    
    msg->Add(gmeventStr);

    valid=!(msg->overrun);
}

csString psGMEventListMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("XML: \'%s\'", gmEventsXML.GetDataSafe());

    return msgtext;
}
//--------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psGMEventInfoMessage,MSGTYPE_GMEVENT_INFO);

psGMEventInfoMessage::psGMEventInfoMessage(int cnum, int cmd, int id, const char *name, const char *info)
{
    if (name)
    {
        csString escpxml_info = EscpXML(info);
        xml.Format("<QuestNotebook><Description text=\"%s\"/></QuestNotebook>",escpxml_info.GetData());
    }
    else
        xml = "";

    msg = new MsgEntry(sizeof(int32_t) + sizeof(uint8_t) + xml.Length() + 1);

    msg->SetType(MSGTYPE_GMEVENT_INFO);
    msg->clientnum = cnum;

    msg->Add( (uint8_t) cmd );

    if (cmd == CMD_QUERY || cmd == CMD_DISCARD)
    {
        msg->Add( (int32_t) id );
    }
    else if (cmd == CMD_INFO)
    {
        msg->Add( xml );
    }
    msg->ClipToCurrentSize();

    valid=!(msg->overrun);
}

psGMEventInfoMessage::psGMEventInfoMessage(MsgEntry* msg)
{
    command = msg->GetUInt8();
    if (command == CMD_QUERY || command == CMD_DISCARD)
        id = msg->GetInt32();
    else if (command == CMD_INFO)
        xml = msg->GetStr();
    valid=!(msg->overrun);
}


csString psGMEventInfoMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Command: %d", command);
    if (command == CMD_QUERY || command == CMD_DISCARD)
        msgtext.AppendFmt(" Id: %d", id);
    else if (command == CMD_INFO)
        msgtext.AppendFmt(" XML: '%s'", xml.GetDataSafe());

    return msgtext;
}

//-----------------------------------------------------------------------------


psFactionMessage::psFactionMessage(int cnum, int cmd)
{	
	this->client = cnum;
	this->cmd = cmd;

}


psFactionMessage::psFactionMessage(MsgEntry* message)
{
	cmd			= message->GetInt8();
	int facts   = message->GetInt32();

	for ( int z = 0; z < facts; z++ )
	{
		psFactionMessage::FactionPair *fp = new psFactionMessage::FactionPair;
		fp->faction = message->GetStr();
		fp->rating  = message->GetInt32();

		factionInfo.Push(fp);
	}    
}


void psFactionMessage::AddFaction( csString factionName, int rating )
{
	psFactionMessage::FactionPair *pair = new psFactionMessage::FactionPair;
	pair->faction = factionName;
	pair->rating = rating;

	factionInfo.Push(pair);
}


void psFactionMessage::BuildMsg()
{	
	size_t size = sizeof(uint8_t)+sizeof(int32_t);

	for ( size_t z = 0; z < factionInfo.GetSize(); z++ )
	{
		size += factionInfo[z]->faction.Length()+1;
		size += sizeof(int32_t);
	}

	msg = new MsgEntry(size);
    msg->SetType(MSGTYPE_FACTION_INFO);
    msg->clientnum = client;

    msg->Add((uint8_t)cmd);
	msg->Add((int32_t)factionInfo.GetSize());

	for(size_t i = 0; i < factionInfo.GetSize(); i++)
	{
		msg->Add(factionInfo[i]->faction);
		msg->Add((int32_t)factionInfo[i]->rating);
	}

	valid=!(msg->overrun);
}

csString psFactionMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;
    msgtext.AppendFmt("Command: %d", cmd);
    return msgtext;
}

PSF_IMPLEMENT_MSG_FACTORY(psFactionMessage, MSGTYPE_FACTION_INFO);






//---------------------------------------------------------------------------

/* TODO: When all messages are converted to use PSF_
   this case statement should be deleted. Possibly
   replace with a new function in the PSF that return
   the message type in a string instead of looping
   through the factory list.
 */
csString GetMsgTypeName(int msgType)
{
    return psfMsgTypeName(msgType);
}

csString GetDecodedMessage(MsgEntry* me,csStringHash* msgstrings, iEngine *engine, bool filterhex)
{
    csString msgtext;
    MsgEntry msg(me); // Take a copy to make sure we dont destroy the message.
                      // Can't do this const since current pointers are modified
                      // when parsing messages.
    psMessageCracker * cracker = NULL;


    csString msgname = GetMsgTypeName(me->bytes->type).GetDataSafe();
    msgname.AppendFmt("(%d)",me->bytes->type);

    msgtext.AppendFmt("%7d %-32s %c %8d %4d",csGetTicks(),
                      msgname.GetData(),
                      (me->priority==PRIORITY_LOW?'L':'H'),
                      me->clientnum,me->bytes->size);


    psMessageCracker::AccessPointers access_pointers;
    access_pointers.msgstrings = msgstrings;
    access_pointers.engine = engine;

    // First print the hex of the message if not filtered
    if (!filterhex)
    {
        msgtext.Append(" : ");
        size_t size = me->bytes->GetSize();

        for (size_t i = 0; i < size; i++)
        {
            msgtext.AppendFmt(" %02X",(unsigned char)me->bytes->payload[i]);
        }
    }

    // Than get the cracker and print the decoded message from the ToString function.
    cracker = psfCreateMsg(me->bytes->type,&msg,&access_pointers);
    if (cracker)
    {
        msgtext.Append(" > ");
        msgtext.Append(cracker->ToString(&access_pointers));

        delete cracker;
    }

    return msgtext;
}

typedef struct
{
    int msgtype;
    csString msgtypename;
    psfMsgFactoryFunc factoryfunc;
} MsgFactoryItem;

typedef struct
{
    csPDelArray<MsgFactoryItem> messages;
} MsgFactory;

static MsgFactory * msgfactory = NULL;

class MsgFactoryImpl
{
public:
    MsgFactoryImpl()
    {
        if (msgfactory == NULL) msgfactory = new MsgFactory;
    }

    virtual ~MsgFactoryImpl()
    {
        if (msgfactory) delete msgfactory;
        msgfactory = NULL;
    }
} psfMsgFactoryImpl;

MsgFactoryItem * psfFindFactory(int msgtype)
{
    for (size_t n = 0; n < msgfactory->messages.GetSize(); n++)
    {
        if (msgfactory->messages[n]->msgtype == msgtype)
            return msgfactory->messages[n];
    }
    return NULL;
}

MsgFactoryItem * psfFindFactory(const char* msgtypename)
{
    for (size_t n = 0; n < msgfactory->messages.GetSize(); n++)
    {
        if (msgfactory->messages[n]->msgtypename == msgtypename)
            return msgfactory->messages[n];
    }
    return NULL;
}


void psfRegisterMsgFactoryFunction(psfMsgFactoryFunc factoryfunc, int msgtype, const char * msgtypename)
{
    if (msgfactory == NULL) msgfactory = new MsgFactory;

    MsgFactoryItem * factory = psfFindFactory(msgtype);
    if (factory)
    {
        Error2("Multiple factories for %s",msgtypename);
        return;
    }

    MsgFactoryItem * newfac = new MsgFactoryItem;
    newfac->factoryfunc = factoryfunc;
    newfac->msgtype = msgtype;
    newfac->msgtypename = msgtypename;

    msgfactory->messages.Push(newfac);
}

psMessageCracker* psfCreateMsg(int msgtype,
                               MsgEntry* me,
                               psMessageCracker::AccessPointers * access_ptrs )
{
    if (!msgfactory) return NULL;

    MsgFactoryItem * factory = psfFindFactory(msgtype);
    if (factory)
        return factory->factoryfunc(me,access_ptrs);

    return NULL;
}

csString psfMsgTypeName(int msgtype)
{
    if (!msgfactory) return "No factory";

    MsgFactoryItem * factory = psfFindFactory(msgtype);
    if (factory)
        return factory->msgtypename;

    return csString().Format("unknown (type=%i)", msgtype);
}

int psfMsgType(const char * msgtypename)
{
    if (!msgfactory) return -1;

    MsgFactoryItem * factory = psfFindFactory(msgtypename);
    if (factory)
        return factory->msgtype;

    return -1;
}

//--------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psSequenceMessage,MSGTYPE_SEQUENCE);

psSequenceMessage::psSequenceMessage(int cnum, const char *name, int cmd, int count)
{
    msg = new MsgEntry(strlen(name) + 1 + sizeof(uint8_t) + sizeof(int32_t));

    msg->SetType(MSGTYPE_SEQUENCE);
    msg->clientnum = cnum;

    msg->Add( name );
    msg->Add( (uint8_t)cmd );
    msg->Add( (int32_t) count );
    msg->ClipToCurrentSize();

    valid=!(msg->overrun);
}

psSequenceMessage::psSequenceMessage(MsgEntry* msg)
{
    name    = msg->GetStr();
    command = msg->GetUInt8();
    count   = msg->GetInt32();

    valid=!(msg->overrun);
}


csString psSequenceMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;

    msgtext.AppendFmt("Sequence: '%s' Cmd: %d Count: %d", 
                      name.GetDataSafe(),command,count);

    return msgtext;
}

//-----------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psPlaySoundMessage, MSGTYPE_PLAYSOUND);

psPlaySoundMessage::psPlaySoundMessage(uint32_t clientnum, csString snd)
{
    msg = new MsgEntry(sound.Length()+1);
    msg->SetType(MSGTYPE_PLAYSOUND);
    msg->clientnum = clientnum;
    msg->Add(sound);
    valid=!(msg->overrun);
}

psPlaySoundMessage::psPlaySoundMessage(MsgEntry* msg)
{
    sound = msg->GetStr();
    valid=!(msg->overrun);
}

csString psPlaySoundMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext("Message Sound:");
    return msgtext+sound;
}

//-----------------------------------------------------------------------------

PSF_IMPLEMENT_MSG_FACTORY(psCharCreateCPMessage,MSGTYPE_CHAR_CREATE_CP);

psCharCreateCPMessage::psCharCreateCPMessage( uint32_t client, int32_t rID, int32_t CPVal )
{
    msg = new MsgEntry( 100);
    msg->SetType(MSGTYPE_CHAR_CREATE_CP );
    msg->clientnum = client;
    msg->Add(rID);
    msg->Add(CPVal);
    msg->ClipToCurrentSize();
    valid = !(msg->overrun );
}

psCharCreateCPMessage::psCharCreateCPMessage( MsgEntry* message )
{
    if(!message)
    {
        return;
    }
    raceID = message->GetUInt32();
    CPValue = message->GetUInt32();
}

csString psCharCreateCPMessage::ToString(AccessPointers * /*access_ptrs*/)
{
    csString msgtext;
    //msgtext.AppendFmt("Race '%i': has '%i' cppoints", raceID, CPValue);
    return msgtext;
}

PSF_IMPLEMENT_MSG_FACTORY(psCharIntroduction,MSGTYPE_INTRODUCTION);

psCharIntroduction::psCharIntroduction( )
{
    msg = new MsgEntry(100);
    msg->SetType(MSGTYPE_INTRODUCTION);
    msg->clientnum = 0;
    msg->ClipToCurrentSize();
    valid = !(msg->overrun);
}

psCharIntroduction::psCharIntroduction( MsgEntry* message )
{
}

csString psCharIntroduction::ToString(AccessPointers * access_ptrs)
{
    csString msgtext;
    return msgtext;
}