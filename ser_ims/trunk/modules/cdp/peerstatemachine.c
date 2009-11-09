/**
 * $Id$
 *  
 * Copyright (C) 2004-2006 FhG Fokus
 *
 * This file is part of Open IMS Core - an open source IMS CSCFs & HSS
 * implementation
 *
 * Open IMS Core is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * For a license to use the Open IMS Core software under conditions
 * other than those described here, or to purchase support for this
 * software, please contact Fraunhofer FOKUS by e-mail at the following
 * addresses:
 *     info@open-ims.org
 *
 * Open IMS Core is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * It has to be noted that this Open Source IMS Core System is not 
 * intended to become or act as a product in a commercial context! Its 
 * sole purpose is to provide an IMS core reference implementation for 
 * IMS technology testing and IMS application prototyping for research 
 * purposes, typically performed in IMS test-beds.
 * 
 * Users of the Open Source IMS Core System have to be aware that IMS
 * technology may be subject of patents and licence terms, as being 
 * specified within the various IMS-related IETF, ITU-T, ETSI, and 3GPP
 * standards. Thus all Open IMS Core users have to take notice of this 
 * fact and have to agree to check out carefully before installing, 
 * using and extending the Open Source IMS Core System, if related 
 * patents and licences may become applicable to the intended usage 
 * context.  
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * 
 */
 
/**
 * \file
 * 
 * CDiameterPeer Diameter base protocol state-machine definition 
 * 
 *  \author Dragos Vingarzan vingarzan -at- fokus dot fraunhofer dot de
 * 
 */
#include <sys/socket.h> 
#include <netinet/in.h>
#include <unistd.h>

#include "peerstatemachine.h"
#include "diameter_api.h"
#include "diameter_ims.h"

#include "utils.h"
#include "receiver.h"
#include "peermanager.h"
#include "config.h"
#include "worker.h"
#include "authstatemachine.h"

extern dp_config *config;		/**< Configuration for this diameter peer 	*/

/** Strings for the peer states */
char *dp_states[]={"Closed","Wait_Conn_Ack","Wait_I_CEA","Wait_Conn_Ack_Elect","Wait_Returns","R_Open","I_Open","Closing"};
/** Strings for the peer events */
char *dp_events[]={"Start","Stop","Timeout","Win_Election","R_Conn_CER","I_Rcv_Conn_Ack","I_Rcv_Conn_NAck",
	"I_Rcv_CER","I_Rcv_CEA","R_Rcv_CER","R_Rcv_CEA","I_Rcv_Non_CEA",
	"I_Rcv_DPR","I_Rcv_DPA","R_Rcv_DPR","R_Rcv_DPA",
	"I_Rcv_DWR","I_Rcv_DWA","R_Rcv_DWR","R_Rcv_DWA",
	"Send_Message","I_Rcv_Message","R_Rcv_Message","I_Peer_Disc","R_Peer_Disc"};

/**
 * Diameter base protocol state-machine processing.
 * This function get's called for every event. It updates the states and can trigger
 * other events.
 * @param p - the peer for which the event happened
 * @param event - the event that happened
 * @param msg - if a Diameter message was received this is it, or NULL if not
 * @param peer_locked - if the peer lock is already aquired
 * @param sock - socket that this event happened on, or NULL if unrelated
 * @returns 1 on success, 0 on error. Also the peer states are updated
 */
