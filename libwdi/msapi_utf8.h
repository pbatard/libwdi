/*
 * MSAPI_UTF8: Common API calls using UTF-8 strings
 * Compensating for what Microsoft should have done a long long time ago.
 *
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
#include <shlobj.h>
#include <commdlg.h>

#define wchar_to_utf8_no_alloc(wsrc, dest, dest_size) \
	WideCharToMultiByte(CP_UTF8, 0, wsrc, -1, dest, dest_size, NULL, NULL)
#define utf8_to_wchar_no_alloc(src, wdest, wdest_size) \
	MultiByteToWideChar(CP_UTF8, 0, src, -1, wdest, wdest_size)
#define ufree(p) do {if (p != NULL) {free((void*)(p)); p = NULL;}} while(0)
#define Edit_ReplaceSelU(hCtrl, str) ((void)SendMessageLU(hCtrl, EM_REPLACESEL, (WPARAM)FALSE, str))
#define ComboBox_AddStringU(hCtrl, str) ((int)(DWORD)SendMessageLU(hCtrl, CB_ADDSTRING, (WPARAM)FALSE, str))
#define ComboBox_GetTextU(hCtrl, str, max_str) GetWindowTextU(hCtrl, str, max_str)
#define GetSaveFileNameU(p) GetOpenSaveFileNameU(p, TRUE)
#define GetOpenFileNameU(p) GetOpenSaveFileNameU(p, FALSE)

#define wconvert(p)     wchar_t* w ## p = utf8_to_wchar(p)
#define walloc(p, size) wchar_t* w ## p = calloc(size, sizeof(wchar_t))


/*
 * Converts an UTF-16 string to UTF8 (allocate returned string)
 * Returns NULL on error
 */
static __inline char* wchar_to_utf8(const wchar_t* wstr)
{
	int size;
	char* str;

	// Find out the size we need to allocate for our converted string
	size = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
	if (size <= 1)	// An empty string would be size 1
		return NULL;

	if ((str = malloc(size)) == NULL)
		return NULL;

	if (wchar_to_utf8_no_alloc(wstr, str, size) != size) {
		free(str);
		return NULL;
	}

	return str;
}

/*
 * Converts an UTF8 string to UTF-16 (allocate returned string)
 * Returns NULL on error
 */
static __inline wchar_t* utf8_to_wchar(const char* str)
{
	int size;
	wchar_t* wstr;

	// Find out the size we need to allocate for our converted string
	size = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
	if (size <= 1)	// An empty string would be size 1
		return NULL;

	if ((wstr = (wchar_t*) malloc(2*size)) == NULL)
		return NULL;

	if (utf8_to_wchar_no_alloc(str, wstr, size) != size) {
		free(wstr);
		return NULL;
	}
	return wstr;
}

static __inline DWORD FormatMessageU(DWORD dwFlags, LPCVOID lpSource, DWORD dwMessageId,
									 DWORD dwLanguageId, char* lpBuffer, DWORD nSize, va_list *Arguments)
{
	DWORD ret, err;
	walloc(lpBuffer, nSize);
	ret = FormatMessageW(dwFlags, lpSource, dwMessageId, dwLanguageId, wlpBuffer, nSize, Arguments);
	err = GetLastError();
	if ((ret != 0) && ((ret = wchar_to_utf8_no_alloc(wlpBuffer, lpBuffer, nSize)) == 0)) {
		err = GetLastError();
		ret = 0;
	}
	ufree(wlpBuffer);
	SetLastError(err);
	return ret;
}

// SendMessage, with LPARAM as UTF-8 string
static __inline LRESULT SendMessageLU(HWND hWnd, UINT Msg, WPARAM wParam, const char* lParam)
{
	LRESULT ret;
	DWORD err;
	wconvert(lParam);
	ret = SendMessageW(hWnd, Msg, wParam, (LPARAM)wlParam);
	err = GetLastError();
	ufree(wlParam);
	SetLastError(err);
	return ret;
}

static __inline BOOL SHGetPathFromIDListU(LPCITEMIDLIST pidl, char* pszPath)
{
	BOOL ret;
	DWORD err;
	walloc(pszPath, MAX_PATH);
	ret = SHGetPathFromIDListW(pidl, wpszPath);
	err = GetLastError();
	if ((ret) && (wchar_to_utf8_no_alloc(wpszPath, pszPath, MAX_PATH) == 0)) {
		err = GetLastError();
		ret = FALSE;
	}
	ufree(wpszPath);
	SetLastError(err);
	return ret;
}

