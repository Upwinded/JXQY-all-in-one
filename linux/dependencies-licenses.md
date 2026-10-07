# Linux dependency license manifest

`linux/build-dependencies.sh` copies the complete notices listed below into
`ThirdParty/devel/linux/x86_64/licenses/`. That generated directory remains
ignored by Git together with its headers and binaries.

| Component | Version | License/notices copied from source |
| --- | --- | --- |
| SDL | 3.4.10 | `LICENSE.txt`, HIDAPI notices, yuv2rgb notice |
| SDL_image | 3.2.4 | `LICENSE.txt`, embedded stb_image MIT/public-domain notice |
| SDL_ttf | 3.2.2 | `LICENSE.txt` |
| FreeType | 2.13.2 | `LICENSE.TXT`, `docs/FTL.TXT` |
| SDL_mixer | 3.2.4 | `LICENSE.txt`, dr_libs notice, stb_vorbis MIT/public-domain notice, Timidity notice |
| FFmpeg | 5.1.2 | `LICENSE.md`, `COPYING.LGPLv2.1` |

The build disables GPL components and external FFmpeg libraries. SDL_image
uses its embedded stb backend. SDL_mixer uses its embedded decoders. FreeType
is linked statically into SDL_ttf; HarfBuzz and plutosvg are disabled. The
resulting shared-library closure contains only the requested private libraries
plus normal Linux system runtime libraries.
