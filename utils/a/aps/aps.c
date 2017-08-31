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



#include <errno.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <libgen.h>
#include <signal.h>
#include <spawn.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <grp.h>
#include <shadow.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/neutrino.h>
#include <sys/sched_aps.h> 
#include <sys/siginfo.h>
#include <sched.h>
#include <sys/syspage.h>


char * fullprogname = 0;

//============================================================

/*
IMPORTANT : Reserved for future use.  Note however that there is
*NO PLAN* at present for CPU affinity, and it may never happen.

  '-n' if we ever allow aps to operate on remote node like on/pidin -n.
  '-C' for cpu specification (if we ever have cpu affinity for partitions)
  '-R' for runmask specification (if we ever have cpu affinity for partitions)

#ifdef __USAGE
%C - Manage adaptive scheduler partitions

%C show [-d <delay>] [-f <shorthand>] [-l] [-v...] [<partition_name> ...]
%C create -b <budget> [-B <critical_budget>] <partition_name>
%C modify [-b <budget>] [-B <critical_budget>] <partition_name>
%C modify [-y <bankruptcy_policy> ...] [-s <sec_policy> ...]
           [-S <sched_policy> ...] [-w <windowsize_ms>]
show:
-d delay        Delay period in tenths of a second  [defaults: 50]
-f shorthand    Display various information based on the shorthand specified:
                all
                overall_stats
                scheduler
                partitions
                usage [default]
-l              Display at intervals decided by the -d option
-v              More 'v's means more verbosity/details
                Particularly useful for displaying 'usage'.

create:
-b budget       Specify cpu budget percentage
-B milliseconds Specify critical cpu budget in ms

modify: (partition)
-b budget       Specify cpu budget percentage
-B milliseconds Specify critical cpu budget in ms

modify: (scheduler)
-S policy       Specify an APS scheduling policy to use
                (one of: normal [default], freetime_by_ratio, bmp_safety)
-s policy       Specify one or more of the security policies to add.
                Security policies cannot be removed.
                (one of: root0_overall, root_makes_partitions,
                         sys_makes_partitions, parent_modifies,
                         nonzero_budgets, root_makes_critical,
                         sys_makes_critical, root_joins
                         sys_joins, parent_joins, join_self_only
                         partitions_locked,
                         recommended, flexible, basic, none [default])
-w msec         Set averaging windowsize in milliseconds (8 to 400).
-y policy       Set bankruptcy policy
                (one of: cancel_budget, log, reboot,
                         basic [default], recommended, none)
#endif
*/

//============================================================

void
printwarning (char *msgfmt, ...)
{
	va_list ap;
	va_start(ap, msgfmt);
	fprintf(stdout, "Warning: ");
	vfprintf(stdout, msgfmt, ap);
	fprintf(stdout, "\n");
	va_end(ap);
}

void
vprinterr (char *msgfmt, va_list ap)
{
	fprintf(stderr, "Error: ");
	vfprintf(stderr, msgfmt, ap);
	fprintf(stderr, "\n");
}

void
printerr (char *msgfmt, ...)
{
	va_list ap;
	va_start(ap, msgfmt);
	vprinterr(msgfmt, ap);
	va_end(ap);
}

void
fatalerr (char *msgfmt, ...)
{
	va_list ap;
	va_start(ap, msgfmt);
	vprinterr(msgfmt, ap);
	va_end(ap);
	exit(1);
}

//============================================================

int
lookup_id (char *name)
{ 
/* -------
 * returns -1 if cant figure out an id
 */
	sched_aps_lookup_parms 	parm; 
	if (name) {
		APS_INIT_DATA(&parm);
 		parm.name = name;
	    if (SchedCtl(SCHED_APS_LOOKUP, &parm, sizeof(parm)) == EOK) {
			return parm.id;
		}
	}
	return -1;
}

/*
 * Note: don't assume the list is sorted.
 */ 
int
partition_name_is_in_list (char *name,
						   char **name_list, int num_names)
{
	int i;
	
	for (i=0; i < num_names; i++) {
		if (strcmp(name, name_list[i]) == 0) {
			return 1;
		}
	}
	return 0;
}

//============================================================
//
// General purpose keyword to/from bitmask flags functions
//

#define OPT_COMBO      0x1
#define OPT_CLOBBER    0x2

typedef struct { 
	_Uint32t flag_value;
	char 	*keyword_name;
	int		 options;		/* bit 0: it's a combo
							 * bit 1: replace value, instead of or'ing
							 */
} keyword_t;

typedef struct {
	keyword_t *keywords;
	int num_keywords;
} keyword_subparm_t;


