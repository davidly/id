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
static const int MaxIFDHeaders = 210; // assume anything more than this is a corrupt or badly parsed file
bool g_FullInformation = false;
DWORD g_Heif_Exif_ItemID                = 0xffffffff;
__int64 g_Heif_Exif_Offset              = 0;
__int64 g_Heif_Exif_Length              = 0;
struct HeifProperty
{
    string type;
    DWORD width = 0;
    DWORD height = 0;
    BYTE bitsPerChannel = 0;
    BYTE lumaBitDepth = 0;
    BYTE chromaBitDepth = 0;
    BYTE rotation = 0;
    BYTE mirrorAxis = 0;
    bool hasRotation = false;
    bool hasMirror = false;
    vector<BYTE> iccProfile;
};
DWORD g_Heif_Primary_ItemID = 0xffffffff;
vector<HeifProperty> g_Heif_Properties;
vector<DWORD> g_Heif_Primary_Properties;
bool g_Heif_ReadingProperties = false;
__int64 g_Heif_Idat_Offset = 0;
__int64 g_Canon_CR3_Exif_IFD0           = 0;
__int64 g_Canon_CR3_Exif_Exif_IFD       = 0;
__int64 g_Canon_CR3_Exif_Makernotes_IFD = 0;
__int64 g_Canon_CR3_Exif_GPS_IFD        = 0;
__int64 g_Canon_CR3_Embedded_JPG_Length = 0;

__int64 g_WebP_Exif_Offset              = 0;
__int64 g_WebP_Exif_Length              = 0;
    
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
    prf( "      Fujifilm RAF headers, CFA metadata, raw dimensions, and embedded JPG metadata are parsed\n" );
    exit( 1 );
} //Usage

__int64 GetStreamLength() { return g_pStream->Length(); }

bool IsFileRangeValid( __int64 offset, __int64 length )
{
    __int64 fileLength = GetStreamLength();
    return offset >= 0 && length >= 0 && offset <= fileLength && length <= fileLength - offset;
}

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

unique_ptr<WCHAR[]> GetUTF8( __int64 offset, DWORD length )
{
    if ( length > INT_MAX || offset < 0 || offset > g_pStream->Length() ||
         length > (QWORD) ( g_pStream->Length() - offset ) )
        return nullptr;

    unique_ptr<char[]> bytes( new char[ (size_t) length + 1 ] );
    if ( !g_pStream->Seek( offset ) || g_pStream->Read( bytes.get(), length ) != length )
        return nullptr;
    bytes[ length ] = 0;

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

    int cwc = length ? MultiByteToWideChar( CP_UTF8, 0, bytes.get(), (int) length, NULL, 0 ) : 0;
    unique_ptr<WCHAR[]> pwc( new WCHAR[ (size_t) cwc + 1 ] );
    if ( cwc && !MultiByteToWideChar( CP_UTF8, 0, bytes.get(), (int) length, pwc.get(), cwc ) )
        return nullptr;
    pwc[ cwc ] = 0;

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

void DumpShortData( __int64 initialOffset, __int64 headerBase, DWORD length, DWORD indent, DWORD offsetOfSmallValue )
{
    __int64 offset = initialOffset + headerBase;

    // In case the data is inline in the record

    if ( length <= 2 )
        offset = offsetOfSmallValue;

    if ( length > 0x100 && ! g_FullInformation)
        length = 0x100;

    __int64 itemSize = 2;
    __int64 beyond = offset + itemSize * length;
    const __int64 itemsPerRow = 16;
    int16_t buf[ itemsPerRow ];

    while ( offset < beyond )
    {
        for ( int i = 0; i < indent; i++ )
            prf( " " );

        prf( "%#10llx  ", offset );

        __int64 cap = __min( offset + itemSize * itemsPerRow, beyond );
        __int64 toread = ( ( offset + itemsPerRow ) > beyond ) ? ( length % itemsPerRow ) : itemsPerRow;

        GetBytes( offset, buf, toread * itemSize );

        for ( __int64 o = offset; o < cap; o += itemSize )
            prf( "%04x ", 0xffff & buf[ ( o - offset ) / itemSize ] );

        offset += ( itemSize * itemsPerRow );

        prf( "\n" );
    }
} //DumpShortData

void DumpLongData( __int64 initialOffset, __int64 headerBase, DWORD length, DWORD indent, DWORD offsetOfSmallValue )
{
    __int64 offset = initialOffset + headerBase;

    // In case the data is inline in the record

    if ( length <= 4 )
        offset = offsetOfSmallValue;

    if ( length > 0x100 && ! g_FullInformation)
        length = 0x100;

    __int64 itemSize = 4;
    __int64 beyond = offset + itemSize * length;
    const __int64 itemsPerRow = 8;
    int32_t buf[ itemsPerRow ];

    while ( offset < beyond )
    {
        for ( int i = 0; i < indent; i++ )
            prf( " " );

        prf( "%#10llx  ", offset );

        __int64 cap = __min( offset + itemSize * itemsPerRow, beyond );
        __int64 toread = ( ( offset + itemsPerRow ) > beyond ) ? ( length % itemsPerRow ) : itemsPerRow;

        GetBytes( offset, buf, toread * itemSize );

        for ( __int64 o = offset; o < cap; o += itemSize )
            prf( "%08x ", 0xffffffff & buf[ ( o - offset ) / itemSize ] );

        offset += ( itemSize * itemsPerRow );

        prf( "\n" );
    }
} //DumpLongData

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

const char * FujifilmMakerNoteName( WORD tag )
{
    switch ( tag )
    {
        case 0x0000: return "Version";
        case 0x0010: return "Internal serial number";
        case 0x1000: return "Quality";
        case 0x1001: return "Sharpness";
        case 0x1002: return "White balance";
        case 0x1003: return "Saturation";
        case 0x1004: return "Contrast";
        case 0x1005: return "Color temperature";
        case 0x1006: return "Contrast setting";
        case 0x100a: return "White balance fine tune";
        case 0x100b: return "Noise reduction";
        case 0x100e: return "High ISO noise reduction";
        case 0x100f: return "Clarity";
        case 0x1010: return "Flash mode";
        case 0x1011: return "Flash strength";
        case 0x1020: return "Macro";
        case 0x1021: return "Focus mode";
        case 0x1022: return "Focus area";
        case 0x1023: return "Focus point";
        case 0x102b: return "Focus priority";
        case 0x102d: return "Focus setting";
        case 0x102e: return "Continuous focus setting";
        case 0x1030: return "Slow sync";
        case 0x1031: return "Picture mode";
        case 0x1032: return "Exposure count";
        case 0x1033: return "EXR auto";
        case 0x1034: return "EXR mode";
        case 0x1040: return "Shadow tone";
        case 0x1041: return "Highlight tone";
        case 0x1044: return "Digital zoom";
        case 0x1045: return "Lens modulation optimizer";
        case 0x1047: return "Grain effect roughness";
        case 0x1048: return "Color Chrome effect";
        case 0x1049: return "Monochromatic color warm-cool";
        case 0x104b: return "Monochromatic color magenta-green";
        case 0x104c: return "Grain effect size";
        case 0x104d: return "Crop mode";
        case 0x104e: return "Color Chrome FX blue";
        case 0x1050: return "Shutter type";
        case 0x1051: return "Crop flag";
        case 0x1052: return "Crop top-left";
        case 0x1053: return "Crop size";
        case 0x1100: return "Auto bracketing";
        case 0x1101: return "Sequence number";
        case 0x1102: return "White balance bracketing";
        case 0x1103: return "Drive setting";
        case 0x1105: return "Pixel shift shots";
        case 0x1106: return "Pixel shift offset";
        case 0x1150: return "Composite image mode";
        case 0x1151: return "Composite image count 1";
        case 0x1152: return "Composite image count 2";
        case 0x1153: return "Panorama angle";
        case 0x1154: return "Panorama direction";
        case 0x1201: return "Advanced filter";
        case 0x1210: return "FinePix color mode";
        case 0x1300: return "Blur warning";
        case 0x1301: return "Focus warning";
        case 0x1302: return "Exposure warning";
        case 0x1400: return "Dynamic range";
        case 0x1401: return "Film mode";
        case 0x1402: return "Dynamic range setting";
        case 0x1403: return "Development dynamic range";
        case 0x1404: return "Minimum focal length";
        case 0x1405: return "Maximum focal length";
        case 0x1406: return "Maximum aperture at minimum focal length";
        case 0x1407: return "Maximum aperture at maximum focal length";
        case 0x140b: return "Auto dynamic range";
        case 0x1422: return "Image stabilization";
        case 0x1425: return "Scene recognition";
        case 0x1431: return "Rating";
        case 0x1436: return "Image generation";
        case 0x1438: return "Image count";
        case 0x1443: return "Dynamic range priority";
        case 0x1444: return "Dynamic range priority auto";
        case 0x1445: return "Dynamic range priority fixed";
        case 0x1446: return "Flicker reduction";
        case 0x1447: return "Fujifilm model";
        case 0x1448: return "Fujifilm model 2";
        case 0x144a: return "White balance red";
        case 0x144b: return "White balance green";
        case 0x144c: return "White balance blue";
        case 0x144d: return "Roll angle";
        case 0x4005: return "Face element selected";
        case 0x4100: return "Faces detected";
        default: return NULL;
    }
}

const char * FujifilmMakerNoteValueName( WORD tag, DWORD value )
{
    if ( 0x1001 == tag )
    {
        switch ( value )
        {
            case 0: return "-4 (softest)";
            case 1: return "-3 (very soft)";
            case 2: return "-2 (soft)";
            case 3: return "0 (normal)";
            case 4: return "+2 (hard)";
            case 5: return "+3 (very hard)";
            case 6: return "+4 (hardest)";
            case 0x82: return "-1 (medium soft)";
            case 0x84: return "+1 (medium hard)";
            case 0xffff: return "n/a";
        }
    }
    else if ( 0x1020 == tag )
    {
        if ( 0 == value ) return "Off";
        if ( 1 == value ) return "On";
    }
    else if ( 0x1021 == tag )
    {
        if ( 0 == value ) return "Auto";
        if ( 1 == value ) return "Manual";
        if ( 0xffff == value ) return "Movie";
    }
    else if ( 0x1002 == tag )
    {
        switch ( value )
        {
            case 0x000: return "Auto";
            case 0x001: return "Auto (white priority)";
            case 0x002: return "Auto (ambiance priority)";
            case 0x100: return "Daylight";
            case 0x200: return "Cloudy";
            case 0x300: return "Daylight fluorescent";
            case 0x301: return "Day white fluorescent";
            case 0x302: return "White fluorescent";
            case 0x303: return "Warm white fluorescent";
            case 0x304: return "Living room warm white fluorescent";
            case 0x400: return "Incandescent";
            case 0x500: return "Flash";
            case 0x600: return "Underwater";
            case 0xf00: return "Custom";
            case 0xf01: return "Custom 2";
            case 0xf02: return "Custom 3";
            case 0xf03: return "Custom 4";
            case 0xf04: return "Custom 5";
            case 0xff0: return "Kelvin";
        }
    }
    else if ( 0x1003 == tag )
    {
        switch ( value )
        {
            case 0x000: return "0 (normal)";
            case 0x080: return "+1 (medium high)";
            case 0x0c0: return "+3 (very high)";
            case 0x0e0: return "+4 (highest)";
            case 0x100: return "+2 (high)";
            case 0x180: return "-1 (medium low)";
            case 0x200: return "Low";
            case 0x300: return "None (B&W)";
            case 0x301: return "B&W red filter";
            case 0x302: return "B&W yellow filter";
            case 0x303: return "B&W green filter";
            case 0x310: return "B&W sepia";
            case 0x400: return "-2 (low)";
            case 0x4c0: return "-3 (very low)";
            case 0x4e0: return "-4 (lowest)";
            case 0x500: return "Acros";
            case 0x501: return "Acros red filter";
            case 0x502: return "Acros yellow filter";
            case 0x503: return "Acros green filter";
        }
    }
    else if ( 0x1004 == tag || 0x1006 == tag )
    {
        if ( 0 == value ) return "Normal";
        if ( 0x80 == value ) return "Medium high";
        if ( 0x100 == value ) return "High";
        if ( 0x180 == value ) return "Medium low";
        if ( 0x200 == value ) return "Low";
        if ( 0x300 == value && 0x1006 == tag ) return "Low";
    }
    else if ( 0x1050 == tag )
    {
        switch ( value )
        {
            case 0: return "Mechanical";
            case 1: return "Electronic";
            case 2: return "Electronic (long shutter speed)";
            case 3: return "Electronic front curtain";
        }
    }
    else if ( 0x1100 == tag )
    {
        if ( 0 == value ) return "Off";
        if ( 1 == value ) return "On";
        if ( 2 == value ) return "Pre-shot";
        if ( 6 == value ) return "Pixel shift";
    }
    else if ( 0x1150 == tag )
    {
        if ( 0 == value ) return "n/a";
        if ( 1 == value ) return "Pro low-light";
        if ( 2 == value ) return "Pro focus";
        if ( 32 == value ) return "Panorama";
        if ( 128 == value ) return "HDR";
        if ( 1024 == value ) return "Multi-exposure";
    }
    else if ( 0x1154 == tag )
    {
        if ( 1 == value ) return "Right";
        if ( 2 == value ) return "Left";
        if ( 3 == value ) return "Up";
        if ( 4 == value ) return "Down";
    }
    else if ( 0x1436 == tag )
    {
        if ( 0 == value ) return "Original image";
        if ( 1 == value ) return "Re-developed from RAW";
    }
    else if ( 0x1443 == tag )
    {
        if ( 0 == value ) return "Auto";
        if ( 1 == value ) return "Fixed";
    }
    else if ( 0x1444 == tag )
    {
        if ( 1 == value ) return "Weak";
        if ( 2 == value ) return "Strong";
        if ( 3 == value ) return "Plus";
    }
    else if ( 0x1445 == tag )
    {
        if ( 1 == value ) return "Weak";
        if ( 2 == value ) return "Strong";
    }
    else if ( 0x1400 == tag )
    {
        if ( 1 == value ) return "Standard";
        if ( 3 == value ) return "Wide";
    }
    else if ( 0x1401 == tag )
    {
        switch ( value )
        {
            case 0x000: return "Provia/Standard";
            case 0x100: return "Studio Portrait";
            case 0x110: return "Studio Portrait Enhanced Saturation";
            case 0x120: return "Astia/Smooth Skin Tone";
            case 0x200: return "Velvia/Fujichrome";
            case 0x400: return "Velvia";
            case 0x500: return "Pro Neg. Std";
            case 0x501: return "Pro Neg. Hi";
            case 0x600: return "Classic Chrome";
            case 0x700: return "Eterna";
            case 0x800: return "Classic Negative";
            case 0x900: return "Bleach Bypass";
            case 0xa00: return "Nostalgic Neg";
            case 0xb00: return "Reala ACE";
        }
    }
    else if ( 0x1402 == tag )
    {
        if ( 0 == value ) return "Auto";
        if ( 1 == value ) return "Manual";
        if ( 0x100 == value ) return "Standard (100%)";
        if ( 0x200 == value ) return "Wide 1 (230%)";
        if ( 0x201 == value ) return "Wide 2 (400%)";
        if ( 0x8000 == value ) return "Film simulation";
    }
    else if ( 0x1300 == tag )
    {
        if ( 0 == value ) return "None";
        if ( 1 == value ) return "Blur warning";
    }
    else if ( 0x1301 == tag )
    {
        if ( 0 == value ) return "Good";
        if ( 1 == value ) return "Out of focus";
    }
    else if ( 0x1302 == tag )
    {
        if ( 0 == value ) return "Good";
        if ( 1 == value ) return "Bad exposure";
    }

    return NULL;
}

void EnumerateFujifilmMakernotes( int depth, __int64 IFDOffset, __int64 headerBase, bool littleEndian )
{
    // https://www.exiv2.org/tags-fujifilm.html

    vector<IFDHeader> aHeaders( MaxIFDHeaders );
    // In Fuji files, the base is relative to the IFD start, not the preceding base.
    __int64 tagHeaderBase = IFDOffset - 12;

    while ( 0 != IFDOffset )
    {
        if ( !IsFileRangeValid( IFDOffset + headerBase, 2 ) )
        {
            Space( depth );
            prf( "Fujifilm MakerNote IFD header is outside the file; skipping\n" );
            break;
        }

        WORD NumTags = GetWORD( IFDOffset + headerBase, littleEndian );
        Space( depth );
        prf( "Fujifilm MakerNote IFD: %u tags\n", NumTags );
        IFDOffset += 2;

        if ( NumTags > MaxIFDHeaders ||
             !IsFileRangeValid( IFDOffset + headerBase, (__int64) NumTags * sizeof( IFDHeader ) + sizeof( DWORD ) ) )
        {
            Space( depth );
            prf( "Fujifilm MakerNote tag table is invalid; skipping\n" );
            break;
        }

        if ( !GetIFDHeaders( IFDOffset + headerBase, aHeaders.data(), NumTags, littleEndian ) )
        {
            Space( depth );
            prf( "Fujifilm MakerNote contains an invalid tag; skipping\n" );
            break;
        }

        for ( int i = 0; i < NumTags; i++ )
        {
            IFDHeader & head = aHeaders[ i ];
            IFDOffset += sizeof IFDHeader;
            const char * tagName = FujifilmMakerNoteName( head.id );

            if ( g_FullInformation )
            {
                Space( depth );
                prf( "Fujifilm MakerNote tag %d ID %#x, type %d, count %u, value/offset %#x\n",
                     i, head.id, head.type, head.count, head.offset );
            }

            Space( depth );
            prf( "Fujifilm %s%s", tagName ? tagName : "MakerNote tag", tagName ? ": " : " " );
            if ( !tagName )
                prf( "%#x: ", head.id );

            if ( 2 == head.type )
            {
                __int64 stringOffset = ( head.count <= 4 )
                    ? IFDOffset - 4 + tagHeaderBase + headerBase
                    : (__int64) head.offset + tagHeaderBase + headerBase;

                if ( head.count > 0 && head.count <= 0x10000 &&
                     IsFileRangeValid( stringOffset, head.count ) )
                {
                    vector<char> text( min( head.count, (DWORD) 1024 ) + 1, 0 );
                    GetBytes( stringOffset, text.data(), (int) text.size() - 1 );
                    for ( size_t c = 0; c + 1 < text.size(); c++ )
                    {
                        unsigned char ch = (unsigned char) text[ c ];
                        if ( 0 == ch )
                            break;
                        if ( ch < ' ' || ch > '~' )
                        {
                            text[ c ] = 0;
                            break;
                        }
                    }
                    prf( "%s\n", text.data() );
                }
                else
                    prf( "(invalid string range; %u bytes)\n", head.count );
            }
            else if ( 7 == head.type && 4 == head.count &&
                      ( 0x0000 == head.id || 0x1408 == head.id || 0x1409 == head.id ) )
            {
                char version[ 5 ] = {};
                __int64 versionOffset = IFDOffset - 4 + headerBase;
                if ( IsFileRangeValid( versionOffset, 4 ) )
                {
                    GetBytes( versionOffset, version, 4 );
                    prf( "%.*s\n", 4, version );
                }
                else
                    prf( "(invalid version data range)\n" );
            }
            else if ( 1 == head.type || 6 == head.type || 3 == head.type || 8 == head.type ||
                      4 == head.type || 9 == head.type || 5 == head.type || 10 == head.type )
            {
                DWORD valueSize = ( 1 == head.type || 2 == head.type || 6 == head.type || 7 == head.type ) ? 1 :
                                  ( 3 == head.type || 8 == head.type ) ? 2 :
                                  ( 5 == head.type || 10 == head.type ) ? 8 : 4;
                unsigned __int64 byteCount = (unsigned __int64) head.count * valueSize;
                __int64 valuesOffset = ( byteCount <= 4 )
                    ? IFDOffset - 4 + headerBase
                    : (__int64) head.offset + tagHeaderBase + headerBase;

                if ( 0 == head.count || byteCount > INT_MAX ||
                     !IsFileRangeValid( valuesOffset, (__int64) byteCount ) )
                {
                    prf( "(invalid numeric data range; type %u, count %u)\n", head.type, head.count );
                }
                else if ( 5 == head.type || 10 == head.type )
                {
                    DWORD valuesToPrint = min( head.count, 4u );
                    if ( valuesToPrint > 1 )
                        prf( "[%u values] ", head.count );
                    for ( DWORD v = 0; v < valuesToPrint; v++ )
                    {
                        if ( v )
                            prf( ", " );
                        if ( 5 == head.type )
                        {
                            DWORD numerator = GetDWORD( valuesOffset + v * 8, littleEndian );
                            DWORD denominator = GetDWORD( valuesOffset + v * 8 + 4, littleEndian );
                            if ( denominator )
                                prf( "%u / %u = %.6f", numerator, denominator, (double) numerator / denominator );
                            else
                                prf( "%u / 0", numerator );
                        }
                        else
                        {
                            LONG numerator = (LONG) GetDWORD( valuesOffset + v * 8, littleEndian );
                            LONG denominator = (LONG) GetDWORD( valuesOffset + v * 8 + 4, littleEndian );
                            if ( denominator )
                                prf( "%ld / %ld = %.6f", numerator, denominator, (double) numerator / denominator );
                            else
                                prf( "%ld / 0", numerator );
                        }
                    }
                    if ( valuesToPrint < head.count )
                        prf( ", ..." );
                    prf( "\n" );
                }
                else if ( 1 == head.count )
                {
                    DWORD value = head.offset;
                    if ( 1 == head.type || 6 == head.type )
                        value &= 0xff;
                    else if ( 3 == head.type || 8 == head.type )
                        value &= 0xffff;

                    if ( 9 == head.type )
                        prf( "%ld", (LONG) value );
                    else
                        prf( "%u (0x%x)", value, value );

                    const char * valueName = FujifilmMakerNoteValueName( head.id, value );
                    if ( valueName )
                        prf( " (%s)", valueName );
                    prf( "\n" );
                }
                else
                {
                    DWORD valuesToPrint = min( head.count, 8u );
                    prf( "[%u values] ", head.count );
                    for ( DWORD v = 0; v < valuesToPrint; v++ )
                    {
                        if ( v )
                            prf( ", " );
                        if ( 1 == head.type || 6 == head.type )
                            prf( "%u", GetBYTE( valuesOffset + v ) );
                        else if ( 3 == head.type || 8 == head.type )
                            prf( "%u", GetWORD( valuesOffset + v * 2, littleEndian ) );
                        else if ( 9 == head.type )
                            prf( "%ld", (LONG) GetDWORD( valuesOffset + v * 4, littleEndian ) );
                        else
                            prf( "%u", GetDWORD( valuesOffset + v * 4, littleEndian ) );
                    }
                    if ( valuesToPrint < head.count )
                        prf( ", ..." );
                    prf( "\n" );
                }
            }
            else
            {
                prf( "(type %u, count %u, value/offset %#x)\n", head.type, head.count, head.offset );
                if ( 7 == head.type && head.count > 0 )
                {
                    __int64 dataOffset = ( head.count <= 4 )
                        ? IFDOffset - 4 + headerBase
                        : (__int64) head.offset + tagHeaderBase + headerBase;
                    DWORD dataLength = min( head.count, (DWORD) 0x100 );
                    if ( IsFileRangeValid( dataOffset, dataLength ) )
                        DumpBinaryData( dataOffset, 0, dataLength, depth + 1, dataOffset );
                }
            }
        }

        IFDOffset += sizeof( DWORD );
        DWORD nextIFD = GetDWORD( IFDOffset - sizeof( DWORD ) + headerBase, littleEndian );
        IFDOffset = nextIFD;
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

const char * SonyMakerNoteTagName( WORD tag )
{
    switch ( tag )
    {
        case 258: return "Quality";
        case 260: return "FlashExposureComp";
        case 8194: return "Rating";
        case 8196: return "Contrast";
        case 8197: return "Saturation";
        case 8198: return "Sharpness";
        case 8199: return "Brightness";
        case 8200: return "LongExposureNoiseReduction";
        case 8201: return "HighISONoiseReduction";
        case 8202: return "HDR";
        case 8203: return "MultiFrameNoiseReduction";
        case 8206: return "PictureEffect";
        case 8207: return "SoftSkinEffect";
        case 8209: return "VignettingCorrection";
        case 8210: return "LateralChromaticAberration";
        case 8211: return "DistortionCorrectionSetting";
        case 8212: return "WBShiftAB_GM";
        case 8214: return "AutoPortraitFramed";
        case 8215: return "FlashAction";
        case 8218: return "ElectronicFrontCurtainShutter";
        case 8219: return "FocusMode";
        case 8220: return "AFAreaModeSetting";
        case 8221: return "FlexibleSpotPosition";
        case 8222: return "AFPointSelected";
        case 8224: return "AFPointsUsed";
        case 8225: return "AFTracking";
        case 8226: return "FocalPlaneAFPointsUsed";
        case 8227: return "MultiFrameNREffect";
        case 8230: return "WBShiftAB_GM_Precise";
        case 8231: return "FocusLocation";
        case 8232: return "VariableLowPassFilter";
        case 8233: return "RAWFileType";
        case 8235: return "PrioritySetInAWB";
        case 8236: return "MeteringMode2";
        case 8237: return "ExposureStandardAdjustment";
        case 8238: return "Quality";
        case 8239: return "PixelShiftInfo";
        case 8241: return "SerialNumber";
        case 45056: return "FileFormat";
        case 45057: return "SonyModelID";
        case 45088: return "CreativeStyle";
        case 45089: return "ColorTemperature";
        case 45090: return "ColorCompensationFilter";
        case 45091: return "SceneMode";
        case 45092: return "ZoneMatching";
        case 45093: return "DynamicRangeOptimizer";
        case 45094: return "ImageStabilization";
        case 45095: return "LensType";
        case 45097: return "ColorMode";
        case 45098: return "LensSpec";
        case 45099: return "FullImageSize";
        case 45100: return "PreviewImageSize";
        case 45120: return "Macro";
        case 45121: return "ExposureMode";
        case 45122: return "FocusMode";
        case 45123: return "AFAreaMode";
        case 45124: return "AFIlluminator";
        case 45127: return "JPEGQuality";
        case 45128: return "FlashLevel";
        case 45129: return "ReleaseMode";
        case 45130: return "SequenceNumber";
        case 45131: return "Anti-Blur";
        case 45134: return "FocusMode";
        case 45135: return "DynamicRangeOptimizer";
        case 45136: return "HighISONoiseReduction2";
        case 45138: return "IntelligentAuto";
        case 45140: return "WhiteBalance";
        default: return NULL;
    }
}

const char * SonyMakerNoteValueName( WORD tag, DWORD value )
{
    switch ( tag )
    {
        case 8209:
        case 8210:
        case 8211:
            if ( 0 == value ) return "Off";
            if ( 2 == value ) return "Auto";
            if ( 0xffffffff == value ) return "n/a";
            break;
        case 8219:
            if ( 0 == value ) return "Manual";
            if ( 2 == value ) return "AF-S";
            if ( 3 == value ) return "AF-C";
            if ( 4 == value ) return "AF-A";
            if ( 6 == value ) return "DMF";
            if ( 7 == value ) return "AF-D";
            break;
        case 8233:
            if ( 0 == value ) return "Compressed RAW";
            if ( 1 == value ) return "Uncompressed RAW";
            if ( 65535 == value ) return "n/a";
            break;
        case 8235:
            if ( 0 == value ) return "Standard";
            if ( 1 == value ) return "Ambience";
            if ( 2 == value ) return "White";
            break;
        case 45093:
            if ( 0 == value ) return "Off";
            if ( 1 == value ) return "Standard";
            if ( 2 == value ) return "Advanced Auto";
            if ( 3 == value ) return "Auto";
            if ( value >= 8 && value <= 12 )
            {
                static const char * const levels[] = {
                    "Advanced Lv1", "Advanced Lv2", "Advanced Lv3",
                    "Advanced Lv4", "Advanced Lv5"
                };
                return levels[ value - 8 ];
            }
            if ( value >= 16 && value <= 20 )
            {
                static const char * const levels[] = {
                    "Lv1", "Lv2", "Lv3", "Lv4", "Lv5"
                };
                return levels[ value - 16 ];
            }
            break;
        case 45094:
            if ( 0 == value ) return "Off";
            if ( 1 == value ) return "On";
            if ( 0xffffffff == value ) return "n/a";
            break;
        case 45122:
            if ( 1 == value ) return "AF-S";
            if ( 2 == value ) return "AF-C";
            if ( 4 == value ) return "Permanent-AF";
            if ( 65535 == value ) return "n/a";
            break;
        case 45127:
            if ( 0 == value ) return "Standard";
            if ( 1 == value ) return "Fine";
            if ( 2 == value ) return "Extra Fine";
            if ( 65535 == value ) return "n/a";
            break;
        case 45134:
            if ( 0 == value ) return "Manual";
            if ( 2 == value ) return "AF-S";
            if ( 3 == value ) return "AF-C";
            if ( 5 == value ) return "Semi-manual";
            if ( 6 == value ) return "DMF";
            break;
        case 45135:
            if ( 0 == value ) return "Off";
            if ( 1 == value ) return "Standard";
            if ( 2 == value ) return "Plus";
            break;
        case 45136:
            if ( 0 == value ) return "Normal";
            if ( 1 == value ) return "High";
            if ( 2 == value ) return "Low";
            if ( 3 == value ) return "Off";
            if ( 65535 == value ) return "n/a";
            break;
        case 45138:
            if ( 0 == value ) return "Off";
            if ( 1 == value ) return "On";
            if ( 2 == value ) return "Advanced";
            break;
        case 45140:
            switch ( value )
            {
                case 0: return "Auto";
                case 4: return "Custom";
                case 5: return "Daylight";
                case 6: return "Cloudy";
                case 7: return "Cool White Fluorescent";
                case 8: return "Day White Fluorescent";
                case 9: return "Daylight Fluorescent";
                case 10: return "Incandescent2";
                case 11: return "Warm White Fluorescent";
                case 14: return "Incandescent";
                case 15: return "Flash";
                case 17: return "Underwater 1 (Blue Water)";
                case 18: return "Underwater 2 (Green Water)";
                case 19: return "Underwater Auto";
            }
            break;
    }

    return NULL;
}

void PrintSonyMakerNoteTag( int depth, const char * name, const IFDHeader & head,
                            __int64 inlineValueOffset, __int64 headerBase, bool littleEndian )
{
    DWORD valueSize = ( 1 == head.type || 2 == head.type || 6 == head.type || 7 == head.type ) ? 1 :
                      ( 3 == head.type || 8 == head.type ) ? 2 :
                      ( 4 == head.type || 9 == head.type ) ? 4 :
                      ( 5 == head.type || 10 == head.type ) ? 8 : 0;
    unsigned __int64 byteCount = (unsigned __int64) head.count * valueSize;
    __int64 valuesOffset = ( byteCount <= 4 )
        ? inlineValueOffset + headerBase
        : (__int64) head.offset + headerBase;

    Space( depth );
    if ( 0 == valueSize || 0 == head.count || byteCount > INT_MAX ||
         !IsFileRangeValid( valuesOffset, (__int64) byteCount ) )
    {
        prf( "Sony %s: (invalid or unsupported data; type %u, count %u)\n",
             name, head.type, head.count );
        return;
    }

    if ( 2 == head.type )
    {
        vector<char> text( min( head.count, 1024u ) + 1, 0 );
        DWORD textLength = min( head.count, (DWORD) text.size() - 1 );
        GetBytes( valuesOffset, text.data(), (int) textLength );
        for ( DWORD i = 0; i < textLength; i++ )
        {
            unsigned char ch = (unsigned char) text[ i ];
            if ( 0 == ch )
                break;
            if ( ch < ' ' || ch > '~' )
                text[ i ] = '?';
        }
        prf( "Sony %s: %s\n", name, text.data() );
        return;
    }

    DWORD valuesToPrint = min( head.count, 16u );
    prf( "Sony %s: ", name );
    for ( DWORD i = 0; i < valuesToPrint; i++ )
    {
        if ( i )
            prf( ", " );

        __int64 valueOffset = valuesOffset + (__int64) i * valueSize;
        if ( 1 == head.count && 1 == head.type )
        {
            const char * valueName = SonyMakerNoteValueName( head.id, GetBYTE( valueOffset ) );
            if ( valueName )
            {
                prf( "%s", valueName );
                continue;
            }
        }
        else if ( 1 == head.count && 3 == head.type )
        {
            const char * valueName = SonyMakerNoteValueName( head.id, GetWORD( valueOffset, littleEndian ) );
            if ( valueName )
            {
                prf( "%s", valueName );
                continue;
            }
        }
        else if ( 1 == head.count && 4 == head.type )
        {
            const char * valueName = SonyMakerNoteValueName( head.id, GetDWORD( valueOffset, littleEndian ) );
            if ( valueName )
            {
                prf( "%s", valueName );
                continue;
            }
        }

        if ( 1 == head.type )
            prf( "%u", GetBYTE( valueOffset ) );
        else if ( 6 == head.type )
            prf( "%d", (signed char) GetBYTE( valueOffset ) );
        else if ( 3 == head.type )
            prf( "%u", GetWORD( valueOffset, littleEndian ) );
        else if ( 8 == head.type )
            prf( "%d", (short) GetWORD( valueOffset, littleEndian ) );
        else if ( 4 == head.type )
            prf( "%u", GetDWORD( valueOffset, littleEndian ) );
        else if ( 9 == head.type )
            prf( "%ld", (LONG) GetDWORD( valueOffset, littleEndian ) );
        else if ( 5 == head.type || 10 == head.type )
        {
            if ( 10 == head.type )
            {
                LONG numerator = (LONG) GetDWORD( valueOffset, littleEndian );
                LONG denominator = (LONG) GetDWORD( valueOffset + 4, littleEndian );
                if ( denominator )
                    prf( "%ld/%ld=%.8g", numerator, denominator,
                         (double) numerator / denominator );
                else
                    prf( "%ld/0", numerator );
            }
            else
            {
                DWORD numerator = GetDWORD( valueOffset, littleEndian );
                DWORD denominator = GetDWORD( valueOffset + 4, littleEndian );
                if ( denominator )
                    prf( "%u/%u=%.8g", numerator, denominator,
                         (double) numerator / denominator );
                else
                    prf( "%u/0", numerator );
            }
        }
        else
            prf( "%02x", GetBYTE( valueOffset ) );
    }
    if ( valuesToPrint < head.count )
        prf( ", ..." );
    if ( 45057 == head.id && 1 == head.count && 3 == head.type )
    {
        WORD modelID = GetWORD( valuesOffset, littleEndian );
        if ( 297 == modelID )
            prf( " (DSC-RX100)" );
        else if ( 298 == modelID )
            prf( " (DSC-RX1)" );
    }
    prf( "\n" );
}

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

void PrintDngRationalArray( const char * name, const IFDHeader & head, __int64 headerBase, bool littleEndian )
{
    if ( ( 5 != head.type && 10 != head.type ) || 0 == head.count )
    {
        prf( "%-37s (unsupported rational type %u, count %u)\n", name, head.type, head.count );
        return;
    }

    unsigned __int64 byteCount = (unsigned __int64) head.count * 8;
    __int64 valuesOffset = (__int64) head.offset + headerBase;
    if ( byteCount > INT_MAX || !IsFileRangeValid( valuesOffset, (__int64) byteCount ) )
    {
        prf( "%-37s (invalid rational data range; count %u)\n", name, head.count );
        return;
    }

    prf( "%-37s", name );
    DWORD valuesToPrint = min( head.count, 32u );
    for ( DWORD valueIndex = 0; valueIndex < valuesToPrint; valueIndex++ )
    {
        if ( valueIndex )
            prf( ", " );

        __int64 valueOffset = valuesOffset + (__int64) valueIndex * 8;
        if ( 10 == head.type )
        {
            LONG numerator = (LONG) GetDWORD( valueOffset, littleEndian );
            LONG denominator = (LONG) GetDWORD( valueOffset + 4, littleEndian );
            if ( denominator )
                prf( "%.10g", (double) numerator / denominator );
            else
                prf( "%ld/0", numerator );
        }
        else
        {
            DWORD numerator = GetDWORD( valueOffset, littleEndian );
            DWORD denominator = GetDWORD( valueOffset + 4, littleEndian );
            if ( denominator )
                prf( "%.10g", (double) numerator / denominator );
            else
                prf( "%u/0", numerator );
        }
    }
    if ( valuesToPrint < head.count )
        prf( ", ..." );
    prf( "\n" );
}

void PrintDngBlackLevel( const IFDHeader & head, __int64 inlineValueOffset,
                         __int64 headerBase, bool littleEndian )
{
    DWORD valueSize = ( 1 == head.type || 6 == head.type ) ? 1 :
                      ( 3 == head.type || 8 == head.type ) ? 2 :
                      ( 4 == head.type || 9 == head.type ) ? 4 :
                      ( 5 == head.type || 10 == head.type ) ? 8 : 0;
    unsigned __int64 byteCount = (unsigned __int64) head.count * valueSize;
    __int64 valuesOffset = ( byteCount <= 4 )
        ? inlineValueOffset + headerBase
        : (__int64) head.offset + headerBase;

    if ( 0 == valueSize || 0 == head.count || byteCount > INT_MAX ||
         !IsFileRangeValid( valuesOffset, (__int64) byteCount ) )
    {
        prf( "BlackLevel:                         (invalid data range; type %u, count %u)\n",
             head.type, head.count );
        return;
    }

    prf( "BlackLevel:                         " );
    DWORD valuesToPrint = min( head.count, 32u );
    for ( DWORD valueIndex = 0; valueIndex < valuesToPrint; valueIndex++ )
    {
        if ( valueIndex )
            prf( ", " );

        __int64 valueOffset = valuesOffset + (__int64) valueIndex * valueSize;
        if ( 5 == head.type || 10 == head.type )
        {
            if ( 10 == head.type )
            {
                LONG numerator = (LONG) GetDWORD( valueOffset, littleEndian );
                LONG denominator = (LONG) GetDWORD( valueOffset + 4, littleEndian );
                if ( denominator )
                    prf( "%.10g", (double) numerator / denominator );
                else
                    prf( "%ld/0", numerator );
            }
            else
            {
                DWORD numerator = GetDWORD( valueOffset, littleEndian );
                DWORD denominator = GetDWORD( valueOffset + 4, littleEndian );
                if ( denominator )
                    prf( "%.10g", (double) numerator / denominator );
                else
                    prf( "%u/0", numerator );
            }
        }
        else if ( 1 == head.type )
            prf( "%u", GetBYTE( valueOffset ) );
        else if ( 6 == head.type )
            prf( "%d", (signed char) GetBYTE( valueOffset ) );
        else if ( 3 == head.type )
            prf( "%u", GetWORD( valueOffset, littleEndian ) );
        else if ( 8 == head.type )
            prf( "%d", (short) GetWORD( valueOffset, littleEndian ) );
        else if ( 4 == head.type )
            prf( "%u", GetDWORD( valueOffset, littleEndian ) );
        else
            prf( "%ld", (LONG) GetDWORD( valueOffset, littleEndian ) );
    }
    if ( valuesToPrint < head.count )
        prf( ", ..." );
    prf( "\n" );
}

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
            prf( "numtags is > MaxIFDHeaders; it's likely not in EXIF format, so the data may be noise. skipping (%d)\n", NumTags );
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
                __int64 o = ( head.count * 2 <= 4 )
                    ? IFDOffset - 4 + headerBase
                    : (__int64) head.offset + headerBase;
                if ( !IsFileRangeValid( o, 8 ) )
                {
                    prf( "(invalid data range)\n" );
                    continue;
                }
                for ( int i = 0; i < 4; i++ )
                {
                    WORD x = GetWORD( o + i * 2, littleEndian );
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
                WORD rows = GetWORD( IFDOffset - 4 + headerBase, littleEndian );
                WORD columns = GetWORD( IFDOffset - 2 + headerBase, littleEndian );
                prf( "BlackLevelRepeatDim                 %u, %u\n", rows, columns );
            }                                               
            else if ( 50714 == head.id )
                PrintDngBlackLevel( head, IFDOffset - 4, headerBase, littleEndian );
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
            prf( "numtags is > MaxIFDHeaders; it's likely not in EXIF format, so the data may be noise. skipping (%d)\n", NumTags );
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
            prf( "numtags is > MaxIFDHeaders; it's likely not in EXIF format, so the data may be noise. skipping (%d)\n", NumTags );
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

const char * NikonMakernoteTagName( WORD tag )
{
    switch ( tag )
    {
        case 0x0001: return "Version";
        case 0x0002: return "ISO";
        case 0x0003: return "ColorMode";
        case 0x0004: return "Quality";
        case 0x0005: return "WhiteBalance";
        case 0x0006: return "Sharpness";
        case 0x0007: return "FocusMode";
        case 0x0008: return "FlashSetting";
        case 0x0009: return "FlashType";
        case 0x000b: return "WhiteBalanceFineTune";
        case 0x000c: return "WhiteBalanceLevels";
        case 0x000d: return "ProgramShift";
        case 0x000e: return "ExposureDifference";
        case 0x000f: return "ISOSelection";
        case 0x0010: return "DataDump";
        case 0x0011: return "PreviewIFD";
        case 0x0012: return "FlashExposureCompensation";
        case 0x0013: return "ISOSetting";
        case 0x0016: return "ImageBoundary";
        case 0x0017: return "FlashExposureCompensation";
        case 0x0018: return "FlashExposureBracket";
        case 0x0019: return "ExposureBracketCompensation";
        case 0x001a: return "ImageProcessing";
        case 0x001b: return "HighSpeedCrop";
        case 0x001c: return "ExposureTuning";
        case 0x001d: return "SerialNumber";
        case 0x001e: return "ColorSpace";
        case 0x001f: return "VibrationReductionInfo";
        case 0x0020: return "ImageAuthentication";
        case 0x0022: return "ActiveD-Lighting";
        case 0x0023: return "PictureControl";
        case 0x0024: return "WorldTime";
        case 0x0025: return "ISOInfo";
        case 0x002a: return "VignetteControl";
        case 0x0034: return "ShutterMode";
        case 0x0037: return "MechanicalShutterCount";
        case 0x003f: return "WhiteBalanceFineTune2";
        case 0x0080: return "ImageAdjustment";
        case 0x0081: return "ToneCompensation";
        case 0x0082: return "AuxiliaryLens";
        case 0x0083: return "LensType";
        case 0x0084: return "Lens";
        case 0x0085: return "FocusDistance";
        case 0x0086: return "DigitalZoom";
        case 0x0087: return "FlashMode";
        case 0x0088: return "AFInfo";
        case 0x0089: return "ShootingMode";
        case 0x008a: return "AutoBracketRelease";
        case 0x008b: return "LensFStops";
        case 0x008c: return "ContrastCurve";
        case 0x008d: return "ColorHue";
        case 0x008f: return "SceneMode";
        case 0x0090: return "LightSource";
        case 0x0091: return "ShotInfo";
        case 0x0092: return "HueAdjustment";
        case 0x0093: return "NEFCompression";
        case 0x0094: return "Saturation";
        case 0x0095: return "NoiseReduction";
        case 0x0096: return "LinearizationTable";
        case 0x0097: return "ColorBalance";
        case 0x0098: return "LensData";
        case 0x0099: return "RawImageCenter";
        case 0x009a: return "SensorPixelSize";
        case 0x009c: return "SceneAssist";
        case 0x009e: return "RetouchHistory";
        case 0x00a0: return "SerialNumber";
        case 0x00a2: return "ImageDataSize";
        case 0x00a5: return "ImageCount";
        case 0x00a6: return "DeletedImageCount";
        case 0x00a7: return "ShutterCount";
        case 0x00a8: return "FlashInfo";
        case 0x00a9: return "ImageOptimization";
        case 0x00aa: return "Saturation2";
        case 0x00ab: return "VariProgram";
        case 0x00ac: return "ImageStabilization";
        case 0x00ad: return "AFResponse";
        case 0x00b0: return "MultiExposure";
        case 0x00b1: return "HighISONoiseReduction";
        case 0x00b6: return "PowerUpTime";
        case 0x00b7: return "AFInfo2";
        case 0x00b8: return "FileInfo";
        case 0x00bb: return "RetouchInfo";
        case 0x00c0: return "PictureControlData";
    }

    return NULL;
} //NikonMakernoteTagName

DWORD NikonMakernoteTypeSize( WORD type )
{
    switch ( type )
    {
        case 1: case 2: case 6: case 7: return 1;
        case 3: case 8: return 2;
        case 4: case 9: case 11: return 4;
        case 5: case 10: case 12: return 8;
    }

    return 0;
} //NikonMakernoteTypeSize

void PrintNikonMakernoteValue( int depth, const char * name, WORD tag, const IFDHeader & head,
                              __int64 valueOffset, bool littleEndian )
{
    Space( depth );
    prf( "makernote Nikon.%s: ", name );

    if ( 2 == head.type )
    {
        char value[ 256 ];
        if ( head.count >= sizeof value || !IsFileRangeValid( valueOffset, head.count ) )
        {
            prf( "%u bytes (string unavailable)\n", head.count );
            return;
        }

        GetString( valueOffset, value, _countof( value ), head.count );
        prf( "%s\n", value );
        return;
    }

    if ( 7 == head.type )
    {
        if ( 0x0001 == tag && head.count <= 16 && IsFileRangeValid( valueOffset, head.count ) )
        {
            char value[ 17 ];
            GetString( valueOffset, value, _countof( value ), head.count );
            if ( 4 == strlen( value ) &&
                 value[ 0 ] >= '0' && value[ 0 ] <= '9' &&
                 value[ 1 ] >= '0' && value[ 1 ] <= '9' )
                prf( "%d.%c%c\n", ( value[ 0 ] - '0' ) * 10 + value[ 1 ] - '0', value[ 2 ], value[ 3 ] );
            else
                prf( "%s\n", value );
        }
        else
            prf( "%u bytes of binary data\n", head.count );
        return;
    }

    if ( !IsIntType( head.type ) && 5 != head.type && 10 != head.type )
    {
        prf( "%u values (TIFF type %u)\n", head.count, head.type );
        return;
    }

    DWORD typeSize = NikonMakernoteTypeSize( head.type );
    if ( 0 == typeSize || head.count > 32 || !IsFileRangeValid( valueOffset, (QWORD) typeSize * head.count ) )
    {
        prf( "%u values (data unavailable)\n", head.count );
        return;
    }

    if ( 1 == head.count && ( 1 == head.type || 3 == head.type || 4 == head.type ) )
    {
        QWORD value = 1 == head.type ? GetBYTE( valueOffset ) :
                      3 == head.type ? GetWORD( valueOffset, littleEndian ) :
                                       GetDWORD( valueOffset, littleEndian );
        if ( 0x001e == tag && 1 == value )
        {
            prf( "sRGB (1)\n" );
            return;
        }
        if ( 0x001e == tag && 2 == value )
        {
            prf( "Adobe RGB (2)\n" );
            return;
        }
        if ( 0x0083 == tag && 0 == value )
        {
            prf( "AF (0)\n" );
            return;
        }
        if ( 0x0083 == tag && 2 == value )
        {
            prf( "D (2)\n" );
            return;
        }
        if ( 0x0083 == tag && 6 == value )
        {
            prf( "G (6)\n" );
            return;
        }
        if ( 0x0087 == tag && 0 == value )
        {
            prf( "Did Not Fire (0)\n" );
            return;
        }
        if ( 0x0093 == tag && 1 == value )
        {
            prf( "Lossy (type 1) (1)\n" );
            return;
        }
        if ( 0x0093 == tag && 2 == value )
        {
            prf( "Uncompressed (2)\n" );
            return;
        }
        if ( 0x0093 == tag && 3 == value )
        {
            prf( "Lossless (3)\n" );
            return;
        }
        if ( 0x0093 == tag && 10 == value )
        {
            prf( "Packed 14 bits (10)\n" );
            return;
        }
        if ( 0x0089 == tag && 0 == value )
        {
            prf( "Single-Frame (0)\n" );
            return;
        }
        if ( 0x0089 == tag && 32 == value )
        {
            prf( "Single-Frame, Auto ISO (32)\n" );
            return;
        }
        if ( 0x00b1 == tag && 6 == value )
        {
            prf( "High (6)\n" );
            return;
        }
    }

    if ( 0x0084 == tag && 5 == head.type && 4 == head.count )
    {
        DWORD minFocalNumerator = GetDWORD( valueOffset, littleEndian );
        DWORD minFocalDenominator = GetDWORD( valueOffset + 4, littleEndian );
        DWORD maxFocalNumerator = GetDWORD( valueOffset + 8, littleEndian );
        DWORD maxFocalDenominator = GetDWORD( valueOffset + 12, littleEndian );
        DWORD minApertureNumerator = GetDWORD( valueOffset + 16, littleEndian );
        DWORD minApertureDenominator = GetDWORD( valueOffset + 20, littleEndian );
        DWORD maxApertureNumerator = GetDWORD( valueOffset + 24, littleEndian );
        DWORD maxApertureDenominator = GetDWORD( valueOffset + 28, littleEndian );
        if ( minFocalDenominator && maxFocalDenominator && minApertureDenominator && maxApertureDenominator )
        {
            double minFocal = (double) minFocalNumerator / minFocalDenominator;
            double maxFocal = (double) maxFocalNumerator / maxFocalDenominator;
            double minAperture = (double) minApertureNumerator / minApertureDenominator;
            double maxAperture = (double) maxApertureNumerator / maxApertureDenominator;
            if ( minFocal == maxFocal && minAperture == maxAperture )
                prf( "%.4g mm f/%.4g\n", minFocal, minAperture );
            else
                prf( "%.4g-%.4g mm, f/%.4g-f/%.4g\n", minFocal, maxFocal, minAperture, maxAperture );
            return;
        }
    }

    if ( 0x009a == tag && 5 == head.type && 2 == head.count )
    {
        DWORD widthNumerator = GetDWORD( valueOffset, littleEndian );
        DWORD widthDenominator = GetDWORD( valueOffset + 4, littleEndian );
        DWORD heightNumerator = GetDWORD( valueOffset + 8, littleEndian );
        DWORD heightDenominator = GetDWORD( valueOffset + 12, littleEndian );
        if ( widthDenominator && heightDenominator )
        {
            prf( "%.4g x %.4g um\n",
                 (double) widthNumerator / widthDenominator,
                 (double) heightNumerator / heightDenominator );
            return;
        }
    }

    DWORD count = head.count;
    for ( DWORD i = 0; i < count; i++ )
    {
        if ( 0 != i )
            prf( ", " );

        __int64 offset = valueOffset + (__int64) i * typeSize;
        if ( 1 == head.type || 6 == head.type )
            prf( "%d", 6 == head.type ? (char) GetBYTE( offset ) : GetBYTE( offset ) );
        else if ( 3 == head.type || 8 == head.type )
        {
            WORD value = GetWORD( offset, littleEndian );
            prf( "%d", 8 == head.type ? (short) value : value );
        }
        else if ( 4 == head.type || 9 == head.type )
        {
            DWORD value = GetDWORD( offset, littleEndian );
            prf( "%lld", (long long) ( 9 == head.type ? (LONG) value : value ) );
        }
        else
        {
            DWORD numerator = GetDWORD( offset, littleEndian );
            DWORD denominator = GetDWORD( offset + 4, littleEndian );
            if ( 10 == head.type )
            {
                if ( (LONG) denominator )
                    prf( "%.6g", (double) (LONG) numerator / (LONG) denominator );
                else
                    prf( "%d/0", (LONG) numerator );
            }
            else
            {
                if ( denominator )
                    prf( "%.6g", (double) numerator / denominator );
                else
                    prf( "%u/0", numerator );
            }
        }
    }

    prf( "\n" );
} //PrintNikonMakernoteValue

void EnumerateNikonMakernotes( int depth, __int64 IFDOffset, __int64 headerBase, bool littleEndian )
{
    // https://www.exiv2.org/tags-nikon.html

    char acBuffer[ 100 ];
    __int64 originalNikonMakernotesOffset = IFDOffset - 8;
    vector<IFDHeader> aHeaders( MaxIFDHeaders );

    while ( 0 != IFDOffset ) 
    {
        if ( !IsFileRangeValid( IFDOffset + headerBase, sizeof( WORD ) ) )
        {
            Space( depth );
            prf( "nikon makernote IFD header is outside the file; skipping\n" );
            break;
        }

        WORD NumTags = GetWORD( IFDOffset + headerBase, littleEndian );
        Space( depth );
        prf( "nikon makernote IFDOffset %#llx %lld, NumTags %d, headerBase %#llx %lld\n", IFDOffset, IFDOffset, NumTags, headerBase, headerBase );
        IFDOffset += 2;

        if ( NumTags > MaxIFDHeaders )
        {
            Space( depth );
            prf( "numtags is > MaxIFDHeaders; it's likely not in EXIF format, so the data may be noise. skipping (%d)\n", NumTags );
            break;
        }

        if ( !IsFileRangeValid( IFDOffset - 2 + headerBase,
                                2 + (__int64) NumTags * sizeof( IFDHeader ) + sizeof( DWORD ) ) )
        {
            Space( depth );
            prf( "nikon makernote IFD is outside the file; skipping\n" );
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
                char unknownTagName[ 32 ];
                const char * tagName = NikonMakernoteTagName( head.id );
                DWORD typeSize = NikonMakernoteTypeSize( head.type );
                QWORD valueSize = (QWORD) typeSize * head.count;
                __int64 valueOffset = valueSize <= sizeof( head.offset )
                    ? IFDOffset - 4 + headerBase
                    : (__int64) head.offset + originalNikonMakernotesOffset + headerBase;
                if ( NULL == tagName )
                {
                    sprintf_s( unknownTagName, _countof( unknownTagName ), "Tag%04X", head.id );
                    tagName = unknownTagName;
                }

                PrintNikonMakernoteValue( depth, tagName, head.id, head, valueOffset, littleEndian );
            }
        }

        IFDOffset = GetDWORD( IFDOffset + headerBase, littleEndian );
    }
} //EnumerateNikonMakernotes

const char * CanonCameraSettingsTagName( DWORD tag )
{
    switch ( tag )
    {
        case 1:  return "MacroMode";
        case 2:  return "SelfTimer";
        case 3:  return "Quality";
        case 4:  return "CanonFlashMode";
        case 5:  return "ContinuousDrive";
        case 7:  return "FocusMode";
        case 9:  return "RecordMode";
        case 10: return "CanonImageSize";
        case 11: return "EasyMode";
        case 12: return "DigitalZoom";
        case 13: return "Contrast";
        case 14: return "Saturation";
        case 15: return "Sharpness";
        case 16: return "CameraISO";
        case 17: return "MeteringMode";
        case 18: return "FocusRange";
        case 19: return "AFPoint";
        case 20: return "CanonExposureMode";
        case 22: return "LensType";
        case 23: return "MaxFocalLength";
        case 24: return "MinFocalLength";
        case 25: return "FocalUnits";
        case 26: return "MaxAperture";
        case 27: return "MinAperture";
        case 28: return "FlashActivity";
        case 29: return "FlashBits";
        case 32: return "FocusContinuous";
        case 33: return "AESetting";
        case 34: return "ImageStabilization";
        case 35: return "DisplayAperture";
        case 36: return "ZoomSourceWidth";
        case 37: return "ZoomTargetWidth";
        case 39: return "SpotMeteringMode";
        case 40: return "PhotoEffect";
        case 41: return "ManualFlashOutput";
        case 42: return "ColorTone";
        case 46: return "SRAWQuality";
    }

    return NULL;
} //CanonCameraSettingsTagName

const char * CanonShotInfoTagName( DWORD tag )
{
    switch ( tag )
    {
        case 1:  return "AutoISO";
        case 2:  return "BaseISO";
        case 3:  return "MeasuredEV";
        case 4:  return "TargetAperture";
        case 5:  return "TargetExposureTime";
        case 6:  return "ExposureCompensation";
        case 7:  return "WhiteBalance";
        case 8:  return "SlowShutter";
        case 9:  return "SequenceNumber";
        case 10: return "OpticalZoomCode";
        case 12: return "CameraTemperature";
        case 13: return "FlashGuideNumber";
        case 14: return "AFPointsInFocus";
        case 15: return "FlashExposureComp";
        case 16: return "AutoExposureBracketing";
        case 17: return "AEBBracketValue";
        case 18: return "ControlMode";
        case 20: return "FocusDistanceUpper";
        case 21: return "FNumber";
        case 22: return "ExposureTime";
        case 23: return "MeasuredEV2";
        case 24: return "BulbDuration";
        case 26: return "CameraType";
        case 27: return "AutoRotate";
        case 28: return "NDFilter";
        case 29: return "SelfTimer2";
        case 33: return "FlashOutput";
    }

    return NULL;
} //CanonShotInfoTagName

const char * CanonFileInfoTagName( DWORD tag )
{
    switch ( tag )
    {
        case 3:  return "BracketMode";
        case 4:  return "BracketValue";
        case 5:  return "BracketShotNumber";
        case 6:  return "RawJpgQuality";
        case 7:  return "RawJpgSize";
        case 8:  return "LongExposureNoiseReduction2";
        case 9:  return "WBBracketMode";
        case 12: return "WBBracketValueAB";
        case 13: return "WBBracketValueGM";
        case 14: return "FilterEffect";
        case 15: return "ToningEffect";
        case 19: return "LiveViewShooting";
        case 20: return "FocusDistanceUpper";
        case 21: return "FocusDistanceLower";
        case 25: return "FlashExposureLock";
        case 61: return "RFLensType";
    }

    return NULL;
} //CanonFileInfoTagName

bool EnumerateCanonPackedShorts( int depth, const char * name, __int64 offset, __int64 headerBase, DWORD count, bool littleEndian, bool shotInfo )
{
    if ( g_FullInformation || count < 2 ||
         GetWORD( offset + headerBase, littleEndian ) != (unsigned __int64) count * sizeof( WORD ) )
        return false;

    Space( depth );
    prf( "  %s:\n", name );

    for ( DWORD tag = 1; tag < count; tag++ )
    {
        WORD value = GetWORD( offset + headerBase + tag * sizeof( WORD ), littleEndian );
        const char * tagName = shotInfo ? CanonShotInfoTagName( tag ) : CanonCameraSettingsTagName( tag );

        if ( NULL != tagName )
        {
            Space( depth + 1 );
            if ( shotInfo && 2 == tag )
            {
                if ( 0 != value && 0xffff != value )
                    prf( "%-30s : ISO %.0lf (raw %u)\n", tagName, 100.0 * pow( 2.0, (double) value / 32.0 - 5.0 ), value );
                else
                    prf( "%-30s : %u (raw; unavailable)\n", tagName, value );
            }
            else if ( shotInfo && ( 4 == tag || 21 == tag ) )
            {
                if ( 0xffff != value )
                    prf( "%-30s : f/%.1lf (raw %u)\n", tagName, pow( 2.0, (double) value / 64.0 ), value );
                else
                    prf( "%-30s : %u (raw; unavailable)\n", tagName, value );
            }
            else if ( shotInfo && ( 5 == tag || 22 == tag ) )
            {
                short signedValue = (short) value;
                if ( 0xffff == value )
                    prf( "%-30s : %u (raw; unavailable)\n", tagName, value );
                else if ( signedValue < 0 )
                    prf( "%-30s : %.0lf s (raw %u)\n", tagName, pow( 2.0, -(double) signedValue / 32.0 ), value );
                else
                    prf( "%-30s : 1/%.0lf s (raw %u)\n", tagName, pow( 2.0, (double) signedValue / 32.0 ), value );
            }
            else if ( shotInfo && 3 == tag )
                prf( "%-30s : %u (raw; scaling varies by camera)\n", tagName, value );
            else if ( shotInfo && 12 == tag )
            {
                if ( 0 != value && 0xffff != value )
                    prf( "%-30s : %d C (raw %u)\n", tagName, (int) value - 128, value );
                else
                    prf( "%-30s : %u (raw; unavailable)\n", tagName, value );
            }
            else
                prf( "%-30s : %u (raw)\n", tagName, value );
        }
        else if ( 0 != value && 0xffff != value && 0x7fff != value )
        {
            Space( depth + 1 );
            prf( "Undocumented tag %-12u %#x\n", tag, value );
        }
    }

    return true;
} //EnumerateCanonPackedShorts

bool EnumerateCanonFileInfo( int depth, __int64 offset, __int64 headerBase, DWORD count, bool littleEndian )
{
    if ( g_FullInformation || count < 2 ||
         GetWORD( offset + headerBase, littleEndian ) != (unsigned __int64) count * sizeof( WORD ) )
        return false;

    Space( depth );
    prf( "  CanonFileInfo:\n" );
    for ( DWORD tag = 1; tag < count; tag++ )
    {
        const char * tagName = CanonFileInfoTagName( tag );
        if ( NULL == tagName )
            continue;

        WORD value = GetWORD( offset + headerBase + tag * sizeof( WORD ), littleEndian );
        Space( depth + 1 );
        prf( "%-30s : %u (raw)\n", tagName, value );
    }

    return true;
} //EnumerateCanonFileInfo

bool EnumerateCanonAFInfo2( int depth, __int64 offset, __int64 headerBase, DWORD wordCount, bool littleEndian )
{
    if ( g_FullInformation || wordCount < 8 ||
         GetWORD( offset + headerBase, littleEndian ) != (unsigned __int64) wordCount * sizeof( WORD ) )
        return false;

    WORD numAFPoints = GetWORD( offset + headerBase + 2 * sizeof( WORD ), littleEndian );
    unsigned __int64 bitmapWords = ( numAFPoints + 15 ) / 16;
    unsigned __int64 requiredWords = 8 + 4 * (unsigned __int64) numAFPoints + 2 * bitmapWords;
    if ( 0 == numAFPoints || requiredWords > wordCount )
        return false;

    Space( depth );
    prf( "  AFInfo2:\n" );
    static const char * names[] =
    {
        "AFAreaMode", "NumAFPoints", "ValidAFPoints", "CanonImageWidth",
        "CanonImageHeight", "AFImageWidth", "AFImageHeight"
    };
    for ( DWORD field = 1; field < _countof( names ) + 1; field++ )
    {
        Space( depth + 1 );
        prf( "%-30s : %u\n", names[ field - 1 ], GetWORD( offset + headerBase + field * sizeof( WORD ), littleEndian ) );
    }

    unsigned __int64 widthsStart = 8;
    unsigned __int64 heightsStart = widthsStart + numAFPoints;
    unsigned __int64 xPositionsStart = heightsStart + numAFPoints;
    unsigned __int64 yPositionsStart = xPositionsStart + numAFPoints;
    DWORD activeAreas = 0;
    for ( DWORD point = 0; point < numAFPoints; point++ )
    {
        WORD width = GetWORD( offset + headerBase + ( widthsStart + point ) * sizeof( WORD ), littleEndian );
        WORD height = GetWORD( offset + headerBase + ( heightsStart + point ) * sizeof( WORD ), littleEndian );
        short x = (short) GetWORD( offset + headerBase + ( xPositionsStart + point ) * sizeof( WORD ), littleEndian );
        short y = (short) GetWORD( offset + headerBase + ( yPositionsStart + point ) * sizeof( WORD ), littleEndian );

        if ( 0 == width && 0 == height && 0 == x && 0 == y )
            continue;

        if ( 0 == activeAreas )
        {
            Space( depth + 1 );
            prf( "AF areas (index, width, height, x, y):\n" );
        }
        Space( depth + 2 );
        prf( "%u: %u, %u, %d, %d\n", point, width, height, x, y );
        activeAreas++;
    }

    return true;
} //EnumerateCanonAFInfo2

void EnumerateCanonMakernotes( int depth, __int64 IFDOffset, __int64 headerBase, bool littleEndian )
{
    char acBuffer[ 100 ];
    vector<IFDHeader> aHeaders( MaxIFDHeaders );

    while ( 0 != IFDOffset ) 
    {
        WORD NumTags = GetWORD( IFDOffset + headerBase, littleEndian );
        Space( depth );
        prf( "canon makernote IFDOffset %#llx %lld, NumTags %d, headerBase %#llx %lld\n", IFDOffset, IFDOffset, NumTags, headerBase, headerBase );
        IFDOffset += 2;

        if ( NumTags > MaxIFDHeaders )
        {
            Space( depth );
            prf( "numtags is > MaxIFDHeaders; it's likely not in EXIF format, so the data may be noise. skipping (%d)\n", NumTags );
            break;
        }
    
        if ( !GetIFDHeaders( IFDOffset + headerBase, aHeaders.data(), NumTags, littleEndian ) )
        {
            Space( depth );
            prf( "canon makernotes has a tag that's invalid. skipping\n" );
            break;
        }

        for ( int i = 0; i < NumTags; i++ )
        {
            IFDHeader & head = aHeaders[ i ];
            IFDOffset += sizeof IFDHeader;

            if ( g_FullInformation )
            {
                Space( depth );
                prf( "  CanonMakernote tag %d ID %u, type %u, count %u, offset/value %u\n",
                     i, (unsigned) head.id, (unsigned) head.type, (unsigned) head.count, (unsigned) head.offset );
            }

            bool packedTagDecoded = false;
            if ( 1 == head.id && 3 == head.type )
                packedTagDecoded = EnumerateCanonPackedShorts( depth, "CameraSettings", head.offset, headerBase, head.count, littleEndian, false );
            else if ( 4 == head.id && 3 == head.type )
                packedTagDecoded = EnumerateCanonPackedShorts( depth, "ShotInfo", head.offset, headerBase, head.count, littleEndian, true );
            else if ( 38 == head.id && 3 == head.type )
                packedTagDecoded = EnumerateCanonAFInfo2( depth, head.offset, headerBase, head.count, littleEndian );
            else if ( 147 == head.id && 3 == head.type )
                packedTagDecoded = EnumerateCanonFileInfo( depth, head.offset, headerBase, head.count, littleEndian );

            if ( packedTagDecoded )
                continue;

            Space( depth );

            if ( 6 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "  ImageType:                      %s\n", acBuffer );
            }
            else if ( 7 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "  FirmwareVersion:                %s\n", acBuffer );
            }
            else if ( 149 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "  LensModel:                      %s\n", acBuffer );
            }
            else if ( 150 == head.id && 2 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "  InternalSerialNumber:           %s\n", acBuffer );
            }
            else if ( 56 == head.id && 7 == head.type && head.count > 4 )
            {
                GetString( head.offset + headerBase + 4, acBuffer, _countof( acBuffer ), min( head.count - 4, (DWORD) _countof( acBuffer ) - 1 ) );
                prf( "  BatteryType:                    %s\n", acBuffer );
            }
            else if ( 151 == head.id && 7 == head.type )
            {
                prf( "  DustRemovealData:\n" );
                DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
            }
            else if ( 152 == head.id && 3 == head.type && 4 == head.count )
            {
                prf( "  CropInfo:\n" );
                static const char * cropNames[] = { "CropLeftMargin", "CropRightMargin", "CropTopMargin", "CropBottomMargin" };
                for ( DWORD crop = 0; crop < _countof( cropNames ); crop++ )
                {
                    Space( depth + 1 );
                    prf( "%s:                        %u\n", cropNames[ crop ], GetWORD( head.offset + headerBase + crop * sizeof( WORD ), littleEndian ) );
                }
            }
            else if ( 16 == head.id && 4 == head.type && 1 == head.count )
            {
                prf( "  CanonModelID:                    0x%08X (%u)\n",
                     (unsigned) head.offset, (unsigned) head.offset );
            }
            else
            {
                prf( "  CanonMakernote tag %d ID %u==%#x, type %u, count %u, offset/value %u: ",
                     i, (unsigned) head.id, (unsigned) head.id, (unsigned) head.type,
                     (unsigned) head.count, (unsigned) head.offset );

                if ( 2 == head.type )
                {
                    ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                    GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                    prf( " %s\n", acBuffer );
                }
                else if ( 3 == head.type )
                {
                    if ( 1 == head.count )
                        prf( " %u\n", (unsigned) head.offset );
                    else
                    {
                        prf( "\n" );
                        DumpShortData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
                    }

                }
                else if ( 4 == head.type )
                {
                    if ( 1 == head.count )
                        prf( " %u\n", (unsigned) head.offset );
                    else
                    {
                        prf( "\n" );
                        DumpLongData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
                    }

                }
                else if ( 7 == head.type )
                {
                    prf( "\n" );
                    DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
                }
                else
                    prf( "\n" );
            }
        }

        IFDOffset = GetDWORD( IFDOffset + headerBase, littleEndian );
    }
} //EnumerateCanonMakernotes

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

const char * PanasonicMakerNoteTagName( WORD tag )
{
    struct TagName { WORD id; const char * name; };
    static const TagName names[] = {
        { 1, "ImageQuality" }, { 2, "FirmwareVersion" }, { 3, "WhiteBalance" },
        { 7, "FocusMode" }, { 15, "AFAreaMode" }, { 26, "ImageStabilization" },
        { 28, "MacroMode" }, { 31, "ShootingMode" }, { 32, "Audio" },
        { 33, "DataDump" }, { 35, "WhiteBalanceBias" }, { 36, "FlashBias" },
        { 37, "InternalSerialNumber" }, { 38, "PanasonicExifVersion" },
        { 39, "VideoFrameRate" }, { 40, "ColorEffect" }, { 41, "TimeSincePowerOn" },
        { 42, "BurstMode" }, { 43, "SequenceNumber" }, { 44, "ContrastMode" },
        { 45, "NoiseReduction" }, { 46, "SelfTimer" }, { 48, "Rotation" },
        { 49, "AFAssistLamp" }, { 50, "ColorMode" }, { 51, "BabyAge" },
        { 52, "OpticalZoomMode" }, { 53, "ConversionLens" }, { 54, "TravelDay" },
        { 56, "BatteryLevel" }, { 57, "Contrast" }, { 58, "WorldTimeLocation" },
        { 59, "TextStamp" }, { 60, "ProgramISO" }, { 61, "AdvancedSceneType" },
        { 62, "TextStamp" }, { 63, "FacesDetected" }, { 64, "Saturation" },
        { 65, "Sharpness" }, { 66, "FilmMode" }, { 67, "JPEGQuality" },
        { 68, "ColorTempKelvin" }, { 69, "BracketSettings" }, { 70, "WBShiftAB" },
        { 71, "WBShiftGM" }, { 72, "FlashCurtain" }, { 73, "LongExposureNoiseReduction" },
        { 75, "PanasonicImageWidth" }, { 76, "PanasonicImageHeight" },
        { 77, "AFPointPosition" }, { 81, "LensType" }, { 82, "LensSerialNumber" },
        { 83, "AccessoryType" }, { 84, "AccessorySerialNumber" }, { 89, "Transform" },
        { 93, "IntelligentExposure" }, { 96, "LensFirmwareVersion" }, { 98, "FlashWarning" },
        { 99, "RecognizedFaceFlags" }, { 101, "Title" }, { 102, "BabyName" },
        { 103, "Location" }, { 105, "Country" }, { 107, "State" }, { 109, "City" },
        { 111, "Landmark" }, { 112, "IntelligentResolution" }, { 118, "HDRShot" },
        { 119, "BurstSpeed" }, { 121, "IntelligentD-Range" }, { 124, "ClearRetouch" },
        { 128, "City2" }, { 134, "ManometerPressure" }, { 137, "PhotoStyle" },
        { 138, "ShadingCompensation" }, { 139, "WBShiftIntelligentAuto" },
        { 140, "AccelerometerZ" }, { 141, "AccelerometerX" }, { 142, "AccelerometerY" },
        { 143, "CameraOrientation" }, { 144, "RollAngle" }, { 145, "PitchAngle" },
        { 146, "WBShiftCreativeControl" }, { 147, "SweepPanoramaDirection" },
        { 148, "SweepPanoramaFieldOfView" }, { 150, "TimerRecording" },
        { 157, "InternalNDFilter" }, { 158, "HDR" }, { 159, "ShutterType" },
        { 161, "FilterEffect" }, { 163, "ClearRetouchValue" }, { 167, "OutputLUT" },
        { 171, "TouchAE" }, { 172, "MonochromeFilterEffect" }, { 173, "HighlightShadow" },
        { 175, "TimeStamp" }, { 179, "VideoBurstResolution" }, { 180, "MultiExposure" },
        { 185, "RedEyeRemoval" }, { 187, "VideoBurstMode" }, { 188, "DiffractionCorrection" },
        { 189, "FocusBracket" }, { 190, "LongExposureNRUsed" }, { 191, "PostFocusMerging" },
        { 193, "VideoPreburst" }, { 196, "LensTypeMake" }, { 197, "LensTypeModel" },
        { 202, "SensorType" }, { 209, "ISO" }, { 210, "MonochromeGrainEffect" },
        { 214, "NoiseReductionStrength" }, { 32768, "MakerNoteVersion" },
        { 32769, "SceneMode" }, { 32770, "HighlightWarning" },
        { 32771, "DarkFocusEnvironment" }, { 32772, "WBRedLevel" },
        { 32773, "WBGreenLevel" }, { 32774, "WBBlueLevel" },
        { 32776, "TextStamp" }, { 32777, "TextStamp" }, { 32784, "BabyAge" },
        { 32786, "Transform" }
    };

    for ( size_t i = 0; i < _countof( names ); i++ )
        if ( names[ i ].id == tag )
            return names[ i ].name;
    return NULL;
}

const char * PanasonicMakerNoteValueName( WORD tag, DWORD value )
{
    if ( 1 == tag )
    {
        if ( 1 == value ) return "TIFF";
        if ( 2 == value ) return "High";
        if ( 3 == value ) return "Normal";
        if ( 6 == value ) return "Very High";
        if ( 7 == value ) return "RAW";
        if ( 9 == value ) return "Motion Picture";
        if ( 11 == value ) return "Full HD Movie";
        if ( 12 == value ) return "4K Movie";
    }
    else if ( 3 == tag )
    {
        if ( 1 == value ) return "Auto";
        if ( 2 == value ) return "Daylight";
        if ( 3 == value ) return "Cloudy";
        if ( 4 == value ) return "Incandescent";
        if ( 5 == value ) return "Manual";
        if ( 8 == value ) return "Flash";
        if ( 10 == value ) return "Black & White";
        if ( 11 == value ) return "Manual 2";
        if ( 12 == value ) return "Shade";
        if ( 13 == value ) return "Kelvin";
        if ( 14 == value ) return "Manual 3";
        if ( 15 == value ) return "Manual 4";
        if ( 19 == value ) return "Auto (cool)";
    }
    else if ( 7 == tag )
    {
        if ( 1 == value ) return "Auto";
        if ( 2 == value ) return "Manual";
        if ( 4 == value ) return "Auto, Focus button";
        if ( 5 == value ) return "Auto, Continuous";
        if ( 6 == value ) return "AF-S";
        if ( 7 == value ) return "AF-C";
        if ( 8 == value ) return "AF-F";
    }
    else if ( 44 == tag )
    {
        if ( 0 == value ) return "Normal";
        if ( 1 == value ) return "Low";
        if ( 2 == value ) return "High";
        if ( 5 == value ) return "Normal 2";
        if ( 6 == value ) return "Medium Low";
        if ( 7 == value ) return "Medium High";
        if ( 13 == value ) return "High Dynamic";
        if ( 24 == value ) return "Dynamic Range (film-like)";
        if ( 46 == value ) return "Match Filter Effects Toy";
        if ( 55 == value ) return "Match Photo Style L. Monochrome";
        if ( 256 == value ) return "Low";
        if ( 272 == value ) return "Normal";
        if ( 288 == value ) return "High";
    }
    else if ( 45 == tag )
    {
        if ( 0 == value ) return "Standard";
        if ( 1 == value ) return "Low (-1)";
        if ( 2 == value ) return "High (+1)";
        if ( 3 == value ) return "Lowest (-2)";
        if ( 4 == value ) return "Highest (+2)";
        if ( 5 == value ) return "+5";
        if ( 6 == value ) return "+6";
        if ( 65531 == value ) return "-5";
        if ( 65532 == value ) return "-4";
        if ( 65533 == value ) return "-3";
        if ( 65534 == value ) return "-2";
        if ( 65535 == value ) return "-1";
    }
    else if ( 26 == tag )
    {
        if ( 2 == value ) return "On, Optical";
        if ( 3 == value ) return "Off";
        if ( 4 == value ) return "On, Mode 2";
        if ( 5 == value ) return "On, Optical Panning";
        if ( 6 == value ) return "On, Body-only";
        if ( 7 == value ) return "On, Body-only Panning";
        if ( 9 == value ) return "Dual IS";
        if ( 10 == value ) return "Dual IS Panning";
        if ( 11 == value ) return "Dual2 IS";
        if ( 12 == value ) return "Dual2 IS Panning";
    }
    else if ( 28 == tag )
    {
        if ( 1 == value ) return "On";
        if ( 2 == value ) return "Off";
        if ( 257 == value ) return "Tele-Macro";
        if ( 513 == value ) return "Macro Zoom";
    }
    else if ( 31 == tag )
    {
        if ( 1 == value ) return "Normal";
        if ( 2 == value ) return "Portrait";
        if ( 3 == value ) return "Scenery";
        if ( 4 == value ) return "Sports";
        if ( 5 == value ) return "Night Portrait";
        if ( 6 == value ) return "Program";
        if ( 7 == value ) return "Aperture Priority";
        if ( 8 == value ) return "Shutter Priority";
        if ( 9 == value ) return "Macro";
        if ( 10 == value ) return "Spot";
        if ( 11 == value ) return "Manual";
        if ( 21 == value ) return "Night Scenery";
        if ( 22 == value ) return "Food";
        if ( 29 == value ) return "Underwater";
        if ( 34 == value ) return "Intelligent ISO";
        if ( 37 == value ) return "Intelligent Auto";
        if ( 48 == value ) return "Movie";
        if ( 51 == value ) return "HDR";
        if ( 59 == value ) return "Creative Control";
        if ( 60 == value ) return "Intelligent Auto Plus";
        if ( 62 == value ) return "Panorama";
    }
    else if ( 32 == tag )
    {
        if ( 1 == value ) return "Yes";
        if ( 2 == value ) return "No";
        if ( 3 == value ) return "Stereo";
    }
    else if ( 39 == tag && 0 == value )
        return "n/a";
    else if ( 40 == tag )
    {
        if ( 1 == value ) return "Off";
        if ( 2 == value ) return "Warm";
        if ( 3 == value ) return "Cool";
        if ( 4 == value ) return "Black & White";
        if ( 5 == value ) return "Sepia";
        if ( 6 == value ) return "Happy";
        if ( 8 == value ) return "Vivid";
    }
    else if ( 42 == tag )
    {
        if ( 0 == value ) return "Off";
        if ( 1 == value ) return "On";
        if ( 2 == value ) return "Auto Exposure Bracketing (AEB)";
        if ( 3 == value ) return "Focus Bracketing";
        if ( 4 == value ) return "Unlimited";
        if ( 8 == value ) return "White Balance Bracketing";
    }
    else if ( 46 == tag )
    {
        if ( 0 == value || 1 == value ) return "Off";
        if ( 2 == value ) return "10 s";
        if ( 3 == value ) return "2 s";
        if ( 4 == value ) return "10 s / 3 pictures";
    }
    else if ( 48 == tag )
    {
        if ( 1 == value ) return "Horizontal (normal)";
        if ( 3 == value ) return "Rotate 180";
        if ( 6 == value ) return "Rotate 90 CW";
        if ( 8 == value ) return "Rotate 270 CW";
    }
    else if ( 49 == tag )
    {
        if ( 1 == value ) return "Fired";
        if ( 2 == value ) return "Enabled but Not Used";
        if ( 3 == value ) return "Disabled but Required";
        if ( 4 == value ) return "Disabled and Not Required";
    }
    else if ( 50 == tag )
    {
        if ( 0 == value ) return "Normal";
        if ( 1 == value ) return "Natural";
        if ( 2 == value ) return "Vivid";
    }
    else if ( 52 == tag )
    {
        if ( 1 == value ) return "Standard";
        if ( 2 == value ) return "Extended";
    }
    else if ( 53 == tag )
    {
        if ( 1 == value ) return "Off";
        if ( 2 == value ) return "Wide";
        if ( 3 == value ) return "Telephoto";
        if ( 4 == value ) return "Macro";
    }
    else if ( 54 == tag && 65535 == value )
        return "n/a";
    else if ( 56 == tag && 256 == value )
        return "n/a";
    else if ( 56 == tag )
    {
        if ( 1 == value ) return "Full";
        if ( 2 == value ) return "Medium";
        if ( 3 == value ) return "Low";
        if ( 4 == value ) return "Near Empty";
        if ( 7 == value ) return "Near Full";
        if ( 8 == value ) return "Medium Low";
    }
    else if ( ( 57 == tag || 64 == tag || 65 == tag ) && 0 == value )
        return "Normal";
    else if ( 58 == tag )
    {
        if ( 1 == value ) return "Home";
        if ( 2 == value ) return "Destination";
    }
    else if ( ( 59 == tag || 62 == tag || 73 == tag ||
                118 == tag || 124 == tag || 138 == tag || 171 == tag ) )
    {
        if ( 0 == value || ( ( 59 == tag || 62 == tag || 73 == tag ) && 1 == value ) )
            return "Off";
        if ( 1 == value || ( ( 59 == tag || 62 == tag || 73 == tag ) && 2 == value ) )
            return "On";
    }
    else if ( 60 == tag )
    {
        if ( 0xffff == value ) return "n/a";
        if ( 0xfffe == value ) return "Intelligent ISO";
    }
    else if ( 67 == tag )
    {
        if ( 0 == value ) return "n/a (Movie)";
        if ( 2 == value ) return "High";
        if ( 3 == value ) return "Standard";
        if ( 6 == value ) return "Very High";
        if ( 255 == value ) return "n/a (RAW only)";
    }
    else if ( 72 == tag )
    {
        if ( 0 == value ) return "n/a";
        if ( 1 == value ) return "1st";
        if ( 2 == value ) return "2nd";
    }
    else if ( 93 == tag )
    {
        if ( 0 == value ) return "Off";
        if ( 1 == value ) return "Low";
        if ( 2 == value ) return "Standard";
        if ( 3 == value ) return "High";
    }
    else if ( 98 == tag )
    {
        if ( 0 == value ) return "No";
        if ( 1 == value ) return "Yes (flash required but disabled)";
    }
    else if ( 112 == tag )
    {
        if ( 0 == value ) return "Off";
        if ( 1 == value ) return "Low";
        if ( 2 == value ) return "Standard";
        if ( 3 == value ) return "High";
        if ( 4 == value ) return "Extended";
    }
    else if ( 121 == tag )
    {
        if ( 0 == value ) return "Off";
        if ( 1 == value ) return "Low";
        if ( 2 == value ) return "Standard";
        if ( 3 == value ) return "High";
    }
    else if ( 137 == tag )
    {
        if ( 0 == value ) return "Auto";
        if ( 1 == value ) return "Standard or Custom";
        if ( 2 == value ) return "Vivid";
        if ( 3 == value ) return "Natural";
        if ( 4 == value ) return "Monochrome";
        if ( 5 == value ) return "Scenery";
        if ( 6 == value ) return "Portrait";
        if ( 8 == value ) return "Cinelike D";
        if ( 9 == value ) return "Cinelike V";
        if ( 11 == value ) return "L. Monochrome";
        if ( 15 == value ) return "L. Monochrome D";
    }
    else if ( 143 == tag )
    {
        if ( 0 == value ) return "Normal";
        if ( 1 == value ) return "Rotate CW";
        if ( 2 == value ) return "Rotate 180";
        if ( 3 == value ) return "Rotate CCW";
        if ( 4 == value ) return "Tilt Upwards";
        if ( 5 == value ) return "Tilt Downwards";
    }
    else if ( 147 == tag )
    {
        if ( 0 == value ) return "Off";
        if ( 1 == value ) return "Left to Right";
        if ( 2 == value ) return "Right to Left";
        if ( 3 == value ) return "Top to Bottom";
        if ( 4 == value ) return "Bottom to Top";
    }
    else if ( 150 == tag )
    {
        if ( 0 == value ) return "Off";
        if ( 1 == value ) return "Time Lapse";
        if ( 2 == value ) return "Stop-motion Animation";
        if ( 3 == value ) return "Focus Bracketing";
    }
    else if ( 158 == tag )
    {
        if ( 0 == value ) return "Off";
        if ( 100 == value ) return "1 EV";
        if ( 200 == value ) return "2 EV";
        if ( 300 == value ) return "3 EV";
        if ( 32868 == value ) return "1 EV (Auto)";
        if ( 32968 == value ) return "2 EV (Auto)";
        if ( 33068 == value ) return "3 EV (Auto)";
    }
    else if ( 159 == tag )
    {
        if ( 0 == value ) return "Mechanical";
        if ( 1 == value ) return "Electronic";
        if ( 2 == value ) return "Hybrid";
    }

    return NULL;
}

void PrintPanasonicMakerNoteTag( int depth, const IFDHeader & head,
                                 __int64 inlineValueOffset, __int64 headerBase, bool littleEndian )
{
    const char * name = PanasonicMakerNoteTagName( head.id );
    DWORD valueSize = ( 1 == head.type || 2 == head.type || 6 == head.type || 7 == head.type ) ? 1 :
                      ( 3 == head.type || 8 == head.type ) ? 2 :
                      ( 4 == head.type || 9 == head.type ) ? 4 :
                      ( 5 == head.type || 10 == head.type ) ? 8 : 0;
    unsigned __int64 byteCount = (unsigned __int64) head.count * valueSize;
    __int64 valuesOffset = ( byteCount <= 4 )
        ? inlineValueOffset + headerBase
        : (__int64) head.offset + headerBase;

    Space( depth );
    if ( 0 == valueSize || 0 == head.count || byteCount > INT_MAX ||
         !IsFileRangeValid( valuesOffset, (__int64) byteCount ) )
    {
        prf( "Panasonic %s: (invalid or unsupported data; type %u, count %u)\n",
             name, head.type, head.count );
        return;
    }

    if ( ( 2 == head.id || 96 == head.id ) && 7 == head.type && 4 == head.count )
    {
        BYTE version[ 4 ];
        GetBytes( valuesOffset, version, sizeof version );
        prf( "Panasonic %s: %u.%u.%u.%u\n", name,
             version[ 0 ], version[ 1 ], version[ 2 ], version[ 3 ] );
        return;
    }

    bool textTag = ( 37 == head.id || 38 == head.id || 51 == head.id ||
                     ( head.id >= 81 && head.id <= 84 ) || 96 == head.id ||
                     ( head.id >= 101 && head.id <= 111 ) || 128 == head.id ||
                     175 == head.id || 32768 == head.id || 32784 == head.id );
    if ( 2 == head.type || ( 7 == head.type && textTag ) )
    {
        vector<char> text( min( head.count, 1024u ) + 1, 0 );
        DWORD textLength = min( head.count, (DWORD) text.size() - 1 );
        GetBytes( valuesOffset, text.data(), (int) textLength );
        for ( DWORD i = 0; i < textLength; i++ )
        {
            unsigned char ch = (unsigned char) text[ i ];
            if ( 0 == ch )
                break;
            if ( ch < ' ' || ch > '~' )
                text[ i ] = '?';
        }
        prf( "Panasonic %s: %s\n", name, text.data() );
        return;
    }

    if ( 7 == head.type )
    {
        prf( "Panasonic %s: (binary data, %u bytes)\n", name, head.count );
        return;
    }

    DWORD valuesToPrint = min( head.count, 16u );
    prf( "Panasonic %s: ", name );
    for ( DWORD i = 0; i < valuesToPrint; i++ )
    {
        if ( i )
            prf( ", " );

        __int64 valueOffset = valuesOffset + (__int64) i * valueSize;
        DWORD value = 0;
        if ( 1 == head.type )
            value = GetBYTE( valueOffset );
        else if ( 3 == head.type )
            value = GetWORD( valueOffset, littleEndian );
        else if ( 4 == head.type )
            value = GetDWORD( valueOffset, littleEndian );

        if ( 1 == head.count && ( 1 == head.type || 3 == head.type || 4 == head.type ) )
        {
            const char * valueName =
                ( 44 == head.id && 2 == value && 0 == strcmp( g_acModel, "DMC-GF1" ) )
                ? "Normal" : PanasonicMakerNoteValueName( head.id, value );
            if ( NULL != valueName )
            {
                prf( "%s", valueName );
                continue;
            }
        }

        if ( 1 == head.type )
            prf( "%u", value );
        else if ( 6 == head.type )
            prf( "%d", (signed char) GetBYTE( valueOffset ) );
        else if ( 3 == head.type )
            prf( "%u", value );
        else if ( 8 == head.type )
            prf( "%d", (short) GetWORD( valueOffset, littleEndian ) );
        else if ( 4 == head.type )
            prf( "%u", value );
        else if ( 9 == head.type )
            prf( "%ld", (LONG) GetDWORD( valueOffset, littleEndian ) );
        else if ( 5 == head.type || 10 == head.type )
        {
            if ( 10 == head.type )
            {
                LONG numerator = (LONG) GetDWORD( valueOffset, littleEndian );
                LONG denominator = (LONG) GetDWORD( valueOffset + 4, littleEndian );
                if ( denominator )
                    prf( "%ld/%ld=%.8g", numerator, denominator,
                         (double) numerator / denominator );
                else
                    prf( "%ld/0", numerator );
            }
            else
            {
                DWORD numerator = GetDWORD( valueOffset, littleEndian );
                DWORD denominator = GetDWORD( valueOffset + 4, littleEndian );
                if ( denominator )
                    prf( "%u/%u=%.8g", numerator, denominator,
                         (double) numerator / denominator );
                else
                    prf( "%u/0", numerator );
            }
        }
    }
    if ( valuesToPrint < head.count )
        prf( ", ..." );
    prf( "\n" );
}

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
            prf( "numtags is > MaxIFDHeaders; it's likely not in EXIF format, so the data may be noise. skipping (%d)\n", NumTags );
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
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                if ( !IsFileRangeValid( (__int64) stringOffset + headerBase, 16 ) )
                {
                    Space( depth );
                    prf( "Panasonic InternalSerialNumber:   (invalid data range)\n" );
                }
                else
                {
                    BYTE serial[ 16 ];
                    GetBytes( stringOffset + headerBase, serial, sizeof serial );
                    bool legacySerial = ( 0 == serial[ 13 ] &&
                                          serial[ 0 ] >= 'A' && serial[ 0 ] <= 'Z' &&
                                          serial[ 1 ] >= '0' && serial[ 1 ] <= '9' &&
                                          serial[ 2 ] >= '0' && serial[ 2 ] <= '9' );
                    for ( int j = 3; j < 13 && legacySerial; j++ )
                    {
                        if ( serial[ j ] < '0' || serial[ j ] > '9' )
                            legacySerial = false;
                    }
                    Space( depth );
                    if ( legacySerial )
                    {
                        prf( "Panasonic InternalSerialNumber:   (%c%c%c) 20%c%c:%c%c:%c%c no. %c%c%c%c\n",
                             serial[ 0 ], serial[ 1 ], serial[ 2 ],
                             serial[ 3 ], serial[ 4 ], serial[ 5 ], serial[ 6 ],
                             serial[ 7 ], serial[ 8 ], serial[ 9 ], serial[ 10 ],
                             serial[ 11 ], serial[ 12 ] );
                    }
                    else
                    {
                        int outputLength = 0;
                        for ( int j = 0; j < _countof( serial ); j++ )
                        {
                            if ( 0 != serial[ j ] )
                                acBuffer[ outputLength++ ] = (char) serial[ j ];
                        }
                        acBuffer[ outputLength ] = 0;
                        DetectGarbage( acBuffer );
                        prf( "Panasonic InternalSerialNumber:   %s\n", acBuffer );
                    }
                }
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
                const char * tagName = PanasonicMakerNoteTagName( head.id );
                if ( NULL != tagName )
                    PrintPanasonicMakerNoteTag( depth, head, IFDOffset - 4, headerBase, littleEndian );
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
        }

        IFDOffset = GetDWORD( IFDOffset + headerBase, littleEndian );
    }
} //EnumeratePanasonicMakernotes

const char * OlympusIFDTagName( const char * directory, WORD tag )
{
    struct TagName { WORD id; const char * name; };
    static const TagName equipment[] = {
        { 0, "EquipmentVersion" }, { 256, "CameraType2" }, { 257, "SerialNumber" },
        { 258, "InternalSerialNumber" }, { 259, "FocalPlaneDiagonal" },
        { 260, "BodyFirmwareVersion" }, { 513, "LensType" }, { 514, "LensSerialNumber" },
        { 515, "LensModel" }, { 516, "LensFirmwareVersion" },
        { 517, "MaxApertureAtMinFocal" }, { 518, "MaxApertureAtMaxFocal" },
        { 519, "MinFocalLength" }, { 520, "MaxFocalLength" }, { 522, "MaxAperture" },
        { 523, "LensProperties" }, { 769, "Extender" }, { 770, "ExtenderSerialNumber" },
        { 771, "ExtenderModel" }, { 772, "ExtenderFirmwareVersion" },
        { 1027, "ConversionLens" }, { 4096, "FlashType" }, { 4097, "FlashModel" },
        { 4098, "FlashFirmwareVersion" }, { 4099, "FlashSerialNumber" }
    };
    static const TagName cameraSettings[] = {
        { 0, "CameraSettingsVersion" }, { 256, "PreviewImageValid" },
        { 257, "PreviewImageStart" }, { 258, "PreviewImageLength" },
        { 512, "ExposureMode" }, { 513, "AELock" }, { 514, "MeteringMode" },
        { 515, "ExposureShift" }, { 516, "NDFilter" }, { 768, "MacroMode" },
        { 769, "FocusMode" }, { 770, "FocusProcess" }, { 771, "AFSearch" },
        { 772, "AFAreas" }, { 773, "AFPointSelected" }, { 774, "AFFineTune" },
        { 775, "AFFineTuneAdj" }, { 776, "FocusBracketStepSize" },
        { 1024, "FlashMode" }, { 1025, "FlashExposureComp" },
        { 1027, "FlashRemoteControl" }, { 1028, "FlashControlMode" },
        { 1029, "FlashIntensity" }, { 1030, "ManualFlashStrength" },
        { 1280, "WhiteBalance2" }, { 1281, "WhiteBalanceTemperature" },
        { 1282, "WhiteBalanceBracket" }, { 1283, "CustomSaturation" },
        { 1284, "ModifiedSaturation" }, { 1285, "ContrastSetting" },
        { 1286, "SharpnessSetting" }, { 1287, "ColorSpace" }, { 1289, "SceneMode" },
        { 1290, "NoiseReduction" }, { 1291, "DistortionCorrection" },
        { 1292, "ShadingCompensation" }, { 1293, "CompressionFactor" },
        { 1295, "Gradation" }, { 1312, "PictureMode" }, { 1313, "PictureModeSaturation" },
        { 1314, "PictureModeHue" }, { 1315, "PictureModeContrast" },
        { 1316, "PictureModeSharpness" }, { 1317, "PictureModeBWFilter" },
        { 1318, "PictureModeTone" }, { 1319, "NoiseFilter" }, { 1321, "ArtFilter" },
        { 1324, "MagicFilter" }, { 1325, "PictureModeEffect" }, { 1326, "ToneLevel" },
        { 1327, "ArtFilterEffect" }, { 1330, "ColorCreatorEffect" },
        { 1335, "MonochromeProfileSettings" }, { 1336, "FilmGrainEffect" },
        { 1337, "ColorProfileSettings" }, { 1338, "MonochromeVignetting" },
        { 1339, "MonochromeColor" }, { 1536, "DriveMode" }, { 1537, "PanoramaMode" },
        { 1539, "ImageQuality2" }, { 1540, "ImageStabilization" },
        { 2052, "StackedImage" }, { 2304, "ManometerPressure" },
        { 2305, "ManometerReading" }, { 2306, "ExtendedWBDetect" },
        { 2307, "RollAngle" }, { 2308, "PitchAngle" }, { 2312, "DateTimeUTC" }
    };
    static const TagName rawDevelopment[] = {
        { 0, "RawDevVersion" }, { 256, "RawDevExposureBiasValue" },
        { 257, "RawDevWhiteBalanceValue" }, { 258, "RawDevWBFineAdjustment" },
        { 259, "RawDevGrayPoint" }, { 260, "RawDevSaturationEmphasis" },
        { 261, "RawDevMemoryColorEmphasis" }, { 262, "RawDevContrastValue" },
        { 263, "RawDevSharpnessValue" }, { 264, "RawDevColorSpace" },
        { 265, "RawDevEngine" }, { 266, "RawDevNoiseReduction" },
        { 267, "RawDevEditStatus" }, { 268, "RawDevSettings" }
    };
    static const TagName imageProcessing[] = {
        { 0, "ImageProcessingVersion" }, { 256, "WB_RBLevels" },
        { 258, "WB_RBLevels3000K" }, { 259, "WB_RBLevels3300K" },
        { 260, "WB_RBLevels3600K" }, { 261, "WB_RBLevels3900K" },
        { 262, "WB_RBLevels4000K" }, { 263, "WB_RBLevels4300K" },
        { 264, "WB_RBLevels4500K" }, { 265, "WB_RBLevels4800K" },
        { 266, "WB_RBLevels5300K" }, { 267, "WB_RBLevels6000K" },
        { 268, "WB_RBLevels6600K" }, { 269, "WB_RBLevels7500K" },
        { 270, "WB_RBLevelsCWB1" }, { 271, "WB_RBLevelsCWB2" },
        { 272, "WB_RBLevelsCWB3" }, { 273, "WB_RBLevelsCWB4" },
        { 275, "WB_GLevel3000K" }, { 276, "WB_GLevel3300K" },
        { 277, "WB_GLevel3600K" }, { 278, "WB_GLevel3900K" },
        { 279, "WB_GLevel4000K" }, { 280, "WB_GLevel4300K" },
        { 281, "WB_GLevel4500K" }, { 282, "WB_GLevel4800K" },
        { 283, "WB_GLevel5300K" }, { 284, "WB_GLevel6000K" },
        { 285, "WB_GLevel6600K" }, { 286, "WB_GLevel7500K" },
        { 287, "WB_GLevel" }, { 512, "ColorMatrix" }, { 768, "Enhancer" },
        { 769, "EnhancerValues" }, { 784, "CoringFilter" }, { 785, "CoringValues" },
        { 1536, "BlackLevel2" }, { 1552, "GainBase" }, { 1553, "ValidBits" },
        { 1554, "CropLeft" }, { 1555, "CropTop" }, { 1556, "CropWidth" },
        { 1557, "CropHeight" }, { 1589, "UnknownBlock1" }, { 1590, "UnknownBlock2" },
        { 2053, "SensorCalibration" }, { 4112, "NoiseReduction2" },
        { 4113, "DistortionCorrection2" }, { 4114, "ShadingCompensation2" },
        { 4124, "MultipleExposureMode" }, { 4355, "UnknownBlock3" },
        { 4356, "UnknownBlock4" }, { 4370, "AspectRatio" }, { 4371, "AspectFrame" },
        { 4608, "FacesDetected" }, { 4609, "FaceDetectArea" }, { 4610, "MaxFaces" },
        { 4611, "FaceDetectFrameSize" }, { 4615, "FaceDetectFrameCrop" },
        { 4870, "CameraTemperature" }, { 6400, "KeystoneCompensation" },
        { 6401, "KeystoneDirection" }, { 6406, "KeystoneValue" }
    };
    static const TagName focusInfo[] = {
        { 0, "FocusInfoVersion" }, { 521, "AutoFocus" }, { 528, "SceneDetect" },
        { 529, "SceneArea" }, { 530, "SceneDetectData" }, { 768, "ZoomStepCount" },
        { 769, "FocusStepCount" }, { 771, "FocusStepInfinity" },
        { 772, "FocusStepNear" }, { 773, "FocusDistance" }, { 776, "AFPoint" },
        { 4609, "ExternalFlash" }, { 4611, "ExternalFlashGuideNumber" },
        { 4612, "ExternalFlashBounce" }, { 4613, "ExternalFlashZoom" },
        { 4616, "InternalFlash" }, { 4617, "ManualFlash" }, { 4618, "MacroLED" },
        { 5376, "SensorTemperature" }, { 5632, "ImageStabilization" }
    };
    static const TagName main[] = {
        { 0, "MakerNoteVersion" }, { 64, "CompressedImageSize" }, { 129, "PreviewImageData" },
        { 136, "PreviewImageStart" }, { 137, "PreviewImageLength" },
        { 256, "ThumbnailImage" }, { 260, "BodyFirmwareVersion" },
        { 512, "SpecialMode" }, { 513, "Quality" }, { 514, "Macro" },
        { 515, "BWMode" }, { 516, "DigitalZoom" }, { 517, "FocalPlaneDiagonal" },
        { 518, "LensDistortionParams" }, { 519, "CameraType" }, { 521, "CameraID" },
        { 640, "PreviewImage" }, { 768, "PreCaptureFrames" }, { 769, "WhiteBoard" },
        { 770, "OneTouchWB" }, { 771, "WhiteBalanceBracket" }, { 772, "WhiteBalanceBias" },
        { 1024, "SensorArea" }, { 1025, "BlackLevel" }, { 1027, "SceneMode" },
        { 1028, "SerialNumber" }, { 1029, "Firmware" }, { 3840, "DataDump" },
        { 3841, "DataDump2" }, { 4096, "ShutterSpeedValue" }, { 4097, "ISOValue" },
        { 4098, "ApertureValue" }, { 4099, "BrightnessValue" },
        { 4100, "FlashMode" }, { 4101, "FlashDevice" },
        { 4102, "ExposureCompensation" }, { 4103, "SensorTemperature" },
        { 4104, "LensTemperature" }, { 4105, "LightCondition" }, { 4106, "FocusRange" },
        { 4107, "FocusMode" }, { 4108, "ManualFocusDistance" },
        { 4109, "ZoomStepCount" }, { 4110, "FocusStepCount" }, { 4111, "Sharpness" },
        { 4112, "FlashChargeLevel" }, { 4113, "ColorMatrix" }, { 4114, "BlackLevel" },
        { 4115, "ColorTemperatureBG" }, { 4116, "ColorTemperatureRG" },
        { 4117, "WBMode" }, { 4119, "RedBalance" }, { 4120, "BlueBalance" },
        { 4121, "ColorMatrixNumber" }, { 4122, "SerialNumber" },
        { 4131, "FlashExposureComp" }, { 4137, "Contrast" }, { 4138, "SharpnessFactor" },
        { 4139, "ColorControl" }, { 4140, "ValidBits" }, { 4141, "CoringFilter" },
        { 4142, "OlympusImageWidth" }, { 4143, "OlympusImageHeight" },
        { 4144, "SceneDetect" }, { 4145, "SceneArea" }, { 4147, "SceneDetectData" },
        { 4148, "CompressionRatio" }, { 4149, "PreviewImageValid" },
        { 4150, "PreviewImageStart" }, { 4151, "PreviewImageLength" },
        { 4152, "AFResult" }, { 4153, "CCDScanMode" }, { 4154, "NoiseReduction" },
        { 4155, "FocusStepInfinity" }, { 4156, "FocusStepNear" },
        { 4157, "LightValueCenter" }, { 4158, "LightValuePeriphery" },
        { 4159, "FieldCount" }, { 8272, "CameraParameters" }
    };

    const TagName * names = NULL;
    size_t count = 0;
    if ( 0 == strcmp( directory, "Equipment" ) )
    {
        names = equipment;
        count = _countof( equipment );
    }
    else if ( 0 == strcmp( directory, "CameraSettings" ) )
    {
        names = cameraSettings;
        count = _countof( cameraSettings );
    }
    else if ( 0 == strcmp( directory, "RawDevelopment" ) )
    {
        names = rawDevelopment;
        count = _countof( rawDevelopment );
    }
    else if ( 0 == strcmp( directory, "ImageProcessing" ) )
    {
        names = imageProcessing;
        count = _countof( imageProcessing );
    }
    else if ( 0 == strcmp( directory, "FocusInfo" ) )
    {
        names = focusInfo;
        count = _countof( focusInfo );
    }
    else if ( 0 == strcmp( directory, "Main" ) )
    {
        names = main;
        count = _countof( main );
    }

    for ( size_t i = 0; i < count; i++ )
        if ( names[ i ].id == tag )
            return names[ i ].name;
    return NULL;
}

const char * OlympusIFDValueName( const char * directory, WORD tag, DWORD value )
{
    if ( 0 == strcmp( directory, "CameraSettings" ) )
    {
        if ( 513 == tag || 774 == tag || 1027 == tag ||
             1284 == tag || 1291 == tag || 1292 == tag || 1537 == tag ||
             2306 == tag )
            return value ? "On" : "Off";
        if ( 256 == tag )
            return value ? "Yes" : "No";
        if ( 512 == tag )
        {
            if ( 0 == value ) return "Auto";
            if ( 1 == value ) return "Manual";
            if ( 2 == value ) return "Auto bracket";
            if ( 3 == value ) return "Program";
            if ( 4 == value ) return "Aperture priority";
            if ( 5 == value ) return "Shutter priority";
        }
        if ( 514 == tag )
        {
            if ( 2 == value ) return "Center-weighted";
            if ( 3 == value ) return "Spot";
            if ( 5 == value ) return "ESP";
        }
        if ( 771 == tag )
            return value ? "Ready" : "Not ready";
        if ( 1281 == tag && 0 == value )
            return "Auto";
        if ( 1280 == tag && 0 == value )
            return "Auto";
        if ( 1287 == tag )
        {
            if ( 0 == value ) return "sRGB";
            if ( 1 == value ) return "Adobe RGB";
            if ( 2 == value ) return "Pro Photo RGB";
        }
        if ( 1289 == tag && 0 == value )
            return "Standard";
        if ( 1290 == tag )
        {
            if ( 0 == value ) return "(none)";
            if ( 1 == value ) return "Noise Reduction";
            if ( 2 == value ) return "Noise Filter";
            if ( 4 == value ) return "Noise Filter (ISO Boost)";
            if ( 8 == value ) return "Auto";
        }
        if ( 1536 == tag && 0 == value )
            return "Single Shot";
        if ( 1540 == tag )
        {
            if ( 1 == value ) return "On, Mode 1";
            if ( 4 == value ) return "On, Mode 4";
        }
        if ( 2052 == tag )
            return value ? "Yes" : "No";
    }
    else if ( 0 == strcmp( directory, "Equipment" ) )
    {
        if ( ( 769 == tag || 4096 == tag || 4097 == tag ) && 0 == value )
            return "None";
    }
    else if ( 0 == strcmp( directory, "ImageProcessing" ) )
    {
        if ( 4113 == tag || 4114 == tag || 6400 == tag )
            return value ? "On" : "Off";
        if ( 4124 == tag && 0 == value )
            return "Off";
        if ( 4370 == tag )
        {
            if ( 0 == value ) return "4:3";
            if ( 1 == value ) return "3:2";
            if ( 2 == value ) return "16:9";
            if ( 3 == value ) return "6:6";
            if ( 4 == value ) return "5:4";
        }
    }
    else if ( 0 == strcmp( directory, "RawDevelopment" ) && 264 == tag )
    {
        if ( 0 == value ) return "sRGB";
        if ( 1 == value ) return "Adobe RGB";
        if ( 2 == value ) return "Pro Photo RGB";
    }
    return NULL;
}

const char * OlympusCameraTypeName( const char * code )
{
    struct CameraType { const char * code; const char * name; };
    static const CameraType names[] = {
        { "D4401", "XZ-1" }, { "S0059", "E-M10MarkII" },
        { "S0095", "OM-1" }, { "S0121", "OM-1MarkII" }
    };
    for ( size_t i = 0; i < _countof( names ); i++ )
        if ( 0 == strcmp( code, names[ i ].code ) )
            return names[ i ].name;
    return NULL;
}

void PrintOlympusMainIFDTag( int depth, const IFDHeader & head,
                             __int64 inlineValueOffset, __int64 noteBase,
                             __int64 headerBase, bool littleEndian )
{
    const char * name = OlympusIFDTagName( "Main", head.id );
    DWORD valueSize = ( 1 == head.type || 2 == head.type || 6 == head.type || 7 == head.type ) ? 1 :
                      ( 3 == head.type || 8 == head.type ) ? 2 :
                      ( 4 == head.type || 9 == head.type || 13 == head.type ) ? 4 :
                      ( 5 == head.type || 10 == head.type ) ? 8 : 0;
    unsigned __int64 byteCount = (unsigned __int64) head.count * valueSize;
    __int64 valuesOffset = ( byteCount <= 4 )
        ? inlineValueOffset + headerBase
        : (__int64) head.offset + noteBase + headerBase;

    Space( depth );
    if ( NULL == name || 0 == valueSize || 0 == head.count ||
         byteCount > INT_MAX || !IsFileRangeValid( valuesOffset, (__int64) byteCount ) )
    {
        prf( "Olympus main tag 0x%04x: invalid or unsupported data (type %u, count %u)\n",
             head.id, head.type, head.count );
        return;
    }

    if ( 512 == head.id && 4 == head.type && 3 == head.count )
    {
        DWORD mode = GetDWORD( valuesOffset, littleEndian );
        DWORD sequence = GetDWORD( valuesOffset + 4, littleEndian );
        DWORD panorama = GetDWORD( valuesOffset + 8, littleEndian );
        const char * modeName = 0 == mode ? "Normal" : 2 == mode ? "Fast" :
                                3 == mode ? "Panorama" : "Unknown";
        const char * panoramaName = 0 == panorama ? "(none)" :
                                    1 == panorama ? "Left to Right" :
                                    2 == panorama ? "Right to Left" :
                                    3 == panorama ? "Bottom to Top" :
                                    4 == panorama ? "Top to Bottom" : "Unknown";
        prf( "Olympus SpecialMode: %s, Sequence: %u, Panorama: %s\n",
             modeName, sequence, panoramaName );
        return;
    }

    prf( "Olympus %s: ", name );
    if ( 0 == head.id && 7 == head.type && 4 == head.count )
    {
        BYTE version[ 4 ];
        GetBytes( valuesOffset, version, sizeof version );
        prf( "%c%c%c%c\n", version[ 0 ], version[ 1 ], version[ 2 ], version[ 3 ] );
        return;
    }
    if ( 7 == head.type )
    {
        prf( "(binary data, %u bytes)\n", head.count );
        return;
    }
    if ( 2 == head.type )
    {
        vector<char> text( min( head.count, 1024u ) + 1, 0 );
        DWORD textLength = min( head.count, (DWORD) text.size() - 1 );
        GetBytes( valuesOffset, text.data(), (int) textLength );
        while ( textLength && ( 0 == text[ textLength - 1 ] || ' ' == text[ textLength - 1 ] ) )
            text[ --textLength ] = 0;
        prf( "%s\n", text.data() );
        return;
    }

    DWORD valuesToPrint = min( head.count, 16u );
    for ( DWORD i = 0; i < valuesToPrint; i++ )
    {
        if ( i )
            prf( ", " );
        __int64 valueOffset = valuesOffset + (__int64) i * valueSize;
        if ( 1 == head.type )
            prf( "%u", GetBYTE( valueOffset ) );
        else if ( 3 == head.type )
            prf( "%u", GetWORD( valueOffset, littleEndian ) );
        else if ( 4 == head.type || 13 == head.type )
            prf( "%u", GetDWORD( valueOffset, littleEndian ) );
        else if ( 6 == head.type )
            prf( "%d", (signed char) GetBYTE( valueOffset ) );
        else if ( 8 == head.type )
            prf( "%d", (short) GetWORD( valueOffset, littleEndian ) );
        else if ( 9 == head.type )
            prf( "%ld", (LONG) GetDWORD( valueOffset, littleEndian ) );
        else if ( 5 == head.type || 10 == head.type )
        {
            LONG numerator = (LONG) GetDWORD( valueOffset, littleEndian );
            LONG denominator = (LONG) GetDWORD( valueOffset + 4, littleEndian );
            if ( denominator )
                prf( "%ld/%ld=%.8g", numerator, denominator,
                     (double) numerator / denominator );
            else
                prf( "%ld/0", numerator );
        }
    }
    if ( valuesToPrint < head.count )
        prf( ", ..." );
    prf( "\n" );
}

void EnumerateOlympusIFD( int depth, __int64 IFDOffset, __int64 headerBase,
                          bool littleEndian, const char * directory )
{
    DWORD originalIFDOffset = (DWORD) IFDOffset;
    bool previewIsValid = false;
    const WORD maxOlympusIFDHeaders = 1024;
    vector<IFDHeader> aHeaders( maxOlympusIFDHeaders );

    while ( 0 != IFDOffset ) 
    {
        if ( !IsFileRangeValid( IFDOffset + headerBase, 2 ) )
        {
            Space( depth );
            prf( "Olympus %s IFD: invalid directory offset\n", directory );
            break;
        }

        WORD NumTags = GetWORD( IFDOffset + headerBase, littleEndian );
        if ( NumTags > maxOlympusIFDHeaders )
        {
            Space( depth );
            prf( "Olympus %s IFD has too many tags; skipping (%u)\n", directory, NumTags );
            break;
        }
    
        if ( !IsFileRangeValid( IFDOffset + headerBase + 2,
                                (__int64) NumTags * sizeof( IFDHeader ) + sizeof( DWORD ) ) ||
             !GetIFDHeaders( IFDOffset + headerBase + 2, aHeaders.data(), NumTags, littleEndian ) )
        {
            Space( depth );
            prf( "Olympus %s IFD has invalid tag data; skipping\n", directory );
            break;
        }

        Space( depth );
        prf( "Olympus %s IFD: %u tags\n", directory, NumTags );
        IFDOffset += 2;
        for ( int i = 0; i < NumTags; i++ )
        {
            IFDHeader & head = aHeaders[ i ];
            IFDOffset += sizeof IFDHeader;

            if ( g_FullInformation )
            {
                Space( depth );
                prf( "Olympus %s tag %u type %u count %u offset/value %u\n",
                     directory, head.id, head.type, head.count, head.offset );
            }

            const char * name = OlympusIFDTagName( directory, head.id );
            if ( NULL == name )
            {
                Space( depth );
                prf( "Olympus %s tag 0x%04x (type %u, count %u): ",
                     directory, head.id, head.type, head.count );
                if ( 7 == head.type )
                    prf( "(binary data, %u bytes)\n", head.count );
                else
                    prf( "unrecognized field\n" );
                continue;
            }

            DWORD valueSize = ( 1 == head.type || 2 == head.type || 6 == head.type || 7 == head.type ) ? 1 :
                              ( 3 == head.type || 8 == head.type ) ? 2 :
                              ( 4 == head.type || 9 == head.type || 11 == head.type || 13 == head.type ) ? 4 :
                              ( 5 == head.type || 10 == head.type || 12 == head.type ) ? 8 : 0;
            unsigned __int64 byteCount = (unsigned __int64) head.count * valueSize;
            __int64 valuesOffset = ( byteCount <= 4 )
                ? IFDOffset - 4 + headerBase
                : (__int64) head.offset + headerBase;
            if ( 0 == valueSize || 0 == head.count || byteCount > INT_MAX ||
                 !IsFileRangeValid( valuesOffset, (__int64) byteCount ) )
            {
                Space( depth );
                prf( "Olympus %s.%s: (invalid or unsupported data; type %u, count %u)\n",
                     directory, name, head.type, head.count );
                continue;
            }

            Space( depth );
            prf( "Olympus %s.%s: ", directory, name );
            if ( ( 0 == strcmp( directory, "Equipment" ) ||
                   0 == strcmp( directory, "CameraSettings" ) ||
                   0 == strcmp( directory, "RawDevelopment" ) ||
                   0 == strcmp( directory, "ImageProcessing" ) ||
                   0 == strcmp( directory, "FocusInfo" ) ) &&
                 0 == head.id && 7 == head.type && 4 == head.count )
            {
                BYTE version[ 4 ];
                GetBytes( valuesOffset, version, sizeof version );
                prf( "%c%c%c%c\n", version[ 0 ], version[ 1 ], version[ 2 ], version[ 3 ] );
                continue;
            }
            if ( 2 == head.type )
            {
                vector<char> text( min( head.count, 1024u ) + 1, 0 );
                DWORD textLength = min( head.count, (DWORD) text.size() - 1 );
                GetBytes( valuesOffset, text.data(), (int) textLength );
                while ( textLength &&
                        ( 0 == text[ textLength - 1 ] || ' ' == text[ textLength - 1 ] ) )
                    text[ --textLength ] = 0;
                for ( DWORD j = 0; j < textLength; j++ )
                {
                    unsigned char ch = (unsigned char) text[ j ];
                    if ( 0 == ch )
                        break;
                    if ( ch < ' ' || ch > '~' )
                        text[ j ] = '?';
                }
                if ( "Equipment" == string( directory ) && 256 == head.id )
                {
                    const char * model = OlympusCameraTypeName( text.data() );
                    if ( model )
                        prf( "%s (%s)\n", model, text.data() );
                    else
                        prf( "%s\n", text.data() );
                }
                else
                    prf( "%s\n", text.data() );
                continue;
            }

            if ( 7 == head.type )
            {
                prf( "(binary data, %u bytes)\n", head.count );
                continue;
            }

            if ( "Equipment" == string( directory ) &&
                 513 == head.id && 1 == head.type && 6 == head.count )
            {
                BYTE make = GetBYTE( valuesOffset );
                BYTE model = GetBYTE( valuesOffset + 2 );
                BYTE submodel = GetBYTE( valuesOffset + 3 );
                if ( 0 == make && 7 == model && 0x10 == submodel )
                    prf( "Olympus M.Zuiko Digital ED 12mm F2.0 (0 07 10)\n" );
                else if ( 0 == make && 0x40 == model && 0x10 == submodel )
                    prf( "OM System M.Zuiko Digital ED 150-600mm F5.0-6.3 (0 40 10)\n" );
                else
                    prf( "%u, %u, %u, %u, %u, %u\n",
                         GetBYTE( valuesOffset ), GetBYTE( valuesOffset + 1 ),
                         GetBYTE( valuesOffset + 2 ), GetBYTE( valuesOffset + 3 ),
                         GetBYTE( valuesOffset + 4 ), GetBYTE( valuesOffset + 5 ) );
                continue;
            }

            if ( "Equipment" == string( directory ) &&
                 ( 260 == head.id || 516 == head.id || 772 == head.id || 4098 == head.id ) &&
                 4 == head.type && 1 == head.count )
            {
                DWORD version = GetDWORD( valuesOffset, littleEndian );
                char versionText[ 16 ];
                sprintf_s( versionText, "%x", version );
                size_t versionLength = strlen( versionText );
                if ( versionLength > 3 )
                {
                    memmove( versionText + versionLength - 2,
                             versionText + versionLength - 3, 4 );
                    versionText[ versionLength - 3 ] = '.';
                }
                prf( "%s\n", versionText );
                continue;
            }

            if ( "Equipment" == string( directory ) &&
                 ( 517 == head.id || 518 == head.id || 522 == head.id ) &&
                 3 == head.type && 1 == head.count )
            {
                WORD apex = GetWORD( valuesOffset, littleEndian );
                prf( "%.1f\n", 0 == apex ? 0.0 : pow( 2.0, (double) apex / 512.0 ) );
                continue;
            }

            if ( "CameraSettings" == string( directory ) &&
                 257 == head.id && 4 == head.type && 1 == head.count )
            {
                DWORD previewOffset = GetDWORD( valuesOffset, littleEndian );
                if ( previewIsValid )
                {
                    g_Embedded_Image_Offset = (__int64) previewOffset + headerBase;
                    prf( "%lld\n", g_Embedded_Image_Offset );
                }
                else
                    prf( "%u\n", previewOffset );
                continue;
            }

            if ( "CameraSettings" == string( directory ) &&
                 1312 == head.id && 3 == head.type && 2 == head.count )
            {
                WORD mode = GetWORD( valuesOffset, littleEndian );
                WORD parameter = GetWORD( valuesOffset + 2, littleEndian );
                const char * modeName = 1 == mode ? "Vivid" : 2 == mode ? "Natural" :
                                        3 == mode ? "Muted" : 4 == mode ? "Portrait" :
                                        5 == mode ? "i-Enhance" : NULL;
                if ( modeName )
                    prf( "%s; %u\n", modeName, parameter );
                else
                    prf( "%u, %u\n", mode, parameter );
                continue;
            }

            if ( "ImageProcessing" == string( directory ) &&
                 4124 == head.id && 3 == head.type && 2 == head.count )
            {
                WORD mode = GetWORD( valuesOffset, littleEndian );
                WORD frames = GetWORD( valuesOffset + 2, littleEndian );
                if ( 0 == mode )
                    prf( "Off; %u\n", frames );
                else if ( 2 == mode )
                    prf( "On (%u frames); %u\n", mode, frames );
                else
                    prf( "%u, %u\n", mode, frames );
                continue;
            }

            if ( 1 == head.count &&
                 ( 1 == head.type || 3 == head.type || 4 == head.type ) )
            {
                DWORD value = 1 == head.type ? GetBYTE( valuesOffset ) :
                              3 == head.type ? GetWORD( valuesOffset, littleEndian ) :
                              GetDWORD( valuesOffset, littleEndian );
                const char * valueName = OlympusIFDValueName( directory, head.id, value );
                if ( NULL != valueName )
                {
                    prf( "%s\n", valueName );
                    if ( 256 == head.id && 0 == strcmp( directory, "CameraSettings" ) )
                        previewIsValid = ( 0 != value );
                    continue;
                }
            }

            DWORD valuesToPrint = min( head.count, 16u );
            for ( DWORD j = 0; j < valuesToPrint; j++ )
            {
                if ( j )
                    prf( ", " );
                __int64 valueOffset = valuesOffset + (__int64) j * valueSize;
                if ( 1 == head.type )
                    prf( "%u", GetBYTE( valueOffset ) );
                else if ( 6 == head.type )
                    prf( "%d", (signed char) GetBYTE( valueOffset ) );
                else if ( 3 == head.type )
                {
                    WORD value = GetWORD( valueOffset, littleEndian );
                    if ( "ImageProcessing" == string( directory ) &&
                         512 == head.id )
                        prf( "%d", (short) value );
                    else
                        prf( "%u", value );
                }
                else if ( 8 == head.type )
                    prf( "%d", (short) GetWORD( valueOffset, littleEndian ) );
                else if ( 4 == head.type || 13 == head.type )
                    prf( "%u", GetDWORD( valueOffset, littleEndian ) );
                else if ( 9 == head.type )
                    prf( "%ld", (LONG) GetDWORD( valueOffset, littleEndian ) );
                else if ( 5 == head.type || 10 == head.type )
                {
                    LONG numerator = (LONG) GetDWORD( valueOffset, littleEndian );
                    LONG denominator = (LONG) GetDWORD( valueOffset + 4, littleEndian );
                    if ( denominator )
                        prf( "%ld/%ld=%.8g", numerator, denominator,
                             (double) numerator / denominator );
                    else
                        prf( "%ld/0", numerator );
                }
                else
                    prf( "%02x", GetBYTE( valueOffset ) );
            }
            if ( valuesToPrint < head.count )
                prf( ", ..." );
            prf( "\n" );

            if ( "CameraSettings" == string( directory ) )
            {
                if ( 258 == head.id && previewIsValid )
                    g_Embedded_Image_Length = head.offset;
            }
        }

        IFDOffset = GetDWORD( IFDOffset + headerBase, littleEndian );
    }
} //EnumerateOlympusIFD

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
    else if ( !strcmp( g_acMake, "OM Digital Solutions" ) )
    {
        // OM System notes add a 16-byte signature/header before the TIFF IFD.
        IFDOffset += 16;
        isOlympus = true;
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
    else if ( !strcmp( g_acMake, "CANON" ) ||
              !strcmp( g_acMake, "Canon" ) ) // M3 uses this
    {
        isCanon = true;
        EnumerateCanonMakernotes( 0, IFDOffset, headerBase, littleEndian );
        return;
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
            prf( "numtags is > MaxIFDHeaders; it's likely not in EXIF format, so the data may be noise. skipping (%d)\n", NumTags );
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
            else if ( isOlympus && 13 == head.type &&
                      ( 0x2010 == head.id || 0x2020 == head.id ||
                        0x2030 == head.id || 0x2040 == head.id ||
                        0x2050 == head.id ) )
            {
                const char * directory = 0x2010 == head.id ? "Equipment" :
                                         0x2020 == head.id ? "CameraSettings" :
                                         0x2030 == head.id ? "RawDevelopment" :
                                         0x2040 == head.id ? "ImageProcessing" : "FocusInfo";
                prf( "Olympus %s IFD offset: %u\n", directory, head.offset );
                EnumerateOlympusIFD( depth + 1, head.offset,
                                     originalIFDOffset + headerBase,
                                     littleEndian, directory );
            }
            else if ( isSony && NULL != SonyMakerNoteTagName( head.id ) )
                PrintSonyMakerNoteTag( depth, SonyMakerNoteTagName( head.id ), head,
                                       IFDOffset - 4, headerBase, littleEndian );
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
                if ( isOlympus && 256 == head.id && 7 == head.type )
                {
                    prf( "Olympus ThumbnailImage: (binary data, %u bytes)\n", head.count );
                }
                else if ( isOlympus && 521 == head.id &&
                          ( 2 == head.type || 7 == head.type ) )
                {
                    ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                    GetString( stringOffset + originalIFDOffset + headerBase,
                               acBuffer, _countof( acBuffer ), head.count );
                    DetectGarbage( acBuffer );
                    prf( "Olympus CameraID: %s\n", acBuffer );
                }
                else if ( isOlympus && NULL != OlympusIFDTagName( "Main", head.id ) )
                {
                    PrintOlympusMainIFDTag( depth, head, IFDOffset - 4,
                                            originalIFDOffset, headerBase, littleEndian );
                }
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
                    {
                        if ( isOlympus )
                            prf( "(binary data, %u bytes)\n", head.count );
                        else
                            DumpBinaryData( head.offset, headerBase, head.count, 4, IFDOffset - 4 );
                    }
                }

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

const char * PanasonicCameraTagValueName( WORD tag, DWORD value )
{
    if ( 4354 == tag || 4608 == tag || 5138 == tag )
        return ( 0 == value ) ? "No" : ( 1 == value ) ? "Yes" : NULL;

    if ( 13056 == tag || 13824 == tag )
    {
        switch ( value )
        {
            case 0: return "Auto";
            case 1: return "Daylight";
            case 2: return "Cloudy";
            case 3: return "Tungsten";
            case 4: return "n/a";
            case 5: return "Flash";
            case 6: return "n/a";
            case 7: return "n/a";
            case 8: return "Custom#1";
            case 9: return "Custom#2";
            case 10: return "Custom#3";
            case 11: return "Custom#4";
            case 12: return "Shade";
            case 13: return "Kelvin";
            case 16: return "AWBc";
        }
    }

    if ( 13569 == tag )
    {
        switch ( value )
        {
            case 1: return "Horizontal (normal)";
            case 2: return "Mirror horizontal";
            case 3: return "Rotate 180";
            case 4: return "Mirror vertical";
            case 5: return "Mirror horizontal and rotate 270 CW";
            case 6: return "Rotate 90 CW";
            case 7: return "Mirror horizontal and rotate 90 CW";
            case 8: return "Rotate 270 CW";
        }
    }

    return "unknown";
}

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
            prf( "numtags is > MaxIFDHeaders; it's likely not in EXIF format, so the data may be noise. skipping (%d)\n", NumTags );
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
                prf( "Panasonic FlashFired:               %s\n",
                     PanasonicCameraTagValueName( head.id, head.offset ) );
            else if ( 4357 == head.id && 4 == head.type )
                prf( "panasonic ZoomPosition:             %d\n", head.offset );
            else if ( 4608 == head.id && 1 == head.type )
                prf( "Panasonic LensAttached:             %s\n",
                     PanasonicCameraTagValueName( head.id, head.offset ) );
            else if ( 4609 == head.id && 3 == head.type )
                prf( "panasonic LensTypeMake:             %d\n", head.offset );
            else if ( 4610 == head.id && 3 == head.type )
                prf( "panasonic LensTypeModel:            %d\n", head.offset );
            else if ( 4611 == head.id && 3 == head.type )
                prf( "panasonic FocalLengthIn35mmFormat:  %d\n", head.offset );
            else if ( 4865 == head.id && 8 == head.type )
            {
                short value = (short) head.offset;
                // Panasonic stores APEX aperture and shutter values in scaled fixed-point form.
                prf( "Panasonic ApertureValue:            f/%.1lf (raw %d)\n",
                     pow( 2.0, (double) value / 512.0 ), value );
            }
            else if ( 4866 == head.id && 8 == head.type )
            {
                short value = (short) head.offset;
                if ( value < 0 )
                    prf( "Panasonic ShutterSpeedValue:        %.3g s (raw %d)\n",
                         pow( 2.0, -(double) value / 256.0 ), value );
                else
                    prf( "Panasonic ShutterSpeedValue:        1/%.0lf s (raw %d)\n",
                         pow( 2.0, (double) value / 256.0 ), value );
            }
            else if ( 4867 == head.id && 8 == head.type )
                prf( "Panasonic SensitivityValue:         %g (raw %d)\n",
                     (double) (short) head.offset / 256.0, (short) head.offset );
            else if ( 4869 == head.id && 3 == head.type )
                prf( "panasonic HighISOMode:              %d\n", head.offset );
            else if ( 5138 == head.id && 1 == head.type )
                prf( "Panasonic FacesDetected:            %s\n",
                     PanasonicCameraTagValueName( head.id, head.offset ) );
            else if ( 12800 == head.id && 3 == head.type )
                prf( "panasonic WB_CFA0_LevelDaylight:    %d\n", head.offset );
            else if ( 12801 == head.id && 3 == head.type )
                prf( "panasonic WB_CFA1_LevelDaylight:    %d\n", head.offset );
            else if ( 12802 == head.id && 3 == head.type )
                prf( "panasonic WB_CFA2_LevelDaylight:    %d\n", head.offset );
            else if ( 12803 == head.id && 3 == head.type )
                prf( "panasonic WB_CFA3_LevelDaylight:    %d\n", head.offset );
            else if ( 13056 == head.id && 1 == head.type )
                prf( "Panasonic WhiteBalanceSet:          %s\n",
                     PanasonicCameraTagValueName( head.id, head.offset ) );
            else if ( 13344 == head.id && 3 == head.type )
                prf( "panasonic WB_RedLevelAuto:          %d\n", head.offset );
            else if ( 13345== head.id && 3 == head.type )
                prf( "panasonic WB_BlueLevelAuto:         %d\n", head.offset );
            else if ( 13569== head.id && 1 == head.type )
                prf( "Panasonic Orientation:              %s\n",
                     PanasonicCameraTagValueName( head.id, head.offset ) );
            else if ( 13824== head.id && 1 == head.type )
                prf( "Panasonic WhiteBalanceDetected:     %s\n",
                     PanasonicCameraTagValueName( head.id, head.offset ) );
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

const char * ExifMeteringMode( DWORD x )
{
    switch ( x )
    {
        case 0: return "unknown";
        case 1: return "average";
        case 2: return "center-weighted average";
        case 3: return "spot";
        case 4: return "multi-spot";
        case 5: return "multi-segment";
        case 6: return "partial";
        case 255: return "other";
    }

    return "unknown";
} //ExifMeteringMode

const char * ExifComponentConfiguration( BYTE component )
{
    switch ( component )
    {
        case 0: return "-";
        case 1: return "Y";
        case 2: return "Cb";
        case 3: return "Cr";
        case 4: return "R";
        case 5: return "G";
        case 6: return "B";
    }

    return "unknown";
} //ExifComponentConfiguration

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
            prf( "numtags is > MaxIFDHeaders; it's likely not in EXIF format, so the data may be noise. skipping (%d)\n", NumTags );
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
                __int64 versionOffset = IFDOffset - 4 + headerBase;
                prf( "GPS VersionID:                      %u.%u.%u.%u\n",
                     GetBYTE( versionOffset ), GetBYTE( versionOffset + 1 ),
                     GetBYTE( versionOffset + 2 ), GetBYTE( versionOffset + 3 ) );
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
                if ( 0 == head.count )
                    prf( "GPS MeasureMode:                    (empty)\n" );
                else
                {
                    ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                    GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                    if ( 0 == acBuffer[ 0 ] )
                        prf( "GPS MeasureMode:                    (empty)\n" );
                    else if ( '2' == acBuffer[ 0 ] || '3' == acBuffer[ 0 ] )
                        prf( "GPS MeasureMode:                    %s-dimensional measurement\n", acBuffer );
                    else
                        prf( "GPS MeasureMode:                    %s (unknown)\n", acBuffer );
                }
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
            prf( "numtags is > MaxIFDHeaders; it's likely not in EXIF format, so the data may be noise. skipping (%d)\n", NumTags );
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
            else if ( 36864 == head.id && 7 == head.type )
            {
                ULONG stringOffset = ( head.count <= 4 ) ? ( IFDOffset - 4 ) : head.offset;
                GetString( stringOffset + headerBase, acBuffer, _countof( acBuffer ), head.count );
                prf( "exif ExifVersion                    %s\n", acBuffer );
            }
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
                __int64 componentOffset = ( head.count <= 4 ) ? IFDOffset - 4 : head.offset;
                prf( "exif ComponentsConfiguration:       " );
                for ( DWORD component = 0; component < head.count; component++ )
                {
                    if ( component != 0 )
                        prf( ", " );
                    prf( "%s", ExifComponentConfiguration( GetBYTE( componentOffset + headerBase + component ) ) );
                }
                prf( "\n" );
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
                prf( "exif MeteringMode:                  %s (%d)\n", ExifMeteringMode( head.offset ), head.offset );
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

const char * PanasonicWBTypeName( WORD type )
{
    switch ( type )
    {
        case 0: return "Unknown";
        case 1: return "Daylight";
        case 2: return "Fluorescent";
        case 3: return "Tungsten (Incandescent)";
        case 4: return "Flash";
        case 9: return "Fine Weather";
        case 10: return "Cloudy";
        case 11: return "Shade";
        case 12: return "Daylight Fluorescent";
        case 13: return "Day White Fluorescent";
        case 14: return "Cool White Fluorescent";
        case 15: return "White Fluorescent";
        case 16: return "Warm White Fluorescent";
        case 17: return "Standard Light A";
        case 18: return "Standard Light B";
        case 19: return "Standard Light C";
        case 20: return "D55";
        case 21: return "D65";
        case 22: return "D75";
        case 23: return "D50";
        case 24: return "ISO Studio Tungsten";
        case 255: return "Other";
        default: return NULL;
    }
}

void PrintPanasonicWBInfo2( int depth, DWORD offset, DWORD count, __int64 headerBase, bool littleEndian )
{
    __int64 dataOffset = (__int64) offset + headerBase;
    if ( count < 2 || !IsFileRangeValid( dataOffset, count ) )
    {
        prf( "Panasonic WBInfo2: (invalid data range, %u bytes)\n", count );
        return;
    }

    WORD entries = GetWORD( dataOffset, littleEndian );
    if ( entries > 7 || (unsigned __int64) 2 + (unsigned __int64) entries * 8 > count )
    {
        prf( "Panasonic WBInfo2: (invalid entry count %u in %u bytes)\n", entries, count );
        return;
    }

    prf( "Panasonic WBInfo2: %u white-balance entries\n", entries );
    for ( WORD i = 0; i < entries; i++ )
    {
        __int64 entryOffset = dataOffset + 2 + (__int64) i * 8;
        WORD type = GetWORD( entryOffset, littleEndian );
        const char * typeName = PanasonicWBTypeName( type );
        Space( depth + 1 );
        if ( typeName )
            prf( "WBType%u: %s\n", i + 1, typeName );
        else
            prf( "WBType%u: %u\n", i + 1, type );

        Space( depth + 1 );
        prf( "WB_RGBLevels%u: %u, %u, %u\n", i + 1,
             GetWORD( entryOffset + 2, littleEndian ),
             GetWORD( entryOffset + 4, littleEndian ),
             GetWORD( entryOffset + 6, littleEndian ) );
    }
}

void PrintPanasonicDistortionInfo( DWORD offset, DWORD count, __int64 headerBase, bool littleEndian )
{
    __int64 dataOffset = (__int64) offset + headerBase;
    if ( count < 26 || !IsFileRangeValid( dataOffset, count ) )
    {
        prf( "Panasonic DistortionInfo: (invalid data range, %u bytes)\n", count );
        return;
    }

    static const struct { const char * name; WORD offset; } fields[] = {
        { "DistortionParam02", 4 }, { "DistortionParam04", 8 },
        { "DistortionScale", 10 }, { "DistortionCorrection", 14 },
        { "DistortionParam08", 16 }, { "DistortionParam09", 18 },
        { "DistortionParam11", 22 }, { "DistortionN", 24 }
    };
    for ( size_t i = 0; i < _countof( fields ); i++ )
    {
        short value = (short) GetWORD( dataOffset + fields[ i ].offset, littleEndian );
        if ( 0 == i || 1 == i || 4 == i || 5 == i || 6 == i )
            prf( "Panasonic %s: %.12g (raw %d)\n", fields[ i ].name,
                 (double) value / 32768.0, value );
        else if ( 2 == i )
            prf( "Panasonic %s: %.12g (raw %d)\n", fields[ i ].name,
                 1.0 / ( 1.0 + (double) value / 32768.0 ), value );
        else if ( 3 == i )
        {
            short rawValue = value;
            value &= 0x0f;
            prf( "Panasonic %s: %s (raw %d)\n", fields[ i ].name,
                 ( 0 == value ) ? "Off" : ( 1 == value ) ? "On" : "unknown", rawValue );
        }
        else
            prf( "Panasonic %s: %d\n", fields[ i ].name, value );
    }
}

void PrintPanasonicIFD0Tag( int depth, WORD id, WORD type, DWORD count, DWORD offset, __int64 headerBase, bool littleEndian, __int64 IFDOffset )
{
    if ( 1 == id )
    {
        char version[ 5 ];
        for ( int i = 0; i < 4; i++ )
        {
            int shift = littleEndian ? 8 * i : 8 * ( 3 - i );
            version[ i ] = (char) ( ( offset >> shift ) & 0xff );
        }
        version[ 4 ] = 0;
        prf( "PanasonicRawVersion:                 %s\n", version );
    }
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
    {
        const char * pattern = NULL;
        if ( 1 == offset ) pattern = "[Red,Green][Green,Blue]";
        else if ( 2 == offset ) pattern = "[Green,Red][Blue,Green]";
        else if ( 3 == offset ) pattern = "[Green,Blue][Red,Green]";
        else if ( 4 == offset ) pattern = "[Blue,Green][Green,Red]";
        prf( "Panasonic CFAPattern:                 %s (raw %u)\n",
             pattern ? pattern : "unknown", offset );
    }
    else if ( 10 == id )
        prf( "panasonicBitsPerSample:               %d\n", offset );
    else if ( 11 == id )
    {
        const char * compression = NULL;
        if ( 34316 == offset ) compression = "Panasonic RAW 1";
        else if ( 34826 == offset ) compression = "Panasonic RAW 2";
        else if ( 34828 == offset ) compression = "Panasonic RAW 3";
        else if ( 34830 == offset ) compression = "Panasonic RAW 4";
        if ( compression )
            prf( "Panasonic Compression:               %s (%u)\n", compression, offset );
        else
            prf( "Panasonic Compression:               unknown (%u)\n", offset );
    }
    else if ( 14 == id )
        prf( "panasonicLinearityLimitRed:           %d\n", offset );
    else if ( 15 == id )
        prf( "panasonicLinearityLimitGreen:         %d\n", offset );
    else if ( 16 == id )
        prf( "panasonicLinearityLimitBlue:          %d\n", offset );
    else if ( 17 == id )
        prf( "Panasonic RedBalance:                 %d\n", offset );
    else if ( 18 == id )
        prf( "Panasonic BlueBalance:                %d\n", offset );
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
        PrintPanasonicWBInfo2( depth, offset, count, headerBase, littleEndian );
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
        PrintPanasonicDistortionInfo( offset, count, headerBase, littleEndian );
    else if ( 284 == id )
    {
        // Older Panasonic raws store gamma in 1/256 units; small values use hundredths.
        double gamma = ( offset < 256 ) ? (double) offset / 100.0 :
                       ( offset < 1024 ) ? (double) offset / 256.0 :
                                           (double) offset / 1024.0;
        prf( "Panasonic Gamma:                      %g (raw %u)\n", gamma, offset );
    }
    else if ( 288 == id )
        EnumeratePanasonicCameraTags( depth + 1, offset, headerBase, littleEndian );
    else if ( 289 == id )
        prf( "panasonicMultishot:                   %d\n", offset );
    else
        prf( "panasonicTag ID %d==%#x, type %d, count %d, offset/value %d\n", id, id, type, count, offset );
} //PrintPanasonicIFD0Tag

const char * JPGComponents( int components, const BYTE * componentIDs, const BYTE * samplingFactors, int adobeColorTransform )
{
    if ( 1 == components )
        return "grey scale";

    if ( 3 == components && 0 == adobeColorTransform )
        return "RGB";

    if ( 3 == components && 1 == adobeColorTransform )
        return "YCbCr";

    if ( 3 == components && 1 == componentIDs[ 0 ] && 2 == componentIDs[ 1 ] && 3 == componentIDs[ 2 ] )
        return "YCbCr";

    if ( 3 == components && 0 == componentIDs[ 0 ] && 1 == componentIDs[ 1 ] && 2 == componentIDs[ 2 ] &&
         ( samplingFactors[ 0 ] != samplingFactors[ 1 ] || samplingFactors[ 0 ] != samplingFactors[ 2 ] ) )
        return "YCbCr";

    if ( 4 == components && 2 == adobeColorTransform )
        return "YCCK";

    if ( 4 == components )
        return "CMYK";

    return "unknown";
} //JPGComponents

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
        return "none (RGB or CMYK from frame components)";

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

DWORD ICCReadDWORD( const vector<BYTE> & profile, size_t offset )
{
    return ( (DWORD) profile[ offset ] << 24 ) |
           ( (DWORD) profile[ offset + 1 ] << 16 ) |
           ( (DWORD) profile[ offset + 2 ] << 8 ) |
           (DWORD) profile[ offset + 3 ];
}

void DumpICCData( const vector<BYTE> & profile, size_t offset, size_t length )
{
    const size_t bytesPerRow = 32;

    for ( size_t row = 0; row < length; row += bytesPerRow )
    {
        size_t count = min( bytesPerRow, length - row );
        prf( "             %#10x  ", (DWORD) ( offset + row ) );
        for ( size_t i = 0; i < count; i++ )
            prf( "%02x ", profile[ offset + row + i ] );
        for ( size_t i = count; i < bytesPerRow; i++ )
            prf( "   " );
        prf( " " );
        for ( size_t i = 0; i < count; i++ )
        {
            BYTE ch = profile[ offset + row + i ];
            prf( "%c", ( ch < ' ' || ch >= 127 ) ? '.' : ch );
        }
        prf( "\n" );
    }
}

void PrintICCValue( const ICC_TAG & t, const vector<BYTE> & profile, size_t profileLength )
{
    char acTag[ 5 ] = { 0 };
    memcpy( acTag, t.sig, 4 );

    size_t offset = t.dataOffset;
    size_t length = t.dataSize;
    if ( offset > profileLength || length > profileLength - offset || length < 8 )
    {
        prf( "  invalid ICC tag %s range\n", acTag );
        return;
    }

    char acType[ 5 ] = { 0 };
    memcpy( acType, profile.data() + offset, 4 );

    if ( g_FullInformation )
    {
        prf( "  tag %s, type %s\n", acTag, acType );
        DumpICCData( profile, offset, length );
    }

    if ( !strcmp( acTag, "cprt" ) && length >= 8 )
    {
        string value( (const char *) profile.data() + offset + 8, length - 8 );
        size_t terminator = value.find( '\0' );
        if ( string::npos != terminator )
            value.resize( terminator );
        prf( "  Copyright:                          %s\n", value.c_str() );
    }
    else if ( !strcmp( acTag, "desc" ) && length >= 12 )
    {
        DWORD count = ICCReadDWORD( profile, offset + 8 );
        size_t textLength = min( (size_t) count, length - 12 );
        string value( (const char *) profile.data() + offset + 12, textLength );
        size_t terminator = value.find( '\0' );
        if ( string::npos != terminator )
            value.resize( terminator );
        prf( "  ProfileDescription:                 %s\n", value.c_str() );
    }
    else if ( ( !strcmp( acTag, "wtpt" ) || !strcmp( acTag, "bkpt" ) ||
                !strcmp( acTag, "rXYZ" ) || !strcmp( acTag, "gXYZ" ) ||
                !strcmp( acTag, "bXYZ" ) || !strcmp( acTag, "lumi" ) ||
                !strcmp( acTag, "view" ) ) && length >= 20 )
    {
        DWORD x = ICCReadDWORD( profile, offset + 8 );
        DWORD y = ICCReadDWORD( profile, offset + 12 );
        DWORD z = ICCReadDWORD( profile, offset + 16 );
        const char * label = !strcmp( acTag, "wtpt" ) ? "MediaWhitePoint" :
                             !strcmp( acTag, "bkpt" ) ? "MediaBlackPoint" :
                             !strcmp( acTag, "rXYZ" ) ? "RedMatrixColumn" :
                             !strcmp( acTag, "gXYZ" ) ? "GreenMatrixColumn" :
                             !strcmp( acTag, "bXYZ" ) ? "BlueMatrixColumn" :
                             !strcmp( acTag, "lumi" ) ? "Luminance" : "ViewingConditions";
        prf( "  %s:                    %lf %lf %lf\n", label, ConvertS15Fixed16Number( x ),
                ConvertS15Fixed16Number( y ), ConvertS15Fixed16Number( z ) );
    }
    else if ( !strcmp( acTag, "dmnd" ) )
    {
        if ( length >= 12 )
        {
            size_t textLength = min( (size_t) ICCReadDWORD( profile, offset + 8 ), length - 12 );
            string value( (const char *) profile.data() + offset + 12, textLength );
            size_t terminator = value.find( '\0' );
            if ( string::npos != terminator )
                value.resize( terminator );
            prf( "  DeviceMfgDesc:                      %s\n", value.c_str() );
        }
    }
    else if ( !strcmp( acTag, "dmdd" ) )
    {
        if ( length >= 12 )
        {
            size_t textLength = min( (size_t) ICCReadDWORD( profile, offset + 8 ), length - 12 );
            string value( (const char *) profile.data() + offset + 12, textLength );
            size_t terminator = value.find( '\0' );
            if ( string::npos != terminator )
                value.resize( terminator );
            prf( "  DeviceModelDesc:                    %s\n", value.c_str() );
        }
    }
    else if ( !strcmp( acTag, "vued" ) )
    {
        if ( length >= 12 )
        {
            size_t textLength = min( (size_t) ICCReadDWORD( profile, offset + 8 ), length - 12 );
            string value( (const char *) profile.data() + offset + 12, textLength );
            size_t terminator = value.find( '\0' );
            if ( string::npos != terminator )
                value.resize( terminator );
            prf( "  ViewingCondDesc:                    %s\n", value.c_str() );
        }
    }
    else if ( !strcmp( acTag, "meas" ) )
    {
        if ( length >= 36 )
        {
            DWORD standardObserver = ICCReadDWORD( profile, offset + 8 );
            DWORD xTriStimulus = ICCReadDWORD( profile, offset + 12 );
            DWORD yTriStimulus = ICCReadDWORD( profile, offset + 16 );
            DWORD zTriStimulus = ICCReadDWORD( profile, offset + 20 );
            DWORD geometry = ICCReadDWORD( profile, offset + 24 );
            DWORD flare = ICCReadDWORD( profile, offset + 28 );
            DWORD standard = ICCReadDWORD( profile, offset + 32 );
            prf( "  Measurement:\n" );
            prf( "    standard observer:                %d (%s)\n", standardObserver, GetICCStandardObserver( standardObserver ) );
            prf( "    x tristimulus backing:            %lf\n", ConvertS15Fixed16Number( xTriStimulus ) );
            prf( "    y tristimulus backing:            %lf\n", ConvertS15Fixed16Number( yTriStimulus ) );
            prf( "    z tristimulus backing:            %lf\n", ConvertS15Fixed16Number( zTriStimulus ) );
            prf( "    measurement geometry:             %d (%s)\n", geometry, GetICCGeometry( geometry ) );
            prf( "    measurement flare:                %d (%s)\n", flare, GetICCMeasurementFlare( flare ) );
            prf( "    standard illuminant:              %d (%s)\n", standard, GetICCStandardIlluminant( standard ) );
        }
    }
    else if ( !strcmp( acTag, "tech" ) )
    {
        if ( length >= 12 )
        {
            char acTech[ 5 ] = { 0 };
            memcpy( acTech, profile.data() + offset + 8, 4 );
            prf( "  technology:                         %s\n", acTech );
        }
    }
    else if ( !strcmp( acTag, "rTRC" ) )
    {
        if ( length >= 12 )
            prf( "  Red tone reproduction curve #:      %d\n", ICCReadDWORD( profile, offset + 8 ) );
    }
    else if ( !strcmp( acTag, "gTRC" ) )
    {
        if ( length >= 12 )
            prf( "  Green tone reproduction curve #:    %d\n", ICCReadDWORD( profile, offset + 8 ) );
    }
    else if ( !strcmp( acTag, "bTRC" ) )
    {
        if ( length >= 12 )
            prf( "  Blue tone reproduction curve #:     %d\n", ICCReadDWORD( profile, offset + 8 ) );
    }
    else
    {
        if ( !g_FullInformation )
        {
            prf( "  tag %s, type %s\n", acTag, acType );
            DumpICCData( profile, offset, length );
        }
    }

} //PrintICCValue

void PrintICCProfile( const vector<BYTE> & profile )
{
    static_assert( sizeof( ICC_PROFILE_Values ) == 128, "Unexpected ICC header layout" );
    if ( profile.size() < sizeof( ICC_PROFILE_Values ) )
    {
        prf( "  incomplete ICC profile header (%d bytes)\n", (int) profile.size() );
        return;
    }

    ICC_PROFILE_Values icc;
    memcpy( &icc, profile.data(), sizeof icc );
    icc.ByteSwap( false );

    if ( icc.size < sizeof icc || icc.size > profile.size() )
    {
        prf( "  invalid ICC profile size %u (available %d bytes)\n", icc.size, (int) profile.size() );
        return;
    }

    size_t profileLength = icc.size;
    char fourcc[ 5 ] = { 0 };
    prf( "  size:                           %5u\n", icc.size );
    memcpy( fourcc, &icc.cmmType, 4 );
    prf( "  cmm type:                           %s (%#x)\n", fourcc, icc.cmmType );
    prf( "  version:                            %d.%d.%d\n", icc.version & 0xff, ( icc.version >> 12 ) & 0xf, ( icc.version >> 8 ) & 0xf );
    prf( "  profile class:                      %s (%#x)\n", GetICCProfileClass( icc.profileClass ), icc.profileClass );
    memcpy( fourcc, &icc.colorSpace, 4 );
    prf( "  color space:                        %s (%#x)\n", fourcc, icc.colorSpace );
    memcpy( fourcc, &icc.pcs, 4 );
    prf( "  connection space:                   %s (%#x)\n", fourcc, icc.pcs );
    prf( "  dd/mm/yyyy hh:mm:ss:                %02d/%02d/%04d %02d:%02d:%02d\n", icc.day, icc.month, icc.year, icc.hours, icc.minutes, icc.seconds );
    memcpy( fourcc, &icc.signature, 4 );
    prf( "  signature:                          %s (%#x)\n", fourcc, icc.signature );
    memcpy( fourcc, &icc.platform, 4 );
    prf( "  primary platform:                   %s (%#x)\n", fourcc, icc.platform );
    prf( "  cmm flags:                          %s, %s\n", ( icc.options & 0x1 ) ? "embedded" : "not embedded", ( icc.options & 0x2 ) ? "not independent" : "independent" );
    memcpy( fourcc, &icc.manufacturer, 4 );
    prf( "  manufacturer:                       %s (%#x)\n", fourcc, icc.manufacturer );
    memcpy( fourcc, &icc.model, 4 );
    prf( "  model:                              %s (%#x)\n", fourcc, icc.model );
    prf( "  attributes:                         %s, %s, %s, %s\n", ( icc.attributes & 0x1 ) ? "transparency" : "reflective",
                                                                      ( icc.attributes & 0x2 ) ? "matte" : "glossy",
                                                                      ( icc.attributes & 0x4 ) ? "polarity negative" : "polarity positive",
                                                                      ( icc.attributes & 0x8 ) ? "black and white media" : "color media" );
    prf( "  rendering intent:                   %s\n", GetICCRenderingIntent( icc.intent & 0xffff ) );
    prf( "  illuminant:                         %lf, %lf, %lf\n", ConvertS15Fixed16Number( icc.illuminantX ),
                                                                     ConvertS15Fixed16Number( icc.illuminantY ),
                                                                     ConvertS15Fixed16Number( icc.illuminantZ ) );
    memcpy( fourcc, &icc.creator, 4 );
    prf( "  creator:                            %s (%#x)\n", fourcc, icc.creator );
    prf( "  profile id:                         " );
    for ( size_t i = 0; i < _countof( icc.id ); i++ )
        prf( "%#x", icc.id[ i ] );
    prf( "\n" );

    if ( profileLength < sizeof icc + sizeof( DWORD ) )
        return;

    DWORD tagCount = ICCReadDWORD( profile, sizeof icc );
    size_t tagTableOffset = sizeof icc + sizeof( DWORD );
    if ( tagCount > ( profileLength - tagTableOffset ) / sizeof( ICC_TAG ) )
    {
        prf( "  invalid ICC tag count %u\n", tagCount );
        return;
    }

    prf( "ICC tag table has %u entries\n", tagCount );
    for ( DWORD i = 0; i < tagCount; i++ )
    {
        size_t entryOffset = tagTableOffset + (size_t) i * sizeof( ICC_TAG );
        ICC_TAG tag;
        memcpy( tag.sig, profile.data() + entryOffset, 4 );
        tag.dataOffset = ICCReadDWORD( profile, entryOffset + 4 );
        tag.dataSize = ICCReadDWORD( profile, entryOffset + 8 );
        PrintICCValue( tag, profile, profileLength );
    }
}

void PrintXMPField( const char * data, size_t length, const char * name, const char * label )
{
    string key = string( name ) + "=\"";
    const char * field = strstr( data, key.c_str() );
    char terminator = '"';

    if ( !field )
    {
        key = string( "<" ) + name;
        field = strstr( data, key.c_str() );
        if ( field )
        {
            const char * tagEnd = (const char *) memchr( field, '>', length - min( length, (size_t) ( field - data ) ) );
            if ( tagEnd )
            {
                field = tagEnd + 1;
                const char * listItem = strstr( field, "<rdf:li" );
                if ( listItem )
                {
                    const char * listItemEnd = strchr( listItem, '>' );
                    if ( listItemEnd )
                        field = listItemEnd + 1;
                }
                terminator = '<';
            }
            else
                field = NULL;
        }
    }

    if ( !field )
        return;

    if ( '"' == terminator )
        field += key.size();
    size_t available = length - min( length, (size_t) ( field - data ) );
    size_t valueLength = 0;
    while ( valueLength < available && field[ valueLength ] != terminator )
        valueLength++;

    if ( valueLength )
        prf( "  %-22s: %.*s\n", label, (int) min( valueLength, (size_t) 200 ), field );
}

void PrintXMPData( const char * pcIn, size_t dataLength = 0 )
{
    if ( 0 == dataLength )
        dataLength = strlen( pcIn );

    prf( "XMP metadata:\n" );
    PrintXMPField( pcIn, dataLength, "xmp:Rating", "Rating" );
    PrintXMPField( pcIn, dataLength, "xap:Rating", "Rating" );
    PrintXMPField( pcIn, dataLength, "xmp:Label", "Label" );
    PrintXMPField( pcIn, dataLength, "xmp:CreatorTool", "CreatorTool" );
    PrintXMPField( pcIn, dataLength, "photoshop:DateCreated", "DateCreated" );
    PrintXMPField( pcIn, dataLength, "dc:title", "Title" );
    PrintXMPField( pcIn, dataLength, "dc:description", "Description" );
    PrintXMPField( pcIn, dataLength, "dc:creator", "Creator" );
    PrintXMPField( pcIn, dataLength, "dc:rights", "Rights" );
} //PrintXMPData

int ParseOldJpg( DWORD startingOffset = 0, __int64 maxLength = 0 )
{
    prf( "mimetype:                             image/jpeg\n" );

    __int64 offset = 2 + (__int64) startingOffset;
    __int64 fileLength = GetStreamLength();
    if ( maxLength > 0 )
        fileLength = min( fileLength, (__int64) startingOffset + maxLength );
    int exifOffset = 0;
    int adobeColorTransform = -1;
    vector<vector<BYTE>> iccChunks;
    BYTE iccSegmentCount = 0;
    bool invalidICC = false;

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

    while ( offset + 2 <= fileLength )
    {
        BYTE marker = GetBYTE( offset );
        if ( 0xff != marker )
        {
            prf( "invalid JPEG marker at offset %#llx\n", offset );
            return exifOffset;
        }

        while ( offset + 1 < fileLength && 0xff == GetBYTE( offset + 1 ) )
            offset++;

        BYTE segment = GetBYTE( offset + 1 );

        if ( g_FullInformation )
            prf( "jpg offset %d, segment %#x\n", offset, segment );

        if ( MARKER_EOI == segment )
            break;

        if ( 0x01 == segment || ( segment >= 0xd0 && segment <= 0xd8 ) )
        {
            offset += 2;
            continue;
        }

        if ( offset + 4 > fileLength )
        {
            prf( "truncated JPEG marker %#x at offset %#llx\n", segment, offset );
            break;
        }

        WORD length = GetWORD( offset + 2, false );
        if ( length < 2 || offset + 2 + length > fileLength )
        {
            prf( "invalid JPEG segment length %d for marker %#x at offset %#llx\n", length, segment, offset );
            break;
        }
        size_t data_length = length - 2;
        __int64 dataOffset = offset + 4;

        if ( g_FullInformation )
            prf( "  segment length %d\n", length );

        if ( MARKER_DQT == segment )
        {
            if ( 0 == data_length )
            {
                prf( "empty JPEG quantization table segment\n" );
                break;
            }
            size_t pos = 0;
            int tableCount = 0;
            bool has16BitTables = false;
            while ( pos < data_length )
            {
                BYTE tableInfo = GetBYTE( dataOffset + pos++ );
                BYTE precision = tableInfo >> 4;
                BYTE tableID = tableInfo & 0x0f;
                size_t tableBytes = 64 * ( precision ? 2 : 1 );
                if ( precision > 1 || tableID > 3 || tableBytes > data_length - pos )
                {
                    prf( "invalid JPEG quantization table in segment at offset %#llx\n", offset );
                    break;
                }
                has16BitTables = has16BitTables || 1 == precision;
                pos += tableBytes;
                tableCount++;
            }
            if ( pos != data_length )
                break;
            prf( "quantization tables:                 %d (16-bit precision: %s)\n",
                 tableCount, has16BitTables ? "yes" : "no" );
        }
        else if ( MARKER_COM == segment )
        {
            prf( "comment:                           " );
            for ( size_t i = 0; i < data_length; i++ )
            {
                BYTE ch = GetBYTE( dataOffset + i );
                if ( ch >= ' ' && ch < 127 && '\\' != ch )
                    prf( "%c", ch );
                else if ( '\\' == ch )
                    prf( "\\\\" );
                else
                    prf( "\\x%02x", ch );
            }
            prf( "\n" );
        }
        else if ( 0xc4 == segment || 0xc8 == segment || 0xcc == segment )
        {
            // These 3 fall in the range of the valid SOF? segments, so watch for them and ignore
        }
        else if ( segment >= 0xc0 && segment <= 0xcf )
        {
            if ( 0xc4 == segment || 0xc8 == segment || 0xcc == segment )
            {
                offset += length + 2;
                continue;
            }
            if ( data_length < 6 )
            {
                prf( "invalid JPEG frame header length %d\n", length );
                break;
            }

            prf( "start of frame:                   %5d (%#x) (%s)\n", segment, segment, SOFMarker( segment ) );

            byte precision = GetBYTE( dataOffset );
            WORD height = GetWORD( dataOffset + 1, false );
            WORD width = GetWORD( dataOffset + 3, false );
            byte components = GetBYTE( dataOffset + 5 );
            if ( data_length < 6 + (size_t) components * 3 )
            {
                prf( "invalid JPEG frame component table\n" );
                break;
            }

            prf( "bits per sample:                  %5d\n", precision );
            prf( "image height:                     %5d\n", height );
            prf( "image width:                      %5d\n", width );
            BYTE componentIDs[ 256 ] = { 0 };
            BYTE samplingFactors[ 256 ] = { 0 };

            for ( int component = 0; component < components; component++ )
            {
                componentIDs[ component ] = GetBYTE( dataOffset + 6 + component * 3 );
                samplingFactors[ component ] = GetBYTE( dataOffset + 7 + component * 3 );
                int samplingFactor = samplingFactors[ component ];
                int quantizationTable = GetBYTE( dataOffset + 8 + component * 3 );

                if ( g_FullInformation )
                {
                    prf( "  component %d ID  %5d\n", component, componentIDs[ component ] );
                    prf( "  component %d sampling factor  vertical %d, horizontal %d\n", component, samplingFactor & 0xf, samplingFactor >> 4 );
                    prf( "  component %d quantization table %d\n", component, quantizationTable );
                }
            }

            const char * colorSpace = JPGComponents( components, componentIDs, samplingFactors, adobeColorTransform );
            prf( "color components:                 %5d (%s)\n", components, colorSpace );

            if ( 3 == components && 0 == strcmp( colorSpace, "YCbCr" ) )
            {
                int samplingFactor = GetBYTE( dataOffset + 7 );
                int horizS = samplingFactor >> 4;
                int vertS = samplingFactor & 0xf;
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
            char identifier[ 6 ] = { 0 };
            size_t identifierLength = min( data_length, (size_t) 5 );
            if ( identifierLength )
                GetBytes( dataOffset, identifier, (int) identifierLength );

            if ( 5 == identifierLength && 0 == memcmp( identifier, "JFIF\0", 5 ) )
            {
                if ( data_length < 14 )
                {
                    prf( "invalid short JFIF APP0 segment\n" );
                    break;
                }

                BYTE majorVersion = GetBYTE( dataOffset + 5 );
                BYTE minorVersion = GetBYTE( dataOffset + 6 );
                BYTE units = GetBYTE( dataOffset + 7 );
                WORD xDensity = GetWORD( dataOffset + 8, false );
                WORD yDensity = GetWORD( dataOffset + 10, false );

                prf( "id marker:                            JFIF\n" );
                prf( "major version:                    %5d\n", majorVersion );
                prf( "minor version:                    %5d\n", minorVersion );
                prf( "units:                                %s\n", ( 0 == units ) ? "none" : ( 1 == units ) ? "dots/inch" : ( 2 == units ) ? "dots/cm" : "unknown" );
                prf( "x density:                        %5d\n", xDensity );
                prf( "y density:                        %5d\n", yDensity );
            }
            else
            {
                prf( "APP0 identifier:                     %s\n", identifier );
            }
        }
        else if ( MARKER_APP2 == segment )
        {
            char acHeader[ 13 ] = { 0 };
            size_t headerLength = min( data_length, (size_t) 12 );
            if ( headerLength )
                GetBytes( dataOffset, acHeader, (int) headerLength );
            prf( "APP2 header %s\n", acHeader );

            if ( 12 == headerLength && 0 == memcmp( acHeader, "ICC_PROFILE", 11 ) && 0 == acHeader[ 11 ] )
            {
                if ( data_length < 14 )
                {
                    prf( "invalid short ICC_PROFILE APP2 segment\n" );
                    invalidICC = true;
                }
                else
                {
                    BYTE sequence = GetBYTE( dataOffset + 12 );
                    BYTE segmentCount = GetBYTE( dataOffset + 13 );
                    if ( 0 == sequence || 0 == segmentCount || sequence > segmentCount ||
                         ( 0 != iccSegmentCount && iccSegmentCount != segmentCount ) )
                    {
                        prf( "invalid ICC_PROFILE chunk sequence %d of %d\n", sequence, segmentCount );
                        invalidICC = true;
                    }
                    else
                    {
                        if ( 0 == iccSegmentCount )
                        {
                            iccSegmentCount = segmentCount;
                            iccChunks.resize( segmentCount );
                        }

                        vector<BYTE> & chunk = iccChunks[ sequence - 1 ];
                        if ( !chunk.empty() )
                        {
                            prf( "duplicate ICC_PROFILE chunk %d\n", sequence );
                            invalidICC = true;
                        }
                        else
                        {
                            size_t chunkLength = data_length - 14;
                            if ( 0 == chunkLength )
                            {
                                prf( "empty ICC_PROFILE chunk %d\n", sequence );
                                invalidICC = true;
                            }
                            else
                            {
                                chunk.resize( chunkLength );
                                GetBytes( dataOffset + 14, chunk.data(), (int) chunkLength );
                            }
                        }
                    }
                }
            }
        }
        else if ( MARKER_SOS == segment )
        {
            if ( data_length < 1 )
            {
                prf( "invalid JPEG scan header\n" );
                break;
            }
            BYTE numberOfComponents = GetBYTE( dataOffset );
            if ( data_length < 1 + (size_t) numberOfComponents * 2 + 3 )
            {
                prf( "invalid JPEG scan component table\n" );
                break;
            }
            prf( "number of components:                 %d\n", numberOfComponents );

            for ( int i = 0; i < numberOfComponents; i++ )
            {
                if ( g_FullInformation )
                {
                    BYTE c = GetBYTE( dataOffset + 1 + i * 2 );
                    prf( "  Component ID [%d]: %s", i, ( 1 == c ) ? "Y" : ( 2 == c ) ? "Cb" : ( 3 == c ) ? "Cr" : ( 4 == c ) ? "I" : ( 5 == c ) ? "Q" : "unknown" );
                    BYTE tables = GetBYTE( dataOffset + 2 + i * 2 );
                    prf( " -- DC table: %d, AC table %d\n", tables & 0xf, tables >> 4 );
                }
            }

            __int64 scanOffset = offset + length + 2;
            const size_t scanBufferSize = 64 * 1024 + 1;
            vector<BYTE> scanBuffer( scanBufferSize );
            bool foundMarker = false;
            while ( scanOffset + 1 < fileLength )
            {
                size_t available = (size_t) min( (__int64) scanBufferSize, fileLength - scanOffset );
                GetBytes( scanOffset, scanBuffer.data(), (int) available );
                for ( size_t i = 0; i + 1 < available; )
                {
                    if ( 0xff != scanBuffer[ i ] )
                    {
                        i++;
                        continue;
                    }

                    size_t markerByteOffset = i + 1;
                    while ( markerByteOffset < available && 0xff == scanBuffer[ markerByteOffset ] )
                        markerByteOffset++;
                    if ( markerByteOffset == available )
                        break;

                    BYTE scanMarker = scanBuffer[ markerByteOffset ];
                    if ( 0 == scanMarker || ( scanMarker >= 0xd0 && scanMarker <= 0xd7 ) )
                    {
                        i = markerByteOffset + 1;
                        continue;
                    }

                    offset = scanOffset + markerByteOffset - 1;
                    foundMarker = true;
                    break;
                }
                if ( foundMarker )
                    break;
                if ( available <= 1 )
                    break;
                scanOffset += available - 1;
            }
            if ( !foundMarker )
                break;
            continue;
        }
        else if ( MARKER_PS3 == segment )
        {
            prf( "photoshop irb data:\n" );

            DumpBinaryData( dataOffset, 0, (DWORD) data_length, 8, 0 );
        }
        else if ( MARKER_APP1 == segment )
        {
            char app1Header[ 5 ] = { 0 };
            if ( data_length >= 4 )
                GetBytes( dataOffset, app1Header, 4 );

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
                unique_ptr<char[]> bytes( new char[ data_length + 1 ] );
                GetBytes( dataOffset, bytes.get(), (int) data_length );
                bytes[ data_length ] = 0;
                const char xmpIdentifier[] = "http://ns.adobe.com/xap/1.0/";
                size_t identifierLength = sizeof( xmpIdentifier ) - 1;
                if ( data_length > identifierLength &&
                     0 == memcmp( bytes.get(), xmpIdentifier, identifierLength ) &&
                     0 == bytes[ identifierLength ] )
                    PrintXMPData( bytes.get() + identifierLength + 1, data_length - identifierLength - 1 );
            }
        }
        else if ( MARKER_DRI == segment )
        {
            if ( data_length >= 2 )
                prf( "define restart interval:   %12d\n", GetWORD( dataOffset, false ) );
        }
        else if ( MARKER_APP14 == segment )
        {
            if ( data_length >= 12 )
            {
                char signature[ 5 ] = { 0 };
                GetBytes( dataOffset, signature, 5 );
                if ( 0 == memcmp( signature, "Adobe", 5 ) )
                {
                    WORD DCTEncodeVersion = GetWORD( dataOffset + 5, false );
                    WORD App14Flags0 = GetWORD( dataOffset + 7, false );
                    WORD App14Flags1 = GetWORD( dataOffset + 9, false );
                    BYTE ColorTransform = GetBYTE( dataOffset + 11 );
                    adobeColorTransform = ColorTransform;

                    prf( "app14 Adobe data:\n" );
                    prf( "  DCT encode version:      %12d\n", DCTEncodeVersion );
                    prf( "  app14 flags0:            %12d\n", App14Flags0 );
                    prf( "  app14 flags1:            %12d\n", App14Flags1 );
                    prf( "  color transform:         %12d (%s)\n", ColorTransform, GetJPGApp14ColorTransform( ColorTransform ) );
                }
            }
        }
        else
        {
            prf( "unparsed segment %#x data_length %u\n", segment, (unsigned int) data_length );
            DumpBinaryData( dataOffset, 0, (DWORD) data_length, 8, (DWORD) dataOffset );
        }

        offset += ( length + 2 );
    }

    if ( !invalidICC && 0 != iccSegmentCount )
    {
        bool complete = true;
        size_t profileLength = 0;
        for ( const vector<BYTE> & chunk : iccChunks )
        {
            if ( chunk.empty() )
                complete = false;
            profileLength += chunk.size();
        }

        if ( complete )
        {
            vector<BYTE> profile;
            profile.reserve( profileLength );
            for ( const vector<BYTE> & chunk : iccChunks )
                profile.insert( profile.end(), chunk.begin(), chunk.end() );
            PrintICCProfile( profile );
        }
        else
            prf( "incomplete ICC_PROFILE sequence: found fewer than %d chunks\n", iccSegmentCount );
    }

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

    __int64 fileLength = g_pStream->Length();
    __int64 offset = 4;
    unsigned long long bestPictureArea = 0;
    DWORD bestPictureLength = 0;
    bool sawStreamInfo = false;

    while ( offset <= fileLength && fileLength - offset >= 4 )
    {
        DWORD blockHeader = GetDWORD( offset, false );
        offset += 4;

        bool lastBlock = ( 0 != ( 0x80000000 & blockHeader ) );
        BYTE blockType = ( ( blockHeader >> 24 ) & 0x7f );
        DWORD length = blockHeader & 0xffffff;
        __int64 blockEnd = offset + length;

        if ( g_FullInformation )
            prf( "FLAC header %#x, last block %d, block type %d, length %d, offset %#I64d\n",
                 blockHeader, lastBlock, blockType, length, offset );

        if ( length > (QWORD) ( fileLength - offset ) )
        {
            prf( "invalid FLAC metadata block length: %u\n", length );
            return;
        }

        if ( 0 == blockType )
        {
            if ( sawStreamInfo || 34 != length )
            {
                prf( "invalid FLAC STREAMINFO block length or position: %u\n", length );
                return;
            }
            sawStreamInfo = true;

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
            BYTE channelCount = (BYTE) ( 1 + ( ( threeFields >> 9 ) & 0x7 ) );
            BYTE bitsPerSample = (BYTE) ( 1 + ( ( threeFields >> 4 ) & 0x1f ) );

            unsigned long long ullSamples = GetULONGLONG( offset + 13, false );
            ullSamples &= 0x0fffffffffffffff;
            ullSamples >>= 24;

            if ( 0 == sampleRate )
            {
                prf( "invalid FLAC sample rate: 0\n" );
                return;
            }

            unsigned long long lengthInSeconds = ullSamples / sampleRate;
            unsigned long long lengthInMinutes = lengthInSeconds / 60;
            unsigned long long remainderSeconds = lengthInSeconds % 60;

            prf( "Stream Info:\n" );
            prf( "  Minimum block size      %13d\n", minBlockSize );
            prf( "  Maximum block size      %13d\n", maxBlockSize );
            prf( "  Minimum frame size      %13d\n", minFrameSize );
            prf( "  Maximum frame size      %13d\n", maxFrameSize );
            prf( "  Sample rate per sec     %13d\n", sampleRate );
            prf( "  Channel count           %13d\n", channelCount );
            prf( "  Bits per sample         %13d\n", bitsPerSample );
            prf( "  Samples                 %13I64u\n", ullSamples );
            prf( "  Length in min:sec       %10I64u:%02I64u\n", lengthInMinutes, remainderSeconds );
            prf( "  MD5 signature                       " );

            for ( int i = 0; i < 4; i++ )
            {
                DWORD x = GetDWORD( offset + 18 + i * 4, false );
                prf( "%08x", x );
            }
            prf( "\n" );
        }
        else if ( 4 == blockType )
        {
            prf( "Comments:\n" );
            if ( length < sizeof( DWORD ) )
            {
                prf( "invalid FLAC Vorbis comment block: missing vendor length\n" );
            }
            else
            {
                __int64 o = offset;
                DWORD vendorLength = GetDWORD( o, true );
                o += sizeof DWORD;
                if ( vendorLength > (QWORD) ( blockEnd - o ) )
                {
                    prf( "invalid FLAC Vorbis vendor length: %u\n", vendorLength );
                }
                else
                {
                    unique_ptr<WCHAR[]> vendor = GetUTF8( o, vendorLength );
                    o += vendorLength;
                    if ( !vendor )
                    {
                        prf( "could not decode FLAC Vorbis vendor string\n" );
                    }
                    else
                    {
                        wprf( L"  Vendor                              %ls\n", vendor.get() );
                        if ( blockEnd - o < sizeof DWORD )
                        {
                            prf( "invalid FLAC Vorbis comment block: missing item count\n" );
                        }
                        else
                        {
                            DWORD items = GetDWORD( o, true );
                            o += sizeof DWORD;
                            for ( DWORD i = 0; i < items; i++ )
                            {
                                if ( blockEnd - o < sizeof DWORD )
                                {
                                    prf( "truncated FLAC Vorbis comment length\n" );
                                    break;
                                }

                                DWORD itemLength = GetDWORD( o, true );
                                o += sizeof DWORD;
                                if ( itemLength > (QWORD) ( blockEnd - o ) )
                                {
                                    prf( "invalid FLAC Vorbis comment length: %u\n", itemLength );
                                    break;
                                }

                                unique_ptr<WCHAR[]> item = GetUTF8( o, itemLength );
                                o += itemLength;
                                if ( !item )
                                {
                                    prf( "could not decode FLAC Vorbis comment\n" );
                                    continue;
                                }

                                WCHAR * equal = wcschr( item.get(), L'=' );
                                if ( NULL == equal )
                                {
                                    wprf( L"  %ls\n", item.get() );
                                    continue;
                                }

                                size_t fieldLength = (size_t) ( equal - item.get() ) + 1;
                                const WCHAR * fieldName = item.get();
                                const WCHAR * value = equal + 1;
                                WCHAR field[ 100 ];
                                if ( fieldLength >= _countof( field ) )
                                {
                                    wprf( L"  %ls\n", item.get() );
                                    continue;
                                }

                                CopyMemory( field, fieldName, fieldLength * sizeof( WCHAR ) );
                                field[ fieldLength ] = 0;
                                const WCHAR * displayName = LookupFlacField( field );
                                if ( NULL == displayName )
                                    wprf( L"  %ls\n", item.get() );
                                else if ( !wcscmp( displayName, L"Lyrics" ) )
                                    wprf( L"  %-15ls\n%ls\n", displayName, value );
                                else
                                    wprf( L"  %-15ls                     %ls\n", displayName, value );
                            }
                        }
                    }
                }
            }
        }
        else if ( 6 == blockType )
        {
            prf( "Picture:\n" );
            if ( length < 32 )
            {
                prf( "invalid FLAC picture block: too short\n" );
            }
            else
            {
                __int64 o = offset;
                DWORD pictureType = GetDWORD( o, false );
                prf( "  Picture type            %13u ", pictureType );
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

                DWORD mimeTypeLength = GetDWORD( o, false );
                o += 4;
                if ( mimeTypeLength > (QWORD) ( blockEnd - o ) )
                {
                    prf( "invalid FLAC picture MIME type length: %u\n", mimeTypeLength );
                }
                else
                {
                    unique_ptr<char[]> mimeType( new char[ (size_t) mimeTypeLength + 1 ] );
                    if ( mimeTypeLength &&
                         ( !g_pStream->Seek( o ) ||
                           g_pStream->Read( mimeType.get(), mimeTypeLength ) != mimeTypeLength ) )
                    {
                        prf( "could not read FLAC picture MIME type\n" );
                    }
                    else
                    {
                        mimeType[ mimeTypeLength ] = 0;
                        prf( "  mime type                           %s\n", mimeType.get() );
                        o += mimeTypeLength;

                        if ( blockEnd - o < sizeof DWORD )
                        {
                            prf( "invalid FLAC picture description length\n" );
                        }
                        else
                        {
                            DWORD descriptionLength = GetDWORD( o, false );
                            o += sizeof DWORD;
                            if ( descriptionLength > (QWORD) ( blockEnd - o ) )
                            {
                                prf( "invalid FLAC picture description length: %u\n", descriptionLength );
                            }
                            else
                            {
                                unique_ptr<WCHAR[]> description = GetUTF8( o, descriptionLength );
                                o += descriptionLength;
                                if ( description )
                                    wprf( L"  Description             %ls\n", description.get() );
                                else
                                    prf( "could not decode FLAC picture description\n" );

                                if ( blockEnd - o < 20 )
                                {
                                    prf( "invalid FLAC picture dimensions or image length\n" );
                                }
                                else
                                {
                                    DWORD width = GetDWORD( o, false ); o += 4;
                                    DWORD height = GetDWORD( o, false ); o += 4;
                                    DWORD bpp = GetDWORD( o, false ); o += 4;
                                    DWORD indexedColors = GetDWORD( o, false ); o += 4;
                                    DWORD imageLength = GetDWORD( o, false ); o += 4;
                                    prf( "  Width                   %13u\n", width );
                                    prf( "  Height                  %13u\n", height );
                                    prf( "  Bits per pixel          %13u\n", bpp );
                                    prf( "  Indexed colors          %13u\n", indexedColors );
                                    prf( "  Image size              %13u\n", imageLength );

                                    if ( imageLength > (QWORD) ( blockEnd - o ) )
                                    {
                                        prf( "invalid FLAC picture image length: %u\n", imageLength );
                                    }
                                    else if ( imageLength > 2 && IsPerhapsAnImage( o, 0 ) )
                                    {
                                        unsigned long long pictureArea = (unsigned long long) width * height;
                                        if ( pictureArea > bestPictureArea ||
                                             ( pictureArea == bestPictureArea && imageLength > bestPictureLength ) )
                                        {
                                            bestPictureArea = pictureArea;
                                            bestPictureLength = imageLength;
                                            g_Embedded_Image_Offset = o;
                                            g_Embedded_Image_Length = imageLength;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        offset = blockEnd;
        if ( lastBlock )
            break;
    }

    if ( !sawStreamInfo )
        prf( "missing FLAC STREAMINFO block\n" );
} //EnumerateFlac

class HeifStream
{
    private:
        __int64 offset;
        __int64 length;
        CStream * pStream;
        bool valid;

    public:
        HeifStream( CStream *ps, __int64 o, __int64 l )
        {
            offset = o;
            length = l;
            pStream = ps;
            valid = ( NULL != ps && o >= 0 && l >= 0 );
        }

        __int64 Length() { return length; }
        __int64 Offset() { return offset; }
        CStream * Stream() { return pStream; }
        bool Valid() const { return valid; }
        __int64 SetLengthLimit( __int64 limit )
        {
            __int64 previousLength = length;
            if ( limit >= 0 && limit <= length )
                length = limit;
            else
                valid = false;
            return previousLength;
        }
        void RestoreLength( __int64 previousLength ) { length = previousLength; }

        bool CanRead( __int64 streamOffset, __int64 byteCount ) const
        {
            return valid && streamOffset >= 0 && byteCount >= 0 &&
                   streamOffset <= length && byteCount <= length - streamOffset;
        }

        ULONGLONG GetVariable( __int64 & streamOffset, int byteCount )
        {
            if ( byteCount < 0 || byteCount > 8 || !CanRead( streamOffset, byteCount ) ||
                 offset > LLONG_MAX - streamOffset )
            {
                valid = false;
                return 0;
            }

            BYTE bytes[ 8 ] = {};
            if ( byteCount &&
                 ( !pStream->Seek( offset + streamOffset ) ||
                   pStream->Read( bytes, byteCount ) != (ULONG) byteCount ) )
            {
                valid = false;
                return 0;
            }

            ULONGLONG value = 0;
            for ( int i = 0; i < byteCount; i++ )
                value = ( value << 8 ) | bytes[ i ];
            streamOffset += byteCount;
            return value;
        }

        DWORD GetDWORD( __int64 & streamOffset, bool littleEndian )
        {
            DWORD value = (DWORD) GetVariable( streamOffset, sizeof value );
            return littleEndian ? _byteswap_ulong( value ) : value;
        } //GetDWORD

        WORD GetWORD( __int64 & streamOffset, bool littleEndian )
        {
            WORD value = (WORD) GetVariable( streamOffset, sizeof value );
            return littleEndian ? _byteswap_ushort( value ) : value;
        } //GetWORD

        byte GetBYTE( __int64 & streamOffset )
        {
            return (BYTE) GetVariable( streamOffset, sizeof( BYTE ) );
        } //GetBYTE

        void GetBytes( __int64 & streamOffset, void * pData, int byteCount )
        {
            if ( byteCount < 0 || !CanRead( streamOffset, byteCount ) ||
                 offset > LLONG_MAX - streamOffset )
            {
                valid = false;
                return;
            }

            memset( pData, 0, byteCount );
            if ( byteCount &&
                 ( !pStream->Seek( offset + streamOffset ) ||
                   pStream->Read( pData, byteCount ) != (ULONG) byteCount ) )
                valid = false;
        }
}; //HeifStream

struct M4AParseState
{
    DWORD movieTimeScale = 0;
    ULONGLONG movieDuration = 0;
    ULONGLONG mediaDataBytes = 0;
    DWORD sampleRate = 0;
    WORD channels = 0;
    WORD bitsPerSample = 0;
    bool audioTrack = false;
    string audioFormat;
    bool metadataHeaderPrinted = false;
};

const char * M4AMetadataName( const char * tag )
{
    if ( (BYTE) tag[ 0 ] == 0xa9 )
    {
        if ( 0 == strcmp( tag + 1, "nam" ) ) return "Title";
        if ( 0 == strcmp( tag + 1, "ART" ) ) return "Artist";
        if ( 0 == strcmp( tag + 1, "alb" ) ) return "Album";
        if ( 0 == strcmp( tag + 1, "cmt" ) ) return "Comment";
        if ( 0 == strcmp( tag + 1, "day" ) ) return "Year";
        if ( 0 == strcmp( tag + 1, "gen" ) ) return "Genre";
        if ( 0 == strcmp( tag + 1, "wrt" ) ) return "Composer";
        if ( 0 == strcmp( tag + 1, "too" ) ) return "Encoder";
        if ( 0 == strcmp( tag + 1, "grp" ) ) return "Grouping";
        if ( 0 == strcmp( tag + 1, "lyr" ) ) return "Lyrics";
    }
    else
    {
        if ( 0 == strcmp( tag, "aART" ) ) return "Album Artist";
        if ( 0 == strcmp( tag, "cprt" ) ) return "Copyright";
        if ( 0 == strcmp( tag, "trkn" ) ) return "Track";
        if ( 0 == strcmp( tag, "disk" ) ) return "Disc";
        if ( 0 == strcmp( tag, "covr" ) ) return "Cover Art";
        if ( 0 == strcmp( tag, "tmpo" ) ) return "BPM";
        if ( 0 == strcmp( tag, "cpil" ) ) return "Compilation";
        if ( 0 == strcmp( tag, "pgap" ) ) return "Gapless Playback";
    }

    return NULL;
}

void ParseM4AData( HeifStream & hs, __int64 & offset, ULONGLONG boxLength,
                   M4AParseState & state, const char * itemTag )
{
    ULONGLONG payloadLength = boxLength - 8;
    if ( payloadLength < 8 )
    {
        prf( "invalid M4A metadata data box for %s\n", itemTag );
        return;
    }

    DWORD dataType = hs.GetDWORD( offset, false ) & 0xff;
    hs.GetDWORD( offset, false );
    payloadLength -= 8;

    const char * name = M4AMetadataName( itemTag );
    if ( NULL == name )
        return;

    if ( !state.metadataHeaderPrinted )
    {
        prf( "M4A metadata:\n" );
        state.metadataHeaderPrinted = true;
    }

    if ( 0 == strcmp( itemTag, "covr" ) )
    {
        prf( "  %-22s: %I64u bytes (%s)\n", name, payloadLength,
             13 == dataType ? "JPEG" : 14 == dataType ? "PNG" : "image data" );
    }
    else if ( 0 == strcmp( itemTag, "trkn" ) || 0 == strcmp( itemTag, "disk" ) )
    {
        if ( payloadLength < 6 )
        {
            prf( "  %-22s: invalid track/disc data (%I64u bytes)\n", name, payloadLength );
            return;
        }
        hs.GetWORD( offset, false );
        WORD number = hs.GetWORD( offset, false );
        WORD total = hs.GetWORD( offset, false );
        prf( "  %-22s: %u", name, number );
        if ( total )
            prf( " of %u", total );
        prf( "\n" );
    }
    else if ( 1 == dataType || 2 == dataType )
    {
        if ( payloadLength > 16 * 1024 * 1024 || payloadLength > MAXDWORD )
        {
            prf( "  %-22s: text too large to decode (%I64u bytes)\n", name, payloadLength );
            return;
        }

        DWORD textLength = (DWORD) payloadLength;
        unique_ptr<char[]> bytes( new char[ (size_t) textLength + 2 ] );
        hs.GetBytes( offset, bytes.get(), (int) textLength );
        if ( !hs.Valid() )
        {
            prf( "  %-22s: truncated text data\n", name );
            return;
        }

        unique_ptr<WCHAR[]> text;
        if ( 1 == dataType )
            text = GetUTF8( hs.Offset() + offset, textLength );
        else
        {
            if ( textLength & 1 )
            {
                prf( "  %-22s: invalid UTF-16 text length %u\n", name, textLength );
                return;
            }
            text.reset( new WCHAR[ textLength / 2 + 1 ] );
            for ( DWORD i = 0; i < textLength / 2; i++ )
                text[ i ] = (WCHAR) ( (BYTE) bytes[ i * 2 ] << 8 | (BYTE) bytes[ i * 2 + 1 ] );
            text[ textLength / 2 ] = 0;
        }

        if ( text )
            wprf( L"  %-22S: %ls\n", name, text.get() );
        else
            prf( "  %-22s: could not decode text\n", name );
    }
    else if ( payloadLength <= 4 && payloadLength > 0 )
    {
        ULONGLONG value = hs.GetVariable( offset, (int) payloadLength );
        prf( "  %-22s: %I64u\n", name, value );
    }
}

void PrintHeifPrimaryProperties()
{
    if ( 0xffffffff == g_Heif_Primary_ItemID || g_Heif_Primary_Properties.empty() )
        return;

    bool printedDimensions = false;
    bool printedBitDepth = false;
    bool printedColorProfile = false;
    bool printedRotation = false;
    bool printedMirror = false;

    for ( DWORD propertyID : g_Heif_Primary_Properties )
    {
        if ( 0 == propertyID || propertyID > g_Heif_Properties.size() )
            continue;

        const HeifProperty & property = g_Heif_Properties[ propertyID - 1 ];

        if ( !printedDimensions && property.width && property.height )
        {
            prf( "HEIF primary image dimensions: %u x %u\n", property.width, property.height );
            printedDimensions = true;
        }

        if ( !printedBitDepth && property.bitsPerChannel )
        {
            prf( "HEIF image bit depth: %u bits per channel\n", property.bitsPerChannel );
            printedBitDepth = true;
        }
        else if ( !printedBitDepth && property.lumaBitDepth && property.chromaBitDepth )
        {
            prf( "HEIF image bit depth: %u-bit luma, %u-bit chroma\n",
                 property.lumaBitDepth, property.chromaBitDepth );
            printedBitDepth = true;
        }

        if ( property.hasRotation && !printedRotation && property.rotation )
        {
            prf( "HEIF image rotation: %u degrees counterclockwise\n", property.rotation * 90 );
            printedRotation = true;
        }

        if ( property.hasMirror && !printedMirror )
        {
            prf( "HEIF image mirror axis: %s\n", property.mirrorAxis ? "vertical" : "horizontal" );
            printedMirror = true;
        }

        if ( !printedColorProfile && !property.iccProfile.empty() )
        {
            prf( "HEIF ICC profile:\n" );
            PrintICCProfile( property.iccProfile );
            printedColorProfile = true;
        }
    }
} //PrintHeifPrimaryProperties

// Heif and CR3 use ISO Base Media File Format ISO/IEC 14496-12. This function walks those files and pulls out data including Exif offsets

void EnumerateBoxes( HeifStream & hs, DWORD depth, M4AParseState * m4a = NULL,
                     bool insideIlst = false, const char * itemTag = NULL )
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

        if ( !hs.CanRead( offset, 8 ) )
        {
            prf( "%*struncated box header at offset %I64d\n", indent, "", offset );
            break;
        }

        __int64 boxOffset = offset;
        ULONGLONG boxLen = hs.GetDWORD( offset, false );
        DWORD dwTag = hs.GetDWORD( offset, false );
        char tag[ 5 ];
        tag[ 3 ] = dwTag & 0xff;
        tag[ 2 ] = ( dwTag >> 8 ) & 0xff;
        tag[ 1 ] = ( dwTag >> 16 ) & 0xff;
        tag[ 0 ] = ( dwTag >> 24 ) & 0xff;
        tag[ 4 ] = 0;

        ULONGLONG headerLength = 8;
        if ( 1 == boxLen )
        {
            if ( !hs.CanRead( offset, 8 ) )
            {
                prf( "%*struncated extended box header at offset %I64d\n", indent, "", boxOffset );
                break;
            }

            ULONGLONG high = hs.GetDWORD( offset, false );
            ULONGLONG low = hs.GetDWORD( offset, false );
            boxLen = ( high << 32 ) | low;
            headerLength = 16;
        }

        if ( !hs.Valid() )
        {
            prf( "%*sinvalid box header at offset %I64d\n", indent, "", boxOffset );
            break;
        }

        if ( 0 == boxLen )
            boxLen = (ULONGLONG) ( hs.Length() - boxOffset );

        if ( boxLen < headerLength || boxLen > (ULONGLONG) ( hs.Length() - boxOffset ) )
        {
            prf( "%*sinvalid %s box length %I64u at offset %I64d (remaining %I64d)\n",
                 indent, "", tag, boxLen, boxOffset, hs.Length() - boxOffset );
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

        size_t propertyIndex = 0;
        bool isProperty = g_Heif_ReadingProperties;
        if ( isProperty )
        {
            HeifProperty property;
            property.type = tag;
            propertyIndex = g_Heif_Properties.size();
            g_Heif_Properties.push_back( property );
        }

        __int64 enclosingLength = hs.SetLengthLimit( boxOffset + (__int64) boxLen );

        if ( m4a && insideIlst )
        {
            if ( !strcmp( tag, "data" ) && itemTag )
                ParseM4AData( hs, offset, boxLen, *m4a, itemTag );
            else if ( strcmp( tag, "mean" ) && strcmp( tag, "name" ) )
            {
                HeifStream hsChild( hs.Stream(), hs.Offset() + offset, boxLen - ( offset - boxOffset ) );
                EnumerateBoxes( hsChild, depth + 1, m4a, true, tag );
            }
        }
        else if ( m4a && !strcmp( tag, "ftyp" ) )
        {
            if ( boxLen - headerLength < 8 )
                prf( "incomplete M4A file type box\n" );
            else
            {
                char majorBrand[ 5 ] = {};
                for ( int i = 0; i < 4; i++ )
                    majorBrand[ i ] = (char) hs.GetBYTE( offset );
                for ( int i = 3; i >= 0 && ' ' == majorBrand[ i ]; i-- )
                    majorBrand[ i ] = 0;
                DWORD minorVersion = hs.GetDWORD( offset, false );
                prf( "M4A major brand: %s (minor version %u.%u.%u)\n", majorBrand,
                     ( minorVersion >> 16 ) & 0xff, ( minorVersion >> 8 ) & 0xff, minorVersion & 0xff );

                ULONGLONG compatibleLength = boxLen - headerLength - 8;
                if ( compatibleLength >= 4 && compatibleLength <= 1024 )
                {
                    prf( "M4A compatible brands:" );
                    while ( compatibleLength >= 4 )
                    {
                        char compatibleBrand[ 5 ] = {};
                        for ( int i = 0; i < 4; i++ )
                            compatibleBrand[ i ] = (char) hs.GetBYTE( offset );
                        for ( int i = 3; i >= 0 && ' ' == compatibleBrand[ i ]; i-- )
                            compatibleBrand[ i ] = 0;
                        if ( compatibleBrand[ 0 ] )
                            prf( " %s", compatibleBrand );
                        compatibleLength -= 4;
                    }
                    prf( "\n" );
                }
            }
        }
        else if ( m4a && !strcmp( tag, "mvhd" ) )
        {
            if ( boxLen - headerLength < 20 )
                prf( "incomplete M4A movie header\n" );
            else
            {
                BYTE version = hs.GetBYTE( offset );
                hs.GetBYTE( offset ); hs.GetBYTE( offset ); hs.GetBYTE( offset );
                if ( 0 == version && boxLen - ( offset - boxOffset ) >= 16 )
                {
                    hs.GetDWORD( offset, false );
                    hs.GetDWORD( offset, false );
                    m4a->movieTimeScale = hs.GetDWORD( offset, false );
                    m4a->movieDuration = hs.GetDWORD( offset, false );
                }
                else if ( 1 == version && boxLen - ( offset - boxOffset ) >= 28 )
                {
                    hs.GetVariable( offset, 8 );
                    hs.GetVariable( offset, 8 );
                    m4a->movieTimeScale = hs.GetDWORD( offset, false );
                    m4a->movieDuration = hs.GetVariable( offset, 8 );
                }
                else
                    prf( "unsupported M4A movie header version %u\n", version );
            }
        }
        else if ( m4a && !strcmp( tag, "mdat" ) )
        {
            m4a->mediaDataBytes += boxLen - headerLength;
        }
        else if ( m4a && m4a->audioTrack && !strcmp( tag, "stsd" ) )
        {
            ULONGLONG contentLength = boxLen - headerLength;
            if ( contentLength < 8 )
                prf( "incomplete M4A sample description box\n" );
            else
            {
                hs.GetDWORD( offset, false );
                DWORD entryCount = hs.GetDWORD( offset, false );
                if ( entryCount && contentLength >= 16 )
                {
                    ULONGLONG entryOffset = offset;
                    DWORD entryLength = hs.GetDWORD( offset, false );
                    char format[ 5 ] = {};
                    for ( int i = 0; i < 4; i++ )
                        format[ i ] = (char) hs.GetBYTE( offset );

                    if ( entryLength < 36 || entryLength > contentLength - 8 )
                        prf( "invalid M4A audio sample entry length %u\n", entryLength );
                    else
                    {
                        hs.GetWORD( offset, false );
                        hs.GetWORD( offset, false );
                        hs.GetWORD( offset, false );
                        hs.GetWORD( offset, false );
                        hs.GetWORD( offset, false );
                        hs.GetWORD( offset, false );
                        hs.GetDWORD( offset, false );
                        m4a->channels = hs.GetWORD( offset, false );
                        m4a->bitsPerSample = hs.GetWORD( offset, false );
                        hs.GetWORD( offset, false );
                        hs.GetWORD( offset, false );
                        m4a->sampleRate = hs.GetDWORD( offset, false ) >> 16;
                        m4a->audioFormat = format;

                        prf( "M4A audio format: %s\n", format );
                        prf( "M4A audio channels: %u\n", m4a->channels );
                        prf( "M4A audio bits per sample: %u\n", m4a->bitsPerSample );
                        prf( "M4A audio sample rate: %u Hz\n", m4a->sampleRate );
                    }
                    offset = entryOffset + entryLength;
                }
            }
        }
        else if ( m4a && !strcmp( tag, "ilst" ) )
        {
            HeifStream hsChild( hs.Stream(), hs.Offset() + offset, boxLen - ( offset - boxOffset ) );
            EnumerateBoxes( hsChild, depth + 1, m4a, true );
        }
        else if ( !strcmp( tag, "ftyp" ) )
        {
            char brand[ 5 ];
            for ( int i = 0; i < 4; i++ )
                brand[ i ] = hs.GetBYTE( offset );
            brand[ 4 ] = 0;

            DWORD version = hs.GetDWORD( offset, false );

            //printf( "%*s major brand: %s, minor version: %#x\n", indent, "", brand, version );
        }
        else if ( !strcmp( tag, "pitm" ) )
        {
            DWORD data = hs.GetDWORD( offset, false );
            BYTE version = (BYTE) ( data >> 24 );
            if ( version > 1 )
                hs.GetDWORD( offset, false );
            else if ( 0 == version )
                g_Heif_Primary_ItemID = hs.GetWORD( offset, false );
            else
                g_Heif_Primary_ItemID = hs.GetDWORD( offset, false );
        }
        else if ( !strcmp( tag, "meta" ) )
        {
            DWORD data = hs.GetDWORD( offset, true );

            //printf( "%*s data (version and flags) %#x\n", indent, "", data );

            HeifStream hsChild( hs.Stream(), hs.Offset() + offset, boxLen - ( offset - boxOffset ) );

            EnumerateBoxes( hsChild, depth + 1, m4a );
        }
        else if ( m4a && !strcmp( tag, "hdlr" ) )
        {
            if ( boxLen - headerLength < 12 )
                prf( "incomplete M4A handler box\n" );
            else
            {
                hs.GetDWORD( offset, false );
                hs.GetDWORD( offset, false );
                char handler[ 5 ] = {};
                for ( int i = 0; i < 4; i++ )
                    handler[ i ] = (char) hs.GetBYTE( offset );
                m4a->audioTrack = 0 == strcmp( handler, "soun" );
            }
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

                EnumerateBoxes( hsChild, depth + 1, m4a );
            }
        }
        else if ( !strcmp( tag, "ipma" ) )
        {
            DWORD data = hs.GetDWORD( offset, false );
            BYTE version = (BYTE) ( data >> 24 );
            DWORD flags = data & 0x00ffffff;
            DWORD entryCount = hs.GetDWORD( offset, false );
            bool wideAssociations = 0 != ( flags & 1 );

            for ( DWORD entry = 0; entry < entryCount && hs.Valid(); entry++ )
            {
                DWORD itemID = version ? hs.GetDWORD( offset, false ) : hs.GetWORD( offset, false );
                BYTE associationCount = hs.GetBYTE( offset );

                for ( BYTE association = 0; association < associationCount && hs.Valid(); association++ )
                {
                    WORD value = wideAssociations ? hs.GetWORD( offset, false ) : hs.GetBYTE( offset );
                    DWORD propertyID = wideAssociations ? ( value & 0x7fff ) : ( value & 0x7f );

                    if ( itemID == g_Heif_Primary_ItemID && propertyID )
                        g_Heif_Primary_Properties.push_back( propertyID );
                }
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
            WORD values4 = hs.GetWORD( offset, false );
            int offsetSize = ( values4 >> 12 ) & 0xf;
            int lengthSize = ( values4 >> 8 ) & 0xf;
            int baseOffsetSize = ( values4 >> 4 ) & 0xf;
            int indexSize = version >= 1 ? ( values4 & 0xf ) : 0;
            DWORD itemCount = version >= 2 ? hs.GetDWORD( offset, false ) : hs.GetWORD( offset, false );

            if ( version > 2 || offsetSize > 8 || lengthSize > 8 || baseOffsetSize > 8 || indexSize > 8 )
            {
                prf( "%*sunsupported iloc version or field size\n", indent, "" );
            }
            else
            {
                for ( DWORD i = 0; i < itemCount && hs.Valid(); i++ )
                {
                    ULONGLONG itemID = version >= 2 ? hs.GetDWORD( offset, false ) : hs.GetWORD( offset, false );
                    BYTE constructionMethod = 0;
                    if ( version >= 1 )
                    {
                        values4 = hs.GetWORD( offset, false );
                        constructionMethod = (BYTE) ( values4 & 0xf );
                    }

                    WORD dataReferenceIndex = hs.GetWORD( offset, false );
                    ULONGLONG baseOffset = hs.GetVariable( offset, baseOffsetSize );
                    WORD extentCount = hs.GetWORD( offset, false );
                    ULONGLONG extentOffset = 0;
                    ULONGLONG extentLength = 0;

                    for ( WORD e = 0; e < extentCount && hs.Valid(); e++ )
                    {
                        if ( indexSize )
                            hs.GetVariable( offset, indexSize );

                        ULONGLONG currentExtentOffset = hs.GetVariable( offset, offsetSize );
                        ULONGLONG currentExtentLength = hs.GetVariable( offset, lengthSize );

                        if ( 0 == e )
                        {
                            extentOffset = currentExtentOffset;
                            extentLength = currentExtentLength;
                        }
                    }

                    if ( itemID == g_Heif_Exif_ItemID )
                    {
                        if ( extentCount != 1 || dataReferenceIndex != 0 ||
                             ( constructionMethod != 0 && constructionMethod != 1 ) ||
                             extentLength < 4 ||
                             baseOffset > ULLONG_MAX - extentOffset )
                        {
                            prf( "%*sunsupported or invalid Exif item location\n", indent, "" );
                            continue;
                        }

                        ULONGLONG absoluteOffset = baseOffset + extentOffset;
                        if ( constructionMethod == 1 )
                        {
                            if ( g_Heif_Idat_Offset <= 0 ||
                                 absoluteOffset > ULLONG_MAX - (ULONGLONG) g_Heif_Idat_Offset )
                            {
                                prf( "%*sinvalid idat-relative Exif item location\n", indent, "" );
                                continue;
                            }
                            absoluteOffset += (ULONGLONG) g_Heif_Idat_Offset;
                        }

                        if ( absoluteOffset > (ULONGLONG) LLONG_MAX ||
                             absoluteOffset > (ULONGLONG) hs.Stream()->Length() ||
                             extentLength > (ULONGLONG) hs.Stream()->Length() - absoluteOffset )
                        {
                            prf( "%*sExif item extent is outside the file\n", indent, "" );
                            continue;
                        }

                        g_Heif_Exif_Offset = (__int64) absoluteOffset;
                        g_Heif_Exif_Length = (__int64) extentLength;
                    }
                }
            }
        }
        else if ( !strcmp( tag, "hvcC" ) )
        {
            if ( boxLen - headerLength >= 21 )
            {
                hs.GetBYTE( offset );
                hs.GetBYTE( offset );
                hs.GetDWORD( offset, false );

                for ( int i = 0; i < 6; i++ )
                    hs.GetBYTE( offset );

                hs.GetBYTE( offset );
                hs.GetWORD( offset, false );
                hs.GetBYTE( offset );
                hs.GetBYTE( offset );
                BYTE bitDepthLuma = ( hs.GetBYTE( offset ) & 0x7 ) + 8;
                BYTE bitDepthChroma = ( hs.GetBYTE( offset ) & 0x7 ) + 8;

                if ( isProperty )
                {
                    g_Heif_Properties[ propertyIndex ].lumaBitDepth = bitDepthLuma;
                    g_Heif_Properties[ propertyIndex ].chromaBitDepth = bitDepthChroma;
                }
            }
            else
                prf( "%*sincomplete hvcC property\n", indent, "" );
        }
        else if ( !strcmp( tag, "ispe" ) )
        {
            if ( boxLen - headerLength >= 12 )
            {
                hs.GetDWORD( offset, false );
                DWORD width = hs.GetDWORD( offset, false );
                DWORD height = hs.GetDWORD( offset, false );
                if ( isProperty )
                {
                    g_Heif_Properties[ propertyIndex ].width = width;
                    g_Heif_Properties[ propertyIndex ].height = height;
                }
            }
        }
        else if ( !strcmp( tag, "pixi" ) )
        {
            if ( boxLen - headerLength >= 5 )
            {
                hs.GetDWORD( offset, false );
                BYTE channelCount = hs.GetBYTE( offset );
                BYTE bitsPerChannel = 0;
                for ( BYTE channel = 0; channel < channelCount && hs.Valid(); channel++ )
                {
                    BYTE channelBits = hs.GetBYTE( offset );
                    if ( channelBits > bitsPerChannel )
                        bitsPerChannel = channelBits;
                }
                if ( isProperty )
                    g_Heif_Properties[ propertyIndex ].bitsPerChannel = bitsPerChannel;
            }
        }
        else if ( !strcmp( tag, "irot" ) )
        {
            BYTE value = hs.GetBYTE( offset );
            if ( isProperty )
            {
                g_Heif_Properties[ propertyIndex ].rotation = value & 0x3;
                g_Heif_Properties[ propertyIndex ].hasRotation = true;
            }
        }
        else if ( !strcmp( tag, "imir" ) )
        {
            BYTE value = hs.GetBYTE( offset );
            if ( isProperty )
            {
                g_Heif_Properties[ propertyIndex ].mirrorAxis = value & 0x1;
                g_Heif_Properties[ propertyIndex ].hasMirror = true;
            }
        }
        else if ( !strcmp( tag, "colr" ) )
        {
            if ( boxLen - headerLength >= 4 )
            {
                char colorType[ 5 ] = {};
                for ( int i = 0; i < 4; i++ )
                    colorType[ i ] = (char) hs.GetBYTE( offset );
                ULONGLONG profileLength = boxLen - ( offset - boxOffset );
                if ( isProperty && ( 0 == strcmp( colorType, "prof" ) ||
                                     0 == strcmp( colorType, "rICC" ) ) )
                {
                    if ( profileLength <= 16 * 1024 * 1024 && profileLength <= INT_MAX )
                    {
                        HeifProperty & property = g_Heif_Properties[ propertyIndex ];
                        property.iccProfile.resize( (size_t) profileLength );
                        hs.GetBytes( offset, property.iccProfile.data(), (int) profileLength );
                    }
                    else
                        prf( "%*sICC profile is too large to parse (%I64u bytes)\n", indent, "", profileLength );
                }
            }
        }
        else if ( !strcmp( tag, "iprp" ) )
        {
            HeifStream hsChild( hs.Stream(), hs.Offset() + offset, boxLen - ( offset - boxOffset ) );

            EnumerateBoxes( hsChild, depth + 1, m4a );
        }
        else if ( !strcmp( tag, "ipco" ) )
        {
            HeifStream hsChild( hs.Stream(), hs.Offset() + offset, boxLen - ( offset - boxOffset ) );
            bool wasReadingProperties = g_Heif_ReadingProperties;
            g_Heif_ReadingProperties = true;
            EnumerateBoxes( hsChild, depth + 1, m4a );
            g_Heif_ReadingProperties = wasReadingProperties;
        }
        else if ( !strcmp( tag, "idat" ) )
        {
            g_Heif_Idat_Offset = hs.Offset() + offset;
        }
        else if ( m4a && !strcmp( tag, "udta" ) )
        {
            HeifStream hsChild( hs.Stream(), hs.Offset() + offset, boxLen - ( offset - boxOffset ) );
            EnumerateBoxes( hsChild, depth + 1, m4a );
        }
        else if ( !strcmp( tag, "moov" ) ) // Canon CR3 format
        {
            HeifStream hsChild( hs.Stream(), hs.Offset() + offset, boxLen - ( offset - boxOffset ) );

            EnumerateBoxes( hsChild, depth + 1, m4a );
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

                EnumerateBoxes( hsChild, depth + 1, m4a );
            }

            if ( !strcmp( "eaf42b5e1c984b88b9fbb7dc406e4d16", acGUID ) )
            {
                // preview data -- a reduced-resolution jpg

                HeifStream hsChild( hs.Stream(), hs.Offset() + offset, boxLen - ( offset - boxOffset ) );

                EnumerateBoxes( hsChild, depth + 1, m4a );
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
            bool wasAudioTrack = m4a ? m4a->audioTrack : false;
            if ( m4a )
                m4a->audioTrack = false;
            EnumerateBoxes( hsChild, depth + 1, m4a );
            if ( m4a )
                m4a->audioTrack = wasAudioTrack;
        }
        else if ( !strcmp( tag, "mdia" ) ) // Canon .CR3 metadata
        {
            HeifStream hsChild( hs.Stream(), hs.Offset() + offset, boxLen - ( offset - boxOffset ) );
            EnumerateBoxes( hsChild, depth + 1, m4a );
        }
        else if ( !strcmp( tag, "minf" ) ) // Canon .CR3 metadata
        {
            HeifStream hsChild( hs.Stream(), hs.Offset() + offset, boxLen - ( offset - boxOffset ) );
            EnumerateBoxes( hsChild, depth + 1, m4a );
        }
        else if ( !strcmp( tag, "stbl" ) ) // Canon .CR3 metadata
        {
            HeifStream hsChild( hs.Stream(), hs.Offset() + offset, boxLen - ( offset - boxOffset ) );
            EnumerateBoxes( hsChild, depth + 1, m4a );
        }
        else if ( !strcmp( tag, "stsz" ) ) // Canon .CR3 metadata
        {
            // There is no documentation, but it appears that the 1st of 4 instances of
            // the stsz tag has the full-resolution embedded JPG length at i==3 in this
            // loop. I mean, it worked for one file.

            if ( boxLen - headerLength >= 16 )
            {
                for ( int i = 0; i < 4; i++ )
                {
                    DWORD len = hs.GetDWORD( offset, false );
                    //printf( "stsz tag; %d DWORD: %d\n", i, len );

                    if ( ( 3 == i ) && ( 0 == g_Canon_CR3_Embedded_JPG_Length ) )
                        g_Canon_CR3_Embedded_JPG_Length = len;
                }
            }
        }

        bool validBoxData = hs.Valid();
        hs.RestoreLength( enclosingLength );
        if ( !validBoxData )
        {
            prf( "%*struncated or invalid data in %s box at offset %I64d\n",
                 indent, "", tag, boxOffset );
            break;
        }

        box++;
        offset = boxOffset + boxLen;
    } while ( true );
} //EnumerateBoxes

void EnumerateHeif( CStream * pStream )
{
    g_Heif_Exif_ItemID = 0xffffffff;
    g_Heif_Exif_Offset = 0;
    g_Heif_Exif_Length = 0;
    g_Heif_Primary_ItemID = 0xffffffff;
    g_Heif_Properties.clear();
    g_Heif_Primary_Properties.clear();
    g_Heif_ReadingProperties = false;
    g_Heif_Idat_Offset = 0;

    __int64 length = pStream->Length();
    HeifStream hs( pStream, 0, length );

    EnumerateBoxes( hs, 0 );
    PrintHeifPrimaryProperties();

    if ( g_FullInformation )
        prf( "Heif EXIF info: id %d, offset %I64d, length %I64d\n", g_Heif_Exif_ItemID, g_Heif_Exif_Offset, g_Heif_Exif_Length );
} //EnumerateHeif

void EnumerateM4A( CStream * pStream )
{
    prf( "mimetype:                             audio/mp4\n" );

    M4AParseState state;
    HeifStream hs( pStream, 0, pStream->Length() );
    EnumerateBoxes( hs, 0, &state );

    if ( state.movieTimeScale && state.movieDuration )
    {
        ULONGLONG seconds = state.movieDuration / state.movieTimeScale;
        ULONGLONG remainder = state.movieDuration % state.movieTimeScale;
        if ( remainder >= state.movieTimeScale / 2 + state.movieTimeScale % 2 )
            seconds++;
        prf( "M4A duration: %I64u:%02I64u:%02I64u\n",
             seconds / 3600, ( seconds / 60 ) % 60, seconds % 60 );
        if ( state.mediaDataBytes > 0 )
        {
            double averageBitrate = (double) state.mediaDataBytes * 8.0 /
                                    ( (double) state.movieDuration / state.movieTimeScale ) / 1000.0;
            prf( "M4A average audio bitrate: %.1lf kbps\n", averageBitrate );
        }
    }
    else
        prf( "M4A movie duration is unavailable\n" );

    if ( state.mediaDataBytes )
        prf( "M4A media data size: %I64u bytes\n", state.mediaDataBytes );
} //EnumerateM4A

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
            prf( "numtags is > MaxIFDHeaders; it's likely not in EXIF format, so the data may be noise. skipping (value is %#xs )\n", NumTags );
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
                prf( "MeteringMode:                         %s (%d)\n", ExifMeteringMode( head.offset ), head.offset );
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
            else if ( 50727 == head.id )
                PrintDngRationalArray( "AnalogBalance:", head, headerBase, littleEndian );
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
            else if ( 50706 == head.id || 50707 == head.id )
            {
                const char * name = ( 50706 == head.id ) ? "DNGVersion:" : "DNGBackwardVersion:";
                if ( 1 == head.type && 4 == head.count &&
                     IsFileRangeValid( IFDOffset - 4 + headerBase, 4 ) )
                {
                    BYTE version[ 4 ] = {};
                    GetBytes( IFDOffset - 4 + headerBase, version, sizeof version );
                    prf( "%-37s %u.%u.%u.%u\n", name,
                         version[ 0 ], version[ 1 ], version[ 2 ], version[ 3 ] );
                }
                else
                    prf( "%-37s (invalid version data; type %u, count %u)\n",
                         name, head.type, head.count );
            }
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
            else if ( 50721 == head.id )
                PrintDngRationalArray( "ColorMatrix1:", head, headerBase, littleEndian );
            else if ( 50722 == head.id )
                PrintDngRationalArray( "ColorMatrix2:", head, headerBase, littleEndian );
            else if ( 50728 == head.id )
                PrintDngRationalArray( "AsShotNeutral:", head, headerBase, littleEndian );
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
            else if ( 50740 == head.id )
            {
                __int64 privateDataOffset = ( head.count <= 4 )
                    ? IFDOffset - 4 + headerBase
                    : (__int64) head.offset + headerBase;

                if ( 1 == head.type && 4 == head.count &&
                     ( 0 == strcmp( g_acMake, "SONY" ) || 0 == strcmp( g_acMake, "Sony" ) ) &&
                     IsFileRangeValid( (__int64) head.offset + headerBase, 2 ) )
                {
                    prf( "DNGPrivateData:                       Sony MakerNote pointer %#x\n", head.offset );
                    EnumerateMakernotes( depth + 1, head.offset, headerBase, littleEndian );
                }
                else if ( 1 == head.type && IsFileRangeValid( privateDataOffset, head.count ) )
                    prf( "DNGPrivateData:                       %u bytes at offset %#llx for make %s (not decoded)\n",
                         head.count, privateDataOffset, g_acMake );
                else
                    prf( "DNGPrivateData:                       (unsupported or invalid data; type %u, count %u, offset %#x)\n",
                         head.type, head.count, head.offset );
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
        return "grayscale and alpha";

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

bool PNGReadBytes( __int64 offset, void * data, ULONG length )
{
    return offset >= 0 && offset <= g_pStream->Length() &&
           length <= (QWORD) ( g_pStream->Length() - offset ) &&
           g_pStream->Seek( offset ) && g_pStream->Read( data, length ) == length;
}

bool PNGCheckCRC( __int64 offset, DWORD length, DWORD expectedCRC )
{
    BYTE buffer[ 8192 ];
    ULONGLONG bytesRemaining = (ULONGLONG) length + 4; // chunk type and chunk data
    DWORD crc = 0xffffffff;

    while ( bytesRemaining )
    {
        ULONG bytesToRead = (ULONG) min( bytesRemaining, (ULONGLONG) sizeof buffer );
        if ( !PNGReadBytes( offset, buffer, bytesToRead ) )
            return false;

        for ( ULONG i = 0; i < bytesToRead; i++ )
        {
            crc ^= buffer[ i ];
            for ( int bit = 0; bit < 8; bit++ )
                crc = ( crc >> 1 ) ^ ( ( crc & 1 ) ? 0xedb88320 : 0 );
        }

        offset += bytesToRead;
        bytesRemaining -= bytesToRead;
    }

    return ( ~crc ) == expectedCRC;
}

string PNGPrintableText( const BYTE * bytes, size_t length )
{
    string text;
    size_t limit = min( length, (size_t) 512 );
    text.reserve( limit );
    for ( size_t i = 0; i < limit; i++ )
    {
        BYTE ch = bytes[ i ];
        text += ( ch < 0x20 || 0x7f == ch ) ? ' ' : (char) ch;
    }
    if ( length > limit )
        text += "...";
    return text;
}

bool PNGReadTextChunk( __int64 offset, DWORD length, vector<BYTE> & data )
{
    const DWORD maxTextChunkToRead = 1024 * 1024;
    if ( length > maxTextChunkToRead )
        return false;
    data.resize( length );
    return 0 == length || PNGReadBytes( offset, data.data(), length );
}

// PNG files contain big-endian length/type/data/CRC chunks following their 8-byte signature.
__int64 ParsePNG()
{
    static const BYTE signature[ 8 ] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
    BYTE actualSignature[ sizeof signature ];
    if ( !PNGReadBytes( 0, actualSignature, sizeof actualSignature ) ||
         0 != memcmp( signature, actualSignature, sizeof signature ) )
    {
        prf( "Invalid PNG signature\n" );
        return 0;
    }

    __int64 offset = sizeof signature;
    __int64 fileLength = g_pStream->Length();
    __int64 exifOffset = 0;
    ULONGLONG idatSize = 0;
    DWORD idatSegments = 0;
    DWORD paletteEntries = 0;
    DWORD width = 0, height = 0;
    BYTE bitDepth = 0, colorType = 0;
    bool sawIHDR = false, sawPLTE = false, sawIDAT = false;
    bool idatEnded = false, sawIEND = false;

    prf( "mimetype:                             image/png\n" );

    while ( offset < fileLength )
    {
        if ( fileLength - offset < 12 )
        {
            prf( "truncated PNG chunk header at offset %I64d\n", offset );
            return 0;
        }

        DWORD length = GetDWORD( offset, false );
        char type[ 5 ] = {};
        if ( !PNGReadBytes( offset + 4, type, 4 ) )
        {
            prf( "can't read PNG chunk type at offset %I64d\n", offset );
            return 0;
        }

        for ( int i = 0; i < 4; i++ )
        {
            if ( ! ( ( type[ i ] >= 'A' && type[ i ] <= 'Z' ) ||
                     ( type[ i ] >= 'a' && type[ i ] <= 'z' ) ) )
            {
                prf( "invalid PNG chunk type at offset %I64d\n", offset );
                return 0;
            }
        }
        if ( type[ 2 ] >= 'a' && type[ 2 ] <= 'z' )
        {
            prf( "invalid lowercase reserved bit in PNG chunk type %.4s\n", type );
            return 0;
        }

        if ( (ULONGLONG) length > (ULONGLONG) ( fileLength - offset - 12 ) )
        {
            prf( "PNG chunk %.4s length %u exceeds remaining file data\n", type, length );
            return 0;
        }

        __int64 dataOffset = offset + 8;
        __int64 crcOffset = dataOffset + length;
        DWORD expectedCRC = GetDWORD( crcOffset, false );
        if ( !PNGCheckCRC( offset + 4, length, expectedCRC ) )
        {
            prf( "PNG chunk %.4s has an invalid CRC\n", type );
            return 0;
        }

        if ( g_FullInformation )
            prf( "png chunk type: %.4s, len %u\n", type, length );

        if ( 0 == memcmp( type, "IHDR", 4 ) )
        {
            if ( sawIHDR || offset != 8 || length != 13 )
            {
                prf( "invalid PNG IHDR chunk placement or length %u\n", length );
                return 0;
            }

            width = GetDWORD( dataOffset, false );
            height = GetDWORD( dataOffset + 4, false );
            bitDepth = GetBYTE( dataOffset + 8 );
            colorType = GetBYTE( dataOffset + 9 );
            BYTE compressionMethod = GetBYTE( dataOffset + 10 );
            BYTE filterMethod = GetBYTE( dataOffset + 11 );
            BYTE interlaceMethod = GetBYTE( dataOffset + 12 );

            bool validDepth = false;
            switch ( colorType )
            {
                case 0: validDepth = bitDepth == 1 || bitDepth == 2 || bitDepth == 4 || bitDepth == 8 || bitDepth == 16; break;
                case 2: validDepth = bitDepth == 8 || bitDepth == 16; break;
                case 3: validDepth = bitDepth == 1 || bitDepth == 2 || bitDepth == 4 || bitDepth == 8; break;
                case 4:
                case 6: validDepth = bitDepth == 8 || bitDepth == 16; break;
            }

            if ( 0 == width || 0 == height || width > 0x7fffffff || height > 0x7fffffff || !validDepth ||
                 compressionMethod != 0 || filterMethod != 0 || interlaceMethod > 1 )
            {
                prf( "invalid PNG IHDR values (dimensions %u x %u, bit depth %u, color type %u)\n",
                     width, height, bitDepth, colorType );
                return 0;
            }

            prf( "image width:           %16u\n", width );
            prf( "image height:          %16u\n", height );
            prf( "bit depth:             %16u\n", bitDepth );
            prf( "color type:            %16u (%s)\n", colorType, GetPNGColorType( colorType ) );
            prf( "compression method:    %16u (%s)\n", compressionMethod, 0 == compressionMethod ? "deflate" : "unknown" );
            prf( "filter method:         %16u (%s)\n", filterMethod, GetPNGFilterMethod( filterMethod ) );
            prf( "interlace method:      %16u (%s)\n", interlaceMethod, GetPNGInterlaceMethod( interlaceMethod ) );
            sawIHDR = true;
        }
        else if ( !sawIHDR )
        {
            prf( "PNG IHDR must be the first chunk\n" );
            return 0;
        }
        else if ( 0 == memcmp( type, "IEND", 4 ) )
        {
            if ( length != 0 || !sawIDAT )
            {
                prf( "invalid PNG IEND chunk\n" );
                return 0;
            }
            sawIEND = true;
        }
        else if ( 0 == memcmp( type, "IDAT", 4 ) )
        {
            if ( idatEnded || ( colorType == 3 && !sawPLTE ) )
            {
                prf( "invalid PNG IDAT chunk order\n" );
                return 0;
            }
            sawIDAT = true;
            idatSegments++;
            idatSize += length;
            if ( g_FullInformation )
                prf( "image data length:     %16u\n", length );
        }
        else
        {
            if ( sawIDAT )
                idatEnded = true;

            if ( 0 == memcmp( type, "PLTE", 4 ) )
            {
                if ( sawPLTE || sawIDAT || 0 == length || length > 768 || length % 3 != 0 ||
                     colorType == 0 || colorType == 4 )
                {
                    prf( "invalid PNG palette chunk\n" );
                    return 0;
                }
                paletteEntries = length / 3;
                if ( colorType == 3 && paletteEntries > ( 1u << bitDepth ) )
                {
                    prf( "PNG palette has %u entries, exceeding bit depth %u capacity\n",
                         paletteEntries, bitDepth );
                    return 0;
                }
                sawPLTE = true;
                prf( "palette has %u entries\n", paletteEntries );
                for ( DWORD entry = 0; entry < paletteEntries; entry++ )
                {
                    BYTE rgb[ 3 ];
                    if ( !PNGReadBytes( dataOffset + entry * 3, rgb, sizeof rgb ) )
                        return 0;
                    prf( "    %3u: r %3u, g %3u, b %3u\n", entry, rgb[ 0 ], rgb[ 1 ], rgb[ 2 ] );
                }
            }
            else if ( 0 == memcmp( type, "sRGB", 4 ) )
            {
                if ( length != 1 )
                    prf( "invalid PNG sRGB chunk length %u\n", length );
                else
                {
                    BYTE intent = GetBYTE( dataOffset );
                    prf( "sRGB rendering intent: %16u (%s)\n", intent, GetPNGRenderingIntent( intent ) );
                }
            }
            else if ( 0 == memcmp( type, "gAMA", 4 ) )
            {
                if ( length != 4 )
                    prf( "invalid PNG gAMA chunk length %u\n", length );
                else
                {
                    DWORD gamma = GetDWORD( dataOffset, false );
                    prf( "gamma:               %18.5f (stored %u / 100000)\n", gamma / 100000.0, gamma );
                }
            }
            else if ( 0 == memcmp( type, "pHYs", 4 ) )
            {
                if ( length != 9 )
                    prf( "invalid PNG pHYs chunk length %u\n", length );
                else
                {
                    DWORD pixelsPerUnitX = GetDWORD( dataOffset, false );
                    DWORD pixelsPerUnitY = GetDWORD( dataOffset + 4, false );
                    BYTE unit = GetBYTE( dataOffset + 8 );
                    prf( "pixels per unit X:     %16u\n", pixelsPerUnitX );
                    prf( "pixels per unit Y:     %16u\n", pixelsPerUnitY );
                    prf( "unit specifier:        %16u (%s)\n", unit, GetPNGUnitSpecifier( unit ) );
                    if ( unit == 1 )
                        prf( "resolution:            %.2f x %.2f dpi\n",
                             pixelsPerUnitX * 0.0254, pixelsPerUnitY * 0.0254 );
                }
            }
            else if ( 0 == memcmp( type, "cHRM", 4 ) )
            {
                if ( length != 32 )
                    prf( "invalid PNG cHRM chunk length %u\n", length );
                else
                {
                    static const char * labels[] = { "white point", "red", "green", "blue" };
                    prf( "chromaticity (x, y):\n" );
                    for ( int i = 0; i < 4; i++ )
                        prf( "  %-12s %.5f, %.5f\n", labels[ i ],
                             GetDWORD( dataOffset + i * 8, false ) / 100000.0,
                             GetDWORD( dataOffset + i * 8 + 4, false ) / 100000.0 );
                }
            }
            else if ( 0 == memcmp( type, "tEXt", 4 ) ||
                      0 == memcmp( type, "zTXt", 4 ) ||
                      0 == memcmp( type, "iTXt", 4 ) )
            {
                vector<BYTE> data;
                if ( !PNGReadTextChunk( dataOffset, length, data ) )
                {
                    prf( "PNG %.4s metadata is too large to display (%u bytes)\n", type, length );
                }
                else
                {
                    vector<BYTE>::const_iterator keyEnd = find( data.begin(), data.end(), 0 );
                    size_t keyLength = (size_t) ( keyEnd - data.begin() );
                    if ( keyEnd == data.end() || 0 == keyLength || keyLength > 79 )
                    {
                        prf( "malformed PNG %.4s keyword\n", type );
                    }
                    else
                    {
                        string key = PNGPrintableText( data.data(), keyLength );
                        if ( 0 == memcmp( type, "tEXt", 4 ) )
                        {
                            string value = PNGPrintableText( data.data() + keyLength + 1,
                                                             data.size() - keyLength - 1 );
                            prf( "text key:                             %s, value: %s\n", key.c_str(), value.c_str() );
                        }
                        else if ( 0 == memcmp( type, "zTXt", 4 ) )
                        {
                            size_t methodOffset = keyLength + 1;
                            if ( methodOffset >= data.size() )
                                prf( "malformed PNG zTXt chunk for key %s\n", key.c_str() );
                            else
                                prf( "compressed text key:                  %s, compression method %u, compressed value %u bytes\n",
                                     key.c_str(), data[ methodOffset ],
                                     (DWORD) ( data.size() - methodOffset - 1 ) );
                        }
                        else
                        {
                            size_t cursor = keyLength + 1;
                            if ( data.size() - cursor < 2 )
                                prf( "malformed PNG iTXt chunk for key %s\n", key.c_str() );
                            else
                            {
                                BYTE compressionFlag = data[ cursor++ ];
                                BYTE compressionMethod = data[ cursor++ ];
                                vector<BYTE>::const_iterator languageEnd = find( data.begin() + cursor, data.end(), 0 );
                                if ( languageEnd == data.end() )
                                    prf( "malformed PNG iTXt language tag for key %s\n", key.c_str() );
                                else
                                {
                                    string language = PNGPrintableText( data.data() + cursor,
                                        (size_t) ( languageEnd - ( data.begin() + cursor ) ) );
                                    cursor = (size_t) ( languageEnd - data.begin() ) + 1;
                                    vector<BYTE>::const_iterator translatedEnd = find( data.begin() + cursor, data.end(), 0 );
                                    if ( translatedEnd == data.end() )
                                        prf( "malformed PNG iTXt translated keyword for key %s\n", key.c_str() );
                                    else
                                    {
                                        string translated = PNGPrintableText( data.data() + cursor,
                                            (size_t) ( translatedEnd - ( data.begin() + cursor ) ) );
                                        cursor = (size_t) ( translatedEnd - data.begin() ) + 1;
                                        if ( compressionFlag > 1 || compressionMethod != 0 )
                                            prf( "invalid PNG iTXt compression fields for key %s\n", key.c_str() );
                                        else if ( compressionFlag )
                                            prf( "compressed international text:       %s, language %s, %u compressed bytes\n",
                                                 key.c_str(), language.c_str(), (DWORD) ( data.size() - cursor ) );
                                        else
                                        {
                                            string value = PNGPrintableText( data.data() + cursor, data.size() - cursor );
                                            prf( "international text:                   %s, language %s, translated key %s, value: %s\n",
                                                 key.c_str(), language.c_str(), translated.c_str(), value.c_str() );
                                            if ( 0 == key.compare( "XML:com.adobe.xmp" ) )
                                                PrintXMPData( (const char *) data.data() + cursor, data.size() - cursor );
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            else if ( 0 == memcmp( type, "iCCP", 4 ) )
            {
                BYTE profileName[ 80 ];
                DWORD nameLength = 0;
                while ( nameLength < min( length, (DWORD) sizeof profileName ) &&
                        PNGReadBytes( dataOffset + nameLength, &profileName[ nameLength ], 1 ) &&
                        profileName[ nameLength ] )
                    nameLength++;

                if ( nameLength == 0 || nameLength >= sizeof profileName ||
                     nameLength + 2 > length )
                    prf( "malformed PNG iCCP chunk\n" );
                else
                {
                    BYTE compressionMethod = GetBYTE( dataOffset + nameLength + 1 );
                    prf( "ICC profile name:                      %s\n",
                         PNGPrintableText( profileName, nameLength ).c_str() );
                    prf( "ICC profile compression:               method %u, %u compressed bytes\n",
                         compressionMethod, length - nameLength - 2 );
                    if ( compressionMethod != 0 )
                        prf( "unsupported PNG ICC profile compression method\n" );
                }
            }
            else if ( 0 == memcmp( type, "tIME", 4 ) )
            {
                if ( length != 7 )
                    prf( "invalid PNG tIME chunk length %u\n", length );
                else
                    prf( "last modification:                    %04u-%02u-%02u %02u:%02u:%02u\n",
                         GetWORD( dataOffset, false ), GetBYTE( dataOffset + 2 ),
                         GetBYTE( dataOffset + 3 ), GetBYTE( dataOffset + 4 ),
                         GetBYTE( dataOffset + 5 ), GetBYTE( dataOffset + 6 ) );
            }
            else if ( 0 == memcmp( type, "eXIf", 4 ) )
            {
                if ( exifOffset )
                    prf( "multiple PNG eXIf chunks; using the first\n" );
                else if ( length < 8 )
                    prf( "PNG eXIf chunk is too short to contain a TIFF header\n" );
                else
                {
                    DWORD tiffHeader = GetDWORD( dataOffset, true );
                    if ( tiffHeader != 0x002a4949 && tiffHeader != 0x2a004d4d )
                        prf( "invalid TIFF header in PNG eXIf chunk\n" );
                    else
                        exifOffset = dataOffset;
                }
            }
            else if ( 0 == memcmp( type, "tRNS", 4 ) )
            {
                if ( colorType == 3 && sawPLTE && length <= paletteEntries )
                {
                    prf( "palette transparency: %u entries\n", length );
                    for ( DWORD i = 0; i < length; i++ )
                        prf( "    %3u: alpha %3u\n", i, GetBYTE( dataOffset + i ) );
                }
                else if ( ( colorType == 0 && length == 2 ) || ( colorType == 2 && length == 6 ) )
                {
                    prf( "transparent sample value(s):" );
                    for ( DWORD i = 0; i < length; i += 2 )
                        prf( " %u", GetWORD( dataOffset + i, false ) );
                    prf( "\n" );
                }
                else
                    prf( "invalid PNG tRNS chunk length %u for color type %u\n", length, colorType );
            }
            else if ( 0 == memcmp( type, "sBIT", 4 ) )
            {
                static const char * names[][4] = {
                    { "gray", NULL, NULL, NULL }, { "red", "green", "blue", NULL },
                    { "red", "green", "blue", NULL }, { "red", "green", "blue", NULL },
                    { "gray", "alpha", NULL, NULL }, { "red", "green", "blue", "alpha" }
                };
                static const DWORD expectedLengths[] = { 1, 0, 3, 3, 2, 4, 4 };
                DWORD expectedLength = expectedLengths[ colorType ];
                if ( length != expectedLength )
                    prf( "invalid PNG sBIT chunk length %u for color type %u\n", length, colorType );
                else
                {
                    prf( "significant bits:\n" );
                    for ( DWORD i = 0; i < length; i++ )
                        prf( "  %-8s %u\n", names[ colorType ][ i ], GetBYTE( dataOffset + i ) );
                }
            }
            else if ( 0 == memcmp( type, "bKGD", 4 ) )
            {
                if ( colorType == 3 && length == 1 )
                    prf( "background palette index:              %u\n", GetBYTE( dataOffset ) );
                else if ( ( colorType == 0 || colorType == 4 ) && length == 2 )
                    prf( "background gray sample:                 %u\n", GetWORD( dataOffset, false ) );
                else if ( ( colorType == 2 || colorType == 6 ) && length == 6 )
                    prf( "background RGB sample:                  %u, %u, %u\n",
                         GetWORD( dataOffset, false ), GetWORD( dataOffset + 2, false ),
                         GetWORD( dataOffset + 4, false ) );
                else
                    prf( "invalid PNG bKGD chunk length %u for color type %u\n", length, colorType );
            }
            else if ( 0 == memcmp( type, "oFFs", 4 ) )
            {
                if ( length != 9 )
                    prf( "invalid PNG oFFs chunk length %u\n", length );
                else
                    prf( "image offset:                         %d, %d (unit %u)\n",
                         (LONG) GetDWORD( dataOffset, false ), (LONG) GetDWORD( dataOffset + 4, false ),
                         GetBYTE( dataOffset + 8 ) );
            }
            else if ( ( type[ 0 ] & 0x20 ) == 0 )
            {
                prf( "unrecognized critical PNG chunk %.4s, length %u\n", type, length );
                return 0;
            }
            else
            {
                prf( "unrecognized ancillary PNG chunk %.4s, length %u\n", type, length );
                if ( g_FullInformation && length )
                    DumpBinaryData( dataOffset, 0, length, 8, (DWORD) dataOffset );
            }
        }

        if ( sawIDAT && 0 != memcmp( type, "IDAT", 4 ) && 0 != memcmp( type, "IEND", 4 ) )
            idatEnded = true;

        offset = crcOffset + 4;
        if ( sawIEND )
        {
            if ( offset < fileLength )
                prf( "trailing data after PNG IEND: %I64d bytes\n", fileLength - offset );
            break;
        }
    }

    if ( !sawIHDR || !sawIDAT || !sawIEND )
    {
        prf( "incomplete PNG: missing %s%s%s\n",
             sawIHDR ? "" : "IHDR ",
             sawIDAT ? "" : "IDAT ",
             sawIEND ? "" : "IEND" );
        return 0;
    }

    prf( "IDAT segments:         %16u\n", idatSegments );
    prf( "IDAT data bytes:       %16I64u\n", idatSize );
    return exifOffset;
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

    if ( 6 == c )
        return "alpha bitfields";

    if ( BI_JPEG == c )
        return "JPEG";

    if ( BI_PNG == c )
        return "PNG";

    if ( 11 == c )
        return "CMYK";

    if ( 12 == c )
        return "CMYK RLE8";

    if ( 13 == c )
        return "CMYK RLE4";

    return "unknown";
} //BmpCompression

const char * BmpBitDepth( WORD bits )
{
    switch ( bits )
    {
        case 1: return "monochrome";
        case 4: return "16-color indexed";
        case 8: return "256-color indexed";
        case 16: return "high color";
        case 24: return "true color";
        case 32: return "true color";
    }

    return "unknown";
} //BmpBitDepth

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

    if ( !IsFileRangeValid( 0, 14 ) )
    {
        prf( "BMP file header is truncated\n" );
        return;
    }

    WORD fileType = GetWORD( 0, true );
    DWORD declaredFileSize = GetDWORD( 2, true );
    WORD reserved1 = GetWORD( 6, true );
    WORD reserved2 = GetWORD( 8, true );
    DWORD pixelOffset = GetDWORD( 10, true );
    __int64 fileLength = GetStreamLength();

    prf( "type:                         %#x %#x == %c%c\n", fileType & 0xff, fileType >> 8, fileType & 0xff, fileType >> 8 );
    prf( "declared file size:            %18u\n", declaredFileSize );
    prf( "container length:              %18lld\n", fileLength );
    if ( declaredFileSize > fileLength )
        prf( "  (warning: declared bitmap size exceeds available data by %lld bytes)\n", (__int64) declaredFileSize - fileLength );
    else if ( declaredFileSize && declaredFileSize < fileLength )
        prf( "  trailing bytes after declared bitmap: %u\n", (DWORD) ( fileLength - declaredFileSize ) );

    prf( "reserved 1:                    %18u\n", reserved1 );
    prf( "reserved 2:                    %18u\n", reserved2 );
    prf( "pixel data offset:             %18u\n", pixelOffset );
    if ( declaredFileSize && declaredFileSize < pixelOffset )
        prf( "  (warning: declared bitmap size ends before the pixel data)\n" );

    if ( !IsFileRangeValid( 14, sizeof( DWORD ) ) )
    {
        prf( "DIB header size is unavailable; file is truncated\n" );
        return;
    }

    DWORD headerSize = GetDWORD( 14, true );
    prf( "header size:                   %18u\n", headerSize );

    if ( 12 == headerSize )
    {
        if ( !IsFileRangeValid( 14, headerSize ) )
        {
            prf( "OS/2 BITMAPCOREHEADER is truncated\n" );
            return;
        }

        DWORD width = GetWORD( 18, true );
        DWORD height = GetWORD( 20, true );
        WORD planes = GetWORD( 22, true );
        WORD bitCount = GetWORD( 24, true );
        DWORD paletteEntries = bitCount <= 8 ? ( 1u << bitCount ) : 0;
        __int64 paletteOffset = 14 + headerSize;
        __int64 paletteBytes = pixelOffset >= paletteOffset ? pixelOffset - paletteOffset : 0;

        prf( "header type:                    OS/2 BITMAPCOREHEADER\n" );
        prf( "width:                          %18u\n", width );
        prf( "height:                         %18u (bottom-up)\n", height );
        prf( "planes:                         %18u\n", planes );
        prf( "bit count:                      %18u == %s\n", bitCount, BmpBitDepth( bitCount ) );
        prf( "compression:                    uncompressed\n" );
        if ( paletteEntries )
        {
            unsigned __int64 availableEntryCount = (unsigned __int64) paletteBytes / 3;
            DWORD availableEntries = availableEntryCount < paletteEntries ? (DWORD) availableEntryCount : paletteEntries;
            prf( "palette entries:                %u of %u expected\n", availableEntries, paletteEntries );
            if ( availableEntries < paletteEntries )
                prf( "  (warning: color table overlaps pixel data or is truncated)\n" );
        }
        if ( pixelOffset < paletteOffset || pixelOffset > fileLength )
            prf( "  (warning: pixel data offset is outside the valid BMP data range)\n" );
        return;
    }

    if ( headerSize < 40 || 64 == headerSize ||
         !IsFileRangeValid( 14, headerSize ) )
    {
        prf( "unsupported or truncated DIB header (%u bytes)\n", headerSize );
        return;
    }

    LONG width = (LONG) GetDWORD( 18, true );
    LONG signedHeight = (LONG) GetDWORD( 22, true );
    WORD planes = GetWORD( 26, true );
    WORD bitCount = GetWORD( 28, true );
    DWORD compression = GetDWORD( 30, true );
    DWORD imageSize = GetDWORD( 34, true );
    LONG xPixelsPerMeter = (LONG) GetDWORD( 38, true );
    LONG yPixelsPerMeter = (LONG) GetDWORD( 42, true );
    DWORD colorsUsed = GetDWORD( 46, true );
    DWORD colorsImportant = GetDWORD( 50, true );
    const char * headerType = 40 == headerSize ? "Windows BITMAPINFOHEADER" :
                              52 == headerSize ? "Windows V2" :
                              56 == headerSize ? "Windows V3" :
                              108 == headerSize ? "Windows V4" :
                              124 == headerSize ? "Windows V5" : "Windows DIB";
    prf( "header type:                    %s\n", headerType );
    prf( "width:                          %18d\n", width );
    if ( 0 == signedHeight )
        prf( "height:                         0 (invalid)\n" );
    else if ( signedHeight < 0 )
        prf( "height:                         %18lld (top-down)\n", -(long long) signedHeight );
    else
        prf( "height:                         %18d (bottom-up)\n", signedHeight );
    prf( "planes:                         %18u\n", planes );
    prf( "bit count:                      %18u == %s\n", bitCount, BmpBitDepth( bitCount ) );
    prf( "compression:                    %18u == %s\n", compression, BmpCompression( compression ) );
    prf( "size of image:                  %18u\n", imageSize );
    prf( "pixels per meter X:             %18d\n", xPixelsPerMeter );
    prf( "pixels per meter Y:             %18d\n", yPixelsPerMeter );
    prf( "color indices:                  %18u\n", colorsUsed );
    prf( "colors required:                %18u\n", colorsImportant );

    DWORD redMask = 0, greenMask = 0, blueMask = 0, alphaMask = 0;
    __int64 maskOffset = 14 + 40;
    if ( headerSize >= 52 )
    {
        redMask = GetDWORD( 54, true );
        greenMask = GetDWORD( 58, true );
        blueMask = GetDWORD( 62, true );
        if ( headerSize >= 56 )
            alphaMask = GetDWORD( 66, true );
    }
    else if ( 40 == headerSize && ( BI_BITFIELDS == compression || 6 == compression ) )
    {
        DWORD maskCount = 3 == compression ? 3 : 4;
        if ( IsFileRangeValid( maskOffset, (QWORD) maskCount * sizeof( DWORD ) ) &&
             14 + headerSize + maskCount * sizeof( DWORD ) <= pixelOffset )
        {
            redMask = GetDWORD( maskOffset, true );
            greenMask = GetDWORD( maskOffset + 4, true );
            blueMask = GetDWORD( maskOffset + 8, true );
            if ( 4 == maskCount )
                alphaMask = GetDWORD( maskOffset + 12, true );
            maskOffset += maskCount * sizeof( DWORD );
        }
        else
            prf( "  (warning: color masks are truncated or overlap pixel data)\n" );
    }

    if ( redMask || greenMask || blueMask )
        prf( "RGB masks:                      %#010x %#010x %#010x\n", redMask, greenMask, blueMask );
    if ( alphaMask )
        prf( "alpha mask:                     %#010x\n", alphaMask );

    DWORD colorSpace = 0;
    if ( headerSize >= 108 )
    {
        colorSpace = GetDWORD( 70, true );
        prf( "color space:                    %#010x == %s\n", colorSpace, BmpColorSpace( colorSpace ) );
        if ( LCS_CALIBRATED_RGB == colorSpace )
        {
            prf( "gamma red:                      %#010x\n", GetDWORD( 110, true ) );
            prf( "gamma green:                    %#010x\n", GetDWORD( 114, true ) );
            prf( "gamma blue:                     %#010x\n", GetDWORD( 118, true ) );
        }
    }

    if ( headerSize >= 124 )
    {
        DWORD intent = GetDWORD( 122, true );
        DWORD profileData = GetDWORD( 126, true );
        DWORD profileSize = GetDWORD( 130, true );
        __int64 profileOffset = 14 + (__int64) profileData;
        prf( "intent:                         %18u == %s\n", intent, BmpIntent( intent ) );
        if ( PROFILE_LINKED == colorSpace || PROFILE_EMBEDDED == colorSpace )
        {
            prf( "profile offset:                 %18u (DIB-relative; file offset %lld)\n", profileData, profileOffset );
            prf( "profile size:                   %18u\n", profileSize );
            if ( !IsFileRangeValid( profileOffset, profileSize ) )
                prf( "  (warning: color profile extends beyond available file data)\n" );
        }
    }

    if ( 0 == width || 0 == signedHeight || width < 0 ||
         (unsigned __int64) (unsigned int) width > UINT_MAX / 4 )
    {
        prf( "row layout:                     unavailable (invalid dimensions)\n" );
        return;
    }

    unsigned __int64 absHeight = signedHeight < 0 ? (unsigned __int64) -(long long) signedHeight : (unsigned __int64) signedHeight;
    unsigned __int64 rowBits = (unsigned __int64) (unsigned int) width * bitCount;
    unsigned __int64 rowStride = ((rowBits + 31) / 32) * 4;
    if ( rowStride > ULLONG_MAX / absHeight )
    {
        prf( "row layout:                     unavailable (dimensions overflow)\n" );
        return;
    }

    unsigned __int64 calculatedImageSize = rowStride * absHeight;
    prf( "row stride:                     %18llu bytes\n", rowStride );
    if ( BI_RGB == compression || BI_BITFIELDS == compression || 6 == compression )
        prf( "calculated pixel array:         %18llu bytes\n", calculatedImageSize );

    DWORD paletteEntries = colorsUsed;
    if ( 0 == paletteEntries && bitCount <= 8 )
        paletteEntries = 1u << bitCount;
    __int64 paletteOffset = 14 + headerSize;
    __int64 paletteBytes = pixelOffset >= paletteOffset ? pixelOffset - paletteOffset : 0;
    if ( 40 == headerSize && ( BI_BITFIELDS == compression || 6 == compression ) )
        paletteOffset = maskOffset;
    paletteBytes = pixelOffset >= paletteOffset ? pixelOffset - paletteOffset : 0;
    if ( paletteEntries )
    {
        DWORD entrySize = headerSize == 12 ? 3 : 4;
        unsigned __int64 expectedPaletteBytes = (unsigned __int64) paletteEntries * entrySize;
        prf( "palette entries:                %18u (expected %llu bytes, %lld available before pixels)\n",
             paletteEntries, expectedPaletteBytes, paletteBytes );
        if ( expectedPaletteBytes > (unsigned __int64) paletteBytes )
            prf( "  (warning: color table overlaps pixel data or is truncated)\n" );
    }

    if ( pixelOffset < 14 + headerSize || pixelOffset > fileLength )
        prf( "  (warning: pixel data offset is outside the valid BMP data range)\n" );
    else
    {
        __int64 availablePixelBytes = fileLength - pixelOffset;
        if ( declaredFileSize && declaredFileSize >= pixelOffset && declaredFileSize <= fileLength )
            availablePixelBytes = declaredFileSize - pixelOffset;
        prf( "available pixel data:           %18lld bytes\n", availablePixelBytes );

        if ( 0 != imageSize && imageSize > availablePixelBytes )
            prf( "  (warning: declared image data exceeds available pixel bytes)\n" );
        else if ( 0 == imageSize &&
                  ( BI_RGB == compression || BI_BITFIELDS == compression || 6 == compression ) &&
                  calculatedImageSize > (unsigned __int64) availablePixelBytes )
            prf( "  (warning: calculated pixel array exceeds available pixel bytes)\n" );
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

int DecodeMP3Text( const byte * data, int length, byte encoding, WCHAR * output, int outputCount )
{
    if ( outputCount <= 0 )
        return 0;
    output[ 0 ] = 0;

    if ( 0 == encoding || 3 == encoding )
    {
        while ( length > 0 && 0 == data[ length - 1 ] )
            length--;
        UINT codePage = ( 0 == encoding ) ? 28591 : CP_UTF8;
        int written = MultiByteToWideChar( codePage, 0, (const char *) data, length, output, outputCount - 1 );
        output[ written ] = 0;
        return written;
    }

    if ( 1 == encoding || 2 == encoding )
    {
        bool littleEndian = false;
        if ( length >= 2 && 0xfe == data[ 0 ] && 0xff == data[ 1 ] )
        {
            littleEndian = true;
            data += 2;
            length -= 2;
        }
        else if ( length >= 2 && 0xff == data[ 0 ] && 0xfe == data[ 1 ] )
        {
            littleEndian = true;
            data += 2;
            length -= 2;
        }
        int written = 0;
        for ( int i = 0; i + 1 < length && written < outputCount - 1; i += 2 )
        {
            WORD character = littleEndian
                ? (WORD) ( data[ i ] | ( data[ i + 1 ] << 8 ) )
                : (WORD) ( ( data[ i ] << 8 ) | data[ i + 1 ] );
            if ( 0 == character )
                break;
            output[ written++ ] = character;
        }
        output[ written ] = 0;
        return written;
    }

    return 0;
} //DecodeMP3Text

bool ReadMP3String( __int64 offset, WCHAR * pwc, int cwc, int maxBytes, bool lang,
                    bool throwawayField = false, bool invalidCharsToDot = true,
                    bool unsynchronize = false, WCHAR * description = NULL,
                    bool hasEncodingByte = true )
{
    if ( cwc <= 0 )
        return false;
    pwc[ 0 ] = 0;
    if ( NULL != description )
        description[ 0 ] = 0;
    if ( maxBytes <= 0 )
        return false;

    vector<byte> data( maxBytes );
    GetBytes( offset, data.data(), maxBytes );
    if ( unsynchronize )
    {
        size_t write = 0;
        for ( size_t read = 0; read < data.size(); read++ )
        {
            data[ write++ ] = data[ read ];
            if ( 0xff == data[ read ] && read + 1 < data.size() && 0 == data[ read + 1 ] &&
                 ( read + 2 == data.size() || 0 == data[ read + 2 ] || data[ read + 2 ] >= 0xe0 ) )
                read++;
        }
        data.resize( write );
    }

    if ( data.empty() )
        return false;
    byte encoding = hasEncodingByte ? data[ 0 ] : 0;
    if ( hasEncodingByte && encoding > 3 )
        return false;

    size_t textOffset = hasEncodingByte ? 1 : 0;
    if ( lang )
        textOffset += 3;
    if ( textOffset > data.size() )
        return false;
    size_t descriptionOffset = textOffset;

    if ( throwawayField )
    {
        size_t terminator = textOffset;
        if ( 0 == encoding || 3 == encoding )
        {
            while ( terminator < data.size() && 0 != data[ terminator ] )
                terminator++;
            if ( terminator == data.size() )
                return false;
            textOffset = terminator + 1;
        }
        else
        {
            while ( terminator + 1 < data.size() &&
                    ( 0 != data[ terminator ] || 0 != data[ terminator + 1 ] ) )
                terminator += 2;
            if ( terminator + 1 >= data.size() )
                return false;
            textOffset = terminator + 2;
        }
    }

    DecodeMP3Text( data.data() + textOffset, (int) ( data.size() - textOffset ), encoding, pwc, cwc );

    if ( NULL != description )
    {
        size_t descriptionEnd = descriptionOffset;
        if ( 0 == encoding || 3 == encoding )
        {
            while ( descriptionEnd < data.size() && 0 != data[ descriptionEnd ] )
                descriptionEnd++;
        }
        else
        {
            while ( descriptionEnd + 1 < data.size() &&
                    ( 0 != data[ descriptionEnd ] || 0 != data[ descriptionEnd + 1 ] ) )
                descriptionEnd += 2;
        }
        DecodeMP3Text( data.data() + descriptionOffset, (int) ( descriptionEnd - descriptionOffset ), encoding, description, cwc );
    }

    if ( invalidCharsToDot )
        ConvertInvalidCharsToDot( pwc );
    return true;
} //ReadMP3String

DWORD ReadSyncSafeDWORD( const byte * data )
{
    return ( (DWORD) ( data[ 0 ] & 0x7f ) << 21 ) |
           ( (DWORD) ( data[ 1 ] & 0x7f ) << 14 ) |
           ( (DWORD) ( data[ 2 ] & 0x7f ) << 7 ) |
           (DWORD) ( data[ 3 ] & 0x7f );
} //ReadSyncSafeDWORD

const char * GetMP3Genre( int g );

void PrintMP3ID3v1( __int64 fileLength, WCHAR * value, int valueCount,
                    bool hasTitle, bool hasArtist, bool hasAlbum, bool hasYear,
                    bool hasComment, bool hasTrack, bool hasGenre )
{
    if ( fileLength < 128 )
        return;

    struct ID3v1Tag
    {
        char id[ 3 ];
        char title[ 30 ];
        char artist[ 30 ];
        char album[ 30 ];
        char year[ 4 ];
        char comment[ 30 ];
        byte genre;
    } tag = {};
    GetBytes( fileLength - sizeof tag, &tag, sizeof tag );
    if ( 0 != memcmp( tag.id, "TAG", 3 ) )
        return;

    auto printText = [&]( const char * label, const char * data, int length )
    {
        while ( length > 0 && ( 0 == data[ length - 1 ] || ' ' == data[ length - 1 ] ) )
            length--;
        if ( 0 == length )
            return;
        DecodeMP3Text( (const byte *) data, length, 0, value, valueCount );
        wprf( L"%S: %ws\n", label, value );
    };

    if ( !hasTitle ) printText( "Title", tag.title, sizeof tag.title );
    if ( !hasArtist ) printText( "Lead performer(s)", tag.artist, sizeof tag.artist );
    if ( !hasAlbum ) printText( "Album", tag.album, sizeof tag.album );
    if ( !hasYear ) printText( "Year", tag.year, sizeof tag.year );
    if ( !hasComment ) printText( "Comments", tag.comment, ( 0 == tag.comment[ 28 ] ) ? 28 : 30 );

    if ( !hasTrack && 0 == tag.comment[ 28 ] && 0 != tag.comment[ 29 ] )
        prf( "Track:                %u\n", (byte) tag.comment[ 29 ] );
    if ( !hasGenre && 255 != tag.genre )
        prf( "Genre:                %u == %s\n", tag.genre, GetMP3Genre( tag.genre ) );
} //PrintMP3ID3v1

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
    if ( len <= 0 )
        return;

    prf( "mimetype:             audio/mpeg\n" );

    // some mp3 files have no header and start with 11 bits set.
    // other mp3 files have random header info that's not documented anywhere followed by 11 bits set.

    DWORD dwHead = 0;
    if ( len >= 4 )
        GetBytes( 0, &dwHead, sizeof dwHead );
    bool ID3HeaderExists = ( 0x334449 == ( dwHead & 0xffffff ) ); // "ID3"
    __int64 bitstreamOffset = 0;
    bool id3v2Unsynchronize = false;
    bool hasTitle = false, hasArtist = false, hasAlbum = false;
    bool hasYear = false, hasComment = false, hasTrack = false, hasGenre = false;
    WCHAR awcID3v1[ 256 ];

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
        if ( len < sizeof start )
        {
            prf( "invalid truncated ID3 header\n" );
            return;
        }
        GetBytes( 0, &start, sizeof start );
        DWORD tagSize = ReadSyncSafeDWORD( (const byte *) &start.size );
        start.size = tagSize;

        if ( start.ver[ 0 ] < 2 || start.ver[ 0 ] > 4 || 0xff == start.ver[ 1 ] ||
             (__int64) tagSize > len - sizeof start )
        {
            prf( "invalid or unsupported ID3v2 header/version/size\n" );
            return;
        }
    
        __int64 firstFrameOffset = sizeof start;
        bitstreamOffset = start.size + firstFrameOffset;

        id3v2Unsynchronize = 0 != ( start.flags & 0x80 );
        if ( 0 != ( start.flags & 0x40 ) )
        {
            if ( 2 == start.ver[ 0 ] )
            {
                prf( "compressed ID3v2.2 tag is unsupported\n" );
                return;
            }

            DWORD extendedHeaderSize = 0;
            if ( 3 == start.ver[ 0 ] )
                extendedHeaderSize = GetDWORD( firstFrameOffset, false ) + 4;
            else
            {
                byte syncSafeSize[ 4 ];
                GetBytes( firstFrameOffset, syncSafeSize, sizeof syncSafeSize );
                extendedHeaderSize = ReadSyncSafeDWORD( syncSafeSize );
            }

            if ( extendedHeaderSize < 4 || extendedHeaderSize > start.size )
            {
                prf( "invalid ID3 extended-header size: %u\n", extendedHeaderSize );
                return;
            }
            firstFrameOffset += extendedHeaderSize;
            prf( "ID3 extended header: %u bytes\n", extendedHeaderSize );
        }

        __int64 frameDataEnd = bitstreamOffset;
        if ( 4 == start.ver[ 0 ] && 0 != ( start.flags & 0x10 ) )
            frameDataEnd -= 10;
        if ( frameDataEnd < firstFrameOffset || frameDataEnd > len )
        {
            prf( "invalid ID3 tag boundary\n" );
            return;
        }
    
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
    
        while ( frameOffset < frameDataEnd )
        {
            if ( frameDataEnd - frameOffset < ( 2 == start.ver[ 0 ] ? 6 : 10 ) )
                break;

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
                if ( 4 == start.ver[0] )
                    frameHeader.size = ReadSyncSafeDWORD( (const byte *) &frameHeader.size );
                else
                    frameHeader.size = _byteswap_ulong( frameHeader.size );
                frameHeader.flags = (WORD) ( ( GetBYTE( frameOffset + 8 ) << 8 ) | GetBYTE( frameOffset + 9 ) );
            }
    
            if ( g_FullInformation )
            {
                prf( "frame header %c%c%c%c\n", frameHeader.id[0], frameHeader.id[1], frameHeader.id[2], frameHeader.id[3] );
                prf( "frame size %d == %#x\n", frameHeader.size, frameHeader.size );
                prf( "frame flags %#x\n", frameHeader.flags );
            }
    
            if ( 0 == frameHeader.size )
                break;

            if ( frameOffset + frameHeaderSize > frameDataEnd ||
                 frameHeader.size > frameDataEnd - frameOffset - frameHeaderSize )
            {
                prf( "invalid ID3 frame size: %u exceeds tag boundary\n", frameHeader.size );
                break;
            }

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

            __int64 framePayloadOffset = frameOffset + frameHeaderSize;
            int framePayloadSize = frameHeader.size;
            int physicalFramePayloadSize = frameHeader.size;
            int framePayloadReadSize = framePayloadSize;
            int skippedFramePrefixSize = 0;
            bool frameUnsynchronize = id3v2Unsynchronize;
            WORD formatFlags = (WORD) ( frameHeader.flags & 0xff );
            bool unsupportedFrameEncoding = false;
            if ( 3 == start.ver[ 0 ] )
            {
                unsupportedFrameEncoding = 0 != ( formatFlags & 0xc0 );
                if ( 0 != ( formatFlags & 0x20 ) && framePayloadSize > 0 )
                {
                    framePayloadOffset++;
                    framePayloadSize--;
                    skippedFramePrefixSize++;
                }
                if ( id3v2Unsynchronize )
                {
                    __int64 physicalOffset = framePayloadOffset;
                    int logicalBytes = 0;
                    while ( logicalBytes < framePayloadSize && physicalOffset < frameDataEnd )
                    {
                        byte value = GetBYTE( physicalOffset++ );
                        logicalBytes++;
                        if ( 0xff == value && physicalOffset < frameDataEnd && 0 == GetBYTE( physicalOffset ) &&
                             ( physicalOffset + 1 == frameDataEnd || 0 == GetBYTE( physicalOffset + 1 ) ||
                               GetBYTE( physicalOffset + 1 ) >= 0xe0 ) )
                            physicalOffset++;
                    }
                    if ( logicalBytes != framePayloadSize )
                    {
                        prf( "invalid unsynchronized ID3 frame size\n" );
                        break;
                    }
                    physicalFramePayloadSize = skippedFramePrefixSize +
                                               (int) ( physicalOffset - framePayloadOffset );
                }
            }
            else if ( 4 == start.ver[ 0 ] )
            {
                unsupportedFrameEncoding = 0 != ( formatFlags & 0x0c );
                frameUnsynchronize = frameUnsynchronize || 0 != ( formatFlags & 0x02 );
                if ( 0 != ( formatFlags & 0x40 ) && framePayloadSize > 0 )
                {
                    framePayloadOffset++;
                    framePayloadSize--;
                    skippedFramePrefixSize++;
                }
                if ( 0 != ( formatFlags & 0x01 ) )
                {
                    if ( framePayloadSize < 4 )
                        unsupportedFrameEncoding = true;
                    else
                    {
                        byte dataLength[ 4 ];
                        GetBytes( framePayloadOffset, dataLength, sizeof dataLength );
                        framePayloadOffset += 4;
                        framePayloadSize -= 4;
                    }
                }
            }
            if ( 3 == start.ver[ 0 ] && id3v2Unsynchronize )
                framePayloadReadSize = physicalFramePayloadSize - skippedFramePrefixSize;
            else
                framePayloadReadSize = framePayloadSize;

            if ( ( ( 'T' == frameHeader.id[0] ) && !isMP3Frame( frameHeader.id, "TXXX" ) ) ||
                   ( 'W' == frameHeader.id[0] ) )
                ReadMP3String( framePayloadOffset, awcField, _countof( awcField ), framePayloadReadSize,
                               false, false, true, frameUnsynchronize, NULL,
                               !( 'W' == frameHeader.id[0] && !isMP3Frame( frameHeader.id, "WXXX", "WXX" ) ) );

            if ( unsupportedFrameEncoding )
                prf( "skipping compressed or encrypted ID3 frame %.4s\n", frameHeader.id );
            else if ( isMP3Frame( frameHeader.id, "APIC", "PIC" ) && frameUnsynchronize )
                prf( "skipping unsynchronized embedded-image frame\n" );
            else if ( isMP3Frame( frameHeader.id, "APIC", "PIC" ) )
            {
                // Embedded image (or a link to an image)
                // byte:                           encoding
                // null-terminated ascii:          mime type. iTunes writes JPG or PNG with no null-termination
                // byte:                           picture type
                // null-terminated encoded string: description
                // 
    
                int o = (int) framePayloadOffset;
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
                    int imageSize = framePayloadReadSize - ( o - oFrameData );
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
    
                ReadMP3String( framePayloadOffset, awcField, _countof( awcField ), framePayloadReadSize, true, true, true, frameUnsynchronize );
                if ( 0 != awcField[0] )
                {
                    wprf( L"Comments:             %ws\n", awcField );
                    hasComment = true;
                }
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
                hasAlbum = 0 != awcField[ 0 ];
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
                hasGenre = 0 != awcField[ 0 ];
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
                hasYear = 0 != awcField[ 0 ];
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
                hasTitle = 0 != awcField[ 0 ];
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
                hasArtist = 0 != awcField[ 0 ];
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
                hasTrack = 0 != awcField[ 0 ];
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
                WCHAR description[ 8192 ];
                ReadMP3String( framePayloadOffset, awcField, _countof( awcField ), framePayloadReadSize,
                               false, true, true, frameUnsynchronize, description );
                if ( 0 != description[ 0 ] )
                    wprf( L"User text:            %ws = %ws\n", description, awcField );
            }
            else if ( isMP3Frame( frameHeader.id, "TYER", "TYE" ) )
            {
                wprf( L"Year:                 %ws\n", awcField );
                hasYear = 0 != awcField[ 0 ];
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

                ReadMP3String( framePayloadOffset, awcField, _countof( awcField ), framePayloadReadSize, true, true, false, frameUnsynchronize );

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
                WCHAR description[ 8192 ];
                ReadMP3String( framePayloadOffset, awcField, _countof( awcField ), framePayloadSize,
                               false, true, true, frameUnsynchronize, description );
                if ( 0 != description[ 0 ] )
                    wprf( L"Webpage (%ws):        %ws\n", description, awcField );
                else
                    wprf( L"Webpage:              %ws\n", awcField );
            }
            else
            {
                prf( "unknown frame id %c%c%c%c == %#x %#x %#x %#x\n", frameHeader.id[0], frameHeader.id[1], frameHeader.id[2], frameHeader.id[3],
                        0xff & frameHeader.id[0], 0xff & frameHeader.id[1], 0xff & frameHeader.id[2], 0xff & frameHeader.id[3] );
            }
    
            frameOffset += frameHeaderSize + physicalFramePayloadSize;
    
            if ( frameOffset > g_pStream->Length() )
            {
                prf( "invalid frame offset is beyond the end of the file %#I64x\n", frameOffset );
                break;
            }
    
            // the >= case will be caught in the while loop above; no need to break
    
            if ( frameOffset > frameDataEnd )
            {
                prf( "invalid frame offset %I64d is beyond the ID3 tag boundary %I64d\n", frameOffset, frameDataEnd );
            }
        }
    }
    PrintMP3ID3v1( len, awcID3v1, _countof( awcID3v1 ), hasTitle, hasArtist, hasAlbum,
                   hasYear, hasComment, hasTrack, hasGenre );

    //printf( "bitstreamOffset: %d\n", bitstreamOffset );

    // The 11 consecutive bits turned on signify the start of a compressed audio chunk. The are
    // generally just after the TAG header and data, but sometimes they are just beyond.
    // Most files have the bitstream at the expected location.
    // Most of the remaining have the bistream within 100 bytes of the expected location.
    // Most of the remaining of that have it within 1024.
    // All of my mp3 files aside from Radiohead the king of limbs are within 8192.
    // Since the search rarely happens and it's safe, allow such a long search.
    // http://www.multiweb.cz/twoinches/mp3inside.htm

    const int searchLen = (int) min( (__int64) 8192, max( (__int64) 0, len - bitstreamOffset - 1 ) );
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

    __int64 frameOffset = bitstreamOffset + delta;
    if ( frameOffset + sizeof( DWORD ) > len )
    {
        prf( "truncated MPEG frame header\n" );
        return;
    }
    DWORD h = GetDWORD( frameOffset, false );
    prf( "MPEGHeader:           %#x == ", h );
    for ( int i = 31; i >= 0; i-- )
        prf( 0 != ( ( h >> i ) & 0x1 ) ? "1" : "0" );
    prf( "\n" );

    if ( 0xffe00000 == ( h & 0xffe00000 ) )
    {
        int audioVersion = ( h >> 19 ) & 0x3;
        prf( "MPEG audio Version:  %2d == %s\n", audioVersion, GetMPEGAudioVersion( audioVersion ) );
    
        int layerDescription = ( h >> 17 ) & 0x3;
        prf( "Layer:               %2d == %s\n", layerDescription, GetMPEGLayerDescription( layerDescription ) );
    
        DWORD frameCount = 0;
        DWORD audioBytes = 0;
        DWORD xingFlags = 0;
        bool variableBitrate = false;
        int channelmode = ( h >> 6 ) & 0x3;
        int protectedBit = ( h >> 16 ) & 0x1;
        if ( 1 == layerDescription )
        {
            int sideInformationSize = ( 3 == audioVersion )
                ? ( 3 == channelmode ? 17 : 32 )
                : ( 3 == channelmode ? 9 : 17 );
            __int64 xingOffset = frameOffset + 4 + ( 0 == protectedBit ? 2 : 0 ) + sideInformationSize;
            if ( xingOffset + 8 <= len )
            {
                DWORD marker = GetDWORD( xingOffset, false );
                if ( 0x58696e67 == marker || 0x496e666f == marker ) // Xing or Info
                {
                    __int64 fieldOffset = xingOffset + 4;
                    xingFlags = GetDWORD( fieldOffset, false );
                    fieldOffset += 4;
                    variableBitrate = 0x58696e67 == marker;
                    prf( "%s header:            flags %#x, offset %I64d\n",
                         variableBitrate ? "Variable bitrate (Xing)" : "MPEG Info", xingFlags, xingOffset );

                    if ( 0 != ( xingFlags & 0x1 ) && fieldOffset + 4 <= len )
                    {
                        frameCount = GetDWORD( fieldOffset, false );
                        fieldOffset += 4;
                        prf( "  VBR frames:         %u\n", frameCount );
                    }
                    if ( 0 != ( xingFlags & 0x2 ) && fieldOffset + 4 <= len )
                    {
                        audioBytes = GetDWORD( fieldOffset, false );
                        fieldOffset += 4;
                        prf( "  VBR bytes:          %u\n", audioBytes );
                    }
                    if ( 0 != ( xingFlags & 0x4 ) )
                        fieldOffset += 100;
                    if ( 0 != ( xingFlags & 0x8 ) )
                        fieldOffset += 4;

                    if ( fieldOffset + 9 <= len )
                    {
                        char encoder[ 10 ] = {};
                        GetBytes( fieldOffset, encoder, 9 );
                        if ( 0 == strncmp( encoder, "LAME", 4 ) )
                            prf( "  Encoder:            %.9s\n", encoder );
                    }

                    if ( frameCount > 0 && audioBytes > 0 )
                    {
                        int sampleRateIndex = ( h >> 10 ) & 0x3;
                        static const int sampleRates[ 3 ][ 3 ] =
                        {
                            { 11025, 12000, 8000 },
                            { 22050, 24000, 16000 },
                            { 44100, 48000, 32000 }
                        };
                        int sampleRateRow = ( 3 == audioVersion ) ? 2 : ( 2 == audioVersion ? 1 : 0 );
                        int samplesPerFrame = ( 3 == audioVersion ) ? 1152 : 576;
                        if ( sampleRateIndex < 3 )
                        {
                            double duration = (double) frameCount * samplesPerFrame /
                                              sampleRates[ sampleRateRow ][ sampleRateIndex ];
                            double averageBitrate = (double) audioBytes * 8.0 / duration / 1000.0;
                            bool plausibleCounts = averageBitrate >= 8.0 && averageBitrate <= 640.0;
                            if ( 0x496e666f == marker )
                            {
                                char infoScratch[ 10 ];
                                int nominalBitrate = atoi( GetMPEGBitrate( ( h >> 12 ) & 0xf,
                                                                          audioVersion, layerDescription,
                                                                          infoScratch ) );
                                if ( nominalBitrate > 0 &&
                                     fabs( averageBitrate - nominalBitrate ) > max( 10.0, nominalBitrate * 0.2 ) )
                                    plausibleCounts = false;
                            }

                            if ( plausibleCounts )
                            {
                                int durationSeconds = (int) ( duration + 0.5 );
                                prf( "  Audio duration:     %d:%02d (approx)\n",
                                     durationSeconds / 60, durationSeconds % 60 );
                                prf( "  Average bitrate:    %.1lf kbps (from Xing frame/byte counts)\n",
                                     averageBitrate );
                            }
                            else
                                prf( "  ignored inconsistent Xing frame/byte counts\n" );
                        }
                    }
                }
            }
        }
    
        prf( "Protected:           %2d == %s\n", protectedBit, GetMPEGProtected( protectedBit ) );
    
        int bitrate = ( h >> 12 ) & 0xf;
        char acScratch[ 10 ];
        char const * pcBitrate = GetMPEGBitrate( bitrate, audioVersion, layerDescription, acScratch );
        prf( "Bitrate:             %2d == %s kbps\n", bitrate, variableBitrate ? "variable" : pcBitrate );
    
        int samplerate = ( h >> 10 ) & 0x3;
        char const * pcSamplerate = GetMPEGSamplerate( samplerate, audioVersion, acScratch );
        prf( "Sample rate:         %2d == %s Hz\n", samplerate, pcSamplerate );
    
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
const GUID ASF_Audio_Media =                         { 0xF8699E40, 0x5B4D, 0x11CF, 0xA8, 0xFD, 0x00, 0x80, 0x5F, 0x5C, 0x44, 0x2B };
const GUID ASF_Video_Media =                         { 0xBC19EFC0, 0x5B4D, 0x11CF, 0xA8, 0xFD, 0x00, 0x80, 0x5F, 0x5C, 0x44, 0x2B };
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

void PrintAsfTextField( const WCHAR * label, __int64 offset, WORD byteLength )
{
    if ( 0 == byteLength )
        return;

    if ( 0 != byteLength % sizeof( WCHAR ) )
    {
        prf( "    invalid UTF-16 field length: %u\n", byteLength );
        return;
    }

    vector<WCHAR> text( byteLength / sizeof( WCHAR ) + 1 );
    GetBytes( offset, text.data(), byteLength );
    text[ byteLength / sizeof( WCHAR ) ] = 0;
    wprf( L"    %-30ls: %ls\n", label, text.data() );
} //PrintAsfTextField

void PrintAsfStreamProperties( __int64 offset, QWORD objectSize )
{
    const QWORD fixedSize = 24 + 16 + 16 + sizeof( QWORD ) +
                            2 * sizeof( DWORD ) + sizeof( WORD ) + sizeof( DWORD );
    if ( objectSize < fixedSize )
    {
        prf( "    invalid Stream Properties Object size\n" );
        return;
    }

    __int64 p = offset + 24;
    GUID streamType;
    GUID errorCorrectionType;
    GetBytes( p, &streamType, sizeof streamType );
    p += sizeof streamType;
    GetBytes( p, &errorCorrectionType, sizeof errorCorrectionType );
    p += sizeof errorCorrectionType + sizeof( QWORD );
    DWORD typeSpecificDataLength = GetDWORD( p, true );
    p += sizeof DWORD;
    DWORD errorCorrectionDataLength = GetDWORD( p, true );
    p += sizeof DWORD;
    WORD flags = GetWORD( p, true );
    p += sizeof WORD + sizeof( DWORD );
    QWORD dataLength = (QWORD) typeSpecificDataLength + errorCorrectionDataLength;

    if ( fixedSize + dataLength > objectSize )
    {
        prf( "    invalid Stream Properties data lengths\n" );
        return;
    }

    prf( "    %-30s: %u\n", "Stream Number", flags & 0x7f );
    if ( flags & 0x8000 )
        prf( "    stream is encrypted\n" );

    if ( ASF_Audio_Media == streamType && typeSpecificDataLength >= 18 )
    {
        WORD formatTag = GetWORD( p, true );
        WORD channels = GetWORD( p + 2, true );
        DWORD sampleRate = GetDWORD( p + 4, true );
        DWORD averageBytesPerSecond = GetDWORD( p + 8, true );
        WORD blockAlign = GetWORD( p + 12, true );
        WORD bitsPerSample = GetWORD( p + 14, true );
        WORD extraSize = GetWORD( p + 16, true );

        prf( "    %-30s: Audio\n", "Stream Type" );
        prf( "    %-30s: %#06x\n", "Audio Format Tag", formatTag );
        prf( "    %-30s: %u\n", "Audio Channels", channels );
        prf( "    %-30s: %u Hz\n", "Audio Sample Rate", sampleRate );
        prf( "    %-30s: %I64u bits per second\n", "Stream Average Bitrate",
             (QWORD) averageBytesPerSecond * 8 );
        prf( "    %-30s: %u\n", "Audio Block Align", blockAlign );
        prf( "    %-30s: %u\n", "Audio Bits Per Sample", bitsPerSample );
        if ( g_FullInformation )
            prf( "    %-30s: %u bytes\n", "Audio Format Extra Data", extraSize );
    }
    else if ( ASF_Video_Media == streamType )
    {
        prf( "    %-30s: Video\n", "Stream Type" );
        if ( typeSpecificDataLength >= 11 )
        {
            DWORD width = GetDWORD( p, true );
            DWORD height = GetDWORD( p + 4, true );
            WORD formatDataSize = GetWORD( p + 9, true );
            prf( "    %-30s: %u\n", "Video Width", width );
            prf( "    %-30s: %u\n", "Video Height", height );

            if ( formatDataSize > 0 && formatDataSize <= typeSpecificDataLength - 11 )
            {
                __int64 bitmapInfo = p + 11;
                if ( formatDataSize >= 40 && GetDWORD( bitmapInfo, true ) >= 40 &&
                     GetDWORD( bitmapInfo, true ) <= formatDataSize )
                {
                    DWORD compression = GetDWORD( bitmapInfo + 16, true );
                    prf( "    %-30s: %c%c%c%c\n", "Video Codec FourCC",
                         compression & 0xff, ( compression >> 8 ) & 0xff,
                         ( compression >> 16 ) & 0xff, ( compression >> 24 ) & 0xff );
                }
            }
            else if ( formatDataSize )
                prf( "    invalid video format data size: %u\n", formatDataSize );
        }
        else
            prf( "    incomplete video stream properties data\n" );
    }
    else
    {
        prf( "    %-30s: unknown\n", "Stream Type" );
    }
} //PrintAsfStreamProperties

bool PrintAsfExtendedContentDescription( __int64 objectOffset, QWORD objectSize )
{
    __int64 position = objectOffset + sizeof( GUID ) + sizeof( QWORD );
    __int64 end = objectOffset + (__int64) objectSize;
    if ( objectSize < sizeof( GUID ) + sizeof( QWORD ) + sizeof( WORD ) )
    {
        prf( "    invalid Extended Content Description Object size\n" );
        return false;
    }

    WORD descriptorCount = GetWORD( position, true );
    position += sizeof( WORD );
    prf( "    extended content count        : %u\n", descriptorCount );

    for ( WORD descriptor = 0; descriptor < descriptorCount; descriptor++ )
    {
        if ( end - position < sizeof( WORD ) )
        {
            prf( "    truncated extended content descriptor name length\n" );
            return false;
        }
        WORD nameLength = GetWORD( position, true );
        position += sizeof( WORD );
        if ( ( nameLength & 1 ) || nameLength > end - position ||
             end - position - nameLength < 2 * sizeof( WORD ) )
        {
            prf( "    invalid extended content descriptor name length: %u\n", nameLength );
            return false;
        }

        vector<WCHAR> name( nameLength / sizeof( WCHAR ) + 1 );
        if ( nameLength )
            GetBytes( position, name.data(), nameLength );
        position += nameLength;
        name[ nameLength / sizeof( WCHAR ) ] = 0;

        WORD valueType = GetWORD( position, true );
        position += sizeof( WORD );
        WORD valueLength = GetWORD( position, true );
        position += sizeof( WORD );
        if ( valueLength > end - position )
        {
            prf( "    invalid extended content value length: %u\n", valueLength );
            return false;
        }

        if ( g_FullInformation )
            prf( "      value type %u, length %u\n", valueType, valueLength );

        wprf( L"    %-30ls: ", name.data() );
        if ( 0 == valueType )
        {
            if ( valueLength & 1 )
                prf( "invalid UTF-16 value length %u\n", valueLength );
            else
            {
                vector<WCHAR> value( valueLength / sizeof( WCHAR ) + 1 );
                if ( valueLength )
                    GetBytes( position, value.data(), valueLength );
                value[ valueLength / sizeof( WCHAR ) ] = 0;
                wprf( L"%ls\n", value.data() );
            }
        }
        else if ( 1 == valueType )
        {
            prf( "binary data (%u bytes)\n", valueLength );
            if ( g_FullInformation && valueLength )
                DumpBinaryData( position, 0, valueLength, 6, position );
        }
        else if ( 2 == valueType && sizeof( DWORD ) == valueLength )
            prf( "%s\n", GetDWORD( position, true ) ? "true" : "false" );
        else if ( 3 == valueType && sizeof( DWORD ) == valueLength )
            prf( "%u\n", GetDWORD( position, true ) );
        else if ( 4 == valueType && sizeof( QWORD ) == valueLength )
            prf( "%I64u\n", GetULONGLONG( position, true ) );
        else if ( 5 == valueType && sizeof( WORD ) == valueLength )
            prf( "%u\n", GetWORD( position, true ) );
        else if ( 6 == valueType && sizeof( GUID ) == valueLength )
        {
            GUID value;
            GetBytes( position, &value, sizeof value );
            prf( "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}\n",
                 value.Data1, value.Data2, value.Data3, value.Data4[ 0 ], value.Data4[ 1 ],
                 value.Data4[ 2 ], value.Data4[ 3 ], value.Data4[ 4 ], value.Data4[ 5 ],
                 value.Data4[ 6 ], value.Data4[ 7 ] );
        }
        else
            prf( "unsupported value type %u (%u bytes)\n", valueType, valueLength );

        position += valueLength;
    }

    return true;
} //PrintAsfExtendedContentDescription

// WMA, WMV, etc.
// note: as far as I can tell, WMA files never have an embedded image.

void ParseAsf( const char * mimeType )
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
    if ( len < (int) sizeof( AsfObject ) )
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

    prf( "mimetype:                             %s\n", mimeType );

    __int64 frameOffset = 0;

    while ( frameOffset < len )
    {
        if ( len - frameOffset < (int) sizeof( AsfObject ) )
        {
            prf( "truncated ASF object header at offset %I64d\n", frameOffset );
            return;
        }

        if ( g_FullInformation )
            prf( "reading next frame at offset %I64d\n", frameOffset );

        AsfObject obj;
        GetBytes( frameOffset, &obj, sizeof obj );
    
        PrintAsfGuid( "", obj.id, obj.size );
    
        if ( obj.size < sizeof( AsfObject ) || obj.size > (QWORD) ( len - frameOffset ) )
        {
            prf( "invalid ASF object size %I64u at offset %I64d (remaining %I64d)\n",
                 obj.size, frameOffset, len - frameOffset );
            return;
        }

        if ( ASF_Header_Object == obj.id )
        {

            AsfHeaderObject headobj;
            if ( obj.size < sizeof headobj )
            {
                prf( "invalid ASF Header Object size\n" );
                return;
            }
            GetBytes( frameOffset, &headobj, sizeof headobj );

            prf( "header object count: %d\n", headobj.numObjects );
            if ( headobj.numObjects > ( obj.size - sizeof headobj ) / sizeof( AsfObject ) )
            {
                prf( "invalid ASF header object count: %u\n", headobj.numObjects );
                return;
            }

            __int64 offset = frameOffset + sizeof headobj;

            for ( DWORD i = 0; i < headobj.numObjects; i++ )
            {
                if ( offset > frameOffset + obj.size ||
                     frameOffset + obj.size - offset < sizeof( AsfObject ) )
                {
                    prf( "truncated ASF header object\n" );
                    return;
                }
                AsfObject headerObject;
                GetBytes( offset, &headerObject, sizeof headerObject );

                PrintAsfGuid( "  ", headerObject.id, headerObject.size );
                if ( headerObject.size < sizeof( AsfObject ) ||
                     offset > frameOffset + obj.size ||
                     headerObject.size > frameOffset + obj.size - offset ||
                     headerObject.size > len - offset )
                {
                    prf( "invalid ASF header object size\n" );
                    return;
                }

                if ( ASF_Content_Description_Object == headerObject.id )
                {
                    if ( headerObject.size < sizeof( AsfContentDescriptionObject ) )
                    {
                        prf( "    invalid Content Description Object size\n" );
                        offset += headerObject.size;
                        continue;
                    }
                    AsfContentDescriptionObject cdo;
                    GetBytes( offset, &cdo, sizeof cdo );
                    QWORD textLength = (QWORD) cdo.titleLen + cdo.authorLen +
                                       cdo.copyrightLen + cdo.descriptionLen +
                                       cdo.ratingLen;

                    if ( g_FullInformation )
                    {
                        prf( "    title length: %u\n", cdo.titleLen );
                        prf( "    author length: %u\n", cdo.authorLen );
                        prf( "    copyright length: %u\n", cdo.copyrightLen );
                        prf( "    description length: %u\n", cdo.descriptionLen );
                        prf( "    rating length: %u\n", cdo.ratingLen );
                    }

                    if ( sizeof cdo + textLength > headerObject.size )
                    {
                        prf( "    invalid Content Description field lengths\n" );
                    }
                    else
                    {
                        __int64 textOffset = offset + sizeof cdo;
                        PrintAsfTextField( L"Title", textOffset, cdo.titleLen );
                        textOffset += cdo.titleLen;
                        PrintAsfTextField( L"Author", textOffset, cdo.authorLen );
                        textOffset += cdo.authorLen;
                        PrintAsfTextField( L"Copyright", textOffset, cdo.copyrightLen );
                        textOffset += cdo.copyrightLen;
                        PrintAsfTextField( L"Description", textOffset, cdo.descriptionLen );
                        textOffset += cdo.descriptionLen;
                        PrintAsfTextField( L"Rating", textOffset, cdo.ratingLen );
                    }
                }
                else if ( ASF_Extended_Content_Description_Object == headerObject.id )
                {
                    if ( !PrintAsfExtendedContentDescription( offset, headerObject.size ) )
                        return;
                }
                else if ( ASF_Header_Extension_Object == headerObject.id )
                {
                    if ( headerObject.size < sizeof( AsfHeaderExtensionObject ) )
                    {
                        prf( "    invalid Header Extension Object size\n" );
                        offset += headerObject.size;
                        continue;
                    }
                    AsfHeaderExtensionObject ext;
                    GetBytes( offset, &ext, sizeof ext );

                    prf( "    Header Extension Object size %u\n", ext.dataSize );
                    if ( ext.dataSize > headerObject.size - sizeof ext )
                    {
                        prf( "    invalid Header Extension data size\n" );
                    }
                    else
                    {
                        __int64 nestedOffset = offset + sizeof ext;
                        __int64 nestedEnd = nestedOffset + ext.dataSize;
                        while ( nestedOffset < nestedEnd )
                        {
                            if ( nestedEnd - nestedOffset < sizeof( AsfObject ) )
                            {
                                prf( "    truncated nested ASF header object\n" );
                                break;
                            }
                            AsfObject nestedObject;
                            GetBytes( nestedOffset, &nestedObject, sizeof nestedObject );
                            if ( nestedObject.size < sizeof( AsfObject ) ||
                                 nestedObject.size > (QWORD) ( nestedEnd - nestedOffset ) )
                            {
                                prf( "    invalid nested ASF header object size %I64u\n",
                                     nestedObject.size );
                                break;
                            }
                            if ( ASF_Stream_Properties_Object == nestedObject.id )
                            {
                                PrintAsfGuid( "    ", nestedObject.id, nestedObject.size );
                                PrintAsfStreamProperties( nestedOffset, nestedObject.size );
                            }
                            nestedOffset += nestedObject.size;
                        }
                        if ( g_FullInformation && ext.dataSize )
                            DumpBinaryData( offset + sizeof ext, 0, ext.dataSize, 6,
                                            offset + sizeof ext );
                    }
                }
                else if ( ASF_File_Properties_Object == headerObject.id )
                {
                    if ( headerObject.size < sizeof( AsfFilePropertiesObject ) )
                    {
                        prf( "    invalid File Properties Object size\n" );
                        offset += headerObject.size;
                        continue;
                    }
                    AsfFilePropertiesObject props;
                    GetBytes( offset, &props, sizeof props );

                    QWORD durationMilliseconds = props.playDuration / 10000;
                    QWORD sendDurationMilliseconds = props.sendDuration / 10000;
                    QWORD prerollMilliseconds = props.preroll;
                    QWORD playbackMilliseconds = durationMilliseconds > prerollMilliseconds ?
                                                 durationMilliseconds - prerollMilliseconds : 0;
                    prf( "    %-30s: %I64u.%03I64u seconds\n", "Duration",
                         durationMilliseconds / 1000, durationMilliseconds % 1000 );
                    prf( "    %-30s: %I64u.%03I64u seconds\n", "Duration excluding preroll",
                         playbackMilliseconds / 1000, playbackMilliseconds % 1000 );
                    prf( "    %-30s: %I64u.%03I64u seconds\n", "Send Duration",
                         sendDurationMilliseconds / 1000, sendDurationMilliseconds % 1000 );
                    prf( "    %-30s: %I64u milliseconds\n", "Preroll", prerollMilliseconds );
                    prf( "    %-30s: %u bits per second\n", "Maximum Bitrate", props.maxBitrate );
                }
                else if ( ASF_Stream_Properties_Object == headerObject.id )
                {
                    PrintAsfStreamProperties( offset, headerObject.size );
                }
                else if ( ASF_Codec_List_Object == headerObject.id )
                {
                    const __int64 dataStart = offset + sizeof( AsfObject );
                    const __int64 dataEnd = offset + headerObject.size;
                    if ( headerObject.size < sizeof( AsfObject ) + sizeof( GUID ) + sizeof( DWORD ) )
                    {
                        prf( "    invalid Codec List Object size\n" );
                    }
                    else
                    {
                        DWORD codecCount = GetDWORD( dataStart + sizeof( GUID ), true );
                        __int64 p = dataStart + sizeof( GUID ) + sizeof( DWORD );
                        prf( "    codec count: %u\n", codecCount );

                        for ( DWORD codec = 0; codec < codecCount; codec++ )
                        {
                            if ( p > dataEnd || dataEnd - p < 8 )
                            {
                                prf( "    truncated codec list entry\n" );
                                break;
                            }

                            WORD type = GetWORD( p, true );
                            WORD nameLength = GetWORD( p + 2, true );
                            p += 4;
                            QWORD nameBytes = (QWORD) nameLength * sizeof( WCHAR );
                            if ( nameBytes > (QWORD) ( dataEnd - p ) )
                            {
                                prf( "    invalid codec name length\n" );
                                break;
                            }

                            vector<WCHAR> codecName( (size_t) nameLength + 1 );
                            if ( nameLength )
                                GetBytes( p, codecName.data(), (int) nameBytes );
                            codecName[ nameLength ] = 0;
                            p += nameBytes;

                            if ( dataEnd - p < sizeof( WORD ) )
                            {
                                prf( "    truncated codec description length\n" );
                                break;
                            }
                            WORD descriptionLength = GetWORD( p, true );
                            p += sizeof WORD;
                            QWORD descriptionBytes = (QWORD) descriptionLength * sizeof( WCHAR );
                            if ( descriptionBytes > (QWORD) ( dataEnd - p ) )
                            {
                                prf( "    invalid codec description length\n" );
                                break;
                            }

                            vector<WCHAR> codecDescription( (size_t) descriptionLength + 1 );
                            if ( descriptionLength )
                                GetBytes( p, codecDescription.data(), (int) descriptionBytes );
                            codecDescription[ descriptionLength ] = 0;
                            p += descriptionBytes;

                            if ( dataEnd - p < sizeof( WORD ) )
                            {
                                prf( "    truncated codec information length\n" );
                                break;
                            }
                            WORD informationLength = GetWORD( p, true );
                            p += sizeof WORD;
                            if ( informationLength > (QWORD) ( dataEnd - p ) )
                            {
                                prf( "    invalid codec information length\n" );
                                break;
                            }

                            wprf( L"    Codec Type: %ls\n",
                                  2 == type ? L"Audio" : 1 == type ? L"Video" : L"Unknown" );
                            wprf( L"    Codec Name: %ls\n", codecName.data() );
                            if ( descriptionLength )
                                wprf( L"    Codec Description: %ls\n", codecDescription.data() );
                            if ( g_FullInformation && informationLength )
                                prf( "    Codec Information: %u bytes\n", informationLength );
                            p += informationLength;
                        }
                    }
                }
                else if ( ASF_Stream_Bitrate_Properties_Object == headerObject.id )
                {
                    __int64 o = offset + sizeof AsfObject;
                    WORD recordCount = GetWORD( o, true );
                    o += sizeof WORD;

                    if ( headerObject.size < sizeof( AsfObject ) + sizeof( WORD ) ||
                         recordCount > ( headerObject.size - sizeof( AsfObject ) - sizeof( WORD ) ) /
                                       ( sizeof( WORD ) + sizeof( DWORD ) ) )
                    {
                        prf( "    invalid stream bitrate record count\n" );
                        offset += headerObject.size;
                        continue;
                    }
        
                    //printf( "Bitrate for %u streams\n", recordCount );
        
                    for ( WORD s = 0; s < recordCount; s++ )
                    {
                        WORD flags = GetWORD( o, true );
                        o += sizeof WORD;
                        DWORD averageBitrate = GetDWORD( o, true );
                        o += sizeof DWORD;
        
                        prf( "    %-30s: %u\n", "Stream Number", flags & 0x7f );
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
        return "8-bit ITU-T G.711 �-law";

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

const char * RafRecordName( WORD tag )
{
    switch ( tag )
    {
        case 0x0100: return "Sensor dimensions";
        case 0x0110: return "Active area top-left";
        case 0x0111: return "Active area height and width";
        case 0x0121: return "Output height and width";
        case 0x0130: return "Raw information";
        case 0x0131: return "CFA pattern";
        case 0x2ff0: return "Camera multipliers";
        case 0xc000: return "Additional RAF data";
        default: return NULL;
    }
}

void PrintRafRecordData( WORD tag, __int64 dataOffset, WORD size )
{
    if ( ( 0x0100 == tag || 0x0111 == tag || 0x0121 == tag ) && 4 == size )
    {
        WORD first = GetWORD( dataOffset, false );
        WORD second = GetWORD( dataOffset + 2, false );
        prf( "%u x %u (height x width)\n", first, second );
    }
    else if ( 0x0110 == tag && 4 == size )
    {
        prf( "x=%u, y=%u\n", GetWORD( dataOffset, false ), GetWORD( dataOffset + 2, false ) );
    }
    else if ( 0x0131 == tag && size > 0 )
    {
        if ( 36 == size )
        {
            prf( "6x6 X-Trans CFA pattern (0=red, 1=green, 2=blue):\n" );
            for ( int y = 0; y < 6; y++ )
            {
                prf( "      " );
                for ( int x = 0; x < 6; x++ )
                    prf( "%u%s", GetBYTE( dataOffset + y * 6 + x ), x == 5 ? "" : " " );
                prf( "\n" );
            }
        }
        else
        {
            prf( "%u-byte CFA pattern: ", size );
            for ( WORD i = 0; i < min( size, (WORD) 36 ); i++ )
                prf( "%s%u", i ? " " : "", GetBYTE( dataOffset + i ) );
            prf( "\n" );
        }
    }
    else if ( 0x0130 == tag && size > 0 )
    {
        prf( "%u bytes (layout not documented):", size );
        for ( WORD i = 0; i < min( size, (WORD) 16 ); i++ )
            prf( " %02x", GetBYTE( dataOffset + i ) );
        if ( size > 16 )
            prf( " ..." );
        prf( "\n" );
    }
    else if ( 0x2ff0 == tag && size >= 8 && 0 == ( size % 2 ) )
    {
        prf( "u16 values (documented channel order 1, 0, 3, 2; repeated):" );
        for ( WORD i = 0; i < min( size / 2, (WORD) 16 ); i++ )
            prf( " %u", GetWORD( dataOffset + i * 2, false ) );
        prf( "\n" );
    }
    else if ( 0xc000 == tag )
    {
        prf( "%u bytes (opaque vendor data)\n", size );
    }
    else if ( size > 0 )
    {
        prf( "%u bytes", size );
        for ( WORD i = 0; i < min( size, (WORD) 16 ); i++ )
            prf( " %02x", GetBYTE( dataOffset + i ) );
        if ( size > 16 )
            prf( " ..." );
        prf( "\n" );
    }
    else
        prf( "empty\n" );
}

void ParseRafDataImageDimensions( __int64 dataOffset, WORD size, DWORD fullWidth, DWORD fullHeight,
                                  DWORD & imageWidth, DWORD & imageHeight )
{
    if ( size < 16 || 0 == fullWidth || 0 == fullHeight )
        return;

    // RAFData has several documented index layouts; prefer the plausible pair closest to the full sensor size.
    DWORD values[ 4 ] = {};
    for ( int i = 0; i < _countof( values ); i++ )
        values[ i ] = GetDWORD( dataOffset + i * sizeof( DWORD ), true );

    unsigned __int64 bestScore = ULLONG_MAX;
    DWORD bestWidth = 0;
    DWORD bestHeight = 0;
    for ( int i = 0; i < 3; i++ )
    {
        DWORD width = values[ i ];
        DWORD height = values[ i + 1 ];
        if ( 0 == width || 0 == height || width > fullWidth || height > fullHeight )
            continue;

        unsigned __int64 score = (unsigned __int64) ( fullWidth - width ) + ( fullHeight - height );
        if ( score < bestScore )
        {
            bestScore = score;
            bestWidth = width;
            bestHeight = height;
        }
    }

    if ( bestWidth && bestHeight )
    {
        imageWidth = bestWidth;
        imageHeight = bestHeight;
    }
}

const char * RafFujiIfdTagName( WORD tag )
{
    switch ( tag )
    {
        case 0xf003: return "BitsPerSample";
        case 0xf007: return "StripOffsets";
        case 0xf008: return "StripByteCounts";
        case 0xf00a: return "BlackLevel";
        case 0xf00b: return "GeometricDistortionParams";
        case 0xf00c: return "WB_GRBLevelsStandard";
        case 0xf00d: return "WB_GRBLevelsAuto";
        case 0xf00e: return "WB_GRBLevels";
        case 0xf00f: return "ChromaticAberrationParams";
        case 0xf010: return "VignettingParams";
        default: return NULL;
    }
}

void PrintRafFujiIfdField( WORD tag, __int64 entryOffset, bool littleEndian, const RafHeader & raf )
{
    const char * tagName = RafFujiIfdTagName( tag );
    if ( NULL == tagName )
        return;

    IFDHeader head = {};
    GetBytes( entryOffset, &head, sizeof head );
    head.Endian( littleEndian );
    if ( head.type > 13 )
        return;
    DWORD valueSize = ( 1 == head.type || 2 == head.type || 6 == head.type || 7 == head.type ) ? 1 :
                      ( 3 == head.type || 8 == head.type ) ? 2 :
                      ( 5 == head.type || 10 == head.type ) ? 8 : 4;
    unsigned __int64 byteCount = (unsigned __int64) head.count * valueSize;
    if ( 0 == head.count || byteCount > 0x10000 )
        return;

    __int64 valueOffset = byteCount <= 4 ? entryOffset + 8 : (__int64) raf.cfaOffset + head.offset;
    __int64 cfaEnd = (__int64) raf.cfaOffset + raf.cfaLength;
    if ( valueOffset < raf.cfaOffset || valueOffset > cfaEnd ||
         (__int64) byteCount > cfaEnd - valueOffset || !IsFileRangeValid( valueOffset, (__int64) byteCount ) )
        return;

    prf( "  RAF FujiIFD %-27s: ", tagName );
    if ( 2 == head.type )
    {
        vector<char> text( min( head.count, (DWORD) 256 ) + 1, 0 );
        GetBytes( valueOffset, text.data(), (int) text.size() - 1 );
        for ( size_t i = 0; i + 1 < text.size(); i++ )
        {
            unsigned char ch = (unsigned char) text[ i ];
            if ( 0 == ch )
                break;
            if ( ch < ' ' || ch > '~' )
            {
                text[ i ] = 0;
                break;
            }
        }
        prf( "%s\n", text.data() );
        return;
    }

    DWORD valuesToPrint = min( head.count, 8u );
    if ( valuesToPrint > 1 )
        prf( "[%u values] ", head.count );

    for ( DWORD i = 0; i < valuesToPrint; i++ )
    {
        if ( i )
            prf( ", " );
        if ( 1 == head.type || 6 == head.type )
            prf( "%u", GetBYTE( valueOffset + i ) );
        else if ( 3 == head.type || 8 == head.type )
            prf( "%u", GetWORD( valueOffset + i * 2, littleEndian ) );
        else if ( 4 == head.type || 9 == head.type )
        {
            DWORD value = GetDWORD( valueOffset + i * 4, littleEndian );
            if ( 9 == head.type )
                prf( "%ld", (LONG) value );
            else
                prf( "%u", value );
        }
        else if ( 5 == head.type || 10 == head.type )
        {
            DWORD numerator = GetDWORD( valueOffset + i * 8, littleEndian );
            DWORD denominator = GetDWORD( valueOffset + i * 8 + 4, littleEndian );
            if ( 10 == head.type )
                prf( "%ld/%ld", (LONG) numerator, (LONG) denominator );
            else
                prf( "%u/%u", numerator, denominator );
        }
        else
        {
            prf( "type %u (%u bytes)", head.type, head.count );
            break;
        }
    }
    if ( valuesToPrint < head.count )
        prf( ", ..." );
    prf( "\n" );
}

void ParseRafCfaRecords( const RafHeader & raf, DWORD fullWidth, DWORD fullHeight,
                         DWORD & imageWidth, DWORD & imageHeight )
{
    if ( 0 == raf.cfaHeaderLength )
    {
        prf( "  RAF CFA metadata records:            not present\n" );
        return;
    }
    if ( raf.cfaHeaderLength < sizeof( DWORD ) )
    {
        prf( "  RAF CFA metadata record block is too short\n" );
        return;
    }

    DWORD recordCount = GetDWORD( raf.cfaHeaderOffset, false );
    __int64 recordOffset = (__int64) raf.cfaHeaderOffset + sizeof( DWORD );
    __int64 recordsEnd = (__int64) raf.cfaHeaderOffset + raf.cfaHeaderLength;
    prf( "  RAF CFA metadata records:            %u\n", recordCount );

    for ( DWORD r = 0; r < recordCount; r++ )
    {
        if ( recordOffset > recordsEnd || recordsEnd - recordOffset < 4 )
        {
            prf( "  RAF CFA metadata record %u has a truncated header\n", r );
            return;
        }

        WORD tag = GetWORD( recordOffset, false );
        WORD size = GetWORD( recordOffset + 2, false );
        recordOffset += 4;
        if ( size > recordsEnd - recordOffset )
        {
            prf( "  RAF CFA metadata record %#x exceeds its declared block\n", tag );
            return;
        }

        const char * tagName = RafRecordName( tag );
        prf( "    %-34s: ", tagName ? tagName : "Undocumented record" );
        if ( !tagName )
            prf( "tag %#x, ", tag );
        PrintRafRecordData( tag, recordOffset, size );
        if ( 0xc000 == tag )
            ParseRafDataImageDimensions( recordOffset, size, fullWidth, fullHeight, imageWidth, imageHeight );
        recordOffset += size;
    }
}

bool ParseRafRawDimensions( const RafHeader & raf, DWORD & rawFullWidth, DWORD & rawFullHeight )
{
    if ( raf.cfaLength < 8 || !IsFileRangeValid( raf.cfaOffset, raf.cfaLength ) )
        return false;

    WORD byteOrder = GetWORD( raf.cfaOffset, true );
    bool littleEndian;
    if ( 0x4949 == byteOrder )
        littleEndian = true;
    else if ( 0x4d4d == byteOrder )
        littleEndian = false;
    else
        return false;

    if ( 42 != GetWORD( (__int64) raf.cfaOffset + 2, littleEndian ) )
        return false;

    DWORD firstIfd = GetDWORD( (__int64) raf.cfaOffset + 4, littleEndian );
    __int64 firstIfdOffset = (__int64) raf.cfaOffset + firstIfd;
    if ( firstIfd > raf.cfaLength || !IsFileRangeValid( firstIfdOffset, 2 ) ||
         firstIfdOffset + 2 > (__int64) raf.cfaOffset + raf.cfaLength )
        return false;

    WORD firstIfdCount = GetWORD( firstIfdOffset, littleEndian );
    if ( firstIfdCount > ( raf.cfaLength - firstIfd - 2 ) / sizeof( IFDHeader ) ||
         !IsFileRangeValid( firstIfdOffset + 2, (__int64) firstIfdCount * sizeof( IFDHeader ) ) )
        return false;

    DWORD fujiIfdOffset = 0;
    for ( WORD i = 0; i < firstIfdCount; i++ )
    {
        __int64 entryOffset = firstIfdOffset + 2 + (__int64) i * sizeof( IFDHeader );
        if ( 0xf000 == GetWORD( entryOffset, littleEndian ) &&
             13 == GetWORD( entryOffset + 2, littleEndian ) &&
             1 == GetDWORD( entryOffset + 4, littleEndian ) )
        {
            fujiIfdOffset = GetDWORD( entryOffset + 8, littleEndian );
            break;
        }
    }

    if ( 0 == fujiIfdOffset || fujiIfdOffset > raf.cfaLength - 2 )
        return false;

    __int64 fujiIfd = (__int64) raf.cfaOffset + fujiIfdOffset;
    WORD fujiIfdCount = GetWORD( fujiIfd, littleEndian );
    if ( fujiIfdCount > ( raf.cfaLength - fujiIfdOffset - 2 ) / sizeof( IFDHeader ) ||
         !IsFileRangeValid( fujiIfd + 2, (__int64) fujiIfdCount * sizeof( IFDHeader ) ) )
        return false;

    DWORD rawWidth = 0;
    DWORD rawHeight = 0;
    for ( WORD i = 0; i < fujiIfdCount; i++ )
    {
        __int64 entryOffset = fujiIfd + 2 + (__int64) i * sizeof( IFDHeader );
        WORD tag = GetWORD( entryOffset, littleEndian );
        WORD type = GetWORD( entryOffset + 2, littleEndian );
        DWORD count = GetDWORD( entryOffset + 4, littleEndian );
        if ( 0xf001 == tag && 4 == type && 1 == count )
            rawWidth = GetDWORD( entryOffset + 8, littleEndian );
        else if ( 0xf002 == tag && 4 == type && 1 == count )
            rawHeight = GetDWORD( entryOffset + 8, littleEndian );
        else
            PrintRafFujiIfdField( tag, entryOffset, littleEndian, raf );
    }

    if ( 0 == rawWidth || 0 == rawHeight )
        return false;

    rawFullWidth = rawWidth;
    rawFullHeight = rawHeight;
    return true;
}

bool AttemptFujifilmParse( RafHeader & raf )
{
    if ( !IsFileRangeValid( 0, sizeof raf ) )
    {
        prf( "truncated RAF header\n" );
        return false;
    }

    memset( &raf, 0, sizeof raf );
    GetBytes( 0, &raf, sizeof raf );
    raf.littleEndian();

    if ( 0 != memcmp( raf.magic, "FUJIFILMCCD-RAW ", 16 ) )
    {
        prf( "invalid Fujifilm RAF magic\n" );
        return false;
    }

    prf( "Fujifilm RAF raw file:\n" );
    prf( "  RAF magic:                          %.*s\n", (int) sizeof raf.magic, raf.magic );
    prf( "  RAF format version:                 %.*s\n", (int) sizeof raf.formatVersion, raf.formatVersion );
    prf( "  RAF camera number ID:               %.*s\n", (int) sizeof raf.cameraNumberID, raf.cameraNumberID );
    prf( "  RAF camera body name:               %.*s\n", (int) sizeof raf.camerabodyname, raf.camerabodyname );
    prf( "  RAF sub-version (ExifTool RAFVersion): %.*s\n", (int) sizeof raf.subVersion, raf.subVersion );
    prf( "  RAF JPEG offset:                     %#x\n", raf.jpgOffset );
    prf( "  RAF JPEG length:                     %#x\n", raf.jpgLength );
    prf( "  RAF CFA header offset:               %#x\n", raf.cfaHeaderOffset );
    prf( "  RAF CFA header length:               %#x\n", raf.cfaHeaderLength );
    prf( "  RAF CFA data offset:                 %#x\n", raf.cfaOffset );
    prf( "  RAF CFA data length:                 %#x\n", raf.cfaLength );

    if ( raf.jpgLength < 16 || !IsFileRangeValid( raf.jpgOffset, raf.jpgLength ) )
    {
        prf( "RAF embedded JPEG range is invalid\n" );
        return false;
    }

    DWORD rawFullWidth = 0, rawFullHeight = 0;
    DWORD rawWidth = 0, rawHeight = 0;
    if ( raf.cfaLength && !IsFileRangeValid( raf.cfaOffset, raf.cfaLength ) )
        prf( "RAF CFA raw-data range is outside the file\n" );
    else if ( ParseRafRawDimensions( raf, rawFullWidth, rawFullHeight ) )
        prf( "  RAF raw image full dimensions:       %u x %u\n", rawFullWidth, rawFullHeight );

    if ( raf.cfaHeaderLength && !IsFileRangeValid( raf.cfaHeaderOffset, raf.cfaHeaderLength ) )
        prf( "RAF CFA metadata record block is outside the file\n" );
    else
        ParseRafCfaRecords( raf, rawFullWidth, rawFullHeight, rawWidth, rawHeight );

    if ( rawWidth && rawHeight )
        prf( "  RAF raw image dimensions:            %u x %u\n", rawWidth, rawHeight );

    return true;
} //AttemptFujifilmParse

#pragma pack( push, 1 )
struct RIFFChunk
{
    char CC[ 4 ];
    DWORD size;
};

struct WebPVP8X
{
    uint8_t flags;
    uint8_t reserved[ 3 ];
    uint8_t widthm1[ 3 ];
    uint8_t heightm1[ 3 ];
};

struct WebPVP8
{
    uint8_t frameTag[ 3 ];
    uint8_t startCode[ 3 ];
    uint16_t width;
    uint16_t height;
};

struct WebPVP8L
{
    uint8_t signature; // 0x2f
    uint32_t metadata; // 31..29  28     27..14  13..0
                       // version alpha  height  width
};

struct WebPAnim
{
    uint8_t b;
    uint8_t g;
    uint8_t r;
    uint8_t a;
    uint16_t loopCount;
};

struct WebPAnmf
{
    uint8_t x[ 3 ];
    uint8_t y[ 3 ];
    uint8_t widthm1[ 3 ];
    uint8_t heightm1[ 3 ];
    uint8_t duration[ 3 ];
    uint8_t flags;
};

#pragma pack(pop)

DWORD WebPRead24( __int64 offset )
{
    return (DWORD) GetBYTE( offset ) |
           ( (DWORD) GetBYTE( offset + 1 ) << 8 ) |
           ( (DWORD) GetBYTE( offset + 2 ) << 16 );
}

void EnumerateWebP()
{
    __int64 length = GetStreamLength();

    prf( "mimetype:                            image/webp\n" );

    if ( !IsFileRangeValid( 0, 12 ) )
    {
        prf( "WebP RIFF header is truncated\n" );
        return;
    }

    DWORD riffSize = GetDWORD( 4, true );
    char riff[ 4 ], form[ 4 ];
    GetBytes( 0, riff, sizeof riff );
    GetBytes( 8, form, sizeof form );
    if ( 0 != memcmp( riff, "RIFF", sizeof riff ) || 0 != memcmp( form, "WEBP", sizeof form ) )
    {
        prf( "invalid RIFF/WEBP signature\n" );
        return;
    }
    if ( riffSize < 4 )
    {
        prf( "invalid RIFF size %u (must include the WEBP form type)\n", riffSize );
        return;
    }

    __int64 riffEnd = (__int64) riffSize + 8;
    if ( riffEnd > length )
    {
        prf( "  (warning: RIFF declares %lld bytes but only %lld are available)\n", riffEnd, length );
        riffEnd = length;
    }
    else if ( riffEnd < length )
        prf( "  trailing bytes after RIFF container: %lld\n", length - riffEnd );

    __int64 offset = 12;
    unsigned int frameCount = 0;
    while ( offset < riffEnd )
    {
        if ( !IsFileRangeValid( offset, sizeof( RIFFChunk ) ) || offset + sizeof( RIFFChunk ) > riffEnd )
        {
            prf( "truncated WebP chunk header at offset %lld\n", offset );
            break;
        }

        RIFFChunk chunk;
        GetBytes( offset, &chunk, sizeof( chunk ) );
        if ( chunk.size > riffEnd - offset - sizeof( chunk ) )
        {
            prf( "chunk %.4s at offset %lld declares %u bytes beyond the RIFF container; stopping\n",
                 chunk.CC, offset, chunk.size );
            break;
        }

        __int64 dataOffset = offset + sizeof( chunk );
        __int64 nextChunkOffset = dataOffset + chunk.size + ( chunk.size & 1 );
        if ( nextChunkOffset > riffEnd )
        {
            prf( "chunk %.4s at offset %lld is missing its required padding byte; stopping\n",
                 chunk.CC, offset );
            break;
        }
        prf( "chunk: %.4s size %u\n", chunk.CC, chunk.size );

        if ( 0 == memcmp( chunk.CC, "VP8X", 4 ) )
        {
            struct WebPVP8X vp8x;
            if ( chunk.size < sizeof( vp8x ) )
            {
                prf( "  (warning: VP8X chunk is too short for its feature header)\n" );
                goto nextWebPChunk;
            }
            GetBytes( dataOffset, &vp8x, sizeof( vp8x ) );
            prf( "  flags:       %#x\n", vp8x.flags );
            if ( ( vp8x.flags & 0xc1 ) || vp8x.reserved[ 0 ] || vp8x.reserved[ 1 ] || vp8x.reserved[ 2 ] )
                prf( "  (warning: reserved VP8X flag or header bits are nonzero)\n" );
            prf( "  Anim exists  %u\n", ( 0 != ( vp8x.flags & 0x2 ) ) ); 
            prf( "  XMP  exists  %u\n", ( 0 != ( vp8x.flags & 0x4 ) ) ); 
            prf( "  EXIF exists  %u\n", ( 0 != ( vp8x.flags & 0x8 ) ) ); 
            prf( "  Alpha exists %u\n", ( 0 != ( vp8x.flags & 0x10 ) ) );   
            prf( "  ICCP exists  %u\n", ( 0 != ( vp8x.flags & 0x20 ) ) );
            DWORD width = 0, height = 0;
            memcpy( &width, vp8x.widthm1, 3 );
            memcpy( &height, vp8x.heightm1, 3 );
            width++;
            height++;
            prf( "  width        %u\n", width );
            prf( "  height       %u\n", height );
        }
        else if ( 0 == memcmp( chunk.CC, "VP8 ", 4 ) )
        {
            struct WebPVP8 vp8;
            if ( chunk.size < sizeof( vp8 ) )
            {
                prf( "  (warning: VP8 chunk is too short for its key-frame header)\n" );
                goto nextWebPChunk;
            }
            GetBytes( dataOffset, &vp8, sizeof( vp8 ) );
            DWORD frameTag = WebPRead24( dataOffset );
            WORD width = GetWORD( dataOffset + 6, true );
            WORD height = GetWORD( dataOffset + 8, true );
            if ( 0 != memcmp( vp8.startCode, "\x9d\x01\x2a", 3 ) || ( frameTag & 1 ) )
                prf( "  (warning: VP8 frame is not a valid key-frame header)\n" );
            prf( "  key frame   %u\n", 0 == ( frameTag & 1 ) );
            prf( "  width       %u (scale %u)\n", width & 0x3fff, width >> 14 );
            prf( "  height      %u (scale %u)\n", height & 0x3fff, height >> 14 );
        }
        else if ( 0 == memcmp( chunk.CC, "VP8L", 4 ) )
        {
            struct WebPVP8L vp8l;
            if ( chunk.size < sizeof( vp8l ) )
            {
                prf( "  (warning: VP8L chunk is too short for its image header)\n" );
                goto nextWebPChunk;
            }
            GetBytes( dataOffset, &vp8l, sizeof( vp8l ) );
            vp8l.metadata = GetDWORD( dataOffset + 1, true );
            prf( "  signature    %#x\n", vp8l.signature );
            prf( "  metadata     %#x\n", vp8l.metadata );
            if ( 0x2f != vp8l.signature || 0 != ( ( vp8l.metadata >> 29 ) & 3 ) )
                prf( "  (warning: invalid VP8L signature or version)\n" );
            prf( "    width      %u\n", 1 + ( vp8l.metadata & 0x3fff ) );
            prf( "    height     %u\n", 1 + ( ( vp8l.metadata >> 14 ) & 0x3fff ) );
            prf( "    alpha      %u\n", ( vp8l.metadata >> 28 ) & 1 );
            prf( "    version    %u\n", ( vp8l.metadata >> 29 ) & 3 );
        }
        else if ( 0 == memcmp( chunk.CC, "EXIF", 4 ) )
        {
            if ( 0 != g_WebP_Exif_Offset )
                prf( "  (warning: multiple EXIF chunks; using the last one)\n" );
            g_WebP_Exif_Offset = dataOffset;
            g_WebP_Exif_Length = chunk.size;
            prf( "  exif offset  %llu\n", g_WebP_Exif_Offset );
            prf( "  exif length  %llu\n", g_WebP_Exif_Length );
        }
        else if ( 0 == memcmp( chunk.CC, "ICCP", 4 ) )
            prf( "  ICC profile: %u bytes\n", chunk.size );
        else if ( 0 == memcmp( chunk.CC, "XMP ", 4 ) )
            prf( "  XMP metadata: %u bytes\n", chunk.size );
        else if ( 0 == memcmp( chunk.CC, "ANIM", 4 ) )
        {
            if ( chunk.size < sizeof( struct WebPAnim ) )
            {
                prf( "  (warning: ANIM chunk is shorter than 6 bytes)\n" );
                goto nextWebPChunk;
            }
            struct WebPAnim anim;
            GetBytes( dataOffset, &anim, sizeof( anim ) );
            prf( "  background color BGRA: %#x %#x %#x %#x\n", anim.b, anim.g, anim.r, anim.a );
            prf( "  loop count:            %u\n", GetWORD( dataOffset + 4, true ) );
        }
        else if ( 0 == memcmp( chunk.CC, "ANMF", 4 ) )
        {
            if ( chunk.size < sizeof( struct WebPAnmf ) )
            {
                prf( "  (warning: ANMF chunk is shorter than 16 bytes)\n" );
                goto nextWebPChunk;
            }
            struct WebPAnmf anmf;
            GetBytes( dataOffset, &anmf, sizeof( anmf ) );

            DWORD x = WebPRead24( dataOffset );
            DWORD y = WebPRead24( dataOffset + 3 );
            DWORD width = WebPRead24( dataOffset + 6 ) + 1;
            DWORD height = WebPRead24( dataOffset + 9 ) + 1;
            DWORD duration = WebPRead24( dataOffset + 12 );
            frameCount++;
            prf( "  frame        %u\n", frameCount );
            prf( "  x            %u\n", x * 2 );
            prf( "  y            %u\n", y * 2 );
            prf( "  width        %u\n", width );
            prf( "  height       %u\n", height );
            prf( "  duration ms  %u\n", duration );
            if ( anmf.flags & 0xfc )
                prf( "  (warning: reserved ANMF flag bits are nonzero: %#x)\n", anmf.flags & 0xfc );
            prf( "  alpha blend  %s\n", ( 0 == ( anmf.flags & 0x02 ) ) ? "yes" : "no" );
            prf( "  dispose      %s\n", ( 0 != ( anmf.flags & 0x01 ) ) ? "background" : "none" );
        }

        nextWebPChunk:
        offset = nextChunkOffset;
    }

    if ( frameCount )
        prf( "animation frames: %u\n", frameCount );
} //EnumerateWebP

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

        if ( g_Heif_Exif_Length < 18 ||
             g_Heif_Exif_Offset < 0 ||
             g_Heif_Exif_Offset > g_pStream->Length() ||
             g_Heif_Exif_Length > g_pStream->Length() - g_Heif_Exif_Offset )
        {
            prf( "invalid Exif item extent in this heif (.heic/.hif) file\n" );
            return;
        }

        prf( "mimetype:                             image/heic\n" );
        DWORD o = GetDWORD( g_Heif_Exif_Offset, false );
        if ( o < 6 || o > (ULONGLONG) g_Heif_Exif_Length - 12 )
        {
            prf( "invalid Exif TIFF-header offset in this heif (.heic/.hif) file\n" );
            return;
        }

        heifOffsetBase = o + g_Heif_Exif_Offset + 4;
        char exifIdentifier[ 6 ];
        GetBytes( g_Heif_Exif_Offset + 4, exifIdentifier, sizeof exifIdentifier );
        if ( 0 != memcmp( exifIdentifier, "Exif\0\0", sizeof exifIdentifier ) )
        {
            prf( "invalid Exif item signature in this heif (.heic/.hif) file\n" );
            return;
        }

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
    else if ( !wcsicmp( pwcExt, L".webp" ) )        // Google's image format
    {
        EnumerateWebP();

        if ( 0 == g_WebP_Exif_Offset )
            return;
    }
    else if ( !wcsicmp( pwcExt, L".wma" ) || !wcsicmp( pwcExt, L".wmv" ) )
    {
        ParseAsf( !wcsicmp( pwcExt, L".wmv" ) ?
                  "video/x-ms-wmv" : "audio/x-ms-wma" );
        return;
    }
    else if ( !wcsicmp( pwcExt, L".wav" ) )
    {
        ParseWav();
        return;
    }
    else if ( !wcsicmp( pwcExt, L".m4a" ) )
    {
    EnumerateM4A( g_pStream );
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
            return;
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
            return;
    }

    if ( ( 0xd8ff     != ( header & 0xffff ) ) &&     // JPG
         ( 0x002a4949 != header ) &&                  // CR2 canon and standard TIF and DNG
         ( 0x2a004d4d != header ) &&                  // NEF nikon (big endian!)
         ( 0x4f524949 != header ) &&                  // ORF olympus
         ( 0x00554949 != header ) &&                  // RW2 panasonic
         ( 0x494a5546 != header ) &&                  // RAF fujifilm
         ( 0x474e5089 != header ) &&                  // PNG
         ( 0x4d42     != ( header & 0xffff ) ) &&     // BMP
         ( 0x66697845 != header ) )                   // RIFF WebP
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

        __int64 pngExifOffset = ParsePNG();
        if ( 0 == pngExifOffset )
            return;
        if ( pngExifOffset > ( __int64 ) MAXDWORD - 8 )
        {
            prf( "PNG Exif data is beyond the supported TIFF offset range\n" );
            return;
        }

        header = GetDWORD( pngExifOffset, true );
        littleEndian = ( 0x002a4949 == header );
        headerBase = (DWORD) pngExifOffset;
        startingOffset = headerBase + 4;
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

        RafHeader raf;
        if ( !AttemptFujifilmParse( raf ) )
            return;

        DWORD jpgSig = GetDWORD( raf.jpgOffset, true );

        if ( 0xd8ff != ( jpgSig & 0xffff ) )
        {
            wprf( L"unexpected JPG header in RAF file: %#x, %ws\n", jpgSig, pwc );
            return;
        }

        if ( raf.jpgLength < (__int64) exifHeaderOffset + 4 )
        {
            prf( "embedded RAF JPEG is too short to contain its EXIF header\n" );
            return;
        }

        prf( "Embedded JPG in RAF:\n" );
        int exifMaybe = ParseOldJpg( raf.jpgOffset, raf.jpgLength );
        DWORD exifSig = GetDWORD( raf.jpgOffset + exifHeaderOffset, true );
        //printf( "jpg exif Sig: %#x\n", exifSig );

        if ( 0x002a4949 != exifSig )
        {
            wprf( L"unexpected EXIF header in RAF file %#x, %ws\n", exifSig, pwc );
            return;
        }

        header = exifSig;
        headerBase = raf.jpgOffset + exifHeaderOffset;
        startingOffset = headerBase + 4;

        g_Embedded_Image_Offset = raf.jpgOffset;
        g_Embedded_Image_Length = raf.jpgLength;

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
        // Canon mirrorless .heif files

        if ( 0 != heifOffsetBase )
        {
            headerBase = heifOffsetBase;
            startingOffset += heifOffsetBase;
        }
    }
    else if ( 0x66697845 == header )                   // RIFF WebP
    {
        DWORD payload = GetDWORD( 8, true );
        if ( strncmp( (char *) &payload, "WEBP", 4 ) )
        {
            printf( "RIFF file isn't a webp... payload %#x\n", payload );
            return;
        }

        if ( 0 == g_WebP_Exif_Offset )
        {
            printf( "WebP image doesn't have EXIF data\n" );
            return;
        }

        headerBase = g_WebP_Exif_Offset + 6; // headerBase should point at the first endian byte (e.g. 0x49)
        startingOffset = headerBase + 4; // the first dword to read with the idf offset is 4 beyond that
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
        EnumerateCanonMakernotes( 0, 8, g_Canon_CR3_Exif_Makernotes_IFD, ( 0x4949 == endian ) );
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
