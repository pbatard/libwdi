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
#include <stdio.h>
#include <inttypes.h>
#include <objbase.h>
#include <shellapi.h>
#include <config.h>
#include <ctype.h>
#include <sddl.h>
#include <fcntl.h>

#include "installer.h"
#include "libwdi.h"
#include "logging.h"
#include "tokenizer.h"
#include "embedded.h"	// auto-generated during compilation
#include "msapi_utf8.h"

// Initial timeout delay to wait for the installer to run
#define DEFAULT_TIMEOUT 10000
// Check if we unexpectedly lose communication with the installer process
#define CHECK_COMPLETION (installer_completed?WDI_SUCCESS:WDI_ERROR_TIMEOUT)

// These warnings are taken care of in configure for other platforms
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

// These functions are defined in libwdi_dlg
extern HWND find_security_prompt(void);
extern int run_with_progress_bar(HWND hWnd, int(*function)(void*), void* arglist);

/*
 * Structure used for the threaded call to install_driver_internal()
 */
struct install_driver_params {
	struct wdi_device_info* device_info;
	char* path;
	char* inf_name;
	struct wdi_options_install_driver* options;
};

/*
 * Tokenizer data
 */
enum INF_TAGS
{
	INF_FILENAME,
	CAT_FILENAME,
	DEVICE_DESCRIPTION,
	DEVICE_HARDWARE_ID,
	DEVICE_INTERFACE_GUID,
	DEVICE_MANUFACTURER,
	DRIVER_DATE,
	DRIVER_VERSION,
};

token_entity_t inf_entities[]=
{
	{"INF_FILENAME",""},
	{"CAT_FILENAME",""},
	{"DEVICE_DESCRIPTION",""},
	{"DEVICE_HARDWARE_ID",""},
	{"DEVICE_INTERFACE_GUID",""},
	{"DEVICE_MANUFACTURER",""},
	{"DRIVER_DATE",""},
	{"DRIVER_VERSION",""},
	{NULL, ""} // DO NOT REMOVE!
};

/*
 * Global variables
 */
struct wdi_device_info *current_device = NULL;
bool dlls_available = false;
bool installer_completed = false;
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

// The following is only available on Vista and later
static BOOL (WINAPI *pIsUserAnAdmin)(void) = NULL;
#define INIT_VISTA_SHELL32 if (pIsUserAnAdmin == NULL) {				\
	pIsUserAnAdmin = (BOOL (WINAPI *)(void))							\
		GetProcAddress(GetModuleHandle("SHELL32"), "IsUserAnAdmin");	\
	}
#define IS_VISTA_SHELL32_AVAILABLE (pIsUserAnAdmin != NULL)


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
			wdi_info("Windows 2000");
		} else if ((os_version.dwMajorVersion == 5) && (os_version.dwMinorVersion == 1)) {
			windows_version = WINDOWS_XP;
			wdi_info("Windows XP");
		} else if ((os_version.dwMajorVersion == 5) && (os_version.dwMinorVersion == 2)) {
			windows_version = WINDOWS_2003_XP64;
			wdi_info("Windows 2003 or Windows XP 64 bit");
		} else if (os_version.dwMajorVersion >= 6) {
			if (os_version.dwBuildNumber < 7000) {
				windows_version = WINDOWS_VISTA;
				wdi_info("Windows Vista");
			} else {
				windows_version = WINDOWS_7;
				wdi_info("Windows 7");
			}
		}
	}
}

/*
 * Converts a windows error to human readable string
 * uses retval as errorcode, or, if 0, use GetLastError()
 */
char *windows_error_str(uint32_t retval)
{
static char err_string[STR_BUFFER_SIZE];

	DWORD size;
	size_t i;
	uint32_t errcode, format_errcode;

	errcode = retval?retval:GetLastError();

	safe_sprintf(err_string, STR_BUFFER_SIZE, "[%d] ", errcode);

	size = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, errcode,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR) &err_string[safe_strlen(err_string)],
		STR_BUFFER_SIZE - (DWORD)safe_strlen(err_string), NULL);
	if (size == 0) {
		format_errcode = GetLastError();
		if (format_errcode)
			safe_sprintf(err_string, STR_BUFFER_SIZE,
				"Windows error code %u (FormatMessage error code %u)", errcode, format_errcode);
		else
			safe_sprintf(err_string, STR_BUFFER_SIZE, "Unknown error code %u", errcode);
	} else {
		// Remove CR/LF terminators
		for (i=safe_strlen(err_string)-1; ((err_string[i]==0x0A) || (err_string[i]==0x0D)); i--) {
			err_string[i] = 0;
		}
	}
	return err_string;
}

/*
 * Retrieve the SID of the current user. The returned PSID must be freed by the caller using LocalFree()
 */
static PSID get_sid(void) {
	TOKEN_USER* tu = NULL;
	DWORD len;
	HANDLE token;
	PSID ret = NULL;
	char* psid_string = NULL;

	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
		wdi_err("OpenProcessToken failed: %s", windows_error_str(0));
		return NULL;
	}

	if (!GetTokenInformation(token, TokenUser, tu, 0, &len)) {
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
			wdi_err("GetTokenInformation (pre) failed: %s", windows_error_str(0));
			return NULL;
		}
		tu = (TOKEN_USER*)calloc(1, len);
		if (tu == NULL) {
			return NULL;
		}
	}

	if (GetTokenInformation(token, TokenUser, tu, len, &len)) {
		/*
		 * now of course, the interesting thing is that if you return tu->User.Sid
		 * but free tu, the PSID pointer becomes invalid after a while.
		 * The workaround? Convert to string then back to PSID
		 */
		if (!ConvertSidToStringSidA(tu->User.Sid, &psid_string)) {
			wdi_err("unable to convert SID to string: %s", windows_error_str(0));
			ret = NULL;
		} else {
			if (!ConvertStringSidToSidA(psid_string, &ret)) {
				wdi_err("unable to convert string back to SID: %s", windows_error_str(0));
				ret = NULL;
			}
			// MUST use LocalFree()
			LocalFree(psid_string);
		}
	} else {
		ret = NULL;
		wdi_err("GetTokenInformation (real) failed: %s", windows_error_str(0));
	}
	free(tu);
	return ret;
}

/*
 * Check whether the path is a directory with write access
 * if create is true, create directory if it doesn't exist
 */
static int check_dir(char* path, bool create)
{
	int r;
	DWORD file_attributes;
	PSID sid = NULL;
	SECURITY_ATTRIBUTES s_attr, *ps = NULL;
	SECURITY_DESCRIPTOR s_desc;

	file_attributes = GetFileAttributesU(path);
	if (file_attributes == INVALID_FILE_ATTRIBUTES) {
		switch (GetLastError()) {
		case ERROR_FILE_NOT_FOUND:
		case ERROR_PATH_NOT_FOUND:
			break;
		default:
			wdi_err("unable to read file attributes %s", windows_error_str(0));
			return WDI_ERROR_ACCESS;
		}
	} else {
		if (file_attributes & FILE_ATTRIBUTE_DIRECTORY) {
			// Directory exists
			return WDI_SUCCESS;
		} else {
			// File with the same name as the dir we want to create
			wdi_err("%s is a file, not a directory", path);
			return WDI_ERROR_ACCESS;
		}
	}

	if (!create) {
		wdi_err("%s doesn't exist", path);
		return WDI_ERROR_ACCESS;
	}

	// Change the owner from admin to regular user
	sid = get_sid();
	if ( (sid != NULL)
	  && InitializeSecurityDescriptor(&s_desc, SECURITY_DESCRIPTOR_REVISION)
	  && SetSecurityDescriptorOwner(&s_desc, sid, FALSE) ) {
		s_attr.nLength = sizeof(SECURITY_ATTRIBUTES);
		s_attr.bInheritHandle = FALSE;
		s_attr.lpSecurityDescriptor = &s_desc;
		ps = &s_attr;
	} else {
		wdi_err("could not set security descriptor: %s", windows_error_str(0));
	}

	// SHCreateDirectoryEx creates subdirectories as required
	r = SHCreateDirectoryExU(NULL, path, ps);
	if (sid != NULL) LocalFree(sid);

	switch(r) {
	case ERROR_SUCCESS:
		return WDI_SUCCESS;
	case ERROR_BAD_PATHNAME:
		wdi_err("directory path is invalid %s", path);
		return WDI_ERROR_INVALID_PARAM;
	case ERROR_FILENAME_EXCED_RANGE:
		wdi_err("directory name is too long %s", path);
		return WDI_ERROR_INVALID_PARAM;
	default:
		wdi_err("unable to create directory %s (%s)", path, windows_error_str(0));
		return WDI_ERROR_ACCESS;
	}

	return WDI_SUCCESS;
}

