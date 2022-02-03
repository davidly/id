# id
Image Data. Windows command line app to show image (and audio) metadata for files.

I encourage you to use exiftool, not id. It's much better in every possible way. ID was a way for me to learn about file formats.

The number of hacks per line of code is staggering. Apple, Microsoft, Leica, Panasonic, Olympus, Canon, Nikon, Ricoh, and more
all conspire to ignore standards and make life difficult.

Usage:

    usage: id [-e:name.jpg] [-f] [filename]
      image data enumeration
           filename       filename of the image or audio file to check
           -f             Full information is displayed (all binary data, all field values, etc.)
           -e:<output>    Extract highest-resolution embedded image (if one exists) to the file specified.

      examples:
          id img_0178.cr2
          id -f img_0178.cr2
          id img_0178.cr2 /e:178.jpg
          id track.flac /e:track.png

      notes:
          Most image formats are supported: CR2, NEF, RW2, DNG, PNG, TIFF, JPG, ARW, HEIC, HIF, CR3
          Some non-image formats are supported: FLAC, WAV, MP3
          Some image formats aren't supported yet: presumably many others
          By default, just the first 256 bytes of binary data is displayed. Use -f for all data
          Embedded images may be JPG, PNG, HIF, or some other format

JPG sample:

    C:\>id 07_ilie.jpg
    parsing input file C:\07_ilie.jpg
    file size: 61123
    mimetype:                             image/jpeg
    id marker:                            JFIF
    major version:                        1
    minor version:                        1
    units:                                dots/inch
    x density:                           72
    y density:                           72
    start of frame:                     192 (0xc0) (baseline DCT process frame marker)
    bits per sample:                      8
    image height:                       700
    image width:                        700
    color components:                     3 (YCbCr)
    compression:                          YCbCr4:4:4 (1 1)
    number of components:                 3

