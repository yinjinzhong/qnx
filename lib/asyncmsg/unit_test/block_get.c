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

char *msg = "AsyncMsg Passing";
int chid = -1, coid;
int getisready = 0;

void *get_thread(void *arg)
{
	struct _asyncmsg_get_header *agh, *agh1;

	/* async channel */
	if ((chid = asyncmsg_channel_create(_NTO_CHF_SENDER_LEN, 0666, 2048, 5, NULL, NULL)) == -1) {
		perror("channel_create");
		return -1;
	}

	if ((agh = asyncmsg_get(chid)) == NULL)	{
		perror("get");
		return NULL;
	}
	
	printf("Got message(s): \n\n");
	while (agh1 = agh) {
		agh = agh1->next;
		printf("from process: %d (%d)\n", agh1->info.pid, getpid());
		printf("msglen: %d (%d)\n", agh1->info.msglen, strlen(msg) + 1);
		printf("srclen: %d\n", agh1->info.srcmsglen);
		printf("err: %d\n", agh1->err);
		printf("parts: %d\n", agh1->parts);
		printf("msg: %s\n\n", (char *)agh1->iov->iov_base);
		asyncmsg_free(agh1);
	}

	if (asyncmsg_channel_destroy(chid) == -1) {
		perror("channel_detach");
		return NULL;
	}

	return NULL;
}

/* The asyncmsg_get() will be blocked, and let's see if a put
 * could wake it up
 */
int main(int argc, char **argv)
{
	struct _asyncmsg_connection_attr aca;
	int pid = 0;

	if (argc > 1) {
		if ((pid = fork()) == -1) {
			perror("fork");
			return -1;
		}
		if (!pid) {
			get_thread(0);
			return 0;
		} else {
			/* we lied, the parent don't know the chid in client, 
			 * we assume it's 1 :)
			 */
			chid = 1;
		}
	} else {
		if ((errno = pthread_create(0, 0, get_thread, NULL)) != EOK) {
			perror("pthread_create");
			return -1;
		}
	}
								
	/* Let the get thread block */
	sleep(1);

	if (chid == -1) {
		return -1;
	}
	
	memset(&aca, 0, sizeof(aca));
	aca.buffer_size = 2048;
	aca.max_num_buffer = 5;
	aca.trigger_num_msg = 1;
	
	if ((coid = asyncmsg_connect_attach(0, pid, chid, 0, 0, &aca)) == -1) {
		perror("connect_attach");
		return -1;
	}
	
	if ((asyncmsg_put(coid, msg, strlen(msg) + 1, 0, 0)) == -1) {
		perror("put");
		return -1;
	}

	sleep(1);
	
	if (asyncmsg_connect_detach(coid) == -1) {
		perror("connect_detach");
		return NULL;
	}
	
	return 0;
}

__SRCVERSION("block_get.c $Rev: 153052 $");
