/*
 * Library for USB automated driver installation
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
#include <windows.h>
#include <setupapi.h>
#include <io.h>
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

#define INF_NAME "libusb-device.inf"
#define INSTALLER_TIMEOUT 10000
#define GET_WINDOWS_VERSION do{ if (windows_version == WINDOWS_UNDEFINED) detect_version(); } while(0)

// These warnings are taken care off in configure for other platforms
#if defined(_MSC_VER)

#define __STR2__(x) #x
#define __STR1__(x) __STR2__(x)
#if defined(_WIN64) && defined(OPT_M32)
#pragma message(__FILE__ "(" __STR1__(__LINE__) ") : warning : library is compiled as 64 bit - disabling 32 bit support as it cannot be used")
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

enum windows_version {
	WINDOWS_UNDEFINED,
	WINDOWS_UNSUPPORTED,
	WINDOWS_2K,
	WINDOWS_XP,
	WINDOWS_VISTA,
	WINDOWS_7
};

/*
 * Global variables
 */
char* req_device_id;
bool dlls_available = false;
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
// This call is only available on Vista and later
DLL_DECLARE(WINAPI, BOOL, SetupDiGetDeviceProperty, (HDEVINFO, PSP_DEVINFO_DATA, const DEVPROPKEY*, ULONG*, PBYTE, DWORD, PDWORD, DWORD));

// Detect Windows version
void detect_version(void)
{
	OSVERSIONINFO os_version;

	memset(&os_version, 0, sizeof(OSVERSIONINFO));
	os_version.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	windows_version = WINDOWS_UNSUPPORTED;
	if ((GetVersionEx(&os_version) != 0) && (os_version.dwPlatformId == VER_PLATFORM_WIN32_NT)) {
		if ((os_version.dwMajorVersion == 5) && (os_version.dwMinorVersion == 0)) {
			windows_version = WINDOWS_2K;
		} else if ((os_version.dwMajorVersion == 5) && (os_version.dwMinorVersion == 1)) {
			windows_version = WINDOWS_XP;
		} else if (os_version.dwMajorVersion >= 6) {
			if (os_version.dwBuildNumber < 7000) {
				windows_version = WINDOWS_VISTA;
			} else {
				windows_version = WINDOWS_7;
			}
		}
	}
}

/*
 * Converts a WCHAR string to UTF8 (allocate returned string)
 * Returns NULL on error
 */
char* wchar_to_utf8(LPCWSTR wstr)
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
	DLL_LOAD(Setupapi.dll, SetupDiGetDeviceProperty, FALSE);
	return 0;
}

// List USB devices
struct wdi_device_info* wdi_create_list(bool driverless_only)
{
	unsigned i, j, interface_number;
	DWORD size, reg_type;
	ULONG devprop_type;
	CONFIGRET r;
	HDEVINFO dev_info;
	SP_DEVINFO_DATA dev_info_data;
	char *prefix[3] = {"VID_", "PID_", "MI_"};
	char *token;
	char path[MAX_PATH_LENGTH];
	WCHAR desc[MAX_DESC_LENGTH];
	char driver[MAX_DESC_LENGTH];
	struct wdi_device_info *ret = NULL, *cur = NULL, *device_info;
	bool driverless;

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

		dev_info_data.cbSize = sizeof(dev_info_data);
		if (!SetupDiEnumDeviceInfo(dev_info, i, &dev_info_data)) {
			break;
		}

		// Eliminate USB hubs by checking the driver string
		if (!SetupDiGetDeviceRegistryPropertyA(dev_info, &dev_info_data, SPDRP_SERVICE,
			&reg_type, (BYTE*)driver, MAX_DESC_LENGTH, &size)) {
			driver[0] = 0;
		}
		if (safe_strcmp(driver, "usbhub") == 0) {
			continue;
		}

		// SPDRP_DRIVER seems to do a better job at detecting driverless devices than
		// SPDRP_INSTALL_STATE
		if (SetupDiGetDeviceRegistryPropertyA(dev_info, &dev_info_data, SPDRP_DRIVER,
			&reg_type, (BYTE*)path, MAX_PATH_LENGTH, &size)) {
			if (driverless_only) {
				continue;
			}
		} else {
			// Driverless devices will return an error
			driverless = true;
		}

