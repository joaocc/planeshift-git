/*
* netbase.cpp by Matze Braun <MatzeBraun@gmx.de>
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

#include <csutil/snprintf.h>
#include <csutil/randomgen.h>
#include <csutil/sysfunc.h>
#include <csutil/set.h>
#include <csutil/threading/thread.h>

#include <memory.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef __APPLE__
#include <sys/time.h>  // timeval struct in different place in Mac OS X
#endif

#include <ctype.h> 

#include "util/pserror.h"
#include "net/netbase.h"
#include "net/netpacket.h"
#include "net/message.h"
#include "net/messages.h"
#include "util/psscf.h"
#include "util/psconst.h"
#include "util/log.h"
#include "util/serverconsole.h"
#include "util/strutil.h"

int NetBase::socklibrefcount=0;

#define PSMIN(a,b) ((a<b)?a:b)

// warning: this messes your logs with much data
//#define PACKETDEBUG


NetBase::NetBase(int outqueuesize)
: senders(outqueuesize)
{
    randomgen = new csRandomGen;

    if (socklibrefcount==0 && initSocket())
        ERRORHALT ("couldn't init socket library!");
    socklibrefcount++;

    NetworkQueue = new NetPacketQueueRefCount(MAXQUEUESIZE);
    if (!NetworkQueue)
        ERRORHALT("No Memory!");

    ready=false;
    totaltransferout  = 0;
    totaltransferin   = 0;
    totalcountin      = 0;
    totalcountout     = 0;
    
    profs = new psNetMsgProfiles();
    
    // Initialize the timeout to 100ms
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000;

    msgstrings = NULL;
    engine = NULL;

    logmsgfiltersetting.invert = false;
    logmsgfiltersetting.filterhex = true;
    logmsgfiltersetting.receive = false;
    logmsgfiltersetting.send = false;

    input_buffer = NULL;
}

NetBase::~NetBase()
{
    if (ready)
        Close();

    Notify2(LOG_ANY,"Total bytes sent out was %d.\n",totaltransferout);
    Notify2(LOG_ANY,"Total bytes received was %d.\n", totaltransferin);

    socklibrefcount--;
    if (socklibrefcount==0) {
        exitSocket();
    }

    if (randomgen)
        delete randomgen;
    
    delete profs;

    if (input_buffer)
        cs_free(input_buffer);
}

/* add a Message Queue */
bool NetBase::AddMsgQueue(MsgQueue *q, objID, objID)
{
    inqueues.Push(q);
    return true;
}

void NetBase::RemoveMsgQueue(MsgQueue *q)
{
    inqueues.Delete (q); 
}


void NetBase::ProcessNetwork (csTicks timeout)
{
    // Incoming packets from the wire go to a queue for processing by app.
    while (
        (SendOut() || CheckIn()
        // Outgoing packets from a queue go on the wire.
        ) && csGetTicks() <= timeout
        );
}

bool NetBase::CheckIn()
{    

    // check for incoming packets
    SOCKADDR_IN addr;
    memset (&addr, 0, sizeof(SOCKADDR_IN));
    socklen_t len;

    if (!input_buffer)
    {
        input_buffer = (char*) cs_malloc(MAXPACKETSIZE);

        if (!input_buffer)
        {
            Error2("Failed to cs_malloc %d bytes for packet buffer!\n",MAXPACKETSIZE);
            return false;
        }
    }
    int packetlen;

    // Connection must be initialized!
    CS_ASSERT(ready);

    len = sizeof(SOCKADDR_IN);
    packetlen = RecvFrom (&addr, &len, (void*) input_buffer, MAXPACKETSIZE);

    if (packetlen <= 0)
    {
        return false;
    }
    // Identify the connection
    Connection* connection = GetConnByIP(&addr);

    // Extract the netpacket from the buffer and prep for use locally.
    psNetPacket *bufpacket = psNetPacket::NetPacketFromBuffer(input_buffer,packetlen);
    if (bufpacket==NULL)
    {
        // The data received was too small to make a full packet.
        if (connection)
        {
            Debug3(LOG_NET,connection->clientnum,"Too short packet received from client %d (%d bytes)\n",connection->clientnum,packetlen);
        }
        else
        {
            unsigned char *ip_bytes=(unsigned char *)(&(addr.sin_addr));
            Debug6(LOG_NET,0,"Too short packet received from IP address %d.%d.%d.%d. (%d bytes) No existing connection from this IP.\n",
                (int)ip_bytes[0],(int)ip_bytes[1],(int)ip_bytes[2],(int)ip_bytes[3],packetlen);
        }
        return true; // Continue processing more packets if available
    }
    input_buffer = NULL; //input_buffer now hold by the bufpacket pointer.

    // Endian correction
    bufpacket->UnmarshallEndian();

    // Check for too-big packets - no harm in processing them, but probably a bug somewhere
    if (bufpacket->GetPacketSize() < (unsigned int)packetlen)
    {
        if (connection)
        {
            Debug4(LOG_NET,connection->clientnum,"Too long packet received from client %d (%d bytes received, header reports %zu bytes)\n",
                connection->clientnum,packetlen,bufpacket->GetPacketSize());
        }
        else
        {
            unsigned char *ip_bytes=(unsigned char *)(&(addr.sin_addr));
            pslog::LogMessage (__FILE__, __LINE__, __FUNCTION__,CS_REPORTER_SEVERITY_DEBUG, LOG_NET,
                connection->clientnum,"Too long packet received from IP address %d.%d.%d.%d. (%d bytes received, header reports %zu bytes) No existing connection from this IP.\n",
                (int)ip_bytes[0],(int)ip_bytes[1],(int)ip_bytes[2],(int)ip_bytes[3],packetlen,bufpacket->GetPacketSize());
        }
    }

    //Create new net packet entry and transfere overship of bufpacket to pkt.
    psNetPacketEntry* pkt = new psNetPacketEntry( bufpacket, 
        connection ? connection->clientnum : 0, packetlen);

    // ACK packets can get eaten by HandleAck
    if (HandleAck(pkt, connection, &addr))
    {
        delete pkt;
        return true;
    }

    // Check for doubled packets and drop them
    if (pkt->packet->pktid != 0)
    {
        if (connection && CheckDoublePackets (connection, pkt))
        {
#ifdef PACKETDEBUG
            Debug2(LOG_NET,0,"Droping doubled packet (ID %d)\n", pkt->packet->pktid);
#endif
            delete pkt;
            return true;
        }
    }

#ifdef PACKETDEBUG
    Debug7(LOG_NET,0,"Recveived Pkt, ID: %d, offset %d, from %d size %d (actual %d) flags %d\n", 
        pkt->packet->pktid, pkt->packet->offset, pkt->clientnum, pkt->packet->pktsize,packetlen, pkt->packet->flags);
#endif

    /**
    * Now either send this packet to BuildMessage, or loop through
    * subpackets if they are merged.
    */
    psNetPacketEntry *splitpacket= pkt;
    psNetPacket      *packetdata = NULL;

    do
    {
        splitpacket = pkt->GetNextPacket(packetdata);
        if (splitpacket)
        {
            if (!BuildMessage(splitpacket, connection, &addr))
            {
                // if BuildMessage didn't store it we should delete it now
                delete splitpacket;
            }
        }
    } while (packetdata);

    /**
    * If we split apart a multipacket packet above, remove the merged one
    * now, because the loop above doesn't do it in this case
    */
    if (splitpacket != pkt)
        delete pkt;

    return true;
}

bool NetBase::Flush(MsgQueue * queue)
{
    //@@@ Dirty, hack, this might be in a diffrent thread
    //@@@ need to syncronize this in some way. But at least
    //@@@ by moving this here the client code dosn't call
    //@@@ SendOut anymore.
    SendOut();

    return true;
}


