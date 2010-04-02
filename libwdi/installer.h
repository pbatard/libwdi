/*
 * Library for WinUSB/libusb automated driver installation
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

#include <windows.h>
#if !defined(bool)
#define bool BOOL
#endif
#if !defined(true)
#define true TRUE
#endif
#if !defined(false)
#define false FALSE
#endif

#define MAX_DESC_LENGTH             128
#define MAX_PATH_LENGTH             128
#define MAX_KEY_LENGTH              256
#define STR_BUFFER_SIZE             256
#define MAX_GUID_STRING_LENGTH      40

#define INSTALLER_PIPE_NAME         "\\\\.\\pipe\\libwdi-installer"
#define LOGGING_PIPE_NAME           "\\\\.\\pipe\\libwdi-logger"

#define safe_free(p) do {if (p != NULL) {free(p); p = NULL;}} while(0)
#define safe_strncpy(dst, dst_max, src, count) strncpy(dst, src, min(count, dst_max - 1))
#define safe_strcpy(dst, dst_max, src) safe_strncpy(dst, dst_max, src, strlen(src)+1)
#define safe_strncat(dst, dst_max, src, count) strncat(dst, src, min(count, dst_max - strlen(dst) - 1))
#define safe_strcat(dst, dst_max, src) safe_strncat(dst, dst_max, src, strlen(src)+1)
#define safe_strcmp(str1, str2) strcmp(((str1==NULL)?"<NULL>":str1), ((str2==NULL)?"<NULL>":str2))
#define safe_strncmp(str1, str2, count) strncmp(((str1==NULL)?"<NULL>":str1), ((str2==NULL)?"<NULL>":str2), count)
#define safe_closehandle(h) do {if (h != INVALID_HANDLE_VALUE) {CloseHandle(h); h = INVALID_HANDLE_VALUE;}} while(0)
#define safe_sprintf _snprintf
#define safe_strdup _strdup

#if defined(_MSC_VER)
#define safe_vsnprintf vsprintf_s
#else
#define safe_vsnprintf vsnprintf
#endif

/*
 * For communications between installer <-> libwdi
 */
enum installer_code {
	IC_PRINT_MESSAGE,
	IC_GET_DEVICE_ID,
	IC_GET_HARDWARE_ID,
	IC_SET_TIMEOUT_INFINITE,
	IC_SET_TIMEOUT_DEFAULT,
};

/*
 * Windows versions
 */
enum windows_version {
	WINDOWS_UNDEFINED,
	WINDOWS_UNSUPPORTED,
	WINDOWS_2K,
	WINDOWS_XP,
	WINDOWS_VISTA,
	WINDOWS_7
};

/*
 * API macros - from libusb-win32 1.x
 */
#define DLL_DECLARE(api, ret, name, args)                    \
  typedef ret (api * __dll_##name##_t)args; __dll_##name##_t name

#define DLL_LOAD(dll, name, ret_on_failure)                   \
	do {                                                      \
		HMODULE h = GetModuleHandle(#dll);                    \
	if (!h)                                                   \
		h = LoadLibrary(#dll);                                \
	if (!h) {                                                 \
		if (ret_on_failure) { return -1; }                    \
		else { break; }                                       \
	}                                                         \
	name = (__dll_##name##_t)GetProcAddress(h, #name);        \
	if (name) break;                                          \
	name = (__dll_##name##_t)GetProcAddress(h, #name "A");    \
	if (name) break;                                          \
	name = (__dll_##name##_t)GetProcAddress(h, #name "W");    \
	if (name) break;                                          \
	if(ret_on_failure)                                        \
		return -1;                                            \
	} while(0)


/*
 * Cfgmgr32.dll interface
 */
typedef DWORD DEVNODE, DEVINST;
typedef DEVNODE *PDEVNODE, *PDEVINST;
typedef DWORD RETURN_TYPE;
typedef RETURN_TYPE	CONFIGRET;

#define CR_SUCCESS                        0x00000000
#define CR_NO_SUCH_DEVNODE                0x0000000D
#define CONFIGFLAG_REINSTALL              0x00000020

#define CM_REENUMERATE_NORMAL             0x00000000
#define CM_REENUMERATE_SYNCHRONOUS        0x00000001
#define CM_REENUMERATE_RETRY_INSTALLATION 0x00000002
#define CM_REENUMERATE_ASYNCHRONOUS       0x00000004
#define CM_REENUMERATE_BITS               0x00000007

/*
 * DifXApi.dll interface
 */
#define DRIVER_PACKAGE_REPAIR             0x00000001
#define DRIVER_PACKAGE_FORCE              0x00000004
#define DRIVER_PACKAGE_LEGACY_MODE        0x00000010

#define ERROR_INVALID_CATALOG_DATA        0xE0000304
#ifndef ERROR_DRIVER_STORE_ADD_FAILED
#define ERROR_DRIVER_STORE_ADD_FAILED     0xE0000247
#endif
#ifndef ERROR_NO_AUTHENTICODE_CATALOG
#define ERROR_NO_AUTHENTICODE_CATALOG     0xE000023F
#endif
#ifndef ERROR_IN_WOW64
#define ERROR_IN_WOW64                    0xE0000235
#endif

typedef enum {
	DIFXAPI_SUCCESS,
	DIFXAPI_INFO,
	DIFXAPI_WARNING,
	DIFXAPI_ERROR
} DIFXAPI_LOG;
typedef void (__cdecl* DIFXAPILOGCALLBACK)(DIFXAPI_LOG EventType, DWORD Error, LPCSTR EventDescription, PVOID CallbackContext);

typedef struct {
	LPCSTR  pApplicationId;
	LPCSTR  pDisplayName;
	LPCSTR  pProductName;
	LPCSTR  pMfgName;
} INSTALLERINFO, *PINSTALLERINFO;
typedef const PINSTALLERINFO PCINSTALLERINFO;
