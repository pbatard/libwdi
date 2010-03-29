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

// This standalone installer is a separate exe, as it needs
// - administrative rights, and therefore UAC elevation on platforms that use UAC
// - native 32 or 64 bit execution according to the platform

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <setupapi.h>
#if defined(_MSC_VER)
#include <newdev.h>
#else
#include <ddk/newdev.h>
#endif
#include "installer.h"

#define INF_NAME "libusb-device.inf"
#define REQUEST_TIMEOUT 5000

/*
 * Cfgmgr32.dll interface
 */
typedef CHAR *DEVNODEID_A, *DEVINSTID_A;
typedef WCHAR *DEVNODEID_W, *DEVINSTID_W;
#ifdef UNICODE
typedef DEVNODEID_W DEVNODEID;
typedef DEVINSTID_W DEVINSTID;
#else
typedef DEVNODEID_A DEVNODEID;
typedef DEVINSTID_A DEVINSTID;
#endif

DLL_DECLARE(WINAPI, CONFIGRET, CM_Locate_DevNode, (PDEVINST, DEVINSTID, ULONG));
DLL_DECLARE(WINAPI, CONFIGRET, CM_Reenumerate_DevNode, (DEVINST, ULONG));

/*
 * Globals
 */
HANDLE pipe_handle = INVALID_HANDLE_VALUE;
//enum windows_version windows_version = WINDOWS_UNDEFINED;


// Setup the Cfgmgr32 DLLs
static int init_dlls(void)
{
	DLL_LOAD(Cfgmgr32.dll, CM_Locate_DevNode, TRUE);
	DLL_LOAD(Cfgmgr32.dll, CM_Reenumerate_DevNode, TRUE);
	return 0;
}

/*
// Detect Windows version
#define GET_WINDOWS_VERSION do{ if (windows_version == WINDOWS_UNDEFINED) detect_version(); } while(0)
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
*/

// Log data with parent app through the pipe
// TODO: return a status byte along with the message
void plog_v(const char *format, va_list args)
{
	char buffer[256];
	DWORD junk;
	int size;

	if (pipe_handle == INVALID_HANDLE_VALUE)
		return;

	buffer[0] = IC_PRINT_MESSAGE;

	size = safe_vsnprintf(buffer+1, 255, format, args);
	if (size < 0) {
		buffer[255] = 0;
		size = 254;
	}
	WriteFile(pipe_handle, buffer, (DWORD)size+2, &junk, NULL);
}

void plog(const char *format, ...)
{
	va_list args;

	va_start (args, format);
	plog_v(format, args);
	va_end (args);
}

// Notify the parent app
void send_status(char status)
{
	DWORD junk;
	WriteFile(pipe_handle, &status, 1, &junk, NULL);
}

// Query the parent app for data
int request_data(char req, void *buffer, int size)
{
	OVERLAPPED overlapped;
	DWORD rd_count;
	DWORD r, count = (DWORD)size;

	if ((buffer == NULL) || (size <= 0)) {
		return -1;
	}

	// Set the overlapped for messaging
	memset(&overlapped, 0, sizeof(OVERLAPPED));
	overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (overlapped.hEvent == NULL) {
		plog("failed to create overlapped");
		return -1;
	}

	if (ReadFile(pipe_handle, buffer, count, &rd_count, &overlapped)) {
		// Message was read synchronously
		plog("received unexpected data");
		CloseHandle(overlapped.hEvent);
		return -1;
	}

	if (GetLastError() != ERROR_IO_PENDING) {
		plog("failure to initiate read (%d)", (int)GetLastError());
		CloseHandle(overlapped.hEvent);
		return -1;
	}

	// Now that we're set to receive data, let's send our request
	WriteFile(pipe_handle, &req, 1, &r, NULL);

	// Wait for the response
	r = WaitForSingleObject(overlapped.hEvent, REQUEST_TIMEOUT);
	if ( (r == WAIT_OBJECT_0) && (GetOverlappedResult(pipe_handle, &overlapped, &rd_count, FALSE)) ) {
		CloseHandle(overlapped.hEvent);
		return (int)rd_count;
	}

	if (r == WAIT_TIMEOUT) {
		plog("message request: timed out");
	} else {
		plog("read error: %d", (int)GetLastError());
	}
	CloseHandle(overlapped.hEvent);
	return -1;
}

