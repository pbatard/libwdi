/*
 * List and install driver for USB devices (GUI version)
 * Copyright (c) 2010 Pete Batard <pbatard@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
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
#pragma once

#if !defined(bool)
#define bool BOOL
#endif
#if !defined(true)
#define true TRUE
#endif
#if !defined(false)
#define false FALSE
#endif

#define STR_BUFFER_SIZE             512
#define NOTIFICATION_DELAY          1000
#define MAX_LOG_SIZE                0x7FFFFFFE
#define DEFAULT_DIR                 "C:\\usb_driver"
#define INI_NAME                    "zadig.ini"
#define LIBWDI_URL                  "http://sourceforge.net/apps/mediawiki/libwdi/index.php?title=Main_Page"
#define ZADIG_URL                   "http://sourceforge.net/apps/mediawiki/libwdi/index.php?title=Zadig"
#define WCID_URL                    "http://sourceforge.net/apps/mediawiki/libwdi/index.php?title=WCID_devices"
#define DARK_BLUE                   RGB(0,0,125)
#define BLACK                       RGB(0,0,0)
#define LIGHT_GREY                  RGB(248,248,248)
#define SEPARATOR_GREY              RGB(223,223,223)
#define WHITE                       RGB(255,255,255)
#define GREEN                       RGB(232,255,232)
#define RED                         RGB(255,207,207)

// These are used to flag end users about the driver they are going to replace
enum driver_type {
	DT_SYSTEM,
	DT_LIBUSB,
	DT_UNKNOWN,
	DT_NONE,
	NB_DRIVER_TYPES,
};

// For our custom notifications
enum message_type {
	MSG_INFO,
	MSG_WARNING,
	MSG_ERROR
};

// WM_APP is not sent on focus, unlike WM_USER
enum user_message_type {
	UM_REFRESH_LIST = WM_APP,
	UM_DEVICE_EVENT,
	UM_LOGGER_EVENT
};

// Windows versions
enum windows_version {
	WINDOWS_UNDEFINED,
	WINDOWS_UNSUPPORTED,
	WINDOWS_2K,
	WINDOWS_XP,
	WINDOWS_2003_XP64,
	WINDOWS_VISTA,
	WINDOWS_7
};

// WCID states
enum wcid_state {
	WCID_NONE,
	WCID_FALSE,
	WCID_TRUE,
};

#define safe_free(p) do {if ((void*)p != NULL) {free((void*)p); p = NULL;}} while(0)
#define safe_min(a, b) min((size_t)(a), (size_t)(b))
#define safe_strcp(dst, dst_max, src, count) do {memcpy(dst, src, safe_min(count, dst_max)); \
	((char*)dst)[safe_min(count, dst_max)-1] = 0;} while(0)
#define safe_strcpy(dst, dst_max, src) safe_strcp(dst, dst_max, src, safe_strlen(src)+1)
#define safe_strncat(dst, dst_max, src, count) strncat(dst, src, safe_min(count, dst_max - safe_strlen(dst) - 1))
#define safe_strcat(dst, dst_max, src) safe_strncat(dst, dst_max, src, safe_strlen(src)+1)
#define safe_strcmp(str1, str2) strcmp(((str1==NULL)?"<NULL>":str1), ((str2==NULL)?"<NULL>":str2))
#define safe_stricmp(str1, str2) _stricmp(((str1==NULL)?"<NULL>":str1), ((str2==NULL)?"<NULL>":str2))
#define safe_strncmp(str1, str2, count) strncmp(((str1==NULL)?"<NULL>":str1), ((str2==NULL)?"<NULL>":str2), count)
#define safe_closehandle(h) do {if (h != INVALID_HANDLE_VALUE) {CloseHandle(h); h = INVALID_HANDLE_VALUE;}} while(0)
#define safe_sprintf _snprintf
#define safe_strlen(str) ((((char*)str)==NULL)?0:strlen(str))
#define safe_strdup _strdup
#define MF_CHECK(cond) ((cond)?MF_CHECKED:MF_UNCHECKED)

#if defined(_MSC_VER)
#define safe_vsnprintf(buf, size, format, arg) _vsnprintf_s(buf, size, _TRUNCATE, format, arg)
#else
#define safe_vsnprintf vsnprintf
#endif

/*
 * Shared prototypes
 */
#define dprintf(...) w_printf(false, __VA_ARGS__)
#define dsprintf(...) w_printf(true, __VA_ARGS__)
void detect_windows_version(void);
void w_printf(bool update_status, const char *format, ...);
void browse_for_folder(void);
char* file_dialog(bool save, char* path, char* filename, char* ext, char* ext_desc);
bool file_io(bool save, char* path, char** buffer, DWORD* size);
INT_PTR CALLBACK about_callback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
void create_status_bar(void);
void notification(int type, char* text, char* title);
int run_with_progress_bar(int(*function)(void));
char* to_valid_filename(char* name, char* ext);
HWND create_tooltip(HWND hWnd, char* message, int duration);

/*
 * Globals
 */
extern HINSTANCE main_instance;
extern HWND hDeviceList;
extern HWND hMain;
extern HWND hInfo;
extern HWND hStatus;
extern char extraction_path[MAX_PATH];

/*
 * Redefs
 */
#if (_WIN32_WINNT < 0x0600)
typedef struct
{
	NMHDR	hdr;
	RECT	rcButton;
} NMBCDROPDOWN, *LPNMBCDROPDOWN;
#endif
#if !defined(BCN_DROPDOWN)
#define BCN_DROPDOWN (0U-1248U)
#endif
#if !defined(BCM_SETIMAGELIST)
#define BCM_SETIMAGELIST        (0x1602)
#endif

typedef HIMAGELIST (WINAPI *ImageList_Create_t)(
	int cx,
	int cy,
	UINT flags,
	int cInitial,
	int cGrow
);

typedef int (WINAPI *ImageList_ReplaceIcon_t)(
	HIMAGELIST himl,
	int i,
	HICON hicon
);