int sm_process(peer *p,peer_event_t event,AAAMessage *msg,int peer_locked,int sock)
{
	int result_code;
	peer_event_t next_event;
	int msg_received=0;
		
	if (!peer_locked) lock_get(p->lock);
	LOG(L_DBG,"DBG:sm_process(): Peer %.*s \tState %s \tEvent %s\n",
		p->fqdn.len,p->fqdn.s,dp_states[p->state],dp_events[event-101]);

	switch (p->state){
		case Closed:
			switch (event){
				case Start:
					p->state = Wait_Conn_Ack;
					next_event = I_Snd_Conn_Req(p);
					if (next_event==I_Rcv_Conn_NAck)
						sm_process(p,next_event,0,1,p->I_sock);
					else{
						/* wait for fd to be transmitted to the respective receiver, in order to get a send pipe opened */						
					}
					break;	
				case R_Conn_CER:
					R_Accept(p,sock);
					result_code = Process_CER(p,msg);
					Snd_CEA(p,msg,result_code,p->R_sock);
					if (result_code>=2000 && result_code<3000)
						p->state = R_Open;
					else {
						R_Disc(p);
						p->state = Closed;
					}
					log_peer_list(L_INFO);
					break;
				case Stop:
					/* just ignore this state */
					p->state = Closed;
					break;
				default:
					LOG(L_DBG,"DBG:sm_process(): In state %s invalid event %s\n",
						dp_states[p->state],dp_events[event-101]);
					goto error;
			}
			break;		
		case Wait_Conn_Ack:
			switch(event){
				case I_Rcv_Conn_Ack:
					I_Snd_CER(p);
					p->state = Wait_I_CEA;
					break;	
				case I_Rcv_Conn_NAck:
					Cleanup(p,p->I_sock);
					p->state = Closed;
					break;
/* Commented as not reachable*/						
				case R_Conn_CER:
					R_Accept(p,sock);
					result_code = Process_CER(p,msg);
					if (result_code>=2000 && result_code<3000)
						p->state = Wait_Conn_Ack_Elect;
					else {
						p->state = Wait_Conn_Ack;
						close(sock);
					}
					break;
				case Timeout:
					Error(p,p->I_sock);
					p->state = Closed;
				default:
					LOG(L_DBG,"DBG:sm_process(): In state %s invalid event %s\n",
						dp_states[p->state],dp_events[event-101]);
					goto error;
			}
			break;
			
		case Wait_I_CEA:
			switch(event){
				case I_Rcv_CEA:
					result_code = Process_CEA(p,msg);
					if (result_code>=2000 && result_code<3000)
						p->state = I_Open; 												
					else {
						Cleanup(p,p->I_sock);
						p->state = Closed;
					}
					log_peer_list(L_INFO);
					break;
				case R_Conn_CER:
					R_Accept(p,sock);
					result_code = Process_CER(p,msg);
					p->state = Wait_Returns;
					if (Elect(p,msg))
						sm_process(p,Win_Election,msg,1,sock);
					break;
				case I_Peer_Disc:
					I_Disc(p);
					p->state = Closed;
					break;
				case I_Rcv_Non_CEA:
					Error(p,p->I_sock);
					p->state = Closed;
					break;
				case Timeout:
					Error(p,p->I_sock);
					p->state = Closed;
					break;
				default:
					LOG(L_DBG,"DBG:sm_process(): In state %s invalid event %s\n",
						dp_states[p->state],dp_events[event-101]);
					goto error;
			}
			break;	
/* commented as not reachable */
		case Wait_Conn_Ack_Elect:
			switch(event){
				default:
					LOG(L_DBG,"DBG:sm_process(): In state %s invalid event %s\n",
						dp_states[p->state],dp_events[event-101]);
					goto error;
			}
			break;
		case Wait_Returns:
			switch(event){
				case Win_Election:
					I_Disc(p);
					result_code = Process_CER(p,msg);
					Snd_CEA(p,msg,result_code,p->R_sock);
					if (result_code>=2000 && result_code<3000){
						p->state = R_Open;
					}else{
						R_Disc(p);
						p->state = Closed;
					}
					break;
				case I_Peer_Disc:
					I_Disc(p);
					result_code = Process_CER(p,msg);
					Snd_CEA(p,msg,result_code,p->R_sock);
					if (result_code>=2000 && result_code<3000){
						p->state = R_Open;
					}else{
						R_Disc(p);
						p->state = Closed;
					}
					break;
				case I_Rcv_CEA:
					R_Disc(p);
					result_code = Process_CEA(p,msg);
					if (result_code>=2000 && result_code<3000)
						p->state = I_Open; 
					else {
						Cleanup(p,p->I_sock);
						p->state = Closed;
					}
					break;
				case R_Peer_Disc:
					R_Disc(p);
					p->state = Wait_I_CEA;
					break;
				case R_Conn_CER:
					R_Reject(p,p->R_sock);
					p->state = Wait_Returns;
					break;
				case Timeout:
					if (p->I_sock>=0) Error(p,p->I_sock);
					if (p->R_sock>=0) Error(p,p->R_sock);
					p->state = Closed;
				default:
					LOG(L_DBG,"DBG:sm_process(): In state %s invalid event %s\n",
						dp_states[p->state],dp_events[event-101]);
					goto error;
			}
			break;
		case R_Open:
			switch (event){
				case Send_Message:
					Snd_Message(p,msg);
					p->state = R_Open;
					break;
				case R_Rcv_Message:
					// delayed processing until out of the critical zone
					//Rcv_Process(p,msg);
					msg_received = 1;
					p->state = R_Open;
					break;
				case R_Rcv_DWR:
					result_code = Process_DWR(p,msg);
					Snd_DWA(p,msg,result_code,p->R_sock);
					p->state = R_Open;
					break;
				case R_Rcv_DWA:
					Process_DWA(p,msg);
					p->state = R_Open;
					break;
				case R_Conn_CER:
					R_Reject(p,sock);
					p->state = R_Open;
					break;
				case Stop:
					Snd_DPR(p);
					p->state = Closing;
					break;
				case R_Rcv_DPR:
					Snd_DPA(p,msg,AAA_SUCCESS,p->R_sock);
					R_Disc(p);
					p->state = Closed;
					log_peer_list(L_INFO);
					break;
				case R_Peer_Disc:
					R_Disc(p);
					p->state = Closed;
					log_peer_list(L_INFO);
					break;
				case R_Rcv_CER:
					result_code = Process_CER(p,msg);
					Snd_CEA(p,msg,result_code,p->R_sock);
					if (result_code>=2000 && result_code<3000)
						p->state = R_Open;
					else {
						/*R_Disc(p);p.state = Closed;*/
						p->state = R_Open; /* Or maybe I should disconnect it?*/
					}
					break;
				case R_Rcv_CEA:
					result_code = Process_CEA(p,msg);
					if (result_code>=2000 && result_code<3000)
						p->state = R_Open;
					else {
						/*R_Disc(p);p.state = Closed;*/
						p->state = R_Open; /* Or maybe I should disconnect it?*/
					}
					log_peer_list(L_INFO);
					break;
				default:
					LOG(L_DBG,"DBG:sm_process(): In state %s invalid event %s\n",
						dp_states[p->state],dp_events[event-101]);
					goto error;
			}
			break;			
		case I_Open:
			switch (event){
				case Send_Message:
					Snd_Message(p,msg);
					p->state = I_Open;
					break;
				case I_Rcv_Message:
					// delayed processing until out of the critical zone
					//Rcv_Process(p,msg);
					msg_received = 1;
					p->state = I_Open;
					break;
				case I_Rcv_DWR:
					result_code = Process_DWR(p,msg);
					Snd_DWA(p,msg,result_code,p->I_sock);						
					p->state =I_Open;
					break;
				case I_Rcv_DWA:
					Process_DWA(p,msg);
					p->state =I_Open;
					break;
				case R_Conn_CER:
					R_Reject(p,sock);
					p->state = I_Open;
					break;
				case Stop:
					Snd_DPR(p);
					p->state = Closing;
					break;
				case I_Rcv_DPR:
					Snd_DPA(p,msg,2001,p->I_sock);
					R_Disc(p);
					p->state = Closed;
					log_peer_list(L_INFO);
					break;
				case I_Peer_Disc:
					I_Disc(p);
					p->state = Closed;
					log_peer_list(L_INFO);
					break;
				case I_Rcv_CER:
					result_code = Process_CER(p,msg);
					Snd_CEA(p,msg,result_code,p->I_sock);
					if (result_code>=2000 && result_code<3000)
						p->state = I_Open;
					else {
						/*I_Disc(p);p.state = Closed;*/
						p->state = I_Open; /* Or maybe I should disconnect it?*/
					}
					break;
				case I_Rcv_CEA:
					result_code = Process_CEA(p,msg);
					if (result_code>=2000 && result_code<3000)
						p->state = I_Open;
					else {
						/*I_Disc(p);p.state = Closed;*/
						p->state = I_Open; /* Or maybe I should disconnect it?*/
					}
					break;
				default:
					LOG(L_DBG,"DBG:sm_process(): In state %s invalid event %s\n",
						dp_states[p->state],dp_events[event-101]);
					goto error;
			}
			break;				
		case Closing:
			switch(event){
				case I_Rcv_DPA:
					I_Disc(p);
					p->state = Closed;
					break;
				case R_Rcv_DPA:
					R_Disc(p);
					p->state = Closed;
					break;
				case Timeout:
					if (p->I_sock>=0) Error(p,p->I_sock);
					if (p->R_sock>=0) Error(p,p->R_sock);
					p->state = Closed;
					break;
				case I_Peer_Disc:
					I_Disc(p);
					p->state = Closed;
					break;
				case R_Peer_Disc:
					R_Disc(p);
					p->state = Closed;
					break;
				default:
					LOG(L_DBG,"DBG:sm_process(): In state %s invalid event %s\n",
						dp_states[p->state],dp_events[event-101]);
					goto error;
			}
			break;				
	}
	if (!peer_locked) lock_release(p->lock);
	
	if (msg_received)
		Rcv_Process(p,msg);
	
	return 1;	
error:
	if (!peer_locked) lock_release(p->lock);
	return 0;	
}

