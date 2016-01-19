/*
 * Zadig: Automated Driver Installer for USB devices (GUI version)
 * Copyright (c) 2010-2016 Pete Batard <pete@akeo.ie>
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

#pragma once

#if defined(_MSC_VER)
// disable MSVC warnings that are benign
#pragma warning(disable:4100)  // unreferenced formal parameter
#pragma warning(disable:4127)  // conditional expression is constant
#pragma warning(disable:4201)  // nameless struct/union
#pragma warning(disable:4214)  // bit field types other than int
#pragma warning(disable:4996)  // deprecated API calls
#pragma warning(disable:28159) // more deprecated API calls
#endif

#ifndef ARRAYSIZE
#define ARRAYSIZE(A)                (sizeof(A)/sizeof((A)[0]))
#endif
#define _IGNORE(expr)               do { (void)(expr); } while(0)

#define APPLICATION_NAME            "Zadig"
#define COMPANY_NAME                "Akeo Consulting"
#define APPLICATION_URL             "http://zadig.akeo.ie"
#define STR_BUFFER_SIZE             512
#define NOTIFICATION_DELAY          1000
#define MAX_TOOLTIPS                32
#define MAX_LOG_SIZE                0x7FFFFFFE
#define MAX_PROGRESS                (0xFFFF-1)
#define INI_NAME                    "zadig.ini"
#define LIBWDI_URL                  "http://libwdi.akeo.ie"
#define LIBUSB_URL                  "http://windows.libusb.info"
#define LIBUSB0_URL                 "http://sourceforge.net/apps/trac/libusb-win32/wiki"
#define LIBUSBK_URL                 "http://code.google.com/p/usb-travis/"
#define WINUSB_URL                  "http://msdn.microsoft.com/en-us/library/windows/hardware/ff540174.aspx"
#define HELP_URL                    "https://github.com/pbatard/libwdi/wiki/Zadig"
#define WCID_URL                    "https://github.com/pbatard/libwdi/wiki/WCID-Devices"
#define USB_IDS_URL                 "http://www.linux-usb.org/usb-ids.html"
#define DARK_BLUE                   RGB(0,0,125)
#define BLACK                       RGB(0,0,0)
#define WHITE                       RGB(255,255,255)
#define LIGHT_GREY                  RGB(248,248,248)
#define SEPARATOR_GREY              RGB(223,223,223)
#define FIELD_GREEN                 RGB(232,255,232)
#define FIELD_ORANGE                RGB(255,240,200)
#define ARROW_GREEN                 RGB(92,228,65)
#define ARROW_ORANGE                RGB(253,143,56)
#define APP_VERSION                 "Zadig 2.2.685"

// These are used to flag end users about the driver they are going to replace
enum driver_type {
	DT_SYSTEM,
	DT_LIBUSB,
	DT_UNKNOWN,
	DT_NONE,
	NB_DRIVER_TYPES,
};

// For our custom notifications
enum notification_type {
	MSG_INFO,
	MSG_WARNING,
	MSG_ERROR,
	MSG_QUESTION,
};
typedef INT_PTR (CALLBACK *Callback_t)(HWND, UINT, WPARAM, LPARAM);
typedef struct {
	WORD id;
	Callback_t callback;
} notification_info;	// To provide a "More info..." on notifications

// WM_APP is not sent on focus, unlike WM_USER
enum user_message_type {
	UM_REFRESH_LIST = WM_APP,
	UM_DEVICE_EVENT,
	UM_LOGGER_EVENT,
	UM_DOWNLOAD_INIT,
	UM_DOWNLOAD_EXIT,
};

// WCID states
enum wcid_state {
	WCID_NONE,
	WCID_FALSE,
	WCID_TRUE,
};

// Timers
#define TID_MESSAGE 0x1000

typedef struct {
	WORD version[4];
	DWORD platform_min[2];		// minimum platform version required
	char* download_url;
	char* release_notes;
} APPLICATION_UPDATE;

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
#define safe_sprintf(dst, count, ...) do {_snprintf(dst, count, __VA_ARGS__); (dst)[(count)-1] = 0; } while(0)
#define safe_strlen(str) ((((char*)str)==NULL)?0:strlen(str))
#define safe_strdup _strdup
#define MF_CHECK(cond) ((cond)?MF_CHECKED:MF_UNCHECKED)
#define IGNORE_RETVAL(expr) do { (void)(expr); } while(0)

#if defined(_MSC_VER)
#define safe_vsnprintf(buf, size, format, arg) _vsnprintf_s(buf, size, _TRUNCATE, format, arg)
#else
#define safe_vsnprintf vsnprintf
#endif

/*
 * Shared prototypes
 */
