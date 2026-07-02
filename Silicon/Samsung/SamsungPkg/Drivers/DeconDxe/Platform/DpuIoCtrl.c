/*
 * Ported from lk3rd's target/x1s/dpu_io/dpu_gpio.S (register pokes,
 * translated from AArch64 asm to C) and target/x1s/dpu_io/dpu_io_ctrl.c
 * (board glue that wires those pokes into dsim_drv.c's config_ops).
 *
 * This is the x1s/S6E3HAB board-specific GPIO + MIPI-DPHY control layer:
 *   - LCD_TE_HW (GPC0_4) pinmux for the hardware TE interrupt
 *   - MLCD_RSTB / MIPI_DSI0_nRST (GPA3_4) panel reset line
 *   - PMU MIPI D-PHY isolation enable/disable
 *
 * Panel VCI/VDDR power is supplied by the PMIC and is already enabled by
 * the time UEFI runs, so display_panel_power() is a no-op here exactly
 * as it is in upstream lk3rd.
 */

#include <sys/types.h>
#include <lk/reg.h>
#include <platform/delay.h>
#include <target/dpu_io_ctrl.h>

#include "Exynos9830Regs.h"

/*
 * ########## Register-level GPIO helpers (was dpu_gpio.S) ##########
 */
VOID
display_te_init (
  VOID
  )
{
  UINT32  Val;

  /* LCD_TE_HW (GPC0_4): pull-down enable, TE is active high */
  Val  = readl (EXYNOS9830_GPC0PUD);
  Val &= ~(0xFu << 16);
  Val |= (0x1u << 16);
  writel (Val, EXYNOS9830_GPC0PUD);

  /* LCD_TE_HW (GPC0_4): function -> DISP_TES0 */
  Val  = readl (EXYNOS9830_GPC0CON);
  Val &= ~(0xFu << 16);
  Val |= (0x2u << 16);
  writel (Val, EXYNOS9830_GPC0CON);
}

VOID
display_panel_init (
  VOID
  )
{
  UINT32  Val;

  /* MIPI_DSI0_nRST (GPA3_4): pull-up enable, reset is active low */
  Val  = readl (EXYNOS9830_GPA3PUD);
  Val &= ~(0xFu << 16);
  Val |= (0x3u << 16);
  writel (Val, EXYNOS9830_GPA3PUD);

  /* MIPI_DSI0_nRST (GPA3_4): output */
  Val  = readl (EXYNOS9830_GPA3CON);
  Val &= ~(0xFu << 16);
  Val |= (0x1u << 16);
  writel (Val, EXYNOS9830_GPA3CON);

  /* MIPI_DSI0_nRST: DRV 4X (matches upstream lk3rd verbatim) */
  Val  = readl (EXYNOS9830_GPP5DRV);
  Val &= ~(0xFu << 16);
  Val |= (0x3u << 16);
  writel (Val, EXYNOS9830_GPP5DRV);
}

VOID
display_panel_reset (
  VOID
  )
{
  UINT32  Val;

  /* MLCD_RSTB (GPA3_4) -> low */
  Val  = readl (EXYNOS9830_GPA3DAT);
  Val &= ~(0x1u << 4);
  writel (Val, EXYNOS9830_GPA3DAT);
}

VOID
display_panel_release (
  VOID
  )
{
  UINT32  Val;

  /* MLCD_RSTB (GPA3_4) -> high */
  Val  = readl (EXYNOS9830_GPA3DAT);
  Val &= ~(0x1u << 4);
  Val |= (0x1u << 4);
  writel (Val, EXYNOS9830_GPA3DAT);
}

VOID
display_panel_power (
  VOID
  )
{
  /* No-op: panel VCI/VDDR rails are already enabled by the PMIC before
   * UEFI runs (matches upstream lk3rd's x1s dpu_io_ctrl.c). */
}

/*
 * ########## Machine dependency (was dpu_io_ctrl.c) ##########
 */
static VOID
mipi_phy_control (
  UINT32  DevIndex,
  UINT32  Enable
  )
{
  UINT32  Cfg;

  (VOID)DevIndex;

  /* DPHY isolation with PMU */
  Cfg = readl (EXYNOS9830_POWER_MIPI_PHY_M4S4_CONTROL);
  if (Enable) {
    Cfg |= EXYNOS_MIPI_PHY_ENABLE;
  } else {
    Cfg &= ~(EXYNOS_MIPI_PHY_ENABLE);
  }

  writel (Cfg, EXYNOS9830_POWER_MIPI_PHY_M4S4_CONTROL);
}

/* MIPI-PHY related with master interface */
static VOID
set_mipi_phy_control (
  UINT32  Enable
  )
{
  mipi_phy_control (0, Enable);
}

/* Set GPIO internal interrupt when DECON is using HW trigger */
static VOID
set_gpio_hw_te (
  VOID
  )
{
  display_te_init ();
}

/* Configure and toggle LCD_RESET */
static VOID
set_gpio_lcd_reset (
  enum board_gpio_type  GpioType
  )
{
  (VOID)GpioType;

  /* RESET: "0" -> "1" */
  display_panel_reset ();
  mdelay (5);
  display_panel_release ();
  mdelay (10);
}

/* Configure and enable LCD_POWER */
static VOID
set_gpio_lcd_power (
  enum board_gpio_type  GpioType
  )
{
  (VOID)GpioType;

  display_panel_power ();
  mdelay (10);
}

struct exynos_display_config display_config = {
  .set_mipi_phy       = set_mipi_phy_control,
  .set_gpio_hw_te     = set_gpio_hw_te,
  .set_gpio_lcd_reset = set_gpio_lcd_reset,
  .set_gpio_lcd_power = set_gpio_lcd_power,
};

enum board_gpio_type
get_exynos_board_type (
  VOID
  )
{
  return BOARD_BTYPE;
}

struct exynos_display_config *
get_exynos_display_config (
  VOID
  )
{
  return &display_config;
}
