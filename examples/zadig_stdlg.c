/*
 * Zadig: Automated Driver Installer for USB devices (GUI version)
 * Standard Dialog Routines (Browse for folder, About, etc)
 * Copyright (c) 2010-2017 Pete Batard <pete@akeo.ie>
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
#include <richedit.h>
#include <sddl.h>

#include "libwdi.h"
#include "zadig_resource.h"
#include "zadig.h"
#include "zadig_license.h"
#include "zadig_registry.h"
#include "msapi_utf8.h"
#include "stdfn.h"

/*
 * Globals
 */
static HICON hMessageIcon = (HICON)INVALID_HANDLE_VALUE;
static char* szMessageText = NULL;
static char* szMessageTitle = NULL;
extern HFONT bold_font;
extern float fScale;
static HWND hBrowseEdit;
static WNDPROC pOrgBrowseWndproc;
static const SETTEXTEX friggin_microsoft_unicode_amateurs = {ST_DEFAULT, CP_UTF8};
static BOOL notification_is_question;
static const notification_info* notification_more_info;
static BOOL reg_commcheck = FALSE;
static WNDPROC original_wndproc = NULL;

/*
 * https://blogs.msdn.microsoft.com/oldnewthing/20040802-00/?p=38283/
 */
void SetDialogFocus(HWND hDlg, HWND hCtrl)
{
	SendMessage(hDlg, WM_NEXTDLGCTL, (WPARAM)hCtrl, TRUE);
}

/*
 * Converts a name + ext UTF-8 pair to a valid MS filename.
 * Returned string is allocated and needs to be freed manually
 */
char* to_valid_filename(char* name, char* ext)
{
	size_t i, j, k;
	BOOL found;
	char* ret;
	wchar_t unauthorized[] = L"\x0001\x0002\x0003\x0004\x0005\x0006\x0007\x0008\x000a"
		L"\x000b\x000c\x000d\x000e\x000f\x0010\x0011\x0012\x0013\x0014\x0015\x0016\x0017"
		L"\x0018\x0019\x001a\x001b\x001c\x001d\x001e\x001f\x007f\"*/:<>?\\|,";
	wchar_t to_underscore[] = L" \t";
	wchar_t *wname, *wext, *wret;

	if ((name == NULL) || (ext == NULL)) {
		return NULL;
	}

	if (strlen(name) > WDI_MAX_STRLEN) return NULL;

	// Convert to UTF-16
	wname = utf8_to_wchar(name);
	wext = utf8_to_wchar(ext);
	if ((wname == NULL) || (wext == NULL)) {
		safe_free(wname); safe_free(wext); return NULL;
	}

	// The returned UTF-8 string will never be larger than the sum of its parts
	wret = (wchar_t*)calloc(2*(wcslen(wname) + wcslen(wext) + 2), 1);
	if (wret == NULL) {
		safe_free(wname); safe_free(wext); return NULL;
	}
	wcscpy(wret, wname);
	safe_free(wname);
	wcscat(wret, wext);
	safe_free(wext);

	for (i=0, k=0; i<wcslen(wret); i++) {
		found = FALSE;
		for (j=0; j<wcslen(unauthorized); j++) {
			if (wret[i] == unauthorized[j]) {
				found = TRUE; break;
			}
		}
		if (found) continue;
		found = FALSE;
		for (j=0; j<wcslen(to_underscore); j++) {
			if (wret[i] == to_underscore[j]) {
				wret[k++] = '_';
				found = TRUE; break;
			}
		}
		if (found) continue;
		wret[k++] = wret[i];
	}
	wret[k] = 0;
	ret = wchar_to_utf8(wret);
	safe_free(wret);
	return ret;
}

/*
 * Convert a windows error to human readable string
 * uses retval as errorcode, or, if 0, use GetLastError()
 */
const char *WindowsErrorString(void)
{
	static char err_string[256] = { 0 };

	DWORD size;
	DWORD errcode, format_error;

	errcode = GetLastError();

	static_sprintf(err_string, "[0x%08lX] ", errcode);

	size = FormatMessageU(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, HRESULT_CODE(errcode),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), &err_string[strlen(err_string)],
		sizeof(err_string) - (DWORD)strlen(err_string), NULL);
	if (size == 0) {
		format_error = GetLastError();
		if ((format_error) && (format_error != 0x13D))		// 0x13D, decode error, is returned for unknown codes
			static_sprintf(err_string, "Windows error code 0x%08lX (FormatMessage error code 0x%08lX)",
				errcode, format_error);
		else
			static_sprintf(err_string, "Unknown error 0x%08lX", errcode);
	}

	SetLastError(errcode);	// Make sure we don't change the errorcode on exit
	return err_string;
}

/*
 * Retrieve the SID of the current user. The returned PSID must be freed by the caller using LocalFree()
 */
static PSID GetSid(void) {
	TOKEN_USER* tu = NULL;
	DWORD len;
	HANDLE token;
	PSID ret = NULL;
	char* psid_string = NULL;

	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
		dprintf("OpenProcessToken failed: %s", WindowsErrorString());
		return NULL;
	}

	if (!GetTokenInformation(token, TokenUser, tu, 0, &len)) {
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
			dprintf("GetTokenInformation (pre) failed: %s", WindowsErrorString());
			return NULL;
		}
		tu = (TOKEN_USER*)calloc(1, len);
	}
	if (tu == NULL) {
		return NULL;
	}

	if (GetTokenInformation(token, TokenUser, tu, len, &len)) {
		/*
		 * now of course, the interesting thing is that if you return tu->User.Sid
		 * but free tu, the PSID pointer becomes invalid after a while.
		 * The workaround? Convert to string then back to PSID
		 */
		if (!ConvertSidToStringSidA(tu->User.Sid, &psid_string)) {
			dprintf("unable to convert SID to string: %s", WindowsErrorString());
			ret = NULL;
		} else {
			if (!ConvertStringSidToSidA(psid_string, &ret)) {
				dprintf("unable to convert string back to SID: %s", WindowsErrorString());
				ret = NULL;
			}
			// MUST use LocalFree()
			LocalFree(psid_string);
		}
	} else {
		ret = NULL;
		dprintf("GetTokenInformation (real) failed: %s", WindowsErrorString());
	}
	free(tu);
	return ret;
}

