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
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

#include "gnbd_endian.h"
#include "gnbd_utils.h"


pid_t program_pid;
char *program_name;
verbosity_level verbosity = NORMAL;
char *program_dir = "/var/run/gnbd";
int daemon_status;
char ip_str[16];

char *beip_to_str(ip_t ip)
{
  int i;
  char *p;

  p = ip_str;
  ip = beip_to_cpu(ip);

  for (i = 3; i >= 0; i--)
  {
    p += sprintf(p, "%d", (ip >> (8 * i)) & 0xFF);
    if (i > 0)
      *(p++) = '.';
  }
  return ip_str;
}

static void sig_usr1(int sig)
{
  daemon_status = 0;
}

static void sig_usr2(int sig)
{
  daemon_status = 1;
}

/*FIXME -- does this belong here */
int check_lock(char *file, int *pid){
  int fd;
  char path[1024];
  struct flock lock;

  snprintf(path, 1024, "%s/%s", program_dir, file);
  
  if( (fd = open(path, O_RDWR)) < 0){
    if (errno != ENOENT){
      printe("cannot open lockfile %s : %s\n", path, strerror(errno));
      exit(1);
    }
    return 0;
  }
  lock.l_type = F_WRLCK;
  lock.l_start = 0;
  lock.l_whence = SEEK_SET;
  lock.l_len = 0;

  if (fcntl(fd, F_GETLK, &lock) < 0){
    printe("cannot check for locks on %s : %s\n", path, strerror(errno));
    exit(1);
  }

  if (pid && lock.l_type != F_UNLCK){
    char pid_str[13];
    int count = 0;
    int bytes;
    
    memset(pid_str, 0, 13);
    while( (bytes = read(fd, &pid_str[count], 12 - count)) != 0){
      if (bytes <= 0 && errno != -EINTR){
        printe("cannot read from lockfile %s : %s\n", path, strerror(errno));
        exit(1);
      }
      if (bytes > 0)
        count += bytes;
    }
    if (sscanf(pid_str, "%d\n", pid) != 1){
      printe("invalid pid in lockfile %s", path);
      exit(1);
    }
  }
  
  close(fd);

  if (lock.l_type == F_UNLCK)
    return 0;
  return 1;
}

/**
 * pid_lock - there can be only one.
 * returns 1 - you locked the file
 *         0 - another process is already running
 */
int pid_lock(char *extra_info)
{
   struct flock lock;
   char pid_str[12], path[1024];
   int fd, val;
   
   if (strncmp(program_dir, "/var/run/gnbd", 13) == 0){
     struct stat stat_buf;
     
     if (stat("/var/run/gnbd", &stat_buf) < 0){
       if (errno != ENOENT)
         fail_startup("cannot stat lockfile dir : %s\n", strerror(errno));
       if(mkdir("/var/run/gnbd", S_IRWXU))
         fail_startup("cannot create lockfile directory : %s\n",
                      strerror(errno));
     }
     else if(!S_ISDIR(stat_buf.st_mode))
       fail_startup("/var/run/gnbd is not a directory.\n"
                   "Cannot create lockfile.\n");
   }

   snprintf(path, 1024, "%s/%s%s.pid", program_dir, program_name, extra_info);

   if( (fd = open(path, O_WRONLY | O_CREAT,
                  (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH))) < 0)
     fail_startup("cannot open lockfile '%s' : %s\n", path, strerror(errno));

   lock.l_type = F_WRLCK;
   lock.l_start = 0;
   lock.l_whence = SEEK_SET;
   lock.l_len = 0;

   if (fcntl(fd, F_SETLK, &lock) < 0) {
     if (errno == EACCES || errno == EAGAIN){
       close(fd);
       return 0;
     }
     else
       fail_startup("cannot lock lockfile : %s\n", strerror(errno));
   }
   
   if (ftruncate(fd, 0) < 0)
      fail_startup("cannot truncate lockfile : %s\n", strerror(errno));

   snprintf(pid_str, 12, "%d\n", getpid());
   if (write(fd, pid_str, strlen(pid_str)) != strlen(pid_str))
      fail_startup("error writing to '%s' : %s\n", path, strerror(errno));

   if ((val = fcntl(fd, F_GETFD, 0)) < 0)
      fail_startup("cannot read close-on-exec flag : %s\n", strerror(errno));

   val |= FD_CLOEXEC;
   if (fcntl(fd, F_SETFD, val) < 0)
      fail_startup("cannot set close-on-exec flag : %s\n", strerror(errno));
   return 1;
}

/* This was gleaned from lock_gulmd */
int daemonize(void)
{
  int pid, i;

  if( (pid = fork()) < 0){
    printe("Failed first fork: %s\n", strerror(errno));
    return -1;
  }
  else if (pid != 0){
    return pid;
  }

  setsid();

  if ( (pid = fork()) < 0){
    printe("Failed second fork: %s\n", strerror(errno));
    exit(1);
  }
  else if (pid != 0)
    exit(0);

  chdir("/");
  umask(0);
  /* leave default fds open until startup is completed */
  for(i = open_max()-1; i > 2; --i)
    close(i);  
  openlog(program_name, LOG_PID, LOG_DAEMON);
  return 0;
}

void daemonize_and_exit_parent(void)
{
  int i_am_parent;
  struct sigaction act;

  program_pid = getpid();
  memset(&act,0,sizeof(act));
  act.sa_handler = sig_usr1;
  if (sigaction(SIGUSR1, &act, NULL) < 0){
    printe("cannot set a handler for SIGUSR1 : %s\n", strerror(errno));
    exit(1);
  }
  memset(&act,0,sizeof(act));
  act.sa_handler = sig_usr2;
  if (sigaction(SIGUSR2, &act, NULL) < 0){
    printe("cannot set a handler for SIGUSR2 : %s\n", strerror(errno));
    exit(1);
  }
  daemon_status = -1;
  i_am_parent = daemonize();
  if (i_am_parent < 0)
    exit(1);
  if (i_am_parent){
    while(daemon_status == -1)
      sleep(10);
    exit(daemon_status);
  }
  memset(&act,0,sizeof(act));
  act.sa_handler = SIG_DFL;
  if (sigaction(SIGUSR1, &act, NULL) < 0)
    fail_startup("cannot set default handler for SIGUSR1 : %s\n",
                 strerror(errno));
  memset(&act,0,sizeof(act));
  act.sa_handler = SIG_DFL;
  if (sigaction(SIGUSR2, &act, NULL) < 0)
    fail_startup("cannot set default handler for SIGUSR2 : %s\n",
                 strerror(errno));
}


#ifdef OPEN_MAX
static int openmax = OPEN_MAX;
#else
static int openmax = 0;
#endif /* OPEN_MAX */

#define OM_GUESS 256
/**
 * open_max - clacs max number of open files.
 * Returns: the maximum number of file we can have open at a time.
 *          Or -1 for error.
 */
int open_max(void)
{
   if(openmax == 0) {
      errno = 0;
      if((openmax = sysconf(_SC_OPEN_MAX)) < 0) {
         if( errno == 0) {
            openmax = OM_GUESS;
         }else{
            return -1;
         }
      }
   }
   return openmax;
}
