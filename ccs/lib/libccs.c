/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include "log.h" /* Libraries should not print - so only use log_dbg */
#include "debug.h"
#include "comm_headers.h"

#include <stdio.h>

static int fe_port = 50006;

/**
 * do_request: send request an receive results
 * @buffer: package to send
 *
 * This function does not interpret the contents of the package, except
 * to check the 'comm_err' field.
 *
 * Returns: 0 on success, < 0 on error
 */
static int do_request(char *buffer){
  int error=0;
  int sock;
  comm_header_t *ch = (comm_header_t *)buffer;

  int res_port = IPPORT_RESERVED-1;
  struct sockaddr_in addr;
  long int addrinuse_timo=0;
  int connrefused_timo=1;
 
  ENTER("do_request");

  sock = socket(PF_INET, SOCK_STREAM, 0);
 
  if(sock < 0){
    error = -errno;
    goto fail_no_close;
  }
 
  srandom(getpid());
  for(; res_port >= 512; res_port--){
    addr.sin_family = AF_INET;
    inet_aton("127.0.0.1", (struct in_addr *)&addr.sin_addr.s_addr);
    addr.sin_port = htons(res_port);
    if(bindresvport(sock, &addr)){
      if(errno == EADDRINUSE){
        addrinuse_timo = random();
        addrinuse_timo /= (RAND_MAX/4);
        addrinuse_timo++;
        sleep(addrinuse_timo);
        continue;
      } else if (errno == EINVAL){
	error = -ECONNREFUSED;
      } else {
	error = -errno;
      }
      log_dbg("Unable to bindresvport: %s\n", strerror(errno));
      goto fail;
    }
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(fe_port);
    error = connect(sock, (struct sockaddr *)&addr,
                    sizeof(struct sockaddr_in));
    if(error < 0){
      if(errno == ECONNREFUSED && connrefused_timo <= 8){
        /* server is probably overloaded.  Retry same port in a bit. */
	res_port++;
        sleep(connrefused_timo);
        connrefused_timo *= 2;
        continue;
      } else {
	log_dbg("Unable to connect to server: %s\n", strerror(errno));
        error = -errno;
	goto fail;
      }
    } else {
      break;
    }
  }

  if(res_port < 512){
    error = -errno;
    goto fail;
  }

  error = write(sock, buffer, sizeof(comm_header_t)+ch->comm_payload_size);
  if(error < 0){
    log_dbg("Write to socket failed.\n");
    goto fail;
  } else if(error < (sizeof(comm_header_t)+ch->comm_payload_size)){
    log_dbg("Failed to write full package to socket.\n");
    error = -EBADE;
    goto fail;
  }

  /* ok to take in two passes ? */
  error = read(sock, buffer, sizeof(comm_header_t));
  if(error < 0){
    log_dbg("Read from socket failed.\n");
    goto fail;
  } else if(error < sizeof(comm_header_t)){
    log_dbg("Failed to read complete comm_header_t.\n");
    error = -EBADE;
    goto fail;
  } else if(ch->comm_error){
    log_dbg("Server reports failure: %s\n", strerror(-ch->comm_error));
    error = ch->comm_error;
    goto fail;
  } else {
    error = 0;
  }
  if(ch->comm_payload_size){
    error = read(sock, buffer+sizeof(comm_header_t), ch->comm_payload_size);
    if(error < 0){
      log_dbg("Read from socket failed.\n");
      goto fail;
    } else if(error < ch->comm_payload_size){
      log_dbg("Failed to read complete payload.\n");
      error = -EBADE;
      goto fail;
    } else {
      error = 0;
    }
  }
 fail:
  close(sock);
 fail_no_close:
  EXIT("do_request");
  return error;
}


