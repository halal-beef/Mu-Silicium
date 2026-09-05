#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/PlatformBatteryLib.h>

#include <Protocol/EFIHsI2c.h>
#include <Protocol/EFICharger.h>

#include "Charger.h"

//
// Global Variables
//
STATIC EFI_HSI2C_PROTOCOL *mHsI2cProtocol = NULL;
STATIC UINT8               mBusNumber     = 0;
STATIC UINT8               mSlaveAddress  = 0;
STATIC EFI_EVENT           mWatchdogEvent = NULL;

/**
  Reads one of the Charger Registers.
**/
STATIC
EFI_STATUS
ChargerReadReg (
  IN  UINT8  Register,
  OUT UINT8 *Value)
{
  // Verify HSI2C Protocol
  if (mHsI2cProtocol == NULL) {
    return EFI_NOT_READY;
  }

  return mHsI2cProtocol->Read (mBusNumber, mSlaveAddress, Register, Value);
}

/**
  Updates the Masked Bits of one of the Charger Registers.
**/
STATIC
EFI_STATUS
ChargerUpdateReg (
  IN UINT8 Register,
  IN UINT8 Mask,
  IN UINT8 Data)
{
  EFI_STATUS Status;
  UINT8      Value;

  // Get current Config
  Status = ChargerReadReg (Register, &Value);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Replace only the Masked Bits
  Value = (Value & ~Mask) | (Data & Mask);

  // Write new Config
  return mHsI2cProtocol->Write (mBusNumber, mSlaveAddress, Register, Value);
}

/**
  Unlocks the Charger Configuration Registers.

  The Charger ignores Writes to its Configuration Registers until the
  Protection Field of CNFG_06 reads back as 0x03, so every Write Path has to
  go through here first or it will silently do nothing.
**/
STATIC
EFI_STATUS
ChargerUnlock ()
{
  EFI_STATUS Status;
  UINT8      Value;

  for (UINT8 Retry = 0; Retry < MAX77705_UNLOCK_RETRIES; Retry++) {
    // Get current Protection State
    Status = ChargerReadReg (MAX77705_CHG_REG_CNFG_06, &Value);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    // Stop once the Charger reports itself Unlocked
    if ((Value & MAX77705_CNFG_06_CHGPROT) == MAX77705_CNFG_06_UNLOCKED) {
      return EFI_SUCCESS;
    }

    // Unlock the Configuration Registers
    Status = ChargerUpdateReg (MAX77705_CHG_REG_CNFG_06, MAX77705_CNFG_06_CHGPROT, MAX77705_CNFG_06_UNLOCKED);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    // Give the Charger Time to Latch it
    gBS->Stall (MAX77705_UNLOCK_DELAY_US);
  }

  return EFI_DEVICE_ERROR;
}

EFI_STATUS
EFIAPI
ChargerKickWatchdog ()
{
  EFI_STATUS Status;

  // Verify HSI2C Protocol
  if (mHsI2cProtocol == NULL) {
    return EFI_NOT_READY;
  }

  // Unlock the Configuration Registers
  Status = ChargerUnlock ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Reload the Charger Watchdog
  return ChargerUpdateReg (MAX77705_CHG_REG_CNFG_06, MAX77705_CNFG_06_WDTCLR, MAX77705_CNFG_06_WDTCLR_KICK);
}

/**
  Restarts the Charge Path after the Charger has Turned it Off.

  Clearing an Expired Watchdog leaves the Charger Off rather than Charging, so
  the Path has to be Cycled to bring it back. Only the Mode Field is Touched.
  Every Current and Voltage Limit is left exactly as the Bootloader Programmed
  it, so this cannot Charge the Battery outside its Configured Envelope.
**/
STATIC
EFI_STATUS
ChargerRestartCharging ()
{
  EFI_STATUS Status;
  UINT8      Value;

  // Unlock the Configuration Registers
  Status = ChargerUnlock ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Get current Mode
  Status = ChargerReadReg (MAX77705_CHG_REG_CNFG_00, &Value);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Leave the Charger alone unless it is Configured to Charge. Anything else
  // means OTG, Boost or UNO is Driving it, and Cycling would Interrupt that.
  if ((Value & MAX77705_CNFG_00_MODE) != MAX77705_MODE_BUCK_CHG_ON) {
    return EFI_UNSUPPORTED;
  }

  // Drop to Buck only
  Status = ChargerUpdateReg (MAX77705_CHG_REG_CNFG_00, MAX77705_CNFG_00_MODE, MAX77705_MODE_BUCK_ON);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Let the Charge Path settle
  gBS->Stall (MAX77705_MODE_CYCLE_DELAY_US);

  // Turn the Charger back on
  return ChargerUpdateReg (MAX77705_CHG_REG_CNFG_00, MAX77705_CNFG_00_MODE, MAX77705_MODE_BUCK_CHG_ON);
}

