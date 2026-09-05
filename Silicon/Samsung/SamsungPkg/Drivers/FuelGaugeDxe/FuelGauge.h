#ifndef _FUEL_GAUGE_H_
#define _FUEL_GAUGE_H_

//
// MAX77705 Fuel Gauge Register Addresses
//
// Every Register is 16 Bit and Little Endian on the Wire.
//
#define MAX77705_FG_REG_STATUS      0x00
#define MAX77705_FG_REG_SOCREP      0x06
#define MAX77705_FG_REG_TEMPERATURE 0x08
#define MAX77705_FG_REG_VCELL       0x09
#define MAX77705_FG_REG_CURRENT     0x0A
#define MAX77705_FG_REG_SOCAV       0x0E
#define MAX77705_FG_REG_FULLCAP     0x10
#define MAX77705_FG_REG_DESIGNCAP   0x18
#define MAX77705_FG_REG_AVR_VCELL   0x19

//
// Register Width
//
#define MAX77705_FG_REG_LENGTH      2

//
// VCELL is Reported in Units of 78.125uV. The Conversion is Split across the
// low 12 Bits and the top 4 Bits so the Intermediate never Overflows 32 Bits.
//
#define MAX77705_FG_VCELL_SCALE     78125
#define MAX77705_FG_VCELL_DIVISOR   1000000

//
// A Full Battery Reports 1000 Tenths of a Percent.
//
#define MAX77705_FG_CHARGE_FULL     1000

#endif /* _FUEL_GAUGE_H_ */
