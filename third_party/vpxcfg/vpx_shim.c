/* BongGIF: C replacements for libvpx's x87 control-word helpers (vpx_ports/float_control_word.asm),
   which need an assembler. On x64 MSVC builds without SIMD the x87 unit is not used by libvpx, so
   these only keep the API: they report and accept the default control word. */
unsigned short vpx_winx64_fstcw(void) { return 0x027F; }
void vpx_winx64_fldcw(unsigned short mode) { (void)mode; }
