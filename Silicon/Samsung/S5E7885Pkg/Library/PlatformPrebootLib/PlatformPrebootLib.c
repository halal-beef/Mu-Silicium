#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/IoLib.h>

#include <Protocol/EFIGpio.h>

UINT32
PlatformDetectRevision (VOID)
{
  UINT32     Rev = 0;
  EFI_GPIO_PROTOCOL *gGpio;
  BOOLEAN GpioState;

  EFI_STATUS Status = gBS->LocateProtocol (&gEfiGpioProtocolGuid, NULL, (VOID **)&gGpio);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_WARN, "Fastboot: Exynos GPIO protocol unavailable (%r)\n", Status));
  }
  
  gGpio->GetState (BANK_ID_G, 3, 6, &GpioState);
  Rev |= (GpioState ? 0x1 : 0x0) << 3;

  gGpio->GetState (BANK_ID_G, 4, 1, &GpioState);
  Rev |= (GpioState ? 0x1 : 0x0) << 2;

  gGpio->GetState (BANK_ID_G, 4, 0, &GpioState);
  Rev |= (GpioState ? 0x1 : 0x0) << 1;

  gGpio->GetState (BANK_ID_G, 3, 7, &GpioState);
  Rev |= (GpioState ? 0x1 : 0x0);

  return Rev;
}

VOID
PlatformLastMinuteHandoff(VOID)
{
  // Disable DECON
  MmioWrite32(0x14860070, 0x3070);

  // Disable KASLR
  MmioWrite32(0x80001004, 0x0);
}
