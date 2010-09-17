/*
 * embedder : converts binary resources into a .h include
 * "If you can think of a better way to get ice, I'd like to hear it."
 * Copyright (c) 2010 Pete Batard <pbatard@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/*
 * This tool is meant to be call as a pre-built event when compiling the
 * installer library, to produce a .h that embeds all the necessary driver
 * binary resources.
 *
 * This is required work around the many limitations of resource files, as
 * well as the impossibility to force the MS linker to embed resources always
 * with a static library (unless the library is split into .res + .lib)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#endif

#include <config.h>
#include "embedder.h"
#include "embedder_files.h"

#define safe_free(p) do {if (p != NULL) {free(p); p = NULL;}} while(0)

const int nb_embeddables_fixed = sizeof(embeddable_fixed)/sizeof(struct emb);
int nb_embeddables;
struct emb* embeddable = embeddable_fixed;
#if defined(USER_DIR)
char initial_dir[] = USER_DIR;
#endif

#ifndef MAX_PATH
#ifdef PATH_MAX
#define MAX_PATH PATH_MAX
#else
#define MAX_PATH 260
#endif
#endif

#if defined(_WIN32)
#define NATIVE_STAT				_stat
#define NATIVE_STRDUP			_strdup
#define NATIVE_UNLINK			_unlink
#define NATIVE_SEPARATOR		'\\'
#define NON_NATIVE_SEPARATOR	'/'
#else
#define NATIVE_STAT				stat
#define NATIVE_STRDUP			strdup
#define NATIVE_UNLINK			unlink
#define NATIVE_SEPARATOR		'/'
#define NON_NATIVE_SEPARATOR	'\\'
#if defined(USER_DIR)
static char cwd[MAX_PATH];
#endif
#endif

void dump_buffer_hex(FILE* fd, unsigned char *buffer, size_t size)
{
	size_t i;

	// Make sure we output something even if the original file is empty
	if (size == 0) {
		fprintf(fd, "0x00");
	}

	for (i=0; i<size; i++) {
		if (!(i%0x10))
			fprintf(fd, "\n\t");
		fprintf(fd, "0x%02X,", buffer[i]);
	}
	fprintf(fd, "\n");
}

int get_full_path(char* src, char* dst)
{
#if defined(_WIN32)
	DWORD r;
#else
	char *dn, *bn;
#endif
	if ((src == NULL) || (dst == NULL)) {
		return -1;
	}
#if defined(_WIN32)
	r = GetFullPathNameA(src, MAX_PATH, dst, NULL);
	if ((r != 0) || (r <= MAX_PATH)) {
		return -1;
	}
#else
	// On UNIX, the dirname and basename functions are such a mess
	// you're better off not using them at all.
	bn = strrchr(src, '/');
	if (bn == NULL) {
		dn = ".";
		bn = src;
	} else {
		dn = src;
		bn[0] = 0;
		bn += 1;
	}
	if (realpath(dn, dst) != NULL) {
		strcat(dst, "/");
		strcat(dst, bn);
		if (dn == src) {
			bn -= 1;
			bn[0] = '/';
		}
		return -1;
	}
#endif
	fprintf(stderr, "Unable to get full path for '%s'.\n", src);
	return 0;
}

void __inline handle_separators(char* path)
{
	size_t i;
	if (path == NULL) return;
	for (i=0; i<strlen(path); i++) {
		if (path[i] == NON_NATIVE_SEPARATOR) {
			path[i] = NATIVE_SEPARATOR;
		}
	}
}

#if defined(USER_DIR)
// Modified from http://www.zemris.fer.hr/predmeti/os1/misc/Unix2Win.htm
void scan_dir(char *dirname, int countfiles)
{
	char			dir[MAX_PATH+1];
	char			subdir[MAX_PATH+1];
	char*			entry;
#if defined(_WIN32)
	HANDLE			hList;
	WIN32_FIND_DATA	FileData;
#else
	int r;
	DIR *dp;
	struct dirent*	dir_entry;
	struct stat		stat_info;
#endif

	// Get the proper directory path
	if ( (strlen(initial_dir) + strlen(dirname) + 4) > sizeof(dir) ) {
		fprintf(stderr, "Path overflow.\n");
		return;
	}
	sprintf(dir, "%s%c%s", initial_dir, NATIVE_SEPARATOR, dirname);
#if defined(_WIN32)
	strcat(dir, "\\*");
#endif

	// Get the first file
#if defined(_WIN32)
	hList = FindFirstFile(dir, &FileData);
	if (hList == INVALID_HANDLE_VALUE) return;
#else
	dp = opendir(dir);
	if (dp == NULL) return;
	dir_entry = readdir(dp);
	if (dir_entry == NULL) return;
#endif

	// Traverse through the directory structure
	do {
		// Check the object is a directory or not
#if defined(_WIN32)
		entry = FileData.cFileName;
		if (FileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
#else
		entry = dir_entry->d_name;
		chdir(dir);
		r = NATIVE_STAT(entry, &stat_info);
		chdir(cwd);
		if (r != 0) {
			continue;
		}
		if (S_ISDIR(stat_info.st_mode)) {
#endif
			if ( (strcmp(entry, ".") != 0)
			  && (strcmp(entry, "..") != 0)) {
				// Get the full path for sub directory
				if ( (strlen(dirname) + strlen(entry) + 2) > sizeof(subdir) ) {
					fprintf(stderr, "Path overflow.\n");
					return;
				}
				sprintf(subdir, "%s%c%s", dirname, NATIVE_SEPARATOR, entry);
				scan_dir(subdir, countfiles);
			}
		} else {
			if (!countfiles) {
				if ( (embeddable[nb_embeddables].file_name =
					  malloc(strlen(initial_dir) + strlen(dirname) +
					  strlen(entry) + 2) ) == NULL) {
					return;
				}
				if ( (embeddable[nb_embeddables].extraction_subdir =
					  malloc(strlen(dirname)) ) == NULL) {
					return;
				}
				sprintf(embeddable[nb_embeddables].file_name,
					"%s%s%c%s", initial_dir, dirname, NATIVE_SEPARATOR, entry);
				if (dirname[0] == NATIVE_SEPARATOR) {
					sprintf(embeddable[nb_embeddables].extraction_subdir,
						"%s", dirname+1);
				} else {
					safe_free(embeddable[nb_embeddables].extraction_subdir);
					embeddable[nb_embeddables].extraction_subdir = NATIVE_STRDUP(".");
				}
			}
			nb_embeddables++;
		}
	}
#if defined(_WIN32)
	while ( FindNextFile(hList, &FileData) || (GetLastError() != ERROR_NO_MORE_FILES) );
	FindClose(hList);
#else
	while ((dir_entry = readdir(dp)) != NULL);
	closedir(dp);
#endif
}

void add_user_files(void) {
	int i;

#if !defined(_WIN32)
	getcwd(cwd, sizeof(cwd));
#endif
	handle_separators(initial_dir);
	// Dry run to count additional files
	scan_dir("", -1);
	if (nb_embeddables == nb_embeddables_fixed) {
		fprintf(stderr, "No user embeddable files found.\n");
		return;
	}

	// Extend the array to add the user files
	embeddable = calloc(nb_embeddables, sizeof(struct emb));
	if (embeddable == NULL) {
		fprintf(stderr, "Could not include user embeddable files.\n");
		return;
	}
	// Copy the fixed part of our table into our new array
	for (i=0; i<nb_embeddables_fixed; i++) {
		embeddable[i].file_name = embeddable_fixed[i].file_name;
		embeddable[i].extraction_subdir = embeddable_fixed[i].extraction_subdir;
	}
	nb_embeddables = nb_embeddables_fixed;

	// Fill in the array
	scan_dir("", 0);
}
#endif

int
#ifdef DDKBUILD
__cdecl
#endif
main (int argc, char *argv[])
{
	int ret = 1, i, j;
	size_t size;
	size_t* file_size = NULL;
	int64_t* file_time = NULL;
	FILE *fd, *header_fd;
	struct NATIVE_STAT stbuf;
	struct tm* ltm;
	char internal_name[] = "file_###";
	unsigned char* buffer = NULL;
	char fullpath[MAX_PATH];
	char* fname;

	// Disable stdout bufferring
	setvbuf(stdout, NULL, _IONBF,0);

	if (argc != 2) {
		fprintf(stderr, "You must supply a header name.\n");
		return 1;
	}

	// The lengths you need to go to, to be able to modify strings these days...
	for (i=0; i<nb_embeddables_fixed; i++) {
		size = strlen(embeddable_fixed[i].file_name)+1;
		fname = malloc(size);
		if (fname == NULL) {
			fprintf(stderr, "Couldn't allocate buffer.");
			goto out1;
		}
		memcpy(fname, embeddable_fixed[i].file_name, size);
		embeddable_fixed[i].file_name = fname;
	}

	nb_embeddables = nb_embeddables_fixed;
#if defined(USER_DIR)
	add_user_files();
#endif

	size = sizeof(size_t)*nb_embeddables;
	file_size = malloc(size);
	if (file_size == NULL) goto out1;
	size = sizeof(int64_t)*nb_embeddables;
	file_time = malloc(size);
	if (file_time == NULL) goto out1;

	header_fd = fopen(argv[1], "w");
	if (header_fd == NULL) {
		fprintf(stderr, "Can't create file '%s'.\n", argv[1]);
		goto out1;
	}
	fprintf(header_fd, "#pragma once\n");

	for (i=0; i<nb_embeddables; i++) {
		handle_separators(embeddable[i].file_name);
		if (!get_full_path(embeddable[i].file_name, fullpath)) {
			fprintf(stderr, "Unable to get full path for '%s'.\n", embeddable[i].file_name);
			goto out2;
		}
		printf("Embedding '%s' ", fullpath);
		fd = fopen(embeddable[i].file_name, "rb");
		if (fd == NULL) {
			fprintf(stderr, "Couldn't open file '%s'.\n", fullpath);
			goto out2;
		}

		// Read the creation date
		memset(&stbuf, 0, sizeof(stbuf));
		if ( (NATIVE_STAT(fullpath, &stbuf) == 0) && ((ltm = localtime(&stbuf.st_ctime)) != NULL) ) {
			printf("(%04d.%02d.%02d)\n", ltm->tm_year+1900, ltm->tm_mon+1, ltm->tm_mday);
		} else {
			printf("\n");
		}
		file_time[i] = (int64_t)stbuf.st_ctime;

		fseek(fd, 0, SEEK_END);
		size = (size_t)ftell(fd);
		fseek(fd, 0, SEEK_SET);
		file_size[i] = size;

		buffer = (unsigned char*) malloc(size);
		if (buffer == NULL) {
			fprintf(stderr, "Couldn't allocate buffer.\n");
			goto out3;
		}

		if (fread(buffer, 1, size, fd) != size) {
			fprintf(stderr, "Read error.\n");
			goto out4;
		}
		fclose(fd);

		sprintf(internal_name, "file_%03X", (unsigned char)i);
		fprintf(header_fd, "const unsigned char %s[] = {", internal_name);
		dump_buffer_hex(header_fd, buffer, size);
		fprintf(header_fd, "};\n\n");
		safe_free(buffer);
	}
	fprintf(header_fd, "struct res {\n" \
		"\tchar* subdir;\n" \
		"\tchar* name;\n" \
		"\tsize_t size;\n" \
		"\tint64_t creation_time;\n" \
		"\tconst unsigned char* data;\n" \
		"};\n\n");

	fprintf(header_fd, "const struct res resource[] = {\n");
	for (i=0; i<nb_embeddables; i++) {
		// Split the path
		fname = &embeddable[i].file_name[0];
		for (j = 0; embeddable[i].file_name[j] != 0; j++) {
			if ( (embeddable[i].file_name[j] == '\\')
			  || (embeddable[i].file_name[j] == '/') ) {
				fname = &embeddable[i].file_name[j+1];
			}
		}
		sprintf(internal_name, "file_%03X", (unsigned char)i);
		fprintf(header_fd, "\t{ \"");
		// We need to handle backslash sequences
		for (j=0; j<(int)strlen(embeddable[i].extraction_subdir); j++) {
			fputc(embeddable[i].extraction_subdir[j], header_fd);
			if (embeddable[i].extraction_subdir[j] == '\\') {
				fputc('\\', header_fd);
			}
		}
		fprintf(header_fd, "\", \"%s\", %d, INT64_C(%"PRId64"), %s },\n",
			fname, (int)file_size[i], file_time[i], internal_name);
	}
	fprintf(header_fd, "};\n");
	fprintf(header_fd, "\nconst int nb_resources = sizeof(resource)/sizeof(resource[0]);\n");

	fclose(header_fd);
	ret = 0;
	goto out1;

out4:
	safe_free(buffer);
out3:
	fclose(fd);
out2:
	fclose(header_fd);
	// Must delete a failed file so that Make can relaunch its build
	NATIVE_UNLINK(argv[1]);
out1:
#if defined(USER_DIR)
	for (i=nb_embeddables_fixed; i<nb_embeddables; i++) {
		safe_free(embeddable[i].file_name);
		safe_free(embeddable[i].extraction_subdir);
	}
	if (embeddable != embeddable_fixed) {
		safe_free(embeddable);
	}
#endif
	safe_free(file_size);
	safe_free(file_time);
	for (i=0; i<nb_embeddables_fixed; i++) {
		safe_free(embeddable_fixed[i].file_name);
	}
	return ret;
}