/**
 * Initiator - Send Connection Request.
 * Tries to connect to the remote peer's socket. If the connection is refused, the new
 * state is I_Rcv_Conn_NAck, else I_Rcv_Conn_Ack.
 * \note Must be called with a lock on the peer.
 * @param p - peer to send to
 * @returns the new state for the peer
 */
peer_state_t I_Snd_Conn_Req(peer *p)
{
	LOG(L_INFO,"DBG:I_Snd_Conn_Req(): Peer %.*s \n",
		p->fqdn.len,p->fqdn.s);

	if (p->I_sock>0) close(p->I_sock);
	p->I_sock = -1;
	p->I_sock = peer_connect(p);
	if (p->I_sock<0){
		return I_Rcv_Conn_NAck;
	}
	
	return I_Rcv_Conn_Ack;
}

/**
 * Cleans a socket by closing it and removing the reference
 * \note Must be called with a lock on the peer.
 * @param p - the peer
 * @param sock - socket to close
 */
void Cleanup(peer *p,int sock)
{
	if (sock<0) return;
	close(sock);
	if (p->I_sock == sock) p->I_sock = -1;
	if (p->R_sock == sock) p->R_sock = -1;
}

/**
 * Error action for a peer, triggers a Cleanup action.
 * \note Must be called with a lock on the peer.
 * @param p - the peer
 * @param sock - socket to close
 */
