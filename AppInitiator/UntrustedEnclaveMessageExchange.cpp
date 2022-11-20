/*
 * Copyright (C) 2011-2021 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "sgx_eid.h"
#include "error_codes.h"
#include "datatypes.h"
#include "sgx_urts.h"
#include "UntrustedEnclaveMessageExchange.h"
#include "sgx_dh.h"

#include "fifo_def.h"

#include <map>

/* Function Description: This is OCALL interface for initiator enclave to get ECDH message 1 and session id from responder enclave
 * Parameter Description:
 *      [input, output] dh_msg1: pointer to ecdh msg1 buffer, this buffer is allocated in initiator enclave and filled by responder enclave
 *      [output] session_id: pointer to session id which is allocated by responder enclave
 * */
extern "C" ATTESTATION_STATUS session_request_ocall(sgx_dh_msg1_t* dh_msg1, uint32_t* session_id)
{
	FIFO_MSG msg1_request;
	FIFO_MSG *msg1_response;
	SESSION_MSG1_RESP * msg1_respbody = NULL;
	size_t  msg1_resp_size;

	msg1_request.header.type = FIFO_DH_REQ_MSG1;
	msg1_request.header.size = 0;

	if ((client_send_receive(&msg1_request, sizeof(FIFO_MSG), &msg1_response, &msg1_resp_size) != 0)
		|| (msg1_response == NULL))
	{
		printf("fail to send and receive message.\n");
		return INVALID_SESSION;
	}

	msg1_respbody = (SESSION_MSG1_RESP *)msg1_response->msgbuf;
	memcpy(dh_msg1, &msg1_respbody->dh_msg1, sizeof(sgx_dh_msg1_t));
	*session_id = msg1_respbody->sessionid;
        free(msg1_response);

	return (ATTESTATION_STATUS)0;

}

/* Function Description: This is OCALL interface for initiator enclave to send ECDH message 2 to responder enclave, and receive ECDH message 3 from responder enclave
 * Parameter Description:
 *      [input] dh_msg2: this is pointer to ECDH message 2 generated by initiator enclave
 *      [input, output]dh_msg3: this is pointer to ECDH message 3, this buffer is allocated in initiator enclave and filled by responder enclave
 *      [input] session_id: this is session id allocated by responder enclave
 * */
ATTESTATION_STATUS exchange_report_ocall(sgx_dh_msg2_t *dh_msg2, sgx_dh_msg3_t *dh_msg3, uint32_t session_id)
{
	FIFO_MSG * msg2 = NULL, * msg3 = NULL;
	FIFO_MSG_HEADER * msg2_header = NULL;
	SESSION_MSG2 *msg2_body = NULL;
	SESSION_MSG3 *msg3_body = NULL;
	size_t msg2size, msg3size;

	msg2size = sizeof(FIFO_MSG_HEADER) + sizeof(SESSION_MSG2);
	msg2 = (FIFO_MSG *)malloc(msg2size);
	if (!msg2)
	{
		return ERROR_OUT_OF_MEMORY;
	}
	memset(msg2, 0, msg2size);

	msg2_header = (FIFO_MSG_HEADER *)msg2;
	msg2_header->type = FIFO_DH_MSG2;
	msg2_header->size = sizeof(SESSION_MSG2);

	msg2_body = (SESSION_MSG2 *)msg2->msgbuf;
	memcpy(&msg2_body->dh_msg2, dh_msg2, sizeof(sgx_dh_msg2_t));
	msg2_body->sessionid = session_id;

	if (client_send_receive(msg2, msg2size, &msg3, &msg3size) != 0)
	{
		free(msg2);
		printf("failed to send and receive message.\n");
		return INVALID_SESSION;
	}

	msg3_body = (SESSION_MSG3 *)msg3->msgbuf;
	memcpy(dh_msg3, &msg3_body->dh_msg3, sizeof(sgx_dh_msg3_t));

	free(msg3);
	free(msg2);

	return (ATTESTATION_STATUS)0;
}

/* Function Description: This is OCALL interface for initiator enclave to send request message(encrypted) to responder enclave, and receive response message from responder enclave
 * Parameter Description:
 *      [input] session_id: this is session id allocated by responder enclave
 *      [input] req_message: this is pointer to request message
 *      [input] req_message_size: this is request message size
 *      [input] max_payload_size: this is maximum payload size in response message
 *      [input, output] this is pointer to response message, the buffer is allocated by initiator enclave and filled by responder enclave
 *      [input] response message size
 * */
ATTESTATION_STATUS send_request_ocall(uint32_t session_id, secure_message_t* req_message, size_t req_message_size, size_t max_payload_size, secure_message_t* resp_message, size_t resp_message_size)
{
	FIFO_MSG *msgreq = NULL, * msgresp= NULL;
	FIFO_MSGBODY_REQ * msgbody;

	size_t reqsize, respsize;

	reqsize = sizeof(FIFO_MSG_HEADER) + sizeof(FIFO_MSGBODY_REQ) + req_message_size;

	msgreq = (FIFO_MSG *)malloc(reqsize);
	if (!msgreq)
	{
		return ERROR_OUT_OF_MEMORY;
	}
	memset(msgreq, 0, reqsize);

	msgreq->header.type = FIFO_DH_MSG_REQ;
	msgreq->header.size = sizeof(FIFO_MSGBODY_REQ) + req_message_size;

	msgbody = (FIFO_MSGBODY_REQ *)msgreq->msgbuf;
	msgbody->max_payload_size = max_payload_size;
	msgbody->size = req_message_size;
	msgbody->session_id = session_id;

	memcpy(msgbody->buf, req_message, req_message_size);

	if (client_send_receive(msgreq, reqsize, &msgresp, &respsize) != 0)
	{
		free(msgreq);
		printf("fail to send and receive message.\n");
		return INVALID_SESSION;
	}

	//TODO copy to output message pointer
	memcpy(resp_message, msgresp->msgbuf, msgresp->header.size < resp_message_size ? msgresp->header.size : resp_message_size);

	free(msgresp);
	free(msgreq);

	return (ATTESTATION_STATUS)0;
}

/* Function Description: this is OCALL interface for initiator enclave to close secure session
 * Parameter Description:
 *      [input] session_id: this is session id allocated by responder enclave
 * */
ATTESTATION_STATUS end_session_ocall(uint32_t session_id)
{
	FIFO_MSG *msgresp = NULL;
	FIFO_MSG *closemsg;
	SESSION_CLOSE_REQ * body;
	size_t reqsize, respsize;

	reqsize = sizeof(FIFO_MSG) + sizeof(SESSION_CLOSE_REQ);
	closemsg = (FIFO_MSG *)malloc(reqsize);
	if (!closemsg)
	{
		return ERROR_OUT_OF_MEMORY;
	}
	memset(closemsg, 0,reqsize);

	closemsg->header.type = FIFO_DH_CLOSE_REQ;
	closemsg->header.size = sizeof(SESSION_CLOSE_REQ);

	body = (SESSION_CLOSE_REQ *)closemsg->msgbuf;
	body->session_id = session_id;

	if (client_send_receive(closemsg, reqsize, &msgresp, &respsize) != 0)
	{
		free(closemsg);
		printf("fail to send and receive message.\n");
		return INVALID_SESSION;
	}

	free(closemsg);
	free(msgresp);

	return (ATTESTATION_STATUS)0;
}
