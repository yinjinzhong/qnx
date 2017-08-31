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



#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>
#include <libgen.h>
#include <sys/netmgr.h>
#include <sched.h>
#include <pthread.h>
#include <sys/sched_aps.h>

#include <sys/pathmsg.h>
#include <sys/iomsg.h>
#include <sys/dcmd_ip.h>

#include "pidin.h"
#include <inttypes.h>


static struct {
	procfs_debuginfo		info;
	char					buff[_POSIX_PATH_MAX];
}						name;
static procfs_status	status;
static procfs_info		info;
extern char	   *node;
extern unsigned node_nd;
extern int  find_netdir(char *nodename, char *buf, size_t bufsize, int flag);

int meminfo(struct shared_info *i, int fd, unsigned int stack_base, int display);

static char *
chk_strdup(const char *str)
{
	char	*new;

	new = strdup(str);
	if(!new) {
		error_exit(1, "\nno memory for string\n");
	}
	return new;
}

int 
fwoutput(FILE * fp, int len, const char *str)
{
	int				i = 0;

	if (str && len > 0)
		while (i < len && str[i])
			fputc(str[i++], fp);
	return i;
}

int 
format_data_string(FILE * fp, struct format *fmt, const char *str)
{
	int				len = strlen(str);
	int				n = 0;

	if (fmt->flags & PERSERVE_RIGHT && len > fmt->width)
		str += len - fmt->width;

	if (len > fmt->width) {
		fwoutput(fp, fmt->width, str);
		return 0;
	}

	if (fmt->flags & DATA_CENTERED)
		fwoutput(fp, n = (fmt->width - len) / 2, spaces);
	fwoutput(fp, fmt->width, str);
	if (fmt->flags & DATA_CENTERED)
		fwoutput(fp, fmt->width - len - n, spaces);
	else
		fwoutput(fp, fmt->width - len, spaces);
	return 0;
}

int 
format_title_string(FILE * fp, struct format *fmt, const char *str)
{
	int				len = strlen(str);
	int				n = 0;
	if (!(fmt->flags & TITLE_LEFT_JUSTIFIED)) {
		if (fmt->flags & TITLE_RIGHT_JUSTIFIED)
			fwoutput(fp, fmt->width - len, spaces);
		else
			fwoutput(fp, n = (fmt->width - len) / 2, spaces);
	}

	fwoutput(fp, fmt->width, str);

	if (fmt->flags & TITLE_LEFT_JUSTIFIED)
		fwoutput(fp, fmt->width - len, spaces);
	else if (!(fmt->flags & TITLE_RIGHT_JUSTIFIED))
		fwoutput(fp, fmt->width - len - n, spaces);
	return 0;
}

int 
format_data_int(FILE * fp, struct format *fmt, int d)
{
	char			buffer[20];
	int				n;
	n = sprintf(buffer, "%d", d);
	if (fmt->flags & DATA_FILL)
		fwoutput(fp, fmt->width - n, zeros);
	else if (!(fmt->flags & DATA_LEFT_JUSTIFIED))
		fwoutput(fp, fmt->width - n, spaces);
	fprintf(fp, "%s", buffer);
	if (fmt->flags & DATA_LEFT_JUSTIFIED)
		fwoutput(fp, fmt->width - n, spaces);
	return 0;
}

uint64_t
normalize_data_size(uint64_t size, char **symp) {
	static char	*syms[] = { "", "K", "M", "G", "T", NULL };
	char		**sym;

	sym = syms;
	while((size > 8192) && (*sym != NULL)) {
 		size /= 1024;
		++sym;
	}
	*symp = *sym;
 	return size;
}

int 
format_data_size(FILE * fp, struct format *fmt, int size)
{
	char *sym;

	size = normalize_data_size(size, &sym);
	if (sym != NULL && *sym != '\0') {
		fmt->width--;
	}
	format_data_int(fp, fmt, size);
	if (sym != NULL && *sym != '\0') {
		fputc(*sym, fp);
		fmt->width++;
	}
	return 0;
}

int 
format_data_int_hex(FILE * fp, struct format *fmt, int d)
{
	char			buffer[20];
	int				n;
	n = sprintf(buffer, "%*x", fmt->width, d);
	if (fmt->flags & DATA_FILL)
		fwoutput(fp, fmt->width - n, zeros);
	else if (!(fmt->flags & DATA_LEFT_JUSTIFIED))
		fwoutput(fp, fmt->width - n, spaces);
	fprintf(fp, "%s", buffer);
	if (fmt->flags & DATA_LEFT_JUSTIFIED)
		fwoutput(fp, fmt->width - n, spaces);
	return 0;
}

int 
format_data(FILE * fp, struct format *fmt, const char *f,...)
{
	char			buffer[80];
	int				n;
	va_list			args;

	va_start(args, f);
	n = vsnprintf(buffer, sizeof buffer, f, args);
	va_end(args);
	n = fwoutput(fp, fmt->width, buffer);
	fwoutput(fp, fmt->width - n, spaces);
	return 0;
}

int 
format_data_clock(FILE * fp, struct format *fmt, uint64_t nsec)
{
	int					n;
	struct timespec		t;
	char				buffer[80];
	time_t				now;

	nsec2timespec(&t, nsec);
	time(&now);
	if(now - t.tv_sec > (365 / 2) * 24 * 60 * 60) {
		strftime(buffer, sizeof buffer, "%b %d  %Y", localtime(&t.tv_sec));
	} else {
		strftime(buffer, sizeof buffer, "%b %d %H:%M", localtime(&t.tv_sec));
	}
	n = fwoutput(fp, fmt->width, buffer);
	fwoutput(fp, fmt->width - n, spaces);
	return 0;
}

int 
format_data_time(FILE * fp, struct format *fmt, uint64_t nsec)
{
	unsigned					sec = nsec / 1000000000;

	if(sec >= 60 * 60 * 24 * 100) {
		return format_data(fp, fmt, "%ud", sec / (60 * 60 * 24));
	} else if(sec >= 60 * 60 * 24) {
		unsigned		days = sec / (60 * 60 * 24);

		return format_data(fp, fmt, "%2ud%02uh", days, (sec - days * (60 * 60 * 24)) / (60 * 60));
	} else if(sec >= 60 * 60) {
		unsigned		hours = sec / (60 * 60);

		return format_data(fp, fmt, "%2uh%02um", hours, (sec - hours * (60 * 60)) / 60);
	} else if(sec >= 100) {
		unsigned		mins = sec / 60;

		return format_data(fp, fmt, "%2um%02us", mins, sec - mins * 60);
	} else {
		return format_data(fp, fmt, "%2u.%03u", sec, (unsigned)(nsec - (uint64_t)sec * 1000000000) / 1000000);
	}
}


int 
fill_status(int expectwarn, struct shared_info *i, int *tid, int fd)
{
	status.tid = *tid;
	if (devctl(fd, DCMD_PROC_TIDSTATUS, &status, sizeof status, 0) != EOK) {
		warning_exit(!expectwarn, expectwarn, "\ncouldn't fill_status()\n");
		return 1;
	} else
		return i->status = &status, *tid = status.tid, 0;
}

int 
fill_name(struct shared_info *i, int fd)
{
	if (devctl(fd, DCMD_PROC_MAPDEBUG_BASE, &name, sizeof name, 0) != EOK) {
		name.info.vaddr = 0;
		strcpy(name.info.path, na);	/*
						 * should be available but proc
						 * creates the pid before the
						 * process is fully realized 
						 */
		if (i->info || !fill_info(i, fd)) {
			if (i->info->pid == SYSMGR_PID) {
			        strcpy(name.info.path, "procnto");
			} else if (i->info->flags & _NTO_PF_LOADING) {
				strcpy(name.info.path, "(Loading)");
			}
		}
	}
	i->name = &name.info;
	return 0;
}

int 
fill_mem(struct shared_info *i, int fd)
{
	int				num;
	static char		buffer[4096];
	if (i->mem)
		free(i->mem);
	if (devctl(fd, DCMD_PROC_PAGEDATA, buffer, sizeof buffer, &num) != EOK) {
		i->mem = 0;
		warning_exit(1, 0, "\ncouldn't fill_mem()\n");
		return 1;
	} else {
		i->mem = malloc(num * sizeof *i->mem);
		if (!i->mem)
			return 1;
		memcpy(i->mem, buffer, num * sizeof *i->mem);
		i->num_mem = num;
		return 0;
	}
}

int 
fill_info(struct shared_info *i, int fd)
{
	if (devctl(fd, DCMD_PROC_INFO, &info, sizeof info, 0) != EOK) {
		warning_exit(1, 0, "\ncouldn't fill_info\n");
		return 1;
	} else
		return i->info = &info, 0;
}

int
fill_channels(struct shared_info *i, int fd)
{
int n;

	if (devctl(fd, DCMD_PROC_CHANNELS, NULL, 0, &n) != EOK) {
		warning_exit(1, 0, "\ncouldn't fill_channels 2\n");
		return -1;
	}
	if ( (i->channels = (procfs_channel *)malloc( sizeof(procfs_channel) * n )) == NULL ) {
		warning_exit(1, 0, "\ncouldn't fill_channels 3\n");
		return -1;
	}
	i->num_channels = n;
	if (devctl(fd, DCMD_PROC_CHANNELS, i->channels, sizeof(procfs_channel) * n, &n) != EOK) {
		warning_exit(1, 0, "\ncouldn't fill_channels 4\n");
		free(i->channels);
		i->channels = 0;
	}
	i->num_channels = min(i->num_channels, n);
	return 0;
}

int
fill_timers(struct shared_info *i, int fd)
{
int n;

	if (devctl(fd, DCMD_PROC_TIMERS, NULL, 0, &n) != EOK) {
		warning_exit(1, 0, "\ncouldn't fill_timers 2\n");
	}
	if ( (i->timers = (procfs_timer *)malloc( sizeof(procfs_timer) * n )) == NULL ) {
		warning_exit(1, 0, "\ncouldn't fill_timers 3\n");
		return -1;
	}
	i->num_timers = n;
	if (devctl(fd, DCMD_PROC_TIMERS, i->timers, sizeof(procfs_timer) * n, &n) != EOK) {
		warning_exit(1, 0, "\ncouldn't fill_timers 4\n");
		free(i->timers);
		i->timers = 0;
	}
	i->num_timers = min(i->num_timers, n);
	return 0;
}

#define BUFINCR 10
int
fill_coids(struct shared_info *i, int pid) 
{
	struct coid_info	*fdinfobuffer;
	int		   	bcount, bmax;

	struct _server_info	sinfo;
	int			fd, fd2;
	io_dup_t		msg;
	struct _fdinfo		fdinfo;
	char 			path[512];
	int			oldfd;

	fdinfobuffer = NULL;
	bcount = bmax = 0;

	for(fd = 0 ; 
            (fd = ConnectServerInfo(pid, oldfd = fd, &sinfo)) >= 0 || ((oldfd & _NTO_SIDE_CHANNEL) == 0) ; ++fd) {
		int binsert;
		
		// Older versions of proc did not start at the first side channel
		if(fd < 0 || (((fd ^ oldfd) & _NTO_SIDE_CHANNEL) && fd != _NTO_SIDE_CHANNEL)) {
			oldfd = _NTO_SIDE_CHANNEL;
			fd = _NTO_SIDE_CHANNEL - 1;
			continue;
		}

		if(bcount >= bmax) {
			struct coid_info *newfdbuffer;
			
			newfdbuffer = realloc(fdinfobuffer, (bmax + BUFINCR) * sizeof(*fdinfobuffer));
			if(newfdbuffer == NULL) {
				i->coids = fdinfobuffer;
				i->num_coids = bcount;
				return 0;
			}
			bmax = bmax + BUFINCR;
			fdinfobuffer = newfdbuffer;	
		}
		
		binsert = bcount++;

		memset(&fdinfobuffer[binsert], 0, sizeof(*fdinfobuffer));
		fdinfobuffer[binsert].fd = fd;
		fdinfobuffer[binsert].pid = sinfo.pid;
		fdinfobuffer[binsert].ioflag = -1;
		
		// If a connection points to itself, it probably isn't a resmgr connection
		if(sinfo.pid == pid) {
			continue;
		}

		if((fd2 = ConnectAttach(sinfo.nd, sinfo.pid, sinfo.chid, 0, _NTO_COF_CLOEXEC)) == -1) {
			continue;
		}

		msg.i.type = _IO_DUP;
		msg.i.combine_len = sizeof msg;
		msg.i.info.nd = netmgr_remote_nd(sinfo.nd, ND_LOCAL_NODE);
		msg.i.info.pid = pid;
		msg.i.info.chid = sinfo.chid;
		msg.i.info.scoid = sinfo.scoid;
		msg.i.info.coid = fd;

		//We can't affort to block for long, .5 a second is all we will tolerate.
		//At the very least, when this happens we should still be able to log the
		//entry, just not with any name information.
		{
		struct sigevent event;
		uint64_t	nsec;

		memset(&event, 0, sizeof(event));
		event.sigev_notify = SIGEV_UNBLOCK;
		nsec = 1 * 500000000L;

		TimerTimeout( CLOCK_REALTIME, _NTO_TIMEOUT_SEND | _NTO_TIMEOUT_REPLY, &event, &nsec, NULL );
		if(MsgSendnc(fd2, &msg.i, sizeof msg.i, 0, 0) == -1) {
			ConnectDetach_r(fd2);
			continue;
		}
		}

		if(iofdinfo(fd2, _FDINFO_FLAG_LOCALPATH, &fdinfo, path, sizeof(path)) == -1) {
			close(fd2);
			continue;
		}

		if(S_ISSOCK(fdinfo.mode)) {
			/*
			 * The above gives a path which is really only meaningful
			 * for a bound AF_LOCAL socket.  The following will give
			 * some pretty status info.
			 *
			 * If it fails, just use what we already have.
			 */
			devctl(fd2, DCMD_IP_FDINFO, path, sizeof(path), 0);
		}

		close(fd2);

		if(sinfo.pid == PATHMGR_PID && fdinfo.mode == 0) {
			fdinfobuffer[binsert].ioflag = fdinfo.flags;
			fdinfobuffer[binsert].offset = fdinfo.offset;
			fdinfobuffer[binsert].size = -1;
		} else {
			fdinfobuffer[binsert].ioflag = fdinfo.ioflag;
			fdinfobuffer[binsert].offset = fdinfo.offset;
			fdinfobuffer[binsert].size = fdinfo.size;
		}

		fdinfobuffer[binsert].name = strdup(path);
	}
	
	i->coids = fdinfobuffer;
	i->num_coids = bcount;
	return 0;
}