void Error(peer *p, int sock)
{
	Cleanup(p,sock);
}

/**
 * Adds the Applications to a Capability Exchange message.
 * \note Must be called with a lock on the peer.
 * @param msg - the message to add to (request/answer)
 * @param p - the peer to add applications from
 */
static inline void Snd_CE_add_applications(AAAMessage *msg,peer *p)
{
	int i;
	app_config *app;
	char x[4];
	AAA_AVP *avp1,*avp2;
	AAA_AVP_LIST list;
	str group;
	list.head=0;list.tail=0;
	
	for(i=0;i<config->applications_cnt;i++){
		app = config->applications+i;
		if (app->vendor==0){
			set_4bytes(x,app->id);
			AAACreateAndAddAVPToMessage(msg,
				(app->type==DP_AUTHORIZATION?AVP_Auth_Application_Id:AVP_Acct_Application_Id),
				AAA_AVP_FLAG_MANDATORY,0,x,4);
		}else{
			set_4bytes(x,app->vendor);
			avp1 = AAACreateAVP(AVP_Vendor_Id,AAA_AVP_FLAG_MANDATORY,0,x,4, AVP_DUPLICATE_DATA);			
			AAAAddAVPToList(&list,avp1);
			
			set_4bytes(x,app->id);
			avp2 = AAACreateAVP((app->type==DP_AUTHORIZATION?AVP_Auth_Application_Id:AVP_Acct_Application_Id),
				AAA_AVP_FLAG_MANDATORY,0,x,4,AVP_DUPLICATE_DATA);			
			AAAAddAVPToList(&list,avp2);
		
			group = AAAGroupAVPS(list);	
			AAAFreeAVPList(&list);
			
			AAACreateAndAddAVPToMessage(msg,
				AVP_Vendor_Specific_Application_Id,
				AAA_AVP_FLAG_MANDATORY,0,group.s,group.len);
			shm_free(group.s);
		}
	}
}

/**
 * Send a Capability Exchange Request.
 * \note Must be called with a lock on the peer.
 * @param p - the peer to send to
 */
void I_Snd_CER(peer *p)
{
	AAAMessage *cer=0;
//	AAA_AVP *avp;
	unsigned long ip;
	struct sockaddr_in6 addr;
	socklen_t addrlen;
	char x[18];
	
	cer = AAANewMessage(Code_CE,0,0,0);
	if (!cer) return;
	cer->hopbyhopId = next_hopbyhop();
	cer->endtoendId = next_endtoend();
	addrlen = sizeof(struct sockaddr_in6);
	if (getsockname(p->I_sock,(struct sockaddr*) &addr, &addrlen) == -1) { 
		LOG(L_ERR,"ERROR:I_Snd_CER(): Error on finding local host address > %s\n",strerror(errno));
	}else{
		switch(addr.sin6_family){
			case AF_INET:
				set_2bytes(x,1);
				ip = htonl(((struct sockaddr_in*)&addr)->sin_addr.s_addr);
				set_4bytes(x+2,ip);
				AAACreateAndAddAVPToMessage(cer,AVP_Host_IP_Address,AAA_AVP_FLAG_MANDATORY,0,x,6);
				break;
			case AF_INET6:
				set_2bytes(x,2);
				memcpy(x+2,addr.sin6_addr.s6_addr,16);
				AAACreateAndAddAVPToMessage(cer,AVP_Host_IP_Address,AAA_AVP_FLAG_MANDATORY,0,x,18);
				break;
			default:
				LOG(L_ERR,"ERROR:I_Snd_CER(): unknown address type with family %d\n",addr.sin6_family);
		}
	}

	set_4bytes(x,config->vendor_id);
	AAACreateAndAddAVPToMessage(cer,AVP_Vendor_Id,AAA_AVP_FLAG_MANDATORY,0,x,4);
			
	AAACreateAndAddAVPToMessage(cer,AVP_Product_Name,AAA_AVP_FLAG_MANDATORY,0,config->product_name.s,config->product_name.len);

	Snd_CE_add_applications(cer,p);
//	peer_send(p,p->I_sock,cer,1);
	peer_send_msg(p,cer);
}


void add_peer_application(peer *p, int id, int vendor, app_type type)
{
	int i;
	if (!p->applications) return;
	for(i=0;i<p->applications_cnt;i++)
		if (p->applications[i].id == id &&
			p->applications[i].vendor == vendor &&
			p->applications[i].type == type) return;

	p->applications[p->applications_cnt].id = id;
	p->applications[p->applications_cnt].vendor = vendor;
	p->applications[p->applications_cnt].type = type;
	p->applications_cnt++;	 
}

