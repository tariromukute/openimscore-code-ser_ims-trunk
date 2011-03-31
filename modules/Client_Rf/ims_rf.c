/*
 * $Id$
 *
 * Copyright (C) 2008-2009 FhG Fokus
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
 * author Ancuta Corici, 
 * 	email andreea dot ancuta dot corici -at- fokus dot fraunhofer dot de
 */
/**
 * Client_Rf - implementation of the Rf interface from the CTF side, according to TS32.299 R7
 * 
 * Scope:
 * - Exports parameters and functions
 * - Initialization functions
 * 
 */
 
#ifdef CDP_FOR_SER

#include "mod.h"

#include "../../parser/msg_parser.h"
#include "../../parser/parse_uri.h"
#include "../../sr_module.h"
#include "../../socket_info.h"
#include "../../timer.h"
#include "../../locking.h"
#include "../../modules/tm/tm_load.h"
#include "../cdp/cdp_load.h"
#include "../cdp/diameter_code_avp.h"
#include "../cdp_avp/mod_export.h"

#include "diameter_rf.h"
#include "ims_rf.h"
#include "Rf_data.h"
#include "sip.h"
#include "acr.h"
#include "config.h"
#include "sip_body.h"
#include "sdp_helpr_funcs.h"

extern struct tm_binds tmb;
extern cdp_avp_bind_t *cavpb;
extern client_rf_cfg cfg;

/**
 * Retrieves the SIP request that generated a diameter transaction
 * @param hash - the tm hash value for this request
 * @param label - the tm label value for this request
 * @returns the SIP request
 */
struct sip_msg * trans_get_request_from_current_reply()
{
	struct cell *t;
	t = tmb.t_gett();
	if (!t || t==(void*) -1){
		LOG(L_ERR,"ERR:"M_NAME":trans_get_request_from_current_reply: Reply without transaction\n");
		return 0;
	}
	if (t) return t->uas.request;
	else return 0;
}

int get_sip_header_info(struct sip_msg * req,
	        struct sip_msg * reply,	
		int interim,
		int32_t * acc_record_type,
		str * sip_method,
		str * event, uint32_t * expires,
		str * callid, str * from_uri, str * to_uri){

	LOG(L_DBG, "retrieving sip info\n");
	sip_method->s = req->first_line.u.request.method.s;
	sip_method->len = req->first_line.u.request.method.len;
	
	if(!interim && strncmp(sip_method->s, "INVITE",6) == 0)
		*acc_record_type = AAA_ACCT_START;
	else if	(strncmp(sip_method->s, "BYE",3) == 0)
		*acc_record_type = AAA_ACCT_STOP;
	else if(interim && (strncmp(sip_method->s, "INVITE", 6) == 0	
				|| strncmp(sip_method->s, "UPDATE", 6) ==0))
		*acc_record_type = AAA_ACCT_INTERIM;
	else
		*acc_record_type = AAA_ACCT_EVENT;

	*event = cscf_get_event(req);
	*expires = cscf_get_expires_hdr(req, 0);
	*callid = cscf_get_call_id(req, NULL);

	if(!cscf_get_from_uri(req, from_uri))
		goto error;

	if(!cscf_get_to_uri(req, to_uri))
		goto error;

	LOG(L_DBG, "retrieved sip info : sip_method %.*s acc_record_type %i, event %.*s expires %u "
			"call_id %.*s from_uri %.*s to_uri %.*s\n", 
			sip_method->len, sip_method->s, *acc_record_type, event->len, event->s, *expires, 
			callid->len, callid->s, from_uri->len, from_uri->s, to_uri->len, to_uri->s);

	return 1;
error:
	return 0;
}

int get_ims_charging_info(struct sip_msg *req, 
			struct sip_msg * reply, 
			str * icid, 
			str * orig_ioi, 
			str * term_ioi){

	LOG(L_DBG, "get ims charging info\n");
	if(req)
		cscf_get_p_charging_vector(req, icid, orig_ioi, term_ioi);
	if(reply)
		cscf_get_p_charging_vector(reply, icid, orig_ioi, term_ioi);

	return 1;
}

