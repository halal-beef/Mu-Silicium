/** @file
  Exynos 7885 DECON0 + DPP/IDMA_G0 driver (video mode, single DSI).

  Ported from the Samsung SM-T515 downstream kernel,
  drivers/video/fbdev/exynos/dpu_7885/{decon_reg.c,dpp_reg.c,decon_core.c}
  (GPL-2.0, Copyright (c) Samsung Electronics Co., Ltd.).

  Pipeline: framebuffer -> IDMA_G0 -> DPP0 -> DECON window 3 -> blender ->
  frame FIFO 0 -> formatter 0 -> DSIM0. Everything is polled.

  Order (mirrors decon_enable()):
    1. DECON global/blender/data-path init
    2. black window on WIN3, DECON start, wait one frame
    3. DPP/IDMA programmed with the real framebuffer, WIN3 switched to it
**/

#include "../S5E7885Display.h"
#include "../Regs/RegsDecon.h"
#include "../Regs/RegsDpp.h"

#define DECON_BASE                S5E7885_DECON0_BASE
#define DPP_BASE                  S5E7885_DPP_G0_BASE
#define DMA_BASE                  S5E7885_IDMA_G0_BASE

#define DECON_ALL_BITS            0xFFFFFFFFU

// enum decon_data_path: no compression - FF0 - FORMATTER0 - DSIM_IF0
#define DECON_DPATH_NOCOMP_FF0_FMT0_DSIMIF0   0x001U
#define DECON_ENHANCEPATH_ALL_OFF             0x0U
#define DECON_SHAREPATH_DQE_USE               0x0U
#define DECON_RGB_ORDER_BGR                   0x4U

// enum decon_win_func / decon_win_alpha_mult
#define DECON_PD_FUNC_COPY                    0x1U
#define DECON_ALPHA_MULT_SRC_SEL_AF           0x2U

typedef struct {
  UINT32  Wincon;
  UINT32  StartPos;
  UINT32  EndPos;
  UINT32  Colormap;
  UINT32  StartTime;
  UINT32  PixelCount;
  UINT32  PlaneAlpha;
} DECON_WINDOW_REGS;

//
// Register accessors
//

STATIC
UINT32
DeconRead (
  IN UINT32  Reg
  )
{
  return MmioRead32 (DECON_BASE + Reg);
}

STATIC
VOID
DeconWrite (
  IN UINT32  Reg,
  IN UINT32  Val
  )
{
  MmioWrite32 (DECON_BASE + Reg, Val);
}

STATIC
VOID
DeconWriteMask (
  IN UINT32  Reg,
  IN UINT32  Val,
  IN UINT32  Mask
  )
{
  UINT32  Old;

  Old = DeconRead (Reg);
  DeconWrite (Reg, (Val & Mask) | (Old & ~Mask));
}

STATIC
VOID
DppWrite (
  IN UINT32  Reg,
  IN UINT32  Val
  )
{
  MmioWrite32 (DPP_BASE + Reg, Val);
}

STATIC
VOID
DppWriteMask (
  IN UINT32  Reg,
  IN UINT32  Val,
  IN UINT32  Mask
  )
{
  UINT32  Old;

  Old = MmioRead32 (DPP_BASE + Reg);
  MmioWrite32 (DPP_BASE + Reg, (Val & Mask) | (Old & ~Mask));
}

STATIC
VOID
DmaWrite (
  IN UINT32  Reg,
  IN UINT32  Val
  )
{
  MmioWrite32 (DMA_BASE + Reg, Val);
}

STATIC
VOID
DmaWriteMask (
  IN UINT32  Reg,
  IN UINT32  Val,
  IN UINT32  Mask
  )
{
  UINT32  Old;

  Old = MmioRead32 (DMA_BASE + Reg);
  MmioWrite32 (DMA_BASE + Reg, (Val & Mask) | (Old & ~Mask));
}

STATIC
UINT32
Bool32 (
  IN UINT32  En
  )
{
  return (En != 0) ? DECON_ALL_BITS : 0;
}

//
// DECON
//

BOOLEAN
DeconIsRunning (
  VOID
  )
{
  return (DeconRead (GLOBAL_CONTROL) & GLOBAL_CONTROL_RUN_STATUS) != 0;
}