void save_peer_applications(peer *p,AAAMessage *msg)
{
	int total_cnt=0;
	AAA_AVP *avp,*avp_vendor,*avp2;
	AAA_AVP_LIST group;
	int id,vendor;

	if (p->applications) {
		shm_free(p->applications);
		p->applications = 0;
		p->applications_cnt = 0;
	}
	for(avp=msg->avpList.head;avp;avp = avp->next)
		switch (avp->code){
			case AVP_Auth_Application_Id:
			case AVP_Acct_Application_Id:
			case AVP_Vendor_Specific_Application_Id:				
				total_cnt+=2;/* wasteful, but let's skip decoding */	
				break;				
		}
	p->applications_cnt = 0;
	p->applications = shm_malloc(sizeof(app_config)*total_cnt);
	if (!p->applications){	
		LOG(L_ERR,"ERROR:save_peer_applications(): Error allocating %d bytes! No applications saved...\n",
			sizeof(app_config)*total_cnt);
		return;
	}
	for(avp=msg->avpList.head;avp;avp = avp->next)
	{
		switch (avp->code){
			case AVP_Auth_Application_Id:
				id = get_4bytes(avp->data.s);	
				add_peer_application(p,id,0,DP_AUTHORIZATION);	
				break;
			case AVP_Acct_Application_Id:
				id = get_4bytes(avp->data.s);	
				add_peer_application(p,id,0,DP_ACCOUNTING);	
				break;
			case AVP_Vendor_Specific_Application_Id:
				group = AAAUngroupAVPS(avp->data);
				avp_vendor = AAAFindMatchingAVPList(group,group.head,AVP_Vendor_Id,0,0);				
				avp2 = AAAFindMatchingAVPList(group,group.head,AVP_Auth_Application_Id,0,0);				
				if (avp_vendor&&avp2){
					vendor = get_4bytes(avp_vendor->data.s);
					id = get_4bytes(avp2->data.s);
					add_peer_application(p,id,vendor,DP_AUTHORIZATION);						
				}
				avp2 = AAAFindMatchingAVPList(group,group.head,AVP_Acct_Application_Id,0,0);				
				if (avp_vendor&&avp2){
					vendor = get_4bytes(avp_vendor->data.s);
					id = get_4bytes(avp2->data.s);
					add_peer_application(p,id,vendor,DP_ACCOUNTING);					
				}
				AAAFreeAVPList(&group);
				break;
				
		}
	}	
}

/**
 * Process a Capability Exchange Answer.
 * \note Must be called with a lock on the peer.
 * @param p - the peer that the CEA was received from
 * @param cea - the CEA message
 * @returns the result-code from CEA or AAA_UNABLE_TO_COMPLY if no result-code found
 */	
int Process_CEA(peer *p,AAAMessage *cea)
{
	AAA_AVP *avp;
	avp = AAAFindMatchingAVP(cea,cea->avpList.head,AVP_Result_Code,0,0);
	save_peer_applications(p,cea);
	AAAFreeMessage(&cea);
	if (!avp) return AAA_UNABLE_TO_COMPLY;
	else return get_4bytes(avp->data.s);
}	

/**
 * Initiator - disconnect peer.
 * \note Must be called with a lock on the peer.
 * @param p - the peer to disconnect
 */
void I_Disc(peer *p)
{
	if (p->I_sock>=0){
		close(p->I_sock);
		p->I_sock = -1;
	}
}

/**
 * Receiver - disconnect peer.
 * \note Must be called with a lock on the peer.
 * @param p - the peer to disconnect
 */
void R_Disc(peer *p)
{
	if (p->R_sock>=0){
		close(p->R_sock);
		p->R_sock = -1;
	}
}

/**
 * Process a Diameter Watch-dog Request.
 * The last activity timer is updated in the sm_process(), on any message received.
 * \note Must be called with a lock on the peer.
 * @param p - the peer that the DWR was received from
 * @param dwr - the DWR message
 * @returns AAA_SUCCESS
 */	
int Process_DWR(peer *p,AAAMessage *dwr)
{
	return AAA_SUCCESS;
}

/**
 * Process a Diameter Watch-dog Answer.
 * The flag for waiting a DWA is reseted.
 * \note Must be called with a lock on the peer.
 * @param p - the peer that the DWR was received from
 * @param dwa - the DWA message
 */	
void Process_DWA(peer *p,AAAMessage *dwa)
{
	p->waitingDWA = 0;
	AAAFreeMessage(&dwa);
}

/**
 * Sends a Diameter Watch-dog Request.
 * The flag for waiting a DWA is set.
 * \note Must be called with a lock on the peer.
 * @param p - the peer that the DWR was received from
 */	
void Snd_DWR(peer *p)
{
	AAAMessage *dwr=0;
	
	dwr = AAANewMessage(Code_DW,0,0,0);
	if (!dwr) return;	
	dwr->hopbyhopId = next_hopbyhop();
	dwr->endtoendId = next_endtoend();
	if (p->state==I_Open)
		peer_send_msg(p,dwr);	
	else
		peer_send_msg(p,dwr);	
}

/**
 * Sends a Diameter Watch-dog Answer.
 * \note Must be called with a lock on the peer.
 * @param p - the peer that the DWR was received from
 * @param dwr - the DWR message
 * @param result_code - the Result-Code to attach to DWA
 * @param sock - socket to send on
 */	
