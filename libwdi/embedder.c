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
#include <windows.h>
#include <config.h>
#include "embedder.h"
#include "embedder_files.h"

#define safe_free(p) do {if (p != NULL) {free(p); p = NULL;}} while(0)

#if defined(__CYGWIN__ )
#include <libgen.h>	// for basename()
#define _MAX_FNAME 256
#define _MAX_EXT 256
extern int _snprintf(char *buffer, size_t count, const char *format, ...);
// Hack a _splitpath() for cygwin, according to our *very specific* needs
void __inline _splitpath(char *path, char *drive, char *dir, char *fname, char *ext) {
	strncpy(fname, basename(path), _MAX_FNAME);
	ext[0] = 0;
}
#endif

const int nb_embeddables_fixed = sizeof(embeddable_fixed)/sizeof(struct emb);
int nb_embeddables;
struct emb* embeddable = embeddable_fixed;
#if defined(USER_DIR)
char initial_dir[] = USER_DIR;
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

#if defined(USER_DIR)
// Modified from http://www.zemris.fer.hr/predmeti/os1/misc/Unix2Win.htm
void ScanDir(char *dirname, BOOL countfiles)
{
	BOOL            fFinished;
	HANDLE          hList;
	TCHAR           szDir[MAX_PATH+1];
	TCHAR           szSubDir[MAX_PATH+1];
	WIN32_FIND_DATA FileData;

	// Get the proper directory path
	sprintf(szDir, "%s\\%s\\*", initial_dir, dirname);

	// Get the first file
	hList = FindFirstFile(szDir, &FileData);
	if (hList == INVALID_HANDLE_VALUE) {
		return;
	}

	// Traverse through the directory structure
	fFinished = FALSE;
	while (!fFinished) {
		// Check the object is a directory or not
		if (FileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			if ( (strcmp(FileData.cFileName, ".") != 0)
			  && (strcmp(FileData.cFileName, "..") != 0)) {

				// Get the full path for sub directory
				sprintf(szSubDir, "%s\\%s", dirname, FileData.cFileName);

				ScanDir(szSubDir, countfiles);
			}
		} else {
			if (!countfiles) {
				if ( (embeddable[nb_embeddables].file_name =
					  malloc(strlen(initial_dir) + strlen(dirname) +
					  strlen(FileData.cFileName) + 2) ) == NULL) {
					return;
				}
				if ( (embeddable[nb_embeddables].extraction_subdir =
					  malloc(strlen(dirname)) ) == NULL) {
					return;
				}
				sprintf(embeddable[nb_embeddables].file_name,
					"%s%s\\%s", initial_dir, dirname, FileData.cFileName );
				if (dirname[0] == '\\') {
					sprintf(embeddable[nb_embeddables].extraction_subdir,
						"%s", dirname+1);
				} else {
					safe_free(embeddable[nb_embeddables].extraction_subdir);
					embeddable[nb_embeddables].extraction_subdir = _strdup(".");
				}
			}
			nb_embeddables++;
		}

		if (!FindNextFile(hList, &FileData)) {
			if (GetLastError() == ERROR_NO_MORE_FILES) {
				fFinished = TRUE;
			}
		}
	}

	FindClose(hList);
}

void add_user_files(void) {
	int i;

	// Switch slashes to backslashes
	for (i=0; i<(int)strlen(initial_dir); i++) {
		if (initial_dir[i] == '/') {
			initial_dir[i] = '\\';
		}
	}

	// Dry run to count additional files
	ScanDir("", TRUE);
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
	ScanDir("", FALSE);
}
#endif