		// Allocate a driver_info struct to store our data
		device_info = calloc(1, sizeof(struct wdi_device_info));
		if (device_info == NULL) {
			free_di(ret);
			return NULL;
		}
		if (cur == NULL) {
			ret = device_info;
		} else {
			cur->next = device_info;
		}
		cur = device_info;

		// Copy the driver name (this will be used to detect driverless)
		if (driverless) {
			cur->driver = NULL;
		} else {
			cur->driver = safe_strdup(driver);
		}

		// Retrieve device ID. This is needed to re-enumerate our device and force
		// the final driver installation
		r = CM_Get_Device_IDA(dev_info_data.DevInst, path, MAX_PATH_LENGTH, 0);
		if (r != CR_SUCCESS) {
			usbi_err(NULL, "could not retrieve simple path for device %d: CR error %d",
				i, r);
			continue;
		} else {
			usbi_dbg("%s USB device (%d): %s",
				cur->driver?cur->driver:"Driverless", i, path);
		}
		device_info->device_id = _strdup(path);

		GET_WINDOWS_VERSION;
		if (windows_version < WINDOWS_7) {
			// On Vista and earlier, we can use SPDRP_DEVICEDESC
			if (!SetupDiGetDeviceRegistryPropertyW(dev_info, &dev_info_data, SPDRP_DEVICEDESC,
				&reg_type, (BYTE*)desc, 2*MAX_DESC_LENGTH, &size)) {
				usbi_warn(NULL, "could not read device description for %d: %s",
					i, windows_error_str(0));
				desc[0] = 0;
			}
		} else {
			// On Windows 7, the information we want ("Bus reported device description") is
			// accessed through DEVPKEY_Device_BusReportedDeviceDesc
			if (SetupDiGetDeviceProperty == NULL) {
				usbi_warn(NULL, "failed to locate SetupDiGetDeviceProperty() is Setupapi.dll");
				desc[0] = 0;
			} else if (!SetupDiGetDeviceProperty(dev_info, &dev_info_data, &DEVPKEY_Device_BusReportedDeviceDesc,
				&devprop_type, (BYTE*)desc, 2*MAX_DESC_LENGTH, &size, 0)) {
				// fallback to SPDRP_DEVICEDESC (USB husb still use it)
				if (!SetupDiGetDeviceRegistryPropertyW(dev_info, &dev_info_data, SPDRP_DEVICEDESC,
					&reg_type, (BYTE*)desc, 2*MAX_DESC_LENGTH, &size)) {
					usbi_warn(NULL, "could not read device description for %d: %s",
						i, windows_error_str(0));
					desc[0] = 0;
				}
			}
		}

		token = strtok (path, "\\#&");
		while(token != NULL) {
			for (j = 0; j < 3; j++) {
				if (safe_strncmp(token, prefix[j], strlen(prefix[j])) == 0) {
					switch(j) {
					case 0:
						safe_strcpy(device_info->vid, sizeof(device_info->vid), token);
						break;
					case 1:
						safe_strcpy(device_info->pid, sizeof(device_info->pid), token);
						break;
					case 2:
						safe_strcpy(device_info->mi, sizeof(device_info->mi), token);
						// Add the interface if we have space
						if ( (sscanf(token, "MI_%02X", &interface_number) == 1)
						  && (wcslen(desc) + sizeof(" (Interface ###)")) < MAX_DESC_LENGTH ) {
							_snwprintf(&desc[wcslen(desc)], sizeof(" (Interface ###)"), L" (Interface %d)", interface_number);
						}
						break;
					default:
						usbi_err(NULL, "unexpected case");
						break;
					}
				}
			}
			token = strtok (NULL, "\\#&");
		}

		device_info->desc = wchar_to_utf8(desc);
		usbi_dbg("Device description: %s", device_info->desc);

	}

	return ret;
}

