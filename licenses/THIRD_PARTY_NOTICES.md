# Third-party runtime inventory

This file maps the runtime binaries kept in `bin/` to the license texts in
this directory. It is an inventory, not a replacement for those license
texts.

## SDL3 runtime

| Runtime files | Bundled version | License text |
| --- | --- | --- |
| `SDL3.dll` | 3.4.10 | `SDL/README-SDL.txt` |
| `SDL3_image.dll` | 3.2.4 | `SDL/README-sdl_image.txt` |
| `SDL3_ttf.dll` | 3.2.2 | `SDL/README-sdl_ttf.txt`, `SDL/LICENSE.freetype.txt` |
| `SDL3_mixer.dll` | 3.2.4 | `SDL/README-sdl_mixer.txt` |

The checked-in Win32 DLLs are byte-identical to the matching x86 files in
`ThirdParty/devel/win/SDL`. The following optional decoder libraries are
loaded by name by SDL_image/SDL_mixer or are direct dependencies of those
decoder DLLs, and use the corresponding bundled license text:

| Runtime files | License text |
| --- | --- |
| `libavif-16.dll` (includes AOM and dav1d) | `SDL/LICENSE.avif.txt`, `SDL/LICENSE.aom.txt`, `SDL/LICENSE.dav1d.txt` |
| `libogg-0.dll` | `SDL/LICENSE.ogg-vorbis.txt` |
| `libopus-0.dll` | `SDL/LICENSE.opus.txt` |
| `libopusfile-0.dll` | `SDL/LICENSE.opusfile.txt` |
| `libtiff-6.dll` | `SDL/LICENSE.tiff.txt` |
| `libwavpack-1.dll` | `SDL/LICENSE.wavpack.txt` |
| `libwebp-7.dll`, `libwebpdemux-2.dll` | `SDL/LICENSE.webp.txt` |
| `libxmp.dll` | `SDL/LICENSE.xmp.txt` |

The upstream dependency bundle also contains `libgme.dll`, but this project
does not distribute or deploy it: no GME/chiptune resource sample is present,
and enabling that LGPL component requires a separate compliance design.

## FFmpeg runtime

The checked-in Win32 `avcodec-59.dll`, `avformat-59.dll`, `avutil-57.dll`,
`swresample-4.dll`, and `swscale-6.dll` are covered by
`ffmpeg/LICENSE.md` and the applicable LGPL text in `ffmpeg/`. The platform
projects link the matching import libraries supplied under the local dependency
bundles. `COPYING.GPLv3` is retained as an upstream FFmpeg license reference;
its presence does not state that every bundled FFmpeg binary enabled GPL code.

## Microsoft Visual C++ runtime

The checked-in Win32 `msvcp140*.dll`, `vcruntime140*.dll`, `concrt140.dll`,
`vcamp140.dll`, `vccorlib140.dll`, and `vcomp140.dll` report product version
14.51.36231.0 and originate from the Microsoft Visual C++ Redistributable
installed with the active Visual Studio toolchain. Their redistribution is
governed by the Microsoft Visual Studio license terms.

## Other bundled source dependencies

The remaining source dependencies use the texts in `inih/`, `lua/`,
`miniLzo/`, and `iconv/`. The project-specific license is `license.txt`.
