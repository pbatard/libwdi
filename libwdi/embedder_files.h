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
#pragma once

/*
 * This include defines the driver files that should be embedded in the library
 * If you want to add extra files from a specific directory (eg signed inf and cat)
 * you could define the macro below and uncomment the 2 lines starting by USER_DIR
 * in the embeddable[] table
 */
#define USER_DIR "C:\\signeddriver"

struct emb {
	char* file_name;
	char* extraction_subdir;
};

/*
 * files to embed
 */
struct emb embeddable[] = {

#if defined(OPT_M32)
	{ DDK_DIR "\\redist\\wdf\\x86\\WdfCoInstaller" WDF_VER ".dll", "x86" },
	{ DDK_DIR "\\redist\\winusb\\x86\\winusbcoinstaller2.dll", "x86" },
#if defined(LIBUSB0_DIR)
	{ LIBUSB0_DIR "\\x86\\libusb0.dll", "x86" },
	{ LIBUSB0_DIR "\\x86\\libusb0.sys", "x86" },
#endif
	{ INSTALLER_PATH_32 "\\installer_x86.exe", "." },
#endif

#if defined(OPT_M64)
	{ DDK_DIR "\\redist\\wdf\\amd64\\WdfCoInstaller" WDF_VER ".dll", "amd64" },
	{ DDK_DIR "\\redist\\winusb\\amd64\\winusbcoinstaller2.dll", "amd64" },
#if defined(LIBUSB0_DIR)
	{ LIBUSB0_DIR "\\amd64\\libusb0.dll", "amd64" },
	{ LIBUSB0_DIR "\\amd64\\libusb0.sys", "amd64" },
#endif
	{ INSTALLER_PATH_64 "\\installer_x64.exe", "." },
#endif

	{ DDK_DIR "\\license.rtf", "." },	// WinUSB License file
// TODO: include libusb0.sys license

	// User supplied files
//	{ USER_DIR "\\mydriver.inf", "." },
//	{ USER_DIR "\\mydriver.cat", "." },
};