void wdi_destroy_list(struct wdi_device_info* list)
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
	int i;

	for (i=0; i<nb_resources; i++) {
		safe_strcpy(filename, MAX_PATH_LENGTH, path);
		safe_strcat(filename, MAX_PATH_LENGTH, "\\");
		safe_strcat(filename, MAX_PATH_LENGTH, resource[i].subdir);

		if ( (_access(filename, 02) != 0) && (CreateDirectory(filename, 0) == 0) ) {
			usbi_err(NULL, "could not access directory: %s", filename);
			return -1;
		}
		safe_strcat(filename, MAX_PATH_LENGTH, "\\");
		safe_strcat(filename, MAX_PATH_LENGTH, resource[i].name);


		fd = fopen(filename, "wb");
		if (fd == NULL) {
			usbi_err(NULL, "failed to create file: %s", filename);
			return -1;
		}

		fwrite(resource[i].data, resource[i].size, 1, fd);
		fclose(fd);
	}

	usbi_dbg("successfully extracted files to %s", path);
	return 0;
}

// Create an inf and extract coinstallers in the directory pointed by path
// TODO: optional directory deletion
int wdi_create_inf(struct wdi_device_info* device_info, char* path, int type)
{
	char filename[MAX_PATH_LENGTH];
	FILE* fd;
	GUID guid;

	// TODO? create a reusable temp dir if path is NULL?
	if ((path == NULL) || (device_info == NULL)) {
		return -1;
	}

	if ((type < USE_WINUSB) && (type > USE_LIBUSB)) {
		return -1;
	}

	// Try to create directory if it doesn't exist
	if ( (_access(path, 02) != 0) && (CreateDirectory(path, 0) == 0) ) {
		usbi_err(NULL, "could not access directory: %s", path);
		return -1;
	}

	extract_binaries(path);

	safe_strcpy(filename, MAX_PATH_LENGTH, path);
	safe_strcat(filename, MAX_PATH_LENGTH, "\\");
	safe_strcat(filename, MAX_PATH_LENGTH, INF_NAME);

	fd = fopen(filename, "w");
	if (fd == NULL) {
		usbi_err(NULL, "failed to create file: %s", filename);
		return -1;
	}

	fprintf(fd, "; libusb_device.inf\n");
	fprintf(fd, "; Copyright (c) 2010 libusb (GNU LGPL)\n");
	fprintf(fd, "[Strings]\n");
	fprintf(fd, "DeviceName = \"%s\"\n", device_info->desc);
	fprintf(fd, "DeviceID = \"%s&%s", device_info->vid, device_info->pid);
	if (device_info->mi[0] != 0) {
		fprintf(fd, "&%s\"\n", device_info->mi);
	} else {
		fprintf(fd, "\"\n");
	}
	CoCreateGuid(&guid);
	fprintf(fd, "DeviceGUID = \"%s\"\n", guid_to_string(guid));
	fwrite(inf[type], strlen(inf[type]), 1, fd);
	fclose(fd);

	usbi_dbg("succesfully created %s", filename);
	return 0;
}

// Handle messages received from the elevated installer through the pipe
int process_message(char* buffer, DWORD size)
{
	DWORD junk;

	if (size <= 0)
		return -1;

	switch(buffer[0])
	{
	case IC_GET_DEVICE_ID:
		usbi_dbg("got request for device_id");
		WriteFile(pipe_handle, req_device_id, strlen(req_device_id), &junk, NULL);
		break;
	case IC_PRINT_MESSAGE:
		if (size < 2) {
			usbi_err(NULL, "print_message: no data");
			return -1;
		}
		usbi_dbg("[installer process] %s", buffer+1);
		break;
	default:
		usbi_err(NULL, "unrecognized installer message");
		return -1;
	}
	return 0;
}