bool NetBase::HandleAck(psNetPacketEntry *pkt, Connection* connection,
                        LPSOCKADDR_IN addr)
{
    psNetPacket* packet = pkt->packet;

    // REVERTED 3/8/03 Until I can figure out why this is causing client conencts to fail -  Andrew Mann (Rhad)
    // If we don't know who this connection is, don't handle ACKs
    //if (!connection)
    //    return false;


    if (packet->pktsize == PKTSIZE_ACK)  // special pktsize means ACK packet here
    {
#ifdef PACKETDEBUG
        Debug1(LOG_NET,0,"Ack received.\n");
#endif

        // receipt of ack packet is good enough to keep alive connection
        if (connection)
        {
            connection->heartbeat          = 0;
            connection->lastRecvPacketTime = csGetTicks();
            connection->pcknumin++;
        }

        psNetPacketEntry *ack = awaitingack.Find(pkt);

        if (ack) // if acked pkt is found, simply remove it.  We're done.
        {
            csTicks elapsed = csGetTicks() - ack->timestamp;

            netInfos.AddPingTicks(elapsed);

            // printf ("Ping time: %i, average: %i\n", elapsed, netInfos.GetAveragePingTicks());



            if (awaitingack.Delete(ack) == 0 )
            {
#ifdef PACKETDEBUG
                Debug2(LOG_NET,0,"No packet in ack queue :%d\n", ack->packet->pktid);
#endif
            }
            else
            {
                delete ack;
            }
        }
        else // if not found, it is probably a resent ACK which is redundant so do nothing
        {
#ifdef PACKETDEBUG
            Debug1(LOG_NET,0,"No matching packet found. No problem though.\n");
#endif
        }

        return true;   // eat the packet
    }
    else if (packet->GetPriority() == PRIORITY_HIGH) // a HIGH_PRIORITY packet -> ack
    {

#ifdef PACKETDEBUG
        Debug1(LOG_NET,0,"High priority packet received.\n");
#endif

        if (connection)
        {
            psNetPacketEntry *ack = new psNetPacketEntry(PRIORITY_LOW,
                pkt->clientnum,
                packet->pktid,
                packet->offset,
                packet->msgsize,
                PKTSIZE_ACK,(char *)NULL);

            SendFinalPacket(ack,addr);
            
            delete ack;
        }
    }

    return false;
}

bool NetBase::CheckDoublePackets(Connection* connection, psNetPacketEntry* pkt)
{
    int i;
    for (i=0; i<MAXPACKETHISTORY; i++)
        if (connection->packethistoryid[i] == pkt->packet->pktid
            && connection->packethistoryoffset[i] == pkt->packet->offset)
            break;

    connection->packethistoryid[connection->historypos] = pkt->packet->pktid;
    connection->packethistoryoffset[connection->historypos] = pkt->packet->offset;
    connection->historypos = (connection->historypos + 1) % MAXPACKETHISTORY;

    return (i != MAXPACKETHISTORY);
}

void NetBase::CheckResendPkts()
{
    BinaryRBIterator<psNetPacketEntry> loop(&awaitingack);
    psNetPacketEntry *pkt;
    csArray<psNetPacketEntry *> pkts;

    csTicks currenttime = csGetTicks();

    for (pkt = loop.First(); pkt; pkt = ++loop)
    {
        if (pkt->timestamp + PKTMAXLATENCY < currenttime)
            pkts.Push(pkt);
    }
    for (size_t i = 0; i < pkts.GetSize(); i++)
    {
        pkt = pkts.Get(i);
#ifdef PACKETDEBUG
        Debug2(LOG_NET,0,"Resending nonacked HIGH packet (ID %d).\n", pkt->packet->pktid);
#endif
        pkt->timestamp = currenttime;   // update stamp on packet

        // re-add to send queue
        NetworkQueue->Add(pkt);
        //printf("pkt=%p, pkt->packet=%p\n",pkt,pkt->packet);
        // take out of awaiting ack pool.
        // This does NOT delete the pkt mem block itself.
        if (awaitingack.Delete(pkt) == 0 )
        {
#ifdef PACKETDEBUG
            Debug2(LOG_NET,0,"No packet in ack queue :%d\n", pkt->packet->pktid);
#endif
        }
    }
}
    

bool NetBase::SendMergedPackets(NetPacketQueue *q)
{
    csRef<psNetPacketEntry> queueget;
    psNetPacketEntry *candidate, *final;

    queueget = q->Get(); // csRef required for q->Get()
    final = queueget;

    // If there's not at least one packet in the queue, we're done.
    if (!final)
        return false;

    // Try to merge additional packets into a single send.
    while ((queueget=q->Get()))
    {
        candidate = queueget;  // get out of csRef here
        if(!final->Append(candidate))
        {
            // A failed append means that the packet can't fit.
            // Set a random ID for the new merged packet
            if(final->packet->pktid == 0)
            {
                final->packet->pktid = GetRandomID();
            }
            SendSinglePacket(final);
            
            // Start the process again with the packet that wouldn't fit
            final = candidate;
        }
    }

    // There is always data in final here
    // Set a random ID for the new merged packet if this is a merged packet (pktid==0)
    if (final->packet->pktid == 0)
        final->packet->pktid = GetRandomID();
    SendSinglePacket(final);  // this deletes if necessary

    return true;
}

