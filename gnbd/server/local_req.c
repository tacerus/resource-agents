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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <strings.h>
#include <errno.h>
#include <syslog.h>
#include <signal.h>
#include <inttypes.h>

#include "list.h"
#include "gnbd_utils.h"
#include "local_req.h"
#include "device.h"
#include "gserv.h"
#include "trans.h"

int start_local_socket(void){
  int sock;
  struct sockaddr_un addr;
  struct stat stat_buf;
  
  if( (sock = socket(PF_UNIX, SOCK_STREAM, 0)) < 0)
    fail_startup("cannot create unix socket : %s\n", strerror(errno));

  /* FIXME -- I should take the name out of this function, and put it
     someplace that is user defineable */
  addr.sun_family = AF_UNIX;
  snprintf(addr.sun_path, 108, "%s/gnbd_servcomm", program_dir);
  
  if (stat(addr.sun_path, &stat_buf) < 0){
    if (errno != ENOENT)
      fail_startup("cannot stat unix socket file '%s' : %s\n", addr.sun_path,
                   strerror(errno));
  }
  else if (unlink(addr.sun_path) < 0)
    fail_startup("cannot remove unix socket file '%s' : %s\n", addr.sun_path,
                 strerror(errno));

  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    fail_startup("cannot bind unix socket to /var/run/gnbd_servcomm : %s\n",
                 strerror(errno));

  if (chmod(addr.sun_path, S_IRUSR | S_IWUSR) < 0)
    fail_startup("cannot set the file permissions on the unix socket : %s\n",
                 strerror(errno));

  if (listen(sock, 1) < 0)
    fail_startup("cannot listen on unix socket : %s\n", strerror(errno));
  
  return sock;
}

int accept_local_connection(int listening_sock)
{
  int sock;
  struct sockaddr_un addr;
  socklen_t len = sizeof(addr);

  sock = accept(listening_sock, (struct sockaddr *)&addr, &len);
  if (sock < 0){
    log_err("error accepting connect to unix socket : %s\n", strerror(errno));
    return -1;
  }

  return sock;
}

#define DO_TRANS(action, label)\
do {\
  if ((action)){\
    log_err("local transfer failed at line %d : %s\n", \
            __LINE__, strerror(errno));\
    goto label;\
  }\
} while(0)

int check_local_data_len(uint32_t req, int size)
{
  switch(req){
  case LOCAL_CREATE_REQ:
    return (size >= sizeof(info_req_t));
  case LOCAL_REMOVE_REQ:
  case LOCAL_INVALIDATE_REQ:
    return (size >= sizeof(name_req_t));
  case LOCAL_FULL_LIST_REQ:
  case LOCAL_GSERV_LIST_REQ:
  case LOCAL_SHUTDOWN_REQ:
  case LOCAL_VALIDATE_REQ:
    return 1;
  default:
    log_err("unknown local request: %u. closing connection.\n",
            (unsigned int)req);
    return -1;
  }
}

void handle_local_request(int sock, uint32_t cmd, void *buf)
{
  int err;
  uint32_t reply = LOCAL_SUCCESS_REPLY;
  
  /* FIXME -- the command should be text */
  log_verbose("got local command 0x%x\n", (unsigned int)cmd);

  switch(cmd){
  case LOCAL_CREATE_REQ:
    {
      info_req_t *create_req = (info_req_t *)buf;
      
      create_req->name[31] = 0;
      create_req->path[255] = 0;
      err = create_device(create_req->name, create_req->path,
                          (unsigned int)create_req->timeout,
                          (unsigned int)create_req->flags);
      if (err < 0)
        reply = -err;
      DO_TRANS(retry_write(sock, &reply, sizeof(reply)), exit);
      break;
    }
  case LOCAL_REMOVE_REQ:
    {
      name_req_t *remove_req = (name_req_t *)buf;
      err = remove_device(remove_req->name);
      if (err < 0)
        reply = -err;
      DO_TRANS(retry_write(sock, &reply, sizeof(reply)), exit);
      break;
    }
  case LOCAL_INVALIDATE_REQ:
    {
      name_req_t *remove_req = (name_req_t *)buf;
      /* This goes on a waiter list */
      err = invalidate_device(remove_req->name, sock);
      if (err < 0){
        reply = -err;
        DO_TRANS(retry_write(sock, &reply, sizeof(reply)), exit);
        break;
      }
      return;
    }
  case LOCAL_FULL_LIST_REQ:
    {
      char *buffer = NULL;
      uint32_t size;

      err = get_dev_info(&buffer, &size);
      if (err < 0){
        reply = -err;
        DO_TRANS(retry_write(sock, &reply, sizeof(reply)), exit);
        break;
      }
      DO_TRANS(retry_write(sock, &reply, sizeof(reply)), list_exit);
      DO_TRANS(retry_write(sock, &size, sizeof(size)), list_exit);
      if (size)
        DO_TRANS(retry_write(sock, buffer, size), list_exit);
      
    list_exit:
      free(buffer);
      break;
    }
    /* FIXME -- I need to look at which server processes are serving who*/
  case LOCAL_GSERV_LIST_REQ:
    {
      char *buffer = NULL;
      uint32_t size;
      
      err = get_gserv_info(&buffer, &size);
      if (err < 0){
        reply = -err;
        DO_TRANS(retry_write(sock, &reply, sizeof(reply)), exit);
        break;
      }
      DO_TRANS(retry_write(sock, &reply, sizeof(reply)), gserv_exit);
      DO_TRANS(retry_write(sock, &size, sizeof(size)), gserv_exit);
      if (size)
        DO_TRANS(retry_write(sock, buffer, size), gserv_exit);

    gserv_exit:
      free(buffer);
      break;
    }
  case LOCAL_SHUTDOWN_REQ:
    if (have_devices()){
      reply = EBUSY;
      DO_TRANS(retry_write(sock, &reply, sizeof(reply)), exit);
      break;
    }
    DO_TRANS(retry_write(sock, &reply, sizeof(reply)), exit);
    raise(SIGTERM);
    break;
  case LOCAL_VALIDATE_REQ:
    validate_gservs();
    DO_TRANS(retry_write(sock, &reply, sizeof(reply)), exit);
    break;
  default:
    log_err("unknown local request 0x%x\n", cmd);
    reply = ENOTTY;
    DO_TRANS(retry_write(sock, &reply, sizeof(reply)), exit);
  }
 exit:
  close(sock);
}
