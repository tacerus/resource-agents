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

#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/ctype.h>
#include <linux/seq_file.h>
#include <linux/module.h>

#include "dlm_internal.h"
#include "lockspace.h"

#if defined(DLM_DEBUG)
#define DLM_DEBUG_SIZE		(1024)
#define MAX_DEBUG_MSG_LEN	(64)
#else
#define DLM_DEBUG_SIZE		(0)
#define MAX_DEBUG_MSG_LEN	(0)
#endif

static char *			debug_buf;
static unsigned int		debug_size;
static unsigned int		debug_point;
static int			debug_wrap;
static spinlock_t		debug_lock;
static struct proc_dir_entry *	debug_proc_entry = NULL;
static struct proc_dir_entry *	rcom_proc_entry = NULL;

#ifdef CONFIG_CLUSTER_DLM_PROCLOCKS
static struct proc_dir_entry *	locks_proc_entry = NULL;
static struct seq_operations	locks_info_op;
static char			proc_ls_name[255] = "";


static int locks_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &locks_info_op);
}

/* Write simply sets the lockspace to use */
static ssize_t locks_write(struct file *file, const char *buf,
			   size_t count, loff_t * ppos)
{
	if (count < sizeof(proc_ls_name)) {
		copy_from_user(proc_ls_name, buf, count);
		proc_ls_name[count] = '\0';

		/* Remove any trailing LF so that lazy users
		   can just echo "lsname" > /proc/cluster/dlm_locks */
		if (proc_ls_name[count - 1] == '\n')
			proc_ls_name[count - 1] = '\0';

		return count;
	}
	return 0;
}

static struct file_operations locks_fops = {
      open:locks_open,
      write:locks_write,
      read:seq_read,
      llseek:seq_lseek,
      release:seq_release,
};

struct ls_dumpinfo {
	int entry;
	struct list_head *next;
	gd_ls_t *ls;
	gd_res_t *rsb;
};

static int print_resource(gd_res_t * res, struct seq_file *s);

static struct ls_dumpinfo *next_rsb(struct ls_dumpinfo *di)
{
	read_lock(&di->ls->ls_reshash_lock);
	if (!di->next) {
		/* Find the next non-empty hash bucket */
		while (list_empty(&di->ls->ls_reshashtbl[di->entry]) &&
		       di->entry < di->ls->ls_hashsize) {
			di->entry++;
		}
		if (di->entry >= di->ls->ls_hashsize) {
			read_unlock(&di->ls->ls_reshash_lock);
			return NULL;	/* End of hash list */
		}

		di->next = di->ls->ls_reshashtbl[di->entry].next;
	} else {		/* Find the next entry in the list */

		di->next = di->next->next;
		if (di->next->next == di->ls->ls_reshashtbl[di->entry].next) {
			/* End of list - move to next bucket */
			di->next = NULL;
			di->entry++;
			read_unlock(&di->ls->ls_reshash_lock);

			return next_rsb(di);	/* do the top half of this conditional */
		}
	}
	di->rsb = list_entry(di->next, gd_res_t, res_hashchain);
	read_unlock(&di->ls->ls_reshash_lock);

	return di;
}

static void *s_start(struct seq_file *m, loff_t * pos)
{
	struct ls_dumpinfo *di;
	gd_ls_t *ls;
	int i;

	ls = find_lockspace_by_name(proc_ls_name, strlen(proc_ls_name));
	if (!ls)
		return NULL;

	di = kmalloc(sizeof(struct ls_dumpinfo), GFP_KERNEL);
	if (!di)
		return NULL;

	if (*pos == 0)
		seq_printf(m, "DLM lockspace '%s'\n", proc_ls_name);

	di->entry = 0;
	di->next = NULL;
	di->ls = ls;

	for (i = 0; i < *pos; i++)
		if (next_rsb(di) == NULL)
			return NULL;

	return next_rsb(di);
}

static void *s_next(struct seq_file *m, void *p, loff_t * pos)
{
	struct ls_dumpinfo *di = p;

	*pos += 1;

	return next_rsb(di);
}

static int s_show(struct seq_file *m, void *p)
{
	struct ls_dumpinfo *di = p;
	return print_resource(di->rsb, m);
}

static void s_stop(struct seq_file *m, void *p)
{
	kfree(p);
}

static struct seq_operations locks_info_op = {
      start:s_start,
      next:s_next,
      stop:s_stop,
      show:s_show
};

static char *print_lockmode(int mode)
{
	switch (mode) {
	case DLM_LOCK_IV:
		return "--";
	case DLM_LOCK_NL:
		return "NL";
	case DLM_LOCK_CR:
		return "CR";
	case DLM_LOCK_CW:
		return "CW";
	case DLM_LOCK_PR:
		return "PR";
	case DLM_LOCK_PW:
		return "PW";
	case DLM_LOCK_EX:
		return "EX";
	default:
		return "??";
	}
}

