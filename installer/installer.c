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

// This standalone installer is a separate exe, as it needs to be run
// through ShellExecuteEx() for UAC elevation

#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <direct.h>
#include <setupapi.h>
#include <api/difxapi.h>
#include <fcntl.h>
#include <io.h>
#include <stdarg.h>
#include "installer.h"

#define INF_NAME "libusb-device.inf"
#define MAX_PATH_LENGTH 128
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
HANDLE pipe = INVALID_HANDLE_VALUE;

// Setup the Cfgmgr32 DLL
static int init_dlls(void)
{
	DLL_LOAD(Cfgmgr32.dll, CM_Locate_DevNode, TRUE);
	DLL_LOAD(Cfgmgr32.dll, CM_Reenumerate_DevNode, TRUE);
	return 0;
}

// Log data with parent app through the pipe
// TODO: return a status byte along with the message
void plog_v(const char *format, va_list args)
{
	char buffer[256];
	int size;

	if (pipe == INVALID_HANDLE_VALUE)
		return;

	buffer[0] = IC_PRINT_MESSAGE;
	size = vsnprintf_s(buffer+1, 255, _TRUNCATE, format, args);
	if (size < 0) {
		buffer[255] = 0;
		size = 254;
	}
	WriteFile(pipe, buffer, size+2, &size, NULL);
}

void plog(const char *format, ...)
{
	va_list args;

	va_start (args, format);
	plog_v(format, args);
	va_end (args);
}

// Query the parent app for data
int request_data(unsigned char req, void *buffer, int size)
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

	if (ReadFile(pipe, buffer, count, &rd_count, &overlapped)) {
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
	WriteFile(pipe, &req, 1, &r, NULL);

	// Wait for the response
	r = WaitForSingleObject(overlapped.hEvent, REQUEST_TIMEOUT);
	if ( (r == WAIT_OBJECT_0) && (GetOverlappedResult(pipe, &overlapped, &rd_count, FALSE)) ) {
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

// Query aprent app for device ID
char* req_device_id(void)
{
	int size;
	static char device_id[MAX_PATH_LENGTH];

	memset(device_id, 0, MAX_PATH_LENGTH);
	size = request_data(IC_GET_DEVICE_ID, (void*)device_id, sizeof(device_id));
	if (size > 0) {
		plog("got device_id: %s", device_id);
		return device_id;
	}

	plog("failed to read device_id");
	return NULL;
}

// Setup a DifXAPI Log
void __cdecl log_callback(DIFXAPI_LOG Event, DWORD Error, const TCHAR * pEventDescription, PVOID CallbackContext)
{
	if (Error == 0){
		plog("(%u) %s", Event, pEventDescription);
	} else {
		plog("(%u) Error:%u - %s", Event, Error, pEventDescription);
	}
}

// Force re-enumeration of a device (force installation)
// TODO: allow root re-enum
int update_driver(char* device_id)
{
	DEVINST     dev_inst;
	CONFIGRET   status;

	plog("updating driver node %s...", device_id);
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

	plog("final installation succeeded...");
	return 0;
}


// TODO: allow commandline options
int main(int argc, char** argv)
{
	DWORD r;
	char* device_id;
	BOOL reboot_needed = FALSE;
	char path[MAX_PATH_LENGTH];
	char log[MAX_PATH_LENGTH];
	FILE *fd;
	OSVERSIONINFO os_version;
	DWORD legacy_flag;

	// Connect to the messaging pipe
	pipe = CreateFile("\\\\.\\pipe\\libusb-installer", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OVERLAPPED, NULL);
	if (pipe == INVALID_HANDLE_VALUE) {
		printf("could not open pipe for writing: errcode %d\n", (int)GetLastError());
		return -1;
	}

	if (init_dlls()) {
		plog("could not access dfmgr32 DLL");
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

	// TODO: use GetFullPathName() to get full inf path
	_getcwd(path, MAX_PATH_LENGTH);
	safe_strcat(path, MAX_PATH_LENGTH, "\\");
	safe_strcat(path, MAX_PATH_LENGTH, INF_NAME);

	device_id = req_device_id();

	// using DRIVER_PACKAGE_LEGACY_MODE generates a warning on Vista and later
	memset(&os_version, 0, sizeof(OSVERSIONINFO));
	os_version.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	if ( (GetVersionEx(&os_version) != 0)
	  && (os_version.dwPlatformId == VER_PLATFORM_WIN32_NT) ) {
		legacy_flag = (os_version.dwMajorVersion >= 6)?0:DRIVER_PACKAGE_LEGACY_MODE;
	}

	plog("Installing driver - please wait...");
	DIFXAPISetLogCallback(log_callback, NULL);
	// TODO: set app dependency?
	r = DriverPackageInstall(path, legacy_flag|DRIVER_PACKAGE_REPAIR|DRIVER_PACKAGE_FORCE,
		NULL, &reboot_needed);
	DIFXAPISetLogCallback(NULL, NULL);
	// Will fail if inf not signed, unless DRIVER_PACKAGE_LEGACY_MODE is specified.
	// r = 87 ERROR_INVALID_PARAMETER on path == NULL
	// r = 2 ERROR_FILE_NOT_FOUND => failed to open inf
	// r = 5 ERROR_ACCESS_DENIED if needs admin elevation
	// r = 0xD ERROR_INVALID_DATA => inf is missing some data
	// r = 0xE0000003 ERROR_GENERAL_SYNTAX the syntax of the inf is invalid or the inf is empty
	// r = 0xE0000304 ERROR_INVALID_CATALOG_DATA => no cat
	// r = 0xE000023F ERROR_NO_AUTHENTICODE_CATALOG => user cancelled on warnings
	// r = 0xE0000247 if user decided not to install on warnings
	// r = 0x800B0100 ERROR_WRONG_INF_STYLE => missing cat entry in inf
	// r = 0xB7 => missing DRIVER_PACKAGE_REPAIR flag
	switch(r) {
	case 0:
		plog("  completed");
		plog("reboot %s needed", reboot_needed?"":"not");
		break;
	case ERROR_NO_MORE_ITEMS:
		plog("more recent driver was found (DRIVER_PACKAGE_FORCE option required)");
		goto out;
	case ERROR_NO_SUCH_DEVINST:
		plog("device not detected (DRIVER_PACKAGE_ONLY_IF_DEVICE_PRESENT needs to be disabled)");
		goto out;
	case ERROR_INVALID_PARAMETER:
		plog("invalid path");
		goto out;
	case ERROR_FILE_NOT_FOUND:
		plog("failed to open %s", path);
		goto out;
	case ERROR_ACCESS_DENIED:
		plog("this process needs to be run with administrative privileges");
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
		plog("cancelled by user");
		goto out;
	// TODO: make DRIVER_PACKAGE_REPAIR optional
	case ERROR_ALREADY_EXISTS:
		plog("driver already exists");
		goto out;
	default:
		plog("unhandled error %X", r);
		goto out;
	}

	update_driver(device_id);

out:
	CloseHandle(pipe);
	return 0;
}