int get_timestamps(struct sip_msg * req, 
			struct sip_msg * reply, 
			time_t * req_timestamp, 
			time_t * reply_timestamp){

	if(reply)
		*reply_timestamp = time(NULL);
	if(req)
		*req_timestamp = time(NULL);
	return 1;
}

/**
 * extract the media description list from the SDP of the message, if SDP body included
 */

int get_media_description_list(char* start, char * end, sdp_media_component_t * sdp_media_elem){

	str sdp_par = {0,0};
	str_list_slot_t * str_slot = NULL;
	int len = end-start;
	LOG(L_DBG, "searching from start %.*s\n", len, start);

	if((sdp_par.s = find_next_sdp_line(start, end,'a',NULL))){
		sdp_par.len = get_line_length(sdp_par.s, end);
		LOG(L_DBG, "a line is %.*s\n", sdp_par.len, sdp_par.s);
		mem_new(str_slot, sizeof(str_list_t), pkg);
		str_dup(str_slot->data,sdp_par,pkg);
//		WL_APPEND(&(sdp_media_elem->sdp_media_descriptions),str_slot);
		//str_slot=0;
	}
/*
	if((sdp_par.s = find_next_sdp_line(start, end,'b',NULL))){
		sdp_par.len = get_line_length(sdp_par.s, end);
		LOG(L_DBG, "b line is %.*s\n", sdp_par.len, sdp_par.s);
		mem_new(str_slot, sizeof(str_list_t), pkg);
		str_dup(str_slot->data, sdp_par, pkg);
		WL_APPEND(&(sdp_media_elem->sdp_media_descriptions),str_slot);
		//str_slot=0;
	}

	if((sdp_par.s = find_next_sdp_line(start, end,'c',NULL))){
		sdp_par.len = get_line_length(sdp_par.s, end);
		LOG(L_DBG, "c line is %.*s\n", sdp_par.len, sdp_par.s);
		mem_new(str_slot, sizeof(str_list_t), pkg);
		//str_dup(str_slot->data, sdp_par, pkg);
		//WL_APPEND(&(sdp_media_elem->sdp_media_descriptions),str_slot);
		//str_slot=0;
	}
*/
	return 1;
out_of_memory:
	if(str_slot) mem_free(str_slot, pkg);
	return 0;
}

int append_sip_sdp_comp(struct sip_msg * msg, 
			int sdp_type, 
			sdp_media_component_list_t * sdp_media_comps){
	

	sdp_media_component_t * sdp_media_elem = NULL;
	str sdp_body = {0,0};
	str mline_s = {0,0}, next_mline_s= {0,0};
	char * end, *media_info_end;
	str copy_sdp_body = {0,0};

	LOG(L_DBG, "checking if the message has sdp body\n");
	/*if the SIP message contains SDP body*/
	if(extract_sdp_body(msg, &copy_sdp_body)==-1) {
		return 1;
	}
	
	str_dup(sdp_body, copy_sdp_body, pkg);
	
	end = sdp_body.s + sdp_body.len;

	mline_s.s = find_sdp_line(sdp_body.s,end,'m');
	for(;mline_s.s;mline_s.s = next_mline_s.s){
		
		mline_s.len = get_line_length(mline_s.s, end);
		LOG(L_DBG, "mline is %.*s, sdp body is %.*s\n", 
			mline_s.len, mline_s.s, sdp_body.len, sdp_body.s);
		
		next_mline_s.s = find_next_sdp_line(mline_s.s,end,'m',NULL);
		
		mem_new(sdp_media_elem, sizeof(sdp_media_component_t), pkg);

		str_dup_ptr_ptr(sdp_media_elem->sdp_media_name, &mline_s, pkg);
		LOG(L_DBG, "mline is %.*s, sdp body is %.*s\n", 
			mline_s.len, mline_s.s, sdp_body.len, sdp_body.s);
		
		media_info_end = sdp_body.s+sdp_body.len-5;
		LOG(L_DBG, "last 5 characters from sdp_body are: %.*s\n",
			5, media_info_end);
		media_info_end = end-1;
		LOG(L_DBG, "log3, media_info_end is %s\n", media_info_end);

		if(!get_media_description_list(mline_s.s+mline_s.len, end, 
					sdp_media_elem))
			goto error;
		LOG(L_DBG, "log4\n");
		
		mem_new(sdp_media_elem->sdp_type, sizeof(int32_t), pkg);
		*(sdp_media_elem->sdp_type) = sdp_type;
		WL_APPEND(sdp_media_comps, sdp_media_elem);
		LOG(L_DBG, "log6\n");
	}
	str_free(sdp_body,pkg);
	return 1;
out_of_memory:
	LOG(L_ERR, "append_sip_sdp_comp: out of pkg memory\n");
error:
	WL_FREE(sdp_media_elem, sdp_media_component_list_t, pkg);
	return 0;
}