/**
  Keeps the Charger Watchdog Fed for as long as UEFI is Running.
**/
STATIC
VOID
EFIAPI
ChargerWatchdogTick (
  IN EFI_EVENT Event,
  IN VOID     *Context)
{
  EFI_STATUS Status;
  UINT8      Value;
  UINT8      Input;

  // Reload the Watchdog
  Status = ChargerKickWatchdog ();
  if (EFI_ERROR (Status)) {
    return;
  }

  // Find out whether the Charge Path is Off
  Status = ChargerReadReg (MAX77705_CHG_REG_DETAILS_01, &Value);
  if (EFI_ERROR (Status)) {
    return;
  }

  // Nothing to Restart unless there is a Supply to Charge from. Without one
  // the Charge Path is Off because it should be, and Cycling the Mode every
  // Tick would just be Noise on the Bus.
  Status = ChargerReadReg (MAX77705_CHG_REG_INT_OK, &Input);
  if (EFI_ERROR (Status) || !(Input & MAX77705_CHGIN_OK)) {
    return;
  }

  // Kicking Clears an Expired Watchdog, but it does not bring Charging back by
  // itself, so the Path is Restarted whenever it is found Off.
  //
  // The Charger takes a Moment to leave the Off State, so it will still read
  // Off right after this. The next Tick is what Confirms it Started.
  Value &= MAX77705_CHG_DTLS;

  if (Value == MAX77705_CHG_DTLS_OFF || Value == MAX77705_CHG_DTLS_OFF_WDT) {
    ChargerRestartCharging ();
  }
}