static void print_lock(struct seq_file *s, gd_lkb_t * lkb, gd_res_t * res)
{

	seq_printf(s, "%08x %s", lkb->lkb_id, print_lockmode(lkb->lkb_grmode));

	if (lkb->lkb_status == GDLM_LKSTS_CONVERT
	    || lkb->lkb_status == GDLM_LKSTS_WAITING)
		seq_printf(s, " (%s)", print_lockmode(lkb->lkb_rqmode));

	if (lkb->lkb_range) {
		/* This warns on Alpha. Tough. Only I see it */
		if (lkb->lkb_status == GDLM_LKSTS_CONVERT
		    || lkb->lkb_status == GDLM_LKSTS_GRANTED)
			seq_printf(s, " %" PRIx64 "-%" PRIx64,
				   lkb->lkb_range[GR_RANGE_START],
				   lkb->lkb_range[GR_RANGE_END]);
		if (lkb->lkb_status == GDLM_LKSTS_CONVERT
		    || lkb->lkb_status == GDLM_LKSTS_WAITING)
			seq_printf(s, " (%" PRIx64 "-%" PRIx64 ")",
				   lkb->lkb_range[RQ_RANGE_START],
				   lkb->lkb_range[RQ_RANGE_END]);
	}

	if (lkb->lkb_nodeid) {
		if (lkb->lkb_nodeid != res->res_nodeid)
			seq_printf(s, " Remote: %3d %08x", lkb->lkb_nodeid,
				   lkb->lkb_remid);
		else
			seq_printf(s, " Master:     %08x", lkb->lkb_remid);
	}

	if (lkb->lkb_status != GDLM_LKSTS_GRANTED)
		seq_printf(s, "  LQ: %d", lkb->lkb_lockqueue_state);

	seq_printf(s, "\n");
}

static int print_resource(gd_res_t *res, struct seq_file *s)
{
	int i;
	struct list_head *locklist;

	seq_printf(s, "\nResource %p (parent %p). Name (len=%d) \"", res,
		   res->res_parent, res->res_length);
	for (i = 0; i < res->res_length; i++) {
		if (isprint(res->res_name[i]))
			seq_printf(s, "%c", res->res_name[i]);
		else
			seq_printf(s, "%c", '.');
	}
	if (res->res_nodeid)
		seq_printf(s, "\"  \nLocal Copy, Master is node %d\n",
			   res->res_nodeid);
	else
		seq_printf(s, "\"  \nMaster Copy\n");

	/* Print the LVB: */
	if (res->res_lvbptr) {
		seq_printf(s, "LVB: ");
		for (i = 0; i < DLM_LVB_LEN; i++) {
			if (i == DLM_LVB_LEN / 2)
				seq_printf(s, "\n     ");
			seq_printf(s, "%02x ",
				   (unsigned char) res->res_lvbptr[i]);
		}
		seq_printf(s, "\n");
	}

	/* Print the locks attached to this resource */
	seq_printf(s, "Granted Queue\n");
	list_for_each(locklist, &res->res_grantqueue) {
		gd_lkb_t *this_lkb =
		    list_entry(locklist, gd_lkb_t, lkb_statequeue);
		print_lock(s, this_lkb, res);
	}

	seq_printf(s, "Conversion Queue\n");
	list_for_each(locklist, &res->res_convertqueue) {
		gd_lkb_t *this_lkb =
		    list_entry(locklist, gd_lkb_t, lkb_statequeue);
		print_lock(s, this_lkb, res);
	}

	seq_printf(s, "Waiting Queue\n");
	list_for_each(locklist, &res->res_waitqueue) {
		gd_lkb_t *this_lkb =
		    list_entry(locklist, gd_lkb_t, lkb_statequeue);
		print_lock(s, this_lkb, res);
	}
	return 0;
}
#endif				/* CONFIG_CLUSTER_DLM_PROCLOCKS */

void dlm_debug_log(gd_ls_t *ls, const char *fmt, ...)
{
	va_list va;
	int i, n, size, len;
	char buf[MAX_DEBUG_MSG_LEN+1];

	spin_lock(&debug_lock);

	if (!debug_buf)
		goto out;

	size = MAX_DEBUG_MSG_LEN;
	memset(buf, 0, size+1);

	n = snprintf(buf, size, "%s ", ls->ls_name);
	size -= n;

	va_start(va, fmt);
	vsnprintf(buf+n, size, fmt, va);
	va_end(va);

	len = strlen(buf);
	if (len > MAX_DEBUG_MSG_LEN-1)
		len = MAX_DEBUG_MSG_LEN-1;
	buf[len] = '\n';
	buf[len+1] = '\0';

	for (i = 0; i < strlen(buf); i++) {
		debug_buf[debug_point++] = buf[i];

		if (debug_point == debug_size) {
			debug_point = 0;
			debug_wrap = 1;
		}
	}
 out:
	spin_unlock(&debug_lock);
}

