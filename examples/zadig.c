/*
 * Zadig: Automated Driver Installer for USB devices (GUI version)
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

/*
 * WARNING: if any part of the resulting executable name contains "setup" or "instal(l)"
 * it will require UAC elevation on Vista and later, and, when run from a cygwin/MSYS
 * shell, will produce a "sh: Bad file number" message.
 * See the paragraph on Automatic Elevation at http://helpware.net/VistaCompat.htm
 */

#include <windows.h>
#include <windowsx.h>
#include <stdlib.h>
#include <stdio.h>
#include <io.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <process.h>
#include <shlobj.h>

//#include <dwmapi.h>

#include "../libwdi/libwdi.h"

#include "resource.h"
#include "zadig.h"

#define dclear() SendDlgItemMessage(hMain, IDC_INFO, LB_RESETCONTENT, 0, 0)
#define dprintf(...) w_printf(IDC_INFO, __VA_ARGS__)

#define INF_NAME "libusb_device.inf"

#define EX_STYLE    (WS_EX_TOOLWINDOW | WS_EX_WINDOWEDGE | WS_EX_STATICEDGE | WS_EX_APPWINDOW)
#define COMBO_STYLE (WS_CHILD | WS_VISIBLE | CBS_AUTOHSCROLL | WS_VSCROLL | WS_TABSTOP | CBS_NOINTEGRALHEIGHT)

/*
 * Globals
 */
static HINSTANCE main_instance;
static HWND hDeviceList;
static HWND hDriver;
static HWND hMain;
static HMENU hMenu;
char   path[MAX_PATH];

/*
 * On screen logging
 */
void w_printf_v(HWND hWnd, const char *format, va_list args)
{
	char str[STR_BUFFER_SIZE];
	int size;

	size = safe_vsnprintf(str, STR_BUFFER_SIZE, format, args);
	if (size < 0) {
		str[STR_BUFFER_SIZE-1] = 0;
	}
	Edit_SetSel(hWnd, -1, -1);
	Edit_ReplaceSel(hWnd, str);
}