Canon CR3 sample:

    C:\Users\david\OneDrive\id>id R5_00707.CR3
    parsing input file C:\Users\david\OneDrive\id\R5_00707.CR3
    file size: 11565650
    mimetype:                             image/x-canon-cr3
    ImageWidth:                           8192
    ImageHeight:                          5464
    BitsPerSample:                        8, 8, 8
    Compression:                          6 (JPEG (old-style))
    Make:                                 Canon
    Model:                                Canon EOS R5
    Orientation:                          1 (horizontal (normal))
    XResolution:                          72 / 1 = 72.000000
    YResolution:                          72 / 1 = 72.000000
    ResolutionUnit:                       inches (2)
    DateTime:                             2020:10:24 16:57:01
    Artist:
    Copyright:
    Canon CR3 Exif IFD:
    exif IFDOffset 8, NumTags 39
    exif ExposureTime:                  4 / 1 = 4.000000
    exif FNumber:                       8 / 1 = 8.000000
    exif ExposureProgram                manual
    exif ISO                            100
    exif Sensitivity Type               recommended exposure index
    exif Recommended Exposure Index     100
    exif ExifVersion                    4
    exif DateTimeOriginal:              2020:10:24 16:57:01
    exif CreateDate:                    2020:10:24 16:57:01
    exif Offset Time:                   -08:00
    exif Offset Time Original:          -08:00
    exif Offset Time Digital:           -08:00
    exif ComponentsConfiguration:       4 bytes
              0xa2  00 00 00 18                                                                                      ....
    exif ShutterSpeedValue:             -131072 / 65536 = -2.000000
    exif ApertureValue:                 393216 / 65536 = 6.000000
    exif ExposureCompensation:          0 / 1 = 0.000000
    exif MeteringMode:                  5
    exif Flash:                         0 - no flash
    exif FocalLength:                   11 / 1 = 11.000000
    exif User Comment:
    exif SubSecTime:                    83
    exif SubSecTimeOriginal:            83
    exif SubSecTimeDigitized:           83
    exif FlashpixVersion                0x30303130
    exif Color Space                    sRGB
    exif PixelWidth                     8192
    exif PixelHeight                    5464
    exif XResolution:                   8192000 / 1419 = 5773.079634
    exif YResolution:                   5464000 / 947 = 5769.799366
    exif SensorSizeUnit:                2 (inches)
    exif Custom Rendered:               normal
    exif Exposure Mode:                 manual
    exif White Balance:                 auto
    exif Scene Capture Type:            standard
    exif Owner Name:
    exif Body Serial Number:            022021002580
    exif Lens min/max FL and Aperture:  11.00, 24.00, 0.00, 0.00
    exif Lens Model:                    EF11-24mm f/4L USM
    exif Lens Serial Number:            2400000736
    exif (Computed) sensor WxH:         36.042600, 24.053800
    Canon CR3 Makernotes:
    makernote IFDOffset 8==0x8, NumTags 56, headerBase 2384==0x950, effective 0x958
    makernote tag 0 ID 1==0x1, type 3, count 55, offset/value 686
    makernote tag 1 ID 2==0x2, type 3, count 4, offset/value 796
    makernote tag 2 ID 3==0x3, type 3, count 4, offset/value 804
    makernote tag 3 ID 4==0x4, type 3, count 34, offset/value 812
    makernote tag 4 ID 6==0x6, type 2, count 13, offset/value 880 Canon EOS R5
    makernote tag 5 ID 7==0x7, type 2, count 24, offset/value 944 Firmware Version 1.1.0
    makernote tag 6 ID 9==0x9, type 2, count 32, offset/value 968
    makernote tag 7 ID 16==0x10, type 4, count 1, offset/value -2147482591
    makernote tag 8 ID 19==0x13, type 3, count 4, offset/value 1000
    makernote tag 9 ID 25==0x19, type 3, count 1, offset/value 1
    makernote tag 10 ID 38==0x26, type 3, count 4419, offset/value 1008
    makernote tag 11 ID 40==0x28, type 1, count 16, offset/value 9846
    makernote tag 12 ID 50==0x32, type 4, count 11, offset/value 9862
    makernote tag 13 ID 51==0x33, type 4, count 4, offset/value 9906
    makernote tag 14 ID 53==0x35, type 4, count 4, offset/value 9922
    makernote tag 15 ID 56==0x38, type 7, count 76, offset/value 9938
            0x3022  4c 00 00 00 4c 50 2d 45 36 4e 48 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  L...LP-E6NH.....................
            0x3042  00 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................................
            0x3062  00 00 00 00 00 00 00 00 00 00 00 00                                                              ............
    makernote tag 16 ID 63==0x3f, type 4, count 1, offset/value 17
    makernote tag 17 ID 147==0x93, type 3, count 77, offset/value 10014
    makernote tag 18 ID 149==0x95, type 2, count 138, offset/value 10168 EF11-24mm f/4L USM
    makernote tag 19 ID 150==0x96, type 2, count 16, offset/value 10306 FQ0020415
    makernote tag 20 ID 151==0x97, type 7, count 1024, offset/value 10322
            0x31a2  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................................
            0x31c2  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................................
            0x31e2  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................................
            0x3202  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................................
            0x3222  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................................
            0x3242  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................................
            0x3262  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................................
            0x3282  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................................
    makernote tag 21 ID 152==0x98, type 3, count 4, offset/value 11346
    makernote tag 22 ID 153==0x99, type 4, count 178, offset/value 11354
    makernote tag 23 ID 154==0x9a, type 4, count 5, offset/value 12066
    makernote tag 24 ID 160==0xa0, type 3, count 18, offset/value 12086
    makernote tag 25 ID 170==0xaa, type 3, count 6, offset/value 12122
    makernote tag 26 ID 180==0xb4, type 3, count 1, offset/value 1
    makernote tag 27 ID 208==0xd0, type 4, count 1, offset/value 0
    makernote sensor width 8352, height    5586
    makernote left, top, right, bottom:   144 112 8335 5575
    makernote tag 29 ID 16392==0x4008, type 3, count 3, offset/value 12168
    makernote tag 30 ID 16393==0x4009, type 3, count 3, offset/value 12174
    makernote tag 31 ID 16400==0x4010, type 2, count 32, offset/value 12180
    makernote tag 32 ID 16401==0x4011, type 7, count 252, offset/value 12212
            0x3904  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................................
            0x3924  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................................
            0x3944  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................................
            0x3964  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................................
            0x3984  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................................
            0x39a4  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................................
            0x39c4  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................................
            0x39e4  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00              ............................
    makernote tag 33 ID 16402==0x4012, type 2, count 32, offset/value 12464
    makernote tag 34 ID 16403==0x4013, type 4, count 11, offset/value 12496
    makernote tag 35 ID 16406==0x4016, type 4, count 10, offset/value 12540
    makernote tag 36 ID 16408==0x4018, type 4, count 15, offset/value 12580
    makernote tag 37 ID 16409==0x4019, type 7, count 30, offset/value 12640
            0x3ab0  24 00 00 07 36 83 00 0f 2a 00 0f 67 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00        $...6...*..g..................
    makernote tag 38 ID 16416==0x4020, type 4, count 8, offset/value 12670
    makernote tag 39 ID 16417==0x4021, type 4, count 5, offset/value 12702
    makernote tag 40 ID 16421==0x4025, type 4, count 9, offset/value 12722
    makernote tag 41 ID 16423==0x4027, type 4, count 6, offset/value 12758
    makernote tag 42 ID 16424==0x4028, type 4, count 25, offset/value 12782
    makernote tag 43 ID 16428==0x402c, type 4, count 2, offset/value 12882
    makernote tag 44 ID 16433==0x4031, type 7, count 1122, offset/value 12890
            0x3baa  62 04 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00  b...............................
            0x3bca  01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00  ................................
            0x3bea  01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00  ................................
            0x3c0a  01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00  ................................
            0x3c2a  01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00  ................................
            0x3c4a  01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00  ................................
            0x3c6a  01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00  ................................
            0x3c8a  01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00 01 00  ................................
    makernote tag 45 ID 16437==0x4035, type 7, count 556, offset/value 14012
            0x400c  db 07 6c 07 72 07 ee 06 20 05 94 03 dd 02 8c 02 37 02 cf 01 19 0a 73 08 e8 07 07 08 7d 07 3a 06  ..l.r... .......7.....s.....}.:.
            0x402c  7a 05 fd 04 8d 04 92 03 d9 0c ec 07 92 05 2a 05 4a 05 26 05 73 04 a3 03 21 03 9f 02 47 05 80 05  z.............*.J.&.s...!...G...
            0x404c  61 05 09 06 f3 08 ef 0c ba 0f 00 12 9b 14 e3 1a 12 04 f4 04 7b 05 50 05 df 05 2a 08 5e 0a b5 0b  a...................{.P...*.^...
            0x406c  82 0c 8c 13 2f 03 a9 05 ab 08 21 0a 10 0b 57 0c 0b 0f 5e 12 51 16 65 1b d0 01 ef 01 ed 01 19 02  ..../.....!...W...^.Q.e.........
            0x408c  e4 02 d5 03 58 04 a7 04 4d 05 30 07 c3 01 24 02 51 02 38 02 6c 02 1b 03 80 03 8e 03 d1 03 d0 05  ....X...M.0...$.Q.8.l...........
            0x40ac  c0 01 fe 02 41 04 a4 04 ef 04 74 05 3d 06 0e 07 ee 07 fd 07 00 00 0f 00 19 00 17 00 0f 00 1b 00  ....A.....t.=...................
            0x40cc  21 00 1f 00 1b 00 1f 00 00 00 f4 ff e4 ff ee ff 03 00 fb ff 01 00 e8 ff ef ff d7 ff 00 00 d9 ff  !...............................
            0x40ec  db ff dd ff d4 ff d1 ff cd ff d0 ff c0 ff ac ff a0 03 ea 02 b1 02 6d 02 d9 01 36 01 fa 00 d5 00  ......................m...6.....
    makernote tag 46 ID 16439==0x4037, type 7, count 24, offset/value 14568
            0x4238  80 02 aa 01 0a 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00                          ........................
    makernote tag 47 ID 16441==0x4039, type 1, count 16, offset/value 14592
    makernote tag 48 ID 16442==0x403a, type 3, count 256, offset/value 14608
    makernote tag 49 ID 16444==0x403c, type 4, count 3, offset/value 15120
    makernote tag 50 ID 16447==0x403f, type 4, count 3, offset/value 15132
    makernote tag 51 ID 16448==0x4040, type 4, count 10, offset/value 15144
    makernote tag 52 ID 16457==0x4049, type 3, count 4, offset/value 15184
    makernote tag 53 ID 16459==0x404b, type 3, count 2, offset/value -1
    makernote tag 54 ID 16465==0x4051, type 3, count 42, offset/value 15192
    makernote tag 55 ID 16463==0x404f, type 3, count 5, offset/value 15276
    Canon CR3 Exif GPS IFD:
    GPS IFDOffset 8, NumTags 1
    GPS VersionID:                      0x302
    Embedded image found: offset 314368 length 2128644  embedded image header: 0x6008400dbffd8ff; image is likely jpg