void Snd_DWA(peer *p,AAAMessage *dwr,int result_code,int sock)
{
	AAAMessage *dwa;
	char x[4];	

	dwa = AAANewMessage(Code_DW,0,0,dwr);
	if (!dwa) goto done;	

	set_4bytes(x,result_code);
	AAACreateAndAddAVPToMessage(dwa,AVP_Result_Code,AAA_AVP_FLAG_MANDATORY,0,x,4);
	
	peer_send_msg(p,dwa);
done:	
	AAAFreeMessage(&dwr);
}

/**
 * Sends a Disconnect Peer Request.
 * \note Must be called with a lock on the peer.
 * @param p - the peer to send to
 */
void Snd_DPR(peer *p)
{
	AAAMessage *dpr=0;
	char x[4];
	
	dpr = AAANewMessage(Code_DP,0,0,0);	
	if (!dpr) return;
	dpr->hopbyhopId = next_hopbyhop();
	dpr->endtoendId = next_endtoend();

	set_4bytes(x,0/*busy*/);
	AAACreateAndAddAVPToMessage(dpr,AVP_Disconnect_Cause,AAA_AVP_FLAG_MANDATORY,0,x,4);

	if (p->state==I_Open)
		peer_send_msg(p,dpr);	
	else
		peer_send_msg(p,dpr);	
}

/**
 * Sends a Disconnect Peer Answer.
 * \note Must be called with a lock on the peer.
 * @param p - the peer to send to
 * @param dpr - the DPR message
 * @param result_code - the Result-Code to attach to DPA
 * @param sock - socket to send on
 */
void Snd_DPA(peer *p,AAAMessage *dpr,int result_code,int sock)
{
	AAAMessage *dpa;

	dpa = AAANewMessage(Code_DP,0,0,dpr);	
	if (dpa) peer_send_msg(p,dpa);
	AAAFreeMessage(&dpr);
}

/**
 * Receiver - Accept a connection.
 * \note Must be called with a lock on the peer.
 * @param p - peer identification
 * @param sock - socket to communicate through
 */
void R_Accept(peer *p,int sock)
{
	p->R_sock = sock;
	touch_peer(p);
}

/**
 * Receiver - Reject a connection.
 * \note Must be called with a lock on the peer.
 * @param p - peer identification
 * @param sock - socket to communicate through
 */
void R_Reject(peer *p,int sock)
{
	close(sock);
}

/**
 * Process a Capabilities Exchange Request.
 * Checks whether there are common applications.
 * \note Must be called with a lock on the peer.
 * @param p - peer identification
 * @param cer - the CER message
 * @returns the Result-Code of the operation, AAA_SUCCESS or AAA_NO_COMMON_APPLICATION
 */
int Process_CER(peer *p,AAAMessage *cer)
{
	int common_app=0;
	AAA_AVP *avp,*avp_vendor,*avp2;
	AAA_AVP_LIST group;
	int i,id,vendor;
	for(avp=cer->avpList.head;avp;avp = avp->next)
	{
		switch (avp->code){
			case AVP_Auth_Application_Id:
				id = get_4bytes(avp->data.s);	
				for(i=0;i<config->applications_cnt;i++)
					if (id == config->applications[i].id &&
						config->applications[i].vendor==0 &&
						config->applications[i].type==DP_AUTHORIZATION) common_app++;	
				break;
			case AVP_Acct_Application_Id:
				id = get_4bytes(avp->data.s);	
				for(i=0;i<config->applications_cnt;i++)
					if (id == config->applications[i].id &&
						config->applications[i].vendor==0 &&
						config->applications[i].type==DP_ACCOUNTING) common_app++;	
				break;
			case AVP_Vendor_Specific_Application_Id:
				group = AAAUngroupAVPS(avp->data);
				avp_vendor = AAAFindMatchingAVPList(group,group.head,AVP_Vendor_Id,0,0);				
				avp2 = AAAFindMatchingAVPList(group,group.head,AVP_Auth_Application_Id,0,0);				
				if (avp_vendor&&avp2){
					vendor = get_4bytes(avp_vendor->data.s);
					id = get_4bytes(avp2->data.s);
					for(i=0;i<config->applications_cnt;i++)
						if (id == config->applications[i].id &&
							config->applications[i].vendor==vendor &&
							config->applications[i].type==DP_AUTHORIZATION) common_app++;	
					
				}
				avp2 = AAAFindMatchingAVPList(group,group.head,AVP_Acct_Application_Id,0,0);				
				if (avp_vendor&&avp2){
					vendor = get_4bytes(avp_vendor->data.s);
					id = get_4bytes(avp2->data.s);
					for(i=0;i<config->applications_cnt;i++)
						if (id == config->applications[i].id &&
							config->applications[i].vendor==vendor &&
							config->applications[i].type==DP_ACCOUNTING) common_app++;	
					
				}
				AAAFreeAVPList(&group);
				break;
				
		}
	}
	
	if (common_app!=0){
		save_peer_applications(p,cer);
		return AAA_SUCCESS;
	}else 
		return AAA_NO_COMMON_APPLICATION;	
}

