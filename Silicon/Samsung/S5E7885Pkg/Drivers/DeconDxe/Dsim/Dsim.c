/** @file
  Exynos 7885 DSIM (MIPI DSI host) driver.

  The register sequences are ported from the Samsung SM-T515 downstream
  kernel, drivers/video/fbdev/exynos/dpu_7885/{dsim_reg.c,dsim_drv.c}
  (GPL-2.0, Copyright (c) Samsung Electronics Co., Ltd.). Everything is
  polled; no interrupt is routed anywhere.

  Order of use (mirrors dsim_enable()):
    DsimInit  -> PHY/PLL, lanes, link config. Lanes sit in LP-11 afterwards.
    (caller resets the panel here: "panel reset should be set after LP-11")
    DsimStart -> HS clock on.
    DsimWriteShort, DsimWriteLong, DsimReadData -> panel commands (LP escape mode).
**/

#include "../S5E7885Display.h"
#include "../Regs/RegsDsim.h"
#include "DsimPhyTable.h"

#define DSIM_BASE                 S5E7885_DSIM0_BASE

#define DSIM_PLL_STABLE_TIME      0x15F90U
#define DSIM_PH_FIFOCTRL_THRESH   32U
#define DSIM_STABLE_VFP_VALUE     2U
#define DSIM_STOP_STATE_CNT       0xAU
#define DSIM_BTA_TIMEOUT          0xFFU
#define DSIM_LP_RX_TIMEOUT        0xFFFFU
#define DSIM_PIXEL_FORMAT_RGB24   0x0U
#define DSIM_RX_FIFO_MAX_DEPTH    64U
#define DSIM_PL_FIFO_THRESHOLD    2048U

#define DSIM_LANE_CLOCK           BIT0

// MIPI DSI receive packet data types
#define MIPI_DSI_RX_ACK_AND_ERROR_REPORT       0x02
#define MIPI_DSI_RX_END_OF_TRANSMISSION        0x08
#define MIPI_DSI_RX_GENERIC_SHORT_READ_1BYTE   0x11
#define MIPI_DSI_RX_GENERIC_SHORT_READ_2BYTE   0x12
#define MIPI_DSI_RX_GENERIC_LONG_READ          0x1A
#define MIPI_DSI_RX_DCS_LONG_READ              0x1C
#define MIPI_DSI_RX_DCS_SHORT_READ_1BYTE       0x21
#define MIPI_DSI_RX_DCS_SHORT_READ_2BYTE       0x22

// "VSTATUS interrupt select" field value for DSIM_VSYNC (dsim.h enum: VBP=0, VSYNC=1)
#define DSIM_VSYNC_SEL            1U

// Peripheral error report bits (Error_Range[13:0])
#define MIPI_DSI_ERR_BIT_MASK     0x3F3FU

#define DSIM_ALL_BITS             0xFFFFFFFFU

STATIC UINT32  mDataLaneMask;

STATIC
UINT32
DsimRead (
  IN UINT32  Reg
  )
{
  return MmioRead32 (DSIM_BASE + Reg);
}

STATIC
VOID
DsimWrite (
  IN UINT32  Reg,
  IN UINT32  Val
  )
{
  MmioWrite32 (DSIM_BASE + Reg, Val);
}

STATIC
VOID
DsimWriteMask (
  IN UINT32  Reg,
  IN UINT32  Val,
  IN UINT32  Mask
  )
{
  UINT32  Old;

  Old = DsimRead (Reg);
  DsimWrite (Reg, (Val & Mask) | (Old & ~Mask));
}

STATIC
UINT32
DsimBool (
  IN UINT32  En
  )
{
  return (En != 0) ? DSIM_ALL_BITS : 0;
}

STATIC
VOID
DsimClearInt (
  IN UINT32  Src
  )
{
  DsimWrite (DSIM_INTSRC, Src);
}

