/*
 * psnetmanager.h by Matze Braun <MatzeBraun@gmx.de>
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
#ifndef __PSNETMANAGER_H__
#define __PSNETMANAGER_H__

#include <csutil/csstring.h>
#include <csutil/ref.h>
#include "iclient/netmanager.h"

class psNetConnection;
class psAuthenticationClient;
class psClientMsgHandler;
class psUserCommands;
class psGuildCommands;
class psGroupCommands;
class psUtilityCommands;
class psAdminCommands;

/**
 * This class holds references to different network classes and provides some
 * conveniance functions to access them
 */
class psNetManager : public iNetManager
{
protected:
    
public:
    SCF_DECLARE_IBASE;
    
    psNetManager();
    virtual ~psNetManager();

    bool Initialize(iObjectRegistry* object_reg );
    
    /** Connect to given server and port */
    bool Connect(const char* server, int port);

    /** Disconnect from server */
    void Disconnect();
    
    /** Send a disconnect message to the server.
     */
    void SendDisconnect(bool final = true);

    /** Sends an authentication message to the server */
    void Authenticate(const csString name, const csString pwd);

    MsgHandler* GetMsgHandler();
    CmdHandler* GetCmdHandler() { return cmdhandler; }

    const char* GetLastError() { return errormsg; }
    const char* GetAuthMessage();

    psNetConnection* GetConnection() { return connection; }
    
    bool IsConnected() { return connected; }

protected:
    iObjectRegistry*                object_reg;
    psNetConnection*                connection;
    csRef<psClientMsgHandler>       msghandler;
    csRef<psAuthenticationClient>   authclient;

    /* Command stuff here */
    csRef<CmdHandler>               cmdhandler;
    csRef<psUserCommands>           usercmds;
    csRef<psGuildCommands>          guildcmds;
    csRef<psGroupCommands>          groupcmds;
    csRef<psUtilityCommands>        utilcmds;
    csRef<psAdminCommands>          admincmds;
    bool connected;
    csString errormsg;
};

#endif

