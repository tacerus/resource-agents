/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  2002  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/
#ifndef __utils_verb_flags_h__
#define __utils_verb_flags_h__
int get_verbosity_string(char *str, size_t slen, uint32_t verb);
void set_verbosity(char *str, uint32_t *verb);
#endif /*__utils_verb_flags_h__*/
/* vim: set ai cin et sw=3 ts=3 : */
