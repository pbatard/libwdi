/*
 * cathash v1.0: MS cat file compliant SHA-1 implementation, for PE and data files
 * Copyright (c) 2011 Pete Batard <pete@akeo.ie>
 *
 * based on PolarSSL/sha1.c - Copyright (c) 2006-2010, Brainspark B.V. (GPLv2+)
 *   http://polarssl.org/trac/browser/trunk/library/sha1.c
 * based on crypto++/test.cpp - Copyright Wei Dai/MaidSafe (Public Domain)
 *   http://maidsafe-dht.googlecode.com/svn/trunk/src/maidsafe/cryptopp/test.cpp
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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

/*
 * SHA-1 context structure
 */
typedef struct
{
    uint32_t total[2];     /*!< number of bytes processed  */
    uint32_t state[5];     /*!< intermediate digest state  */
    uint8_t  buffer[64];   /*!< data block being processed */

    uint8_t  ipad[64];     /*!< HMAC: inner padding        */
    uint8_t  opad[64];     /*!< HMAC: outer padding        */
}
sha1_context;

/*
 * 32 & 16 bit integer manipulation macros
 */
#ifndef GET_ULONG_BE
#define GET_ULONG_BE(n,b,i)                        \
{                                                  \
    (n) = ( (uint32_t) (b)[(i)    ] << 24 )        \
        | ( (uint32_t) (b)[(i) + 1] << 16 )        \
        | ( (uint32_t) (b)[(i) + 2] <<  8 )        \
        | ( (uint32_t) (b)[(i) + 3]       );       \
}
#endif

#ifndef PUT_ULONG_BE
#define PUT_ULONG_BE(n,b,i)                        \
{                                                  \
    (b)[(i)    ] = (uint8_t) ( (n) >> 24 );        \
    (b)[(i) + 1] = (uint8_t) ( (n) >> 16 );        \
    (b)[(i) + 2] = (uint8_t) ( (n) >>  8 );        \
    (b)[(i) + 3] = (uint8_t) ( (n)       );        \
}
#endif

#ifndef GET_ULONG_LE
#define GET_ULONG_LE(n,b,i)                        \
{                                                  \
    (n) = ( (uint32_t) (b)[(i)    ]       )        \
        | ( (uint32_t) (b)[(i) + 1] <<  8 )        \
        | ( (uint32_t) (b)[(i) + 2] << 16 )        \
        | ( (uint32_t) (b)[(i) + 3] << 24 );       \
}
#endif

#ifndef GET_USHORT_LE
#define GET_USHORT_LE(n,b,i)                       \
{                                                  \
    (n) = ( (uint16_t) (b)[(i)    ]       )        \
        | ( (uint32_t) (b)[(i) + 1] <<  8 );       \
}
#endif

/*
 * SHA-1 context setup
 */
void sha1_starts( sha1_context *ctx )
{
    ctx->total[0] = 0;
    ctx->total[1] = 0;

    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
}

static void sha1_process( sha1_context *ctx, const uint8_t data[64] )
{
    uint32_t temp, W[16], A, B, C, D, E;

    GET_ULONG_BE( W[ 0], data,  0 );
    GET_ULONG_BE( W[ 1], data,  4 );
    GET_ULONG_BE( W[ 2], data,  8 );
    GET_ULONG_BE( W[ 3], data, 12 );
    GET_ULONG_BE( W[ 4], data, 16 );
    GET_ULONG_BE( W[ 5], data, 20 );
    GET_ULONG_BE( W[ 6], data, 24 );
    GET_ULONG_BE( W[ 7], data, 28 );
    GET_ULONG_BE( W[ 8], data, 32 );
    GET_ULONG_BE( W[ 9], data, 36 );
    GET_ULONG_BE( W[10], data, 40 );
    GET_ULONG_BE( W[11], data, 44 );
    GET_ULONG_BE( W[12], data, 48 );
    GET_ULONG_BE( W[13], data, 52 );
    GET_ULONG_BE( W[14], data, 56 );
    GET_ULONG_BE( W[15], data, 60 );

#define S(x,n) ((x << n) | ((x & 0xFFFFFFFF) >> (32 - n)))

#define R(t)                                            \
(                                                       \
    temp = W[(t -  3) & 0x0F] ^ W[(t - 8) & 0x0F] ^     \
           W[(t - 14) & 0x0F] ^ W[ t      & 0x0F],      \
    ( W[t & 0x0F] = S(temp,1) )                         \
)

#define P(a,b,c,d,e,x)                                  \
{                                                       \
    e += S(a,5) + F(b,c,d) + K + x; b = S(b,30);        \
}

    A = ctx->state[0];
    B = ctx->state[1];
    C = ctx->state[2];
    D = ctx->state[3];
    E = ctx->state[4];

#define F(x,y,z) (z ^ (x & (y ^ z)))
#define K 0x5A827999

    P( A, B, C, D, E, W[0]  );
    P( E, A, B, C, D, W[1]  );
    P( D, E, A, B, C, W[2]  );
    P( C, D, E, A, B, W[3]  );
    P( B, C, D, E, A, W[4]  );
    P( A, B, C, D, E, W[5]  );
    P( E, A, B, C, D, W[6]  );
    P( D, E, A, B, C, W[7]  );
    P( C, D, E, A, B, W[8]  );
    P( B, C, D, E, A, W[9]  );
    P( A, B, C, D, E, W[10] );
    P( E, A, B, C, D, W[11] );
    P( D, E, A, B, C, W[12] );
    P( C, D, E, A, B, W[13] );
    P( B, C, D, E, A, W[14] );
    P( A, B, C, D, E, W[15] );
    P( E, A, B, C, D, R(16) );
    P( D, E, A, B, C, R(17) );
    P( C, D, E, A, B, R(18) );
    P( B, C, D, E, A, R(19) );

#undef K
#undef F

#define F(x,y,z) (x ^ y ^ z)
#define K 0x6ED9EBA1

    P( A, B, C, D, E, R(20) );
    P( E, A, B, C, D, R(21) );
    P( D, E, A, B, C, R(22) );
    P( C, D, E, A, B, R(23) );
    P( B, C, D, E, A, R(24) );
    P( A, B, C, D, E, R(25) );
    P( E, A, B, C, D, R(26) );
    P( D, E, A, B, C, R(27) );
    P( C, D, E, A, B, R(28) );
    P( B, C, D, E, A, R(29) );
    P( A, B, C, D, E, R(30) );
    P( E, A, B, C, D, R(31) );
    P( D, E, A, B, C, R(32) );
    P( C, D, E, A, B, R(33) );
    P( B, C, D, E, A, R(34) );
    P( A, B, C, D, E, R(35) );
    P( E, A, B, C, D, R(36) );
    P( D, E, A, B, C, R(37) );
    P( C, D, E, A, B, R(38) );
    P( B, C, D, E, A, R(39) );

#undef K
#undef F

#define F(x,y,z) ((x & y) | (z & (x | y)))
#define K 0x8F1BBCDC

    P( A, B, C, D, E, R(40) );
    P( E, A, B, C, D, R(41) );
    P( D, E, A, B, C, R(42) );
    P( C, D, E, A, B, R(43) );
    P( B, C, D, E, A, R(44) );
    P( A, B, C, D, E, R(45) );
    P( E, A, B, C, D, R(46) );
    P( D, E, A, B, C, R(47) );
    P( C, D, E, A, B, R(48) );
    P( B, C, D, E, A, R(49) );
    P( A, B, C, D, E, R(50) );
    P( E, A, B, C, D, R(51) );
    P( D, E, A, B, C, R(52) );
    P( C, D, E, A, B, R(53) );
    P( B, C, D, E, A, R(54) );
    P( A, B, C, D, E, R(55) );
    P( E, A, B, C, D, R(56) );
    P( D, E, A, B, C, R(57) );
    P( C, D, E, A, B, R(58) );
    P( B, C, D, E, A, R(59) );

#undef K
#undef F

#define F(x,y,z) (x ^ y ^ z)
#define K 0xCA62C1D6

    P( A, B, C, D, E, R(60) );
    P( E, A, B, C, D, R(61) );
    P( D, E, A, B, C, R(62) );
    P( C, D, E, A, B, R(63) );
    P( B, C, D, E, A, R(64) );
    P( A, B, C, D, E, R(65) );
    P( E, A, B, C, D, R(66) );
    P( D, E, A, B, C, R(67) );
    P( C, D, E, A, B, R(68) );
    P( B, C, D, E, A, R(69) );
    P( A, B, C, D, E, R(70) );
    P( E, A, B, C, D, R(71) );
    P( D, E, A, B, C, R(72) );
    P( C, D, E, A, B, R(73) );
    P( B, C, D, E, A, R(74) );
    P( A, B, C, D, E, R(75) );
    P( E, A, B, C, D, R(76) );
    P( D, E, A, B, C, R(77) );
    P( C, D, E, A, B, R(78) );
    P( B, C, D, E, A, R(79) );

#undef K
#undef F

    ctx->state[0] += A;
    ctx->state[1] += B;
    ctx->state[2] += C;
    ctx->state[3] += D;
    ctx->state[4] += E;
}