STATIC
EFI_STATUS
DeconWaitRunStatus (
  IN BOOLEAN  Running,
  IN UINT32   TimeoutUs
  )
{
  UINT32  Cnt;

  for (Cnt = TimeoutUs / 100; Cnt != 0; Cnt--) {
    if (DeconIsRunning () == Running) {
      return EFI_SUCCESS;
    }

    MicroSecondDelay (100);
  }

  DEBUG ((DEBUG_ERROR, "DECON: timed out waiting for run status %d\n", Running));
  return EFI_TIMEOUT;
}

STATIC
EFI_STATUS
DeconWaitUpdateDone (
  IN UINT32  TimeoutUs
  )
{
  UINT32  Cnt;

  for (Cnt = TimeoutUs / 100; Cnt != 0; Cnt--) {
    if (DeconRead (SHADOW_REG_UPDATE_REQ) == 0) {
      return EFI_SUCCESS;
    }

    MicroSecondDelay (100);
  }

  DEBUG ((DEBUG_ERROR, "DECON: shadow register update did not complete\n"));
  return EFI_TIMEOUT;
}

STATIC
VOID
DeconClearIntAll (
  VOID
  )
{
  UINT32  Mask;

  Mask = DPU_MDNIE_DIMMING_END_INT_EN | DPU_MDNIE_DIMMING_START_INT_EN |
         DPU_DQE_DIMMING_END_INT_EN | DPU_DQE_DIMMING_START_INT_EN |
         DPU_FRAME_DONE_INT_EN | DPU_FRAME_START_INT_EN |
         DPU_FIFO_SEL_FIFO0 | DPU_UNDER_FLOW_INT_EN;
  DeconWriteMask (INTERRUPT_PENDING, DECON_ALL_BITS, Mask);

  Mask = DPU_RESOURCE_CONFLICT_INT_EN | DPU_TIME_OUT_INT_EN | DPU_ERROR_INT_EN;
  DeconWriteMask (EXTRA_INTERRUPT_PENDING, DECON_ALL_BITS, Mask);
}

STATIC
VOID
DeconRegInit (
  IN CONST DPU_LCD_INFO  *Lcd
  )
{
  UINT32  Val;
  UINT32  Mask;
  UINT32  Width;
  UINT32  Height;
  UINT32  Cnt;
  UINT32  Th;

  Width  = Lcd->Xres;
  Height = Lcd->Yres;

  // Clock gating on, 8K of shared SRAM, RGB (video) interface
  DeconWriteMask (CLOCK_CONTROL_0, DECON_ALL_BITS, CLOCK_CONTROL_0_F_MASK);
  DeconWrite (SRAM_SHARE_ENABLE, FLOATING_SRAM_SHARE_ENABLE_F | FF0_SRAM_SHARE_ENABLE_F);
  DeconWriteMask (
    GLOBAL_CONTROL,
    GLOBAL_CONTROL_OPERATION_MODE_RGBIF_F,
    GLOBAL_CONTROL_OPERATION_MODE_F
    );

  // Blender background size
  Val  = BLENDER_BG_HEIGHT_F (Height) | BLENDER_BG_WIDTH_F (Width);
  Mask = BLENDER_BG_HEIGHT_MASK | BLENDER_BG_WIDTH_MASK;
  DeconWriteMask (BLENDER_BG_IMAGE_SIZE_0, Val, Mask);
  DeconWrite (BLENDER_BG_IMAGE_SIZE_1, Height * Width);

  // SW trigger (video mode does not use it, but the kernel sets it up)
  DeconWriteMask (HW_SW_TRIG_CONTROL, 0, HW_TRIG_EN);

  // decon_reg_configure_lcd(): output colour order matches the DSIM side
  DeconWriteMask (
    FORMATTER_CONTROL,
    FORMATTER_OUT_RGB_ORDER_F (DECON_RGB_ORDER_BGR),
    FORMATTER_OUT_RGB_ORDER_MASK
    );

  // Data path: NOCOMP - FF0 - FORMATTER0 - DSIM_IF0, enhancements off
  Val  = ENHANCE_LOGIC_PATH_F (DECON_ENHANCEPATH_ALL_OFF) |
         COMP_LINKIF_WB_PATH_F (DECON_DPATH_NOCOMP_FF0_FMT0_DSIMIF0);
  Mask = ENHANCE_LOGIC_PATH_MASK | COMP_LINKIF_WB_PATH_MASK;
  DeconWriteMask (DATA_PATH_CONTROL_2, Val, Mask);

  // decon_reg_config_data_path_size(): splitter, frame FIFO 0, formatter 0
  DeconWrite (SPLITTER_SIZE_CONTROL_0, SPLITTER_HEIGHT_F (Height) | SPLITTER_WIDTH_F (Width * 2));
  DeconWrite (SPLITTER_SIZE_CONTROL_1, Height * Width);
  DeconWrite (SPLITTER_SPLIT_IDX_CONTROL, Width);
  DeconWrite (SPLITTER_OVERLAP_CONTROL, SPLITTER_OVERLAP_F (0));

  Cnt = Height * Width;
  DeconWrite (FRAME_FIFO_0_SIZE_CONTROL_0, FRAME_FIFO_HEIGHT_F (Height) | FRAME_FIFO_WIDTH_F (Width));
  DeconWrite (FRAME_FIFO_0_SIZE_CONTROL_1, Cnt);
  Th = FRAME_FIFO_0_TH_F (Width);
  DeconWriteMask (FRAME_FIFO_TH_CONTROL_0, Th, FRAME_FIFO_0_TH_MASK);

  DeconWrite (FORMATTER0_SIZE_CONTROL_0, FORMATTER_HEIGHT_F (Height) | FORMATTER_WIDTH_F (Width));
  DeconWrite (FORMATTER0_SIZE_CONTROL_1, Cnt);

  DeconWriteMask (DATA_PATH_CONTROL_2, APS_PATH_F (DECON_SHAREPATH_DQE_USE), APS_PATH_MASK);
  DeconWriteMask (DATA_PATH_CONTROL_2, HSC_PATH_F (DECON_SHAREPATH_DQE_USE), HSC_PATH_MASK);

  // decon_reg_per_frame_off()
  DeconWriteMask (GLOBAL_CONTROL, 0, GLOBAL_CONTROL_DECON_EN_F);

  // Asserted interrupts must be cleared before the HW is started
  DeconClearIntAll ();
}

