/*
* Library for USB automated driver installation
* Copyright (c) 2010-2017 Pete Batard <pete@akeo.ie>
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

#include <windows.h>
#include <stdint.h>

#pragma once

#define REGKEY_HKCU                 HKEY_CURRENT_USER
#define REGKEY_HKLM                 HKEY_LOCAL_MACHINE

#define WDI_COMPANY_NAME            "Akeo Consulting"
#define WDI_APPLICATION_NAME        "libwdi"

// Windows versions
enum WindowsVersion {
	WINDOWS_UNDEFINED = -1,
	WINDOWS_UNSUPPORTED = 0,
	WINDOWS_7 = 0x61,
	WINDOWS_8 = 0x62,
	WINDOWS_8_1 = 0x63,
	WINDOWS_10_PREVIEW1 = 0x64,
	WINDOWS_10 = 0xA0,
	WINDOWS_MAX
};

extern int nWindowsVersion;
extern char WindowsVersionStr[128];

void GetWindowsVersion(void);

/* Read a string registry key value */
static __inline BOOL ReadRegistryStr(HKEY key_root, const char* key_name, char* str, DWORD len)
{
	const char software_prefix[] = "SOFTWARE\\";
	char long_key_name[MAX_PATH] = { 0 };
	BOOL r = FALSE;
	size_t i = 0;
	LONG s;
	HKEY hApp = NULL;
	DWORD dwType = -1, dwSize = len;
	LPBYTE dest = (LPBYTE)str;

	memset(dest, 0, len);

	if (key_name == NULL)
		return FALSE;

	for (i = strlen(key_name); i > 0; i--) {
		if (key_name[i] == '\\')
			break;
	}

	if (i != 0) {
		// Prefix with "SOFTWARE" if needed
		if (_strnicmp(key_name, software_prefix, sizeof(software_prefix) - 1) != 0) {
			if (i + sizeof(software_prefix) >= sizeof(long_key_name))
				return FALSE;
			strcpy(long_key_name, software_prefix);
			strncat(long_key_name, key_name,
				min(strlen(key_name) + 1, sizeof(long_key_name) - sizeof(software_prefix) - 1));
			long_key_name[sizeof(software_prefix) + i - 1] = 0;
		} else {
			if (i >= sizeof(long_key_name))
				return FALSE;
			strncpy(long_key_name, key_name, sizeof(long_key_name));
			long_key_name[i] = 0;
		}
		i++;
		if (RegOpenKeyExA(key_root, long_key_name, 0, KEY_READ, &hApp) != ERROR_SUCCESS) {
			hApp = NULL;
			goto out;
		}
	} else {
		goto out;
	}

	s = RegQueryValueExA(hApp, &key_name[i], NULL, &dwType, (LPBYTE)dest, &dwSize);
	// No key means default value of 0 or empty string
	if ((s == ERROR_FILE_NOT_FOUND) || ((s == ERROR_SUCCESS) && (dwType == REG_SZ) && (dwSize > 0))) {
		r = TRUE;
	}
out:
	if (hApp != NULL)
		RegCloseKey(hApp);
	return r;
}
