# id
Image Data. Windows command line app to show image (and audio) data for files.

I encourage you to use exiftool, not id. It's much better in every possible way. ID was a way for me to learn about file formats.

Usage:

    usage: id [-e:name.jpg] [-f] [filename]
      image data enumeration
           filename       filename of the image to check
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

Sample:

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
