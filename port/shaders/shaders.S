/* DKU: the GX2 shader groups (port/shaders/*.gsh, built by CafeGLSL from
   the .vert/.frag sources; Makefile.wiiu), linked into the RPX. */
    .section .rodata
#define GSH(sym, file) .balign 256; .global sym; sym: .incbin file
    GSH(dku_gsh_main, "port/shaders/main.gsh")
    GSH(dku_gsh_blit, "port/shaders/blit.gsh")
    GSH(dku_gsh_mod, "port/shaders/mod.gsh")
    GSH(dku_gsh_dark, "port/shaders/dark.gsh")