/*
 * We need a sub-callback to read the content of the edit box on exit and update
 * our path, else if what the user typed does match the selection, it is discarded.
 * Talk about a convoluted way of producing an intuitive folder selection dialog
 */
INT CALLBACK BrowseDlgCallback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch(message) {
	case WM_DESTROY:
		GetWindowTextU(hBrowseEdit, szFolderPath, sizeof(szFolderPath));
		break;
	}
	return (INT)CallWindowProc(pOrgBrowseWndproc, hDlg, message, wParam, lParam);
}

/*
 * Main BrowseInfo callback to set the initial directory and populate the edit control
 */
INT CALLBACK BrowseInfoCallback(HWND hDlg, UINT message, LPARAM lParam, LPARAM pData)
{
	char dir[MAX_PATH];
	wchar_t* wpath;
	LPITEMIDLIST pidl;

	switch(message) {
	case BFFM_INITIALIZED:
		pOrgBrowseWndproc = (WNDPROC)SetWindowLongPtr(hDlg, GWLP_WNDPROC, (LONG_PTR)BrowseDlgCallback);
		// Windows hides the full path in the edit box by default, which is bull.
		// Get a handle to the edit control to fix that
		hBrowseEdit = FindWindowExA(hDlg, NULL, "Edit", NULL);
		SetWindowTextU(hBrowseEdit, szFolderPath);
		SetDialogFocus(hDlg, hBrowseEdit);
		// On Windows 7, MinGW only properly selects the specified folder when using a pidl
		wpath = utf8_to_wchar(szFolderPath);
		pidl = SHSimpleIDListFromPath(wpath);
		safe_free(wpath);
		// NB: see http://connect.microsoft.com/VisualStudio/feedback/details/518103/bffm-setselection-does-not-work-with-shbrowseforfolder-on-windows-7
		// for details as to why we send BFFM_SETSELECTION twice.
		SendMessageW(hDlg, BFFM_SETSELECTION, (WPARAM)FALSE, (LPARAM)pidl);
		Sleep(100);
		PostMessageW(hDlg, BFFM_SETSELECTION, (WPARAM)FALSE, (LPARAM)pidl);
		break;
	case BFFM_SELCHANGED:
		// Update the status
		if (SHGetPathFromIDListU((LPITEMIDLIST)lParam, dir)) {
			SendMessageLU(hDlg, BFFM_SETSTATUSTEXT, 0, dir);
			SetWindowTextU(hBrowseEdit, dir);
		}
		break;
	}
	return 0;
}

/*
 * Browse for a folder and update the folder edit box
 */
void BrowseForFolder(void) {

	BROWSEINFOW bi;
	LPITEMIDLIST pidl;
	WCHAR *wpath;
	size_t i;
	HRESULT hr;
	IShellItem *psi = NULL;
	IShellItem *si_path = NULL;	// Automatically freed
	IFileOpenDialog *pfod = NULL;
	WCHAR *fname;
	char* tmp_path = NULL;

	dialog_showing++;
	hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC,
		&IID_IFileOpenDialog, (LPVOID)&pfod);
	if (FAILED(hr)) {
		dprintf("CoCreateInstance for FileOpenDialog failed: error %X", hr);
		pfod = NULL;	// Just in case
		goto fallback;
	}
	hr = pfod->lpVtbl->SetOptions(pfod, FOS_PICKFOLDERS);
	if (FAILED(hr)) {
		dprintf("Failed to set folder option for FileOpenDialog: error %X", hr);
		goto fallback;
	}
	// Set the initial folder (if the path is invalid, will simply use last)
	wpath = utf8_to_wchar(szFolderPath);
	// The new IFileOpenDialog makes us split the path
	fname = NULL;
	if ((wpath != NULL) && (wcslen(wpath) >= 1)) {
		for (i = wcslen(wpath) - 1; i != 0; i--) {
			if (wpath[i] == L'\\') {
				wpath[i] = 0;
				fname = &wpath[i + 1];
				break;
			}
		}
	}

	hr = SHCreateItemFromParsingName(wpath, NULL, &IID_IShellItem, (LPVOID)&si_path);
	if (SUCCEEDED(hr)) {
		if (wpath != NULL) {
			pfod->lpVtbl->SetFolder(pfod, si_path);
		}
		if (fname != NULL) {
			pfod->lpVtbl->SetFileName(pfod, fname);
		}
	}
	safe_free(wpath);

	hr = pfod->lpVtbl->Show(pfod, hMainDialog);
	if (SUCCEEDED(hr)) {
		hr = pfod->lpVtbl->GetResult(pfod, &psi);
		if (SUCCEEDED(hr)) {
			psi->lpVtbl->GetDisplayName(psi, SIGDN_FILESYSPATH, &wpath);
			tmp_path = wchar_to_utf8(wpath);
			CoTaskMemFree(wpath);
			if (tmp_path == NULL) {
				dprintf("Could not convert path");
			} else {
				static_strcpy(szFolderPath, tmp_path);
				safe_free(tmp_path);
			}
		} else {
			dprintf("Failed to set folder option for FileOpenDialog: error %X", hr);
		}
	} else if ((hr & 0xFFFF) != ERROR_CANCELLED) {
		// If it's not a user cancel, assume the dialog didn't show and fallback
		dprintf("Could not show FileOpenDialog: error %X", hr);
		goto fallback;
	}
	pfod->lpVtbl->Release(pfod);
	dialog_showing--;
	return;
fallback:
	if (pfod != NULL) {
		pfod->lpVtbl->Release(pfod);
	}

	memset(&bi, 0, sizeof(BROWSEINFOW));
	bi.hwndOwner = hMainDialog;
	bi.lpszTitle = L"Please select folder";
	bi.lpfn = BrowseInfoCallback;
	// BIF_NONEWFOLDERBUTTON = 0x00000200 is unknown on MinGW
	bi.ulFlags = BIF_RETURNFSANCESTORS | BIF_RETURNONLYFSDIRS |
		BIF_DONTGOBELOWDOMAIN | BIF_EDITBOX | 0x00000200;
	pidl = SHBrowseForFolderW(&bi);
	if (pidl != NULL) {
		CoTaskMemFree(pidl);
	}
	dialog_showing--;
}

