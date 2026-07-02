/*
 * Compatibility shim for LK's <platform/delay.h>.
 */
#ifndef __COMPAT_PLATFORM_DELAY_H__
#define __COMPAT_PLATFORM_DELAY_H__

#include <Library/TimerLib.h>

#define udelay(us)  MicroSecondDelay(us)
#define mdelay(ms)  MicroSecondDelay((ms) * 1000)

#endif /* __COMPAT_PLATFORM_DELAY_H__ */