void dlm_debug_dump(void)
{
	int i;

	spin_lock(&debug_lock);
	if (debug_wrap) {
		for (i = debug_point; i < debug_size; i++)
			printk("%c", debug_buf[i]);
	}
	for (i = 0; i < debug_point; i++)
		printk("%c", debug_buf[i]);
	spin_unlock(&debug_lock);
}

void dlm_debug_setup(int size)
{
	char *b = NULL;

	if (size > PAGE_SIZE)
		size = PAGE_SIZE;
	if (size)
		b = kmalloc(size, GFP_KERNEL);

	spin_lock(&debug_lock);
	if (debug_buf)
		kfree(debug_buf);
	if (!size || !b)
		goto out;
	debug_size = size;
	debug_point = 0;
	debug_wrap = 0;
	debug_buf = b;
	memset(debug_buf, 0, debug_size);
 out:
        spin_unlock(&debug_lock);
}

static void dlm_debug_init(void)
{
	debug_buf = NULL;
        debug_size = 0;
	debug_point = 0;
	debug_wrap = 0;
	spin_lock_init(&debug_lock);

	dlm_debug_setup(DLM_DEBUG_SIZE);
}

#ifdef CONFIG_PROC_FS
int dlm_debug_info(char *b, char **start, off_t offset, int length)
{
	int i, n = 0;

	spin_lock(&debug_lock);

	if (debug_wrap) {
		for (i = debug_point; i < debug_size; i++)
			n += sprintf(b + n, "%c", debug_buf[i]);
	}
	for (i = 0; i < debug_point; i++)
		n += sprintf(b + n, "%c", debug_buf[i]);

	spin_unlock(&debug_lock);

	return n;
}

int dlm_rcom_info(char *b, char **start, off_t offset, int length)
{
	gd_ls_t *ls;
	gd_csb_t *csb;
	int n = 0;

	ls = find_lockspace_by_name(proc_ls_name, strlen(proc_ls_name));
	if (!ls)
		return 0;

	n += sprintf(b + n, "nodeid names_send_count names_send_msgid "
				   "names_recv_count names_recv_msgid "
				   "locks_send_count locks_send_msgid "
				   "locks_recv_count locks_recv_msgid\n");

	list_for_each_entry(csb, &ls->ls_nodes, csb_list) {
		n += sprintf(b + n, "%u %u %u %u %u %u %u %u %u\n",
			     csb->csb_node->gn_nodeid,
			     csb->csb_names_send_count,
                	     csb->csb_names_send_msgid,
                	     csb->csb_names_recv_count,
                	     csb->csb_names_recv_msgid,
                	     csb->csb_locks_send_count,
                	     csb->csb_locks_send_msgid,
                	     csb->csb_locks_recv_count,
                	     csb->csb_locks_recv_msgid);
        }
	return n;
}
#endif

void dlm_proc_init(void)
{
#ifdef CONFIG_PROC_FS
	debug_proc_entry = create_proc_entry("cluster/dlm_debug", S_IRUGO,
					     NULL);
	if (!debug_proc_entry)
		return;

	debug_proc_entry->get_info = &dlm_debug_info;

	rcom_proc_entry = create_proc_entry("cluster/dlm_rcom", S_IRUGO, NULL);
	if (!rcom_proc_entry)
		return;

	rcom_proc_entry->get_info = &dlm_rcom_info;
#endif
	dlm_debug_init();

#ifdef CONFIG_CLUSTER_DLM_PROCLOCKS
	locks_proc_entry = create_proc_read_entry("cluster/dlm_locks",
						  S_IFREG | 0400,
						  NULL, NULL, NULL);
	if (!locks_proc_entry)
		return;
	locks_proc_entry->proc_fops = &locks_fops;
#endif
}

void dlm_proc_exit(void)
{
#ifdef CONFIG_PROC_FS
	if (debug_proc_entry) {
		remove_proc_entry("cluster/dlm_debug", NULL);
		dlm_debug_setup(0);
	}

	if (rcom_proc_entry)
		remove_proc_entry("cluster/dlm_rcom", NULL);
#endif

#ifdef CONFIG_CLUSTER_DLM_PROCLOCKS
	if (locks_proc_entry)
		remove_proc_entry("cluster/dlm_locks", NULL);
#endif
}
