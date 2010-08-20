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
#include <string.h>
#include <objbase.h>
#include <process.h>
#include <shellapi.h>
#include <commctrl.h>

#include "libwdi.h"
#include "msapi_utf8.h"
#include "resource.h"
#include "zadig.h"
#include "libconfig/libconfig.h"

#define NOT_DURING_INSTALL if (installation_running) return (INT_PTR)TRUE

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
HMENU hMenuLogLevel;
WNDPROC original_wndproc;
char app_dir[MAX_PATH];
char extraction_path[MAX_PATH];
char* driver_display_name[WDI_NB_DRIVERS] = { "WinUSB", "libusb0", "Custom (extract only)" };
struct wdi_options_create_list cl_options = {false, false, true};
struct wdi_options_prepare_driver pd_options = {WDI_WINUSB, NULL};
struct wdi_device_info *device, *list = NULL;
int current_device_index = CB_ERR;
char* current_device_hardware_id = NULL;
char* editable_desc = NULL;
int default_driver_type = WDI_WINUSB;
int log_level = WDI_LOG_LEVEL_INFO;
// Application states
bool advanced_mode = false;
bool create_device = false;
bool extract_only = false;
bool from_install = false;
bool installation_running = false;
// Libconfig
config_t cfg;
config_setting_t *setting;

/*
 * On screen logging and status
 */