/**
 * Send a Capabilities Exchange Answer.
 * Checks whether there are common applications.
 * \note Must be called with a lock on the peer.
 * @param p - peer identification
 * @param cer - the CER message
 * @param result_code - the Result-Code to send
 * @param sock - socket to send through
 */
void Snd_CEA(peer *p,AAAMessage *cer,int result_code,int sock)
{
	AAAMessage *cea;
	unsigned int ip;
	struct sockaddr_in6 addr;
	socklen_t addrlen;
	char x[18];
	
	cea = AAANewMessage(Code_CE,0,0,cer);	
	if (!cea) goto done;
	
	addrlen = sizeof(struct sockaddr_in6);
	if (getsockname(sock, (struct sockaddr*)&addr, &addrlen) == -1) { 
		LOG(L_ERR,"ERROR:Snd_CEA(): Error on finding local host address > %s\n",strerror(errno));
	}else{
		switch(addr.sin6_family){
			case AF_INET:
				set_2bytes(x,1);
				ip = htonl(((struct sockaddr_in*)&addr)->sin_addr.s_addr);
				set_4bytes(x+2,ip);
				AAACreateAndAddAVPToMessage(cea,AVP_Host_IP_Address,AAA_AVP_FLAG_MANDATORY,0,x,6);
				break;
			case AF_INET6:
				set_2bytes(x,2);
				memcpy(x+2,addr.sin6_addr.s6_addr,16);
				AAACreateAndAddAVPToMessage(cea,AVP_Host_IP_Address,AAA_AVP_FLAG_MANDATORY,0,x,18);
				break;
			default:
				LOG(L_ERR,"ERROR:Snd_CEA(): unknown address type with family %d\n",addr.sin6_family);
		}
	}

	set_4bytes(x,config->vendor_id);
	AAACreateAndAddAVPToMessage(cea,AVP_Vendor_Id,AAA_AVP_FLAG_MANDATORY,0,x,4);
			
	AAACreateAndAddAVPToMessage(cea,AVP_Product_Name,AAA_AVP_FLAG_MANDATORY,0,config->product_name.s,config->product_name.len);

	set_4bytes(x,result_code);
	AAACreateAndAddAVPToMessage(cea,AVP_Result_Code,AAA_AVP_FLAG_MANDATORY,0,x,4);

	Snd_CE_add_applications(cea,p);

	peer_send(p,sock,cea,1);
done:	
	AAAFreeMessage(&cer);
}

/**
 * Perform the Election mechanism.
 * When 2 peers connect to each-other at the same moment, an election is triggered.
 * That means that based on the alphabetical relation between the FQDNs, one peer is
 * kept as winner on Initiator and that connection is kept, while the other keeps the same
 * connection as Receiver and drops its Initiator connection.
 * \note Must be called with a lock on the peer.
 * @param p - peer identification
 * @param cer - the CER message
 * @returns 1 if winning, 0 if loosing
 */  
int Elect(peer *p,AAAMessage *cer)
{
	/* returns if we win the election */
	AAA_AVP *avp;
	str remote,local;
	int i,d;

	local = config->fqdn;

	avp = AAAFindMatchingAVP(cer,cer->avpList.head,AVP_Origin_Host,0,0);
	if (!avp) {
		return 1;
	}else{
		remote = avp->data;
		for(i=0;i<remote.len&&i<local.len;i++){
			d = ((unsigned char) local.s[i])-((unsigned char) remote.s[i]);
			if (d>0) return 1;
			if (d<0) return 0;
		}
		if (local.len>remote.len) return 1;
		return 0;
	}
}

/**
 * Sends a message to the peer.
 * \note Must be called with a lock on the peer.
 * @param p - peer to send to
 * @param msg - message to send
 */