/**
* _ccs_connect
* @cluster_name: name of the cluster (optional)
* @flags: blocking or force
*
* This function will return a descriptor on success that should be
* used with the other functions for proper operation.
*
* Returns: >= 0 on success, < 0 on error
*/
int _ccs_connect(const char *cluster_name, int flags){
  int error = 0;
  char *buffer = NULL;
  comm_header_t *ch = NULL;
  char *payload = NULL;

  ENTER("ccs_connect");

  if(!(buffer = malloc(512))){
    error = -ENOMEM;
    goto fail;
  }    

  memset(buffer, 0, 512);
  ch = (comm_header_t *)buffer;
  payload = (buffer + sizeof(comm_header_t));

  ch->comm_type = COMM_CONNECT;
  if(flags & COMM_CONNECT_BLOCKING){
    ch->comm_flags |= COMM_CONNECT_BLOCKING;
  }

  if(flags & COMM_CONNECT_FORCE){
    ch->comm_flags |= COMM_CONNECT_FORCE;
  }

  if(cluster_name){
    ch->comm_payload_size = strlen(cluster_name)+1;
    if(ch->comm_payload_size > (512 - sizeof(comm_header_t))){
      error = -ENAMETOOLONG;
      goto fail;
    }

    strcpy(payload, cluster_name); /* already checked if it will fit */
  }

  if(!(error = do_request(buffer))){
    /* Not an error, just reusing the 'error' variable */
    error = ch->comm_desc;
  }

 fail:
  if(buffer) { free(buffer); }
  EXIT("ccs_connect");
  return error;
}


/**
 * ccs_connect
 *
 * This function will only allow a connection if the node is part of
 * a quorate cluster.
 *
 * Returns: ccs_desc on success, < 0 on failure
 */
int ccs_connect(void){
  return _ccs_connect(NULL, 0);
}


/**
 * ccs_force_connect
 *
 * This function will only allow a connection even if the node is not
 * part of a quorate cluster.  It will use the configuration file
 * located in /etc/cluster/cluster.xml.  If that file does not exist,
 * a copy of the file will be broadcasted for.  If blocking is specified,
 * the broadcasts will be retried until a config file is located.  Otherwise,
 * the fuction will return an error if the initial broadcast is not successful.
 *
 * Returns: ccs_desc on success, < 0 on failure
 */
int ccs_force_connect(const char *cluster_name, int blocking){
  if(blocking){
    return _ccs_connect(cluster_name, COMM_CONNECT_FORCE | COMM_CONNECT_BLOCKING);
  } else {
    return _ccs_connect(cluster_name, COMM_CONNECT_FORCE);
  }
}


/**
 * ccs_disconnect
 * @desc: the descriptor returned by ccs_connect
 *
 * This function frees all associated state kept with an open connection
 *
 * Returns: 0 on success, < 0 on error
 */
int ccs_disconnect(int desc){
  int error = 0;
  char *buffer = NULL;
  comm_header_t *ch = NULL;
  char *payload = NULL;

  ENTER("ccs_disconnect");

  if(!(buffer = malloc(512))){
    error = -ENOMEM;
    goto fail;
  }    

  memset(buffer, 0, 512);
  ch = (comm_header_t *)buffer;
  payload = (buffer + sizeof(comm_header_t));

  ch->comm_type = COMM_DISCONNECT;
  ch->comm_desc = desc;

  error = do_request(buffer);

 fail:
  if(buffer) { free(buffer); }

  EXIT("ccs_disconnect");
  return error;
}


/**
 * ccs_get
 * @desc:
 * @query:
 * @rtn: value returned
 *
 * This function will allocate space for the value that is the result
 * of the given query.  It is the user's responsibility to ensure that
 * the data returned is freed.
 *
 * Returns: 0 on success, < 0 on failure
 */