void w_printf_v(bool update_status, const char *format, va_list args)
{
	char str[STR_BUFFER_SIZE+2];
	int size;
	size_t slen;

	size = safe_vsnprintf(str, STR_BUFFER_SIZE, format, args);
	if (size < 0) {
		str[STR_BUFFER_SIZE-1] = 0;
	}
	slen = safe_strlen(str);
	str[slen] = '\r';
	str[slen+1] = '\n';
	str[slen+2] = 0;
	// Set cursor to the end of the buffer
	Edit_SetSel(hInfo, MAX_LOG_SIZE, MAX_LOG_SIZE);
	Edit_ReplaceSelU(hInfo, str);
	if (update_status) {
		SetDlgItemTextU(hMain, IDC_STATUS, str);
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
		GetTextExtentPointU(hdc, dev->desc, &size);
		max_width = max(max_width, size.cx);

		index = ComboBox_AddStringU(hDeviceList, dev->desc);
		if ((index != CB_ERR) && (index != CB_ERRSPACE)) {
			junk = ComboBox_SetItemData(hDeviceList, index, (LPARAM)dev);
		} else {
			dprintf("could not populate dropdown list past device #%d", index);
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
	int i;
	const char* libusb_name[] = { "WinUSB", "libusb0" };
	const char* system_name[] = { "usbhub", "usbccgp", "USBSTOR", "HidUsb"};

	if ((dev == NULL) || (dev->driver == NULL)) {
		return DT_NONE;
	}
	for (i=0; i<sizeof(libusb_name)/sizeof(libusb_name[0]); i++) {
		if (safe_strcmp(dev->driver, libusb_name[i]) == 0) {
			return DT_LIBUSB;
		}
	}
	for (i=0; i<sizeof(system_name)/sizeof(system_name[0]); i++) {
		if (safe_strcmp(dev->driver, system_name[i]) == 0) {
			return DT_SYSTEM;
		}
	}
	return DT_UNKNOWN;
}

/*
 * Perform the driver installation
 */
int install_driver(void)
{
	struct wdi_device_info* dev = device;
	struct wdi_options_install_driver options;
	char str_buf[STR_BUFFER_SIZE];
	char* inf_name = NULL;
	bool need_dealloc = false;
	int tmp, r = WDI_ERROR_OTHER;

	installation_running = true;
	if (GetMenuState(hMenuDevice, IDM_CREATE, MF_CHECKED) & MF_CHECKED) {
		// If the device is created from scratch, override the existing device
		dev = calloc(1, sizeof(struct wdi_device_info));
		if (dev == NULL) {
			dprintf("could not create new device_info struct for installation");
			r = WDI_ERROR_RESOURCE; goto out;
		}
		need_dealloc = true;

		// Retrieve the various device parameters
		if (ComboBox_GetTextU(GetDlgItem(hMain, IDC_DEVICEEDIT), str_buf, STR_BUFFER_SIZE) == 0) {
			notification(MSG_ERROR, "The description string cannot be empty.", "Driver Installation");
			r = WDI_ERROR_INVALID_PARAM; goto out;
		}
		dev->desc = safe_strdup(str_buf);
		GetDlgItemTextA(hMain, IDC_VID, str_buf, STR_BUFFER_SIZE);
		if (sscanf(str_buf, "%4x", &tmp) != 1) {
			dprintf("could not convert VID string - aborting");
			r = WDI_ERROR_INVALID_PARAM; goto out;
		}
		dev->vid = (unsigned short)tmp;
		GetDlgItemTextA(hMain, IDC_PID, str_buf, STR_BUFFER_SIZE);
		if (sscanf(str_buf, "%4x", &tmp) != 1) {
			dprintf("could not convert PID string - aborting");
			r = WDI_ERROR_INVALID_PARAM; goto out;
		}
		dev->pid = (unsigned short)tmp;
		GetDlgItemTextA(hMain, IDC_MI, str_buf, STR_BUFFER_SIZE);
		if ( (safe_strlen(str_buf) != 0)
		  && (sscanf(str_buf, "%2x", &tmp) == 1) ) {
			dev->is_composite = true;
			dev->mi = (unsigned char)tmp;
		} else {
			dev->is_composite = false;
			dev->mi = 0;
		}
	}

	inf_name = to_valid_filename(dev->desc, ".inf");
	dprintf("Using inf name: %s", inf_name);

	// Perform extraction/installation
	GetDlgItemTextU(hMain, IDC_FOLDER, extraction_path, MAX_PATH);
	r = wdi_prepare_driver(dev, extraction_path, inf_name, &pd_options);
	if (r == WDI_SUCCESS) {
		dsprintf("Succesfully extracted driver files.");
		// Perform the install if not extracting the files only
		if ((pd_options.driver_type != WDI_USER) && (!extract_only)) {
			if ( (get_driver_type(dev) == DT_SYSTEM)
			  && (MessageBoxA(hMain, "You are about to replace a system driver.\n"
					"Are you sure this is what you want?", "Warning - System Driver",
					MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDNO) ) {
				r = WDI_ERROR_USER_CANCEL; goto out;
			}
			dsprintf("Installing driver. Please wait...");
			options.hWnd = hMain;
			r = wdi_install_driver(dev, extraction_path, inf_name, &options);
			// Switch to non driverless-only mode and set hw ID to show the newly installed device
			current_device_hardware_id = safe_strdup(dev->hardware_id);
			if ((r == WDI_SUCCESS) && (!cl_options.list_all)) {
				toggle_driverless(false);
			}
			PostMessage(hMain, WM_DEVICECHANGE, 0, 0);	// Force a refresh
		}
	} else {
		dsprintf("Could not extract files");
	}
out:
	if (need_dealloc) {
		free(dev);
	}
	safe_free(inf_name);
	from_install = true;
	installation_running = false;
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
		pd_options.driver_type = (WDI_NB_DRIVERS + pd_options.driver_type + increment)%WDI_NB_DRIVERS;
		if (!wdi_is_driver_supported(pd_options.driver_type, NULL)) {
			continue;
		}
		if (!extract_only) {
			SetDlgItemTextA(hMain, IDC_INSTALL, (pd_options.driver_type == WDI_USER)?"Extract Files":"Install Driver");
		}
		found = true;
		break;
	}
	SetDlgItemTextA(hMain, IDC_TARGET,
		found?driver_display_name[pd_options.driver_type]:"(NONE)");
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
	int install_shift = 62;
	int dialog_shift = 385;
	RECT rect;
	POINT point;
	int toggle;
	HDC hdc;

	advanced_mode = !(GetMenuState(hMenuOptions, IDM_ADVANCEDMODE, MF_CHECKED) & MF_CHECKED);

	// Adjust the shifts according to the DPI
	hdc = GetDC(hMain);
	install_shift = install_shift * GetDeviceCaps(hdc, LOGPIXELSX) / 96;
	dialog_shift = dialog_shift * GetDeviceCaps(hdc, LOGPIXELSX) / 96;
	ReleaseDC(hMain, hdc);

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
			dprintf("program assertion failed - editable_desc != NULL");
			return;
		}
		editable_desc = malloc(STR_BUFFER_SIZE);
		if (editable_desc == NULL) {
			dprintf("could not allocate buffer to edit description");
			CheckDlgButton(hMain, IDC_EDITNAME, BST_UNCHECKED);
			combo_breaker(false);
			return;
		}
		safe_strcpy(editable_desc, STR_BUFFER_SIZE, device->desc);
		free(device->desc);	// No longer needed
		device->desc = editable_desc;
		SetDlgItemTextU(hMain, IDC_DEVICEEDIT, editable_desc);
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
		SetDlgItemTextA(hMain, IDC_VID, "");
		SetDlgItemTextA(hMain, IDC_PID, "");
		SetDlgItemTextA(hMain, IDC_MI, "");
		SetDlgItemTextA(hMain, IDC_DEVICEEDIT, "");
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
	if (pd_options.driver_type == WDI_USER) {
		return;
	}
	extract_only = !(GetMenuState(hMenuOptions, IDM_EXTRACT, MF_CHECKED) & MF_CHECKED);
	CheckMenuItem(hMenuOptions, IDM_EXTRACT, extract_only?MF_CHECKED:MF_UNCHECKED);
	SetDlgItemTextA(hMain, IDC_INSTALL, extract_only?"Extract Files":"Install Driver");
}

// Toggle ignore hubs & composite
void toggle_hubs(bool refresh)
{
	cl_options.list_hubs = GetMenuState(hMenuOptions, IDM_IGNOREHUBS, MF_CHECKED) & MF_CHECKED;

	if (create_device) {
		toggle_create(true);
	}

	CheckMenuItem(hMenuOptions, IDM_IGNOREHUBS, cl_options.list_hubs?MF_UNCHECKED:MF_CHECKED);
	// Reset Edit button
	CheckDlgButton(hMain, IDC_EDITNAME, BST_UNCHECKED);
	// Reset Combo
	combo_breaker(false);
	if (refresh) {
		PostMessage(hMain, UM_REFRESH_LIST, 0, 0);
	}
}

// Toggle driverless device listing
void toggle_driverless(bool refresh)
{
	cl_options.list_all = !(GetMenuState(hMenuOptions, IDM_LISTALL, MF_CHECKED) & MF_CHECKED);
	EnableMenuItem(hMenuOptions, IDM_IGNOREHUBS, cl_options.list_all?MF_ENABLED:MF_GRAYED);

	if (create_device) {
		toggle_create(true);
	}

	CheckMenuItem(hMenuOptions, IDM_LISTALL, cl_options.list_all?MF_CHECKED:MF_UNCHECKED);
	// Reset Edit button
	CheckDlgButton(hMain, IDC_EDITNAME, BST_UNCHECKED);
	// Reset Combo
	combo_breaker(false);
	if (refresh) {
		PostMessage(hMain, UM_REFRESH_LIST, 0, 0);
	}
}

/*
 * Change the log level
 */
void set_loglevel(DWORD menu_cmd)
{
	CheckMenuItem(hMenuLogLevel, log_level+IDM_LOGLEVEL_DEBUG, MF_UNCHECKED);
	CheckMenuItem(hMenuLogLevel, menu_cmd, MF_CHECKED);
	log_level = menu_cmd - IDM_LOGLEVEL_DEBUG;
	wdi_set_log_level(log_level);
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
	hMenuLogLevel = GetSubMenu(hMenuOptions, 4);

	// Create the status line
	create_status_bar();

	// The application always starts in advanced mode
	CheckMenuItem(hMenuOptions, IDM_ADVANCEDMODE, MF_CHECKED);

	// Setup logging
	err = wdi_register_logger(hMain, UM_LOGGER_EVENT, 0);
	if (err != WDI_SUCCESS) {
		dprintf("Unable to access log output - logging will be disabled (%s)", wdi_strerror(err));
	}
	// Increase the size of our log textbox to MAX_LOG_SIZE (unsigned word)
	PostMessage(hInfo, EM_LIMITTEXT, MAX_LOG_SIZE , 0);

	// Limit the input size of VID, PID, MI
	PostMessage(GetDlgItem(hMain, IDC_VID), EM_SETLIMITTEXT, 4, 0);
	PostMessage(GetDlgItem(hMain, IDC_PID), EM_SETLIMITTEXT, 4, 0);
	PostMessage(GetDlgItem(hMain, IDC_MI), EM_SETLIMITTEXT, 2, 0);

	// Set the extraction directory
	SetDlgItemTextU(hMain, IDC_FOLDER, DEFAULT_DIR);

	// Parse the ini file and set the startup options accordingly
	parse_ini();
	set_loglevel(log_level+IDM_LOGLEVEL_DEBUG);

	if (!advanced_mode) {
		toggle_advanced();	// We start in advanced mode
	}
	if (cl_options.list_all) {
		toggle_driverless(false);
	}
	if (cl_options.list_hubs) {
		toggle_hubs(false);
	}
	if (extract_only) {
		toggle_extract();
	}
	pd_options.driver_type = default_driver_type;
	select_next_driver(0);
}

/*
 * Use libconfig to parse the default ini file
 */
bool parse_ini(void) {
	const char* tmp = NULL;
	int i;

	// Check if the ini file exists
	if (GetFileAttributesU(INI_NAME) == INVALID_FILE_ATTRIBUTES) {
		dprintf("ini file '%s' not found - default parameters will be used", INI_NAME);
		return false;
	}

	// Parse the file
	if (!config_read_file(&cfg, INI_NAME)) {
		dprintf("%s:%d - %s", config_error_file(&cfg),
			config_error_line(&cfg), config_error_text(&cfg));
		return false;
	}

	dprintf("reading ini file '%s'", INI_NAME);

	// Set the various boolean options
	config_lookup_bool(&cfg, "advanced_mode", &advanced_mode);
	config_lookup_bool(&cfg, "list_all", &cl_options.list_all);
	config_lookup_bool(&cfg, "include_hubs", &cl_options.list_hubs);
	config_lookup_bool(&cfg, "extract_only", &extract_only);
	config_lookup_bool(&cfg, "trim_whitespaces", &cl_options.trim_whitespaces);

	// Set the log level
	config_lookup_int(&cfg, "log_level", &log_level);
	if ((log_level < 0) && (log_level > 3)) {
		log_level = WDI_LOG_LEVEL_INFO;
	}

	// Set the default extraction dir
	if (config_lookup_string(&cfg, "default_dir", &tmp) == CONFIG_TRUE) {
		SetDlgItemText(hMain, IDC_FOLDER, tmp);
	}

	// Set the default driver
	config_lookup_int(&cfg, "default_driver", &default_driver_type);
	if ((default_driver_type < 0) || (default_driver_type >= WDI_NB_DRIVERS)) {
		dprintf("invalid value '%d' for ini option 'default_driver'", default_driver_type);
		default_driver_type = WDI_WINUSB;
	}
	if (!wdi_is_driver_supported(default_driver_type, NULL)) {
		dprintf("'%s' driver is not available", driver_display_name[default_driver_type]);
		for (i=(default_driver_type+1)%WDI_NB_DRIVERS; i!=default_driver_type; i++) {
			if (wdi_is_driver_supported(i, NULL)) {
				default_driver_type = i;
				break;
			}
		}
		if (i!=default_driver_type) {
			notification(MSG_ERROR, "No driver is available for installation with this application.\n"
				"The application will close", "No Driver Available");
			EndDialog(hMain, 0);
		}
		dprintf("falling back to '%s' for default driver", driver_display_name[default_driver_type]);
	} else {
		dprintf("default driver set to '%s'", driver_display_name[default_driver_type]);
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
		dprintf("%s:%d - %s", config_error_file(&cfg),
			config_error_line(&cfg), config_error_text(&cfg));
		return false;
	}

	dev = config_lookup(&cfg, "device");
	if (dev != NULL) {
		if (!create_device) {
			toggle_create(false);
		}

		if (config_setting_lookup_string(dev, "Description", &desc)) {
			SetDlgItemTextU(hMain, IDC_DEVICEEDIT, (char*)desc);
		}

		if (config_setting_lookup_int(dev, "VID", &tmp)) {
			safe_sprintf(str_tmp, 5, "%04X", tmp);
			SetDlgItemTextA(hMain, IDC_VID, str_tmp);
		}

		if (config_setting_lookup_int(dev, "PID", &tmp)) {
			safe_sprintf(str_tmp, 5, "%04X", tmp);
			SetDlgItemTextA(hMain, IDC_PID, str_tmp);
		}

		if (config_setting_lookup_int(dev, "MI", &tmp)) {
			safe_sprintf(str_tmp, 5, "%02X", tmp);
			SetDlgItemTextA(hMain, IDC_MI, str_tmp);
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
	char log_buf[2*STR_BUFFER_SIZE];
	char *log_buffer, *filepath;
	const char* vid_string;
	int nb_devices, tmp, r;
	DWORD delay, read_size, log_size;

	// The following local variables are used to change the visual aspect of the fields
	static HWND hDeviceEdit;
	static HWND hVid, hPid, hMi;
	static HWND hFolder, hDriver, hTarget;
	static HWND hToolTip = NULL;
	static HBRUSH white_brush = (HBRUSH)FALSE;
	static HBRUSH green_brush = (HBRUSH)FALSE;
	static HBRUSH red_brush = (HBRUSH)FALSE;
	static HBRUSH grey_brush = (HBRUSH)FALSE;
	static HBRUSH driver_background[NB_DRIVER_TYPES];

	switch (message) {

	case WM_DEVICECHANGE:
		NOT_DURING_INSTALL;
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
				dprintf("Unable to create notification delay thread - notification events will be disabled");
			}
		}
		return (INT_PTR)TRUE;

	case UM_DEVICE_EVENT:
		NOT_DURING_INSTALL;
		notification_delay_thid = -1L;
		if (create_device) {
			if (MessageBoxA(hMain, "The USB device list has been modified.\n"
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
		r = wdi_read_logger(log_buf, sizeof(log_buf), &read_size);
		if (r == WDI_SUCCESS) {
			dprintf("%s", log_buf);
		} else {
			dprintf("wdi_read_logger: error %s", wdi_strerror(r));
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
		NOT_DURING_INSTALL;
		// Reset edit mode if selected
		if (IsDlgButtonChecked(hMain, IDC_EDITNAME) == BST_CHECKED) {
			combo_breaker(false);
			CheckDlgButton(hMain, IDC_EDITNAME, BST_UNCHECKED);
		}
		if (list != NULL) wdi_destroy_list(list);
		if (!from_install) {
			current_device_index = 0;
		}
		r = wdi_create_list(&list, &cl_options);
		if (r == WDI_SUCCESS) {
			nb_devices = display_devices();
			// Send a dropdown selection message to update fields
			PostMessage(hMain, WM_COMMAND, MAKELONG(IDC_DEVICELIST, CBN_SELCHANGE),
				(LPARAM)hDeviceList);
		} else {
			nb_devices = -1;
			tmp = ComboBox_ResetContent(hDeviceList);
			SetDlgItemTextA(hMain, IDC_VID, "");
			SetDlgItemTextA(hMain, IDC_PID, "");
			display_driver(false);
			display_mi(false);
			EnableWindow(GetDlgItem(hMain, IDC_EDITNAME), false);
		}
		// Make sure we don't override the install status on refresh from install
		if (!from_install) {
			dsprintf("%d device%s found.", nb_devices+1, (nb_devices!=0)?"s":"");
		} else {
			dprintf("%d device%s found.", nb_devices+1, (nb_devices!=0)?"s":"");
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

	// Set background colour of read only fields
	case WM_CTLCOLORSTATIC:
		// Must be transparent for XP and non Aero Vista/7
		SetBkMode((HDC)wParam, TRANSPARENT);
		if ( ((HWND)lParam == hVid)
		  || ((HWND)lParam == hPid)
		  || ((HWND)lParam == hMi) ) {
			return (INT_PTR)white_brush;
		} else if ((HWND)lParam == hDriver) {
			return (INT_PTR)driver_background[get_driver_type(device)];
		} else if ((HWND)lParam == hTarget) {
			return (INT_PTR)grey_brush;
		}
		// Restore transparency if we don't change the background
		SetBkMode((HDC)wParam, OPAQUE);
		return (INT_PTR)FALSE;

	case WM_COMMAND:
		switch(LOWORD(wParam)) {
		case IDC_EDITNAME:			// checkbox: "Edit Desc."
			toggle_edit();
			break;
		case IDC_DEVICEEDIT:		// edit: device description
			switch (HIWORD(wParam)) {
			case EN_CHANGE:
				GetDlgItemTextU(hMain, IDC_DEVICEEDIT, editable_desc, STR_BUFFER_SIZE);
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
							dprintf("could not use modified device description");
							editable_desc = device->desc;
						} else {
							safe_sprintf(editable_desc, STR_BUFFER_SIZE, "(Unknown Device)");
							device->desc = editable_desc;
						}
					}
					// Display the current driver info
					if (device->driver != NULL) {
						SetDlgItemTextU(hMain, IDC_DRIVER, device->driver);
						display_driver(true);
					} else {
						display_driver(false);
					}
					pd_options.driver_type = default_driver_type;
					if ((!select_next_driver(0)) && (!select_next_driver(1))) {
						dprintf("no driver is selectable in libwdi!");
					}
					// Display the VID,PID,MI
					safe_sprintf(str_tmp, 5, "%04X", device->vid);
					SetDlgItemTextA(hMain, IDC_VID, str_tmp);
					// Display the vendor string as a tooltip
					DestroyWindow(hToolTip);
					vid_string = wdi_get_vendor_name(device->vid);
					if (vid_string == NULL) {
						vid_string = "Unknown Vendor";
					}
					hToolTip = create_tooltip(GetDlgItem(hMain, IDC_VID), (char*)vid_string, -1);
					safe_sprintf(str_tmp, 5, "%04X", device->pid);
					SetDlgItemTextA(hMain, IDC_PID, str_tmp);
					if (device->is_composite) {
						safe_sprintf(str_tmp, 5, "%02X", device->mi);
						SetDlgItemTextA(hMain, IDC_MI, str_tmp);
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
			r = install_driver();
			if (r == WDI_SUCCESS) {
				if (!extract_only) {
					dsprintf("Driver Installation: SUCCESS");
					notification(MSG_INFO, "The driver was installed successfully.", "Driver Installation");
				}
			} else if (r == WDI_ERROR_USER_CANCEL) {
				dsprintf("Driver Installation: Cancelled by User");
				notification(MSG_WARNING, "Driver installation cancelled by user.", "Driver Installation");
			} else {
				dsprintf("Driver Installation: FAILED (%s)", wdi_strerror(r));
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
			log_size = GetWindowTextLengthU(hInfo);
			log_buffer = malloc(log_size);
			if (log_buffer != NULL) {
				log_size = GetDlgItemTextU(hMain, IDC_INFO, log_buffer, log_size);
				if (log_size == 0) {
					dprintf("unable to read log text");
				} else {
					log_size--;	// remove NULL terminator
					filepath = file_dialog(true, app_dir, "zadig.log", "log", "Zadig log");
					if (filepath != NULL) {
						file_io(true, filepath, &log_buffer, &log_size);
					}
					safe_free(filepath);
				}
				safe_free(log_buffer);
			} else {
				dprintf("could not allocate buffer to save log");
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
			DialogBoxA(main_instance, MAKEINTRESOURCE(IDD_ABOUTBOX), hMain, about_callback);
			break;
		case IDM_ONLINEHELP:
			ShellExecuteA(hDlg, "open", "http://libusb.org/wiki/libwdi_zadig",
				NULL, NULL, SW_SHOWNORMAL);
			break;
		case IDM_EXTRACT:
			toggle_extract();
			break;
		case IDM_ADVANCEDMODE:
			toggle_advanced();
			break;
		case IDM_LISTALL:
			toggle_driverless(true);
			break;
		case IDM_IGNOREHUBS:
			toggle_hubs(true);
			break;
		case IDM_LOGLEVEL_ERROR:
		case IDM_LOGLEVEL_WARNING:
		case IDM_LOGLEVEL_INFO:
		case IDM_LOGLEVEL_DEBUG:
			set_loglevel(LOWORD(wParam));
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
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

	// Initialize libconfig
	config_init(&cfg);

	// Retrieve the current application directory
	GetCurrentDirectoryU(MAX_PATH, app_dir);

	// Create the main Window
	if (DialogBoxA(hInstance, "MAIN_DIALOG", NULL, main_callback) == -1) {
		MessageBox(NULL, "Could not create Window", "DialogBox failure", MB_ICONSTOP);
	}

	// Exit libconfig
	// TODO: This call can prevent cygwin from exiting cleanly, probably due to a libconfig bug.
//	config_destroy(&cfg);

	CloseHandle(mutex);

	return 0;
}