STATIC
VOID
DsimSetInt (
  IN UINT32  En
  )
{
  UINT32  Val;
  UINT32  Mask;

  Val  = (En != 0) ? 0 : DSIM_ALL_BITS;
  Mask = DSIM_INTMSK_SW_RST_RELEASE | DSIM_INTMSK_SFR_PL_FIFO_EMPTY |
         DSIM_INTMSK_SFR_PH_FIFO_EMPTY | DSIM_INTMSK_FRAME_DONE |
         DSIM_INTMSK_INVALID_SFR_VALUE | DSIM_INTMSK_UNDER_RUN |
         DSIM_INTMSK_RX_DATA_DONE | DSIM_INTMSK_ERR_RX_ECC |
         DSIM_INTMSK_VT_STATUS;

  DsimWriteMask (DSIM_INTMSK, Val, Mask);
}

STATIC
VOID
DsimSwReset (
  VOID
  )
{
  UINT32  Cnt;
  UINT32  State;

  Cnt = 1000;
  DsimWriteMask (DSIM_SWRST, DSIM_ALL_BITS, DSIM_SWRST_RESET);

  do {
    State = DsimRead (DSIM_SWRST) & DSIM_SWRST_RESET;
    Cnt--;
    MicroSecondDelay (10);
  } while (State != 0 && Cnt != 0);

  if (Cnt == 0) {
    DEBUG ((DEBUG_ERROR, "DSIM: software reset timed out\n"));
  }
}

STATIC
VOID
DsimDphyReset (
  VOID
  )
{
  // Reset is active low
  DsimWriteMask (DSIM_SWRST, 0, DSIM_DPHY_RST);
  DsimWriteMask (DSIM_SWRST, DSIM_ALL_BITS, DSIM_DPHY_RST);
}

STATIC
BOOLEAN
DsimIsPllStable (
  VOID
  )
{
  return (DsimRead (DSIM_LINK_STATUS3) & DSIM_LINK_STATUS3_PLL_STABLE) != 0;
}

STATIC
EFI_STATUS
DsimEnablePll (
  IN UINT32  En
  )
{
  UINT32  Cnt;

  if (En == 0) {
    DsimWriteMask (DSIM_PLLCTRL, 0, DSIM_PLLCTRL_PLL_EN);
    return EFI_SUCCESS;
  }

  Cnt = 1000;
  DsimClearInt (DSIM_INTSRC_PLL_STABLE);
  DsimWriteMask (DSIM_PLLCTRL, DSIM_ALL_BITS, DSIM_PLLCTRL_PLL_EN);

  while (TRUE) {
    Cnt--;
    if (DsimIsPllStable ()) {
      return EFI_SUCCESS;
    }

    if (Cnt == 0) {
      return EFI_TIMEOUT;
    }

    MicroSecondDelay (10);
  }
}

STATIC
VOID
DsimSetEscClkPrescaler (
  IN UINT32  En,
  IN UINT32  Div
  )
{
  UINT32  Val;
  UINT32  Mask;

  Val  = (En != 0) ? DSIM_CLK_CTRL_ESCCLK_EN : 0;
  Mask = DSIM_CLK_CTRL_ESCCLK_EN | DSIM_CLK_CTRL_ESC_PRESCALER_MASK;
  Val |= DSIM_CLK_CTRL_ESC_PRESCALER (Div);
  DsimWriteMask (DSIM_CLK_CTRL, Val, Mask);
}

STATIC
VOID
DsimSetPllFreq (
  IN UINT32  P,
  IN UINT32  M,
  IN UINT32  S
  )
{
  UINT32  Val;

  Val = ((P & 0x3F) << 13) | ((M & 0x3FF) << 3) | ((S & 0x7) << 0);
  DsimWriteMask (DSIM_PLLCTRL, Val, DSIM_PLLCTRL_PMS_MASK);
}

