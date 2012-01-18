/*
* 7z.c: 7z handling from memory buffer
*
* Copyright (c) 2011 Pete Batard <pete@akeo.ie>
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

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <windows.h>
#include <setupapi.h>
#include <config.h>

#include "libwdi.h"
#include "installer.h"
#include "logging.h"
#include "../common/7z/7z.h"
#include "../common/7z/Types.h"
#include "../common/7z/7zAlloc.h"
#include "../common/7z/7zCrc.h"
#include "../common/7z/7zFile.h"
#include "../common/msapi_utf8.h"

#ifndef USE_WINDOWS_FILE
/* for mkdir */
// TODO: remove this and assume dir already exists
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <errno.h>
#endif
#endif

/* Buffer IStream implementation */
typedef struct
{
	ISeekInStream s;
	const unsigned char *buffer;
	size_t curpos;
	size_t size;
} BufferInStream;

static WRes Buffer_Read(BufferInStream *p, void *data, size_t *size)
{
	size_t actualSize;
	if (*size == 0)
		return 0;

	actualSize = min(*size, p->size - p->curpos);

	memcpy(data, &p->buffer[p->curpos], actualSize);
	p->curpos += actualSize;

	if (*size == actualSize)
		return 0;
	return EOF;
}

static WRes Buffer_Seek(BufferInStream *p, Int64 *pos, ESzSeek origin)
{
	int res;
	switch (origin)
	{
	case SZ_SEEK_SET: p->curpos = (size_t)*pos; break;
	case SZ_SEEK_CUR: p->curpos += (size_t)*pos; break;
	case SZ_SEEK_END: p->curpos = p->size + (size_t)*pos; break;
	default: return 1;
	}
	if (p->curpos > p->size) p->curpos = p->size;
	res = 0;
	*pos = (Int64)p->curpos;
	return res;
}

static SRes BufferInStream_Read(void *pp, void *buf, size_t *size)
{
	BufferInStream *p = (BufferInStream *)pp;
	return (Buffer_Read(p, buf, size) == 0) ? SZ_OK : SZ_ERROR_READ;
}

static SRes BufferInStream_Seek(void *pp, Int64 *pos, ESzSeek origin)
{
	BufferInStream *p = (BufferInStream *)pp;
	return Buffer_Seek(p, pos, origin);
}

static void BufferInStream_CreateVTable(BufferInStream *p)
{
	p->s.Read = BufferInStream_Read;
	p->s.Seek = BufferInStream_Seek;
}

static void Buffer_Init(BufferInStream *p, const unsigned char *buffer, size_t size)
{
	p->buffer = buffer;
	p->size = size;
	p->curpos = 0;
}

/* 7z operations */
static CSzArEx db;
static ISzAlloc allocImp = {SzAlloc, SzFree};
static ISzAlloc allocTempImp = {SzAllocTemp, SzFreeTemp};
static CLookToRead lookStream;
static uint32_t blockIndex;
static BufferInStream archiveStream;

/* Open an archive in memory */
int _7z_open(const uint8_t* arch, const size_t arch_size)
{
	int r;

	if ( (arch == NULL) || (arch_size < 8) ) {
		return WDI_ERROR_INVALID_PARAM;
	}

	blockIndex = 0xFFFFFFFF;
	Buffer_Init(&archiveStream, arch, arch_size);
	BufferInStream_CreateVTable(&archiveStream);
	LookToRead_CreateVTable(&lookStream, False);
	lookStream.realStream = &archiveStream.s;
	LookToRead_Init(&lookStream);

	CrcGenerateTable();

	SzArEx_Init(&db);
	r = SzArEx_Open(&db, &lookStream.s, &allocImp, &allocTempImp);
	return (r == SZ_OK)?WDI_SUCCESS:WDI_ERROR_ACCESS;
}

void _7z_close(void)
{
	SzArEx_Free(&db, &allocImp);
}

/*
 * id: id in archive ([0x00-0x7F] ASCII only)
 * buf: pointer to a pointer to the destination buffer
 * buf_size: extracted data size
 * buffer will be allocated and needs to be freed by calling _7z_free()
 */
int _7z_extract(const char* id, uint8_t** buf, size_t* buf_size)
{
	uint32_t i;
	int r, ret = 0;
	uint16_t *wtemp = NULL, *wid = NULL;
	size_t offset, len, outSizeProcessed;
	const CSzFileItem *f;

	if ( (id == NULL) || (buf == NULL) || (buf_size == NULL) ) {
		return WDI_ERROR_INVALID_PARAM;
	}
	*buf_size = 0;

	wtemp = (uint16_t*)calloc(MAX_PATH, sizeof(wtemp[0]));
	wid = (uint16_t*)calloc(safe_strlen(id)+1, sizeof(wid[0]));
	if ((wid == NULL) || (wtemp == NULL)) {
		ret = WDI_ERROR_RESOURCE;
		goto out;
	}
	for (i=0; i<safe_strlen(id); i++) {
		wid[i] = id[i];	// All ids are in the lower ASCII table
	}

	for (i=0; i<db.db.NumFiles; i++) {
		f = db.db.Files + i;
		if (f->IsDir) continue;

		len = SzArEx_GetFileNameUtf16(&db, i, NULL);
		if (len > MAX_PATH) {
			ret = WDI_ERROR_RESOURCE;
			goto out;
		}
		SzArEx_GetFileNameUtf16(&db, i, wtemp);
		if (memcmp(wid, wtemp, len*sizeof(wid[0])) != 0) continue;

		r = SzArEx_Extract(&db, &lookStream.s, i, &blockIndex, buf, buf_size,
			&offset, &outSizeProcessed, &allocImp, &allocTempImp);
		switch (r) {
		case SZ_OK:
			wdi_err("succesfully extracted '%s'", id);
			ret = WDI_SUCCESS;
			break;
		case SZ_ERROR_UNSUPPORTED:
			wdi_err("decoder doesn't support this archive format");
			ret = WDI_ERROR_NOT_SUPPORTED;
			goto out;
		case SZ_ERROR_MEM:
			ret = WDI_ERROR_RESOURCE;
			goto out;
		case SZ_ERROR_CRC:
			wdi_err("CRC error on wdi archive file '%s'", id);
			ret = WDI_ERROR_IO;
			goto out;
		default:
			wdi_err("7z error extracting archive file '%s': #%d", id, r);
			ret = WDI_ERROR_OTHER;
			goto out;
		}
	}

out:
	safe_free(wid);
	safe_free(wtemp);

	return ret;
}

void _7z_free(uint8_t* buf)
{
	IAlloc_Free(&allocImp, buf);
}