int fill_gprs(struct shared_info *i, int fd, int tid) {
	if ((i->gprs = malloc(sizeof(procfs_greg))) == NULL ) {
		warning_exit(1,0, "\ncouldn't allocate reg context\n");
		return -1;
	}

	if (devctl(fd, DCMD_PROC_CURTHREAD, &tid, sizeof tid, 0) != EOK) {
		free(i->gprs);
		return -1;
	}
	if (devctl(fd, DCMD_PROC_GETGREG, i->gprs, sizeof(procfs_greg), &(i->gprs_size)) != EOK) {
		free(i->gprs);
		return -1;
	}

	return 0;
}

int
fill_irqs(struct shared_info *i, int fd)
{
int n;

	if (devctl(fd, DCMD_PROC_IRQS, NULL, 0, &n) != EOK) {
		warning_exit(1, 0, "\ncouldn't fill_irqs 2\n");
	}
	if ( (i->irqs = (procfs_irq *)malloc( sizeof(procfs_irq) * n )) == NULL ) {
		warning_exit(1, 0, "\ncouldn't fill_irqs 3\n");
		return -1;
	}
	i->num_irqs = n;
	if (devctl(fd, DCMD_PROC_IRQS, i->irqs, sizeof(procfs_irq) * n, &n) != EOK) {
		warning_exit(1, 0, "\ncouldn't fill_irqs 4\n");
		free(i->irqs);
		i->irqs = 0;
	}
	i->num_irqs = min(i->num_irqs, n);
	return 0;
}

int 
fill_tls(struct shared_info *i, int tid, int fd)
{
	if (!i->status && fill_status(0, i, &tid, fd)) {
		warning_exit(1, 0, "couldn't fill_tls()\n");
		return 1;
	}
	if (!(i->tls = malloc(sizeof *i->tls))) {
		warning_exit(1, 0, "\nno memory to fill_tls()\n");
		return 1;
	}
	if (lseek64(fd, i->status->tls, SEEK_SET) == -1) {
		warning_exit(1, 0, "couldn't fill_tls(): %s\n", strerror(errno));
		free(i->tls);
		i->tls = 0;
		return 1;
	}
	if (read(fd, i->tls, sizeof *i->tls) < sizeof *i->tls) {
		warning_exit(1, 0, "\ncouldn't fill_tls: %s\n", strerror(errno));
		free(i->tls);
		i->tls = 0;
		return 1;
	}
	return 0;
}

static char *print_sigevent( struct sigevent *info )
{
static char *notify_names[] = {
    "NONE",
    "SIGNAL",
    "SIGNAL_CODE",
    "SIGNAL_THREAD",
    "PULSE",
    "UNBLOCK",
    "INTR",
	"THREAD"
};
static char buf[1024];
char *p = buf;

	p += sprintf( p, "%s ", notify_names[info->sigev_notify] );
	switch ( info->sigev_notify ) {
	case SIGEV_SIGNAL:
		p += sprintf( p, "%u", (int)info->sigev_signo );
		break;
	case SIGEV_SIGNAL_CODE:
	case SIGEV_SIGNAL_THREAD:
		p += sprintf( p, "%lu %#hx:%#lx", (long)info->sigev_signo, (short)info->sigev_code, (long)info->sigev_value.sival_int );
		break;
	case SIGEV_PULSE:
		p += sprintf( p, "%#lx:%ld %#hx:%#lx", (long)info->sigev_coid, (long)info->sigev_priority, (int)info->sigev_code, (long)info->sigev_value.sival_int );
		break;
	case SIGEV_UNBLOCK:
		break;
	case SIGEV_INTR:
		break;
	case SIGEV_THREAD:
		p += sprintf( p, "%hx:%lx", (short)info->sigev_code, (long)info->sigev_value.sival_int );
		break;
	default:
		break;
	}
	return buf;
	return NULL;
}

/* Show a list of channels for the process */
int 
Channels(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *info)
{
int i;
	if (!info->channels && fill_channels(info, fd))
		return 1;
	if ( *tid != 1 )
		return 0;

	for ( i = 0; i < info->num_channels; i++ ) {
		fprintf( fp, "\n\t\t" );
		fprintf( fp, "%08x: flags %08x PulseQ %d SendQ %d ReceiveQ %d ReplyQ %d ",
				info->channels[i].chid,
			  info->channels[i].flags,
			  info->channels[i].pulse_queue_depth,
			  info->channels[i].send_queue_depth,
			  info->channels[i].receive_queue_depth,
			  info->channels[i].reply_queue_depth
			  );
	}

	return 0;
}

/* Show a list of interrupts for the process */
int 
Interrupt(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *info)
{
int i;
	if (!info->irqs && fill_irqs(info, fd))
		return 1;

	for ( i = 0; i < info->num_irqs; i++ ) {
		if ( info->irqs[i].tid != *tid )
			continue;
		fprintf( fp, "\n\t\t" );
		fprintf( fp, "%d ", info->irqs[i].id );
		fprintf( fp, "%#16x ", info->irqs[i].vector );
		fprintf( fp, "%4d ", info->irqs[i].mask_count );
		fprintf( fp, "%c%c%c ", 
			(info->irqs[i].flags & _NTO_INTR_FLAGS_TRK_MSK) ? 'T':'-',
			(info->irqs[i].flags & _NTO_INTR_FLAGS_PROCESS) ? 'P':'-',
			(info->irqs[i].flags & _NTO_INTR_FLAGS_END) ? 'E':'-'
		);
		if ( info->irqs[i].handler )
			fprintf( fp, "@0x%p:0x%p ", info->irqs[i].handler, info->irqs[i].area );
		else
			fprintf( fp, "=%s", print_sigevent(&info->irqs[i].event) );
	}

	return 0;
}

#define REGNAME(_idx)   ((_idx) > nregnames-1) ? "--" : regnames[(_idx)]
#ifdef __MIPS__

#ifdef __BIGENDIAN__
#define REG_LOW_WORD 1
#else
#define REG_LOW_WORD 0
#endif

#define GETREG(_ptr,_idx)   ((uint32_t*)(_ptr))[(((_idx)*2)+REG_LOW_WORD)]
#define NREGS(_size)		((_size)/(sizeof(uint32_t)*2))
#else
#define GETREG(_ptr,_idx)   ((uint32_t*)(_ptr))[(_idx)]
#define NREGS(_size)		((_size)/sizeof(uint32_t))
#endif

int
Registers(FILE* fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *info)
{
	int i;
	int nregs;
	
	if (fill_gprs(info, fd, *tid))
		return 1;

	nregs = NREGS(info->gprs_size);
	fprintf(fp,"\n");
	for (i =0 ; i < nregs ; i++) {
		fprintf(fp, "%5s:%08x ", 
			REGNAME(i), GETREG(info->gprs, i));
		if (!((i+1)%4)) fprintf(fp, "\n");
	}
	fprintf(fp,"\n");
	
	return 0;
}

/* Show a list of timers for the process */
int 
Timers(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *info)
{
int i, first;
static char *clockid_names[] = {
	"REAL",
	"SOFT",
	"MONO",
	"PROC",
	"THRD"
};
	if (!info->timers && fill_timers(info, fd))
		return 1;

	first = 0;
	for ( i = 0; i < info->num_timers; i++ ) {
		if ( info->timers[i].info.tid ) {
			if ((info->timers[i].info.tid+1) != *tid )
				continue;
		} else if( *tid != 1 ) {
			continue;
		}

		if(first++ != 0) {
			fprintf(fp, "\n");
		}
		fprintf( fp, "\t%6d %4d %4d %s %c%c%c  %llu/%llu %s",
			info->timers[i].id, info->timers[i].info.tid, info->timers[i].info.overruns, 
			clockid_names[info->timers[i].info.clockid],
			(info->timers[i].info.flags & _NTO_TI_EXPIRED) ? 'X':'-',
			(info->timers[i].info.flags & _NTO_TI_ABSOLUTE) ? 'A':'-',
			(info->timers[i].info.flags & _NTO_TI_ACTIVE) ? 'a':'-',
			info->timers[i].info.otime.nsec/1000,
			info->timers[i].info.otime.interval_nsec/1000,
			print_sigevent(&info->timers[i].info.event)
			);
	}
	if(first++ != 0) {
		fprintf(fp, "\n");
	}

	return 0;
}