STATIC
EFI_STATUS
DsimSetClocks (
  IN CONST DPU_LCD_INFO  *Lcd
  )
{
  UINT32      HsClk;
  UINT32      ByteClk;
  UINT32      EscClk;
  UINT32      EscDiv;
  UINT32      Row;
  CONST UINT32 *T;
  UINT32      Fin;

  Fin = 26;

  HsClk   = (Lcd->PllM * Fin) / (Lcd->PllP * (1U << Lcd->PllS));
  ByteClk = HsClk / 8;

  EscDiv = ByteClk / Lcd->EscClkMhz;
  if ((ByteClk / EscDiv) > Lcd->EscClkMhz) {
    EscDiv += 1;
  }

  EscClk = ByteClk / EscDiv;

  DEBUG ((
    DEBUG_INFO,
    "DSIM: hs %u MHz, byte %u MHz, esc %u MHz (div %u)\n",
    HsClk,
    ByteClk,
    EscClk,
    EscDiv
    ));

  DsimSetEscClkPrescaler (1, EscDiv);

  // D-PHY timing values, indexed by distance from the top row (1500 Mbps)
  if (HsClk > DPHY_TIMING_TOP_MBPS) {
    return EFI_UNSUPPORTED;
  }

  Row = (DPHY_TIMING_TOP_MBPS - HsClk) / 10;
  if (Row >= (sizeof (mDphyTiming) / sizeof (mDphyTiming[0]))) {
    DEBUG ((DEBUG_ERROR, "DSIM: no D-PHY timing for %u MHz\n", HsClk));
    return EFI_UNSUPPORTED;
  }

  if ((EscClk > 20) || (EscClk < 7)) {
    DEBUG ((DEBUG_ERROR, "DSIM: %u MHz is not a usable escape clock\n", EscClk));
    return EFI_UNSUPPORTED;
  }

  T = mDphyTiming[Row];

  DsimWrite (
    DSIM_PHY_TIMING,
    DSIM_PHY_TIMING_M_TLPX_CTL (T[8]) | DSIM_PHY_TIMING_M_THSEXIT_CTL (T[9])
    );
  DsimWrite (
    DSIM_PHY_TIMING1,
    DSIM_PHY_TIMING1_M_TCLKPRPR_CTL (T[1]) |
    DSIM_PHY_TIMING1_M_TCLKZERO_CTL (T[2]) |
    DSIM_PHY_TIMING1_M_TCLKPOST_CTL (T[3]) |
    DSIM_PHY_TIMING1_M_TCLKTRAIL_CTL (T[4])
    );
  DsimWrite (
    DSIM_PHY_TIMING2,
    DSIM_PHY_TIMING2_M_THSPRPR_CTL (T[5]) |
    DSIM_PHY_TIMING2_M_THSZERO_CTL (T[6]) |
    DSIM_PHY_TIMING2_M_THSTRAIL_CTL (T[7])
    );
  DsimWrite (
    DSIM_PHYCTRL_B1,
    DSIM_PHYCTRL_B1_B_DPHYCTL (mBDphyCtl[EscClk - 7])
    );

  // DSIM_PHY_CHARIC_VAL[0]: only M_PLL_CTRL1 is non-zero for DSIM0
  DsimWrite (DSIM_PLL_CTRL1, 0x40000000U);
  DsimWrite (DSIM_PLL_CTRL2, 0x0);
  DsimWrite (DSIM_PHYCTRL_B2, 0x0);
  DsimWrite (DSIM_PHYCTRL_M1, 0x0);
  DsimWrite (DSIM_PHYCTRL_M2, 0x0);

  DsimSetPllFreq (Lcd->PllP, Lcd->PllM, Lcd->PllS);
  DsimWrite (DSIM_PLLTMR, DSIM_PLL_STABLE_TIME);

  return DsimEnablePll (1);
}

STATIC
VOID
DsimSetLanes (
  IN UINT32  Lanes,
  IN UINT32  En
  )
{
  DsimWriteMask (DSIM_CONFIG, DsimBool (En), DSIM_CONFIG_LANES_EN (Lanes));
  MicroSecondDelay (400);
}

STATIC
VOID
DsimSetEscClkOnLane (
  IN UINT32  En,
  IN UINT32  Lanes
  )
{
  UINT32  Lane;
  UINT32  Val;

  Lane = (Lanes >> 1) | (1U << 4);
  Val  = (En != 0) ? DSIM_CLK_CTRL_LANE_ESCCLK_EN (Lane) : 0;
  DsimWriteMask (DSIM_CLK_CTRL, Val, DSIM_CLK_CTRL_LANE_ESCCLK_EN_MASK);
}

