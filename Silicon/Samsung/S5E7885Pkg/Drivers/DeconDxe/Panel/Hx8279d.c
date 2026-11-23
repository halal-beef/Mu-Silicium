/** @file
  HX8279D panel (Galaxy Tab A 10.1 2019, gta3xllte) initialisation.

  Mirrors hx8279d_init() / hx8279d_displayon() from the SM-T515 downstream
  kernel, drivers/video/fbdev/exynos/dpu_7885/panels/hx8279d_gta3xllte_lcd_ctrl.c
  (GPL-2.0, Copyright (c) Samsung Electronics Co., Ltd.).

  The panel never gets a sleep-out (0x11) / display-on (0x29); it is a
  vendor-register part: 0xB0 selects a page, 0xB3 holds the display on/off
  control and 0xB8 the backlight level.
**/

#include "../S5E7885Display.h"
#include "Hx8279dParam.h"

STATIC BOOLEAN  mVariantVe = FALSE;

STATIC
EFI_STATUS
PanelWrite (
  IN CONST UINT8  *Cmd,
  IN UINT32       Size
  )
{
  EFI_STATUS  Status;
  UINT32      Try;

  Status = EFI_DEVICE_ERROR;

  // The downstream driver makes two attempts per packet
  for (Try = 0; Try < 2; Try++) {
    if (Size == 1) {
      Status = DsimWriteShort (MIPI_DSI_DCS_SHORT_WRITE, Cmd[0], 0);
    } else if (Size == 2) {
      Status = DsimWriteShort (MIPI_DSI_DCS_SHORT_WRITE_PARAM, Cmd[0], Cmd[1]);
    } else {
      Status = DsimWriteLong (MIPI_DSI_DCS_LONG_WRITE, Cmd, Size);
    }

    if (!EFI_ERROR (Status)) {
      break;
    }
  }

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "HX8279D: write of 0x%02x failed (%r)\n", Cmd[0], Status));
  }

  return Status;
}

STATIC
EFI_STATUS
PanelWriteTable (
  IN CONST HX8279D_CMD  *Table,
  IN UINTN              Count
  )
{
  UINTN       Index;
  EFI_STATUS  Status;

  for (Index = 0; Index < Count; Index++) {
    Status = PanelWrite (Table[Index].Cmd, 2);
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "HX8279D: init table entry %u (0x%02x, 0x%02x) failed\n",
        (UINT32)Index,
        Table[Index].Cmd[0],
        Table[Index].Cmd[1]
        ));
      return Status;
    }
  }

  return EFI_SUCCESS;
}

STATIC
VOID
PanelDetectVariant (
  VOID
  )
{
#if HX8279D_FORCE_VARIANT == 1
  mVariantVe = FALSE;
#elif HX8279D_FORCE_VARIANT == 2
  mVariantVe = TRUE;
#else
  UINT8  Id;
  INTN   Ret;
  UINT32 Try;

  // id[2] comes from the DUAL register (0xB1), as in hx8279d_read_id().
  Id  = 0;
  Ret = -1;
  for (Try = 0; Try < 2; Try++) {
    Ret = DsimReadData (HX8279D_DUAL_REG, 1, &Id);
    if (Ret == 1) {
      break;
    }
  }

  if (Ret != 1) {
    DEBUG ((DEBUG_WARN, "HX8279D: ID read failed, assuming the standard variant\n"));
    mVariantVe = FALSE;
    return;
  }

  mVariantVe = (Id == HX8279D_ID_VARIANT_VE);
#endif

  DEBUG ((DEBUG_INFO, "HX8279D: using the %a gamma/brightness tables\n", mVariantVe ? "VE" : "standard"));
}

STATIC
EFI_STATUS
PanelSetBrightness (
  IN UINT32  Level
  )
{
  UINT8       Page4[2];
  UINT8       Bl[2];
  EFI_STATUS  Status;

  if (Level > HX8279D_EXTEND_BRIGHTNESS) {
    Level = HX8279D_EXTEND_BRIGHTNESS;
  }

  Page4[0] = 0xB0;
  Page4[1] = 0x04;
  Bl[0]    = 0xB8;
  Bl[1]    = (UINT8)(mVariantVe ? mHx8279dBrightnessVe[Level] : mHx8279dBrightness[Level]);

  Status = PanelWrite (Page4, 2);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  DEBUG ((DEBUG_INFO, "HX8279D: brightness %u -> 0x%02x\n", Level, Bl[1]));
  return PanelWrite (Bl, 2);
}

EFI_STATUS
Hx8279dInit (
  VOID
  )
{
  STATIC CONST UINT8  SeqTable0[]    = { 0xB0, 0x00 };
  STATIC CONST UINT8  SeqTable4[]    = { 0xB0, 0x04 };
  STATIC CONST UINT8  SeqTable5[]    = { 0xB0, 0x05 };
  STATIC CONST UINT8  SeqEotpOff[]   = { 0xB1, 0xE5 };
  STATIC CONST UINT8  SeqTlpx80ns[]  = { 0xB3, 0x52 };
  STATIC CONST UINT8  SeqScreenOff[] = { 0xB3, 0x88 };
  STATIC CONST UINT8  SeqBl00[]      = { 0xB8, 0x00 };
  EFI_STATUS          Status;

  // Preamble. Failures here are logged by PanelWrite() but, as in the
  // vendor driver, do not abort the sequence.
  PanelWrite (SeqTable5, 2);
  PanelWrite (SeqEotpOff, 2);
  PanelWrite (SeqTlpx80ns, 2);
  PanelWrite (SeqTable0, 2);
  PanelWrite (SeqScreenOff, 2);
  PanelWrite (SeqTable4, 2);
  PanelWrite (SeqBl00, 2);
  PanelWrite (SeqTable0, 2);

  PanelDetectVariant ();

  if (mVariantVe) {
    Status = PanelWriteTable (mHx8279dInitTableVe, HX8279D_INIT_TABLE_VE_COUNT);
  } else {
    Status = PanelWriteTable (mHx8279dInitTable, HX8279D_INIT_TABLE_COUNT);
  }

  if (EFI_ERROR (Status)) {
    return Status;
  }

  MicroSecondDelay (110 * 1000);
  return EFI_SUCCESS;
}

EFI_STATUS
Hx8279dDisplayOn (
  VOID
  )
{
  STATIC CONST UINT8  SeqTable0[]   = { 0xB0, 0x00 };
  STATIC CONST UINT8  SeqDisplayOn[] = { 0xB3, 0x08 };
  EFI_STATUS          Status;

  Status = PanelWrite (SeqTable0, 2);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = PanelWrite (SeqDisplayOn, 2);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  MicroSecondDelay (40 * 1000);

  return PanelSetBrightness (HX8279D_UI_DEFAULT_BRIGHT);
}
