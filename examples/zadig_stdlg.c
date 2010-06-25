/*
 * Zadig: Automated Driver Installer for USB devices (GUI version)
 * Standard Dialog Routines (Browse for folder, About, etc)
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
#include <windowsx.h>
#include <stdlib.h>
#include <stdio.h>
#include <io.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <shlobj.h>
#include <shellapi.h>
#include <process.h>
#include <commdlg.h>

#include "resource.h"
#include "zadig.h"

#define UM_PROGRESS_START          (WM_APP)
#define UM_PROGRESS_STOP           (WM_APP+1)

#if (_WIN32_WINNT >= 0x0600)
// Available on Vista and later
static HRESULT (__stdcall *pSHCreateItemFromParsingName)(PCWSTR, IBindCtx*, REFIID, void **) = NULL;
#endif

#ifndef PBM_SETMARQUEE
#define PBM_SETMARQUEE (WM_USER+10)
#endif

// TODO: make sure this is never called in release
void NOT_IMPLEMENTED(void) {
	MessageBox(NULL, "Feature not implemented yet", "Not implemented", MB_ICONSTOP);
}

#define INIT_VISTA_SHELL32 if (pSHCreateItemFromParsingName == NULL) {								\
	pSHCreateItemFromParsingName = (HRESULT (__stdcall *)(PCWSTR, IBindCtx*, REFIID, void **))	\
			GetProcAddress(GetModuleHandle("SHELL32"), "SHCreateItemFromParsingName");			\
	}
#define IS_VISTA_SHELL32_AVAILABLE (pSHCreateItemFromParsingName != NULL)

/*
 * Globals
 */
static uintptr_t progress_thid = -1L;
static HWND hProgress = INVALID_HANDLE_VALUE;
static HICON hMessageIcon = INVALID_HANDLE_VALUE;
static char* message_text = NULL;
static char* message_title = NULL;
int (*progress_function)(void);

/*
 * Converts a WCHAR string to UTF8 (allocate returned string)
 * Returns NULL on error
 */
char* wchar_to_utf8(WCHAR* wstr)
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
 * Converts an UTF8 string to WCHAR (allocate returned string)
 * Returns NULL on error
 */
WCHAR* utf8_to_wchar(char* str)
{
	int size;
	WCHAR* wstr;

	// Find out the size we need to allocate for our converted string
	size = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
	if (size <= 1)	// An empty string would be size 1
		return NULL;

	if ((wstr = (WCHAR*) malloc(2*size)) == NULL)
		return NULL;

	if (MultiByteToWideChar(CP_UTF8, 0, str, -1, wstr, size) != size) {
		free(wstr);
		return NULL;
	}
	return wstr;
}


/*
 * returns true if the path is a directory with write access
 */
static __inline bool check_dir(char* cpath)
{
	struct _stat st;
	if ( (_access(cpath, 02) == 0)
	  && (_stat(cpath, &st) == 0)
	  && (st.st_mode & _S_IFDIR) ) {
		return true;
	}
	return false;
}

/*
 * Converts a windows error to human readable string
 * uses retval as errorcode, or, if 0, use GetLastError()
 */
static char *windows_error_str(DWORD retval)
{
#define ERR_BUFFER_SIZE             256
static char err_string[ERR_BUFFER_SIZE];

	DWORD size;
	DWORD errcode, format_errcode;

	errcode = retval?retval:GetLastError();

	safe_sprintf(err_string, ERR_BUFFER_SIZE, "[%d] ", errcode);

	size = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, errcode,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR) &err_string[strlen(err_string)],
		ERR_BUFFER_SIZE, NULL);
	if (size == 0)
	{
		format_errcode = GetLastError();
		if (format_errcode)
			safe_sprintf(err_string, ERR_BUFFER_SIZE,
				"Windows error code %u (FormatMessage error code %u)", errcode, format_errcode);
		else
			safe_sprintf(err_string, ERR_BUFFER_SIZE, "Unknown error code %u", errcode);
	}
	return err_string;
}

