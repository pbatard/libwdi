/*
 * Zadig: Automated Driver Installer for USB devices (GUI version)
 * Elementary Unicode compliant find/replace parser
 * Copyright © 2012-2014 Pete Batard <pete@akeo.ie>
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

/* Memory leaks detection - define _CRTDBG_MAP_ALLOC as preprocessor macro */
#ifdef _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include <malloc.h>
#include <io.h>
#include <fcntl.h>

#include "msapi_utf8.h"
#include "zadig.h"

static const wchar_t wspace[] = L" \t";

/*
 * Parse a line of UTF-16 text and return the data if it matches the 'token'
 * The parsed line is of the form: [ ]token[ ]=[ ]["]data["][ ] and is 
 * modified by the parser
 */
static wchar_t* get_token_data_line(const wchar_t* wtoken, wchar_t* wline)
{
	size_t i, r;
	BOOLEAN quoteth = FALSE;

	if ((wtoken == NULL) || (wline == NULL) || (wline[0] == 0))
		return NULL;

	i = 0;

	// Skip leading spaces
	i += wcsspn(&wline[i], wspace);

	// Our token should begin a line
	if (_wcsnicmp(&wline[i], wtoken, wcslen(wtoken)) != 0)
		return NULL;

	// Token was found, move past token
	i += wcslen(wtoken);

	// Skip spaces
	i += wcsspn(&wline[i], wspace);

	// Check for an equal sign
	if (wline[i] != L'=') 
		return NULL;
	i++;

	// Skip spaces after equal sign
	i += wcsspn(&wline[i], wspace);

	// eliminate leading quote, if it exists
	if (wline[i] == L'"') {
		quoteth = TRUE;
		i++;
	}

	// Keep the starting pos of our data
	r = i;

	// locate end of string or quote
	while ( (wline[i] != 0) && ((wline[i] != L'"') || ((wline[i] == L'"') && (!quoteth))) )
		i++;
	wline[i--] = 0;

	// Eliminate trailing EOL characters
	while ((i>=r) && ((wline[i] == L'\r') || (wline[i] == L'\n')))
		wline[i--] = 0;

	return (wline[r] == 0)?NULL:&wline[r];
}

/*
 * Parse a file (ANSI or UTF-8 or UTF-16) and return the data for the first occurrence of 'token'
 * The returned string is UTF-8 and MUST be freed by the caller
 */
char* get_token_data_file(const char* token, const char* filename)
{
	wchar_t *wtoken = NULL, *wdata= NULL, *wfilename = NULL;
	wchar_t buf[1024];
	FILE* fd = NULL;
	char *ret = NULL;

	if ((filename == NULL) || (token == NULL))
		return NULL;
	if ((filename[0] == 0) || (token[0] == 0))
		return NULL;

	wfilename = utf8_to_wchar(filename);
	if (wfilename == NULL) {
		dprintf("Could not convert '%s' to UTF-16\n", filename);
		goto out;
	}
	wtoken = utf8_to_wchar(token);
	if (wfilename == NULL) {
		dprintf("Could not convert '%s' to UTF-16\n", token);
		goto out;
	}
	fd = _wfopen(wfilename, L"r, ccs=UNICODE");
	if (fd == NULL) goto out;

	// Process individual lines. NUL is always appended.
	// Ideally, we'd check that our buffer fits the line
	while (fgetws(buf, ARRAYSIZE(buf), fd) != NULL) {
		wdata = get_token_data_line(wtoken, buf);
		if (wdata != NULL) {
			ret = wchar_to_utf8(wdata);
			break;
		}
	}

out:
	if (fd != NULL)
		fclose(fd);
	safe_free(wfilename);
	safe_free(wtoken);
	return ret;
}

/*
 * Parse a buffer (ANSI or UTF-8) and return the data for the 'n'th occurrence of 'token'
 * The returned string is UTF-8 and MUST be freed by the caller
 */