bool NetBase::SendSinglePacket(psNetPacketEntry* pkt)
{
    if (!SendFinalPacket (pkt))
    {
        delete pkt;
        return false;
    }

    /**
    * Add to tree of packets we need acks on.  These will be
    * checked in Recv code and removed from tree.  Old packets
    * will be checked by a timer and resent.
    */
    if (pkt->packet->GetPriority() == PRIORITY_HIGH)
    {

#ifdef PACKETDEBUG
        Debug3(LOG_NET,0,"Sent HIGH pkt id %d to client %d.\n", 
            pkt->packet->pktid, pkt->clientnum);
#endif
        awaitingack.Insert(pkt);
        // queue holds ref now -> don't delete pkt
    }
    else
    {
        delete pkt;
    }

    return true;
}

bool NetBase::SendFinalPacket(psNetPacketEntry *pkt)
{
    Connection* connection = GetConnByNum(pkt->clientnum);
    if (!connection)
    {
        // This is probably happening because packets needed to be
        // resent to a linkdead client.
#ifdef PACKETDEBUG
        Debug2(LOG_NET,0,"Packet for clientnum %d is being discarded.\n", pkt->clientnum);
#endif
        return false;
    }

    return SendFinalPacket(pkt,&(connection->addr));
    
}

bool NetBase::SendFinalPacket(psNetPacketEntry *pkt, LPSOCKADDR_IN addr)
{
    // send packet...
#ifdef PACKETDEBUG
    Debug5(LOG_NET,0,"SendPacket ID: %d to %d size %d flags %d\n",
        pkt->packet->pktid, pkt->clientnum, pkt->packet->pktsize, pkt->packet->flags);
#endif

    uint16_t size = (uint16_t)pkt->packet->GetPacketSize();
    void *data = pkt->GetData();

    pkt->packet->MarshallEndian();

    int err = SendTo (addr, data, size);
    if (err != (int)size )
    {
        Error4("Send error %d: %d bytes sent and %d bytes expected to be sent.\n", errno,err,size);
        pkt->packet->UnmarshallEndian();
        return false;
    }

    pkt->packet->UnmarshallEndian();
    return true;
}

bool NetBase::SendOut()
{
    bool sent_anything = false;

    CS_ASSERT(ready);

    // Part1: Client (This is only used when we're the client)
    if (SendMergedPackets(NetworkQueue))  // client uses this queue
    {
        sent_anything=true;
    }

    // Part2: Server (only used when we're the server)
    csRef<NetPacketQueueRefCount> q; 
    while (q = csPtr<NetPacketQueueRefCount> (senders.Get()))
    {
        if (SendMergedPackets(q))
        {
            sent_anything = true;
        }
    }

    return sent_anything;
}


bool NetBase::Init(bool autobind)
{
    int err;

    if (ready)
        Close();

    mysocket = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    /* set to non blocking operation */
    unsigned long arg=1;
    err = SOCK_IOCTL (mysocket, FIONBIO, &arg);
    if (err < 0)
    {
        Error1("set to nonblock failed");
        Close(SOCKET_CLOSE_FORCED);
        return false;
    }

    if (autobind)
    {
        if (!Bind ((int) INADDR_ANY, 0))
        {
            Close(SOCKET_CLOSE_FORCED);
            return false;
        }
    }

    return true;
}

bool NetBase::Bind (const char* str, int port)
{
    int addr = inet_addr(str);

    return Bind(addr, port);
}

bool NetBase::Bind (int addr, int port)
{
    int err;
    SOCKADDR_IN sockaddr;

    if (!mysocket)
        return false;

    memset ((void*) &sockaddr, 0, sizeof(SOCKADDR_IN));
    sockaddr.sin_family = AF_INET;                         
    sockaddr.sin_addr.s_addr = addr;                       
    sockaddr.sin_port = htons(port);

    err = bind (mysocket, (LPSOCKADDR) &sockaddr, sizeof(SOCKADDR_IN));
    if (err < 0)
    {                                                                      
        Error1("bind() failed");
        return false;
    }

    ready = true;
    return true;
}

void NetBase::Close(bool force)
{
    if (ready || force)
        SOCK_CLOSE(mysocket); 

    ready = false;
}

