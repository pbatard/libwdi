/*
 * Convert binary resources into a .h include
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
	char* internal_name;
	char* extraction_subdir;
	char* extraction_name;
};


struct emb embeddable[] = {
	// WinUSB driver DLLs (32 and 64 bit)
#if !defined(OPT_M32) && !defined(OPT_M64)
#error both 32 and 64 bit support have been disabled - check your config.h
#endif

#if defined(OPT_M32)
	{ DDK_DIR "\\redist\\wdf\\x86\\WdfCoInstaller" WDF_VER ".dll", "x86_dll1", "x86", "WdfCoInstaller" WDF_VER ".dll" },
	{ DDK_DIR "\\redist\\winusb\\x86\\winusbcoinstaller2.dll", "x86_dll2", "x86", "winusbcoinstaller2.dll" },
	{ DDK_DIR "\\redist\\DIFx\\DIFxAPI\\x86\\DIFxAPI.dll", "x86_dll3", "x86", "DIFxAPI.dll" },
	{ INSTALLER_PATH_32 "\\installer_x86.exe", "installer_32", ".", "installer_x86.exe" },
#endif
#if defined(OPT_M64)
	{ DDK_DIR "\\redist\\wdf\\amd64\\WdfCoInstaller" WDF_VER ".dll", "amd64_dll1", "amd64", "WdfCoInstaller" WDF_VER ".dll" },
	{ DDK_DIR "\\redist\\winusb\\amd64\\winusbcoinstaller2.dll", "amd64_dll2", "amd64", "winusbcoinstaller2.dll" },
	{ DDK_DIR "\\redist\\DIFx\\DIFxAPI\\amd64\\DIFxAPI.dll", "amd64_dll3", "amd64", "DIFxAPI.dll" },
	{ INSTALLER_PATH_64 "\\installer_x64.exe", "installer_64", ".", "installer_x64.exe" },
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
	unsigned char* buffer;
	char fullpath[MAX_PATH_LENGTH];

	// Disable stdout bufferring
	setvbuf(stdout, NULL, _IONBF,0);

	if (argc != 2) {
		fprintf(stderr, "You must supply a header name\n");
		return 1;
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
			if (i>=nb_embeddables-2) {
				fprintf(stderr, "Please make sure you compiled BOTH the 64 and 32 bit "
					"versions of the installer executables before compiling this library.\n");
			}
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

		fprintf(header_fd, "const unsigned char %s[] = {", embeddable[i].internal_name);
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
		fprintf(header_fd, "\t{ \"%s\", \"%s\", %d, %s },\n",
			embeddable[i].extraction_subdir, embeddable[i].extraction_name,
			(int)file_size[i], embeddable[i].internal_name);
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
