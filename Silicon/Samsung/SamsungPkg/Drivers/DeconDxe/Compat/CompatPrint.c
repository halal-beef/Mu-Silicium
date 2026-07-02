/*
 * Minimal printf() shim used by the ported lk3rd DPU, DSIM and DPP CAL
 * and driver sources (the decon_dbg / decon_err / dsim_dbg / dpp_dbg
 * family of macros all bottom out in printf()).
 *
 * The ported code only ever passes plain ASCII C strings (format
 * specifier "%s", argument type char pointer) -- never UEFI CHAR16
 * strings -- but EDK2's PrintLib treats "%s" as a CHAR16 pointer and
 * "%a" as a CHAR8 pointer. To avoid silently garbling every debug
 * line, we rewrite "%s" to "%a" in the format string before handing it
 * to AsciiVSPrint(). All other conversions (%d, %u, %x, %X, %p, %c, %%,
 * with width/zero-pad modifiers such as "%08x") are already compatible.
 */
#include <sys/types.h>
#include <Library/DebugLib.h>
#include <Library/PrintLib.h>

#define DECON_PRINTF_BUF_SIZE   256

VOID
EFIAPI
DeconCompatPrintf (
  IN CONST CHAR8  *Format,
  ...
  )
{
  CHAR8    FixedFormat[DECON_PRINTF_BUF_SIZE];
  CHAR8    OutBuf[DECON_PRINTF_BUF_SIZE];
  UINTN    Src;
  UINTN    Dst;
  VA_LIST  Marker;

  Src = 0;
  Dst = 0;
  while (Format[Src] != '\0' && Dst < (DECON_PRINTF_BUF_SIZE - 2)) {
    if (Format[Src] == '%' && Format[Src + 1] == 's') {
      FixedFormat[Dst++] = '%';
      FixedFormat[Dst++] = 'a';
      Src += 2;
    } else {
      FixedFormat[Dst++] = Format[Src++];
    }
  }
  FixedFormat[Dst] = '\0';

  VA_START (Marker, Format);
  AsciiVSPrint (OutBuf, sizeof (OutBuf), FixedFormat, Marker);
  VA_END (Marker);

  DEBUG ((DEBUG_INFO, "%a", OutBuf));
}
