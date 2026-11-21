# id

Image Data. A Windows command-line app that shows metadata for image, audio and video files.

I encourage you to use exiftool, not id. It's much better in every possible way. id was a way for me to learn about file formats.

The number of hacks per line of code is staggering. Apple, Microsoft, Leica, Panasonic, Olympus, Canon, Nikon, Ricoh, Fujifilm,
and more conspire to ignore standards and make life difficult.

id can also extract the images embedded in raw, FLAC and MP3 files. These are usually JPG or PNG.

The same source builds a second tool, `imgenai`, which reports whether an image's metadata indicates generative AI use. See [imgenai](#imgenai) below.

## Building

Both tools build with the Microsoft C++ compiler for x64. Run the commands from an x64 Visual Studio Developer Command Prompt (or after running `vcvars64.bat`) in the repository directory. The source is `id.cxx` plus `djl_strm.hxx`. The required Windows libraries are linked with `#pragma comment`.

To build `id.exe`, run `m.bat`, which runs:

```
cl /nologo id.cxx /I.\ /O2i /EHac /Zi /Gy /D_AMD64_ /link /incremental:no /OPT:REF
```

To build `imgenai.exe`, define `IMGENAI_ONLY`. The `/Fo`, `/Fd` and `/PDB` options keep its object and symbol files separate from those of `id.exe`:

```
cl /nologo /D IMGENAI_ONLY id.cxx /I.\ /O2i /EHac /Zi /Gy /Fo:imgenai.obj /Fd:imgenai-vc.pdb /Fe:imgenai.exe /link /incremental:no /OPT:REF /PDB:imgenai.pdb
```

The GitHub Actions workflow builds both and uploads them as the `application` artifact.

## id usage

```
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
      Still-image formats include: JPG, TIFF, PNG, BMP, GIF, SVG, WebP, HEIF/HEIC, AVIF, JP2/J2K, JXL, and camera RAW
      Some non-image formats are supported: FLAC, WAV, MP3, M4A, MP4, MOV, M4V, WMA, WMV
      Some image formats aren't supported yet: presumably many others
      By default, just the first 256 bytes of binary data is displayed. Use -f for all data
      Embedded images may be JPG, PNG, HIF, or some other format
      Fujifilm RAF headers, CFA metadata, raw dimensions, and embedded JPG metadata are parsed
```

## id samples

### JPG

```
parsing input file C:\Users\david\OneDrive\id\07_ilie.jpg
file size:                        61123
mimetype:                             image/jpeg
id marker:                            JFIF
major version:                        1
minor version:                        1
units:                                dots/inch
x density:                           72
y density:                           72
quantization tables:                 1 (16-bit precision: no)
quantization tables:                 1 (16-bit precision: no)
start of frame:                     192 (0xc0) (baseline DCT process frame marker)
bits per sample:                      8
image height:                       700
image width:                        700
color components:                     3 (YCbCr)
compression:                          YCbCr4:4:4 (1 1)
number of components:                 3
```

### Canon CR3

Most of the Canon makernote is omitted where `...` appears.

```
parsing input file C:\Users\david\OneDrive\id\R5_00707.CR3
file size:                     11565650
XMP metadata:
  Rating                : 0
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
exif ExifVersion                    0231
exif DateTimeOriginal:              2020:10:24 16:57:01
exif CreateDate:                    2020:10:24 16:57:01
exif Offset Time:                   -08:00
exif Offset Time Original:          -08:00
exif Offset Time Digital:           -08:00
exif ComponentsConfiguration:       Y, Cb, Cr, -
exif ShutterSpeedValue:             -131072 / 65536 = -2.000000
exif ApertureValue:                 393216 / 65536 = 6.000000 == f / 8.000000
exif ExposureCompensation:          0 / 1 = 0.000000
exif MeteringMode:                  multi-segment (5)
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
canon makernote IFDOffset 0x8 8, NumTags 56, headerBase 0x950 2384
  CameraSettings:
  MacroMode                      : 2 (raw)
  SelfTimer                      : 0 (raw)
  Quality                        : 7 (raw)
  CanonFlashMode                 : 0 (raw)
  ContinuousDrive                : 0 (raw)
  FocusMode                      : 0 (raw)
  RecordMode                     : 12 (raw)
  ...
  AFInfo2:
  AFAreaMode                     : 6
  NumAFPoints                    : 1053
  ValidAFPoints                  : 1
  CanonImageWidth                : 8192
  CanonImageHeight               : 5464
  AFImageWidth                   : 8192
  AFImageHeight                  : 5464
  AF areas (index, width, height, x, y):
    0: 1536, 769, -462, 1014
  CanonMakernote tag 11 ID 40==0x28, type 1, count 16, offset/value 9846:
  ...
Canon CR3 Exif GPS IFD:
GPS IFDOffset 8, NumTags 1
GPS VersionID:                      2.3.0.0
Embedded image found: offset 314368 length 2128644  embedded image header: 0x6008400dbffd8ff; image is likely jpg
```

