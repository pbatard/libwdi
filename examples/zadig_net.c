/*
 * Zadig: Automated Driver Installer for USB devices (GUI version)
 * Networking functionality (web file download, check for update, etc.)
 * Copyright © 2012-2023 Pete Batard <pete@akeo.ie>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* Memory leaks detection - define _CRTDBG_MAP_ALLOC as preprocessor macro */
#ifdef _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#include <windows.h>
#include <wininet.h>
#include <netlistmgr.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <inttypes.h>

#include "msapi_utf8.h"
#include "stdfn.h"
#include "zadig.h"
#include "zadig_registry.h"
#include "zadig_resource.h"

#if defined(__MINGW32__)
#define INetworkListManager_get_IsConnectedToInternet INetworkListManager_IsConnectedToInternet
#endif

/* Maximum download chunk size, in bytes */
#define DOWNLOAD_BUFFER_SIZE    10240
/* Default delay between update checks (1 day) */
#define DEFAULT_UPDATE_INTERVAL (24 * 3600)

DWORD error_code;
APPLICATION_UPDATE update = { { 0,0,0,0 }, { 0,0 }, NULL, NULL };

static BOOL force_update = FALSE;
static BOOL force_update_check = FALSE;
static BOOL update_check_in_progress = FALSE;

/* MinGW is missing some of those */
#if !defined(ERROR_INTERNET_DISCONNECTED)
#define ERROR_INTERNET_DISCONNECTED (INTERNET_ERROR_BASE + 163)
#endif
#if !defined(ERROR_INTERNET_SERVER_UNREACHABLE)
#define ERROR_INTERNET_SERVER_UNREACHABLE (INTERNET_ERROR_BASE + 164)
#endif
#if !defined(ERROR_INTERNET_PROXY_SERVER_UNREACHABLE)
#define ERROR_INTERNET_PROXY_SERVER_UNREACHABLE (INTERNET_ERROR_BASE + 165)
#endif
#if !defined(ERROR_INTERNET_BAD_AUTO_PROXY_SCRIPT)
#define ERROR_INTERNET_BAD_AUTO_PROXY_SCRIPT (INTERNET_ERROR_BASE + 166)
#endif
#if !defined(ERROR_INTERNET_UNABLE_TO_DOWNLOAD_SCRIPT)
#define ERROR_INTERNET_UNABLE_TO_DOWNLOAD_SCRIPT (INTERNET_ERROR_BASE + 167)
#endif
#if !defined(ERROR_INTERNET_FAILED_DUETOSECURITYCHECK)
#define ERROR_INTERNET_FAILED_DUETOSECURITYCHECK (INTERNET_ERROR_BASE + 171)
#endif
#if !defined(ERROR_INTERNET_NOT_INITIALIZED)
#define ERROR_INTERNET_NOT_INITIALIZED (INTERNET_ERROR_BASE + 172)
#endif
#if !defined(ERROR_INTERNET_NEED_MSN_SSPI_PKG)
#define ERROR_INTERNET_NEED_MSN_SSPI_PKG (INTERNET_ERROR_BASE + 173)
#endif
#if !defined(ERROR_INTERNET_LOGIN_FAILURE_DISPLAY_ENTITY_BODY)
#define ERROR_INTERNET_LOGIN_FAILURE_DISPLAY_ENTITY_BODY (INTERNET_ERROR_BASE + 174)
#endif

/*
 * FormatMessage does not handle internet errors
 * https://docs.microsoft.com/en-us/windows/desktop/wininet/wininet-errors
 */
