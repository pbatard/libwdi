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
#define _usbi_log(ctx, level, ...) usbi_log(ctx, level, __FUNCTION__, __VA_ARGS__)
#else
#define _usbi_log(ctx, level, ...)
#endif

#ifdef ENABLE_DEBUG_LOGGING
#define usbi_dbg(...) _usbi_log(NULL, LOG_LEVEL_DEBUG, __VA_ARGS__)
#else
#define usbi_dbg(...)
#endif

#define usbi_info(ctx, ...) _usbi_log(ctx, LOG_LEVEL_INFO, __VA_ARGS__)
#define usbi_warn(ctx, ...) _usbi_log(ctx, LOG_LEVEL_WARNING, __VA_ARGS__)
#define usbi_err(ctx, ...) _usbi_log(ctx, LOG_LEVEL_ERROR, __VA_ARGS__)

#else /* !defined(_MSC_VER) || _MSC_VER > 1200 */

void usbi_log_v(struct libusb_context *ctx, enum usbi_log_level level,
	const char *function, const char *format, va_list args);

#ifdef ENABLE_LOGGING
#define LOG_BODY(ctxt, level) \
{                             \
	va_list args;             \
	va_start (args, format);  \
	usbi_log_v(ctxt, level, "", format, args); \
	va_end(args);             \
}
#else
#define LOG_BODY(ctxt, level) { }
#endif

void inline usbi_info(struct libusb_context *ctx, const char *format, ...)
	LOG_BODY(ctx,LOG_LEVEL_INFO)
void inline usbi_warn(struct libusb_context *ctx, const char *format, ...)
	LOG_BODY(ctx,LOG_LEVEL_WARNING)
void inline usbi_err( struct libusb_context *ctx, const char *format, ...)
	LOG_BODY(ctx,LOG_LEVEL_ERROR)

void inline usbi_dbg(const char *format, ...)
#ifdef ENABLE_DEBUG_LOGGING
	LOG_BODY(NULL,LOG_LEVEL_DEBUG)
#else
{ }
#endif

#endif /* !defined(_MSC_VER) || _MSC_VER > 1200 */

struct libusb_context {
	int debug;
	int debug_fixed;
};

// TODO: ensure we get an error as a reminder
//#define USBI_GET_CONTEXT(ctx) if (!(ctx)) (ctx) = usbi_default_context

extern void usbi_log(struct libusb_context *ctx, enum usbi_log_level level,
					 const char *function, const char *format, ...);
