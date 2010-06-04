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

// Uncomment and set to your DDK installation directory
// TODO: comment before release!
#define DDK_PATH "E:\\WinDDK\\7600.16385.0"
#if !defined(DDK_PATH)
#error "Make sure you set DDK_PATH before compiling this file"
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

#if defined(_MSC_VER)
#define INSTALLER_PATH_32 "..\\Win32\\Release\\lib"
#define INSTALLER_PATH_64 "..\\x64\\Release\\lib"
#else
// TODO: ????
#define INSTALLER_PATH_32 ".lib"
#define INSTALLER_PATH_64 ".lib64"
#endif



struct emb embeddable[] = {
	// WinUSB driver DLLs (32 and 64 bit)
	{ DDK_PATH "\\redist\\wdf\\amd64\\WdfCoInstaller01009.dll", "amd64_dll1", "amd64", "WdfCoInstaller01009.dll" },
	{ DDK_PATH "\\redist\\winusb\\amd64\\winusbcoinstaller2.dll", "amd64_dll2", "amd64", "winusbcoinstaller2.dll" },
	{ DDK_PATH "\\redist\\wdf\\x86\\WdfCoInstaller01009.dll", "x86_dll1", "x86", "WdfCoInstaller01009.dll" },
	{ DDK_PATH "\\redist\\winusb\\x86\\winusbcoinstaller2.dll", "x86_dll2", "x86", "winusbcoinstaller2.dll" },
	// Installer executable requiring UAC elevation
	// Why do we need two installers? Glad you asked. If you try to run the x86 installer on an x64
	// system, you will get the annoying "This program might not have installed properly" prompt.
	{ INSTALLER_PATH_32 "\\driver-installer.exe", "installer_32", ".", "installer_x86.exe" },
	{ INSTALLER_PATH_64 "\\driver-installer.exe", "installer_64", ".", "installer_x64.exe" },
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

int main (int argc, char *argv[])
{
	int  ret, i;
	size_t size;
	size_t* file_size;
	FILE *fd, *header_fd;
	unsigned char* buffer;

	// Disable stdout bufferring
	setvbuf(stdout, NULL, _IONBF,0);

	if (argc != 2) {
		fprintf(stderr, "You must supply a header name\n");
		return 1;
	}

	size = sizeof(size_t)*nb_embeddables;
	file_size = malloc(size);
	if (file_size == NULL) {
		fprintf(stderr, "Couldn't even allocate a measly %d bytes\n", size);
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
		printf("Embedding '%s'\n",embeddable[i].file_name);
		fd = fopen(embeddable[i].file_name, "rb");
		if (fd == NULL) {
			fprintf(stderr, "Couldn't open file '%s'\n", embeddable[i].file_name);
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
			file_size[i], embeddable[i].internal_name);
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
out1:
	free(file_size);
	return ret;
}



