/* DKU: upstream src/blobs.S without the SH-4 symbol underscore. DKU_PAK_DIR
   is the folder holding the packs (upstream's, or a converted set). */
#define STR2(x) #x
#define STR(x) STR2(x)
    .section .rodata
    .balign 32
    .global dk_textures_pak
dk_textures_pak:
    .incbin STR(DKU_PAK_DIR/textures.pak)

    .balign 32
    .global dk_sfx_pak
dk_sfx_pak:
    .incbin STR(DKU_PAK_DIR/sfx.pak)
#if defined(__linux__) && defined(__ELF__)
    .section .note.GNU-stack,"",%progbits
#endif