// Query parent app for device ID
char* req_id(enum installer_code id_code)
{
	int size;
	static char device_id[MAX_PATH_LENGTH];
	static char hardware_id[MAX_PATH_LENGTH];
	char* id = NULL;

	switch(id_code) {
	case IC_GET_DEVICE_ID:
		id = device_id;
		break;
	case IC_GET_HARDWARE_ID:
		id = hardware_id;
		break;
	default:
		plog("req_id: unknown ID requested");
		return NULL;
	}

	memset(id, 0, MAX_PATH_LENGTH);
	size = request_data(id_code, (void*)id, MAX_PATH_LENGTH);
	if (size > 0) {
		plog("got %s_id: %s", (id_code==IC_GET_DEVICE_ID)?"device":"hardware", id);
		return id;
	}

	plog("failed to read %s_id", (id_code==IC_GET_DEVICE_ID)?"device":"hardware");
	return NULL;
}

// Query parent app for hardware ID
char* req_hardware_id(void)
{
	int size;
	static char hardware_id[MAX_PATH_LENGTH];

	memset(hardware_id, 0, MAX_PATH_LENGTH);
	size = request_data(IC_GET_HARDWARE_ID, (void*)hardware_id, sizeof(hardware_id));
	if (size > 0) {
		plog("got hardware_id: %s", hardware_id);
		return hardware_id;
	}

	plog("failed to read hardware_id");
	return NULL;
}

// Force re-enumeration of a device (force installation)
// TODO: allow root re-enum
int update_driver(char* device_id)
{
	DEVINST dev_inst;
	CONFIGRET status;

	plog("re-enumerating driver node %s...", device_id);
	status = CM_Locate_DevNode(&dev_inst, device_id, 0);
	if (status != CR_SUCCESS) {
		plog("failed to locate device_id %s: %x\n", device_id, status);
		return -1;
	}

	status = CM_Reenumerate_DevNode(dev_inst, CM_REENUMERATE_RETRY_INSTALLATION);
	if (status != CR_SUCCESS) {
		plog("failed to re-enumerate device node: CR code %X", status);
		return -1;
	}

	plog("re-enumeration succeeded...");
	return 0;
}

