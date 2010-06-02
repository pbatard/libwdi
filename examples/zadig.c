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

// Enable Visual Styles, so that our application looks good on all platforms
// http://msdn.microsoft.com/en-us/library/bb773175%28v=VS.85%29.aspx
// NB: This only works with /D "ISOLATION_AWARE_ENABLED"
#if defined(_MSC_VER)
// MSVC doesn't actually use our manifest file, so provide the dep as a pragma
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif
#include <windows.h>
#include <windowsx.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <objbase.h>
#include <process.h>
#include <shellapi.h>
#include <commctrl.h>

#include "../libwdi/libwdi.h"
#include "resource.h"
#include "zadig.h"

#define NOT_DURING_INSTALL if (install_thread != NULL) return FALSE

/*
 * Globals
 */
HINSTANCE main_instance;
HWND hDeviceList;
HWND hDriver;
HWND hMain;
HWND hInfo;
HWND hStatus;
HMENU hMenuDevice;
HMENU hMenuOptions;
char extraction_path[MAX_PATH];
char* driver_display_name[WDI_NB_DRIVERS] = { "WinUSB.sys (Default)", "libusb0.sys" };
int driver_type = WDI_NB_DRIVERS-1;
HANDLE install_thread = NULL;
struct wdi_device_info *device, *list = NULL;
char* editable_desc = NULL;
// Application states
bool advanced_mode = false;
bool create_device = false;
bool extract_only = false;
bool from_install = false;
bool list_driverless_only = true;

/*
 * On screen logging and status
 */
void w_printf_v(bool update_status, const char *format, va_list args)
{
	char str[STR_BUFFER_SIZE];
	int size;

	size = safe_vsnprintf(str, STR_BUFFER_SIZE, format, args);
	if (size < 0) {
		str[STR_BUFFER_SIZE-1] = 0;
	}
	Edit_SetSel(hInfo, -1, -1);
	Edit_ReplaceSel(hInfo, str);
	if (update_status) {
		SetDlgItemText(hMain, IDC_STATUS, str);
	}
}

void w_printf(bool update_status, const char *format, ...)
{
	va_list args;

	va_start (args, format);
	w_printf_v(update_status, format, args);
	va_end (args);
}

/*
 * Populate the USB device list
 */
int display_devices(void)
{
	struct wdi_device_info *dev;
	int junk, index = -1;
	HDC hdc;
	SIZE size;
	LONG max_width = 0;

	hdc = GetDC(hDeviceList);
	junk = ComboBox_ResetContent(hDeviceList);

	for (dev = list; dev != NULL; dev = dev->next) {
		// Compute the width needed to accomodate our text
		GetTextExtentPoint(hdc, dev->desc, (int)strlen(dev->desc)+1, &size);
		max_width = max(max_width, size.cx);

		index = ComboBox_AddString(hDeviceList, dev->desc);
		if ((index != CB_ERR) && (index != CB_ERRSPACE)) {
			junk = ComboBox_SetItemData(hDeviceList, index, (LPARAM)dev);
		} else {
			dprintf("could not populate dropdown list past device #%d\n", index);
		}
	}

	// Select first entry
	SendMessage(hDeviceList, CB_SETCURSEL, 0, 0);
	// Set the width to computed value
	SendMessage(hDeviceList, CB_SETDROPPEDWIDTH, max_width, 0);

	return index;
}

/*
 * Get the device pointer of current selection
 */
struct wdi_device_info* get_selected_device(void)
{
	struct wdi_device_info *dev = NULL;
	int index;
	index = (int) SendDlgItemMessage(hMain, IDC_DEVICELIST, CB_GETCURSEL, 0, 0);
	if (index != CB_ERR) {
		// Use the device pointers as dropdown values for easy access
		dev = (struct wdi_device_info*) SendDlgItemMessage(hMain, IDC_DEVICELIST,
			CB_GETITEMDATA, index, 0);
	}
	return dev;
}

/*
 * This thread is used to send an event notification back to our app
 * param: a DWORD delay, in ms
 */
void __cdecl notification_delay_thread(void* param)
{
	DWORD delay = (DWORD)(uintptr_t)param;
	Sleep(delay);
	PostMessage(hMain, UM_DEVICE_EVENT, 0, 0);
	_endthread();
}