/*
 * We need a callback to set the initial directory
 */
INT CALLBACK BrowseCallbackProc(HWND hwnd, UINT uMsg, LPARAM lp, LPARAM pData)
{
	char szDir[MAX_PATH];

	switch(uMsg)
	{
	case BFFM_INITIALIZED:
		// Invalid path will just be ignored
		SendMessageA(hwnd, BFFM_SETSELECTION, TRUE, (LPARAM)extraction_path);
		break;
	case BFFM_SELCHANGED:
	  // Update the status
	  if (SHGetPathFromIDListA((LPITEMIDLIST) lp ,szDir)) {
		 SendMessageA(hwnd,BFFM_SETSTATUSTEXT,0,(LPARAM)szDir);
	  }
	  break;
	}
	return 0;
}

/*
 * Browse for a folder and update the folder edit box
 * Will use the newer IFileOpenDialog if running on Vista and later
 */
void browse_for_folder(void) {

	BROWSEINFO bi;
	LPITEMIDLIST pidl;

#if (_WIN32_WINNT >= 0x0600)	// Vista and later
	size_t i;
	HRESULT hr;
	IShellItem *psi = NULL;
	IShellItem *si_path = NULL;	// Automatically freed
	IFileOpenDialog *pfod = NULL;
	WCHAR *wpath, *fname;
	char* tmp_path = NULL;
#endif

	// Retrieve the path to use as the starting folder
	GetDlgItemText(hMain, IDC_FOLDER, extraction_path, MAX_PATH);

#if (_WIN32_WINNT >= 0x0600)	// Vista and later
	// Even if we have Vista support with the compiler,
	// it does not mean we have the Vista API available
	INIT_VISTA_SHELL32;
	if (IS_VISTA_SHELL32_AVAILABLE) {
		hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC,
			&IID_IFileOpenDialog, (LPVOID)&pfod);
		if (FAILED(hr)) {
			dprintf("CoCreateInstance for FileOpenDialog failed: error %X\n", hr);
			pfod = NULL;	// Just in case
			goto fallback;
		}
		hr = pfod->lpVtbl->SetOptions(pfod, FOS_PICKFOLDERS);
		if (FAILED(hr)) {
			dprintf("Failed to set folder option for FileOpenDialog: error %X\n", hr);
			goto fallback;
		}
		// Set the initial folder (if the path is invalid, will simply use last)
		wpath = utf8_to_wchar(extraction_path);
		// The new IFileOpenDialog makes us split the path
		fname = NULL;
		if ((wpath != NULL) && (wcslen(wpath) >= 1)) {
			for (i=wcslen(wpath)-1; i!=0; i--) {
				if (wpath[i] == L'\\') {
					wpath[i] = 0;
					fname = &wpath[i+1];
					break;
				}
			}
		}

		hr = (*pSHCreateItemFromParsingName)(wpath, NULL, &IID_IShellItem, (LPVOID)&si_path);
		if (SUCCEEDED(hr)) {
			if (wpath != NULL) {
				hr = pfod->lpVtbl->SetFolder(pfod, si_path);
			}
			if (fname != NULL) {
				hr = pfod->lpVtbl->SetFileName(pfod, fname);
			}
		}
		safe_free(wpath);

		hr = pfod->lpVtbl->Show(pfod, hMain);
		if (SUCCEEDED(hr)) {
			hr = pfod->lpVtbl->GetResult(pfod, &psi);
			if (SUCCEEDED(hr)) {
				psi->lpVtbl->GetDisplayName(psi, SIGDN_FILESYSPATH, &wpath);
				tmp_path = wchar_to_utf8(wpath);
				CoTaskMemFree(wpath);
				if (tmp_path == NULL) {
					dprintf("Could not convert path\n");
				} else {
					SetDlgItemTextA(hMain, IDC_FOLDER, tmp_path);
					safe_free(tmp_path);
				}
			} else {
				dprintf("Failed to set folder option for FileOpenDialog: error %X\n", hr);
			}
		} else if ((hr & 0xFFFF) != ERROR_CANCELLED) {
			// If it's not a user cancel, assume the dialog didn't show and fallback
			dprintf("could not show FileOpenDialog: error %X\n", hr);
			goto fallback;
		}
		pfod->lpVtbl->Release(pfod);
		return;
	}