void
print_keywords_from_flags (keyword_subparm_t *subparm, char *line_prefix,
						   _Uint32t keyword_flags)
{
	int i; 
	keyword_t *op;
	int something_printed = 0;
	
	for (i=0; i < subparm->num_keywords; i++){
		op = &subparm->keywords[i];
		if (op->options & OPT_COMBO) {
			if (op->flag_value == keyword_flags) {
				something_printed = 1;
				printf("%s%s\n",line_prefix, 
					   subparm->keywords[i].keyword_name);
				/* No need to search further once we have an EXACT
				 * match for a combo */
				break;
			}
		} else {
			if (op->flag_value & keyword_flags) {
				something_printed = 1;
				printf("%s%s\n",line_prefix, 
					   subparm->keywords[i].keyword_name);
			}
		}
	}
	if (! something_printed) {
		printf("%snone\n", line_prefix);
	}
}

/* returns: 1 if valid keywords. updates *flagp with flag value if
 * keyword is valid */ 
int
keyword_to_flag (keyword_subparm_t *subparm, char *keyword, _Uint32t *flagp)
{ 
	int i; 
    for (i=0; i<subparm->num_keywords; i++) {
		if (strcmp(subparm->keywords[i].keyword_name, keyword) == 0) {
			if (subparm->keywords[i].options & OPT_CLOBBER) {
				*flagp = subparm->keywords[i].flag_value;
			} else {
				*flagp |= subparm->keywords[i].flag_value;
			}
			return 1; 
		}
	}
	return 0; 
} 


//============================================================
//
// APS Security keyword to/from flags mapping
//

/* Combos must be listed first, then, followed by the other flags
 * sorted in ascending order, and finally, at the end "none". */

keyword_t security_options[] = { 
	{SCHED_APS_SEC_RECOMMENDED,           "recommended",OPT_COMBO|OPT_CLOBBER},
	{SCHED_APS_SEC_FLEXIBLE,              "flexible",   OPT_COMBO|OPT_CLOBBER},
	{SCHED_APS_SEC_BASIC,                 "basic",      OPT_COMBO|OPT_CLOBBER},
	{SCHED_APS_SEC_JOIN_SELF_ONLY,        "join_self_only",       0x0},
	{SCHED_APS_SEC_NONZERO_BUDGETS,       "nonzero_budgets",      0x0},
	{SCHED_APS_SEC_PARENT_JOINS,          "parent_joins",         0x0},
	{SCHED_APS_SEC_PARENT_MODIFIES,       "parent_modifies",      0x0},
	{SCHED_APS_SEC_PARTITIONS_LOCKED,     "partitions_locked",    0x0},
	{SCHED_APS_SEC_ROOT0_OVERALL,         "root0_overall",        0x0},
	{SCHED_APS_SEC_ROOT_JOINS,            "root_joins",           0x0}, 
	{SCHED_APS_SEC_ROOT_MAKES_CRITICAL,   "root_makes_critical",  0x0},
	{SCHED_APS_SEC_ROOT_MAKES_PARTITIONS, "root_makes_partitions",0x0},
	{SCHED_APS_SEC_SYS_JOINS,             "sys_joins",            0x0},
	{SCHED_APS_SEC_SYS_MAKES_CRITICAL,    "sys_makes_critical",   0x0},
	{SCHED_APS_SEC_SYS_MAKES_PARTITIONS,  "sys_makes_partitions", 0x0},
	{SCHED_APS_SEC_OFF,                   "none",                 0x2},
};



keyword_subparm_t security_handle_options = {
	security_options,
	sizeof(security_options)/sizeof(keyword_t), /* Number of keywords */
	
}; 

void
print_security_flags (char *line_prefix,
					  _Uint32t security_flags)
{ 
	print_keywords_from_flags(&security_handle_options, line_prefix,
							  security_flags);
} 


//============================================================
//
// APS bankruptcy keyword to/from flags mapping
//

/* Combos must be listed first, then, followed by the other flags
 * sorted in ascending order, and finally, at the end "none". */

keyword_t  bankruptcy_options[] = { 
	{SCHED_APS_BNKR_BASIC,         "basic",         OPT_COMBO|OPT_CLOBBER},
	{SCHED_APS_BNKR_RECOMMENDED,   "recommended",   OPT_COMBO|OPT_CLOBBER},
	{SCHED_APS_BNKR_CANCEL_BUDGET, "cancel_budget", 0x0},
	{SCHED_APS_BNKR_LOG,           "log",           0x0},
	{SCHED_APS_BNKR_REBOOT,        "reboot",        0x0},
	/* 'none' is treated seperately, since a null pointer means 'none'... */
};

keyword_subparm_t bankruptcy_handle_options = {
	bankruptcy_options,
	sizeof(bankruptcy_options)/sizeof(keyword_t), /* Number of keywords */
}; 

void
print_bankruptcy_flags (char *line_prefix,
						_Uint32t bankruptcy_flags)
{ 
	print_keywords_from_flags(&bankruptcy_handle_options, line_prefix,
							  bankruptcy_flags); 
} 


//============================================================
//
// APS utility shorthand keyword to/from flags mapping
//

