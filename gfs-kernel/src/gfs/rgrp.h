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

#ifndef __RGRP_DOT_H__
#define __RGRP_DOT_H__

void gfs_mhc_add(struct gfs_rgrpd *rgd, struct buffer_head **bh,
			 unsigned int num);
int gfs_mhc_fish(struct gfs_sbd *sdp, struct buffer_head *bh);
void gfs_mhc_zap(struct gfs_rgrpd *rgd);

void gfs_depend_add(struct gfs_rgrpd *rgd, uint64_t formal_ino);
void gfs_depend_sync(struct gfs_rgrpd *rgd);

struct gfs_rgrpd *gfs_blk2rgrpd(struct gfs_sbd *sdp, uint64_t blk);
struct gfs_rgrpd *gfs_rgrpd_get_first(struct gfs_sbd *sdp);
struct gfs_rgrpd *gfs_rgrpd_get_next(struct gfs_rgrpd *rgd);

void gfs_clear_rgrpd(struct gfs_sbd *sdp);

int gfs_rindex_hold(struct gfs_sbd *sdp, struct gfs_holder *ri_gh);

int gfs_rgrp_read(struct gfs_rgrpd *rgd);
void gfs_rgrp_relse(struct gfs_rgrpd *rgd);

void gfs_rgrp_lvb_fill(struct gfs_rgrpd *rgd);
int gfs_rgrp_lvb_init(struct gfs_rgrpd *rgd);

struct gfs_alloc *gfs_alloc_get(struct gfs_inode *ip);
void gfs_alloc_put(struct gfs_inode *ip);

int gfs_inplace_reserve_i(struct gfs_inode *ip,
			 char *file, unsigned int line);
#define gfs_inplace_reserve(ip) \
gfs_inplace_reserve_i((ip), __FILE__, __LINE__)

void gfs_inplace_release(struct gfs_inode *ip);

unsigned char gfs_get_block_type(struct gfs_rgrpd *rgd, uint64_t block);

void gfs_blkalloc(struct gfs_inode *ip, uint64_t *block);
int gfs_metaalloc(struct gfs_inode *ip, uint64_t *block);
int gfs_dialloc(struct gfs_inode *dip, uint64_t *block);

void gfs_blkfree(struct gfs_inode *ip, uint64_t bstart, uint32_t blen);
void gfs_metafree(struct gfs_inode *ip, uint64_t bstart, uint32_t blen);
void gfs_difree_uninit(struct gfs_rgrpd *rgd, uint64_t addr);
void gfs_difree(struct gfs_rgrpd *rgd, struct gfs_inode *ip);

struct gfs_rgrp_list {
	unsigned int rl_rgrps;
	unsigned int rl_space;
	struct gfs_rgrpd **rl_rgd;
	struct gfs_holder *rl_ghs;
};

void gfs_rlist_add(struct gfs_sbd *sdp, struct gfs_rgrp_list *rlist,
		   uint64_t block);
void gfs_rlist_alloc(struct gfs_rgrp_list *rlist, unsigned int state,
		     int flags);
void gfs_rlist_free(struct gfs_rgrp_list *rlist);

int gfs_reclaim_metadata(struct gfs_sbd *sdp, struct gfs_reclaim_stats *stats);

#endif /* __RGRP_DOT_H__ */
