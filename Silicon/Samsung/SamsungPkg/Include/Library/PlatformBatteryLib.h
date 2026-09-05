#ifndef _PLATFORM_BATTERY_LIB_H_
#define _PLATFORM_BATTERY_LIB_H_

//
// Battery Data
//
// The Charger and the Fuel Gauge live on the same Chip and the same HSI2C Bus,
// but answer on different Slave Addresses, so they are Described together and
// Consumed by separate Drivers.
//
typedef struct {
  UINT8 BusNumber;
  UINT8 ChargerSlave;
  UINT8 FuelGaugeSlave;
} EFI_BATTERY_DATA;

/**
  This Function gets the Platform Battery Data.

  @return Data                             - The Battery Data.
  @return NULL                             - There is no Battery Data.
**/
EFI_BATTERY_DATA*
GetBatteryData ();

#endif /* _PLATFORM_BATTERY_LIB_H_ */