### JPG with generative AI metadata

This is the [IPTC example image](https://iptc.org/news/iptc-photo-metadata-standard-2025-1-adds-ai-properties/) made with Bing Image Creator. id prints the IPTC source type and what it means. It also notes the C2PA manifest and lists the AI markers found in the manifest text. The hex dump is the IPTC-IIM block in the Photoshop APP13 segment.

```
parsing input file C:\Users\david\OneDrive\imgenai\tests\images\iptc-bing-robot.jpeg
file size:                       148681
mimetype:                             image/jpeg
photoshop irb data:
              0xbe  50 68 6f 74 6f 73 68 6f 70 20 33 2e 30 00 38 42 49 4d 04 04 00 00 00 00 00 b8 1c 02 05 00 1d 47  Photoshop 3.0.8BIM.............G
              0xde  65 6e 41 49 20 52 6f 62 6f 74 20 69 6e 20 67 61 72 64 65 6e 20 65 78 61 6d 70 6c 65 1c 02 37 00  enAI Robot in garden example..7.
              0xfe  08 32 30 32 35 31 31 32 36 1c 02 50 00 00 1c 02 6e 00 20 42 72 65 6e 64 61 6e 20 51 75 69 6e 6e  .20251126..P....n. Brendan Quinn
             0x11e  2f 42 69 6e 67 20 49 6d 61 67 65 20 43 72 65 61 74 6f 72 1c 02 78 00 53 43 75 74 65 20 72 6f 62  /Bing Image Creator..x.SCute rob
             0x13e  6f 74 20 73 69 74 74 69 6e 67 20 61 74 20 61 20 63 61 73 74 2d 69 72 6f 6e 20 74 61 62 6c 65 20  ot sitting at a cast-iron table
             0x15e  69 6e 20 61 20 67 61 72 64 65 6e 20 64 72 61 77 69 6e 67 20 61 20 70 69 63 74 75 72 65 20 69 6e  in a garden drawing a picture in
             0x17e  20 61 20 6e 6f 74 65 62 6f 6f 6b 1c 02 00 00 02 00 04                                             a notebook.......
XMP metadata:
  DateCreated           : 2025-11-26
  Title                 : GenAI Robot in garden example
  Description           : Cute robot sitting at a cast-iron table in a garden drawing a picture in a notebook
  Credit                : Brendan Quinn/Bing Image Creator
  DigitalSourceType:    http://cv.iptc.org/newscodes/digitalsourcetype/trainedAlgorithmicMedia
  AI-use interpretation: created using generative AI (IPTC label)
JPEG: C2PA/JUMBF data block detected; Content Credentials may be present
  The signature is not validated; assertion text is only searched for generative AI markers
  Note: a manifest indicates provenance data, not by itself that AI was used
id marker:                            JFIF
major version:                        1
minor version:                        1
units:                                none
x density:                            1
y density:                            1
quantization tables:                 1 (16-bit precision: no)
quantization tables:                 1 (16-bit precision: no)
start of frame:                     192 (0xc0) (baseline DCT process frame marker)
bits per sample:                      8
image height:                      1248
image width:                        832
color components:                     3 (YCbCr)
compression:                          YCbCr4:2:0 (2 2)
number of components:                 3
  AI generator metadata: C2PA manifest (signature not verified): digitalSourceType trainedAlgorithmicMedia
  AI generator metadata: C2PA manifest (signature not verified): generator Microsoft Responsible AI
  AI generator metadata: C2PA manifest (signature not verified): generator Image Creator from Designer
ImageDescription:                     Cute robot sitting at a cast-iron table in a garden drawing a picture in a notebook
XResolution:                          72 / 1 = 72.000000
YResolution:                          72 / 1 = 72.000000
ResolutionUnit:                       inches (2)
YCbCrPositioning:                     1
```

### FLAC

```
parsing input file C:\Users\david\OneDrive\id\wake.flac
file size:                     31983601
mimetype:                             audio/flac
Stream Info:
  Minimum block size               4096
  Maximum block size               4096
  Minimum frame size                 16
  Maximum frame size              13983
  Sample rate per sec             44100
  Channel count                       2
  Bits per sample                    16
  Samples                      11725896
  Length in min:sec                4:25
  MD5 signature                       6e4748c30445eaee2c09a452becd9066
Comments:
  Vendor                              reference libFLAC 1.3.2 20170101
  Album                               Solid State
  Album Artist                        Jonathan Coulton
  Artist                              Jonathan Coulton
  Encoder                             xACT 2.39
  Title                               Wake Up
  Track                               1
  Total Tracks                        17
  Year                                2017
Picture:
  Picture type                        3 Cover (front)
  mime type                           image/png
  Description
  Width                            2110
  Height                           2110
  Bits per pixel                     32
  Indexed colors                      0
  Image size                    3546480
Embedded image metadata:
file size:                      3546480
mimetype:                             image/png
image width:                       2110
image height:                      2110
bit depth:                            8
color type:                           2 (RGB)
compression method:                   0 (deflate)
filter method:                        0 (none)
interlace method:                     0 (sequential)
sRGB rendering intent:                0 (perceptual)
pixels per unit X:                11811
pixels per unit Y:                11811
unit specifier:                       1 (meters)
resolution:            300.00 x 300.00 dpi
IDAT segments:                      217
IDAT data bytes:                3543797
Embedded image found: offset 786 length 3546480  embedded image header: 0xa1a0a0d474e5089; image is likely png
```

### WAV

The hex dumps are shortened where `...` appears.

```
parsing input file C:\Users\david\OneDrive\id\tmbg.wav
file size:                     50759408
header.size (file size - 8):   50759400
mimetype:                      audio/x-wav
chunk: JUNK, size 92
        0x14  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................................
        ...
chunk: bext, size 602
        0x78  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................................
        ...
chunk: fmt , size 40
  type of format:              1 -- PCM
  channels:                    2
  sample rate (samples/sec):   48000
  dataRate (Bytes/sec):        288000
  blockAlign:                  6
  bits per sample:             24
chunk: minf, size 16
       0x30a  38 f0 6b 95 08 d7 d6 01 02 00 00 00 00 00 00 00                                                  8.k.............
chunk: elm1, size 214
       0x322  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................................
        ...
chunk: data, size 50490390
  data size:                   50490390
  sample count:                8415065
  seconds:                     175.313854
       0x400  01 00 00 fc ff ff 06 00 00 fd ff ff 02 00 00 f9 ff ff fc ff ff f5 ff ff ff ff ff fa ff ff fe ff  ................................
        ...
chunk: regn, size 92
   0x302701e  01 00 00 00 00 00 00 00 00 00 00 2a d2 a4 42 6e e7 43 80 00 00 00 00 00 00 00 00 00 59 67 80 00  ...........*..Bn.C..........Yg..
        ...
chunk: umid, size 24
   0x3027082  00 00 00 00 00 00 00 2a d2 a4 42 6e e7 43 80 00 00 00 00 00 00 00 00 00                          .......*..Bn.C..........
chunk: DGDA, size 267853
   0x30270a2  04 00 00 00 01 00 41 6e 61 6c 79 73 69 73 53 65 74 73 48 64 72 04 00 ea 15 04 00 02 00 00 00 41  ......AnalysisSetsHdr..........A
        ...
```

## imgenai

`imgenai` reuses id's parsers to assess whether an image's metadata indicates generative AI use. It prints one of three results, followed by the evidence it found:

- **Metadata declares generative AI use.** An explicit declaration is present. The declarations are listed below.
- **Metadata records an AI-capable editor.** A software field names an editor that has generative features. This shows only that the app was used, not that its generative features were.
- **Metadata cannot determine whether generative AI was used.** Nothing conclusive was found.

Metadata is easy to remove or forge, so no result rules out generative AI, and missing metadata proves nothing.

```
usage: imgenai [-s:<outputbase>] [-c:<settings.toml>] image
  Assess whether image metadata indicates generative AI may have been used.
  -s:<outputbase>  Write a JPEG copy without recognized AI XMP or C2PA segments.
  -c:<settings.toml>  Run c2patool.exe beside imgenai.exe.
```

The exit code is 0 when the assessment completes, 1 for a usage error, and 2 when the image can't be read or the `-c` or `-s` step fails.

### Sample

```
RESULT: Metadata declares generative AI use.
Metadata evidence:
  DigitalSourceType: http://cv.iptc.org/newscodes/digitalsourcetype/trainedAlgorithmicMedia
  Generative AI declaration: IPTC AI Prompt Information
  Generative AI declaration: IPTC AI Prompt Writer Name
  Generative AI declaration: IPTC AI System Used
  Generative AI declaration: C2PA manifest (signature not verified): digitalSourceType trainedAlgorithmicMedia
  Generative AI declaration: C2PA manifest (signature not verified): generator Microsoft Responsible AI
  Generative AI declaration: C2PA manifest (signature not verified): generator Image Creator from Designer
  JPEG: C2PA/JUMBF data block detected; Content Credentials may be present
Metadata alone cannot rule out generative AI use.
```

### What counts as a declaration

None of these are signature-checked. XMP properties are matched by namespace, and values wrapped in `rdf:Alt`, `rdf:Bag` or `rdf:Seq` are read.

| Source | Field | Counts when |
|---|---|---|
| XMP | `Iptc4xmpExt:DigitalSourceType` | The value is `trainedAlgorithmicMedia` or `compositeWithTrainedAlgorithmicMedia`. The IPTC newscodes prefix can use `http` or `https` or be omitted, and a trailing slash is allowed. |
| XMP | `Iptc4xmpExt:AISystemUsed`, `AISystemVersionUsed`, `AIPromptInformation`, `AIPromptWriterName` | Any non-empty value. These fields were added in IPTC Photo Metadata 2025.1. |
| XMP | `photoshop:Credit` | The value is `Made with Google AI` or `Edited with Google AI`, as Google Photos writes. |
| XMP | `xmp:CreatorTool`, `stEvt:softwareAgent` (edit history) | The value names a generator, such as Midjourney, Firefly, DALL-E, Stable Diffusion, ComfyUI, NovelAI, Ideogram or Google Gemini. |
| XMP, PNG `AIGC` text | TC260 `AIGC`, the implicit label from China's GB 45438-2025 | The label is 1, meaning AI-generated. Labels 2 and 3 (possibly and suspected AI-generated) appear only in `id` output. |
| C2PA manifest | Text in the JUMBF boxes | It contains a generative IPTC source type, or names a generator such as OpenAI, ChatGPT, DALL-E, Adobe Firefly, Microsoft Responsible AI, Image Creator from Designer, Bing Image Creator, Midjourney or Stable Diffusion. Only the text is searched. Use `-c` to validate the signature. |
| PNG text (`tEXt`, `zTXt`, `iTXt`) | Generator keys such as `parameters`, `prompt` and `workflow`, plus `Comment` and `Description` | The value holds generation settings. Recognized writers are Stable Diffusion WebUI and its forks, ComfyUI, InvokeAI, Fooocus and Easy Diffusion. JSON settings like NovelAI's and a Midjourney `Description` with a Job ID also count. Compressed chunks are decompressed, and APNG `comf` chunks are read too. |
| EXIF `UserComment`, GIF and JPEG comments | The comment text | The text holds generation settings. UserComment is decoded from ASCII or from UTF-16 in either byte order. |
| EXIF | `Make`, `Model` | The value holds a ComfyUI graph (`workflow:{` or `prompt:{`). ComfyUI writes these in its WebP and AVIF output. |
| EXIF, PNG | `Software`, PNG `Source` | The value names a generator. |

### AI-capable editors

A software field that names one of these editors gives the AI-capable editor result: Adobe Photoshop, Lightroom and Camera Raw, Topaz, Luminar Neo, ON1 Photo RAW, Canva, Picsart, PhotoDirector, Affinity, ACDSee Photo Studio, Google Photos and Samsung Photo Assist. The software fields are CreatorTool, the edit history, and EXIF or PNG `Software`. Names are heuristic aliases matched as whole words, so "Canvas" doesn't match Canva.

Adobe editors count only from the version that added a generative feature. An Adobe string with no version that can be read counts as AI-capable.

| Editor | First version | Feature |
|---|---|---|
| Photoshop | 22.0 | Neural Filters (Generative Fill arrived in 24.6) |
| Lightroom Classic | 13.3 | Generative Remove |
| Lightroom | 7.3 | Generative Remove |
| Camera Raw | 16.3 | Generative Remove |

Photoshop CS and CC versions, and Lightroom before Lightroom Classic, are treated as predating generative features. The cutoffs are in `ClassifyEditor` in `id.cxx`.

### Evidence that doesn't change the result

These are listed as evidence, but the result stays "cannot determine":

- A C2PA manifest with no AI markers in its text, such as a camera credential.
- A generic source type such as `algorithmicMedia` or `digitalCapture`.
- An editor version older than its generative features.
- A malformed XMP packet whose text mentions an AI term. If the packet breaks after a declaration, the parsed declaration still counts.
- A `dcterms:provenance` reference to a remote C2PA manifest, which isn't fetched.

### Where metadata is read

XMP is read from JPEG (including Extended XMP), TIFF-based raw files, CR3, PNG, WebP, HEIF/AVIF, JPEG 2000 and JPEG XL. C2PA manifests are read from JPEG APP11, PNG `caBX`, WebP `C2PA`, ISO BMFF (HEIF, AVIF, MP4), JPEG 2000 and JPEG XL. A manifest split over several JPEG APP11 segments is reassembled.

These aren't read:
- Brotli-compressed C2PA or JPEG XL metadata boxes.
- Settings kept in sidecar files.
- Data hidden in pixels, such as NovelAI's alpha-channel metadata or SynthID watermarks.

### JPEG metadata copy

```
imgenai -s:C:\temp\clean D:\photos\source.jpg
```

The output is `C:\temp\clean.jpg`. Use a base path in an existing directory and omit the extension. The command won't overwrite an existing file.

The copy drops these segments:
- Standard XMP APP1 packets that declare generative AI, name a generator in CreatorTool or the edit history, or are malformed and mention an AI term. Other fields in a dropped packet are lost with it.
- Extended XMP segments whose packet declares generative AI, or whose standard packet is dropped.
- Recognized embedded C2PA APP11 packets, including genuine camera credentials.

EXIF and comments are kept. A generator named in EXIF `Software`, or settings in EXIF UserComment, remain in the copy. Image scan data is copied without recompression. Files with trailing data are rejected when segments would be removed, since offsets into trailing images could become invalid.

### Verify Content Credentials

Place [c2patool.exe](https://github.com/contentauth/c2patool/releases) beside `imgenai.exe`. Pass a [C2PA settings file](https://opensource.contentauthenticity.org/docs/c2patool/docs/usage/) that contains the trust policy you want.

```
imgenai -c:C:\temp\c2pa.toml D:\photos\source.jpg
```

The command prints the validator's JSON report after the metadata assessment. Check the active manifest's validation state, signature trust, asset-binding result, signed actions and ingredients. The `RESULT:` line reports metadata evidence only, independent of the validation report. A missing or untrusted credential can't establish whether AI was used. `id.exe` doesn't use the verifier.