#define SHORTHAND_OVERALL_STATS      0x000000001
#define SHORTHAND_PARTITIONS         0x000000002
#define SHORTHAND_SCHEDULER          0x000000004
#define SHORTHAND_USAGE              0x000000008
#define SHORTHAND_ALL                (SHORTHAND_OVERALL_STATS| \
									  SHORTHAND_PARTITIONS| \
									  SHORTHAND_SCHEDULER| \
									  SHORTHAND_USAGE)

/* Combos must be listed first, then, followed by the other flags
 * sorted in ascending order, and finally, at the end "none". */
keyword_t shorthand_options[] = {
	{SHORTHAND_ALL,           "all",           OPT_COMBO|OPT_CLOBBER},
	{SHORTHAND_OVERALL_STATS, "overall_stats", 0},
	{SHORTHAND_PARTITIONS,    "partitions",    0},
	{SHORTHAND_SCHEDULER,     "scheduler",     0},
	{SHORTHAND_USAGE,         "usage",         0},
};

keyword_subparm_t shorthand_handle_options = {
	shorthand_options,
	sizeof(shorthand_options)/sizeof(keyword_t), /* Number of keywords */
}; 


//============================================================
//
// APS scheduling policy keyword to/from flags mapping
//

/* Combos must be listed first, then, followed by the other flags
 * sorted in ascending order, and finally, at the end "none". */

keyword_t  schedpol_options[] = { 
	{SCHED_APS_SCHEDPOL_DEFAULT,           "normal",		OPT_COMBO|OPT_CLOBBER},
	{SCHED_APS_SCHEDPOL_FREETIME_BY_RATIO, "freetime_by_ratio",	0},
	{SCHED_APS_SCHEDPOL_BMP_SAFETY,        "bmp_safety",		0},
	/* 'none' is treated separately, since a null pointer means 'none'... */
};

keyword_subparm_t schedpol_handle_options = {
	schedpol_options,
	sizeof(schedpol_options)/sizeof(keyword_t), /* Number of keywords */
}; 

void
print_schedpol_flags (char *line_prefix,
						_Uint32t schedpol_flags)
{ 
	print_keywords_from_flags(&schedpol_handle_options, line_prefix,
							  schedpol_flags); 
} 

//============================================================

/*
 * NOTE: Do not cache the information that is printed in case the
 * 'show' is looping.
 */
int
print_scheduler (void)
{
	int ret;
	sched_aps_info sched_info;
	APS_INIT_DATA(&sched_info);
	
	ret = SchedCtl(SCHED_APS_QUERY_PARMS, &sched_info, sizeof(sched_info)); 
	if (ret != EOK) { 
		printerr("can't query scheduler parameters: %s (%d)",
				 strerror(errno), errno);
		return ret;
	}

	print_security_flags("security option   : ",
						 sched_info.sec_flags);

	print_bankruptcy_flags("bankruptcy policy : ",
						   sched_info.bankruptcy_policy);

	print_schedpol_flags("scheduling policy : ",
						   sched_info.scheduling_policy_flags);

	{
	unsigned long ws =
		(sched_info.windowsize_cycles / sched_info.cycles_per_ms);
	printf("window size (ms)  : %lu\n", ws);
	}

	return ret;
}

/*
 * NOTE: Do not cache the information that is printed in case the
 * 'show' is looping.
 */
int
print_overall_stats (void)
{
	int ret;
	sched_aps_overall_stats p;
	APS_INIT_DATA(&p);
	
	ret = SchedCtl(SCHED_APS_OVERALL_STATS, &p, sizeof(p));
	if (ret == EOK) {
		printf("last bankrupting id        : %d\n",
			   p.id_at_last_bankruptcy);
		printf("pid,tid at last bankruptcy : %d,%d\n",
			   p.pid_at_last_bankruptcy, p.tid_at_last_bankruptcy);
	}

	return ret;
}


/* the probable maximum number of partitions we'll have to deal with */
#define APS_MAX_PARTITIONS 16 
/* @@@ APS_MAX_PARTITIONS should eventually be replaced with code to read max_partitions from SCHED_APS_QUERY_PARMS */

typedef struct {
	sched_aps_info sched_info; 
	sched_aps_partition_stats part_stats[APS_MAX_PARTITIONS];
	sched_aps_partition_info  part_info[APS_MAX_PARTITIONS];
} sched_and_parts_data_t;

/*
 * Read all the scheduler and partitions info by keeping the system
 * calls as close to one another as possible.  This ensures that the
 * numbers are more accurate/consistent, since it's not impossible
 * that the numbers change as we read them.
 *
 * Return: a pointer to the data that the caller is responsible to
 * free, or return 0 (NULL) if the memory could not be allocated.
 *
 * Note: the caller should not access member part_stats, unless the
 * function was invoked with include_part_stats=1.
 */