void Snd_Message(peer *p, AAAMessage *msg)
{
	AAASession *session=0;
	int rcode;
	int send_message_before_session_sm=0;
	LOG(L_DBG,"Snd_Message called to peer [%.*s] for %s with code %d \n",
		p->fqdn.len,p->fqdn.s,is_req(msg)?"request":"response",msg->commandCode);
	touch_peer(p);
	if (msg->sessionId) session = get_session(msg->sessionId->data);
	
	if (session){
		LOG(L_DBG,"There is a session of type %d\n",session->type);
		switch (session->type){
			case AUTH_CLIENT_STATEFULL:
				if (is_req(msg))
					auth_client_statefull_sm_process(session,AUTH_EV_SEND_REQ,msg);
				else {
					if (msg->commandCode == IMS_ASA){
						if (!msg->res_code){
							msg->res_code = AAAFindMatchingAVP(msg,0,AVP_Result_Code,0,0);
						}
						if (!msg->res_code) auth_client_statefull_sm_process(session,AUTH_EV_SEND_ASA_UNSUCCESS,msg);
						else {
							rcode = get_4bytes(msg->res_code->data.s);
							if (rcode>=2000 && rcode<3000) {
								peer_send_msg(p,msg);
								send_message_before_session_sm=1;
								auth_client_statefull_sm_process(session,AUTH_EV_SEND_ASA_SUCCESS,msg);
							}
							else auth_client_statefull_sm_process(session,AUTH_EV_SEND_ASA_UNSUCCESS,msg);
						}
						
					}else
						auth_client_statefull_sm_process(session,AUTH_EV_SEND_ANS,msg);
				}
				break;
			case AUTH_SERVER_STATEFULL:
				LOG(L_DBG,"this message is matched here to see what request or reply it is\n");
				if (is_req(msg))
				{
					if (msg->commandCode== IMS_ASR)
					{
						LOG(L_DBG,"ASR\n");
						auth_server_statefull_sm_process(session,AUTH_EV_SEND_ASR,msg);
					} else {
						//would be a RAR but ok!
						LOG(L_DBG,"other request\n");
						auth_server_statefull_sm_process(session,AUTH_EV_SEND_REQ,msg);
					}
				} else {
					if (msg->commandCode == IMS_STR)
					{
						LOG(L_DBG,"STA\n");
						auth_server_statefull_sm_process(session,AUTH_EV_SEND_STA,msg);
					} else {
						LOG(L_DBG,"other reply\n");
						auth_server_statefull_sm_process(session,AUTH_EV_SEND_ANS,msg);
					}
				}
				break;				 
			default:
				break;
		}
		sessions_unlock(session->hash);
	}
	if (!send_message_before_session_sm) peer_send_msg(p,msg);
	
}

/**
 * Processes an incoming message.
 * This actually just puts the message into a message queue. One worker will pick-it-up
 * and do the actual processing.
 * \note Must be called with a lock on the peer.
 * @param p - peer received from
 * @param msg - the message received
 */ 
void Rcv_Process(peer *p, AAAMessage *msg)
{
	AAASession *session=0;
	str id={0,0};
	unsigned int hash; // we need this here because after the sm_processing , we might end up
					   // with no session any more
	int nput=0;
	if (msg->sessionId) session = get_session(msg->sessionId->data);

	if (session){
		hash=session->hash;
		switch (session->type){
			case AUTH_CLIENT_STATEFULL:
				if (is_req(msg)){
					if (msg->commandCode==IMS_ASR)
						auth_client_statefull_sm_process(session,AUTH_EV_RECV_ASR,msg);
					else 
						auth_client_statefull_sm_process(session,AUTH_EV_RECV_REQ,msg);
				}else {
					if (msg->commandCode==IMS_STA)
						nput=auth_client_statefull_sm_process(session,AUTH_EV_RECV_STA,msg);
					else
						auth_client_statefull_sm_process(session,AUTH_EV_RECV_ANS,msg);
				}
				break;
			 case AUTH_SERVER_STATEFULL:
			 	if (is_req(msg))
			 	{
			 		auth_server_statefull_sm_process(session,AUTH_EV_RECV_REQ,msg);
			 	}else{
			 		if (msg->commandCode==IMS_ASA)
			 			auth_server_statefull_sm_process(session,AUTH_EV_RECV_ASA,msg);
			 		else
			 			auth_server_statefull_sm_process(session,AUTH_EV_RECV_ANS,msg);
			 	}
			 	break;
			default:
				break;			 
		}
		sessions_unlock(hash);
	}else{
		if (msg->sessionId){
			if (msg->commandCode == IMS_ASR) 
				auth_client_statefull_sm_process(0,AUTH_EV_RECV_ASR,msg);
			else
			{
				if (msg->commandCode == IMS_AAR)
				{
					//an AAR starts the Authorization State Machine for the server
					id.s = shm_malloc(msg->sessionId->data.len);
					if (!id.s){
						LOG(L_ERR,"Error allocating %d bytes of shm!\n",msg->sessionId->data.len);
						id.len = 0;
					}else{
						id.len = msg->sessionId->data.len;
						memcpy(id.s,msg->sessionId->data.s,id.len);
						session=new_session(id,AUTH_SERVER_STATEFULL);
						if (session)
						{
							hash=session->hash;
							add_session(session);
							sessions_lock(hash);
							//create an auth session with the id of the message!!!
							//and get from it the important data
							auth_server_statefull_sm_process(session,AUTH_EV_RECV_REQ,msg);
							sessions_unlock(hash);
						}
					}
				}

			}
			//this is quite a big error
			//if (msg->commandCode == IMS_AAR)
			//{
				//session=AAACreateAuthSession(0,0,1,0,0);
				
				//shm_str_dup(session->id,msg->sessionId->data);

			//}
			// Any other cases to think about?	 
		} 
				 
	}
	if (!nput && !put_task(p,msg)){
		LOG(L_ERR,"ERROR:Rcv_Process(): Queue refused task\n");
		if (msg) AAAFreeMessage(&msg); 
	}
	//if (msg) LOG(L_ERR,"DBG:Rcv_Process(): task added to queue command %d, flags %#1x endtoend %u hopbyhop %u\n",msg->commandCode,msg->flags,msg->endtoendId,msg->hopbyhopId);
	
//	AAAPrintMessage(msg);
	
}