/*
 * SHA-1 process buffer
 */
void sha1_update( sha1_context *ctx, const uint8_t *input, size_t ilen )
{
    size_t fill;
    uint32_t left;

    if( ilen <= 0 )
        return;

    left = ctx->total[0] & 0x3F;
    fill = 64 - left;

    ctx->total[0] += (uint32_t) ilen;
    ctx->total[0] &= 0xFFFFFFFF;

    if( ctx->total[0] < (uint32_t) ilen )
        ctx->total[1]++;

    if( left && ilen >= fill )
    {
        memcpy( (void *) (ctx->buffer + left),
                (void *) input, fill );
        sha1_process( ctx, ctx->buffer );
        input += fill;
        ilen  -= fill;
        left = 0;
    }

    while( ilen >= 64 )
    {
        sha1_process( ctx, input );
        input += 64;
        ilen  -= 64;
    }

    if( ilen > 0 )
    {
        memcpy( (void *) (ctx->buffer + left),
                (void *) input, ilen );
    }
}

static const uint8_t sha1_padding[64] =
{
 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/*
 * SHA-1 final digest
 */
void sha1_finish( sha1_context *ctx, uint8_t output[20] )
{
    uint32_t last, padn;
    uint32_t high, low;
    uint8_t  msglen[8];

    high = ( ctx->total[0] >> 29 )
         | ( ctx->total[1] <<  3 );
    low  = ( ctx->total[0] <<  3 );

    PUT_ULONG_BE( high, msglen, 0 );
    PUT_ULONG_BE( low,  msglen, 4 );

    last = ctx->total[0] & 0x3F;
    padn = ( last < 56 ) ? ( 56 - last ) : ( 120 - last );

    sha1_update( ctx, (uint8_t *) sha1_padding, padn );
    sha1_update( ctx, msglen, 8 );

    PUT_ULONG_BE( ctx->state[0], output,  0 );
    PUT_ULONG_BE( ctx->state[1], output,  4 );
    PUT_ULONG_BE( ctx->state[2], output,  8 );
    PUT_ULONG_BE( ctx->state[3], output, 12 );
    PUT_ULONG_BE( ctx->state[4], output, 16 );
}

/*
 * output = SHA-1( input buffer )
 */
void sha1( const uint8_t *input, size_t ilen, uint8_t output[20] )
{
    sha1_context ctx;

    sha1_starts( &ctx );
    sha1_update( &ctx, input, ilen );
    sha1_finish( &ctx, output );

    memset( &ctx, 0, sizeof( sha1_context ) );
}

typedef struct {
    size_t offset;
    size_t size;
} range_t;

static void __inline copy_range( range_t* dst, range_t* src )
{
    dst->offset = src->offset;
    dst->size = src->size;
}

#define SORT_ITEM        range_t
#define SORT_COPY        copy_range
#define SORT_CMP( a, b ) ( a.offset < b.offset )
static void sort(SORT_ITEM a[], size_t n)
{
    size_t m, i, j, k;
    static SORT_ITEM* b = NULL;
    int need_free = 0;

    if( n < 2 )
        return;

    if ( b == NULL)
    {
        b = (SORT_ITEM*) malloc( sizeof( SORT_ITEM ) * ( n/2 + 1 ) );
        need_free = 1;
    }

    m = n >> 1;

    sort( a, m );
    sort( &a[m], n - m ); 

    for( i=0; i < m; i++ )
        SORT_COPY( &b[i], &a[i] );

    i = 0; j = m; k = 0;
    while( ( i < m ) && ( j < n ) )
        SORT_COPY( &a[k++], ( SORT_CMP( a[j], b[i] ) ) ? &a[j++] : &b[i++] );
    while( i < m )
        SORT_COPY( &a[k++], &b[i++] );

    if( need_free )
        free( b );
}

/* requires the range table to be sorted by offset with no overlapping or contiguous ranges */
static size_t skipranges( FILE* f, uint8_t buf[], size_t n, range_t range[], size_t nranges )
{
    size_t i, p, wtop, wpos, toread;
    size_t r = n;

    for( i = 0; i < nranges; i++ )
    {
        wtop = ftell(f);
        wpos = wtop - r;
        if( ( range[i].offset < wpos ) || ( range[i].offset > wtop ) )
            continue;

        p = range[i].offset - wpos;
        if( p + range[i].size > r )
        {
            fseek( f, range[i].offset + range[i].size, SEEK_SET );
            toread = r - p;
        }
        else
        {
            toread = range[i].size;
            memmove( &buf[p], &buf[p + toread], r - p - toread );
        }
        r += fread( &buf[r - toread], 1, toread, f ) - toread;
    }

    return r;
}

/* If running on Windows, we can validate our computation against the native one */
#define VALIDATE_HASH
#if defined( VALIDATE_HASH ) && defined( _WIN32 )
#include <windows.h>

static __inline LPWSTR UTF8toWCHAR( LPCSTR szStr )
{
    int size = 0;
    LPWSTR wszStr = NULL;

    size = MultiByteToWideChar( CP_UTF8, 0, szStr, -1, NULL, 0 );
    if ( size <= 1 )
        return NULL;

    if ( ( wszStr = (wchar_t*)calloc( size, sizeof( wchar_t ) ) ) == NULL )
        return NULL;
    if ( MultiByteToWideChar( CP_UTF8, 0, szStr, -1, wszStr, size ) != size ) {
        free( wszStr );
        return NULL;
    }
    return wszStr;
}

typedef BOOL ( WINAPI *CryptCATAdminCalcHashFromFileHandle_t )(
    HANDLE hFile,
    DWORD *pcbHash,
    BYTE *pbHash,
    DWORD dwFlags
);

static BOOL ValidateHash( BYTE* myHash, LPCSTR szfilePath )
{
    CryptCATAdminCalcHashFromFileHandle_t CryptCATAdminCalcHashFromFileHandle = NULL;
    int i;
    BOOL r = FALSE;
    HANDLE hFile = NULL;
    HMODULE h = NULL;
    BYTE Hash[20];
    DWORD cbHash = 20;
    LPWSTR wszFilePath = NULL;

    if ( ( h = GetModuleHandleA( "wintrust" ) ) == NULL )
        h = LoadLibraryA( "wintrust" );
    CryptCATAdminCalcHashFromFileHandle =
        (CryptCATAdminCalcHashFromFileHandle_t) GetProcAddress( h, "CryptCATAdminCalcHashFromFileHandle" );
    if (CryptCATAdminCalcHashFromFileHandle == NULL)
       goto out;

    wszFilePath = UTF8toWCHAR( szfilePath );
    hFile = CreateFileW( wszFilePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
    if ( hFile == INVALID_HANDLE_VALUE )
        goto out;
    if ( ( !CryptCATAdminCalcHashFromFileHandle( hFile, &cbHash, Hash, 0 ) ) )
        goto out;
    for ( i=0; i < 20; i++ )
    if( memcmp(Hash, myHash, 20 ) == 0 )
        r = TRUE;
    else
    {
        printf( "\nThe CAT hash computed by this program differs from the Microsoft one!\n" );
        printf( "expected: ");
        for ( i=0; i < 20; i++ )
            printf( "%02x", Hash[i] );
        printf( "\n" );
        printf( "computed: ");
        for ( i=0; i < 20; i++ )
            printf( "%02x", myHash[i] );
        printf( "\n\n" );
        printf( "please e-mail pete@akeo.ie with your data, so that we address the issue.\n" );
    }

out:
    if  (wszFilePath != NULL )
        free( wszFilePath );
    if ( hFile )
        CloseHandle( hFile );
    return r;
}
#endif

/*
 * output = CAT SHA-1( file contents )
 */
int
#ifdef DDKBUILD
__cdecl
#endif
main ( int argc, char** argv )
{
    FILE *f;
    size_t i, n, nb_ranges = 0;
    sha1_context ctx;
    uint8_t buf[0x400];
    uint8_t output[20];
    uint16_t optional_header_magic;
    uint32_t coff_pos, optional_header_pos, checksum_pos;
    uint32_t cert_table_directory, cert_table_pos, cert_table_size;
    range_t range[3];

    if( argc < 2 )
    {
        fprintf( stderr, "\ncathash v1.0 - MS compatible SHA-1 for PE and data\n");
        fprintf( stderr, "Copyright (c) 2011 Pete Batard <pete@akeo.ie>\n\n");
        fprintf( stderr, "This program is free software: you can redistribute it and/or modify\n");
        fprintf( stderr, "it under the terms of the GNU General Public License as published by\n");
        fprintf( stderr, "the Free Software Foundation, either version 3 of the License, or\n");
        fprintf( stderr, "(at your option) any later version.\n\n");
        fprintf( stderr, "Usage: cathash filename\n");
        return( 1 );
    }

    if( ( f = fopen( argv[1], "rb" ) ) == NULL )
    {
        fprintf (stderr, "can't open '%s': %s\n", argv[1], strerror( errno ) );
        return( 1 );
    }

    /* PE images have some data sections skipped from hash generation */
    if( fread( buf, 1, 0x40, f ) == 0x40 )
    {
        if( ( buf[0] != 'M' ) || ( buf[1] != 'Z' ) )
            goto sha1;

        GET_USHORT_LE( coff_pos, buf, 0x3c );
        optional_header_pos = coff_pos + 0x18;
        fseek( f, optional_header_pos, SEEK_SET );

        if( fread( buf, 1, 0xa0, f ) != 0xa0 )
            goto sha1;

        GET_USHORT_LE( optional_header_magic, buf, 0 );
        if( ( optional_header_magic != 0x10b ) && ( optional_header_magic != 0x20b ) )
            goto sha1;

        checksum_pos = optional_header_pos + 0x40;

        cert_table_directory = optional_header_magic == 0x10b ? 0x80 : 0x90;
        GET_ULONG_LE( cert_table_pos, buf, cert_table_directory );
        GET_ULONG_LE( cert_table_size, buf, cert_table_directory + 4 );

        range[nb_ranges].offset = optional_header_pos + cert_table_directory;
        range[nb_ranges++].size = 8;
        range[nb_ranges].offset = checksum_pos;
        range[nb_ranges++].size = 4;
        range[nb_ranges].offset = cert_table_pos;
        range[nb_ranges++].size = cert_table_size;

        sort(range, nb_ranges);
    }

sha1:
    fseek(f, 0, SEEK_SET);
    sha1_starts( &ctx );

    while( ( n = fread( buf, 1, sizeof( buf ), f ) ) > 0 )
    {
        n = skipranges( f, buf, n, range, nb_ranges);
        if ( n > 0 )
            sha1_update( &ctx, buf, n );
    }

    if( ferror( f ) != 0 )
    {
        fclose( f );
        fprintf( stderr, "error reading '%s': %s\n", argv[1], strerror( errno ) );
        return( 1 );
    }

    fclose( f );
    sha1_finish( &ctx, output );

#if defined( VALIDATE_HASH ) && defined( _WIN32 )
    if( !ValidateHash( output, argv[1] ) )
        return( 1 );
#endif

    for ( i=0; i < 20; i++ )
        printf( "%02x", output[i] );
    printf( "\n" );

    return( 0 );
}