// Retrieve the driver type according to its service string
int get_driver_type(struct wdi_device_info* dev)
{
	const char* winusb_name = "WinUSB";
	const char* libusb_name = "libusb";
	const char* usbstor_name = "USBSTOR";
	const char* hidusb_name = "HidUsb";

	if ((dev == NULL) || (dev->driver == NULL)) {
		return DT_NONE;
	}
	if ( (safe_strcmp(dev->driver, winusb_name) == 0)
	  || (safe_strcmp(dev->driver, libusb_name) == 0) ) {
		return DT_LIBUSB;
	}
	if ( (safe_strcmp(dev->driver, usbstor_name) == 0)
	  || (safe_strcmp(dev->driver, hidusb_name) == 0) ) {
		return DT_SYSTEM;
	}
	return DT_UNKNOWN;
}

/*
 * Thread that performs the driver installation
 * param: a pointer to the currently selected wdi_device_info structure
 */
void __cdecl install_driver_thread(void* param)
{
	struct wdi_device_info* dev = (struct wdi_device_info*)(uintptr_t)param;
	static char str_buf[STR_BUFFER_SIZE];
	bool need_dealloc = false;
	int r, tmp;

	if (IsDlgButtonChecked(hMain, IDC_CREATE) == BST_CHECKED) {
		// If the device is created friom scratch, ignore the parameter
		dev = calloc(1, sizeof(struct wdi_device_info));
		if (dev == NULL) {
			dprintf("could not create new device_info struct for installation\n");
			install_thread = NULL;
			_endthread();
		}
		need_dealloc = true;

		// Retrieve the various device parameters
		// TODO: actuall test creation!
		GetDlgItemText(hMain, IDC_DEVICELIST, str_buf, STR_BUFFER_SIZE);
		dev->desc = safe_strdup(str_buf);
		GetDlgItemText(hMain, IDC_VID, str_buf, STR_BUFFER_SIZE);
		// TODO: use custom scanf for hex
		if (sscanf(str_buf, "%4x", &tmp) != 1) {
			dprintf("could not convert VID string - aborting\n");
			return;
		}
		dev->vid = (unsigned short)tmp;
		GetDlgItemText(hMain, IDC_PID, str_buf, STR_BUFFER_SIZE);
		if (sscanf(str_buf, "%4x", &tmp) != 1) {
			dprintf("could not convert PID string - aborting\n");
			return;
		}
		dev->pid = (unsigned short)tmp;
		GetDlgItemText(hMain, IDC_MI, str_buf, STR_BUFFER_SIZE);
		if ( (strlen(str_buf) != 0)
		  && (sscanf(str_buf, "%2x", &tmp) == 1) ) {
			dev->is_composite = true;
			dev->mi = (unsigned char)tmp;
		} else {
			dev->is_composite = false;
			dev->mi = 0;
		}
	}

	// Perform extraction/installation
	GetDlgItemText(hMain, IDC_FOLDER, extraction_path, MAX_PATH);
	if (wdi_create_inf(dev, extraction_path, INF_NAME, driver_type) == WDI_SUCCESS) {
		dsprintf("Succesfully extracted driver files to %s\n", extraction_path);
		// Perform the install if not extracting the files only
		if (!extract_only) {
			if ( (get_driver_type(dev) == DT_SYSTEM)
			  && (MessageBox(hMain, "You are about to replace a system driver.\n"
					"Are you sure this is what you want?", "Warning - System Driver",
					MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDNO) ) {
				goto out;
			}
			toggle_busy();
			dsprintf("Installing driver. Please wait...\n");
			r = wdi_install_driver(dev, extraction_path, INF_NAME);
			if (r == WDI_SUCCESS) {
				dsprintf("Driver Installation: SUCCESS\n");
			} else if (r == WDI_ERROR_USER_CANCEL) {
				dsprintf("Driver Installation: Cancelled by User\n");
			} else {
				dsprintf("Driver Installation: FAILED (%s)\n", wdi_strerror(r));
			}
			toggle_busy();
			PostMessage(hMain, WM_DEVICECHANGE, 0, 0);	// Force a refresh
		}
	} else {
		dsprintf("Could not create/extract files in %s\n", str_buf);
	}
out:
	if (need_dealloc) {
		free(dev);
	}
	install_thread = NULL;
	from_install = true;
	_endthread();
}

/*
 * Toggle between combo and edit
 */
void combo_breaker(bool edit)
{
	if (edit) {
		ShowWindow(GetDlgItem(hMain, IDC_DEVICELIST), SW_HIDE);
		ShowWindow(GetDlgItem(hMain, IDC_DEVICEEDIT), SW_SHOW);
	} else {
		ShowWindow(GetDlgItem(hMain, IDC_DEVICEEDIT), SW_HIDE);
		ShowWindow(GetDlgItem(hMain, IDC_DEVICELIST), SW_SHOW);
	}
}

/*
 * Select the next available target driver
 * increment: go through the list up or down
 */
bool select_next_driver(bool increment)
{
	int i;
	bool found = false;

	for (i=0; i<WDI_NB_DRIVERS; i++) {	// don't loop forever
		driver_type = (WDI_NB_DRIVERS + driver_type +
			(increment?1:-1))%WDI_NB_DRIVERS;
		if (!wdi_is_driver_supported(driver_type)) {
			continue;
		}
		found = true;
		break;
	}
	SetDlgItemText(hMain, IDC_TARGET,
		found?driver_display_name[driver_type]:"(NONE)");
	return found;
}

// Hide or Show the MI/Driver fields
void display_driver(bool show)
{
	int cmd = show?SW_SHOW:SW_HIDE;
	ShowWindow(GetDlgItem(hMain, IDC_DRIVER), cmd);
	ShowWindow(GetDlgItem(hMain, IDC_STATIC_DRIVER), cmd);
}

void display_mi(bool show)
{
	int cmd = show?SW_SHOW:SW_HIDE;
	ShowWindow(GetDlgItem(hMain, IDC_MI), cmd);
	ShowWindow(GetDlgItem(hMain, IDC_STATIC_MI), cmd);
}


/*
 * Application state functions
 */

// Toggle "advanced" mode
void toggle_advanced(void)
{
	// How much in y should we move/reduce our controls around
	const int install_shift = 62;
	const int dialog_shift = 385;
	RECT rect;
	POINT point;
	int toggle;

	advanced_mode = !(GetMenuState(hMenuOptions, IDM_ADVANCEDMODE, MF_CHECKED) & MF_CHECKED);

	// Increase or decrease the Window size
	GetWindowRect(hMain, &rect);
	point.x = (rect.right - rect.left);
	point.y = (rect.bottom - rect.top);
	MoveWindow(hMain, rect.left, rect.top, point.x,
		point.y + (advanced_mode?dialog_shift:-dialog_shift) , TRUE);

	// Move the install button up or down
	GetWindowRect(GetDlgItem(hMain, IDC_INSTALL), &rect);
	point.x = rect.left;
	point.y = rect.top;
	ScreenToClient(hMain, &point);
	GetClientRect(GetDlgItem(hMain, IDC_INSTALL), &rect);
	MoveWindow(GetDlgItem(hMain, IDC_INSTALL), point.x,
		point.y + (advanced_mode?install_shift:-install_shift),
		rect.right, rect.bottom, TRUE);

	// Move the status bar up or down
	GetWindowRect(hStatus, &rect);
	point.x = rect.left;
	point.y = rect.top;
	ScreenToClient(hMain, &point);
	GetClientRect(hStatus, &rect);
	MoveWindow(hStatus, point.x, point.y + (advanced_mode?dialog_shift:-dialog_shift),
		(rect.right - rect.left), (rect.bottom - rect.top), TRUE);

	// Hide or show the various advanced options
	toggle = advanced_mode?SW_SHOW:SW_HIDE;
	ShowWindow(GetDlgItem(hMain, IDC_EXTRACTONLY), toggle);
	ShowWindow(GetDlgItem(hMain, IDC_CREATE), toggle);
	ShowWindow(GetDlgItem(hMain, IDC_DRIVERLESSONLY), toggle);
	ShowWindow(GetDlgItem(hMain, IDC_BROWSE), toggle);
	ShowWindow(GetDlgItem(hMain, IDC_FOLDER), toggle);
	ShowWindow(GetDlgItem(hMain, IDC_STATIC_FOLDER), toggle);
	ShowWindow(GetDlgItem(hMain, IDC_TARGET), toggle);
	ShowWindow(GetDlgItem(hMain, IDC_TARGETSPIN), toggle);
	ShowWindow(GetDlgItem(hMain, IDC_STATIC_TARGET), toggle);

	// Toggle the menu checkmark
	CheckMenuItem(hMenuOptions, IDM_ADVANCEDMODE, advanced_mode?MF_CHECKED:MF_UNCHECKED);
}

// Toggle edit description
void toggle_edit(void)
{
	if (IsDlgButtonChecked(hMain, IDC_EDITNAME) == BST_CHECKED) {
		combo_breaker(true);
		if (editable_desc != NULL) {
			dprintf("program assertion failed - editable_desc != NULL\n");
			return;
		}
		editable_desc = malloc(STR_BUFFER_SIZE);
		if (editable_desc == NULL) {
			dprintf("could not allocate buffer to edit description\n");
			CheckDlgButton(hMain, IDC_EDITNAME, BST_UNCHECKED);
			combo_breaker(false);
			return;
		}
		safe_strcpy(editable_desc, STR_BUFFER_SIZE, device->desc);
		free(device->desc);	// No longer needed
		device->desc = editable_desc;
		SetDlgItemText(hMain, IDC_DEVICEEDIT, editable_desc);
		SetFocus(GetDlgItem(hMain, IDC_DEVICEEDIT));
	} else {
		combo_breaker(false);
		display_devices();
		editable_desc = NULL;
	}
}

// Toggle device creation mode
void toggle_create(bool refresh)
{
	create_device = !(GetMenuState(hMenuDevice, IDM_CREATE, MF_CHECKED) & MF_CHECKED);
	if (create_device) {
		// Disable Edit Desc. if selected
		if (IsDlgButtonChecked(hMain, IDC_EDITNAME) == BST_CHECKED) {
			CheckDlgButton(hMain, IDC_EDITNAME, BST_UNCHECKED);
			toggle_edit();
		}
		combo_breaker(true);
		EnableWindow(GetDlgItem(hMain, IDC_EDITNAME), false);
		EnableWindow(GetDlgItem(hMain, IDC_DRIVERLESSONLY), false);
		SetDlgItemText(hMain, IDC_VID, "");
		SetDlgItemText(hMain, IDC_PID, "");
		SetDlgItemText(hMain, IDC_MI, "");
		SetDlgItemText(hMain, IDC_DEVICEEDIT, "");
		PostMessage(GetDlgItem(hMain, IDC_VID), EM_SETREADONLY, (WPARAM)FALSE, 0);
		PostMessage(GetDlgItem(hMain, IDC_PID), EM_SETREADONLY, (WPARAM)FALSE, 0);
		PostMessage(GetDlgItem(hMain, IDC_MI), EM_SETREADONLY, (WPARAM)FALSE, 0);
		display_mi(true);
		display_driver(false);
		SetFocus(GetDlgItem(hMain, IDC_DEVICEEDIT));
	} else {
		combo_breaker(false);
		PostMessage(GetDlgItem(hMain, IDC_VID), EM_SETREADONLY, (WPARAM)TRUE, 0);
		PostMessage(GetDlgItem(hMain, IDC_PID), EM_SETREADONLY, (WPARAM)TRUE, 0);
		PostMessage(GetDlgItem(hMain, IDC_MI), EM_SETREADONLY, (WPARAM)TRUE, 0);
		if (refresh) {
			PostMessage(hMain, UM_REFRESH_LIST, 0, 0);
		}
	}
	CheckMenuItem(hMenuDevice, IDM_CREATE, create_device?MF_CHECKED:MF_UNCHECKED);
}

// Toggle files extraction mode
void toggle_extract(void)
{
	extract_only = !(GetMenuState(hMenuOptions, IDM_EXTRACT, MF_CHECKED) & MF_CHECKED);
	CheckMenuItem(hMenuOptions, IDM_EXTRACT, extract_only?MF_CHECKED:MF_UNCHECKED);
	SetDlgItemText(hMain, IDC_INSTALL, extract_only?"Extract Files":"Install Driver");
}

// Toggle driverless device listing
void toggle_driverless(void)
{
	list_driverless_only = !(GetMenuState(hMenuOptions, IDM_DRIVERLESSONLY, MF_CHECKED) & MF_CHECKED);

	if (create_device) {
		toggle_create(true);
	}

	CheckMenuItem(hMenuOptions, IDM_DRIVERLESSONLY, list_driverless_only?MF_CHECKED:MF_UNCHECKED);
	// Reset Edit button
	CheckDlgButton(hMain, IDC_EDITNAME, BST_UNCHECKED);
	// Reset Combo
	combo_breaker(false);
	PostMessage(hMain, UM_REFRESH_LIST, 0, 0);
}

void init_dialog(HWND hDlg)
{
	// Quite a burden to carry around as parameters
	hMain = hDlg;
	hDeviceList = GetDlgItem(hDlg, IDC_DEVICELIST);
	hDriver = GetDlgItem(hDlg, IDC_DRIVER);
	hInfo = GetDlgItem(hDlg, IDC_INFO);
	hMenuDevice = GetSubMenu(GetMenu(hDlg), 0);
	hMenuOptions = GetSubMenu(GetMenu(hDlg), 1);

	// Create the status line
	create_status_bar();

	// The application always starts in advanced mode
	CheckMenuItem(hMenuOptions, IDM_ADVANCEDMODE, MF_CHECKED);

	// Switch to basic mode if needed
	if (!advanced_mode) {
		toggle_advanced();
	}

	// Setup logging
	wdi_register_logger(hMain, UM_LOGGER_EVENT);
	wdi_set_log_level(LOG_LEVEL_DEBUG);
	// Increase the size of our log textbox to 64 KB
	PostMessage(hInfo, EM_LIMITTEXT, 0xFFFF, 0);

	// Limit the input size of VID, PID, MI
	PostMessage(GetDlgItem(hMain, IDC_VID), EM_SETLIMITTEXT, 4, 0);
	PostMessage(GetDlgItem(hMain, IDC_PID), EM_SETLIMITTEXT, 4, 0);
	PostMessage(GetDlgItem(hMain, IDC_MI), EM_SETLIMITTEXT, 2, 0);

	// Set the default extraction dir
	SetDlgItemText(hMain, IDC_FOLDER, DEFAULT_DIR);
}


/*
 * Main dialog callback
 */
INT_PTR CALLBACK main_callback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	static HANDLE delay_thread = NULL;
	static DWORD last_scroll = 0;
	char str_tmp[5];
	char log_buf[STR_BUFFER_SIZE];
	char* log_buffer;
	int nb_devices, junk, r, log_size;
	DWORD delay, read_size;

	// The following local variables are used to change the visual aspect of the fields
	static HWND hDeviceEdit;
	static HWND hVid, hPid, hMi;
	static HWND hFolder, hDriver, hTarget;
	static HBRUSH white_brush = (HBRUSH)FALSE;
	static HBRUSH green_brush = (HBRUSH)FALSE;
	static HBRUSH red_brush = (HBRUSH)FALSE;
	static HBRUSH driver_background[NB_DRIVER_TYPES];

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
		return TRUE;

	case UM_DEVICE_EVENT:
		delay_thread = NULL;
		// Don't handle these events when installation has started
		NOT_DURING_INSTALL;
		if (create_device) {
			if (MessageBox(hMain, "An USB device has been plugged or unplugged.\n"
				"Do you want to refresh the application?\n(you will lose all your modifications)",
				"USB Event Notification", MB_YESNO | MB_ICONINFORMATION) == IDYES) {
				if (create_device) {
					toggle_create(false);
				}
				CheckMenuItem(hMenuDevice, IDM_CREATE, MF_UNCHECKED);
				combo_breaker(false);
				PostMessage(hMain, UM_REFRESH_LIST, 0, 0);
			}
		} else {
			PostMessage(hMain, UM_REFRESH_LIST, 0, 0);
		}
		return TRUE;

	case UM_LOGGER_EVENT:
		// TODO: use different colours according to the log level?
//		dprintf("log level: %d\n", wParam);
		r = wdi_read_logger(log_buf, STR_BUFFER_SIZE, &read_size);
		if (r == WDI_SUCCESS) {
			dprintf("%s\n", log_buf);
		} else {
			dprintf("wdi_read_logger: error %s\n", wdi_strerror(r));
		}
		return TRUE;

	case WM_INITDIALOG:
		// Setup local visual variables
		white_brush = CreateSolidBrush(WHITE);
		green_brush = CreateSolidBrush(GREEN);
		red_brush = CreateSolidBrush(RED);
		driver_background[DT_NONE] = white_brush;
		driver_background[DT_LIBUSB] = green_brush;
		driver_background[DT_SYSTEM] = red_brush;
		driver_background[DT_UNKNOWN] = (HBRUSH)FALSE;

		// Speedup checks for WM_CTLCOLOR
		hDeviceEdit = GetDlgItem(hDlg, IDC_DEVICEEDIT);
		hVid = GetDlgItem(hDlg, IDC_VID);
		hPid = GetDlgItem(hDlg, IDC_PID);
		hMi = GetDlgItem(hDlg, IDC_MI);
		hDriver = GetDlgItem(hDlg, IDC_DRIVER);
		hTarget = GetDlgItem(hDlg, IDC_TARGET);
		hFolder = GetDlgItem(hDlg, IDC_FOLDER);

		// Main init
		init_dialog(hDlg);

		// Fall through
	case UM_REFRESH_LIST:
		NOT_DURING_INSTALL;
		// Reset edit mode if selected
		if (IsDlgButtonChecked(hMain, IDC_EDITNAME) == BST_CHECKED) {
			combo_breaker(false);
			CheckDlgButton(hMain, IDC_EDITNAME, BST_UNCHECKED);
		}
		if (list != NULL) wdi_destroy_list(list);
		r = wdi_create_list(&list, list_driverless_only);
		if (r == WDI_SUCCESS) {
			nb_devices = display_devices();
			// Send a dropdown selection message to update fields
			PostMessage(hMain, WM_COMMAND, MAKELONG(IDC_DEVICELIST, CBN_SELCHANGE),
				(LPARAM) hDeviceList);
		} else {
			nb_devices = -1;
			junk = ComboBox_ResetContent(hDeviceList);
			SetDlgItemText(hMain, IDC_VID, "");
			SetDlgItemText(hMain, IDC_PID, "");
			display_driver(false);
			display_mi(false);
			EnableWindow(GetDlgItem(hMain, IDC_EDITNAME), false);
		}
		// Make sure we don't override the install status on refresh from install
		if (!from_install) {
			dsprintf("%d device%s found.\n", nb_devices+1, (nb_devices>0)?"s":"");
		} else {
			dprintf("%d device%s found.\n", nb_devices+1, (nb_devices>0)?"s":"");
			from_install = false;
		}
		return TRUE;

	case WM_VSCROLL:
		// TODO: ability to scroll existing driver text
		NOT_DURING_INSTALL;
		if (LOWORD(wParam) == 4) {
			if (!select_next_driver(HIWORD(wParam) <= last_scroll)) {
				dprintf("no driver is selectable in libwdi!");
			}
			last_scroll = HIWORD(wParam);
			return TRUE;
		}
		return FALSE;

	// Change the font colour of editable fields to dark blue
	case WM_CTLCOLOREDIT:
		if ( ((HWND)lParam == hDeviceEdit)
		  || ((HWND)lParam == hVid)
		  || ((HWND)lParam == hPid)
		  || ((HWND)lParam == hMi)
		  || ((HWND)lParam == hFolder) ) {
			SetTextColor((HDC)wParam, DARK_BLUE);
			return (INT_PTR)white_brush;
		}
		return (INT_PTR)FALSE;

	// Set background colour of read only fields to white
	case WM_CTLCOLORSTATIC:
		if ( ((HWND)lParam == hVid)
		  || ((HWND)lParam == hPid)
		  || ((HWND)lParam == hMi)
		  || ((HWND)lParam == hTarget) ) {
			return (INT_PTR)white_brush;
		} else if ((HWND)lParam == hDriver) {
			return (INT_PTR)driver_background[get_driver_type(device)];
		}
		return (INT_PTR)FALSE;

	case WM_COMMAND:
		NOT_DURING_INSTALL;
		switch(LOWORD(wParam)) {
		case IDC_EDITNAME:			// checkbox: "Edit Desc."
			toggle_edit();
			break;
		case IDC_DEVICEEDIT:		// edit: device description
			switch (HIWORD(wParam)) {
			case EN_CHANGE:
				GetDlgItemText(hMain, IDC_DEVICEEDIT, editable_desc, STR_BUFFER_SIZE);
				break;
			default:
				return FALSE;
			}
			break;
		case IDC_DEVICELIST:		// dropdown: device description
			switch (HIWORD(wParam)) {
			case CBN_SELCHANGE:
				device = get_selected_device();
				if (device != NULL) {
					// Change the description string if needed
					if (device->desc == NULL) {
						editable_desc = malloc(STR_BUFFER_SIZE);
						if (editable_desc == NULL) {
							dprintf("could not use modified device description\n");
							editable_desc = device->desc;
						} else {
							safe_sprintf(editable_desc, STR_BUFFER_SIZE, "(Unknown Device)");
							device->desc = editable_desc;
						}
					}
					// Display the current driver info
					if (device->driver != NULL) {
						SetDlgItemText(hMain, IDC_DRIVER, device->driver);
						display_driver(true);
					} else {
						display_driver(false);
					}
					driver_type = WDI_NB_DRIVERS-1;
					if (!select_next_driver(true)) {
						dprintf("no driver is selectable in libwdi!");
					}
					// Display the VID,PID,MI
					safe_sprintf(str_tmp, 5, "%04X", device->vid);
					SetDlgItemText(hMain, IDC_VID, str_tmp);
					safe_sprintf(str_tmp, 5, "%04X", device->pid);
					SetDlgItemText(hMain, IDC_PID, str_tmp);
					if (device->is_composite) {
						safe_sprintf(str_tmp, 5, "%02X", device->mi);
						SetDlgItemText(hMain, IDC_MI, str_tmp);
						display_mi(true);
					} else {
						display_mi(false);
					}
					EnableWindow(GetDlgItem(hMain, IDC_EDITNAME), true);
				}
				break;
			default:
				return FALSE;
			}
			break;
		case IDC_INSTALL:	// button: "Install"
			if (install_thread != NULL) {
				dprintf("program assertion failed - another install thread is running\n");
			} else {
				// Using a thread prevents application freezout
				install_thread = (HANDLE)_beginthread(install_driver_thread, 0, (void*)(uintptr_t)device);
				if (install_thread == NULL) {
					dprintf("unable to create install_thread\n");
				}
			}
			break;
		case IDC_BROWSE:	// button: "Browse..."
			browse_for_folder();
			break;
		case IDC_CLEAR:		// button: "Clear Log"
			Edit_SetText(hInfo, "");
			break;
		case IDC_SAVE:		// button: "Save Log"
			log_size = GetWindowTextLength(hInfo);
			log_buffer = malloc(log_size);
			if (log_buffer != NULL) {
				log_size = GetDlgItemTextA(hMain, IDC_INFO, log_buffer, log_size);
				save_file("C:", "zadig.log", "log", "Zadig log", log_buffer, log_size);
				safe_free(log_buffer);
			} else {
				dprintf("could not allocate buffer to save log\n");
			}
			break;
		case IDOK:			// close application
		case IDCANCEL:
			wdi_destroy_list(list);
			EndDialog(hDlg, 0);
			break;
		// Menus
		case IDM_CREATE:
			toggle_create(true);
			break;
		case IDM_OPEN:
			NOT_IMPLEMENTED();
			break;
		case IDM_ABOUT:
			DialogBox(main_instance, MAKEINTRESOURCE(IDD_ABOUTBOX), hMain, About);
			break;
		case IDM_ONLINEHELP:
			ShellExecute(hDlg, "open", "http://libusb.org/wiki/libwdi_zadig",
				NULL, NULL, SW_SHOWNORMAL);
			break;
		case IDM_EXTRACT:
			toggle_extract();
			break;
		case IDM_ADVANCEDMODE:
			toggle_advanced();
			break;
		case IDM_DRIVERLESSONLY:	// checkbox: "List Only Driverless Devices"
			toggle_driverless();
			break;
		default:
			return FALSE;
		}
		return TRUE;

	default:
		return FALSE;

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

	// Initialize COM for folder selection
	CoInitialize(NULL);

	// Create the main Window
	if (DialogBox(hInstance, "MAIN_DIALOG", NULL, main_callback) == -1) {
		MessageBox(NULL, "Could not create Window", "DialogBox failure", MB_ICONSTOP);
	}

	return (0);
}

