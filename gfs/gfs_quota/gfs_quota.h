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

#ifndef __GFS_QUOTA_DOT_H__
#define __GFS_QUOTA_DOT_H__



/*  Extern Macro  */

#ifndef EXTERN
#define EXTERN extern
#define INIT(X)
#else
#undef EXTERN
#define EXTERN
#define INIT(X) =X 
#endif



#define die(fmt, args...) \
{ \
  fprintf(stderr, "%s: ", prog_name); \
  fprintf(stderr, fmt, ##args); \
  exit(EXIT_FAILURE); \
}

#define type_zalloc(ptr, type, count) \
{ \
  (ptr) = (type *)malloc(sizeof(type) * (count)); \
  if ((ptr)) \
    memset((char *)(ptr), 0, sizeof(type) * (count)); \
  else \
    die("unable to allocate memory on line %d of file %s\n", \
	__LINE__, __FILE__); \
}

#define type_alloc(ptr, type, count) \
{ \
  (ptr) = (type *)malloc(sizeof(type) * (count)); \
  if (!(ptr)) \
    die("unable to allocate memory on line %d of file %s\n", \
	__LINE__, __FILE__); \
}



#define GQ_OP_LIST           (12)
#define GQ_OP_SYNC           (13)
#define GQ_OP_GET            (14)
#define GQ_OP_LIMIT          (15)
#define GQ_OP_WARN           (16)
#define GQ_OP_CHECK          (17)
#define GQ_OP_INIT           (18)

#define GQ_ID_USER           (23)
#define GQ_ID_GROUP          (24)

#define GQ_UNITS_MEGABYTE    (0)
#define GQ_UNITS_KILOBYTE    (34)
#define GQ_UNITS_FSBLOCK     (35)
#define GQ_UNITS_BASICBLOCK  (36)



struct commandline
{
  unsigned int operation;

  uint64 new_value;
  int new_value_set;

  unsigned int id_type;
  uint32 id;

  unsigned int units;

  int no_hidden_file_blocks;
  int numbers;

  char filesystem[PATH_MAX];
};
typedef struct commandline commandline_t;



EXTERN char *prog_name;



/*  main.c  */

void check_for_gfs(int fd, char *path);
void do_sync(commandline_t *comline);
uint64 compute_hidden_blocks(commandline_t *comline, int fd);


/*  check.c  */

void do_check(commandline_t *comline);
void do_init(commandline_t *comline);



/*  names.c  */

uint32 name_to_id(int user, char *name, int numbers);
char *id_to_name(int user, uint32 id, int numbers);



#endif  /*  __GFS_QUOTA_DOT_H__  */

