/*
 * Library for USB automated driver installation
 * Copyright (c) 2010-2022 Pete Batard <pete@akeo.ie>
 * Parts of the code from libusb by Daniel Drake, Johannes Erdfelt et al.
 * For more info, please visit http://libwdi.akeo.ie
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
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

/* Memory leaks detection - define _CRTDBG_MAP_ALLOC as preprocessor macro */
#ifdef _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#include <windows.h>
#include <setupapi.h>
#include <io.h>
#include <sys/types.h>
#include <stdio.h>
#include <inttypes.h>
#include <objbase.h>
#include <shellapi.h>
#include <config.h>
#include <ctype.h>
#include <sddl.h>
#include <fcntl.h>
#include <wincrypt.h>
#include <winternl.h>
#include <assert.h>

#include "installer.h"
#include "libwdi.h"
#include "libwdi_i.h"
#include "logging.h"
#include "tokenizer.h"
#include "embedded.h"	// auto-generated during compilation
#include "msapi_utf8.h"
#include "stdfn.h"

// Global variables
static struct wdi_device_info *current_device = NULL;
static BOOL filter_driver = FALSE;
static DWORD timeout = DEFAULT_TIMEOUT;
static HANDLE pipe_handle = INVALID_HANDLE_VALUE;
static VS_FIXEDFILEINFO driver_version[WDI_NB_DRIVERS-1] = { {0}, {0}, {0}, {0} };
static const char* driver_name[WDI_NB_DRIVERS-1] = {"winusbcoinstaller2.dll", "libusb0.dll", "libusbK.dll", ""};
static const char* inf_template[WDI_NB_DRIVERS-1] = {"winusb.inf.in", "libusb0.inf.in", "libusbk.inf.in", "usbser.inf.in"};
static const char* cat_template[WDI_NB_DRIVERS-1] = {"winusb.cat.in", "libusb0.cat.in", "libusbk.cat.in", "usbser.cat.in"};
static const char* ms_compat_id[WDI_NB_DRIVERS-1] = {"MS_COMP_WINUSB", "MS_COMP_LIBUSB0", "MS_COMP_LIBUSBK", "MS_COMP_USBSER"};
int nWindowsVersion = WINDOWS_UNDEFINED;
int nWindowsBuildNumber = -1;
char WindowsVersionStr[128] = "Windows ";

// Detect Windows version
#define GET_WINDOWS_VERSION do { if (nWindowsVersion == WINDOWS_UNDEFINED) GetWindowsVersion(); } while(0)

BOOL is_x64(void)
{
	BOOL ret = FALSE;
	// Detect if we're running a 32 or 64 bit system
	if (sizeof(uintptr_t) < 8) {
		IsWow64Process(GetCurrentProcess(), &ret);
	} else {
		ret = TRUE;
	}
	return ret;
}

static const char* GetEdition(DWORD ProductType)
{
	static char unknown_edition_str[64];

	// From: https://docs.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getproductinfo
	// These values can be found in the winnt.h header.
	switch (ProductType) {
	case 0x00000000: return "";	//  Undefined
	case 0x00000001: return "Ultimate";
	case 0x00000002: return "Home Basic";
	case 0x00000003: return "Home Premium";
	case 0x00000004: return "Enterprise";
	case 0x00000005: return "Home Basic N";
	case 0x00000006: return "Business";
	case 0x00000007: return "Server Standard";
	case 0x00000008: return "Server Datacenter";
	case 0x00000009: return "Smallbusiness Server";
	case 0x0000000A: return "Server Enterprise";
	case 0x0000000B: return "Starter";
	case 0x0000000C: return "Server Datacenter (Core)";
	case 0x0000000D: return "Server Standard (Core)";
	case 0x0000000E: return "Server Enterprise (Core)";
	case 0x00000010: return "Business N";
	case 0x00000011: return "Web Server";
	case 0x00000012: return "HPC Edition";
	case 0x00000013: return "Storage Server (Essentials)";
	case 0x0000001A: return "Home Premium N";
	case 0x0000001B: return "Enterprise N";
	case 0x0000001C: return "Ultimate N";
	case 0x00000022: return "Home Server";
	case 0x00000024: return "Server Standard without Hyper-V";
	case 0x00000025: return "Server Datacenter without Hyper-V";
	case 0x00000026: return "Server Enterprise without Hyper-V";
	case 0x00000027: return "Server Datacenter without Hyper-V (Core)";
	case 0x00000028: return "Server Standard without Hyper-V (Core)";
	case 0x00000029: return "Server Enterprise without Hyper-V (Core)";
	case 0x0000002A: return "Hyper-V Server";
	case 0x0000002F: return "Starter N";
	case 0x00000030: return "Pro";
	case 0x00000031: return "Pro N";
	case 0x00000034: return "Server Solutions Premium";
	case 0x00000035: return "Server Solutions Premium (Core)";
	case 0x00000040: return "Server Hyper Core V";
	case 0x00000042: return "Starter E";
	case 0x00000043: return "Home Basic E";
	case 0x00000044: return "Premium E";
	case 0x00000045: return "Pro E";
	case 0x00000046: return "Enterprise E";
	case 0x00000047: return "Ultimate E";
	case 0x00000048: return "Enterprise (Eval)";
	case 0x0000004F: return "Server Standard (Eval)";
	case 0x00000050: return "Server Datacenter (Eval)";
	case 0x00000054: return "Enterprise N (Eval)";
	case 0x00000057: return "Thin PC";
	case 0x00000058: case 0x00000059: case 0x0000005A: case 0x0000005B: case 0x0000005C: return "Embedded";
	case 0x00000062: return "Home N";
	case 0x00000063: return "Home China";
	case 0x00000064: return "Home Single Language";
	case 0x00000065: return "Home";
	case 0x00000067: return "Pro with Media Center";
	case 0x00000069: case 0x0000006A: case 0x0000006B: case 0x0000006C: return "Embedded";
	case 0x0000006F: return "Home Connected";
	case 0x00000070: return "Pro Student";
	case 0x00000071: return "Home Connected N";
	case 0x00000072: return "Pro Student N";
	case 0x00000073: return "Home Connected Single Language";
	case 0x00000074: return "Home Connected China";
	case 0x00000079: return "Education";
	case 0x0000007A: return "Education N";
	case 0x0000007D: return "Enterprise LTSB";
	case 0x0000007E: return "Enterprise LTSB N";
	case 0x0000007F: return "Pro S";
	case 0x00000080: return "Pro S N";
	case 0x00000081: return "Enterprise LTSB (Eval)";
	case 0x00000082: return "Enterprise LTSB N (Eval)";
	case 0x0000008A: return "Pro Single Language";
	case 0x0000008B: return "Pro China";
	case 0x0000008C: return "Enterprise Subscription";
	case 0x0000008D: return "Enterprise Subscription N";
	case 0x00000091: return "Server Datacenter SA (Core)";
	case 0x00000092: return "Server Standard SA (Core)";
	case 0x00000095: return "Utility VM";
	case 0x000000A1: return "Pro for Workstations";
	case 0x000000A2: return "Pro for Workstations N";
	case 0x000000A4: return "Pro for Education";
	case 0x000000A5: return "Pro for Education N";
	case 0x000000AB: return "Enterprise G";	// I swear Microsoft are just making up editions...
	case 0x000000AC: return "Enterprise G N";
	case 0x000000B6: return "Home OS";
	case 0x000000B7: return "Cloud E";
	case 0x000000B8: return "Cloud E N";
	case 0x000000BD: return "Lite";
	case 0xABCDABCD: return "(Unlicensed)";
	default:
		static_sprintf(unknown_edition_str, "(Unknown Edition 0x%02X)", (uint32_t)ProductType);
		return unknown_edition_str;
	}
}

/*
 * Modified from smartmontools' os_win32.cpp
 */
