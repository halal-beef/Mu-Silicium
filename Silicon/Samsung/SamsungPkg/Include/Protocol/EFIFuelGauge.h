#ifndef _EFI_FUEL_GAUGE_H_
#define _EFI_FUEL_GAUGE_H_

/**
  This Function gets the Battery Charge Level.

  @param[out] Percent                      - The Charge Level, in Tenths of a
                                             Percent. 1000 is a Full Battery.

  @return EFI_SUCCESS                      - The Level was Read Successfully.
  @return EFI_INVALID_PARAMETER            - The "Percent" Parameter is NULL.
  @return EFI_NOT_READY                    - The Fuel Gauge isn't Init yet.
  @return EFI_DEVICE_ERROR                 - The Fuel Gauge did not respond.
**/
typedef
EFI_STATUS
(EFIAPI *EFI_FUEL_GAUGE_GET_CHARGE_LEVEL) (
  OUT UINT32 *Percent
  );

/**
  This Function gets the Battery Terminal Voltage.

  @param[out] Voltage                      - The Voltage, in Millivolts.

  @return EFI_SUCCESS                      - The Voltage was Read Successfully.
  @return EFI_INVALID_PARAMETER            - The "Voltage" Parameter is NULL.
  @return EFI_NOT_READY                    - The Fuel Gauge isn't Init yet.
  @return EFI_DEVICE_ERROR                 - The Fuel Gauge did not respond.
**/
typedef
EFI_STATUS
(EFIAPI *EFI_FUEL_GAUGE_GET_VOLTAGE) (
  OUT UINT32 *Voltage
  );

//
// Define Protocol
//
typedef struct {
  EFI_FUEL_GAUGE_GET_CHARGE_LEVEL GetChargeLevel;
  EFI_FUEL_GAUGE_GET_VOLTAGE      GetVoltage;
} EFI_FUEL_GAUGE_PROTOCOL;

#endif /* _EFI_FUEL_GAUGE_H_ */