sched_and_parts_data_t *
read_all_sched_and_parts (int include_part_stats)
{
	sched_and_parts_data_t *d;
	int i;
	int ret = EOK;

	d = (sched_and_parts_data_t*)calloc(1, sizeof(sched_and_parts_data_t));
	if (d == 0) {
		printerr("unable to allocate memory");
		return 0;
	}

	APS_INIT_DATA(&(d->sched_info));
	ret = SchedCtl(SCHED_APS_QUERY_PARMS,
				   &(d->sched_info), sizeof(d->sched_info)); 
	if (ret != EOK) { 
		printerr("can't query scheduler parms: %s (%d)",
				 strerror(errno), errno);
	} else {
		if (include_part_stats) {
			APS_INIT_DATA(&(d->part_stats)); 
			d->part_stats[0].id = 0;
			ret = SchedCtl(SCHED_APS_PARTITION_STATS,
						   &(d->part_stats), sizeof(d->part_stats));
		} else {
			ret = EOK;
		}
		
		if (ret != EOK) { 
			printerr("can't read partition stats: %s (%d)",
					 strerror(errno), errno); 
		} else {

			for(i = 0; i < d->sched_info.num_partitions; i++) {
				APS_INIT_DATA(&(d->part_info[i]));
				d->part_info[i].id = i;
				ret = SchedCtl(SCHED_APS_QUERY_PARTITION,
							   &(d->part_info[i]),
							   sizeof(d->part_info[0]));
				if (ret != EOK) { 
					printerr("can't get info for partition %d: %s (%d)",
							 i, strerror(errno), errno);
					break;
				}
			}
		}
	}

	if (ret != EOK) {
		free(d);
		return 0;
	}
	return d;
}

/*
 * NOTE: Do not cache the information that is printed in case the
 * 'show' is looping.
 */
int
print_partitions (char **partition_names, int num_partition_names)
{
	int i;
	sched_and_parts_data_t * d;

	/* Note: data must be freed at the end */
	d = read_all_sched_and_parts(0 /* do not include part_stats */);
	if (d == 0) {
		return -1;
	}
	
	printf("%-*s  id   parentId   %%  ms    pidBnkr    tidBnkr\n",
		   APS_PARTITION_NAME_LENGTH,
		   "PartitionName");
	for(i = 0; i < d->sched_info.num_partitions; i++) {

		if (num_partition_names == 0 ||
			partition_name_is_in_list(d->part_info[i].name,
									  partition_names,
									  num_partition_names)) {
			printf("%-*s %3d %10d %3d %3d %10d %10d\n",
				   APS_PARTITION_NAME_LENGTH,
				   d->part_info[i].name,
				   i,
				   d->part_info[i].parent_id,
				   d->part_info[i].budget_percent,
				   (int)(d->part_info[i].critical_budget_cycles /
						 d->sched_info.cycles_per_ms),
				   d->part_info[i].pid_at_last_bankruptcy,
				   d->part_info[i].tid_at_last_bankruptcy);
		}
	}

	free(d);
	return EOK;
}

typedef enum {
	pg_nothing = 0,
	pg_simple,
	pg_enhancedused,
    pg_enhancedused_enhancedcritical,
	pg_max_level = pg_enhancedused_enhancedcritical,
} pg_detail_level_t;

/*
 * NOTE: Do not cache the information that is printed in case the
 * 'show' is looping.
 */
