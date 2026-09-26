# elf2aif with support for images over 32MB

`elf2aif` converts a statically linked ARM ELF program into a RISC OS
Absolute (AIF, filetype &FF8) file, so it can run without the ELF loader in
!SharedLibs. This is the GCCSDK version with EABI support (`-e`), plus
fixes for large programs. `elf2aif-large-images.diff` shows the changes
against the original.

## The 32MB problem

With `-e`, elf2aif appends a small relocation routine to the end of the
image and makes the second word of the AIF header a `BL` to it. A `BL` can
only reach +/-32MB. With a bigger image, the offset overflowed into the sign
bit, so the branch went backwards into nowhere and the program crashed as
soon as it started. Past 64MB it would also have corrupted the instruction
itself.

## The fix

- The header now branches to a three-word trampoline placed straight after
  the EABI entry code, in the space the ELF headers used to occupy. It jumps
  on to the relocation routine with a PC-relative `ADD PC, PC, R0`, so it
  works for an image of any size. LR is left untouched, so the relocation
  routine returns to the AIF header as before.
- All branches the tool writes are now checked for range, and it stops with
  an error instead of writing a bad instruction.
- It checks that the header, entry code and trampoline end before the
  program's first section (the old check only compared against the entry
  point).
- The read-only size in the header was wrong for images with no read-write
  segment.

Checked with a 40MB test program: the old tool's branch pointed 24MB before
the start of the image, and the new one reaches the relocation routine.
Apart from those words, the output is byte-for-byte the same as before.

## Building

    gcc -O2 -DPACKAGE_STRING='"elf2aif"' \
        -I<gccsdk>/gcc4/riscos/elf2aif/src -o elf2aif elf2aif.c

(the include path provides `elf/common.h` and `elf/external.h`)

## Use

    elf2aif -e openttd openttd,ff8

## Where this copy comes from

Copied from the RISC OS OpenTTD port (github.com/adyoull/riscos-openttd,
`tools/elf2aif`) so riscos-unixlib can build its RISC OS test programs on
its own. The original is GCCSDK's `gcc4/riscos/elf2aif/src/elf2aif.c`
(GPL-2 or later, © GCCSDK Developers); `elf2aif-large-images.diff` is the
change against GCCSDK `64c6f81`. Keep this copy in step with OpenTTD's.
