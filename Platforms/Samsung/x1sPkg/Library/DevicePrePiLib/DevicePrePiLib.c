#include <Library/DevicePrePiLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>

#include "libfdt.h"

VOID
DeviceInitialize ()
{
  UINT32 fdtBase = MmioRead32(0x92200000);

  DEBUG((EFI_D_WARN, "\n\n\n\nFDT Base: 0x%x\n", fdtBase));
DEBUG((EFI_D_ERROR, "\n\n\n\nFDT Base: 0x%x\n", fdtBase));

  UINT32 fdt = MmioRead32(0x8A080000);
  DEBUG((EFI_D_WARN, "FDT MAGIC: 0x%x\n", fdt));

  CONST VOID *fdtP = (CONST VOID *)(0x8A080000);

  if (fdt_check_header(fdtP) == 0) DEBUG((EFI_D_WARN, "ITS AN FDT JIM\n"));

  INT32 offset = 0;

      while ((offset = fdt_next_node(fdtP, offset, NULL)) >= 0) {
const char *node_name = fdt_get_name(fdtP, offset, NULL);
DEBUG((EFI_D_WARN, "Node: %s\n", node_name));
  }

//  if(FdtCheckHeader (fdt) == 0) DEBUG((EFI_D_WARN, "ITS AN FDT JIM\n"));

  while(TRUE){}
}
