/*
 * libwdi logging functions
 * Copyright (c) Johannes Erdfelt, Daniel Drake et al.
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
#include <config.h>
#include <stdarg.h>
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <stdint.h>
#include "libwdi.h"
#include "logging.h"

HANDLE logger_rd_handle = INVALID_HANDLE_VALUE;
HANDLE logger_wr_handle = INVALID_HANDLE_VALUE;
// Handle and Message for the destination Window when registered
HWND logger_dest = NULL;
UINT logger_msg = 0;
// Detect spurious log readouts
unsigned log_messages_pending = 0;
// Global debug level
static int global_log_level = LOG_LEVEL_WARNING;

extern char *windows_error_str(uint32_t retval);
int create_logger(void);

void pipe_wdi_log_v(enum wdi_log_level level,
	const char *function, const char *format, va_list args)
{
	char buffer[LOGBUF_SIZE];
	DWORD junk;
	int size1, size2;
	const char *prefix;

	if (logger_wr_handle == INVALID_HANDLE_VALUE)
		return;

#ifndef ENABLE_DEBUG_LOGGING
	if (level < global_log_level)
		return;
#endif

	switch (level) {
	case LOG_LEVEL_INFO:
		prefix = "info";
		break;
	case LOG_LEVEL_WARNING:
		prefix = "warning";
		break;
	case LOG_LEVEL_ERROR:
		prefix = "error";
		break;
	case LOG_LEVEL_DEBUG:
		prefix = "debug";
		break;
	default:
		prefix = "unknown";
		break;
	}

	size1 = safe_snprintf(buffer, LOGBUF_SIZE, "libwdi:%s [%s] ", prefix, function);
	size2 = 0;
	if (size1 < 0) {
		buffer[LOGBUF_SIZE-1] = 0;
		size1 = LOGBUF_SIZE-1;
	} else {
		size2 = safe_vsnprintf(buffer+size1, LOGBUF_SIZE-size1, format, args);
		if (size2 < 0) {
			buffer[LOGBUF_SIZE-1] = 0;
			size2 = LOGBUF_SIZE-1-size1;
		}
	}
	WriteFile(logger_wr_handle, buffer, (DWORD)(size1+size2+1), &junk, NULL);

	// Notify the destination window of a new log message
	// TODO: use wparam for error/debug/etc
	log_messages_pending++;
	// SendMessage ensures that log message does not occur after the function has returned
	SendMessage(logger_dest, logger_msg, level, 0);
}

void console_wdi_log_v(enum wdi_log_level level,
	const char *function, const char *format, va_list args)
{
	FILE *stream;
	const char *prefix;

	stream = stdout;

#ifndef ENABLE_DEBUG_LOGGING
	if (level < global_log_level)
		return;
#endif

	switch (level) {
	case LOG_LEVEL_INFO:
		prefix = "info";
		break;
	case LOG_LEVEL_WARNING:
		stream = stderr;
		prefix = "warning";
		break;
	case LOG_LEVEL_ERROR:
		stream = stderr;
		prefix = "error";
		break;
	case LOG_LEVEL_DEBUG:
		stream = stderr;
		prefix = "debug";
		break;
	default:
		stream = stderr;
		prefix = "unknown";
		break;
	}

	fprintf(stream, "libwdi:%s [%s] ", prefix, function);

	vfprintf(stream, format, args);

	fprintf(stream, "\n");

}

void wdi_log(enum wdi_log_level level,
	const char *function, const char *format, ...)
{
	va_list args;

	va_start (args, format);
	if (logger_dest != NULL) {
		pipe_wdi_log_v(level, function, format, args);
	} else {
		console_wdi_log_v(level, function, format, args);
	}
	va_end (args);
}

// Create a synchronous pipe for messaging
int create_logger(void)
{
	if (logger_wr_handle != INVALID_HANDLE_VALUE) {
		// We (supposedly) don't have logging, so try to reach a stderr
		fprintf(stderr, "trying to recreate logger pipe\n");
		return WDI_ERROR_EXISTS;
	}

	// Read end of the pipe
	logger_rd_handle = CreateNamedPipe(LOGGER_PIPE_NAME, PIPE_ACCESS_INBOUND,
		PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE, 1, 4096, 4096, 0, NULL);
	if (logger_rd_handle == INVALID_HANDLE_VALUE) {
		fprintf(stderr, "could not create logger pipe for reading: %s\n", windows_error_str(0));
		return WDI_ERROR_RESOURCE;
	}

	// Write end of the pipe
	logger_wr_handle = CreateFile(LOGGER_PIPE_NAME, GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, NULL);
	if (logger_wr_handle == INVALID_HANDLE_VALUE) {
		fprintf(stderr, "could not create logger pipe for writing: %s\n", windows_error_str(0));
		CloseHandle(logger_rd_handle);
		logger_rd_handle = INVALID_HANDLE_VALUE;
		return WDI_ERROR_RESOURCE;
	}

	log_messages_pending = 0;

	return WDI_SUCCESS;
}

// Destroy the logging pipe
void destroy_logger(void)
{
	if (logger_wr_handle != INVALID_HANDLE_VALUE) {
		CloseHandle(logger_wr_handle);
		logger_wr_handle = INVALID_HANDLE_VALUE;
	}
	if (logger_rd_handle != INVALID_HANDLE_VALUE) {
		CloseHandle(logger_rd_handle);
		logger_rd_handle = INVALID_HANDLE_VALUE;
	}
}

/*
 * Register a Window as destination for logging message
 * This Window will be notified with a message event and should call
 * wdi_read_logger() to retreive the message data
 */