STATIC
VOID
DeconSetWindowControl (
  IN UINT32                   WinIdx,
  IN CONST DECON_WINDOW_REGS  *Regs,
  IN BOOLEAN                  WinmapEn
  )
{
  UINT32  WinEn;
  UINT32  Alpha0;
  UINT32  Alpha1;
  UINT32  Val;
  UINT32  Mask;

  WinEn = ((Regs->Wincon & WIN_EN_F (WinIdx)) != 0) ? 1 : 0;

  if (WinEn != 0) {
    // decon_reg_set_win_bnd_function(), DECON_BLENDING_NONE
    Alpha0 = (Regs->PlaneAlpha <= 0xFF) ? Regs->PlaneAlpha : 0xFF;
    Alpha1 = 0;

    DeconWriteMask (
      WIN_CONTROL_0 (WinIdx),
      WIN_ALPHA1_F (Alpha1) | WIN_ALPHA0_F (Alpha0),
      WIN_ALPHA1_MASK | WIN_ALPHA0_MASK
      );
    DeconWriteMask (
      WIN_CONTROL_0 (WinIdx),
      WIN_ALPHA_MULT_SRC_SEL_F (DECON_ALPHA_MULT_SRC_SEL_AF),
      WIN_ALPHA_MULT_SRC_SEL_MASK
      );
    DeconWriteMask (WIN_CONTROL_0 (WinIdx), WIN_FUNC_F (DECON_PD_FUNC_COPY), WIN_FUNC_MASK);

    DeconWrite (WIN_START_POSITION (WinIdx), Regs->StartPos);
    DeconWrite (WIN_END_POSITION (WinIdx), Regs->EndPos);
    DeconWrite (WIN_START_TIME_CONTROL (WinIdx), Regs->StartTime);
    DeconWrite (WIN_PIXEL_COUNT (WinIdx), Regs->PixelCount);
  }

  DeconWriteMask (DATA_PATH_CONTROL_0, Bool32 (WinEn), WIN_EN_F (WinIdx));

  if (WinEn != 0) {
    // decon_reg_set_winmap()
    DeconWriteMask (DATA_PATH_CONTROL_0, Bool32 (WinmapEn), WIN_MAPCOLOR_EN_F (WinIdx));

    Val  = WIN_MAPCOLOR_A_F ((Regs->Colormap >> 24) & 0xFF) |
           WIN_MAPCOLOR_R_F ((Regs->Colormap >> 16) & 0xFF);
    Mask = WIN_MAPCOLOR_A_MASK | WIN_MAPCOLOR_R_MASK;
    DeconWriteMask (WIN_COLORMAP_0 (WinIdx), Val, Mask);

    Val  = WIN_MAPCOLOR_G_F ((Regs->Colormap >> 8) & 0xFF) |
           WIN_MAPCOLOR_B_F ((Regs->Colormap >> 0) & 0xFF);
    Mask = WIN_MAPCOLOR_G_MASK | WIN_MAPCOLOR_B_MASK;
    DeconWriteMask (WIN_COLORMAP_1 (WinIdx), Val, Mask);
  }

  // decon_reg_config_win_channel()
  DeconWriteMask (
    DATA_PATH_CONTROL_1,
    WIN_CHMAP_F (WinIdx, DECON_WIN_CHANNEL),
    WIN_CHMAP_MASK (WinIdx)
    );
}