/*
 * read or write I/O to a file
 * buffer is allocated by the procedure. path is UTF-8
 */
BOOL FileIo(BOOL save, char* path, char** buffer, DWORD* size)
{
	SECURITY_ATTRIBUTES s_attr, *ps = NULL;
	SECURITY_DESCRIPTOR s_desc;
	PSID sid = NULL;
	HANDLE handle;
	BOOL r;
	BOOL ret = FALSE;

	// Change the owner from admin to regular user
	sid = GetSid();
	if ( (sid != NULL)
	  && InitializeSecurityDescriptor(&s_desc, SECURITY_DESCRIPTOR_REVISION)
	  && SetSecurityDescriptorOwner(&s_desc, sid, FALSE) ) {
		s_attr.nLength = sizeof(SECURITY_ATTRIBUTES);
		s_attr.bInheritHandle = FALSE;
		s_attr.lpSecurityDescriptor = &s_desc;
		ps = &s_attr;
	} else {
		dprintf("could not set security descriptor: %s", WindowsErrorString());
	}

	if (!save) {
		*buffer = NULL;
	}
	handle = CreateFileU(path, save?GENERIC_WRITE:GENERIC_READ, FILE_SHARE_READ,
		ps, save?CREATE_ALWAYS:OPEN_EXISTING, 0, NULL);

	if (handle == INVALID_HANDLE_VALUE) {
		dprintf("Could not %s file '%s'", save?"create":"open", path);
		goto out;
	}

	if (save) {
		r = WriteFile(handle, *buffer, *size, size, NULL);
	} else {
		*size = GetFileSize(handle, NULL);
		*buffer = (char*)malloc(*size);
		if (*buffer == NULL) {
			dprintf("Could not allocate buffer for reading file");
			goto out;
		}
		r = ReadFile(handle, *buffer, *size, size, NULL);
	}

	if (!r) {
		dprintf("I/O Error: %s", WindowsErrorString());
		goto out;
	}

	dsprintf("%s '%s'", save?"Saved file as":"Opened file", path);
	ret = TRUE;

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
* All string parameters are UTF-8
* IMPORTANT NOTE: Remember that you need to call CoInitializeEx() for
* *EACH* thread you invoke FileDialog from, as GetDisplayName() will
* return error 0x8001010E otherwise.
*/
char* FileDialog(BOOL save, char* path, const ext_t* ext, DWORD options)
{
	DWORD tmp;
	OPENFILENAMEA ofn;
	char selected_name[MAX_PATH];
	char *ext_string = NULL, *all_files = NULL;
	size_t i, j, ext_strlen;
	BOOL r;
	char* filepath = NULL;
	HRESULT hr = FALSE;
	IFileDialog *pfd = NULL;
	IShellItem *psiResult;
	COMDLG_FILTERSPEC* filter_spec = NULL;
	wchar_t *wpath = NULL, *wfilename = NULL;
	IShellItem *si_path = NULL;	// Automatically freed

	if ((ext == NULL) || (ext->count == 0) || (ext->extension == NULL) || (ext->description == NULL))
		return NULL;
	dialog_showing++;

	filter_spec = (COMDLG_FILTERSPEC*)calloc(ext->count + 1, sizeof(COMDLG_FILTERSPEC));
	if (filter_spec != NULL) {
		// Setup the file extension filter table
		for (i = 0; i < ext->count; i++) {
			filter_spec[i].pszSpec = utf8_to_wchar(ext->extension[i]);
			filter_spec[i].pszName = utf8_to_wchar(ext->description[i]);
		}
		filter_spec[i].pszSpec = L"*.*";
		filter_spec[i].pszName = L"All files";

		hr = CoCreateInstance(save ? &CLSID_FileSaveDialog : &CLSID_FileOpenDialog, NULL, CLSCTX_INPROC,
			&IID_IFileDialog, (LPVOID)&pfd);

		if (FAILED(hr)) {
			SetLastError(hr);
			dprintf("CoCreateInstance for FileOpenDialog failed: %s\n", WindowsErrorString());
			pfd = NULL;	// Just in case
			goto fallback;
		}

		// Set the file extension filters
		pfd->lpVtbl->SetFileTypes(pfd, (UINT)ext->count + 1, filter_spec);

		// Set the default directory
		wpath = utf8_to_wchar(path);
		hr = SHCreateItemFromParsingName(wpath, NULL, &IID_IShellItem, (LPVOID)&si_path);
		if (SUCCEEDED(hr)) {
			pfd->lpVtbl->SetFolder(pfd, si_path);
		}
		safe_free(wpath);

		// Set the default filename
		wfilename = utf8_to_wchar((ext->filename == NULL) ? "" : ext->filename);
		if (wfilename != NULL) {
			pfd->lpVtbl->SetFileName(pfd, wfilename);
		}

		// Display the dialog
		hr = pfd->lpVtbl->Show(pfd, hMainDialog);

		// Cleanup
		safe_free(wfilename);
		for (i = 0; i < ext->count; i++) {
			safe_free(filter_spec[i].pszSpec);
			safe_free(filter_spec[i].pszName);
		}
		safe_free(filter_spec);

		if (SUCCEEDED(hr)) {
			// Obtain the result of the user's interaction with the dialog.
			hr = pfd->lpVtbl->GetResult(pfd, &psiResult);
			if (SUCCEEDED(hr)) {
				hr = psiResult->lpVtbl->GetDisplayName(psiResult, SIGDN_FILESYSPATH, &wpath);
				if (SUCCEEDED(hr)) {
					filepath = wchar_to_utf8(wpath);
					CoTaskMemFree(wpath);
				} else {
					SetLastError(hr);
					dprintf("Unable to access file path: %s\n", WindowsErrorString());
				}
				psiResult->lpVtbl->Release(psiResult);
			}
		} else if ((hr & 0xFFFF) != ERROR_CANCELLED) {
			// If it's not a user cancel, assume the dialog didn't show and fallback
			SetLastError(hr);
			dprintf("Could not show FileOpenDialog: %s\n", WindowsErrorString());
			goto fallback;
		}
		pfd->lpVtbl->Release(pfd);
		dialog_showing--;
		return filepath;
	}

fallback:
	safe_free(filter_spec);
	if (pfd != NULL) {
		pfd->lpVtbl->Release(pfd);
	}

	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hMainDialog;
	// Selected File name
	static_sprintf(selected_name, "%s", (ext->filename == NULL) ? "" : ext->filename);
	ofn.lpstrFile = selected_name;
	ofn.nMaxFile = MAX_PATH;
	// Set the file extension filters
	all_files = "All files";
	ext_strlen = 0;
	for (i = 0; i<ext->count; i++) {
		ext_strlen += safe_strlen(ext->description[i]) + 2 * safe_strlen(ext->extension[i]) + sizeof(" ()\r\r");
	}
	ext_strlen += safe_strlen(all_files) + sizeof(" (*.*)\r*.*\r");
	ext_string = (char*)malloc(ext_strlen + 1);
	if (ext_string == NULL)
		return NULL;
	ext_string[0] = 0;
	for (i = 0, j = 0; i<ext->count; i++) {
		j += _snprintf(&ext_string[j], ext_strlen - j, "%s (%s)\r%s\r", ext->description[i], ext->extension[i], ext->extension[i]);
	}
	j = _snprintf(&ext_string[j], ext_strlen - j, "%s (*.*)\r*.*\r", all_files);
	// Microsoft could really have picked a better delimiter!
	for (i = 0; i<ext_strlen; i++) {
		// Since the VS Code Analysis tool is dumb...
#if defined(_MSC_VER)
#pragma warning(suppress: 6385)
#endif
		if (ext_string[i] == '\r') {
#if defined(_MSC_VER)
#pragma warning(suppress: 6386)
#endif
			ext_string[i] = 0;
		}
	}
	ofn.lpstrFilter = ext_string;
	ofn.nFilterIndex = 1;
	ofn.lpstrInitialDir = path;
	ofn.Flags = OFN_OVERWRITEPROMPT | options;
	// Show Dialog
	if (save) {
		r = GetSaveFileNameU(&ofn);
	} else {
		r = GetOpenFileNameU(&ofn);
	}
	if (r) {
		filepath = safe_strdup(selected_name);
	} else {
		tmp = CommDlgExtendedError();
		if (tmp != 0) {
			dprintf("Could not select file for %s. Error %X\n", save ? "save" : "open", tmp);
		}
	}
	safe_free(ext_string);
	dialog_showing--;
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
		0, 0, 0, 0, hMainDialog, (HMENU)IDC_STATUS,  main_instance, NULL);

	// Create 2 status areas
	GetClientRect(hMainDialog, &rect);
	edge[0] = rect.right - (int)(100.0f*fScale);
	edge[1] = rect.right;
	SendMessage(hStatus, SB_SETPARTS, (WPARAM) 2, (LPARAM)&edge);
}