int LIBWDI_API wdi_register_logger(HWND hWnd, UINT message)
{
	int r;

	if (logger_dest != NULL) {
		return WDI_ERROR_EXISTS;
	}

	r = create_logger();
	if (r == WDI_SUCCESS) {
		logger_dest = hWnd;
		logger_msg = message;
	}

	return r;
}

/*
 * Unregister a Window as destination for logging message
 */
int LIBWDI_API wdi_unregister_logger(HWND hWnd)
{
	if (logger_dest == NULL) {
		return WDI_SUCCESS;
	}

	if (logger_dest != hWnd) {
		return WDI_ERROR_INVALID_PARAM;
	}

	destroy_logger();
	logger_dest = NULL;
	logger_msg = 0;

	return WDI_SUCCESS;
}

/*
 * Read a log message
 */
int LIBWDI_API wdi_read_logger(char* buffer, DWORD buffer_size, DWORD* message_size)
{
	int size;

	if ( (logger_rd_handle == INVALID_HANDLE_VALUE) && (create_logger() != WDI_SUCCESS) ) {
		*message_size = 0;
		return WDI_ERROR_NOT_FOUND;
	}

	if (log_messages_pending == 0) {
		size = safe_snprintf(buffer, buffer_size, "ERROR: log buffer is empty");
		if (size <0) {
			buffer[buffer_size-1] = 0;
			return buffer_size;
		}
		*message_size = (DWORD)size;
		return WDI_SUCCESS;
	}
	log_messages_pending--;

	// TODO: use a flag to prevent readout if no data
	if (ReadFile(logger_rd_handle, (void*)buffer, buffer_size, message_size, NULL)) {
		// TODO: add LF?
		return WDI_SUCCESS;
	}

	*message_size = 0;
	return WDI_ERROR_IO;
}

/*
 * Set the global log level. Only works if ENABLE_DEBUG_LOGGING is not set
 */
int LIBWDI_API wdi_set_log_level(int level)
{
#if defined(ENABLE_DEBUG_LOGGING)
	return WDI_ERROR_NOT_SUPPORTED;
#endif
	if ( (level < LOG_LEVEL_DEBUG) || (level > LOG_LEVEL_ERROR) ) {
		return WDI_ERROR_INVALID_PARAM;
	}
	global_log_level = level;
	return WDI_SUCCESS;
}