STATIC
VOID
DeconFillWindow (
  IN  CONST DPU_LCD_INFO  *Lcd,
  IN  UINT32              PlaneAlpha,
  OUT DECON_WINDOW_REGS   *Regs
  )
{
  ZeroMem (Regs, sizeof (*Regs));
  Regs->Wincon     = WIN_EN_F (DECON_WIN_ID);
  Regs->StartPos   = WIN_STRPTR_Y_F (0) | WIN_STRPTR_X_F (0);
  Regs->EndPos     = WIN_ENDPTR_Y_F (Lcd->Yres - 1) | WIN_ENDPTR_X_F (Lcd->Xres - 1);
  Regs->Colormap   = 0x000000;
  Regs->PixelCount = Lcd->Xres * Lcd->Yres;
  Regs->PlaneAlpha = PlaneAlpha;
}

//
// DPP / IDMA_G0
//

STATIC
VOID
DppInit (
  VOID
  )
{
  // dpp_reg_init(), minus the interrupt masks (we poll)
  DmaWriteMask (IDMA_ENABLE, DECON_ALL_BITS, IDMA_ALL_CLOCK_GATE_EN_MASK);
  DppWriteMask (DPP_ENABLE, DECON_ALL_BITS, DPP_ALL_CLOCK_GATE_EN_MASK);

  // dpp_reg_set_lookup_table()
  DmaWrite (IDMA_IN_QOS_LUT07_00, 0x44444444U);
  DmaWrite (IDMA_IN_QOS_LUT15_08, 0x44444444U);

  // dpp_reg_set_dynamic_clock_gating()
  DmaWriteMask (IDMA_DYNAMIC_GATING_EN, DECON_ALL_BITS, IDMA_DG_EN_ALL);
  DppWriteMask (DPP_DYNAMIC_GATING_EN, DECON_ALL_BITS, DPP_DG_EN_ALL);

  // dpp_reg_set_plane_alpha_fixed()
  DmaWriteMask (IDMA_OUT_CON, IDMA_OUT_FRAME_ALPHA (0xFF), IDMA_OUT_FRAME_ALPHA_MASK);

  // dpp_reg_control_linecnt(): capture mode 0, enabled
  DppWriteMask (DPP_LINECNT_CON, 0, DPP_LC_CAPTURE_MASK);
  DppWriteMask (DPP_LINECNT_CON, DECON_ALL_BITS, DPP_LC_ENABLE_MASK);
}