STATIC
VOID
DsimEnableWordClock (
  IN UINT32  En
  )
{
  DsimWriteMask (DSIM_CLK_CTRL, DsimBool (En), DSIM_CLK_CTRL_WORDCLK_EN);
}

STATIC
EFI_STATUS
DsimSetHsClock (
  IN UINT32  En
  )
{
  UINT32  Cnt;
  UINT32  State;
  UINT32  NonCont;

  NonCont = DsimRead (DSIM_CLK_CTRL) & DSIM_CLK_CTRL_NONCONT_CLOCK_LANE;
  Cnt     = 1000;

  if (En != 0) {
    DsimWriteMask (DSIM_CLK_CTRL, DSIM_ALL_BITS, DSIM_CLK_CTRL_TX_REQUEST_HSCLK);
    if (NonCont != 0) {
      return EFI_SUCCESS;
    }

    do {
      State = DsimRead (DSIM_DPHY_STATUS) & DSIM_DPHY_STATUS_TX_READY_HS_CLK;
      Cnt--;
      MicroSecondDelay (10);
    } while (State == 0 && Cnt != 0);

    if (Cnt == 0) {
      DEBUG ((DEBUG_ERROR, "DSIM: master is not in HS state\n"));
      return EFI_TIMEOUT;
    }
  } else {
    DsimWriteMask (DSIM_CLK_CTRL, 0, DSIM_CLK_CTRL_TX_REQUEST_HSCLK);
    do {
      // Use the link's clock lane status rather than the D-PHY stop state
      State = (DSIM_LINK_STATUS2_CLK_LANE_STATUS_GET (DsimRead (DSIM_LINK_STATUS2)) == 0) ? 1 : 0;
      Cnt--;
      MicroSecondDelay (10);
    } while (State == 0 && Cnt != 0);

    if (Cnt == 0) {
      DEBUG ((DEBUG_ERROR, "DSIM: clock lane did not return to LP\n"));
      return EFI_TIMEOUT;
    }
  }

  return EFI_SUCCESS;
}

