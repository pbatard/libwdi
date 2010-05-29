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

#include "resource.h"
#include "zadig.h"

#if (_WIN32_WINNT >= 0x0600)
// Available on Vista and later
static HRESULT (__stdcall *pSHCreateItemFromParsingName)(PCWSTR, IBindCtx*, REFIID, void **) = NULL;
#endif

// TODO: make sure this is never called in release
void NOT_IMPLEMENTED(void) {
	MessageBox(NULL, "NOT IMPLEMENTED", "Feature not implemented yet", MB_ICONSTOP);
}

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
		SendMessageA(hwnd, BFFM_SETSELECTION, TRUE, (LPARAM)path);
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
 * Will use the newer IFileOpenDialog if compiled for Vista and later
 */
void browse_for_folder(void) {

	BROWSEINFO bi;
	LPITEMIDLIST pidl;
#if (_WIN32_WINNT >= 0x0600)	// Vista and later
	size_t i;
	HRESULT hr;
	IShellItem *psi = NULL;
	IShellItem *si_path = NULL;
	IFileOpenDialog *pfod = NULL;
	WCHAR *wpath, *fname;
	char* tmp_path = NULL;
#endif

	// Retrieve the path to use as the starting folder
	GetDlgItemText(hMain, IDC_FOLDER, path, MAX_PATH);

#if (_WIN32_WINNT >= 0x0600)	// Vista and later
	// Even if we have Vista support with the compiler,
	// it does not mean we have the Vista API available
	if (pSHCreateItemFromParsingName == NULL) {
		pSHCreateItemFromParsingName = (HRESULT (__stdcall *)(PCWSTR, IBindCtx*, REFIID, void **))
			GetProcAddress(GetModuleHandle("SHELL32"), "SHCreateItemFromParsingName");
	}

	if (pSHCreateItemFromParsingName != NULL) {
		hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC,
			&IID_IFileOpenDialog, (LPVOID) &pfod);
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
		wpath = utf8_to_wchar(path);
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

		hr = (*pSHCreateItemFromParsingName)(wpath, NULL, &IID_IShellItem, (LPVOID) &si_path);
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
		if (SHGetPathFromIDListA(pidl, path)) {
			SetDlgItemTextA(hMain, IDC_FOLDER, path);
		}
		CoTaskMemFree(pidl);
	}
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