static __inline int GetWindowTextU(HWND hWnd, char* lpString, int nMaxCount)
{
	UINT ret;
	DWORD err;
	walloc(lpString, nMaxCount);
	ret = GetWindowTextW(hWnd, wlpString, nMaxCount);
	err = GetLastError();
	if ( (ret != 0) && ((ret = wchar_to_utf8_no_alloc(wlpString, lpString, nMaxCount)) == 0) ) {
		err = GetLastError();
	}
	ufree(wlpString);
	SetLastError(err);
	return ret;
}

static __inline int SetWindowTextU(HWND hWnd, const char* lpString)
{
	UINT ret;
	DWORD err;
	wconvert(lpString);
	ret = SetWindowTextW(hWnd, wlpString);
	err = GetLastError();
	ufree(wlpString);
	SetLastError(err);
	return ret;
}

static __inline int GetWindowTextLengthU(HWND hWnd)
{
	int ret;
	DWORD err;
	wchar_t* wbuf = NULL;
	char* buf = NULL;

	ret = GetWindowTextLengthW(hWnd);
	err = GetLastError();
	if (ret == 0) goto out;
	wbuf = calloc(ret, sizeof(wchar_t));
	err = GetLastError();
	if (wbuf == NULL) {
		ret = 0; goto out;
	}
	ret = GetWindowTextW(hWnd, wbuf, ret);
	err = GetLastError();
	if (ret == 0) goto out;
	buf = wchar_to_utf8(wbuf);
	err = GetLastError();
	if (buf == NULL) {
		ret = 0; goto out;
	}
	ret = (int)strlen(buf) + 2;	// GetDlgItemText seems to add a character
	err = GetLastError();
out:
	ufree(wbuf);
	ufree(buf);
	return ret;
}

static __inline UINT GetDlgItemTextU(HWND hDlg, int nIDDlgItem, char* lpString, int nMaxCount)
{
	UINT ret;
	DWORD err;
	walloc(lpString, nMaxCount);
	ret = GetDlgItemTextW(hDlg, nIDDlgItem, wlpString, nMaxCount);
	err = GetLastError();
	if ((ret != 0) && ((ret = wchar_to_utf8_no_alloc(wlpString, lpString, nMaxCount)) == 0)) {
		err = GetLastError();
	}
	ufree(wlpString);
	SetLastError(err);
	return ret;
}

static __inline BOOL SetDlgItemTextU(HWND hDlg, int nIDDlgItem, const char* lpString)
{
	BOOL ret;
	DWORD err;
	wconvert(lpString);
	ret = SetDlgItemTextW(hDlg, nIDDlgItem, wlpString);
	err = GetLastError();
	ufree(wlpString);
	SetLastError(err);
	return ret;
}

