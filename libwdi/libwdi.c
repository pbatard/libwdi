/*
 * Library for USB automated driver installation
 * Copyright (c) 2010 Pete Batard <pbatard@gmail.com>
 * Parts of the code from libusb by Daniel Drake, Johannes Erdfelt et al.
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
#include <windows.h>
#include <setupapi.h>
#include <io.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <inttypes.h>
#include <objbase.h>
#include <shellapi.h>
#include <config.h>

#include "installer.h"
#include "libwdi.h"
#include "logging.h"
#include "infs.h"
#include "resource.h"	// auto-generated during compilation

// Initial timeout delay to wait for the installer to run
#define DEFAULT_TIMEOUT 10000

// These warnings are taken care off in configure for other platforms
#if defined(_MSC_VER)

#define __STR2__(x) #x
#define __STR1__(x) __STR2__(x)
#if defined(_WIN64) && defined(OPT_M32)
// a 64 bit application/library CANNOT be used on 32 bit platforms
#pragma message(__FILE__ "(" __STR1__(__LINE__) ") : warning : library is compiled as 64 bit - disabling 32 bit support")
#undef OPT_M32
#endif

#if !defined(OPT_M32) && !defined(OPT_M64)
#error both 32 and 64 bit support have been disabled - check your config.h
#endif
#if defined(OPT_M64) && !defined(OPT_M32)
#pragma message(__FILE__ "(" __STR1__(__LINE__) ") : warning : this library will be INCOMPATIBLE with 32 bit platforms")
#endif
#if defined(OPT_M32) && !defined(OPT_M64)
#pragma message(__FILE__ "(" __STR1__(__LINE__) ") : warning : this library will be INCOMPATIBLE with 64 bit platforms")
#endif

#endif /* _MSC_VER */

/*
 * Global variables
 */
struct wdi_device_info *current_device = NULL;
bool dlls_available = false;
DWORD timeout = DEFAULT_TIMEOUT;
HANDLE pipe_handle = INVALID_HANDLE_VALUE;
// for 64 bit platforms detection
static BOOL (__stdcall *pIsWow64Process)(HANDLE, PBOOL) = NULL;
enum windows_version windows_version = WINDOWS_UNDEFINED;

/*
 * For the retrieval of the device description on Windows 7
 */
#ifndef DEVPROPKEY_DEFINED
typedef struct {
    GUID  fmtid;
    ULONG pid;
} DEVPROPKEY;
#endif

const DEVPROPKEY DEVPKEY_Device_BusReportedDeviceDesc = {
	{ 0x540b947e, 0x8b40, 0x45bc, {0xa8, 0xa2, 0x6a, 0x0b, 0x89, 0x4c, 0xbd, 0xa2} }, 4 };

/*
 * Cfgmgr32.dll, SetupAPI.dll interface
 */
DLL_DECLARE(WINAPI, CONFIGRET, CM_Get_Parent, (PDEVINST, DEVINST, ULONG));
DLL_DECLARE(WINAPI, CONFIGRET, CM_Get_Child, (PDEVINST, DEVINST, ULONG));
DLL_DECLARE(WINAPI, CONFIGRET, CM_Get_Sibling, (PDEVINST, DEVINST, ULONG));
DLL_DECLARE(WINAPI, CONFIGRET, CM_Get_Device_IDA, (DEVINST, PCHAR, ULONG, ULONG));
// This call is only available on XP and later
DLL_DECLARE(WINAPI, DWORD, CMP_WaitNoPendingInstallEvents, (DWORD));
// This call is only available on Vista and later
DLL_DECLARE(WINAPI, BOOL, SetupDiGetDeviceProperty, (HDEVINFO, PSP_DEVINFO_DATA, const DEVPROPKEY*, ULONG*, PBYTE, DWORD, PDWORD, DWORD));
DLL_DECLARE(WINAPI, BOOL, IsUserAnAdmin, (void));

