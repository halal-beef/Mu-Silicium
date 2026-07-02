/*
 * Compatibility shim for LK's <lk/reg.h>.
 * The ported CAL code accesses registers with Linux/LK-style
 * readl(addr) / writel(val, addr) helpers. Map these onto EDK2's
 * MmioRead32/MmioWrite32 (from Library/IoLib) so no register-poking
 * logic has to change.
 */
#ifndef __COMPAT_LK_REG_H__
#define __COMPAT_LK_REG_H__

#include <sys/types.h>
#include <Library/IoLib.h>

#ifndef __iomem
#define __iomem
#endif

#define readb(a)        MmioRead8((UINTN)(a))
#define writeb(v, a)    MmioWrite8((UINTN)(a), (v))

#define readw(a)        MmioRead16((UINTN)(a))
#define writew(v, a)    MmioWrite16((UINTN)(a), (v))

#define readl(a)        MmioRead32((UINTN)(a))
#define writel(v, a)    MmioWrite32((UINTN)(a), (v))

#define readq(a)        MmioRead64((UINTN)(a))
#define writeq(v, a)    MmioWrite64((UINTN)(a), (v))

#endif /* __COMPAT_LK_REG_H__ */
