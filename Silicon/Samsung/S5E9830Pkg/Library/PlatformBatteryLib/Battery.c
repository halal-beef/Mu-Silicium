#include <Library/PlatformBatteryLib.h>

STATIC
EFI_BATTERY_DATA
gBatteryData = {
  .BusNumber      = 7,
  .ChargerSlave   = 0x69,
  .FuelGaugeSlave = 0x36
};

EFI_BATTERY_DATA*
GetBatteryData ()
{
  return &gBatteryData;
}