static __inline HANDLE CreateFileU(const char* lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
								   LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
								   DWORD dwFlagsAndAttributes,  HANDLE hTemplateFile)
{
	HANDLE ret;
	DWORD err;
	wconvert(lpFileName);
	ret = CreateFileW(wlpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
		dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
	err = GetLastError();
	ufree(wlpFileName);
	SetLastError(err);
	return ret;
}

// This function differs from regular GetTextExtentPoint in that it uses a zero terminated string
static __inline BOOL GetTextExtentPointU(HDC hdc, const char* lpString, LPSIZE lpSize)
{
	BOOL ret;
	DWORD err;
	wconvert(lpString);
	ret = GetTextExtentPointW(hdc, wlpString, (int)wcslen(wlpString)+1, lpSize);
	err = GetLastError();
	ufree(wlpString);
	SetLastError(err);
	return ret;
}

static __inline DWORD GetCurrentDirectoryU(DWORD nBufferLength, char* lpBuffer)
{
	DWORD ret, err;
	walloc(lpBuffer, nBufferLength);
	ret = GetCurrentDirectoryW(nBufferLength, wlpBuffer);
	err = GetLastError();
	if ((ret !=0) && ((ret = wchar_to_utf8_no_alloc(wlpBuffer, lpBuffer, nBufferLength)) == 0)) {
		err = GetLastError();
	}
	ufree(wlpBuffer);
	SetLastError(err);
	return ret;
}

static __inline DWORD GetFileAttributesU(const char* lpFileName)
{
	DWORD ret, err;
	wconvert(lpFileName);
	ret = GetFileAttributesW(wlpFileName);
	err = GetLastError();
	ufree(wlpFileName);
	SetLastError(err);
	return ret;
}

static __inline BOOL WINAPI GetOpenSaveFileNameU(LPOPENFILENAMEA lpofn, BOOL save)
{
	BOOL ret;
	DWORD err;
	size_t i, len;
	OPENFILENAMEW wofn;
	memset(&wofn, 0, sizeof(wofn));
	wofn.lStructSize = sizeof(wofn);
	wofn.hwndOwner = lpofn->hwndOwner;
	wofn.hInstance = lpofn->hInstance;
	// Count on Microsoft to use an moronic scheme for filters
	// that relies on NULL separators and double NULL terminators
	if (lpofn->lpstrFilter != NULL) {
		// Replace the NULLs by something that can be converted
		for (i=0; ; i++) {
			if (lpofn->lpstrFilter[i] == 0) {
				((char*)lpofn->lpstrFilter)[i] = '\r';
				if (lpofn->lpstrFilter[i+1] == 0) {
					break;
				}
			}
		}
		wofn.lpstrFilter = utf8_to_wchar(lpofn->lpstrFilter);
		// And revert
		len = wcslen(wofn.lpstrFilter);	// don't use in the loop as it would be reevaluated
		for (i=0; i<len; i++) {
			if (wofn.lpstrFilter[i] == '\r') {
				((wchar_t*)wofn.lpstrFilter)[i] = 0;
			}
		}
		len = strlen(lpofn->lpstrFilter);
		for (i=0; i<len; i++) {
			if (lpofn->lpstrFilter[i] == '\r') {
				((char*)lpofn->lpstrFilter)[i] = 0;
			}
		}
	} else {
		wofn.lpstrFilter = NULL;
	}
	// TODO: NULL pair buffer
	wofn.lpstrCustomFilter = utf8_to_wchar(lpofn->lpstrCustomFilter);
	wofn.nMaxCustFilter = lpofn->nMaxCustFilter;
	wofn.nFilterIndex = lpofn->nFilterIndex;
	wofn.lpstrFile = calloc(lpofn->nMaxFile, sizeof(wchar_t));
	utf8_to_wchar_no_alloc(lpofn->lpstrFile, wofn.lpstrFile, lpofn->nMaxFile);
	wofn.nMaxFile = lpofn->nMaxFile;
	// TODO: buffer
	wofn.lpstrFileTitle = utf8_to_wchar(lpofn->lpstrFileTitle);
	wofn.nMaxFileTitle = lpofn->nMaxFileTitle; // TODO
	wofn.lpstrInitialDir = utf8_to_wchar(lpofn->lpstrInitialDir);
	wofn.lpstrTitle = utf8_to_wchar(lpofn->lpstrTitle);
	wofn.Flags = lpofn->Flags;
	// TODO: find the backslash
	wofn.nFileOffset = lpofn->nFileOffset;
	// TODO: fidn the dot
	wofn.nFileExtension = lpofn->nFileExtension; //TODO
	wofn.lpstrDefExt = utf8_to_wchar(lpofn->lpstrDefExt);
	wofn.lCustData = lpofn->lCustData;
	wofn.lpfnHook = lpofn->lpfnHook;
	wofn.lpTemplateName = utf8_to_wchar(lpofn->lpTemplateName);
	wofn.pvReserved = lpofn->pvReserved;
	wofn.dwReserved = lpofn->dwReserved;
	wofn.FlagsEx = lpofn->FlagsEx;

	if (save) {
		ret = GetSaveFileNameW(&wofn);
	} else {
		ret = GetOpenFileNameW(&wofn);
	}
	err = GetLastError();
	if ((ret) && (wchar_to_utf8_no_alloc(wofn.lpstrFile, lpofn->lpstrFile, lpofn->nMaxFile) == 0)) {
		err = GetLastError();
		ret = FALSE;
	}
	ufree(wofn.lpstrCustomFilter);
	ufree(wofn.lpstrDefExt);
	ufree(wofn.lpstrFile);
	ufree(wofn.lpstrFileTitle);
	ufree(wofn.lpstrFilter);
	ufree(wofn.lpstrInitialDir);
	ufree(wofn.lpstrTitle);
	ufree(wofn.lpTemplateName);
	SetLastError(err);
	return ret;
}
