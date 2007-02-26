/*
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
 * Serving-CSCF - Registration Related Operations
 * 
 * 
 *  \author Dragos Vingarzan vingarzan -at- fokus dot fraunhofer dot de
 * 
 */
 
#ifndef S_CSCF_REGISTRATION_H_
#define S_CSCF_REGISTRATION_H_

#include "mod.h"
#include "../../locking.h"

int S_add_path_service_routes(struct sip_msg *msg,char *str1,char *str2 );

int S_add_service_route(struct sip_msg *msg,char *str1,char *str2 );

int S_check_visited_network_id(struct sip_msg *msg,char *str1,char *str2 );

int S_REGISTER_reply(struct sip_msg *msg, int code,  char *text);

int S_is_integrity_protected(struct sip_msg *msg,char *str1,char *str2 );

int S_is_authorized(struct sip_msg *msg,char *str1,char *str2 );

int S_challenge(struct sip_msg *msg,char *str1,char *str2 );


/** Enumeration for the Authorization Vector status */
enum auth_vector_status {
	AUTH_VECTOR_UNUSED = 0,
	AUTH_VECTOR_SENT = 1,
	AUTH_VECTOR_USED = 2,
	AUTH_VECTOR_USELESS = 3
} ;

/** Authorization Vector storage structure */
typedef struct _auth_vector {
	int item_number;	/**< index of the auth vector		*/
	str algorithm;		/**< algorithm						*/
	str authenticate;	/**< challenge (rand|autn in AKA)	*/
	str authorization; 	/**< expected response				*/
	str ck;				/**< Cypher Key						*/
	str ik;				/**< Integrity Key					*/
	unsigned int expires;/**< expires in (after it is sent)	*/
	
	enum auth_vector_status status;/**< current status		*/
	struct _auth_vector *next;/**< next av in the list		*/
	struct _auth_vector *prev;/**< previous av in the list	*/
} auth_vector;



/** Set of auth_vectors used by a private id */
typedef struct _auth_userdata{
	str private_identity;	/**< authorization username		*/
	str public_identity;	/**< public identity linked to	*/
	unsigned int hash;		/**< hash of the auth data		*/
	unsigned int expires;	/**< expires in					*/
	
	auth_vector *head;		/**< first auth vector in list	*/
	auth_vector *tail;		/**< last auth vector in list	*/
	struct _auth_userdata *next;/**< next element in list	*/
	struct _auth_userdata *prev;/**< previous element in list*/
} auth_userdata;

/** Authorization user data hash slot */
typedef struct {
	void *head;				/**< first in the slot			*/ 
	void *tail;				/**< last in the slot			*/
} hash_slot_t;

/** User data hash table 
 * \todo move lock in auth_userdata to improve performance of registration
 */
typedef struct _auth_data{
	hash_slot_t *table;		/**< hash table 				*/
	int size;				/**< size of the hash table		*/
	
	gen_lock_t *lock;		/**< lock for operations		*/
} auth_data;





int pack_challenge(struct sip_msg *msg,str realm,auth_vector *av);

int S_MAR(struct sip_msg *msg, str public_identity, str private_identity,
					int count,str algorithm,str nonce,str auts,str server_name,str realm);


/*
 * Storage of authentication vectors
 */
 
int auth_data_init(int size);

void auth_data_destroy();

auth_vector *new_auth_vector(int item_number,str auth_scheme,str authenticate,
			str authorization,str ck,str ik);
void free_auth_vector(auth_vector *av);

auth_userdata *new_auth_userdata(str private_identity,str public_identity);
void free_auth_userdata(auth_userdata *aud);					

int add_auth_vector(str private_identity,str public_identity,auth_vector *av);
auth_vector* get_auth_vector(str private_identity,str public_identity,int status,str *nonce);

int drop_auth_userdata(str private_identity,str public_identity);

inline void start_reg_await_timer(auth_vector *av);

void reg_await_timer(unsigned int ticks, void* param);



#endif //S_CSCF_REGISTRATION_H_