void w_printf(int dialog, const char *format, ...)
{
	va_list args;
	HWND hWnd;

	hWnd = GetDlgItem(hMain, dialog);

	va_start (args, format);
	w_printf_v(hWnd, format, args);
	va_end (args);
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
 * Populate the USB device list
 */
int display_devices(struct wdi_device_info* list)
{
	struct wdi_device_info *device;
	int index = -1;
	int junk;
	HDC hdc;
	SIZE size;
	LONG max_width = 0;

	hdc = GetDC(hDeviceList);
	junk = ComboBox_ResetContent(hDeviceList);

	for (device = list; device != NULL; device = device->next) {
		// The dropdown width needs to accomodate our text
		GetTextExtentPoint(hdc, device->desc, (int)strlen(device->desc)+1, &size);
		max_width = max(max_width, size.cx);

		index = ComboBox_AddString(hDeviceList, device->desc);
		if ((index != CB_ERR) && (index != CB_ERRSPACE)) {
			junk = ComboBox_SetItemData(hDeviceList, index, (LPARAM) device);
		} else {
			dprintf("could not populate dropdown list past device #%d\n", index);
		}
	}

	SendMessage(hDeviceList, CB_SETCURSEL, 0, 0);
	SendMessage(hDeviceList, CB_SETDROPPEDWIDTH, max_width, 0);

	return index;
}

/*
 * Get the device pointer of current selection
 */
struct wdi_device_info* get_selected_device(void)
{
	struct wdi_device_info *device = NULL;
	int index;
	index = (int) SendDlgItemMessage(hMain, IDC_DEVICELIST, CB_GETCURSEL, 0, 0);
	if (index != CB_ERR) {
		// Use the device pointers as dropdown values for easy access
		device = (struct wdi_device_info*) SendDlgItemMessage(hMain, IDC_DEVICELIST,
			CB_GETITEMDATA, index, 0);
	}
	return device;
}

/*
 * The lengths you need to go through just to change a combobox style...
 */
void combo_breaker(DWORD type)
{
	RECT rect, rect2;
	POINT point;
	int junk;

	GetClientRect(hDeviceList, &rect);
	GetWindowRect(hDeviceList, &rect2);
	point.x = rect2.left;
	point.y = rect2.top;
	ScreenToClient(hMain, &point);
	junk = ComboBox_ResetContent(hDeviceList);
	DestroyWindow(hDeviceList);

	hDeviceList = CreateWindowEx(0, "COMBOBOX", "", COMBO_STYLE | type,
		point.x, point.y, rect.right, rect.bottom*((type==CBS_SIMPLE)?1:8),
		hMain, (HMENU)IDC_DEVICELIST, main_instance, NULL);
}

/*
 * Thread that sends a device event notification back to our dialog after a delay
 */
void __cdecl notification_delay_thread(void* param)
{
	DWORD delay = (DWORD)(uintptr_t)param;
	Sleep(delay);
	PostMessage(hMain, UM_DEVICE_EVENT, 0, 0);
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

	hr = SHCreateItemFromParsingName(wpath, NULL, &IID_IShellItem, (LPVOID) &si_path);
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
 * Perform the driver installation
 * dev: currently selected device (will be ignored if create new is selected)
 */
void install_driver(struct wdi_device_info *dev)
{
	struct wdi_device_info* device = dev;
	static char str_buf[STR_BUFFER_SIZE];
	int tmp;

	if (IsDlgButtonChecked(hMain, IDC_CREATE) == BST_CHECKED) {
		device = calloc(1, sizeof(struct wdi_device_info));
		if (device != NULL) {
			GetDlgItemText(hMain, IDC_DEVICELIST, str_buf, STR_BUFFER_SIZE);
			device->desc = safe_strdup(str_buf);
			GetDlgItemText(hMain, IDC_VID, str_buf, STR_BUFFER_SIZE);
			// TODO: use custom scanf for hex
			if (sscanf(str_buf, "%4x", &tmp) != 1) {
				dprintf("could not convert VID string - aborting\n");
				return;
			}
			device->vid = (unsigned short)tmp;
			GetDlgItemText(hMain, IDC_PID, str_buf, STR_BUFFER_SIZE);
			if (sscanf(str_buf, "%4x", &tmp) != 1) {
				dprintf("could not convert PID string - aborting\n");
				return;
			}
			device->pid = (unsigned short)tmp;
			GetDlgItemText(hMain, IDC_MI, str_buf, STR_BUFFER_SIZE);
			if ( (strlen(str_buf) != 0)
			  && (sscanf(str_buf, "%2x", &tmp) == 1) ) {
				device->mi = (short)tmp;
			} else {
				device->mi = -1;
			}
		}
	}
	GetDlgItemText(hMain, IDC_FOLDER, path, MAX_PATH);
	if (wdi_create_inf(device, path, INF_NAME, WDI_WINUSB) == 0) {
		dprintf("Extracted driver files to %s\n", path);
		if (wdi_install_driver(device, path, INF_NAME) == 0) {
			dprintf("SUCCESS\n");
		} else {
			dprintf("DRIVER INSTALLATION FAILED\n");
		}
	} else {
		dprintf("Could not create/extract files in %s\n", str_buf);
	}
}

/*
 * About dialog callback
 */
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_INITDIALOG:
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

/*
 * Main dialog callback
 */
INT_PTR CALLBACK main_callback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	static struct wdi_device_info *device, *list = NULL;
	static bool list_driverless_only = true;
	static char* editable_desc = NULL;
	static HANDLE delay_thread = NULL;
	char str_tmp[5];
	char log_buf[STR_BUFFER_SIZE];
	int nb_devices, junk;
	DWORD delay;

	switch (message) {

	case WM_DEVICECHANGE:
		/*
		 * Why the convoluted process on device notification?
		 * 1. When not using RegisterDeviceNotification(), Windows sends an undefined number
		 * of WM_DEVICECHANGE events in rapid sequence, all with the exact SAME wParam/lParam
		 * so that we cannot differentiate between them. Notifying on each of those would
		 * bother the user too much.
		 * 2. When using RegisterDeviceNotification(), it is possible to get unique
		 * WM_DEVICECHANGE events but only for devices that already have a driver, because
		 * there is no device interface class for unknown/driverless devices and Microsoft
		 * has not publicized any way of doing so, it is NOT possible to get a single notifi-
		 * cation event for insertion/removal of devices that don't have a driver.
		 * Our solution then is to initiate delayed notification thread on the first
		 * WM_DEVICECHANGE message we receive, and wait for this thread to send a user defined
		 * event back to our main callback.
		 */
		if (delay_thread == NULL) {
			delay = NOTIFICATION_DELAY;
			delay_thread = (HANDLE)_beginthread(notification_delay_thread, 0, (void*)(uintptr_t)delay);
			if (delay_thread == NULL) {
				dprintf("Unable to create notification delay thread - notification events will be disabled\n");
			}
		}
		break;

	case UM_DEVICE_EVENT:
		// TODO: don't handle these events when installation has started!
		delay_thread = NULL;
		if (IsDlgButtonChecked(hMain, IDC_CREATE) == BST_CHECKED) {
			if (MessageBox(hMain, "The device list has changed.\n"
				"Do you want to refresh the list\n"
				"and lose all your modifications?", "Device Event Notification",
				MB_YESNO | MB_ICONINFORMATION) == IDYES) {
				CheckDlgButton(hMain, IDC_CREATE, BST_UNCHECKED);
				EnableWindow(GetDlgItem(hMain, IDC_DRIVERLESSONLY), true);
				combo_breaker(CBS_DROPDOWNLIST);
				PostMessage(hMain, UM_REFRESH_LIST, 0, 0);
			}
		} else {
			PostMessage(hMain, UM_REFRESH_LIST, 0, 0);
		}
		break;

	case UM_LOGGER_EVENT:
		// TODO: use different colours according to the log level?
//		dprintf("log level: %d\n", wParam);
		if (wdi_read_logger(log_buf, STR_BUFFER_SIZE) != 0) {
			dprintf("%s\n", log_buf);
		} else {
			dprintf("wdi_read_logger returned 0\n");
		}
		break;

	case WM_INITDIALOG:
		// Quite a burden to carry around as parameters
		hMain = hDlg;
		hDeviceList = GetDlgItem(hDlg, IDC_DEVICELIST);
		hDriver = GetDlgItem(hDlg, IDC_DRIVER);
		hMenu = GetSubMenu(GetMenu(hDlg), 0);

		// Initialize COM for folder selection
		CoInitialize(NULL);

		SetDlgItemText(hMain, IDC_FOLDER, "C:\\test");
		CheckDlgButton(hMain, IDC_DRIVERLESSONLY, list_driverless_only?BST_CHECKED:BST_UNCHECKED);
		// Try without... and lament for the lack of consistancy of MS controls.
		combo_breaker(CBS_DROPDOWNLIST);

		wdi_register_logger(hMain, UM_LOGGER_EVENT);
		wdi_set_log_level(LOG_LEVEL_DEBUG);

		// Fall through
	case UM_REFRESH_LIST:
		// TODO: replace with a manual clear button
		dclear();

		if (list != NULL) wdi_destroy_list(list);
		list = wdi_create_list(list_driverless_only);
		if (list != NULL) {
			nb_devices = display_devices(list);
			dprintf("%d device%s found.\n", nb_devices+1, (nb_devices>0)?"s":"");
			// Send a dropdown selection message to update fields
			PostMessage(hMain, WM_COMMAND, MAKELONG(IDC_DEVICELIST, CBN_SELCHANGE),
				(LPARAM) hDeviceList);
		} else {
			junk = ComboBox_ResetContent(hDeviceList);
			SetDlgItemText(hMain, IDC_VID, "");
			SetDlgItemText(hMain, IDC_PID, "");
			SetDlgItemText(hMain, IDC_MI, "");
			SetDlgItemText(hMain, IDC_DRIVER, "");
			EnableWindow(GetDlgItem(hMain, IDC_EDITNAME), false);
			dprintf("No device found.\n");
		}
		break;

	case WM_COMMAND:
		switch(LOWORD(wParam)) {
		case IDC_DRIVERLESSONLY:	// checkbox: "List Only Driverless Devices"
			list_driverless_only = (IsDlgButtonChecked(hMain, IDC_DRIVERLESSONLY) == BST_CHECKED);
			// Reset Edit button
			CheckDlgButton(hMain, IDC_EDITNAME, BST_UNCHECKED);
			// Reset Combo
			combo_breaker(CBS_DROPDOWNLIST);
			PostMessage(hMain, UM_REFRESH_LIST, 0, 0);
			break;
		case IDC_EDITNAME:			// checkbox: "Edit Device Name"
			if (IsDlgButtonChecked(hMain, IDC_EDITNAME) == BST_CHECKED) {
				combo_breaker(CBS_SIMPLE);
				if (device->desc != editable_desc) {
					editable_desc = malloc(STR_BUFFER_SIZE);
					if (editable_desc == NULL) {
						// TODO
					} else {
						safe_strcpy(editable_desc, STR_BUFFER_SIZE, device->desc);
						free(device->desc);	// No longer needed
						device->desc = editable_desc;
					}
				}
				junk = ComboBox_AddString(hDeviceList, editable_desc);
				SendMessage(hDeviceList, CB_SETCURSEL, 0, 0);
				PostMessage(hDeviceList, WM_SETFOCUS, 0, 0);
			} else {

				combo_breaker(CBS_DROPDOWNLIST);
				display_devices(list);
			}
			break;
		case IDC_CREATE:			// checkbox: "Non Listed Device (Create)"
			if (IsDlgButtonChecked(hMain, IDC_CREATE) == BST_CHECKED) {
				combo_breaker(CBS_SIMPLE);
				EnableWindow(GetDlgItem(hMain, IDC_EDITNAME), false);
				EnableWindow(GetDlgItem(hMain, IDC_DRIVERLESSONLY), false);
				SetDlgItemText(hMain, IDC_VID, "");
				SetDlgItemText(hMain, IDC_PID, "");
				SetDlgItemText(hMain, IDC_MI, "");
				SetDlgItemText(hMain, IDC_DRIVER, "");
				EnableWindow(GetDlgItem(hMain, IDC_PID), true);
				EnableWindow(GetDlgItem(hMain, IDC_VID), true);
				EnableWindow(GetDlgItem(hMain, IDC_MI), true);
				PostMessage(hDeviceList, WM_SETFOCUS, 0, 0);
			} else {
				EnableWindow(GetDlgItem(hMain, IDC_PID), false);
				EnableWindow(GetDlgItem(hMain, IDC_VID), false);
				EnableWindow(GetDlgItem(hMain, IDC_MI), false);
				EnableWindow(GetDlgItem(hMain, IDC_DRIVERLESSONLY), true);
				PostMessage(hMain, WM_COMMAND, MAKELONG(IDC_DRIVERLESSONLY, CBN_SELCHANGE), 0);
			}
			break;
		case IDC_DEVICELIST:		// dropdown/field: device description
			switch (HIWORD(wParam)) {
			case CBN_SELCHANGE:
				device = get_selected_device();
				if (device != NULL) {
					// Change the description string if needed
					if (device->desc == NULL) {
						editable_desc = malloc(STR_BUFFER_SIZE);
						if (editable_desc == NULL) {
							// TODO
						} else {
							safe_sprintf(editable_desc, STR_BUFFER_SIZE, "(Unknown Device)");
							device->desc = editable_desc;
						}
					}
					if (device->driver != NULL) {
						SendMessage(hDriver, WM_SETTEXT, 0, (LPARAM)device->driver);
					} else {
						SendMessage(hDriver, WM_SETTEXT, 0, (LPARAM)"(NONE)");
					}
					safe_sprintf(str_tmp, 5, "%04X", device->vid);
					SetDlgItemText(hMain, IDC_VID, str_tmp);
					safe_sprintf(str_tmp, 5, "%04X", device->pid);
					SetDlgItemText(hMain, IDC_PID, str_tmp);
					if (device->mi >= 0) {
						safe_sprintf(str_tmp, 5, "%02X", device->mi);
						SetDlgItemText(hMain, IDC_MI, str_tmp);
					}
					EnableWindow(GetDlgItem(hMain, IDC_EDITNAME), true);
				}
				break;
			case CBN_EDITCHANGE:
				ComboBox_GetText(hDeviceList, editable_desc, STR_BUFFER_SIZE);
				break;
			default:
				break;
			}
			break;
		case IDC_INSTALL:	// button: Install
			install_driver(device);
			break;
		case IDC_BROWSE:	// button: Browse
			browse_for_folder();
			break;
		case IDOK:
		case IDCANCEL:
			wdi_destroy_list(list);
			EndDialog(hDlg, 0);
			break;
		// Menus
		case IDM_ABOUT:
			DialogBox(main_instance, MAKEINTRESOURCE(IDD_ABOUTBOX), hMain, About);
			break;
		case IDM_BASICMODE:
			CheckMenuItem(hMenu, IDM_BASICMODE, MF_CHECKED);
			CheckMenuItem(hMenu, IDM_ADVANCEDMODE, MF_UNCHECKED);
			// TODO: switch dialog
			break;
		case IDM_ADVANCEDMODE:
			CheckMenuItem(hMenu, IDM_ADVANCEDMODE, MF_CHECKED);
			CheckMenuItem(hMenu, IDM_BASICMODE, MF_UNCHECKED);
			// TODO: switch dialog
			break;
		default:
			break;
		}
		break;

	default:
		break;

	}
	return FALSE;
}

/*
 * Application Entrypoint
 */
int __stdcall WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{

	// Save instance of the application for further reference
	main_instance = hInstance;

	// Create the main Window
	if (DialogBox(hInstance, "MAIN_DIALOG", NULL, main_callback) == -1) {
		MessageBox(NULL, "Could not create Window", "DialogBox failure", MB_ICONSTOP);
	}

	return (0);
}

