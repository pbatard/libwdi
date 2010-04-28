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

#define MAX_PATH_LENGTH 256

#if defined(_MSC_VER)
#define __STR2__(x) #x
#define __STR1__(x) __STR2__(x)
// TODO: feed preprocessor arg to custom step?
#if defined(_WIN64) && defined(OPT_M32)
// a 64 bit application/library CANNOT be used on 32 bit platforms
#pragma message(__FILE__ "(" __STR1__(__LINE__) ") : warning : library is compiled as 64 bit - disabling 32 bit support")
#undef OPT_M32
#endif
#endif

#if defined(_MSC_VER) && !defined(DDKBUILD)
#if defined(_DEBUG)
#define INSTALLER_PATH_32 "..\\Win32\\Debug\\lib"
#define INSTALLER_PATH_64 "..\\x64\\Debug\\lib"
#else
#define INSTALLER_PATH_32 "..\\Win32\\Release\\lib"
#define INSTALLER_PATH_64 "..\\x64\\Release\\lib"
#endif
#else
// If you compile with shared libs, DON'T PICK THE EXE IN "installer",
// as it won't run from ANYWHERE ELSE! Use the one from .libs instead.
#define INSTALLER_PATH_32 "."
#define INSTALLER_PATH_64 "."
#endif

/*
 * files to embed
 */
struct emb {
	char* file_name;
	char* extraction_subdir;
	char* extraction_name;
};


struct emb embeddable[] = {
	// WinUSB driver DLLs (32 and 64 bit)
#if !defined(OPT_M32) && !defined(OPT_M64)
#error both 32 and 64 bit support have been disabled - check your config.h
#endif

#if defined(OPT_M32)
	{ DDK_DIR "\\redist\\wdf\\x86\\WdfCoInstaller" WDF_VER ".dll", "x86", "WdfCoInstaller" WDF_VER ".dll" },
	{ DDK_DIR "\\redist\\winusb\\x86\\winusbcoinstaller2.dll", "x86", "winusbcoinstaller2.dll" },
	{ INSTALLER_PATH_32 "\\installer_x86.exe", ".", "installer_x86.exe" },
#endif
#if defined(OPT_M64)
	{ DDK_DIR "\\redist\\wdf\\amd64\\WdfCoInstaller" WDF_VER ".dll", "amd64", "WdfCoInstaller" WDF_VER ".dll" },
	{ DDK_DIR "\\redist\\winusb\\amd64\\winusbcoinstaller2.dll", "amd64", "winusbcoinstaller2.dll" },
	{ INSTALLER_PATH_64 "\\installer_x64.exe", ".", "installer_x64.exe" },
#endif
};
const int nb_embeddables = sizeof(embeddable)/sizeof(embeddable[0]);

void dump_buffer_hex(FILE* fd, unsigned char *buffer, size_t size)
{
	size_t i;

	for (i=0; i<size; i++) {
		if (!(i%0x10))
			fprintf(fd, "\n\t");
		fprintf(fd, "0x%02X,", buffer[i]);
	}
	fprintf(fd, "\n");
}

int
#ifdef _MSC_VER
__cdecl
#endif
main (int argc, char *argv[])
{
	int  ret, i;
	DWORD r;
	size_t size;
	size_t* file_size;
	FILE *fd, *header_fd;
	HANDLE header_handle = INVALID_HANDLE_VALUE, file_handle = INVALID_HANDLE_VALUE;
	FILETIME header_time, file_time;
	BOOL rebuild = TRUE;
	char internal_name[] = "file_##";

	unsigned char* buffer;
	char fullpath[MAX_PATH_LENGTH];

	// Disable stdout bufferring
	setvbuf(stdout, NULL, _IONBF,0);

	if (argc != 2) {
		fprintf(stderr, "You must supply a header name\n");
		return 1;
	}

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
		r = GetFullPathNameA(embeddable[i].file_name, MAX_PATH_LENGTH, fullpath, NULL);
		if ((r == 0) || (r > MAX_PATH_LENGTH)) {
			fprintf(stderr, "Unable to get full path for %s\n", embeddable[i].file_name);
			ret = 1;
			goto out2;
		}
		printf("Embedding '%s'\n", fullpath);
		fd = fopen(embeddable[i].file_name, "rb");
		if (fd == NULL) {
			fprintf(stderr, "Couldn't open file '%s'\n", fullpath);
			ret = 1;
			goto out2;
		}

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

		_snprintf(internal_name, sizeof(internal_name), "file_%02X", (unsigned char)i);
		fprintf(header_fd, "const unsigned char %s[] = {", internal_name);
		dump_buffer_hex(header_fd, buffer, size);
		fprintf(header_fd, "};\n\n");
		free(buffer);
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
		_snprintf(internal_name, sizeof(internal_name), "file_%02X", (unsigned char)i);
		fprintf(header_fd, "\t{ \"%s\", \"%s\", %d, %s },\n",
			embeddable[i].extraction_subdir, embeddable[i].extraction_name,
			(int)file_size[i], internal_name);
	}
	fprintf(header_fd, "};\n");
	fprintf(header_fd, "const int nb_resources = sizeof(resource)/sizeof(resource[0]);\n");
	fclose(header_fd);
	free(file_size);
	return 0;

out4:
	free(buffer);
out3:
	fclose(fd);
out2:
	fclose(header_fd);
	// Must delete a failed file so that Make can relaunch its build
	DeleteFile(argv[1]);
out1:
	free(file_size);
	return ret;
}
