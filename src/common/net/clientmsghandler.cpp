/*
 * psclientmsghandler.cpp by Keith Fulton <keith@paqrat.com>
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

#include <iutil/objreg.h>
#include <iutil/eventq.h>
#include <csutil/event.h>
#include <csutil/eventnames.h>

#include "net/netbase.h"
#include "net/clientmsghandler.h"

psClientMsgHandler::psClientMsgHandler()
{
    // no special code needed right now
    scfiEventHandler = NULL;
    object_reg = NULL;
}

psClientMsgHandler::~psClientMsgHandler()
{
    if (scfiEventHandler)
    {
        csRef<iEventQueue> queue = csQueryRegistry<iEventQueue> (object_reg);
        if (queue)
        {
            queue->RemoveListener(scfiEventHandler);
        }
    }    
}

bool psClientMsgHandler::Initialize(NetBase* nb, iObjectRegistry* object_reg)
{
    // Create and register inbound queue
    if (!MsgHandler::Initialize(nb))
        return false;

    psClientMsgHandler::object_reg = object_reg;

    // This hooks our HandleEvent function into main application event loop
    csRef<iEventQueue> queue =  csQueryRegistry<iEventQueue> (object_reg);
    if (!queue)
        return false;
    scfiEventHandler = new EventHandler(this);

    csEventID esub[] = { 
      csevFrame (object_reg),
      CS_EVENTLIST_END 
    };
    queue->RegisterListener(scfiEventHandler, esub);

    return true;
}

bool psClientMsgHandler::HandleEvent(iEvent &ev)
{
    if (!netbase->IsReady())
        return false;
    
    return DispatchQueue();
}

bool psClientMsgHandler::DispatchQueue()
{
    /* 
     * If called, it publishes (and therefore handles)
     * all the messages in the inbound queue before returning.
     */
    csRef<MsgEntry> msg;

    while((msg = queue->Get()))
    {
		// Check for out of sequence messages.  Handle normally if not sequenced.
		if (msg->GetSequenceNumber() == 0)
		{
		    Publish(msg);
			/* Destroy this message.  Note that Msghandler normally does this in the 
	         * Run() loop for the server.
		     */
			// don't forget to release the packet
	        msg = NULL;
		}
		else // sequenced message
		{
			int seqnum = msg->GetSequenceNumber();
			OrderedMessageChannel *channel = pendingQueues.Get(msg->GetType(),NULL);
			if (!channel) // new type of sequence to track
			{
				printf("Adding new sequence channel for msgtype %d.\n", msg->GetType());
				channel = new OrderedMessageChannel;
				pendingQueues.Put(msg->GetType(), channel);
			}
			int nextSequenceExpected  = channel->GetCurrentSequenceNumber();
			printf("Expecting sequence number %d, got %d.\n", nextSequenceExpected, seqnum);

			if (seqnum < nextSequenceExpected)
			{
				printf("Cannot have a sequence number lower than expected!\n");
				seqnum = nextSequenceExpected;
			}

			channel->pendingMessages.Put(seqnum - nextSequenceExpected, msg);
			printf("Added as element %u to pending queue.\n", seqnum - nextSequenceExpected);
			
			if (seqnum == nextSequenceExpected) // have something to publish
			{
				while (channel->pendingMessages.GetSize() && channel->pendingMessages[0] != NULL)
				{
					printf("Publishing sequence number %d.\n", channel->GetCurrentSequenceNumber());
					Publish(channel->pendingMessages[0]);
					channel->pendingMessages[0] = NULL;  // release the ref
					channel->pendingMessages.DeleteIndex(0);
					channel->IncrementSequenceNumber();
				}
			}
		}
	}
    return false;  // this function should not eat the event
}