STATIC
VOID
DsimSetConfig (
  IN CONST DPU_LCD_INFO  *Lcd
  )
{
  UINT32  Threshold;
  UINT32  NumOfTransfer;

  // Video mode: clock lane stays on between lines if requested
  DsimWriteMask (DSIM_CLK_CTRL, DsimBool (Lcd->ClkLaneOnOff), DSIM_CLK_CTRL_CLKLANE_ONOFF);

  // HW deskew off
  DsimWriteMask (DSIM_DESKEW_CTRL, 0, DSIM_DESKEW_CTRL_HW_EN);

  DsimWriteMask (
    DSIM_TIMEOUT,
    DSIM_TIMEOUT_BTA_TOUT (DSIM_BTA_TIMEOUT),
    DSIM_TIMEOUT_BTA_TOUT_MASK
    );
  DsimWriteMask (
    DSIM_TIMEOUT,
    DSIM_TIMEOUT_LPDR_TOUT (DSIM_LP_RX_TIMEOUT),
    DSIM_TIMEOUT_LPDR_TOUT_MASK
    );
  DsimWriteMask (
    DSIM_ESCMODE,
    DSIM_ESCMODE_STOP_STATE_CNT (DSIM_STOP_STATE_CNT),
    DSIM_ESCMODE_STOP_STATE_CNT_MASK
    );

  Threshold = Lcd->Xres;
  DsimWriteMask (DSIM_THRESHOLD, DSIM_THRESHOLD_LEVEL (Threshold), DSIM_THRESHOLD_LEVEL_MASK);

  DsimWriteMask (DSIM_RESOL, DSIM_RESOL_VRESOL (Lcd->Yres), DSIM_RESOL_VRESOL_MASK);
  DsimWriteMask (DSIM_RESOL, DSIM_RESOL_HRESOL (Lcd->Xres), DSIM_RESOL_HRESOL_MASK);

  // Porch (video mode)
  DsimWriteMask (DSIM_VPORCH, DSIM_VPORCH_VBP (Lcd->Vbp), DSIM_VPORCH_VBP_MASK);
  DsimWriteMask (DSIM_VPORCH, DSIM_VPORCH_VFP_TOTAL (Lcd->Vfp), DSIM_VPORCH_VFP_TOTAL_MASK);
  DsimWriteMask (
    DSIM_VPORCH,
    DSIM_VPORCH_STABLE_VFP (DSIM_STABLE_VFP_VALUE),
    DSIM_VPORCH_STABLE_VFP_MASK
    );
  DsimWriteMask (
    DSIM_VPORCH,
    DSIM_VPORCH_VFP_CMD_ALLOW (Lcd->Vfp - DSIM_STABLE_VFP_VALUE - 3),
    DSIM_VPORCH_VFP_CMD_ALLOW_MASK
    );
  DsimWriteMask (DSIM_HPORCH, DSIM_HPORCH_HBP (Lcd->Hbp), DSIM_HPORCH_HBP_MASK);
  DsimWriteMask (DSIM_HPORCH, DSIM_HPORCH_HFP (Lcd->Hfp), DSIM_HPORCH_HFP_MASK);
  DsimWriteMask (DSIM_SYNC, DSIM_SYNC_VSA (Lcd->Vsa), DSIM_SYNC_VSA_MASK);
  DsimWriteMask (DSIM_SYNC, DSIM_SYNC_HSA (Lcd->Hsa), DSIM_SYNC_HSA_MASK);

  NumOfTransfer = Lcd->Xres * Lcd->Yres / Threshold;
  DsimWriteMask (
    DSIM_NUM_OF_TRANSFER,
    DSIM_NUM_OF_TRANSFER_PER_FRAME (NumOfTransfer),
    DSIM_NUM_OF_TRANSFER_PER_FRAME_MASK
    );

  DsimWriteMask (
    DSIM_CONFIG,
    DSIM_CONFIG_NUM_OF_DATA_LANE (Lcd->DataLanes - 1),
    DSIM_CONFIG_NUM_OF_DATA_LANE_MASK
    );
  DsimWriteMask (DSIM_CONFIG, DsimBool (Lcd->EotpDisabled == 0), DSIM_CONFIG_EOTP_EN);
  DsimWriteMask (DSIM_CONFIG, 0, DSIM_CONFIG_PER_FRAME_READ_EN);
  DsimWriteMask (
    DSIM_CONFIG,
    DSIM_CONFIG_PIXEL_FORMAT (DSIM_PIXEL_FORMAT_RGB24),
    DSIM_CONFIG_PIXEL_FORMAT_MASK
    );
  DsimWriteMask (DSIM_CONFIG, DSIM_CONFIG_VC_ID (0), DSIM_CONFIG_VC_ID_MASK);
  DsimWriteMask (DSIM_CONFIG, DSIM_ALL_BITS, DSIM_CONFIG_VIDEO_MODE);
  DsimWriteMask (DSIM_CONFIG, 0, DSIM_CONFIG_CPRS_EN);

  DsimWriteMask (DSIM_CONFIG, 0, DSIM_CONFIG_HSA_DISABLE);
  DsimWriteMask (DSIM_CONFIG, 0, DSIM_CONFIG_HBP_DISABLE);
  DsimWriteMask (DSIM_CONFIG, DSIM_ALL_BITS, DSIM_CONFIG_HFP_DISABLE);
  DsimWriteMask (DSIM_CONFIG, 0, DSIM_CONFIG_HSE_DISABLE);
  DsimWriteMask (DSIM_CONFIG, DSIM_ALL_BITS, DSIM_CONFIG_BURST_MODE);
  DsimWriteMask (DSIM_CONFIG, 0, DSIM_CONFIG_SYNC_INFORM);

  DsimWriteMask (
    DSIM_CMD_CONFIG,
    DSIM_CMD_CONFIG_MULTI_PKT_CNT (DSIM_PH_FIFOCTRL_THRESH),
    DSIM_CMD_CONFIG_MULTI_PKT_CNT_MASK
    );
  DsimWriteMask (DSIM_CMD_CONFIG, 0, DSIM_CMD_CONFIG_MULTI_CMD_PKT_EN);
  DsimWriteMask (DSIM_CMD_CONFIG, 0, DSIM_CMD_CONFIG_PKT_GO_EN);

  DsimWriteMask (
    DSIM_VIDEO_TIMER,
    DSIM_VIDEO_TIMER_COMPENSATE (Lcd->VtCompensation),
    DSIM_VIDEO_TIMER_COMPENSATE_MASK
    );
  DsimWriteMask (
    DSIM_VIDEO_TIMER,
    DSIM_VIDEO_TIMER_VSTATUS_INTR_SEL (DSIM_VSYNC_SEL),
    DSIM_VIDEO_TIMER_VSTATUS_INTR_SEL_MASK
    );

  // 1 byte PPI
  DsimWriteMask (DSIM_CSIS_LB, DSIM_ALL_BITS, DSIM_CSIS_LB_1BYTEPPI_MODE);

  DsimWriteMask (DSIM_SFR_CTRL, DSIM_ALL_BITS, DSIM_SFR_CTRL_SHADOW_REG_READ_EN);
  DsimWriteMask (DSIM_SFR_CTRL, DSIM_ALL_BITS, DSIM_SFR_CTRL_SHADOW_EN);
}

