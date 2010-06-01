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
#include <commdlg.h>

#include "resource.h"
#include "zadig.h"

#if (_WIN32_WINNT >= 0x0600)
// Available on Vista and later
static HRESULT (__stdcall *pSHCreateItemFromParsingName)(PCWSTR, IBindCtx*, REFIID, void **) = NULL;
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
 * Save a binary buffer through a save as dialog
 * Will use the newer IFileOpenDialog if running on Vista and later
 */
void save_file(char* path, char* filename, char* ext, char* ext_desc,
			   void* buffer, DWORD size)
{
	HANDLE handle;
	DWORD tmp;
	OPENFILENAME ofn;
	char selected_name[STR_BUFFER_SIZE];
	char* ext_string = NULL;
	size_t i, ext_strlen;
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

		hr = CoCreateInstance(&CLSID_FileSaveDialog, NULL, CLSCTX_INPROC,
			&IID_IFileDialog, (LPVOID)&pfd);

		if (FAILED(hr)) {
			dprintf("CoCreateInstance for FileSaveDialog failed: error %X\n", hr);
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
					handle = CreateFileW(wpath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
					if (handle != INVALID_HANDLE_VALUE) {
						WriteFile(handle, buffer, size, &tmp, NULL);
						CloseHandle(handle);
						path = wchar_to_utf8(wpath);
						dsprintf("Saved log as '%s'\n", path);
						safe_free(path);
					}
					CoTaskMemFree(wpath);
				}
				// Do something with the result.
				psiResult->lpVtbl->Release(psiResult);
			}
		} else if ((hr & 0xFFFF) != ERROR_CANCELLED) {
			// If it's not a user cancel, assume the dialog didn't show and fallback
			dprintf("could not show FileOpenDialog: error %X\n", hr);
			goto fallback;
		}
		pfd->lpVtbl->Release(pfd);
		return;
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
	// Why couldn't Microsoft use a better delimiter
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
	if (GetSaveFileNameA(&ofn)) {
		handle = CreateFileA(selected_name, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
		if (handle != INVALID_HANDLE_VALUE) {
			WriteFile(handle, buffer, size, &tmp, NULL);
			CloseHandle(handle);
			dsprintf("Saved log as '%s'\n", selected_name);
		}
	} else {
		tmp = CommDlgExtendedError();
		if (tmp != 0) {
			dprintf("Could not save file. Error %X\n", tmp);
		}
	}
	safe_free(ext_string);
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
 * Another callback is needed to change the cursor when hovering over the URL
 * Why don't we use syslink? Because it requires Unicode
 */
INT_PTR CALLBACK About_URL(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	WNDPROC original_wndproc;

	original_wndproc = (WNDPROC)GetProp(hDlg, "PROP_ORIGINAL_PROC");
	switch (message)
	{
	case WM_SETCURSOR:
		if ((HWND)wParam == GetDlgItem(hDlg, IDC_URL)) {
			SetCursor(LoadCursor(NULL, IDC_HAND));
			return (INT_PTR)TRUE;
		}
	}
	return CallWindowProc(original_wndproc, hDlg, message, wParam, lParam);
}

/*
 * About dialog callback
 */
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	HDC hdcStatic;
	WNDPROC original_wndproc;

	switch (message) {
	case WM_INITDIALOG:
		// Subclass the callback so that we can change the cursor
		original_wndproc = (WNDPROC)GetWindowLongPtr(hDlg, GWLP_WNDPROC);
		SetPropA(hDlg, "PROP_ORIGINAL_PROC", (HANDLE)original_wndproc);
		SetWindowLongPtr(hDlg, GWLP_WNDPROC, (LONG_PTR)About_URL);
		return (INT_PTR)TRUE;
	case WM_CTLCOLORSTATIC:
		// Change the link colour to blue
		hdcStatic = (HDC)wParam;
		if ((HWND)lParam == GetDlgItem(hDlg, IDC_URL)) {
			SetTextColor(hdcStatic, RGB(0,0,255));
			SetBkMode(hdcStatic, TRANSPARENT);
			return (INT_PTR)GetStockObject(NULL_BRUSH);
		}
		break;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK:
		case IDCANCEL:
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		case IDC_URL:	// NB: control must have Notify enabled
			ShellExecute(hDlg, "open", "http://libusb.org/wiki/libwdi",
				NULL, NULL, SW_SHOWNORMAL);
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

/*
 * Toggle the application cursor to busy and back
 */
void toggle_busy(void)
{
	static bool is_busy = false;
	static ULONG_PTR saved_cursor[5];
	HCURSOR cursor;

	if (!is_busy) {
		saved_cursor[0] = GetClassLongPtr(hMain, GCLP_HCURSOR);
		saved_cursor[1] = GetClassLongPtr(hDeviceList, GCLP_HCURSOR);
		saved_cursor[2] = GetClassLongPtr(hInfo, GCLP_HCURSOR);
		saved_cursor[3] = GetClassLongPtr(GetDlgItem(hMain, IDC_INSTALL), GCLP_HCURSOR);
		saved_cursor[4] = GetClassLongPtr(GetDlgItem(hMain, IDC_TARGETSPIN), GCLP_HCURSOR);
		cursor = LoadCursorA(NULL, IDC_WAIT);
		SetClassLongPtr(hMain, GCLP_HCURSOR, (ULONG_PTR)cursor);
		SetClassLongPtr(hDeviceList, GCLP_HCURSOR, (ULONG_PTR)cursor);
		SetClassLongPtr(hInfo, GCLP_HCURSOR, (ULONG_PTR)cursor);
		SetClassLongPtr(GetDlgItem(hMain, IDC_INSTALL), GCLP_HCURSOR, (ULONG_PTR)cursor);
		SetClassLongPtr(GetDlgItem(hMain, IDC_TARGETSPIN), GCLP_HCURSOR, (ULONG_PTR)cursor);
	} else {
		SetClassLongPtr(hMain, GCLP_HCURSOR, saved_cursor[0]);
		SetClassLongPtr(hDeviceList, GCLP_HCURSOR, saved_cursor[1]);
		SetClassLongPtr(hInfo, GCLP_HCURSOR, saved_cursor[2]);
		SetClassLongPtr(GetDlgItem(hMain, IDC_INSTALL), GCLP_HCURSOR, saved_cursor[3]);
		SetClassLongPtr(GetDlgItem(hMain, IDC_TARGETSPIN), GCLP_HCURSOR, saved_cursor[4]);
	}
	is_busy = !is_busy;
	PostMessage(hMain, WM_SETCURSOR, 0, 0);		// Needed to restore the cursor
}