fallback:
	if (pfod != NULL) {
		pfod->lpVtbl->Release(pfod);
	}
#endif
	memset(&bi, 0, sizeof(BROWSEINFO));
	bi.hwndOwner = hMain;
	bi.lpszTitle = "Please select directory";
	bi.pidlRoot = NULL;
	bi.lpfn = BrowseCallbackProc;
	bi.ulFlags = BIF_RETURNFSANCESTORS | BIF_RETURNONLYFSDIRS |
		BIF_DONTGOBELOWDOMAIN | BIF_USENEWUI;
	pidl = SHBrowseForFolder(&bi);
	if (pidl != NULL) {
		// get the name of the folder
		if (SHGetPathFromIDListA(pidl, extraction_path)) {
			SetDlgItemTextA(hMain, IDC_FOLDER, extraction_path);
		}
		CoTaskMemFree(pidl);
	}
}

/*
 * read or write I/O to a file
 * buffer is allocated by the procedure
 */
bool file_io(bool save, char* path, char** buffer, DWORD* size)
{
	HANDLE handle;
	BOOL r;
	bool ret = false;

	if (!save) {
		*buffer = NULL;
	}
	handle = CreateFileA(path, save?GENERIC_WRITE:GENERIC_READ, FILE_SHARE_READ,
		NULL, save?CREATE_ALWAYS:OPEN_EXISTING, 0, NULL);

	if (handle == INVALID_HANDLE_VALUE) {
		dprintf("Could not %s file '%s'\n", save?"create":"open", path);
		goto out;
	}

	if (save) {
		r = WriteFile(handle, *buffer, *size, size, NULL);
	} else {
		*size = GetFileSize(handle, NULL);
		*buffer = malloc(*size);
		if (*buffer == NULL) {
			dprintf("Could not allocate buffer for reading file\n");
			goto out;
		}
		r = ReadFile(handle, *buffer, *size, size, NULL);
	}

	if (!r) {
		dprintf("I/O Error: %s\n", windows_error_str(0));
		goto out;
	}

	dsprintf("%s '%s'\n", save?"Saved file as":"Opened file", path);
	ret = true;

out:
	CloseHandle(handle);
	if (!ret) {
		// Only leave a buffer allocated if successful
		*size = 0;
		if (!save) {
			safe_free(*buffer);
		}
	}
	return ret;
}

/*
 * Return the UTF8 path of a file selected through a load or save dialog
 * Will use the newer IFileOpenDialog if running on Vista and later
 */
