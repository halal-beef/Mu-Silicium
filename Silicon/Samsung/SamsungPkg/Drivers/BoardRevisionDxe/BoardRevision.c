#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include <Protocol/EFIAdc.h>
#include <Protocol/EFIGpio.h>

STATIC EFI_ADC_PROTOCOL *mAdcProtocol;
STATIC EFI_GPIO_PROTOCOL *mGpioProtocol;

UINT32 gRevisionVoltages[] =
{
    136,
    409,
    682,
    955,
    1228,
    1501,
    1774,
    2047,
    2321,
    2594,
    2867,
    3140,
    3413,
    3686,
    3959
};

UINT32
GetBoardRevision (VOID)
{
  INTN Voltage = -1;
  INTN AdcIndex = 0;
  BOOLEAN GpioState = FALSE;
  UINT32 Revision = 0;

  mGpioProtocol->SetPinPull(BANK_ID_P, 5, 5, PULL_NONE);
  mGpioProtocol->GetPin(BANK_ID_P, 5, 5, &GpioState);

  if (GpioState)
    Revision |= (1 << 4);

  Voltage = mAdcProtocol->ReadRaw(6);
  if (Voltage < 0)
  {
    DEBUG ((EFI_D_ERROR, "Failed to read board revision voltage from ADC!\n"));
    return 0;
  }

  while (Voltage > gRevisionVoltages[AdcIndex] && AdcIndex < 14)
  {
    AdcIndex++;
  }

  Revision |= AdcIndex;

  return Revision;
}

EFI_STATUS
EFIAPI
BoardRevisionInit (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;

  Status = gBS->LocateProtocol (&gEfiAdcProtocolGuid, NULL, (VOID *)&mAdcProtocol);
  if (EFI_ERROR (Status))
  {
    DEBUG ((EFI_D_ERROR, "Failed to Locate ADC Protocol! Status = %r\n", Status));
    return Status;
  }

  Status = gBS->LocateProtocol (&gEfiGpioProtocolGuid, NULL, (VOID *)&mGpioProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate GPIO Protocol! Status = %r\n", Status));
    return Status;
  }

  DEBUG((EFI_D_ERROR, "\n\n\nBoard Revision: %d\n", GetBoardRevision()));

  return EFI_SUCCESS;
}