void GetWindowsVersion(void)
{
	OSVERSIONINFOEXA vi, vi2;
	DWORD dwProductType;
	const char* w = 0;
	const char* w64 = "32 bit";
	char* vptr;
	size_t vlen;
	unsigned major, minor;
	ULONGLONG major_equal, minor_equal;
	BOOL ws;

	nWindowsVersion = WINDOWS_UNDEFINED;
	static_strcpy(WindowsVersionStr, "Windows Undefined");

	memset(&vi, 0, sizeof(vi));
	vi.dwOSVersionInfoSize = sizeof(vi);
	if (!GetVersionExA((OSVERSIONINFOA*)&vi)) {
		memset(&vi, 0, sizeof(vi));
		vi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);
		if (!GetVersionExA((OSVERSIONINFOA*)&vi))
			return;
	}

	if (vi.dwPlatformId == VER_PLATFORM_WIN32_NT) {

		if (vi.dwMajorVersion > 6 || (vi.dwMajorVersion == 6 && vi.dwMinorVersion >= 2)) {
			// Starting with Windows 8.1 Preview, GetVersionEx() does no longer report the actual OS version
			// See: http://msdn.microsoft.com/en-us/library/windows/desktop/dn302074.aspx
			// And starting with Windows 10 Preview 2, Windows enforces the use of the application/supportedOS
			// manifest in order for VerSetConditionMask() to report the ACTUAL OS major and minor...

			major_equal = VerSetConditionMask(0, VER_MAJORVERSION, VER_EQUAL);
			for (major = vi.dwMajorVersion; major <= 9; major++) {
				memset(&vi2, 0, sizeof(vi2));
				vi2.dwOSVersionInfoSize = sizeof(vi2); vi2.dwMajorVersion = major;
				if (!VerifyVersionInfoA(&vi2, VER_MAJORVERSION, major_equal))
					continue;
				if (vi.dwMajorVersion < major) {
					vi.dwMajorVersion = major; vi.dwMinorVersion = 0;
				}

				minor_equal = VerSetConditionMask(0, VER_MINORVERSION, VER_EQUAL);
				for (minor = vi.dwMinorVersion; minor <= 9; minor++) {
					memset(&vi2, 0, sizeof(vi2)); vi2.dwOSVersionInfoSize = sizeof(vi2);
					vi2.dwMinorVersion = minor;
					if (!VerifyVersionInfoA(&vi2, VER_MINORVERSION, minor_equal))
						continue;
					vi.dwMinorVersion = minor;
					break;
				}

				break;
			}
		}

		if (vi.dwMajorVersion <= 0xf && vi.dwMinorVersion <= 0xf) {
			ws = (vi.wProductType <= VER_NT_WORKSTATION);
			nWindowsVersion = vi.dwMajorVersion << 4 | vi.dwMinorVersion;
			switch (nWindowsVersion) {
			case WINDOWS_XP: w = "XP";
				break;
			case WINDOWS_2003: w = (ws ? "XP_64" : (!GetSystemMetrics(89) ? "Server 2003" : "Server 2003_R2"));
				break;
			case WINDOWS_VISTA: w = (ws ? "Vista" : "Server 2008");
				break;
			case WINDOWS_7: w = (ws ? "7" : "Server 2008_R2");
				break;
			case WINDOWS_8: w = (ws ? "8" : "Server 2012");
				break;
			case WINDOWS_8_1: w = (ws ? "8.1" : "Server 2012_R2");
				break;
			case WINDOWS_10_PREVIEW1: w = (ws ? "10 (Preview 1)" : "Server 10 (Preview 1)");
				break;
				// Starting with Windows 10 Preview 2, the major is the same as the public-facing version
			case WINDOWS_10:
				if (vi.dwBuildNumber < 20000) {
					w = (ws ? "10" : ((vi.dwBuildNumber < 17763) ? "Server 2016" : "Server 2019"));
					break;
				}
				nWindowsVersion = WINDOWS_11;
				// Fall through
			case WINDOWS_11: w = (ws ? "11" : "Server 2022");
				break;
			default:
				if (nWindowsVersion < WINDOWS_XP)
					nWindowsVersion = WINDOWS_UNSUPPORTED;
				else
					w = "12 or later";
				break;
			}
		}
	}

	if (is_x64())
		w64 = "64-bit";

	GetProductInfo(vi.dwMajorVersion, vi.dwMinorVersion, vi.wServicePackMajor, vi.wServicePackMinor, &dwProductType);
	vptr = &WindowsVersionStr[sizeof("Windows ") - 1];
	vlen = sizeof(WindowsVersionStr) - sizeof("Windows ") - 1;
	if (!w)
		safe_sprintf(vptr, vlen, "%s %u.%u %s", (vi.dwPlatformId == VER_PLATFORM_WIN32_NT ? "NT" : "??"),
			(unsigned)vi.dwMajorVersion, (unsigned)vi.dwMinorVersion, w64);
	else if (vi.wServicePackMinor)
		safe_sprintf(vptr, vlen, "%s SP%u.%u %s", w, vi.wServicePackMajor, vi.wServicePackMinor, w64);
	else if (vi.wServicePackMajor)
		safe_sprintf(vptr, vlen, "%s SP%u %s", w, vi.wServicePackMajor, w64);
	else
		safe_sprintf(vptr, vlen, "%s%s%s, %s",
			w, (dwProductType != PRODUCT_UNDEFINED) ? " " : "", GetEdition(dwProductType), w64);

	// Add the build number (including UBR if available) for Windows 8.0 and later
	nWindowsBuildNumber = vi.dwBuildNumber;
	if (nWindowsVersion >= 0x62) {
		HKEY hCurrentVersion;
		DWORD dwType = REG_DWORD, dwSize = sizeof(DWORD), dwUbr = 0;
		if (RegOpenKeyExA(REGKEY_HKLM, "Software\\Microsoft\\Windows NT\\CurrentVersion",
			0, KEY_READ, &hCurrentVersion) == ERROR_SUCCESS) {
			RegQueryValueExA(hCurrentVersion, "UBR", NULL, &dwType, (LPBYTE)&dwUbr, &dwSize);
			RegCloseKey(hCurrentVersion);
		}

		vptr = &WindowsVersionStr[safe_strlen(WindowsVersionStr)];
		vlen = sizeof(WindowsVersionStr) - safe_strlen(WindowsVersionStr) - 1;
		if (dwUbr != 0)
			safe_sprintf(vptr, vlen, " (Build %d.%d)", nWindowsBuildNumber, (int)dwUbr);
		else
			safe_sprintf(vptr, vlen, " (Build %d)", nWindowsBuildNumber);
	}
}

/*
 * Converts a windows error to human readable string
 * uses retval as errorcode, or, if 0, use GetLastError()
 */
char *wdi_windows_error_str(DWORD retval)
{
	static char err_string[STR_BUFFER_SIZE];
	DWORD size, presize, error_code, format_error;

	error_code = retval ? retval : GetLastError();

	static_sprintf(err_string, "[0x%08lX] ", error_code);
	presize = (DWORD)strlen(err_string);

	size = FormatMessageU(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
		HRESULT_CODE(error_code), MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
		&err_string[presize], (DWORD)(sizeof(err_string) - strlen(err_string)), NULL);
	if (size == 0) {
		format_error = GetLastError();
		if ((format_error) && (format_error != ERROR_MR_MID_NOT_FOUND) && (format_error != ERROR_MUI_FILE_NOT_LOADED))
			static_sprintf(err_string, "Windows error code 0x%08lX (FormatMessage error code 0x%08lX)",
				error_code, format_error);
		else
			static_sprintf(err_string, "Windows error code 0x%08lX", error_code);
	} else {
		// Microsoft may suffix CRLF to error messages, which we need to remove...
		assert(presize > 2);
		size += presize - 2;
		// Cannot underflow if the above assert passed since our first char is neither of the following
		while ((err_string[size] == 0x0D) || (err_string[size] == 0x0A) || (err_string[size] == 0x20))
			err_string[size--] = 0;
	}

	SetLastError(error_code);	// Make sure we don't change the errorcode on exit
	return err_string;
}

// Retrieve the SID of the current user. The returned PSID must be freed by the caller using LocalFree()
static PSID GetSid(void) {
	TOKEN_USER* tu = NULL;
	DWORD len;
	HANDLE token;
	PSID ret = NULL;
	char* psid_string = NULL;

	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
		wdi_err("OpenProcessToken failed: %s", wdi_windows_error_str(0));
		return NULL;
	}

	if (!GetTokenInformation(token, TokenUser, tu, 0, &len)) {
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
			wdi_err("GetTokenInformation (pre) failed: %s", wdi_windows_error_str(0));
			return NULL;
		}
		tu = (TOKEN_USER*)calloc(1, len);
	}
	if (tu == NULL) {
		return NULL;
	}

	if (GetTokenInformation(token, TokenUser, tu, len, &len)) {
		/*
		 * now of course, the interesting thing is that if you return tu->User.Sid
		 * but free tu, the PSID pointer becomes invalid after a while.
		 * The workaround? Convert to string then back to PSID
		 */
		if (!ConvertSidToStringSidA(tu->User.Sid, &psid_string)) {
			wdi_err("Unable to convert SID to string: %s", wdi_windows_error_str(0));
			ret = NULL;
		} else {
			if (!ConvertStringSidToSidA(psid_string, &ret)) {
				wdi_err("Unable to convert string back to SID: %s", wdi_windows_error_str(0));
				ret = NULL;
			}
			// MUST use LocalFree()
			LocalFree(psid_string);
		}
	} else {
		ret = NULL;
		wdi_err("GetTokenInformation (real) failed: %s", wdi_windows_error_str(0));
	}
	free(tu);
	return ret;
}

/*
 * Check whether the path is a directory with write access
 * if create is TRUE, create directory if it doesn't exist
 */
static int check_dir(const char* path, BOOL create)
{
	int r;
	DWORD file_attributes;
	PSID sid = NULL;
	SECURITY_ATTRIBUTES s_attr, *ps = NULL;
	SECURITY_DESCRIPTOR s_desc;
	char* full_path;

	file_attributes = GetFileAttributesU(path);
	if (file_attributes == INVALID_FILE_ATTRIBUTES) {
		switch (GetLastError()) {
		case ERROR_FILE_NOT_FOUND:
		case ERROR_PATH_NOT_FOUND:
			break;
		default:
			wdi_err("Unable to read file attributes %s", wdi_windows_error_str(0));
			return WDI_ERROR_ACCESS;
		}
	} else {
		if (file_attributes & FILE_ATTRIBUTE_DIRECTORY) {
			// Directory exists
			return WDI_SUCCESS;
		} else {
			// File with the same name as the dir we want to create
			wdi_err("'%s' is a file, not a directory", path);
			return WDI_ERROR_ACCESS;
		}
	}

	if (!create) {
		wdi_err("'%s' does not exist", path);
		return WDI_ERROR_ACCESS;
	}

	// Change the owner from admin to regular user
	sid = GetSid();
	if ( (sid != NULL)
	  && InitializeSecurityDescriptor(&s_desc, SECURITY_DESCRIPTOR_REVISION)
	  && SetSecurityDescriptorOwner(&s_desc, sid, FALSE) ) {
		s_attr.nLength = sizeof(SECURITY_ATTRIBUTES);
		s_attr.bInheritHandle = FALSE;
		s_attr.lpSecurityDescriptor = &s_desc;
		ps = &s_attr;
	} else {
		wdi_err("Could not set security descriptor: %s", wdi_windows_error_str(0));
	}

	// SHCreateDirectoryEx creates subdirectories as required
	r = SHCreateDirectoryExU(NULL, path, ps);
	if (r == ERROR_BAD_PATHNAME) {
		// A relative path was used => Convert to full
		full_path = (char*)malloc(MAX_PATH);
		if (full_path == NULL) {
			wdi_err("Could not allocate buffer to convert relative path");
			if (sid != NULL) LocalFree(sid);
			return WDI_ERROR_RESOURCE;
		}
		GetCurrentDirectoryU(MAX_PATH, full_path);
		safe_strcat(full_path, MAX_PATH, "\\");
		safe_strcat(full_path, MAX_PATH, path);
		r = SHCreateDirectoryExU(NULL, full_path, ps);
		free(full_path);
	}
	if (sid != NULL) LocalFree(sid);

	switch(r) {
	case ERROR_SUCCESS:
		return WDI_SUCCESS;
	case ERROR_FILENAME_EXCED_RANGE:
		wdi_err("Directory name '%s' is too long", path);
		return WDI_ERROR_INVALID_PARAM;
	default:
		wdi_err("Unable to create directory '%s' (%s)", path, wdi_windows_error_str(0));
		return WDI_ERROR_ACCESS;
	}

	return WDI_SUCCESS;
}

