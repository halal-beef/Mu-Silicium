/*
 * Compatibility shim for LK's <platform/sfr.h>. Upstream this file pulls
 * in the whole exynos9830.h SoC header; the ported DPU code only actually
 * needs EXYNOS9830_SYSREG_DPU (== DPU_SYSREG_BASE_ADDR).
 */
#ifndef __COMPAT_PLATFORM_SFR_H__
#define __COMPAT_PLATFORM_SFR_H__

#define EXYNOS9830_SYSREG_DPU   0x19021000

#endif /* __COMPAT_PLATFORM_SFR_H__ */