int
print_groups_usage (int detail_level,
					char **partition_names, int num_names)
{
	int 	i;
	int		total_per_bud;
	double	total_per_used, total_per_w2, total_per_w3;
    double sched_windowsize_s, windowsize2_s, windowsize3_s; 		
	pg_detail_level_t pg_detail_level;
	sched_and_parts_data_t *d;
	static char * long_dash_string="----------------------------------------";

	/* Note: data must be freed at the end */
	d = read_all_sched_and_parts(1 /* include part_stats */);
	if (d == 0) {
		return -1;
	}

	if (detail_level < pg_simple) {
		pg_detail_level = pg_simple;
	} else if (detail_level > pg_enhancedused_enhancedcritical) {
		pg_detail_level = pg_enhancedused_enhancedcritical;
	} else {
		pg_detail_level = detail_level;
	}

	sched_windowsize_s =
		(double)d->sched_info.windowsize_cycles /
		(double)d->sched_info.cycles_per_ms/1000.0; 
	windowsize2_s =
		(double)d->sched_info.windowsize2_cycles /
		(double)d->sched_info.cycles_per_ms/1000.0; 
	windowsize3_s =
		(double)d->sched_info.windowsize3_cycles /
		(double)d->sched_info.cycles_per_ms/1000.0;
	
	switch(pg_detail_level) {
		case pg_nothing:
			break;
			
		case pg_simple:
			printf("%*.*s     +---- CPU Time ----+-- Critical Time --\n",
				   APS_PARTITION_NAME_LENGTH, APS_PARTITION_NAME_LENGTH, "");
			printf("%-*.*s  id | Budget |    Used | Budget |      Used\n",
				   APS_PARTITION_NAME_LENGTH, APS_PARTITION_NAME_LENGTH, "Partition name");
			printf("%*.*s-----+------------------+-------------------\n",
				   APS_PARTITION_NAME_LENGTH, APS_PARTITION_NAME_LENGTH, long_dash_string);
			break;
			
		case pg_enhancedused:
			printf("%*.*s     +----------- CPU Time ------------+-- Critical Time --\n",
				   APS_PARTITION_NAME_LENGTH, APS_PARTITION_NAME_LENGTH, "");
			printf("                    |        |           Used         |        |\n");
			printf("%-*.*s  id | Budget | %2.3fs   %2.2fs   %2.1fs | Budget |      Used\n",
				   APS_PARTITION_NAME_LENGTH, APS_PARTITION_NAME_LENGTH, "Partition name", 
				   sched_windowsize_s, windowsize2_s, windowsize3_s);
			printf("%*.*s-----+---------------------------------+-------------------\n",
				   APS_PARTITION_NAME_LENGTH, APS_PARTITION_NAME_LENGTH, long_dash_string);
			break;
			
		case pg_enhancedused_enhancedcritical:
			printf("%*.*s     +----------- CPU Time ------------+------------ Critical Time ------------\n",
				   APS_PARTITION_NAME_LENGTH, APS_PARTITION_NAME_LENGTH, "");
			printf("%*.*s     |        |           Used         |        |               Used\n",
				   APS_PARTITION_NAME_LENGTH, APS_PARTITION_NAME_LENGTH, "");
			printf("Partition name   id | Budget | %2.3fs   %2.2fs   %2.1fs | Budget |      %2.1fs      %2.1fs     %2.1fs\n",
				   sched_windowsize_s, windowsize2_s, windowsize3_s, 
				   sched_windowsize_s, windowsize2_s, windowsize3_s);
			printf("%*.*s-----+---------------------------------+---------------------------------------\n",
				   APS_PARTITION_NAME_LENGTH, APS_PARTITION_NAME_LENGTH, long_dash_string);
			break;
	}
	
	total_per_used = total_per_w2 = total_per_w3 = 0.0;
	total_per_bud = 0;   
	for(i = 0; i < d->sched_info.num_partitions; i++) {
		double per, per_w2,per_w3;
		int  critical_budget_ms;
		double  critical_used_ms, critical_used_ms_w2, critical_used_ms_w3; 
	
		if (num_names == 0 ||
			partition_name_is_in_list(d->part_info[i].name,
									  partition_names, num_names)) {
			
			critical_used_ms =
				(double)d->part_stats[i].critical_time_cycles /
				(double)d->sched_info.cycles_per_ms;	
			critical_used_ms_w2 =
				(double)d->part_stats[i].critical_time_cycles_w2 /
				(double)d->sched_info.cycles_per_ms;	
			critical_used_ms_w3 =
				(double)d->part_stats[i].critical_time_cycles_w3 /
				(double)d->sched_info.cycles_per_ms;	
			critical_budget_ms =
				d->part_info[i].critical_budget_cycles /
				d->sched_info.cycles_per_ms;
			total_per_bud += d->part_info[i].budget_percent;
		
			per =
				(((double)100.0)*(double)d->part_stats[i].run_time_cycles) /
				(double)d->sched_info.windowsize_cycles;
			total_per_used +=per;

			per_w2 =
				(((double)100.0)*(double)d->part_stats[i].run_time_cycles_w2) /
				(double)d->sched_info.windowsize2_cycles;
			total_per_w2 += per_w2;
			
			per_w3 =
				(((double)100.0)*(double)d->part_stats[i].run_time_cycles_w3) /
				(double)d->sched_info.windowsize3_cycles;
			total_per_w3 += per_w3;
		
			switch(pg_detail_level) {
				case pg_nothing:
					break;
				
				case pg_simple:
					printf("%-*.*s %3d |   %3d%% | %6.2f%% |  %3dms | %7.3fms\n",
						   APS_PARTITION_NAME_LENGTH, APS_PARTITION_NAME_LENGTH, d->part_info[i].name,
						   i,
						   d->part_info[i].budget_percent,
						   per, critical_budget_ms, critical_used_ms);
					break;

				case pg_enhancedused:
					printf("%-*.*s %3d |   %3d%% |%6.2f%% %6.2f%% %6.2f%% |  %3dms | %7.3fms\n",
						   APS_PARTITION_NAME_LENGTH, APS_PARTITION_NAME_LENGTH, d->part_info[i].name,
						   i, 
						   d->part_info[i].budget_percent,
						   per, per_w2, per_w3,
						   critical_budget_ms, critical_used_ms);
					break;

				case pg_enhancedused_enhancedcritical:
					printf("%-*.*s %3d |   %3d%% |%6.2f%% %6.2f%% %6.2f%% |  %3dms | %7.3fms %7.3fms %7.3fms\n",
						   APS_PARTITION_NAME_LENGTH, APS_PARTITION_NAME_LENGTH, d->part_info[i].name,
						   i,
						   d->part_info[i].budget_percent,
						   per, per_w2, per_w3,
						   critical_budget_ms, critical_used_ms,
						   critical_used_ms_w2, critical_used_ms_w3);
					break;
			}
		}
	}

	/* Only print totals if all partitions are displayed */
	switch(pg_detail_level) {
		case pg_nothing:
			break;
			
		case pg_simple:
			printf("%*.*s-----+------------------+-------------------\n",
				   APS_PARTITION_NAME_LENGTH, APS_PARTITION_NAME_LENGTH, long_dash_string);
			printf("%-*.*s     |   %3d%% | %6.2f%% |\n",
				   APS_PARTITION_NAME_LENGTH, APS_PARTITION_NAME_LENGTH, "Total",
				   total_per_bud, total_per_used);
			break;
		case pg_enhancedused:
			printf("%*.*s-----+---------------------------------+-------------------\n",
				   APS_PARTITION_NAME_LENGTH, APS_PARTITION_NAME_LENGTH, long_dash_string);
			printf("%-*.*s     |   %3d%% |%6.2f%% %6.2f%% %6.2f%% |\n",
				   APS_PARTITION_NAME_LENGTH, APS_PARTITION_NAME_LENGTH, "Total",
				   total_per_bud, total_per_used,
				   total_per_w2, total_per_w3);
			break;
			
		case pg_enhancedused_enhancedcritical:
			printf("%*.*s-----+---------------------------------+---------------------------------------\n",
				   APS_PARTITION_NAME_LENGTH, APS_PARTITION_NAME_LENGTH, long_dash_string);
			printf("%-*.*s     |   %3d%% |%6.2f%% %6.2f%% %6.2f%% |\n",
				   APS_PARTITION_NAME_LENGTH, APS_PARTITION_NAME_LENGTH, "Total",
				   total_per_bud, total_per_used,
				   total_per_w2, total_per_w3);
			break;
	}

	free(d);
	return EOK;
}


