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

#ifndef __LOCKING_DOT_H__
#define __LOCKING_DOT_H__

int gfs_mount_lockproto(struct gfs_sbd *sdp, int silent);
void gfs_unmount_lockproto(struct gfs_sbd *sdp);

#endif /* __LOCKING_DOT_H__ */
