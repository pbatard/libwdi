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
 * you should either define the macro USER_DIR in msvc/config.h (MS compilers) or
 * use the --with-userdir option when running configure.
 */

struct emb {
	char* file_name;
	char* extraction_subdir;
};

/*
 * files to embed
 */
struct emb embeddable_fixed[] = {

// 32 bit driver files
#if defined(OPT_M32)
#if defined(DDK_DIR)
	{ DDK_DIR "\\redist\\wdf\\x86\\WdfCoInstaller" WDF_VER ".dll", "x86" },
	{ DDK_DIR "\\redist\\winusb\\x86\\winusbcoinstaller2.dll", "x86" },
#endif
	{ INSTALLER_PATH_32 "\\installer_x86.exe", "." },
#endif

// 64 bit driver files
#if defined(OPT_M64)
#if defined(DDK_DIR)
	{ DDK_DIR "\\redist\\wdf\\amd64\\WdfCoInstaller" WDF_VER ".dll", "amd64" },
	{ DDK_DIR "\\redist\\winusb\\amd64\\winusbcoinstaller2.dll", "amd64" },
#endif
#if defined(LIBUSB0_DIR)
	{ LIBUSB0_DIR "\\bin\\amd64\\libusb0.dll", "amd64" },
	{ LIBUSB0_DIR "\\bin\\amd64\\libusb0.sys", "amd64" },
#endif
	{ INSTALLER_PATH_64 "\\installer_x64.exe", "." },
#endif

// IA64 (Itanium) driver files
#if defined(OPT_IA64)
#if defined(DDK_DIR)
	{ DDK_DIR "\\redist\\wdf\\ia64\\WdfCoInstaller" WDF_VER ".dll", "ia64" },
	{ DDK_DIR "\\redist\\winusb\\ia64\\winusbcoinstaller2.dll", "ia64" },
#endif
#if defined(LIBUSB0_DIR)
	{ LIBUSB0_DIR "\\bin\\ia64\\libusb0.dll", "ia64" },
	{ LIBUSB0_DIR "\\bin\\ia64\\libusb0.sys", "ia64" },
#endif
#endif

// Common driver files
// On 64 bit, for WOW64, we must include the 32 bit libusb0 files as well
#if defined(LIBUSB0_DIR)
	{ LIBUSB0_DIR "\\bin\\x86\\libusb0_x86.dll", "x86" },
	{ LIBUSB0_DIR "\\bin\\x86\\libusb0.sys", "x86" },
	{ LIBUSB0_DIR "\\installer_license.txt", "license\\libusb-win32" },
#endif
#if defined(DDK_DIR)
	{ DDK_DIR "\\license.rtf", "license\\WinUSB" },	// WinUSB License file
#endif

// inf templates for the tokenizer ("" directory means no extraction)
	{ "winusb.inf.in", "" },
	{ "libusb-win32.inf.in", "" },
};