const char* WinInetErrorString(void)
{
	static char error_string[256];
	DWORD size = sizeof(error_string);

	error_code = HRESULT_CODE(GetLastError());

	if ((error_code < INTERNET_ERROR_BASE) || (error_code > INTERNET_ERROR_LAST))
		return WindowsErrorString();

	switch(error_code) {
	case ERROR_INTERNET_OUT_OF_HANDLES:
		return "No more handles could be generated at this time.";
	case ERROR_INTERNET_TIMEOUT:
		return "The request has timed out.";
	case ERROR_INTERNET_INTERNAL_ERROR:
		return "An internal error has occurred.";
	case ERROR_INTERNET_INVALID_URL:
		return "The URL is invalid.";
	case ERROR_INTERNET_UNRECOGNIZED_SCHEME:
		return "The URL scheme could not be recognized or is not supported.";
	case ERROR_INTERNET_NAME_NOT_RESOLVED:
		return "The server name could not be resolved.";
	case ERROR_INTERNET_PROTOCOL_NOT_FOUND:
		return "The requested protocol could not be located.";
	case ERROR_INTERNET_INVALID_OPTION:
		return "A request specified an invalid option value.";
	case ERROR_INTERNET_BAD_OPTION_LENGTH:
		return "The length of an option supplied is incorrect for the type of option specified.";
	case ERROR_INTERNET_OPTION_NOT_SETTABLE:
		return "The request option cannot be set, only queried.";
	case ERROR_INTERNET_SHUTDOWN:
		return "The Win32 Internet function support is being shut down or unloaded.";
	case ERROR_INTERNET_INCORRECT_USER_NAME:
		return "The request to connect and log on to an FTP server could not be completed because the supplied user name is incorrect.";
	case ERROR_INTERNET_INCORRECT_PASSWORD:
		return "The request to connect and log on to an FTP server could not be completed because the supplied password is incorrect.";
	case ERROR_INTERNET_LOGIN_FAILURE:
		return "The request to connect to and log on to an FTP server failed.";
	case ERROR_INTERNET_INVALID_OPERATION:
		return "The requested operation is invalid.";
	case ERROR_INTERNET_OPERATION_CANCELLED:
		return "The operation was cancelled, usually because the handle on which the request was operating was closed before the operation completed.";
	case ERROR_INTERNET_INCORRECT_HANDLE_TYPE:
		return "The type of handle supplied is incorrect for this operation.";
	case ERROR_INTERNET_INCORRECT_HANDLE_STATE:
		return "The requested operation cannot be carried out because the handle supplied is not in the correct state.";
	case ERROR_INTERNET_NOT_PROXY_REQUEST:
		return "The request cannot be made via a proxy.";
	case ERROR_INTERNET_REGISTRY_VALUE_NOT_FOUND:
		return "A required registry value could not be located.";
	case ERROR_INTERNET_BAD_REGISTRY_PARAMETER:
		return "A required registry value was located but is an incorrect type or has an invalid value.";
	case ERROR_INTERNET_NO_DIRECT_ACCESS:
		return "Direct network access cannot be made at this time.";
	case ERROR_INTERNET_NO_CONTEXT:
		return "An asynchronous request could not be made because a zero context value was supplied.";
	case ERROR_INTERNET_NO_CALLBACK:
		return "An asynchronous request could not be made because a callback function has not been set.";
	case ERROR_INTERNET_REQUEST_PENDING:
		return "The required operation could not be completed because one or more requests are pending.";
	case ERROR_INTERNET_INCORRECT_FORMAT:
		return "The format of the request is invalid.";
	case ERROR_INTERNET_ITEM_NOT_FOUND:
		return "The requested item could not be located.";
	case ERROR_INTERNET_CANNOT_CONNECT:
		return "The attempt to connect to the server failed.";
	case ERROR_INTERNET_CONNECTION_ABORTED:
		return "The connection with the server has been terminated.";
	case ERROR_INTERNET_CONNECTION_RESET:
		return "The connection with the server has been reset.";
	case ERROR_INTERNET_FORCE_RETRY:
		return "Calls for the Win32 Internet function to redo the request.";
	case ERROR_INTERNET_INVALID_PROXY_REQUEST:
		return "The request to the proxy was invalid.";
	case ERROR_INTERNET_HANDLE_EXISTS:
		return "The request failed because the handle already exists.";
	case ERROR_INTERNET_SEC_INVALID_CERT:
		return "The SSL certificate is invalid.";
	case ERROR_INTERNET_SEC_CERT_DATE_INVALID:
		return "SSL certificate date that was received from the server is bad. The certificate is expired.";
	case ERROR_INTERNET_SEC_CERT_CN_INVALID:
		return "SSL certificate common name (host name field) is incorrect.";
	case ERROR_INTERNET_SEC_CERT_ERRORS:
		return "The SSL certificate contains errors.";
	case ERROR_INTERNET_SEC_CERT_NO_REV:
		return "The SSL certificate was not revoked.";
	case ERROR_INTERNET_SEC_CERT_REV_FAILED:
		return "The revocation check of the SSL certificate failed.";
	case ERROR_INTERNET_HTTP_TO_HTTPS_ON_REDIR:
		return "The application is moving from a non-SSL to an SSL connection because of a redirect.";
	case ERROR_INTERNET_HTTPS_TO_HTTP_ON_REDIR:
		return "The application is moving from an SSL to an non-SSL connection because of a redirect.";
	case ERROR_INTERNET_MIXED_SECURITY:
		return "Some of the content being viewed may have come from unsecured servers.";
	case ERROR_INTERNET_CHG_POST_IS_NON_SECURE:
		return "The application is posting and attempting to change multiple lines of text on a server that is not secure.";
	case ERROR_INTERNET_POST_IS_NON_SECURE:
		return "The application is posting data to a server that is not secure.";
	case ERROR_FTP_TRANSFER_IN_PROGRESS:
		return "The requested operation cannot be made on the FTP session handle because an operation is already in progress.";
	case ERROR_FTP_DROPPED:
		return "The FTP operation was not completed because the session was aborted.";
	case ERROR_GOPHER_PROTOCOL_ERROR:
	case ERROR_GOPHER_NOT_FILE:
	case ERROR_GOPHER_DATA_ERROR:
	case ERROR_GOPHER_END_OF_DATA:
	case ERROR_GOPHER_INVALID_LOCATOR:
	case ERROR_GOPHER_INCORRECT_LOCATOR_TYPE:
	case ERROR_GOPHER_NOT_GOPHER_PLUS:
	case ERROR_GOPHER_ATTRIBUTE_NOT_FOUND:
	case ERROR_GOPHER_UNKNOWN_LOCATOR:
		return "Gopher? Really??? What is this, 1994?";
	case ERROR_HTTP_HEADER_NOT_FOUND:
		return "The requested header could not be located.";
	case ERROR_HTTP_DOWNLEVEL_SERVER:
		return "The server did not return any headers.";
	case ERROR_HTTP_INVALID_SERVER_RESPONSE:
		return "The server response could not be parsed.";
	case ERROR_HTTP_INVALID_HEADER:
		return "The supplied header is invalid.";
	case ERROR_HTTP_INVALID_QUERY_REQUEST:
		return "The request made to HttpQueryInfo is invalid.";
	case ERROR_HTTP_HEADER_ALREADY_EXISTS:
		return "The header could not be added because it already exists.";
	case ERROR_HTTP_REDIRECT_FAILED:
		return "The redirection failed because either the scheme changed or all attempts made to redirect failed.";
	case ERROR_INTERNET_SECURITY_CHANNEL_ERROR:
		return "This system's SSL library is too old to be able to access this website.";
	case ERROR_INTERNET_CLIENT_AUTH_CERT_NEEDED:
		return "Client Authentication certificate needed";
	case ERROR_INTERNET_BAD_AUTO_PROXY_SCRIPT:
		return "Bad auto proxy script.";
	case ERROR_INTERNET_UNABLE_TO_DOWNLOAD_SCRIPT:
		return "Unable to download script.";
	case ERROR_INTERNET_NOT_INITIALIZED:
		return "Internet has not be initialized.";
	case ERROR_INTERNET_UNABLE_TO_CACHE_FILE:
		return "Unable to cache the file.";
	case ERROR_INTERNET_TCPIP_NOT_INSTALLED:
		return "TPC/IP not installed.";
	case ERROR_INTERNET_DISCONNECTED:
		return "Internet is disconnected.";
	case ERROR_INTERNET_SERVER_UNREACHABLE:
		return "Server could not be reached.";
	case ERROR_INTERNET_PROXY_SERVER_UNREACHABLE:
		return "Proxy server could not be reached.";
	case ERROR_INTERNET_FAILED_DUETOSECURITYCHECK:
		return "A security check prevented internet connection.";
	case ERROR_INTERNET_NEED_MSN_SSPI_PKG:
		return "This connection requires an MSN Security Support Provider Interface package.";
	case ERROR_INTERNET_LOGIN_FAILURE_DISPLAY_ENTITY_BODY:
		return "Please ask Microsoft about that one!";
	case ERROR_INTERNET_EXTENDED_ERROR:
		InternetGetLastResponseInfoA(&error_code, error_string, &size);
		return error_string;
	default:
		static_sprintf(error_string, "Unknown internet error 0x%08lX", error_code);
		return error_string;
	}
}

