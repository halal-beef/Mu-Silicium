#include <Library/BaseLib.h>
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

        if (node_name && AsciiStrStr(node_name, "decon_f")) {

	    DEBUG((EFI_D_WARN, "Node: %s\n", node_name));
            int len;
            const fdt32_t *reg = fdt_getprop(fdtP, offset, "reg", &len);
            if (reg && len > 0) {
                int num_regs = len / sizeof(fdt32_t);
                for (int i = 0; i < num_regs; i++) {
                    DEBUG((EFI_D_ERROR, "0x%08x\n", fdt32_to_cpu(reg[i])));
                }
            }
}
}
//  if(FdtCheckHeader (fdt) == 0) DEBUG((EFI_D_WARN, "ITS AN FDT JIM\n"));

  while(TRUE){}
}