int NetBase::GetIPByName(LPSOCKADDR_IN addr, const char *name)
{
    struct hostent *hentry;

    hentry = gethostbyname (name);
    if (!hentry)
        return -1;

    addr->sin_addr.s_addr = ((struct in_addr *) hentry->h_addr)->s_addr;
    return 0;
}

void NetBase::LogMessages(char dir,MsgEntry* me)
{
    if (DoLogDebug(LOG_MESSAGES) && !FilterLogMessage(me->bytes->type,dir))
        Debug3(LOG_MESSAGES,0,"%c: %s\n",dir,
               GetDecodedMessage(me,msgstrings,engine,logmsgfiltersetting.filterhex).GetData());
}

csString NetBase::LogMessageFilter(char *arg)
{
    if (strlen(arg) == 0)
    {
        return "Please specify message to be filtered.\nSyntax: filtermsg [+/-<msg_type>|invert|filterhex [no]|clear|receive|send|show]";
    }
    WordArray words(arg);
    if (words[0] == "invert")
    {
        InvertLogMessageFilter();
    } else if (words[0] == "filterhex")
    {
        bool filterhex = true;
        if (words[1] == "no") filterhex = false;
        
        SetLogMessageFilterHex(filterhex);
    } else if (words[0] == "show")
    {
        LogMessageFilterDump();
    } else if (words[0] == "clear")
    {
        LogMessageFilterClear();
    } else if (words[0] == "receive")
    {
        ToggleReceiveMessageFilter();
    } else if (words[0] == "send")
    {
        ToggleSendMessageFilter();
    } else
    {
        bool add = true;
        int type = -1;
        if (words[0].GetAt(0) == '+')
        {
            type = atoi(words[0]);
        }
        else if (words[0].GetAt(0) == '-')
        {
            add = false;
            type = -atoi(words[0]);
        }
        else
        {
            type = atoi(words[0]);
        }

        if (type == 0)
        {
            csString name = words[0];
            if (name.GetAt(0) == '+' || name.GetAt(0) == '-')
            {
                name.DeleteAt(0,1);
            }
                
            type = psfMsgType(name);
        }
        
        if (type != -1)
        {
            if (add)
            {
                AddFilterLogMessage(type);
                return csString().Format("Add %s(%d)",psfMsgTypeName(type).GetDataSafe(),type);
            }
            else
            {
                RemoveFilterLogMessage(type);
                return csString().Format("Remove %s(%d)",psfMsgTypeName(type).GetDataSafe(),type);
            }
        }
    }
    
    return "";
}


void NetBase::AddFilterLogMessage(int type)
{
    size_t n;
    for (n = 0; n < logmessagefilter.GetSize(); n++)
    {
        if (logmessagefilter[n] == type)
        {
            // Message already in filter list
            return;
        }
    }
    logmessagefilter.Push(type);
}

void NetBase::RemoveFilterLogMessage(int type)
{
    size_t n;
    for (n = 0; n < logmessagefilter.GetSize(); n++)
    {
        if (logmessagefilter[n] == type) 
            logmessagefilter.DeleteIndex(n);
    }
}

void NetBase::LogMessageFilterClear()
{
    logmessagefilter.DeleteAll();
}

bool NetBase::FilterLogMessage(int type, char dir)
{
    size_t n;
    bool result = false;

    // Check global receive and transmit filters
    if ((dir == 'R' || dir == 'r') && logmsgfiltersetting.receive)
    {
        return true;
    }
    if ((dir == 'S' || dir == 's') && logmsgfiltersetting.send)
    {
        return true;
    }
    
    for (n = 0; n < logmessagefilter.GetSize(); n++)
    {
        if (logmessagefilter[n] == type) result = true;
    }
    if (logmsgfiltersetting.invert)
        result = !result;

    
    
    return result;
}

void NetBase::LogMessageFilterDump()
{
    size_t n;
    
    CPrintf(CON_CMDOUTPUT,"\nLogMessage filter setting\n");
    CPrintf(CON_CMDOUTPUT,"Invert filter  : %s\n",(logmsgfiltersetting.invert?"true":"false"));
    CPrintf(CON_CMDOUTPUT,"Filter hex     : %s\n",(logmsgfiltersetting.filterhex?"true":"false"));
    CPrintf(CON_CMDOUTPUT,"Filter receive : %s\n",(logmsgfiltersetting.receive?"true":"false"));
    CPrintf(CON_CMDOUTPUT,"Filter send    : %s\n",(logmsgfiltersetting.send?"true":"false"));
    CPrintf(CON_CMDOUTPUT,"Filter msgs    :\n");
    for (n = 0; n < logmessagefilter.GetSize(); n++)
    {
        CPrintf(CON_CMDOUTPUT,"%s(%d)\n",GetMsgTypeName(logmessagefilter[n]).GetDataSafe(),logmessagefilter[n]);
    }
}


