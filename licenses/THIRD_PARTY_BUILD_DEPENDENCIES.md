# Third-party build dependencies

This inventory describes the prebuilt development files published in the
`thirdparty` release. It supplements, but does not
replace, the accompanying upstream license texts.

## Windows

| Component | Bundled version |
| --- | --- |
| SDL | 3.4.10 |
| SDL_image | 3.2.4 |
| SDL_ttf | 3.2.2 |
| SDL_mixer | 3.2.4 |
| FFmpeg development headers | 5.1.2 |
| FFmpeg x86 runtime | 5.1.2 |
| FFmpeg x64 runtime | 5.1.3, ABI 59 |

The FFmpeg x64 runtime reports `--enable-version3`; both LGPL v2.1 and LGPL
v3 texts are included. The bundle does not claim that every upstream FFmpeg
license option or optional codec is enabled.

## Android

| Component | Bundled version |
| --- | --- |
| SDL | 3.4.10 |
| SDL_image | 3.2.6 |
| SDL_ttf | 3.2.2 |
| SDL_mixer | 3.2.4 |
| FFmpeg | 5.1.2 |

The Android bundle contains `arm64-v8a` and `x86_64` FFmpeg libraries and the
SDL Android Archive packages consumed by the Gradle project.

## License locations

- SDL and bundled decoder notices: `licenses/SDL/`
- FFmpeg licensing overview: `licenses/ffmpeg/LICENSE.md`
- FFmpeg LGPL texts: `licenses/ffmpeg/COPYING.LGPLv2.1` and
  `licenses/ffmpeg/COPYING.LGPLv3`
- FFmpeg GPL v3 reference retained from the upstream source distribution:
  `licenses/ffmpeg/COPYING.GPLv3`