BOOLEAN
DsimIsActive (
  VOID
  )
{
  return ((DsimRead (DSIM_CLK_CTRL) & DSIM_CLK_CTRL_TX_REQUEST_HSCLK) != 0) ||
         DsimIsPllStable ();
}

EFI_STATUS
DsimInit (
  IN CONST DPU_LCD_INFO  *Lcd
  )
{
  EFI_STATUS  Status;

  mDataLaneMask = DSIM_LANE_CLOCK | (((1U << Lcd->DataLanes) - 1U) << 1);

  DsimDphyReset ();
  DsimSwReset ();

  Status = DsimSetClocks (Lcd);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "DSIM: PHY PLL did not lock (%r)\n", Status));
    return Status;
  }

  DsimSetLanes (mDataLaneMask, 1);
  DsimSetEscClkOnLane (1, mDataLaneMask);
  DsimEnableWordClock (1);

  DsimSetConfig (Lcd);

  return EFI_SUCCESS;
}

VOID
DsimStart (
  VOID
  )
{
  DsimSetHsClock (1);
  DsimSetInt (1);
}

VOID
DsimStop (
  VOID
  )
{
  DsimClearInt (DSIM_ALL_BITS);
  DsimSetInt (0);

  if (EFI_ERROR (DsimSetHsClock (0))) {
    DEBUG ((DEBUG_WARN, "DSIM: clock lane was not switched to LP\n"));
  }

  if (mDataLaneMask == 0) {
    mDataLaneMask = 0x1F;
  }

  DsimSetLanes (mDataLaneMask, 0);
  DsimSetEscClkOnLane (0, mDataLaneMask);

  // Clocks off
  DsimWrite (DSIM_PLL_CTRL1, 0x0);
  DsimWrite (DSIM_PLL_CTRL2, 0x0);
  DsimSetEscClkPrescaler (0, 0xFF);
  DsimEnablePll (0);

  DsimSwReset ();
}

//
// Packet transfer
//

STATIC
BOOLEAN
DsimWaitFifoEmpty (
  IN UINT32  Mask
  )
{
  UINT32  Cnt;

  for (Cnt = 5000; Cnt != 0; Cnt--) {
    if ((DsimRead (DSIM_FIFOCTRL) & Mask) == Mask) {
      return TRUE;
    }

    MicroSecondDelay (10);
  }

  return FALSE;
}

STATIC
VOID
DsimWriteTxHeader (
  IN UINT32   DataId,
  IN UINT32   Data0,
  IN UINT32   Data1,
  IN BOOLEAN  Bta
  )
{
  UINT32  Val;

  Val = DSIM_PKTHDR_BTA_TYPE (Bta ? 1 : 0) | DSIM_PKTHDR_ID (DataId) |
        DSIM_PKTHDR_DATA0 (Data0) | DSIM_PKTHDR_DATA1 (Data1);
  DsimWriteMask (DSIM_PKTHDR, Val, DSIM_PKTHDR_DATA);
}

