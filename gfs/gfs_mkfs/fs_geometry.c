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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <time.h>

#include "global.h"
#include <linux/gfs_ondisk.h>
#include "osi_list.h"

#include "mkfs_gfs.h"





/**
 * how_many_rgrps - figure out how many RG to put in a subdevice
 * @comline: the command line
 * @sdev: the subdevice
 *
 * Returns: the number of RGs
 */

static uint64 how_many_rgrps(commandline_t *comline, mkfs_subdevice_t *sdev)
{
  uint64 nrgrp;
  unsigned int min = (comline->expert) ? 1 : 4;

  nrgrp = DIV_RU(sdev->length, (comline->rgsize << 20) / comline->bsize);

  if (nrgrp < min)
    nrgrp = min;

  if (comline->debug)
    printf("  nrgrp = %"PRIu64"\n", nrgrp);

  return nrgrp;
}


/**
 * compute_rgrp_layout - figure out where the RG in a FS are
 * @comline: the command line
 * @device: the device layout
 * @rlist: the list of resource groups
 *
 * Returns: a list of rgrp_list_t structures
 */

void compute_rgrp_layout(commandline_t *comline, mkfs_device_t *device, osi_list_t *rlist)
{
  mkfs_subdevice_t *sdev;
  rgrp_list_t *rl, *rlast = NULL;
  osi_list_t *tmp;
  uint64 rgrp, nrgrp;
  unsigned int x;
  int first_sdev = TRUE;


  for (x = 0; x < device->nsubdev; x++)
  {
    sdev = &device->subdev[x];

    if (!sdev->is_journal)
    {
      /*  If this is the first subdevice reserve space for the superblock  */

      if (first_sdev)
	sdev->length -= comline->sb_addr + 1;


      if (comline->debug)
	printf("\nData Subdevice %u\n", x);

      nrgrp = how_many_rgrps(comline, sdev);


      for (rgrp = 0; rgrp < nrgrp; rgrp++)
      {
	type_zalloc(rl, rgrp_list_t, 1);

	rl->subdevice = x;

	if (rgrp)
	{
	  rl->rg_offset = rlast->rg_offset + rlast->rg_length;
	  rl->rg_length = sdev->length / nrgrp;
	}
	else
	{
	  rl->rg_offset = sdev->start;
	  rl->rg_length = sdev->length - (nrgrp - 1) * (sdev->length / nrgrp);

	  if (first_sdev)
	    rl->rg_offset += comline->sb_addr + 1;
	}

	osi_list_add_prev(&rl->list, rlist);

	rlast = rl;
      }

      first_sdev = FALSE;

      comline->rgrps += nrgrp;
    }
  }


  if (comline->debug)
  {
    printf("\n");

    for (tmp = rlist->next; tmp != rlist; tmp = tmp->next)
    {
      rl = osi_list_entry(tmp, rgrp_list_t, list);
      printf("subdevice %u:  rg_o = %"PRIu64", rg_l = %"PRIu64"\n",
	     rl->subdevice,
	     rl->rg_offset, rl->rg_length);
    }
  }
}


/**
 * compute_journal_layout - figure out where the journals in a FS are
 * @comline: the command line
 * @device: the device layout
 * @jlist: the list of journals
 *
 * Returns: a list of journal_list_t structures
 */

void compute_journal_layout(commandline_t *comline, mkfs_device_t *device, osi_list_t *jlist)
{
  mkfs_subdevice_t *sdev;
  journal_list_t *jl;
  osi_list_t *tmp;
  unsigned int x, j = 0;
  uint64 boffset, bcount;
  unsigned int min_jsize = (comline->expert) ? 1 : 32;


  for (x = 0; x < device->nsubdev; x++)
  {
    sdev = &device->subdev[x];

    if (sdev->is_journal)
    {
      type_zalloc(jl, journal_list_t, 1);

      /*  Align the journals on seg_size boundries  */

      boffset = sdev->start;
      bcount = sdev->length;

      if ((bcount + comline->seg_size) * comline->bsize < min_jsize << 20)
	die("journal %d is too small (minimum size is %u MB)\n", j, min_jsize);

      if (boffset % comline->seg_size)
      {
	bcount -= comline->seg_size - (boffset % comline->seg_size);
	boffset += comline->seg_size - (boffset % comline->seg_size);
      }

      jl->start = boffset;
      jl->segments = bcount / comline->seg_size;

      osi_list_add_prev(&jl->list, jlist);

      j++;
    }
  }


  if (comline->debug)
  {
    printf("\n");

    for (tmp = jlist->next, j = 0; tmp != jlist; tmp = tmp->next, j++)
    {
      jl = osi_list_entry(tmp, journal_list_t, list);
      printf("journal %u:  start = %"PRIu64", segments = %u\n",
	     j, jl->start, jl->segments);
    }
  }
}

