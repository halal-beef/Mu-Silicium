_Head:
  adr   x1, _Payload    /* Set _Entry address */
  b     _Start          /* Jump to the real code */

#if REQUIRES_KERNEL_HEADER == 1
_StackBase:
  .quad FD_BASE         /* Text Offset */

_StackSize:
  .quad FD_SIZE         /* Image Size */
  .quad 0               /* Flags */
  .quad 0               /* Reserved */
  .quad 0               /* Reserved */
  .quad 0               /* Reserved */
  .ascii "ARM\x64"      /* ARM64 Magic */
  .long 0               /* Reserved */
#endif

_Start:
  mov   x4, x1
  ldr   x5, =FD_BASE
  cmp   x4, x5
  beq   _Entry
  ldr   x6,  =FD_SIZE

_CopyLoop:
  ldp   x2, x3, [x4], #16
  stp   x2, x3, [x5], #16
  subs  x6, x6, #16
  b.ne  _CopyLoop

  /* Make the copied FD visible to instruction fetch and to MMU-off data reads */
  ldr   x0, =FD_BASE
  ldr   x1, =FD_SIZE
  add   x1, x0, x1              /* end address */
  mrs   x3, ctr_el0
  ubfx  x3, x3, #16, #4         /* DminLine: log2(words) */
  mov   x2, #4
  lsl   x3, x2, x3              /* D-cache line size in bytes */
  sub   x2, x3, #1
  bic   x4, x0, x2              /* align start down to a line */

_CleanLoop:
  dc    cvac, x4                /* clean to PoC */
  add   x4, x4, x3
  cmp   x4, x1
  b.lo  _CleanLoop

  dsb   ish
  ic    ialluis                 /* drop any stale I-cache lines */
  dsb   ish
  isb

  ldr   x5, =FD_BASE

_Entry:
  br    x5

_Dead:
  b     _Dead

.text
.align 4

_Payload:
