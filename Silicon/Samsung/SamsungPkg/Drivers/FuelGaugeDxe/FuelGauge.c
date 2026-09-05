#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/PlatformBatteryLib.h>

#include <Protocol/EFIHsI2c.h>
#include <Protocol/EFIFuelGauge.h>

#include "FuelGauge.h"

//
// Global Variables
//
STATIC EFI_HSI2C_PROTOCOL *mHsI2cProtocol = NULL;
STATIC UINT8               mBusNumber     = 0;
STATIC UINT8               mSlaveAddress  = 0;

/**
  Reads one of the 16 Bit Fuel Gauge Registers.
**/
STATIC
EFI_STATUS
FuelGaugeReadReg (
  IN  UINT8   Register,
  OUT UINT16 *Value)
{
  EFI_STATUS Status;
  UINT8      Data[MAX77705_FG_REG_LENGTH];

  // Verify HSI2C Protocol
  if (mHsI2cProtocol == NULL) {
    return EFI_NOT_READY;
  }

  // Read the Register
  Status = mHsI2cProtocol->ReadBuffer (mBusNumber, mSlaveAddress, Register, Data, MAX77705_FG_REG_LENGTH);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Assemble the Little Endian Value
  *Value = (UINT16)((Data[1] << 8) | Data[0]);

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
FuelGaugeGetChargeLevel (OUT UINT32 *Percent)
{
  EFI_STATUS Status;
  UINT16     Value;

  // Verify Percent Parameter
  if (Percent == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  // Get Reported State of Charge
  Status = FuelGaugeReadReg (MAX77705_FG_REG_SOCREP, &Value);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // The high Byte is whole Percent and the low Byte is the Fraction, so the
  // Result comes out in Tenths of a Percent.
  *Percent = (((Value >> 8) * 100) + (((Value & 0xFF) * 100) / 256)) / 10;

  // A Fuel Gauge that has not Learned the Pack yet can Report over Full.
  if (*Percent > MAX77705_FG_CHARGE_FULL) {
    *Percent = MAX77705_FG_CHARGE_FULL;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
FuelGaugeGetVoltage (OUT UINT32 *Voltage)
{
  EFI_STATUS Status;
  UINT16     Value;
  UINT32     Low;
  UINT32     High;

  // Verify Voltage Parameter
  if (Voltage == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  // Get Cell Voltage
  Status = FuelGaugeReadReg (MAX77705_FG_REG_VCELL, &Value);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Scale the low 12 Bits and the top 4 Bits separately, so the Intermediate
  // stays inside 32 Bits. The top Nibble is Shifted back after Scaling.
  Low  = ((UINT32)(Value & 0x0FFF) * MAX77705_FG_VCELL_SCALE) / MAX77705_FG_VCELL_DIVISOR;
  High = ((UINT32)((Value & 0xF000) >> 4) * MAX77705_FG_VCELL_SCALE) / MAX77705_FG_VCELL_DIVISOR;

  *Voltage = Low + (High << 4);

  return EFI_SUCCESS;
}

STATIC EFI_FUEL_GAUGE_PROTOCOL mFuelGauge = {
  FuelGaugeGetChargeLevel,
  FuelGaugeGetVoltage
};

EFI_STATUS
EFIAPI
InitFuelGauge (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS          Status;
  EFI_HSI2C_PROTOCOL *HsI2cProtocol;
  UINT16              Value;

  // Get Battery Data
  EFI_BATTERY_DATA *BatteryData = GetBatteryData ();

  // Verify Battery Data
  if (BatteryData == NULL) {
    return EFI_UNSUPPORTED;
  }

  // Locate HSI2C Protocol
  Status = gBS->LocateProtocol (&gEfiHsI2cProtocolGuid, NULL, (VOID *)&HsI2cProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate HSI2C Protocol! Status = %r\n", Status));
    return Status;
  }

  // Init HSI2C Bus
  Status = HsI2cProtocol->InitBus (BatteryData->BusNumber);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Init HSI2C Bus %u for the Fuel Gauge! Status = %r\n", BatteryData->BusNumber, Status));
    return Status;
  }

  // Save HSI2C Details
  mHsI2cProtocol = HsI2cProtocol;
  mBusNumber     = BatteryData->BusNumber;
  mSlaveAddress  = BatteryData->FuelGaugeSlave;

  // Verify the Fuel Gauge is present.
  //
  // The Status Register is Read purely so the Slave has to Acknowledge before
  // any Protocol is Published. Its Value is not Interpreted here.
  Status = FuelGaugeReadReg (MAX77705_FG_REG_STATUS, &Value);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Probe the Fuel Gauge on Bus %u! Status = %r\n", mBusNumber, Status));

    // Drop the Details again so the Protocol can never be Used Half Init.
    mHsI2cProtocol = NULL;

    return Status;
  }

  // Register Fuel Gauge Protocol
  Status = gBS->InstallProtocolInterface (&ImageHandle, &gEfiFuelGaugeProtocolGuid, EFI_NATIVE_INTERFACE, &mFuelGauge);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Register Fuel Gauge Protocol!\n"));
    return Status;
  }

  return EFI_SUCCESS;
}
