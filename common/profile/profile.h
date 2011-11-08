/*
 * profile.h
 *
 * Copyright (C) 2005 by Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 *
 * Copyright (C) 1985-2005 by the Massachusetts Institute of Technology.
 *
 * All rights reserved.
 *
 * Export of this software from the United States of America may require
 * a specific license from the United States Government.  It is the
 * responsibility of any person or organization contemplating export to
 * obtain such a license before exporting.
 *
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of M.I.T. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  Furthermore if you modify this software you must label
 * your software as modified software and not distribute it in such a
 * fashion that it might be confused with the original MIT software.
 * M.I.T. makes no representations about the suitability of this software
 * for any purpose.  It is provided "as is" without express or implied
 * warranty.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef _PROFILE_H
#define _PROFILE_H

//#define PROFILE_DEBUG

typedef struct _profile_t *profile_t;

typedef void (*profile_syntax_err_cb_t)(const char *file, long err,
					int line_num);

/*
 * Used by the profile iterator in prof_get.c
 */
#define PROFILE_ITER_LIST_SECTION	0x0001
#define PROFILE_ITER_SECTIONS_ONLY	0x0002
#define PROFILE_ITER_RELATIONS_ONLY	0x0004

/*
 * Simplified error handling
 */
enum profile_error {
	PROF_NO_ERROR,
	PROF_VERSION,
	PROF_MAGIC_NODE,
	PROF_NO_SECTION,
	PROF_NO_RELATION,
	PROF_ADD_NOT_SECTION,
	PROF_SECTION_WITH_VALUE,
	PROF_BAD_LINK_LIST,
	PROF_BAD_GROUP_LVL,
	PROF_BAD_PARENT_PTR,
	PROF_MAGIC_ITERATOR,
	PROF_SET_SECTION_VALUE,
	PROF_EINVAL,
	PROF_READ_ONLY,
	PROF_SECTION_NOTOP,
	PROF_SECTION_SYNTAX,
	PROF_RELATION_SYNTAX,
	PROF_EXTRA_CBRACE,
	PROF_MISSING_OBRACE,
	PROF_MAGIC_PROFILE,
	PROF_MAGIC_SECTION,
	PROF_TOPSECTION_ITER_NOSUPP,
	PROF_INVALID_SECTION,
	PROF_END_OF_SECTIONS,
	PROF_BAD_NAMESET,
	PROF_NO_PROFILE,
	PROF_MAGIC_FILE,
	PROF_FAIL_OPEN,
	PROF_EXISTS,
	PROF_BAD_BOOLEAN,
	PROF_BAD_INTEGER,
	PROF_MAGIC_FILE_DATA
};

struct profile_node {
	long	magic;
	char *name;
	char *value;
	int group_level;
	unsigned int final:1;		/* Indicate don't search next file */
	unsigned int deleted:1;
	struct profile_node *first_child;
	struct profile_node *parent;
	struct profile_node *next, *prev;
};

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

long profile_open
	(const char* filename, profile_t *ret_profile);

void profile_close
	(profile_t profile);

long profile_get_string
	(profile_t profile, const char *name, const char *subname,
			const char *subsubname, const char *def_val,
			char **ret_string);

long profile_get_integer
	(profile_t profile, const char *name, const char *subname,
			const char *subsubname, int def_val,
			int *ret_default);

long profile_get_uint
	(profile_t profile, const char *name, const char *subname,
		const char *subsubname, unsigned int def_val,
		unsigned int *ret_int);

long profile_get_boolean
	(profile_t profile, const char *name, const char *subname,
			const char *subsubname, int def_val,
			int *ret_default);

long profile_iterator_create
	(profile_t profile, const char *const *names,
		   int flags, void **ret_iter);

void profile_iterator_free
	(void **iter_p);

long profile_iterator
	(void	**iter_p, char **ret_name, char **ret_value);

profile_syntax_err_cb_t profile_set_syntax_err_cb(profile_syntax_err_cb_t hook);

const char* profile_errtostr(long error_code);

#ifdef PROFILE_DEBUG
void do_cmd(profile_t profile, char **argv);
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _PROFILE_H */