int ccs_get(int desc, const char *query, char **rtn){
  int error = 0;
  char *buffer = NULL;
  comm_header_t *ch = NULL;
  char *payload = NULL;

  ENTER("ccs_get");

  if(!(buffer = malloc(512))){
    error = -ENOMEM;
    goto fail;
  }    

  memset(buffer, 0, 512);
  ch = (comm_header_t *)buffer;
  payload = (buffer + sizeof(comm_header_t));

  ch->comm_type = COMM_GET;
  ch->comm_desc = desc;

  ch->comm_payload_size = sprintf(payload, "%s", query)+1;

  error = do_request(buffer);

  if(!error){
    if(ch->comm_payload_size){
      *rtn = (char *)strdup(payload);
      if(!*rtn){ error = -ENOMEM; }
    } else {
      *rtn = NULL;
    }
  }

 fail:
  if(buffer) { free(buffer); }

  EXIT("ccs_get");
  return error;
}


/**
 * ccs_set: set an individual element's value in the config file.
 * @desc:
 * @path:
 * @val:
 *
 * This function is used to update individual elements in a config file.
 * It's effects are cluster wide.  It only succeeds when the node is part
 * of a quorate cluster.
 *
 * Note currently implemented.
 * 
 * Returns: 0 on success, < 0 on failure
 */
int ccs_set(int desc, const char *path, char *val){
  ENTER(ccs_set);
  EXIT(ccs_set);
  return -ENOSYS;
}


/**
 * ccs_get_state: return the stored state of the connection
 * @desc:
 * @cw_path:
 * @prev_query:
 *
 * This function will return the current working path and the
 * previous query.  It is the user's responsibility to free
 * both returned values.
 *
 * Returns: 0 on success, < 0 on failure
 */
int ccs_get_state(int desc, char **cw_path, char **prev_query){
  int error = 0;
  char *buffer = NULL;
  comm_header_t *ch = NULL;
  char *payload = NULL;

  ENTER("ccs_get_state");

  if(!(buffer = malloc(512))){
    error = -ENOMEM;
    goto fail;
  }    

  *cw_path = *prev_query = NULL;

  memset(buffer, 0, 512);
  ch = (comm_header_t *)buffer;
  payload = (buffer + sizeof(comm_header_t));

  ch->comm_type = COMM_GET_STATE;
  ch->comm_desc = desc;

  error = do_request(buffer);
  if(!error){
    *cw_path = (char *)strdup(payload);
    if(!*cw_path){
      error = -ENOMEM;
      goto fail;
    }
    *prev_query = (char *)strdup(payload+strlen(payload)+1);
    if(!*prev_query){
      error = -ENOMEM;
      free(*cw_path);
      *cw_path = NULL;
      goto fail;
    }
  }

 fail:
  if(buffer) { free(buffer); }

  EXIT("ccs_get_state");
  return error;
}


/**
 * ccs_set_state
 * @desc:
 * @cw_path:
 * @reset_query:
 *
 * This function allows the user to specify a current working path,
 * from which all later queries will be relative.  It also allows the
 * user to erase memory of the last query - useful if the user wanted
 * to reset the index of a list to 0.
 *
 * Returns: 0 on success, < 0 on failure
 */
int ccs_set_state(int desc, const char *cw_path, int reset_query){
  int error = 0;
  char *buffer = NULL;
  comm_header_t *ch = NULL;
  char *payload = NULL;

  ENTER("ccs_set_state");

  if(!(buffer = malloc(512))){
    error = -ENOMEM;
    goto fail;
  }    

  memset(buffer, 0, 512);
  ch = (comm_header_t *)buffer;
  payload = (buffer + sizeof(comm_header_t));

  ch->comm_type = COMM_SET_STATE;
  ch->comm_desc = desc;

  if(reset_query){
    ch->comm_flags |= COMM_SET_STATE_RESET_QUERY;
  }

  if(strlen(cw_path)+1 > 512-sizeof(comm_header_t)){
    error = -ENAMETOOLONG;
    goto fail;
  }

  /* sprintf does not include trailing \0 */
  ch->comm_payload_size = sprintf(payload, "%s", cw_path) + 1;

  error = do_request(buffer);

 fail:
  if(buffer) { free(buffer); }

  EXIT("ccs_set_state");
  return error;
}