// Detect Windows version
#define GET_WINDOWS_VERSION do{ if (windows_version == WINDOWS_UNDEFINED) detect_version(); } while(0)
static void detect_version(void)
{
	OSVERSIONINFO os_version;

	memset(&os_version, 0, sizeof(OSVERSIONINFO));
	os_version.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	windows_version = WINDOWS_UNSUPPORTED;
	if ((GetVersionEx(&os_version) != 0) && (os_version.dwPlatformId == VER_PLATFORM_WIN32_NT)) {
		if ((os_version.dwMajorVersion == 5) && (os_version.dwMinorVersion == 0)) {
			windows_version = WINDOWS_2K;
			wdi_dbg("Windows 2000");
		} else if ((os_version.dwMajorVersion == 5) && (os_version.dwMinorVersion == 1)) {
			windows_version = WINDOWS_XP;
			wdi_dbg("Windows XP");
		} else if ((os_version.dwMajorVersion == 5) && (os_version.dwMinorVersion == 2)) {
			windows_version = WINDOWS_2003_XP64;
			wdi_dbg("Windows 2003 or Windows XP 64 bit");
		} else if (os_version.dwMajorVersion >= 6) {
			if (os_version.dwBuildNumber < 7000) {
				windows_version = WINDOWS_VISTA;
				wdi_dbg("Windows Vista");
			} else {
				windows_version = WINDOWS_7;
				wdi_dbg("Windows 7");
			}
		}
	}
}

/*
 * Converts a WCHAR string to UTF8 (allocate returned string)
 * Returns NULL on error
 */
static char* wchar_to_utf8(WCHAR* wstr)
{
	int size;
	char* str;

	// Find out the size we need to allocate for our converted string
	size = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
	if (size <= 1)	// An empty string would be size 1
		return NULL;

	if ((str = malloc(size)) == NULL)
		return NULL;

	if (WideCharToMultiByte(CP_UTF8, 0, wstr, -1, str, size, NULL, NULL) != size) {
		free(str);
		return NULL;
	}

	return str;
}

/*
 * Converts a windows error to human readable string
 * uses retval as errorcode, or, if 0, use GetLastError()
 */
char *windows_error_str(uint32_t retval)
{
static char err_string[STR_BUFFER_SIZE];

	DWORD size;
	uint32_t errcode, format_errcode;

	errcode = retval?retval:GetLastError();

	safe_sprintf(err_string, STR_BUFFER_SIZE, "[%d] ", errcode);

	size = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, errcode,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR) &err_string[strlen(err_string)],
		STR_BUFFER_SIZE, NULL);
	if (size == 0)
	{
		format_errcode = GetLastError();
		if (format_errcode)
			safe_sprintf(err_string, STR_BUFFER_SIZE,
				"Windows error code %u (FormatMessage error code %u)", errcode, format_errcode);
		else
			safe_sprintf(err_string, STR_BUFFER_SIZE, "Unknown error code %u", errcode);
	}
	return err_string;
}

/*
 * Check whether the path is a directory with write access
 * if create is true, create directory if it doesn't exist
 */
static int check_dir(char* path, bool create)
{
	struct _stat st;

	if (_access(path, 02) == 0) {
		memset(&st, 0, sizeof(st));
		if (_stat(path, &st) == 0) {
			if (!(st.st_mode & _S_IFDIR)) {
				wdi_err("%s is a file, not a directory");
				return WDI_ERROR_ACCESS;
			}
			return WDI_SUCCESS;
		}
		return WDI_ERROR_ACCESS;
	}

	if (!create) {
		wdi_err("%s doesn't exist");
		return WDI_ERROR_ACCESS;
	}

	if (CreateDirectoryA(path, 0) == 0) {
		wdi_err("could not create directory %s", path);
		return WDI_ERROR_ACCESS;
	}

	return WDI_SUCCESS;
}

/*
 * Returns a constant string with an English short description of the given
 * error code. The caller should never free() the returned pointer since it
 * points to a constant string.
 * The returned string is encoded in ASCII form and always starts with a
 * capital letter and ends without any dot.
 * \param errcode the error code whose description is desired
 * \returns a short description of the error code in English
 */
