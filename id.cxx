#define UNICODE

#include <windows.h>
#include <shlwapi.h>
#include <dshow.h>
#include <dmo.h>

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <io.h>
#include <limits.h>
#include <locale.h>
#include <float.h>
#include <eh.h>
#include <math.h>

#include <string>
#include <memory>
#include <vector>

#include "djl_strm.hxx"

using namespace std;

/*
   1 = BYTE An 8-bit unsigned integer
   2 = ASCII An 8-bit byte containing one 7-bit ASCII code. The final byte is terminated with NULL
   3 = SHORT A 16-bit (2-byte) unsigned integer
   4 = LONG A 32-bit (4-byte) unsigned integer
   5 = RATIONAL Two LONGs. The first LONG is the numerator and the second LONG the denominator
   6 = no official definition, but used as an unsigned BYTE
   7 = UNDEFINED An 8-bit byte that can take any value depending on the field definition,
   8 = no official definition. Nokia uses it as an integer to represent ISO. Use as type 4.
   9 = SLONG A 32-bit (4-byte) signed integer (2's complement notation),
  10 = SRATIONAL Two SLONGs. The first SLONG is the numerator and the second SLONG is the denominator.
  11 = No official definition. Sigma uses it for FLOATs (4 bytes)
  12 = No official definition. Apple uses it for double floats (8 bytes)
  13 = IFD pointer (Olympus ORF uses this)
*/

#pragma comment( lib, "ole32.lib" )
#pragma comment( lib, "oleaut32.lib" )
#pragma comment( lib, "shell32.lib" )
#pragma comment( lib, "shlwapi.lib" )
#pragma comment( lib, "ntdll.lib" )
#pragma comment( lib, "strmiids.lib" )

typedef unsigned __int64 QWORD;

CStream * g_pStream = NULL;
static const int MaxIFDHeaders = 200; // assume anything more than this is a corrupt or badly parsed file
bool g_FullInformation = false;
DWORD g_Heif_Exif_ItemID                = 0xffffffff;
__int64 g_Heif_Exif_Offset              = 0;
__int64 g_Heif_Exif_Length              = 0;
__int64 g_Canon_CR3_Exif_IFD0           = 0;
__int64 g_Canon_CR3_Exif_Exif_IFD       = 0;
__int64 g_Canon_CR3_Exif_Makernotes_IFD = 0;
__int64 g_Canon_CR3_Exif_GPS_IFD        = 0;
__int64 g_Canon_CR3_Embedded_JPG_Length = 0;

__int64 g_Embedded_Image_Offset = 0;
__int64 g_Embedded_Image_Length = 0;
WCHAR * g_Embedded_File_Extension = L".JPG";

char g_acMake[ 100 ] = { 0, 0, 0 };
char g_acModel[ 100 ] = { 0, 0, 0 };

class CFile
{
    public:
        FILE * fp;

        CFile( FILE * file )
        {
            fp = file;
        }

        ~CFile()
        {
            Close();
        }

        void Close()
        {
            if ( NULL != fp )
            {
                fclose( fp );
                fp = NULL;
            }
        }
};

struct IFDHeader
{
    WORD id;
    WORD type;
    DWORD count;
    DWORD offset;

    void Endian( bool littleEndian )
    {
        if ( !littleEndian )
        {
            id = _byteswap_ushort( id );
            type = _byteswap_ushort( type );
            count = _byteswap_ulong( count );
            offset = _byteswap_ulong( offset );
        }

        offset = AdjustOffset( littleEndian );
    }

    private:

        DWORD AdjustOffset( bool littleEndian )
        {
            if ( 1 != count )
                return offset;
        
            if ( littleEndian )
            {
                // Mask off the bits that should be 0
    
                if ( 1 == type || 6 == type )
                    return offset & 0xff;
        
                if ( 3 == type || 8 == type )
                    return offset & 0xffff;
            }
            else
            {
                // The DWORD has already been swapped, but to interpret it as a 1 or 2 byte quantity,
                // that must be shifted as well.
    
                if ( 1 == type || 6 == type )
                    return ( offset >> 24 );
        
                if ( 3 == type || 8 == type )
                    return ( offset >> 16 );
            }
        
            return offset;
        } //AdjustOffset
};
    
void printWide( WCHAR const * pwc )
{
    fflush( 0 ); // make sure the C runtime is caught up
    HANDLE h = GetStdHandle( STD_OUTPUT_HANDLE );
    DWORD mode;

    if ( GetConsoleMode( h, &mode ) )
    {
        // This is the only way I can get Korean characters to show up in the console window.
        // No form of printf with various locale/codepage settings seem to work.

        WriteConsole( GetStdHandle( STD_OUTPUT_HANDLE ), pwc, wcslen( pwc ), 0, 0 );
    }
    else
    {
        int c = WideCharToMultiByte( CP_UTF8, 0, pwc, -1, 0, 0, 0, 0 );
        if ( c > 1 )
        {
            vector<char> u8( c );
            WideCharToMultiByte( CP_UTF8, 0, pwc, -1, u8.data(), c, 0, 0 );
            char * p = u8.data();

            while ( 0 != *p )
            {
                char * plf = strchr( p, '\n' );
                if ( 0 != plf )
                {
                    size_t l = plf - p;
                    WriteFile( h, p, l, 0, 0 );
                    char * pcrlf = "\r\n";
                    WriteFile( h, pcrlf, 2, 0, 0 );
                    p = plf + 1;
                }
                else
                {
                    WriteFile( h, p, strlen( p ), 0, 0 );
                    break;
                }
            }
        }
    }
} //printWide

void printNarrow( char const * pc )
{
    HANDLE h = GetStdHandle( STD_OUTPUT_HANDLE );
    DWORD mode;
    int len = strlen( pc );

    if ( 0 != len )
    {
        if ( GetConsoleMode( h, &mode ) )
        {
            fflush( 0 ); // make sure the C runtime is caught up
            WriteConsoleA( GetStdHandle( STD_OUTPUT_HANDLE ), pc, len, 0, 0 );
        }
        else
            printf( "%s", pc ); // need printf to do /n to /r/n processing
    }
} //printNarrow

void prf( const char * format, ... )
{
    static char buf[ 65536 ];
    va_list args;
    va_start( args, format );
    _vsnprintf_s( buf, _countof( buf ), _countof( buf ) - 1, format, args );
    va_end( args );

    printNarrow( buf );
} //prf

void wprf( const WCHAR * format, ... )
{
    static WCHAR buf[ 65536 ];
    va_list args;
    va_start( args, format );
    _vsnwprintf_s( buf, _countof( buf ), _countof( buf ) - 1, format, args );
    va_end( args );

    printWide( buf );
} //wprf

void Usage()
{
    prf( "usage: id [-e:name.jpg] [-f] [filename]\n" );
    prf( "  image data enumeration\n" );
    prf( "       filename       filename of the image to check\n" );
    prf( "       -f             Full information is displayed (all binary data, all field values, etc.)\n" );
    prf( "       -e:<output>    Extract highest-resolution embedded image (if one exists) to the file specified.\n" );
    prf( "\n" );
    prf( "  examples:\n" );
    prf( "      id img_0178.cr2\n" ); 
    prf( "      id -f img_0178.cr2\n" ); 
    prf( "      id img_0178.cr2 /e:178.jpg\n" ); 
    prf( "      id track.flac /e:track.png\n" ); 
    prf( "\n" );
    prf( "  notes:\n" );
    prf( "      Most image formats are supported: CR2, NEF, RW2, DNG, PNG, TIFF, JPG, ARW, HEIC, HIF, CR3, BMP, ORF\n" );
    prf( "      Some non-image formats are supported: FLAC, WAV, MP3, WMA, WMV\n" );
    prf( "      Some image formats aren't supported yet: presumably many others\n" );
    prf( "      By default, just the first 256 bytes of binary data is displayed. Use -f for all data\n" );
    prf( "      Embedded images may be JPG, PNG, HIF, or some other format\n" );
    prf( "      Fujifilm RAF files are only supported in that the embedded JPG is parsed\n" );
    exit( 1 );
} //Usage

__int64 GetStreamLength() { return g_pStream->Length(); }

unsigned long long GetULONGLONG( __int64 offset, bool littleEndian )
{
    unsigned long long ull = 0;

    if ( g_pStream->Seek( offset ) )
    {
        g_pStream->Read( &ull, sizeof ull );

        if ( !littleEndian )
            ull = _byteswap_uint64( ull );
    }

    return ull;
} //GetULONGULONG

DWORD GetDWORD( __int64 offset, bool littleEndian )
{
    DWORD dw = 0;     // Note: some files are malformed and point to reads beyond the EOF. Return 0 in these cases

    if ( g_pStream->Seek( offset ) )
    {
        g_pStream->Read( &dw, sizeof dw );

        if ( !littleEndian )
            dw = _byteswap_ulong( dw );
    }

    return dw;
} //GetDWORD

WORD GetWORD( __int64 offset, bool littleEndian )
{
    WORD w = 0;

    if ( g_pStream->Seek( offset ) )
    {
        g_pStream->Read( &w, sizeof w );

        if ( !littleEndian )
            w = _byteswap_ushort( w );
    }

    return w;
} //GetWORD

byte GetBYTE( __int64 offset )
{
    byte b = 0;

    if ( g_pStream->Seek( offset ) )
        g_pStream->Read( &b, sizeof b );

    return b;
} //GetBYTE

void GetBytes( __int64 offset, void * pData, int byteCount )
{
    memset( pData, 0, byteCount );

    if ( g_pStream->Seek( offset ) )
        g_pStream->Read( pData, byteCount );
} //GetBytes

bool GetIFDHeaders( __int64 offset, IFDHeader * pHeader, WORD numHeaders, bool littleEndian )
{
    bool ok = true;
    int cb = sizeof IFDHeader * numHeaders;
    GetBytes( offset, pHeader, cb );
    for ( WORD i = 0; i < numHeaders; i++ )
        pHeader[i].Endian( littleEndian );

    for ( WORD i = 0; i < numHeaders; i++ )
    {
        // validate type info, because if it's wrong we're likely parsing the file incorrectly.
        // Note the Panasonic LX100, S1R, zs100, & zs200 write 0x100 to the type's second byte, so mask it off.
        // Not all Panasonic RAW files do this -- GF1 for example.

        if ( !strcmp( g_acMake, "Panasonic" ) && ( 0x100 == ( 0xff00 & pHeader[i].type ) ) )
            pHeader[i].type &= 0xff;

        if ( pHeader[i].type > 13 )
        {
            prf( "(warning) record %d has invalid type %#x\n", i, pHeader[i].type );
            ok = false;
            break;
        }
    }

    return ok;
} //GetIFDHeaders

void GetString( __int64 offset, char * pcOutput, int outputSize, int maxBytes )
{
    if ( outputSize <= 0 )
        return;

    *pcOutput = 0;

    if ( maxBytes < 0 )
        maxBytes = 0;

    if ( g_pStream->Seek( offset ) )
    {
        int maxlen = __min( outputSize - 1, maxBytes );

        g_pStream->Read( pcOutput, maxlen );

        // These strings sometimes have trailing spaces. Remove them.

        pcOutput[ maxlen ] = 0;
        int len = strlen( pcOutput );
        len--;

        while ( len >= 0 )
        {
            if ( ' ' == pcOutput[ len ] )
                pcOutput[ len ] = 0;
            else
                break;

            len--;
        }
    }
} //GetString

WCHAR * GetUTF8( __int64 offset, DWORD length )
{
    if ( !g_pStream->Seek( offset ) )
        return NULL;

    unique_ptr<char> bytes( new char[ length + 1 ] );
    g_pStream->Read( bytes.get(), length );
    bytes.get()[ length ] = 0;

    #if false
        prf( "field %d bytes read: %s\n", length, bytes.get() );
        
        for ( int i = 0; i < length; i++ )
        {
            if ( 0 == ( i % 16 ) )
            {
                if ( 0 != i )
                    prf( "\n" );
        
                prf( "offset %#x", i );
            }
        
            prf( " %#x", (DWORD) (BYTE) bytes.get()[i] );
        }
        prf( "\n" );
    #endif

    int cwc = MultiByteToWideChar( CP_UTF8, 0, bytes.get(), length + 1, NULL, 0 );
    WCHAR * pwc = new WCHAR[ cwc ];
    MultiByteToWideChar( CP_UTF8, 0, bytes.get(), length + 1, pwc, cwc );

    // Convert some well-known Unicode chars to ascii equivalents so they
    // appear correctly in a CMD window

    for ( int i = 0; i < cwc; i++ )
    {
        // Right and Left Single Quotation Mark

        if ( 0x2019 == pwc[ i ] || 0x2018 == pwc[ i ] )
            pwc[i] = '\'';
    }

    return pwc;
} //GetUTF8

bool IsIntType( WORD tagType )
{
    return ( 1 == tagType || 3 == tagType || 4 == tagType || 6 == tagType || 8 == tagType || 9 == tagType );
} //IsIntType

void Space( int depth )
{
    int x = 2 * depth;

    for ( int i = 0; i < x; i++ )
        prf( " " );
} //Space

void DumpBinaryData( __int64 initialOffset, __int64 headerBase, DWORD length, DWORD indent, DWORD offsetOfSmallValue )
{
    __int64 offset = initialOffset + headerBase;

    // In case the data is inline in the record

    if ( length <= 4 )
        offset = offsetOfSmallValue;

    if ( length > 0x100 && ! g_FullInformation)
        length = 0x100;

    __int64 beyond = offset + length;
    const __int64 bytesPerRow = 32;
    byte buf[ bytesPerRow ];

    while ( offset < beyond )
    {
        for ( int i = 0; i < indent; i++ )
            prf( " " );

        prf( "%#10llx  ", offset );

        __int64 cap = __min( offset + bytesPerRow, beyond );
        __int64 toread = ( ( offset + bytesPerRow ) > beyond ) ? ( length % bytesPerRow ) : bytesPerRow;

        GetBytes( offset, buf, toread );

        for ( __int64 o = offset; o < cap; o++ )
            prf( "%02x ", buf[ o - offset ] );

        DWORD spaceNeeded = ( bytesPerRow - ( cap - offset ) ) * 3;

        for ( ULONG sp = 0; sp < ( 1 + spaceNeeded ); sp++ )
            prf( " " );

        for ( __int64 o = offset; o < cap; o++ )
        {
            char ch = buf[ o - offset ];

            if ( ch < ' ' || 127 == ch )
                ch = '.';
            prf( "%c", ch );
        }

        offset += bytesPerRow;

        prf( "\n" );
    }
} //DumpBinaryData

void DumpBinaryData( byte * pData, DWORD length, DWORD indent )
{
    __int64 offset = 0;
    __int64 beyond = length;
    const __int64 bytesPerRow = 32;
    byte buf[ bytesPerRow ];

    while ( offset < beyond )
    {
        for ( int i = 0; i < indent; i++ )
            prf( " " );

        prf( "%#10llx  ", offset );

        __int64 cap = __min( offset + bytesPerRow, beyond );
        __int64 toread = ( ( offset + bytesPerRow ) > beyond ) ? ( length % bytesPerRow ) : bytesPerRow;

        memcpy( buf, pData + offset, toread );

        for ( __int64 o = offset; o < cap; o++ )
            prf( "%02x ", buf[ o - offset ] );

        DWORD spaceNeeded = ( bytesPerRow - ( cap - offset ) ) * 3;

        for ( ULONG sp = 0; sp < ( 1 + spaceNeeded ); sp++ )
            prf( " " );

        for ( __int64 o = offset; o < cap; o++ )
        {
            char ch = buf[ o - offset ];

            if ( ch < ' ' || 127 == ch )
                ch = '.';
            prf( "%c", ch );
        }

        offset += bytesPerRow;

        prf( "\n" );
    }
} //DumpBinaryData

void EnumerateFujifilmMakernotes( int depth, __int64 IFDOffset, __int64 headerBase, bool littleEndian )
{
    // https://www.exiv2.org/tags-fujifilm.html

    char acBuffer[ 100 ];
    vector<IFDHeader> aHeaders( MaxIFDHeaders );

    // In Fuji files, the base is not relative to the prior base; it's relative to the IFD start.

    DWORD tagHeaderBase = IFDOffset - 12;

    while ( 0 != IFDOffset ) 
    {
        WORD NumTags = GetWORD( IFDOffset + headerBase, littleEndian );
        Space( depth );
        prf( "makernote IFDOffset %lld, NumTags %d, headerBase %lld, tagHeaderBase %d\n", IFDOffset, NumTags, headerBase, tagHeaderBase );
        IFDOffset += 2;

        if ( NumTags > MaxIFDHeaders )
        {
            Space( depth );
            prf( "numtags is > 200; it's likely not in EXIF format, so the data may be noise. skipping (%d)\n", NumTags );
            break;
        }
    
        if ( !GetIFDHeaders( IFDOffset + headerBase, aHeaders.data(), NumTags, littleEndian ) )
        {
            Space( depth );
            prf( "fujifilm makernotes has a tag that's invalid. skipping\n" );
            break;
        }

        for ( int i = 0; i < NumTags; i++ )
        {
            IFDHeader & head = aHeaders[ i ];
            IFDOffset += sizeof IFDHeader;

            if ( g_FullInformation )
            {
                Space( depth );
                prf( "makernote tag %d ID %d, type %d, count %d, offset/value %d\n", i, head.id, head.type, head.count, head.offset );
            }

            Space( depth );

            if ( 0 == head.id )
                prf( "fujifilm makernote Version           %d\n", head.offset );
            else if ( 16 == head.id )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + tagHeaderBase + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "fujifilm makernote Serial #:         %s\n", acBuffer );
            }
            else if ( 4096 == head.id )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + tagHeaderBase + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "fujifilm makernote Quality:          %s\n", acBuffer );
            }
            else if ( 4097 == head.id && 4 == head.type )
                prf( "Fujifilm Sharpness:                  %d\n", head.offset );
            else if ( 4098 == head.id && 4 == head.type )
                prf( "Fujifilm WhiteBalance:               %d\n", head.offset );
            else if ( 4099 == head.id && 4 == head.type )
                prf( "Fujifilm Saturation:                 %d\n", head.offset );
            else if ( 5169 == head.id && 4 == head.type )
            {
                prf( "Fujifilm Rating:                     %d\n", head.offset );
            }
            else
            {
                prf( "fujifilm makernote tag %d ID %d==%#x, type %d, count %d, offset/value %d", i, head.id, head.id, head.type, head.count, head.offset );

                if ( 2 == head.type )
                {
                    ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                    GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                    prf( " %s", acBuffer );
                }

                prf( "\n" );

                if ( 7 == head.type )
                    DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
            }
        }

        IFDOffset = GetDWORD( IFDOffset + headerBase, littleEndian );
    }
} //EnumerateFujifilmMakernotes

const char * PhotometricInterpretationString( DWORD pi )
{
    if ( 0 == pi )
        return "WhiteIsZero";
    if ( 1 == pi )
        return "BlackIsZero";
    if ( 2 == pi )
        return "RGB";
    if ( 3 == pi )
        return "Palette color";
    if ( 4 == pi )
        return "Transparency mask";
    if ( 5 == pi )
        return "CMYK";
    if ( 6 == pi )
        return "YCbCr";
    if ( 8 == pi )
        return "CIE L*a*b*";
    if ( 9 == pi )
        return "CIE L8a*b* alternate";
    if ( 10 == pi )
        return "ITU L8a*b*";
    if ( 32803 == pi )
        return "CFA (Color Filter Array)";
    if ( 34892 == pi )
        return "LinearRaw";

    return "";
} //PhotometricInterpretationString

const char * CFAColorToString( BYTE x )
{
    if ( 0 == x )
        return "red";
    if ( 1 == x )
        return "green";
    if ( 2 == x )
        return "blue";
    if ( 3 == x )
        return "cyan";
    if ( 4 == x )
        return "magenda";
    if ( 5 == x )
        return "yellow";
    if ( 6 == x )
        return "white";

    return "unknown color";
} //CFAColorToString

const char * CFALayout( DWORD x )
{
    if ( 1 == x )
        return "rectangular";
    if ( 2 == x )
        return "staggered layout a";
    if ( 3 == x )
        return "staggered layout b";
    if ( 4 == x )
        return "staggered layout c";
    if ( 5 == x )
        return "staggered layout d";

    return "unknown layout";
} //CFALayout

const char * SonyRawFileType( DWORD x )
{
    if ( 0 == x )
        return "Uncompressed 14-bit RAW";
    if ( 1 == x )
        return "Uncompressed 12-bit RAW";
    if ( 2 == x )
        return "Compressed RAW";
    if ( 3 == x )
        return "Lossless Compressed RAW";

    return "unknown";
} //SonyRawFileType

const char * ExifHCUsage( DWORD x )
{
    if ( 0 == x )
        return "CT";
    if ( 1 == x )
        return "Line Art";
    if ( 2 == x )
        return "Trap";

    return "unknown";
} //ExifHCUsage

const char * ExifSampleFormat( DWORD x )
{
    if ( 1 == x )
        return "unsigned";
    if ( 2 == x )
        return "signed";
    if ( 3 == x )
        return "float";
    if ( 4 == x )
        return "undefined";
    if ( 5 == x )
        return "complex int";
    if ( 6 == x )
        return "complex float";

    return "unknown";
} //ExifSampleFormat

const char * ExifRasterPadding( DWORD x )
{
    if ( 0 == x )
        return "byte";
    if ( 1 == x )
        return "word";
    if ( 2 == x )
        return "long word";
    if ( 9 == x )
        return "sector";
    if ( 10 == x )
        return "long sector";

    return "unknown";
} //ExifRasterPadding

const char * ExifExtraSamples( DWORD x )
{
    if ( 0 == x )
        return "unspecified";
    if ( 1 == x )
        return "associated alpha";
    if ( 2 == x )
        return "unassociated alpha";

    return "unknown";
} //ExifExtraSamples

const char * ExifSensingMethod( DWORD x )
{
    if ( 2 == x )
        return "one-chip color area";
    if ( 3 == x )
        return "two-chip color area";
    if ( 4 == x )
        return "three-chop color area";
    if ( 5 == x )
        return "color sequential area";
    if ( 7 == x )
        return "trilinear";
    if ( 8 == x )
        return "color sequential linear";

    return "unknown";
} //ExifSensingMethod

const char * TagPreviewColorSpace( DWORD x )
{
    if ( 1 == x )
        return "Gray Gamma 2.2";
    if ( 2 == x )
        return "sRGB";
    if ( 3 == x )
        return "Adobe RGB";
    if ( 4 == x )
        return "ProPhoto RGB";

    return "unknown";
} //TagPreviewColorSpace

const char * YCbCrSubSampling( WORD low, WORD high )
{
    if ( 1 == low && 1 == high )
        return "YCbCr4:4:4 (1 1)";
    if ( 1 == low && 2 == high )
        return "YCbCr4:4:0 (1 2)";
    if ( 1 == low && 4 == high )
        return "YCbCr4:4:1 (1 4)";
    if ( 2 == low && 1 == high )
        return "YCbCr4:4:2 (2 1)";
    if ( 2 == low && 2 == high )
        return "YCbCr4:2:0 (2 2)";
    if ( 2 == low && 4 == high )
        return "YCbCr4:2:1 (2 4)";
    if ( 4 == low && 1 == high )
        return "YCbCr4:1:1 (4 1)";
    if ( 4 == low && 2 == high )
        return "YCbCr4:1:0 (4 2)";

    return "unknown";
} //YCbCrSubSampling

const char * ResolutionUnit( DWORD x )
{
    if ( 1 == x )
        return "none";

    if ( 2 == x )
        return "inches";

    if ( 3 == x )
        return "cm";

    return "unknown";
} //ResolutionUnit

const char * YCbCrPositioning( DWORD x )
{
    if ( 1 == x )
        return "centered";
    if ( 2 == x )
        return "co-sited";

    return "unknown";
} //YCbCrPositioning

const char * LikelyImageHeader( unsigned long long x )
{
    if ( 0xd8ff == ( x & 0xffff ) )
        return "jpg";

    if ( 0x5089 == ( x & 0xffff ) )
        return "png";

    if ( 0x4d42 == ( x & 0xffff ) )
        return "bmp";

    if ( 7079746620000000 == x )
        return "heif container (hif or heic)";

    return "unknown";
} //LikelyImageHeader

bool IsPerhapsAnImageHeader( unsigned long long x )
{
    return ( ( 0xd8ff == ( x & 0xffff ) ) ||    // jpg
             ( 0x5089 == ( x & 0xffff ) ) ||    // png
             ( 0x4d42 == ( x & 0xffff ) ) ||    // bmp
             ( 7079746620000000 == x ) );       // hif, heic, etc.
} //IsPerhapsAnImageHeader

bool IsPerhapsAnImage( __int64 offset, __int64 headerBase )
{
    unsigned long long x = GetULONGLONG( offset + headerBase, true );

    bool isimage = IsPerhapsAnImageHeader( x );

    //printf( "checking if %#llx is an image header at effective offset %#llx: %d\n", x, offset + headerBase, isimage );

    return isimage;
} //IsPerhapsAnImage

const char * CompressionType( DWORD x )
{
    if ( 1 == x )
        return "uncompressed";
    if ( 6 == x )
        return "JPEG (old-style)";
    if ( 7 == x )
        return "JPEG";
    if ( 8 == x )
        return "deflate (zip)";
    if ( 5 == x )
        return "LZW";
    if ( 99 == x )
        return "JPEG";
    if ( 32773 == x )
        return "PackBits";

    return "unknown";
} //CompressionType

void EnumerateGenericIFD( int depth, __int64 IFDOffset, __int64 headerBase, bool littleEndian )
{
    char acBuffer[ 100 ];
    __int64 provisionalJPGOffset = 0;
    __int64 provisionalJPGFromRAWOffset = 0;
    int currentIFD = 0;
    bool likelyRAW = false;
    vector<IFDHeader> aHeaders( MaxIFDHeaders );

    while ( 0 != IFDOffset ) 
    {
        provisionalJPGOffset = 0;
        provisionalJPGFromRAWOffset = 0;

        WORD NumTags = GetWORD( IFDOffset + headerBase, littleEndian );
        prf( "  GenericIFD (%d) IFDOffset %lld, NumTags %d\n", currentIFD, IFDOffset, NumTags );
        IFDOffset += 2;

        if ( NumTags > MaxIFDHeaders )
        {
            Space( depth );
            prf( "numtags is > 200; it's likely not in EXIF format, so the data may be noise. skipping\n" );
            break;
        }
    
        if ( !GetIFDHeaders( IFDOffset + headerBase, aHeaders.data(), NumTags, littleEndian ) )
        {
            Space( depth );
            prf( "generic IFD has a tag that's invalid. skipping\n" );
            break;
        }

        for ( int i = 0; i < NumTags; i++ )
        {
            IFDHeader & head = aHeaders[ i ];
            IFDOffset += sizeof IFDHeader;

            if ( g_FullInformation )
            {
                Space( depth );
                prf( "GenericIFD tag %d ID %d, type %d, count %d, offset/value %d\n", i, head.id, head.type, head.count, head.offset );
            }

            Space( depth );

            if ( 254 == head.id && 4 == head.type )
            {
                likelyRAW = ( 0 == ( 1 & head.offset ) );
                prf( "NewSubfileType:                     %#x (%s)\n", head.offset, likelyRAW ? "main RAW image" : "reduced resolution copy" );
            }
            else if ( 256 == head.id && IsIntType( head.type ) )
                prf( "ImageWidth:                         %d\n", head.offset );
            else if ( 257 == head.id && IsIntType( head.type ) )   
                prf( "ImageHeight:                        %d\n", head.offset );
            else if ( 258 == head.id && 3 == head.type && 3 == head.count )
                prf( "BitsPerSample:                      %d, %d, %d\n", GetWORD( head.offset + headerBase, littleEndian ),
                        GetWORD( head.offset + headerBase + 2, littleEndian ), GetWORD( head.offset + headerBase + 4, littleEndian ) );
            else if ( 258 == head.id && 3 == head.type && 1 == head.count )
                prf( "BitsPerSample:                      %d\n", head.offset );
            else if ( 259 == head.id && IsIntType( head.type ) )
                prf( "Compression:                        %d (%s)\n", head.offset, CompressionType( head.offset ) );
            else if ( 262 == head.id && IsIntType( head.type ) )
                prf( "PhotometricIntperpretation:         %d (%s)\n", head.offset, PhotometricInterpretationString( head.offset ) );
            else if ( 273 == head.id && IsIntType( head.type ) )
            {
                prf( "StripOffsets:                       %d\n", head.offset );

                //printf( "embedded jpg offset perhaps %d, likelyraw %d\n", IsPerhapsAnImage( head.offset, headerBase ), likelyRAW );

                if ( 0 != head.offset && 0xffffffff != head.offset && IsPerhapsAnImage( head.offset, headerBase ) && !likelyRAW )
                    provisionalJPGOffset = head.offset + headerBase;
            }
            else if ( 274 == head.id && IsIntType( head.type ) )
                prf( "Orientation:                        %d\n", head.offset );
            else if ( 277 == head.id && IsIntType( head.type ) )
                prf( "SamplesPerPixel:                    %d\n", head.offset );
            else if ( 278 == head.id && IsIntType( head.type ) )
                prf( "RowsPerStrip:                       %d\n", head.offset );
            else if ( 279 == head.id && IsIntType( head.type ) )
            {
                prf( "StripByteCounts:                    %d\n", head.offset );

                //printf( "provisional offset %d, embedded length: %I64d\n", provisionalJPGOffset, g_Embedded_Image_Length );

                if ( 0 != provisionalJPGOffset && 0 != head.offset && 0xffffffff != head.offset && !likelyRAW && head.offset > g_Embedded_Image_Length )
                {
                    //printf( "AAA GenericIFD overwriting length %I64d with   %d\n", g_Embedded_Image_Length, head.offset );
                    g_Embedded_Image_Length = head.offset;
                    g_Embedded_Image_Offset = provisionalJPGOffset;
                }
            }
            else if ( 280 == head.id && IsIntType( head.type ) )
                prf( "MinSampleValue:                     %d\n", head.offset );
            else if ( 281 == head.id && 7 == head.type )
            {
                prf( "MaxSampleValue:                     %d bytes\n", head.count );
                DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
            }
            else if ( 282 == head.id && 3 == head.type )
                prf( "XResolution:                        %d\n", head.offset );
            else if ( 282 == head.id && 5 == head.type )
            {
                DWORD num = GetDWORD( head.offset + headerBase, littleEndian );
                DWORD den = GetDWORD( head.offset + headerBase + 4, littleEndian );
                prf( "XResolution:                        %d / %d = %lf\n", num, den, (double) num / (double) den );
            }
            else if ( 283 == head.id && 5 == head.type )
            {
                DWORD num = GetDWORD( head.offset + headerBase, littleEndian );
                DWORD den = GetDWORD( head.offset + headerBase + 4, littleEndian );
                prf( "YResolution:                        %d / %d = %lf\n", num, den, (double) num / (double) den );
            }
            else if ( 284 == head.id && IsIntType( head.type ) )
                prf( "PlanarConfiguration:                %d\n", head.offset );
            else if ( 296 == head.id && IsIntType( head.type ) )
                prf( "ResolutionUnit:                     %s (%d)\n", ResolutionUnit( head.offset ), head.offset );
            else if ( 315 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "HostComputer:                       %s\n", acBuffer );
            }
            else if ( 318 == head.id && 5 == head.type && 2 == head.count )
            {
                LONG num1 = GetDWORD( head.offset +      headerBase, littleEndian );
                LONG den1 = GetDWORD( head.offset +  4 + headerBase, littleEndian );
                double d1 = (double) num1 / (double) den1;

                LONG num2 = GetDWORD( head.offset +  8 + headerBase, littleEndian );
                LONG den2 = GetDWORD( head.offset + 12 + headerBase, littleEndian );
                double d2 = (double) num2 / (double) den2;

                prf( "WhitePoint:                         %lf, %lf\n", d1, d2 );
            }
            else if ( 319 == head.id && 5 == head.type && 6 == head.count )
            {
                LONG num1 = GetDWORD( head.offset +      headerBase, littleEndian );
                LONG den1 = GetDWORD( head.offset +  4 + headerBase, littleEndian );
                double d1 = (double) num1 / (double) den1;

                LONG num2 = GetDWORD( head.offset +  8 + headerBase, littleEndian );
                LONG den2 = GetDWORD( head.offset + 12 + headerBase, littleEndian );
                double d2 = (double) num2 / (double) den2;

                LONG num3 = GetDWORD( head.offset + 16 + headerBase, littleEndian );
                LONG den3 = GetDWORD( head.offset + 20 + headerBase, littleEndian );
                double d3 = (double) num3 / (double) den3;

                LONG num4 = GetDWORD( head.offset + 24 + headerBase, littleEndian );
                LONG den4 = GetDWORD( head.offset + 28 + headerBase, littleEndian );
                double d4 = (double) num4 / (double) den4;

                LONG num5 = GetDWORD( head.offset + 32 + headerBase, littleEndian );
                LONG den5 = GetDWORD( head.offset + 36 + headerBase, littleEndian );
                double d5 = (double) num5 / (double) den5;

                LONG num6 = GetDWORD( head.offset + 40 + headerBase, littleEndian );
                LONG den6 = GetDWORD( head.offset + 44 + headerBase, littleEndian );
                double d6 = (double) num6 / (double) den6;

                prf( "PrimaryChromaticities:              %lf, %lf, %lf, %lf, %lf, %lf\n", d1, d2, d3, d4, d5, d6 );
            }
            else if ( 322 == head.id && 4 == head.type )
                prf( "TileWidth:                          %d\n", head.offset );
            else if ( 323 == head.id && 4 == head.type )
                prf( "TileLength:                         %d\n", head.offset );
            else if ( 324 == head.id && 4 == head.type )
            {
                prf( "TileOffsets:                        type %d count %d\n", head.type, head.count );
                DumpBinaryData( head.offset, headerBase, head.count / 4, 4, IFDOffset - 4 );
            }
            else if ( 325 == head.id && 4 == head.type )
            {
                prf( "TileByteCounts:                     type %d count %d\n", head.type, head.count );
                DumpBinaryData( head.offset, headerBase, head.count / 4, 4, IFDOffset - 4 );
            }
            else if ( 513 == head.id && IsIntType( head.type ) )
            {
                prf( "JPGFromRAWStart:                    %d\n", head.offset );

                if ( 0 != head.offset && 0xffffffff != head.offset && !likelyRAW && IsPerhapsAnImage( head.offset, headerBase ) && !likelyRAW ) 
                    provisionalJPGFromRAWOffset = head.offset + headerBase;

            }
            else if ( 514 == head.id && IsIntType( head.type ) )
            {
                prf( "JPGFromRAWLength:                   %d\n", head.offset );

                //printf( "non-strip offset %d, length %d\n", provisionalEmbeddedJPGFromRAWOffset, head.offset );

                if ( 0 != head.offset && 0xffffffff != head.offset && 0 != provisionalJPGFromRAWOffset && ( head.offset > g_Embedded_Image_Length ) && !likelyRAW )
                {
                    //printf( "AAA GenericIFD 512 overwriting length %I64d with %d\n", g_Embedded_Image_Length, head.offset );
                    g_Embedded_Image_Length = head.offset;
                    g_Embedded_Image_Offset = provisionalJPGFromRAWOffset;
                }
            }
            else if ( 529 == head.id && 5 == head.type && 3 == head.count )
            {
                LONG num1 = GetDWORD( head.offset +      headerBase, littleEndian );
                LONG den1 = GetDWORD( head.offset +  4 + headerBase, littleEndian );
                double d1 = (double) num1 / (double) den1;

                LONG num2 = GetDWORD( head.offset +  8 + headerBase, littleEndian );
                LONG den2 = GetDWORD( head.offset + 12 + headerBase, littleEndian );
                double d2 = (double) num2 / (double) den2;

                LONG num3 = GetDWORD( head.offset + 16 + headerBase, littleEndian );
                LONG den3 = GetDWORD( head.offset + 20 + headerBase, littleEndian );
                double d3 = (double) num3 / (double) den3;

                prf( "YCbCrCoefficients:                  %lf, %lf, %lf\n", d1, d2, d3 );
            }
            else if ( 530 == head.id && 3 == head.type && 2 == head.count )
            {
                WORD low = head.offset & 0xffff;
                WORD high = ( head.offset >> 16 ) & 0xffff;

                prf( "YCbCrSubSampling:                   %s\n", YCbCrSubSampling( low, high ) );
            }
            else if ( 531 == head.id && 3 == head.type && 1 == head.count )
                prf( "YCbCrPositioning:                   %s\n", YCbCrPositioning( head.offset ) );
            else if ( 28672 == head.id && 3 == head.type )
                prf( "Sony Raw File Type:                 %s\n", SonyRawFileType( head.offset ) );
            else if ( 28673 == head.id && 3 == head.type )
                prf( "Sony Unknown(28673):                %d\n", head.offset );
            else if ( 28688 == head.id && 3 == head.type && 4 == head.count )
            {
                prf( "Sony Tone Curve:                    " );
                DWORD o = head.offset + headerBase;
                for ( int i = 0; i < 4; i++ )
                {
                    DWORD x = GetDWORD( o, littleEndian );
                    o += 4;
                    prf( "%d", x );
                    if ( 3 != i )
                        prf( ", " );
                }
                prf( "\n" );
            }
            else if ( 28689 == head.id && 3 == head.type && 4 == head.count )
            {
                prf( "Sony Unknown:                       " );
                DWORD o = head.offset + headerBase;
                for ( int i = 0; i < 4; i++ )
                {
                    DWORD x = GetDWORD( o, littleEndian );
                    o += 4;
                    prf( "%d", x );
                    if ( 3 != i )
                        prf( ", " );
                }
                prf( "\n" );
            }
            else if ( 28704 == head.id && 4 == head.type )
                prf( "Sony Unknown(28704):                %d\n", head.offset );
            else if ( 33421 == head.id && 3 == head.type && 2 == head.count )
            {
                short cfaMinRows = head.offset & 0xffff;
                short cfaMinCols = ( head.offset >> 16 ) & 0xffff;
                prf( "CFARepeatPatternDim rows/cols       %d / %d\n", cfaMinRows, cfaMinCols );
            }
            else if ( 33422 == head.id && 1 == head.type && 4 == head.count )
            {
                BYTE a = head.offset & 0xff;
                BYTE b = ( head.offset >> 8 ) & 0xff;
                BYTE c = ( head.offset >> 16 ) & 0xff;
                BYTE d = ( head.offset >> 24 ) & 0xff;
                prf( "CFAPattern:                         %#x, %s, %s, %s, %s\n", head.offset, CFAColorToString( a ), CFAColorToString( b ), CFAColorToString( c ), CFAColorToString( d ) );
            }
            else if ( 37399 == head.id )
                prf( "Sensing Method:                     %s\n", ExifSensingMethod( head.offset ) );
            else if ( 50711 == head.id && 3 == head.type )
            {
                prf( "CFALayout:                          %#x, %s\n", head.offset, CFALayout( head.offset ) );
            }
            else if ( 50713 == head.id && 3 == head.type && 2 == head.count )
            {
                short a = head.offset & 0xffff;
                short b = ( head.offset >> 16 ) && 0xffff;
                prf( "BlackLevelRepeatDim                 %#x / %#x\n", a, b );
            }                                               
            else if ( 50714 == head.id && 4 == head.type && 4 == head.count )
            {
                LONG num1 = GetDWORD( head.offset +      headerBase, littleEndian );
                LONG den1 = GetDWORD( head.offset +  4 + headerBase, littleEndian );
                double d1 = (double) num1 / (double) den1;

                LONG num2 = GetDWORD( head.offset +  8 + headerBase, littleEndian );
                LONG den2 = GetDWORD( head.offset + 12 + headerBase, littleEndian );
                double d2 = (double) num2 / (double) den2;

                LONG num3 = GetDWORD( head.offset + 16 + headerBase, littleEndian );
                LONG den3 = GetDWORD( head.offset + 20 + headerBase, littleEndian );
                double d3 = (double) num3 / (double) den3;

                LONG num4 = GetDWORD( head.offset + 24 + headerBase, littleEndian );
                LONG den4 = GetDWORD( head.offset + 28 + headerBase, littleEndian );
                double d4 = (double) num4 / (double) den4;

                prf( "BlackLevel:                         %lf, %lf, %lf, %lf\n", d1, d2, d3, d4 );
            }
            else if ( 50714 == head.id && 3 == head.type && 4 == head.count )
            {
                WORD num1 = GetWORD( head.offset +      headerBase, littleEndian );
                WORD den1 = GetWORD( head.offset +  2 + headerBase, littleEndian );
                double d1 = (double) num1 / (double) den1;

                WORD num2 = GetWORD( head.offset +  4 + headerBase, littleEndian );
                WORD den2 = GetWORD( head.offset +  6 + headerBase, littleEndian );
                double d2 = (double) num2 / (double) den2;

                WORD num3 = GetWORD( head.offset +  8 + headerBase, littleEndian );
                WORD den3 = GetWORD( head.offset + 10 + headerBase, littleEndian );
                double d3 = (double) num3 / (double) den3;

                WORD num4 = GetWORD( head.offset + 12 + headerBase, littleEndian );
                WORD den4 = GetWORD( head.offset + 14 + headerBase, littleEndian );
                double d4 = (double) num4 / (double) den4;

                prf( "BlackLevel:                         %lf, %lf, %lf, %lf\n", d1, d2, d3, d4 );
            }
            else if ( 50714 == head.id && 5 == head.type && 1 == head.count ) // Hasselblad uses this
            {
                LONG num = GetDWORD( head.offset +      headerBase, littleEndian );
                LONG den = GetDWORD( head.offset +  4 + headerBase, littleEndian );

                double d = (double) num / (double) den;
                prf( "BlackLevel:                         %d / %d = %lf\n", num, den, d );
            }
            else if ( 50717 == head.id && 4 == head.type && 1 == head.count )
                prf( "WhiteLevel:                         %d\n", head.offset );
            else if ( 50717 == head.id && 3 == head.type && 1 == head.count ) // Hasselblad uses this
                prf( "WhiteLevel:                         %d\n", head.offset );
            else if ( 50718 == head.id && 5 == head.type && 2 == head.count )
            {
                LONG num1 = GetDWORD( head.offset +      headerBase, littleEndian );
                LONG den1 = GetDWORD( head.offset +  4 + headerBase, littleEndian );
                double d1 = (double) num1 / (double) den1;

                LONG num2 = GetDWORD( head.offset +  8 + headerBase, littleEndian );
                LONG den2 = GetDWORD( head.offset + 12 + headerBase, littleEndian );
                double d2 = (double) num2 / (double) den2;

                prf( "DefaultScale:                       %lf, %lf\n", d1, d2 );
            }
            else if ( 50719 == head.id && 4 == head.type && 2 == head.count ) // Ricoh uses type 4
            {
                LONG x1 = GetDWORD( head.offset +      headerBase, littleEndian );
                LONG x2 = GetDWORD( head.offset +  4 + headerBase, littleEndian );

                prf( "DefaultCropOrigin:                  %d, %d\n", x1, x2 );
            }
            else if ( 50719 == head.id && 3 == head.type && 2 == head.count ) // Leica uses type 3
            {
                WORD x1 = head.offset & 0xffff;
                WORD x2 = ( ( head.offset >> 16 ) & 0xffff );

                prf( "DefaultCropOrigin:                  %d, %d\n", x1, x2 );
            }
            else if ( 50720 == head.id && 4 == head.type && 2 == head.count )
            {
                LONG x1 = GetDWORD( head.offset +     headerBase, littleEndian );
                LONG x2 = GetDWORD( head.offset + 4 + headerBase, littleEndian );

                prf( "DefaultCropSize:                    %d, %d\n", x1, x2 );
            }
            else if ( 50720 == head.id && 3 == head.type && 2 == head.count )
            {
                WORD x1 = head.offset & 0xffff;
                WORD x2 = ( ( head.offset >> 16 ) & 0xffff );

                prf( "DefaultCropSize:                    %d, %d\n", x1, x2 );
            }
            else if ( 50733 == head.id && 4 == head.type && 1 == head.count )
                prf( "BayerGreenSplit                     %d\n", head.offset );
            else if ( 50738 == head.id && 5 == head.type && 1 == head.count )
            {
                LONG num1 = GetDWORD( head.offset +     headerBase, littleEndian );
                LONG den1 = GetDWORD( head.offset + 4 + headerBase, littleEndian );
                double d1 = (double) num1 / (double) den1;

                prf( "AntiAliasStrength:                  %lf\n", d1 );
            }
            else if ( 50780 == head.id && 5 == head.type && 1 == head.count )
            {
                LONG num1 = GetDWORD( head.offset +     headerBase, littleEndian );
                LONG den1 = GetDWORD( head.offset + 4 + headerBase, littleEndian );
                double d1 = (double) num1 / (double) den1;

                prf( "BestQualityScale:                   %lf\n", d1 );
            }
            else if ( 50781 == head.id && 1 == head.type & 16 == head.count )
            {
                prf( "RawDataUniqueID: 16 bytes at offset %d\n", head.offset );
                DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );

            }
            else if ( 50829 == head.id && 4 == head.type && 4 == head.count )
            {
                prf( "ActiveArea:                         %d, %d, %d, %d\n",
                        GetDWORD( head.offset + headerBase, littleEndian ),
                        GetDWORD( 4 + head.offset + headerBase, littleEndian ),
                        GetDWORD( 8 + head.offset + headerBase, littleEndian ),
                        GetDWORD( 12 + head.offset + headerBase, littleEndian ) );
            }
            else if ( 50829 == head.id && 3 == head.type && 4 == head.count )
            {
                prf( "ActiveArea:                         %d, %d, %d, %d\n",
                        GetWORD( head.offset + headerBase, littleEndian ),
                        GetWORD( 2 + head.offset + headerBase, littleEndian ),
                        GetWORD( 4 + head.offset + headerBase, littleEndian ),
                        GetWORD( 6 + head.offset + headerBase, littleEndian ) );
            }
            else if ( 50970 == head.id )
                prf( "PreviewColorSpace:                  %s\n", TagPreviewColorSpace( head.offset ) );
            else if ( 51008 == head.id )
            {
                prf( "OpcodeList1:                        type %d, %d bytes, offset %d\n", head.type, head.count, head.offset );
                DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
            }
            else if ( 51009 == head.id )
            {
                prf( "OpcodeList2:                        type %d, %d bytes, offset %d\n", head.type, head.count, head.offset );
                DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
            }
            else if ( 51022 == head.id )
            {
                prf( "OpcodeList3:                        type %d, %d bytes, offset %d\n", head.type, head.count, head.offset );
                DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
            }
            else if ( 51125 == head.id && 5 == head.type && 4 == head.count )
            {
                LONG num1 = GetDWORD( head.offset +      headerBase, littleEndian );
                LONG den1 = GetDWORD( head.offset +  4 + headerBase, littleEndian );
                double d1 = (double) num1 / (double) den1;

                LONG num2 = GetDWORD( head.offset +  8 + headerBase, littleEndian );
                LONG den2 = GetDWORD( head.offset + 12 + headerBase, littleEndian );
                double d2 = (double) num2 / (double) den2;

                LONG num3 = GetDWORD( head.offset + 16 + headerBase, littleEndian );
                LONG den3 = GetDWORD( head.offset + 20 + headerBase, littleEndian );
                double d3 = (double) num3 / (double) den3;

                LONG num4 = GetDWORD( head.offset + 24 + headerBase, littleEndian );
                LONG den4 = GetDWORD( head.offset + 28 + headerBase, littleEndian );
                double d4 = (double) num4 / (double) den4;

                prf( "DefaultUserCrop:                    %lf, %lf, %lf, %lf\n", d1, d2, d3, d4 );
            }
            else
            {
                prf( "GenericIFD tag %d ID %d==%#x, type %d, count %d, offset/value %d\n", i, head.id, head.id, head.type, head.count, head.offset );

                if ( 7 == head.type )
                    DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
            }
        }

        IFDOffset = GetDWORD( IFDOffset + headerBase, littleEndian );
        currentIFD++;
    }
} //EnumerateGenericIFD

void EnumerateInteropIFD( int depth, __int64 IFDOffset, __int64 headerBase, bool littleEndian )
{
    char acBuffer[ 100 ];
    vector<IFDHeader> aHeaders( MaxIFDHeaders );

    while ( 0 != IFDOffset ) 
    {
        WORD NumTags = GetWORD( IFDOffset + headerBase, littleEndian );
        Space( depth );
        prf( "IFDOffset %lld, NumTags %d\n", IFDOffset, NumTags );
        IFDOffset += 2;

        if ( NumTags > MaxIFDHeaders )
        {
            Space( depth );
            prf( "numtags is > 200; it's likely not in EXIF format, so the data may be noise. skipping\n" );
            break;
        }
    
        if ( !GetIFDHeaders( IFDOffset + headerBase, aHeaders.data(), NumTags, littleEndian ) )
        {
            Space( depth );
            prf( "InteropIFD has a tag that's invalid. skipping\n" );
            break;
        }

        for ( int i = 0; i < NumTags; i++ )
        {
            IFDHeader & head = aHeaders[ i ];
            IFDOffset += sizeof IFDHeader;

            if ( g_FullInformation )
            {
                Space( depth );
                prf( "InteripIFD tag %d ID %d, type %d, count %d, offset/value %d\n", i, head.id, head.type, head.count, head.offset );
            }

            Space( depth );

            if ( 1 == head.id )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "interop index:                    %s\n", acBuffer );
            }
            else if ( 2 == head.id && 7 == head.type && 4 == head.count )
                prf( "interop version:                  %#x\n", head.offset );
            else
                prf( "tag %d ID %d==%#x, type %d, count %d, offset/value %d\n", i, head.id, head.id, head.type, head.count, head.offset );
        }

        IFDOffset = GetDWORD( IFDOffset + headerBase, littleEndian );
    }
} //EnumerateInteropIFD

void EnumerateNikonPreviewIFD( int depth, __int64 IFDOffset, __int64 headerBase, bool littleEndian )
{
    char acBuffer[ 100 ];
    vector<IFDHeader> aHeaders( MaxIFDHeaders );
    __int64 provisionalJPGOffset = 0;

    while ( 0 != IFDOffset ) 
    {
        provisionalJPGOffset = 0;

        WORD NumTags = GetWORD( IFDOffset + headerBase, littleEndian );
        Space( depth );
        prf( "nikonpreviewIFD IFDOffset %#llx %lld, NumTags %d, headerBase %#llx %lld\n", IFDOffset, IFDOffset, NumTags, headerBase, headerBase );
        IFDOffset += 2;

        if ( NumTags > MaxIFDHeaders )
        {
            Space( depth );
            prf( "numtags is > 200; it's likely not in EXIF format, so the data may be noise. skipping\n" );
            break;
        }
    
        if ( !GetIFDHeaders( IFDOffset + headerBase, aHeaders.data(), NumTags, littleEndian ) )
        {
            Space( depth );
            prf( "nikon previewifd has a tag that's invalid. skipping\n" );
            break;
        }

        for ( int i = 0; i < NumTags; i++ )
        {
            IFDHeader & head = aHeaders[ i ];
            IFDOffset += sizeof IFDHeader;

            if ( g_FullInformation )
            {
                Space( depth );
                prf( "NikonPreviewIFD tag %d ID %d, type %d, count %d, offset/value %d\n", i, head.id, head.type, head.count, head.offset );
            }

            Space( depth );

            if ( 0x103 == head.id && 3 == head.type )
                prf( "makernote NikonPreview.Compression:        %s (%d)\n", CompressionType( head.offset), head.offset );
            else if ( 0x11a == head.id && 5 == head.type )
                prf( "makernote NikonPreview.XResolution:        %d\n", head.offset );
            else if ( 0x11b == head.id && 5 == head.type )
                prf( "makernote NikonPreview.YResolution:        %d\n", head.offset );
            else if ( 0x128 == head.id && 3 == head.type )
                prf( "makernote NikonPreview.ResolutionUnit:     %s (%d)\n", ResolutionUnit( head.offset), head.offset );
            else if ( 0x201 == head.id && 4 == head.type )
            {
                provisionalJPGOffset = head.offset + headerBase;
                prf( "makernote NikonPreview.PreviewImageStart:  %d (effective is %lld)\n", head.offset, provisionalJPGOffset );
            }
            else if ( 0x202 == head.id && 4 == head.type )
            {
                //printf( "!!!!!!!!!!!!!overwriting length %I64d with %d\n", g_Embedded_Image_Length, head.offset );
                prf( "makernote NikonPreview.PreviewImageLength: %d\n", head.offset );

                if ( 0 != provisionalJPGOffset && 0 != head.offset && 0xffffffff != head.offset && head.offset > g_Embedded_Image_Length )
                {
                    g_Embedded_Image_Offset = provisionalJPGOffset;
                    g_Embedded_Image_Length = head.offset;
                }
            }
            else if ( 0x213 == head.id && 3 == head.type )
                prf( "makernote NikonPreview.YCbCrPositioning:   %s (%d)\n", YCbCrPositioning( head.offset ), head.offset );
            else
            {
                prf( "nikonPreviewIFD tag %d ID %d==%#x, type %d, count %d, offset/value %d\n", i, head.id, head.id, head.type, head.count, head.offset );

                if ( 7 == head.type )
                    DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
            }
        }

        IFDOffset = GetDWORD( IFDOffset + headerBase, littleEndian );
    }
} //EnumerateNikonPreviewIFD

void EnumerateNikonMakernotes( int depth, __int64 IFDOffset, __int64 headerBase, bool littleEndian )
{
    // https://www.exiv2.org/tags-nikon.html

    char acBuffer[ 100 ];
    DWORD originalNikonMakernotesOffset = IFDOffset - 8; // the -8 here is just from trial and error. But it works.
    vector<IFDHeader> aHeaders( MaxIFDHeaders );

    while ( 0 != IFDOffset ) 
    {
        WORD NumTags = GetWORD( IFDOffset + headerBase, littleEndian );
        Space( depth );
        prf( "nikon makernote IFDOffset %#llx %lld, NumTags %d, headerBase %#llx %lld\n", IFDOffset, IFDOffset, NumTags, headerBase, headerBase );
        IFDOffset += 2;

        if ( NumTags > MaxIFDHeaders )
        {
            Space( depth );
            prf( "numtags is > 200; it's likely not in EXIF format, so the data may be noise. skipping\n" );
            break;
        }
    
        if ( !GetIFDHeaders( IFDOffset + headerBase, aHeaders.data(), NumTags, littleEndian ) )
        {
            Space( depth );
            prf( "nikon makernotes has a tag that's invalid. skipping\n" );
            break;
        }

        for ( int i = 0; i < NumTags; i++ )
        {
            IFDHeader & head = aHeaders[ i ];
            IFDOffset += sizeof IFDHeader;

            if ( g_FullInformation )
            {
                Space( depth );
                prf( "NikonMakernote tag %d ID %d, type %d, count %d, offset/value %d\n", i, head.id, head.type, head.count, head.offset );
            }

            Space( depth );

            if ( 2 == head.id && 3 == head.type )
                prf( "makernote Nikon.ISOSpeed:               %d\n", head.offset );
            else if ( 4 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset + originalNikonMakernotesOffset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "makernote Nikon.Quality:                %s\n", acBuffer );
            }
            else if ( 5 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset + originalNikonMakernotesOffset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "makernote Nikon.WhiteBalance:           %s\n", acBuffer );
            }
            else if ( 6 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset + originalNikonMakernotesOffset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "makernote Nikon.Sharpening:             %s\n", acBuffer );
            }
            else if ( 7 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset + originalNikonMakernotesOffset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "makernote Nikon.Focus:                  %s\n", acBuffer );
            }
            else if ( 8 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset + originalNikonMakernotesOffset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "makernote Nikon.FlashSetting:           %s\n", acBuffer );
            }
            else if ( 9 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset + originalNikonMakernotesOffset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "makernote Nikon.FlashDevice:            %s\n", acBuffer );
            }
            else if ( 17 == head.id && 4 == head.type && 1 == head.count )
            {
                // Nikon Preview IFD

                prf( "makernote Nikon.PreviewIFD:             %#x\n", head.offset );

                // This "original - 8" is clearly a hack. But it works on RAW images from the D300, D70, and D100
                // Note it's needed to correctly compute the preview IFD start, the embedded JPG preview start, and strings

                EnumerateNikonPreviewIFD( depth + 1, head.offset, originalNikonMakernotesOffset + headerBase, littleEndian );
            }
            else
            {
                prf( "makernote tag %d ID %d==%#x, type %d, count %d, offset/value %d\n", i, head.id, head.id, head.type, head.count, head.offset );

                if ( 7 == head.type )
                    DumpBinaryData( head.offset + originalNikonMakernotesOffset, headerBase, head.count, 4, IFDOffset - 4 );
            }
        }

        IFDOffset = GetDWORD( IFDOffset + headerBase, littleEndian );
    }
} //EnumerateNikonMakernotes

void DetectGarbage( char * pc )
{
    char * pcIn = pc;
    while ( 0 != *pcIn )
    {
        if ( *pcIn < ' ' )
        {
            strcpy( pc, "garbage characters detected" );
            return;
        }
        pcIn++;
    }
} //DetectGarbage

void EnumeratePanasonicMakernotes( int depth, __int64 IFDOffset, __int64 headerBase, bool littleEndian )
{
    char acBuffer[ 100 ];
    vector<IFDHeader> aHeaders( MaxIFDHeaders );

    while ( 0 != IFDOffset ) 
    {
        WORD NumTags = GetWORD( IFDOffset + headerBase, littleEndian );
        Space( depth );
        prf( "Panasonic makernote IFDOffset %#llx %lld, NumTags %d, headerBase %#llx %lld\n", IFDOffset, IFDOffset, NumTags, headerBase, headerBase );
        IFDOffset += 2;

        if ( NumTags > MaxIFDHeaders )
        {
            Space( depth );
            prf( "numtags is > 200; it's likely not in EXIF format, so the data may be noise. skipping\n" );
            break;
        }
    
        if ( !GetIFDHeaders( IFDOffset + headerBase, aHeaders.data(), NumTags, littleEndian ) )
        {
            Space( depth );
            prf( "Panasonic makernotes has a tag that's invalid. skipping\n" );
            break;
        }

        for ( int i = 0; i < NumTags; i++ )
        {
            IFDHeader & head = aHeaders[ i ];
            IFDOffset += sizeof IFDHeader;

            if ( g_FullInformation )
            {
                Space( depth );
                prf( "PanasonicMakernote tag %d ID %d, type %d, count %d, offset/value %d\n", i, head.id, head.type, head.count, head.offset );
            }

            // Note: Photomatix Pro 5.0.1 (64-bit) generates .tif files where these 3 strings are garbage.
            // Detect this case and make the strings empty.

            if ( 37 == head.id && 7 == head.type && 16 == head.count )
            {
                // treat this as if it's a string even though it's a type 7 

                Space( depth );
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                DetectGarbage( acBuffer );
                prf( "panasonic serial number:          %s\n", acBuffer );
            }
            else if ( 81 == head.id && 2 == head.type )
            {
                Space( depth );
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                DetectGarbage( acBuffer );
                prf( "panasonic lens model:             %s\n", acBuffer );
            }
            else if ( 82 == head.id && 2 == head.type )
            {
                Space( depth );
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                DetectGarbage( acBuffer );
                prf( "panasonic lens serial #:          %s\n", acBuffer );
            }
            else
            {
                Space( depth );
                prf( "panasonic makernote tag %d ID %d==%#x, type %d, count %d, offset/value %d", i, head.id, head.id, head.type, head.count, head.offset );

                if ( 2 == head.type )
                {
                    ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                    GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                    prf( " %s", acBuffer );
                }

                prf( "\n" );

                if ( 7 == head.type )
                    DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
            }
        }

        IFDOffset = GetDWORD( IFDOffset + headerBase, littleEndian );
    }
} //EnumeratePanasonicMakernotes

void EnumerateOlympusCameraSettingsIFD( int depth, __int64 IFDOffset, __int64 headerBase, bool littleEndian )
{
    char acBuffer[ 100 ];
    DWORD originalIFDOffset = IFDOffset;
    bool previewIsValid = false;
    vector<IFDHeader> aHeaders( MaxIFDHeaders );

    while ( 0 != IFDOffset ) 
    {
        WORD NumTags = GetWORD( IFDOffset + headerBase, littleEndian );
        Space( depth );
        prf( "olympus camera settings IFDOffset %#llx==%lld, NumTags %d, headerBase %#llx==%lld\n", IFDOffset, IFDOffset, NumTags, headerBase, headerBase );
        IFDOffset += 2;

        if ( NumTags > MaxIFDHeaders )
        {
            Space( depth );
            prf( "numtags is > 200; it's likely not in EXIF format, so the data may be noise. skipping\n" );
            break;
        }
    
        if ( !GetIFDHeaders( IFDOffset + headerBase, aHeaders.data(), NumTags, littleEndian ) )
        {
            Space( depth );
            prf( "Olympus CameraSettingsIFD has a tag that's invalid. skipping\n" );
            break;
        }

        for ( int i = 0; i < NumTags; i++ )
        {
            IFDHeader & head = aHeaders[ i ];
            IFDOffset += sizeof IFDHeader;

            if ( g_FullInformation )
            {
                Space( depth );
                prf( "olympus camera settings ifd tag %d ID %d, type %d, count %d, offset/value %d==%#x\n", i, head.id, head.type, head.count, head.offset, head.offset );
            }

            Space( depth );

            if ( 0 == head.id && 7 == head.type && 4 == head.count )
                prf( "olympusCameraSettings.CameraSettingsVersion:    %d\n", head.offset );
            else if ( 256 == head.id && 4 == head.type )
            {
                prf( "olympusCameraSettings.PreviewImageValid:        %d\n", head.offset );

                previewIsValid = ( 0 != head.offset );
            }
            else if ( 257 == head.id && 4 == head.type )
            {
                if ( previewIsValid )
                    g_Embedded_Image_Offset = head.offset + headerBase;

                prf( "olympusCameraSettings.PreviewImageStart:        %d (effective is %lld)\n", head.offset, g_Embedded_Image_Offset );
            }
            else if ( 258 == head.id && 4 == head.type )
            {
                if ( previewIsValid )
                    g_Embedded_Image_Length = head.offset;
                prf( "olympusCameraSettings.PreviewImageLength:       %d\n", head.offset );
            }
            else
            {
                prf( "olympusCameraSettings.tag %d ID %d==%#x, type %d, count %d, offset/value %d==%#x\n", i, head.id, head.id, head.type, head.count, head.offset, head.offset );

                if ( 7 == head.type )
                    DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
            }
        }

        IFDOffset = GetDWORD( IFDOffset + headerBase, littleEndian );
    }
} //EnumerateOlympusCameraSettingsIFD

void EnumerateMakernotes( int depth, __int64 IFDOffset, __int64 headerBase, bool littleEndian )
{
    __int64 originalIFDOffset = IFDOffset;

    bool isLeica = false;
    bool isOlympus = false;
    bool isPentax = false;
    bool isNikon = false;
    bool isFujifilm = false;
    bool isRicoh = false;
    bool isRicohTheta = false;
    bool isEastmanKodak = false;
    bool isPanasonic = false;
    bool isApple = false;
    bool isSony = false;
    bool isCanon = false;
    bool isHasselblad = false;
    bool isSigma = false;
    bool isMotorola = false;
    char acBuffer[ 100 ];

    // Note: Hasselblad, Canon, and Sony cameras have no manufacturer string filler before the IFD begins.
    //       ... But Sony cellphones do have the filler

    if ( g_FullInformation )
    {
        Space( depth );
        prf( "make: %s\n", g_acMake );
        prf( "model: %s\n", g_acModel );
    }

    // sample headers:
    //   #1    036e: 41 70 70 6c 65 20 69 4f 53 00 00 01 4d 4d 00 24 [Apple iOS...MM.$]
    //         037e: 00 01 00 09 00 00 00 01 00 00 00 0c 00 02 00 07 [................]
    //
    //   #2    015a: 4e 69 6b 6f 6e 00 02 00 00 00 49 49 2a 00 08 00 [Nikon.....II*...]
    //         016a: 00 00 04 00 01 00 07 00 04 00 00 00 30 32 30 30 [............0200]


    // There is no standard for makernotes, and this dirOffset will work for some but not others

#if false

    // Temporarily fold headerBase into dirOffset

    DWORD dirOffset = IFDOffset + headerBase;

    char acDirectoryName[ 100 ];
    GetString( dirOffset, acDirectoryName, _countof( acDirectoryName ), _countof( acDirectoryName ) );
    dirOffset += ( 1 + strlen( acDirectoryName ) );

    // The word after the header is the count of dwords to subsequently skip prior to the count of entries
    // But we don't yet know its endian layout, so it may be swapped

    WORD dwordHeader = GetWORD( dirOffset, littleEndian );
    dirOffset += 2;

    BYTE b = GetBYTE( dirOffset );
    while ( 0x49 != b && 0x4d != b )
    {
        dirOffset++;
        b = GetBYTE( dirOffset );
    }

    WORD endianFlag = GetWORD( dirOffset, littleEndian );

    if ( ( endianFlag == 0x4949 ) != littleEndian )
        dwordHeader = _byteswap_ushort( dwordHeader );

    littleEndian = ( 0x4d4d != endianFlag );
    if ( littleEndian )
        dirOffset += 4;  // get past the 4-byte exif header
    else
        dirOffset += 2;  // get past the remaining 2-bytes of the header

    // get past the specified # of dwords prior to the count

    dirOffset += ( 4 * ( dwordHeader - 1 ) );

    dirOffset -= headerBase;

#endif

    if ( !strcmp( g_acMake, "NIKON CORPORATION" ) )
    {
        //printf( "nikon IFDOffset %lld, headerBase %lld\n", IFDOffset, headerBase );
        IFDOffset += 10;
        WORD endian = GetWORD( IFDOffset + headerBase, littleEndian );
        littleEndian = ( 0x4d4d != endian );
        //printf( "endian value: %#x\n", endian );

        // https://www.exiv2.org/tags-nikon.html     Format 3 for D100

        IFDOffset += 8;
        isNikon = true;

        EnumerateNikonMakernotes( depth, IFDOffset, headerBase, littleEndian );
        return;
    }
    if ( !strcmp( g_acMake, "Nikon" ) )
    {
        //printf( "nikon IFDOffset %lld, headerBase %lld\n", IFDOffset, headerBase );
        IFDOffset += 10;
        WORD endian = GetWORD( IFDOffset + headerBase, littleEndian );
        littleEndian = ( 0x4d4d != endian );
        //printf( "endian value: %#x\n", endian );

        // https://www.exiv2.org/tags-nikon.html     Format 3 for D100

        IFDOffset += 8;
        isNikon = true;

        EnumerateNikonMakernotes( depth, IFDOffset, headerBase, littleEndian );
        return;
    }
    if ( !strcmp( g_acMake, "NIKON" ) )
    {
        //printf( "nikon IFDOffset %lld, headerBase %lld\n", IFDOffset, headerBase );
        IFDOffset += 10;
        WORD endian = GetWORD( IFDOffset + headerBase, littleEndian );
        littleEndian = ( 0x4d4d != endian );
        //printf( "endian value: %#x\n", endian );

        // https://www.exiv2.org/tags-nikon.html     Format 3 for D100

        IFDOffset += 8;
        isNikon = true;

        EnumerateNikonMakernotes( depth, IFDOffset, headerBase, littleEndian );
        return;
    }
    else if ( !strcmp( g_acMake, "LEICA CAMERA AG" ) || !strcmp( g_acMake, "Leica Camera AG" ) )
    {
        //printf( "leica IFDOffset %lld, headerBase %lld\n", IFDOffset, headerBase );

        IFDOffset += 8;
        isLeica = true;
    }
    else if ( !strcmp( g_acMake, "RICOH IMAGING COMPANY, LTD." ) )
    {
        // GR III, etc.
        prf( "ricoh IFDOffset %lld, headerBase %lld\n", IFDOffset, headerBase );

        IFDOffset += 8;
        isRicoh = true;

        if ( !strcmp( g_acModel, "PENTAX K-3 Mark III" ) )
            IFDOffset += 2;
    }
    else if ( !strcmp( g_acMake, "RICOH" ) )
    {
        // THETA, etc.
        prf( "ricoh theta IFDOffset %lld, headerBase %lld\n", IFDOffset, headerBase );

        IFDOffset += 8;
        isRicohTheta = true;
    }
    else if ( !strcmp( g_acMake, "PENTAX" ) )
    {
        prf( "pentax IFDOffset %lld, headerBase %lld\n", IFDOffset, headerBase );

        IFDOffset += 6;
        isPentax = true;
    }
    else if ( !strcmp( g_acMake, "OLYMPUS IMAGING CORP." ) )
    {
        //printf( "olympus IFDOffset %lld, headerBase %lld\n", IFDOffset, headerBase );

        IFDOffset += 12;
        isOlympus = true;
    }
    else if ( !strcmp( g_acMake, "OLYMPUS CORPORATION" ) )
    {
        //printf( "olympus IFDOffset %lld, headerBase %lld\n", IFDOffset, headerBase );

        IFDOffset += 12;
        isOlympus = true;
    }
    else if ( !strcmp( g_acMake, "Eastman Kodak Company" ) )
    {
        prf( "eastman kodak IFDOffset %lld, headerBase %lld\n", IFDOffset, headerBase );

        isEastmanKodak = true;
        return; // apparently unparsable
    }
    else if ( !strcmp( g_acMake, "FUJIFILM" ) )
    {
        IFDOffset += 12;
        isFujifilm = true;

        EnumerateFujifilmMakernotes( depth, IFDOffset, headerBase, littleEndian );
        return;
    }
    else if ( !strcmp( g_acMake, "Panasonic" ) )
    {
        IFDOffset += 12;
        isPanasonic = true;

        EnumeratePanasonicMakernotes( depth, IFDOffset, headerBase, littleEndian );
        return;
    }
    else if ( !strcmp( g_acMake, "Apple" ) )
    {
        if ( !strcmp( g_acModel, "iPhone 12" ) )
        {
            IFDOffset += 14;
            littleEndian = false;
        }
        else
            IFDOffset += 14;

        isApple = true;
    }
    else if ( !strcmp( g_acMake, "SONY" ) ) // real camera
    {
        isSony = true;
    }
    else if ( !strcmp( g_acMake, "Sony" ) ) // cellphone
    {
        IFDOffset += 12;
        isSony = true;
    }
    else if ( !strcmp( g_acMake, "CANON" ) )
    {
        isCanon = true;
    }
    else if ( !strcmp( g_acMake, "" ) ) // Panasonic really did his
    {
        if ( !strcmp( g_acModel, "DMC-GM1" ) )
        {
            IFDOffset += 12;
            isPanasonic = true;
        }
    }
    else if ( !strcmp( g_acMake, "Hasselblad" ) )
    {
        isHasselblad = true;
    }
    else if ( !strcmp( g_acMake, "SIGMA" ) )
    {
        IFDOffset += 10;
        isSigma = true;
    }
    else if ( !strcmp( g_acMake, "Motorola" ) )
    {
        IFDOffset += 8; // Pointless at least for the ZN5, which has makernotes not even exiftool can parse
        isMotorola = true;
    }

    vector<IFDHeader> aHeaders( MaxIFDHeaders );

    //IFDOffset = dirOffset;

    while ( 0 != IFDOffset ) 
    {
        WORD NumTags = GetWORD( IFDOffset + headerBase, littleEndian );
        Space( depth );
        prf( "makernote IFDOffset %lld==%#llx, NumTags %d, headerBase %lld==%#llx, effective %#llx\n", IFDOffset, IFDOffset, NumTags, headerBase, headerBase, IFDOffset + headerBase );
//        prf( "  makernote dirOffset: %#x\n", dirOffset );
        IFDOffset += 2;

        if ( NumTags > MaxIFDHeaders )
        {
            Space( depth );
            prf( "        numtags is > 200; it's likely not in EXIF format, so the data may be noise. skipping (value is %#xs )\n", NumTags );
            break;
        }
    
        if ( !GetIFDHeaders( IFDOffset + headerBase, aHeaders.data(), NumTags, littleEndian ) )
        {
            Space( depth );
            prf( "MakernotesIFD has a tag that's invalid. skipping\n" );
            break;
        }

        for ( int i = 0; i < NumTags; i++ )
        {
            IFDHeader & head = aHeaders[ i ];
            IFDOffset += sizeof IFDHeader;

            if ( g_FullInformation )
            {
                Space( depth );
                prf( "makernote tag %d ID %d==%#x, type %d, count %d, offset/value %d==%#x\n", i, head.id, head.id, head.type, head.count, head.offset, head.offset );
            }

            Space( depth );

            if ( 2 == head.id && 2 == head.type && isRicohTheta )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;

                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "makernote FirmwareVersion:           %s\n", acBuffer );
            }
            else if ( 5 == head.id && 7 == head.type && isRicohTheta )
            {
                if ( head.count < ( _countof( acBuffer ) - 1 ) )
                {
                    GetBytes( head.offset + headerBase, acBuffer, head.count );
                    acBuffer[ head.count ] = 0;
                    
                    prf( "makernote Serial Number:             %s\n", acBuffer );
                }
            }
            else if ( 224 == head.id && 17 == head.count )
            {
                short sensorWidth = GetWORD( head.offset + headerBase + 2, littleEndian );
                short sensorHeight = GetWORD( head.offset + headerBase + 4, littleEndian );
                prf( "makernote sensor width %d, height    %d\n", sensorWidth, sensorHeight );

                short leftBorder = GetWORD( head.offset + headerBase + 10, littleEndian );
                short topBorder = GetWORD( head.offset + headerBase + 12, littleEndian );
                short rightBorder = GetWORD( head.offset + headerBase + 14, littleEndian );
                short bottomBorder = GetWORD( head.offset + headerBase + 16, littleEndian );
                Space( depth );
                prf( "makernote left, top, right, bottom:   %d %d %d %d\n", leftBorder, topBorder, rightBorder, bottomBorder );
            }
            else if ( 553 == head.id && 2 == head.type && isRicoh )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;

                // Ricoh assumes the base is originalIFDOffset
                GetString( originalIFDOffset + stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "makernote Serial Number:             %s\n", acBuffer );
            }
            else if ( 773 == head.id && isLeica )
            {
                prf( "makernote serial number:             %d\n", head.offset );
            }
            else if ( 1031 == head.id && isLeica )
            {
                char acOriginalFileName[ 100 ];
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acOriginalFileName, _countof( acOriginalFileName ), head.count );
                prf( "makernote OriginalFileName:          %s\n", acOriginalFileName );
            }
            else if ( 1032 == head.id && isLeica )
            {
                char acOriginalDirectory[ 100 ];
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acOriginalDirectory, _countof( acOriginalDirectory ), head.count );
                prf( "makernote OriginalDirectory:         %s\n", acOriginalDirectory );
            }
            else if ( 1280 == head.id && isLeica )
            {
                char acInternalSerialNumber[ 100 ];
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acInternalSerialNumber, _countof( acInternalSerialNumber ), head.count );
                prf( "makernote InternalSerialNumber:      %s\n", acInternalSerialNumber );
            }
            else if ( 8224 == head.id && 13 == head.type && isOlympus )
            {
                // Olympus camera settings IFD

                prf( "makernote OlympusCameraSettingsIFD   %d, originalIFDOffset %lld\n", head.offset, originalIFDOffset );

                EnumerateOlympusCameraSettingsIFD( depth + 1, head.offset, originalIFDOffset + headerBase, littleEndian );
            }
            else if ( 29184 == head.id && isSony )
                prf( "makernote Sony Sr2SubIFDOffset       %#x\n", head.offset );
            else if ( 29185 == head.id && isSony )
                prf( "makernote Sony Sr2SubIFDLength       %#x\n", head.offset );
            else if ( 29217 == head.id && isSony )
                prf( "makernote Sony Sr2SubIFDKey          %#x\n", head.offset );
            else if ( 29248 == head.id && isSony )
                prf( "makernote Sony IDC_IFD               %#x\n", head.offset );
            else if ( 29249 == head.id && isSony )
                prf( "makernote Sony IDC_IFD2              %#x\n", head.offset );
            else if ( 29264 == head.id && isSony )
                prf( "makernote Sony MRWInfo               %#x\n", head.offset );
            else
            {
                prf( "makernote tag %d ID %d==%#x, type %d, count %d, offset/value %d", i, head.id, head.id, head.type, head.count, head.offset );

                if ( 2 == head.type )
                {
                    ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                    GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                    prf( " %s", acBuffer );
                }

                prf( "\n" );

                if ( 7 == head.type )
                    DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );

                /*
                    For ORF files at least, 13 doesn't point to an easily consumable IFD

                else if ( 13 == head.type )
                    EnumerateGenericIFD( depth + 1, head.offset, headerBase, littleEndian );
                */
            }
        }

        IFDOffset = GetDWORD( IFDOffset + headerBase, littleEndian );
    }
} //EnumerateMakernotes

void EnumeratePanasonicCameraTags( int depth, __int64 IFDOffset, __int64 headerBase, bool littleEndian )
{
    vector<IFDHeader> aHeaders( MaxIFDHeaders );

    WORD header = GetWORD( IFDOffset + headerBase, littleEndian );
    if ( 0x4949 != header && 0x4d4d != header )
        prf( "unknown panasonic endianness header %#x\n", header );

    littleEndian = ( 0x4949 == header );

    IFDOffset += 8; // There is a TIFF Header at the base offset

    while ( 0 != IFDOffset ) 
    {
        WORD NumTags = GetWORD( IFDOffset + headerBase, littleEndian );
        Space( depth );
        prf( "panasonic IFDOffset %lld, NumTags %d, headerBase %lld\n", IFDOffset, NumTags, headerBase );
        IFDOffset += 2;
    
        if ( NumTags > MaxIFDHeaders )
        {
            Space( depth );
            prf( "numtags is > 200; it's likely not in EXIF format, so the data may be noise. skipping\n" );
            break;
        }
    
        if ( !GetIFDHeaders( IFDOffset + headerBase, aHeaders.data(), NumTags, littleEndian ) )
        {
            Space( depth );
            prf( "PanasonicCameraTags has a tag that's invalid. skipping\n" );
            break;
        }

        for ( int i = 0; i < NumTags; i++ )
        {
            IFDHeader & head = aHeaders[ i ];
            IFDOffset += sizeof IFDHeader;

            if ( g_FullInformation )
            {
                Space( depth );
                prf( "panasonic tag %d ID %d, type %d, count %d, offset/value %d\n", i, head.id, head.type, head.count, head.offset );
            }

            Space( depth );

            if ( 4096 == head.id && 257 == head.type )
                prf( "panasonic CameraIFD_0x1000:         %d\n", head.offset ); 
            else if ( 4352 == head.id && 3 == head.type )
                prf( "panasonic FocusStepNear:            %d\n", head.offset );
            else if ( 4353 == head.id && 3 == head.type )
                prf( "panasonic FocusStepCount:           %d\n", head.offset );
            else if ( 4354 == head.id && 1 == head.type )
                prf( "panasonic FlashFired:               %d\n", head.offset );
            else if ( 4357 == head.id && 4 == head.type )
                prf( "panasonic ZoomPosition:             %d\n", head.offset );
            else if ( 4608 == head.id && 1 == head.type )
                prf( "panasonic LensAttached:             %d\n", head.offset );
            else if ( 4609 == head.id && 3 == head.type )
                prf( "panasonic LensTypeMake:             %d\n", head.offset );
            else if ( 4610 == head.id && 3 == head.type )
                prf( "panasonic LensTypeModel:            %d\n", head.offset );
            else if ( 4611 == head.id && 3 == head.type )
                prf( "panasonic FocalLengthIn35mmFormat:  %d\n", head.offset );
            else if ( 4865 == head.id && 8 == head.type )
                prf( "panasonic ApertureValue:            %d\n", head.offset );
            else if ( 4866 == head.id && 8 == head.type )
                prf( "panasonic ShutterSpeedValue:        %d\n", head.offset );
            else if ( 4867 == head.id && 8 == head.type )
                prf( "panasonic SensitivityValue:         %d\n", head.offset );
            else if ( 4869 == head.id && 3 == head.type )
                prf( "panasonic HighISOMode:              %d\n", head.offset );
            else if ( 5138 == head.id && 1 == head.type )
                prf( "panasonic FacesDetected:            %d\n", head.offset );
            else if ( 12800 == head.id && 3 == head.type )
                prf( "panasonic WB_CFA0_LevelDaylight:    %d\n", head.offset );
            else if ( 12801 == head.id && 3 == head.type )
                prf( "panasonic WB_CFA1_LevelDaylight:    %d\n", head.offset );
            else if ( 12802 == head.id && 3 == head.type )
                prf( "panasonic WB_CFA2_LevelDaylight:    %d\n", head.offset );
            else if ( 12803 == head.id && 3 == head.type )
                prf( "panasonic WB_CFA3_LevelDaylight:    %d\n", head.offset );
            else if ( 13056 == head.id && 1 == head.type )
                prf( "panasonic WhiteBalanceSet:          %d\n", head.offset );
            else if ( 13344 == head.id && 3 == head.type )
                prf( "panasonic WB_RedLevelAuto:          %d\n", head.offset );
            else if ( 13345== head.id && 3 == head.type )
                prf( "panasonic WB_BlueLevelAuto:         %d\n", head.offset );
            else if ( 13569== head.id && 1 == head.type )
                prf( "panasonic Orientation:              %d\n", head.offset );
            else if ( 13824== head.id && 1 == head.type )
                prf( "panasonic WhiteBalanceDetected:     %d\n", head.offset );
            else
            {
                prf( "panasonic tag %d ID %d==%#x, type %d, count %d, offset/value %d\n", i, head.id, head.id, head.type, head.count, head.offset );

                if ( 7 == head.type )
                    DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
            }
        }

        IFDOffset = GetDWORD( IFDOffset + headerBase, littleEndian );
    }
} //EnumeratePanasonicCameraTags

void EnumerateAdobeImageResources( int depth, DWORD offset, DWORD headerBase, DWORD count )
{
    Space( depth );
    prf( "Adobe Image Resources\n" );

    // https://www.adobe.com/content/dam/acom/en/devnet/photoshop/psir/ps_image_resources.pdf

    DWORD o = offset + headerBase;

    DWORD signature = GetDWORD( o, true );
    o += 4;

    if ( 0x4d494238 == signature )
    {
        WORD id = GetWORD( o, false );
        o += 2;

        WORD nameLen = GetWORD( o, false );
        o += 2;
    
        o += nameLen;
    
        DWORD dataLen = GetDWORD( o, false );
        o += 4;
    
        Space( depth );

        if ( 1061 == id && 16 == dataLen )
        {
            BYTE captionDigest[ 16 ];

            prf( "caption digest:                     {" );

            for ( int i = 0; i < 16; i++ )
            {
                captionDigest[ i ] = GetBYTE( o++ );
                prf( "%x", captionDigest[ i ] );
            }

            prf( "}\n" );
        }
        else
            prf( "  Adobe Image Resource ID %d, name len %d, data len %d\n", id, nameLen, dataLen );
    }
} //EnumerateAdobeImageResoures

void EnumerateIPTC( int depth, DWORD offset, DWORD headerBase, DWORD count )
{
    Space( depth );
    prf( "IPTC dataset\n" );

    /*
        https://exiftool.org/TagNames/IPTC.html

        Record  Tag Name
          1     IPTCEnvelope
          2     IPTCApplication 
          3     IPTCNewsPhoto   
          7     IPTCPreObjectData 
          8     IPTCObjectData 
          9     IPTCPostObjectData 
        240     IPTCFotoStation
    */

    DWORD o = offset + headerBase;
    char acBuffer[ 50 ];

    do
    {
        BYTE tagMarker = GetBYTE( o++ );

        if ( 0x1c != tagMarker )
            break;

        BYTE recordNumber = GetBYTE( o++ );
        BYTE datasetNumber = GetBYTE( o++ );
        WORD dataLength = GetWORD( o, false );
        o += 2;
        DWORD dataO = o;
        bool extended = false;

        if ( 0x8000 & dataLength )
        {
            extended = true;
            dataLength &= 0x7fff;
        }

        bool known = false;
        Space( depth );

        if ( 1 == recordNumber )
        {
            if ( 0 == datasetNumber )
            {
                known = true;

                WORD envelopeRecordVersion = GetWORD( dataO, false );
                prf( "envelope record version:            %d\n", envelopeRecordVersion );
            }
            else if ( 90 == datasetNumber )
            {
                known = true;
                int chars = __min( _countof( acBuffer ) - 1, dataLength );

                for ( int i = 0; i < chars; i++ )
                    acBuffer[ i ] = GetBYTE( dataO++ );

                acBuffer[ chars ] = 0;

                prf( "coded character set:                %s\n", acBuffer );
            }

        }
        else if ( 2 == recordNumber )
        {
            if ( 0 == datasetNumber )
            {
                known = true;

                WORD appRecordVersion = GetWORD( dataO, false );
                prf( "application record version:         %d\n", appRecordVersion );
            }
            else if ( 55 == datasetNumber )
            {
                known = true;

                for ( int i = 0; i < 8; i++ )
                    acBuffer[ i ] = GetBYTE( dataO++ );

                acBuffer[ 8 ] = 0;

                prf( "date created:                       %s\n", acBuffer );
            }
            else if ( 60 == datasetNumber )
            {
                known = true;

                for ( int i = 0; i < 11; i++ )
                    acBuffer[ i ] = GetBYTE( dataO++ );

                acBuffer[ 11 ] = 0;

                prf( "time created:                       %s\n", acBuffer );
            }
        }

        if ( !known )
            prf( "marker %#x, rec number %d, datasetNumber %d, length %d, extended %d\n", tagMarker, recordNumber, datasetNumber, dataLength, extended );

        o += dataLength;
    } while ( true );
} //EnumerateIPTC

const char * ExifSensitivityType( DWORD x )
{
    if ( 1 == x )
        return "standard output sensitivity";
    if ( 2 == x )
        return "recommended exposure index";
    if ( 3 == x )
        return "ISO speed";
    if ( 4 == x )
        return "standard output sensitivity and recommended exposure index";
    if ( 5 == x )
        return "standard output sensitivity and ISO speed";
    if ( 6 == x )
        return "recommended exposure index and ISO speed";
    if ( 7 == x )
        return "standard output sensitivity, recommended exposure index and ISO speed";

    return "unknown";
} //ExifSensitivityType

const char * ExifExposureProgram( DWORD x )
{
    if ( 1 == x )
        return "manual";
    if ( 2 == x )
        return "program AE";
    if ( 3 == x )
        return "aperture-priority AE";
    if ( 4 == x )
        return "shutter speed priority AE";
    if ( 5 == x )
        return "creative (slow speed)";
    if ( 6 == x )
        return "action (high speed)";
    if ( 7 == x )
        return "portrait";
    if ( 8 == x )
        return "landscape";
    if ( 9 == x )
        return "bulb";

    return "unknown";
} //ExifExposureProgram

const char * ExifColorSpace( DWORD x )
{
    if ( 1 == x )
        return "sRGB";
    if ( 2 == x )
        return "Adobe RGB";
    if ( 0xfffd == x )
        return "Wide Gamut RGB";
    if ( 0xfffe == x )
        return "ICC Profile";
    if ( 0xffff == x )
        return "Uncalibrated";

    return "unknown";
} //ExifColorSpace

const char * ExifFileSource( DWORD x )
{
    if ( 1 == x )
        return "film scanner";
    if ( 2 == x )
        return "reflection print scanner";
    if ( 3 == x )
        return "digital camera";

    return "unknown";
} //ExifFileSource

const char * ExifCustomRendered( DWORD x )
{
    if ( 0 == x )
        return "normal";
    if ( 1 == x )
        return "custom";
    if ( 2 == x )
        return "HDR (no original saved)";
    if ( 3 == x )
        return "HDR (original saved)";
    if ( 4 == x )
        return "original (for HDR )";
    if ( 6 == x )
        return "panorama";
    if ( 7 == x )
        return "portrait HDR";
    if ( 8 == x )
        return "portrait";

    return "unknown";
} //ExifCustomRendered

const char * ExifExposureMode( DWORD x )
{
    if ( 0 == x )
        return "auto";
    if ( 1 == x )
        return "manual";
    if ( 2 == x )
        return "auto bracket";

    return "unknown";
} //ExifExposureMode

const char * ExifWhiteBalance( DWORD x )
{
    if ( 0 == x )
        return "auto";
    if ( 1 == x )
        return "manual";

    return "unknown";
} //ExifWhiteBalance

const char * ExifSceneCaptureType( DWORD x )
{
    if ( 0 == x )
        return "standard";
    if ( 1 == x )
        return "landscape";
    if ( 2 == x )
        return "portrait";
    if ( 3 == x )
        return "night";

    return "unknown";
} //ExifSceneCaptureType

const char * ExifContrast( DWORD x )
{
    if ( 0 == x )
        return "normal";
    if ( 1 == x )
        return "low";
    if ( 2 == x )
        return "high";

    return "unknown";
} //ExifContrast

const char * ExifSharpness( DWORD x )
{
    if ( 0 == x )
        return "normal";
    if ( 1 == x )
        return "soft";
    if ( 2 == x )
        return "hard";

    return "unknown";
} //ExifSharpness

const char * ExifPredictor( DWORD x )
{
    if ( 1 == x )
        return "none";
    if ( 2 == x )
        return "horizontal differencing";
    if ( 3 == x )
        return "floating point";
    if ( 34892 == x )
        return "horizontal difference x2";
    if ( 34893 == x )
        return "horizontal difference x4";
    if ( 34894 == x )
        return "floating point x2";
    if ( 34894 == x )
        return "floating point x4";

    return "unknown";
} //ExifPredictor

const char * ExifFillOrder( DWORD x )
{
    if ( 1 == x )
        return "normal";
    if ( 2 == x )
        return "reversed";

    return "unknown";
} //ExifFillOrder

const char * ExifSubjectDistanceRange( DWORD x )
{
    if ( 1 == x )
        return "macro";
    if ( 2 == x )
        return "close";
    if ( 3 == x )
        return "distant";

    return "unknown";
} //ExifSubjectDistanceRange

const char * ExifGainControl( DWORD x )
{
    if ( 0 == x )
        return "none";
    if ( 1 == x )
        return "low gain up";
    if ( 2 == x )
        return "high gain up";
    if ( 3 == x )
        return "low gain down";
    if ( 4 == x )
        return "high gain down";

    return "unknown";
} //ExifGainControl

const char * ExifComposite( DWORD x )
{
    if ( 1 == x )
        return "not a composite image";
    if ( 2 == x )
        return "general composite image";
    if ( 3 == x )
        return "composite image captured while shooting";

    return "unknown";
} //ExifComposite

void EnumerateGPSTags( int depth, __int64 IFDOffset, __int64 headerBase, bool littleEndian )
{
    vector<IFDHeader> aHeaders( MaxIFDHeaders );
    char acBuffer[ 200 ];

    if ( 0xffffffff == IFDOffset )
    {
        prf( "warning: GPS IFDOffset is invalid: %lld\n", IFDOffset );
        return;
    }

    while ( 0 != IFDOffset ) 
    {
        WORD NumTags = GetWORD( IFDOffset + headerBase, littleEndian );
        Space( depth );
        prf( "GPS IFDOffset %lld, NumTags %d\n", IFDOffset, NumTags );
        IFDOffset += 2;
    
        if ( NumTags > MaxIFDHeaders )
        {
            Space( depth );
            prf( "numtags is > 200; it's likely not in EXIF format, so the data may be noise. skipping\n" );
            break;
        }
    
        if ( !GetIFDHeaders( IFDOffset + headerBase, aHeaders.data(), NumTags, littleEndian ) )
        {
            Space( depth );
            prf( "GPS Tags has a tag that's invalid. skipping\n" );
            break;
        }

        for ( int i = 0; i < NumTags; i++ )
        {
            IFDHeader & head = aHeaders[ i ];
            IFDOffset += sizeof IFDHeader;

            if ( g_FullInformation )
            {
                Space( depth );
                prf( "GPS tag %d ID %d, type %d, count %d, offset/value %d\n", i, head.id, head.type, head.count, head.offset );
            }

            Space( depth );

            if ( 0 == head.id && 1 == head.type && 4 == head.count )
            {
                prf( "GPS VersionID:                      %#x\n", head.offset );
            }
            else if ( 1 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "GPS LatitudeRef:                    %s\n", acBuffer );
            }
            else if ( 2 == head.id && ( ( 10 == head.type ) || ( 5 == head.type ) ) && 3 == head.count )
            {
                DWORD num1 = GetDWORD( head.offset +      headerBase, littleEndian );
                DWORD den1 = GetDWORD( head.offset +  4 + headerBase, littleEndian );
                double d1 = (double) num1 / (double) den1;

                DWORD num2 = GetDWORD( head.offset +  8 + headerBase, littleEndian );
                DWORD den2 = GetDWORD( head.offset + 12 + headerBase, littleEndian );
                double d2 = (double) num2 / (double) den2;

                DWORD num3 = GetDWORD( head.offset + 16 + headerBase, littleEndian );
                DWORD den3 = GetDWORD( head.offset + 20 + headerBase, littleEndian );
                double d3 = (double) num3 / (double) den3;

                prf( "GPS Latitude:                       %lf, %lf, %lf\n", d1, d2, d3 );
            }
            else if ( 3 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "GPS LongitudeRef:                   %s\n", acBuffer );
            }
            else if ( 4 == head.id && ( ( 10 == head.type ) || ( 5 == head.type ) ) && 3 == head.count )
            {
                DWORD num1 = GetDWORD( head.offset +      headerBase, littleEndian );
                DWORD den1 = GetDWORD( head.offset +  4 + headerBase, littleEndian );
                double d1 = (double) num1 / (double) den1;

                DWORD num2 = GetDWORD( head.offset +  8 + headerBase, littleEndian );
                DWORD den2 = GetDWORD( head.offset + 12 + headerBase, littleEndian );
                double d2 = (double) num2 / (double) den2;

                DWORD num3 = GetDWORD( head.offset + 16 + headerBase, littleEndian );
                DWORD den3 = GetDWORD( head.offset + 20 + headerBase, littleEndian );
                double d3 = (double) num3 / (double) den3;

                prf( "GPS Longitude:                      %lf, %lf, %lf\n", d1, d2, d3 );
            }
            else if ( 5 == head.id && 1 == head.type )
                prf( "GPS AltitudeRef:                    %d (%s sea level)\n", head.offset, ( 0 == head.offset ) ? "above" : "below" );
            else if ( 6 == head.id && 5 == head.type && 1 == head.count )
            {
                DWORD num1 = GetDWORD( head.offset +      headerBase, littleEndian );
                DWORD den1 = GetDWORD( head.offset +  4 + headerBase, littleEndian );
                double d1 = (double) num1 / (double) den1;

                prf( "GPS Altitude:                       %lf\n", d1 );
            }
            else if ( 7 == head.id && ( ( 10 == head.type) || ( 5 == head.type ) ) && 3 == head.count )
            {
                DWORD num1 = GetDWORD( head.offset +      headerBase, littleEndian );
                DWORD den1 = GetDWORD( head.offset +  4 + headerBase, littleEndian );
                double d1 = (double) num1 / (double) den1;

                DWORD num2 = GetDWORD( head.offset +  8 + headerBase, littleEndian );
                DWORD den2 = GetDWORD( head.offset + 12 + headerBase, littleEndian );
                double d2 = (double) num2 / (double) den2;

                DWORD num3 = GetDWORD( head.offset + 16 + headerBase, littleEndian );
                DWORD den3 = GetDWORD( head.offset + 20 + headerBase, littleEndian );
                double d3 = (double) num3 / (double) den3;

                prf( "GPS TimeStamp:                      %lf, %lf, %lf\n", d1, d2, d3 );
            }
            else if ( 8 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "GPS Satellites:                     %s\n", acBuffer );
            }
            else if ( 9 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                const char * status = "unknown";
                if ( 'v' == tolower( acBuffer[ 0 ] ) )
                    status = "void measurement";
                else if ( 'a' == tolower( acBuffer[ 0 ] ) )
                    status = "active measurement";
                prf( "GPS Status:                         %s (%s)\n", acBuffer, status );
            }
            else if ( 10 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "GPS MeasureMode:                    %s-dimensional measurement\n", acBuffer );
            }
            else if ( 11 == head.id && 5 == head.type && 1 == head.count )
            {
                DWORD num1 = GetDWORD( head.offset +      headerBase, littleEndian );
                DWORD den1 = GetDWORD( head.offset +  4 + headerBase, littleEndian );
                double d1 = (double) num1 / (double) den1;

                prf( "GPS DOP:                            %lf\n", d1 );
            }
            else if ( 12 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );

                char c = tolower( acBuffer[ 0 ] );
                const char * units = "unknown";

                if ( 'k' == c )
                    units = "km/h";
                else if ( 'm' == c )
                    units = "mph";
                else if ( 'n' == c )
                    units = "knots";

                prf( "GPS SpeedRef:                       %s (%s)\n", acBuffer, units );
            }
            else if ( 13 == head.id && 5 == head.type && 1 == head.count )
            {
                DWORD num1 = GetDWORD( head.offset +      headerBase, littleEndian );
                DWORD den1 = GetDWORD( head.offset +  4 + headerBase, littleEndian );
                double d1 = (double) num1 / (double) den1;

                prf( "GPS Speed:                          %lf\n", d1 );
            }
            else if ( 15 == head.id && 5 == head.type && 1 == head.count )
            {
                DWORD num1 = GetDWORD( head.offset +      headerBase, littleEndian );
                DWORD den1 = GetDWORD( head.offset +  4 + headerBase, littleEndian );
                double d1 = (double) num1 / (double) den1;

                prf( "GPS Track:                          %lf\n", d1 );
            }
            else if ( 16 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );

                char c = tolower( acBuffer[ 0 ] );
                const char * direction = "unknown";

                if ( 'm' == c )
                    direction = "magnetic";
                else if ( 't' == c )
                    direction = "true";

                prf( "GPS ImgDirectionRef:                %s (%s north)\n", acBuffer, direction );
            }
            else if ( 17 == head.id && 5 == head.type && 1 == head.count )
            {
                DWORD num1 = GetDWORD( head.offset +      headerBase, littleEndian );
                DWORD den1 = GetDWORD( head.offset +  4 + headerBase, littleEndian );
                double d1 = (double) num1 / (double) den1;

                prf( "GPS ImgDirection:                   %lf\n", d1 );
            }
            else if ( 18 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "GPS DestLatitudeRef:                %s\n", acBuffer );
            }
            else if ( 23 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );

                char c = tolower( acBuffer[ 0 ] );
                const char * direction = "unknown";

                if ( 'm' == c )
                    direction = "magnetic";
                else if ( 't' == c )
                    direction = "true";

                prf( "GPS DestBearingRef:                 %s (%s north)\n", acBuffer, direction );
            }
            else if ( 24 == head.id && 5 == head.type && 1 == head.count )
            {
                DWORD num1 = GetDWORD( head.offset +      headerBase, littleEndian );
                DWORD den1 = GetDWORD( head.offset +  4 + headerBase, littleEndian );
                double d1 = (double) num1 / (double) den1;

                prf( "GPS DestBearing:                    %lf\n", d1 );
            }
            else if ( 27 == head.id && 7 == head.type )
            {
                prf( "GPS ProcessingMethod type           %d, count %d, offset/value %d\n", i, head.type, head.count, head.offset );
                DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
            }
            else if ( 27 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "GPS ProcessingMethod type           %s\n", acBuffer );
            }
            else if ( 29 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "GPS DateStamp:                      %s\n", acBuffer );
            }
            else if ( 31 == head.id && 5 == head.type && 1 == head.count )
            {
                DWORD num1 = GetDWORD( head.offset +      headerBase, littleEndian );
                DWORD den1 = GetDWORD( head.offset +  4 + headerBase, littleEndian );
                double d1 = (double) num1 / (double) den1;

                prf( "GPS HPositioningError:              %lf\n", d1 );
            }
            else if ( 59932 == head.id && 7 == head.type )
            {
                prf( "GPS Padding type                    %d, count %d, offset %d\n", head.type, head.count, head.offset );
                DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
            }
            else
            {
                prf( "GPS tag %d ID %d==%#x, type %d, count %d, offset/value %d\n", i, head.id, head.id, head.type, head.count, head.offset );

                if ( 7 == head.type )
                    DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
            }
        }

        IFDOffset = GetDWORD( IFDOffset + headerBase, littleEndian );

        if ( 0xffffffff == IFDOffset )
        {
            prf( "warning: next GPS IFDOffset is invalid: %lld\n", IFDOffset );
            return;
        }
    }
} //EnumerateGPSTags

const char * ExifSceneType( DWORD offset )
{
    if ( 1 == offset )
        return "directly photographed";

    return "unknown scene type";
} //ExifSceneType

const char * ExifSensorSizeUnit( DWORD x )
{
    if ( 2 == x )
        return "inches";
    if ( 3 == x )
        return "centimeters";
    if ( 4 == x )
        return "millimeters";
    if ( 5 == x )
        return "micrometers";

    return "unknown";
} //ExifSensorSizeUnit

const char * ExifImageExposureTimes( DWORD offset )
{
    // http://www.cipa.jp/std/documents/e/DC-X008-Translation-2019-E.pdf

    if ( 0 == offset )
        return "total exposure period";
    if ( 8 == offset )
        return "sum of respective exposure times";
    if ( 16 == offset )
        return "sum of respective exposure times used";
    if ( 24 == offset )
        return "max exposure time";
    if ( 32 == offset )
        return "max exposure time used";
    if ( 40 == offset )
        return "min exposure time";
    if ( 48 == offset )
        return "min exposure time used";
    if ( 56 == offset )
        return "number of sequences";
    if ( 58 == offset )
        return "number of source images in sequence";
    if ( 60 == offset )
        return "exposure time of 1st image";
    if ( 68 == offset )
        return "exposure time of 2nd image";

    return "unknown";
} //ExifImageExposureTimes

const char * GetFlash( short x )
{
    x &= 0x7f; // mask off unrecognized bits

    if ( 0x0 == x ) return "no flash";
    if ( 0x1 == x ) return "fired";
    if ( 0x5 == x ) return "fired, return not detected";
    if ( 0x7 == x ) return "fired, return detected";
    if ( 0x8 == x ) return "on, did not fire";
    if ( 0x9 == x ) return "on, fired";
    if ( 0xd == x ) return "on, return not detected";
    if ( 0xf == x ) return "on, return detected";
    if ( 0x10 == x ) return "off, did not fire";
    if ( 0x14 == x ) return "off, did not fire, return not detected";
    if ( 0x18 == x ) return "auto, did not fire";
    if ( 0x19 == x ) return "auto, fired";
    if ( 0x1d == x ) return "auto, fired, return not detected";
    if ( 0x1f == x ) return "auto, fired, return detected";
    if ( 0x20 == x ) return "no flash function";
    if ( 0x30 == x ) return "off, no flash function";
    if ( 0x41 == x ) return "fired, red-eye reduction";
    if ( 0x45 == x ) return "fired, red-eye reduction, return not detected";
    if ( 0x47 == x ) return "fired, red-eye reduction, return detected";
    if ( 0x49 == x ) return "on, red-eye reduction";
    if ( 0x4d == x ) return "on, red-eye reduction, return not detected";
    if ( 0x4f == x ) return "on, red-eye reduction, return detected";
    if ( 0x50 == x ) return "off, red-eye reduction";
    if ( 0x58 == x ) return "auto, did not fire, red-eye reduction";
    if ( 0x59 == x ) return "auto, fired, red-eye reduction";
    if ( 0x5d == x ) return "auto, fired, red-eye reduction, return not detected";
    if ( 0x5f == x ) return "auto, fired, red-eye reduction, return detected";
    return "unknown";
} //GetFlash

bool GetFlash( short x, char * ac, int len )
{
    if ( len < 100 )
        return false;

    *ac = 0;

    if ( x & 0x1 )
        strcat( ac, "flash fired, " );
    else
        strcat( ac, "flash did not fire, " );

    short returnedLight = ( x >> 1 ) & 0x3;
    if ( 0 == returnedLight )
        strcat( ac, "no strobe return detection function, " );
    else if ( 1 == returnedLight )
        strcat( ac, "01, " );
    else if ( 2 == returnedLight )
        strcat( ac, "strobe return light not detected, " );
    else
        strcat( ac, "strobe return light detected, " );

    short flashMode = ( x >> 3 ) & 0x3;
    if ( 0 == flashMode )
        strcat( ac, "unknown, " );
    else if ( 1 == flashMode )
        strcat( ac, "compulsory flash firing, " );
    else if ( 2 == flashMode )
        strcat( ac, "compulsory flash suppression, " );
    else
        strcat( ac, "auto mode, " );

    short flashFunction = ( x >> 5 ) & 0x1;
    if ( 0 == flashFunction )
        strcat( ac, "flash function present, " );
    else
        strcat( ac, "no flash function, " );

    short redEye = ( x >> 6 ) & 0x1;
    if ( 0 == redEye )
        strcat( ac, "no red-eye reduction mode" );
    else
        strcat( ac, "red-eye reduction supported" );

    return true;
} //GetFlash

void EnumerateExifTags( int depth, __int64 IFDOffset, __int64 headerBase, bool littleEndian )
{
    DWORD XResolutionNum = 0;
    DWORD XResolutionDen = 0;
    DWORD YResolutionNum = 0;
    DWORD YResolutionDen = 0;
    DWORD sensorSizeUnit = 0; // 2==inch, 3==centimeter
    double focalLength = 0.0;
    WORD pixelWidth = 0;
    WORD pixelHeight = 0;
    vector<IFDHeader> aHeaders( MaxIFDHeaders );
    char acBuffer[ 200 ];

    while ( 0 != IFDOffset ) 
    {
        WORD NumTags = GetWORD( IFDOffset + headerBase, littleEndian );
        Space( depth );
        prf( "exif IFDOffset %lld, NumTags %d\n", IFDOffset, NumTags );
        IFDOffset += 2;
    
        if ( NumTags > MaxIFDHeaders )
        {
            Space( depth );
            prf( "numtags is > 200; it's likely not in EXIF format, so the data may be noise. skipping (%d)\n", NumTags );
            break;
        }
    
        if ( !GetIFDHeaders( IFDOffset + headerBase, aHeaders.data(), NumTags, littleEndian ) )
        {
            Space( depth );
            prf( "ExifTags has a tag that's invalid. skipping\n" );
            break;
        }

        for ( int i = 0; i < NumTags; i++ )
        {
            IFDHeader & head = aHeaders[ i ];
            IFDOffset += sizeof IFDHeader;

            if ( g_FullInformation )
            {
                Space( depth );
                prf( "exif tag %d ID %d=%#x, type %d, count %d, offset/value %d\n", i, head.id, head.id, head.type, head.count, head.offset );
            }

            Space( depth );

            if ( 282 == head.id && 10 == head.type )
            {
                DWORD num = GetDWORD( head.offset + headerBase, littleEndian );
                DWORD den = GetDWORD( head.offset + headerBase + 4, littleEndian );
                prf( "XResolution:                        %d / %d = %lf\n", num, den, (double) num / (double) den );
            }
            else if ( 283 == head.id && 10 == head.type )
            {
                DWORD num = GetDWORD( head.offset + headerBase, littleEndian );
                DWORD den = GetDWORD( head.offset + headerBase + 4, littleEndian );
                prf( "YResolution:                        %d / %d = %lf\n", num, den, (double) num / (double) den );
            }
            else if ( 306 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "exif ModifyDate:                    %s\n", acBuffer );
            }
            else if ( 513 == head.id && 4 == head.type )
                prf( "exif ThumbnailOffset:               %d\n", head.offset );
            else if ( 514 == head.id && 4 == head.type )
                prf( "exif ThumbnailLength:               %d\n", head.offset );
            else if ( 33434 == head.id && 5 == head.type )
            {
                DWORD num = GetDWORD( head.offset + headerBase, littleEndian );
                DWORD den = GetDWORD( head.offset + headerBase + 4, littleEndian );
                prf( "exif ExposureTime:                  %d / %d = %lf\n", num, den, (double) num / (double) den );
            }
            else if ( 33434 == head.id && 4 == head.type )
            {
                DWORD den = head.offset;
                prf( "exif ExposureTime (type 4):         %d / %d = %lf\n", 1, den, (double) 1 / (double) den );
            }
            else if ( 33437 == head.id && 5 == head.type )
            {
                DWORD num = GetDWORD( head.offset + headerBase, littleEndian );
                DWORD den = GetDWORD( head.offset + headerBase + 4, littleEndian );
                prf( "exif FNumber:                       %d / %d = %lf\n", num, den, (double) num / (double) den );
            }
            else if ( 34850 == head.id )
                prf( "exif ExposureProgram                %s\n", ExifExposureProgram( head.offset ) );
            else if ( 34855 == head.id )
                prf( "exif ISO                            %d\n", head.offset );
            else if ( 34864 == head.id )
                prf( "exif Sensitivity Type               %s\n", ExifSensitivityType( head.offset ) );
            else if ( 34865 == head.id )
                prf( "exif Standard Output Sensitivity:   %d\n", head.offset );
            else if ( 34866 == head.id )
                prf( "exif Recommended Exposure Index     %d\n", head.offset );
            else if ( 36864 == head.id )
                prf( "exif ExifVersion                    %d\n", head.count );
            else if ( 36867 == head.id && 2 == head.type )
            {
                char acDateTimeOriginal[ 100 ];
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acDateTimeOriginal, _countof( acDateTimeOriginal ), head.count );
                prf( "exif DateTimeOriginal:              %s\n", acDateTimeOriginal );
            }
            else if ( 36868 == head.id && 2 == head.type )
            {
                char acCreateDate[ 100 ];
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acCreateDate, _countof( acCreateDate ), head.count );
                prf( "exif CreateDate:                    %s\n", acCreateDate );
            }
            else if ( 36880 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "exif Offset Time:                   %s\n", acBuffer );
            }
            else if ( 36881 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "exif Offset Time Original:          %s\n", acBuffer );
            }
            else if ( 36882 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "exif Offset Time Digital:           %s\n", acBuffer );
            }
            else if ( 37121 == head.id && 7 == head.type )
            {
                prf( "exif ComponentsConfiguration:       %d bytes\n", head.count );
                DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
            }
            else if ( 37122 == head.id && 5 == head.type )
            {
                DWORD num = GetDWORD( head.offset + headerBase, littleEndian );
                DWORD den = GetDWORD( head.offset + 4 + headerBase, littleEndian );

                prf( "exif CompressedBitsPerPixel:        %d / %d = %lf\n", num, den, (double) num / (double) den );
            }
            else if ( 37377 == head.id && 10 == head.type )
            {
                int num = (int) GetDWORD( head.offset + headerBase, littleEndian );
                int den = (int) GetDWORD( head.offset + 4 + headerBase, littleEndian );

                prf( "exif ShutterSpeedValue:             %d / %d = %lf\n", num, den, (double) num / (double) den );
            }
            else if ( 37378 == head.id && 5 == head.type )
            {
                DWORD num = GetDWORD( head.offset + headerBase, littleEndian );
                DWORD den = GetDWORD( head.offset + 4 + headerBase, littleEndian );

                double aperture = (double) num / (double) den;
                double fnumber = pow( sqrt( 2.0 ), aperture );
                prf( "exif ApertureValue:                 %d / %d = %lf == f / %lf\n", num, den, aperture, fnumber );
            }
            else if ( 37379 == head.id && 10 == head.type )
            {
                int num = (int) GetDWORD( head.offset + headerBase, littleEndian );
                int den = (int) GetDWORD( head.offset + 4 + headerBase, littleEndian );

                prf( "exif BrightnessValue:               %d / %d = %lf\n", num, den, (double) num / (double) den );
            }
            else if ( 37379 == head.id && 5 == head.type ) // iPhone 11 uses head.type 5
            {
                DWORD num = GetDWORD( head.offset + headerBase, littleEndian );
                DWORD den = GetDWORD( head.offset + 4 + headerBase, littleEndian );

                prf( "exif BrightnessValue:               %d / %d = %lf\n", num, den, (double) num / (double) den );
            }
            else if ( 37380 == head.id && 10 == head.type )
            {
                int num = (int) GetDWORD( head.offset + headerBase, littleEndian );
                int den = (int) GetDWORD( head.offset + 4 + headerBase, littleEndian );

                prf( "exif ExposureCompensation:          %d / %d = %lf\n", num, den, (double) num / (double) den );
            }
            else if ( 37381 == head.id && 5 == head.type )
            {
                DWORD num = GetDWORD( head.offset + headerBase, littleEndian );
                DWORD den = GetDWORD( head.offset + 4 + headerBase, littleEndian );

                prf( "exif MaxApertureValue:              %d / %d = %lf\n", num, den, (double) num / (double) den );
            }
            else if ( 37382 == head.id )
            {
                DWORD num = GetDWORD( head.offset + headerBase, littleEndian );
                DWORD den = GetDWORD( head.offset + 4 + headerBase, littleEndian );
 
                prf( "exif SubjectDistance (meters):      %d / %d = %lf\n", num, den, (double) num / (double) den );
            }
            else if ( 37383 == head.id )
                prf( "exif MeteringMode:                  %d\n", head.offset );
            else if ( 37384 == head.id )
                prf( "exif LightSource:                   %d\n", head.offset );
            else if ( 37385 == head.id )
                prf( "exif Flash:                         %d - %s\n", head.offset, GetFlash( head.offset ) );
            else if ( 37386 == head.id && 5 == head.type )
            {
                DWORD num = GetDWORD( head.offset + headerBase, littleEndian );
                DWORD den = GetDWORD( head.offset + 4 + headerBase, littleEndian );

                if ( 0 != den )
                    focalLength = (double) num / (double) den;
 
                prf( "exif FocalLength:                   %d / %d = %lf\n", num, den, (double) num / (double) den );
            }
            else if ( 37393 == head.id && 4 == head.type && 1 == head.count )
                prf( "exif ImageNumber:                   %d\n", head.offset );
            else if ( 37396 == head.id && 3 == head.type && 4 == head.count )
            {
                WORD a = GetWORD( head.offset +     headerBase, littleEndian );
                WORD b = GetWORD( head.offset + 2 + headerBase, littleEndian );
                WORD c = GetWORD( head.offset + 4 + headerBase, littleEndian );
                WORD d = GetWORD( head.offset + 6 + headerBase, littleEndian );

                prf( "exif SubjectArea:                   %d, %d, %d, %d\n", a, b, c, d );
            }
            else if ( 37500 == head.id )
            {
                prf( "makernotes IFD at offset:           %d\n", head.offset );
                EnumerateMakernotes( depth + 1, head.offset, headerBase, littleEndian );
            }
            else if ( 37510 == head.id )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "exif User Comment:                  %s\n", acBuffer );
            }
            else if ( 37520 == head.id && 2 == head.type )
            {
                char acSubSecTime[ 100 ];
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acSubSecTime, _countof( acSubSecTime ), head.count );
                prf( "exif SubSecTime:                    %s\n", acSubSecTime );
            }
            else if ( 37521 == head.id && 2 == head.type )
            {
                char acSubSecTimeOriginal[ 100 ];
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acSubSecTimeOriginal, _countof( acSubSecTimeOriginal ), head.count );
                prf( "exif SubSecTimeOriginal:            %s\n", acSubSecTimeOriginal );
            }
            else if ( 37522 == head.id && 2 == head.type )
            {
                char acSubSecTimeDigitized[ 100 ];
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acSubSecTimeDigitized, _countof( acSubSecTimeDigitized ), head.count );
                prf( "exif SubSecTimeDigitized:           %s\n", acSubSecTimeDigitized );
            }
            else if ( 40960 == head.id )
                prf( "exif FlashpixVersion                %#x\n", head.offset );
            else if ( 40961 == head.id )
                prf( "exif Color Space                    %s\n", ExifColorSpace( head.offset ) );
            else if ( 40962 == head.id )
            {
                pixelWidth = head.offset;
                prf( "exif PixelWidth                     %d\n", pixelWidth );
            }
            else if ( 40963 == head.id )
            {
                pixelHeight = head.offset;
                prf( "exif PixelHeight                    %d\n", pixelHeight );
            }
            else if ( 40964 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "exif Related Sound File:            %s\n", acBuffer );
            }
            else if ( 40965 == head.id )
            {
                prf( "exif Interop IFD\n" );
                EnumerateInteropIFD( depth + 1, head.offset, headerBase, littleEndian );
            }
            else if ( 41486 == head.id )
            {
                XResolutionNum = GetDWORD( head.offset + headerBase, littleEndian );
                XResolutionDen = GetDWORD( head.offset + headerBase + 4, littleEndian );
                ULONG num = XResolutionNum;
                ULONG den = XResolutionDen;
                prf( "exif XResolution:                   %d / %d = %lf\n", num, den, (double) num / (double) den );
            }
            else if ( 41487 == head.id )
            {
                YResolutionNum = GetDWORD( head.offset + headerBase, littleEndian );
                YResolutionDen = GetDWORD( head.offset + headerBase + 4, littleEndian );
                ULONG num = YResolutionNum;
                ULONG den = YResolutionDen;
                prf( "exif YResolution:                   %d / %d = %lf\n", num, den, (double) num / (double) den );
            }
            else if ( 41488 == head.id )
            {
                sensorSizeUnit = head.offset;
                prf( "exif SensorSizeUnit:                %d (%s)\n", sensorSizeUnit, ExifSensorSizeUnit( sensorSizeUnit ) );
            }
            else if ( 41495 == head.id )
                prf( "exif Sensing Method:                %s\n", ExifSensingMethod( head.offset ) );
            else if ( 41728 == head.id && 7 == head.type )
                prf( "exif FileSource:                    %s\n", ExifFileSource( head.offset ) );
            else if ( 41729 == head.id && ( 7 == head.type ) || ( 1 == head.type ) )
                prf( "exif Scene Type:                    %s\n", ExifSceneType( head.offset ) );
            else if ( 41730 == head.id && 7 == head.type && 8 == head.count )
            {
                prf( "exif CFA Pattern:                   " );

                ULONG o = head.offset + headerBase;

                for ( int i = 0; i < 8; i++ )
                {
                    prf( "%#x", GetBYTE( o++ ) );
                    if ( 7 != i )
                        prf( ", " );
                }

                prf( "\n" );
            }
            else if ( 41985 == head.id )
                prf( "exif Custom Rendered:               %s\n", ExifCustomRendered( head.offset ) );
            else if ( 41986 == head.id )
                prf( "exif Exposure Mode:                 %s\n", ExifExposureMode( head.offset ) );
            else if ( 41987 == head.id )
                prf( "exif White Balance:                 %s\n", ExifWhiteBalance( head.offset ) );
            else if ( 41988 == head.id && 5 == head.type )
            {
                DWORD num = GetDWORD( head.offset + headerBase, littleEndian );
                DWORD den = GetDWORD( head.offset + 4 + headerBase, littleEndian );

                prf( "exif Digital Zoom Ratio:            %d / %d = %lf\n", num, den, (double) num / (double) den );
            }
            else if ( 41989 == head.id )
                prf( "exif FocalLengthIn35mmFilm:         %d\n", head.offset );
            else if ( 41990 == head.id )
                prf( "exif Scene Capture Type:            %s\n", ExifSceneCaptureType( head.offset ) );
            else if ( 41991 == head.id )
                prf( "exif Gain Control:                  %s\n", ExifGainControl( head.offset ) );
            else if ( 41992 == head.id )
                prf( "exif Contrast:                      %s\n", ExifContrast( head.offset ) );
            else if ( 41993 == head.id )
                prf( "exif Saturation:                    %s\n", ExifContrast( head.offset ) ); // same values as saturation
            else if ( 41994 == head.id )
                prf( "exif Sharpness:                     %s\n", ExifSharpness( head.offset ) );
            else if ( 41996 == head.id )
                prf( "exif Subject Distance Range:        %s\n", ExifSubjectDistanceRange( head.offset ) );
            else if ( 42016 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "exif ImageUniqueID:                 %s\n", acBuffer );
            }
            else if ( 42032 == head.id )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "exif Owner Name:                    %s\n", acBuffer );
            }
            else if ( 42033 == head.id )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "exif Body Serial Number:            %s\n", acBuffer );
            }
            else if ( 42034 == head.id && 5 == head.type && 4 == head.count )
            {
                DWORD numMinFL = GetDWORD( head.offset +      headerBase, littleEndian );
                DWORD denMinFL = GetDWORD( head.offset +  4 + headerBase, littleEndian );

                DWORD numMaxFL = GetDWORD( head.offset +  8 + headerBase, littleEndian );
                DWORD denMaxFL = GetDWORD( head.offset + 12 + headerBase, littleEndian );

                DWORD numMinAp = GetDWORD( head.offset + 16 + headerBase, littleEndian );
                DWORD denMinAp = GetDWORD( head.offset + 20 + headerBase, littleEndian );

                DWORD numMaxAp = GetDWORD( head.offset + 24 + headerBase, littleEndian );
                DWORD denMaxAp = GetDWORD( head.offset + 28 + headerBase, littleEndian );

                prf( "exif Lens min/max FL and Aperture:  %.2lf, %.2lf, %.2lf, %.2lf\n",
                        (double) numMinFL / (double) denMinFL,
                        (double) numMaxFL / (double) denMaxFL,
                        (double) numMinAp / (double) denMinAp,
                        (double) numMaxAp / (double) denMaxAp );
            }
            else if ( 42035 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "exif Lens Make:                     %s\n", acBuffer );
            }
            else if ( 42036 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "exif Lens Model:                    %s\n", acBuffer );
            }
            else if ( 42037 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "exif Lens Serial Number:            %s\n", acBuffer );
            }
            else if ( 42080 == head.id && 3 == head.type && 1 == head.count )
                prf( "exif Composite Image:               %s\n", ExifComposite( head.offset ) );
            else if ( 42081 == head.id && 3 == head.type && 2 == head.count )
            {
                WORD images = ( head.offset >> 16 ) & 0xffff;
                WORD imagesUsed = head.offset & 0xffff;

                prf( "exif Composite Image Count:         %d images, %d of which were used\n", images, imagesUsed );
            }
            else if ( 42082 == head.id && 7 == head.type )
            {
                prf( "exif ImageExposure Times\n" );

                DWORD soFar = 0;

                while ( soFar < head.count )
                {
                    if ( soFar < 56 )
                    {
                        ULONG num = GetDWORD( head.offset + soFar + headerBase, littleEndian );
                        ULONG den = GetDWORD( head.offset + soFar + 4 + headerBase, littleEndian );
                        double d = (double) num / (double) den;

                        Space( depth );
                        prf( "       %6lf    -- %s\n", d, ExifImageExposureTimes( soFar ) );

                        soFar += 8;
                    }
                    else if ( 56 == soFar )
                    {
                        Space( depth );
                        prf( "       %8d    -- %s\n", head.offset, ExifImageExposureTimes( soFar ) );
                        soFar += 2;
                    }
                    else
                        soFar = head.count;
                }
            }
            else if ( 42240 == head.id && 5 == head.type )
            {
                DWORD num = GetDWORD( head.offset +      headerBase, littleEndian );
                DWORD den = GetDWORD( head.offset +  4 + headerBase, littleEndian );
                prf( "exif Gamma:                         %d / %d = %lf\n", num, den, (double) num / (double) den );
            }
            else if ( 59932 == head.id && 7 == head.type )
            {
                prf( "exif Padding type                   %d, count %d, offset %d\n", head.type, head.count, head.offset );
                DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
            }
            else if ( 59933 == head.id && 9 == head.type )
                prf( "exif MSFTOffsetSchema type           %d, count %d, offset %d\n", head.type, head.count, head.offset );
            else if ( 0 == head.id )
                prf( "exif has invalid tag %d ID %d==%#x, type %d, count %d, offset/value %d\n", i, head.id, head.id, head.type, head.count, head.offset );
            else
            {
                prf( "exif tag %d ID %d==%#x, type %d, count %d, offset/value %d\n", i, head.id, head.id, head.type, head.count, head.offset );

                if ( 7 == head.type )
                    DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
            }
        }

        IFDOffset = GetDWORD( IFDOffset + headerBase, littleEndian );
    }

    if ( 0 != XResolutionNum && 0 != XResolutionDen && 0 != YResolutionNum && 0 != YResolutionDen && 0 != sensorSizeUnit &&
         0 != pixelWidth && 0 != pixelHeight)
    {
        //printf( "XResolutionNum %d, XResolutionDen %d, YResoltuionNum %d, YResolutionDen %d\n",
        //        XResolutionNum, XResolutionDen, YResolutionNum, YResolutionDen );

        //printf( "pixel dimension: %d %d\n", pixelWidth, pixelHeight );

        double resolutionX = (double) XResolutionNum / (double) XResolutionDen;
        double resolutionY = (double) YResolutionNum / (double) YResolutionDen;

        //printf( "ppu X %lf, Y %lf\n", resolutionX, resolutionY );

        double unitsX = (double) pixelWidth / resolutionX;
        double unitsY = (double) pixelHeight / resolutionY;

        double sensorSizeXmm = 0.0;
        double sensorSizeYmm = 0.0;

        if ( 2 == sensorSizeUnit )
        {
            // inches

            sensorSizeXmm = unitsX * 25.4;
            sensorSizeYmm = unitsY * 25.4;
        }
        else if ( 3 == sensorSizeUnit )
        {
            sensorSizeXmm = unitsX * 10.0;
            sensorSizeYmm = unitsY * 10.0;
        }
        else // 4==mm 5==um perhaps?
        {
        }

        Space( depth );
        prf( "exif (Computed) sensor WxH:         %lf, %lf\n", sensorSizeXmm, sensorSizeYmm );
    }
} //EnumerateExifTags

void PrintPanasonicIFD0Tag( int depth, WORD id, WORD type, DWORD count, DWORD offset, __int64 headerBase, bool littleEndian, __int64 IFDOffset )
{
    if ( 1 == id )
        prf( "panasonicRawVersion:                  %#x\n", offset );
    else if ( 2 == id )
        prf( "panasonicSensorWidth:                 %d\n", offset );
    else if ( 3 == id )
        prf( "panasonicSensorHeight:                %d\n", offset );
    else if ( 4 == id )
        prf( "panasonicSensorTopBorder:             %d\n", offset );
    else if ( 5 == id )
        prf( "panasonicSensorLeftBorder:            %d\n", offset );
    else if ( 6 == id )
        prf( "panasonicSensorBottomBorder:          %d\n", offset );
    else if ( 7 == id )
        prf( "panasonicSensorRightBorder:           %d\n", offset );
    else if ( 8 == id )
        prf( "panasonicSamplesPerPixel:             %d\n", offset );
    else if ( 9 == id )
        prf( "panasonicCFAPattern:                  %d\n", offset );
    else if ( 10 == id )
        prf( "panasonicBitsPerSample:               %d\n", offset );
    else if ( 11 == id )
        prf( "panasonicCompression:                 %d\n", offset );
    else if ( 14 == id )
        prf( "panasonicLinearityLimitRed:           %d\n", offset );
    else if ( 15 == id )
        prf( "panasonicLinearityLimitGreen:         %d\n", offset );
    else if ( 16 == id )
        prf( "panasonicLinearityLimitBlue:          %d\n", offset );
    else if ( 23 == id )
        prf( "panasonicISO:                         %d\n", offset );
    else if ( 24 == id )
        prf( "panasonicHighISOMultiplierRed:        %d\n", offset );
    else if ( 25 == id )
        prf( "panasonicHighISOMultiplierGreen:      %d\n", offset );
    else if ( 26 == id )
        prf( "panasonicHighISOMultiplierBlue:       %d\n", offset );
    else if ( 27 == id )
    {
        prf( "panasonicNoiseReductionParams:        %d bytes at %d\n", count, offset );
        DumpBinaryData( offset, headerBase, count, 4, IFDOffset - 4 );
    }
    else if ( 28 == id )
        prf( "panasonicBlackLevelRed:               %d\n", offset );
    else if ( 29 == id )
        prf( "panasonicBlackLevelGreen:             %d\n", offset );
    else if ( 30 == id )
        prf( "panasonicBlackLevelBlue:              %d\n", offset );
    else if ( 36 == id )
        prf( "panasonicWBRedLevel:                  %d\n", offset );
    else if ( 37 == id )
        prf( "panasonicWBGreenLevel:                %d\n", offset );
    else if ( 38 == id )
        prf( "panasonicWBBlueLevel:                 %d\n", offset );
    else if ( 39 == id )
    {
        prf( "panasonicWBInfo2:                     %d bytes at %d\n", count, offset );
        DumpBinaryData( offset, headerBase, count, 4, IFDOffset - 4 );
    }
    else if ( 45 == id )
        prf( "panasonicRawFormat:                   %d\n", offset );
    else if ( 46 == id )
    {
        prf( "panasonicJPGFromRaw:                  %d bytes at %d\n", count, offset );
        DumpBinaryData( offset, headerBase, count, 4, IFDOffset - 4 );

        g_Embedded_Image_Offset = offset;
        g_Embedded_Image_Length = count;
    }
    else if ( 47 == id )
        prf( "panasonicCropTop:                     %d\n", offset );
    else if ( 48 == id )
        prf( "panasonicCropLeft:                    %d\n", offset );
    else if ( 49 == id )
        prf( "panasonicCropBottom:                  %d\n", offset );
    else if ( 50 == id )
        prf( "panasonicCropRight:                   %d\n", offset );
    else if ( 280 == id )
        prf( "panasonicRawDataOffset                %d\n", offset );
    else if ( 281 == id )
    {
        prf( "panasonicDistortionInfo               %d bytes at %d\n", count, offset );
        DumpBinaryData( offset, headerBase, count, 4, IFDOffset - 4 );
    }
    else if ( 284 == id )
        prf( "panasonicGamma:                       %d\n", offset );
    else if ( 288 == id )
        EnumeratePanasonicCameraTags( depth + 1, offset, headerBase, littleEndian );
    else if ( 289 == id )
        prf( "panasonicMultishot:                   %d\n", offset );
    else
        prf( "panasonicTag ID %d==%#x, type %d, count %d, offset/value %d\n", id, id, type, count, offset );
} //PrintPanasonicIFD0Tag

const char * JPGComponents( int x )
{
    if ( 1 == x )
        return "grey scale";

    if ( 3 == x )
        return "YCbCr";

    if ( 4 == x )
        return "CMYK";

    return "unknown";
} //JPGComponents

struct QuantizationTableSegment
{
    BYTE Pq;
    BYTE Tq;
    BYTE table[ 64 ];
};

struct ICC_PROFILE_Values
{
    DWORD size;
    DWORD cmmType;
    DWORD version;
    DWORD profileClass;
    DWORD colorSpace;
    DWORD pcs;
    WORD year;
    WORD month;
    WORD day;
    WORD hours;
    WORD minutes;
    WORD seconds;
    DWORD signature;
    DWORD platform;
    DWORD options;
    DWORD manufacturer;
    DWORD model;
    long long attributes;
    DWORD intent;
    DWORD illuminantX;
    DWORD illuminantY;
    DWORD illuminantZ;
    DWORD creator;
    BYTE id[ 16 ];
    BYTE reserved[ 28 ];

    void ByteSwap( bool littleEndian )
    {
        if ( !littleEndian )
        {
            size = _byteswap_ulong( size );
            profileClass = _byteswap_ulong( profileClass );
            year = _byteswap_ushort( year );
            month = _byteswap_ushort( month );
            day = _byteswap_ushort( day );
            hours = _byteswap_ushort( hours );
            minutes = _byteswap_ushort( minutes );
            seconds = _byteswap_ushort( seconds );
            intent = _byteswap_ulong( intent );
            illuminantX = _byteswap_ulong( illuminantX );
            illuminantY = _byteswap_ulong( illuminantY );
            illuminantZ = _byteswap_ulong( illuminantZ );
        }
    }
};

const char * SOFMarker( BYTE b )
{
    if ( 0xc0 == b ) return "baseline DCT process frame marker";
    if ( 0xc1 == b ) return "extended sequential DCT frame marker, Huffman coding";
    if ( 0xc2 == b ) return "progressive DCT frame marker, Huffman coding";
    if ( 0xc3 == b ) return "lossless process frame marker, Huffman coding";
    if ( 0xc5 == b ) return "differential sequential DCT frame marker, Huffman coding";
    if ( 0xc6 == b ) return "differential progressive DCT frame marker, Huffman coding";
    if ( 0xc7 == b ) return "differential lossless process frame marker, Huffman coding";
    if ( 0xc9 == b ) return "sequential DCT frame marker, arithmetic coding";
    if ( 0xca == b ) return "progressive DCT frame marker, arithmetic coding";
    if ( 0xcb == b ) return "lossless process frame marker, arithmetic coding";
    if ( 0xcd == b ) return "differential sequential DCT frame marker, arithmetic coding";
    if ( 0xce == b ) return "differential progressive DCT frame marker, arithmetic coding";
    if ( 0xcf == b ) return "differential lossless process frame marker, arithmetic coding";

    return "unknown";
} //SOFMarker

const char * GetICCProfileClass( DWORD d )
{
    if ( 0x73636e72 == d )
        return "input device profile";

    if ( 0x6d6e7472 == d )
        return "display device profile";

    if ( 0x70727472 == d )
        return "output device profile";

    if ( 0x6c696e6b == d )
        return "devicelink profile";

    if ( 0x73706163 == d )
        return "colorspace profile";

    if ( 0x61627374 == d )
        return "abstract profile";

    if ( 0x6e6d636c == d )
        return "namedcolor profile";

    return "unknown";
} //GetICCProfileClass

const char * GetICCRenderingIntent( WORD w )
{
    if ( 0 == w )
        return "perceptual";

    if ( 1 == w )
        return "media-relative colorimetric";

    if ( 2 == w )
        return "saturation";

    if ( 3 == w )
        return "ICC-absolute colorimetric";

    return "unknown";
} //GetICCRenderingIntent

double ConvertS15Fixed16Number( DWORD d )
{
    // Number Encoded as
    // -32768.0               80000000h
    // 0.0                    00000000h
    // 1.0                    00010000h
    // 32767 + (65535/65536)  7FFFFFFFh

    if ( d & 0x80000000 )
    {
        // number is negative
        // This codepath hasn't been tested

        d &= 0x7fffffff;

        return ( - ( (double) d * 32768.0 / (double) 0x7fffffff ) );
    }

    if ( d < 0x10000 )
    {
        // number is 0 - 1
        // Note: this is the only codepath that's really been tested

        return ( (double) d / (double) 0x10000 );
    }

    // This codepath has been tested for 1.0, but nothing else

    double maxvalue = 32767.0 * 65535.0 / 65536.0;
    DWORD range = 0x7fffffff - 0x10000;

    return 1.0 + ( ( (double) d - (double) 0x10000 ) / (double) range ) * maxvalue;
} //ConvertS15Fixed16Number

const char * GetJPGApp14ColorTransform( byte ct )
{
    if ( 0 == ct )
        return ( "unknown (rgb or cmyk)" );

    if ( 1 == ct )
        return "YCbCr";

    if ( 2 == ct )
        return "YCCK";

    return "unknown";
} //GetJPGApp14ColorTransform

struct ICC_TAG
{
    char  sig[4];
    DWORD dataOffset;
    DWORD dataSize;
};

const char * GetICCStandardObserver( DWORD s )
{
    if ( 1 == s )
        return "CIE 1931 standard colorimetric observer";

    if ( 2 == s )
        return "CIE 1964 standard colorimetric observer";

    return "unknown";
} //GetICCStandardObserver

const char * GetICCGeometry( DWORD geom )
{
    if ( 1 == geom )
        return "0/45 or 45/0";

    if ( 2 == geom )
        return "0/d or d/0";

    return "unknown";
} //GetICCGeometry

const char * GetICCMeasurementFlare( DWORD flare )
{
    static char acResult[ 100 ];

    sprintf( acResult, "%lf%%", 100.0 * (double) flare / (double) 0x10000 );

    return acResult;
} //GetICCMeasurementFlare

const char * GetICCStandardIlluminant( DWORD i )
{
    if ( 1 == i )
        return "D50";

    if ( 2 == i )
        return "D65";

    if ( 3 == i )
        return "D93";

    if ( 4 == i )
        return "F2";

    if ( 5 == i )
        return "D55";

    if ( 6 == i )
        return "A";

    if ( 7 == i )
        return "Equi-Power (E)";

    if ( 8 == i )
        return "F8";

    return "unknown";
} //GetICCStandardIlluminant

void PrintICCValue( ICC_TAG & t, DWORD offset )
{
    char acTag[ 5 ];
    acTag[ 4 ] = 0;
    memcpy( acTag, t.sig, 4 );

    char acType[ 5 ];
    acType[ 4 ] = 0;
    GetBytes( offset, acType, 4 );

    if ( g_FullInformation )
    {
        prf( "  tag %s, type %s\n", acTag, acType );
        DumpBinaryData( offset, 0, t.dataSize, 8, offset );
    }

    if ( !strcmp( acTag, "cprt" ) )
    {
        int len = t.dataSize - 8;
        vector<char> value( len );
        GetBytes( offset + 8, value.data(), len );
        prf( "  Copyright:                          %s\n", value.data() );
    }
    else if ( !strcmp( acTag, "desc" ) )
    {
        int len = t.dataSize - 12;
        vector<char> value( len );
        GetBytes( offset + 12, value.data(), len );
        prf( "  ProfileDescription:                 %s\n", value.data() );
    }
    else if ( !strcmp( acTag, "wtpt" ) )
    {
        DWORD x = GetDWORD( offset + 8, false );
        DWORD y = GetDWORD( offset + 12, false );
        DWORD z = GetDWORD( offset + 16, false );
        prf( "  MediaWhitePoint:                    %lf %lf %lf\n", ConvertS15Fixed16Number( x ),
                ConvertS15Fixed16Number( y ), ConvertS15Fixed16Number( z ) );
    }
    else if ( !strcmp( acTag, "bkpt" ) )
    {
        DWORD x = GetDWORD( offset + 8, false );
        DWORD y = GetDWORD( offset + 12, false );
        DWORD z = GetDWORD( offset + 16, false );
        prf( "  MediaBlackPoint:                    %lf %lf %lf\n", ConvertS15Fixed16Number( x ),
                ConvertS15Fixed16Number( y ), ConvertS15Fixed16Number( z ) );
    }
    else if ( !strcmp( acTag, "rXYZ" ) )
    {
        DWORD x = GetDWORD( offset + 8, false );
        DWORD y = GetDWORD( offset + 12, false );
        DWORD z = GetDWORD( offset + 16, false );
        prf( "  RedMatrixColumn:                    %lf %lf %lf\n", ConvertS15Fixed16Number( x ),
                ConvertS15Fixed16Number( y ), ConvertS15Fixed16Number( z ) );
    }
    else if ( !strcmp( acTag, "gXYZ" ) )
    {
        DWORD x = GetDWORD( offset + 8, false );
        DWORD y = GetDWORD( offset + 12, false );
        DWORD z = GetDWORD( offset + 16, false );
        prf( "  GreenMatrixColumn:                  %lf %lf %lf\n", ConvertS15Fixed16Number( x ),
                ConvertS15Fixed16Number( y ), ConvertS15Fixed16Number( z ) );
    }
    else if ( !strcmp( acTag, "bXYZ" ) )
    {
        DWORD x = GetDWORD( offset + 8, false );
        DWORD y = GetDWORD( offset + 12, false );
        DWORD z = GetDWORD( offset + 16, false );
        prf( "  BlueMatrixColumn:                   %lf %lf %lf\n", ConvertS15Fixed16Number( x ),
                ConvertS15Fixed16Number( y ), ConvertS15Fixed16Number( z ) );
    }
    else if ( !strcmp( acTag, "dmnd" ) )
    {
        int len = t.dataSize - 12;
        vector<char> value( len );
        GetBytes( offset + 12, value.data(), len );
        prf( "  DeviceMfgDesc:                      %s\n", value.data() );
    }
    else if ( !strcmp( acTag, "dmdd" ) )
    {
        int len = t.dataSize - 12;
        vector<char> value( len );
        GetBytes( offset + 12, value.data(), len );
        prf( "  DeviceModelDesc:                    %s\n", value.data() );
    }
    else if ( !strcmp( acTag, "vued" ) )
    {
        int len = t.dataSize - 12;
        vector<char> value( len );
        GetBytes( offset + 12, value.data(), len );
        prf( "  ViewingCondDesc:                    %s\n", value.data() );
    }
    else if ( !strcmp( acTag, "lumi" ) )
    {
        DWORD x = GetDWORD( offset + 8, false );
        DWORD y = GetDWORD( offset + 12, false );
        DWORD z = GetDWORD( offset + 16, false );
        prf( "  Luminance:                          %lf %lf %lf\n", ConvertS15Fixed16Number( x ),
                ConvertS15Fixed16Number( y ), ConvertS15Fixed16Number( z ) );
    }
    else if ( !strcmp( acTag, "meas" ) )
    {
        struct MeasurementType
        {
            DWORD type;
            DWORD reserved;
            DWORD standardObserver;
            DWORD xTriStimulus;
            DWORD yTriStimulus;
            DWORD zTriStimulus;
            DWORD geometry;
            DWORD flare;
            DWORD standard;
        };

        MeasurementType mt;
        GetBytes( offset, &mt, sizeof mt );
        mt.standardObserver = _byteswap_ulong( mt.standardObserver );
        mt.xTriStimulus = _byteswap_ulong( mt.xTriStimulus );
        mt.yTriStimulus = _byteswap_ulong( mt.yTriStimulus );
        mt.zTriStimulus = _byteswap_ulong( mt.zTriStimulus );
        mt.geometry = _byteswap_ulong( mt.geometry );
        mt.flare = _byteswap_ulong( mt.flare );
        mt.standard = _byteswap_ulong( mt.standard );

        prf( "  Measurement:\n" );
        prf( "    standard observer:                %d\n", mt.standardObserver, GetICCStandardObserver( mt.standardObserver ) );
        prf( "    x tristimulus backing:            %lf\n", ConvertS15Fixed16Number( mt.xTriStimulus ) );
        prf( "    y tristimulus backing:            %lf\n", ConvertS15Fixed16Number( mt.yTriStimulus ) );
        prf( "    z tristimulus backing:            %lf\n", ConvertS15Fixed16Number( mt.zTriStimulus ) );
        prf( "    measurement geometry:             %d (%s)\n", mt.geometry, GetICCGeometry( mt.geometry ) );
        prf( "    measurement flare:                %d (%s)\n", mt.flare, GetICCMeasurementFlare( mt.flare ) );
        prf( "    standard illuminant:              %d (%s)\n", mt.standard, GetICCStandardIlluminant( mt.standard ) );
    }
    else if ( !strcmp( acTag, "tech" ) )
    {
        char acTech[ 5 ];
        acTech[ 4 ] = 0;
        GetBytes( offset + 8, acTech, 4 );
        prf( "  technology:                         %s\n", acTech );
    }
    else if ( !strcmp( acTag, "view" ) )
    {
        DWORD x = GetDWORD( offset + 8, false );
        DWORD y = GetDWORD( offset + 12, false );
        DWORD z = GetDWORD( offset + 16, false );
        prf( "  ViewingConditions:                  %lf %lf %lf\n", ConvertS15Fixed16Number( x ),
                ConvertS15Fixed16Number( y ), ConvertS15Fixed16Number( z ) );
    }
    else if ( !strcmp( acTag, "rTRC" ) )
    {
        DWORD count = GetDWORD( offset + 8, false );
        prf( "  Red tone reproduction curve #:      %d\n", count );
    }
    else if ( !strcmp( acTag, "gTRC" ) )
    {
        DWORD count = GetDWORD( offset + 8, false );
        prf( "  Green tone reproduction curve #:    %d\n", count );
    }
    else if ( !strcmp( acTag, "bTRC" ) )
    {
        DWORD count = GetDWORD( offset + 8, false );
        prf( "  Blue tone reproduction curve #:     %d\n", count );
    }
    else
    {
        if ( !g_FullInformation )
        {
            prf( "  tag %s, type %s\n", acTag, acType );
            DumpBinaryData( offset, 0, t.dataSize, 8, offset );
        }
    }

} //PrintICCValue

void PrintXMPData( const char * pcIn )
{
    // look for known xml tags rather than exhaustively parse the xml

    const char * pcTag = "xmp:Rating>";
    const char * pcRating = strstr( pcIn, pcTag );

    if ( !pcRating )
    {
        // jpg and Sony RAW ARW files will have this form

        pcTag = "xmp:Rating=\"";
        pcRating = strstr( pcIn, pcTag );
    }

    if ( !pcRating )
    {
        // Hasselblad RAW files have this form

        pcTag = "xap:Rating>";
        pcRating = strstr( pcIn, pcTag );
    }

    if ( pcRating )
    {
        pcRating += strlen( pcTag );
        int rating = *pcRating;

        if ( rating >= '0' && rating <= '5' )          // doesn't handle Adobe Bridge's -1
            prf( "Rating:                               %c\n", rating );
        else
            prf( "XMP Rating value isn't 0-5: '%c' == %#x\n", rating, rating );
    }
} //PrintXMPData

int ParseOldJpg( DWORD startingOffset = 0 )
{
    prf( "mimetype:                             image/jpeg\n" );

    DWORD offset = 2 + startingOffset;
    WORD width = 0;
    WORD height = 0;
    int DQTSegmentCount;
    QuantizationTableSegment * pDQTSegs = NULL;
    int exifOffset = 0;

    const BYTE MARKER_SOF0  = 0xc0;          // start of frame baseline
    const BYTE MARKER_SOF2  = 0xc2;          // start of frame progressive
    const BYTE MARKER_DHT   = 0xc4;          // Define Huffman table
    const BYTE MARKER_JPG   = 0xc8;          // JPG extensions
    const BYTE MARKER_DAC   = 0xcc;          // Define arithmetic coding conditioning(s)
    const BYTE MARKER_SOF15 = 0xcf;          // last of the SOF? marker
    const BYTE MARKER_SOI   = 0xd8;          // start of image
    const BYTE MARKER_EOI   = 0xd9;          // end of image
    const BYTE MARKER_SOS   = 0xda;          // start of scan
    const BYTE MARKER_DQT   = 0xdb;          // Quantization table
    const BYTE MARKER_DRI   = 0xdd;          // 
    const BYTE MARKER_APP0  = 0xe0;          // JFIF application segment
    const BYTE MARKER_APP1  = 0xe1;          // Exif or adobe segment
    const BYTE MARKER_APP2  = 0xe2;          // ICC_PROFILE
    const BYTE MARKER_PS3   = 0xed;          // Photoshop 3.08BIM
    const BYTE MARKER_APP14 = 0xee;          // Adobe  JPEG::Adobe directory
    const BYTE MARKER_COM   = 0xfe;          // comment

    do
    {
        BYTE marker = GetBYTE( offset );
        if ( 0xff != marker )
            return exifOffset;

        BYTE segment = GetBYTE( offset + 1 );

        if ( g_FullInformation )
            prf( "jpg offset %d, segment %#x\n", offset, segment );

        if ( MARKER_EOI == segment )
        {
            //printf( "found 0xd9 EOI segment\n" );
            return exifOffset;
        }

        if ( 0xff == segment )
        {
            offset += 2;
            continue;
        }

        WORD length = GetWORD( offset + 2, false );
        WORD data_length = length - 2;

        if ( g_FullInformation )
            prf( "  segment length %d\n", length );

        if ( MARKER_DQT == segment )
        {
            if ( length < 67 )
                return exifOffset;

            if ( 0 != ( data_length % 65 ) )
                return exifOffset;

            DQTSegmentCount = data_length / 65;

            if ( g_FullInformation )
                prf( "  dqt segments: %d\n", DQTSegmentCount );

            int o = offset + 4;
            pDQTSegs = new QuantizationTableSegment[ DQTSegmentCount ];

            for ( int i = 0; i < DQTSegmentCount; i++ )
            {
                BYTE dqtInfo = GetBYTE( o++ );
                pDQTSegs[ i ].Pq = dqtInfo & 0xf;
                pDQTSegs[ i ].Tq = ( dqtInfo >> 4 ) & 0xf;

                for ( int j = 0; j < 64; j++ )
                    pDQTSegs[ i ].table[ j ] = GetBYTE( o++ );
            }
        }
        else if ( MARKER_COM == segment )
        {
            unique_ptr<char> comment( new char[ data_length + 1 ] );

            for ( int i = 0; i < data_length; i++ )
                comment.get()[i] = GetBYTE( offset + 4 + i );

            comment.get()[ data_length ] = 0;

            prf( "comment:                           %s\n", comment.get() );
        }
        else if ( MARKER_DHT == segment || MARKER_JPG == segment || MARKER_DAC == segment )
        {
            // These 3 fall in the range of the valid SOF? segments, so watch for them and ignore
        }
        else if ( segment >= MARKER_SOF0 && segment <= MARKER_SOF15 )
        {
            prf( "start of frame:                   %5d (%#x) (%s)\n", segment, segment, SOFMarker( segment ) );

            byte precision = GetBYTE( offset + 4 );
            height = GetWORD( offset + 5, false );
            width = GetWORD( offset + 7, false );
            byte components = GetBYTE( offset + 9 );

            prf( "bits per sample:                  %5d\n", precision );
            prf( "image height:                     %5d\n", height );
            prf( "image width:                      %5d\n", width );
            prf( "color components:                 %5d (%s)\n", components, JPGComponents( components ) );

            for ( int component = 0; component < components; component++ )
            {
                int componentID = GetBYTE( offset + 10 + ( component * 3 ) );
                int samplingFactor = GetBYTE( offset + 11 + ( component * 3 ) );
                int quantizationTable = GetBYTE( offset + 12 + ( component * 3 ) );

                if ( g_FullInformation )
                {
                    prf( "  component %d ID  %5d\n", component, componentID );
                    prf( "  component %d sampling factor  vertical %d, horizontal %d\n", component, samplingFactor & 0xf, samplingFactor >> 4 );
                    prf( "  component %d quantization table %d\n", component, quantizationTable );
                }
            }

            int componentID = GetBYTE( offset + 10 );
            int samplingFactor = GetBYTE( offset + 11 );
            int horizS = samplingFactor >> 4;
            int vertS = samplingFactor & 0xf;

            if ( 3 == components )
            {
                int sub2 = 0;
                int sub3 = 0;

                if ( 1 == horizS && 1 == vertS )
                {
                    sub2 = 4;
                    sub3 = 4;
                }
                else if ( 2 == horizS && 1 == vertS )
                {
                    sub2 = 2;
                    sub3 = 2;
                }
                else if ( 2 == horizS && 2 == vertS )
                {
                    sub2 = 2;
                    sub3 = 0;
                }

                prf( "compression:                          YCbCr4:%d:%d (%d %d)\n", sub2, sub3, horizS, vertS );
            }

        }
        else if ( MARKER_APP0 == segment )
        {
            char acID[ 5 ];
            for ( int i = 0; i < 5; i++ )
                acID[ i ] = GetBYTE( offset + 4 + i );
            acID[ 4 ] = 0;

            BYTE majorVersion = GetBYTE( offset + 9 );
            BYTE minorVersion = GetBYTE( offset + 10 );
            BYTE units = GetBYTE( offset + 11 );
            WORD xDensity = GetWORD( offset + 12, false );
            WORD yDensity = GetWORD( offset + 14, false );
            BYTE thumbWidth = GetBYTE( offset + 16 );
            BYTE thumbHeight = GetBYTE( offset + 17 );

            prf( "id marker:                            %s\n", acID );
            prf( "major version:                    %5d\n", majorVersion );
            prf( "minor version:                    %5d\n", minorVersion );
            prf( "units:                                %s\n", ( 0 == units ) ? "none" : ( 1 == units ) ? "dots/inch" : ( 2 == units ) ? "dots/cm" : "unknown" );
            prf( "x density:                        %5d\n", xDensity );
            prf( "y density:                        %5d\n", yDensity );
        }
        else if ( MARKER_APP2 == segment )
        {
            //printf( "app2 offset %#x\n", offset );
            // https://www.color.org/ICC1V42.pdf

            char acHeader[ 13 ];
            acHeader[ 12 ] = 0;
            GetBytes( offset + 4, &acHeader, 12 );
            prf( "APP2 header %s\n", acHeader );

            if ( !stricmp( acHeader, "FPXR" ) )
            {
                // fujifilm-specific header
            }
            else if ( !stricmp( acHeader, "ICC_PROFILE" ) )
            {
                int icc_offset = offset + 4 + 14;
                ICC_PROFILE_Values icc;
                GetBytes( icc_offset, &icc, sizeof icc );
                icc.ByteSwap( false );
    
                //printf( "data_length %d, sizeof icc %d\n", data_length, (int) sizeof icc );
                prf( "  size:                           %5d\n", icc.size );
    
                memcpy( acHeader, &icc.cmmType, 4 );
                acHeader[ 4 ] = 0;
                prf( "  cmm type:                           %s (%#x)\n", acHeader, icc.cmmType );
    
                prf( "  version:                            %d.%d.%d\n", icc.version & 0xff, ( icc.version >> 12 ) & 0xf, ( icc.version >> 8 ) & 0xf );
                prf( "  profile class:                      %s (%#x)\n", GetICCProfileClass( icc.profileClass ), icc.profileClass );
    
                memcpy( acHeader, &icc.colorSpace, 4 );
                acHeader[ 4 ] = 0;
                prf( "  color space:                        %s (%#x)\n", acHeader, icc.colorSpace );
    
                memcpy( acHeader, &icc.pcs, 4 );
                acHeader[ 4 ] = 0;
                prf( "  connection space:                   %s (%#x)\n", acHeader, icc.pcs );
    
                prf( "  dd/mm/yyyy hh:mm:ss:                %02d/%02d/%04d %02d:%02d:%02d\n", icc.day, icc.month, icc.year, icc.hours, icc.minutes, icc.seconds );
    
                memcpy( acHeader, &icc.signature, 4 );
                acHeader[ 4 ] = 0;
                prf( "  signature:                          %s (%#x)\n", acHeader, icc.signature );
    
                memcpy( acHeader, &icc.platform, 4 );
                acHeader[ 4 ] = 0;
                prf( "  primary platform:                   %s (%#x)\n", acHeader, icc.platform );
    
                prf( "  cmm flags:                          %s, %s\n", ( icc.options & 0x1 ) ? "embedded" : "not embedded", ( icc.options & 0x2 ) ? "not independent" : "independent" );
    
                memcpy( acHeader, &icc.manufacturer, 4 );
                acHeader[ 4 ] = 0;
                prf( "  manufacturer:                       %s (%#x)\n", acHeader, icc.manufacturer );
    
                memcpy( acHeader, &icc.model, 4 );
                acHeader[ 4 ] = 0;
                prf( "  model:                              %s (%#x)\n", acHeader, icc.model );
    
                prf( "  attributes:                         %s, %s, %s, %s\n", ( icc.attributes & 0x1 ) ? "transparency" : "reflective",
                                                                                  ( icc.attributes & 0x2 ) ? "matte" : "glossy",
                                                                                  ( icc.attributes & 0x4 ) ? "polarity negative" : "polarity positive",
                                                                                  ( icc.attributes & 0x8 ) ? "black and white media" : "color media" );
    
                prf( "  rendering intent:                   %s\n", GetICCRenderingIntent( icc.intent & 0xffff ) );
    
                prf( "  illuminant:                         %lf, %lf, %lf\n", ConvertS15Fixed16Number( icc.illuminantX ),
                                                                                 ConvertS15Fixed16Number( icc.illuminantY ),
                                                                                 ConvertS15Fixed16Number( icc.illuminantZ ) );
                memcpy( acHeader, &icc.creator, 4 );
                acHeader[ 4 ] = 0;
                prf( "  creator:                            %s (%#x)\n", acHeader, icc.creator );
    
                prf( "  profile id:                         " );
                for ( size_t i = 0; i < _countof( icc.id ); i++ )
                    prf( "%#x", icc.id[ i ] );
                prf( "\n" );
    
                //printf( "data_length and sizeof icc: %d %d\n", data_length, (int) sizeof icc );
    
                if ( data_length > (int) sizeof icc )
                {
                    // A tag table follows
    
                    DWORD currentOffset = offset + 4 + 14 + sizeof icc;
    
                    DWORD tagCount = GetDWORD( currentOffset, false );
                    currentOffset += 4;
                    prf( "ICC tag table has %d entries\n", tagCount );
    
                    for ( DWORD tag = 0; tag < tagCount; tag++ )
                    {
                        ICC_TAG t;
                        t.sig[0] = GetBYTE( currentOffset );
                        t.sig[1] = GetBYTE( currentOffset + 1 );
                        t.sig[2] = GetBYTE( currentOffset + 2 );
                        t.sig[3] = GetBYTE( currentOffset + 3 );
                        t.dataOffset = GetDWORD( currentOffset + 4, false );
                        t.dataSize = GetDWORD( currentOffset + 8, false );
    
                        DWORD value_offset = icc_offset + t.dataOffset;
                        PrintICCValue( t, value_offset );
                        currentOffset += sizeof t;
                    }
                }
            }
        }
        else if ( MARKER_SOS == segment )
        {
            DWORD o = offset + 4;

            BYTE numberOfComponents = GetBYTE( o++ );
            prf( "number of components:                 %d\n", numberOfComponents );

            if ( numberOfComponents > 4 )
                numberOfComponents = 4;

            BYTE c_ID[ 4 ];
            BYTE c_Huff[ 4 ];

            for ( int i = 0; i < numberOfComponents; i++ )
            {
                c_ID[ i ] = GetBYTE( o++ );
                c_Huff[ i ] = GetBYTE( o++ );
            }

            for ( int i = 0; i < numberOfComponents; i++ )
            {
                if ( g_FullInformation )
                {
                    BYTE c = c_ID[ i ];
                    prf( "  Component ID [%d]: %s", i, ( 1 == c ) ? "Y" : ( 2 == c ) ? "Cb" : ( 3 == c ) ? "Cr" : ( 4 == c ) ? "I" : ( 5 == c ) ? "Q" : "unknown" );
                    prf( " -- DC table: %d, AC table %d\n", c_Huff[ i ] & 0xf, c_Huff[ i ] >> 4 );
                }
            }

            bool normal = ( ( 3 == numberOfComponents ) && ( 1 == c_ID[ 0 ] ) && ( 2 == c_ID[ 1 ] ) && ( 3 == c_ID[ 2 ] ) );

            break;
        }
        else if ( MARKER_PS3 == segment )
        {
            prf( "photoshop irb data:\n" );

            DumpBinaryData( offset + 4, 0, data_length, 8, 0 );
        }
        else if ( MARKER_APP1 == segment )
        {
            char app1Header[ 5 ];
            GetBytes( (__int64) offset + 4, app1Header, 4 );
            app1Header[4] = 0;

            if ( g_FullInformation )
                prf( "app1 header: %s\n", app1Header );

            if ( !stricmp( app1Header, "exif" ) )
            {
                // just return the exifoffset so it can be parsed later

                //DumpBinaryData( offset + 4, 0, data_length, 8, 0 );
                exifOffset = offset + 8;
            }
            else if ( !stricmp( app1Header, "http" ) )
            {
                // there will be a null-terminated header string then another string with xmp data

                //DumpBinaryData( offset + 4, 0, data_length, 8, 0 );

                unique_ptr<char> bytes( new char[ data_length + 1 ] );
                GetBytes( (__int64) offset + 4, bytes.get(), data_length );
                bytes.get()[ data_length ] = 0;
                int headerlen = strlen( bytes.get() );

                prf( "adobe header: %s\n", bytes.get() );
                prf( "adobe data:   %s\n", bytes.get() + headerlen + 1 );
                PrintXMPData( bytes.get() + headerlen + 1 );
            }
        }
        else if ( MARKER_DRI == segment )
        {
            WORD restart_interval = GetWORD( offset + 4, false );
            prf( "define restart interval:   %12d\n", restart_interval );
        }
        else if ( MARKER_APP14 == segment )
        {
            DWORD app14_offset = offset + 4 + 5;

            WORD DCTEncodeVersion = GetWORD( app14_offset, false );
            WORD App14Flags0 = GetWORD( app14_offset + 2, false );
            WORD App14Flags1 = GetWORD( app14_offset + 4, false );
            WORD ColorTransform = GetBYTE( app14_offset + 5 );

            prf( "app14 Adobe data:\n" );
            prf( "  DCT encode version:      %12d\n", DCTEncodeVersion );
            prf( "  app14 flags0:            %12d\n", App14Flags0 );
            prf( "  app14 flags1:            %12d\n", App14Flags1 );
            prf( "  color transform:         %12d (%s)\n", ColorTransform, GetJPGApp14ColorTransform( ColorTransform ) );
        }
        else
        {
            prf( "unparsed segment %#x data_length %d\n", segment, data_length );
            DumpBinaryData( offset + 4, 0, data_length, 8, offset + 4 );
        }

        if ( 0 == length )
            break;

        // note: this will not work for many segments, where additional math is required to find lengths.
        // Luckily, the 0xc0 Start Of Frame 0 SOF0 segment generally comes early

        offset += ( length + 2 );
    } while (true);

    return exifOffset;
} //ParseOldJpg

struct FlacLookupEntry
{
    const WCHAR * key;
    const WCHAR * value;
};

FlacLookupEntry FlacFields [] = {
    { L"ALBUM ARTIST=",         L"Album Artist" },
    { L"ALBUM=",                L"Album" },
    { L"ALBUMARTIST=",          L"Album Artist" },
    { L"ARTIST=",               L"Artist" },
    { L"AUDIOHASH=",            L"Audio Hash" },
    { L"BAND=",                 L"Band" },
    { L"BARCODE=",              L"Barcode" },
    { L"CODING_HISTORY=",       L"Coding History" },
    { L"COMMENT=",              L"Comment" },
    { L"COMPOSER=",             L"Composer" },
    { L"COPYRIGHT=",            L"Copyright" },
    { L"CREATION_TIME=",        L"Creation Time" },
    { L"DATE=",                 L"Year" },
    { L"DESCRIPTION=",          L"Description" },
    { L"DISCNUMBER=",           L"Disc Number" },
    { L"DISCTOTAL=",            L"Disc Total" },
    { L"ENCODED_BY=",           L"Encoded By" },
    { L"ENCODER=",              L"Encoder" },
    { L"GENRE=",                L"Genre" },
    { L"ISRC=",                 L"ISRC" },
    { L"ORGANIZATION=",         L"Organization" },
    { L"ORIGINATOR_REFERENCE=", L"Originator Reference" },
    { L"PHC=",                  L"PHC" },
    { L"RELEASE_GUID=",         L"Release Guid" },
    { L"TIME_REFERENCE=",       L"Time Reference" },
    { L"TITLE=",                L"Title" },
    { L"TOOL NAME=",            L"Tool Name" },
    { L"TOOL VERSION=",         L"Tool Version" },
    { L"TOTALDISCS=",           L"Total Discs" },
    { L"TOTALTRACKS=",          L"Total Tracks" },
    { L"TRACKNUMBER=",          L"Track" },
    { L"TRACKTOTAL=",           L"Total Tracks" },
    { L"UMID=",                 L"UMID" },
    { L"UNSYNCEDLYRICS=",       L"Lyrics" },
    { L"UPC=",                  L"UPC" },
    { L"publisher=",            L"Publisher" },
    { L"track=",                L"Track" },
    { L"year=",                 L"Year" },
};

const WCHAR * LookupFlacField( const WCHAR * pwcField )
{
    for ( int i = 0; i < _countof( FlacFields ); i++ )
    {
        if ( !wcsicmp( pwcField, FlacFields[ i ].key ) )
            return FlacFields[i].value;
    }

    return NULL;
} //LookupFlacField

void EnumerateFlac()
{
    prf( "mimetype:                             audio/flac\n" );

    // Data is big-endian!

    ULONG offset = 4;

    do
    {
        ULONG blockHeader = GetDWORD( offset, false );
        offset += 4;

        bool lastBlock = ( 0 != ( 0x80000000 & blockHeader ) );
        BYTE blockType = ( ( blockHeader >> 24 ) & 0x7f );
        ULONG length = blockHeader & 0xffffff;

        if ( g_FullInformation )
            prf( "FLAC header %#x, last block %d, block type %d, length %d, offset %#x\n", blockHeader, lastBlock, blockType, length, offset );

        // protect against malformed files

        if ( 0 == length )
            break;

        if ( 0 == blockType )
        {
            // streaminfo

            WORD minBlockSize = GetWORD( offset, false );
            WORD maxBlockSize = GetWORD( offset + 2, false );

            DWORD minFrameSize = GetDWORD( offset + 4, false );
            minFrameSize >>= 8;
            minFrameSize &= 0x00ffffff;
            DWORD maxFrameSize = GetDWORD( offset + 7, false );
            maxFrameSize >>= 8;
            maxFrameSize &= 0x00ffffff;

            DWORD threeFields = GetDWORD( offset + 10, false );

            DWORD sampleRate = threeFields >> 12;
            BYTE channelCount = 1 + ( threeFields >> 9 ) & 0x7;
            BYTE bitsPerSample = 1 + ( ( threeFields >> 4 ) & 0x1f );

            unsigned long long ullSamples = GetULONGLONG( offset + 13, false );

            ullSamples &= 0x0fffffffffffffff;
            ullSamples >>= 24;

            DWORD lengthInSeconds = (DWORD) ( ullSamples / (unsigned long long) sampleRate );
            DWORD lengthInMinutes = lengthInSeconds / 60;
            DWORD remainderSeconds = lengthInSeconds % 60;

            prf( "Stream Info:\n" );

            prf( "  Minimum block size      %13d\n", minBlockSize );
            prf( "  Maximum block size      %13d\n", maxBlockSize );
            prf( "  Minimum frame size      %13d\n", minFrameSize );
            prf( "  Maximum frame size      %13d\n", maxFrameSize );
            prf( "  Sample rate per sec     %13d\n", sampleRate );
            prf( "  Channel count           %13d\n", channelCount );
            prf( "  Bits per sample         %13d\n", bitsPerSample );
            prf( "  Samples                 %13I64d\n", ullSamples );
            prf( "  Length in min:sec       %10d:%02d\n", lengthInMinutes, remainderSeconds );
            prf( "  MD5 signature                       " );

            DWORD o = offset + 4 + 6 + 8;

            for ( int i = 0; i < ( 16 / 4 ); i++ )
            {
                DWORD x = GetDWORD( o, false );
                o += 4;
                prf( "%08x", x );
            }
            prf( "\n" );
        }
        else if ( 4 == blockType )
        {
            // Vorbis_comment. This data is little-endian, unlike the rest of FLAC files

            prf( "Comments:\n" );

            DWORD o = offset;
            DWORD vendorLength = GetDWORD( o, true );
            o += 4;
            unique_ptr<WCHAR> vendor( GetUTF8( o, vendorLength ) );
            o += vendorLength;

            wprf( L"  Vendor                              %ws\n", vendor.get() );

            DWORD items = GetDWORD( o, true );
            o += 4;

            for ( DWORD i = 0; i < items; i++ )
            {
                DWORD itemLen = GetDWORD( o, true );
                o += 4;

                unique_ptr<WCHAR> item( GetUTF8( o, itemLen ) );
                o += itemLen;

                WCHAR field[ 100 ];
                WCHAR * equal = wcschr( item.get(), L'=' );
                int len = ( 1 + equal - item.get() );
                if ( ( 0 != equal ) && ( len < _countof( field ) ) )
                {
                    CopyMemory( field, item.get(), 2 * len );
                    field[ len ] = 0;

                    const WCHAR * pwcName = LookupFlacField( field );

                    if ( NULL == pwcName )
                        wprf( L"  %ws\n", item.get() );
                    else
                    {
                        const WCHAR * value = equal + 1;

                        if ( !wcscmp( pwcName, L"Lyrics" ) )// && ( !wcschr( value, L'\r' ) || !wcschr( value, L'\n' ) ) )
                            wprf( L"  %-15ws\n%ws\n", pwcName, value );
                        else
                            wprf( L"  %-15ws                     %ws\n", pwcName, value );
                    }
                }
            }
        }
        else if ( 6 == blockType )
        {
            // Picture

            prf( "Picture:\n" );

            DWORD o = offset;

            DWORD pictureType = GetDWORD( o, false );
            prf( "  Picture type            %13d ", pictureType );

            if ( 0 == pictureType ) prf( "Other" );
            else if ( 1 == pictureType ) prf( "32x32 pixels 'file icon' (PNG only)" );
            else if ( 2 == pictureType ) prf( "Other file icon" );
            else if ( 3 == pictureType ) prf( "Cover (front)" );
            else if ( 4 == pictureType ) prf( "Cover (back)" );
            else if ( 5 == pictureType ) prf( "Leaflet page" );
            else if ( 6 == pictureType ) prf( "Media (e.g. label side of CD)" );
            else if ( 7 == pictureType ) prf( "Lead artist/lead performer/soloist" );
            else if ( 8 == pictureType ) prf( "Artist/performer" );
            else if ( 9 == pictureType ) prf( "Conductor" );
            else if ( 10 == pictureType ) prf( "Band/Orchestra" );
            else if ( 11 == pictureType ) prf( "Composer" );
            else if ( 12 == pictureType ) prf( "Lyricist/text writer" );
            else if ( 13 == pictureType ) prf( "Recording Location" );
            else if ( 14 == pictureType ) prf( "During recording" );
            else if ( 15 == pictureType ) prf( "During performance" );
            else if ( 16 == pictureType ) prf( "Movie/video screen capture" );
            else if ( 17 == pictureType ) prf( "A bright coloured fish" );
            else if ( 18 == pictureType ) prf( "Illustration" );
            else if ( 19 == pictureType ) prf( "Band/artist logotype" );
            else if ( 20 == pictureType ) prf( "Publisher/Studio logotype" );
            else prf( "(Unknown pictureType)" );

            prf( "\n" );
            o += 4;

            DWORD mimeTypeBytes = GetDWORD( o, false );
            if ( mimeTypeBytes > 1000 )
            {
                prf( "malformed mime type bytes? %#x\n", mimeTypeBytes );
                return;
            }

            o += 4;

            unique_ptr<char> mimeType( new char [ mimeTypeBytes + 1 ] );
            mimeType.get()[ mimeTypeBytes ] = 0;
            g_pStream->Seek( o );
            g_pStream->Read( mimeType.get(), mimeTypeBytes );
            prf( "  mime type                           %s\n", mimeType.get() );

            o += mimeTypeBytes;

            DWORD descriptionBytes = GetDWORD( o, false );
            if ( descriptionBytes > 1000 )
            {
                prf( "malformed description bytes? %#x\n", descriptionBytes );
                return;
            }

            o += 4;

            unique_ptr<char> description( new char [ descriptionBytes + 1 ] );
            description.get()[ descriptionBytes ] = 0;
            g_pStream->Seek( o );
            g_pStream->Read( description.get(), descriptionBytes );
            prf( "  Description             %s\n", description.get() );

            o += descriptionBytes;

            DWORD width = GetDWORD( o, false );
            prf( "  Width                   %13d\n", width );
            o += 4;

            DWORD height = GetDWORD( o, false );
            prf( "  Height                  %13d\n", height );
            o += 4;

            DWORD bpp = GetDWORD( o, false );
            prf( "  Bits per pixel          %13d\n", bpp );
            o += 4;

            DWORD indexedColors = GetDWORD( o, false );
            prf( "  Indexed colors          %13d\n", indexedColors );
            o += 4;

            DWORD imageLength = GetDWORD( o, false );
            prf( "  Image size              %13d\n", imageLength );
            o += 4;

            if ( ( imageLength > 2 ) && IsPerhapsAnImage( o, 0 ) )
            {
                g_Embedded_Image_Offset = o;
                g_Embedded_Image_Length = imageLength;
            }
        }

        offset += length;

        if ( lastBlock )
            break;
    } while ( true );
} //EnumerateFlac

class HeifStream
{
    private:
        __int64 offset;
        __int64 length;
        CStream * pStream;

    public:
        HeifStream( CStream *ps, __int64 o, __int64 l )
        {
            offset = o;
            length = l;
            pStream = ps;
        }

        __int64 Length() { return length; }
        __int64 Offset() { return offset; }
        CStream * Stream() { return pStream; }

        DWORD GetDWORD( __int64 & streamOffset, bool littleEndian )
        {
            DWORD dw = 0;

            if ( pStream->Seek( offset + streamOffset ) )
            {
                pStream->Read( &dw, sizeof dw );

                if ( !littleEndian )
                    dw = _byteswap_ulong( dw );

                streamOffset += sizeof dw;
            }

            return dw;
        } //GetDWORD

        WORD GetWORD( __int64 & streamOffset, bool littleEndian )
        {
            WORD w = 0;

            if ( pStream->Seek( offset + streamOffset ) )
            {
                pStream->Read( &w, sizeof w );
        
                if ( !littleEndian )
                    w = _byteswap_ushort( w );
   
                streamOffset += sizeof w;
            }
        
            return w;
        } //GetWORD

        byte GetBYTE( __int64 & streamOffset )
        {
            byte b = 0;

            if ( pStream->Seek( offset + streamOffset ) )
            {
                pStream->Read( &b, sizeof b );

                streamOffset += sizeof b;
            }

            return b;
        } //GetBYTE

        void GetBytes( __int64 & streamOffset, void * pData, int byteCount )
        {
            memset( pData, 0, byteCount );

            if ( pStream->Seek( offset + streamOffset ) )
                pStream->Read( pData, byteCount );
        }
}; //HeifStream

// Heif and CR3 use ISO Base Media File Format ISO/IEC 14496-12. This function walks those files and pulls out data including Exif offsets

void EnumerateBoxes( HeifStream & hs, DWORD depth )
{
    __int64 offset = 0;
    DWORD box = 0;
    int indent = depth * 2;

    do
    {
        if ( g_FullInformation )
            prf( "%*soffset %I64d, length %I64d\n", indent, "", offset, hs.Length() );

        if ( ( 0 == hs.Length() ) || ( offset >= hs.Length() ) )
            break;

        __int64 boxOffset = offset;
        ULONGLONG boxLen = hs.GetDWORD( offset, false );

        if ( 0 == boxLen )
            break;

        DWORD dwTag = hs.GetDWORD( offset, false );
        char tag[ 5 ];
        tag[ 3 ] = dwTag & 0xff;
        tag[ 2 ] = ( dwTag >> 8 ) & 0xff;
        tag[ 1 ] = ( dwTag >> 16 ) & 0xff;
        tag[ 0 ] = ( dwTag >> 24 ) & 0xff;
        tag[ 4 ] = 0;

        if ( 1 == boxLen )
        {
            ULONGLONG high = hs.GetDWORD( offset, false );
            ULONGLONG low = hs.GetDWORD( offset, false );

            boxLen = ( high << 32 ) | low;
        }

        if ( boxLen > hs.Length() )
        {
            prf( "box length %lld is greater than the substream length %lld\n", boxLen, hs.Length() );
            break;
        }

        if ( g_FullInformation )
        {
            prf( "%*sbox %d::%d has length %I64d and tag %s\n", indent, "", depth, box, boxLen, tag );

            ULONGLONG toshow = __min( boxLen, 0x100 );

            unique_ptr<byte> bytes( new byte[ toshow ] );
            hs.GetBytes( boxOffset, bytes.get(), toshow );
            DumpBinaryData( bytes.get(), toshow, 8 );
        }

        if ( 0 == boxLen )
            break;

        if ( !strcmp( tag, "ftyp" ) )
        {
            char brand[ 5 ];
            for ( int i = 0; i < 4; i++ )
                brand[ i ] = hs.GetBYTE( offset );
            brand[ 4 ] = 0;

            DWORD version = hs.GetDWORD( offset, false );

            //printf( "%*s major brand: %s, minor version: %#x\n", indent, "", brand, version );
        }
        else if ( !strcmp( tag, "meta" ) )
        {
            DWORD data = hs.GetDWORD( offset, true );

            //printf( "%*s data (version and flags) %#x\n", indent, "", data );

            HeifStream hsChild( hs.Stream(), hs.Offset() + offset, boxLen - ( offset - boxOffset ) );

            EnumerateBoxes( hsChild, depth + 1 );
        }
        else if ( !strcmp( tag, "iinf" ) )
        {
            DWORD data = hs.GetDWORD( offset, false );
            BYTE version = (BYTE) ( ( data >> 24 ) & 0xff );
            DWORD flags = data & 0x00ffffff;

            DWORD entriesSize = ( version > 0 ) ? 4 : 2;
            DWORD entries = 0;

            if ( 2 == entriesSize )
                entries = hs.GetWORD( offset, false );
            else
                entries = hs.GetDWORD( offset, false );

            //printf( "%*s data (version and flags) %#x, version %d, flags %#x, entries %d\n", indent, "", data, version, flags, entries );

            if ( entries > 0 )
            {
                HeifStream hsChild( hs.Stream(), hs.Offset() + offset, boxLen - ( offset - boxOffset ) );

                EnumerateBoxes( hsChild, depth + 1 );
            }
        }
        else if ( !strcmp( tag, "infe" ) )
        {
            DWORD data = hs.GetDWORD( offset, false );
            BYTE version = (BYTE) ( ( data >> 24 ) & 0xff );
            DWORD flags = data & 0x00ffffff;

            //printf( "%*s data (version and flags) %#x, version %d, flags %#x\n", indent, "", data, version, flags );

            if ( version <= 1 )
            {
                WORD itemID = hs.GetWORD( offset, false );
                WORD itemProtection = hs.GetWORD( offset, false );
            }
            else if ( version >= 2 )
            {
                bool hiddenItem = ( 0 != (flags & 0x1 ) );

                ULONG itemID = 0;

                if ( 2 == version )
                    itemID = hs.GetWORD( offset, false );
                else
                    itemID = hs.GetDWORD( offset, false );

                WORD protectionIndex = hs.GetWORD( offset, false );

                char itemType[ 5 ];
                for ( int i = 0; i < 4; i++ )
                    itemType[ i ] = hs.GetBYTE( offset );
                itemType[ 4 ] = 0;

                //printf( "%*s hidden %s, itemID %d, protectionIndex %d, itemType %s\n",
                //        indent, "", hiddenItem ? "true" : "false", itemID, protectionIndex, itemType );

                if ( !strcmp( itemType, "Exif" ) )
                    g_Heif_Exif_ItemID = itemID;
            }
        }
        else if ( !strcmp( tag, "iloc" ) )
        {
            DWORD data = hs.GetDWORD( offset, false );
            BYTE version = (BYTE) ( ( data >> 24 ) & 0xff );
            DWORD flags = data & 0x00ffffff;

            //printf( "%*s data (version and flags) %#x, version %d, flags %#x\n", indent, "", data, version, flags );

            WORD values4 = hs.GetWORD( offset, false );
            int offsetSize = ( values4 >> 12 ) & 0xf;
            int lengthSize = ( values4 >> 8 ) & 0xf;
            int baseOffsetSize = ( values4 >> 4 ) & 0xf;
            int indexSize = 0;

            if ( version > 1 )
                indexSize = ( values4 & 0xf );

            int itemCount = 0;

            if ( version < 2 )
                itemCount = hs.GetWORD( offset, false );
            else
                itemCount = hs.GetDWORD( offset, false );

            //printf( "%*s iloc offsetSize %d, lengthSize %d, baseOffsetSize %d, indexSize %d, itemCount %d\n",
            //        indent, "", offsetSize, lengthSize, baseOffsetSize, indexSize, itemCount );

            for ( int i = 0; i < itemCount; i++ )
            {
                int itemID = 0;

                if ( version < 2 )
                    itemID = hs.GetWORD( offset, false );
                else
                    itemID = hs.GetWORD( offset, false );

                BYTE constructionMethod = 0;

                if ( version >= 1 )
                {
                    values4 = hs.GetWORD( offset, false );

                    constructionMethod = ( values4 & 0xf );
                }

                WORD dataReferenceIndex = hs.GetWORD( offset, false );

                __int64 baseOffset = 0;

                if ( 4 == baseOffsetSize )
                {
                    baseOffset = hs.GetDWORD( offset, false );
                }
                else if ( 8 == baseOffsetSize )
                {
                    __int64 high = hs.GetDWORD( offset, false );
                    __int64 low = hs.GetDWORD( offset, false );

                    baseOffset = ( high << 32 ) | low;
                }

                WORD extentCount = hs.GetWORD( offset, false );

                //printf( "%*s   item %d, itemID %d, constructionMethod %d, dataReferenceIndex %d, baseOffset %I64d, extentCount %d\n",
                //        indent, "", i, itemID, constructionMethod, dataReferenceIndex, baseOffset, extentCount );

                for ( int e = 0; e < extentCount; e++ )
                {
                    __int64 extentIndex = 0;
                    __int64 extentOffset = 0;
                    __int64 extentLength = 0;

                    if ( version > 1 && indexSize > 0 )
                    {
                        if ( 4 == indexSize )
                        {
                            extentIndex = hs.GetDWORD( offset, false );
                        }
                        else if ( 8 == indexSize )
                        {
                            __int64 high = hs.GetDWORD( offset, false );
                            __int64 low = hs.GetDWORD( offset, false );

                            extentIndex = ( high << 32 ) | low;
                        }
                    }

                    if ( 4 == offsetSize )
                    {
                        extentOffset = hs.GetDWORD( offset, false );
                    }
                    else if ( 8 == offsetSize )
                    {
                        __int64 high = hs.GetDWORD( offset, false );
                        __int64 low = hs.GetDWORD( offset, false );

                        extentOffset = ( high << 32 ) | low;
                    }

                    if ( 4 == lengthSize )
                    {
                        extentLength = hs.GetDWORD( offset, false );
                    }
                    else if ( 8 == lengthSize )
                    {
                        __int64 high = hs.GetDWORD( offset, false );
                        __int64 low = hs.GetDWORD( offset, false );

                        extentLength = ( high << 32 ) | low;
                    }

                    //printf( "%*s     extent %d, extentIndex %I64d, extentOffset %I64d, extentLength %I64d\n",
                    //        indent, "", e, extentIndex, extentOffset, extentLength );

                    if ( itemID == g_Heif_Exif_ItemID )
                    {
                        g_Heif_Exif_Offset = extentOffset;
                        g_Heif_Exif_Length = extentLength;
                    }
                }
            }
        }
        else if ( !strcmp( tag, "hvcC" ) )
        {
            BYTE version = hs.GetBYTE( offset );
            BYTE byteInfo = hs.GetBYTE( offset );
            BYTE profileSpace = ( byteInfo >> 6 ) & 3;
            bool tierFlag = 0 != ( ( byteInfo >> 4 ) & 0x1 );
            BYTE  profileIDC = byteInfo & 0x1f;

            DWORD profileCompatibilityFlags = hs.GetDWORD( offset, false );

            for ( int i = 0; i < 6; i++ )
                BYTE b = hs.GetBYTE( offset );

            BYTE generalLevelIDC = hs.GetBYTE( offset );
            WORD minSpacialSegmentationIDC = hs.GetWORD( offset, false ) & 0xfff;
            BYTE parallelismType = hs.GetBYTE( offset ) & 0x3;
            BYTE chromaFormat = hs.GetBYTE( offset ) & 0x3;
            BYTE bitDepthLuma = ( hs.GetBYTE( offset ) & 0x7 ) + 8;
            BYTE bitDepthChroma = ( hs.GetBYTE( offset ) & 0x7 ) + 8;
            WORD avgFrameRate = hs.GetWORD( offset, false );

            prf( "Heif hvvC metadata (non-Exif):\n" );
            prf( "  Luma Bit Depth:      %d\n", bitDepthLuma );
            prf( "  Chroma Bit Depth:    %d\n", bitDepthChroma );
        }
        else if ( !strcmp( tag, "iprp" ) )
        {
            HeifStream hsChild( hs.Stream(), hs.Offset() + offset, boxLen - ( offset - boxOffset ) );

            EnumerateBoxes( hsChild, depth + 1 );
        }
        else if ( !strcmp( tag, "ipco" ) )
        {
            HeifStream hsChild( hs.Stream(), hs.Offset() + offset, boxLen - ( offset - boxOffset ) );

            EnumerateBoxes( hsChild, depth + 1 );
        }
        else if ( !strcmp( tag, "moov" ) ) // Canon CR3 format
        {
            HeifStream hsChild( hs.Stream(), hs.Offset() + offset, boxLen - ( offset - boxOffset ) );

            EnumerateBoxes( hsChild, depth + 1 );
        }
        else if ( !strcmp( tag, "uuid" ) )
        {
            char acGUID[ 33 ];
            acGUID[ 32 ] = 0;

            for ( int i = 0; i < 16; i++ )
            {
                BYTE x = hs.GetBYTE( offset );
                sprintf( acGUID + i * 2, "%02x", x );
            }

            if ( g_FullInformation )
                prf( "%*s  tag uuid: {%s}\n", indent, "", acGUID );

            if ( !strcmp( "85c0b687820f11e08111f4ce462b6a48", acGUID ) )
            {
                // Canon CR3, will contain CNCV, CCTP, CTBO, CMT1, CMD2, CMD3, CMT4, etc.

                HeifStream hsChild( hs.Stream(), hs.Offset() + offset, boxLen - ( offset - boxOffset ) );

                EnumerateBoxes( hsChild, depth + 1 );
            }

            if ( !strcmp( "eaf42b5e1c984b88b9fbb7dc406e4d16", acGUID ) )
            {
                // preview data -- a reduced-resolution jpg

                HeifStream hsChild( hs.Stream(), hs.Offset() + offset, boxLen - ( offset - boxOffset ) );

                EnumerateBoxes( hsChild, depth + 1 );
            }

            if ( !strcmp( "be7acfcb97a942e89c71999491e3afac", acGUID ) )
            {
                // prf( "found cr3 xmp data box\n" );

                ULONGLONG xmpLen = boxLen - ( offset - boxOffset );
                //printf( "xmpLen: %lld\n", xmpLen );

                unique_ptr<char> bytes( new char[ xmpLen + 1 ] );
                hs.GetBytes( offset, bytes.get(), xmpLen );
                bytes.get()[ xmpLen ] = 0; // ensure it'll be null-terminated
                //DumpBinaryData( bytes.get(), xmpLen, 8 );

                PrintXMPData( bytes.get() );
            }
        }
        else if ( !strcmp( tag, "CMT1" ) )
        {
            //printf( "Exif IFDO at file offset %I64d\n", hs.Offset() + offset );
            g_Canon_CR3_Exif_IFD0 = hs.Offset() + offset;
        }
        else if ( !strcmp( tag, "CMT2" ) )
        {
            //printf( "Exif Exif IFD at file offset %I64d\n", hs.Offset() + offset );
            g_Canon_CR3_Exif_Exif_IFD = hs.Offset() + offset;
        }
        else if ( !strcmp( tag, "CMT3" ) )
        {
            //printf( "Exif Canon Makernotes at file offset %I64d\n", hs.Offset() + offset );
            g_Canon_CR3_Exif_Makernotes_IFD = hs.Offset() + offset;
        }
        else if ( !strcmp( tag, "CMT4" ) )
        {
            //printf( "Exif GPS IFD at file offset %I64d\n", hs.Offset() + offset );
            g_Canon_CR3_Exif_GPS_IFD = hs.Offset() + offset;
        }
        else if ( !strcmp( tag, "PRVW" ) )
        {
            prf( "PREVIEWPREVIEWPREVIEW!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n" );
            DWORD unk = hs.GetDWORD( offset, false );
            WORD unkW = hs.GetWORD( offset, false );
            WORD width = hs.GetWORD( offset, false );
            WORD height = hs.GetWORD( offset, false );
            unkW = hs.GetWORD( offset, false );
            DWORD length = hs.GetDWORD( offset, false );

            // This should work per https://github.com/exiftool/canon_cr3, but it doesn't exist

            g_Embedded_Image_Length = length;
            g_Embedded_Image_Offset = offset + hs.Offset();
        }
        else if ( !strcmp( tag, "mdat" ) ) // Canon .CR3 main data
        {
            DWORD jpgOffset = hs.Offset() + offset;
            DWORD head = hs.GetDWORD( offset, false );

            //printf( "mdat header: %#x\n", head );

            if ( ( 0xffd8ffdb == head ) && // looks like JPG
                 ( 0 != g_Canon_CR3_Embedded_JPG_Length ) )
            {
                g_Embedded_Image_Length = g_Canon_CR3_Embedded_JPG_Length;
                g_Embedded_Image_Offset = jpgOffset;
            }
        }
        else if ( !strcmp( tag, "trak" ) ) // Canon .CR3 metadata
        {
            HeifStream hsChild( hs.Stream(), hs.Offset() + offset, boxLen - ( offset - boxOffset ) );
            EnumerateBoxes( hsChild, depth + 1 );
        }
        else if ( !strcmp( tag, "mdia" ) ) // Canon .CR3 metadata
        {
            HeifStream hsChild( hs.Stream(), hs.Offset() + offset, boxLen - ( offset - boxOffset ) );
            EnumerateBoxes( hsChild, depth + 1 );
        }
        else if ( !strcmp( tag, "minf" ) ) // Canon .CR3 metadata
        {
            HeifStream hsChild( hs.Stream(), hs.Offset() + offset, boxLen - ( offset - boxOffset ) );
            EnumerateBoxes( hsChild, depth + 1 );
        }
        else if ( !strcmp( tag, "stbl" ) ) // Canon .CR3 metadata
        {
            HeifStream hsChild( hs.Stream(), hs.Offset() + offset, boxLen - ( offset - boxOffset ) );
            EnumerateBoxes( hsChild, depth + 1 );
        }
        else if ( !strcmp( tag, "stsz" ) ) // Canon .CR3 metadata
        {
            // There is no documentation, but it appears that the 1st of 4 instances of
            // the stsz tag has the full-resolution embedded JPG length at i==3 in this
            // loop. I mean, it worked for one file.

            for ( int i = 0; i < 5; i++ )
            {
                DWORD len = hs.GetDWORD( offset, false );
                //printf( "stsz tag; %d DWORD: %d\n", i, len );

                if ( ( 3 == i ) && ( 0 == g_Canon_CR3_Embedded_JPG_Length ) )
                    g_Canon_CR3_Embedded_JPG_Length = len;
            }
        }

        box++;
        offset = boxOffset + boxLen;
    } while ( true );
} //EnumerateBoxes

void EnumerateHeif( CStream * pStream )
{
    __int64 length = pStream->Length();
    HeifStream hs( pStream, 0, length );

    EnumerateBoxes( hs, 0 );

    if ( g_FullInformation )
        prf( "Heif EXIF info: id %d, offset %I64d, length %I64d\n", g_Heif_Exif_ItemID, g_Heif_Exif_Offset, g_Heif_Exif_Length );
} //EnumerateHeif

const char * GetOrientation( int o )
{
    if ( 1 == o )
        return "horizontal (normal)";

    if ( 2 == o )
        return "mirror horizontal";

    if ( 3 == o )
        return "rotate 180";

    if ( 4 == o )
        "mirror vertical";

    if ( 5 == o )
        return "mirror horizontal and rotate 270 cw";

    if ( 6 == o )
        return "rotate 90 cw";

    if ( 7 == o )
        return "mirror horizontal and rotate 90 cw";

    if ( 8 == o )
        return "rotate 270 cw";

    return "unknown";
} //GetOrientation

const char * GetProfileEmbedPolicy( int x )
{
    if ( 0 == x )
        return "allow copying";

    if ( 1 == x )
        return "embed if used";

    if ( 2 == x )
        return "never embed";

    if ( 3 == x )
        return "no restrictions";

    return "unknown";
} //GetProfileEmbedPolicy

void EnumerateIFD0( int depth, __int64 IFDOffset, __int64 headerBase, bool littleEndian, WCHAR * pwcExt )
{
    char acBuffer[ 100 ];
    int currentIFD = 0;
    __int64 provisionalJPGOffset = 0;
    __int64 provisionalEmbeddedJPGOffset = 0;
    bool likelyRAW = false;
    int lastBitsPerSample = 0;
    vector<IFDHeader> aHeaders( MaxIFDHeaders );

    while ( 0 != IFDOffset ) 
    {
        provisionalJPGOffset = 0;
        provisionalEmbeddedJPGOffset = 0;

        WORD NumTags = GetWORD( IFDOffset + headerBase, littleEndian );
        IFDOffset += 2;
        //printf( "IFDOffset %lld, NumTags %d, headerBase %lld\n", IFDOffset, NumTags, headerBase );
    
        if ( NumTags > MaxIFDHeaders )
        {
            Space( depth );
            prf( "numtags is > 200; it's likely not in EXIF format, so the data may be noise. skipping\n" );
            break;
        }
    
        if ( !GetIFDHeaders( IFDOffset + headerBase, aHeaders.data(), NumTags, littleEndian ) )
        {
            Space( depth );
            prf( "IFD0 has a tag that's invalid. skipping\n" );
            break;
        }

        if ( g_FullInformation )
        {
            Space( depth );
            prf( "IFD0 directory with %d tags\n", NumTags );
        }

        for ( int i = 0; i < NumTags; i++ )
        {
            IFDHeader & head = aHeaders[ i ];
            IFDOffset += sizeof IFDHeader;

            if ( g_FullInformation )
            {
                Space( depth );
                prf( "IFD%d tag %d ID %d==%#x, type %d, count %d, offset/value %d==%#x, o %lld\n", currentIFD, i, head.id, head.id, head.type, head.count, head.offset, head.offset, IFDOffset + headerBase );
            }

            Space( depth );

            if ( ( !wcsicmp( pwcExt, L".rw2" ) ) && ( ( head.id < 254 ) || ( head.id >= 280 && head.id <= 290 ) ) )
            {
                PrintPanasonicIFD0Tag( depth, head.id, head.type, head.count, head.offset,  headerBase, littleEndian, IFDOffset );
                continue;
            }

            bool tagHandled = true;

            if ( 11 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "ProcessingSoftware:                   %s\n", acBuffer );
            }
            else if ( 254 == head.id && IsIntType( head.type ) )
            {
                likelyRAW = ( 0 == ( 1 & head.offset ) );
                prf( "NewSubfileType:                       %#x (%s)\n", head.offset, likelyRAW ? "main RAW image" : "reduced resolution copy" );
            }
            else if ( 255 == head.id && IsIntType( head.type ) )
                prf( "SubfileType:                          %d\n", head.offset );
            else if ( 256 == head.id && IsIntType( head.type ) )
                prf( "ImageWidth:                           %d\n", head.offset );
            else if ( 257 == head.id && IsIntType( head.type ) )
                prf( "ImageHeight:                          %d\n", head.offset );
            else if ( 258 == head.id && 3 == head.type && 2 == head.count )
            {
                lastBitsPerSample = GetWORD( head.offset + headerBase, littleEndian );
                prf( "BitsPerSample:                        %d, %d\n",
                        GetWORD( head.offset + headerBase, littleEndian ),
                        GetWORD( head.offset + headerBase + 2, littleEndian ) );
            }
            else if ( 258 == head.id && 3 == head.type && 3 == head.count )
            {
                lastBitsPerSample = GetWORD( head.offset + headerBase, littleEndian );
                prf( "BitsPerSample:                        %d, %d, %d\n",
                        GetWORD( head.offset + headerBase, littleEndian ),
                        GetWORD( head.offset + headerBase + 2, littleEndian ),
                        GetWORD( head.offset + headerBase + 4, littleEndian ) );
            }
            else if ( 258 == head.id && 3 == head.type && 4 == head.count )
            {
                lastBitsPerSample = GetWORD( head.offset + headerBase, littleEndian );
                prf( "BitsPerSample:                        %d, %d, %d %d\n",
                        GetWORD( head.offset + headerBase, littleEndian ),
                        GetWORD( head.offset + headerBase + 2, littleEndian ),
                        GetWORD( head.offset + headerBase + 4, littleEndian ),
                        GetWORD( head.offset + headerBase + 6, littleEndian ) );
            }
            else if ( 258 == head.id && 3 == head.type && 1 == head.count )
                prf( "BitsPerSample:                        %d\n", head.offset );
            else if ( 259 == head.id && IsIntType( head.type ) )
                prf( "Compression:                          %d (%s)\n", head.offset, CompressionType( head.offset ) );
            else if ( 262 == head.id && IsIntType( head.type ) )
                prf( "PhotometricIntperpretation:           %d (%s)\n", head.offset, PhotometricInterpretationString( head.offset ) );
            else if ( 266 == head.id && 3 == head.type )
                prf( "FillOrder:                            %d (%s)\n", head.offset, ExifFillOrder( head.offset ) );
            else if ( 269 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "DocumentName:                         %s\n", acBuffer );
            }
            else if ( 270 == head.id  && 2 == head.type )
            {
                char acImageDescription[ 100 ];
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acImageDescription, _countof( acImageDescription ), head.count );
                prf( "ImageDescription:                     %s\n", acImageDescription );
            }
            else if ( 271 == head.id  && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, g_acMake, _countof( g_acMake ), head.count );

                prf( "Make:                                 %s\n", g_acMake );
            }
            else if ( 272 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, g_acModel, _countof( g_acModel ), head.count );
                prf( "Model:                                %s\n", g_acModel );

                WCHAR awcModel[ 100 ];
                mbstowcs( awcModel, g_acModel, _countof( awcModel ) );

                // Garbage character at the end of this string for this camera

                if ( wcsstr( awcModel, L"HTC Touch Diamond P3700" ) )
                    awcModel[ 22 ] = 0;
            }
            else if ( 273 == head.id && IsIntType( head.type ) )
            {
                prf( "StripOffsets:                         %d\n", head.offset );

                if ( 0 != head.offset && 0xffffffff != head.offset && !likelyRAW && IsPerhapsAnImage( head.offset, headerBase ) )
                    provisionalJPGOffset = head.offset + headerBase;
            }
            else if ( 274 == head.id && IsIntType( head.type ) )
                prf( "Orientation:                          %d (%s)\n", head.offset, GetOrientation( head.offset ) );
            else if ( 277 == head.id && IsIntType( head.type ) )
                prf( "SamplesPerPixel:                      %d\n", head.offset );
            else if ( 278 == head.id && IsIntType( head.type ) )
                prf( "RowsPerStrip:                         %d\n", head.offset );
            else if ( 279 == head.id && IsIntType( head.type ) )
            {
                prf( "StripByteCounts:                      %d\n", head.offset );
                //printf( "!!!!!! lastBitsPerSample %d, length %d, likelyRAW %d provisionalOffset %lld\n", lastBitsPerSample, head.offset, likelyRAW, provisionalJPGOffset );

                if ( ( lastBitsPerSample != 16 ) && 0 != provisionalJPGOffset && 0 != head.offset && 0xffffffff != head.offset && !likelyRAW && ( head.offset > g_Embedded_Image_Length ) )
                {
                    //printf( "overwriting length %I64d with %d\n", g_Embedded_Image_Length, head.offset );
                    g_Embedded_Image_Length = head.offset;
                    g_Embedded_Image_Offset = provisionalJPGOffset;
                }
            }
            else if ( 280 == head.id && 3 == head.type )
            {
                if ( 1 == head.count )
                    prf( "Min Sample Value:                     %d\n", head.offset );
                else if ( 3 == head.count )
                {
                    WORD w1 = GetWORD( head.offset + headerBase, littleEndian );
                    WORD w2 = GetWORD( head.offset + headerBase + 2, littleEndian );
                    WORD w3 = GetWORD( head.offset + headerBase + 4, littleEndian );
                    prf( "Min Sample Values:                    %d, %d, %d\n", w1, w2, w3 );
                }
            }
            else if ( 281 == head.id && 3 == head.type )
            {
                if ( 1 == head.count )
                    prf( "Max Sample Value:                     %d\n", head.offset );
                else if ( 3 == head.count )
                {
                    WORD w1 = GetWORD( head.offset + headerBase, littleEndian );
                    WORD w2 = GetWORD( head.offset + headerBase + 2, littleEndian );
                    WORD w3 = GetWORD( head.offset + headerBase + 4, littleEndian );
                    prf( "Max Sample Values:                    %d, %d, %d\n", w1, w2, w3 );
                }
            }
            else if ( 282 == head.id && 3 == head.type )
                prf( "XResolution:                          %d\n", head.offset );
            else if ( 282 == head.id && ( ( 5 == head.type ) || ( 10 == head.type ) ) )
            {
                DWORD num = GetDWORD( head.offset + headerBase, littleEndian );
                DWORD den = GetDWORD( head.offset + headerBase + 4, littleEndian );
                prf( "XResolution:                          %d / %d = %lf\n", num, den, (double) num / (double) den );
            }
            else if ( 283 == head.id && ( ( 5 == head.type ) || ( 10 == head.type ) ) )
            {
                DWORD num = GetDWORD( head.offset + headerBase, littleEndian );
                DWORD den = GetDWORD( head.offset + headerBase + 4, littleEndian );
                prf( "YResolution:                          %d / %d = %lf\n", num, den, (double) num / (double) den );
            }
            else if ( 284 == head.id && IsIntType( head.type ) )
                prf( "PlanarConfiguration:                  %d\n", head.offset );
            else if ( 285 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "PageName:                             %s\n", acBuffer );
            }
            else if ( 296 == head.id && IsIntType( head.type ) )
                prf( "ResolutionUnit:                       %s (%d)\n", ResolutionUnit( head.offset ), head.offset );
            else if ( 297 == head.id && 3 == head.type && 2 == head.count )
            {
                 WORD low = head.offset & 0xffff;
                 WORD high = ( head.offset >> 16 ) & 0xffff;

                 prf( "PageNumber:                          %d, %d\n", low, high );
            }
            else if ( 305 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "Software:                             %s\n", acBuffer );
            }
            else if ( 306 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "DateTime:                             %s\n", acBuffer );
            }
            else if ( 315 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "Artist:                               %s\n", acBuffer );
            }
            else if ( 316 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "HostComputer:                         %s\n", acBuffer );
            }
            else if ( 317 == head.id && 3 == head.type && 1 == head.count )
                prf( "Predictor:                            %s\n", ExifPredictor( head.offset ) );
            else if ( 318 == head.id && 5 == head.type && 2 == head.count )
            {
                LONG num1 = GetDWORD( head.offset +      headerBase, littleEndian );
                LONG den1 = GetDWORD( head.offset +  4 + headerBase, littleEndian );
                double d1 = (double) num1 / (double) den1;

                LONG num2 = GetDWORD( head.offset +  8 + headerBase, littleEndian );
                LONG den2 = GetDWORD( head.offset + 12 + headerBase, littleEndian );
                double d2 = (double) num2 / (double) den2;

                prf( "WhitePoint:                           %lf, %lf\n", d1, d2 );
            }
            else if ( 319 == head.id && 5 == head.type && 6 == head.count )
            {
                LONG num1 = GetDWORD( head.offset +      headerBase, littleEndian );
                LONG den1 = GetDWORD( head.offset +  4 + headerBase, littleEndian );
                double d1 = (double) num1 / (double) den1;

                LONG num2 = GetDWORD( head.offset +  8 + headerBase, littleEndian );
                LONG den2 = GetDWORD( head.offset + 12 + headerBase, littleEndian );
                double d2 = (double) num2 / (double) den2;

                LONG num3 = GetDWORD( head.offset + 16 + headerBase, littleEndian );
                LONG den3 = GetDWORD( head.offset + 20 + headerBase, littleEndian );
                double d3 = (double) num3 / (double) den3;

                LONG num4 = GetDWORD( head.offset + 24 + headerBase, littleEndian );
                LONG den4 = GetDWORD( head.offset + 28 + headerBase, littleEndian );
                double d4 = (double) num4 / (double) den4;

                LONG num5 = GetDWORD( head.offset + 32 + headerBase, littleEndian );
                LONG den5 = GetDWORD( head.offset + 36 + headerBase, littleEndian );
                double d5 = (double) num5 / (double) den5;

                LONG num6 = GetDWORD( head.offset + 40 + headerBase, littleEndian );
                LONG den6 = GetDWORD( head.offset + 44 + headerBase, littleEndian );
                double d6 = (double) num6 / (double) den6;

                prf( "PrimaryChromaticities:                %lf, %lf, %lf, %lf, %lf, %lf\n", d1, d2, d3, d4, d5, d6 );
            }
            else if ( 320 == head.id && 3 == head.type )
                prf( "Colormap                              %d entries\n", head.count );
            else if ( 322 == head.id && 4 == head.type )
                prf( "TileWidth                             %d\n", head.offset );
            else if ( 323 == head.id && 4 == head.type )
                prf( "TileLength                            %d\n", head.offset );
            else if ( 330 == head.id && 4 == head.type )
            {
                prf( "Child IFDs (%d of them) at offset %d\n", head.count, head.offset );
                // prf( "  head.type %d, head.count %d\n", head.type, head.count );

                if ( 1 == head.count )
                    EnumerateGenericIFD( depth + 1,  head.offset, headerBase, littleEndian );
                else
                {
                    for ( int i = 0; i < head.count; i++ )
                    {
                        DWORD oIFD = GetDWORD( ( i * 4 ) + head.offset + headerBase, littleEndian );
                        EnumerateGenericIFD( depth + 1,  oIFD, headerBase, littleEndian );
                    }
                }
            }
            else if ( 338 == head.id && 3 == head.type && 1 == head.count )
                prf( "ExtraSamples:                         %s (%d)\n", ExifExtraSamples( head.offset ), head.offset );
            else if ( 339 == head.id && 3 == head.type && 3 == head.count )
            {
                WORD val0 = GetWORD( 0 + head.offset + headerBase, littleEndian );
                WORD val1 = GetWORD( 2 + head.offset + headerBase, littleEndian );
                WORD val2 = GetWORD( 4 + head.offset + headerBase, littleEndian );
                prf( "SampleFormat:                         %s (%d), %s (%d), %s (%d)\n",
                        ExifSampleFormat( val0 ), val0,
                        ExifSampleFormat( val1 ), val1,
                        ExifSampleFormat( val2 ), val2 );
            }
            else if ( 339 == head.id && 3 == head.type && 4 == head.count )
            {
                WORD val0 = GetWORD( 0 + head.offset + headerBase, littleEndian );
                WORD val1 = GetWORD( 2 + head.offset + headerBase, littleEndian );
                WORD val2 = GetWORD( 4 + head.offset + headerBase, littleEndian );
                WORD val3 = GetWORD( 6 + head.offset + headerBase, littleEndian );
                prf( "SampleFormat:                         %s (%d), %s (%d), %s (%d), %s (%d)\n",
                        ExifSampleFormat( val0 ), val0,
                        ExifSampleFormat( val1 ), val1,
                        ExifSampleFormat( val2 ), val2,
                        ExifSampleFormat( val3 ), val3 );
            }
            else if ( 347 == head.id && 7 == head.type )
            {
                prf( "JPEGTables:                           %d bytes\n", head.count );
                DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
            }
            else if ( 513 == head.id && IsIntType( head.type ) )
            {
                prf( "JPEGInterchangeFormat:                %d\n", head.offset );

                if ( 0 != head.offset && 0xffffffff != head.offset && IsPerhapsAnImage( head.offset, headerBase ) && !likelyRAW )
                    provisionalEmbeddedJPGOffset = head.offset + headerBase;

            }
            else if ( 514 == head.id && IsIntType( head.type ) )
            {
                prf( "JPEGInterchangeFormatLength:          %d\n", head.offset );

                //printf( "non-strip offset %lld, length %d\n", provisionalEmbeddedJPGOffset, head.offset );

                if ( 0 != head.offset && 0xffffffff != head.offset && 0 != provisionalEmbeddedJPGOffset && ( head.offset > g_Embedded_Image_Length ) && !likelyRAW )
                {
                    //printf( "AAAA EnumerateIFD0 overwriting length %I64d with %d\n", g_Embedded_Image_Length, head.offset );
                    g_Embedded_Image_Length = head.offset;
                    g_Embedded_Image_Offset = provisionalEmbeddedJPGOffset;
                }
            }
            else if ( 530 == head.id && 3 == head.type && 2 == head.count )
            {
                WORD low = head.offset & 0xffff;
                WORD high = ( head.offset >> 16 ) & 0xffff;

                prf( "YCbCrSubSampling:                     %s\n", YCbCrSubSampling( low, high ) );
            }
            else if ( 530 == head.id && 3 == head.type && 1 == head.count )
                prf( "YCbCrSubSampling (malformed):         %d\n", head.offset );
            else if ( 531 == head.id && IsIntType( head.type ) )
                prf( "YCbCrPositioning:                     %d\n", head.offset );
            else if ( 532 == head.id && 5 == head.type && 6 == head.count )
            {
                DWORD num1 = GetDWORD( head.offset +      headerBase, littleEndian );
                DWORD den1 = GetDWORD( head.offset +  4 + headerBase, littleEndian );
                double d1 = (double) num1 / (double) den1;

                DWORD num2 = GetDWORD( head.offset +  8 + headerBase, littleEndian );
                DWORD den2 = GetDWORD( head.offset + 12 + headerBase, littleEndian );
                double d2 = (double) num2 / (double) den2;

                DWORD num3 = GetDWORD( head.offset + 16 + headerBase, littleEndian );
                DWORD den3 = GetDWORD( head.offset + 20 + headerBase, littleEndian );
                double d3 = (double) num3 / (double) den3;

                DWORD num4 = GetDWORD( head.offset + 24 + headerBase, littleEndian );
                DWORD den4 = GetDWORD( head.offset + 28 + headerBase, littleEndian );
                double d4 = (double) num4 / (double) den4;

                DWORD num5 = GetDWORD( head.offset + 32 + headerBase, littleEndian );
                DWORD den5 = GetDWORD( head.offset + 36 + headerBase, littleEndian );
                double d5 = (double) num5 / (double) den5;

                DWORD num6 = GetDWORD( head.offset + 40 + headerBase, littleEndian );
                DWORD den6 = GetDWORD( head.offset + 44 + headerBase, littleEndian );
                double d6 = (double) num6 / (double) den6;
                
                prf( "ReferenceBlackWhite:                  %lf, %lf, %lf, %lf, %lf, %lf\n", d1, d2, d3, d4, d5, d6 );
            }
            else if ( 700 == head.id )
            {
                prf( "XMP:                                  %d bytes at offset %d\n", head.count, head.offset );

                DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );

                unique_ptr<char> bytes( new char[ head.count + 1 ] );
                bytes.get()[ head.count ] = 0; // ensure it'll be null-terminated
                GetBytes( head.offset + headerBase, bytes.get(), head.count );
                PrintXMPData( bytes.get() );
            }
            else if ( 769 == head.id && 5 == head.type && 1 == head.count )
            {
                DWORD num = GetDWORD( head.offset + headerBase, littleEndian );
                DWORD den = GetDWORD( head.offset + headerBase + 4, littleEndian );
                prf( "IFD0_tag769:                          %d / %d = %lf\n", num, den, (double) num / (double) den );
            }
            else if ( 770 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "IFD0_tag770:                          %s\n", acBuffer );
            }
            else if ( 771 == head.id && 1 == head.type && 1 == head.count )
                prf( "IFD0_tag771: type                     %d, count %d, value %d\n", head.type, head.count, head.offset );
            else if ( 4097 == head.id && 3 == head.type )
                prf( "RelatedImageWidth                     %d\n", head.offset );
            else if ( 4098 == head.id && 3 == head.type )
                prf( "RelatedImageHeight                    %d\n", head.offset );
            else if ( 16384 == head.id && 3 == head.type && 1 == head.count )
                prf( "IFD0_Tag16384: type                   %d, count %d, offset %d\n", head.type, head.count, head.offset );
            else if ( 16385 == head.id && 1 == head.type && 1 == head.count )
                prf( "IFD0_Tag16385: type                   %d, count %d, offset %d\n", head.type, head.count, head.offset );
            else if ( 18246 == head.id && 3 == head.type && 1 == head.count )
                prf( "Rating:                               %d\n", head.offset );
            else if ( 18249 == head.id && 3 == head.type && 1 == head.count )
                prf( "RatingPercent:                        %d\n", head.offset );
            else if ( 20498 == head.id && 4 == head.type && 1 == head.count )
                prf( "ThumbnailFormat:                      %d\n", head.offset );
            else if ( 20752 == head.id && 1 == head.type && 1 == head.count )
                prf( "IFD0tag20752:                         %d\n", head.offset );
            else if ( 20753 == head.id && 4 == head.type && 1 == head.count )
                prf( "IFD0tag20752:                         %d\n", head.offset );
            else if ( 20754 == head.id && 4 == head.type && 1 == head.count )
                prf( "IFD0tag20752:                         %d\n", head.offset );
            else if ( 33432 == head.id && 2 == head.type )
            {
                char acCopyright[ 100 ];
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acCopyright, _countof( acCopyright ), head.count );
                prf( "Copyright:                            %s\n", acCopyright );
            }
            else if ( 33434 == head.id && 10 == head.type )
            {
                DWORD den = head.offset;
                prf( "exif ExposureTime (type 10):          %d / %d = %lf\n", 1, den, (double) 1 / (double) den );
            }
            else if ( 33437 == head.id && 10 == head.type )
            {
                DWORD num = GetDWORD( head.offset + headerBase, littleEndian );
                DWORD den = GetDWORD( head.offset + headerBase + 4, littleEndian );
                prf( "exif FNumber (type 10):               %d / %d = %lf\n", num, den, (double) num / (double) den );
            }
            else if ( 33723 == head.id && ( ( 7 == head.type ) || ( 4 == head.type ) ) )
                EnumerateIPTC( depth + 1,  head.offset, headerBase, head.count );
            else if ( 34016 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "Site:                                 %s\n", acBuffer );
            }
            else if ( 34017 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "ColorSequence:                        %s\n", acBuffer );
            }
            else if ( 34018 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "IT8Header:                            %s\n", acBuffer );
            }
            else if ( 34019 == head.id && 3 == head.type )
                prf( "RasterPadding                         %s (%d)\n", ExifRasterPadding( head.offset), head.offset );
            else if ( 34020 == head.id && 3 == head.type )
                prf( "BitsPerRunLength:                     %d\n", head.offset );
            else if ( 34021 == head.id && 3 == head.type )
                prf( "BitsPerExtendedRunLength:             %d\n", head.offset );
            else if ( 34022 == head.id && 1 == head.type && 0 != head.count )
                prf( "ColorTable:                           %d\n", head.offset );
            else if ( 34023 == head.id && 1 == head.type && 0 != head.count )
                prf( "ImageColorIndicator:                  %d\n", head.offset );
            else if ( 34024 == head.id && 1 == head.type && 0 != head.count )
                prf( "BackgroundColorIndicator:             %d\n", head.offset );
            else if ( 34025 == head.id && 1 == head.type && 0 != head.count )
                prf( "ImageColorValue:                      %d\n", head.offset );
            else if ( 34026 == head.id && 1 == head.type && 0 != head.count )
                prf( "BackgroundColorValue:                 %d\n", head.offset );
            else if ( 34027 == head.id && 1 == head.type && 0 != head.count )
                prf( "PixelIntensityRange:                  %d\n", head.offset );
            else if ( 34028 == head.id && 1 == head.type && 0 != head.count )
                prf( "TransparencyIndicator                 %d\n", head.offset );
            else if ( 34029 == head.id && 1 == head.type && 0 != head.count )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "ColorCharacterization:                %s\n", acBuffer );
            }
            else if ( 34030 == head.id && 4 == head.type && 1 == head.count )
                prf( "HCUsage:                              %s (%d)\n", ExifHCUsage( head.offset ), head.offset );
            else if ( 34377 == head.id && ( ( 7 == head.type ) || ( 1 == head.type ) ) )
                EnumerateAdobeImageResources( depth + 1,  head.offset, headerBase, head.count );
            else if ( 34391 == head.id && 1 == head.type )
            {
                prf( "mystery tag %d:\n", head.id );
                DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
            }
            else if ( 34392 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "mystery tag:                          %s (tag %d)\n", acBuffer, head.id );
            }
            else if ( 34665 == head.id )
                EnumerateExifTags( depth + 1,  head.offset, headerBase, littleEndian );
            else if ( 34675 == head.id && 7 == head.type )
            {
                prf( "Image.InterColorProfile:              %d bytes at offset %d\n", head.count, head.offset );
                DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
            }
            else if ( 34850 == head.id )
                prf( "ExposureProgram                       %s\n", ExifExposureProgram( head.offset ) );
            else if ( 34853 == head.id )
            {
                prf( "GPSInfo:                              %d bytes at offset %d type %d\n", head.count, head.offset, head.type );
                EnumerateGPSTags( depth + 1,  head.offset, headerBase, littleEndian );
            }
            else if ( 34855 == head.id && 8 == head.type ) // Note: type 8 is bogus, but Nokia uses it
                prf( "ISO                                   %d\n", head.offset );
            else if ( 36867 == head.id && 2 == head.type )
            {
                char acDateTimeOriginal[ 100 ];
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acDateTimeOriginal, _countof( acDateTimeOriginal ), head.count );
                prf( "DateTimeOriginal:                     %s\n", acDateTimeOriginal );
            }
            else if ( 36868 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "exif CreateDate:                    %s\n", acBuffer );
            }
            else if ( 36880 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "OffsetTime:                           %s\n", acBuffer );
            }
            else if ( 36881 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "OffsetTimeOriginal:                   %s\n", acBuffer );
            }
            else if ( 36882 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "OffsetTimeDigitized:                  %s\n", acBuffer );
            }
            else if ( 37383 == head.id && 3 == head.type )
                prf( "MeteringMode:                         %d\n", head.offset );
            else if ( 37384 == head.id && 3 == head.type)
                prf( "LightSource:                          %d\n", head.offset );
            else if ( 37385 == head.id && 3 == head.type )
                prf( "Flash:                                %d - %s\n", head.offset, GetFlash( head.offset ) );
            else if ( 37386 == head.id && 5 == head.type )
            {
                DWORD num = GetDWORD( head.offset + headerBase, littleEndian );
                DWORD den = GetDWORD( head.offset + 4 + headerBase, littleEndian );

                double focalLength = 0.0;
                if ( 0 != den )
                    focalLength = (double) num / (double) den;
 
                prf( "FocalLength:                          %d / %d = %lf\n", num, den, focalLength );
            }
            else if ( 37396 == head.id && 3 == head.type && 4 == head.count )
            {
                WORD a = GetWORD( head.offset +     headerBase, littleEndian );
                WORD b = GetWORD( head.offset + 2 + headerBase, littleEndian );
                WORD c = GetWORD( head.offset + 4 + headerBase, littleEndian );
                WORD d = GetWORD( head.offset + 6 + headerBase, littleEndian );

                prf( "SubjectArea:                          %d, %d, %d, %d\n", a, b, c, d );
            }
            else if ( 37398 == head.id )
                prf( "TIFF-EPStandardID:                    %#x\n", head.offset );
            else
                tagHandled = false;

            if ( 37520 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "SubSecTime:                           %s\n", acBuffer );
            }
            else if ( 37521 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "SubSecTimeOriginal:                   %s\n", acBuffer );
            }
            else if ( 37522 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "SubSecTimeDigitized:                  %s\n", acBuffer );
            }
            else if ( 37724 == head.id && 7 == head.type )
            {
                prf( "ImageSourceData (photoshop)           %d bytes\n", head.count );
                DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
            }
            else if ( 40091 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "Windows XPTitle:                      %s\n", acBuffer );
            }
            else if ( 40091 == head.id && 1 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "Windows XPTitle\n" );
                DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
            }
            else if ( 40092 == head.id && 1 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "Windows XPComment\n" );
                DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
            }
            else if ( 40093 == head.id && 1 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "Windows XPAuthor\n" );
                DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
            }
            else if ( 40094 == head.id && 1 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "Windows XPKeywords\n" );
                DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
            }
            else if ( 40095 == head.id && 1 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "Windows XPSubject:\n" );
                DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
            }
            else if ( 41037 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "mystery tag:                          %s (tag %d)\n", acBuffer, head.id );
            }
            else if ( 41038 == head.id && 1 == head.type )
            {
                prf( "mystery tag %d:\n", head.id );
                DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
            }
            else if ( 41985 == head.id )
                prf( "Custom Rendered:                      %s\n", ExifCustomRendered( head.offset ) );
            else if ( 41986 == head.id )
                prf( "Exposure Mode:                        %s\n", ExifExposureMode( head.offset ) );
            else if ( 41987 == head.id )
                prf( "White Balance:                        %s\n", ExifWhiteBalance( head.offset ) );
            else if ( 41988 == head.id && 5 == head.type )
            {
                DWORD num = GetDWORD( head.offset + headerBase, littleEndian );
                DWORD den = GetDWORD( head.offset + 4 + headerBase, littleEndian );

                prf( "Digital Zoom Ratio:                   %d / %d = %lf\n", num, den, (double) num / (double) den );
            }
            else if ( 41989 == head.id && IsIntType( head.type ) )
                prf( "Focal Length in 35mm Film:            %d\n", head.offset );
            else if ( 41990 == head.id )
                prf( "Scene Capture Type:                   %s\n", ExifSceneCaptureType( head.offset ) );
            else if ( 41991 == head.id )
                prf( "Gain Control:                         %s\n", ExifGainControl( head.offset ) );
            else if ( 41992 == head.id )
                prf( "Contrast:                             %s\n", ExifContrast( head.offset ) );
            else if ( 41993 == head.id )
                prf( "Saturation:                           %s\n", ExifContrast( head.offset ) ); // same values as saturation
            else if ( 41994 == head.id )
                prf( "Sharpness:                            %s\n", ExifSharpness( head.offset ) );
            else if ( 41995 == head.id && 7 == head.type )
            {
                prf( "DeviceSettingDescription:\n" );
                DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
            }
            else if ( 41996 == head.id )
                prf( "Subject Distance Range:               %s\n", ExifSubjectDistanceRange( head.offset ) );
            else if ( 42016 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "ImageUniqueID:                        %s\n", acBuffer );
            }
            else if ( 42034 == head.id && 5 == head.type && 4 == head.count )
            {
                DWORD numMinFL = GetDWORD( head.offset +      headerBase, littleEndian );
                DWORD denMinFL = GetDWORD( head.offset +  4 + headerBase, littleEndian );

                DWORD numMaxFL = GetDWORD( head.offset +  8 + headerBase, littleEndian );
                DWORD denMaxFL = GetDWORD( head.offset + 12 + headerBase, littleEndian );

                DWORD numMinAp = GetDWORD( head.offset + 16 + headerBase, littleEndian );
                DWORD denMinAp = GetDWORD( head.offset + 20 + headerBase, littleEndian );

                DWORD numMaxAp = GetDWORD( head.offset + 24 + headerBase, littleEndian );
                DWORD denMaxAp = GetDWORD( head.offset + 28 + headerBase, littleEndian );

                prf( "Lens min/max FL and Aperture:         %.2lf, %.2lf, %.2lf, %.2lf\n",
                        (double) numMinFL / (double) denMinFL,
                        (double) numMaxFL / (double) denMaxFL,
                        (double) numMinAp / (double) denMinAp,
                        (double) numMaxAp / (double) denMaxAp );
            }
            else if ( 42035 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "Lens Make:                            %s\n", acBuffer );
            }
            else if ( 42036 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "Lens Model:                           %s\n", acBuffer );
            }
            else if ( 42037 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "Lens Serial Number:                   %s\n", acBuffer );
            }
            else if ( 42080 == head.id && 3 == head.type && 1 == head.count )
                prf( "Composite Image:                      %s\n", ExifComposite( head.offset ) );
            else if ( 50727 == head.id && 5 == head.type && 3 == head.count )
            {
                LONG num1 = GetDWORD( head.offset +      headerBase, littleEndian );
                LONG den1 = GetDWORD( head.offset +  4 + headerBase, littleEndian );
                double d1 = (double) num1 / (double) den1;

                LONG num2 = GetDWORD( head.offset +  8 + headerBase, littleEndian );
                LONG den2 = GetDWORD( head.offset + 12 + headerBase, littleEndian );
                double d2 = (double) num2 / (double) den2;

                LONG num3 = GetDWORD( head.offset + 16 + headerBase, littleEndian );
                LONG den3 = GetDWORD( head.offset + 20 + headerBase, littleEndian );
                double d3 = (double) num3 / (double) den3;

                prf( "AnalogBalance:                        %lf, %lf, %lf\n", d1, d2, d3 );
            }
            else if ( 50341 == head.id && 7 == head.type )
            {
                prf( "PrintImageMatching:                   %d bytes\n", head.count );
                DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
            }
            else if ( 50648 == head.id )                 
                prf( "Canon-Specific(50648):                %d\n", head.offset );
            else if ( 50649 == head.id )
                prf( "Canon-Specific(50649):                %d\n", head.offset );
            else if ( 50656 == head.id )
                prf( "Canon-Specific(50656):                %d\n", head.offset );
            else if ( 50706 == head.id && IsIntType( head.type ) )
                prf( "DNGVersion:                           %#x\n", head.offset );
            else if ( 50707 == head.id && IsIntType( head.type ) )
                prf( "DNGBackwardVersion:                   %#x\n", head.offset );
            else if ( 50708 == head.id && 2 == head.type )
            {
                char acUniqueCameraModel[ 100 ];
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acUniqueCameraModel, _countof( acUniqueCameraModel ), head.count );
                prf( "UniqueCameraModel:                    %s\n", acUniqueCameraModel );
            }
            else if ( 50709 == head.id && 2 == head.type )
            {
                char acLocalizedCameraModel[ 100 ];
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acLocalizedCameraModel, _countof( acLocalizedCameraModel ), head.count );
                prf( "LocalizedCameraModel:                 %s\n", acLocalizedCameraModel );
            }
            else if ( 50709 == head.id && 1 == head.type )
            {
                // Treat type 1 as type 2. LEICA X-U (Typ 113) does this, unlike every other camera that uses type 2

                char acLocalizedCameraModel[ 100 ];
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acLocalizedCameraModel, _countof( acLocalizedCameraModel ), head.count );
                prf( "LocalizedCameraModel:                 %s\n", acLocalizedCameraModel );
            }
            else if ( 50721 == head.id && 10 == head.type )
            {
                int num = (int) GetDWORD( head.offset + headerBase, littleEndian );
                int den = (int) GetDWORD( head.offset + headerBase + 4, littleEndian );
                prf( "ColorMatrix1:                         %d / %d = %lf\n", num, den, (double) num / (double) den );
            }
            else if ( 50722 == head.id && 10 == head.type )
            {
                int num = (int) GetDWORD( head.offset + headerBase, littleEndian );
                int den = (int) GetDWORD( head.offset + headerBase + 4, littleEndian );
                prf( "ColorMatrix2:                         %d / %d = %lf\n", num, den, (double) num / (double) den );
            }
            else if ( 50727 == head.id && 5 == head.type )
            {
                DWORD num = GetDWORD( head.offset + headerBase, littleEndian );
                DWORD den = GetDWORD( head.offset + headerBase + 4, littleEndian );
                prf( "AnalogBalance:                        %d / %d = %lf\n", num, den, (double) num / (double) den );
            }
            else if ( 50728 == head.id && 5 == head.type )
            {
                DWORD num = GetDWORD( head.offset + headerBase, littleEndian );
                DWORD den = GetDWORD( head.offset + headerBase + 4, littleEndian );
                prf( "AsShotNeutral:                        %d / %d = %lf\n", num, den, (double) num / (double) den );
            }
            else if ( 50730 == head.id && 10 == head.type )
            {
                int num = (int) GetDWORD( head.offset + headerBase, littleEndian );
                int den = (int) GetDWORD( head.offset + headerBase + 4, littleEndian );
                prf( "BaselineExposure:                     %d / %d = %lf\n", num, den, (double) num / (double) den );
            }
            else if ( 50731 == head.id && 5 == head.type )
            {
                DWORD num = GetDWORD( head.offset + headerBase, littleEndian );
                DWORD den = GetDWORD( head.offset + headerBase + 4, littleEndian );
                prf( "BaselineNoise:                        %d / %d = %lf\n", num, den, (double) num / (double) den );
            }
            else if ( 50732 == head.id && 5 == head.type )
            {
                DWORD num = GetDWORD( head.offset + headerBase, littleEndian );
                DWORD den = GetDWORD( head.offset + headerBase + 4, littleEndian );
                prf( "BaselineSharpness:                    %d / %d = %lf\n", num, den, (double) num / (double) den );
            }
            else if ( 50734 == head.id && 5 == head.type )
            {
                DWORD num = GetDWORD( head.offset + headerBase, littleEndian );
                DWORD den = GetDWORD( head.offset + headerBase + 4, littleEndian );
                prf( "LinearResponseLimit:                  %d / %d = %lf\n", num, den, (double) num / (double) den );
            }
            else if ( 50735 == head.id && 2 == head.type )
            {
                char acCameraSerialNumber[ 100 ];
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acCameraSerialNumber, _countof( acCameraSerialNumber ), head.count );
                prf( "CameraSerialNumber:                   %s\n", acCameraSerialNumber );
            }
            else if ( 50736 == head.id && 5 == head.type && 4 == head.count )
            {
                DWORD num1 = GetDWORD( head.offset +      headerBase, littleEndian );
                DWORD den1 = GetDWORD( head.offset +  4 + headerBase, littleEndian );
                double d1 = (double) num1 / (double) den1;

                DWORD num2 = GetDWORD( head.offset +  8 + headerBase, littleEndian );
                DWORD den2 = GetDWORD( head.offset + 12 + headerBase, littleEndian );
                double d2 = (double) num2 / (double) den2;

                DWORD num3 = GetDWORD( head.offset + 16 + headerBase, littleEndian );
                DWORD den3 = GetDWORD( head.offset + 20 + headerBase, littleEndian );
                double d3 = (double) num3 / (double) den3;

                DWORD num4 = GetDWORD( head.offset + 24 + headerBase, littleEndian );
                DWORD den4 = GetDWORD( head.offset + 28 + headerBase, littleEndian );
                double d4 = (double) num4 / (double) den4;

                prf( "DNGLensInfo:                          %lf, %lf, %lf, %lf\n", d1, d2, d3, d4 );
            }
            else if ( 50739 == head.id && 5 == head.type && 1 == head.count )
            {
                // IFD0 tag 36 ID 50739==0xc633, type 5, count 1, offset/value 2242

                DWORD num = GetDWORD( head.offset + headerBase, littleEndian );
                DWORD den = GetDWORD( head.offset + headerBase + 4, littleEndian );
                prf( "ShadowScale:                          %d / %d = %lf\n", num, den, (double) num / (double) den );
            }
            else if ( 50740 == head.id && IsIntType( head.type ) )
            {
                prf( "current offset %#llx, head.type: %d, head.count: %d, head.id %d==%#x, head.offset %#x\n",
                        IFDOffset, head.type, head.count, head.id, head.id, head.offset );

                prf( "DNGPrivateData:                       %#x for make %s\n", head.offset, g_acMake );

                // Sony and Ricoh Makernotes (in addition to makernotes stored in Exif IFD)

                EnumerateMakernotes( depth + 1,  head.offset, headerBase, littleEndian );
            }
            else if ( 50741 == head.id && IsIntType( head.type ) )
                prf( "MakerNoteSafety:                      %#x\n", head.offset );
            else if ( 50752 == head.id )
                prf( "Canon-Specific(50752):                %d\n", head.offset );
            else if ( 50778 == head.id && IsIntType( head.type ) )
                prf( "CalibrationIlluminant1:               %#x\n", head.offset );
            else if ( 50779 == head.id && IsIntType( head.type ) )
                prf( "CalibrationIlluminant2:               %#x\n", head.offset );
            else if ( 50781 == head.id && 1 == head.type && 16 == head.count )
            {
                // Hasselblad uses this
                prf( "RawDataUniqueID:                      %d (type) %d (count) %d (offset/value)\n", head.type, head.count, head.offset );

                DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
            }
            else if ( 50885 == head.id )
                prf( "Canon-Specific(50885):                %d\n", head.offset );
            else if ( 50898 == head.id && 7 == head.type )
            {
                prf( "Panosonic-Title                       type %d, count %d, offset %d\n", head.type, head.count, head.offset );
                DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
            }
            else if ( 50899 == head.id && 7 == head.type )
            {
                prf( "Panosonic-Title2                      type %d, count %d, offset %d\n", head.type, head.count, head.offset );
                DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
            }
            else if ( 50908 == head.id )
                prf( "Canon-Specific(50908):                %d\n", head.offset );
            else if ( 50934 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "AsShotProfileName:                    %s\n", acBuffer );
            }
            else if ( 50937 == head.id && 4 == head.type && 3 == head.count )
            {
                DWORD num1 = GetDWORD( head.offset +     headerBase, littleEndian );
                DWORD num2 = GetDWORD( head.offset + 4 + headerBase, littleEndian );
                DWORD num3 = GetDWORD( head.offset + 8 + headerBase, littleEndian );

                prf( "ProfileHueSatMapDims:                 %d %d %d\n", num1, num2, num3 );
            }
            else if ( 50938 == head.id && 11 == head.type )
                prf( "ProfileHueSatMapData1:                %d floats\n", head.count );
            else if ( 50939 == head.id && 11 == head.type )
                prf( "ProfileHueSatMapData2:                %d floats\n", head.count );
            else if ( 50940 == head.id && 11 == head.type )
                prf( "ProfileToneCurve:                     %d floats\n", head.count );
            else if ( 50941 == head.id && 4 == head.type )
                prf( "ProfileEmbedPolicy:                   %d (%s)\n", head.count, GetProfileEmbedPolicy( head.count ) );
            else if ( 50970 == head.id )
                prf( "PreviewColorSpace:                    %s\n", TagPreviewColorSpace( head.offset ) );
            else if ( 59932 == head.id && 7 == head.type )
            {
                prf( "IFD0 Padding type                     %d, count %d, offset %d\n", head.type, head.count, head.offset );
                DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
            }
            else if ( 59933 == head.id && 9 == head.type )
                prf( "MSFTOffsetSchema type                 %d, count %d, offset %d\n", head.type, head.count, head.offset );
            else if ( !tagHandled )
            {
                prf( "IFD0 tag %d ID %d==%#x, type %d, count %d, offset/value %d\n", i, head.id, head.id, head.type, head.count, head.offset );

                if ( 7 == head.type )
                    DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );

                if ( 0 == head.id )
                {
                    prf( "warning: malformed entry head.id of 0\n" );
                    return;
                }
            }
        }

        IFDOffset = GetDWORD( IFDOffset + headerBase, littleEndian );

        if ( g_FullInformation )
            prf( "end of IFD0 directory, offset to next directory: %I64d\n", IFDOffset );

        currentIFD++;
    }
} //EnumerateIFD0

const char * GetPNGColorType( byte ct )
{
    if ( 0 == ct )
        return "grayscale";

    if ( 2 == ct )
        return "RGB";

    if ( 3 == ct )
        return "palette index";

    if ( 4 == ct )
        return "greyscale and alpha";

    if ( 6 == ct )
        return "RGBA";

    return "unknown";
} //GetPNGColorType

const char * GetPNGRenderingIntent( byte ri )
{
    if ( 0 == ri )
        return "perceptual";

    if ( 1 == ri )
        return "relative colorimetric";

    if ( 2 == ri )
        return "saturation";

    if ( 3 == ri )
        return "absolute colorimetric";

    return "unknown";
} //GetPNGRenderingIntent

const char * GetPNGUnitSpecifier( byte b )
{
    if ( 0 == b )
        return "pixels";
    if ( 1 == b )
        return "meters";

    return "unknown";
} //GetPNGUnitSpecifier

const char * GetPNGFilterMethod( byte fm )
{
    if ( 0 == fm )
        return "none";

    if ( 1 == fm )
        return "sub";

    if ( 2 == fm )
        return "up";

    if ( 3 == fm )
        return "average";

    if ( 4 == fm )
        return "paeth";

    return "unknown";
} //GetPNGFilterMethod

const char * GetPNGInterlaceMethod( byte im )
{
    if ( 0 == im )
        return "sequential";

    if ( 1 == im )
        return "adam7 interlaced";

    return "unknown";
} //GetPNGInterlaceMethod

// https://en.wikipedia.org/wiki/Portable_Network_Graphics
// http://www.libpng.org/pub/png/spec/1.2/PNG-Chunks.html
// PNG files are big-endian. 8-byte header then records of:
//    4: length
//    4: type
//    length: data
//    4: crc

void ParsePNG()
{
    DWORD dwHeader0 = GetDWORD( 0, false );
    DWORD dwHeader4 = GetDWORD( 4, false );
    if ( 0x89504e47 != dwHeader0 || 0xd0a1a0a != dwHeader4 )
    {
        prf( "Invalid PNG header: first 4 bytes: %#x, next 4 bytes: %#x\n", dwHeader0, dwHeader4 );
        prf( "expected 0x89504e47 then 0xd0a1a0a\n" );
        return;
    }

    DWORD offset = 8;
    DWORD width, height;
    byte bitDepth, colorType, compressionMethod, filterMethod, interlaceMethod;
    DWORD idatSegments = 0;
    DWORD idatSize = 0;

    prf( "mimetype:                             image/png\n" );

    do
    {
        bool seekOK = g_pStream->Seek( offset );

        if ( !seekOK || g_pStream->AtEOF() )
            return;

        DWORD len = GetDWORD( offset, false );

        if ( len > ( 128 * 1024 * 1024 ) )
        {
            prf( "aparently invalid length: %d\n", len );
            return;
        }

        DWORD type = GetDWORD( offset + 4, false );

        // malformed file

        if ( 0 == type )
            break;

        char acChunkType[ 5 ];
        memcpy( acChunkType, &type, 4 );
        acChunkType[ 4 ] = 0;

        if ( g_FullInformation )
            prf( "png chunk type: %s %#x, len %d\n", acChunkType, type, len );

        if ( 0x49454e44 == type ) // IEND  end of chunks
        {
            if ( g_FullInformation )
                prf( "offset of IEND chunk: %d\n", offset );

            break;
        }
        else if ( 0x49484452 == type ) // IHDR   header
        {
            width = GetDWORD( offset + 8, false );
            height = GetDWORD( offset + 12, false );
            bitDepth = GetBYTE( offset + 16 );
            colorType = GetBYTE( offset + 17 );
            compressionMethod = GetBYTE( offset + 18 );
            filterMethod = GetBYTE( offset + 19 );
            interlaceMethod = GetBYTE( offset + 20 );

            prf( "image width:           %16d\n", width );
            prf( "image height:          %16d\n", height );
            prf( "bit depth:             %16d\n", bitDepth );
            prf( "color type:            %16d (%s)\n", colorType, GetPNGColorType( colorType) );
            prf( "compression method:    %16d\n", compressionMethod );
            prf( "filter method:         %16d (%s)\n", filterMethod, GetPNGFilterMethod( filterMethod ) );
            prf( "interlace method:      %16d (%s)\n", interlaceMethod, GetPNGInterlaceMethod( interlaceMethod ) );
        }
        else if ( 0x73524742 == type ) // sRGB
        {
            BYTE renderingIntent = GetBYTE( offset + 8 );

            prf( "sRGB rendering intent: %16d (%s)\n", renderingIntent, GetPNGRenderingIntent( renderingIntent ) );
        }
        else if ( 0x67414d41 == type ) // gAMA
        {
            DWORD gamma = GetDWORD( offset + 8, false );

            prf( "gamma:               %18d\n", gamma );
        }
        else if ( 0x70485973 == type ) // pHYs
        {
            DWORD pixelsPerUnitX = GetDWORD( offset + 8, false );
            DWORD pixelsPerUnitY = GetDWORD( offset + 12, false );
            BYTE unitSpecifier = GetBYTE( offset + 16 );

            prf( "pixels per unit X:     %16d\n", pixelsPerUnitX );
            prf( "pixels per unit Y:     %16d\n", pixelsPerUnitY );
            prf( "unit specifier:        %16d (%s)\n", unitSpecifier, GetPNGUnitSpecifier( unitSpecifier ) );
        }
        else if ( 0x49444154 == type ) // IDAT
        {
            // image data; ignore for now

            if ( g_FullInformation )
                prf( "flac image data length:     %16d\n", len );

            idatSegments++;
            idatSize += len;
        }
        else if ( 0x6348524d == type ) // cHRM
        {
            // chromacity coordinates of the display primaries and white point
            prf( "chromacity:\n" );
            prf( "       white point x %18u\n", GetDWORD( offset + 8, false ) );
            prf( "       white point y %18u\n", GetDWORD( offset + 12, false ) );
            prf( "       red x         %18u\n", GetDWORD( offset + 16, false ) );
            prf( "       red y         %18u\n", GetDWORD( offset + 20, false ) );
            prf( "       green x       %18u\n", GetDWORD( offset + 24, false ) );
            prf( "       green y       %18u\n", GetDWORD( offset + 28, false ) );
            prf( "       blue x        %18u\n", GetDWORD( offset + 32, false ) );
            prf( "       blue y        %18u\n", GetDWORD( offset + 36, false ) );
        }
        else if ( 0x74455874 == type ) // tEXt
        {
            char acTextKey[ 80 ];
            acTextKey[ 0 ] = 0;

            GetString( offset + 8, acTextKey, _countof( acTextKey ), _countof( acTextKey ) );

            int keylen = strlen( acTextKey );
            int valuelen = len - keylen - 1;
            unique_ptr<char> textValue( new char[ valuelen + 1 ] );
            GetString( offset + 8 + keylen + 1, textValue.get(), valuelen + 1, valuelen + 1 );

            prf( "text key:                             %s, value: %s\n", acTextKey, textValue.get() );
        }
        else if ( 0x69545874 == type ) // iTXt
        {
            prf( "text chunk length:     %16d\n", len );
            DumpBinaryData( offset + 8, 0, len, 8, offset + 8 );
        }
        else if ( 0x504c5445 == type ) // PLTE palette
        {
            DWORD entries = len / 3;

            if ( 0 != ( len % 3 ) )
            {
                prf( "PNG palette chunk is malformed\n" );
                return;
            }

            prf( "palette has %d entries\n", entries );

            for ( DWORD e = 0; e < entries; e++ )
            {
                DWORD o = offset + 8 + e * 3;
                prf( "    %3d: r %3d, g %3d, b %3d\n", e, GetBYTE( o ), GetBYTE( o + 1 ), GetBYTE( o + 2 ) );
            }
        }
        else if ( 0x74524e53 == type ) // tRNS transparency information
        {
            prf( "transparency length:   %16d\n", len );
            DumpBinaryData( offset + 8, 0, len, 8, offset + 8 );
        }
        else if ( 0x73424954 == type ) // sBIT
        {
            prf( "significant bits len:  %16d\n", len );
            DumpBinaryData( offset + 8, 0, len, 8, offset + 8 );
        }
        else if ( 0x624b4744 == type ) // bKGD
        {
            prf( "background color len:  %16d\n", len );
            DumpBinaryData( offset + 8, 0, len, 8, offset + 8 );
        }
        else if ( 0x74494d45 == type ) // tIME
        {
            prf( "last modification:                    year %d, month %d, day %d, hour %d, minute %d, second %d\n",
                    GetWORD( offset + 8, false ),
                    GetBYTE( offset + 10 ),
                    GetBYTE( offset + 11 ),
                    GetBYTE( offset + 12 ),
                    GetBYTE( offset + 13 ),
                    GetBYTE( offset + 14 ) );
        }
        else if ( 0x7a545874 == type ) // zTXt
        {
            char acTextKey[ 80 ];
            acTextKey[ 0 ] = 0;
            GetString( offset + 8, acTextKey, _countof( acTextKey ), _countof( acTextKey ) );
            int keyLen = strlen( acTextKey );

            BYTE compressionMethod = GetBYTE( offset + 8 + keyLen + 1 );
            int valueLen = len - keyLen - 2;
            prf( "compressed text key:                  %s, compression method %d, value %d bytes\n", acTextKey, compressionMethod, valueLen );
            int dataOffset = offset + 8 + keyLen + valueLen + 2;
            DumpBinaryData( dataOffset, 0, valueLen, 8, dataOffset );
        }
        else if ( 0x69434350 == type ) // iCCP
        {
            prf( "embedded ICC profile length %d\n", len );
            DumpBinaryData( offset + 8, 0, len, 8, offset + 8 );
        }
        else if ( 0x6f464673 == type ) // oFFs
        {
            prf( "%s tag %d\n", acChunkType, len );
            DumpBinaryData( offset + 8, 0, len, 8, offset + 8 );
        }
#if false
        else if ( 0x6d655461 == type ) // meTa
        {
        }
        else if ( 0x76704167 == type )
        {
        }
        else if ( 0x69444f54 == type )
        {
        }
        else if ( 0x65584966 == type )
        {
        }
        else if ( 0x70725657 == type )
        {
        }
        else if ( 0x6d6b4246 == type )
        {
        }
        else if ( 0x6d6b5453 == type )
        {
        }
        else if ( 0x6d6b4254 == type )
        {
        }
        else if ( 0x6d6b4253 == type )
        {
        }
        else if ( 0x70416e69 == type )
        {
        }
#endif
        else
        {
            prf( "unrecognized PNG chunk type %s %#x, length %d\n", acChunkType, type, len );
            DumpBinaryData( offset + 8, 0, len, 8, offset + 8 );

            if ( 0 == *acChunkType )
                break;
        }

        offset += ( 12 + len );
    } while( true );

    prf( "IDAT segments:         %16d\n", idatSegments );
    prf( "IDAT data bytes:       %16d\n", idatSize );
} //ParsePNG

const char * BmpCompression( DWORD c )
{
    if ( BI_RGB == c )
        return "uncompressed";

    if ( BI_RLE8 == c )
        return "run-length encoded 8 (RLE8)";

    if ( BI_RLE4 == c )
        return "run-length encoded 4 (RLE4)";

    if ( BI_BITFIELDS == c )
        return "bitfields";

    if ( BI_JPEG == c )
        return "JPEG";

    if ( BI_PNG == c )
        return "PNG";

    return "unknown";
} //BmpCompression

const char * BmpColorSpace( DWORD cs )
{
    if ( LCS_CALIBRATED_RGB == cs )
        return "calibrated";

    if ( LCS_sRGB == cs )
        return "sRGB color space";

    if ( LCS_WINDOWS_COLOR_SPACE == cs )
        return "system default color space";

    if ( PROFILE_LINKED == cs )
        return "linked profile";

    if ( PROFILE_EMBEDDED == cs )
        return "embedded profile";

    return "unknown";
} //BmpColorSpace

const char * BmpIntent( DWORD i )
{
    if ( LCS_GM_ABS_COLORIMETRIC == i )
        return "match absolute colorimetric";

    if ( LCS_GM_BUSINESS == i )
        return "graphic saturation";

    if ( LCS_GM_GRAPHICS == i )
        return "proof relative colorimetric";

    if ( LCS_GM_IMAGES == i )
        return "picture perceptual";

    return "unknown";
} //BmpIntent

void ParseBMP()
{
    prf( "mimetype:                             image/bmp\n" );

    BITMAPFILEHEADER bfh;
    GetBytes( 0, &bfh, sizeof bfh );

    prf( "type:                         %#x %#x == %c%c\n", bfh.bfType & 0xff, bfh.bfType >> 8, bfh.bfType & 0xff, bfh.bfType >> 8 );
    prf( "size of file field:%20d\n", bfh.bfSize );

    if ( bfh.bfSize != g_pStream->Length() )
        prf( "  (warning: file size %lld isn't the same as bitmap file header size %d)\n", g_pStream->Length(), bfh.bfSize );

    prf( "reserved 1:          %18d\n", bfh.bfReserved1 );
    prf( "reserved 2:          %18d\n", bfh.bfReserved2 );
    prf( "offset of bits:      %18d\n", bfh.bfOffBits );

    BITMAPV5HEADER bih;
    GetBytes( sizeof bfh, &bih, sizeof bih );

    prf( "header size:         %18d\n", bih.bV5Size );
    prf( "width:               %18d\n", bih.bV5Width );
    prf( "height:              %18d\n", bih.bV5Height );
    prf( "planes:              %18d\n", bih.bV5Planes );
    prf( "bit count:           %18d\n", bih.bV5BitCount );
    prf( "compression:         %18d == %s\n", bih.bV5Compression, BmpCompression( bih.bV5Compression ) );
    prf( "size of image:       %18d\n", bih.bV5SizeImage );
    prf( "pixels per meter X:  %18d\n", bih.bV5XPelsPerMeter );
    prf( "pixels per meter Y:  %18d\n", bih.bV5YPelsPerMeter );
    prf( "color indices:       %18d\n", bih.bV5ClrUsed );
    prf( "colors required:     %18d\n", bih.bV5ClrImportant );

    if ( bih.bV5Size >= sizeof BITMAPV4HEADER )
    {
        // >= bV5RedMask

        if ( BI_BITFIELDS == bih.bV5Compression )
            prf( "RGB masks:                            %#010x %#010x %#010x\n", bih.bV5RedMask, bih.bV5GreenMask, bih.bV5BlueMask );

        if ( LCS_CALIBRATED_RGB == bih.bV5CSType )
        {
            prf( "gamma red:        %#21x\n", bih.bV5GammaRed );
            prf( "gamma green:      %#21x\n", bih.bV5GammaGreen );
            prf( "gamma blue:       %#21x\n", bih.bV5GammaBlue );
        }

        prf( "alpha mask:                  %#010x\n", bih.bV5AlphaMask );
        prf( "color space:         %18d == %s\n", bih.bV5CSType, BmpColorSpace( bih.bV5CSType ) );
    }

    if ( bih.bV5Size >= sizeof BITMAPV5HEADER )
    {
        // >= bV5Intent

        prf( "intent:              %18d == %s\n", bih.bV5Intent, BmpIntent( bih.bV5Intent ) );

        if ( PROFILE_LINKED == bih.bV5CSType || PROFILE_EMBEDDED == bih.bV5CSType )
        {
            prf( "profile offset:       %18d\n", bih.bV5ProfileData );
            prf( "profile size:         %18d\n", bih.bV5ProfileSize );
        }
    }
} //ParseBMP

const char * MP3PictureType( byte x )
{
    if (0x00 == x ) return "Other";
    if (0x01 == x ) return "32x32 pixels 'file icon' (PNG only)";
    if (0x02 == x ) return "Other file icon";
    if (0x03 == x ) return "Cover (front)";
    if (0x04 == x ) return "Cover (back)";
    if (0x05 == x ) return "Leaflet page";
    if (0x06 == x ) return "Media (e.g. label side of CD)";
    if (0x07 == x ) return "Lead artist/lead performer/soloist";
    if (0x08 == x ) return "Artist/performer";
    if (0x09 == x ) return "Conductor";
    if (0x0A == x ) return "Band/Orchestra";
    if (0x0B == x ) return "Composer";
    if (0x0C == x ) return "Lyricist/text writer";
    if (0x0D == x ) return "Recording Location";
    if (0x0E == x ) return "During recording";
    if (0x0F == x ) return "During performance";
    if (0x10 == x ) return "Movie/video screen capture";
    if (0x11 == x ) return "A bright coloured fish";
    if (0x12 == x ) return "Illustration";
    if (0x13 == x ) return "Band/artist logotype";
    if (0x14 == x ) return "Publisher/Studio logotype";
    return "unknown";
} //MP3PictureType

bool isMP3Frame( char const * frameID, char const * name, const char * name2 = 0 )
{
    if ( * (DWORD *) frameID == * (DWORD *) name )
        return true;

    if ( NULL == name2 )
        return false;

    return ( * (DWORD *) frameID == * (DWORD *) name2 );
} //isMP3Frame

bool isValidMP3Frame( char const * pc )
{
    for ( int i = 0; i < 4; i++ )
    {
        char c = pc[i];

        if ( 0 == c )
        {
            if ( 0 == i )
                return false;
            else
                continue;
        }

        if ( ( c > 'Z' || c < 'A' ) && ( c > '9' || c < '0' ) )
            return false;
    }

    return true;
} //isValidMP3Frame

void ConvertInvalidCharsToDot( WCHAR * pwc )
{
    for ( WCHAR *p = pwc; *p; p++ )
    {
        if ( *p < ' ' )
            *p = '.';
    }
} //ConvertInvalidCharsToDot

void ReadMP3String( __int64 offset, WCHAR * pwc, int cwc, int maxBytes, bool lang, bool throwawayField = false, bool invalidCharsToDot = true )
{
    int cbOutput = cwc * sizeof WCHAR;
    if ( cwc > 0 )
        *pwc = 0;

    if ( maxBytes > cbOutput )
    {
        prf( "maxBytes %d is > cbOutput %d; truncating\n", maxBytes, cbOutput );
        maxBytes = cbOutput - 2;
    }

    unique_ptr<byte> data( new byte[ maxBytes ] );
    GetBytes( offset, data.get(), maxBytes );
    int o = 0;
    byte encoding = data.get()[ o++ ];

    if ( o >= maxBytes )
        return;

    if ( encoding > 3 )
    {
        // there must not be an encoding

        encoding = 0;
        o--;
    }

    if ( lang )
    {
        // skip past the language tag

        o += 3;
    }

    if ( o >= maxBytes )
        return;

    // skip past the throwaway null-terminated string

    if ( throwawayField )
    {
        if ( 0 == encoding || 3 == encoding )
        {
            while ( 0 != data.get()[ o ] )
            {
                o++;

                if ( o >= maxBytes )
                    return;
            }

            o++;
        }
        else if ( 1 == encoding || 2 == encoding )
        {
            while ( 0 != ( data.get()[ o ] | ( ( data.get()[ o + 1 ] ) << 8 ) ) )
            {
                o += 2;

                if ( o >= maxBytes )
                    return;
            }

            o += 2;
        }
    }

    if ( o >= maxBytes )
        return;

    if ( 0 == encoding || 3 == encoding )
    {
        int cwcWritten = MultiByteToWideChar( CP_UTF8, 0, (char *) data.get() + o, maxBytes - o, pwc, cwc );
        pwc[ cwcWritten ] = 0;
    }
    else if ( 1 == encoding )
    {
        WCHAR *pwcInput = (WCHAR *) ( data.get() + o );

        if ( 0xfeff == *pwcInput )
        {
            pwcInput++;
            o += 2;
        }

        int wchars = ( maxBytes - o ) / 2;
        memcpy( pwc, pwcInput, 2 * wchars );
        pwc[ wchars ] = 0;
    
        // Convert some well-known Unicode chars to ascii equivalents so they
        // appear correctly in a CMD window
        
        for ( int i = 0; i < wcslen( pwc ); i++ )
        {
            // Right and Left Single Quotation Mark
        
            if ( 0x2019 == pwc[ i ] || 0x2018 == pwc[ i ] )
                pwc[i] = '\'';
        }
    }
    else if ( 2 == encoding )
    {
        // UTF-16. I've yet to find an mp3 file with this
        prf( "MP3 file with encoding 2!\n" );
    }
    else
        prf( "unknown string encoding: %d\n", encoding );

    if ( invalidCharsToDot )
        ConvertInvalidCharsToDot( pwc );
} //ReadMP3String

const char * GetMPEGAudioVersion( int x )
{
    if ( 0 == x )
        return "MPEG Version 2.5";

    if ( 1 == x )
        return "reserved";

    if ( 2 == x )
        return "MPEG Version 2 (ISO/IEC 13818-3)";

    if ( 3 == x )
        return "MPEG Version 1 (ISO/IEC 11172-3)";

    return "unknown";
} //GetMPEGAudioVersion

const char * GetMPEGLayerDescription( int x )
{
    if ( 0 == x )
        return "reserved";

    if ( 1 == x )
        return "Layer III";

    if ( 2 == x )
        return "Layer II";

    if ( 3 == x )
        return "Layer I";

    return "unknown";
} //GetMPEGLayerDescription

const char * GetMPEGProtected( int x )
{
    if ( 0 == x )
        return "Protected by CRC";

    if ( 1 == x )
        return "Not protected by CRC";

    return "unknown";
} //GetMPEGProtected

/*
B   2   (20,19) MPEG Audio version ID
00 - MPEG Version 2.5
01 - reserved
10 - MPEG Version 2 (ISO/IEC 13818-3)
11 - MPEG Version 1 (ISO/IEC 11172-3)
Note: MPEG Version 2.5 is not official standard. Bit No 20 in frame header is used to indicate version 2.5. Applications that do not support this MPEG version expect this bit always to be set, meaning that frame sync (A) is twelve bits long, not eleve 
as stated here. Accordingly, B is one bit long (represents only bit No 19). I recommend using methodology presented here, since this allows you to distinguish all three versions and keep full compatibility.

C   2   (18,17) Layer description
00 - reserved
01 - Layer III
10 - Layer II
11 - Layer I

bits    V1,L1   V1,L2   V1,L3   V2,L1   V2, L2 & L3
0000    free    free    free    free    free
0001    32      32      32      32      8
0010    64      48      40      48      16
0011    96      56      48      56      24
0100    128     64      56      64      32
0101    160     80      64      80      40
0110    192     96      80      96      48
0111    224     112     96      112     56
1000    256     128     112     128     64
1001    288     160     128     144     80
1010    320     192     160     160     96
1011    352     224     192     176     112
1100    384     256     224     192     128
1101    416     320     256     224     144
1110    448     384     320     256     160
1111    bad     bad     bad     bad     bad

// V2 is both V2 and V2.5
*/

int MPEGBitrates[ 16 ][ 5 ]
{
  {  -1,  -1,  -1,  -1,  -1, },
  {  32,  32,  32,  32,   8, },
  {  64,  48,  40,  48,  16, },
  {  96,  56,  48,  56,  24, },
  { 128,  64,  56,  64,  32, },
  { 160,  80,  64,  80,  40, },
  { 192,  96,  80,  96,  48, },
  { 224, 112,  96, 112,  56, },
  { 256, 128, 112, 128,  64, },
  { 288, 160, 128, 144,  80, },
  { 320, 192, 160, 160,  96, },
  { 352, 224, 192, 176, 112, },
  { 384, 256, 224, 192, 128, },
  { 416, 320, 256, 224, 144, },
  { 448, 384, 320, 256, 160, },
  {  -2,  -2,  -2,  -2,  -2, },
};

int MPEGVerLayer( int v, int l )
{
    bool mpeg2 = ( 0 == v || 2 == v ); // MPEG Version 2.5 or 2

    if ( 3 == v && 3 == l )
        return 0;

    if ( 3 == v && 2 == l )
        return 1;

    if ( 3 == v && 1 == l )
        return 2;

    if ( mpeg2 && 3 == l )
        return 3;

    if ( mpeg2 && ( 2 == l || 1 == l ) )
        return 4;

    return -1;
} //MPEGVerLayer

const char * GetMPEGBitrate( int bitrate, int audioVersion, int layerDescription, char * scratch )
{
    int index = MPEGVerLayer( audioVersion, layerDescription );

    if ( -1 == index )
        return "unknown";

    if ( bitrate >= 16 )
        return "unknown";

    int br = MPEGBitrates[ bitrate ][ index ];

    if ( br == -1 )
        return "free";

    if ( br == -2 )
        return "bad";

    sprintf( scratch, "%d", br );
    return scratch;
} //GetMPEGBitrate

const char * GetMPEGSamplerate( int samplerate, int v, char * acScratch )
{
    if ( 0 == samplerate )
    {
        if ( 3 == v )
            return "44100";
        if ( 2 == v )
            return "22050";
        if ( 0 == v )
            return "11025";
    }
    else if ( 1 == samplerate )
    {
        if ( 3 == v )
            return "48000";
        if ( 2 == v )
            return "24000";
        if ( 0 == v )
            return "12000";
    }
    else if ( 2 == samplerate )
    {
        if ( 3 == v )
            return "32000";
        if ( 2 == v )
            return "16000";
        if ( 0 == v )
            return "8000";
    }

    return "unknown";
} //GetMPEGSampleRate

const char * GetMPEGChannelMode( int x )
{
    if ( 0 == x )
        return "stereo";

    if ( 1 == x )
        return "joint stereo (stereo)";

    if ( 2 == x )
        return "dual channel (stereo)";

    return "single channel (mono)";
} //GetMPEGChannelMode

const char * GetMPEGModeExtension( int x, int l )
{
    if ( 2 == l || 3 == l )
    {
        if ( 0 == x )
            return "bands 4 to 31";
        if ( 1 == x )
            return "bands 8 to 31";
        if ( 2 == x )
            return "bands 12 to 31";
        return "bands 16 to 31";
    }
    else if ( 1 == l )
    {
        if ( 0 == x )
            return "intensity stereo off, ms stereo off";
        if ( 1 == x )
            return "intensity stereo on, ms stereo off";
        if( 2 == x )
            return "intensity stereo off, ms stereo on";
        return "intensity stereo on, ms stereo on";
    }

    return "unknown";
} //GetMPEGModeExtension

const char * GetMPEGEmphasis( int x )
{
    if ( 0 == x )
        return "none";

    if ( 1 == x )
        return "50/15 ms";

    if ( 2 == x )
        return "reserved";

    return "CCIT J.17";
} //GetMPEGEmphasis

static const char * g_MP3Genres[]
{
    "Blues",
    "Classic Rock",
    "Country",
    "Dance",
    "Disco",
    "Funk",
    "Grunge",
    "Hip-Hop",
    "Jazz",
    "Metal",
    "New Age",
    "Oldies",
    "Other",
    "Pop",
    "R&B",
    "Rap",
    "Reggae",
    "Rock",
    "Techno",
    "Industrial",
    "Alternative",
    "Ska",
    "Death Metal",
    "Pranks",
    "Soundtrack",
    "Euro-Techno",
    "Ambient",
    "Trip-Hop",
    "Vocal",
    "Jazz+Funk",
    "Fusion",
    "Trance",
    "Classical",
    "Instrumental",
    "Acid",
    "House",
    "Game",
    "Sound Clip",
    "Gospel",
    "Noise",
    "AlternRock",
    "Bass",
    "Soul",
    "Punk",
    "Space",
    "Meditative",
    "Instrumental Pop",
    "Instrumental Rock",
    "Ethnic",
    "Gothic",
    "Darkwave",
    "Techno-Industrial",
    "Electronic",
    "Pop-Folk",
    "Eurodance",
    "Dream",
    "Southern Rock",
    "Comedy",
    "Cult",
    "Gangsta",
    "Top 40",
    "Christian Rap",
    "Pop/Funk",
    "Jungle",
    "Native American",
    "Cabaret",
    "New Wave",
    "Psychadelic",
    "Rave",
    "Showtunes",
    "Trailer",
    "Lo-Fi",
    "Tribal",
    "Acid Punk",
    "Acid Jazz",
    "Polka",
    "Retro",
    "Musical",
    "Rock & Roll",
    "Hard Rock",
    "Folk",
    "Folk-Rock",
    "National Folk",
    "Swing",
    "Fast Fusion",
    "Bebob",
    "Latin",
    "Revival",
    "Celtic",
    "Bluegrass",
    "Avantgarde",
    "Gothic Rock",
    "Progressive Rock",
    "Psychedelic Rock",
    "Symphonic Rock",
    "Slow Rock",
    "Big Band",
    "Chorus",
    "Easy Listening",
    "Acoustic",
    "Humour",
    "Speech",
    "Chanson",
    "Opera",
    "Chamber Music",
    "Sonata",
    "Symphony",
    "Booty Bass",
    "Primus",
    "Porn Groove",
    "Satire",
    "Slow Jam",
    "Club",
    "Tango",
    "Samba",
    "Folklore",
    "Ballad",
    "Power Ballad",
    "Rhythmic Soul",
    "Freestyle",
    "Duet",
    "Punk Rock",
    "Drum Solo",
    "A cappella",
    "Euro-House",
    "Dance Hall",
};

const char * GetMP3Genre( WCHAR const * pwc )
{
    int g = -1;

    if ( ( L'(' == pwc[ 0 ] ) && ( pwc[ 1 ] >= L'0' ) && ( pwc[ 1 ] <= L'9' ) )
        g = _wtoi( pwc + 1 );

    if ( -1 == g )
        return "invalid genre specification";

    if ( g >= 0 && g < _countof( g_MP3Genres ) )
        return g_MP3Genres[ g ];

    return "unknown";
} //GetMP3Genre

const char * GetMP3Genre( int g )
{
    if ( g >= 0 && g < _countof( g_MP3Genres ) )
        return g_MP3Genres[ g ];

    return "unknown";
} //GetMP3Genre

const char * GetMP3SourceFrequency( int x )
{
    if ( 0 == x ) return "32 kHz";
    if ( 1 == x ) return "44.1 kHz";
    if ( 2 == x ) return "48 kHz";
    if ( 3 == x ) return "over 48 kHz";
    return "unknown";
} //GetMP3SourceFrequency

void ParseMP3()
{
    #pragma pack(push, 1)

    __int64 len = g_pStream->Length();
    if ( len < 128 )
        return;

    prf( "mimetype:             audio/mpeg\n" );

    // some mp3 files have no header and start with 11 bits set.
    // other mp3 files have random header info that's not documented anywhere followed by 11 bits set.

    DWORD dwHead = 0;
    GetBytes( 0, &dwHead, sizeof dwHead );
    bool ID3HeaderExists = ( 0x334449 == ( dwHead & 0xffffff ) ); // "ID3"
    int bitstreamOffset = 0;

    if ( ID3HeaderExists )
    {
        struct ID3v2Header
        {
            char id[ 3 ];
            byte ver[ 2 ];
            byte flags;
            DWORD size;
        };
    
        ID3v2Header start;
        memset( &start, 0, sizeof start );
        GetBytes( 0, &start, sizeof start );
        start.size = _byteswap_ulong( start.size );
        start.size = ( ( start.size & 0x7f000000 ) >> 3 ) | ( ( start.size & 0x7f0000 ) >> 2 ) | ( ( start.size & 0x7f00 ) >> 1 ) | ( start.size & 0x7f );
    
        __int64 firstFrameOffset = sizeof start;
        bitstreamOffset = start.size + firstFrameOffset;
    
        #if false // I've not found a file that has this.
            struct ID3v2ExtendedHeader
            {
                DWORD size;
                WORD flags;
                DWORD paddingSize;
            };
        
            ID3v2ExtendedHeader extendedHeader;
            memset( &extendedHeader, 0, sizeof extendedHeader );
            bool extendedHeaderExists = 0 != ( start.flags & 0x20 );
            if ( extendedHeaderExists )
            {
                // untested: I haven't seen a file that uses this code
        
                GetBytes( firstFrameOffset, &extendedHeader, sizeof extendedHeader );
                firstFrameOffset += sizeof extendedHeader;
                prf( "extended MP3 header exists\n" );
            }
        #endif
    
        prf( "ID3 version:          ID3v2.%d.%d\n", start.ver[0], start.ver[1] );
        prf( "  flags:              %#x\n", start.flags );
        prf( "  data size:          %d\n", start.size );
    
        __int64 frameOffset = firstFrameOffset;
    
        struct ID3v23FrameHeader
        {
            char id[4];
            DWORD size;
            WORD flags;
        };
    
        struct ID3v22FrameHeader
        {
            char id[3];
            byte size[3];
        };
    
        ID3v23FrameHeader frameHeader;
        WCHAR awcField[ 8192 ];
    
        while ( frameOffset < ( start.size + firstFrameOffset ) )
        {
            if ( g_FullInformation )
                prf( "reading next frame at offset %I64d\n", frameOffset );
    
            memset( &frameHeader, 0, sizeof frameHeader );
            int frameHeaderSize = sizeof frameHeader;
    
            if ( 2 == start.ver[0] )
            {
                ID3v22FrameHeader frame2;
                memset( &frame2, 0, sizeof frame2 );
                GetBytes( frameOffset, &frame2, sizeof frame2 );
    
                frameHeader.id[0] = frame2.id[0];
                frameHeader.id[1] = frame2.id[1];
                frameHeader.id[2] = frame2.id[2];
                frameHeader.id[3] = 0;
    
                frameHeader.size = frame2.size[2] | ( frame2.size[1] << 8 ) | ( frame2.size[0] << 16 );
                frameHeader.flags = 0;
    
                frameHeaderSize = sizeof frame2;
            }
            else if ( start.ver[0] >= 3 )
            {
                GetBytes( frameOffset, &frameHeader, sizeof frameHeader );
                frameHeader.size = _byteswap_ulong( frameHeader.size );
            }
    
            if ( g_FullInformation )
            {
                prf( "frame header %c%c%c%c\n", frameHeader.id[0], frameHeader.id[1], frameHeader.id[2], frameHeader.id[3] );
                prf( "frame size %d == %#x\n", frameHeader.size, frameHeader.size );
                prf( "frame flags %#x\n", frameHeader.flags );
            }
    
            if ( 0 == frameHeader.size )
                break;
    
            if ( frameHeader.size > 1024 * 1024 * 40 )
            {
                prf( "invalid frame size is too large: %u == %#x\n", frameHeader.size, frameHeader.size );
                break;
            }
    
            if ( !isValidMP3Frame( frameHeader.id ) )
            {
                prf( "invalid frame id %c%c%c%c == %#x %#x %#x %#x\n", frameHeader.id[0], frameHeader.id[1], frameHeader.id[2], frameHeader.id[3],
                        0xff & frameHeader.id[0], 0xff & frameHeader.id[1], 0xff & frameHeader.id[2], 0xff & frameHeader.id[3] );
                break;
            }
    
            if ( g_FullInformation )
                DumpBinaryData( frameOffset + frameHeaderSize, 0, frameHeader.size, 4, frameOffset + frameHeaderSize );
    
            awcField[0] = 0;
    
            if ( ( ( 'T' == frameHeader.id[0] ) && !isMP3Frame( frameHeader.id, "TXXX" ) ) ||
                   ( 'W' == frameHeader.id[0] ) )
                ReadMP3String( frameOffset + frameHeaderSize, awcField, _countof( awcField ), frameHeader.size, false, false );
    
            if ( isMP3Frame( frameHeader.id, "APIC", "PIC" ) )
            {
                // Embedded image (or a link to an image)
                // byte:                           encoding
                // null-terminated ascii:          mime type. iTunes writes JPG or PNG with no null-termination
                // byte:                           picture type
                // null-terminated encoded string: description
                // 
    
                int o = frameOffset + frameHeaderSize;
                int oFrameData = o;
                byte encoding = GetBYTE( o++ );
    
                if ( 0 != encoding && 1 != encoding && 3 != encoding )
                {
                    // 2 == UTF-16 isn't handled because I can't find a file like that to test
    
                    prf( "invalid encoding for image text %d\n", encoding );
                    return;
                }
    
                // mimetype encoding is always ascii
    
                char acMimeType[ 100 ];
                int i = 0;
    
                do
                {
                    if ( i >= ( _countof( acMimeType ) - 1 ) )
                    {
                        prf( "invalid mime type; it may not be null-terminated\n" );
                        return;
                    }

                    char c = GetBYTE( o++ );
                    acMimeType[ i++ ] = c;
                    if ( 0 == c )
                        break;
                } while ( true );
    
                acMimeType[i] = 0;

                // iTunes writes the mimetype as "JPG" or "PNG" with no null-termination sometimes.
                // Hunt for the 0xff then backup o so it points at the picture type
    
                bool isJPG = !strcmp( acMimeType, "JPG" );
                bool isPNG = !strcmp( acMimeType, "PNG" );
        
                if ( isJPG || isPNG )
                {
                    int to = o;
        
                    for ( int z = 0; z < 20; z++ )
                    {
                        WORD w = GetWORD( to + z, true );
        
                        if ( ( ( 0xd8ff == w ) && isJPG ) || ( ( 0x5089 == w ) && isPNG ) )
                        {
                            o = to + z - 2;
                            break;
                        }
                    }
                }
    
                byte pictureType = GetBYTE( o++ );
    
                prf( "Embedded image:       %s\n", acMimeType );
                prf( "  picture type:       %#x == %s\n", 0xff & pictureType, MP3PictureType( pictureType ) );
    
                i = 0;
    
                if ( 0 == encoding )
                {
                    do
                    {
                        WCHAR wc = GetBYTE( o++ );
                        awcField[i++] = wc;
    
                        if ( 0 == wc || i >= ( _countof( awcField ) - 1 ) )
                            break;
                    } while ( true );
                }
                else if ( 1 == encoding )
                {
                    do
                    {
                        WCHAR wc = GetWORD( o, true );
                        o += 2;
    
                        if ( 0xfeff != wc )
                            awcField[i++] = wc;
    
                        if ( 0 == wc || i >= ( _countof( awcField ) - 1 ) )
                            break;
                    } while( true );
                }
                else if ( 3 == encoding )
                {
                    do
                    {
                        WCHAR wc = GetBYTE( o++ );
                        awcField[i++] = wc;
    
                        if ( 0 == wc || i >= ( _countof( awcField ) - 1 ) )
                            break;
                    } while ( true );
                }

                if ( i >= ( _countof( awcField ) ) )
                {
                    prf( "invalid image description; couldn't find a null termination\n" );
                    return;
                }
    
                awcField[ i ] = 0;

                ConvertInvalidCharsToDot( awcField );
                wprf( L"  description:        %ws\n", awcField );
    
                if ( o < ( oFrameData + frameHeader.size ) )
                {
                    int imageSize = frameHeader.size - ( o - oFrameData );
                    prf( "  image size:         %d at file offset %d\n", imageSize, o );
    
                    // Sometimes there are multiple embedded images. Use the first with a reasonable size.
    
                    if ( 0 == g_Embedded_Image_Offset && 0 == g_Embedded_Image_Length && imageSize > 1000 )
                    {
                        bool isAnImage = IsPerhapsAnImage( o, 0 );
    
                        if ( isAnImage )
                        {
                            g_Embedded_Image_Offset = o;
                            g_Embedded_Image_Length = imageSize;
                        }
                    }
                }
                else if ( g_FullInformation )
                    prf( "invalid image offset %d is beyond size of the frame %d\n", o, frameHeader.size );
            }
            else if ( isMP3Frame( frameHeader.id, "COMM", "COM" ) )
            {
                // encoding:          byte
                // language:          byte byte byte
                // short description: null-terminated string
                // text:              non-null terminated string to end of size
    
                ReadMP3String( frameOffset + frameHeaderSize, awcField, _countof( awcField ), frameHeader.size, true, true );
                if ( 0 != awcField[0] )
                    wprf( L"Comments:             %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "GEOB" ) )
            {
                // general encapsulated object
            }
            else if ( isMP3Frame( frameHeader.id, "MCDI" ) )
            {
                // music cd identifier
            }
            else if ( isMP3Frame( frameHeader.id, "MJCF" ) )
            {
            }
            else if ( isMP3Frame( frameHeader.id, "PCNT" ) )
            {
                // player count
            }
            else if ( isMP3Frame( frameHeader.id, "PCST", "PCS" ) )
            {
                wprf( L"Podcast:              %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "POPM" ) )
            {
                // popularimeter
            }
            else if ( isMP3Frame( frameHeader.id, "PRIV" ) )
            {
                // private frame
            }
            else if ( isMP3Frame( frameHeader.id, "TALB", "TAL" ) )
            {
                wprf( L"Album:                %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TBPM", "TBP" ) )
            {
                wprf( L"Beats per minute:     %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TCMP", "TCP" ) )
            {
                wprf( L"Compilation:          %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TCOM", "TCM" ) )
            {
                wprf( L"Composer:             %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TCON", "TCO" ) )
            {
                wprf( L"Genre:                %ws", awcField );
                if ( L'(' == awcField[0] )
                    prf( " == %s\n", GetMP3Genre( awcField ) );
                else
                    prf( "\n" );
            }
            else if ( isMP3Frame( frameHeader.id, "TCOP" ) )
            {
                wprf( L"Copyright:            %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TDAT", "TDA" ) )
            {
                wprf( L"Date:                 %wc%wc/%wc%wc\n", awcField[0], awcField[1], awcField[2], awcField[3] );
            }
            else if ( isMP3Frame( frameHeader.id, "TDEN" ) )
            {
                wprf( L"Encoding time:        %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TDRC" ) )
            {
                wprf( L"Recording time:       %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TDRL" ) )
            {
                wprf( L"Release time:         %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TENC", "TEN" ) )
            {
                wprf( L"Encoded by:           %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TIME", "TIM" ) )
            {
                wprf( L"Time:                 %wc%wc:%wc%wc\n", awcField[0], awcField[1], awcField[2], awcField[3] );
            }
            else if ( isMP3Frame( frameHeader.id, "TIT1", "TT1" ) )
            {
                wprf( L"Content group:        %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TIT2", "TT2" ) )
            {
                wprf( L"Title:                %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TIT3", "TT3" ) )
            {
                wprf( L"Subtitle:             %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TLAN" ) )
            {
                wprf( L"Languages:            %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TLEN" ) )
            {
                wprf( L"Length:               %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TMED" ) )
            {
                wprf( L"Media type:           %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TOFN" ) )
            {
                wprf( L"Original filename:    %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TOPE" ) )
            {
                wprf( L"Original artist:      %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TORY" ) )
            {
                wprf( L"Original release:    %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TOWN" ) )
            {
                wprf( L"File owner:           %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TPE1", "TP1" ) )
            {
                wprf( L"Lead performer(s):    %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TPE2", "TP2" ) )
            {
                wprf( L"Band:                 %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TPE3", "TP3" ) )
            {
                wprf( L"Conductor:            %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TPOS", "TPA" ) )
            {
                wprf( L"Part of set:          %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TPUB" ) )
            {
                wprf( L"Publisher:            %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TRCK", "TRK" ) )
            {
                wprf( L"Track:                %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TRDA" ) )
            {
                wprf( L"Recording dates:      %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TS2" ) )
            {
                wprf( L"Artist (TS2):         %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TSA" ) )
            {
                wprf( L"Album (TSA):          %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TSO2" ) )
            {
                wprf( L"iTunes artist sort:   %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TSOA" ) )
            {
                wprf( L"Album sort order:     %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TSOP" ) )
            {
                wprf( L"Performer sort order: %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TSP" ) )
            {
                wprf( L"Artist (TSP):         %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TSOT" ) )
            {
                wprf( L"Title sort order:     %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TSRC" ) )
            {
                wprf( L"ISRC:                 %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TSSE", "TSS" ) )
            {
                wprf( L"Encoding software:    %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TST" ) )
            {
                wprf( L"Title Sort Order :    %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TXXX" ) )
            {
            }
            else if ( isMP3Frame( frameHeader.id, "TYER", "TYE" ) )
            {
                wprf( L"Year:                 %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "UFID", "UFI" ) )
            {
                // unique file identifier
            }
            else if ( isMP3Frame( frameHeader.id, "USLT", "ULT" ) )
            {
                // byte:            encoding
                // byte[3]:         language
                // string           description
                // string           lyrics

                ReadMP3String( frameOffset + frameHeaderSize, awcField, _countof( awcField ), frameHeader.size, true, true, false );

                if ( wcschr( awcField, L'\n' ) || wcschr( awcField, L'\r' ) || wcschr( awcField, 0xa ) )
                    wprf( L"Lyrics:\n%ws\n", awcField );
                else
                    wprf( L"Lyrics:               %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "WFED" ) )
            {
                wprf( L"Feed webpage:         %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "WOAR", "WAR" ) )
            {
                wprf( L"Artist webpage:       %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "WOAF", "WAF" ) )
            {
                wprf( L"Official webpage:     %ws\n", awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "WXXX", "WXX" ) )
            {
                wprf( L"Webpage:              %ws\n", awcField );
            }
            else
            {
                prf( "unknown frame id %c%c%c%c == %#x %#x %#x %#x\n", frameHeader.id[0], frameHeader.id[1], frameHeader.id[2], frameHeader.id[3],
                        0xff & frameHeader.id[0], 0xff & frameHeader.id[1], 0xff & frameHeader.id[2], 0xff & frameHeader.id[3] );
            }
    
            frameOffset += ( frameHeaderSize + frameHeader.size );
    
            if ( frameOffset >= g_pStream->Length() )
            {
                prf( "invalid frame offset is beyond the end of the file %#I64x\n", frameOffset );
                break;
            }
    
            // the >= case will be caught in the while loop above; no need to break
    
            if ( frameOffset > ( start.size + firstFrameOffset ) )
            {
                prf( "invalid frame offset %I64d is beyond the size in the header + firstFrame %I64d\n", frameOffset, start.size + firstFrameOffset );
            }
        }
    }
    else
    {
        // There is apparently no modern MP3 tag data, so look for the old 128 bytes at the end of the file

        struct TagsEnd
        {
            char tag[ 3 ];
            char name[ 30 ];
            char artist[ 30 ];
            char album[ 30 ];
            char year [ 4 ];
            char comment[ 30 ];
            char genre;
        };
    
        TagsEnd end;
        memset( &end, 0, sizeof end );
        GetBytes( len - 128, &end, sizeof end );
    
        bool endTagExists = ( 'T' == end.tag[0] && 'A' == end.tag[1] && 'G' == end.tag[2] );

        if ( endTagExists )
        {
            //printf( "MP3 end of file tags:\n" );
            prf( "song name:            %.30s\n", end.name );
            prf( "artist:               %.30s\n", end.artist );
            prf( "album:                %.30s\n", end.album );
            prf( "year:                 %c%c%c%c\n", end.year[0], end.year[1], end.year[2], end.year[3] );
            prf( "comment:              %.30s\n", end.comment );

            if ( 0 == end.comment[ 28 ] && 0 != end.comment[29 ] )
                prf( "track:                %d\n", 0xff & end.comment[29] );

            if ( -1 != end.genre && 0 != end.genre ) // this throws away "blues", but it's usually incorrect anyway.
                prf( "genre:                %d == %s\n", end.genre, GetMP3Genre( end.genre ) );
        }
    }

    //printf( "bitstreamOffset: %d\n", bitstreamOffset );

    // The 11 consecutive bits turned on signify the start of a compressed audio chunk. The are
    // generally just after the TAG header and data, but sometimes they are just beyond.
    // Most files have the bitstream at the expected location.
    // Most of the remaining have the bistream within 100 bytes of the expected location.
    // Most of the remaining of that have it within 1024.
    // All of my mp3 files aside from Radiohead the king of limbs are within 8192.
    // Since the search rarely happens and it's safe, allow such a long search.
    // http://www.multiweb.cz/twoinches/mp3inside.htm

    const int searchLen = 8192;
    int delta = 0;

    for ( delta = 0; delta < searchLen; delta++ )
    {
        WORD h = GetWORD( bitstreamOffset + delta, false );
        //printf( "h at delta %d: %#x\n", delta, h );
        if ( ( 0xffe0 & h ) == 0xffe0 )
            break;
    }

    if ( delta == searchLen )
    {
        prf( "can't find bitstream header pattern 0xffe0\n" );
        return;
    }

    if ( delta > 0 )
        prf( "found bitstream header %d bytes from end of TAG set\n", delta );

    // check for Variable Bitrate
    
    struct VBRInfo
    {
        DWORD header;            // For many older MP3 files, this header is the only valid data
        byte unused4[ 9 ];
        DWORD xing13;
        DWORD unused17;
        DWORD xing21;
        byte unused25[11];
        DWORD xing36;
        DWORD flags40;
        DWORD frames44;
        DWORD len48;
        byte toc52[100];
        DWORD scale152;

        char      m_lame_version[9];       // "LAME<major>.<minor><release>
        uint8_t   m_revision : 4;
        uint8_t   m_vbr_type : 4;
        uint8_t   m_lowpass_frequency;
        uint32_t  m_peak_signal;           // 9.23 fixed point
        uint16_t  m_radio_replay_pad : 2;
        uint16_t  m_radio_replay_set_name : 2;
        uint16_t  m_radio_replay_originator_code : 2;
        uint16_t  m_radio_replay_gain : 10;
        uint16_t  m_audiophile_replay_gain;
        uint8_t   m_flag_ath_type : 4;
        uint8_t   m_flag_expn_psy_tune : 1;
        uint8_t   m_flag_safe_joint : 1;
        uint8_t   m_flag_no_gap_more : 1;
        uint8_t   m_flag_no_gap_previous : 1;
        uint8_t   m_average_bit_rate;
        uint8_t   m_delay_padding_delay_high;
        uint8_t   m_delay_padding_delay_low : 4;
        uint8_t   m_delay_padding_padding_high : 4;
        uint8_t   m_delay_padding_padding_low;
        uint8_t   m_noise_shaping : 2;
        uint8_t   m_stereo_mode : 3;
        uint8_t   m_non_optimal : 1;
        uint8_t   m_source_frequency : 2;
        uint8_t   m_unused;                // set to 0
        uint16_t  m_preset;
        uint32_t  m_music_length;
        uint16_t  m_music_crc16;
        uint16_t  m_crc16;                 // if (protection bit)
    };
    
    VBRInfo vbr;
    GetBytes( bitstreamOffset + delta, &vbr, sizeof vbr );
    vbr.header = _byteswap_ulong( vbr.header );
    vbr.flags40 = _byteswap_ulong( vbr.flags40 );
    vbr.frames44 = _byteswap_ulong( vbr.frames44 );
    vbr.len48 = _byteswap_ulong( vbr.len48 );
    vbr.scale152 = _byteswap_ulong( vbr.scale152 );
    vbr.m_music_length = _byteswap_ulong( vbr.m_music_length );

    DWORD h = vbr.header;
    prf( "MPEGHeader:           %#x == ", h );
    for ( int i = 31; i > 0; i-- )
        prf( 0 != ( ( h >> i ) & 0x1 ) ? "1" : "0" );
    prf( "\n" );

    if ( 0xffe00000 == ( h & 0xffe00000 ) )
    {
        int audioVersion = ( h >> 19 ) & 0x3;
        prf( "MPEG audio Version:  %2d == %s\n", audioVersion, GetMPEGAudioVersion( audioVersion ) );
    
        int layerDescription = ( h >> 17 ) & 0x3;
        prf( "Layer:               %2d == %s\n", layerDescription, GetMPEGLayerDescription( layerDescription ) );
    
        const DWORD xing = 0x676e6958;  // "Xing"
        bool variableBitrate = false;
    
        if ( 3 == audioVersion && xing == vbr.xing36 )
        {
            variableBitrate = true;
            prf( "Variable bitrate:     xing for mpeg1 and channel != mono, flags %#x, xing offset %d\n", vbr.flags40, bitstreamOffset + delta + 36 );
    
            if ( vbr.flags40 & 0x1 )
                prf( "  VBR frames:         %d\n", vbr.frames44 );
    
            if ( vbr.flags40 & 0x2 )
                prf( "  VBR bytes:          %d\n", vbr.len48 );
    
            if ( vbr.flags40 & 0x8 )
                prf( "  VBR scale:          %d\n", vbr.scale152 );

            if ( 0 != vbr.m_lame_version[0] )
            {
                prf( "  Encoder:            " );
                for ( int x = 0; x < 9; x++ )
                {
                    if ( 0 == vbr.m_lame_version[x] )
                        break;
                    prf( "%c", vbr.m_lame_version[x] );
                }
                prf( "\n" );

                //printf( "  Encoder:            %.9s\n", vbr.m_lame_version );
                prf( "  Revision:           %d\n", vbr.m_revision );
                prf( "  VBR type:           %d\n", vbr.m_vbr_type );
                prf( "  Lowpass frequency:  %3.1lf kHz\n", (double) vbr.m_lowpass_frequency * 100.0 / 1000.0 );
                prf( "  Peak signal:        %#x\n", vbr.m_peak_signal );
                prf( "  Average bitrate:    %d kbps\n", vbr.m_average_bit_rate );
                prf( "  Source frequency:   %s\n", GetMP3SourceFrequency( vbr.m_source_frequency ) );
                prf( "  Audio bytes:        %d\n", vbr.m_music_length );
    
                if ( vbr.m_flag_no_gap_more )
                    prf( "  No-gap next:        %d\n", vbr.m_flag_no_gap_more );
                if ( vbr.m_flag_no_gap_previous )
                    prf( "  No-gap previous:    %d\n", vbr.m_flag_no_gap_previous );
            }
        }
        else if ( 3 == audioVersion && xing == vbr.xing21 )
            prf( "xing for mpeg1 and channel == mono\n" );
        else if ( 2 == audioVersion && xing == vbr.xing21 )
            prf( "xing for mpeg2 and channel != mono\n" );
        else if ( 2 == audioVersion && xing == vbr.xing13 )
            prf( "xing for mpeg2 and channel == mono\n" );
    
        int protectedBit = ( h >> 16 ) & 0x1;
        prf( "Protected:           %2d == %s\n", protectedBit, GetMPEGProtected( protectedBit ) );
    
        int bitrate = ( h >> 12 ) & 0xf;
        char acScratch[ 10 ];
        char const * pcBitrate = GetMPEGBitrate( bitrate, audioVersion, layerDescription, acScratch );
        prf( "Bitrate:             %2d == %s kbps\n", bitrate, variableBitrate ? "variable" : pcBitrate );
    
        int samplerate = ( h >> 10 ) & 0x3;
        char const * pcSamplerate = GetMPEGSamplerate( samplerate, audioVersion, acScratch );
        prf( "Sample rate:         %2d == %s Hz\n", samplerate, pcSamplerate );
    
        int channelmode = ( h >> 6 ) & 0x3;
        prf( "Channel mode:        %2d == %s\n", channelmode, GetMPEGChannelMode( channelmode ) );
    
        int modeextension = ( h >> 4 ) & 0x3;
        prf( "Mode extension:      %2d == %s\n", modeextension, GetMPEGModeExtension( modeextension, layerDescription ) );
    
        int copyright = ( h >> 3 ) & 0x1;
        prf( "Copyright:           %2d == %s\n", copyright, copyright ? "true" : "false" );
    
        int original = ( h >> 2 ) & 0x1;
        prf( "Original media:      %2d == %s\n", original, original ? "true" : "false" );
    
        int emphasis = h & 0x3;
        prf( "Emphasis:            %2d == %s\n", emphasis, GetMPEGEmphasis( emphasis ) );
    }
    else
    {
        prf( "top 11 bits of frame sync aren't set; metadata is suspect\n" );
    }

    #pragma pack(pop)
} //ParseMP3

const GUID ASF_Header_Object =                       { 0x75B22630, 0x668E, 0x11CF, 0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C };
const GUID ASF_Data_Object =                         { 0x75B22636, 0x668E, 0x11CF, 0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C };
const GUID ASF_Simple_Index_Object =                 { 0x33000890, 0xE5B1, 0x11CF, 0x89, 0xF4, 0x00, 0xA0, 0xC9, 0x03, 0x49, 0xCB };
const GUID ASF_Index_Object =                        { 0xD6E229D3, 0x35DA, 0x11D1, 0x90, 0x34, 0x00, 0xA0, 0xC9, 0x03, 0x49, 0xBE };
const GUID ASF_Media_Object_Index_Object =           { 0xFEB103F8, 0x12AD, 0x4C64, 0x84, 0x0F, 0x2A, 0x1D, 0x2F, 0x7A, 0xD4, 0x8C };
const GUID ASF_Timecode_Index_Object =               { 0x3CB73FD0, 0x0C4A, 0x4803, 0x95, 0x3D, 0xED, 0xF7, 0xB6, 0x22, 0x8F, 0x0C };
                                                     
const GUID ASF_File_Properties_Object =              { 0x8CABDCA1, 0xA947, 0x11CF, 0x8E, 0xE4, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65 };
const GUID ASF_Stream_Properties_Object =            { 0xB7DC0791, 0xA9B7, 0x11CF, 0x8E, 0xE6, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65 };
const GUID ASF_Header_Extension_Object =             { 0x5FBF03B5, 0xA92E, 0x11CF, 0x8E, 0xE3, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65 };
const GUID ASF_Codec_List_Object =                   { 0x86D15240, 0x311D, 0x11D0, 0xA3, 0xA4, 0x00, 0xA0, 0xC9, 0x03, 0x48, 0xF6 };
const GUID ASF_Script_Command_Object =               { 0x1EFB1A30, 0x0B62, 0x11D0, 0xA3, 0x9B, 0x00, 0xA0, 0xC9, 0x03, 0x48, 0xF6 };
const GUID ASF_Marker_Object =                       { 0xF487CD01, 0xA951, 0x11CF, 0x8E, 0xE6, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65 };
const GUID ASF_Bitrate_Mutual_Exclusion_Object =     { 0xD6E229DC, 0x35DA, 0x11D1, 0x90, 0x34, 0x00, 0xA0, 0xC9, 0x03, 0x49, 0xBE };
const GUID ASF_Error_Correction_Object =             { 0x75B22635, 0x668E, 0x11CF, 0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C };
const GUID ASF_Content_Description_Object =          { 0x75B22633, 0x668E, 0x11CF, 0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C };
const GUID ASF_Extended_Content_Description_Object = { 0xD2D0A440, 0xE307, 0x11D2, 0x97, 0xF0, 0x00, 0xA0, 0xC9, 0x5E, 0xA8, 0x50 };
const GUID ASF_Content_Branding_Object =             { 0x2211B3FA, 0xBD23, 0x11D2, 0xB4, 0xB7, 0x00, 0xA0, 0xC9, 0x55, 0xFC, 0x6E };
const GUID ASF_Stream_Bitrate_Properties_Object =    { 0x7BF875CE, 0x468D, 0x11D1, 0x8D, 0x82, 0x00, 0x60, 0x97, 0xC9, 0xA2, 0xB2 };
const GUID ASF_Content_Encryption_Object =           { 0x2211B3FB, 0xBD23, 0x11D2, 0xB4, 0xB7, 0x00, 0xA0, 0xC9, 0x55, 0xFC, 0x6E };
const GUID ASF_Extended_Content_Encryption_Object =  { 0x298AE614, 0x2622, 0x4C17, 0xB9, 0x35, 0xDA, 0xE0, 0x7E, 0xE9, 0x28, 0x9C };
const GUID ASF_Digital_Signature_Object =            { 0x2211B3FC, 0xBD23, 0x11D2, 0xB4, 0xB7, 0x00, 0xA0, 0xC9, 0x55, 0xFC, 0x6E };

const char * GetAsfGuid( GUID & id )
{
    if ( ASF_Header_Object == id )
        return "ASF_Header_Object";

    if ( ASF_Data_Object == id )
        return "ASF_Data_Object";

    if ( ASF_Simple_Index_Object == id )
        return "ASF_Simple_Index Object";

    if ( ASF_Index_Object == id )
        return "ASF_Index_Object";

    if ( ASF_Media_Object_Index_Object == id )
        return "ASF_Media_Object_Index Object";

    if ( ASF_Timecode_Index_Object == id )
        return "ASF_Timecode_Index_Object";

    if ( ASF_File_Properties_Object == id )
        return "ASF_File_Properties_Object";

    if ( ASF_Stream_Properties_Object == id )
        return "ASF_Stream_Properties_Object";

    if ( ASF_Header_Extension_Object == id )
        return "ASF_Header_Extension_Object";

    if ( ASF_Codec_List_Object == id )
        return "ASF_Codec_List_Object";

    if ( ASF_Script_Command_Object == id )
        return "ASF_Script_Command_Object";

    if ( ASF_Marker_Object == id )
        return "ASF_Marker_Object";

    if ( ASF_Bitrate_Mutual_Exclusion_Object == id )
        return "ASF_Bitrate_Mutual_Exclusion_Object";

    if ( ASF_Error_Correction_Object == id )
        return "ASF_Error_Correction_Object";

    if ( ASF_Content_Description_Object == id )
        return "ASF_Content_Description_Object";

    if ( ASF_Extended_Content_Description_Object == id )
        return "ASF_Extended_Content_Description_Object";

    if ( ASF_Content_Branding_Object == id )
        return "ASF_Content_Branding_Object";

    if ( ASF_Stream_Bitrate_Properties_Object == id )
        return "ASF_Stream_Bitrate_Properties_Object";

    if ( ASF_Content_Encryption_Object == id )
        return "ASF_Content_Encryption_Object";

    if ( ASF_Extended_Content_Encryption_Object == id )
        return "ASF_Extended_Content_Encryption_Object";

    if ( ASF_Digital_Signature_Object == id )
        return "ASF_Digital_Signature_Object";

    return "unknown";
} //GetAsfGuid

void PrintAsfGuid( const char * pcPrefix, GUID & id, QWORD size )
{
    prf( "%s{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x} == %s, size %I64d == %#I64x\n",
            pcPrefix,
            id.Data1,
            id.Data2,
            id.Data3,
            id.Data4[0], id.Data4[1],
            id.Data4[2], id.Data4[3],
            id.Data4[4], id.Data4[5],
            id.Data4[6], id.Data4[7],
            GetAsfGuid( id ),
            size,
            size );
} //PrintAsfGuid

// WMA, WMV, etc.
// note: as far as I can tell, WMA files never have an embedded image.

void ParseAsf()
{
    #pragma pack(push, 1)

    struct AsfObject
    {
        GUID  id;
        QWORD size;
    };

    struct AsfHeaderObject : AsfObject
    {
        DWORD numObjects;
        BYTE  reserved1;
        BYTE  reserved2;
    };

    struct AsfFilePropertiesObject : AsfObject
    {
        GUID fileID;
        QWORD size;
        QWORD creationDate;
        QWORD dataPacketsCount;
        QWORD playDuration;
        QWORD sendDuration;
        QWORD preroll;
        DWORD flags;
        DWORD minDataPacketSize;
        DWORD maxDataPacketSize;
        DWORD maxBitrate;
    };

    struct AsfContentDescriptionObject : AsfObject
    {
        WORD titleLen;
        WORD authorLen;
        WORD copyrightLen;
        WORD descriptionLen;
        WORD ratingLen;
    };

    struct AsfHeaderExtensionObject : AsfObject
    {
        GUID reserved1;
        WORD reserved2;
        DWORD dataSize;
    };

    __int64 len = g_pStream->Length();
    if ( len < 128 )
    {
        prf( "ASF file is too small\n" );
        return;
    }

    DWORD dwHead = 0;
    GetBytes( 0, &dwHead, sizeof dwHead );

    if ( 0x75b22630 != dwHead )
    {
        prf( "can't find an ASF header (0x75b22630 expected): %#x\n", dwHead );
        return;
    }

    WCHAR awcBuf[ 200 ];
    __int64 frameOffset = 0;

    while ( frameOffset < len )
    {
        if ( g_FullInformation )
            prf( "reading next frame at offset %I64d\n", frameOffset );

        AsfObject obj;
        GetBytes( frameOffset, &obj, sizeof obj );
    
        PrintAsfGuid( "", obj.id, obj.size );
    
        if ( 0 == obj.size )
            break;
    
        if ( obj.size > 1024 * 1024 * 40 )
        {
            prf( "invalid frame size is too large: %I64u == %#I64x\n", obj.size, obj.size );
            return;
        }

        if ( ASF_Header_Object == obj.id )
        {

            AsfHeaderObject headobj;
            GetBytes( frameOffset, &headobj, sizeof headobj );

            prf( "header object count: %d\n", headobj.numObjects );

            __int64 offset = frameOffset + sizeof headobj;

            for ( DWORD i = 0; i < headobj.numObjects; i++ )
            {
                AsfObject headerObject;
                GetBytes( offset, &headerObject, sizeof headerObject );

                PrintAsfGuid( "  ", headerObject.id, headerObject.size );

                if ( ASF_Content_Description_Object == headerObject.id )
                {
                    AsfContentDescriptionObject cdo;
                    GetBytes( offset, &cdo, sizeof cdo );

                    if ( g_FullInformation )
                    {
                        prf( "    title length: %u\n", cdo.titleLen );
                        prf( "    author length: %u\n", cdo.authorLen );
                        prf( "    copyright length: %u\n", cdo.copyrightLen );
                        prf( "    description length: %u\n", cdo.descriptionLen );
                        prf( "    rating length: %u\n", cdo.ratingLen );
                    }

                    if ( ( cdo.titleLen >= 0 ) && ( cdo.titleLen < ( sizeof awcBuf - 2 ) ) )
                    {
                        GetBytes( offset + sizeof cdo, awcBuf, cdo.titleLen );
                        awcBuf[ cdo.titleLen / 2 ] = 0;
                        wprf( L"    %-30s: %ws\n", "Title", awcBuf );
                    }

                    if ( ( cdo.authorLen >= 0 ) && ( cdo.authorLen < ( sizeof awcBuf - 2 ) ) )
                    {
                        GetBytes( offset + sizeof cdo + cdo.titleLen, awcBuf, cdo.authorLen );
                        awcBuf[ cdo.authorLen / 2 ] = 0;
                        wprf( L"    %-30s: %ws\n", "Author", awcBuf );
                    }
                }
                else if ( ASF_Extended_Content_Description_Object == headerObject.id )
                {
                    __int64 ecdOffset = offset + sizeof headerObject;
                    WORD cdCount = GetWORD( ecdOffset, true );
                    ecdOffset += sizeof WORD;

                    prf( "    extended content count        : %u\n", cdCount );

                    for ( WORD cd = 0; cd < cdCount; cd++ )
                    {
                        WORD namelen = GetWORD( ecdOffset, true );
                        ecdOffset += sizeof WORD;

                        if ( g_FullInformation )
                            prf( "      descriptor name length: %u\n", namelen );

                        unique_ptr<byte> name( new byte[ 2 + namelen ] );
                        GetBytes( ecdOffset, name.get(), namelen );
                        ecdOffset += namelen;

                        WCHAR * pwcName = (WCHAR *) name.get();
                        pwcName[ namelen / 2 ] = 0;
                        if ( g_FullInformation )
                            wprf( L"      name: %ws\n", pwcName );

                        WORD valuetype = GetWORD( ecdOffset, true );
                        ecdOffset += sizeof WORD;

                        WORD valuelen = GetWORD( ecdOffset, true );
                        ecdOffset += sizeof WORD;

                        unique_ptr<byte> value( new byte[ 2 + valuelen ] );
                        GetBytes( ecdOffset, value.get(), valuelen );
                        ecdOffset += valuelen;

                        if ( g_FullInformation )
                            prf( "      value type %u, length %u\n", valuetype, valuelen );

                        prf( "    %-30ws: ", pwcName );

                        if ( 0 == valuetype )
                        {
                            WCHAR * pwcValue = (WCHAR *) value.get();
                            pwcValue[ valuelen / 2 ] = 0;
                            wprf( L"%ws\n", pwcValue );
                        }
                        else if ( 1 == valuetype )
                        {
                            prf( "\n" );
                            DumpBinaryData( ecdOffset - valuelen, 0, valuelen, 6, ecdOffset - valuelen );
                        }
                        else if ( 2 == valuetype )
                        {
                            BOOL x = * ( BOOL * ) value.get();
                            prf( "%s\n", x ? "true" : "false" );
                        }
                        else if ( 3 == valuetype )
                        {
                            DWORD x = * ( DWORD * ) value.get();
                            prf( "%u\n", x );
                        }
                        else if ( 4 == valuetype )
                        {
                            QWORD x = * ( QWORD * ) value.get();
                            prf( "%I64u\n", x );
                        }
                        else if ( 5 == valuetype )
                        {
                            WORD x = * ( WORD * ) value.get();
                            prf( "%u\n", x );
                        }
                    }
                }
                else if ( ASF_Header_Extension_Object == headerObject.id )
                {
                    AsfHeaderExtensionObject ext;
                    GetBytes( offset, &ext, sizeof ext );

                    prf( "    Header Extension Object size %u\n", ext.dataSize );
                    DumpBinaryData( offset + sizeof ext, 0, ext.dataSize, 6, offset + sizeof ext );
                }
                else if ( ASF_File_Properties_Object == headerObject.id )
                {
                    AsfFilePropertiesObject props;
                    GetBytes( offset, &props, sizeof props );

                    QWORD durationSecs = props.playDuration / 10000000;
                    prf( "    %-30s: %I64u seconds\n", "Duration", durationSecs );
                    prf( "    %-30s: %u bits per second\n", "Maximum Bitrate", props.maxBitrate );
                }
                else if ( ASF_Stream_Bitrate_Properties_Object == headerObject.id )
                {
                    __int64 o = offset + sizeof AsfObject;
                    WORD recordCount = GetWORD( o, true );
                    o += sizeof WORD;
        
                    //printf( "Bitrate for %u streams\n", recordCount );
        
                    for ( WORD s = 0; s < recordCount; s++ )
                    {
                        WORD flags = GetWORD( o, true );
                        o += sizeof WORD;
                        DWORD averageBitrate = GetDWORD( o, true );
                        o += sizeof DWORD;
        
                        prf( "    %-30s: %u\n", "Stream Number", flags & 0xef );
                        prf( "    %-30s: %u bits per second\n", "Average Bitrate", averageBitrate );
                    }
                }

                offset += headerObject.size;
            }
        }

        frameOffset += obj.size;
    }

    #pragma pack(pop)
} //ParseAsf

const char * WavFormatType( DWORD format )
{
    if ( 1 == format )
        return "PCM";

    if ( 3 == format )
        return "IEEE float";

    if ( 6 == format )
        return "8-bit ITU-T G.711 A-law";

    if ( 7 == format )
        return "8-bit ITU-T G.711 æ-law";

    if ( 0xfffe == ( format & 0xffff ) )
        return "Extensible (Subformat)";

    return "unknown";
} //WavFormatType

#include <initguid.h>

// 454E4F4E-0000-0010-8000-00AA00389B71
DEFINE_GUID(MEDIASUBTYPE_PCM_NONE,
                        0x454E4F4E, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71);

// 20776172-0000-0010-8000-00AA00389B71
DEFINE_GUID(MEDIASUBTYPE_PCM_RAW,
                        0x20776172, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71);

// 736f7774-0000-0010-8000-00AA00389B71
DEFINE_GUID(MEDIASUBTYPE_PCM_TWOS,
                        0x736f7774, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71);

// 74776f73-0000-0010-8000-00AA00389B71
DEFINE_GUID(MEDIASUBTYPE_PCM_SOWT,
                        0x74776f73, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71);

// 34326E69-0000-0010-8000-00AA00389B71 (big-endian int24)
DEFINE_GUID(MEDIASUBTYPE_PCM_IN24,
                        0x34326E69, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71);

// 32336E69-0000-0010-8000-00AA00389B71 (big-endian int32)
DEFINE_GUID(MEDIASUBTYPE_PCM_IN32,
                        0x32336E69, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71);

// 32336C66-0000-0010-8000-00AA00389B71 (big-endian float32)
DEFINE_GUID(MEDIASUBTYPE_PCM_FL32,
                        0x32336C66, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71);

// 34366C66-0000-0010-8000-00AA00389B71 (big-endian float64)
DEFINE_GUID(MEDIASUBTYPE_PCM_FL64,
                        0x34366C66, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71);

// Reverse GUIDs for little-endian 'in24', 'in32', 'fl32', 'fl64'
// 696E3234-0000-0010-8000-00AA00389B71 (little-endian int24, reverse 'in24')
DEFINE_GUID(MEDIASUBTYPE_PCM_IN24_le,
                        0x696E3234, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71);
// 696E3332-0000-0010-8000-00AA00389B71 (little-endian int32, reverse 'in32')
DEFINE_GUID(MEDIASUBTYPE_PCM_IN32_le,
                        0x696E3332, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71);
// 666C3332-0000-0010-8000-00AA00389B71 (little-endian float32, reverse 'fl32')
DEFINE_GUID(MEDIASUBTYPE_PCM_FL32_le,
                        0x666C3332, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71);
// 666C3634-0000-0010-8000-00AA00389B71 (little-endian float64, reverse 'fl64')
DEFINE_GUID(MEDIASUBTYPE_PCM_FL64_le,
                        0x666C3634, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71);

const char * WavExtendedFormatType( GUID & guid )
{
    if ( MEDIASUBTYPE_PCM == guid )
        return "MEDIASUBTYPE_PCM";

    if ( MEDIASUBTYPE_IEEE_FLOAT == guid )
        return "MEDIASUBTYPE_IEEE_FLOAT";

    if ( MEDIASUBTYPE_PCM_FL64 == guid )
        return "MEDIASUBTYPE_PCM_FL64";

    if ( MEDIASUBTYPE_PCM_FL64_le == guid )
        return "MEDIASUBTYPE_PCM_FL64_le";

    if ( MEDIASUBTYPE_PCM_FL32 == guid )
        return "MEDIASUBTYPE_PCM_FL32";

    if ( MEDIASUBTYPE_PCM_FL32_le == guid )
        return "MEDIASUBTYPE_PCM_FL32_le";

    if ( MEDIASUBTYPE_PCM_IN32 == guid )
        return "MEDIASUBTYPE_PCM_IN32";

    if ( MEDIASUBTYPE_PCM_IN32_le == guid )
        return "MEDIASUBTYPE_PCM_IN32_le";

    if ( MEDIASUBTYPE_PCM_IN24 == guid )
        return "MEDIASUBTYPE_PCM_IN24";

    if ( MEDIASUBTYPE_PCM_IN24_le == guid )
        return "MEDIASUBTYPE_PCM_IN24_le";

    return "unknown";
} //WavExtendedFormatType

void PrintGUID( GUID & guid )
{
    prf( "Guid = {%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX}",
            guid.Data1, guid.Data2, guid.Data3,
            guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
            guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7] );
} //PrintGUID

void ParseWav()
{
    #pragma pack(push, 1)

    struct WavHeader
    {
        DWORD riff;               // "RIFF"
        DWORD size;               // size of file minus 8 bytes
        DWORD wave;               // "WAVE"
    };

    struct WavSubchunk
    {
        DWORD format;             // e.g. "fmt" or "JUNK" or "bext"
        DWORD formatSize;         // size of format data
        WORD  formatType;         // 1 == PCM
        WORD  channels;           // count of channels
        DWORD sampleRate;         // e.g. 44100 samples per second
        DWORD dataRate;           // ( sampleRate * bitsPerSample * Channels ) / 8
        WORD  blockAlign;         // number of bytes for one sample including all channels
        WORD  bitsPerSample;      // probably 16
        WORD  cbExtension;        // optional; # of bytes that follow in this struct
        WORD  validBits;          // # of valid bits
        DWORD channelMask;        // speaker position mask
        GUID  subFormat;          // the extended format of the data
    };

    struct PositionPeak
    {
        float value;              // signed value of the peak
        DWORD position;           // sample frame for the peak
    };

    struct WavPeakchunk
    {
        DWORD format;             // e.g. "fmt" or "JUNK" or "bext"
        DWORD formatSize;         // size of format data
        DWORD version;            // version of this chunk format. 1 is known
        DWORD timeStamp;          // seconds since 1/1/1970
        PositionPeak peak[ 0 ];   // actual size is the # of channels in the WavSubchunk
    };

    WavHeader header;

    __int64 len = g_pStream->Length();
    if ( len < sizeof header )
    {
        prf( "Wav file is too small\n" );
        return;
    }

    GetBytes( 0, &header, sizeof header );

    if ( memcmp( & header.riff, "RIFF", sizeof header.riff ) )
    {
        prf( "Wav header isn't RIFF\n" );
        return;
    }

    if ( memcmp( & header.wave, "WAVE", sizeof header.wave ) )
    {
        prf( "Wav wave header isn't WAVE\n" );
        return;
    }

    prf( "header.size (file size - 8):   %d\n", header.size );
    prf( "mimetype:                      audio/x-wav\n" );

    __int64 offset = sizeof header;

    WavSubchunk fmtSubchunk = {0};

    while ( offset < len )
    {
        WavSubchunk chunk;
        GetBytes( offset, &chunk, sizeof chunk );

        if ( 0 == chunk.format )
            break;

        prf( "chunk: %c%c%c%c, size %d\n", chunk.format & 0xff, ( chunk.format >> 8 ) & 0xff,
                                              ( chunk.format >> 16 ) & 0xff, ( chunk.format >> 24 ) & 0xff,
                                              chunk.formatSize );

        if ( !memcmp( &chunk.format, "fmt ", 4 ) )
        {
            prf( "  type of format:              %d -- %s\n",    chunk.formatType, WavFormatType( chunk.formatType ) );
            if ( 0xfffe == chunk.formatType )
            {
                prf( "  extended format:             %s\n", WavExtendedFormatType( chunk.subFormat ) );
                prf( "                               " );
                PrintGUID( chunk.subFormat );
                prf( "\n" );
            }
            prf( "  channels:                    %d\n", chunk.channels );
            prf( "  sample rate (samples/sec):   %d\n", chunk.sampleRate );
            prf( "  dataRate (Bytes/sec):        %d\n", chunk.dataRate );
            prf( "  blockAlign:                  %d\n", chunk.blockAlign );
            prf( "  bits per sample:             %d\n", chunk.bitsPerSample );

            fmtSubchunk = chunk;
        }
        else if ( !memcmp( &chunk.format, "data", 4 ) )
        {
            int samples = chunk.formatSize / fmtSubchunk.blockAlign;

            prf( "  data size:                   %d\n", chunk.formatSize );
            prf( "  sample count:                %d\n", samples );
            prf( "  seconds:                     %lf\n", (double) samples / (double) fmtSubchunk.sampleRate );

            DumpBinaryData( offset + 8, 0, chunk.formatSize, 2, offset + 8 );
        }
        else if ( !memcmp( &chunk.format, "LIST", 4 ) )
        {
            DumpBinaryData( offset + 8, 0, chunk.formatSize, 2, offset + 8 );
        }
        else if ( !memcmp( &chunk.format, "fact", 4 ) )
        {
            if ( chunk.formatSize >= 4 )
            {
                DWORD samplesPerChannel;
                memcpy( &samplesPerChannel, &chunk.formatType, sizeof( samplesPerChannel ) );
                prf( "  samples per channel:         %u\n", samplesPerChannel );
            }
        }
        else if ( !memcmp( &chunk.format, "PEAK", 4 ) )
        {
            size_t chunkSize = 12 + chunk.formatSize;
            unique_ptr<byte> bytes( new byte[ chunkSize ] );
            WavPeakchunk * ppeak = (WavPeakchunk *) bytes.get();

            if ( chunk.formatSize != ( sizeof( WavPeakchunk::version ) +
                                       sizeof( WavPeakchunk::timeStamp ) +
                                       fmtSubchunk.channels * sizeof( PositionPeak ) ) )
                prf( "peak chunk is malformed\n" );
            else
            {
                GetBytes( offset, ppeak, chunkSize );
                prf( "  version:                   %d\n", ppeak->version );
                if ( 1 == ppeak->version )
                {
                    time_t the_time_t = (time_t) ppeak->timeStamp;
                    struct tm the_tm = * localtime( & the_time_t );
                    prf( "  timestamp:                   %#x == %s", ppeak->timeStamp, asctime( & the_tm ) );
                    for ( DWORD c = 0; c < fmtSubchunk.channels; c++ )
                        prf( "  channel %d:                   value %f and position %u\n", c, ppeak->peak[ c ].value, ppeak->peak[ c ].position );
                }
            }
        }
        else
            DumpBinaryData( offset + 8, 0, chunk.formatSize, 2, offset + 8 );

        offset += chunk.formatSize + 8;
    }

    #pragma pack(pop)
} //ParseWav

const char * IcoType( byte type )
{
    if ( 1 == type )
        return "icon";
    if ( 2 == type )
        return "cursor";
    return "unknown";
} //IcoType

void ParseICO()
{
    struct IcoHeader
    {
        WORD reserved0;
        WORD type; // 1 for icon 2 for cursor
        WORD count; // # of images in the file
    };

    struct IcoDirectoryEntry
    {
        byte width;
        byte height;
        byte paletteColors; // 0 if no palette
        byte reserved0;
        WORD colorPlanes;   // horizontal coordinate of hotspot for .cur files from left
        WORD bpp;           // vertical coordinate of hotspot for .cur files from top
        DWORD dataSize;
        DWORD dataOffset;
    };

    IcoHeader header;

    __int64 len = g_pStream->Length();
    if ( len < sizeof header )
    {
        prf( "Ico/Cur file is too small\n" );
        return;
    }

    GetBytes( 0, &header, sizeof header );

    if ( 0 != header.reserved0 )
    {
        prf( "Ico/Cur file's first byte isn't 0\n" );
        return;
    }

    prf( "mimetype:                             image/ico\n" );
    prf( "file type:                            %u == %s\n", header.type, IcoType( header.type ) );
    prf( "image count:                          %u\n", header.count );

    for ( WORD d = 0; d < header.count; d++ )
    {
        IcoDirectoryEntry entry;

        GetBytes( sizeof( IcoHeader ) + d * sizeof( IcoDirectoryEntry ), &entry, sizeof entry );

        prf( "  directory entry                     %u\n", d );
        prf( "    width                             %u\n", entry.width );
        prf( "    height                            %u\n", entry.height );
        prf( "    palette colors                    %u\n", entry.paletteColors );
        prf( "    reserved                          %u\n", entry.reserved0 );

        if ( 1 == header.type )
        {
            prf( "    color planes                      %u\n", entry.colorPlanes );
            prf( "    bits per pixel                    %u\n", entry.bpp );
        }
        else if ( 2 == header.type )
        {
            prf( "    horizontal hotspot from left      %u\n", entry.colorPlanes );
            prf( "    vertical hotspot from top         %u\n", entry.bpp );
        }

        prf( "    image data size                   %u\n", entry.dataSize );
        prf( "    image data offset                 %u\n", entry.dataOffset );


        DWORD dwHeader0 = GetDWORD( entry.dataOffset, false );
        DWORD dwHeader4 = GetDWORD( 4 + entry.dataOffset, false );
        prf( "    image format                      %s\n", ( 0x89504e47 == dwHeader0 && 0xd0a1a0a == dwHeader4 ) ? "PNG" : "BMP" );
    }

} //ParseICO

struct RafHeader
{
    char      magic[ 16 ];
    char      formatVersion[ 4 ];
    char      cameraNumberID[ 8 ];
    char      camerabodyname[ 32 ];
    char      subVersion[ 4 ];
    char      unknown[ 20 ];
    DWORD     jpgOffset;
    DWORD     jpgLength;
    DWORD     cfaHeaderOffset;
    DWORD     cfaHeaderLength;
    DWORD     cfaOffset;
    DWORD     cfaLength;

    void littleEndian()
    {
        jpgOffset = _byteswap_ulong( jpgOffset );
        jpgLength = _byteswap_ulong( jpgLength );
        cfaHeaderOffset = _byteswap_ulong( cfaHeaderOffset );
        cfaHeaderLength = _byteswap_ulong( cfaHeaderLength );
        cfaOffset = _byteswap_ulong( cfaOffset );
        cfaLength = _byteswap_ulong( cfaLength );
    }
};

void AttemptFujifilmParse()
{
    prf( "Fujifilm RAF raw file:\n" );
    RafHeader raf = { 0 };
    GetBytes( 0, &raf, sizeof raf );
    raf.littleEndian();

    prf( "  RAF magic:                          " );
    for ( int i = 0; i < sizeof raf.magic; i++ )
        prf( "%c", raf.magic[ i ] );
    prf( "\n" );

    prf( "  RAF format version:                 " );
    for ( int i = 0; i < sizeof raf.formatVersion; i++ )
        prf( "%c", raf.formatVersion[ i ] );
    prf( "\n" );

    prf( "  RAF camera number ID:               " );
    for ( int i = 0; i < sizeof raf.cameraNumberID; i++ )
        prf( "%c", raf.cameraNumberID[ i ] );
    prf( "\n" );

    prf( "  RAF camera body name:               %s\n", raf.camerabodyname );

    prf( "  RAF sub version:                    " );
    for ( int i = 0; i < sizeof raf.subVersion; i++ )
        prf( "%c", raf.subVersion[ i ] );
    prf( "\n" );

    prf( "  RAF jpg offset:                     %#x\n", raf.jpgOffset );
    prf( "  RAF jpg length:                     %#x\n", raf.jpgLength );
    prf( "  RAF cfa header offset:              %#x\n", raf.cfaHeaderOffset );
    prf( "  RAF cfa header length:              %#x\n", raf.cfaHeaderLength );
    prf( "  RAF cfa offset:                     %#x\n", raf.cfaOffset );
    prf( "  RAF cfa length:                     %#x\n", raf.cfaLength );

    DWORD recordCount = GetDWORD( raf.cfaHeaderOffset, false );
    prf( "  RAF count of records:               %d\n", recordCount );

    DWORD recordOffset = raf.cfaHeaderOffset + sizeof DWORD;

    for ( DWORD r = 0; r < recordCount; r++ )
    {
        WORD tagID = GetWORD( recordOffset, false );
        recordOffset += sizeof WORD;
        WORD size = GetWORD( recordOffset, false );
        recordOffset += sizeof WORD;

        prf( "    RAF cfa record tag                %d == %#x\n", tagID, tagID );
        prf( "    RAF cfa record size               %d\n", size );

        if ( 4 == size )
        {
            DWORD val = GetDWORD( recordOffset, false );
            prf( "    RAF cfa record value:             %#x\n", val );
        }

        recordOffset += size;
    }
} //AttemptFujifilmParse

void EnumerateImageData( WCHAR const * pwc )
{
    g_pStream = new CStream( pwc );
    unique_ptr<CStream> stream( g_pStream );

    if ( !g_pStream->Ok() )
    {
        wprf( L"can't open file %ws\n", pwc );
        return;
    }

    prf( "file size: %28I64d\n", GetStreamLength() );
    WCHAR * pwcExt = PathFindExtension( pwc );
    DWORD heifOffsetBase = 0;

    if ( !wcsicmp( pwcExt, L".heic" ) ||          // Apple iOS photos
         !wcsicmp( pwcExt, L".hif" ) )            // Canon HEIF photos
    {
        // enumeration of the heif file is just to find the EXIF data offset, reflected in the g_Heif_Exif_* variables

        EnumerateHeif( g_pStream );

        if ( 0 == g_Heif_Exif_Offset )
        {
            prf( "can't find Exif data in this heif (.heic/.hif) file\n" );
            return;
        }

        prf( "mimetype:                             image/heic\n" );
        DWORD o = GetDWORD( g_Heif_Exif_Offset, false );

        heifOffsetBase = o + g_Heif_Exif_Offset + 4;
        g_pStream->Seek( heifOffsetBase );
        prf( "file size: %I64d\n", GetStreamLength() );

        //printf( "o: %d, heifOffsetBase: %d\n", o, heifOffsetBase );
    }
    else if ( !wcsicmp( pwcExt, L".cr3" ) )        // Canon's newer RAW format
    {
        // enumeration of the heif file is just to find the EXIF data offset, reflected in the g_Canon_CR3_* variables
        // Heif and CR3 use ISO Base Media File Format ISO/IEC 14496-12

        EnumerateHeif( g_pStream );

        if ( 0 == g_Canon_CR3_Exif_IFD0 )
        {
            prf( "can't find Exif data in this Canon CR3 file\n" );
            return;
        }

        prf( "mimetype:                             image/x-canon-cr3\n" );
        heifOffsetBase = g_Canon_CR3_Exif_IFD0;
        g_pStream->Seek( heifOffsetBase );

        //printf( "o: %d, heifOffsetBase: %d\n", o, heifOffsetBase );
    }
    else if ( !wcsicmp( pwcExt, L".wma" ) || !wcsicmp( pwcExt, L".wmv" ) )
    {
        ParseAsf();
        return;
    }
    else if ( !wcsicmp( pwcExt, L".wav" ) )
    {
        ParseWav();
        return;
    }
    else if ( !wcsicmp( pwcExt, L".m4a" ) )
    {
        EnumerateHeif( g_pStream );
        return;
    }
    else if ( !wcsicmp( pwcExt, L".ico" ) || !wcsicmp( pwcExt, L".cur" ) )
    {
        ParseICO();
        return;
    }

    DWORD header = 0;
    ULONG readbytes = g_pStream->Read( &header, sizeof header );

    if ( 0 == readbytes )
    {
        prf( "can't read from the file\n" );
        return;
    }

    if ( g_FullInformation )
        prf( "header %#x\n", header );

    if ( 0x43614c66 == header )
    {
        EnumerateFlac();

        if ( 0 != g_Embedded_Image_Offset && 0 != g_Embedded_Image_Length )
        {
            CStream * embeddedImage = new CStream( pwc, g_Embedded_Image_Offset, g_Embedded_Image_Length );

            embeddedImage->Read( &header, sizeof header );

            prf( "Embedded image metadata:\n" );

            if ( g_FullInformation )
                prf( "header of image embedded in flac: %#x\n", header );

            stream.reset( embeddedImage );
            g_pStream = embeddedImage;
            prf( "file size: %28I64d\n", GetStreamLength() );
        }
        else
        {
            return;
        }
    }
    else if ( 0x03334449 == header || 0x02334449 == header || 0x04334449 == header || 0xfbff == ( header & 0xffff ) || !wcsicmp( pwcExt, L".mp3" ) )
    {
        // likely mp3

        if ( wcsicmp( pwcExt, L".mp3" ) )
            prf( "file header says mp3, but file extension isn't mp3. Trying anyway!\n" );

        ParseMP3();

        if ( 0 != g_Embedded_Image_Offset && 0 != g_Embedded_Image_Length )
        {
            CStream * embeddedImage = new CStream( pwc, g_Embedded_Image_Offset, g_Embedded_Image_Length );

            embeddedImage->Read( &header, sizeof header );

            prf( "Embedded image metadata:\n" );

            if ( g_FullInformation )
                prf( "header of image embedded in mp3: %#x\n", header );

            stream.reset( embeddedImage );
            g_pStream = embeddedImage;
            prf( "file size: %I64d\n", GetStreamLength() );
        }
        else
        {
            return;
        }
    }

    if ( ( 0xd8ff     != ( header & 0xffff ) ) &&     // JPG
         ( 0x002a4949 != header ) &&                  // CR2 canon and standard TIF and DNG
         ( 0x2a004d4d != header ) &&                  // NEF nikon (big endian!)
         ( 0x4f524949 != header ) &&                  // ORF olympus
         ( 0x00554949 != header ) &&                  // RW2 panasonic
         ( 0x494a5546 != header ) &&                  // RAF fujifilm
         ( 0x474e5089 != header ) &&                  // PNG
         ( 0x4d42     != ( header & 0xffff ) ) )      // BMP
    {
        wprf( L"header %#x is unrecognized in file %ws\n", header, pwc );
        return;
    }

    // BMP: 0x4d42  "BM"
    if ( 0x4d42 == ( header & 0xffff ) )
    {
        ParseBMP();
        return;
    }

    bool littleEndian = true;
    DWORD startingOffset = 4;
    DWORD headerBase = 0;
    DWORD exifHeaderOffset = 12;

    if ( 0x474e5089 == header )
    {
        DWORD nextFour = GetDWORD( 4, false );

        if ( 0x0d0a1a0a != nextFour )
        {
            prf( "second four bytes of PNG not recognized: %#x\n", nextFour );
            return;
        }

        ParsePNG();
        return;
    }
    else if ( 0xd8ff == ( header & 0xffff ) ) 
    {
        // special handling for JPG files

        int exifMaybe = ParseOldJpg();

        if ( g_FullInformation )
            prf( "exifMaybe offset %#x\n", exifMaybe );

        if ( 0 == exifMaybe )
            return;

        int saveMaybe = exifMaybe;
        exifMaybe += 5;  // Get past "Exif."
        DWORD maybe = GetDWORD( exifMaybe, littleEndian );

        if ( ( 0x002a4949 != maybe ) && ( 0x2a004d4d != maybe ) )
        {
            exifMaybe = 12; // It's just very common
            maybe = GetDWORD( exifMaybe, littleEndian );
        }

        if ( ( 0x002a4949 != maybe ) && ( 0x2a004d4d != maybe ) )
        {
            exifMaybe = saveMaybe + 2; // JFIF files are like this sometimes
            maybe = GetDWORD( exifMaybe, littleEndian );
        }

        if ( ( 0x002a4949 == maybe ) || ( 0x2a004d4d == maybe ) )
        {
            if ( g_FullInformation )
                prf( "found exif info pointed to by app1 data at offset %d\n", exifMaybe + 5 );
            exifHeaderOffset = exifMaybe;
            headerBase = exifHeaderOffset;
            startingOffset = exifHeaderOffset + 4;
            header = maybe;
        }
    }
    else if ( 0x494a5546 == header )
    {
        // Fujifilm RAF files aren't like TIFF files. They have their own format.
        // But RAF files have an embedded JPG with full properties, so show those.
        // https://libopenraw.freedesktop.org/formats/raf/

        AttemptFujifilmParse();

        DWORD jpgOffset = GetDWORD( 84, false );
        DWORD jpgLength = GetDWORD( 88, false );
        DWORD jpgSig = GetDWORD( jpgOffset, true );
        //printf( "jpgOffset %d, jpgSig: %#x\n", jpgOffset, jpgSig );

        if ( 0xd8ff != ( jpgSig & 0xffff ) )
        {
            wprf( L"unexpected JPG header in RAF file: %#x, %ws\n", jpgSig, pwc );
            return;
        }

        prf( "Embedded JPG in RAF:\n" );
        int exifMaybe = ParseOldJpg( jpgOffset );
        DWORD exifSig = GetDWORD( jpgOffset + exifHeaderOffset, true );
        //printf( "jpg exif Sig: %#x\n", exifSig );

        if ( 0x002a4949 != exifSig )
        {
            wprf( L"unexpected EXIF header in RAF file %#x, %ws\n", exifSig, pwc );
            return;
        }

        header = exifSig;
        headerBase = jpgOffset + exifHeaderOffset;
        startingOffset = headerBase + 4;

        g_Embedded_Image_Offset = jpgOffset;
        g_Embedded_Image_Length = jpgLength;

        //printf( "headerBase %d, startingOffset %d\n", headerBase, startingOffset );
    }
    else if ( 0x2a004d4d == header )
    {
        // Apple iPhone .heic files

        if ( 0 != heifOffsetBase )
        {
            headerBase = heifOffsetBase;
            startingOffset += heifOffsetBase;
        }
    }
    else if ( 0x002a4949 == header )
    {
        // Canon mirrorless .hif files

        if ( 0 != heifOffsetBase )
        {
            headerBase = heifOffsetBase;
            startingOffset += heifOffsetBase;
        }
    }
    
    if ( 0x4d4d == ( header & 0xffff ) ) // NEF
        littleEndian = false;

    if ( g_FullInformation )
        prf( "little endian: %#x, starting offset %d, headerBase %d\n", littleEndian, startingOffset, headerBase );

    DWORD IFDOffset = GetDWORD( startingOffset, littleEndian );

    if ( !wcsicmp( pwcExt, L".dng" ) )
        prf( "mimetype:                             image/x-adobe-dng\n" );
    else if ( !wcsicmp( pwcExt, L".tif" ) || !wcsicmp( pwcExt, L".tiff" ) )
        prf( "mimetype:                             image/tiff\n" );
    else if ( !wcsicmp( pwcExt, L".rw2" ) )
        prf( "mimetype:                             image/x-panasonic-rw2\n" );
    else if ( !wcsicmp( pwcExt, L".cr2" ) )
        prf( "mimetype:                             image/x-canon-cr2\n" );
    else if ( !wcsicmp( pwcExt, L".arw" ) )
        prf( "mimetype:                             image/x-sony-arw\n" );
    else if ( !wcsicmp( pwcExt, L".raf" ) )
        prf( "mimetype:                             image/x-fujifilm-raf\n" );
    else if ( !wcsicmp( pwcExt, L".orf" ) )
        prf( "mimetype:                             image/x-olympus-orf\n" );

    EnumerateIFD0( 0, IFDOffset, headerBase, littleEndian, pwcExt );

    if ( ( 0 != g_Embedded_Image_Offset ) && ( 0 != g_Embedded_Image_Length ) && !wcsicmp( pwcExt, L".rw2" )  )
    {
        // Panasonic raw files sometimes have embedded JPGs with metadata not in the actual RW2 file.
        // Specifically, Serial Number, Lens Model, and Lens Serial Number can only be retrieved in this way.

        g_pStream = new CStream( pwc, g_Embedded_Image_Offset, g_Embedded_Image_Length );
        stream.reset( g_pStream );

        if ( !g_pStream->Ok() )
        {
            wprf( L"can't open file %ws\n", pwc );
            return;
        }

        prf( "file size: %I64d\n", GetStreamLength() );
        int exifMaybe = ParseOldJpg();
        if ( 0 != exifMaybe )
        {
            int saveMaybe = exifMaybe;
            exifMaybe += 5;  // Get past "Exif."
            DWORD maybe = GetDWORD( exifMaybe, littleEndian );

            if ( ( 0x002a4949 != maybe ) && ( 0x2a004d4d != maybe ) )
            {
                exifMaybe = 12; // It's just very common
                maybe = GetDWORD( exifMaybe, littleEndian );
            }

            if ( ( 0x002a4949 != maybe ) && ( 0x2a004d4d != maybe ) )
            {
                exifMaybe = saveMaybe + 2; // JFIF files are like this sometimes
                maybe = GetDWORD( exifMaybe, littleEndian );
            }

            if ( ( 0x002a4949 == maybe ) || ( 0x2a004d4d == maybe ) )
            {
                if ( g_FullInformation )
                    prf( "found exif info pointed to by app1 data at offset %d\n", exifMaybe + 5 );
                exifHeaderOffset = exifMaybe;
                headerBase = exifHeaderOffset;
                startingOffset = exifHeaderOffset + 4;
                header = maybe;
                littleEndian = ( 0x4949 == ( header & 0xffff ) );

                DWORD IFDOffset = GetDWORD( startingOffset, littleEndian );
                EnumerateIFD0( 0, IFDOffset, headerBase, littleEndian, pwcExt );
            }
        }
    }

    if ( 0 != g_Canon_CR3_Exif_Exif_IFD )
    {
        prf( "Canon CR3 Exif IFD:\n" );

        WORD endian = GetWORD( g_Canon_CR3_Exif_Exif_IFD, littleEndian );

        EnumerateExifTags( 0, 8, g_Canon_CR3_Exif_Exif_IFD, ( 0x4949 == endian ) );
    }

    if ( 0 != g_Canon_CR3_Exif_Makernotes_IFD )
    {
        prf( "Canon CR3 Makernotes:\n" );

        WORD endian = GetWORD( g_Canon_CR3_Exif_Makernotes_IFD, littleEndian );

        EnumerateMakernotes( 0, 8, g_Canon_CR3_Exif_Makernotes_IFD, ( 0x4949 == endian ) );
    }

    if ( 0 != g_Canon_CR3_Exif_GPS_IFD  )
    {
        prf( "Canon CR3 Exif GPS IFD:\n" );

        WORD endian = GetWORD( g_Canon_CR3_Exif_GPS_IFD, littleEndian );

        EnumerateGPSTags( 0, 8, g_Canon_CR3_Exif_GPS_IFD, ( 0x4949 == endian ) );
    }
} //EnumerateImageData

extern "C" int __cdecl wmain( int argc, WCHAR * argv[] )
{
    setlocale( LC_ALL, "en_US.UTF-8" );
    freopen( 0, "w", stdout );

    if ( argc < 2 )
        Usage();

    _set_se_translator([](unsigned int u, EXCEPTION_POINTERS *pExp)
    {
        wprf( L"translating exception %x\n", u );
        std::string error = "SE Exception: ";
        switch (u)
        {
            case 0xC0000005:
                error += "Access Violation";
                break;
            default:
                char result[11];
                sprintf_s(result, 11, "0x%08X", u);
                error += result;
        };

        wprf( L"throwing std::exception\n" );
    
        throw std::exception(error.c_str());
    });

    WCHAR awcFilename[ MAX_PATH + 1 ];
    awcFilename[0] = 0;
    WCHAR awcEmbeddedImage[ MAX_PATH + 1 ];
    awcEmbeddedImage[0] = 0;

    int iArg = 1;
    while ( iArg < argc )
    {
        const WCHAR * pwcArg = argv[iArg];
        WCHAR a0 = pwcArg[0];

        if ( ( L'-' == a0 ) || ( L'/' == a0 ) )
        {
           WCHAR a1 = towlower( pwcArg[1] );

           if ( 'e' == a1 )
           {
               if ( ':' != pwcArg[ 2 ] )
                   Usage();

               _wfullpath( awcEmbeddedImage, pwcArg + 3, _countof( awcEmbeddedImage ) );
           }
           else if ( 'f' == a1 )
               g_FullInformation = true;
           else
               Usage();
        }
        else
        {
            if ( 0 != awcFilename[ 0 ] )
                Usage();

            _wfullpath( awcFilename, pwcArg, _countof( awcFilename ) );
        }

       iArg++;
    }

    if ( 0 == awcFilename[0] )
        Usage();

    try
    {
        wprf( L"parsing input file %ws\n", awcFilename );

        EnumerateImageData( awcFilename );

        if ( ( 0 != g_Embedded_Image_Offset ) || ( 0 != g_Embedded_Image_Length ) )
        {
            prf( "Embedded image found: offset %I64d length %I64d", g_Embedded_Image_Offset, g_Embedded_Image_Length );

            FILE * fpIn = _wfopen( awcFilename, L"rb" );
            CFile fileIn( fpIn );
    
            if ( NULL != fpIn )
            {
                _fseeki64( fpIn, g_Embedded_Image_Offset, SEEK_SET );
    
                unsigned long long x = 0;
    
                fread( &x, sizeof x, 1, fpIn );

                prf( "  embedded image header: %#llx; image is likely %s", x, LikelyImageHeader( x ) );
            }

            prf( "\n" );
        }

        if ( g_FullInformation )
        {
            if ( ( 0 == g_Embedded_Image_Offset ) || ( 0 == g_Embedded_Image_Length ) )
                prf( "No embedded image found in the file\n" );
        }

        if ( 0 != awcEmbeddedImage[0] )
        {
            if ( ( 0 != g_Embedded_Image_Offset ) && ( 0 != g_Embedded_Image_Length ) )
            {
                FILE * fpIn = _wfopen( awcFilename, L"rb" );
                CFile fileIn( fpIn );
    
                if ( NULL == fpIn )
                {
                    wprf( L"can't open input file %ws\n", awcFilename );
                    Usage();
                }
    
                _fseeki64( fpIn, g_Embedded_Image_Offset, SEEK_SET );
    
                unique_ptr<BYTE> bytes( new BYTE[ g_Embedded_Image_Length ] );
                BYTE * pb = bytes.get();
    
                fread( pb, g_Embedded_Image_Length, 1, fpIn );

                unsigned long long x = * (unsigned long long *) pb;

                if ( !IsPerhapsAnImageHeader( x ) )
                    prf( "warning, embedded image not written: file doesn't have a recognized header; instead it has: %#llx\n", x );

                FILE * fpOut = _wfopen( awcEmbeddedImage, L"wb" );
                CFile fileOut( fpOut );
    
                if ( NULL == fpOut )
                {
                    wprf( L"can't open embedded image output file %ws\n", awcEmbeddedImage );
                    Usage();
                }
    
                fwrite( pb, g_Embedded_Image_Length, 1, fpOut );
                wprf( L"Embedded image containing %lld bytes written to %ws\n", g_Embedded_Image_Length, awcEmbeddedImage );
            }
            else
            {
                prf( "embedded JPG not found\n" );
            }
        }
    }
    catch( ... )
    {
        prf( "caught exception\n" );
    }

    return 0;
} //wmain