/* Show a list of connection ids for the process */
int 
Coids(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *info)
{
int i, smallfd;
char type;
char flagbuf[3];

	if (!info->coids && fill_coids(info, pid))
		return 0;

	//Format: fd:pid: flags: offset: name
	for ( i = 0; i < info->num_coids; i++ ) {
		smallfd = info->coids[i].fd & ~_NTO_SIDE_CHANNEL;
		if(info->coids[i].fd & _NTO_SIDE_CHANNEL) {
			type = 's';
		} else {
			type = ' ';
		}
		
		if(i != 0) {
			fprintf(fp, "\n");
		}
		fprintf(fp, "\t%4d%c %6d ", smallfd, type, info->coids[i].pid);

		//Differentiate resource manager mount points from file opens
		if(info->coids[i].ioflag == -1) {
			//Don't bother with the display of ones with no extra info
		} else if(info->coids[i].pid == PATHMGR_PID && info->coids[i].size == -1) { 
			//In the future, we may want to expose other flags
			flagbuf[0] = 'M';
			flagbuf[1] = 'P';
			flagbuf[2] = '\0';
			fprintf(fp, "%s %8d ", flagbuf, 0);
		} else {
			flagbuf[0] = (info->coids[i].ioflag & _IO_FLAG_RD) ? 'r' : '-';
			flagbuf[1] = (info->coids[i].ioflag & _IO_FLAG_WR) ? 'w' : '-';
			flagbuf[2] = '\0';
			fprintf(fp, "%s %8d ", flagbuf, info->coids[i].offset);
		}

		if(info->coids[i].name) {
			fprintf( fp, "%s", info->coids[i].name);
		}
	}

	return 0;
}



int 
Arguments(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *info)
{
	char			p[64];
	char		   *args[32];
	int				needspace = 0;
	int				argc, valid, i;

	if (!info->info && fill_info(info, fd))
		return 1;

	if (!info->info->initial_stack) {
		if (!info->name && fill_name(info, fd))
			return 1;

		fprintf(fp, "%s", info->name->path);
		return 0;
	}

	if (lseek64(fd, info->info->initial_stack, SEEK_SET) == -1) {
		warning_exit(1, 0, "\ncouldn't seek to stack: %s\n", strerror(errno));
		return 1;
	}
	if ((valid = read(fd, p, 4)) < 4) {
		warning_exit(1, 0, "\ncouldn't read stack: %s\n", strerror(errno));
		return 1;
	}
	argc = *(int *) p;
	while (argc) {
		int				num;

		num = min(argc, 32);
		if (read(fd, args, num * sizeof args[0]) < num * sizeof args[0]) {
			warning_exit(1, 0, "\ncouldn't read stack: %s\n", strerror(errno));
			return 1;
		}
		argc -= num;

		for (i = 0; i < num; i++) {
			/* don't continue past a null argv[i] pointer */
			if ( args[i] == 0 ) {
				return 0;
			}
			if (needspace)
				fputc(' ', stdout);
			if (lseek64(fd, (off64_t)(uintptr_t)args[i], SEEK_SET) == -1) {
				warning_exit(1, 0, "\ncouln't read arg: %s\n", strerror(errno));
				return 1;
			}
			do {
				if ((valid = read(fd, p, sizeof p - 1)) == -1) {
					warning_exit(1, 0, "\ncouldn't read arg: %s\n", strerror(errno));
					return 1;
				}
				p[valid] = 0;
				printf("%s", p);
			} while (valid != 0 && !memchr(p, 0, valid));
			needspace = 1;
		}
	}
	return 0;
}

/* FIXME: This shouldn't be done this way as it implies 
   knowledge of internal structure.  Neither peterv nor 
   dtdodge has any other answer. */
#include <dirent.h>
#define PID_MASK		0xfff
#define PINDEX(pid)		((pid) & PID_MASK)
#include <pthread.h>
int
format_mutex(const sync_t *mu, pid_t *pid, int *tid, int *count) {
	int pindex, retval;
	pid_t mypid;

	if (count) {
		*count = mu->__count & _NTO_SYNC_COUNTMASK;
	}
	if (tid) {
		*tid = mu->__owner&0xffff;
	}
	mypid = getpid();
	*pid = pindex = ((mu->__owner&0x0fff0000)>>16);
	if (PINDEX(mypid) == pindex) {
		*pid = mypid; /* shortcut to local case */
		return 0;
	} else {
		DIR *dir;
		struct dirent *dirent;
		if (!(dir = opendir("/proc"))) {
			return -1;
		}
		retval = -1;
		while (dirent = readdir(dir)) {
			int p;
			p = strtoul(dirent->d_name, 0, 0);
			if (PINDEX(p) == pindex) {
				*pid = p;
				retval = 0;
			}
		}
		closedir(dir);
	}
	return errno = ESRCH, retval;
}

int 
WhereBlocked(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *info)
{
	char			buffer[200];

	assert(info);
	assert(fp);
	assert(fmt);

	if (!info->status && fill_status(0, info, tid, fd))
		return 1;

	switch (info->status->state) {
	case STATE_DEAD:
	case STATE_READY:
	case STATE_RUNNING:
	case STATE_STOPPED:
	case STATE_SIGSUSPEND:
	case STATE_SIGWAITINFO:
	case STATE_INTR:
	case STATE_NANOSLEEP:
	case STATE_WAITCTX:
	default:
		format_data_string(fp, fmt, spaces);
		break;

	case STATE_WAITTHREAD:
	case STATE_JOIN:
		format_data_int(fp, fmt, info->status->blocked.join.tid);
		break;

	case STATE_SEM:
		format_data(fp, fmt, "%x", info->status->blocked.sync.id);
		break;

	case STATE_STACK:
		format_data(fp, fmt, "%x", info->status->blocked.stack.size);
		break;

	case STATE_MUTEX:
		{
			/*
			 * sync field contains the mutex itself, or the
			 * mutex used in conjunction with the condvar to wait
			 */
			sync_t			mu;
			int				sz;

			if (lseek64(fd, (off64_t)(uintptr_t)info->status->blocked.sync.sync, SEEK_SET) == -1 ||
				(sz = read(fd, &mu, sizeof mu)) < 0 ||
				sz < sizeof mu)
				format_data(fp, fmt, "%x", info->status->blocked.sync.id);
			else {
				pid_t pid;
				int tid, count;
				format_mutex(&mu, &pid, &tid, &count);
 				format_data(fp, fmt, "%d-%02d #%d", pid, tid, count);
 			}
		}
		break;

	case STATE_CONDVAR:
		format_data(fp, fmt, "%x", info->status->blocked.sync.sync);
		break;

	case STATE_SEND:
	case STATE_REPLY:
		sprintf(buffer, "%d", info->status->blocked.connect.pid);
		if (ND_NODE_CMP(info->status->blocked.connect.nd, ND_LOCAL_NODE)) {
			int len = strlen(buffer);
			char netbuf[50];
			DIR           *dp;
			struct dirent *dent = NULL;

			if (find_netdir(node, netbuf, sizeof netbuf, 1) != -1) {
				buffer[len] = '@';

				if ((dp = opendir(netbuf)) != NULL) {
					while (dent = readdir(dp)) {
						if ((int)(dent->d_ino & ND_NODE_MASK) == (info->status->blocked.connect.nd & ND_NODE_MASK))
						  break;
					}
					closedir(dp);
				}
			}

			if (dent) {
				strncpy(&buffer[len + 1], dent->d_name, 200 - len - 1);
			} else {
				sprintf(&buffer[len + 1], "%d", (info->status->blocked.connect.nd & ND_NODE_MASK));
			}
		}
		format_data_string(fp, fmt, buffer);
		break;

	case STATE_RECEIVE:
		sprintf(buffer, "%d", info->status->blocked.channel.chid);
		format_data_string(fp, fmt, buffer);
		break;
	case STATE_WAITPAGE:
		format_data(fp, fmt, "%x", info->status->blocked.waitpage.vaddr);
		break;
	}
	return 0;
}