EFI_STATUS
DsimWriteShort (
  IN UINT32  DataId,
  IN UINT32  Data0,
  IN UINT32  Data1
  )
{
  // Wait for earlier packets to leave the SFR FIFOs
  if (!DsimWaitFifoEmpty (DSIM_FIFOCTRL_EMPTY_PH_SFR)) {
    DEBUG ((DEBUG_ERROR, "DSIM: PH FIFO not empty before id %02x\n", DataId));
    return EFI_TIMEOUT;
  }

  DsimWriteMask (DSIM_CMD_CONFIG, DSIM_ALL_BITS, DSIM_CMD_CONFIG_PKT_GO_EN);
  DsimWriteTxHeader (DataId, Data0, Data1, FALSE);
  DsimWriteMask (DSIM_CMD_CONFIG, 0, DSIM_CMD_CONFIG_PKT_GO_EN);

  if (!DsimWaitFifoEmpty (DSIM_FIFOCTRL_EMPTY_PH_SFR)) {
    DEBUG ((DEBUG_ERROR, "DSIM: PH FIFO did not drain after id %02x\n", DataId));
    return EFI_TIMEOUT;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
DsimWriteLong (
  IN UINT32       DataId,
  IN CONST UINT8  *Payload,
  IN UINT32       Size
  )
{
  UINT32  Index;
  UINT32  Word;
  UINT32  Rem;
  UINT32  Whole;

  if (Size > DSIM_PL_FIFO_THRESHOLD) {
    return EFI_BAD_BUFFER_SIZE;
  }

  if (!DsimWaitFifoEmpty (DSIM_FIFOCTRL_EMPTY_PH_SFR | DSIM_FIFOCTRL_EMPTY_PL_SFR)) {
    DEBUG ((DEBUG_ERROR, "DSIM: FIFOs not empty before long write\n"));
    return EFI_TIMEOUT;
  }

  DsimWriteMask (DSIM_CMD_CONFIG, DSIM_ALL_BITS, DSIM_CMD_CONFIG_PKT_GO_EN);

  Whole = Size / 4;
  for (Index = 0; Index < Whole; Index++) {
    Word = (UINT32)Payload[0] | ((UINT32)Payload[1] << 8) |
           ((UINT32)Payload[2] << 16) | ((UINT32)Payload[3] << 24);
    DsimWrite (DSIM_PAYLOAD, Word);
    Payload += 4;
  }

  Rem = Size % 4;
  if (Rem != 0) {
    Word = 0;
    for (Index = 0; Index < Rem; Index++) {
      Word |= (UINT32)Payload[Index] << (Index * 8);
    }

    DsimWrite (DSIM_PAYLOAD, Word);
  }

  DsimWriteTxHeader (DataId, Size & 0xFF, (Size & 0xFF00) >> 8, FALSE);
  DsimWriteMask (DSIM_CMD_CONFIG, 0, DSIM_CMD_CONFIG_PKT_GO_EN);

  if (!DsimWaitFifoEmpty (DSIM_FIFOCTRL_EMPTY_PH_SFR | DSIM_FIFOCTRL_EMPTY_PL_SFR)) {
    DEBUG ((DEBUG_ERROR, "DSIM: FIFOs did not drain after long write\n"));
    return EFI_TIMEOUT;
  }

  return EFI_SUCCESS;
}

STATIC
INTN
DsimRxErrorCheck (
  IN UINT32  RxFifo
  )
{
  UINT32  ErrBit;

  ErrBit = RxFifo >> 8;
  if ((ErrBit & MIPI_DSI_ERR_BIT_MASK) == 0) {
    return 0;
  }

  DEBUG ((DEBUG_ERROR, "DSIM: peripheral error report 0x%04x (rx_fifo 0x%08x)\n", ErrBit, RxFifo));
  return -1;
}

INTN
DsimReadData (
  IN  UINT32  Addr,
  IN  UINT32  Count,
  OUT UINT8   *Buffer
  )
{
  UINT32  RxFifo;
  UINT32  RxSize;
  UINT32  Depth;
  UINT32  Cnt;
  UINT32  I;
  UINT32  J;
  INTN    Ret;
  BOOLEAN Done;

  RxSize = 0;
  Depth  = DSIM_RX_FIFO_MAX_DEPTH;

  // The read is issued before the video stream starts, so there is no need
  // to wait for a safe line count like the kernel does while streaming.
  DsimClearInt (DSIM_INTSRC_RX_DATA_DONE);

  if (EFI_ERROR (DsimWriteShort (MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE, Count, 0))) {
    return -1;
  }

  if (!DsimWaitFifoEmpty (DSIM_FIFOCTRL_EMPTY_PH_SFR)) {
    return -1;
  }

  DsimWriteMask (DSIM_CMD_CONFIG, DSIM_ALL_BITS, DSIM_CMD_CONFIG_PKT_GO_EN);
  DsimWriteTxHeader (MIPI_DSI_DCS_READ, Addr, 0, TRUE);
  DsimWriteMask (DSIM_CMD_CONFIG, 0, DSIM_CMD_CONFIG_PKT_GO_EN);

  Done = FALSE;
  for (Cnt = 10000; Cnt != 0; Cnt--) {
    if ((DsimRead (DSIM_INTSRC) & DSIM_INTSRC_RX_DATA_DONE) != 0) {
      Done = TRUE;
      break;
    }

    MicroSecondDelay (10);
  }

  if (!Done) {
    DEBUG ((DEBUG_ERROR, "DSIM: read of 0x%02x timed out\n", Addr));
    return -1;
  }

  Ret = 0;
  do {
    RxFifo = DsimRead (DSIM_RXFIFO);

    switch (RxFifo & 0xFF) {
      case MIPI_DSI_RX_ACK_AND_ERROR_REPORT:
        Ret = DsimRxErrorCheck (RxFifo);
        if (Ret < 0) {
          return Ret;
        }

        break;

      case MIPI_DSI_RX_END_OF_TRANSMISSION:
        break;

      case MIPI_DSI_RX_DCS_SHORT_READ_1BYTE:
      case MIPI_DSI_RX_DCS_SHORT_READ_2BYTE:
      case MIPI_DSI_RX_GENERIC_SHORT_READ_1BYTE:
      case MIPI_DSI_RX_GENERIC_SHORT_READ_2BYTE:
        for (I = 0; I < Count && I < 2; I++) {
          Buffer[I] = (UINT8)((RxFifo >> (8 + I * 8)) & 0xFF);
        }

        RxSize = (Count < 2) ? Count : 2;
        break;

      case MIPI_DSI_RX_DCS_LONG_READ:
      case MIPI_DSI_RX_GENERIC_LONG_READ:
        RxSize = (RxFifo & 0x00FFFF00) >> 8;
        if (RxSize > Count) {
          RxSize = Count;
        }

        for (I = 0; I < (RxSize >> 2); I++) {
          RxFifo = DsimRead (DSIM_RXFIFO);
          for (J = 0; J < 4; J++) {
            Buffer[(I * 4) + J] = (UINT8)((RxFifo >> (J * 8)) & 0xFF);
          }
        }

        if ((RxSize % 4) != 0) {
          RxFifo = DsimRead (DSIM_RXFIFO);
          for (J = 0; J < (RxSize % 4); J++) {
            Buffer[(4 * I) + J] = (UINT8)((RxFifo >> (J * 8)) & 0xFF);
          }
        }

        break;

      default:
        DEBUG ((DEBUG_ERROR, "DSIM: invalid RX packet 0x%08x\n", RxFifo));
        return -1;
    }

    Depth--;
  } while ((DsimRead (DSIM_FIFOCTRL) & DSIM_FIFOCTRL_EMPTY_RX) == 0 && Depth != 0);

  if (Depth == 0) {
    DEBUG ((DEBUG_ERROR, "DSIM: RX FIFO never drained, check D-PHY HS clock values\n"));
    return -1;
  }

  return (INTN)RxSize;
}