/*
 * fopen equivalent, that uses CreateFile with security attributes
 * to create file as the user of the application
 */
static FILE *fcreate(const char *filename, const char *mode)
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
	for (i=0; i<safe_strlen(mode); i++) {
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
	sid = get_sid();
	if ( (sid != NULL)
	  && InitializeSecurityDescriptor(&s_desc, SECURITY_DESCRIPTOR_REVISION)
	  && SetSecurityDescriptorOwner(&s_desc, sid, FALSE) ) {
		s_attr.nLength = sizeof(SECURITY_ATTRIBUTES);
		s_attr.bInheritHandle = FALSE;
		s_attr.lpSecurityDescriptor = &s_desc;
		ps = &s_attr;
	} else {
		wdi_err("could not set security descriptor: %s", windows_error_str(0));
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

/*
 * Find out if the driver selected is actually embedded in this version of the library
 */
bool LIBWDI_API wdi_is_driver_supported(int driver_type, VS_FIXEDFILEINFO** driver_info)
{
	if (driver_info != NULL) {
		*driver_info = NULL;
	}
	switch (driver_type) {
	case WDI_WINUSB:
#if defined(DDK_DIR)
		if (driver_info != NULL) {
			*driver_info = (VS_FIXEDFILEINFO*)&driver_version[0];
		}
		// WinUSB is not supported on Win2k/2k3
		GET_WINDOWS_VERSION;
		if ( (windows_version == WINDOWS_2K)
		  || (windows_version == WINDOWS_2003_XP64) ) {
			return false;
		}
		return true;
#else
		return false;
#endif
	case WDI_LIBUSB:
#if defined(LIBUSB0_DIR)
		if (driver_info != NULL) {
			*driver_info = (VS_FIXEDFILEINFO*)&driver_version[1];
		}
		return true;
#else
		return false;
#endif
	case WDI_USER:
#if defined(USER_DIR)
		return true;
#else
		return false;
#endif
	default:
		wdi_err("unknown driver type");
		return false;
	}
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
const char* LIBWDI_API wdi_strerror(int errcode)
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
		return "Requested resource not found";
	case WDI_ERROR_BUSY:
		return "Requested resource busy or same function call already in process";
	case WDI_ERROR_TIMEOUT:
		return "Operation timed out";
	case WDI_ERROR_OVERFLOW:
		return "Overflow";
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
	case WDI_ERROR_PENDING_INSTALLATION:
		return "Another installer is already running";
	case WDI_ERROR_NEEDS_ADMIN:
		return "Unable to run installer process with administrative privileges";
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
	return "Unknown error";
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
	safe_free(di->device_id);
	safe_free(di->hardware_id);
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
	return WDI_SUCCESS;
}

// List USB devices
int LIBWDI_API wdi_create_list(struct wdi_device_info** list,
							   struct wdi_options_create_list* options)
{
	unsigned i, j, tmp;
	unsigned unknown_count = 1;
	DWORD size, reg_type;
	ULONG devprop_type;
	CONFIGRET r;
	HDEVINFO dev_info;
	SP_DEVINFO_DATA dev_info_data;
	char *prefix[3] = {"VID_", "PID_", "MI_"};
	char *token, *end;
	char strbuf[STR_BUFFER_SIZE];
	wchar_t desc[MAX_DESC_LENGTH];
	struct wdi_device_info *start = NULL, *cur = NULL, *device_info = NULL;
	const char usbhub_name[] = "usbhub";
	const char usbccgp_name[] = "usbccgp";
	bool is_hub, is_composite_parent, has_vid;

	MUTEX_START;

	if (!dlls_available) {
		init_dlls();
	}

	// List all connected USB devices
	dev_info = SetupDiGetClassDevs(NULL, "USB", NULL, DIGCF_PRESENT|DIGCF_ALLCLASSES);
	if (dev_info == INVALID_HANDLE_VALUE) {
		*list = NULL;
		MUTEX_RETURN WDI_ERROR_NO_DEVICE;
	}

	// Find the ones that are driverless
	for (i = 0; ; i++)
	{
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
			*list = NULL;
			MUTEX_RETURN WDI_ERROR_RESOURCE;
		}

		// SPDRP_DRIVER seems to do a better job at detecting driverless devices than
		// SPDRP_INSTALL_STATE
		if (SetupDiGetDeviceRegistryPropertyA(dev_info, &dev_info_data, SPDRP_DRIVER,
			&reg_type, (BYTE*)strbuf, STR_BUFFER_SIZE, &size)) {
			if ((options == NULL) || (!options->list_all)) {
				continue;
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
		is_hub = false;
		if (safe_strcmp(strbuf, usbhub_name) == 0) {
			if (!options->list_hubs) {
				continue;
			}
			is_hub = true;
		}
		// Also eliminate composite devices parent drivers, as replacing these drivers
		// is a bad idea
		is_composite_parent = false;
		if (safe_strcmp(strbuf, usbccgp_name) == 0) {
			if (!options->list_hubs) {
				continue;
			}
			is_composite_parent = true;
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
					i, "yorgl"); // windows_error_str(0));
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
					wdi_dbg("could not read device description for %d: %s",
						i, windows_error_str(0));
					safe_swprintf(desc, MAX_DESC_LENGTH, L"Unknown Device #%d", unknown_count++);
				}
			}
		}

		device_info->is_composite = false;	// non composite by default
		device_info->mi = 0;
		token = strtok (strbuf, "\\#&");
		has_vid = false;
		while(token != NULL) {
			for (j = 0; j < 3; j++) {
				if (safe_strncmp(token, prefix[j], safe_strlen(prefix[j])) == 0) {
					switch(j) {
					case 0:
						if (sscanf(token, "VID_%04X", &tmp) != 1) {
							wdi_err("could not convert VID string");
						} else {
							device_info->vid = (unsigned short)tmp;
						}
						has_vid = true;
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
							device_info->is_composite = true;
							device_info->mi = (unsigned char)tmp;
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
		if ((options != NULL) && (options->trim_whitespaces)) {
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
	MUTEX_RETURN (*list==NULL)?WDI_ERROR_NO_DEVICE:WDI_SUCCESS;
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
	MUTEX_RETURN WDI_SUCCESS;
}

// extract the embedded binary resources
static int extract_binaries(char* path)
{
	FILE *fd;
	char filename[MAX_PATH_LENGTH];
	int i, r;

	for (i=0; i<nb_resources; i++) {
		// Ignore tokenizer files
		if (resource[i].subdir[0] == 0) {
			continue;
		}
		safe_strcpy(filename, MAX_PATH_LENGTH, path);
		safe_strcat(filename, MAX_PATH_LENGTH, "\\");
		safe_strcat(filename, MAX_PATH_LENGTH, resource[i].subdir);

		r = check_dir(filename, true);
		if (r != WDI_SUCCESS) {
			return r;
		}
		safe_strcat(filename, MAX_PATH_LENGTH, "\\");
		safe_strcat(filename, MAX_PATH_LENGTH, resource[i].name);

		fd = fcreate(filename, "w");
		if (fd == NULL) {
			wdi_err("failed to create file: %s", filename);
			return WDI_ERROR_RESOURCE;
		}

		fwrite(resource[i].data, 1, resource[i].size, fd);
		fclose(fd);
	}

	wdi_info("successfully extracted driver files to %s", path);
	return WDI_SUCCESS;
}

// tokenizes a resource stored in resource.h
static long tokenize_internal(char* resource_name, char** dst, const token_entity_t* token_entities,
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

// Create an inf and extract coinstallers in the directory pointed by path
int LIBWDI_API wdi_prepare_driver(struct wdi_device_info* device_info, char* path,
								  char* inf_name, struct wdi_options_prepare_driver* options)
{
	const wchar_t bom = 0xFEFF;
	char filename[MAX_PATH_LENGTH];
	FILE* fd;
	GUID guid;
	int driver_type, r;
	SYSTEMTIME system_time;
	FILETIME file_time;
	char* cat_name = NULL;
	const char* inf_ext = ".inf";
	const char* vendor_name = NULL;
	char *dst = NULL;
	wchar_t *wdst = NULL;
	long inf_file_size;

	MUTEX_START;

	if ((device_info == NULL) || (inf_name == NULL)) {
		wdi_err("one of the required parameter is NULL");
		MUTEX_RETURN WDI_ERROR_INVALID_PARAM;
	}

	if (!dlls_available) {
		init_dlls();
	}

	// Check the inf file provided and create the cat file name
	if (strcmp(inf_name+safe_strlen(inf_name)-4, inf_ext) != 0) {
		wdi_err("inf name provided must have a '.inf' extension");
		MUTEX_RETURN WDI_ERROR_INVALID_PARAM;
	}

	// Try to use the user's temp dir if no path is provided
	if ((path == NULL) || (path[0] == 0)) {
		path = getenv("TEMP");
		if (path == NULL) {
			wdi_err("no path provided and unable to use TEMP");
			MUTEX_RETURN WDI_ERROR_INVALID_PARAM;
		} else {
			wdi_info("no path provided - extracting to '%s'", path);
		}
	}

	// Try to create directory if it doesn't exist
	r = check_dir(path, true);
	if (r != WDI_SUCCESS) {
		MUTEX_RETURN r;
	}

	if (options == NULL) {
		for (driver_type=0; driver_type<WDI_NB_DRIVERS; driver_type++) {
			if (wdi_is_driver_supported(driver_type, NULL)) {
				break;
			}
		}
		if (driver_type == WDI_NB_DRIVERS) {
			wdi_warn("Program assertion failed - no driver supported");
			MUTEX_RETURN WDI_ERROR_NOT_FOUND;
		}
	} else {
		driver_type = options->driver_type;
	}

	// For custom drivers, as we cannot autogenerate the inf, simply extract binaries
	if (driver_type == WDI_USER) {
		wdi_warn("Custom driver - extracting binaries only (no inf/cat creation)");
		MUTEX_RETURN extract_binaries(path);
	}

	if ( (driver_type != WDI_LIBUSB) && (driver_type != WDI_WINUSB) )  {
		wdi_err("unknown type");
		MUTEX_RETURN WDI_ERROR_INVALID_PARAM;
	}

	if (device_info->desc == NULL) {
		wdi_err("no description was given for the device - aborting");
		MUTEX_RETURN WDI_ERROR_NOT_FOUND;
	}

	r = extract_binaries(path);
	if (r != WDI_SUCCESS) {
		MUTEX_RETURN r;
	}

	// Set the inf filename
	safe_strcpy(filename, MAX_PATH_LENGTH, path);
	safe_strcat(filename, MAX_PATH_LENGTH, "\\");
	safe_strcat(filename, MAX_PATH_LENGTH, inf_name);

	// Populate the inf and cat names
	static_strcpy(inf_entities[INF_FILENAME].replace, inf_name);
	cat_name = safe_strdup(inf_name);
	if (cat_name == NULL) {
		MUTEX_RETURN WDI_ERROR_RESOURCE;
	}
	cat_name[safe_strlen(inf_name)-3] = 'c';
	cat_name[safe_strlen(inf_name)-2] = 'a';
	cat_name[safe_strlen(inf_name)-1] = 't';
	static_strcpy(inf_entities[CAT_FILENAME].replace, cat_name);
	safe_free(cat_name);

	// Populate the Device Description and Hardware ID
	static_strcpy(inf_entities[DEVICE_DESCRIPTION].replace, device_info->desc);
	if (device_info->is_composite) {
		static_sprintf(inf_entities[DEVICE_HARDWARE_ID].replace, "VID_%04X&PID_%04X&MI_%02X",
			device_info->vid, device_info->pid, device_info->mi);
	} else {
		static_sprintf(inf_entities[DEVICE_HARDWARE_ID].replace, "VID_%04X&PID_%04X",
			device_info->vid, device_info->pid);
	}

	// Populate the Device Interface GUID
	CoCreateGuid(&guid);
	static_sprintf(inf_entities[DEVICE_INTERFACE_GUID].replace, "%s", guid_to_string(guid));

	// Resolve the Manufacturer (Vendor Name)
	if ((options != NULL) && (options->vendor_name != NULL)) {
		static_strcpy(inf_entities[DEVICE_MANUFACTURER].replace, options->vendor_name);
	} else {
		vendor_name = wdi_get_vendor_name(device_info->vid);
		if (vendor_name == NULL) {
			vendor_name = "(Unknown Vendor)";
		}
		static_strcpy(inf_entities[DEVICE_MANUFACTURER].replace, vendor_name);
	}

	// Extra check, in case somebody modifies our code
	if ((driver_type < 0) && (driver_type > sizeof(driver_version)/sizeof(driver_version[0]))) {
		wdi_err("program assertion failed - driver_version[] index out of range");
		MUTEX_RETURN WDI_ERROR_OTHER;
	}

	// Write the date and version data
	file_time.dwHighDateTime = driver_version[driver_type].dwFileDateMS;
	file_time.dwLowDateTime = driver_version[driver_type].dwFileDateLS;
	if ( ((file_time.dwHighDateTime == 0) && (file_time.dwLowDateTime == 0))
	  || (!FileTimeToSystemTime(&file_time, &system_time)) ) {
		GetLocalTime(&system_time);
	}
	static_sprintf(inf_entities[DRIVER_DATE].replace,
		"%02d/%02d/%04d", system_time.wMonth, system_time.wDay, system_time.wYear);
	static_sprintf(inf_entities[DRIVER_VERSION].replace, "%d.%d.%d.%d",
		(int)driver_version[driver_type].dwFileVersionMS>>16, (int)driver_version[driver_type].dwFileVersionMS&0xFFFF,
		(int)driver_version[driver_type].dwFileVersionLS>>16, (int)driver_version[driver_type].dwFileVersionLS&0xFFFF);

	// Tokenize the file
	if ((inf_file_size = tokenize_internal((driver_type == WDI_WINUSB)?"winusb.inf.in":"libusb-win32.inf.in",
		&dst, inf_entities, "#", "#" ,0)) > 0) {
		fd = fcreate(filename, "w");
		if (fd == NULL) {
			wdi_err("failed to create file: %s", filename);
			MUTEX_RETURN WDI_ERROR_ACCESS;
		}
		// Converting to UTF-16 is the only way to get devices using a
		// non-english locale to display properly in device manager. UTF-8 will not do.
		wdst = utf8_to_wchar(dst);
		fwrite(&bom, 2, 1, fd);	// Write the BOM
		fwrite(wdst, 2, wcslen(wdst), fd);
		fclose(fd);
		safe_free(wdst);
		safe_free(dst);
	} else {
		wdi_err("could not tokenize inf file (%d)", inf_file_size);
		MUTEX_RETURN WDI_ERROR_ACCESS;
	}

	// Create a blank cat file
	filename[safe_strlen(filename)-3] = 'c';
	filename[safe_strlen(filename)-2] = 'a';
	filename[safe_strlen(filename)-1] = 't';
	fd = fcreate(filename, "w");
	if (fd == NULL) {
		wdi_err("failed to create file: %s", filename);
		MUTEX_RETURN WDI_ERROR_ACCESS;
	}
	fprintf(fd, "This file will contain the digital signature of the files to be installed\n"
		"on the system.\nThis file will be provided by Microsoft upon certification of your drivers.");
	fclose(fd);

	// Restore extension for debug output
	filename[safe_strlen(filename)-3] = 'i';
	filename[safe_strlen(filename)-2] = 'n';
	filename[safe_strlen(filename)-1] = 'f';
	wdi_info("succesfully created %s", filename);
	MUTEX_RETURN WDI_SUCCESS;
}

// Handle messages received from the elevated installer through the pipe
static int process_message(char* buffer, DWORD size)
{
	DWORD tmp;
	char* sid_str;

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
		if (current_device->device_id != NULL) {
			WriteFile(pipe_handle, current_device->device_id, (DWORD)safe_strlen(current_device->device_id), &tmp, NULL);
		} else {
			wdi_warn("no device_id - sending empty string");
			WriteFile(pipe_handle, "\0", 1, &tmp, NULL);
		}
		break;
	case IC_GET_HARDWARE_ID:
		wdi_dbg("got request for hardware_id");
		if (current_device->hardware_id != NULL) {
			WriteFile(pipe_handle, current_device->hardware_id, (DWORD)safe_strlen(current_device->hardware_id), &tmp, NULL);
		} else {
			wdi_warn("no hardware_id - sending empty string");
			WriteFile(pipe_handle, "\0", 1, &tmp, NULL);
		}
		break;
	case IC_PRINT_MESSAGE:
		if (size < 2) {
			wdi_err("print_message: no data");
			return WDI_ERROR_NOT_FOUND;
		}
		wdi_log(WDI_LOG_LEVEL_DEBUG, "installer process", "%s", buffer+1);
		break;
	case IC_SYSLOG_MESSAGE:
		if (size < 2) {
			wdi_err("syslog_message: no data");
			return WDI_ERROR_NOT_FOUND;
		}
		wdi_log(WDI_LOG_LEVEL_DEBUG, "syslog", "%s", buffer+1);
		break;
	case IC_SET_STATUS:
		if (size < 2) {
			wdi_err("set status: no data");
			return WDI_ERROR_NOT_FOUND;
		}
		return (int)buffer[1];
		break;
	case IC_SET_TIMEOUT_INFINITE:
		wdi_dbg("switching timeout to infinite");
		timeout = INFINITE;
		break;
	case IC_SET_TIMEOUT_DEFAULT:
		wdi_dbg("switching timeout back to finite");
		timeout = DEFAULT_TIMEOUT;
		break;
	case IC_INSTALLER_COMPLETED:
		wdi_dbg("installer process completed");
		installer_completed = true;
		break;
	case IC_GET_USER_SID:
		if (ConvertSidToStringSidA(get_sid(), &sid_str)) {
			WriteFile(pipe_handle, sid_str, (DWORD)safe_strlen(sid_str), &tmp, NULL);
			LocalFree(sid_str);
		} else {
			wdi_warn("no user_sid - sending empty string");
			WriteFile(pipe_handle, "\0", 1, &tmp, NULL);
		}
		break;
	default:
		wdi_err("unrecognized installer message");
		return WDI_ERROR_NOT_FOUND;
	}
	return WDI_SUCCESS;
}

// Run the elevated installer
static int install_driver_internal(void* arglist)
{
	struct install_driver_params* params = (struct install_driver_params*)arglist;
	struct wdi_device_info* device_info = params->device_info;
	char* path = params->path;
	char* inf_name = params->inf_name;

	SHELLEXECUTEINFOA shExecInfo;
	STARTUPINFOA si;
	PROCESS_INFORMATION pi;
	char exename[STR_BUFFER_SIZE];
	HANDLE handle[2] = {INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE};
	OVERLAPPED overlapped;
	int r;
	DWORD err, rd_count, to_read, offset, bufsize = STR_BUFFER_SIZE;
	BOOL is_x64 = false;
	char *buffer = NULL, *new_buffer;

	MUTEX_START;

	if (!dlls_available) {
		init_dlls();
	}

	current_device = device_info;

	// Try to use the user's temp dir if no path is provided
	if ((path == NULL) || (path[0] == 0)) {
		path = getenv("TEMP");
		if (path == NULL) {
			wdi_err("no path provided and unable to use TEMP");
			MUTEX_RETURN WDI_ERROR_INVALID_PARAM;
		} else {
			wdi_info("no path provided - installing from '%s'", path);
		}
	}

	if ((device_info == NULL) || (inf_name == NULL)) {
		wdi_err("one of the required parameter is NULL");
		MUTEX_RETURN WDI_ERROR_INVALID_PARAM;
	}

	// Detect if another installation is in process
	if (CMP_WaitNoPendingInstallEvents != NULL) {
		if (CMP_WaitNoPendingInstallEvents(0) == WAIT_TIMEOUT) {
			wdi_warn("detected another pending installation - aborting");
			MUTEX_RETURN WDI_ERROR_PENDING_INSTALLATION;
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
	if (GetFileAttributesU(exename) == INVALID_FILE_ATTRIBUTES) {
		wdi_err("this application does not contain the required %s bit installer", is_x64?"64":"32");
		wdi_err("please contact the application provider for a %s bit compatible version", is_x64?"64":"32");
		r = WDI_ERROR_NOT_FOUND; goto out;
	}

	installer_completed = false;
	GET_WINDOWS_VERSION;
	INIT_VISTA_SHELL32;
	if ( (windows_version >= WINDOWS_VISTA) && IS_VISTA_SHELL32_AVAILABLE && (!pIsUserAnAdmin()) )  {
		// On Vista and later, we must take care of UAC with ShellExecuteEx + runas
		shExecInfo.cbSize = sizeof(SHELLEXECUTEINFOA);
		shExecInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
		shExecInfo.hwnd = NULL;
		shExecInfo.lpVerb = "runas";
		shExecInfo.lpFile = exename;
		shExecInfo.lpParameters = inf_name;
		shExecInfo.lpDirectory = path;
		shExecInfo.nShow = SW_HIDE;
		shExecInfo.hInstApp = NULL;

		err = 0;
		if (!ShellExecuteExU(&shExecInfo)) {
			err = GetLastError();
		}

		if ((err == ERROR_CANCELLED) || (shExecInfo.hProcess == NULL)) {
			wdi_info("operation cancelled by the user");
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
		if (!CreateProcessU(NULL, exename, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, path, &si, &pi)) {
			wdi_err("CreateProcess failed: %s", windows_error_str(0));
			r = WDI_ERROR_NEEDS_ADMIN; goto out;
		}
		handle[1] = pi.hProcess;
	}

	r = WDI_SUCCESS;
	offset = 0;
	buffer = malloc(bufsize);
	if (buffer == NULL) {
		wdi_err("unable to alloc buffer: aborting");
		r =WDI_ERROR_RESOURCE; goto out;
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
				r = CHECK_COMPLETION; goto out;
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
							r = CHECK_COMPLETION; goto out;
						case ERROR_MORE_DATA:
							bufsize *= 2;
							wdi_dbg("message overflow (async) - increasing buffer size to %d bytes", bufsize);
							new_buffer = realloc(buffer, bufsize);
							if (new_buffer == NULL) {
								wdi_err("unable to realloc buffer: aborting");
								r = WDI_ERROR_RESOURCE;
							} else {
								buffer = new_buffer;
								offset += to_read;
							}
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
					r = CHECK_COMPLETION; goto out;
				default:
					wdi_err("could not read from pipe (wait): %s", windows_error_str(0));
					break;
				}
				break;
			case ERROR_MORE_DATA:
				bufsize *= 2;
				wdi_dbg("message overflow (sync) - increasing buffer size to %d bytes", bufsize);
				new_buffer = realloc(buffer, bufsize);
				if (new_buffer == NULL) {
					wdi_err("unable to realloc buffer: aborting");
					r = WDI_ERROR_RESOURCE;
				} else {
					buffer = new_buffer;
					offset += to_read;
				}
				break;
			default:
				wdi_err("could not read from pipe (sync): %s", windows_error_str(0));
				break;
			}
		}
	}
out:
	// If the security prompt is still active, attempt to destroy it
	DestroyWindow(find_security_prompt());
	current_device = NULL;
	safe_free(buffer);
	safe_closehandle(handle[0]);
	safe_closehandle(handle[1]);
	safe_closehandle(pipe_handle);
	MUTEX_RETURN r;
}

int LIBWDI_API wdi_install_driver(struct wdi_device_info* device_info, char* path,
								  char* inf_name, struct wdi_options_install_driver* options)
{
	struct install_driver_params params;
	params.device_info = device_info;
	params.inf_name = inf_name;
	params.options = options;
	params.path = path;

	if ((options == NULL) || (options->hWnd == NULL)) {
		wdi_dbg("using standard mode");
		return install_driver_internal((void*)&params);
	}
	wdi_dbg("using progress bar mode");
	return run_with_progress_bar(options->hWnd, install_driver_internal, (void*)&params);
}