int 
Environment(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *info)
{
	char			p[64];
	char		   *args[32];
	int				needspace = 0;
	int				argc, valid, i;
	off64_t			pos;

	if (!info->info && fill_info(info, fd))
		return 1;

	if (!info->info->initial_stack)
		return 0;

	if (lseek64(fd, info->info->initial_stack, SEEK_SET) == -1) {
		warning_exit(1, 0, "\ncouldn't seek to stack: %s\n", strerror(errno));
		return 1;
	}
	if ((valid = read(fd, p, 4)) < 4) {
		warning_exit(1, 0, "\ncouldn't read stack: %s\n", strerror(errno));
		return 1;
	}
	argc = *(int *) p;
	while (argc) {
		int				num;

		num = min(argc, 32);
		if (read(fd, args, num * sizeof args[0]) < 0) {
			warning_exit(1, 0, "\ncouldn't read stack: %s\n", strerror(errno));
			return 1;
		}
		argc -= num;
	}
	if (read(fd, args, sizeof args[0]) < sizeof args[0]) {
		warning_exit(1, 0, "\ncouldn't read stack: %s\n", strerror(errno));
		return 1;
	}
	pos = tell64(fd);
	do {
		if (lseek64(fd, pos, SEEK_SET) == -1) {
			warning_exit(1, 0, "\ncouldn't read environment: %s\n", strerror(errno));
			return 1;
		}
		if (read(fd, args, sizeof args) < 0) {
			warning_exit(1, 0, "\ncouldn't read stack: %s\n", strerror(errno));
			return 1;
		}
		pos = tell64(fd);

		for (i = 0; args[i] && i < sizeof args / sizeof args[0]; i++) {
			if (needspace)
				fputc(' ', stdout);
			if (lseek64(fd, (off64_t)(uintptr_t)args[i], SEEK_SET) == -1) {
				warning_exit(1, 0, "\ncouln't read arg: %s\n", strerror(errno));
				return 1;
			}
			do {
				if ((valid = read(fd, p, sizeof p - 1)) == -1) {
					warning_exit(1, 0, "\ncouldn't read arg: %s\n", strerror(errno));
					return 1;
				}
				p[valid] = 0;
				printf("%s", p);
			} while (!memchr(p, 0, valid));
			needspace = 1;
		}
	} while (i == sizeof args / sizeof args[0]);
	return 0;
}

int 
PidTidField(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	char			buf[20];
	assert(i);
	assert(fp);
	assert(fmt);
	if (fmt->flags & DATA_FILL)
		sprintf(buf, "%08d-%02d", pid, *tid);
	else
		sprintf(buf, "% 8d-%02d", pid, *tid);
	fwoutput(fp, fmt->width, buf);
	return 0;
}

int 
State(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	int				num_states = num_thread_states;
	const char		*s = "???";

	assert(i);
	assert(fp);
	assert(fmt);

	if (!i->status && fill_status(0, i, tid, fd))
		return 1;

	if (num_states > i->status->state)
		s = thread_state[i->status->state];

	format_data_string(fp, fmt, s);
	return 0;
}

int 
KerCall(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	assert(i);
	assert(fp);
	assert(fmt);

	if (!i->status && fill_status(0, i, tid, fd))
		return 1;

	if ((i->status->flags & _DEBUG_FLAG_IPINVAL) || i->status->state == STATE_READY || i->status->state == STATE_RUNNING)
		/*
		 * not at kernel call 
		 */
		fwoutput(fp, fmt->width, spaces);
	else {
		fwoutput(fp,
				 fmt->width - 2,
				 i->status->syscall >= sizeof(kc_names) / sizeof(char *)?
				 kc_names[sizeof	 (kc_names) / sizeof(char *) - 1] :
				 kc_names[i->status->syscall]);
	}
	return 0;
}

int 
LastCPU(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	assert(i);
	assert(fp);
	assert(fmt);

	if (!i->status && fill_status(0, i, tid, fd))
		return 1;

	format_data_int(fp, fmt, i->status->last_cpu);
	return 0;
}

int 
Memory(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	struct memobjects *mo;

	if (!i->status && fill_status(0, i, tid, fd))
		return 1;

	if (!i->text && !i->data && meminfo(i, fd, i->status->sp, 0))
		return 1;

	if(!(i->flags & NO_MEMINFO)) {
		mo = i->memobjects + i->next_memobject;
		format_data_string(fp, fmt, mo->name);
	}

	return 0;
}

int 
MemObjectCode(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	struct memobjects *mo;
	int size;

	if (!i->status && fill_status(0, i, tid, fd))
		return 1;

	if (!i->text && !i->data && meminfo(i, fd, i->status->sp, 0))
		return 1;

	if(!(i->flags & NO_MEMINFO)) {
		mo = i->memobjects + i->next_memobject;
		size = mo->text_size;
		if (size) {
			format_data_size(fp, fmt, size);
		} else {
			format_data_string(fp, fmt, "");
		}
	}

	return 0;
}

int 
MemObjectData(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	struct memobjects *mo;
	int size;

	if (!i->status && fill_status(0, i, tid, fd))
		return 1;

	if (!i->text && !i->data && meminfo(i, fd, i->status->sp, 0))
		return 1;

	if(!(i->flags & NO_MEMINFO)) {
		mo = i->memobjects + i->next_memobject;
		size = mo->data_size;
		if (size) {
			format_data_size(fp, fmt, size);
		} else {
			format_data_string(fp, fmt, "");
		}
	}

	return 0;
}

int 
MemObjectMapAddr(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	struct memobjects *mo;

	if (!i->status && fill_status(0, i, tid, fd))
		return 1;

	if (!i->text && !i->data && meminfo(i, fd, i->status->sp, 0))
		return 1;

	if(!(i->flags & NO_MEMINFO)) {
		mo = i->memobjects + i->next_memobject;
		format_data_int_hex(fp, fmt, mo->vaddr);
	}

	return 0;
}

int 
MemObjectOffset(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	struct memobjects *mo;

	if (!i->status && fill_status(0, i, tid, fd))
		return 1;

	if (!i->text && !i->data && meminfo(i, fd, i->status->sp, 0))
		return 1;

	if(!(i->flags & NO_MEMINFO)) {
		mo = i->memobjects + i->next_memobject;
		if (mo->offset != -1) {
			int l = fmt->width;
			fwoutput(fp, 1, "(");
			fmt->width -= 2;
			format_data_int_hex(fp, fmt, mo->offset);
			fwoutput(fp, 1, ")");
			fmt->width = l;
		} else {
			format_data_string(fp, fmt, "");
		}
	}

	return 0;
}

int 
Name(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	assert(i);
	assert(fp);
	assert(fmt);

	if (!i->name && fill_name(i, fd))
		return 1;

	format_data_string(fp, fmt, i->name->path);
	return 0;
}


int 
ThreadName(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	char threadname[_NTO_THREAD_NAME_MAX];
	int  ret;

	ret = __getset_thread_name(pid, *tid, NULL, -1, threadname, _NTO_THREAD_NAME_MAX);
	if (ret != EOK) {
		*threadname = '\0';			//Failure treated the same as no name
	}

	if(*threadname == '\0') {
		sprintf(threadname, "%d", *tid);
	}

	format_data_string(fp, fmt, threadname);

	return 0;
}


struct map {
	unsigned		addr;
	size_t			size;
	unsigned		flags;
	unsigned		debug_vaddr;
	off64_t			offset;
};

struct object {
	struct object  *next;
	ino_t			ino;
	dev_t			dev;
	char		   *name;
	struct map		text;
	struct map		data;
};

int 
meminfo(struct shared_info *i, int fd, unsigned int stack_base, int display)
{
	int				num, j, retval = 0;
	procfs_mapinfo	*m, *mapinfo = 0;
	struct {
		procfs_debuginfo	info;
		char				buff[_POSIX_PATH_MAX];
	}				map;
	int				num_maps = 0;
	struct object  *list, **plist, *object, *execed;
	struct memobjects *mo;
	unsigned		text_sysram, data_sysram, stack_sysram, stack_vsize;

	if (!i->info && fill_info(i, fd))
		return 1;

	// get mapping info
	for (num = num_maps + 1; num > num_maps;) {
		if (num > num_maps) {
			if (!(m = realloc(mapinfo, (num + 10) * sizeof *mapinfo))) {
				free(mapinfo);
				return 1;
			}
			mapinfo = m;
			num_maps = num + 10;
		}
		if (devctl(fd, DCMD_PROC_PAGEDATA, mapinfo, sizeof *mapinfo * num_maps, &num) != EOK) {
			retval = 1;
			goto cleanup;
		}
	}
	if(num == 0) {
		i->flags |= NO_MEMINFO;
	}

	// initialize counters
	text_sysram = data_sysram = stack_sysram = stack_vsize = 0;
	execed = 0;
	list = 0;
	plist = &list;

	for (m = mapinfo, j = num; j--; m++) {
		procfs_mapinfo	*m2;
		int				j;

		if (m->ino == 0) {
			continue;
		}
		map.info.vaddr = m->vaddr;
		if (devctl(fd, DCMD_PROC_MAPDEBUG, &map, sizeof map, 0) != EOK) {
			continue;
		}
		/*
		 * is it executable? 
		 */
		if (map.info.vaddr == m->vaddr && !(m->flags & MAP_ELF)) {
			if (m->flags & MAP_STACK) {
				/*
				 * mapped as stack 
				 */
				if (stack_base == 0 ||
						(stack_base >= m->vaddr && 
						 stack_base <= m->vaddr + m->size)) {
					stack_vsize += m->size;
					if(m->flags & PG_HWMAPPED) {
						stack_sysram += m->size;
					}
				}
			} else if (m->flags & MAP_SYSRAM) {
				/*
				 * other mapping that uses memory (could be object mapped
				 * private) 
				 */
				data_sysram += m->size;
			}
			/*
			 * If anonymous memory, make inode zero so we don't display it
			 * later on 
			 */
			if (strstr(map.info.path, "/dev/zero")) {
				m->ino = 0;
			}
			/*
			 * not executable, check next 
			 */
			continue;
		}
		object = malloc(sizeof *object);
		if (!object) {
			warning_exit(1, 0, "\nno memory for object\n");
			retval = 1;
			goto cleanup;
		}
		/*
		 * If it is base_address, we want to sort this object first later on 
		 */
		if (info.base_address > m->vaddr && info.base_address <= m->vaddr + m->size) {
			execed = object;
		}
		/*
		 * initialize text 
		 */
		memset(object, 0x00, sizeof *object);
		object->dev = m->dev;
		object->ino = m->ino;
		object->text.addr = m->vaddr;
		object->text.size = m->size;
		object->text.flags = m->flags;
		object->text.offset = m->offset;
		object->text.debug_vaddr = map.info.vaddr;
		object->name = chk_strdup(map.info.path);

		/*
		 * check for matching data 
		 */
		for (m2 = mapinfo, j = num; j--; m2++) {
			if (m->vaddr != m2->vaddr && m2->ino == m->ino && m2->dev == m->dev) {
				map.info.vaddr = m2->vaddr;
				if (devctl(fd, DCMD_PROC_MAPDEBUG, &map, sizeof map, 0) != EOK) {
					continue;
				}
				/*
				 * lower debug_vaddr is always text, if nessessary, swap 
				 */
				if ((int) map.info.vaddr < (int) object->text.debug_vaddr) {
					object->data = object->text;
					object->text.addr = m2->vaddr;
					object->text.size = m2->size;
					object->text.flags = m2->flags;
					object->text.offset = m2->offset;
					object->text.debug_vaddr = map.info.vaddr;
				} else {
					object->data.addr = m2->vaddr;
					object->data.size = m2->size;
					object->data.flags = m2->flags;
					object->data.offset = m2->offset;
					object->data.debug_vaddr = map.info.vaddr;
				}

				/*
				 * If it is base_address, we want to sort this object first
				 * later on 
				 */
				if (info.base_address > m2->vaddr && info.base_address <= m2->vaddr + m2->size) {
					execed = object;
				}
				/*
				 * make sure we don't look at this entry again 
				 */
				m2->ino = 0;
				break;
			}
		}
		/*
		 * make sure we don't look at this entry again 
		 */
		m->ino = 0;

		/*
		 * add it to the list 
		 */
		*plist = object;
		plist = &object->next;

		/*
		 * adjust totals 
		 */
		text_sysram += object->text.size;
		data_sysram += object->data.size;
	}
	// *plist= 0; // null terminate list

	/*
	 * move execed object to the first 
	 */
	for (plist = &list; object = *plist; plist = &object->next) {
		if (object == execed) {
			*plist = object->next;
			object->next = list;
			list = object;
			break;
		}
	}

	i->text = text_sysram;
	i->data = data_sysram;
	i->stack = stack_sysram;
	i->vstack = stack_vsize;

	for (j = 0, object = list ? list->next : list; object; object = object->next) j++;
	i->num_memobjects = j;
	i->next_memobject = 0;
	/* over allocate by the number of mapped objects we can come up with later */
	mo = i->memobjects = malloc((i->num_memobjects+num)*sizeof *i->memobjects);
	if (!mo) {
		warning_exit(1, 0, "\nout of memory\n");
		retval = 1;
		goto cleanup;
	}

	for (object = list ? list->next : list; object; object = object->next) {
		mo->name = chk_strdup(basename(object->name ? object->name : "unknown"));
		mo->text_size = object->text.size;
		mo->data_size = object->data.size;
		mo->vaddr = object->text.addr;
		mo->offset = -1;

		if (i->flags & SEPARATE_MEMORY) {
			i->text -= mo->text_size;
			i->data -= mo->data_size;
		}

		j--;
		mo++;
	}
	assert(!j);

	for (m = mapinfo, j = num; j--; m++) {
		if (m->ino) {
			map.info.vaddr = m->vaddr;
			if (devctl(fd, DCMD_PROC_MAPDEBUG, &map, sizeof map, 0) != EOK)
				continue;

			mo->name = chk_strdup(map.info.path);
			mo->text_size = 0;
			mo->data_size = m->size;
			mo->vaddr = m->vaddr;
			mo->offset = m->offset;

			mo++;
			i->num_memobjects++;
		}
	}

cleanup:
	/*
	 * clean up object list 
	 */
	if (list) {
		for (object = list; object;) {
			struct object  *obj;
			if (object->name) {
				free(object->name);
			}
			obj = object->next;
			free(object);
			object = obj;
		}
		list = 0;
	}
	free(mapinfo);
	return retval;
}