EFI_STATUS
EFIAPI
ChargerGetStatus (OUT EFI_CHARGER_STATUS *Status)
{
  EFI_STATUS Result;
  UINT8      Value;

  // Verify Status Parameter
  if (Status == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  // Get Charger Details
  Result = ChargerReadReg (MAX77705_CHG_REG_DETAILS_01, &Value);
  if (EFI_ERROR (Result)) {
    return Result;
  }

  // Isolate the Charging State
  Value = (Value & MAX77705_CHG_DTLS) >> MAX77705_CHG_DTLS_SHIFT;

  // Map the State the same way the Vendor Driver does
  switch (Value) {
    case MAX77705_CHG_DTLS_PRECHARGE:
    case MAX77705_CHG_DTLS_FAST_CC:
    case MAX77705_CHG_DTLS_FAST_CV:
      *Status = CHARGER_STATUS_CHARGING;
      break;

    case MAX77705_CHG_DTLS_TOP_OFF:
    case MAX77705_CHG_DTLS_DONE:
      *Status = CHARGER_STATUS_FULL;
      break;

    case MAX77705_CHG_DTLS_OFF_TIMER:
    case MAX77705_CHG_DTLS_OFF_SUSP:
    case MAX77705_CHG_DTLS_OFF_INPUT:
      *Status = CHARGER_STATUS_NOT_CHARGING;
      break;

    case MAX77705_CHG_DTLS_OFF:
    case MAX77705_CHG_DTLS_OFF_JEITA:
    case MAX77705_CHG_DTLS_OFF_WDT:
      *Status = CHARGER_STATUS_DISCHARGING;
      break;

    default:
      *Status = CHARGER_STATUS_UNKNOWN;
      break;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
ChargerGetBatteryState (OUT EFI_CHARGER_BATTERY_STATE *State)
{
  EFI_STATUS Result;
  UINT8      Value;

  // Verify State Parameter
  if (State == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  // Get Charger Details
  Result = ChargerReadReg (MAX77705_CHG_REG_DETAILS_01, &Value);
  if (EFI_ERROR (Result)) {
    return Result;
  }

  // Isolate the Battery State
  Value = (Value & MAX77705_BAT_DTLS) >> MAX77705_BAT_DTLS_SHIFT;

  // Map the State the same way the Vendor Driver does
  switch (Value) {
    case MAX77705_BAT_DTLS_NO_BATTERY:
      *State = CHARGER_BATTERY_NOT_PRESENT;
      break;

    case MAX77705_BAT_DTLS_LOW_VPQLB:
    case MAX77705_BAT_DTLS_LOW:
      *State = CHARGER_BATTERY_LOW_VOLTAGE;
      break;

    case MAX77705_BAT_DTLS_DEAD:
      *State = CHARGER_BATTERY_DEAD;
      break;

    case MAX77705_BAT_DTLS_GOOD:
      *State = CHARGER_BATTERY_OK;
      break;

    case MAX77705_BAT_DTLS_OVP:
      *State = CHARGER_BATTERY_OVER_VOLTAGE;
      break;

    case MAX77705_BAT_DTLS_OCP:
      *State = CHARGER_BATTERY_OVER_CURRENT;
      break;

    default:
      *State = CHARGER_BATTERY_UNKNOWN;
      break;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
ChargerIsInputPresent (OUT BOOLEAN *Present)
{
  EFI_STATUS Result;
  UINT8      Value;

  // Verify Present Parameter
  if (Present == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  // Get Interrupt Status
  Result = ChargerReadReg (MAX77705_CHG_REG_INT_OK, &Value);
  if (EFI_ERROR (Result)) {
    return Result;
  }

  // CHGIN_OK is set only while the Input is present and within Limits, so it
  // already excludes both the Under and the Over Voltage Cases.
  *Present = (BOOLEAN)((Value & MAX77705_CHGIN_OK) != 0);

  return EFI_SUCCESS;
}

STATIC EFI_CHARGER_PROTOCOL mCharger = {
  ChargerGetStatus,
  ChargerGetBatteryState,
  ChargerIsInputPresent
};

EFI_STATUS
EFIAPI
InitCharger (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS          Status;
  EFI_HSI2C_PROTOCOL *HsI2cProtocol;
  UINT8               Value;

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
    DEBUG ((EFI_D_ERROR, "Failed to Init HSI2C Bus %u for the Charger! Status = %r\n", BatteryData->BusNumber, Status));
    return Status;
  }

  // Save HSI2C Details
  mHsI2cProtocol = HsI2cProtocol;
  mBusNumber     = BatteryData->BusNumber;
  mSlaveAddress  = BatteryData->ChargerSlave;

  // Verify the Charger is present.
  //
  // This only has to Acknowledge. Nothing is Interpreted from the Value, and
  // no Unlock is needed to Read.
  Status = ChargerReadReg (MAX77705_CHG_REG_INT_OK, &Value);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Probe the Charger on Bus %u! Status = %r\n", mBusNumber, Status));

    // Drop the Details again so the Protocol can never be Used Half Init.
    mHsI2cProtocol = NULL;

    return Status;
  }

  // Kick the Charger Watchdog once, the same way the Vendor Driver does at the
  // end of its Probe, then Restart the Charge Path if the Watchdog had already
  // Turned it Off before UEFI got Here.
  ChargerWatchdogTick (NULL, NULL);

  // Keep Feeding the Watchdog. Without this the Charger Stops Charging a short
  // while into UEFI and the Battery Drains even with a Charger Attached.
  Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, ChargerWatchdogTick, NULL, &mWatchdogEvent);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Create the Charger Watchdog Event! Status = %r\n", Status));
  } else {
    Status = gBS->SetTimer (mWatchdogEvent, TimerPeriodic, MAX77705_WDT_KICK_INTERVAL);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "Failed to Start the Charger Watchdog Timer! Status = %r\n", Status));
      gBS->CloseEvent (mWatchdogEvent);
      mWatchdogEvent = NULL;
    }
  }

  // Register Charger Protocol
  Status = gBS->InstallProtocolInterface (&ImageHandle, &gEfiChargerProtocolGuid, EFI_NATIVE_INTERFACE, &mCharger);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Register Charger Protocol!\n"));
    return Status;
  }

  return EFI_SUCCESS;
}