bool NetBase::SendMessage(MsgEntry* me)
{
    if (me->overrun)
    {
        CS_ASSERT(!"NetBase::SendMessage() Failed to send message in overrun state!\n");
        return false;
    }
    return SendMessage(me,NULL);
}

bool NetBase::SendMessage(MsgEntry* me,NetPacketQueueRefCount *queue)
{
    profs->AddSentMsg(me);

    size_t bytesleft = me->bytes->GetTotalSize();
    size_t offset    = 0;

    if (!queue)
        queue = NetworkQueue;

    uint32_t id = GetRandomID();

    LogMessages('S',me);
    
    while (bytesleft > 0)
    {
        // The pktlen does not include the length of the headers, but the MAXPACKETSIZE does
        size_t pktlen = PSMIN(MAXPACKETSIZE-sizeof(struct psNetPacket), bytesleft);

        psNetPacketEntry *pNewPkt = new psNetPacketEntry(me->priority, 
            me->clientnum, 
            id, 
            (uint16_t)offset,
            (uint16_t)me->bytes->GetTotalSize(), 
            (uint16_t)pktlen, 
            me->bytes);

        if (!queue->Add(pNewPkt))
        {
            Error1("NetworkQueue full. Could not add packet.\n");
            return false;
        }

        bytesleft -= pktlen;
        offset    += pktlen;
    }

    return true;
}

void NetBase::CheckFragmentTimeouts(void)
{
    psNetPacketEntry *pkt;

    // A set of packet ids that should NOT be discarded
    csSet<unsigned> newids;

    psNetPacketEntry *oldpackets[10];
    int count=0,index=0;
    csTicks current = csGetTicks();

    // Max age = 12 seconds

    // If we've been up less than 12 seconds, or the ticks just wrapped, don't check
    if (current<12000)
        return;

    // Iterate through all packets in the list searching for old packets
    BinaryRBIterator<psNetPacketEntry> loop(&packets);
    for (pkt = loop.First(); pkt; pkt = ++loop)
    {
        // If this id is already known to be new then go to the next one
        if(newids.In(pkt->packet->pktid))
            continue;

        if (pkt->timestamp > current || pkt->timestamp < current-12000)
        {
            // Maximum of 10 ol
            if (count<10)
                oldpackets[count++]=pkt;
        }
        else // This id should not be discarded
            newids.AddNoTest(pkt->packet->pktid);
    }
     
    // Delete any old packet entries from the awaiting-assembly list, and also delete the packets themselves
    for (index=0;index<count;index++)
    {
        if(newids.In(oldpackets[index]->packet->pktid))
            continue;

        Debug2(LOG_NET,0,"Fragment reassembly timeout.  Discarding packet with packet id %u.\n", (oldpackets[index])->packet->pktid);
        /* Drop refcount until the packet is removed from the list.
         *  If we don't do this all at once we'll do this again the very next check since the time wont be altered.
         */
        while (packets.Delete(oldpackets[index])>0);
            delete oldpackets[index];
    }
}