char* file_dialog(bool save, char* path, char* filename, char* ext, char* ext_desc)
{
	DWORD tmp;
	OPENFILENAME ofn;
	char selected_name[STR_BUFFER_SIZE];
	char* ext_string = NULL;
	size_t i, ext_strlen;
	BOOL r;
	char* filepath = NULL;

#if (_WIN32_WINNT >= 0x0600)	// Vista and later
	HRESULT hr = FALSE;
	IFileDialog *pfd;
	IShellItem *psiResult;
	COMDLG_FILTERSPEC filter_spec[2];
	char* ext_filter;
	WCHAR *wpath = NULL, *wfilename = NULL;
	IShellItem *si_path = NULL;	// Automatically freed

	INIT_VISTA_SHELL32;
	if (IS_VISTA_SHELL32_AVAILABLE) {
		// Setup the file extension filter table
		ext_filter = malloc(strlen(ext)+3);
		if (ext_filter != NULL) {
			safe_sprintf(ext_filter, strlen(ext)+3, "*.%s", ext);
			filter_spec[0].pszSpec = utf8_to_wchar(ext_filter);
			safe_free(ext_filter);
			filter_spec[0].pszName = utf8_to_wchar(ext_desc);
			filter_spec[1].pszSpec = L"*.*";
			filter_spec[1].pszName = L"All files";
		}

		hr = CoCreateInstance(save?&CLSID_FileSaveDialog:&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC,
			&IID_IFileDialog, (LPVOID)&pfd);

		if (FAILED(hr)) {
			dprintf("CoCreateInstance for FileOpenDialog failed: error %X\n", hr);
			pfd = NULL;	// Just in case
			goto fallback;
		}

		// Set the file extension filters
		pfd->lpVtbl->SetFileTypes(pfd, 2, filter_spec);

		// Set the default directory
		wpath = utf8_to_wchar(path);
		hr = (*pSHCreateItemFromParsingName)(wpath, NULL, &IID_IShellItem, (LPVOID) &si_path);
		if (SUCCEEDED(hr)) {
			pfd->lpVtbl->SetFolder(pfd, si_path);
		}
		safe_free(wpath);

		// Set the default filename
		wfilename = utf8_to_wchar(filename);
		if (wfilename != NULL) {
			pfd->lpVtbl->SetFileName(pfd, wfilename);
		}

		// Display the dialog
		hr = pfd->lpVtbl->Show(pfd, hMain);

		// Cleanup
		safe_free(wfilename);
		safe_free(filter_spec[0].pszSpec);
		safe_free(filter_spec[0].pszName);

		if (SUCCEEDED(hr)) {
			// Obtain the result of the user's interaction with the dialog.
			hr = pfd->lpVtbl->GetResult(pfd, &psiResult);
			if (SUCCEEDED(hr)) {
				hr = psiResult->lpVtbl->GetDisplayName(psiResult, SIGDN_FILESYSPATH, &wpath);
				if (SUCCEEDED(hr)) {
					filepath = wchar_to_utf8(wpath);
					CoTaskMemFree(wpath);
				}
				psiResult->lpVtbl->Release(psiResult);
			}
		} else if ((hr & 0xFFFF) != ERROR_CANCELLED) {
			// If it's not a user cancel, assume the dialog didn't show and fallback
			dprintf("could not show FileOpenDialog: error %X\n", hr);
			goto fallback;
		}
		pfd->lpVtbl->Release(pfd);
		return filepath;
	}

fallback:
	if (pfd != NULL) {
		pfd->lpVtbl->Release(pfd);
	}
#endif

	memset(&ofn, 0, sizeof(OPENFILENAME));
	ofn.lStructSize = sizeof(OPENFILENAME);
	ofn.hwndOwner = hMain;
	// File name
	safe_strcpy(selected_name, STR_BUFFER_SIZE, filename);
	ofn.lpstrFile = selected_name;
	ofn.nMaxFile = STR_BUFFER_SIZE;
	// Set the file extension filters
	ext_strlen = strlen(ext_desc) + 2*strlen(ext) + sizeof(" (*.)\0*.\0All Files (*.*)\0*.*\0\0");
	ext_string = malloc(ext_strlen);
	safe_sprintf(ext_string, ext_strlen, "%s (*.%s)\r*.%s\rAll Files (*.*)\r*.*\r\0", ext_desc, ext, ext);
	// Microsoft could really have picked a better delimiter!
	for (i=0; i<ext_strlen; i++) {
		if (ext_string[i] == '\r') {
			ext_string[i] = 0;
		}
	}
	ofn.lpstrFilter = ext_string;
	// Initial dir
	ofn.lpstrInitialDir = path;
	ofn.Flags = OFN_OVERWRITEPROMPT;
	// Show Dialog
	if (save) {
		r = GetSaveFileNameA(&ofn);
	} else {
		r = GetOpenFileNameA(&ofn);
	}
	if (r) {
		filepath = safe_strdup(selected_name);
	} else {
		tmp = CommDlgExtendedError();
		if (tmp != 0) {
			dprintf("Could not selected file for %s. Error %X\n", save?"save":"open", tmp);
		}
	}
	safe_free(ext_string);
	return filepath;
}

