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
#include "libconfig/libconfig.h"

void toggle_driverless(bool refresh);
bool parse_ini(void);

/*
 * Globals
 */
HINSTANCE main_instance;
HWND hDeviceList;
HWND hMain;
HWND hInfo;
HWND hStatus;
HMENU hMenuDevice;
HMENU hMenuOptions;
WNDPROC original_wndproc;
char app_dir[MAX_PATH];
char extraction_path[MAX_PATH];
char* driver_display_name[WDI_NB_DRIVERS] = { "WinUSB", "libusb0", "Custom (extract only)" };
struct wdi_options options = {WDI_WINUSB, false, true};
struct wdi_device_info *device, *list = NULL;
int current_device_index = CB_ERR;
char* current_device_hardware_id = NULL;
char* editable_desc = NULL;
int default_driver_type = WDI_WINUSB;
// Application states
bool advanced_mode = false;
bool create_device = false;
bool extract_only = false;
bool from_install = false;
// Libconfig
config_t cfg;
config_setting_t *setting;

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
	// Set cursor to the end of the buffer
	Edit_SetSel(hInfo, MAX_LOG_SIZE , MAX_LOG_SIZE);
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

		// Select by Hardware ID if one's available
		if (safe_strcmp(current_device_hardware_id, dev->hardware_id) == 0) {
			current_device_index = index;
			safe_free(current_device_hardware_id);
		}
	}

	// Select current entry
	if (current_device_index == CB_ERR) {
		current_device_index = 0;
	}
	junk = ComboBox_SetCurSel(hDeviceList, current_device_index);
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

	current_device_index = ComboBox_GetCurSel(hDeviceList);
	if (current_device_index != CB_ERR) {
		// Use the device pointers as dropdown values for easy access
		dev = (struct wdi_device_info*)ComboBox_GetItemData(hDeviceList, current_device_index);
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
	const char* libusb_name = "libusb0";
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
 * Perform the driver installation
 */
int install_driver(void)
{
	struct wdi_device_info* dev = device;
	static char str_buf[STR_BUFFER_SIZE];
	bool need_dealloc = false;
	int tmp, r = WDI_ERROR_OTHER;

	if (GetMenuState(hMenuDevice, IDM_CREATE, MF_CHECKED) & MF_CHECKED) {
		// If the device is created from scratch, override the existing device
		dev = calloc(1, sizeof(struct wdi_device_info));
		if (dev == NULL) {
			dprintf("could not create new device_info struct for installation\n");
			r = WDI_ERROR_RESOURCE; goto out;
		}
		need_dealloc = true;

		// Retrieve the various device parameters
		if (ComboBox_GetText(GetDlgItem(hMain, IDC_DEVICEEDIT), str_buf, STR_BUFFER_SIZE) == 0) {
			notification(MSG_ERROR, "The description string cannot be empty.", "Driver Installation");
			r = WDI_ERROR_INVALID_PARAM; goto out;
		}
		dev->desc = safe_strdup(str_buf);
		GetDlgItemText(hMain, IDC_VID, str_buf, STR_BUFFER_SIZE);
		if (sscanf(str_buf, "%4x", &tmp) != 1) {
			dprintf("could not convert VID string - aborting\n");
			r = WDI_ERROR_INVALID_PARAM; goto out;
		}
		dev->vid = (unsigned short)tmp;
		GetDlgItemText(hMain, IDC_PID, str_buf, STR_BUFFER_SIZE);
		if (sscanf(str_buf, "%4x", &tmp) != 1) {
			dprintf("could not convert PID string - aborting\n");
			r = WDI_ERROR_INVALID_PARAM; goto out;
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
	r = wdi_prepare_driver(dev, extraction_path, INF_NAME, &options);
	if (r == WDI_SUCCESS) {
		dsprintf("Succesfully extracted driver files\n");
		// Perform the install if not extracting the files only
		if ((options.driver_type != WDI_USER) && (!extract_only)) {
			if ( (get_driver_type(dev) == DT_SYSTEM)
			  && (MessageBox(hMain, "You are about to replace a system driver.\n"
					"Are you sure this is what you want?", "Warning - System Driver",
					MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDNO) ) {
				r = WDI_ERROR_USER_CANCEL; goto out;
			}
			dsprintf("Installing driver. Please wait...\n");
			r = wdi_install_driver(dev, extraction_path, INF_NAME, &options);
			// Switch to non driverless-only mode and set hw ID to show the newly installed device
			current_device_hardware_id = safe_strdup(dev->hardware_id);
			if ((r == WDI_SUCCESS) && (!options.list_all)) {
				toggle_driverless(false);
			}
			PostMessage(hMain, WM_DEVICECHANGE, 0, 0);	// Force a refresh
		}
	} else {
		dsprintf("Could not extract files\n");
	}
out:
	if (need_dealloc) {
		free(dev);
	}
	from_install = true;
	return r;
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
bool select_next_driver(int increment)
{
	int i;
	bool found = false;

	for (i=0; i<WDI_NB_DRIVERS; i++) {	// don't loop forever
		options.driver_type = (WDI_NB_DRIVERS + options.driver_type + increment)%WDI_NB_DRIVERS;
		if (!wdi_is_driver_supported(options.driver_type)) {
			continue;
		}
		if (!extract_only) {
			SetDlgItemText(hMain, IDC_INSTALL, (options.driver_type == WDI_USER)?"Extract Files":"Install Driver");
		}
		found = true;
		break;
	}
	SetDlgItemText(hMain, IDC_TARGET,
		found?driver_display_name[options.driver_type]:"(NONE)");
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
	if (options.driver_type == WDI_USER) {
		return;
	}
	extract_only = !(GetMenuState(hMenuOptions, IDM_EXTRACT, MF_CHECKED) & MF_CHECKED);
	CheckMenuItem(hMenuOptions, IDM_EXTRACT, extract_only?MF_CHECKED:MF_UNCHECKED);
	SetDlgItemText(hMain, IDC_INSTALL, extract_only?"Extract Files":"Install Driver");
}

// Toggle driverless device listing
void toggle_driverless(bool refresh)
{
	options.list_all = (GetMenuState(hMenuOptions, IDM_DRIVERLESSONLY, MF_CHECKED) & MF_CHECKED);

	if (create_device) {
		toggle_create(true);
	}

	CheckMenuItem(hMenuOptions, IDM_DRIVERLESSONLY, options.list_all?MF_UNCHECKED:MF_CHECKED);
	// Reset Edit button
	CheckDlgButton(hMain, IDC_EDITNAME, BST_UNCHECKED);
	// Reset Combo
	combo_breaker(false);
	if (refresh) {
		PostMessage(hMain, UM_REFRESH_LIST, 0, 0);
	}
}

void init_dialog(HWND hDlg)
{
	int err;

	// Quite a burden to carry around as parameters
	hMain = hDlg;
	hDeviceList = GetDlgItem(hDlg, IDC_DEVICELIST);
	hInfo = GetDlgItem(hDlg, IDC_INFO);
	hMenuDevice = GetSubMenu(GetMenu(hDlg), 0);
	hMenuOptions = GetSubMenu(GetMenu(hDlg), 1);

	// Create the status line
	create_status_bar();

	// The application always starts in advanced mode
	CheckMenuItem(hMenuOptions, IDM_ADVANCEDMODE, MF_CHECKED);

	// Setup logging
	err = wdi_register_logger(hMain, UM_LOGGER_EVENT, 0);
	if (err != WDI_SUCCESS) {
		dprintf("Unable to access log output - logging will be disabled (%s)\n", wdi_strerror(err));
	}
	wdi_set_log_level(LOG_LEVEL_DEBUG);
	// Increase the size of our log textbox to MAX_LOG_SIZE (unsigned word)
	PostMessage(hInfo, EM_LIMITTEXT, MAX_LOG_SIZE , 0);

	// Limit the input size of VID, PID, MI
	PostMessage(GetDlgItem(hMain, IDC_VID), EM_SETLIMITTEXT, 4, 0);
	PostMessage(GetDlgItem(hMain, IDC_PID), EM_SETLIMITTEXT, 4, 0);
	PostMessage(GetDlgItem(hMain, IDC_MI), EM_SETLIMITTEXT, 2, 0);

	// Set the extraction directory
	SetDlgItemText(hMain, IDC_FOLDER, DEFAULT_DIR);

	// Parse the ini file and set the startup options accordingly
	parse_ini();

	if (!advanced_mode) {
		toggle_advanced();	// We start in advanced mode
	}
	if (!options.list_all) {
		toggle_driverless(false);
	}
	if (extract_only) {
		toggle_extract();
	}
	options.driver_type = default_driver_type;
	select_next_driver(0);
}

/*
 * Use libconfig to parse the default ini file
 */
bool parse_ini(void) {
	const char* tmp = NULL;
	int i;

	// Check if the ini file exists
	if (GetFileAttributes(INI_NAME) == INVALID_FILE_ATTRIBUTES) {
		dprintf("could not open ini file '%s'\n", INI_NAME);
		return false;
	}

	// Parse the file
	if (!config_read_file(&cfg, INI_NAME)) {
		dprintf("%s:%d - %s\n", config_error_file(&cfg),
			config_error_line(&cfg), config_error_text(&cfg));
		return false;
	}

	dprintf("reading ini file '%s'\n", INI_NAME);

	// Set the various boolean options
	config_lookup_bool(&cfg, "advanced_mode", &advanced_mode);
	config_lookup_bool(&cfg, "list_all", &options.list_all);
	config_lookup_bool(&cfg, "extract_only", &extract_only);
	config_lookup_bool(&cfg, "trim_whitespaces", &options.trim_whitespaces);

	// Set the default extraction dir
	if (config_lookup_string(&cfg, "default_dir", &tmp) == CONFIG_TRUE) {
		SetDlgItemText(hMain, IDC_FOLDER, tmp);
	}

	// Set the default driver
	config_lookup_int(&cfg, "default_driver", &default_driver_type);
	if ((default_driver_type < 0) || (default_driver_type >= WDI_NB_DRIVERS)) {
		dprintf("invalid value '%d' for ini option 'default_driver'\n", default_driver_type);
		default_driver_type = WDI_WINUSB;
	}
	if (!wdi_is_driver_supported(default_driver_type)) {
		dprintf("'%s' driver is not available, ", driver_display_name[default_driver_type]);
		for (i=(default_driver_type+1)%WDI_NB_DRIVERS; i!=default_driver_type; i++) {
			if (wdi_is_driver_supported(i)) {
				default_driver_type = i;
				break;
			}
		}
		if (i!=default_driver_type) {
			notification(MSG_ERROR, "No driver is available for installation with this application.\n"
				"The application will close", "No Driver Available");
			EndDialog(hMain, 0);
		}
		dprintf("falling back to '%s' for default driver\n", driver_display_name[default_driver_type]);
	} else {
		dprintf("default driver set to '%s'\n", driver_display_name[default_driver_type]);
	}

	return true;
}

/*
 * Use libconfig to parse a preset device configuration file
 */
bool parse_preset(char* filename)
{
	config_setting_t *dev;
	int tmp;
	char str_tmp[5];
	const char* desc = NULL;

	if (filename == NULL) {
		return false;
	}

	if (!config_read_file(&cfg, filename)) {
		dprintf("%s:%d - %s\n", config_error_file(&cfg),
			config_error_line(&cfg), config_error_text(&cfg));
		return false;
	}

	dev = config_lookup(&cfg, "device");
	if (dev != NULL) {
		if (!create_device) {
			toggle_create(false);
		}

		if (config_setting_lookup_string(dev, "Description", &desc)) {
			SetDlgItemText(hMain, IDC_DEVICEEDIT, desc);
		}

		if (config_setting_lookup_int(dev, "VID", &tmp)) {
			safe_sprintf(str_tmp, 5, "%04X", tmp);
			SetDlgItemText(hMain, IDC_VID, str_tmp);
		}

		if (config_setting_lookup_int(dev, "PID", &tmp)) {
			safe_sprintf(str_tmp, 5, "%04X", tmp);
			SetDlgItemText(hMain, IDC_PID, str_tmp);
		}

		if (config_setting_lookup_int(dev, "MI", &tmp)) {
			safe_sprintf(str_tmp, 5, "%02X", tmp);
			SetDlgItemText(hMain, IDC_MI, str_tmp);
		}
		return true;
	}
	return false;
}

/*
 * Work around the limitations of edit control, for UI aesthetics
 */
INT_PTR CALLBACK subclass_callback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_SETCURSOR:
		if ( ((HWND)wParam == GetDlgItem(hDlg, IDC_DRIVER))
		  || ((HWND)wParam == GetDlgItem(hDlg, IDC_TARGET)) ) {
			SetCursor(LoadCursor(NULL, IDC_ARROW));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return CallWindowProc(original_wndproc, hDlg, message, wParam, lParam);
}

/*
 * Main dialog callback
 */
INT_PTR CALLBACK main_callback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	static uintptr_t notification_delay_thid = -1L;
	static DWORD last_scroll = 0;
	char str_tmp[5];
	char log_buf[STR_BUFFER_SIZE];
	char *log_buffer, *filepath;
	int nb_devices, tmp, r;
	DWORD delay, read_size, log_size;

	// The following local variables are used to change the visual aspect of the fields
	static HWND hDeviceEdit;
	static HWND hVid, hPid, hMi;
	static HWND hFolder, hDriver, hTarget;
	static HBRUSH white_brush = (HBRUSH)FALSE;
	static HBRUSH green_brush = (HBRUSH)FALSE;
	static HBRUSH red_brush = (HBRUSH)FALSE;
	static HBRUSH grey_brush = (HBRUSH)FALSE;
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
		if (notification_delay_thid == -1L) {
			delay = NOTIFICATION_DELAY;
			notification_delay_thid = _beginthread(notification_delay_thread, 0, (void*)(uintptr_t)delay);
			if (notification_delay_thid == -1L) {
				dprintf("Unable to create notification delay thread - notification events will be disabled\n");
			}
		}
		return (INT_PTR)TRUE;

	case UM_DEVICE_EVENT:
		notification_delay_thid = -1L;
		if (create_device) {
			if (MessageBox(hMain, "The USB device list has been modified.\n"
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
		return (INT_PTR)TRUE;

	case UM_LOGGER_EVENT:
		r = wdi_read_logger(log_buf, STR_BUFFER_SIZE, &read_size);
		if (r == WDI_SUCCESS) {
			dprintf("%s\n", log_buf);
		} else {
			dprintf("wdi_read_logger: error %s\n", wdi_strerror(r));
		}
		return (INT_PTR)TRUE;

	case WM_INITDIALOG:
		// Setup local visual variables
		white_brush = CreateSolidBrush(WHITE);
		green_brush = CreateSolidBrush(GREEN);
		red_brush = CreateSolidBrush(RED);
		grey_brush = CreateSolidBrush(LIGHT_GREY);
		driver_background[DT_NONE] = grey_brush;
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

		// Subclass the callback so that we can change the cursor
		original_wndproc = (WNDPROC)GetWindowLongPtr(hDlg, GWLP_WNDPROC);
		SetWindowLongPtr(hDlg, GWLP_WNDPROC, (LONG_PTR)subclass_callback);

		// Main init
		init_dialog(hDlg);

		// Fall through
	case UM_REFRESH_LIST:
		// Reset edit mode if selected
		if (IsDlgButtonChecked(hMain, IDC_EDITNAME) == BST_CHECKED) {
			combo_breaker(false);
			CheckDlgButton(hMain, IDC_EDITNAME, BST_UNCHECKED);
		}
		if (list != NULL) wdi_destroy_list(list);
		r = wdi_create_list(&list, &options);
		if (r == WDI_SUCCESS) {
			nb_devices = display_devices();
			// Send a dropdown selection message to update fields
			PostMessage(hMain, WM_COMMAND, MAKELONG(IDC_DEVICELIST, CBN_SELCHANGE),
				(LPARAM) hDeviceList);
		} else {
			nb_devices = -1;
			tmp = ComboBox_ResetContent(hDeviceList);
			SetDlgItemText(hMain, IDC_VID, "");
			SetDlgItemText(hMain, IDC_PID, "");
			display_driver(false);
			display_mi(false);
			EnableWindow(GetDlgItem(hMain, IDC_EDITNAME), false);
		}
		// Make sure we don't override the install status on refresh from install
		if (!from_install) {
			dsprintf("%d device%s found.\n", nb_devices+1, (nb_devices!=0)?"s":"");
		} else {
			dprintf("%d device%s found.\n", nb_devices+1, (nb_devices!=0)?"s":"");
			from_install = false;
		}
		return (INT_PTR)TRUE;

	case WM_VSCROLL:
		if (LOWORD(wParam) == 4) {
			if (!select_next_driver( ((HIWORD(wParam) <= last_scroll))?+1:-1)) {
				dprintf("no driver is selectable in libwdi!");
			}
			last_scroll = HIWORD(wParam);
			return (INT_PTR)TRUE;
		}
		return (INT_PTR)FALSE;

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
		  || ((HWND)lParam == hMi) ) {
			return (INT_PTR)white_brush;
		} else if ((HWND)lParam == hDriver) {
			return (INT_PTR)driver_background[get_driver_type(device)];
		} else if ((HWND)lParam == hTarget) {
			return (INT_PTR)grey_brush;
		}
		return (INT_PTR)FALSE;

	case WM_COMMAND:
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
				return (INT_PTR)FALSE;
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
					options.driver_type = default_driver_type;
					if (!select_next_driver(0)) {
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
				return (INT_PTR)FALSE;
			}
			break;
		case IDC_INSTALL:	// button: "Install"
			r = run_with_progress_bar(install_driver);
			if (r == WDI_SUCCESS) {
				if (!extract_only) {
					dsprintf("Driver Installation: SUCCESS\n");
					notification(MSG_INFO, "The driver was installed successfully.", "Driver Installation");
				}
			} else if (r == WDI_ERROR_USER_CANCEL) {
				dsprintf("Driver Installation: Cancelled by User\n");
				notification(MSG_WARNING, "Driver installation cancelled by user.", "Driver Installation");
			} else {
				dsprintf("Driver Installation: FAILED (%s)\n", wdi_strerror(r));
				notification(MSG_ERROR, "The driver installation failed.", "Driver Installation");
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
				filepath = file_dialog(true, app_dir, "zadig.log", "log", "Zadig log"); //, log_buffer, log_size);
				if (filepath != NULL) {
					file_io(true, filepath, &log_buffer, &log_size);
				}
				safe_free(filepath);
				safe_free(log_buffer);
			} else {
				dprintf("could not allocate buffer to save log\n");
			}
			break;
		case IDC_TARGET:	// prevent focus
		case IDC_DRIVER:
			if (HIWORD(wParam) == EN_SETFOCUS) {
				SetFocus(hMain);
				return (INT_PTR)TRUE;
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
			filepath = file_dialog(false, app_dir, "sample.cfg", "cfg", "Zadig device config");
			parse_preset(filepath);
			break;
		case IDM_ABOUT:
			DialogBox(main_instance, MAKEINTRESOURCE(IDD_ABOUTBOX), hMain, about_callback);
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
			current_device_index = 0;
			toggle_driverless(true);
			break;
		default:
			return (INT_PTR)FALSE;
		}
		return (INT_PTR)TRUE;

	default:
		return (INT_PTR)FALSE;

	}
	return (INT_PTR)FALSE;
}

/*
 * Application Entrypoint
 */
int __stdcall WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	HANDLE mutex = NULL;

	// Prevent 2 applications from running at the same time
	mutex = CreateMutex(NULL, TRUE, "Global/Zadig");
	if ((mutex == NULL) || (GetLastError() == ERROR_ALREADY_EXISTS))
	{
		MessageBox(NULL, "Another Zadig application is running.\n"
			"Please close the first application before running another one.",
			"Other instance detected", MB_ICONSTOP);
		return 0;
	}

	// Save instance of the application for further reference
	main_instance = hInstance;

	// Initialize COM for folder selection
	CoInitialize(NULL);

	// Initialize libconfig
	config_init(&cfg);

	// Retrieve the current application directory
	GetCurrentDirectory(MAX_PATH, app_dir);

	// Create the main Window
	if (DialogBox(hInstance, "MAIN_DIALOG", NULL, main_callback) == -1) {
		MessageBox(NULL, "Could not create Window", "DialogBox failure", MB_ICONSTOP);
	}

	// Exit libconfig
	config_destroy(&cfg);

	CloseHandle(mutex);

	return 0;
}