bool NetBase::BuildMessage(psNetPacketEntry* pkt, Connection* &connection,
                           LPSOCKADDR_IN addr)
{
    if (connection)
    {
        // Update last packet time in connection
        connection->heartbeat          = 0;
        connection->lastRecvPacketTime = csGetTicks();
    }

    psNetPacket* packet = pkt->packet;


    // already received the whole packet?
    if (packet->offset == 0 && packet->pktsize == packet->msgsize) 
    {
        /* Before reading a message header, make sure the packet is large enough to contain a message header
        *  Note that at this point packet->pktsize should already be verified to be valid (within the length of read data)
        */
        if (packet->pktsize < sizeof(psMessageBytes))
        {
            Debug1(LOG_NET,connection->clientnum,"Dropping packet too short to contain message header.\n");
            return false;
        }

        psMessageBytes* msg = (psMessageBytes*) packet->data;
        // The packet's report of the message size should agree with the message header
        if (packet->msgsize != msg->GetTotalSize())
        {
            Debug3(LOG_NET,connection ? connection->clientnum : 0, "Dropping packet with different message size in packet header (%d) and message header (%zu).\n",
                packet->msgsize,msg->GetTotalSize());
        }
        else
        {
            csRef<MsgEntry> me;
            me.AttachNew(new MsgEntry(msg));
            me->priority = packet->GetPriority();
            HandleCompletedMessage(me, connection, addr,pkt);
        }
        return false;
    }


    // This is a fragment. Add to the waiting packet list.  It may be removed ahead.
    packets.Insert(pkt);

    // Build message from packet or add packet to existing partial message
    csRef<MsgEntry> me = 
        CheckCompleteMessage(pkt->clientnum,pkt->packet->pktid);
    if (me)
    {
        HandleCompletedMessage(me, connection, addr, pkt);
    }
    return true;
}

csPtr<MsgEntry> NetBase::CheckCompleteMessage(uint32_t client, uint32_t id)
{
    psNetPacketEntry *first, *pkt;

    first = new psNetPacketEntry(PRIORITY_LOW, client, id,
        0, 0, 0, (char *)NULL);

    // This search is FAST, and without the first packet, you can't build the message.
    pkt = packets.Find(first);
    delete first;
    if (!pkt)
    {
#ifdef PACKETDEBUG
        Debug1(LOG_NET,0,"No First Packet found!?! Packets out of order, but should be no problem.\n");
        Debug2(LOG_NET,0,"MSGID was: %d\n", id);
#endif
        return NULL;
    }

    unsigned int length=0, totallength = pkt->packet->msgsize;
    // Invalidated is set to true if something nasty is detected with the packets in this message and the whole message should be discarded
    bool invalidated=false;

    csRef<MsgEntry> me;
    me.AttachNew(new MsgEntry(totallength, pkt->packet->GetPriority()));


    // The tree is sorted on: clientnum ascending, packetid ascending, offset ascending
    BinaryRBIterator<psNetPacketEntry> loop(&packets);
    for (pkt = loop.First(); pkt; pkt = ++loop)
    {
        // Skip packets until we reach packets from the desired client
        if (pkt->clientnum<client)
            continue;
        // Examine packets from this client in more detail
        if (pkt->clientnum==client)
        {
            // Skip packets until we reach partials with this packetid
            if (pkt->packet->pktid < id)
                continue;

            // Found a piece
            if (pkt->packet->pktid == id)
            {
                // Verify that all pieces have the same msgsize as the first piece
                if (totallength != pkt->packet->msgsize)
                {
                    Warning3(LOG_NET,"Discarding invalid message.  Message fragments have inconsistant total message size.  First fragment reports %u. Later fragment reports %u.\n",
                                      totallength,pkt->packet->msgsize);
                    invalidated=true;
                    break;
                }

                // Verify that no packet can overflow the message buffer
                if (pkt->packet->offset + pkt->packet->pktsize > totallength)
                {
                    Warning4(LOG_NET,"Discarding MALICIOUS message.  Message fragment attempted to overflow message buffer.  Message size %u. Fragment offset %u.  Fragment length %u.\n",
                                      totallength,pkt->packet->offset,pkt->packet->pktsize);
                    invalidated=true;
                    break;
                }


                // Copy the contents into the total message data in the right location
                memcpy( ((char *)(me->bytes)) + pkt->packet->offset, pkt->packet->data,
                    pkt->packet->pktsize);
                // Add to our length count
                length += pkt->packet->pktsize;
            }
            else if (pkt->packet->pktid > id) // We're past our packet
                break;
        }
        else
        {
            // We're past our client
            break;
        }
    }

    /*  Early exit.
     *  We can return early only if we do not have to delete any packets from this list.
     *  This means we do not have a complete message and there is no sign of invalid data.
     */
    if (!invalidated && length != totallength) // sum of all packets is entire message
    {
        // We didn't get the entire message.  The smartpointer will take care of releasing the message object
        return NULL;
    }

    /* The entire message is now read or invalid.  A BinaryTreeIterator is NOT SAFE to use when deleting entries from the tree!
    *  This would be relatively slow if we had a lot of packets pending, but it's better than risking a crash.
    *
    * We start out with the known client id, packet id, and 0 offset.  We add the length field of the found packet
    *   to the offset of the test packet, delete the found packet and repeat until the total length is accounted for
    *   or we don't find any more (the later should not happen).
    *
    */
    psNetPacketEntry *search = new psNetPacketEntry(PRIORITY_LOW, client, id,
        0, 0, 0, (char *)NULL);
    while ((search->packet->offset < totallength) && (pkt = packets.Find(search)))
    {
        search->packet->offset+=pkt->packet->pktsize;

        // We don't care how many times it's in the queue, there's a mistake if it's over 1. Let's fix it now.
        while (packets.Delete(pkt)>0); 
        // And delete the packet entry object
        delete pkt;
    }
    /* If the search offset didnt finish at the end of the packet, we have gaps, not all pieces were deleted,
     * someone is trying to play games, and we should ignore the message, and 
     * TODO:  disconnect the connection (if connected) and blacklist the IP for a short period (5 minutes?)
     */
    if (!invalidated && search->packet->offset != totallength)
    {
        Warning2(LOG_NET,"Discarding MALICIOUS message.  Gaps in fragments attempt to capture data from uninitialized memory.  Missing packet with offset %u. (but total length of remaining fragments equals total message length).\n",
                            search->packet->offset);
        invalidated=true;
    }

    // Delete the object used for searching
    delete search;

    // Something nasty was detected.  The message is invalid.
    if (invalidated)
        return NULL;
   
    // MsgEntry msg is filled in completely and the packet pieces that make up the message are deleted.
    return csPtr<MsgEntry> (me);
}

