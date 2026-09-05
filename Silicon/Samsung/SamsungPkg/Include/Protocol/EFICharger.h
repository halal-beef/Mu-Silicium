#ifndef _EFI_CHARGER_H_
#define _EFI_CHARGER_H_

//
// Charging Status
//
typedef enum {
  CHARGER_STATUS_UNKNOWN = 0,
  CHARGER_STATUS_CHARGING,
  CHARGER_STATUS_FULL,
  CHARGER_STATUS_NOT_CHARGING,
  CHARGER_STATUS_DISCHARGING
} EFI_CHARGER_STATUS;

//
// Battery Condition, as seen by the Charger rather than the Fuel Gauge
//
typedef enum {
  CHARGER_BATTERY_UNKNOWN = 0,
  CHARGER_BATTERY_NOT_PRESENT,
  CHARGER_BATTERY_OK,
  CHARGER_BATTERY_LOW_VOLTAGE,
  CHARGER_BATTERY_DEAD,
  CHARGER_BATTERY_OVER_VOLTAGE,
  CHARGER_BATTERY_OVER_CURRENT
} EFI_CHARGER_BATTERY_STATE;

/**
  This Function gets the Charging Status.

  @param[out] Status                       - The Charging Status.

  @return EFI_SUCCESS                      - The Status was Read Successfully.
  @return EFI_INVALID_PARAMETER            - The "Status" Parameter is NULL.
  @return EFI_NOT_READY                    - The Charger isn't Init yet.
  @return EFI_DEVICE_ERROR                 - The Charger did not respond.
**/
typedef
EFI_STATUS
(EFIAPI *EFI_CHARGER_GET_STATUS) (
  OUT EFI_CHARGER_STATUS *Status
  );

/**
  This Function gets the Battery Condition reported by the Charger.

  @param[out] State                        - The Battery Condition.

  @return EFI_SUCCESS                      - The State was Read Successfully.
  @return EFI_INVALID_PARAMETER            - The "State" Parameter is NULL.
  @return EFI_NOT_READY                    - The Charger isn't Init yet.
  @return EFI_DEVICE_ERROR                 - The Charger did not respond.
**/
typedef
EFI_STATUS
(EFIAPI *EFI_CHARGER_GET_BATTERY_STATE) (
  OUT EFI_CHARGER_BATTERY_STATE *State
  );

/**
  This Function reports whether a valid Input Supply is Attached.

  @param[out] Present                      - TRUE when Input Power is Valid.

  @return EFI_SUCCESS                      - The State was Read Successfully.
  @return EFI_INVALID_PARAMETER            - The "Present" Parameter is NULL.
  @return EFI_NOT_READY                    - The Charger isn't Init yet.
  @return EFI_DEVICE_ERROR                 - The Charger did not respond.
**/
typedef
EFI_STATUS
(EFIAPI *EFI_CHARGER_IS_INPUT_PRESENT) (
  OUT BOOLEAN *Present
  );

//
// Define Protocol
//
typedef struct {
  EFI_CHARGER_GET_STATUS        GetStatus;
  EFI_CHARGER_GET_BATTERY_STATE GetBatteryState;
  EFI_CHARGER_IS_INPUT_PRESENT  IsInputPresent;
} EFI_CHARGER_PROTOCOL;

#endif /* _EFI_CHARGER_H_ */