/*
 * Center a dialog with regards to the main application Window or the desktop
 * See http://msdn.microsoft.com/en-us/library/windows/desktop/ms644996.aspx#init_box
 */
void center_dialog(HWND hDlg)
{
	HWND hParent;
	RECT rc, rcDlg, rcParent;

	if ((hParent = GetParent(hDlg)) == NULL) {
		hParent = GetDesktopWindow();
	}

	GetWindowRect(hParent, &rcParent);
	GetWindowRect(hDlg, &rcDlg);
	CopyRect(&rc, &rcParent);

	// Offset the parent and dialog box rectangles so that right and bottom
	// values represent the width and height, and then offset the parent again
	// to discard space taken up by the dialog box.
	OffsetRect(&rcDlg, -rcDlg.left, -rcDlg.top);
	OffsetRect(&rc, -rc.left, -rc.top);
	OffsetRect(&rc, -rcDlg.right, -rcDlg.bottom);

	SetWindowPos(hDlg, HWND_TOP, rcParent.left + (rc.right / 2), rcParent.top + (rc.bottom / 2) - 25, 0, 0, SWP_NOSIZE);
}

/*
 * License callback
 */
INT_PTR CALLBACK license_callback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {
	case WM_INITDIALOG:
		center_dialog(hDlg);
		SetDlgItemTextA(hDlg, IDC_LICENSE_TEXT, gplv3);
		break;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK:
		case IDCANCEL:
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
	}
	return (INT_PTR)FALSE;
}

/*
 * About dialog callback
 */