//============================================================


int
show_cmd (int argc, char **argv)
{
	int has_error = 0;
	int o;
	unsigned long number;
	char **partition_names = 0;
	int num_partition_names = 0;
	int detail_level = 1;
	int do_loop = 0;
	long delay = 50;
	char *rest;
	char *shorthand = 0;
	_Uint32t shorthand_flags = 0;
	
	optind = 1;
	while((o = getopt(argc, argv, ":d:vlf:")) != -1) {
		switch(o) {
			case 'd':
				rest = 0;
				number = strtoul(optarg, &rest, 10);
				if ((rest && rest[0] != 0) || number == ULONG_MAX) {
					fatalerr("invalid value %s for '-%c'", optarg, o);
				}
				delay = number;
				break;
				
			case 'v':			/* partition usage information */
				detail_level++;
				break;

			case 'l':
				do_loop = 1;
				break;

			case 'f':
				shorthand = optarg;
				if (keyword_to_flag(&shorthand_handle_options,
									optarg, &shorthand_flags) == 0) {
					fatalerr("invalid shorthand \"%s\"", optarg);
				}
				break;

			case ':':
				fatalerr("missing argument for '-%c'", optopt);
				break;
				
			case '?':
				fatalerr("unknown option '-%c'", optopt);
				break;
		}
	}

	if (optind < argc) {
		partition_names = &(argv[optind]);
		num_partition_names = argc-optind;
	}

	if (!shorthand_flags) {
		shorthand_flags = SHORTHAND_USAGE;
	}

	while (!has_error) {
		int add_line = 0;
		
		if (shorthand_flags & SHORTHAND_SCHEDULER) {
			if (print_scheduler() != EOK) {
				has_error = 1;
			}
			add_line = 1;
		}
		
		if (shorthand_flags & SHORTHAND_OVERALL_STATS) {
			if (add_line) printf("\n");
			if (print_overall_stats() != EOK) {
				has_error = 1;
			}
			add_line = 1;
		}

		if (shorthand_flags & SHORTHAND_PARTITIONS) {
			if (add_line) printf("\n");
			if (print_partitions(partition_names,
								 num_partition_names) != EOK) {
				has_error = 1;
			}
			add_line = 1;
		}
		
		if (shorthand_flags & SHORTHAND_USAGE) {
			if (add_line) printf("\n");
			if (print_groups_usage(detail_level,
								   partition_names,
								   num_partition_names) != EOK) {
				has_error = 1;
			}
			add_line = 1;
		}

		if (!has_error) {
			if (do_loop == 0 || delay == 0) {
				break;
			} else {
				useconds_t us = delay*100000;
				if (add_line) printf("\n");
				if (usleep(us)) {
					has_error = 1;
				} else {
					printf("\n");
				}
			}
		}
	}

	if (has_error) {
		return 1;
	}
	return 0;
}