// Run the elevated installer
int wdi_run_installer(char* path, char* device_id)
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

	req_device_id = device_id;
	memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));

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
	pipe_handle = CreateNamedPipe("\\\\.\\pipe\\libusb-installer", PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,
		PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE, 1, 4096, 4096, 0, NULL);
	if (pipe_handle == INVALID_HANDLE_VALUE) {
		usbi_err(NULL, "could not create read pipe: %s", windows_error_str(0));
		r = -1; goto out;
	}

	// Set the overlapped for messaging
	memset(&overlapped, 0, sizeof(OVERLAPPED));
	handle[0] = CreateEvent(NULL, TRUE, FALSE, NULL);
	if(handle[0] == NULL) {
		r = -1; goto out;
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
		usbi_err(NULL, "this application does not contain the required %s bit installer", is_x64?"64":"32");
		usbi_err(NULL, "please contact the application provider for a %s bit compatible version", is_x64?"64":"32");
		r = -1; goto out;
	}

	GET_WINDOWS_VERSION;
	if (windows_version >= WINDOWS_VISTA) {
		// On Vista and later, we must take care of UAC with ShellExecuteEx + runas
		shExecInfo.cbSize = sizeof(SHELLEXECUTEINFO);

		shExecInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
		shExecInfo.hwnd = NULL;
		shExecInfo.lpVerb = "runas";
		shExecInfo.lpFile = exename;
		// if INF_NAME ever has a space, it will be seen as multiple parameters
		shExecInfo.lpParameters = INF_NAME;
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
			usbi_dbg("operation cancelled by the user");
			r = -1; goto out;
		}
		else if (err) {
			usbi_err(NULL, "ShellExecuteEx failed: %s", windows_error_str(err));
		}

		handle[1] = shExecInfo.hProcess;
	} else {
		// On XP and earlier, simply yse CreateProcess
		safe_strcat(exename, STR_BUFFER_SIZE, " " INF_NAME);
		if (!CreateProcessA(NULL, exename, NULL, NULL, FALSE, CREATE_NO_WINDOW,	NULL, path, &si, &pi)) {
			usbi_err(NULL, "CreateProcess failed: %s", windows_error_str(0));
		}
		handle[1] = pi.hProcess;
	}

	while (1) {
		if (ReadFile(pipe_handle, buffer, STR_BUFFER_SIZE, &rd_count, &overlapped)) {
			// Message was read synchronously
			process_message(buffer, rd_count);
		} else {
			switch(GetLastError()) {
			case ERROR_BROKEN_PIPE:
				// The pipe has been ended - wait for installer to finish
				if ((WaitForSingleObject(handle[1], INSTALLER_TIMEOUT) == WAIT_TIMEOUT)) {
					TerminateProcess(handle[1], 0);
				}
				r = 0; goto out;
			case ERROR_PIPE_LISTENING:
				// Wait for installer to open the pipe
				Sleep(100);
				continue;
			case ERROR_IO_PENDING:
				switch(WaitForMultipleObjects(2, handle, FALSE, INSTALLER_TIMEOUT)) {
				case WAIT_OBJECT_0: // Pipe event
					if (GetOverlappedResult(pipe_handle, &overlapped, &rd_count, FALSE)) {
						// Message was read asynchronously
						process_message(buffer, rd_count);
					} else {
						switch(GetLastError()) {
						case ERROR_BROKEN_PIPE:
							// The pipe has been ended - wait for installer to finish
							if ((WaitForSingleObject(handle[1], INSTALLER_TIMEOUT) == WAIT_TIMEOUT)) {
								TerminateProcess(handle[1], 0);
							}
							r = 0; goto out;
						case ERROR_MORE_DATA:
							usbi_warn(NULL, "program assertion failed: message overflow");
							process_message(buffer, rd_count);
							break;
						default:
							usbi_err(NULL, "could not read from pipe (async): %s", windows_error_str(0));
							break;
						}
					}
					break;
				case WAIT_TIMEOUT:
					// Lost contact
					usbi_err(NULL, "installer failed to respond - aborting");
					TerminateProcess(handle[1], 0);
					r = -1; goto out;
				case WAIT_OBJECT_0+1:
					// installer process terminated
					r = 0; goto out;
				default:
					usbi_err(NULL, "could not read from pipe (wait): %s", windows_error_str(0));
					break;
				}
				break;
			default:
				usbi_err(NULL, "could not read from pipe (sync): %s", windows_error_str(0));
				break;
			}
		}
	}
out:
	safe_closehandle(handle[0]);
	safe_closehandle(handle[1]);
	safe_closehandle(pipe_handle);
	return r;
}

//TODO: add a call to free strings & list