INT_PTR CALLBACK about_callback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	int i;
	const int edit_id[2] = {IDC_ABOUT_BLURB, IDC_ABOUT_COPYRIGHTS};
	char about_blurb[2048];
	const char* edit_text[2] = {about_blurb, additional_copyrights};
	HWND hEdit[2];
	TEXTRANGEW tr;
	ENLINK* enl;
	wchar_t wUrl[256];

	switch (message) {
	case WM_INITDIALOG:
		dialog_showing++;
		set_title_bar_icon(hDlg);
		center_dialog(hDlg);
		if (reg_commcheck)
			ShowWindow(GetDlgItem(hDlg, IDC_ABOUT_UPDATES), SW_SHOW);
		safe_sprintf(about_blurb, sizeof(about_blurb), about_blurb_format,
			application_version[0], application_version[1], application_version[2]);
		for (i=0; i<ARRAYSIZE(hEdit); i++) {
			hEdit[i] = GetDlgItem(hDlg, edit_id[i]);
			SendMessage(hEdit[i], EM_AUTOURLDETECT, 1, 0);
			/* Can't use SetDlgItemText, because it only works with RichEdit20A... and VS insists
			 * on reverting to RichEdit20W as soon as you edit the dialog. You can try all the W
			 * methods you want, it JUST WON'T WORK unless you use EM_SETTEXTEX. Also see:
			 * http://blog.kowalczyk.info/article/eny/Setting-unicode-rtf-text-in-rich-edit-control.html */
			SendMessageA(hEdit[i], EM_SETTEXTEX, (WPARAM)&friggin_microsoft_unicode_amateurs, (LPARAM)edit_text[i]);
			SendMessage(hEdit[i], EM_SETSEL, -1, -1);
			SendMessage(hEdit[i], EM_SETEVENTMASK, 0, ENM_LINK);
			SendMessage(hEdit[i], EM_SETBKGNDCOLOR, 0, (LPARAM)GetSysColor(COLOR_BTNFACE));
		}
		break;
	case WM_NOTIFY:
		switch (((LPNMHDR)lParam)->code) {
		case EN_LINK:
			enl = (ENLINK*) lParam;
			if (enl->msg == WM_LBUTTONUP) {
				tr.lpstrText = wUrl;
				tr.chrg.cpMin = enl->chrg.cpMin;
				tr.chrg.cpMax = enl->chrg.cpMax;
				SendMessageW(enl->nmhdr.hwndFrom, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
				wUrl[ARRAYSIZE(wUrl)-1] = 0;
				ShellExecuteW(hDlg, L"open", wUrl, NULL, NULL, SW_SHOWNORMAL);
			}
			break;
		}
		break;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK:
		case IDCANCEL:
			EndDialog(hDlg, LOWORD(wParam));
			dialog_showing--;
			return (INT_PTR)TRUE;
		case IDC_ABOUT_LICENSE:
			DialogBoxW(main_instance, MAKEINTRESOURCEW(IDD_LICENSE), hDlg, license_callback);
			break;
		case IDC_ABOUT_UPDATES:
			DialogBoxW(main_instance, MAKEINTRESOURCEW(IDD_UPDATE_POLICY), hDlg, UpdateCallback);
			break;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

/*
 * We use our own MessageBox for notifications to have greater control (center, no close button, etc)
 */
INT_PTR CALLBACK notification_callback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	LRESULT loc;
	int i;
	// Prevent resizing
	static LRESULT disabled[9] = { HTLEFT, HTRIGHT, HTTOP, HTBOTTOM, HTSIZE,
		HTTOPLEFT, HTTOPRIGHT, HTBOTTOMLEFT, HTBOTTOMRIGHT };
	static HBRUSH white_brush, separator_brush;

	switch (message) {
	case WM_INITDIALOG:
		white_brush = CreateSolidBrush(WHITE);
		separator_brush = CreateSolidBrush(SEPARATOR_GREY);
		set_title_bar_icon(hDlg);
		center_dialog(hDlg);
		// Change the default icon
		if (Static_SetIcon(GetDlgItem(hDlg, IDC_NOTIFICATION_ICON), hMessageIcon) == 0) {
			dprintf("Could not set dialog icon\n");
		}
		// Set the dialog title
		if (szMessageTitle != NULL) {
			SetWindowTextU(hDlg, szMessageTitle);
		}
		// Enable/disable the buttons and set text
		if (!notification_is_question) {
			SetWindowTextU(GetDlgItem(hDlg, IDNO), "Close");
		} else {
			ShowWindow(GetDlgItem(hDlg, IDYES), SW_SHOW);
		}
		if ((notification_more_info != NULL) && (notification_more_info->callback != NULL)) {
			ShowWindow(GetDlgItem(hDlg, IDC_MORE_INFO), SW_SHOW);
		}
		// Set the control text
		if (szMessageText != NULL) {
			SetWindowTextU(GetDlgItem(hDlg, IDC_NOTIFICATION_TEXT), szMessageText);
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
		case IDYES:
		case IDNO:
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		case IDC_MORE_INFO:
			if (notification_more_info != NULL)
				DialogBoxW(main_instance, MAKEINTRESOURCEW(notification_more_info->id),
					hDlg, notification_more_info->callback);
			break;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

/*
 * Display a custom notification
 */
BOOL notification(int type, const notification_info* more_info, char* title, char* format, ...)
{
	BOOL ret;
	va_list args;

	dialog_showing++;
	szMessageText = (char*)malloc(MAX_PATH);
	if (szMessageText == NULL) return FALSE;
	szMessageTitle = title;
	va_start(args, format);
	safe_vsnprintf(szMessageText, MAX_PATH-1, format, args);
	va_end(args);
	szMessageText[MAX_PATH-1] = 0;
	notification_more_info = more_info;
	notification_is_question = FALSE;

	switch(type) {
	case MSG_WARNING:
		hMessageIcon = LoadIcon(NULL, IDI_WARNING);
		break;
	case MSG_ERROR:
		hMessageIcon = LoadIcon(NULL, IDI_ERROR);
		break;
	case MSG_QUESTION:
		hMessageIcon = LoadIcon(NULL, IDI_QUESTION);
		notification_is_question = TRUE;
		break;
	case MSG_INFO:
	default:
		hMessageIcon = LoadIcon(NULL, IDI_INFORMATION);
		break;
	}
	ret = (DialogBox(main_instance, MAKEINTRESOURCE(IDD_NOTIFICATION), hMainDialog, notification_callback) == IDYES);
	safe_free(szMessageText);
	dialog_showing--;
	return ret;
}

struct {
	HWND hTip;
	WNDPROC original_proc;
	LPWSTR wstring;
} ttlist[MAX_TOOLTIPS] = { {0} };

INT_PTR CALLBACK tooltip_callback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	LPNMTTDISPINFOW lpnmtdi;
	int i = MAX_TOOLTIPS;

	// Make sure we have an original proc
	for (i=0; i<MAX_TOOLTIPS; i++) {
		if (ttlist[i].hTip == hDlg) break;
	}
	if (i == MAX_TOOLTIPS) {
		return (INT_PTR)FALSE;
	}

	switch (message)
	{
	case WM_NOTIFY:
		switch (((LPNMHDR)lParam)->code) {
		case TTN_GETDISPINFOW:
			lpnmtdi = (LPNMTTDISPINFOW)lParam;
			lpnmtdi->lpszText = ttlist[i].wstring;
			SendMessage(hDlg, TTM_SETMAXTIPWIDTH, 0, 300);
			return (INT_PTR)TRUE;
		}
		break;
	}
	return CallWindowProc(ttlist[i].original_proc, hDlg, message, wParam, lParam);
}

/*
 * Create a tooltip for the control passed as first parameter
 * duration sets the duration in ms. Use -1 for default
 * message is an UTF-8 string
 */
HWND create_tooltip(HWND hControl, char* message, int duration)
{
	TOOLINFOW toolInfo = {0};
	int i;

	if ( (hControl == NULL) || (message == NULL) ) {
		return (HWND)NULL;
	}

	// Find an empty slot
	for (i=0; i<MAX_TOOLTIPS; i++) {
		if (ttlist[i].hTip == NULL) break;
	}
	if (i == MAX_TOOLTIPS) {
		return (HWND)NULL; // No more space
	}

	// Create the tooltip window
	ttlist[i].hTip = CreateWindowExW(0, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hMainDialog, NULL,
		main_instance, NULL);

	if (ttlist[i].hTip == NULL) {
		return (HWND)NULL;
	}

	// Subclass the tooltip to handle multiline
	ttlist[i].original_proc = (WNDPROC)SetWindowLongPtr(ttlist[i].hTip, GWLP_WNDPROC, (LONG_PTR)tooltip_callback);

	// Set the string to display (can be multiline)
	ttlist[i].wstring = utf8_to_wchar(message);

	// Set tooltip duration (ms)
	PostMessage(ttlist[i].hTip, TTM_SETDELAYTIME, (WPARAM)TTDT_AUTOPOP, (LPARAM)duration);

	// Associate the tooltip to the control
	toolInfo.cbSize = sizeof(toolInfo);
	toolInfo.hwnd = ttlist[i].hTip;	// Set to the tooltip itself to ease up subclassing
	toolInfo.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
	toolInfo.uId = (UINT_PTR)hControl;
	toolInfo.lpszText = LPSTR_TEXTCALLBACKW;
	SendMessageW(ttlist[i].hTip, TTM_ADDTOOLW, 0, (LPARAM)&toolInfo);

	return ttlist[i].hTip;
}

void destroy_tooltip(HWND hWnd)
{
	int i;

	if (hWnd == NULL) return;
	for (i=0; i<MAX_TOOLTIPS; i++) {
		if (ttlist[i].hTip == hWnd) break;
	}
	if (i == MAX_TOOLTIPS) return;
	DestroyWindow(hWnd);
	safe_free(ttlist[i].wstring);
	ttlist[i].original_proc = NULL;
	ttlist[i].hTip = NULL;
}

void destroy_all_tooltips(void)
{
	int i;

	for (i=0; i<MAX_TOOLTIPS; i++) {
		if (ttlist[i].hTip == NULL) continue;
		DestroyWindow(ttlist[i].hTip);
		safe_free(ttlist[i].wstring);
	}
}

/*
 * Update policy and settings dialog callback
 */
INT_PTR CALLBACK UpdateCallback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	HWND hPolicy;
	static HWND hFrequency, hBeta;
	int32_t freq;
	char update_policy_text[4096];

	switch (message) {
	case WM_INITDIALOG:
		set_title_bar_icon(hDlg);
		center_dialog(hDlg);
		hFrequency = GetDlgItem(hDlg, IDC_UPDATE_FREQUENCY);
		hBeta = GetDlgItem(hDlg, IDC_INCLUDE_BETAS);
		IGNORE_RETVAL(ComboBox_SetItemData(hFrequency, ComboBox_AddStringU(hFrequency, "Disabled"), -1));
		IGNORE_RETVAL(ComboBox_SetItemData(hFrequency, ComboBox_AddStringU(hFrequency, "Daily (Default)"), 86400));
		IGNORE_RETVAL(ComboBox_SetItemData(hFrequency, ComboBox_AddStringU(hFrequency, "Weekly"), 604800));
		IGNORE_RETVAL(ComboBox_SetItemData(hFrequency, ComboBox_AddStringU(hFrequency, "Monthly"), 2629800));
		freq = ReadRegistryKey32(REGKEY_HKCU, REGKEY_UPDATE_INTERVAL);
		EnableWindow(GetDlgItem(hDlg, IDC_CHECK_NOW), (freq != 0));
		EnableWindow(hBeta, (freq >= 0));
		switch(freq) {
		case -1:
			IGNORE_RETVAL(ComboBox_SetCurSel(hFrequency, 0));
			break;
		case 0:
		case 86400:
			IGNORE_RETVAL(ComboBox_SetCurSel(hFrequency, 1));
			break;
		case 604800:
			IGNORE_RETVAL(ComboBox_SetCurSel(hFrequency, 2));
			break;
		case 2629800:
			IGNORE_RETVAL(ComboBox_SetCurSel(hFrequency, 3));
			break;
		default:
			IGNORE_RETVAL(ComboBox_SetItemData(hFrequency, ComboBox_AddStringU(hFrequency, "Custom"), freq));
			IGNORE_RETVAL(ComboBox_SetCurSel(hFrequency, 4));
			break;
		}
		IGNORE_RETVAL(ComboBox_AddStringU(hBeta, "Yes"));
		IGNORE_RETVAL(ComboBox_AddStringU(hBeta, "No"));
		IGNORE_RETVAL(ComboBox_SetCurSel(hBeta, GetRegistryKeyBool(REGKEY_HKCU, REGKEY_INCLUDE_BETAS)?0:1));
		hPolicy = GetDlgItem(hDlg, IDC_POLICY);
		SendMessage(hPolicy, EM_AUTOURLDETECT, 1, 0);
		safe_sprintf(update_policy_text, sizeof(update_policy_text), update_policy);
		SendMessageA(hPolicy, EM_SETTEXTEX, (WPARAM)&friggin_microsoft_unicode_amateurs, (LPARAM)update_policy_text);
		SendMessage(hPolicy, EM_SETSEL, -1, -1);
		SendMessage(hPolicy, EM_SETEVENTMASK, 0, ENM_LINK);
		SendMessageA(hPolicy, EM_SETBKGNDCOLOR, 0, (LPARAM)GetSysColor(COLOR_BTNFACE));
		break;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDCLOSE:
		case IDCANCEL:
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		case IDC_CHECK_NOW:
			CheckForUpdates(TRUE);
			return (INT_PTR)TRUE;
		case IDC_UPDATE_FREQUENCY:
			if (HIWORD(wParam) != CBN_SELCHANGE)
				break;
			freq = (int32_t)ComboBox_GetItemData(hFrequency, ComboBox_GetCurSel(hFrequency));
			WriteRegistryKey32(REGKEY_HKCU, REGKEY_UPDATE_INTERVAL, (DWORD)freq);
			EnableWindow(hBeta, (freq >= 0));
			return (INT_PTR)TRUE;
		case IDC_INCLUDE_BETAS:
			if (HIWORD(wParam) != CBN_SELCHANGE)
				break;
			SetRegistryKeyBool(REGKEY_HKCU, REGKEY_INCLUDE_BETAS, ComboBox_GetCurSel(hBeta) == 0);
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

/*
 * Initial update check setup
 */
BOOL SetUpdateCheck(void)
{
	BOOL enable_updates;
	DWORD commcheck = GetTickCount();
	notification_info more_info = { IDD_UPDATE_POLICY, UpdateCallback };
	char filename[MAX_PATH] = "", exename[] = APPLICATION_NAME ".exe";
	size_t fn_len, exe_len;

	// Test if we have access to the registry. If not, forget it.
	WriteRegistryKey32(REGKEY_HKCU, REGKEY_COMM_CHECK, commcheck);
	if (ReadRegistryKey32(REGKEY_HKCU, REGKEY_COMM_CHECK) != commcheck)
		return FALSE;
	reg_commcheck = TRUE;

	// If the update interval is not set, this is the first time we run so prompt the user
	if (ReadRegistryKey32(REGKEY_HKCU, REGKEY_UPDATE_INTERVAL) == 0) {

		// Add a hack for people who'd prefer the app not to prompt about update settings on first run.
		// If the executable is called "<app_name>.exe", without version, we disable the prompt
		GetModuleFileNameU(NULL, filename, sizeof(filename));
		fn_len = safe_strlen(filename);
		exe_len = safe_strlen(exename);
		if ((fn_len > exe_len) && (safe_stricmp(&filename[fn_len-exe_len], exename) == 0)) {
			dprintf("Short name used - Disabling initial update policy prompt\n");
			enable_updates = TRUE;
		} else {
			enable_updates = notification(MSG_QUESTION, &more_info, APPLICATION_NAME " update policy",
				"Do you want to allow " APPLICATION_NAME " to check for application updates online?");
		}
		if (!enable_updates) {
			WriteRegistryKey32(REGKEY_HKCU, REGKEY_UPDATE_INTERVAL, -1);
			return FALSE;
		}
		// If the user hasn't set the interval in the dialog, set to default
		if ( (ReadRegistryKey32(REGKEY_HKCU, REGKEY_UPDATE_INTERVAL) == 0) ||
			 ((ReadRegistryKey32(REGKEY_HKCU, REGKEY_UPDATE_INTERVAL) == -1) && enable_updates) )
			WriteRegistryKey32(REGKEY_HKCU, REGKEY_UPDATE_INTERVAL, 86400);
	}
	return TRUE;
}

static void CreateStaticFont(HDC dc, HFONT* hyperlink_font) {
	TEXTMETRIC tm;
	LOGFONT lf;

	if (*hyperlink_font != NULL)
		return;
	GetTextMetrics(dc, &tm);
	lf.lfHeight = tm.tmHeight;
	lf.lfWidth = 0;
	lf.lfEscapement = 0;
	lf.lfOrientation = 0;
	lf.lfWeight = tm.tmWeight;
	lf.lfItalic = tm.tmItalic;
	lf.lfUnderline = TRUE;
	lf.lfStrikeOut = tm.tmStruckOut;
	lf.lfCharSet = tm.tmCharSet;
	lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
	lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
	lf.lfQuality = DEFAULT_QUALITY;
	lf.lfPitchAndFamily = tm.tmPitchAndFamily;
	GetTextFace(dc, LF_FACESIZE, lf.lfFaceName);
	*hyperlink_font = CreateFontIndirect(&lf);
}

/*
 * Work around the limitations of edit control, to display a hand cursor for hyperlinks
 * NB: The LTEXT control must have SS_NOTIFY attribute for this to work
 */
static INT_PTR CALLBACK subclass_callback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_SETCURSOR:
		if ((HWND)wParam == GetDlgItem(hDlg, IDC_WEBSITE)) {
			SetCursor(LoadCursor(NULL, IDC_HAND));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return CallWindowProc(original_wndproc, hDlg, message, wParam, lParam);
}

/*
 * New version notification dialog
 */
INT_PTR CALLBACK new_version_callback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	int i;
	HWND hNotes;
	char tmp[128], cmdline[] = APPLICATION_NAME " /W";
	static char* filepath = NULL;
	static int download_status = 0;
	STARTUPINFOA si;
	PROCESS_INFORMATION pi;
	HFONT hyperlink_font = NULL;
	EXT_DECL(exe_ext, NULL, __VA_GROUP__("*.exe"), __VA_GROUP__("Application"));

	switch (message) {
	case WM_INITDIALOG:
		download_status = 0;
		set_title_bar_icon(hDlg);
		center_dialog(hDlg);
		// Subclass the callback so that we can change the cursor
		original_wndproc = (WNDPROC)SetWindowLongPtr(hDlg, GWLP_WNDPROC, (LONG_PTR)subclass_callback);
		hNotes = GetDlgItem(hDlg, IDC_RELEASE_NOTES);
		SendMessage(hNotes, EM_AUTOURLDETECT, 1, 0);
		SendMessageA(hNotes, EM_SETTEXTEX, (WPARAM)&friggin_microsoft_unicode_amateurs, (LPARAM)update.release_notes);
		SendMessage(hNotes, EM_SETSEL, -1, -1);
		SendMessage(hNotes, EM_SETEVENTMASK, 0, ENM_LINK);
		safe_sprintf(tmp, sizeof(tmp), "Your version: %d.%d (Build %d)",
			application_version[0], application_version[1], application_version[2]);
		SetWindowTextU(GetDlgItem(hDlg, IDC_YOUR_VERSION), tmp);
		safe_sprintf(tmp, sizeof(tmp), "Latest version: %d.%d (Build %d)",
			update.version[0], update.version[1], update.version[2]);
		SetWindowTextU(GetDlgItem(hDlg, IDC_LATEST_VERSION), tmp);
		SetWindowTextU(GetDlgItem(hDlg, IDC_DOWNLOAD_URL), update.download_url);
		SendMessage(GetDlgItem(hDlg, IDC_PROGRESS), PBM_SETRANGE, 0, (MAX_PROGRESS<<16) & 0xFFFF0000);
		if (update.download_url == NULL)
			EnableWindow(GetDlgItem(hDlg, IDC_DOWNLOAD), FALSE);
		break;
	case WM_CTLCOLORSTATIC:
		if ((HWND)lParam != GetDlgItem(hDlg, IDC_WEBSITE))
			return FALSE;
		// Change the font for the hyperlink
		SetBkMode((HDC)wParam, TRANSPARENT);
		CreateStaticFont((HDC)wParam, &hyperlink_font);
		SelectObject((HDC)wParam, hyperlink_font);
		SetTextColor((HDC)wParam, RGB(0,0,125));	// DARK_BLUE
		return (INT_PTR)CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDCLOSE:
		case IDCANCEL:
			if (download_status != 1) {
				safe_free(filepath);
				EndDialog(hDlg, LOWORD(wParam));
			}
			return (INT_PTR)TRUE;
		case IDC_WEBSITE:
			ShellExecuteA(hDlg, "open", APPLICATION_URL, NULL, NULL, SW_SHOWNORMAL);
			break;
		case IDC_DOWNLOAD:	// Also doubles as abort and launch function
			switch(download_status) {
			case 1:		// Abort
				download_status = 0;
				error_code = ERROR_SEVERITY_ERROR|ERROR_CANCELLED;
				break;
			case 2:		// Launch newer version and close this one
				Sleep(1000);	// Add a delay on account of antivirus scanners
				memset(&si, 0, sizeof(si));
				memset(&pi, 0, sizeof(pi));
				si.cb = sizeof(si);
				if (!CreateProcessU(filepath, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
					print_status(0, TRUE, "Failed to launch new application");
					dprintf("Failed to launch new application: %s\n", WindowsErrorString());
				} else {
					print_status(0, FALSE, "Launching new application...");
					PostMessage(hDlg, WM_COMMAND, (WPARAM)IDCLOSE, 0);
					PostMessage(hMainDialog, WM_CLOSE, 0, 0);
				}
				break;
			default:	// Download
				if (update.download_url == NULL) {
					print_status(0, TRUE, "Could not get download URL\n");
					break;
				}
				for (i=(int)strlen(update.download_url); (i>0)&&(update.download_url[i]!='/'); i--);
				exe_ext.filename = PathFindFileNameU(update.download_url);
				filepath = FileDialog(TRUE, app_dir, &exe_ext, OFN_NOCHANGEDIR);
				if (filepath == NULL) {
					print_status(0, TRUE, "Could not get save path\n");
					break;
				}
				DownloadFileThreaded(update.download_url, filepath, hDlg);
				break;
			}
			return (INT_PTR)TRUE;
		}
		break;
	case UM_DOWNLOAD_INIT:
		error_code = 0;
		download_status = 1;
		SetWindowTextU(GetDlgItem(hDlg, IDC_DOWNLOAD), "Abort");
		return (INT_PTR)TRUE;
	case UM_DOWNLOAD_EXIT:
		if (wParam) {
			SetWindowTextU(GetDlgItem(hDlg, IDC_DOWNLOAD), "Launch");
			download_status = 2;
		} else {
			SetWindowTextU(GetDlgItem(hDlg, IDC_DOWNLOAD), "Download");
			download_status = 0;
		}
		return (INT_PTR)TRUE;
	}
	return (INT_PTR)FALSE;
}

void download_new_version(void)
{
	DialogBoxW(main_instance, MAKEINTRESOURCEW(IDD_NEW_VERSION), hMainDialog, new_version_callback);
}

void set_title_bar_icon(HWND hDlg)
{
	HDC hDC;
	int i16, s16, s32;
	HICON hSmallIcon, hBigIcon;

	// High DPI scaling
	i16 = GetSystemMetrics(SM_CXSMICON);
	hDC = GetDC(hDlg);
	fScale = GetDeviceCaps(hDC, LOGPIXELSX) / 96.0f;
	ReleaseDC(hDlg, hDC);
	// Adjust icon size lookup
	s16 = i16;
	s32 = (int)(32.0f*fScale);
	if (s16 >= 54)
		s16 = 64;
	else if (s16 >= 40)
		s16 = 48;
	else if (s16 >= 28)
		s16 = 32;
	else if (s16 >= 20)
		s16 = 24;
	if (s32 >= 54)
		s32 = 64;
	else if (s32 >= 40)
		s32 = 48;
	else if (s32 >= 28)
		s32 = 32;
	else if (s32 >= 20)
		s32 = 24;

	// Create the title bar icon
	hSmallIcon = (HICON)LoadImage(main_instance, MAKEINTRESOURCE(IDI_ZADIG), IMAGE_ICON, s16, s16, 0);
	SendMessage (hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hSmallIcon);
	hBigIcon = (HICON)LoadImage(main_instance, MAKEINTRESOURCE(IDI_ZADIG), IMAGE_ICON, s32, s32, 0);
	SendMessage (hDlg, WM_SETICON, ICON_BIG, (LPARAM)hBigIcon);
}
