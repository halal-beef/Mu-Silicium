#include <Library/DebugLib.h>

UINT32
PlatformDetectRevision (VOID)
{
  DEBUG ((EFI_D_INFO, "PlatformPrebootLib not implemented for this SoC\n"));
  return 0xFFFFFFFF;
}

VOID
PlatformLastMinuteHandoff(VOID)
{
  DEBUG ((EFI_D_INFO, "PlatformPrebootLib not implemented for this SoC\n"));
}