int
create_cmd (int argc, char **argv)
{
	int has_error = 0;
	int o;
	char *rest;
	sched_aps_create_parms p; 
	int ret;
	unsigned long number;
	int opt_b_cnt = 0;

	APS_INIT_DATA(&p); 
	p.budget_percent = 0;
	p.critical_budget_ms = 0;
	p.name = 0;
	
	optind = 1;
	while ((o = getopt(argc, argv, ":b:B:")) != -1) {
		switch (o) {
			case 'b':				/* budget */
				opt_b_cnt ++;
				rest = 0;
				number = strtoul(optarg, &rest, 10);
				if ((rest && rest[0] != 0) || number == ULONG_MAX) {
					fatalerr("invalid value %s for -%c", optarg, o);
				}
				p.budget_percent = number;
				break;
			
			case 'B':				/* Critical budget (ms) */
				rest = 0;
				number = strtoul(optarg, &rest, 10);
				if ((rest && rest[0] != 0) || number == ULONG_MAX) {
					fatalerr("invalid value %s for '-%c'", optarg, o);
				}
				p.critical_budget_ms = number;
				break;

			case ':':
				fatalerr("missing argument for '-%c'", optopt);
				break;
				
			case '?':
				fatalerr("unknown option '-%c'", optopt);
				break;
		}
	}

	if (opt_b_cnt == 0) {
		fatalerr("must specify -b <budget_percent>");
	}

	if (optind != argc-1) {
		fatalerr("must specify a single partition name as operand");
	}
	
	p.name = argv[optind];
	ret = SchedCtl(SCHED_APS_CREATE_PARTITION, &p, sizeof(p));
	if (ret != EOK) {
		printerr("couldn't create partition \"%s\": %s (%d)",
				 p.name, strerror(errno), errno);
		has_error = 1;
	}

	if (has_error) {
		return 1;
	} else {
		return 0;
	}
}

/*
 * Modify either the specified partition or the scheduller.
 *
 * Note: There's been much talk about whether to use a single 'modify'
 * command, or one for partition modifications and one for scheduler
 * modifications, and no clear advantage of one approach vs the
 * other... so...
 */
