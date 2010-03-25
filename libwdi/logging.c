/*
 * libwdi logging functions - copied from libusb:
 * Copyright (c) Johannes Erdfelt, Daniel Drake et al.
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

#include <config.h>
#include <stdarg.h>
#include <stdio.h>
#include "logging.h"

struct libusb_context *usbi_default_context = NULL;

void usbi_log_v(struct libusb_context *ctx, enum usbi_log_level level,
	const char *function, const char *format, va_list args)
{
	FILE *stream = stdout;
	const char *prefix;

// TODO: init default context
#ifndef ENABLE_DEBUG_LOGGING
	USBI_GET_CONTEXT(ctx);
	if (!ctx->debug)
		return;
	if (level == LOG_LEVEL_WARNING && ctx->debug < 2)
		return;
	if (level == LOG_LEVEL_INFO && ctx->debug < 3)
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

void usbi_log(struct libusb_context *ctx, enum usbi_log_level level,
	const char *function, const char *format, ...)
{
	va_list args;

	va_start (args, format);
	usbi_log_v(ctx, level, function, format, args);
	va_end (args);
}
