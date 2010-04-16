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
#pragma once

#define LOGGER_PIPE_NAME           "\\\\.\\pipe\\libwdi-logger"
#define LOGBUF_SIZE                256

#if defined(_MSC_VER)
#define safe_vsnprintf vsprintf_s
#define safe_snprintf sprintf_s
#else
#define safe_vsnprintf vsnprintf
#define safe_snprintf snprintf
#endif


#if !defined(_MSC_VER) || _MSC_VER > 1200

#ifdef ENABLE_LOGGING
#define _wdi_log(level, ...) wdi_log(level, __FUNCTION__, __VA_ARGS__)
#else
#define _wdi_log(level, ...)
#endif

#ifdef ENABLE_DEBUG_LOGGING
#define wdi_dbg(...) _wdi_log(LOG_LEVEL_DEBUG, __VA_ARGS__)
#else
#define wdi_dbg(...)
#endif

#define wdi_info(ctx, ...) _wdi_log(LOG_LEVEL_INFO, __VA_ARGS__)
#define wdi_warn(ctx, ...) _wdi_log(LOG_LEVEL_WARNING, __VA_ARGS__)
#define wdi_err(ctx, ...) _wdi_log(LOG_LEVEL_ERROR, __VA_ARGS__)

#else /* !defined(_MSC_VER) || _MSC_VER > 1200 */

void wdi_log_v(enum wdi_log_level level,
	const char *function, const char *format, va_list args);

#ifdef ENABLE_LOGGING
#define LOG_BODY(level)       \
{                             \
	va_list args;             \
	va_start (args, format);  \
	wdi_log_v(level, "", format, args); \
	va_end(args);             \
}
#else
#define LOG_BODY(level) { }
#endif

void inline wdi_info(const char *format, ...)
	LOG_BODY(LOG_LEVEL_INFO)
void inline wdi_warn(const char *format, ...)
	LOG_BODY(LOG_LEVEL_WARNING)
void inline wdi_err( const char *format, ...)
	LOG_BODY(LOG_LEVEL_ERROR)

void inline wdi_dbg(const char *format, ...)
#ifdef ENABLE_DEBUG_LOGGING
	LOG_BODY(LOG_LEVEL_DEBUG)
#else
{ }
#endif

#endif /* !defined(_MSC_VER) || _MSC_VER > 1200 */

extern void wdi_log(enum wdi_log_level level, const char *function, const char *format, ...);