int 
pid(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	if (!i->info && !fill_info(i, fd))
		return 1;

	format_data_int(fp, fmt, i->info->pid);
	return 0;
}

int 
tid(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	if (!i->status && fill_status(0, i, tid, fd))
		return 1;

	format_data_int(fp, fmt, i->status->tid);
	return 0;
}

#define NO_MEMINFO_STRING	"---"

int 
codesize(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	if (!i->status && fill_status(0, i, tid, fd))
		return 1;

	if (!i->text && meminfo(i, fd, i->status->sp, 0))
		return 1;

	if(i->flags & NO_MEMINFO) {
		fwoutput(fp, fmt->width - (sizeof(NO_MEMINFO_STRING) - 1), spaces);
		fwoutput(fp, sizeof(NO_MEMINFO_STRING) - 1, NO_MEMINFO_STRING);
	} else {	
		format_data_size(fp, fmt, i->text);
	}
	return 0;
}

int 
datasize(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	if (!i->status && fill_status(0, i, tid, fd))
		return 1;

	if (!i->text && meminfo(i, fd, i->status->sp, 0))
		return 1;


	if(i->flags & NO_MEMINFO) {
		fwoutput(fp, fmt->width - (sizeof(NO_MEMINFO_STRING) - 1), spaces);
		fwoutput(fp, sizeof(NO_MEMINFO_STRING) - 1, NO_MEMINFO_STRING);
	} else {	
		format_data_size(fp, fmt, i->data);
	}
	return 0;
}

int 
stacksize(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	unsigned int	size = 0, vsize = 0;
	int				width = fmt->width;
	char			*sym, buffer[16];

	if (i->status) {
		free(i->status);
		i->status = 0;
	}
	if (!i->status && fill_status(0, i, tid, fd))
		return 1;

	if (!i->text && meminfo(i, fd, i->status->sp, 0))
		return 1;

	if(i->flags & NO_MEMINFO) {
		fwoutput(fp, fmt->width - (sizeof(NO_MEMINFO_STRING) - 1), spaces);
		fwoutput(fp, sizeof(NO_MEMINFO_STRING) - 1, NO_MEMINFO_STRING);
	} else {	
		size = i->stack;
		// @@@ for now.
		vsize = i->status->stksize;
		if (!(i->status->tid_flags & _NTO_TF_ALLOCED_STACK)) {
			vsize = i->status->stksize;
			if(*tid != 1) {
				size = vsize;
			}
		}

		size = normalize_data_size(size, &sym);
		sprintf(buffer, "%d%s", size, sym);
		vsize = normalize_data_size(vsize, &sym);
		sprintf(&buffer[strlen(buffer)], "(%d%s)", vsize, sym);

		fwoutput(fp, fmt->width - strlen(buffer) - 1, spaces);
		fwoutput(fp, strlen(buffer), buffer);
		if (!(i->status->tid_flags & _NTO_TF_ALLOCED_STACK)) {
			fwoutput(fp, 1, "*");
		} else {
			fwoutput(fp, 1, " ");
		}
		fmt->width = width;
	}
	return 0;
}

int 
long_name(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	assert(i);
	assert(fp);
	assert(fmt);

	if (!i->name && fill_name(i, fd))
		return 1;

	fprintf(fp, "%s ", i->name->path);
	return 0;
}

int 
Pgrp(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	assert(i);
	assert(fp);
	assert(fmt);

	if (!i->info && fill_info(i, fd))
		return 1;

	format_data_int(fp, fmt, i->info->pgrp);
	return 0;
}

int 
parentpid(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	assert(i);
	assert(fp);
	assert(fmt);

	if (!i->info && fill_info(i, fd))
		return 1;

	if (i->info->parent)
		format_data_int(fp, fmt, i->info->parent);
	else
		format_data_string(fp, fmt, spaces);

	return 0;
}

int 
Child(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	assert(i);
	assert(fp);
	assert(fmt);

	if (!i->info && fill_info(i, fd))
		return 1;

	if (i->info->child)
		format_data_int(fp, fmt, i->info->child);
	else
		format_data_string(fp, fmt, spaces);

	return 0;
}

int 
Sibling(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	assert(i);
	assert(fp);
	assert(fmt);

	if (!i->info && fill_info(i, fd))
		return 1;

	if (i->info->sibling)
		format_data_int(fp, fmt, i->info->sibling);
	else
		format_data_string(fp, fmt, spaces);

	return 0;
}

int 
Sid(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	assert(i);
	assert(fp);
	assert(fmt);

	if (!i->info && fill_info(i, fd))
		return 1;

	if (i->info->sid)
		format_data_int(fp, fmt, i->info->sid);
	else
		format_data_string(fp, fmt, spaces);

	return 0;
}

int 
Uid(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	assert(i);
	assert(fp);
	assert(fmt);

	if (!i->info && fill_info(i, fd))
		return 1;

	format_data_int(fp, fmt, i->info->uid);

	return 0;
}

int 
Gid(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	assert(i);
	assert(fp);
	assert(fmt);

	if (!i->info && fill_info(i, fd))
		return 1;

	format_data_int(fp, fmt, i->info->gid);

	return 0;
}

int 
EUid(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	assert(i);
	assert(fp);
	assert(fmt);

	if (!i->info && fill_info(i, fd))
		return 1;

	format_data_int(fp, fmt, i->info->euid);

	return 0;
}

int 
EGid(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	assert(i);
	assert(fp);
	assert(fmt);

	if (!i->info && fill_info(i, fd))
		return 1;

	format_data_int(fp, fmt, i->info->egid);

	return 0;
}

int 
SUid(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	assert(i);
	assert(fp);
	assert(fmt);

	if (!i->info && fill_info(i, fd))
		return 1;

	format_data_int(fp, fmt, i->info->suid);

	return 0;
}

int 
SGid(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	assert(i);
	assert(fp);
	assert(fmt);

	if (!i->info && fill_info(i, fd))
		return 1;

	format_data_int(fp, fmt, i->info->sgid);

	return 0;
}

#define BITS_SIZE   ((unsigned)sizeof(unsigned)*8)

int 
SigIgnore(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	unsigned long  bit0 = 0, bit1 = 0, signo;
	assert(i);
	assert(fp);
	assert(fmt);

	if (!i->info && fill_info(i, fd))
		return 1;

	for ( signo = _SIGMIN; signo <= _SIGMAX; signo++ ) {
		if ( sigismember( &i->info->sig_ignore, signo ) ) {
			if ( signo <= BITS_SIZE ) {
			   	bit0 |= 1 << (signo-1);
			} else {
			   	bit1 |= 1 << (signo-1-BITS_SIZE);
			}
		}
	}
	format_data(fp, fmt, "%08x%08x", bit1, bit0);
	return 0;
}