int
#ifdef _MSC_VER
__cdecl
#endif
main (int argc, char *argv[])
{
	int ret, i, j, drv_index;
	DWORD r, version_size;
	void* version_buf;
	UINT junk;
	VS_FIXEDFILEINFO *file_info, drv_info[2] = { {0}, {0} };
	WIN32_FIND_DATA file_data;
	SYSTEMTIME file_date;
	size_t size;
	size_t* file_size;
	FILE *fd, *header_fd;
	HANDLE header_handle = INVALID_HANDLE_VALUE, file_handle = INVALID_HANDLE_VALUE;
	FILETIME header_time, file_time;
	BOOL rebuild = TRUE;
	char internal_name[] = "file_###";
	unsigned char* buffer;
	char fullpath[MAX_PATH];
	char fname[_MAX_FNAME];
	char ext[_MAX_EXT];

	// Disable stdout bufferring
	setvbuf(stdout, NULL, _IONBF,0);

	if (argc != 2) {
		fprintf(stderr, "You must supply a header name\n");
		return 1;
	}

	nb_embeddables = nb_embeddables_fixed;
#if defined(USER_DIR)
	add_user_files();
#endif
	// Check if any of the embedded files have changed
	header_handle = CreateFileA(argv[1], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (header_handle != INVALID_HANDLE_VALUE) {
		// Header already exists
		header_time.dwHighDateTime = 0; header_time.dwLowDateTime = 0;
		GetFileTime(header_handle, NULL, NULL, &header_time);
		rebuild = FALSE;
		for (i=0; ; i++) {
			if (file_handle != INVALID_HANDLE_VALUE) {
				CloseHandle(file_handle);
				file_handle = INVALID_HANDLE_VALUE;
			}
			if ( (i>=nb_embeddables) || (rebuild) )
				break;
			file_handle = CreateFileA(embeddable[i].file_name, GENERIC_READ, FILE_SHARE_READ,
				NULL, OPEN_EXISTING, 0, NULL);
			if (file_handle != INVALID_HANDLE_VALUE) {
				file_time.dwHighDateTime = 0; file_time.dwLowDateTime = 0;
				GetFileTime(file_handle, NULL, NULL, &file_time);
				if (CompareFileTime(&header_time, &file_time) <= 0) {
					rebuild = TRUE;
					break;
				}
			}
		}
		CloseHandle(header_handle);
	}

	if (!rebuild) {
		printf("  resources haven't changed - skipping step\n");
		return 0;
	}

	size = sizeof(size_t)*nb_embeddables;
	file_size = malloc(size);
	if (file_size == NULL) {
		fprintf(stderr, "Couldn't even allocate a measly %d bytes\n", (int)size);
		return 1;
	}

	header_fd = fopen(argv[1], "w");
	if (header_fd == NULL) {
		fprintf(stderr, "Can't create file '%s'\n", argv[1]);
		ret = 1;
		goto out1;
	}
	fprintf(header_fd, "#pragma once\n");

	for (i=0; i<nb_embeddables; i++) {
		r = GetFullPathNameA(embeddable[i].file_name, MAX_PATH, fullpath, NULL);
		if ((r == 0) || (r > MAX_PATH)) {
			fprintf(stderr, "Unable to get full path for %s\n", embeddable[i].file_name);
			ret = 1;
			goto out2;
		}
		printf("Embedding '%s' ", fullpath);
		fd = fopen(embeddable[i].file_name, "rb");
		if (fd == NULL) {
			fprintf(stderr, "Couldn't open file '%s'\n", fullpath);
			ret = 1;
			goto out2;
		}

		// Identify the WinUSB and libusb0 files we'll pick the date & version of
		for (j=(int)strlen(embeddable[i].file_name); j>=0; j--) {
			if (embeddable[i].file_name[j] == '\\') break;
		}
		drv_index = -1;
		if (strcmp(&embeddable[i].file_name[j+1], "winusbcoinstaller2.dll") == 0) {
			drv_index = 0;
		} else if (strcmp(&embeddable[i].file_name[j+1], "libusb0.sys") == 0) {
			drv_index = 1;
		}

		// Read the creation date (once more)
		file_data.ftCreationTime.dwHighDateTime = 0;
		file_data.ftCreationTime.dwLowDateTime = 0;
		if ( (FindFirstFileA(fullpath, &file_data) != INVALID_HANDLE_VALUE)
		  && (FileTimeToSystemTime(&file_data.ftCreationTime, &file_date)) ) {
			printf("(%04d.%02d.%02d", file_date.wYear, file_date.wMonth, file_date.wDay);
		}

		// Read the version
		version_size = GetFileVersionInfoSizeA(fullpath, NULL);
		version_buf = malloc(version_size);
		if (version_buf != NULL) {
			if ( (GetFileVersionInfoA(fullpath, 0, version_size, version_buf))
			  && (VerQueryValueA(version_buf, "\\", (void*)&file_info, &junk)) ) {
				printf(", v%d.%d.%d.%d)", (int)file_info->dwFileVersionMS>>16, (int)file_info->dwFileVersionMS&0xFFFF,
					(int)file_info->dwFileVersionLS>>16, (int)file_info->dwFileVersionLS&0xFFFF);
				if ( (drv_index != -1) && (drv_info[drv_index].dwSignature == 0) ) {
					printf(" [using this file for %s version info]", drv_index==0?"WinUSB":"libusb0");
					memcpy(&drv_info[drv_index], file_info, sizeof(VS_FIXEDFILEINFO));
					drv_info[drv_index].dwFileDateLS = file_data.ftCreationTime.dwLowDateTime;
					drv_info[drv_index].dwFileDateMS = file_data.ftCreationTime.dwHighDateTime;
				}
			} else {
				printf(")");
			}
			free(version_buf);
		}
		printf("\n");

		fseek(fd, 0, SEEK_END);
		size = (size_t)ftell(fd);
		fseek(fd, 0, SEEK_SET);
		file_size[i] = size;

		buffer = (unsigned char*) malloc(size);
		if (buffer == NULL) {
			fprintf(stderr, "Couldn't allocate buffer");
			ret = 1;
			goto out3;
		}

		if (fread(buffer, 1, size, fd) != size) {
			fprintf(stderr, "Read error");
			ret = 1;
			goto out4;
		}
		fclose(fd);

		_snprintf(internal_name, sizeof(internal_name), "file_%03X", (unsigned char)i);
		fprintf(header_fd, "const unsigned char %s[] = {", internal_name);
		dump_buffer_hex(header_fd, buffer, size);
		fprintf(header_fd, "};\n\n");
		safe_free(buffer);
		fclose(fd);
	}

	fprintf(header_fd, "struct res {\n" \
		"\tchar* subdir;\n" \
		"\tchar* name;\n" \
		"\tsize_t size;\n" \
		"\tconst unsigned char* data;\n" \
		"};\n\n");

	fprintf(header_fd, "const struct res resource[] = {\n");
	for (i=0; i<nb_embeddables; i++) {
		_splitpath(embeddable[i].file_name, NULL, NULL, fname, ext);
		strncat(fname, ext, sizeof(fname));
		_snprintf(internal_name, sizeof(internal_name), "file_%03X", (unsigned char)i);
		fprintf(header_fd, "\t{ \"");
		// We need to handle backslash sequences
		for (j=0; j<(int)strlen(embeddable[i].extraction_subdir); j++) {
			fputc(embeddable[i].extraction_subdir[j], header_fd);
			if (embeddable[i].extraction_subdir[j] == '\\') {
				fputc('\\', header_fd);
			}
		}
		fprintf(header_fd, "\", \"%s\", %d, %s },\n",
			fname, (int)file_size[i], internal_name);
	}
	fprintf(header_fd, "};\n");
	fprintf(header_fd, "const int nb_resources = sizeof(resource)/sizeof(resource[0]);\n\n");

	// These will be used to populate the inf data
	fprintf(header_fd, "// WinUSB = 0, libusb0 = 1\n");
	fprintf(header_fd, "const VS_FIXEDFILEINFO driver_version[2] = {\n");
	for (i=0; i<2; i++) {
		fprintf(header_fd, "	{ 0x%08X, 0x%08X, 0x%08X, 0x%08X, 0x%08X, 0x%08X, 0x%08X,\n",
			drv_info[i].dwSignature, drv_info[i].dwStrucVersion, drv_info[i].dwFileVersionMS,
			drv_info[i].dwFileVersionLS, drv_info[i].dwProductVersionMS,
			drv_info[i].dwProductVersionLS, drv_info[i].dwFileFlagsMask);
		fprintf(header_fd, "	  0x%08X, 0x%08X, 0x%08X, 0x%08X, 0x%08X, 0x%08X},\n",
			drv_info[i].dwFileFlags, drv_info[i].dwFileOS, drv_info[i].dwFileType,
			drv_info[i].dwFileSubtype, drv_info[i].dwFileDateMS, drv_info[i].dwFileDateLS);
	}
	fprintf(header_fd, "};\n");

	fclose(header_fd);
	safe_free(file_size);
#if defined(USER_DIR)
	for (i=nb_embeddables_fixed; i<nb_embeddables; i++) {
		safe_free(embeddable[i].extraction_subdir);
		safe_free(embeddable[i].file_name);
	}
	if (embeddable != embeddable_fixed) {
		safe_free(embeddable);
	}
#endif
	return 0;

out4:
	safe_free(buffer);
out3:
	fclose(fd);
out2:
	fclose(header_fd);
	// Must delete a failed file so that Make can relaunch its build
	DeleteFile(argv[1]);
out1:
	safe_free(file_size);
	return ret;
}
