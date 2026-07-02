/*
 * Compatibility shim for LK's <stdio.h>.
 * Only printf() is used by the ported CAL/driver code, exclusively for
 * decon_dbg/decon_err/dsim_dbg/... style diagnostics. Route it to the
 * DXE debug log.
 */
#ifndef __COMPAT_STDIO_H__
#define __COMPAT_STDIO_H__

#include <sys/types.h>

VOID
EFIAPI
DeconCompatPrintf (
  IN CONST CHAR8  *Format,
  ...
  );

#define printf(...) DeconCompatPrintf(__VA_ARGS__)

#endif /* __COMPAT_STDIO_H__ */