int get_sdp_media_comp(struct sip_msg* req, 
			struct sip_msg * reply, 
			sdp_media_component_list_t * sdp_media_comps){

	LOG(L_DBG, "extracting the sdp info from the request\n");
	if(req && !(append_sip_sdp_comp(req, 0, sdp_media_comps)))
		goto error;

	LOG(L_DBG, "extracting the sdp info from the reply\n");
	if(reply && !(append_sip_sdp_comp(reply, 1, sdp_media_comps)))
		goto error;

	return 1;
error:
	WL_FREE_ALL(sdp_media_comps, sdp_media_component_list_t, pkg);
	return 0;
}

/*
 * creates the rf session for a session establishment
 * @param aor: the aor to be handled
 * @return: 0 - ok, -1 - error, -2 - out of memory
 */

Rf_ACR_t * dlg_create_rf_data(struct sip_msg * req,
	       			struct sip_msg * reply,	
				int dir, int interim){

	Rf_ACR_t * rf_data=0;
	str user_name ={0,0}, sip_method = {0,0}, event = {0,0}; 
	uint32_t expires = 0;
	str callid = {0,0}, to_uri = {0,0}, from_uri ={0,0}, 
	    icid= {0,0}, orig_ioi = {0,0}, term_ioi = {0,0};

	event_type_t * event_type = 0;
	ims_information_t * ims_info = 0;
	time_stamps_t * time_stamps = 0;
	time_t req_timestamp=0, reply_timestamp=0;
	int32_t acct_record_type;
	uint32_t acct_record_number;
	subscription_id_t subscr;
	sdp_media_component_list_t sdp_media_comps = {0,0};

	LOG(L_DBG, "in dlg_create_rf_data\n");

	if(!get_sip_header_info(req, reply, interim, &acct_record_type, 
				&sip_method, &event, &expires, 
				&callid, &from_uri, &to_uri))
		goto error;

	if(!get_subseq_acct_record_nb(callid, acct_record_type, &acct_record_number, dir, expires)){
		LOG(L_ERR, "ERR:"M_NAME":dlg_create_rf_data: could not retrieve "
			"accounting record number for session id %.*s\n", 
			callid.len, callid.s);
		goto error;
	}

	if(dir == 0)	user_name = from_uri;
	else 		user_name = to_uri;

/*	if(!get_ims_charging_info(req, reply, &icid, &orig_ioi, &term_ioi))
		goto error;
*/
	LOG(L_DBG, "retrieved ims charging info icid %.*s orig_ioi %.*s term_ioi %.*s\n",
			icid.len, icid.s, orig_ioi.len, orig_ioi.s, term_ioi.len, term_ioi.s);

	if(!get_timestamps(req, reply, &req_timestamp, &reply_timestamp))
		goto error;

	if(!get_sdp_media_comp(req, reply, &sdp_media_comps))
		goto error;
	LOG(L_DBG, "successfully extracted sdp media info , head: %p and tail %p\n",
		sdp_media_comps.head, sdp_media_comps.tail);

	if(!(event_type = new_event_type(&sip_method, &event, &expires)))
		goto error;
	
	if(!(time_stamps = new_time_stamps(&req_timestamp, NULL,
						&reply_timestamp, NULL)))
		goto error;

	if(!(ims_info = new_ims_information(event_type, 
					time_stamps,
					&callid, &callid,
					&from_uri, &to_uri, 
					&icid, &orig_ioi, &term_ioi, 
					&sdp_media_comps, dir)))
		goto error;

	LOG(L_DBG, "created ims information\n");

	//sdp_media_comps.head = sdp_media_comps.tail = NULL;

	event_type = 0;
	time_stamps = 0;

	subscr.type = Subscription_Type_IMPU;
	subscr.id.s = from_uri.s;
	subscr.id.len = from_uri.len;

	rf_data = new_Rf_ACR(acct_record_type, acct_record_number,
			&user_name, ims_info, &subscr);
	LOG(L_DBG, "created rf data\n");
	if(!rf_data) {
		LOG(L_ERR,"ERR:"M_NAME":dlg_create_rf_data: no memory left for generic\n");
		goto out_of_memory;
	}
	ims_info = 0;

	return rf_data;

out_of_memory:
	LOG(L_ERR, "dlg_create_rf_data: out of memory\n");
error:
	//WL_FREE_ALL(&sdp_media_comps, sdp_media_component_list_t, pkg);
	time_stamps_free(time_stamps);
	event_type_free(event_type);
	ims_information_free(ims_info);
	Rf_free_ACR(rf_data);
	return NULL;
}

int sip_create_rf_info(struct sip_msg * msg, int dir, int interim, Rf_ACR_t ** rf_data){
	
	struct sip_msg * req;

	LOG(L_DBG, "creating rf data\n");
	if(msg->first_line.type == SIP_REQUEST){
		/*end of session*/
		if(strncmp(msg->first_line.u.request.method.s, "BYE",3)==0 ){
			if(!(*rf_data = dlg_create_rf_data(msg, NULL, dir, interim)))
				goto error;
		}
	}else{
		LOG(L_DBG, "reply code is %i\n", msg->first_line.u.reply.statuscode);
		if(msg->first_line.u.reply.statuscode != 200)
		       return 1;

		req = trans_get_request_from_current_reply();
		if(!req) {
			LOG(L_ERR, "could not retrieve request from current transaction\n");
			return CSCF_RETURN_ERROR;
		}
		/*start of session*/
		 if(strncmp(req->first_line.u.request.method.s, INVITE, 6) == 0){

			if(!(*rf_data = dlg_create_rf_data(req, msg, dir, interim)))
				goto error;
		}
	}

	LOG(L_DBG, "created sip rf info\n");
	return 1;
error:
	return 0;
}

/**
 * Send an ACR to the CDF based on the SIP message (request or reply)
 * @param msg - SIP message
 * @param str1 - direction
 * @param str2 - 0 : initial or 1: subsequent
 * @returns #CSCF_RETURN_TRUE if OK, #CSCF_RETURN_ERROR on error
 */
int Rf_Send_ACR(struct sip_msg *msg,char *str1, char *str2){
	
	AAASession * auth = 0;
	Rf_ACR_t * rf_data = 0;
	int dir =0;
	
	LOG(L_DBG, "trying to create and send acr, params are: %s and %s\n", str1, str2);
	if(str1[0] == 'o' || str1[0] == 'O')
		dir = 0;
	else if (str1[0] == 't' || str1[0] == 'T')
		dir = 1;
	else {
		LOG(L_ERR, "ERR:"M_NAME":Rf_Send_ACR: invalid direction %s (accepted \"orig\" or \"term\")\n", str1);
		return CSCF_RETURN_ERROR;
	}

	if( str2[0] != '0' && str2[0]!='1'){
		LOG(L_ERR, "ERR:"M_NAME":Rf_Send_ACR: invalid type of request %s : 0 for initial and 1 for subsequent\n", str2);
		return CSCF_RETURN_ERROR;
	}

	if(!sip_create_rf_info(msg, dir, str2[0]-'0', &rf_data))
		goto error;
	
	if (!rf_data)
		return CSCF_RETURN_TRUE;

	if(!(auth = create_rf_session(rf_data)))
		goto error;

	if(!AAASendACR(auth, rf_data))
		goto error;
	
	//cavpb->cdp->AAAFreeMessage(&acr);
	cavpb->cdp->AAADropSession(auth);
	
	LOG(L_DBG, "Rf_Send_ACR:"M_NAME": request was created and sent\n");
	Rf_free_ACR(rf_data);

	return CSCF_RETURN_TRUE;
error:
	Rf_free_ACR(rf_data);
	if(auth){
		cavpb->cdp->AAASessionsUnlock(auth->hash);
		cavpb->cdp->AAADropSession(auth);
	}

	return CSCF_RETURN_ERROR;
}



#endif /*CDP_FOR_SER*/