STATIC
VOID
DppConfigure (
  IN CONST DPU_LCD_INFO  *Lcd,
  IN UINT32              FrameBufferAddress
  )
{
  // 1:1, so the scaler ratio is 1.0 in 20-bit fixed point. IDMA_G0 is a
  // graphics channel with no scaler, so the coefficient tables are not loaded.
  DppWriteMask (DPP_MAIN_H_RATIO, DPP_H_RATIO (1U << 20), DPP_H_RATIO_MASK);
  DppWriteMask (DPP_MAIN_V_RATIO, DPP_V_RATIO (1U << 20), DPP_V_RATIO_MASK);

  // Source offset / allocated size / cropped size (all equal to the panel)
  DmaWrite (IDMA_SRC_OFFSET, IDMA_SRC_OFFSET_Y (0) | IDMA_SRC_OFFSET_X (0));
  DmaWrite (IDMA_SRC_SIZE, IDMA_SRC_HEIGHT (Lcd->Yres) | IDMA_SRC_WIDTH (Lcd->Xres));
  DmaWrite (IDMA_IMG_SIZE, IDMA_IMG_HEIGHT (Lcd->Yres) | IDMA_IMG_WIDTH (Lcd->Xres));
  DppWrite (DPP_IMG_SIZE, DPP_IMG_HEIGHT (Lcd->Yres) | DPP_IMG_WIDTH (Lcd->Xres));

  // No flip, no block mode
  DmaWriteMask (IDMA_IN_CON, IDMA_IN_FLIP (0), IDMA_IN_FLIP_MASK);
  DmaWriteMask (IDMA_IN_CON, 0, IDMA_BLOCK_EN);

  // Not compressed: BASE_ADDR_C is unused for a single-plane RGB format
  DmaWrite (IDMA_IN_BASE_ADDR_Y, FrameBufferAddress);
  DmaWrite (IDMA_IN_BASE_ADDR_C, 0);

  // XRGB8888 (little-endian word 0xXXRRGGBB = EFI BGRX), per-frame alpha
  DmaWriteMask (IDMA_IN_CON, IDMA_IMG_FORMAT (IDMA_IMG_FORMAT_XRGB8888), IDMA_IMG_FORMAT_MASK);
  DppWriteMask (DPP_IN_CON, DPP_ALPHA_SEL (0), DPP_ALPHA_SEL_MASK);
  DppWriteMask (DPP_IN_CON, DPP_IMG_FORMAT (0), DPP_IMG_FORMAT_MASK);
  DmaWriteMask (IDMA_RECOVERY_CTRL, 0, IDMA_RECOVERY_EN);
}

//
// Public API
//

STATIC
VOID
DeconProgramFrameBuffer (
  IN CONST DPU_LCD_INFO  *Lcd,
  IN UINT32              FrameBufferAddress
  )
{
  DECON_WINDOW_REGS  Win;

  DppConfigure (Lcd, FrameBufferAddress);

  DeconFillWindow (Lcd, 0xFF, &Win);
  DeconSetWindowControl (DECON_WIN_ID, &Win, FALSE);

  // Latch window + global shadow registers
  DeconWriteMask (
    SHADOW_REG_UPDATE_REQ,
    DECON_ALL_BITS,
    SHADOW_REG_UPDATE_REQ_WIN (DECON_WIN_ID) | SHADOW_REG_UPDATE_REQ_GLOBAL
    );
  DeconWaitUpdateDone (20 * 1000);
}

VOID
DeconSetFrameBuffer (
  IN CONST DPU_LCD_INFO  *Lcd,
  IN UINT32              FrameBufferAddress
  )
{
  DppInit ();
  DeconProgramFrameBuffer (Lcd, FrameBufferAddress);
}

VOID
DeconDisable (
  VOID
  )
{
  if (!DeconIsRunning ()) {
    return;
  }

  // decon_reg_stop(): per-frame off, latch, wait for the run status to drop
  DeconWriteMask (GLOBAL_CONTROL, 0, GLOBAL_CONTROL_DECON_EN_F);
  DeconWriteMask (SHADOW_REG_UPDATE_REQ, DECON_ALL_BITS, SHADOW_REG_UPDATE_REQ_GLOBAL);
  DeconWaitRunStatus (FALSE, 20 * 1000);
}

EFI_STATUS
DeconEnable (
  IN CONST DPU_LCD_INFO  *Lcd,
  IN UINT32              FrameBufferAddress
  )
{
  EFI_STATUS         Status;
  DECON_WINDOW_REGS  Win;

  DeconRegInit (Lcd);

  // Black window first: "blender configuration must be set before DECON
  // start, otherwise DECON will go into an abnormal state".
  DeconFillWindow (Lcd, 0, &Win);
  DeconSetWindowControl (DECON_WIN_ID, &Win, TRUE);
  DeconWriteMask (
    SHADOW_REG_UPDATE_REQ,
    DECON_ALL_BITS,
    SHADOW_REG_UPDATE_REQ_WIN (DECON_WIN_ID)
    );

  // decon_reg_start()
  DeconWriteMask (
    GLOBAL_CONTROL,
    DECON_ALL_BITS,
    GLOBAL_CONTROL_DECON_EN | GLOBAL_CONTROL_DECON_EN_F
    );
  DeconWriteMask (SHADOW_REG_UPDATE_REQ, DECON_ALL_BITS, SHADOW_REG_UPDATE_REQ_GLOBAL);

  Status = DeconWaitRunStatus (TRUE, 20 * 1000);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // One full frame of black before the image appears
  MicroSecondDelay (18 * 1000);

  DppInit ();
  DeconProgramFrameBuffer (Lcd, FrameBufferAddress);

  return EFI_SUCCESS;
}
