/** @file
  Exynos 7885 platform glue for the display path.

  Covers everything outside DECON/DSIM/DPP that must happen before the panel
  can be driven: DISPAUD power domain, the DISPAUD CMU bus clock mux, the
  MIPI D-PHY isolation and the three panel GPIOs.

  Sources (SM-T515 downstream kernel):
    - drivers/soc/samsung/cal-if/exynos7885/pmucal_local_exynos7885.h
    - drivers/soc/samsung/cal-if/ra.c, exynos7885/cmucal-sfr.c
    - drivers/video/fbdev/exynos/dpu_7885/dsim_drv.c (dphy_power_on)
    - arch/arm64/boot/dts/exynos/dtbo/display-lcd_gta3xl_common.dtsi
**/

#include "../S5E7885Display.h"

// CMU_TOP: gate in front of the DISPAUD bus clock
#define CMU_TOP_BASE                  0x12060000U
#define CMU_TOP_GATE_DISPAUD_BUS      (CMU_TOP_BASE + 0x203CU)
#define CMU_GATE_MANUAL               BIT20   // 1: CG_VAL decides
#define CMU_GATE_CG_VAL               BIT21   // 1: clock passes

#define GPIO_CON                      0x0U
#define GPIO_DAT                      0x4U

STATIC
VOID
GpioSetOutput (
  IN UINT32   BankOffset,
  IN UINT32   Pin,
  IN BOOLEAN  High
  )
{
  UINTN  Bank;

  Bank = S5E7885_GPIO_TOP_BASE + BankOffset;

  // Latch the level first so the pin does not glitch when it becomes an output
  MmioAndThenOr32 (Bank + GPIO_DAT, ~(1U << Pin), High ? (1U << Pin) : 0);
  MmioAndThenOr32 (Bank + GPIO_CON, ~(0xFU << (Pin * 4)), 0x1U << (Pin * 4));
}

EFI_STATUS
PlatformDispaudPowerOn (
  VOID
  )
{
  UINT32  Cnt;
  UINT32  Gate;

  if ((MmioRead32 (S5E7885_PMU_DISPAUD_STATUS) & 0xF) == 0xF) {
    DEBUG ((DEBUG_INFO, "DISPAUD power domain already on\n"));
  } else {
    MmioAndThenOr32 (S5E7885_PMU_DISPAUD_CONFIGURATION, ~0xFU, 0xF);

    for (Cnt = 1000; Cnt != 0; Cnt--) {
      if ((MmioRead32 (S5E7885_PMU_DISPAUD_STATUS) & 0xF) == 0xF) {
        break;
      }

      MicroSecondDelay (10);
    }

    if (Cnt == 0) {
      DEBUG ((DEBUG_ERROR, "DISPAUD power domain did not come up\n"));
      return EFI_TIMEOUT;
    }
  }

  //
  // Make sure the DISPAUD bus clock actually comes from CMU_TOP rather than
  // the 26 MHz oscillator: the "user mux" resets to OSC. ra_set_div_mux() in
  // the kernel flips this same bit. Guard against CMU_TOP having left the
  // bus clock manually gated, which would stall every register access below.
  //
  Gate = MmioRead32 (CMU_TOP_GATE_DISPAUD_BUS);
  if (((Gate & CMU_GATE_MANUAL) != 0) && ((Gate & CMU_GATE_CG_VAL) == 0)) {
    DEBUG ((DEBUG_WARN, "CMU_TOP DISPAUD_BUS gate was off, enabling\n"));
    MmioOr32 (CMU_TOP_GATE_DISPAUD_BUS, CMU_GATE_CG_VAL);
  }

  if ((MmioRead32 (S5E7885_CMU_DISPAUD_BUS_USER) & S5E7885_CMU_USER_MUX_SEL) == 0) {
    DEBUG ((DEBUG_INFO, "DISPAUD bus user mux was on OSC, switching to CMU_TOP\n"));
    MmioOr32 (S5E7885_CMU_DISPAUD_BUS_USER, S5E7885_CMU_USER_MUX_SEL);

    for (Cnt = 1000; Cnt != 0; Cnt--) {
      if ((MmioRead32 (S5E7885_CMU_DISPAUD_BUS_USER) & S5E7885_CMU_USER_MUX_BUSY) == 0) {
        break;
      }

      MicroSecondDelay (10);
    }

    if (Cnt == 0) {
      DEBUG ((DEBUG_ERROR, "DISPAUD bus user mux switch timed out\n"));
      return EFI_TIMEOUT;
    }
  }

  DEBUG ((
    DEBUG_INFO,
    "DISPAUD CMU: user mux 0x%08x, CMU_TOP gate 0x%08x\n",
    MmioRead32 (S5E7885_CMU_DISPAUD_BUS_USER),
    MmioRead32 (CMU_TOP_GATE_DISPAUD_BUS)
    ));

  return EFI_SUCCESS;
}

VOID
PlatformDphyIsolation (
  IN BOOLEAN  Release
  )
{
  // bit 0 = 1: isolation released (phy_power_on equivalent)
  MmioWrite32 (S5E7885_PMU_DPHY_ISOLATION_DSIM0, Release ? 1 : 0);
}

VOID
PlatformPanelPower (
  IN BOOLEAN  On
  )
{
  if (On) {
    // dsim_set_panel_power_enable
    GpioSetOutput (PANEL_LDO_EN_BANK, PANEL_LDO_EN_PIN, TRUE);
    MicroSecondDelay (10000);
    GpioSetOutput (PANEL_BUCK_EN_BANK, PANEL_BUCK_EN_PIN, TRUE);
    MicroSecondDelay (15000);
  } else {
    // dsim_set_panel_power_disable
    GpioSetOutput (PANEL_RESET_BANK, PANEL_RESET_PIN, FALSE);
    GpioSetOutput (PANEL_BUCK_EN_BANK, PANEL_BUCK_EN_PIN, FALSE);
    MicroSecondDelay (10000);
    GpioSetOutput (PANEL_LDO_EN_BANK, PANEL_LDO_EN_PIN, FALSE);
  }
}

VOID
PlatformPanelReset (
  VOID
  )
{
  // dsim_reset_panel: high 10 ms, low 10 ms, high 10 ms
  GpioSetOutput (PANEL_RESET_BANK, PANEL_RESET_PIN, TRUE);
  MicroSecondDelay (10000);
  GpioSetOutput (PANEL_RESET_BANK, PANEL_RESET_PIN, FALSE);
  MicroSecondDelay (10000);
  GpioSetOutput (PANEL_RESET_BANK, PANEL_RESET_PIN, TRUE);
  MicroSecondDelay (10000);
}