/*
 * fopen equivalent, that uses CreateFile with security attributes
 * to create file as the user of the application. Supports UTF-8.
 */
static FILE *fopen_as_userU(const char *filename, const char *mode)
{
	HANDLE handle;
	size_t i;
	DWORD access_mode = 0;
	SECURITY_ATTRIBUTES *ps = NULL;
	int lowlevel_fd;
	PSID sid = NULL;
	SECURITY_ATTRIBUTES s_attr;
	SECURITY_DESCRIPTOR s_desc;

	if ((filename == NULL) || (mode == NULL)) {
		return NULL;
	}

	// Simple mode handling.
	for (i=0; i<strlen(mode); i++) {
		if (mode[i] == 'r') {
			access_mode |= GENERIC_READ;
		} else if (mode[i] == 'w') {
			access_mode |= GENERIC_WRITE;
		}
	}
	if (!(access_mode & GENERIC_WRITE)) {
		// If the file is not used for writing, might as well use fopen
		return NULL;
	}

	// Change the owner from admin to regular user
	sid = GetSid();
	if ( (sid != NULL)
	  && InitializeSecurityDescriptor(&s_desc, SECURITY_DESCRIPTOR_REVISION)
	  && SetSecurityDescriptorOwner(&s_desc, sid, FALSE) ) {
		s_attr.nLength = sizeof(SECURITY_ATTRIBUTES);
		s_attr.bInheritHandle = FALSE;
		s_attr.lpSecurityDescriptor = &s_desc;
		ps = &s_attr;
	} else {
		wdi_err("Could not set security descriptor: %s", wdi_windows_error_str(0));
	}

	handle = CreateFileU(filename, access_mode, FILE_SHARE_READ,
		ps, CREATE_ALWAYS, 0, NULL);
	if (sid != NULL) LocalFree(sid);

	if (handle == INVALID_HANDLE_VALUE) {
		return NULL;
	}

	lowlevel_fd = _open_osfhandle((intptr_t)handle,
		(access_mode&GENERIC_READ)?_O_RDWR:_O_WRONLY);
	return _fdopen(lowlevel_fd, mode);
}


// Retrieve the version info from the WinUSB, libusbK or libusb0 drivers
int get_version_info(int driver_type, VS_FIXEDFILEINFO* driver_info)
{
	// Version (avoids linking against version.lib)
	PF_DECL_LOAD_LIBRARY(Version);
	PF_TYPE_DECL(WINAPI, BOOL, VerQueryValueW, (LPCVOID, LPWSTR, LPVOID, PUINT));
	PF_TYPE_DECL(WINAPI, BOOL, GetFileVersionInfoW, (LPWSTR, DWORD, DWORD, LPVOID));
	PF_TYPE_DECL(WINAPI, BOOL, GetFileVersionInfoSizeW, (LPWSTR, LPDWORD));
	FILE *fd;
	int res, r;
	char* tmpdir;
	char filename[MAX_PATH];
	wchar_t *wfilename;
	int64_t t;
	DWORD version_size;
	void* version_buf;
	UINT junk;
	VS_FIXEDFILEINFO *file_info;

	if ((driver_type < 0) || (driver_type >= WDI_USER) || (driver_info == NULL)) {
		r = WDI_ERROR_INVALID_PARAM;
		goto out;
	}

	// No need to extract the version again if available
	if (driver_version[driver_type].dwSignature != 0) {
		memcpy(driver_info, &driver_version[driver_type], sizeof(VS_FIXEDFILEINFO));
		r = WDI_SUCCESS;
		goto out;
	}

	// Avoid the need for end user apps to link against version.lib
	r = WDI_ERROR_RESOURCE;
	PF_INIT_OR_OUT(VerQueryValueW, Version);
	PF_INIT_OR_OUT(GetFileVersionInfoW, Version);
	PF_INIT_OR_OUT(GetFileVersionInfoSizeW, Version);

	for (res=0; res<nb_resources; res++) {
		// Identify the WinUSB and libusb0 files we'll pick the date & version of
		if (safe_strcmp(resource[res].name, driver_name[driver_type]) == 0) {
			break;
		}
	}
	if (res == nb_resources) {
		r = WDI_ERROR_NOT_FOUND;
		goto out;
	}

	// First, we need a physical file => extract it
	tmpdir = getenvU("TEMP");
	if (tmpdir == NULL) {
		wdi_warn("Unable to use TEMP to extract file");
		r = WDI_ERROR_RESOURCE;
		goto out;
	}
	r = check_dir(tmpdir, TRUE);
	if (r != WDI_SUCCESS) {
		free(tmpdir);
		goto out;
	}

	safe_strcpy(filename, MAX_PATH, tmpdir);
	free(tmpdir);
	safe_strcat(filename, MAX_PATH, "\\");
	if (resource[res].name != NULL)	// Stupid Clang!
		safe_strcat(filename, MAX_PATH, resource[res].name);

	fd = fopen_as_userU(filename, "w");
	if (fd == NULL) {
		wdi_warn("Failed to create file '%s' (%s)", filename, wdi_windows_error_str(0));
		r = WDI_ERROR_RESOURCE;
		goto out;
	}

	fwrite(resource[res].data, 1, resource[res].size, fd);
	fclose(fd);

	// Read the version
	wfilename = utf8_to_wchar(filename);
	version_size = pfGetFileVersionInfoSizeW(wfilename, NULL);
	version_buf = malloc(version_size);
	r = WDI_SUCCESS;
	if ( (version_buf != NULL)
	  && (pfGetFileVersionInfoW(wfilename, 0, version_size, version_buf))
	  && (pfVerQueryValueW(version_buf, L"\\", (void*)&file_info, &junk)) ) {
		// Fill the creation date of VS_FIXEDFILEINFO with the one from embedded.h
		t = unixtime_to_msfiletime((time_t)resource[res].creation_time);
		file_info->dwFileDateLS = (DWORD)t;
		file_info->dwFileDateMS = t >> 32;
		memcpy(&driver_version[driver_type], file_info, sizeof(VS_FIXEDFILEINFO));
		memcpy(driver_info, file_info, sizeof(VS_FIXEDFILEINFO));
	} else {
		wdi_warn("Unable to allocate buffer for version info");
		r = WDI_ERROR_RESOURCE;
	}
	safe_free(wfilename);
	safe_free(version_buf);
	DeleteFileU(filename);

out:
	PF_FREE_LIBRARY(Version);
	return r;
}


// Find out if the driver selected is actually embedded in this version of the library
BOOL LIBWDI_API wdi_is_driver_supported(int driver_type, VS_FIXEDFILEINFO* driver_info)
{
	if (driver_type < WDI_USER) {	// github issue #40
		if (driver_type != WDI_CDC) {
			// The CDC driver does not have embedded binaries
			if (driver_info != NULL) {
				memset(driver_info, 0, sizeof(VS_FIXEDFILEINFO));
			}
			get_version_info(driver_type, driver_info);
		}
	}

	switch (driver_type) {
	case WDI_WINUSB:
#if defined(WDK_DIR)
		return TRUE;
#else
		return FALSE;
#endif
	case WDI_LIBUSB0:
#if defined(LIBUSB0_DIR)
		return TRUE;
#else
		return FALSE;
#endif
	case WDI_LIBUSBK:
#if defined(LIBUSBK_DIR)
		return TRUE;
#else
		return FALSE;
#endif
	case WDI_USER:
#if defined(USER_DIR)
		return TRUE;
#else
		return FALSE;
#endif
	case WDI_CDC:
		return TRUE;
	default:
		wdi_err("Unknown driver type");
		return FALSE;
	}
}

/*
 * Find out if a file is embedded in the current libwdi resources
 * path is the relative path for
 */
BOOL LIBWDI_API wdi_is_file_embedded(const char* path, const char* name)
{
	int i;

	for (i=0; i<nb_resources; i++) {
		if (safe_strcmp(name, resource[i].name) == 0) {
			if (path == NULL) {
				return TRUE;
			}
			if (safe_strcmp(path, resource[i].subdir) == 0) {
				return TRUE;
			}
		}
	}
	return FALSE;
}

/*
 * Returns a constant string with an English short description of the given
 * error code. The caller should never free() the returned pointer since it
 * points to a constant string.
 * The returned string is encoded in ASCII form and always starts with a
 * capital letter and ends without any dot.
 * \param error_code the error code whose description is desired
 * \returns a short description of the error code in English
 */
const char* LIBWDI_API wdi_strerror(int error_code)
{
	static char unknown[] = "Unknown error (-9223372036854775808)";	// min 64 bit int in decimal
	switch (error_code)
	{
	case WDI_SUCCESS:
		return "Success";
	case WDI_ERROR_IO:
		return "Input/Output error";
	case WDI_ERROR_INVALID_PARAM:
		return "Invalid parameter";
	case WDI_ERROR_ACCESS:
		return "Access denied";
	case WDI_ERROR_NO_DEVICE:
		return "No such device";
	case WDI_ERROR_NOT_FOUND:
		return "Requested resource not found";
	case WDI_ERROR_BUSY:
		return "Requested resource busy or similar call already in progress";
	case WDI_ERROR_TIMEOUT:
		return "Operation timed out";
	case WDI_ERROR_OVERFLOW:
		return "Overflow";
	case WDI_ERROR_INTERRUPTED:
		return "System call interrupted";
	case WDI_ERROR_RESOURCE:
		return "Could not allocate resource";
	case WDI_ERROR_NOT_SUPPORTED:
		return "Operation not supported or not implemented";
	case WDI_ERROR_EXISTS:
		return "Resource already exists";
	case WDI_ERROR_USER_CANCEL:
		return "Cancelled by user";
	// The errors below are generated during driver installation
	case WDI_ERROR_PENDING_INSTALLATION:
		return "Another installation is detected pending";
	case WDI_ERROR_NEEDS_ADMIN:
		return "Unable to run process with required administrative privileges";
	case WDI_ERROR_WOW64:
		return "Attempted to use a 32 bit installer on a 64 bit machine";
	case WDI_ERROR_INF_SYNTAX:
		return "The syntax of the inf is invalid";
	case WDI_ERROR_CAT_MISSING:
		return "Unable to locate cat file";
	case WDI_ERROR_UNSIGNED:
		return "System policy has been modified to reject unsigned drivers";
	case WDI_ERROR_OTHER:
		return "Other error";
	}
	static_sprintf(unknown, "Unknown Error: %d", error_code);
	return (const char*)unknown;
}

