/** @file
  S5E7885 (Exynos 7885) display bring-up: shared definitions.

  Target: Samsung Galaxy Tab A 10.1 2019 (gta3xllte), HX8279D panel,
  one DSI link (DSIM0), DECON0 -> window 3 <- IDMA_G0.

  Register programming is derived from the Samsung SM-T515 downstream
  kernel (drivers/video/fbdev/exynos/dpu_7885), GPL-2.0.
**/

#ifndef S5E7885_DISPLAY_H_
#define S5E7885_DISPLAY_H_

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>

//
// Policy knobs
//

// When non-zero, a display pipeline that is already running (left by the
// previous boot stage) is shut down and the whole panel/DSI/DECON chain is
// brought up again from scratch. This is the path that matters once S-Boot is
// gone. Set to 0 to keep S-Boot's picture alive and only re-point the DMA.
#define S5E7885_FORCE_FULL_INIT  1

// 0 = detect from the panel (register 0xB1), 1 = standard, 2 = "VE" variant.
#define HX8279D_FORCE_VARIANT    0

//
// SoC blocks (see exynos7885.dts / pinctrl-exynos.c / pmucal_local_exynos7885.h)
//

#define S5E7885_PMU_DISPAUD_CONFIGURATION  0x11C84020U
#define S5E7885_PMU_DISPAUD_STATUS         0x11C84024U
#define S5E7885_PMU_DPHY_ISOLATION_DSIM0   0x11C80678U  // bit 0: 1 = released

#define S5E7885_CMU_DISPAUD_BASE           0x14800000U
#define S5E7885_CMU_DISPAUD_BUS_USER       (S5E7885_CMU_DISPAUD_BASE + 0x0120U)
#define S5E7885_CMU_USER_MUX_SEL           BIT4
#define S5E7885_CMU_USER_MUX_BUSY          BIT7

#define S5E7885_DECON0_BASE                0x14860000U
#define S5E7885_DSIM0_BASE                 0x14870000U
#define S5E7885_DPP_G0_BASE                0x14851000U   // DPP "regs"
#define S5E7885_IDMA_G0_BASE               0x148B1000U   // DPU_DMA "dma_regs"

// GPIO: TOP pin controller. Bank type 6: CON +0x0 (4 bit/pin), DAT +0x4.
#define S5E7885_GPIO_TOP_BASE              0x139B0000U
#define S5E7885_GPG1_OFFSET                0x140U
#define S5E7885_GPG2_OFFSET                0x160U

// Panel rails and reset (display-lcd_gta3xl_common.dtsi)
#define PANEL_LDO_EN_BANK                  S5E7885_GPG2_OFFSET
#define PANEL_LDO_EN_PIN                   6
#define PANEL_BUCK_EN_BANK                 S5E7885_GPG2_OFFSET
#define PANEL_BUCK_EN_PIN                  7
#define PANEL_RESET_BANK                   S5E7885_GPG1_OFFSET
#define PANEL_RESET_PIN                    7

// DECON window / DMA channel pairing used by this driver
#define DECON_WIN_ID                       3   // IDMA_G0 is dedicated to WIN3
#define DECON_WIN_CHANNEL                  3   // dpu_dma_type_to_channel(IDMA_G0)

//
// Display description (video mode, 4 lanes, single DSI)
//

typedef struct {
  UINT32  Xres;
  UINT32  Yres;
  UINT32  Hbp;
  UINT32  Hfp;
  UINT32  Hsa;
  UINT32  Vbp;
  UINT32  Vfp;
  UINT32  Vsa;

  // D-PHY PLL: Fout = Fin(26 MHz) * M / (P * 2^S) on the bit clock
  UINT32  PllP;
  UINT32  PllM;
  UINT32  PllS;

  UINT32  EscClkMhz;
  UINT32  DataLanes;       // number of data lanes
  UINT32  VtCompensation;
  UINT32  ClkLaneOnOff;    // keep HS clock running between lines
  UINT32  EotpDisabled;    // 1 = do not send EoTp from the host
} DPU_LCD_INFO;

extern CONST DPU_LCD_INFO  gGta3xlLcdInfo;

//
// Platform.c
//

EFI_STATUS
PlatformDispaudPowerOn (
  VOID
  );

VOID
PlatformDphyIsolation (
  IN BOOLEAN  Release
  );

VOID
PlatformPanelPower (
  IN BOOLEAN  On
  );

VOID
PlatformPanelReset (
  VOID
  );

//
// Dsim.c
//

#define MIPI_DSI_DCS_SHORT_WRITE                    0x05
#define MIPI_DSI_DCS_SHORT_WRITE_PARAM              0x15
#define MIPI_DSI_DCS_READ                           0x06
#define MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE     0x37
#define MIPI_DSI_DCS_LONG_WRITE                     0x39

EFI_STATUS
DsimInit (
  IN CONST DPU_LCD_INFO  *Lcd
  );

VOID
DsimStart (
  VOID
  );

VOID
DsimStop (
  VOID
  );

BOOLEAN
DsimIsActive (
  VOID
  );

EFI_STATUS
DsimWriteShort (
  IN UINT32  DataId,
  IN UINT32  Data0,
  IN UINT32  Data1
  );

EFI_STATUS
DsimWriteLong (
  IN UINT32       DataId,
  IN CONST UINT8  *Payload,
  IN UINT32       Size
  );

// Returns number of bytes read, or a negative value on failure.
INTN
DsimReadData (
  IN  UINT32  Addr,
  IN  UINT32  Count,
  OUT UINT8   *Buffer
  );

//
// Decon.c
//

BOOLEAN
DeconIsRunning (
  VOID
  );

VOID
DeconDisable (
  VOID
  );

EFI_STATUS
DeconEnable (
  IN CONST DPU_LCD_INFO  *Lcd,
  IN UINT32              FrameBufferAddress
  );

// Re-point IDMA_G0 at a (possibly different) buffer on an enabled pipeline.
VOID
DeconSetFrameBuffer (
  IN CONST DPU_LCD_INFO  *Lcd,
  IN UINT32              FrameBufferAddress
  );

//
// Hx8279d.c
//

EFI_STATUS
Hx8279dInit (
  VOID
  );

EFI_STATUS
Hx8279dDisplayOn (
  VOID
  );

#endif // S5E7885_DISPLAY_H_