int modify_cmd (int argc, char **argv)
{
	int has_error = 0;
	char *rest;
	int o;
	unsigned long number;
	
    /* Track whether there are changes to make to the partition, or
	 * the scheduler, given that both a partion and a scheduler can be
	 * changed */
	int do_change_scheduler = 0;
	int do_change_partition = 0;
	char *partition_name = 0;
	
	sched_aps_modify_parms b;	/* budget related, hence b */

	sched_aps_parms p;			/* bankruptcy, windowsize */
	_Uint32t bankruptcy_policy;
	_Uint32t scheduling_policy;
	
	sched_aps_security_parms s;	/* security */
	int do_change_security_policy = 0;
	
	APS_INIT_DATA(&b);
	b.id = -1;
	b.new_budget_percent = -1;
	b.new_critical_budget_ms = -1;

	APS_INIT_DATA(&p);
	p.bankruptcy_policyp = NULL;
	p.scheduling_policy_flagsp = NULL;
	bankruptcy_policy = 0;
	scheduling_policy = 0;
	p.windowsize_ms = -1;

	APS_INIT_DATA(&s);
	s.sec_flags = 0;

	optind=1;
	while ((o = getopt(argc, argv, ":b:B:s:S:w:y:")) != -1) {
		switch (o) {
			case 'b': 			/* budget (percent) */
			{
				rest = 0;
				do_change_partition = 1;
				number = strtoul(optarg, &rest, 10);
				if ((rest && rest[0] != 0) || number == ULONG_MAX) {
					fatalerr("invalid value %s for '-%c'", optarg, o);
				}
				b.new_budget_percent = number;
			}
			break;

			case 'B': 			/* Critical budget (ms) */
			{
				rest = 0;
				do_change_partition = 1;
				number = strtoul(optarg, &rest, 10);
				if ((rest && rest[0] != 0) || number == ULONG_MAX) {
					fatalerr("invalid value %s for '-%c'", optarg, o);
				}
				b.new_critical_budget_ms = number;
			}
			break;

			case 's': 			/* security policy */
			{
				do_change_scheduler = 1;
				do_change_security_policy = 1;
                /* Security is cumulative, so preserve current flag,
				 * and add to it */
				if (keyword_to_flag(&security_handle_options,
									optarg,
									&(s.sec_flags)) == 0) {
					fatalerr("invalid security policy \"%s\"", optarg);
				}
			}
			break;

			case 'S':			/* scheduling policy */
			{
				do_change_scheduler = 1;
				/*
				 * Scheduling policy isn't cumulative, but
				 * may be so one day. Preserver current flag,
				 * and add to it
				 */
				if (keyword_to_flag(&schedpol_handle_options,
									optarg,
									&scheduling_policy) == 0) { 
					fatalerr("invalid scheduling policy \"%s\"", optarg);
				}
				p.scheduling_policy_flagsp = &scheduling_policy;
			}
			break;

			case 'w': 			/* window size */
			{
				rest = 0;
				do_change_scheduler = 1;
				number = strtoul(optarg, &rest, 10);
				if ((rest && rest[0] != 0) || number == ULONG_MAX) {
					fatalerr("invalid value %s for '-%c'", optarg, o);
				}
				p.windowsize_ms = number;
			}
			break;

			case 'y': 			/* Bankruptcy policy */
			{
				do_change_scheduler = 1;
                /* Bankruptcy is cummulative, so preserve current flag
				 * and add to it... */
				if (keyword_to_flag(&bankruptcy_handle_options,
									optarg,
									&bankruptcy_policy) == 0) { 
					fatalerr("invalid bankruptcy policy \"%s\"", optarg);
				}
				p.bankruptcy_policyp = &bankruptcy_policy;
			}
			break;
				
			case ':':
				fatalerr("missing argument for '-%c'", optopt);
				break;
				
			case '?':
				fatalerr("unknown option '-%c'", optopt);
				break;
		}
	}
	
	if (do_change_partition) {
		if (optind != argc-1) {
			fatalerr("there should be one and only one partition name");
		}
		partition_name = argv[optind];
	}

	if (do_change_scheduler) {
		if (optind < argc) {
			/* There shouldn't be any operand */
			fatalerr("operands not allowed when changing only the scheduler");
		}
	}

	if (!do_change_partition && !do_change_scheduler) {
		fatalerr("must specify a change");
	}

	/* Scheduler related changes ... */
	if (do_change_scheduler) {
		if (do_change_security_policy) {
			sched_aps_security_parms s_saved = s;
			int ret = SchedCtl(SCHED_APS_ADD_SECURITY, &s, sizeof(s));
			if (ret != EOK) { 
				printerr("couldn't change security policy: %s (%d)",
						 strerror(errno), errno);
				has_error = 1;
			} else {
				if (((~s_saved.sec_flags)&s.sec_flags) != 0) {
					printwarning("More security policies in effect than what was specified");
				}
			}
		}

		if (p.bankruptcy_policyp || p.windowsize_ms >= 0 || p.scheduling_policy_flagsp) {
			int ret = SchedCtl(SCHED_APS_SET_PARMS, &p, sizeof(p));
			if (ret != EOK) {
				printerr("couldn't change global APS parameters: %s (%d)",
						 strerror(errno), errno);
				has_error = 1;
			}
		}
	}

	/* Partition related changes ... */
	if (do_change_partition) {
		b.id = lookup_id(partition_name);
		if (b.id == -1) {
			printerr("can't find partition \"%s\": %s (%d)",
					 partition_name, strerror(errno), errno);
			has_error = 1;
		} else {
			if (b.new_budget_percent >= 0 || b.new_critical_budget_ms >= 0) {
				int ret = SchedCtl(SCHED_APS_MODIFY_PARTITION, &b, sizeof(b));
				if (ret != EOK) {
					printerr("couldn't set budget: %s (%d)",
							 strerror(errno), errno);
					has_error = 1;
				}
			}
		}
	}
	
	if (has_error) {
		return 1;
	}

	return 0;
}

//============================================================

int
aps_sched_is_running (void)
{
	struct sched_query parms; 
	memset(&parms,0,sizeof(parms));
	if (SchedCtl(SCHED_QUERY_SCHED_EXT,
				 &parms, sizeof(parms)) == EOK) {
		if (parms.extsched == SCHED_EXT_APS) {
			return 1;
		}
	}

	return 0;
}

int
main (int argc, char *argv[])
{
	int argindex = 1;

	fullprogname = argv[0];
	if (! aps_sched_is_running()) {
		fatalerr("APS scheduler not running");
	}

	if (argc == 1) {
		char *default_argv[] = {"show"};
		return show_cmd(1, default_argv);
	} else {
		char * cmd = argv[argindex];

		if (strcmp(cmd, "show") == 0) {
			return show_cmd(argc-argindex, &(argv[argindex]));
		} else if (strcmp(cmd, "create") == 0) {
			return create_cmd(argc-argindex, &(argv[argindex]));
		} else if (strcmp(cmd, "modify") == 0) {
			return modify_cmd(argc-argindex, &(argv[argindex]));
		} else {
			fatalerr("unknown command \"%s\"", cmd);
		}
	}

	return(1);
}

__SRCVERSION("aps.c $Rev: 153052 $");