// convert a GUID to an hex GUID string
static char* guid_to_string(const GUID guid)
{
	static char guid_string[MAX_GUID_STRING_LENGTH];

	sprintf(guid_string, "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
		(unsigned int)guid.Data1, guid.Data2, guid.Data3,
		guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
		guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
	return guid_string;
}

// free a device info struct
static void free_di(struct wdi_device_info *di)
{
	if (di == NULL) {
		return;
	}
	safe_free(di->desc);
	safe_free(di->driver);
	safe_free(di->device_id);
	safe_free(di->hardware_id);
	safe_free(di->compatible_id);
	safe_free(di->upper_filter);
	safe_free(di);
}

// List USB devices
int LIBWDI_API wdi_create_list(struct wdi_device_info** list,
							   struct wdi_options_create_list* options)
{
	PF_DECL_LIBRARY(Cfgmgr32);
	PF_TYPE_DECL(WINAPI, CONFIGRET, CM_Get_Device_IDA, (DEVINST, PCHAR, ULONG, ULONG));

	int r;
	unsigned i, j, tmp;
	unsigned unknown_count = 1;
	DWORD size, reg_type;
	ULONG devprop_type;
	CONFIGRET cr;
	HDEVINFO dev_info;
	SP_DEVINFO_DATA dev_info_data;
	HKEY key;
	char *prefix[3] = {"VID_", "PID_", "MI_"};
	char *token, *end;
	char strbuf[STR_BUFFER_SIZE], drv_version[] = "xxxxx.xxxxx.xxxxx.xxxxx";
	wchar_t desc[MAX_DESC_LENGTH];
	struct wdi_device_info *start = NULL, *cur = NULL, *device_info = NULL;
	// NOTE: Don't forget to update the list of hubs in zadig.c (system_name[]) when adding new entries below
	const char* usbhub_name[] = { "usbhub", "usbhub3", "usb3hub", "nusb3hub", "rusb3hub", "flxhcih", "tihub3",
		"etronhub3", "viahub3", "asmthub3", "iusb3hub", "vusb3hub", "amdhub30", "vhhub" };
	const char usbccgp_name[] = "usbccgp";
	BOOL is_hub, is_composite_parent, has_vid;

	MUTEX_START;

	GET_WINDOWS_VERSION;
	if (nWindowsVersion < WINDOWS_7) {
		wdi_err("This version of Windows is no longer supported");
		r = WDI_ERROR_NOT_SUPPORTED;
		goto out;
	}

	*list = NULL;

	PF_LOAD_LIBRARY(Cfgmgr32);
	r = WDI_ERROR_RESOURCE;
	PF_INIT_OR_OUT(CM_Get_Device_IDA, Cfgmgr32);

	// List all connected USB devices
	dev_info = SetupDiGetClassDevsA(NULL, "USB", NULL, DIGCF_PRESENT|DIGCF_ALLCLASSES);
	if (dev_info == INVALID_HANDLE_VALUE) {
		r = WDI_ERROR_NO_DEVICE;
		goto out;
	}

	// Find the ones that are driverless
	for (i = 0; ; i++) {
		// Free any invalid previously allocated struct
		free_di(device_info);

		dev_info_data.cbSize = sizeof(dev_info_data);
		if (!SetupDiEnumDeviceInfo(dev_info, i, &dev_info_data)) {
			break;
		}

		// Allocate a driver_info struct to store our data
		device_info = (struct wdi_device_info*)calloc(1, sizeof(struct wdi_device_info));
		if (device_info == NULL) {
			wdi_destroy_list(start);
			SetupDiDestroyDeviceInfoList(dev_info);
			r = WDI_ERROR_RESOURCE;
			goto out;
		}

		// SPDRP_DRIVER seems to do a better job at detecting driverless devices than
		// SPDRP_INSTALL_STATE
		drv_version[0] = 0;
		if (SetupDiGetDeviceRegistryPropertyA(dev_info, &dev_info_data, SPDRP_DRIVER,
			&reg_type, (BYTE*)strbuf, STR_BUFFER_SIZE, &size)) {
			if ((options == NULL) || (!options->list_all)) {
				continue;
			}
			// While we have the driver key, pick up the driver version
			key = SetupDiOpenDevRegKey(dev_info, &dev_info_data, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_READ);
			size = sizeof(drv_version);
			if (key != INVALID_HANDLE_VALUE) {
				RegQueryValueExA(key, "DriverVersion", NULL, &reg_type, (BYTE*)drv_version, &size);
				RegCloseKey(key);
			}
		}

		// Eliminate USB hubs by checking the driver string
		strbuf[0] = 0;
		if (!SetupDiGetDeviceRegistryPropertyA(dev_info, &dev_info_data, SPDRP_SERVICE,
			&reg_type, (BYTE*)strbuf, STR_BUFFER_SIZE, &size)) {
			device_info->driver = NULL;
		} else {
			device_info->driver = safe_strdup(strbuf);
		}
		is_hub = FALSE;
		for (j=0; j<ARRAYSIZE(usbhub_name); j++) {
			if (safe_stricmp(strbuf, usbhub_name[j]) == 0) {
				is_hub = TRUE;
				break;
			}
		}
		if (is_hub && ((options == NULL) || (!options->list_hubs))) {
			continue;
		}
		// Also eliminate composite devices parent drivers, as replacing these drivers
		// is a bad idea
		is_composite_parent = FALSE;
		if (safe_stricmp(strbuf, usbccgp_name) == 0) {
			if ((options == NULL) || (!options->list_hubs)) {
				continue;
			}
			is_composite_parent = TRUE;
		}

		// Retrieve the first hardware ID
		if (SetupDiGetDeviceRegistryPropertyA(dev_info, &dev_info_data, SPDRP_HARDWAREID,
			&reg_type, (BYTE*)strbuf, STR_BUFFER_SIZE, &size)) {
			wdi_dbg("Hardware ID: %s", strbuf);
		} else {
			wdi_err("Could not get hardware ID");
			strbuf[0] = 0;
		}
		// We assume that the first one (REG_MULTI_SZ) is the one we are interested in
		device_info->hardware_id = safe_strdup(strbuf);

		// Retrieve the first Compatible ID
		if (SetupDiGetDeviceRegistryPropertyA(dev_info, &dev_info_data, SPDRP_COMPATIBLEIDS,
			&reg_type, (BYTE*)strbuf, STR_BUFFER_SIZE, &size)) {
			wdi_dbg("Compatible ID: %s", strbuf);
		} else {
			strbuf[0] = 0;
		}
		// We assume that the first one (REG_MULTI_SZ) is the one we are interested in
		device_info->compatible_id = safe_strdup(strbuf);

		// Lookup the upper filter
		if (!SetupDiGetDeviceRegistryPropertyA(dev_info, &dev_info_data, SPDRP_UPPERFILTERS,
			&reg_type, (BYTE*)strbuf, STR_BUFFER_SIZE, &size)) {
			device_info->upper_filter = NULL;
		} else {
			wdi_dbg("Upper filter: %s", strbuf);
			device_info->upper_filter = safe_strdup(strbuf);
		}

		// Convert driver version string to integer
		device_info->driver_version = 0;
		if (drv_version[0] != 0) {
			wdi_dbg("Driver version: %s", drv_version);
			token = strtok(drv_version, ".");
			while (token != NULL) {
				device_info->driver_version <<= 16;
				device_info->driver_version += atoi(token);
				token = strtok(NULL, ".");
			}
		} else if (device_info->driver != NULL) {
			// Only produce a warning for non-driverless devices
			wdi_warn("Could not read driver version");
		}

		// Retrieve device ID. This is needed to re-enumerate our device and force
		// the final driver installation
		cr = pfCM_Get_Device_IDA(dev_info_data.DevInst, strbuf, STR_BUFFER_SIZE, 0);
		if (cr != CR_SUCCESS) {
			wdi_err("Could not retrieve simple path for device %d: CR error %d", i, cr);
			continue;
		} else {
			wdi_dbg("%s USB device (%d): %s",
				device_info->driver?device_info->driver:"Driverless", i, strbuf);
		}
		device_info->device_id = safe_strdup(strbuf);

		// The information we want ("Bus reported device description") is accessed
		// through DEVPKEY_Device_BusReportedDeviceDesc
		desc[0] = 0;
		if (!SetupDiGetDevicePropertyW(dev_info, &dev_info_data, &DEVPKEY_Device_BusReportedDeviceDesc,
			&devprop_type, (BYTE*)desc, 2*MAX_DESC_LENGTH, &size, 0)) {
			// fallback to SPDRP_DEVICEDESC (USB hubs still use it)
			if (!SetupDiGetDeviceRegistryPropertyW(dev_info, &dev_info_data, SPDRP_DEVICEDESC,
				&reg_type, (BYTE*)desc, 2*MAX_DESC_LENGTH, &size) || (desc[0] == 0)) {
				wdi_dbg("Could not read device description for %d: %s",
					i, wdi_windows_error_str(0));
				safe_swprintf(desc, MAX_DESC_LENGTH, L"Unknown Device #%d", unknown_count++);
			}
		}

		device_info->is_composite = FALSE;	// non composite by default
		device_info->mi = 0;
		token = strtok (strbuf, "\\#&");
		has_vid = FALSE;
		while(token != NULL) {
			for (j = 0; j < 3; j++) {
				if (safe_strncmp(token, prefix[j], safe_strlen(prefix[j])) == 0) {
					switch(j) {
					case 0:
						if (sscanf(token, "VID_%04X", &tmp) != 1) {
							wdi_err("Could not convert VID string");
						} else {
							device_info->vid = (unsigned short)tmp;
						}
						has_vid = TRUE;
						break;
					case 1:
						if (sscanf(token, "PID_%04X", &tmp) != 1) {
							wdi_err("Could not convert PID string");
						} else {
							device_info->pid = (unsigned short)tmp;
						}
						break;
					case 2:
						if (sscanf(token, "MI_%02X", &tmp) != 1) {
							wdi_err("Could not convert MI string");
						} else {
							device_info->is_composite = TRUE;
							device_info->mi = (unsigned char)tmp;
							if ((wcslen(desc) + sizeof(" (Interface ###)")) < MAX_DESC_LENGTH) {
								_snwprintf(&desc[wcslen(desc)], sizeof(" (Interface ###)"),
									L" (Interface %d)", device_info->mi);
							}
						}
						break;
					default:
						wdi_err("Program assertion failed - Unexpected case");
						break;
					}
				}
			}
			token = strtok (NULL, "\\#&");
		}

		// Eliminate root hubs (no VID/PID => 0 from calloc)
		if ( (is_hub) && (!has_vid) ) {
			continue;
		}

		// Add a suffix for composite parents
		if ( (is_composite_parent)
		  && ((wcslen(desc) + sizeof(" (Composite Parent)")) < MAX_DESC_LENGTH) ) {
			_snwprintf(&desc[wcslen(desc)], sizeof(" (Composite Parent)"),
				L" (Composite Parent)");
		}

		device_info->desc = wchar_to_utf8(desc);

		// Remove trailing whitespaces
		if ((device_info->desc != NULL) && (options != NULL) && (options->trim_whitespaces)) {
			end = device_info->desc + safe_strlen(device_info->desc);
			while ((end != device_info->desc) && isspace(*(end-1))) {
				--end;
			}
			*end = 0;
		}

		wdi_dbg("Device description: '%s'", device_info->desc);

		// Only at this stage do we know we have a valid current element
		if (cur == NULL) {
			start = device_info;
		} else {
			cur->next = device_info;
		}
		cur = device_info;
		// Ensure that we don't free a valid structure
		device_info = NULL;
	}

	SetupDiDestroyDeviceInfoList(dev_info);

	*list = start;
	r = (*list == NULL) ? WDI_ERROR_NO_DEVICE : WDI_SUCCESS;
out:
	PF_FREE_LIBRARY(Cfgmgr32);
	CloseHandle(mutex);
	return r;
}

int LIBWDI_API wdi_destroy_list(struct wdi_device_info* list)
{
	struct wdi_device_info *tmp;

	MUTEX_START;

	while(list != NULL) {
		tmp = list;
		list = list->next;
		free_di(tmp);
	}
	CloseHandle(mutex);
	return WDI_SUCCESS;
}

// extract the embedded binary resources
static int extract_binaries(const char* path)
{
	FILE *fd;
	char filename[MAX_PATH];
	int i, r;

	for (i=0; i<nb_resources; i++) {
		// Ignore tokenizer files
		if (resource[i].subdir[0] == 0) {
			continue;
		}
		safe_strcpy(filename, MAX_PATH, path);
		safe_strcat(filename, MAX_PATH, "\\");
		safe_strcat(filename, MAX_PATH, resource[i].subdir);

		r = check_dir(filename, TRUE);
		if (r != WDI_SUCCESS) {
			return r;
		}
		safe_strcat(filename, MAX_PATH, "\\");
		safe_strcat(filename, MAX_PATH, resource[i].name);

		if ( (safe_strlen(path) + safe_strlen(resource[i].subdir) + safe_strlen(resource[i].name)) > (MAX_PATH - 3)) {
			wdi_err("Qualified path is too long: '%s'", filename);
			return WDI_ERROR_RESOURCE;
		}

		fd = fopen_as_userU(filename, "w");
		if (fd == NULL) {
			wdi_err("Could not create file '%s' (%s)", filename, wdi_windows_error_str(0));
			return WDI_ERROR_RESOURCE;
		}

		fwrite(resource[i].data, 1, resource[i].size, fd);
		fclose(fd);
	}

	wdi_info("Successfully extracted driver files to '%s'", path);
	return WDI_SUCCESS;
}

// tokenizes a resource stored in resource.h and write it to file <dst>
static long wdi_tokenize_resource(const char* resource_name, char** dst, const token_entity_t* token_entities,
								  const char* tok_prefix, const char* tok_suffix, int recursive)
{
	int i;

	for (i=0; i<nb_resources; i++) {
		// Ignore driver files
		if (resource[i].subdir[0] != 0) {
			continue;
		}
		if (strcmp(resource[i].name, resource_name) == 0) {
			return tokenize_string(resource[i].data, (long)resource[i].size,
				dst, token_entities, tok_prefix, tok_suffix, recursive);
		}
	}
	return -ERROR_RESOURCE_DATA_NOT_FOUND;
}

// tokenizes an external file pointed by <src> into destination file <dst>
static long wdi_tokenize_file(const char* src, char** dst, const token_entity_t* token_entities,
							  const char* tok_prefix, const char* tok_suffix, int recursive)
{
	FILE* fd = NULL;
	long size, ret = -ERROR_RESOURCE_DATA_NOT_FOUND;
	char* buffer = NULL;

	fd = fopen(src, "r");
	if (fd == NULL)
		goto out;
	fseek(fd, 0L, SEEK_END);
	size = ftell(fd);
	if (size < 0)
		goto out;
	fseek(fd, 0L, SEEK_SET);
	// +1  for NUL terminator since we pass the whole file as a text string
	buffer = (char*)calloc(size + 1, 1);
	if (buffer == NULL) {
		wdi_err("Could not allocate tokenization buffer");
		ret = WDI_ERROR_RESOURCE;
		goto out;
	}
	if (fread(buffer, 1, size, fd) != size) {
		wdi_err("Could not read file to tokenize");
		ret = -ERROR_RESOURCE_DATA_NOT_FOUND;
		goto out;
	}
	ret = tokenize_string(buffer, size, dst, token_entities, tok_prefix, tok_suffix, recursive);

out:
	free(buffer);
	if (fd)
		fclose(fd);
	return ret;
}

#define CAT_LIST_MAX_ENTRIES 16
// Create an inf and extract coinstallers in the directory pointed by path
int LIBWDI_API wdi_prepare_driver(struct wdi_device_info* device_info, const char* path,
								  const char* inf, struct wdi_options_prepare_driver* options)
{
	PF_DECL_LIBRARY(Ntdll);
	PF_TYPE_DECL(NTAPI, NTSTATUS, NtQuerySystemInformation, (SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG));
	const wchar_t bom = 0xFEFF;
#if defined(ENABLE_DEBUG_LOGGING) || defined(INCLUDE_DEBUG_LOGGING)
	const char* driver_display_name[WDI_NB_DRIVERS] = { "WinUSB", "libusb0.sys", "libusbK.sys", "Generic USB CDC", "user driver" };
#endif
	const char* inf_ext = ".inf";
	const char* vendor_name = NULL;
	const char* cat_list[CAT_LIST_MAX_ENTRIES+1];
	char drv_path[MAX_PATH], inf_path[MAX_PATH], cat_path[MAX_PATH], hw_id[40], cert_subject[64];
	char *strguid, *token, *cat_name = NULL, *dst = NULL, *inf_name;
	wchar_t *wdst = NULL;
	int i, nb_entries, driver_type = WDI_WINUSB, r = WDI_ERROR_OTHER;
	long inf_file_size, cat_file_size;
	BOOL is_android_device = FALSE, is_test_signing_enabled = FALSE;
	FILE* fd;
	GUID guid;
	SYSTEMTIME system_time;
	FILETIME file_time, local_time;
	SYSTEM_CODEINTEGRITY_INFORMATION sci = { 0 };
	ULONG dwcbSz = 0;

	MUTEX_START;

	GET_WINDOWS_VERSION;
	if (nWindowsVersion < WINDOWS_7) {
		wdi_err("This version of Windows is no longer supported");
		r = WDI_ERROR_NOT_SUPPORTED;
		goto out;
	}

	if ((device_info == NULL) || (inf == NULL)) {
		wdi_err("One of the required parameter is NULL");
		r = WDI_ERROR_INVALID_PARAM;
		goto out;
	}

	// If we are dealing with a path (e.g. option 'external_inf'), remove the directory part
	inf_name = (char*)filename(inf);

	// Check the inf file provided and create the cat file name
	if (strcmp(inf_name+safe_strlen(inf_name)-4, inf_ext) != 0) {
		wdi_err("Inf name provided must have a '.inf' extension");
		r = WDI_ERROR_INVALID_PARAM;
		goto out;
	}

	if (path != NULL) {
		static_strcpy(drv_path, path);
	} else {
		// Try to use the user's temp dir
		char* tmp = getenvU("TEMP");
		if (tmp == NULL) {
			wdi_err("No path provided and unable to use TEMP");
			r = WDI_ERROR_INVALID_PARAM;
			goto out;
		} else {
			static_strcpy(drv_path, tmp);
			free(tmp);
			wdi_info("No path provided - extracting to '%s'", drv_path);
		}
	}

	// Try to create directory if it doesn't exist
	r = check_dir(drv_path, TRUE);
	if (r != WDI_SUCCESS) {
		goto out;
	}

	if (options != NULL) {
		driver_type = options->driver_type;
	}

	// Ensure driver_type is what we expect
	if ( (driver_type < 0) || (driver_type > WDI_USER) ) {
		wdi_err("Program assertion failed - Unknown driver type");
		r = WDI_ERROR_INVALID_PARAM;
		goto out;
	}

	if (!wdi_is_driver_supported(driver_type, &driver_version[driver_type])) {
		for (driver_type=0; driver_type<WDI_NB_DRIVERS; driver_type++) {
			if (wdi_is_driver_supported(driver_type, NULL)) {
				wdi_warn("unsupported or no driver type specified, will use %s",
					driver_display_name[driver_type]);
				break;
			}
		}
		if (driver_type == WDI_NB_DRIVERS) {
			wdi_warn("Program assertion failed - no driver supported");
			r = WDI_ERROR_NOT_FOUND;
			goto out;
		}
	}

	// If the target is libusb-win32 and we have the K DLLs, add them to the inf
	if ((driver_type == WDI_LIBUSB0) && (wdi_is_driver_supported(WDI_LIBUSBK, NULL))) {
		wdi_info("K driver available - adding the libusbK DLLs to the libusb-win32 inf");
		static_strcpy(inf_entities[LK_COMMA].replace, ",");
		static_strcpy(inf_entities[LK_DLL].replace, "libusbk.dll");
		static_strcpy(inf_entities[LK_X86_DLL].replace, "libusbk_x86.dll");
		static_strcpy(inf_entities[LK_EQ_X86].replace, "= 1,x86");
		static_strcpy(inf_entities[LK_EQ_X64].replace, "= 1,amd64");
	}

	// For custom drivers, as we cannot autogenerate the inf, simply extract binaries
	if (driver_type == WDI_USER) {
		wdi_info("Custom driver - extracting binaries only (no inf/cat creation)");
		r = extract_binaries(drv_path);
		goto out;
	}

	if (device_info->desc == NULL) {
		wdi_err("No device ID was given for the device - aborting");
		r = WDI_ERROR_INVALID_PARAM;
		goto out;
	}

	r = extract_binaries(drv_path);
	if (r != WDI_SUCCESS) {
		goto out;
	}

	// Populate the inf and cat names & paths
	if ( (strlen(drv_path) >= MAX_PATH) || (strlen(inf_name) >= MAX_PATH) ||
		 ((strlen(drv_path) + strlen(inf_name)) > (MAX_PATH - 2)) ) {
		wdi_err("Qualified path for inf file is too long: '%s\\%s", drv_path, inf_name);
		r = WDI_ERROR_RESOURCE;
		goto out;
	}
	safe_strcpy(inf_path, sizeof(inf_path), drv_path);
	safe_strcat(inf_path, sizeof(inf_path), "\\");
	safe_strcat(inf_path, sizeof(inf_path), inf_name);
	safe_strcpy(cat_path, sizeof(cat_path), inf_path);
	if (safe_strlen(cat_path) < 4) {
		wdi_err("Qualified path for inf file is too short: '%s", cat_path);
		r = WDI_ERROR_RESOURCE;
		goto out;
	}
	cat_path[safe_strlen(cat_path)-3] = 'c';
	cat_path[safe_strlen(cat_path)-2] = 'a';
	cat_path[safe_strlen(cat_path)-1] = 't';

	static_strcpy(inf_entities[INF_FILENAME].replace, inf_name);
	cat_name = safe_strdup(inf_name);
	if (cat_name == NULL) {
		r = WDI_ERROR_RESOURCE;
		goto out;
	}
	cat_name[safe_strlen(inf_name)-3] = 'c';
	cat_name[safe_strlen(inf_name)-2] = 'a';
	cat_name[safe_strlen(inf_name)-1] = 't';
	static_strcpy(inf_entities[CAT_FILENAME].replace, cat_name);
	safe_free(cat_name);

	// Populate the Device Description and Hardware ID
	static_strcpy(inf_entities[DEVICE_DESCRIPTION].replace, device_info->desc);
	if ((options != NULL) && (options->use_wcid_driver)) {
		static_strcpy(inf_entities[DEVICE_HARDWARE_ID].replace, ms_compat_id[driver_type]);
		static_strcpy(inf_entities[USE_DEVICE_INTERFACE_GUID].replace, "NoDeviceInterfaceGUID");
	} else {
		if (device_info->is_composite) {
			static_sprintf(inf_entities[DEVICE_HARDWARE_ID].replace, "VID_%04X&PID_%04X&MI_%02X",
				device_info->vid, device_info->pid, device_info->mi);
		} else {
			static_sprintf(inf_entities[DEVICE_HARDWARE_ID].replace, "VID_%04X&PID_%04X",
				device_info->vid, device_info->pid);
		}
		static_strcpy(inf_entities[USE_DEVICE_INTERFACE_GUID].replace, "AddDeviceInterfaceGUID");
	}

	// Find out if we have an Android device
	for (i=0; i<ARRAYSIZE(android_device); i++) {
		if ((android_device[i].vid == device_info->vid) && (android_device[i].pid == device_info->pid)) {
			is_android_device = TRUE;
			break;
		}
	}

	// Populate the Device Interface GUID
	if ((options != NULL) && (options->use_wcid_driver)) {
		strguid = "UNUSED";
	} else if ((options != NULL) && (options->device_guid != NULL)) {
		strguid = options->device_guid;
	} else if (is_android_device) {
		wdi_info("Using Android Device Interface GUID");
		strguid = (char*)android_device_guid;
	} else {
		IGNORE_RETVAL(CoCreateGuid(&guid));
		strguid = guid_to_string(guid);
	}
	static_sprintf(inf_entities[DEVICE_INTERFACE_GUID].replace, "%s", strguid);

	// Resolve the Manufacturer (Vendor Name)
	if ((options != NULL) && (options->vendor_name != NULL)) {
		static_strcpy(inf_entities[DEVICE_MANUFACTURER].replace, options->vendor_name);
	} else {
		vendor_name = wdi_get_vendor_name(device_info->vid);
		if (vendor_name == NULL) {
			vendor_name = "(Undefined Vendor)";
		}
		static_strcpy(inf_entities[DEVICE_MANUFACTURER].replace, vendor_name);
	}

	// Set the WDF and KMDF versions for WinUSB and libusbK
	static_sprintf(inf_entities[WDF_VERSION].replace, "%05d", WDF_VER);
	static_sprintf(inf_entities[KMDF_VERSION].replace, "%d.%d", WDF_VER/1000, WDF_VER%1000);

	// Extra check, in case somebody modifies our code
	if ((driver_type < 0) && (driver_type >= WDI_USER)) {
		wdi_err("Program assertion failed - driver_version[] index out of range");
		r = WDI_ERROR_OTHER;
		goto out;
	}

	// Write the date and version data
	file_time.dwHighDateTime = driver_version[driver_type].dwFileDateMS;
	file_time.dwLowDateTime = driver_version[driver_type].dwFileDateLS;
	if ( ((file_time.dwHighDateTime == 0) && (file_time.dwLowDateTime == 0))
	  || (!FileTimeToLocalFileTime(&file_time, &local_time))
	  || (!FileTimeToSystemTime(&local_time, &system_time)) ) {
		GetLocalTime(&system_time);
	}
	static_sprintf(inf_entities[DRIVER_DATE].replace,
		"%02d/%02d/%04d", system_time.wMonth, system_time.wDay, system_time.wYear);
	static_sprintf(inf_entities[DRIVER_VERSION].replace, "%d.%d.%d.%d",
		(int)driver_version[driver_type].dwFileVersionMS>>16, (int)driver_version[driver_type].dwFileVersionMS&0xFFFF,
		(int)driver_version[driver_type].dwFileVersionLS>>16, (int)driver_version[driver_type].dwFileVersionLS&0xFFFF);

	// Tokenize the inf
	if ((options != NULL) && (options->external_inf))
		inf_file_size = wdi_tokenize_file(inf, &dst, inf_entities, "#", "#", 0);
	else
		inf_file_size = wdi_tokenize_resource(inf_template[driver_type], &dst, inf_entities, "#", "#", 0);
	if (inf_file_size > 0) {
		fd = fopen_as_userU(inf_path, "w");
		if (fd == NULL) {
			wdi_err("Failed to create file: %s", inf_path);
			r = WDI_ERROR_ACCESS;
			goto out;
		}
		// Converting to UTF-16 is the only way to get devices using a
		// non-English locale to display properly in device manager. UTF-8 will not do.
		wdst = utf8_to_wchar(dst);
		if (wdst == NULL) {
			wdi_err("Could not convert '%s' to UTF-16", dst);
			safe_free(dst);
			r = WDI_ERROR_RESOURCE;
			goto out;
		}
		fwrite(&bom, 2, 1, fd);	// Write the BOM
		fwrite(wdst, 2, wcslen(wdst), fd);
		fclose(fd);
		safe_free(wdst);
		safe_free(dst);
	} else {
		wdi_err("Could not tokenize inf file (%d)", inf_file_size);
		r = WDI_ERROR_ACCESS;
		goto out;
	}
	wdi_info("Successfully created '%s'", inf_path);

	if (IsUserAnAdmin()) {
		// Try to create and self-sign the cat file to remove security prompts
		if ((options != NULL) && (options->disable_cat)) {
			wdi_info(".cat generation disabled by user");
			r = WDI_SUCCESS;
			goto out;
		}
		wdi_info("Creating and self-signing a .cat file...");

		// Tokenize the cat file (for WDF version)
		if ((cat_file_size = wdi_tokenize_resource(cat_template[driver_type],
			&dst, inf_entities, "#", "#", 0)) <= 0) {
			wdi_err("Could not tokenize cat file (%d)", cat_file_size);
			r = WDI_ERROR_ACCESS;
			goto out;
		}

		// Build the filename list
		nb_entries = 0;
		token = strtok(dst, "\n\r");
		do {
			// Eliminate leading, trailing spaces & comments (#...)
			while (isspace(*token)) token++;
			while (strlen(token) && isspace(token[strlen(token)-1]))
				token[strlen(token)-1] = 0;
			if ((*token == '#') || (*token == 0))
				continue;
			cat_list[nb_entries++] = token;
			if (nb_entries >= CAT_LIST_MAX_ENTRIES) {
				wdi_warn("More than %d cat entries - ignoring the rest", CAT_LIST_MAX_ENTRIES);
				break;
			}
		} while ((token = strtok(NULL, "\n\r")) != NULL);

		// Add the inf name to our list
		cat_list[nb_entries++] = inf_name;

		// the DEVICE_HARDWARE_ID is either "VID_####&PID_####[&MI_##]" or the MS Compatible ID
		static_sprintf(hw_id, "USB\\%s", ((options != NULL) && (options->use_wcid_driver))?
			ms_compat_id[driver_type]:inf_entities[DEVICE_HARDWARE_ID].replace);
		static_sprintf(cert_subject, "CN=%s (libwdi autogenerated)", hw_id);

		// Check if testsigning is enabled
		// https://social.msdn.microsoft.com/Forums/Windowsapps/en-US/e6c1be93-7003-4594-b8e4-18ab4a75d273/detecting-testsigning-onoff-via-api
		PF_INIT(NtQuerySystemInformation, Ntdll);
		if (pfNtQuerySystemInformation != NULL) {
			sci.Length = sizeof(sci);
			if (pfNtQuerySystemInformation((SYSTEM_INFORMATION_CLASS)0x67, &sci, sizeof(sci), &dwcbSz) >= 0 && dwcbSz == sizeof(sci))
				is_test_signing_enabled = !!(sci.CodeIntegrityOptions & 0x02);
			wdi_info("Test signing is: %s", is_test_signing_enabled ? "Enabled" : "Disabled");
		}

		// Failures on the following are fatal on Windows 10 when test signing is not enabled
		if (!CreateCat(cat_path, hw_id, drv_path, cat_list, nb_entries)) {
			if (nWindowsVersion >= WINDOWS_10 && !is_test_signing_enabled) {
				wdi_err("Could not create cat file");
				return WDI_ERROR_CAT_MISSING;
			}
			wdi_warn("Could not create cat file");
		} else if ((options != NULL) && (!options->disable_signing) && (!SelfSignFile(cat_path,
			(options->cert_subject != NULL)?options->cert_subject:cert_subject))) {
			if (nWindowsVersion >= WINDOWS_10 && !is_test_signing_enabled) {
				wdi_err("Could not sign cat file");
				return WDI_ERROR_UNSIGNED;
			}
			wdi_warn("Could not sign cat file");
		}
		safe_free(dst);
	} else {
		wdi_info("No .cat file generated (missing elevated privileges)");
	}
	r = WDI_SUCCESS;

out:
	CloseHandle(mutex);
	return r;
}

// Handle messages received from the elevated installer through the pipe
static int process_message(char* buffer, DWORD size)
{
	DWORD tmp;
	char* sid_str;

	if (size <= 0)
		return WDI_ERROR_INVALID_PARAM;

	if (current_device == NULL) {
		wdi_err("Program assertion failed - no current device");
		return WDI_ERROR_NOT_FOUND;
	}

	if (filter_driver) {
		// In filter driver mode, we just do I/O redirection
		if (size > 0) {
			buffer[size] = 0;
			wdi_log(WDI_LOG_LEVEL_INFO, "install-filter", "%s", buffer);
		}
		return WDI_SUCCESS;
	}

	// Note: this is a message pipe, so we don't need to care about
	// multiple messages coexisting in our buffer.
	switch(buffer[0])
	{
	case IC_GET_DEVICE_ID:
		wdi_dbg("Got request for device_id");
		if (current_device->device_id != NULL) {
			WriteFile(pipe_handle, current_device->device_id, (DWORD)safe_strlen(current_device->device_id), &tmp, NULL);
		} else {
			wdi_dbg("No device_id - sending empty string");
			WriteFile(pipe_handle, "\0", 1, &tmp, NULL);
		}
		break;
	case IC_GET_HARDWARE_ID:
		wdi_dbg("Got request for hardware_id");
		if (current_device->hardware_id != NULL) {
			WriteFile(pipe_handle, current_device->hardware_id, (DWORD)safe_strlen(current_device->hardware_id), &tmp, NULL);
		} else {
			wdi_dbg("No hardware_id - sending empty string");
			WriteFile(pipe_handle, "\0", 1, &tmp, NULL);
		}
		break;
	case IC_PRINT_MESSAGE:
		if (size < 2) {
			wdi_err("Print_message: no data");
			return WDI_ERROR_NOT_FOUND;
		}
		wdi_log(WDI_LOG_LEVEL_DEBUG, "installer process", "%s", buffer+1);
		break;
	case IC_SYSLOG_MESSAGE:
		if (size < 2) {
			wdi_err("Syslog_message: no data");
			return WDI_ERROR_NOT_FOUND;
		}
		wdi_log(WDI_LOG_LEVEL_DEBUG, "syslog", "%s", buffer+1);
		break;
	case IC_SET_STATUS:
		if (size < 2) {
			wdi_err("Set status: no data");
			return WDI_ERROR_NOT_FOUND;
		}
		return (int)buffer[1];
		break;
	case IC_SET_TIMEOUT_INFINITE:
		wdi_dbg("Switching timeout to infinite");
		timeout = INFINITE;
		break;
	case IC_SET_TIMEOUT_DEFAULT:
		wdi_dbg("Switching timeout back to finite");
		timeout = DEFAULT_TIMEOUT;
		break;
	case IC_INSTALLER_COMPLETED:
		wdi_dbg("Installer process completed");
		break;
	case IC_GET_USER_SID:
		if (ConvertSidToStringSidA(GetSid(), &sid_str)) {
			WriteFile(pipe_handle, sid_str, (DWORD)safe_strlen(sid_str), &tmp, NULL);
			LocalFree(sid_str);
		} else {
			wdi_warn("No user_sid - sending empty string");
			WriteFile(pipe_handle, "\0", 1, &tmp, NULL);
		}
		break;
	default:
		wdi_err("Unrecognized installer message");
		return WDI_ERROR_NOT_FOUND;
	}
	return WDI_SUCCESS;
}

// Run the elevated installer
static int install_driver_internal(void* arglist)
{
	PF_DECL_LIBRARY(SetupAPI);
	PF_TYPE_DECL(WINAPI, DWORD, CMP_WaitNoPendingInstallEvents, (DWORD));
	struct install_driver_params* params = (struct install_driver_params*)arglist;
	SHELLEXECUTEINFOA shExecInfo;
	STARTUPINFOA si;
	PROCESS_INFORMATION pi;
	SECURITY_ATTRIBUTES sa;
	char path[MAX_PATH], exename[MAX_PATH], exeargs[MAX_PATH];
	HANDLE stdout_w = INVALID_HANDLE_VALUE;
	HANDLE handle[3] = { INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE };
	OVERLAPPED overlapped;
	int r;
	DWORD err, rd_count, to_read, offset, bufsize = LOGBUF_SIZE;
	BOOL is_x64 = FALSE;
	char *buffer = NULL, *new_buffer;
	const char* filter_name = "libusb0";

	MUTEX_START;

	GET_WINDOWS_VERSION;
	if (nWindowsVersion < WINDOWS_7) {
		wdi_err("This version of Windows is no longer supported");
		r = WDI_ERROR_NOT_SUPPORTED;
		goto out;
	}

	PF_LOAD_LIBRARY(SetupAPI);
	r = WDI_ERROR_RESOURCE;
	PF_INIT_OR_OUT(CMP_WaitNoPendingInstallEvents, SetupAPI);

	current_device = params->device_info;
	filter_driver = FALSE;
	if (params->options != NULL)
		filter_driver = params->options->install_filter_driver;

	// Try to use the user's temp dir if no path is provided
	if ((params->path == NULL) || (params->path[0] == 0)) {
		char* tmp = getenvU("TEMP");
		static_strcpy(path, tmp);
		free(tmp);
		wdi_info("No path provided - installing from '%s'", path);
	} else {
		static_strcpy(path, params->path);
	}

	if ((params->device_info == NULL) || (params->inf_name == NULL)) {
		wdi_err("One of the required parameter is NULL");
		r = WDI_ERROR_INVALID_PARAM;
		goto out;
	}

	// Detect if another installation is in process
	if ((params->options != NULL) && (pfCMP_WaitNoPendingInstallEvents != NULL)) {
		if (pfCMP_WaitNoPendingInstallEvents(params->options->pending_install_timeout) == WAIT_TIMEOUT) {
			wdi_warn("Timeout expired while waiting for another pending installation - aborting");
			r = WDI_ERROR_PENDING_INSTALLATION;
			goto out;
		}
	} else {
		wdi_dbg("CMP_WaitNoPendingInstallEvents not available");
	}

	// Detect whether if we should run the 64 bit installer, without
	// relying on external libs
	if (sizeof(uintptr_t) < 8) {
		// This application is not 64 bit, but it might be 32 bit
		// running in WOW64
		IsWow64Process(GetCurrentProcess(), &is_x64);
	} else {
		is_x64 = TRUE;
	}

	// Use a pipe to communicate with our installer
	pipe_handle = CreateNamedPipeA(INSTALLER_PIPE_NAME, PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,
		PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE, 1, 4096, 4096, 0, NULL);
	if (pipe_handle == INVALID_HANDLE_VALUE) {
		wdi_err("Could not create read pipe: %s", wdi_windows_error_str(0));
		r = WDI_ERROR_RESOURCE;
		goto out;
	}

	// Set the overlapped for messaging
	memset(&overlapped, 0, sizeof(OVERLAPPED));
	handle[0] = CreateEvent(NULL, TRUE, FALSE, NULL);
	if(handle[0] == NULL) {
		r = WDI_ERROR_RESOURCE;
		goto out;
	}
	overlapped.hEvent = handle[0];

	if (!filter_driver) {
		// Why do we need two installers? Glad you asked. If you try to run the x86 installer on an x64
		// system, you will get a "System does not work under WOW64 and requires 64-bit version" message.
		static_sprintf(exename, "\"%s\\installer_x%s.exe\"", path, is_x64?"64":"86");
		static_sprintf(exeargs, "\"%s\"", params->inf_name);
	} else {
		// Use libusb-win32's filter driver installer
		static_strcat(path, is_x64 ? "\\amd64" : "\\x86");
		static_sprintf(exename, "\"%s\\install-filter.exe\"", path);
		if (safe_stricmp(current_device->upper_filter, filter_name) == 0) {
			// Device already has the libusb-win32 filter => remove
			static_strcpy(exeargs, "uninstall -d=");
		} else {
			static_strcpy(exeargs, "install -d=");
		}
		static_strcat(exeargs, params->device_info->hardware_id);
		// We need to get a handle to the other end of the pipe for redirection
		sa.nLength = sizeof(SECURITY_ATTRIBUTES);
		sa.bInheritHandle = TRUE;		// REQUIRED for STDIO redirection
		sa.lpSecurityDescriptor = NULL;
		stdout_w = CreateFileA(INSTALLER_PIPE_NAME, GENERIC_WRITE, FILE_SHARE_WRITE,
			&sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OVERLAPPED, NULL);
		if (stdout_w == INVALID_HANDLE_VALUE) {
			wdi_err("Could not create stdout endpoint: %s", wdi_windows_error_str(0));
			r = WDI_ERROR_RESOURCE;
			goto out;
		}
	}
	// At this stage, if either the 32 or 64 bit installer version is missing,
	// it is the application developer's fault...
	if (GetFileAttributesU(exename) == INVALID_FILE_ATTRIBUTES) {
		wdi_err("This application does not contain the required %s bit installer", is_x64?"64":"32");
		wdi_err("Please contact the application provider for a %s bit compatible version", is_x64?"64":"32");
		r = WDI_ERROR_NOT_FOUND; goto out;
	}

	if (IsUserAnAdmin()) {
		// Take care of UAC with ShellExecuteEx + runas
		shExecInfo.cbSize = sizeof(SHELLEXECUTEINFOA);
		shExecInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
		shExecInfo.hwnd = NULL;
		shExecInfo.lpVerb = "runas";
		shExecInfo.lpFile = filter_driver?"install-filter.exe":(is_x64?"installer_x64.exe":"installer_x86.exe");
		shExecInfo.lpParameters = exeargs;
		shExecInfo.lpDirectory = path;
		shExecInfo.lpClass = NULL;
		shExecInfo.nShow = SW_HIDE;
		shExecInfo.hInstApp = NULL;

		err = ERROR_SUCCESS;
		if (!ShellExecuteExU(&shExecInfo)) {
			err = GetLastError();
		}

		switch(err) {
		case ERROR_SUCCESS:
			break;
		case ERROR_CANCELLED:
			wdi_info("Operation cancelled by the user or due to missing data");
			r = WDI_ERROR_USER_CANCEL;
			goto out;
		case ERROR_FILE_NOT_FOUND:
			wdi_info("Could not find installer executable");
			r = WDI_ERROR_NOT_FOUND;
			goto out;
		default:
			wdi_err("ShellExecuteEx failed: %s", wdi_windows_error_str(err));
			r = WDI_ERROR_NEEDS_ADMIN;
			goto out;
		}

		handle[1] = shExecInfo.hProcess;
	} else {
		// If app is already elevated, simply use CreateProcess()
		memset(&si, 0, sizeof(si));
		si.cb = sizeof(si);
		if (filter_driver) {
			si.dwFlags = STARTF_USESTDHANDLES;
			si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
			si.hStdOutput = stdout_w;
			si.hStdError = stdout_w;
		}

		memset(&pi, 0, sizeof(pi));

		static_strcat(exename, " ");
		static_strcat(exename, exeargs);
		if (!CreateProcessU(NULL, exename, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, path, &si, &pi)) {
			wdi_err("CreateProcess failed: %s", wdi_windows_error_str(0));
			r = WDI_ERROR_NEEDS_ADMIN; goto out;
		}
		handle[1] = pi.hProcess;
		handle[2] = pi.hThread;		// MSDN indicates to also close this handle when done
	}

	r = WDI_SUCCESS;
	offset = 0;
	buffer = (char*)malloc(bufsize);
	if (buffer == NULL) {
		wdi_err("Unable to allocate buffer: aborting");
		r = WDI_ERROR_RESOURCE; goto out;
	}

	while (r == WDI_SUCCESS) {
		to_read = bufsize-offset;	// rd_count is useless on sync (reset to 0)
		if (ReadFile(pipe_handle, &buffer[offset], to_read, &rd_count, &overlapped)) {
			offset = 0;
			// Message was read synchronously
			r = process_message(buffer, rd_count);
		} else {
			switch(GetLastError()) {
			case ERROR_BROKEN_PIPE:
				// The pipe has been ended - wait for installer to finish
				if ((WaitForSingleObject(handle[1], timeout) == WAIT_TIMEOUT)) {
					TerminateProcess(handle[1], 0);
				}
				r = check_completion(handle[1]); goto out;
			case ERROR_PIPE_LISTENING:
				// Wait for installer to open the pipe
				Sleep(100);
				continue;
			case ERROR_IO_PENDING:
				switch(WaitForMultipleObjects(2, handle, FALSE, timeout)) {
				case WAIT_OBJECT_0: // Pipe event
					if (GetOverlappedResult(pipe_handle, &overlapped, &rd_count, FALSE)) {
						// Message was read asynchronously
						r = process_message(buffer, rd_count);
						offset = 0;
					} else {
						switch(GetLastError()) {
						case ERROR_BROKEN_PIPE:
							// The pipe has been ended - wait for installer to finish
							if ((WaitForSingleObject(handle[1], timeout) == WAIT_TIMEOUT)) {
								TerminateProcess(handle[1], 0);
							}
							r = check_completion(handle[1]); goto out;
						case ERROR_MORE_DATA:
							bufsize *= 2;
							wdi_dbg("Message overflow (async) - increasing buffer size to %d bytes", bufsize);
							new_buffer = (char*)realloc(buffer, bufsize);
							if (new_buffer == NULL) {
								wdi_err("Unable to realloc buffer: aborting");
								r = WDI_ERROR_RESOURCE;
							} else {
								buffer = new_buffer;
								offset += to_read;
							}
							break;
						default:
							wdi_err("Could not read from pipe (async): %s", wdi_windows_error_str(0));
							break;
						}
					}
					break;
				case WAIT_TIMEOUT:
					// Lost contact
					wdi_err("Installer failed to respond - aborting");
					TerminateProcess(handle[1], 0);
					r = WDI_ERROR_TIMEOUT; goto out;
				case WAIT_OBJECT_0+1:
					// installer process terminated
					r = check_completion(handle[1]); goto out;
				default:
					wdi_err("Could not read from pipe (wait): %s", wdi_windows_error_str(0));
					break;
				}
				break;
			case ERROR_MORE_DATA:
				bufsize *= 2;
				wdi_dbg("message overflow (sync) - increasing buffer size to %d bytes", bufsize);
				new_buffer = (char*)realloc(buffer, bufsize);
				if (new_buffer == NULL) {
					wdi_err("unable to realloc buffer: aborting");
					r = WDI_ERROR_RESOURCE;
				} else {
					buffer = new_buffer;
					offset += to_read;
				}
				break;
			default:
				wdi_err("Could not read from pipe (sync): %s", wdi_windows_error_str(0));
				break;
			}
		}
	}
out:
	// If the security prompt is still active, attempt to destroy it
	DestroyWindow(find_security_prompt());
	current_device = NULL;
	safe_free(buffer);
	safe_closehandle(handle[2]);
	safe_closehandle(handle[1]);
	safe_closehandle(handle[0]);
	safe_closehandle(pipe_handle);
	safe_closehandle(stdout_w);
	PF_FREE_LIBRARY(SetupAPI);
	CloseHandle(mutex);
	return r;
}

int LIBWDI_API wdi_install_driver(struct wdi_device_info* device_info, const char* path,
								  const char* inf, struct wdi_options_install_driver* options)
{
	struct install_driver_params params;
	params.device_info = device_info;
	params.options = options;
	params.path = path;

	// If we are dealing with a path (e.g. option 'external_inf'), remove the directory part
	params.inf_name = filename(inf);

	if ((options == NULL) || (options->hWnd == NULL)) {
		wdi_dbg("Using standard mode");
		return install_driver_internal((void*)&params);
	}
	wdi_dbg("Using progress bar mode");
	return run_with_progress_bar(options->hWnd, install_driver_internal, (void*)&params);
}

// Install a driver signing certificate to the Trusted Publisher system store
// This allows promptless installation if you also provide a signed inf/cat pair
int LIBWDI_API wdi_install_trusted_certificate(const char* cert_name,
											   struct wdi_options_install_cert* options)
{
	int i, r;
	HWND hWnd = NULL;
	BOOL disable_warning = FALSE;

	GET_WINDOWS_VERSION;
	if (nWindowsVersion < WINDOWS_7) {
		wdi_err("This version of Windows is no longer supported");
		r = WDI_ERROR_NOT_SUPPORTED;
		goto out;
	}

	if (safe_strlen(cert_name) == 0) {
		r = WDI_ERROR_INVALID_PARAM;
		goto out;
	}

	if (IsUserAnAdmin()) {
		for (i=0; i<nb_resources; i++) {
			if (safe_strcmp(cert_name, resource[i].name) == 0) {
				break;
			}
		}
		if (i == nb_resources) {
			wdi_err("Unable to locate certificate '%s' in embedded resources", cert_name);
			r = WDI_ERROR_NOT_FOUND;
			goto out;
		}

		if (options != NULL) {
			hWnd = options->hWnd;
			disable_warning = options->disable_warning;
		}

		if (!AddCertToTrustedPublisher((BYTE*)resource[i].data, (DWORD)resource[i].size, disable_warning, hWnd)) {
			wdi_warn("Could not add certificate '%s' as Trusted Publisher", cert_name);
			r = WDI_ERROR_RESOURCE;
			goto out;
		}
		wdi_info("Certificate '%s' successfully added as Trusted Publisher", cert_name);
		r = WDI_SUCCESS;
		goto out;
	}

	wdi_err("This call must be run with elevated privileges");
	r = WDI_ERROR_NEEDS_ADMIN;
out:
	return r;
}

// Return the WDF version used by the native drivers
int LIBWDI_API wdi_get_wdf_version(void)
{
#if defined(WDF_VER)
	return WDF_VER;
#else
	return -1;
#endif
}