/*
 * Open an Internet session
 */
static HINTERNET GetInternetSession(int num_retries)
{
	int i;
	char agent[64];
	BOOL decodingSupport = TRUE;
	VARIANT_BOOL InternetConnection = VARIANT_FALSE;
	DWORD dwFlags, dwTimeout = NET_SESSION_TIMEOUT, dwProtocolSupport = HTTP_PROTOCOL_FLAG_HTTP2;
	HINTERNET hSession = NULL;
	HRESULT hr = S_FALSE;
	INetworkListManager* pNetworkListManager;

	// Create a NetworkListManager Instance to check the network connection
	IGNORE_RETVAL(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
	hr = CoCreateInstance(&CLSID_NetworkListManager, NULL, CLSCTX_ALL,
		&IID_INetworkListManager, (LPVOID*)&pNetworkListManager);
	if (hr == S_OK) {
		for (i = 0; i <= num_retries; i++) {
			hr = INetworkListManager_get_IsConnectedToInternet(pNetworkListManager, &InternetConnection);
			if (hr == S_OK && InternetConnection == VARIANT_TRUE)
				break;
			// INetworkListManager may fail with ERROR_SERVICE_DEPENDENCY_FAIL if the DHCP service
			// is not running, in which case we must fall back to using InternetGetConnectedState().
			// See https://github.com/pbatard/rufus/issues/1801.
			if (hr == HRESULT_FROM_WIN32(ERROR_SERVICE_DEPENDENCY_FAIL)) {
				if (InternetGetConnectedState(&dwFlags, 0)) {
					InternetConnection = VARIANT_TRUE;
					break;
				}
			}
			Sleep(1000);
		}
	}
	if (InternetConnection == VARIANT_FALSE) {
		SetLastError(ERROR_INTERNET_DISCONNECTED);
		goto out;
	}
	static_sprintf(agent, APPLICATION_NAME "/%d.%d.%d (Windows NT %d.%d%s)",
		application_version[0], application_version[1], application_version[2],
		windows_version >> 4, windows_version & 0x0F, is_x64() ? "; WOW64" : "");
	hSession = InternetOpenA(agent, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
	// Set the timeouts
	InternetSetOptionA(hSession, INTERNET_OPTION_CONNECT_TIMEOUT, (LPVOID)&dwTimeout, sizeof(dwTimeout));
	InternetSetOptionA(hSession, INTERNET_OPTION_SEND_TIMEOUT, (LPVOID)&dwTimeout, sizeof(dwTimeout));
	InternetSetOptionA(hSession, INTERNET_OPTION_RECEIVE_TIMEOUT, (LPVOID)&dwTimeout, sizeof(dwTimeout));
	// Enable gzip and deflate decoding schemes
	InternetSetOptionA(hSession, INTERNET_OPTION_HTTP_DECODING, (LPVOID)&decodingSupport, sizeof(decodingSupport));
	// Enable HTTP/2 protocol support
	InternetSetOptionA(hSession, INTERNET_OPTION_ENABLE_HTTP_PROTOCOL, (LPVOID)&dwProtocolSupport, sizeof(dwProtocolSupport));

out:
	return hSession;
}

/*
 * Download a file from an URL
 * Mostly taken from http://support.microsoft.com/kb/234913
 * If hProgressDialog is not NULL, this function will send INIT and EXIT messages
 * to the dialog in question, with WPARAM being set to nonzero for EXIT on success
 * and also attempt to indicate progress using an IDC_PROGRESS control
 */
DWORD DownloadFile(const char* url, const char* file, HWND hProgressDialog)
{
	HWND hProgressBar = NULL;
	BOOL r = FALSE;
	LONG progress_style;
	DWORD dwSize, dwWritten, dwDownloaded, dwTotalSize;
	DWORD DownloadStatus;
	HANDLE hFile = INVALID_HANDLE_VALUE;
	const char* accept_types[] = {"*/*\0", NULL};
	unsigned char buf[DOWNLOAD_BUFFER_SIZE];
	char hostname[64], urlpath[128], msg[MAX_PATH];
	HINTERNET hSession = NULL, hConnection = NULL, hRequest = NULL;
	URL_COMPONENTSA UrlParts = {sizeof(URL_COMPONENTSA), NULL, 1, (INTERNET_SCHEME)0,
		hostname, sizeof(hostname), 0, NULL, 1, urlpath, sizeof(urlpath), NULL, 1};
	size_t last_slash;

	DownloadStatus = 404;
	if (hProgressDialog != NULL) {
		// Use the progress control provided, if any
		hProgressBar = GetDlgItem(hProgressDialog, IDC_PROGRESS);
		if (hProgressBar != NULL) {
			progress_style = GetWindowLong(hProgressBar, GWL_STYLE);
			SetWindowLong(hProgressBar, GWL_STYLE, progress_style & (~PBS_MARQUEE));
			SendMessage(hProgressBar, PBM_SETPOS, 0, 0);
		}
		SendMessage(hProgressDialog, UM_DOWNLOAD_INIT, 0, 0);
	}

	if (file == NULL)
		goto out;

	for (last_slash = safe_strlen(file); last_slash != 0; last_slash--) {
		if ((file[last_slash] == '/') || (file[last_slash] == '\\')) {
			last_slash++;
			break;
		}
	}

	static_sprintf(msg, "Downloading %s: Connecting...", file);
	print_status(0, FALSE, msg);
	dprintf("Downloading %s from %s\n", file, url);

	if ( (!InternetCrackUrlA(url, (DWORD)safe_strlen(url), 0, &UrlParts))
	  || (UrlParts.lpszHostName == NULL) || (UrlParts.lpszUrlPath == NULL)) {
		dprintf("Unable to decode URL: %s\n", WinInetErrorString());
		goto out;
	}
	hostname[sizeof(hostname)-1] = 0;

	// Open an Internet session
	hSession = GetInternetSession(5);
	if (hSession == NULL) {
		dprintf("Could not open Internet session: %s\n", WinInetErrorString());
		goto out;
	}

	hConnection = InternetConnectA(hSession, UrlParts.lpszHostName, UrlParts.nPort, NULL, NULL, INTERNET_SERVICE_HTTP, 0, (DWORD_PTR)NULL);
	if (hConnection == NULL) {
		dprintf("Could not connect to server %s:%d: %s\n", UrlParts.lpszHostName, UrlParts.nPort, WinInetErrorString());
		goto out;
	}

	hRequest = HttpOpenRequestA(hConnection, "GET", UrlParts.lpszUrlPath, NULL, NULL, accept_types,
		INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTP|INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTPS|
		INTERNET_FLAG_NO_COOKIES|INTERNET_FLAG_NO_UI|INTERNET_FLAG_NO_CACHE_WRITE|INTERNET_FLAG_HYPERLINK|
		((UrlParts.nScheme==INTERNET_SCHEME_HTTPS)?INTERNET_FLAG_SECURE:0), (DWORD_PTR)NULL);
	if (hRequest == NULL) {
		dprintf("Could not open URL %s: %s\n", url, WinInetErrorString());
		goto out;
	}

	if (!HttpSendRequestA(hRequest, NULL, 0, NULL, 0)) {
		dprintf("Unable to send request: %s\n", WinInetErrorString());
		goto out;
	}

	// Get the file size
	dwSize = sizeof(DownloadStatus);
	HttpQueryInfoA(hRequest, HTTP_QUERY_STATUS_CODE|HTTP_QUERY_FLAG_NUMBER, (LPVOID)&DownloadStatus, &dwSize, NULL);
	if (DownloadStatus != 200) {
		error_code = ERROR_SEVERITY_ERROR|ERROR_INTERNET_ITEM_NOT_FOUND;
		dprintf("Unable to access file: %d\n", DownloadStatus);
		goto out;
	}
	dwSize = sizeof(dwTotalSize);
	if (!HttpQueryInfoA(hRequest, HTTP_QUERY_CONTENT_LENGTH|HTTP_QUERY_FLAG_NUMBER, (LPVOID)&dwTotalSize, &dwSize, NULL)) {
		dprintf("Unable to retrieve file length: %s\n", WinInetErrorString());
		goto out;
	}
	dprintf("File length: %d bytes\n", dwTotalSize);

	hFile = CreateFileU(file, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		dprintf("Unable to create file '%s': %s\n", &file[last_slash], WinInetErrorString());
		goto out;
	}

	// Keep checking for data until there is nothing left.
	dwSize = 0;
	while (1) {
		if (IS_ERROR(error_code))
			goto out;

		if (!InternetReadFile(hRequest, buf, sizeof(buf), &dwDownloaded) || (dwDownloaded == 0))
			break;
		dwSize += dwDownloaded;
		SendMessage(hProgressBar, PBM_SETPOS, (WPARAM)(MAX_PROGRESS*((1.0f*dwSize)/(1.0f*dwTotalSize))), 0);
		static_sprintf(msg, "Downloading: %0.1f%%", (100.0f*dwSize)/(1.0f*dwTotalSize));
		print_status(0, FALSE, msg);
		if (!WriteFile(hFile, buf, dwDownloaded, &dwWritten, NULL)) {
			dprintf("Error writing file '%s': %s\n", &file[last_slash], WinInetErrorString());
			goto out;
		} else if (dwDownloaded != dwWritten) {
			dprintf("Error writing file '%s': Only %d/%d bytes written\n", &file[last_slash], dwWritten, dwDownloaded);
			goto out;
		}
	}

	if (dwSize != dwTotalSize) {
		dprintf("Could not download complete file - read: %d bytes, expected: %d bytes\n", dwSize, dwTotalSize);
		error_code = ERROR_SEVERITY_ERROR|ERROR_WRITE_FAULT;
		goto out;
	} else {
		r = TRUE;
		dprintf("Successfully downloaded '%s'\n", &file[last_slash]);
	}

out:
	if (hProgressDialog != NULL)
		SendMessage(hProgressDialog, UM_DOWNLOAD_EXIT, (WPARAM)r, 0);
	if (hFile != INVALID_HANDLE_VALUE) {
		// Force a flush - May help with the PKI API trying to process downloaded updates too early...
		FlushFileBuffers(hFile);
		CloseHandle(hFile);
	}
	if (!r) {
		if (file != NULL)
			_unlinkU(file);
		print_status(0, FALSE, "Failed to download file.");
		SetLastError(error_code);
		MessageBoxU(hMainDialog, WinInetErrorString(), "File download", MB_OK|MB_ICONERROR);
	}
	if (hRequest)
		InternetCloseHandle(hRequest);
	if (hConnection)
		InternetCloseHandle(hConnection);
	if (hSession)
		InternetCloseHandle(hSession);

	return r?dwSize:0;
}

/* Threaded download */
static const char *_url, *_file;
static HWND _hProgressDialog;
static DWORD WINAPI _DownloadFileThread(LPVOID param)
{
	ExitThread(DownloadFile(_url, _file, _hProgressDialog));
}

HANDLE DownloadFileThreaded(const char* url, const char* file, HWND hProgressDialog)
{
	_url = url;
	_file = file;
	_hProgressDialog = hProgressDialog;
	return CreateThread(NULL, 0, _DownloadFileThread, NULL, 0, NULL);
}

static __inline uint64_t to_uint64_t(uint16_t x[4]) {
	int i;
	uint64_t ret = 0;
	for (i = 0; i < 4; i++)
		ret = (ret << 16) + x[i];
	return ret;
}

/*
 * Background thread to check for updates
 */
static DWORD WINAPI CheckForUpdatesThread(LPVOID param)
{
	BOOL releases_only, found_new_version = FALSE;
	int status = 0;
	const char* server_url = APPLICATION_URL "/";
	int i, j, k, verbose = 0, verpos[4];
	static const char* archname[] = { "win_x86", "win_x64" };
	static const char* channel[] = { "release", "beta" };		// release channel
	const char* accept_types[] = { "*/*\0", NULL };
	DWORD dwSize, dwDownloaded, dwTotalSize, dwStatus;
	char* buf = NULL;
	char hostname[64], urlpath[128];
	OSVERSIONINFOA os_version = { sizeof(OSVERSIONINFOA), 0, 0, 0, 0, "" };
	HINTERNET hSession = NULL, hConnection = NULL, hRequest = NULL;
	URL_COMPONENTSA UrlParts = { sizeof(URL_COMPONENTSA), NULL, 1, (INTERNET_SCHEME)0,
		hostname, sizeof(hostname), 0, NULL, 1, urlpath, sizeof(urlpath), NULL, 1 };
	SYSTEMTIME ServerTime, LocalTime;
	FILETIME FileTime;
	int64_t local_time = 0, reg_time, server_time, update_interval;

	update_check_in_progress = TRUE;
	verbose = ReadRegistryKey32(REGKEY_HKCU, REGKEY_VERBOSE_UPDATES);
	// Without this the FileDialog will produce error 0x8001010E when compiled for Vista or later
	IGNORE_RETVAL(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED));
	// Unless the update was forced, wait a while before performing the update check
	if (!force_update_check) {
		// TODO: Also check on inactivity
		// It would of course be a lot nicer to use a timer and wake the thread, but my
		// development time is limited and this is FASTER to implement.
		do {
			for (i = 0; (i < 30) && (!force_update_check); i++)
				Sleep(500);
		} while ((!force_update_check) && ((installation_running || (dialog_showing>0))));
		if (!force_update_check) {
			if ((ReadRegistryKey32(REGKEY_HKCU, REGKEY_UPDATE_INTERVAL) == -1)) {
				vuprintf("Check for updates disabled, as per registry settings.\n");
				goto out;
			}
			reg_time = ReadRegistryKey64(REGKEY_HKCU, REGKEY_LAST_UPDATE);
			update_interval = (int64_t)ReadRegistryKey32(REGKEY_HKCU, REGKEY_UPDATE_INTERVAL);
			if (update_interval == 0) {
				WriteRegistryKey32(REGKEY_HKCU, REGKEY_UPDATE_INTERVAL, DEFAULT_UPDATE_INTERVAL);
				update_interval = DEFAULT_UPDATE_INTERVAL;
			}
			GetSystemTime(&LocalTime);
			if (!SystemTimeToFileTime(&LocalTime, &FileTime))
				goto out;
			local_time = ((((int64_t)FileTime.dwHighDateTime)<<32) + FileTime.dwLowDateTime) / 10000000;
			vvuprintf("Local time: %" PRId64 "\n", local_time);
			if (local_time < reg_time + update_interval) {
				vuprintf("Next update check in %" PRId64 " seconds.\n", reg_time + update_interval - local_time);
				goto out;
			}
		}
	}

	print_status(3000, TRUE, "Checking for " APPLICATION_NAME " updates...");
	status++;	// 1

	if (!GetVersionExA(&os_version)) {
		dprintf("Could not read Windows version - Check for updates cancelled.\n");
		goto out;
	}

	if (!InternetCrackUrlA(server_url, (DWORD)safe_strlen(server_url), 0, &UrlParts))
		goto out;
	hostname[sizeof(hostname) - 1] = 0;
	hSession = GetInternetSession(5);
	if (hSession == NULL)
		goto out;
	hConnection = InternetConnectA(hSession, UrlParts.lpszHostName, UrlParts.nPort, NULL, NULL, INTERNET_SERVICE_HTTP, 0, (DWORD_PTR)NULL);
	if (hConnection == NULL)
		goto out;

	status++;	// 2
	releases_only = !GetRegistryKeyBool(REGKEY_HKCU, REGKEY_INCLUDE_BETAS);

	for (k = 0; (k < (releases_only ? 1 : (int)ARRAYSIZE(channel))) && (!found_new_version); k++) {
		dprintf("Checking %s channel...\n", channel[k]);
		// At this stage we can query the server for various update version files.
		// We first try to lookup for "<appname>_<os_arch>_<os_version_major>_<os_version_minor>.ver"
		// and then remove each each of the <os_> components until we find our match. For instance, we may first
		// look for <app_name>_win_x64_6.2.ver (Win8 x64) but only get a match for <app_name>_win_x64_6.ver (Vista x64 or later)
		// This allows sunsetting OS versions (eg XP) or providing different downloads for different archs/groups.
		static_sprintf(urlpath, "%s%s%s_%s_%ld.%ld.ver", APPLICATION_NAME, (k == 0) ? "" : "_",
			(k == 0) ? "" : channel[k], archname[is_x64() ? 1 : 0], os_version.dwMajorVersion, os_version.dwMinorVersion);
		vuprintf("Base update check: %s\n", urlpath);
		for (i = 0, j = (int)safe_strlen(urlpath) - 5; (j>0) && (i<ARRAYSIZE(verpos)); j--) {
			if ((urlpath[j] == '.') || (urlpath[j] == '_')) {
				verpos[i++] = j;
			}
		}
		if (i != ARRAYSIZE(verpos)) {
			dprintf("Broken code in CheckForUpdatesThread()!\n");
			goto out;
		}

		UrlParts.lpszUrlPath = urlpath;
		UrlParts.dwUrlPathLength = sizeof(urlpath);
		for (i=0; i<ARRAYSIZE(verpos); i++) {
			vvuprintf("Trying %s\n", UrlParts.lpszUrlPath);
			hRequest = HttpOpenRequestA(hConnection, "GET", UrlParts.lpszUrlPath, NULL, NULL, accept_types,
				INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTP | INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTPS |
				INTERNET_FLAG_NO_COOKIES | INTERNET_FLAG_NO_UI | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_HYPERLINK |
				((UrlParts.nScheme == INTERNET_SCHEME_HTTPS) ? INTERNET_FLAG_SECURE : 0), (DWORD_PTR)NULL);
			if ((hRequest == NULL) || (!HttpSendRequestA(hRequest, NULL, 0, NULL, 0)))
				goto out;

			// Ensure that we get a text file
			dwSize = sizeof(dwStatus);
			dwStatus = 404;
			HttpQueryInfoA(hRequest, HTTP_QUERY_STATUS_CODE|HTTP_QUERY_FLAG_NUMBER, (LPVOID)&dwStatus, &dwSize, NULL);
			if (dwStatus == 200)
				break;
			InternetCloseHandle(hRequest);
			hRequest = NULL;
			safe_strcpy(&urlpath[verpos[i]], 5, ".ver");
		}
		if (dwStatus != 200) {
			vuprintf("Could not find a %s version file on server %s", channel[k], server_url);
			if ((releases_only) || (k + 1 >= ARRAYSIZE(channel)))
				goto out;
			continue;
		}
		vuprintf("Found match for %s on server %s", urlpath, server_url);

		// We also get a date from Apache, which we'll use to avoid out of sync check,
		// in case some set their clock way into the future and back.
		// On the other hand, if local clock is set way back in the past, we will never check.
		dwSize = sizeof(ServerTime);
		// If we can't get a date we can trust, don't bother...
		if ( (!HttpQueryInfoA(hRequest, HTTP_QUERY_DATE|HTTP_QUERY_FLAG_SYSTEMTIME, (LPVOID)&ServerTime, &dwSize, NULL))
			|| (!SystemTimeToFileTime(&ServerTime, &FileTime)) )
			goto out;
		server_time = ((((int64_t)FileTime.dwHighDateTime) << 32) + FileTime.dwLowDateTime) / 10000000;
		vvuprintf("Server time: %" PRId64 "\n", server_time);
		// Always store the server response time - the only clock we trust!
		WriteRegistryKey64(REGKEY_HKCU, REGKEY_LAST_UPDATE, server_time);
		// Might as well let the user know
		if (!force_update_check) {
			if ((local_time > server_time + 600) || (local_time < server_time - 600)) {
				dprintf("IMPORTANT: Your local clock is more than 10 minutes in the %s. Unless you fix this, "
					APPLICATION_NAME " may not be able to check for updates...",
					(local_time > server_time + 600)?"future":"past");
			}
		}

		dwSize = sizeof(dwTotalSize);
		if (!HttpQueryInfoA(hRequest, HTTP_QUERY_CONTENT_LENGTH|HTTP_QUERY_FLAG_NUMBER, (LPVOID)&dwTotalSize, &dwSize, NULL))
			goto out;

		safe_free(buf);
		// Make sure the file is NUL terminated
		buf = (char*)calloc(dwTotalSize+1, 1);
		if (buf == NULL) goto out;
		// This is a version file - we should be able to gulp it down in one go
		if (!InternetReadFile(hRequest, buf, dwTotalSize, &dwDownloaded) || (dwDownloaded != dwTotalSize))
			goto out;

		status++;
		vuprintf("Successfully downloaded version file (%d bytes)\n", dwTotalSize);

		parse_update(buf, dwTotalSize+1);

		vuprintf("UPDATE DATA:\n");
		vuprintf("  version: %d.%d.%d (%s)\n", update.version[0], update.version[1],
			update.version[2], channel[k]);
		vuprintf("  platform_min: %d.%d\n", update.platform_min[0], update.platform_min[1]);
		vuprintf("  url: %s\n", update.download_url);

		found_new_version = ((to_uint64_t(update.version) > to_uint64_t(application_version)) || (force_update))
			&& ( (os_version.dwMajorVersion > update.platform_min[0])
			  || ( (os_version.dwMajorVersion == update.platform_min[0]) && (os_version.dwMinorVersion >= update.platform_min[1])) );
		dprintf("N%sew %s version found%c\n", found_new_version?"":"o n", channel[k], found_new_version?'!':'.');
	}

out:
	safe_free(buf);
	if (hRequest)
		InternetCloseHandle(hRequest);
	if (hConnection)
		InternetCloseHandle(hConnection);
	if (hSession)
		InternetCloseHandle(hSession);
	switch(status) {
	case 1:
		print_status(3000, TRUE, "Updates: Unable to connect to the internet");
		break;
	case 2:
		print_status(3000, TRUE, "Updates: Unable to access version data");
		break;
	case 3:
	case 4:
		print_status(3000, FALSE, found_new_version?"A new version of " APPLICATION_NAME " is available!":
			"No new version of " APPLICATION_NAME " was found");
	default:
		break;
	}
	// Start the new download after cleanup
	if (found_new_version) {
		// User may have started an operation while we were checking
		while ((!force_update_check) && (installation_running || (dialog_showing > 0))) {
			Sleep(15000);
		}
		download_new_version();
	} else if (force_update_check) {
		PostMessage(hMainDialog, UM_NO_UPDATE, 0, 0);
	}
	force_update_check = FALSE;
	update_check_in_progress = FALSE;
	ExitThread(0);
}

/*
 * Initiate a check for updates. If force is true, ignore the wait period
 */
BOOL CheckForUpdates(BOOL force)
{
	force_update_check = force;
	if (update_check_in_progress)
		return FALSE;
	if (CreateThread(NULL, 0, CheckForUpdatesThread, NULL, 0, NULL) == NULL) {
		dprintf("Unable to start update check thread");
		return FALSE;
	}
	return TRUE;
}