const char* LIBWDI_API wdi_strerror(enum wdi_error errcode)
{
	switch (errcode)
	{
	case WDI_SUCCESS:
		return "Success";
	case WDI_ERROR_IO:
		return "Input/output error";
	case WDI_ERROR_INVALID_PARAM:
		return "Invalid parameter";
	case WDI_ERROR_ACCESS:
		return "Access denied (insufficient permissions)";
	case WDI_ERROR_NO_DEVICE:
		return "No such device (it may have been disconnected)";
	case WDI_ERROR_NOT_FOUND:
		return "Entity not found";
	case WDI_ERROR_BUSY:
		return "Resource busy";
	case WDI_ERROR_TIMEOUT:
		return "Operation timed out";
	case WDI_ERROR_OVERFLOW:
		return "Overflow";
	case WDI_ERROR_PENDING_INSTALLATION:
		return "Another installer is already running";
	case WDI_ERROR_INTERRUPTED:
		return "System call interrupted (perhaps due to signal)";
	case WDI_ERROR_RESOURCE:
		return "Could not acquire resource (insufficient memory, etc.)";
	case WDI_ERROR_NOT_SUPPORTED:
		return "Operation not supported or unimplemented on this platform";
	case WDI_ERROR_EXISTS:
		return "Resource already exists";
	case WDI_ERROR_USER_CANCEL:
		return "Cancelled by user";
	// The errors below are generated during driver installation
	case WDI_ERROR_NEEDS_ADMIN:
		return "Unable to run installer process with administrative privileges";
	case WDI_ERROR_WOW64:
		return "Attempted to use a 32 bit installer on a 64 bit machine";
	case WDI_ERROR_INF_SYNTAX:
		return "The syntax of the inf is invalid";
	case WDI_ERROR_CAT_MISSING:
		return "Unable to locate cat file";
	case WDI_ERROR_UNSIGNED:
		return "System policy has been modified from Windows defaults to reject unsigned drivers";
	case WDI_ERROR_OTHER:
		return "Other error";
	}
	return "Unknown error";
}

