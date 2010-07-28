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
#include <process.h>
#include <sddl.h>
#if defined(_MSC_VER)
#include <newdev.h>
#else
#include <ddk/newdev.h>
#endif
#include "installer.h"
#include "libwdi.h"
#include "msapi_utf8.h"

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
DLL_DECLARE(WINAPI, CONFIGRET, CM_Get_DevNode_Status, (PULONG, PULONG, DEVINST, ULONG));

/*
 * Globals
 */
HANDLE pipe_handle = INVALID_HANDLE_VALUE;
HANDLE syslog_ready_event = INVALID_HANDLE_VALUE;
HANDLE syslog_terminate_event = INVALID_HANDLE_VALUE;
PSID user_psid = NULL;

// Setup the Cfgmgr32 DLLs
static int init_dlls(void)
{
	DLL_LOAD(Cfgmgr32.dll, CM_Locate_DevNode, TRUE);
	DLL_LOAD(Cfgmgr32.dll, CM_Reenumerate_DevNode, TRUE);
	DLL_LOAD(Cfgmgr32.dll, CM_Get_DevNode_Status, TRUE);
	return 0;
}

// Log data with parent app through the pipe
void plog_v(const char *format, va_list args)
{
	char buffer[STR_BUFFER_SIZE];
	DWORD junk;
	int size;

	if (pipe_handle == INVALID_HANDLE_VALUE)
		return;

	buffer[0] = IC_PRINT_MESSAGE;

	size = safe_vsnprintf(buffer+1, STR_BUFFER_SIZE-1, format, args);
	if (size < 0) {
		buffer[STR_BUFFER_SIZE-1] = 0;
		size = STR_BUFFER_SIZE-2;
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

// Post a WDI status code
void pstat(int status)
{
	char data[2];
	DWORD junk;

	data[0] = IC_SET_STATUS;
	data[1] = (char)status;
	WriteFile(pipe_handle, data, 2, &junk, NULL);
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
	char* id_text[3] = {"device_id", "hardware_id", "user_sid"};
	static char device_id[MAX_PATH_LENGTH];
	static char hardware_id[MAX_PATH_LENGTH];
	static char user_sid[MAX_PATH_LENGTH];
	char* id = NULL;

	switch(id_code) {
	case IC_GET_DEVICE_ID:
		id = device_id;
		break;
	case IC_GET_HARDWARE_ID:
		id = hardware_id;
		break;
	case IC_GET_USER_SID:
		id = user_sid;
		break;
	default:
		plog("req_id: unknown ID requested");
		return NULL;
	}

	memset(id, 0, MAX_PATH_LENGTH);
	size = request_data(id_code, (void*)id, MAX_PATH_LENGTH);
	if (size > 0) {
		plog("got %s: %s", id_text[id_code-IC_GET_DEVICE_ID], id);
		return id;
	}

	plog("failed to read %s", id_text[id_code-IC_GET_DEVICE_ID]);
	return NULL;
}

/*
 * Flag phantom/removed devices for reinstallation. See:
 * http://msdn.microsoft.com/en-us/library/aa906206.aspx
 */
void check_removed(char* device_hardware_id)
{
	unsigned i, removed = 0;
	DWORD size, reg_type, config_flags;
	ULONG status, pbm_number;
	HDEVINFO dev_info;
	SP_DEVINFO_DATA dev_info_data;
	char hardware_id[STR_BUFFER_SIZE];

	// List all known USB devices (including non present ones)
	dev_info = SetupDiGetClassDevsA(NULL, "USB", NULL, DIGCF_ALLCLASSES);
	if (dev_info == INVALID_HANDLE_VALUE) {
		return;
	}

	// Find the ones that are driverless
	for (i = 0; ; i++)
	{
		dev_info_data.cbSize = sizeof(dev_info_data);
		if (!SetupDiEnumDeviceInfo(dev_info, i, &dev_info_data)) {
			break;
		}

		// Find the hardware ID
		if (!SetupDiGetDeviceRegistryPropertyA(dev_info, &dev_info_data, SPDRP_HARDWAREID,
			&reg_type, (BYTE*)hardware_id, STR_BUFFER_SIZE, &size)) {
			continue;
		}

		// Match?
		if (safe_strncmp(hardware_id, device_hardware_id, STR_BUFFER_SIZE) != 0) {
			continue;
		}

		// Unplugged?
		if (CM_Get_DevNode_Status(&status, &pbm_number, dev_info_data.DevInst, 0) != CR_NO_SUCH_DEVNODE) {
			continue;
		}

		// Flag for reinstall on next plugin
		if (!SetupDiGetDeviceRegistryPropertyA(dev_info, &dev_info_data, SPDRP_CONFIGFLAGS,
			&reg_type, (BYTE*)&config_flags, sizeof(DWORD), &size)) {
			plog("could not read SPDRP_CONFIGFLAGS for phantom device %s", hardware_id);
			continue;
		}
		config_flags |= CONFIGFLAG_REINSTALL;
		if (!SetupDiSetDeviceRegistryPropertyA(dev_info, &dev_info_data, SPDRP_CONFIGFLAGS,
			(BYTE*)&config_flags, sizeof(DWORD))) {
			plog("could not write SPDRP_CONFIGFLAGS for phantom device %s", hardware_id);
			continue;
		}
		removed++;
	}

	if (removed) {
		plog("flagged %d removed devices for reinstallation", removed);
	}
}

/*
 * Send individual lines of the syslog section pointed by buffer back to the main application
 * xbuffer's payload MUST start at byte 1 to accomodate the SYSLOG_MESSAGE prefix
 */
DWORD process_syslog(char* xbuffer, DWORD size)
{
	DWORD i, write_size, junk, start = 0;
	char* buffer;
	char* ins_string = "<ins>";

	if (xbuffer == NULL) return 0;
	// xbuffer has an extra 1 byte at the beginning
	buffer = xbuffer+1;

	// CR/LF breakdown
	for (i=0; i<size; i++) {
		if ((buffer[i] == 0x0D) || (buffer[i] == 0x0A)) {
			write_size = i-start + 2;	// extra preceding byte + 0 terminator => +2
			do {
				buffer[i++] = 0;
			} while ( ((buffer[i] == 0x0D) || (buffer[i] == 0x0A)) && (i <= size) );

			// The setupapi.dev.log uses a dubious method to mark its current position
			// If there's any "<ins>" line in any log file, it's game over then
			if (safe_strcmp(ins_string, buffer + start) == 0) {
				return start;
			}

			// This is where we use the extra start byte
			xbuffer[start] = IC_SYSLOG_MESSAGE;
			WriteFile(pipe_handle, &xbuffer[start], write_size, &junk, NULL);
			start = i;
		}
	}
	// start does not necessarily equate size, if there are truncated lines at the end
	return start;
}

/*
 * Read from the driver installation syslog in real-time
 */
void __cdecl syslog_reader_thread(void* param)
{
#define NB_SYSLOGS 3
	char* syslog_name[NB_SYSLOGS] = { "\\inf\\setupapi.dev.log", "\\setupapi.log", "\\setupact.log" };
	HANDLE log_handle;
	DWORD last_offset, size, read_size, processed_size;
	char *buffer = NULL;
	char log_path[MAX_PATH_LENGTH];
	DWORD duration = 0;
	int i;

	// Try the various driver installation logs
	for (i=0; i<NB_SYSLOGS; i++) {
		safe_strcpy(log_path, MAX_PATH_LENGTH, getenv("WINDIR"));	// Use %WINDIR% env variable
		safe_strcat(log_path, MAX_PATH_LENGTH, syslog_name[i]);
		log_handle = CreateFileA(log_path, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE,
			NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

		if (log_handle != INVALID_HANDLE_VALUE) {
			plog("using syslog '%s'", log_path);
			break;
		}
	}
	if (i == NB_SYSLOGS) {
		plog("Could not open any syslog");
		goto out;
	}

	// We assert that the log file is never gonna be bigger than 2 GB
	// TODO: special case of setupapi.dev.log's last offset not being the end (v2)
	last_offset = SetFilePointer(log_handle, 0, NULL, FILE_END);
	if (last_offset == INVALID_SET_FILE_POINTER) {
		plog("Could not set syslog offset");
		goto out;
	}

	plog("sylog reader thread started");
	SetEvent(syslog_ready_event);
	processed_size = 0;

	while(WaitForSingleObject(syslog_terminate_event, 0) != WAIT_OBJECT_0) {
		// Find out if file size has increased since last time
		size = GetFileSize(log_handle, NULL);
		if (size == INVALID_FILE_SIZE) {
			plog("could not read syslog file size");
			goto out;
		}
		size -= last_offset;

		if (size != 0) {

			// Read from file and add a zero terminator
			buffer = malloc(size+2);
			if (buffer == NULL) {
				plog("could not alloc buffer to read syslog");
				goto out;
			}
			// Keep an extra spare byte at the beginning
			if (!ReadFile(log_handle, buffer+1, size, &read_size, NULL)) {
				plog("failed to read syslog");
				goto out;
			}
			buffer[read_size+1] = 0;

			// Send all the complete lines through the pipe
			processed_size = process_syslog(buffer, read_size);
			safe_free(buffer);
			last_offset += processed_size;

			// Reposition at start of last line if needed
			if (processed_size != read_size) {
				last_offset = SetFilePointer(log_handle, processed_size-read_size, NULL, FILE_CURRENT);
				if (last_offset == INVALID_SET_FILE_POINTER) {
					plog("Could not set syslog offset");
					goto out;
				}
			}

			// Reset adaptive sleep duration if we did send data out
			if (processed_size !=0) {
				duration = 0;
			}
		}

		// Compute adaptive sleep duration
		if (((size == 0) || (processed_size == 0)) && (duration < 500)) {
			duration += 100;	// read log more frequently on recent update
		}
		Sleep(duration);
	}

out:
	plog("syslog reader thread terminating");
	safe_free(buffer);
	CloseHandle(log_handle);
	_endthread();
}

/*
 * Convert various installation errors to their WDI counterpart
 */
static __inline int process_error(DWORD r, char* path) {
	// Will fail if inf not signed, unless DRIVER_PACKAGE_LEGACY_MODE is specified.
	// r = 87 ERROR_INVALID_PARAMETER on path == NULL or hardware_id empty string
	// r = 2 ERROR_FILE_NOT_FOUND => failed to open inf
	// r = 5 ERROR_ACCESS_DENIED if needs admin elevation
	// r = 0xD ERROR_INVALID_DATA => inf is missing some data
	// r = 0xE0000003 ERROR_GENERAL_SYNTAX the syntax of the inf is invalid or the inf is empty
	// r = 0xE0000217 ERROR_BAD_SERVICE_INSTALLSECT happens if referencing a non present sys in svc section
	// r = 0xE0000304 ERROR_INVALID_CATALOG_DATA => no cat
	// r = 0xE000023F ERROR_NO_AUTHENTICODE_CATALOG => user cancelled on warnings
	// r = 0xE0000235 ERROR_IN_WOW64 => trying to run a 32 bit installer on a 64 bit machine
	// r = 0xE0000247 ERROR_DRIVER_STORE_ADD_FAILED if user decided not to install on warnings
	// r = 0xE0000203 ERROR_NO_DRIVER_SELECTED if the driver provided is not compatible with the target platform
	// r = 0x800B0100 ERROR_WRONG_INF_STYLE => missing cat entry in inf
	// r = 0xE000022F ERROR_NO_CATALOG_FOR_OEM_INF => "reject unsigned driver" policy is enforced
	// r = 0xB7 => missing DRIVER_PACKAGE_REPAIR flag
	switch(r) {
	case ERROR_NO_MORE_ITEMS:
		plog("more recent driver was found (force option required)");
		return WDI_ERROR_EXISTS;
	case ERROR_NO_SUCH_DEVINST:
		plog("device not detected (copying driver files for next time device is plugged in)");
		return WDI_SUCCESS;
	case ERROR_INVALID_PARAMETER:
		plog("invalid path or hardware ID");
		return WDI_ERROR_INVALID_PARAM;
	case ERROR_FILE_NOT_FOUND:
		plog("the system can not find the file specified");
		return WDI_ERROR_NOT_FOUND;
	case ERROR_ACCESS_DENIED:
		plog("this process needs to be run with administrative privileges");
		return WDI_ERROR_NEEDS_ADMIN;
	case ERROR_IN_WOW64:
		plog("attempted to use a 32 bit installer on a 64 bit machine");
		return WDI_ERROR_WOW64;
	case ERROR_INVALID_DATA:
	case ERROR_WRONG_INF_STYLE:
	case ERROR_GENERAL_SYNTAX:
		plog("the syntax of the inf is invalid");
		return WDI_ERROR_INF_SYNTAX;
	case ERROR_BAD_SERVICE_INSTALLSECT:
		plog("a section of the inf has a problem");
		return WDI_ERROR_INF_SYNTAX;
	case ERROR_INVALID_CATALOG_DATA:
		plog("unable to locate cat file");
		return WDI_ERROR_CAT_MISSING;
	case ERROR_NO_AUTHENTICODE_CATALOG:
	case ERROR_DRIVER_STORE_ADD_FAILED:
		plog("operation cancelled by the user");
		return WDI_ERROR_USER_CANCEL;
	case ERROR_NO_DRIVER_SELECTED:
		plog("the driver is not compatible with this version of Windows");
		return WDI_ERROR_NOT_SUPPORTED;
	case ERROR_ALREADY_EXISTS:
		plog("driver already exists");
		return WDI_ERROR_EXISTS;
	case ERROR_NO_CATALOG_FOR_OEM_INF:
		plog("your system policy has been modified from Windows defaults, and");
		plog("is set to reject unsigned drivers. You must revert the driver");
		plog("installation policy to default if you want to install this driver.");
		plog("see http://articles.techrepublic.com.com/5100-10878_11-5875443.html");
		return WDI_ERROR_UNSIGNED;
	default:
		plog("unhandled error %X", r);
		return WDI_ERROR_OTHER;
	}
}

// TODO: allow commandline options (v2)
// TODO: remove existing infs for similar devices (v2)
int
#ifdef _MSC_VER
__cdecl
#endif
main(int argc, char** argv)
{
	DWORD r;
	int ret;
	BOOL b;
	char* hardware_id = NULL;
	char* device_id = NULL;
	char* user_sid = NULL;
	char* inf_name = NULL;
	char path[MAX_PATH_LENGTH];
	char destname[MAX_PATH_LENGTH];
	uintptr_t syslog_reader_thid = -1L;

	// Connect to the messaging pipe
	pipe_handle = CreateFileA(INSTALLER_PIPE_NAME, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OVERLAPPED, NULL);
	if (pipe_handle == INVALID_HANDLE_VALUE) {
		printf("could not open pipe for writing: errcode %d\n", (int)GetLastError());
		return WDI_ERROR_RESOURCE;
	}

	if (init_dlls()) {
		plog("could not init DLLs");
		ret = WDI_ERROR_RESOURCE;
		goto out;
	}

	if (argc < 2) {
		printf("usage: %s <inf_name>\n", argv[0]);
		plog("missing inf_name parameter");
	}

	inf_name = argv[1];
	plog("got parameter %s", argv[1]);
	r = GetFullPathNameU(".", MAX_PATH_LENGTH, path, NULL);
	if ((r == 0) || (r > MAX_PATH_LENGTH)) {
		plog("could not retrieve absolute path of working directory");
		ret = WDI_ERROR_ACCESS;
		goto out;
	}
	safe_strcat(path, MAX_PATH_LENGTH, "\\");
	safe_strcat(path, MAX_PATH_LENGTH, inf_name);

	device_id = req_id(IC_GET_DEVICE_ID);
	hardware_id = req_id(IC_GET_HARDWARE_ID);
	// Will be used if we ever need to create a file, as the original user, from this app
	user_sid = req_id(IC_GET_USER_SID);
	ConvertStringSidToSidA(user_sid, &user_psid);

	// Setup the syslog reader thread
	syslog_ready_event = CreateEvent(NULL, TRUE, FALSE, NULL);
	syslog_terminate_event = CreateEvent(NULL, TRUE, FALSE, NULL);
	syslog_reader_thid = _beginthread(syslog_reader_thread, 0, 0);
	if ( (syslog_reader_thid == -1L)
	  || (WaitForSingleObject(syslog_ready_event, 2000) != WAIT_OBJECT_0) )	{
		plog("Unable to create syslog reader thread");
		SetEvent(syslog_terminate_event);
		// NB: if you try to close the syslog reader thread handle, you get a
		// "more recent driver was found" error from UpdateForPnP. Weird...
	}

	// Find if the device is plugged in
	send_status(IC_SET_TIMEOUT_INFINITE);
	if ((hardware_id != NULL) && (hardware_id[0] != 0)) {
		plog("Installing driver for %s - please wait...", hardware_id);
		b = UpdateDriverForPlugAndPlayDevicesU(NULL, hardware_id, path, INSTALLFLAG_FORCE, NULL);
		send_status(IC_SET_TIMEOUT_DEFAULT);
		if (b == true) {
			// Success
			plog("driver update completed");
			ret = WDI_SUCCESS;
			goto out;
		}

		ret = process_error(GetLastError(), path);
		if (ret != WDI_SUCCESS) {
			goto out;
		}
	}

	// TODO: try URL for OEMSourceMediaLocation (v2)
	plog("Copying inf file (for the next time device is plugged) - please wait...");
	send_status(IC_SET_TIMEOUT_INFINITE);
	b = SetupCopyOEMInfU(path, NULL, SPOST_PATH, 0, destname, MAX_PATH_LENGTH, NULL, NULL);
	send_status(IC_SET_TIMEOUT_DEFAULT);
	if (b) {
		plog("copied inf to %s", destname);
		ret = WDI_SUCCESS;
		goto out;
	}

	ret = process_error(GetLastError(), path);
	if (ret != WDI_SUCCESS) {
		goto out;
	}

	// If needed, flag removed devices for reinstallation. see:
	// http://msdn.microsoft.com/en-us/library/aa906206.aspx
	check_removed(hardware_id);

out:
	// Report any error status code and wait for target app to read it
	send_status(IC_INSTALLER_COMPLETED);
	pstat(ret);
	// TODO: have libwi send an ACK?
	Sleep(1000);
	SetEvent(syslog_terminate_event);
	CloseHandle(syslog_ready_event);
	CloseHandle(syslog_terminate_event);
	CloseHandle((HANDLE)syslog_reader_thid);
	CloseHandle(pipe_handle);
	return ret;
}