#define dprintf(...) w_printf(FALSE, __VA_ARGS__)
#define dsprintf(...) w_printf(TRUE, __VA_ARGS__)
#define vuprintf(...) if (verbose) w_printf(FALSE, __VA_ARGS__)
#define vvuprintf(...) if (verbose > 1) w_printf(FALSE, __VA_ARGS__)
void print_status(unsigned int duration, BOOL debug, const char* message);
int detect_windows_version(void);
void w_printf(BOOL update_status, const char *format, ...);
void browse_for_folder(void);
char* file_dialog(BOOL save, char* path, char* filename, char* ext, char* ext_desc);
BOOL file_io(BOOL save, char* path, char** buffer, DWORD* size);
INT_PTR CALLBACK about_callback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK UpdateCallback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
void create_status_bar(void);
BOOL is_x64(void);
BOOL notification(int type, const notification_info* more_info, char* title, char* format, ...);
int run_with_progress_bar(int(*function)(void));
char* to_valid_filename(char* name, char* ext);
HWND create_tooltip(HWND hWnd, char* message, int duration);
void destroy_tooltip(HWND hWnd);
void destroy_all_tooltips(void);
void set_title_bar_icon(HWND hDlg);
const char *WindowsErrorString(void);
void download_new_version(void);
void parse_update(char* buf, size_t len);
BOOL DownloadFile(const char* url, const char* file, HWND hProgressDialog);
HANDLE DownloadFileThreaded(const char* url, const char* file, HWND hProgressDialog);
BOOL SetUpdateCheck(void);
BOOL CheckForUpdates(BOOL force);

/*
 * Globals
 */
extern HINSTANCE main_instance;
extern HWND hDeviceList;
extern HWND hMain;
extern HWND hInfo;
extern HWND hStatus;
extern WORD application_version[4];
extern DWORD download_error;
extern char extraction_path[MAX_PATH], app_dir[MAX_PATH];
extern int dialog_showing;
extern BOOL installation_running;
extern APPLICATION_UPDATE update;

/* Helper functions to access DLLs */
static __inline HMODULE GetDLLHandle(char* szDLLName)
{
	HANDLE h = GetModuleHandleA(szDLLName);
	if (h == NULL) {
		// coverity[alloc_fn][var_assign]
		h = LoadLibraryA(szDLLName);
	}
	return h;
}
#define PF_TYPE(api, ret, proc, args) typedef ret (api *proc##_t)args
#define PF_DECL(proc) static proc##_t pf##proc = NULL
#define PF_TYPE_DECL(api, ret, proc, args) PF_TYPE(api, ret, proc, args);  PF_DECL(proc)
#define PF_INIT(proc, dllname) if (pf##proc == NULL) pf##proc = (proc##_t) GetProcAddress(GetDLLHandle(#dllname), #proc)
#define PF_INIT_OR_OUT(proc, dllname) PF_INIT(proc, dllname); if (pf##proc == NULL) { \
	dprintf("Unable to locate %s() in %s\n", #proc, #dllname); goto out; }

#ifndef ARRAYSIZE
#define ARRAYSIZE(A) (sizeof(A)/sizeof((A)[0]))
#endif

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
#if !defined(PBS_MARQUEE)
#define PBS_MARQUEE 0x08
#endif
#if !defined(PBM_SETMARQUEE)
#define PBM_SETMARQUEE (WM_USER+10)
#endif
