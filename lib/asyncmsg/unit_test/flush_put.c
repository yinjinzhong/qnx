/*
 * $QNXLicenseC:
 * Copyright 2007, QNX Software Systems. All Rights Reserved.
 * 
 * You must obtain a written license from and pay applicable license fees to QNX 
 * Software Systems before you may reproduce, modify or distribute this software, 
 * or any work that includes all or part of this software.   Free development 
 * licenses are available for evaluation and non-commercial purposes.  For more 
 * information visit http://licensing.qnx.com or email licensing@qnx.com.
 *  
 * This file may contain contributions from others.  Please review this entire 
 * file for other proprietary rights or license notices, as well as the QNX 
 * Development Suite License Guide at http://licensing.qnx.com/license-guide/ 
 * for other information.
 * $
 */




#include <stdio.h>
#include <errno.h>
#include <process.h>
#include <unistd.h>
#include <sys/asyncmsg.h>

int num_callback_run = 0;
int chid;

int callback(int err, void *cmsg, unsigned handle)
{
	num_callback_run++;
	printf("Callback: err = %d, msg = %p, handle = %d\n",
		   err, cmsg, handle);
	return 0;
}

void * thread_for_get(void *arg)
{
	int pchid = (int)arg;
	struct _pulse pulse;
	struct _asyncmsg_get_header *agh, *agh1;

	/* waiting for the event */
	if (MsgReceivePulse(pchid, &pulse, sizeof(pulse), NULL) == -1) {
		perror("MsgReceivePulse");
		return NULL;
	}

	if ((agh = asyncmsg_get(chid)) == NULL)	{
		perror("get");
		return NULL;
	}
	
	printf("Got message(s): \n\n");
	while (agh1 = agh) {
		agh = agh1->next;
		printf("from process: %d (%d)\n", agh1->info.pid, getpid());
		printf("msglen: %d\n", agh1->info.msglen);
		printf("srclen: %d\n", agh1->info.srcmsglen);
		printf("err: %d\n", agh1->err);
		printf("parts: %d\n", agh1->parts);
		printf("msg: %s\n\n", (char *)agh1->iov->iov_base);
		asyncmsg_free(agh1);
	}
	
	return 0;
}

/*
 * No trigger for put, but we force an flush(), see if all
 * messages could be pushed out.
 */
int main()
{
	int coid, pchid, i;
	struct sigevent gev;
	struct _asyncmsg_connection_attr aca;
	char msg[4][80];
	
	/* prepare the event */
	if ((pchid = ChannelCreate(0)) == -1) {
		perror("ChannelCreate");
		return -1;
	}
	
	if ((errno = pthread_create(0, 0, thread_for_get, (void *)pchid)) != EOK)
	{
		perror("pthread_create");
		return -1;
	}
	
	if ((gev.sigev_coid = ConnectAttach(0, 0, pchid, _NTO_SIDE_CHANNEL, 0)) == -1)
	{
		perror("ConnectAttach");
		return -1;
	}
	gev.sigev_notify = SIGEV_PULSE;
	gev.sigev_priority = SIGEV_PULSE_PRIO_INHERIT;

	/* async channel */
	if ((chid = asyncmsg_channel_create(_NTO_CHF_SENDER_LEN, 0666, 2048, 5, &gev, NULL)) == -1) {
		perror("channel_create");
		return -1;
	}
	
	memset(&aca, 0, sizeof(aca));
	aca.buffer_size = 2048;
	aca.max_num_buffer = 5;
	aca.trigger_num_msg = 0;
	aca.call_back = callback;
	
	if ((coid = asyncmsg_connect_attach(0, 0, chid, 0, 0, &aca)) == -1) {
		perror("connect_attach");
		return -1;
	}

	/* put up 4 messages */
	for (i = 0; i < 2; i++) {
		sprintf(msg[i], "Async Message Passing (msgid %d)\n", i);
		if ((asyncmsg_put(coid, msg[i], strlen(msg[i]) + 1, 1234, callback)) == -1) {
			perror("put");
			return -1;
		}
	}
	
	asyncmsg_flush(coid, 0);
	
	/* give the put callback a chance to run */
	while (num_callback_run < 2) 
	  delay(500);
	
	if (asyncmsg_connect_detach(coid) == -1) {
		perror("connect_detach");
		return -1;
	}
	
	if (asyncmsg_channel_destroy(chid) == -1) {
		perror("channel_detach");
		return -1;
	}
	
	return 0;
}

__SRCVERSION("flush_put.c $Rev: 153052 $");