char* get_token_data_buffer(const char* token, unsigned int n, const char* buffer, size_t buffer_size)
{
	unsigned int j, curly_count;
	wchar_t *wtoken = NULL, *wdata = NULL, *wbuffer = NULL, *wline = NULL;
	size_t i;
	BOOL done = FALSE;
	char* ret = NULL;

	// We're handling remote data => better safe than sorry
	if ((token == NULL) || (buffer == NULL) || (buffer_size <= 4) || (buffer_size > 65536))
		goto out;

	// Ensure that our buffer is NUL terminated
	if (buffer[buffer_size-1] != 0)
		goto out;

	wbuffer = utf8_to_wchar(buffer);
	wtoken = utf8_to_wchar(token);
	if ((wbuffer == NULL) || (wtoken == NULL))
		goto out;

	// Process individual lines (or multiple lines when between {}, for RTF)
	for (i=0,j=0,done=FALSE; (j!=n)&&(!done); ) {
		wline = &wbuffer[i];

		for(curly_count=0;((curly_count>0)||((wbuffer[i]!=L'\n')&&(wbuffer[i]!=L'\r')))&&(wbuffer[i]!=0);i++) {
			if (wbuffer[i] == L'{') curly_count++;
			if (wbuffer[i] == L'}') curly_count--;
		}
		if (wbuffer[i]==0) {
			done = TRUE;
		} else {
			wbuffer[i++] = 0;
		}
		wdata = get_token_data_line(wtoken, wline);
		if (wdata != NULL) {
			j++;
		}
	}
out:
	if (wdata != NULL)
		ret = wchar_to_utf8(wdata);
	safe_free(wbuffer);
	safe_free(wtoken);
	return ret;
}

static __inline char* get_sanitized_token_data_buffer(const char* token, unsigned int n, const char* buffer, size_t buffer_size)
{
	size_t i;
	char* data = get_token_data_buffer(token, n, buffer, buffer_size);
	if (data != NULL) {
		for (i=0; i<strlen(data); i++) {
			if ((data[i] == '\\') && (data[i+1] == 'n')) {
				data[i] = '\r';
				data[i+1] = '\n';
			}
		}
	}
	return data;
}

/*
 * Parse an update data file and populates an update structure.
 * NB: since this is remote data, and we're running elevated, it *IS* considered
 * potentially malicious, even if it comes from a supposedly trusted server.
 * len should be the size of the buffer, including the zero terminator
 */
void parse_update(char* buf, size_t len)
{
	size_t i;
	char *data = NULL, *token;
	char allowed_rtf_chars[] = "abcdefghijklmnopqrstuvwxyz|~-_:*'";
	char allowed_std_chars[] = "\r\n ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!\"$%^&+=<>(){}[].,;#@/?";

	// strchr includes the NUL terminator in the search, so take care of backslash before NUL
	if ((buf == NULL) || (len < 2) || (len > 65536) || (buf[len-1] != 0) || (buf[len-2] == '\\'))
		return;
	// Sanitize the data - Not a silver bullet, but it helps
	len = safe_strlen(buf)+1;	// Someone may be inserting NULs
	for (i=0; i<len-1; i++) {
		// Check for valid RTF sequences as well as allowed chars if not RTF
		if (buf[i] == '\\') {
			// NB: we have a zero terminator, so we can afford a +1 without overflow
			if (strchr(allowed_rtf_chars, buf[i+1]) == NULL) {
				buf[i] = ' ';
			}
		} else if ((strchr(allowed_rtf_chars, buf[i]) == NULL) && (strchr(allowed_std_chars, buf[i]) == NULL)) {
			buf[i] = ' ';
		}
	}

	for (i=0; i<4; i++)
		update.version[i] = 0;
	update.platform_min[0] = 5;
	update.platform_min[1] = 2;	// XP or later
	safe_free(update.download_url);
	safe_free(update.release_notes);
	if ((data = get_sanitized_token_data_buffer("version", 1, buf, len)) != NULL) {
		for (i=0; (i<4) && ((token = strtok((i==0)?data:NULL, ".")) != NULL); i++) {
			update.version[i] = (WORD)atoi(token);
		}
		safe_free(data);
	}
	if ((data = get_sanitized_token_data_buffer("platform_min", 1, buf, len)) != NULL) {
		for (i=0; (i<2) && ((token = strtok((i==0)?data:NULL, ".")) != NULL); i++) {
			update.platform_min[i] = (DWORD)atoi(token);
		}
		safe_free(data);
	}
	update.download_url = get_sanitized_token_data_buffer("download_url", 1, buf, len);
	update.release_notes = get_sanitized_token_data_buffer("release_notes", 1, buf, len);
}