void NetBase::QueueMessage(MsgEntry *me)
{
    for (size_t i=0; i<inqueues.GetSize(); i++)
    {
        if (!inqueues[i]->Add(me))
        {
            Error4("*** Input Buffer Full! Yielding for packet, client %u, type %s, input queue %zu!",
                   me->clientnum,GetMsgTypeName(me->GetType()).GetData(),i);

           // Input queue is full. Yield CPU and check back  if it still is full
            CS::Threading::Thread::Yield();

            if (!inqueues[i]->Add(me))
            {
                Error4("*** Input Buffer Full! Yielding for packet, client %u, type %s, input queue %zu!",
                    me->clientnum,GetMsgTypeName(me->GetType()).GetData(),i);
            }
        }
    }
}

void NetBase::HandleCompletedMessage(MsgEntry *me, Connection* &connection,
                                     LPSOCKADDR_IN addr,psNetPacketEntry* pkt)
{
    profs->AddReceivedMsg(me);
    
    if (!connection)
    {
        if (!HandleUnknownClient (addr, me))
        {
            // Authentication wasn't successfull
            CHECK_FINAL_DECREF(me,"Inbound completed msg");
            return;
        }

        // This one should work since handleUnknownClient was successful
        connection = GetConnByIP(addr);
        if (!connection)
        {
            Error1("couldn't get client by num...");
            return;
        }

        // Here we have a second chance to handle acking the packet
        // because at the first spot, the connection was unknown
        // so the packet wasn't acked.  The initial connection
        // packet must be acked here.
        HandleAck(pkt, connection, addr);

        // Update last packet time in connection
        // (Mostly handled in packet, but must be handled here for very first message)
        connection->heartbeat          = 0;
        connection->lastRecvPacketTime = csGetTicks();
    }

    // put the message in the queue
    me->clientnum = connection->clientnum;
    QueueMessage(me);
}

uint32_t NetBase::GetRandomID()
{
    CS::Threading::MutexScopedLock lock(mutex);
    return (unsigned) randomgen->Get(0x7fffffff) + 1;
}

// --------------------------------------------------------------------------

NetBase::Connection::Connection(uint32_t num)
{
    pcknumin=0;
    pcknumout=0;
    clientnum=num;
    valid=false;
    heartbeat=0;
    lastRecvPacketTime = csGetTicks();

    memset(&addr, 0, sizeof(SOCKADDR_IN));
    memset(&packethistoryid, 0, MAXPACKETHISTORY * sizeof(uint32_t));
    memset(&packethistoryoffset, 0, MAXPACKETHISTORY * sizeof(uint32_t));
    historypos = 0;
    buf = NULL;
}

NetBase::Connection::~Connection()
{
    if (buf) 
        cs_free(buf);
}


