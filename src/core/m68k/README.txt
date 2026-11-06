Musashi - Motorola 680x0 emulator by Karl Stenerud (https://github.com/kstenerud/Musashi), MIT licence
(below). Used by core/sndh_player.cpp to run the 68000 player code of Atari ST SNDH tunes.

Taken from the GitHub repository at commit 313ebf1 (March 2026, "Musashi v4.60"):
  m68k.h, m68kcpu.h, m68kcpu.c, m68kconf.h  - the core (line endings LF)
  m68kops.c, m68kops.h                     - opcode handlers and tables, GENERATED once (not part of the build):
      python tools/musashi_68000_only.py <Musashi>/m68k_in.c <dir>/m68k_in.c   (drops the 96 opcode table
          rows and handlers a 68000 does not have - they were illegal instructions on it anyway)
      cl /O2 m68kmake.c          (Musashi's own generator)
      cd <dir> && m68kmake . m68k_in.c
    Generated 1701 opcode handlers from 422 primitives; line endings converted to LF.

FatTracker changes:
  m68kconf.h  - a plain 68000: the 010/020/030/040 and PMMU emulation switched off; interrupt acknowledge
                (ft_sndh_int_ack, the MFP 68901 vectors) and TRAP (ft_sndh_trap, the TOS calls) callbacks
                (marked "FatTracker").
  m68kcpu.h   - SoftFloat is not included: the FPU register type and the softfloat integer typedefs are
                declared in place.
  m68kcpu.c   - m68kfpu.c (68881 FPU, SoftFloat) and m68kmmu.h (PMMU) are left out; their opcodes take the
                line 1111 exception, as on a 68000.
m68kdasm.c (disassembler), m68kfpu.c, m68kmmu.h, softfloat/ and the example/test folders are not used.

Licence (from Musashi's readme.txt):

Copyright © 1998-2001 Karl Stenerud

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
