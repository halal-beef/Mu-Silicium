/*
 * Register addresses ported from lk3rd's
 * platform/exynos9830/include/platform/exynos9830.h -- only the subset
 * needed to bring up DECON/DSIM/the S6E3HAB panel on the x1s target.
 */
#ifndef __EXYNOS9830_REGS_H__
#define __EXYNOS9830_REGS_H__

#define EXYNOS9830_GPIO_PERIC1_BASE            0x10730000
#define EXYNOS9830_GPIO_ALIVE_BASE             0x15850000
#define EXYNOS9830_POWER_BASE                  0x15860000

/* MIPI D-PHY isolation control (PMU) */
#define EXYNOS9830_POWER_MIPI_PHY_M4S4_CONTROL (EXYNOS9830_POWER_BASE + 0x0710)
#define EXYNOS_MIPI_PHY_ENABLE                 (1 << 0)
#define EXYNOS_MIPI_PHY_SRESETN                (1 << 1)
#define EXYNOS_MIPI_PHY_MRESETN                (1 << 2)

/* LCD_TE_HW: GPC0_4 -> DISP_TES0 */
#define EXYNOS9830_GPC0CON                     (EXYNOS9830_GPIO_PERIC1_BASE + 0x00A0)
#define EXYNOS9830_GPC0DAT                     (EXYNOS9830_GPIO_PERIC1_BASE + 0x00A4)
#define EXYNOS9830_GPC0PUD                     (EXYNOS9830_GPIO_PERIC1_BASE + 0x00A8)

/* MIPI_DSI0_nRST drive strength: GPP5 */
#define EXYNOS9830_GPP5CON                     (EXYNOS9830_GPIO_PERIC1_BASE + 0x0000)
#define EXYNOS9830_GPP5DAT                     (EXYNOS9830_GPIO_PERIC1_BASE + 0x0004)
#define EXYNOS9830_GPP5PUD                     (EXYNOS9830_GPIO_PERIC1_BASE + 0x0008)
#define EXYNOS9830_GPP5DRV                     (EXYNOS9830_GPIO_PERIC1_BASE + 0x000C)

/* MLCD_RSTB / MIPI_DSI0_nRST: GPA3_4 (Alive block, x1s/3HA9 wiring) */
#define EXYNOS9830_GPA3CON                     (EXYNOS9830_GPIO_ALIVE_BASE + 0x0060)
#define EXYNOS9830_GPA3DAT                     (EXYNOS9830_GPIO_ALIVE_BASE + 0x0064)
#define EXYNOS9830_GPA3PUD                     (EXYNOS9830_GPIO_ALIVE_BASE + 0x0068)
#define EXYNOS9830_GPA3DRV                     (EXYNOS9830_GPIO_ALIVE_BASE + 0x006C)

#endif /* __EXYNOS9830_REGS_H__ */