// TODO: allow commandline options
int
#ifdef _MSC_VER
__cdecl
#endif
main(int argc, char** argv)
{
	DWORD r;
	BOOL b;
	char* hardware_id;
	char* device_id;
	char path[MAX_PATH_LENGTH];
	char log[MAX_PATH_LENGTH];
	char destname[MAX_PATH_LENGTH];
	FILE *fd;

	// Connect to the messaging pipe
	pipe_handle = CreateFile("\\\\.\\pipe\\libusb-installer", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OVERLAPPED, NULL);
	if (pipe_handle == INVALID_HANDLE_VALUE) {
		printf("could not open pipe for writing: errcode %d\n", (int)GetLastError());
		return -1;
	}

	if (init_dlls()) {
		plog("could not init DLLs");
		return -1;
	}

	safe_strcpy(log, MAX_PATH_LENGTH, argv[0]);
	// TODO - seek for terminal '.exe' and change extension if needed
	safe_strcat(log, MAX_PATH_LENGTH, ".log");

	fd = fopen(log, "w");
	if (fd == NULL) {
		plog("could not open logfile");
		goto out;
	}

	if (argc >= 2) {
		plog("got parameter %s", argv[1]);
		printf("got param %s", argv[1]);
	}

	r = GetFullPathNameA(".", MAX_PATH_LENGTH, path, NULL);
	if ((r == 0) || (r > MAX_PATH_LENGTH)) {
		plog("could not retrieve absolute path of working directory");
		goto out;
	}
	safe_strcat(path, MAX_PATH_LENGTH, "\\");
	safe_strcat(path, MAX_PATH_LENGTH, INF_NAME);

	device_id = req_id(IC_GET_DEVICE_ID);
	hardware_id = req_id(IC_GET_HARDWARE_ID);

	// Find if the device is plugged in
	send_status(IC_SET_TIMEOUT_INFINITE);
	plog("Installing driver - please wait...");
	r = UpdateDriverForPlugAndPlayDevicesA(NULL, hardware_id, path, INSTALLFLAG_FORCE, NULL);
	send_status(IC_SET_TIMEOUT_DEFAULT);
	if (r == true) {
		// Success
		plog("driver update completed");
		// TODO: remove this?
		update_driver(device_id);
		goto out;
	}

	// Will fail if inf not signed, unless DRIVER_PACKAGE_LEGACY_MODE is specified.
	// r = 87 ERROR_INVALID_PARAMETER on path == NULL
	// r = 2 ERROR_FILE_NOT_FOUND => failed to open inf
	// r = 5 ERROR_ACCESS_DENIED if needs admin elevation
	// r = 0xD ERROR_INVALID_DATA => inf is missing some data
	// r = 0xE0000003 ERROR_GENERAL_SYNTAX the syntax of the inf is invalid or the inf is empty
	// r = 0xE0000304 ERROR_INVALID_CATALOG_DATA => no cat
	// r = 0xE000023F ERROR_NO_AUTHENTICODE_CATALOG => user cancelled on warnings
	// r = 0xE0000235 ERROR_IN_WOW64 => trying to run a 32 bit installer on a 64 bit machine
	// r = 0xE0000247 ERROR_DRIVER_STORE_ADD_FAILED if user decided not to install on warnings
	// r = 0x800B0100 ERROR_WRONG_INF_STYLE => missing cat entry in inf
	// r = 0xB7 => missing DRIVER_PACKAGE_REPAIR flag
	switch(r = GetLastError()) {
	case ERROR_NO_MORE_ITEMS:
		plog("more recent driver was found (INSTALLFLAG_FORCE option required)");
		goto out;
	case ERROR_NO_SUCH_DEVINST:
		plog("device not detected (copying driver files for next time device is plugged in)");
		break;
	case ERROR_INVALID_PARAMETER:
		plog("invalid path");
		goto out;
	case ERROR_FILE_NOT_FOUND:
		plog("failed to open %s", path);
		goto out;
	case ERROR_ACCESS_DENIED:
		plog("this process needs to be run with administrative privileges");
		goto out;
	case ERROR_IN_WOW64:
		plog("attempted to use a 32 bit installer on a 64 bit machine");
		goto out;
	case ERROR_INVALID_DATA:
	case ERROR_WRONG_INF_STYLE:
	case ERROR_GENERAL_SYNTAX:
		plog("the syntax of the inf is invalid");
		goto out;
	case ERROR_INVALID_CATALOG_DATA:
		plog("unable to locate cat file");
		goto out;
	case ERROR_NO_AUTHENTICODE_CATALOG:
	case ERROR_DRIVER_STORE_ADD_FAILED:
		plog("operation cancelled by the user");
		goto out;
	case ERROR_ALREADY_EXISTS:
		plog("driver already exists");
		goto out;
	default:
		plog("unhandled error %X", r);
		goto out;
	}

	// TODO: try URL for OEMSourceMediaLocation
	send_status(IC_SET_TIMEOUT_INFINITE);
	b = SetupCopyOEMInfA(path, NULL, SPOST_NONE, SP_COPY_DELETESOURCE, destname, MAX_PATH_LENGTH, NULL, NULL);
	send_status(IC_SET_TIMEOUT_DEFAULT);
	if (b) {
		plog("copied inf to %s", destname);
	} else {
		switch(r = GetLastError()) {
		default:
			plog("SetupCopyOEMInf error %X", r);
			break;
		}
	}

	// TODO: remove phantom drivers as per http://msdn.microsoft.com/en-us/library/aa906206.aspx

out:
	CloseHandle(pipe_handle);
	return 0;
}