/*
 * Create the application status bar
 */
void create_status_bar(void)
{
	RECT rect;
	int edge[2];

	// Create the status bar.
	hStatus = CreateWindowEx(0, STATUSCLASSNAME, NULL, WS_CHILD | WS_VISIBLE,
		0, 0, 0, 0, hMain, (HMENU)IDC_STATUS,  main_instance, NULL);

	// Create 2 status areas
	GetClientRect(hMain, &rect);
	edge[0] = rect.right - 100;
	edge[1] = rect.right;
	SendMessage(hStatus, SB_SETPARTS, (WPARAM) 2, (LPARAM)&edge);
}

/*
 * Center a dialog with regards to the main application Window
 */
void center_dialog(HWND dialog)
{
	POINT Point;
	RECT DialogRect;
	RECT ParentRect;
	int nWidth;
	int nHeight;

	// Get the size of the dialog box.
	GetWindowRect(dialog, &DialogRect);
	GetClientRect(hMain, &ParentRect);

	// Calculate the height and width of the current dialog
	nWidth = DialogRect.right - DialogRect.left;
	nHeight = DialogRect.bottom - DialogRect.top;

	// Find the center point and convert to screen coordinates.
	Point.x = (ParentRect.right - ParentRect.left) / 2;
	Point.y = (ParentRect.bottom - ParentRect.top) / 2;
	ClientToScreen(hMain, &Point);

	// Calculate the new x, y starting point.
	Point.x -= nWidth / 2;
	Point.y -= nHeight / 2 + 35;

	// Move the window.
	MoveWindow(dialog, Point.x, Point.y, nWidth, nHeight, FALSE);
}

/*
 * About dialog callback
 */
INT_PTR CALLBACK about_callback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	char* url = NULL;

	switch (message) {
	case WM_INITDIALOG:
		center_dialog(hDlg);
		break;
	case WM_NOTIFY:
		switch (((LPNMHDR)lParam)->code) {
		case NM_CLICK:
		case NM_RETURN:
			// We have only one URL on the About box
			url = wchar_to_utf8(((PNMLINK)lParam)->item.szUrl);
			if (url != NULL) {
				ShellExecuteA(hDlg, "open", url, NULL, NULL, SW_SHOWNORMAL);
				safe_free(url);
			} else {
				dprintf("Could not open URL\n");
			}
			break;
		}
		break;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK:
		case IDCANCEL:
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

/*
 * Thread executed by the run_with_progress_bar() function
 */
void __cdecl progress_thread(void* param)
{
	int r;

	// Call the user provided function
	r = (*progress_function)();
	progress_thid = -1L;
	PostMessage(hProgress, UM_PROGRESS_STOP, (WPARAM)r, 0);
	_endthread();
}

/*
 * Callback for the run_with_progress_bar() function
 */
INT_PTR CALLBACK progress_callback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	LRESULT loc;
	int i;
	// coordinates that we want to disable (=> no resize)
	static LRESULT disabled[9] = { HTLEFT, HTRIGHT, HTTOP, HTBOTTOM, HTSIZE,
		HTTOPLEFT, HTTOPRIGHT, HTBOTTOMLEFT, HTBOTTOMRIGHT };

	switch (message) {
	case WM_INITDIALOG:
		hProgress = hDlg;
		center_dialog(hDlg);
		// Toggle the progressbar Marquee animation
		SendMessage(GetDlgItem(hDlg, IDC_PROGRESS), PBM_SETMARQUEE, TRUE, 0);
		PostMessage(hDlg, UM_PROGRESS_START, 0, 0);
		return (INT_PTR)TRUE;
	case WM_NCHITTEST:
		// Check coordinates to prevent resize actions
		loc = DefWindowProc(hDlg, message, wParam, lParam);
		for(i = 0; i < 9; i++) {
			if (loc == disabled[i]) {
				return (INT_PTR)TRUE;
			}
		}
		return (INT_PTR)FALSE;
	case UM_PROGRESS_START:
		if (progress_thid != -1L) {
			dprintf("program assertion failed - another operation is in progress\n");
		} else {
			// Using a thread prevents application freezout on security warning
			progress_thid = _beginthread(progress_thread, 0, NULL);
			if (progress_thid != -1L) {
				return (INT_PTR)TRUE;
			}
			dprintf("unable to create progress_thread\n");
		}
		// Fall through and return -1 as an error
		wParam = (WPARAM)-1;
	case UM_PROGRESS_STOP:
		hProgress = INVALID_HANDLE_VALUE;
		EndDialog(hDlg, (int)wParam);
		return (INT_PTR)TRUE;
	}
	return (INT_PTR)FALSE;
}

