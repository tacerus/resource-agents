/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**  
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __FD_DOT_H__
#define __FD_DOT_H__

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <syslog.h>

#include "cnxman-socket.h"
#include "list.h"


extern char *prog_name;

#define MAX_NAME_LEN	33

#define die(fmt, args...) \
do \
{ \
  fprintf(stderr, "%s: ", prog_name); \
  fprintf(stderr, fmt "\n", ##args); \
  syslog(LOG_ERR, fmt, ##args); \
  exit(EXIT_FAILURE); \
} \
while (0)

#define FENCE_ASSERT(x, todo) \
do \
{ \
  if (!(x)) \
  { \
    {todo} \
    die("assertion failed on line %d of file %s\n", __LINE__, __FILE__); \
  } \
} \
while (0)

#define FENCE_RETRY(do_this, until_this) \
for (;;) \
{ \
  do { do_this; } while (0); \
  if (until_this) \
    break; \
  fprintf(stderr, "fenced:  out of memory:  %s, %u\n", __FILE__, __LINE__); \
  sleep(1); \
}

/* log_debug messages only appear when -D is used and then they go to stdout */
#define log_debug(fmt, args...) printf("fenced: " fmt "\n", ##args)



struct fd;
struct fd_node;
struct commandline;

typedef struct fd fd_t;
typedef struct fd_node fd_node_t;
typedef struct commandline commandline_t;

struct commandline
{
	char name[MAX_NAME_LEN];
	int debug;
};

#define FDFL_RUN        (0)
#define FDFL_START      (1)
#define FDFL_FINISH     (2)

struct fd {
	int			cl_sock;
	uint32_t 		our_nodeid;
	uint32_t 		local_id;	/* local unique fd ID */
	uint32_t 		global_id;	/* global unique fd ID */

	int 			last_stop;
	int 			last_start;
	int 			last_finish;

	int			first_recovery;
	int 			prev_count;
	struct list_head 	prev;
	struct list_head 	victims;
	struct list_head 	leaving;
	struct list_head	complete;

	int 			namelen;
	char 			name[1];
};

struct fd_node {
	struct list_head 	list;
	uint32_t 		nodeid;
	int 			namelen;
	char 			name[1];
};


void add_complete_node(fd_t *fd, uint32_t nodeid, uint32_t len, char *name);
void do_recovery(fd_t *fd, struct cl_service_event *ev,
		 struct cl_cluster_node *cl_nodes);
void do_recovery_done(fd_t *fd);
int dispatch_fence_agent(char *victim);

#endif				/*  __FD_DOT_H__  */