int 
SigPending(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	unsigned long  bit0 = 0, bit1 = 0, signo;
	assert(i);
	assert(fp);
	assert(fmt);

	if (!i->info && fill_info(i, fd))
		return 1;
	if (!i->status && fill_status(0, i, tid, fd))
		return 1;

	for ( signo = _SIGMIN; signo <= _SIGMAX; signo++ ) {
		if ( sigismember( &i->info->sig_pending, signo )
				|| sigismember( &i->status->sig_pending, signo ) ) {
			if ( signo <= BITS_SIZE ) {
			   	bit0 |= 1 << (signo-1);
			} else {
			   	bit1 |= 1 << (signo-1-BITS_SIZE);
			}
		}
	}
	format_data(fp, fmt, "%08x%08x", bit1, bit0);
	return 0;
}

int 
ProcessUtime(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	assert(i);
	assert(fp);
	assert(fmt);

	if (!i->info && fill_info(i, fd))
		return 1;

	format_data_time(fp, fmt, i->info->utime);
	return 0;
}

int 
ProcessStime(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	assert(i);
	assert(fp);
	assert(fmt);

	if (!i->info && fill_info(i, fd))
		return 1;

	format_data_time(fp, fmt, i->info->stime);
	return 0;
}

int 
ProcessCutime(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	assert(i);
	assert(fp);
	assert(fmt);

	if (!i->info && fill_info(i, fd))
		return 1;

	format_data_time(fp, fmt, i->info->cutime);
	return 0;
}

int 
ProcessCstime(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	assert(i);
	assert(fp);
	assert(fmt);

	if (!i->info && fill_info(i, fd))
		return 1;

	format_data_time(fp, fmt, i->info->cstime);
	return 0;
}

int 
ThreadSUtime(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	assert(i);
	assert(fp);
	assert(fmt);

	if (!i->info && fill_info(i, fd))
		return 1;
	if (!i->status && fill_status(0, i, tid, fd))
		return 1;

	format_data_time(fp, fmt, i->status->sutime);
	return 0;
}

int 
ProcessStartTime(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	assert(i);
	assert(fp);
	assert(fmt);

	if (!i->info && fill_info(i, fd))
		return 1;

	format_data_clock(fp, fmt, i->info->start_time);
	return 0;
}

int 
ThreadStartTime(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	assert(i);
	assert(fp);
	assert(fmt);

	if (!i->info && fill_info(i, fd))
		return 1;
	if (!i->status && fill_status(0, i, tid, fd))
		return 1;

	format_data_clock(fp, fmt, i->status->start_time);
	return 0;
}

int
DebugFlags(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	assert(i);
	assert(fp);
	assert(fmt);

	if (!i->info && fill_info(i, fd))
		return 1;
	if (!i->status && fill_status(0, i, tid, fd))
		return 1;

	format_data(fp, fmt, "%08x", i->status->flags);
	return 0;
}

int
ThreadFlags(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	assert(i);
	assert(fp);
	assert(fmt);

	if (!i->info && fill_info(i, fd))
		return 1;
	if (!i->status && fill_status(0, i, tid, fd))
		return 1;

	format_data(fp, fmt, "%08x", i->status->tid_flags);
	return 0;
}

int
ProcessFlags(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	assert(i);
	assert(fp);
	assert(fmt);

	if (!i->info && fill_info(i, fd))
		return 1;
	if (!i->status && fill_status(0, i, tid, fd))
		return 1;

	format_data(fp, fmt, "%08x", i->info->flags);
	return 0;
}

int 
NumThreads(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	assert(i);
	assert(fp);
	assert(fmt);

	if (!i->info && fill_info(i, fd))
		return 1;

	format_data_int(fp, fmt, i->info->num_threads);
	return 0;
}

int 
priority(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
	char			q;
	assert(i);
	assert(fp);
	assert(fmt);

	if (!i->status && fill_status(0, i, tid, fd))
		return 1;

	fmt->width--;				
							 
	format_data_int(fp, fmt, i->status->priority);
	fmt->width++;
	switch (i->status->policy) {
	case SCHED_FIFO:
		q = 'f';
		break;
	case SCHED_RR:
		q = 'r';
		break;
	case SCHED_OTHER:
		q = 'o';
		break;
	case SCHED_SPORADIC:
		q = 's';
		break;
	default:
		q = '?';
		break;

	}
	fputc(q, fp);
	return 0;
}

int
ExtSched(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *i)
{
struct sched_query	query;
char				str[32];

	assert(i); assert(fp); assert(fmt);

	if (i->status == NULL && fill_status(0, i, tid, fd))
		return(1);

	str[0] = '\0';
	if (!*node && !SchedCtl(SCHED_QUERY_SCHED_EXT, &query, sizeof(query))) {
		switch (query.extsched) {
		case SCHED_EXT_APS: {
		struct extsched_aps_dbg_thread	*dbg;
		sched_aps_partition_info		aps;
			APS_INIT_DATA(&aps);

			dbg = (struct extsched_aps_dbg_thread *)i->status->extsched;
			aps.id = dbg->id;
			if (!SchedCtl(SCHED_APS_QUERY_PARTITION, &aps, sizeof(aps))) sprintf(str, "%s", aps.name);
			} break;
		}
	}
	format_data_string(fp, fmt, str);

	return(0);
}

int
Rmasks(FILE * fp, int pid, int *tid, struct format *fmt, int fd, struct shared_info *sinfo)
{
	int			rsize, size, *rsizep;
	int			i, j;
	unsigned 		*rmaskp;
	procfs_threadctl	*threadctl;
	const char		*header;

	if (sinfo->info->flags & _NTO_PF_ZOMBIE) {
		/* Save the _devctl() below which will fail with ESRCH */
		return 0;
	}

	rsize = RMSK_SIZE(_syspage_ptr->num_cpu);

	size = offsetof(procfs_threadctl, data);
	size += sizeof(int);			/* rsize */
	size += rsize * sizeof(unsigned);	/* runmask */
	size += rsize * sizeof(unsigned);	/* inherit_mask */

	if ((threadctl = alloca(size)) == NULL)
		return 1;
	/* zeroed masks means get without alteration */
	memset(threadctl, 0x00, size);

	rsizep = (int *)threadctl->data;
	*rsizep = rsize;
	rmaskp = rsizep + 1;

	threadctl->cmd = _NTO_TCTL_RUNMASK_GET_AND_SET_INHERIT;
	threadctl->tid = *tid;

	if (_devctl(fd, DCMD_PROC_THREADCTL, threadctl,
	    size, _DEVCTL_FLAG_NORETVAL) == -1) {
		/*
		 * Old proc?
		 *
		 * We could try _NTO_TCTL_RUNMASK_GET_AND_SET which has 
		 * been around longer to pull out just the rmask (not 
		 * the inherit).  Unfortunatley, old versions of proc also 
		 * didn't allow any _NTO_TCTL_RUNMASK_GET_AND_SET ops 
		 * with O_RDONLY (even if no alter was being performed).
		 */
		return 1;
	}

	header = "Runmask";
	for (i = 0; i < 2; i++) {
		for (j = 0; j < rsize; j++) {
			fprintf(fp, "\t%-12s: %#.*x\n",
			    header,
			    /* number of hex digits in an unsigned */
			    (int)(sizeof(unsigned) << 1),
			    *rmaskp);
			rmaskp++;
			header = "";
		}
		header = "Inherit Mask";
	}

	return 0;
}

__SRCVERSION("pidin_proc.c $Rev: 153052 $");
