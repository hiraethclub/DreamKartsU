# Credits

DreamKartsU is an unofficial Wii U port of **DreamKarts** by Rich Stokes.
The game itself (code, models, textures, effects) is Rich's; see
[upstream/CREDITS.md](upstream/CREDITS.md) for the original credits, the
music credits (Alex Morgan, Pixabay Content License) and the licence notices
of KallistiOS, SH4ZAM and Lilita One, all of which still apply.

Port by Aisling (hiraethclub).

## Port dependencies

- [wut](https://github.com/devkitPro/wut) (zlib licence) and
  [wut-tools](https://github.com/devkitPro/wut-tools) (GPL-2.0, build tools
  only, not linked) by the devkitPro team and contributors
- [devkitPPC](https://devkitpro.org/) toolchain (GCC, binutils, newlib)
- [CafeGLSL](https://github.com/Exzap/CafeGLSL) by Exzap: offline GLSL to
  GX2 shader compiler, used at build time only and not shipped. Its own
  additions are MIT; it is built on Mesa (mainly MIT, see its
  `docs/license.rst`) and uses Decaf's libgfd code by exjam
- [SDL2](https://www.libsdl.org/) (zlib licence): the Linux host build
- [SH4ZAM](https://sh4zam.com/) by Falco Girgis and contributors (MIT),
  pinned in `third_party/sh4zam/`, licence in its `LICENSE`
- [dr_mp3](https://github.com/mackron/dr_libs) by David Reid, based on
  minimp3 by lieff (public domain or MIT No Attribution, at your choice):
  plays the music files. `third_party/dr_libs/dr_mp3.h` at commit
  `35b5c392cc15456736c440a68dbe073178bbffe0`, licence at the end of the file

## Licence notices

### wut

```text
Copyright (c) 2015-present devkitPro, wut Authors

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
```

### SDL2

zlib licence, as above, Copyright (C) 1997-2024 Sam Lantinga.

## Trademarks

Chao, Chaos Emeralds and the Dreamcast belong to SEGA. Wii U belongs to
Nintendo. DreamKartsU is an unofficial fan port and is not affiliated with
or endorsed by SEGA or Nintendo.
