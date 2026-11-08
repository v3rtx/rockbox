#include "config.h"

ENTRY(__start)
#ifdef ROCKBOX_LITTLE_ENDIAN
OUTPUT_FORMAT(elf32-littlearm)
#else
OUTPUT_FORMAT(elf32-bigarm)
#endif
OUTPUT_ARCH(arm)
STARTUP(target/arm/rk27xx/crt0.o)

#define PLUGINSIZE PLUGIN_BUFFER_SIZE
#define CODECSIZE CODEC_SIZE

#define DRAMORIG 0x60000000
#define DRAMSIZE (MEMORYSIZE * 0x100000) - PLUGINSIZE - CODECSIZE

#define IRAMORIG 0x00000000
#define IRAMSIZE 4K

/* End of the audio buffer, where the codec buffer starts */
#define ENDAUDIOADDR  (DRAMORIG + DRAMSIZE)
#define CODECORIG (ENDAUDIOADDR)

/* .init is copied to codec buffer */
#define INITORIG CODECORIG
#define INITSIZE CODECSIZE

/* Where the codec buffer ends, and the plugin buffer starts */
#define ENDADDR (ENDAUDIOADDR + CODECSIZE)

MEMORY
{
    DRAM  : ORIGIN = DRAMORIG, LENGTH = DRAMSIZE
    IRAM  : ORIGIN = IRAMORIG, LENGTH = IRAMSIZE
    INIT  : ORIGIN = INITORIG, LENGTH = INITSIZE
}

SECTIONS
{
  loadaddress = DRAMORIG;

  .text : {
    *(.text*)
    *(.icode*)
    *(.glue_7*)
  } > DRAM

  .rodata : {
    *(.rodata*)
    *(.irodata*)
    . = ALIGN(0x4);
  } > DRAM

  .data : {
    *(.data*)
    *(.idata*)
    . = ALIGN(0x4);
  } > DRAM

  /* NOLOAD sections at end start here. Any part of the binary image past
   * this point is discarded or copied elsewhere. */
  _noloaddram = .;

  .intvect : {
    _intvectstart = . ;
    KEEP(*(.intvect))
    _intvectend = . ;
  } > IRAM AT > DRAM
  _intvectcopy = LOADADDR(.intvect);
  
  .init : {
    . = ALIGN(4);
    _initstart = .;
    *(.init*)
    *(.initdata*)
    _initend = .;
  } > INIT AT > DRAM
  _initcopy = LOADADDR(.init);

  .stack _noloaddram (NOLOAD) :
  {
     *(.stack)
     _stackbegin = .;
     stackbegin = .;
#ifdef COLORFLY_C4_PRO
     /* C4: the multi-volume (NAND+SD) + database (tagtree recursion -> tagcache ->
      * SD storage) main-thread call chain overflows the stock 8K stack -> data abort
      * in sd_write_sectors (str to a stack local past stackend, T:main). 32K. */
     . += 0x8000;
#else
     . += 0x2000;
#endif
     _stackend = .;
     stackend = .;
     _irqstackbegin = .;
     . += 0x400;
     _irqstackend = .;
     _fiqstackbegin = .;
     . += 0x400;
     _fiqstackend = .;
     /* Dedicated ABORT/UNDEF-mode stack (crt0.S). The fault handler UIE runs in
      * abort mode and paints the fault frame; sharing the 1 KB IRQ stack (which
      * already holds the interrupted IRQ frames) overflowed and double-faulted,
      * so give it its own 4 KB. */
     _abortstackbegin = .;
     . += 0x1000;
     _abortstackend = .;
  } > DRAM

  .bss (NOLOAD) : {
     _edata = .;
     *(.bss*);
     *(.ibss);
     *(COMMON);
    . = ALIGN(0x4);
     _end = .;
  } > DRAM

  .audiobuf (NOLOAD) :
  {
      . = ALIGN(4);
      _audiobuffer = .;
      audiobuffer = .;
  } > DRAM

  .audiobufend ENDAUDIOADDR (NOLOAD) :
  {
      audiobufend = .;
      _audiobufend = .;
  } > DRAM

  .codec CODECORIG (NOLOAD) :
  {
      codecbuf = .;
      _codecbuf = .;
  } > DRAM

  .plugin ENDADDR (NOLOAD) :
  {
      _pluginbuf = .;
      pluginbuf = .;
  }

}