// convert a GUID to an hex GUID string
char* guid_to_string(const GUID guid)
{
	static char guid_string[MAX_GUID_STRING_LENGTH];

	sprintf(guid_string, "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
		(unsigned int)guid.Data1, guid.Data2, guid.Data3,
		guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
		guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
	return guid_string;
}

// free a device info struct
void free_di(struct wdi_device_info *di)
{
	if (di == NULL) {
		return;
	}
	safe_free(di->device_id);
	safe_free(di->desc);
	safe_free(di->driver);
	free(di);
}

// Setup the Cfgmgr32 and SetupApi DLLs
static int init_dlls(void)
{
	DLL_LOAD(Cfgmgr32.dll, CM_Get_Parent, TRUE);
	DLL_LOAD(Cfgmgr32.dll, CM_Get_Child, TRUE);
	DLL_LOAD(Cfgmgr32.dll, CM_Get_Sibling, TRUE);
	DLL_LOAD(Cfgmgr32.dll, CM_Get_Device_IDA, TRUE);
	DLL_LOAD(Setupapi.dll, CMP_WaitNoPendingInstallEvents, FALSE);
	DLL_LOAD(Setupapi.dll, SetupDiGetDeviceProperty, FALSE);
	DLL_LOAD(Shell32.dll, IsUserAnAdmin, FALSE);
	return WDI_SUCCESS;
}

// List USB devices
struct wdi_device_info* LIBWDI_API wdi_create_list(bool driverless_only)
{
	unsigned i, j, tmp;
	unsigned unknown_count = 1;
	DWORD size, reg_type;
	ULONG devprop_type;
	CONFIGRET r;
	HDEVINFO dev_info;
	SP_DEVINFO_DATA dev_info_data;
	char *prefix[3] = {"VID_", "PID_", "MI_"};
	char *token;
	char strbuf[STR_BUFFER_SIZE];
	WCHAR desc[MAX_DESC_LENGTH];
	struct wdi_device_info *start = NULL, *cur = NULL, *device_info = NULL;
	bool driverless;
	const char usbhub_name[] = "usbhub";
	const char usbccgp_name[] = "usbccgp";

	if (!dlls_available) {
		init_dlls();
	}

	// List all connected USB devices
	dev_info = SetupDiGetClassDevs(NULL, "USB", NULL, DIGCF_PRESENT|DIGCF_ALLCLASSES);
	if (dev_info == INVALID_HANDLE_VALUE) {
		return NULL;
	}

	// Find the ones that are driverless
	for (i = 0; ; i++)
	{
		driverless = false;

		// Free any invalid previously allocated struct
		free_di(device_info);

		dev_info_data.cbSize = sizeof(dev_info_data);
		if (!SetupDiEnumDeviceInfo(dev_info, i, &dev_info_data)) {
			break;
		}

		// Allocate a driver_info struct to store our data
		device_info = calloc(1, sizeof(struct wdi_device_info));
		if (device_info == NULL) {
			wdi_destroy_list(start);
			return NULL;
		}

		// SPDRP_DRIVER seems to do a better job at detecting driverless devices than
		// SPDRP_INSTALL_STATE
		if (SetupDiGetDeviceRegistryPropertyA(dev_info, &dev_info_data, SPDRP_DRIVER,
			&reg_type, (BYTE*)strbuf, STR_BUFFER_SIZE, &size)) {
			if (driverless_only) {
				continue;
			}
		} else {
			// Driverless devices will return an error
			driverless = true;
		}

		// Eliminate USB hubs by checking the driver string
		if (!SetupDiGetDeviceRegistryPropertyA(dev_info, &dev_info_data, SPDRP_SERVICE,
			&reg_type, (BYTE*)strbuf, STR_BUFFER_SIZE, &size)) {
			device_info->driver = NULL;
		} else {
			device_info->driver = safe_strdup(strbuf);
		}
		if (safe_strcmp(strbuf, usbhub_name) == 0) {
			continue;
		}
		// Also eliminate composite devices parent drivers, as replacing these drivers
		// is a bad idea
		if (safe_strcmp(strbuf, usbccgp_name) == 0) {
			continue;
		}

		// Retrieve the hardware ID
		if (SetupDiGetDeviceRegistryPropertyA(dev_info, &dev_info_data, SPDRP_HARDWAREID,
			&reg_type, (BYTE*)strbuf, STR_BUFFER_SIZE, &size)) {
			wdi_dbg("got hardware ID: %s", strbuf);
		} else {
			wdi_err("could not get hardware ID");
			strbuf[0] = 0;
		}
		device_info->hardware_id = safe_strdup(strbuf);

		// Retrieve device ID. This is needed to re-enumerate our device and force
		// the final driver installation
		r = CM_Get_Device_IDA(dev_info_data.DevInst, strbuf, STR_BUFFER_SIZE, 0);
		if (r != CR_SUCCESS) {
			wdi_err("could not retrieve simple path for device %d: CR error %d", i, r);
			continue;
		} else {
			wdi_dbg("%s USB device (%d): %s",
				device_info->driver?device_info->driver:"Driverless", i, strbuf);
		}
		device_info->device_id = safe_strdup(strbuf);

		GET_WINDOWS_VERSION;
		if (windows_version < WINDOWS_7) {
			// On Vista and earlier, we can use SPDRP_DEVICEDESC
			if (!SetupDiGetDeviceRegistryPropertyW(dev_info, &dev_info_data, SPDRP_DEVICEDESC,
				&reg_type, (BYTE*)desc, 2*MAX_DESC_LENGTH, &size)) {
				wdi_warn("could not read device description for %d: %s",
					i, windows_error_str(0));
				safe_swprintf(desc, MAX_DESC_LENGTH, L"Unknown Device #%d", unknown_count++);
			}
		} else {
			// On Windows 7, the information we want ("Bus reported device description") is
			// accessed through DEVPKEY_Device_BusReportedDeviceDesc
			if (SetupDiGetDeviceProperty == NULL) {
				wdi_warn("failed to locate SetupDiGetDeviceProperty() is Setupapi.dll");
				desc[0] = 0;
			} else if (!SetupDiGetDeviceProperty(dev_info, &dev_info_data, &DEVPKEY_Device_BusReportedDeviceDesc,
				&devprop_type, (BYTE*)desc, 2*MAX_DESC_LENGTH, &size, 0)) {
				// fallback to SPDRP_DEVICEDESC (USB husb still use it)
				if (!SetupDiGetDeviceRegistryPropertyW(dev_info, &dev_info_data, SPDRP_DEVICEDESC,
					&reg_type, (BYTE*)desc, 2*MAX_DESC_LENGTH, &size)) {
					wdi_warn("could not read device description for %d: %s",
						i, windows_error_str(0));
					safe_swprintf(desc, MAX_DESC_LENGTH, L"Unknown Device #%d", unknown_count++);
				}
			}
		}

		device_info->mi = -1;	// No interface number by default
		token = strtok (strbuf, "\\#&");
		while(token != NULL) {
			for (j = 0; j < 3; j++) {
				if (safe_strncmp(token, prefix[j], strlen(prefix[j])) == 0) {
					switch(j) {
					case 0:
						if (sscanf(token, "VID_%04X", &tmp) != 1) {
							wdi_err("could not convert VID string");
						} else {
							device_info->vid = (unsigned short)tmp;
						}
						break;
					case 1:
						if (sscanf(token, "PID_%04X", &tmp) != 1) {
							wdi_err("could not convert PID string");
						} else {
							device_info->pid = (unsigned short)tmp;
						}
						break;
					case 2:
						if (sscanf(token, "MI_%02X", &tmp) != 1) {
							wdi_err("could not convert MI string");
						} else {
							device_info->mi = (short)tmp;
							if ((wcslen(desc) + sizeof(" (Interface ###)")) < MAX_DESC_LENGTH) {
								_snwprintf(&desc[wcslen(desc)], sizeof(" (Interface ###)"),
									L" (Interface %d)", device_info->mi);
							}
						}
						break;
					default:
						wdi_err("unexpected case");
						break;
					}
				}
			}
			token = strtok (NULL, "\\#&");
		}
		device_info->desc = wchar_to_utf8(desc);
		wdi_dbg("Device description: %s", device_info->desc);

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

	return start;
}

void LIBWDI_API wdi_destroy_list(struct wdi_device_info* list)
{
	struct wdi_device_info *tmp;
	while(list != NULL) {
		tmp = list;
		list = list->next;
		free_di(tmp);
	}
}

// extract the embedded binary resources
int extract_binaries(char* path)
{
	char filename[MAX_PATH_LENGTH];
	FILE* fd;
	int i, r;

	for (i=0; i<nb_resources; i++) {
		safe_strcpy(filename, MAX_PATH_LENGTH, path);
		safe_strcat(filename, MAX_PATH_LENGTH, "\\");
		safe_strcat(filename, MAX_PATH_LENGTH, resource[i].subdir);

		r = check_dir(filename, true);
		if (r != WDI_SUCCESS) {
			return r;
		}
		safe_strcat(filename, MAX_PATH_LENGTH, "\\");
		safe_strcat(filename, MAX_PATH_LENGTH, resource[i].name);


		fd = fopen(filename, "wb");
		if (fd == NULL) {
			wdi_err("failed to create file: %s", filename);
			return WDI_ERROR_RESOURCE;
		}

		fwrite(resource[i].data, resource[i].size, 1, fd);
		fclose(fd);
	}

	wdi_dbg("successfully extracted files to %s", path);
	return WDI_SUCCESS;
}

// Create an inf and extract coinstallers in the directory pointed by path
// TODO: optional directory deletion
int LIBWDI_API wdi_create_inf(struct wdi_device_info* device_info, char* path,
							  char* inf_name, enum wdi_driver_type type)
{
	char filename[MAX_PATH_LENGTH];
	FILE* fd;
	GUID guid;
	int r;
	SYSTEMTIME system_time;

	// TODO? create a reusable temp dir if path is NULL?
	if ((path == NULL) || (device_info == NULL)) {
		return -1;
	}

	if (device_info->desc == NULL) {
		wdi_err("no description was given for the device - aborting");
		return WDI_ERROR_NOT_FOUND;
	}

	if (type == WDI_LIBUSB) {
		wdi_err("libusb support is not implemented yet - defaulting to WinUSB");
	}

	// Try to create directory if it doesn't exist
	r = check_dir(path, true);
	if (r != WDI_SUCCESS) {
		return r;
	}

	extract_binaries(path);

	safe_strcpy(filename, MAX_PATH_LENGTH, path);
	safe_strcat(filename, MAX_PATH_LENGTH, "\\");
	safe_strcat(filename, MAX_PATH_LENGTH, inf_name);

	fd = fopen(filename, "w");
	if (fd == NULL) {
		wdi_err("failed to create file: %s", filename);
		return WDI_ERROR_RESOURCE;
	}

	fprintf(fd, "; %s\n", inf_name);
	fprintf(fd, "; Copyright (c) 2010 libusb (GNU LGPL)\n");
	fprintf(fd, "[Strings]\n");
	fprintf(fd, "DeviceName = \"%s\"\n", device_info->desc);
	fprintf(fd, "DeviceID = \"VID_%04X&PID_%04X", device_info->vid, device_info->pid);
	if (device_info->mi >= 0) {
		fprintf(fd, "&MI_%02X\"\n", device_info->mi);
	} else {
		fprintf(fd, "\"\n");
	}
	CoCreateGuid(&guid);
	fprintf(fd, "DeviceGUID = \"%s\"\n", guid_to_string(guid));
	GetLocalTime(&system_time);
	fprintf(fd, "Date = \"%02d/%02d/%04d\"\n", system_time.wMonth, system_time.wDay, system_time.wYear);
	fwrite(inf[type], strlen(inf[type]), 1, fd);
	fclose(fd);

	wdi_dbg("succesfully created %s", filename);
	return WDI_SUCCESS;
}

// Handle messages received from the elevated installer through the pipe
int process_message(char* buffer, DWORD size)
{
	DWORD junk;

	if (size <= 0)
		return WDI_ERROR_INVALID_PARAM;

	if (current_device == NULL) {
		wdi_err("program assertion failed - no current device");
		return WDI_ERROR_NOT_FOUND;
	}

	// Note: this is a message pipe, so we don't need to care about
	// multiple messages coexisting in our buffer.
	switch(buffer[0])
	{
	case IC_GET_DEVICE_ID:
		wdi_dbg("got request for device_id");
		// TODO use device instead of req_ duplication!
		if (current_device->device_id != NULL) {
			WriteFile(pipe_handle, current_device->device_id, strlen(current_device->device_id), &junk, NULL);
		} else {
			wdi_warn("no device_id - sending empty string");
			WriteFile(pipe_handle, "", 1, &junk, NULL);
		}
		break;
	case IC_GET_HARDWARE_ID:
		wdi_dbg("got request for hardware_id");
		if (current_device->hardware_id != NULL) {
			WriteFile(pipe_handle, current_device->hardware_id, strlen(current_device->hardware_id), &junk, NULL);
		} else {
			wdi_warn("no hardware_id - sending empty string");
			WriteFile(pipe_handle, "", 1, &junk, NULL);
		}
		break;
	case IC_PRINT_MESSAGE:
		if (size < 2) {
			wdi_err("print_message: no data");
			return WDI_ERROR_NOT_FOUND;
		}
		wdi_dbg("[installer process] %s", buffer+1);
		break;
	case IC_SET_STATUS:
		if (size < 2) {
			wdi_err("set status: no data");
			return WDI_ERROR_NOT_FOUND;
		}
		return (int)buffer[1];
		break;
	// TODO: only do that if UAC
	case IC_SET_TIMEOUT_INFINITE:
		wdi_dbg("switching timeout to infinite");
		timeout = INFINITE;
		break;
	case IC_SET_TIMEOUT_DEFAULT:
		wdi_dbg("switching timeout back to finite");
		timeout = DEFAULT_TIMEOUT;
		break;
	default:
		wdi_err("unrecognized installer message");
		return WDI_ERROR_OTHER;
	}
	return WDI_SUCCESS;
}

// Run the elevated installer
int LIBWDI_API wdi_install_driver(struct wdi_device_info* device_info, char* path, char* inf_name)
{
	SHELLEXECUTEINFO shExecInfo;
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
	char exename[STR_BUFFER_SIZE];
	HANDLE handle[2] = {INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE};
	OVERLAPPED overlapped;
	int r;
	DWORD err, rd_count;
	BOOL is_x64 = false;
	char buffer[STR_BUFFER_SIZE];

	current_device = device_info;

	// Detect if another installation is in process
	if (CMP_WaitNoPendingInstallEvents != NULL) {
		if (CMP_WaitNoPendingInstallEvents(0) == WAIT_TIMEOUT) {
			wdi_dbg("detected another pending installation - aborting");
			return WDI_ERROR_PENDING_INSTALLATION;
		}
	} else {
		wdi_dbg("CMP_WaitNoPendingInstallEvents not available");
	}

	// Detect whether if we should run the 64 bit installer, without
	// relying on external libs
	if (sizeof(uintptr_t) < 8) {
		// This application is not 64 bit, but it might be 32 bit
		// running in WOW64
		pIsWow64Process = (BOOL (__stdcall *)(HANDLE, PBOOL))
			GetProcAddress(GetModuleHandle("KERNEL32"), "IsWow64Process");
		if (pIsWow64Process != NULL) {
			(*pIsWow64Process)(GetCurrentProcess(), &is_x64);
		}
	} else {
		// TODO: warn at compile time about redist of 64 bit app
		is_x64 = true;
	}

	// Use a pipe to communicate with our installer
	pipe_handle = CreateNamedPipe(INSTALLER_PIPE_NAME, PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,
		PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE, 1, 4096, 4096, 0, NULL);
	if (pipe_handle == INVALID_HANDLE_VALUE) {
		wdi_err("could not create read pipe: %s", windows_error_str(0));
		r = WDI_ERROR_RESOURCE; goto out;
	}

	// Set the overlapped for messaging
	memset(&overlapped, 0, sizeof(OVERLAPPED));
	handle[0] = CreateEvent(NULL, TRUE, FALSE, NULL);
	if(handle[0] == NULL) {
		r = WDI_ERROR_RESOURCE; goto out;
	}
	overlapped.hEvent = handle[0];

	safe_strcpy(exename, STR_BUFFER_SIZE, path);
	// Why do we need two installers? Glad you asked. If you try to run the x86 installer on an x64
	// system, you will get a "System does not work under WOW64 and requires 64-bit version" message.
	if (is_x64) {
		safe_strcat(exename, STR_BUFFER_SIZE, "\\installer_x64.exe");
	} else {
		safe_strcat(exename, STR_BUFFER_SIZE, "\\installer_x86.exe");
	}
	// At this stage, if either the 32 or 64 bit installer version is missing,
	// it is the application developer's fault...
	if (_access(exename, 00) != 0) {
		wdi_err("this application does not contain the required %s bit installer", is_x64?"64":"32");
		wdi_err("please contact the application provider for a %s bit compatible version", is_x64?"64":"32");
		r = WDI_ERROR_NOT_FOUND; goto out;
	}

	GET_WINDOWS_VERSION;
	if ( (windows_version >= WINDOWS_VISTA) && (IsUserAnAdmin != NULL) && (!IsUserAnAdmin()) )  {
		// On Vista and later, we must take care of UAC with ShellExecuteEx + runas
		shExecInfo.cbSize = sizeof(SHELLEXECUTEINFO);

		shExecInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
		shExecInfo.hwnd = NULL;
		shExecInfo.lpVerb = "runas";
		shExecInfo.lpFile = exename;
		// if INF_NAME ever has a space, it will be seen as multiple parameters
		shExecInfo.lpParameters = inf_name;
		shExecInfo.lpDirectory = path;
		// TODO: hide
		//shExecInfo.nShow = SW_NORMAL;
		shExecInfo.nShow = SW_HIDE;
		shExecInfo.hInstApp = NULL;

		err = 0;
		if (!ShellExecuteEx(&shExecInfo)) {
			err = GetLastError();
		}

		if ((err == ERROR_CANCELLED) || (shExecInfo.hProcess == NULL)) {
			wdi_dbg("operation cancelled by the user");
			r = WDI_ERROR_USER_CANCEL; goto out;
		}
		else if (err) {
			wdi_err("ShellExecuteEx failed: %s", windows_error_str(err));
			r = WDI_ERROR_NEEDS_ADMIN; goto out;
		}

		handle[1] = shExecInfo.hProcess;
	} else {
		// On XP and earlier, or if app is already elevated, simply use CreateProcess
		memset(&si, 0, sizeof(si));
		si.cb = sizeof(si);
		memset(&pi, 0, sizeof(pi));

		safe_strcat(exename, STR_BUFFER_SIZE, " ");
		safe_strcat(exename, STR_BUFFER_SIZE, inf_name);
		if (!CreateProcessA(NULL, exename, NULL, NULL, FALSE, CREATE_NO_WINDOW,	NULL, path, &si, &pi)) {
			wdi_err("CreateProcess failed: %s", windows_error_str(0));
			r = WDI_ERROR_NEEDS_ADMIN; goto out;
		}
		handle[1] = pi.hProcess;
	}

	r = WDI_SUCCESS;
	while (r == WDI_SUCCESS) {
		if (ReadFile(pipe_handle, buffer, STR_BUFFER_SIZE, &rd_count, &overlapped)) {
			// Message was read synchronously
			r = process_message(buffer, rd_count);
		} else {
			switch(GetLastError()) {
			case ERROR_BROKEN_PIPE:
				// The pipe has been ended - wait for installer to finish
				if ((WaitForSingleObject(handle[1], timeout) == WAIT_TIMEOUT)) {
					TerminateProcess(handle[1], 0);
				}
				r = WDI_SUCCESS; goto out;
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
					} else {
						switch(GetLastError()) {
						case ERROR_BROKEN_PIPE:
							// The pipe has been ended - wait for installer to finish
							if ((WaitForSingleObject(handle[1], timeout) == WAIT_TIMEOUT)) {
								TerminateProcess(handle[1], 0);
							}
							r = WDI_SUCCESS; goto out;
						case ERROR_MORE_DATA:
							wdi_warn("program assertion failed: message overflow");
							r = process_message(buffer, rd_count);
							break;
						default:
							wdi_err("could not read from pipe (async): %s", windows_error_str(0));
							break;
						}
					}
					break;
				case WAIT_TIMEOUT:
					// Lost contact
					wdi_err("installer failed to respond - aborting");
					TerminateProcess(handle[1], 0);
					r = WDI_ERROR_TIMEOUT; goto out;
				case WAIT_OBJECT_0+1:
					// installer process terminated
					r = WDI_SUCCESS; goto out;
				default:
					wdi_err("could not read from pipe (wait): %s", windows_error_str(0));
					break;
				}
				break;
			default:
				wdi_err("could not read from pipe (sync): %s", windows_error_str(0));
				break;
			}
		}
	}
out:
	current_device = NULL;
	safe_closehandle(handle[0]);
	safe_closehandle(handle[1]);
	safe_closehandle(pipe_handle);
	return r;
}
