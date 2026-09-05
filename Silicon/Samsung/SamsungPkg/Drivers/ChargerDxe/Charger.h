#ifndef _CHARGER_H_
#define _CHARGER_H_

//
// MAX77705 Charger Register Addresses
//
// Unlike the Fuel Gauge Block, every Charger Register is a single Byte.
//
#define MAX77705_CHG_REG_INT_OK      0xB2
#define MAX77705_CHG_REG_DETAILS_00  0xB3
#define MAX77705_CHG_REG_DETAILS_01  0xB4
#define MAX77705_CHG_REG_DETAILS_02  0xB5
#define MAX77705_CHG_REG_CNFG_00     0xB7
#define MAX77705_CHG_REG_CNFG_06     0xBD

//
// INT_OK Bits
//
#define MAX77705_CHG_OK              BIT4
#define MAX77705_CHGIN_OK            BIT6

//
// DETAILS_00 Fields
//
#define MAX77705_CHGIN_DTLS          (BIT5 | BIT6)
#define MAX77705_CHGIN_DTLS_SHIFT    5

//
// DETAILS_01 Fields
//
#define MAX77705_CHG_DTLS            (BIT0 | BIT1 | BIT2 | BIT3)
#define MAX77705_CHG_DTLS_SHIFT      0
#define MAX77705_BAT_DTLS            (BIT4 | BIT5 | BIT6)
#define MAX77705_BAT_DTLS_SHIFT      4

//
// CHG_DTLS Values
//
#define MAX77705_CHG_DTLS_PRECHARGE  0x00
#define MAX77705_CHG_DTLS_FAST_CC    0x01
#define MAX77705_CHG_DTLS_FAST_CV    0x02
#define MAX77705_CHG_DTLS_TOP_OFF    0x03
#define MAX77705_CHG_DTLS_DONE       0x04
#define MAX77705_CHG_DTLS_OFF_TIMER  0x05
#define MAX77705_CHG_DTLS_OFF_SUSP   0x06
#define MAX77705_CHG_DTLS_OFF_INPUT  0x07
#define MAX77705_CHG_DTLS_OFF        0x08
#define MAX77705_CHG_DTLS_OFF_JEITA  0x0A
#define MAX77705_CHG_DTLS_OFF_WDT    0x0B

//
// BAT_DTLS Values
//
#define MAX77705_BAT_DTLS_NO_BATTERY 0x00
#define MAX77705_BAT_DTLS_LOW_VPQLB  0x01
#define MAX77705_BAT_DTLS_DEAD       0x02
#define MAX77705_BAT_DTLS_GOOD       0x03
#define MAX77705_BAT_DTLS_LOW        0x04
#define MAX77705_BAT_DTLS_OVP        0x05
#define MAX77705_BAT_DTLS_OCP        0x06

//
// CNFG_00 Fields
//
#define MAX77705_CNFG_00_MODE        (BIT0 | BIT1 | BIT2 | BIT3)
#define MAX77705_CNFG_00_WDTEN       BIT4

//
// CNFG_00 Mode Values
//
// Only these two are Touched. The Charge Path is Restarted by Dropping to
// Buck only and going back, which is what the Vendor Driver does. Any other
// Mode means something else owns the Charger, such as OTG or Boost, so it is
// left alone.
//
#define MAX77705_MODE_BUCK_ON        0x04
#define MAX77705_MODE_BUCK_CHG_ON    0x05

//
// Delay between the two Halves of a Charge Mode Cycle
//
#define MAX77705_MODE_CYCLE_DELAY_US 10000

//
// Watchdog Kick Interval, in 100ns Units.
//
// The Charger Watchdog Period is not Software Readable, and the Charger has no
// Period Field, only an Enable. The Vendor Kicks it from the Battery Monitor,
// which the Device Tree Polls every 30 Seconds while Charging, so the Hardware
// Period has to be Longer than that. Ten Seconds keeps a wide Margin.
//
#define MAX77705_WDT_KICK_INTERVAL   100000000

//
// CNFG_06 Fields
//
// The Protection Field has to read back as 0x03 before the Charger will accept
// a Write to any Configuration Register. WDTCLR Reloads the Charger Watchdog.
//
#define MAX77705_CNFG_06_CHGPROT     (BIT2 | BIT3)
#define MAX77705_CNFG_06_UNLOCKED    (BIT2 | BIT3)
#define MAX77705_CNFG_06_WDTCLR      (BIT0 | BIT1)
#define MAX77705_CNFG_06_WDTCLR_KICK  BIT0

//
// Unlock Retries, matching the Vendor Driver
//
#define MAX77705_UNLOCK_RETRIES      10
#define MAX77705_UNLOCK_DELAY_US     20000

//
// CHGIN_DTLS Values
//
#define MAX77705_CHGIN_DTLS_UVLO     0x00
#define MAX77705_CHGIN_DTLS_INVALID  0x01
#define MAX77705_CHGIN_DTLS_OVLO     0x02
#define MAX77705_CHGIN_DTLS_VALID    0x03

#endif /* _CHARGER_H_ */