/*
 * Call a blocking function (returning an int) as a modal thread with a progress bar
 */
int run_with_progress_bar(int(*function)(void)) {
	if (function == NULL) {
		return -1;
	}
	progress_function = function;
	return (int)DialogBox(main_instance, MAKEINTRESOURCE(IDD_PROGRESS), hMain, progress_callback);
}

/*
 * We use our own MessageBox for notifications to have greater control (center, no close button, etc)
 */
INT_PTR CALLBACK notification_callback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	LRESULT loc;
	int i;
	HICON junk;
	// Prevent resising
	static LRESULT disabled[9] = { HTLEFT, HTRIGHT, HTTOP, HTBOTTOM, HTSIZE,
		HTTOPLEFT, HTTOPRIGHT, HTBOTTOMLEFT, HTBOTTOMRIGHT };
	static HBRUSH white_brush, separator_brush;

	switch (message) {
	case WM_INITDIALOG:
		white_brush = CreateSolidBrush(WHITE);
		separator_brush = CreateSolidBrush(SEPARATOR_GREY);
		center_dialog(hDlg);
		// Change the default icon
		junk = Static_SetIcon(GetDlgItem(hDlg, IDC_NOTIFICATION_ICON), hMessageIcon);
		// Set the dialog title
		if (message_title != NULL) {
			SetWindowTextA(hDlg, message_title);
		}
		// Set the control text
		if (message_text != NULL) {
			SetWindowTextA(GetDlgItem(hDlg, IDC_NOTIFICATION_TEXT), message_text);
		}
		return (INT_PTR)TRUE;
	case WM_CTLCOLORSTATIC:
		// Change the background colour for static text and icon
		SetBkMode((HDC)wParam, TRANSPARENT);
		if ((HWND)lParam == GetDlgItem(hDlg, IDC_NOTIFICATION_LINE)) {
			return (INT_PTR)separator_brush;
		}
		return (INT_PTR)white_brush;
	case WM_NCHITTEST:
		// Check coordinates to prevent resize actions
		loc = DefWindowProc(hDlg, message, wParam, lParam);
		for(i = 0; i < 9; i++) {
			if (loc == disabled[i]) {
				return (INT_PTR)TRUE;
			}
		}
		return (INT_PTR)FALSE;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK:
		case IDCANCEL:
		case IDC_NOTIFICATION_CLOSE:
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

/*
 * Thread that displays the progress dialog
 */
void notification(int type, char* text, char* title)
{
	message_text = text;
	message_title = title;
	switch(type) {
	case MSG_WARNING:
		hMessageIcon = LoadIcon(NULL, IDI_WARNING);
		break;
	case MSG_ERROR:
		hMessageIcon = LoadIcon(NULL, IDI_ERROR);
		break;
	case MSG_INFO:
	default:
		hMessageIcon = LoadIcon(NULL, IDI_INFORMATION);
		break;
	}
	DialogBox(main_instance, MAKEINTRESOURCE(IDD_NOTIFICATION), hMain, notification_callback);
	message_text = NULL;
}
