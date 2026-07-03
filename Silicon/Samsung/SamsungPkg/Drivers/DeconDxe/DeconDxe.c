/*
 * DeconDxe -- Exynos9830 DECON0/DSIM0 + S6E3HAB (x1s target) bring-up.
 *
 * Ported from lk3rd's platform_init() -> display_drv_init() path:
 *   platform/exynos9830/dpu/decon_core.c, dsim_drv.c, dpp_drv.c
 *   platform/exynos9830/dpu/cal/ (decon_reg, dsim_reg, dpp_reg, format)
 *   target/x1s/dpu_panels/s6e3hab_mipi_lcd.c, s6e3hab_lcd_ctrl.c
 *
 * This driver intentionally does NOT implement GOP. It:
 *   1. Allocates a single 32-bit-addressable framebuffer.
 *   2. Runs the ported display_drv_init() sequence: DSIM0 PHY/link
 *      bring-up, S6E3HAB panel reset + MIPI command-mode init sequence,
 *      DECON0 window/DPP(IDMA G0) programming pointed at that buffer,
 *      and panel display-on.
 *   3. Publishes DECON_FRAMEBUFFER_INFO_PROTOCOL so a separate GOP
 *      driver can pick up the buffer and do the actual
 *      EFI_GRAPHICS_OUTPUT_PROTOCOL plumbing.
 *
 * No second (font) framebuffer/DPP channel is ever probed here -- see
 * decon_core.c's display_drv_init(), which only calls
 * dpp_probe(LOGO_DPP, ...) once.
 */

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>

#include <target/dpu_config.h>

#include "FramebufferInfo.h"

//EFI_GUID  gDeconFramebufferInfoProtocolGuid = DECON_FRAMEBUFFER_INFO_PROTOCOL_GUID;

/* Provided by Drv/decon_core.c (ported lk3rd display bring-up) */
extern int
display_drv_init (
  VOID
  );

extern VOID
decon_set_framebuffer_base (
  unsigned int  Addr
  );

/* Provided by Platform/DpuIoCtrl.c (ported x1s dpu_gpio.S) */
extern VOID
display_panel_init (
  VOID
  );

#define DECON_FB_BPP    4
#define DECON_FB_WIDTH  LCD_WIDTH
#define DECON_FB_HEIGHT LCD_HEIGHT
#define DECON_FB_SIZE   ((UINT64)DECON_FB_WIDTH * DECON_FB_HEIGHT * DECON_FB_BPP)

// hacky, just import s2dos5
#define GPP1CON			*(volatile unsigned int *)(0x10430020)
#define GPP1DAT			*(volatile unsigned int *)(0x10430024)
#define GPP1PUD			*(volatile unsigned int *)(0x10430028)

#define GPIO_DAT_S2DOS05	GPP1DAT
#define S2DOS05_GPIO_DAT_SHIFT	(1)
#define GPIO_PUD_S2DOS05	GPP1PUD &= ~(0xff << (0 * 4));

#define IIC_S2DOS05_ESCL_Hi	GPP1DAT |= (0x1 << 0)
#define IIC_S2DOS05_ESCL_Lo	GPP1DAT &= ~(0x1 << 0)
#define IIC_S2DOS05_ESDA_Hi	GPP1DAT |= (0x1 << 1)
#define IIC_S2DOS05_ESDA_Lo	GPP1DAT &= ~(0x1 << 1)

#define IIC_S2DOS05_ESCL_INP	GPP1CON &= ~(0xf << (0 * 4))
#define IIC_S2DOS05_ESCL_OUTP	GPP1CON = (GPP1CON & ~(0xf << (0 * 4))) \
	                                  | (0x1 << (0 * 4))
#define IIC_S2DOS05_ESDA_INP	GPP1CON &= ~(0xf << (1 * 4))
#define IIC_S2DOS05_ESDA_OUTP	GPP1CON = (GPP1CON & ~(0xf << (1 * 4))) \
	                                  | (0x1 << (1 * 4))

#define DELAY			100

/* S2DOS05 slave address */
#define S2DOS05_ADDR		0xC0

/* S2DOS05 Register Address */
#define S2DOS05_REG_EN		0x03
#define S2DOS05_LDO1_CFG	0x04
#define S2DOS05_LDO4_CFG	0x07

#define LDO_EN			(0x1 << 7)
#define LDO1_EN			(0x1 << 0)
#define LDO4_EN			(0x1 << 3)
#define BUCK_EN			(0x1 << 4)
static void Delay(void)
{
	unsigned long i = 0;

	for (i = 0; i < DELAY; i++)
		;
}

static void IIC_S2DOS05_SCLH_SDAH(void)
{
	IIC_S2DOS05_ESCL_Hi;
	IIC_S2DOS05_ESDA_Hi;
	Delay();
}

static void IIC_S2DOS05_SCLH_SDAL(void)
{
	IIC_S2DOS05_ESCL_Hi;
	IIC_S2DOS05_ESDA_Lo;
	Delay();
}

static void IIC_S2DOS05_SCLL_SDAH(void)
{
	IIC_S2DOS05_ESCL_Lo;
	IIC_S2DOS05_ESDA_Hi;
	Delay();
}

static void IIC_S2DOS05_SCLL_SDAL(void)
{
	IIC_S2DOS05_ESCL_Lo;
	IIC_S2DOS05_ESDA_Lo;
	Delay();
}

static void IIC_S2DOS05_ELow(void)
{
	IIC_S2DOS05_SCLL_SDAL();
	IIC_S2DOS05_SCLH_SDAL();
	IIC_S2DOS05_SCLH_SDAL();
	IIC_S2DOS05_SCLL_SDAL();
}

static void IIC_S2DOS05_EHigh(void)
{
	IIC_S2DOS05_SCLL_SDAH();
	IIC_S2DOS05_SCLH_SDAH();
	IIC_S2DOS05_SCLH_SDAH();
	IIC_S2DOS05_SCLL_SDAH();
}

static void IIC_S2DOS05_EStart(void)
{
	IIC_S2DOS05_SCLH_SDAH();
	IIC_S2DOS05_SCLH_SDAL();
	Delay();
	IIC_S2DOS05_SCLL_SDAL();
}

static void IIC_S2DOS05_EEnd(void)
{
	IIC_S2DOS05_SCLL_SDAL();
	IIC_S2DOS05_SCLH_SDAL();
	Delay();
	IIC_S2DOS05_SCLH_SDAH();
}

static void IIC_S2DOS05_EAck_write(void)
{
	unsigned long ack = 0;

	/* Function <- Input */
	IIC_S2DOS05_ESDA_INP;

	IIC_S2DOS05_ESCL_Lo;
	Delay();
	IIC_S2DOS05_ESCL_Hi;
	Delay();
	ack = GPIO_DAT_S2DOS05;
	IIC_S2DOS05_ESCL_Hi;
	Delay();
	IIC_S2DOS05_ESCL_Hi;
	Delay();

	/* Function <- Output (SDA) */
	IIC_S2DOS05_ESDA_OUTP;

	ack = (ack >> S2DOS05_GPIO_DAT_SHIFT) & 0x1;

	IIC_S2DOS05_SCLL_SDAL();
}

static void IIC_S2DOS05_EAck_read(void)
{
	/* Function <- Output */
	IIC_S2DOS05_ESDA_OUTP;

	IIC_S2DOS05_ESCL_Lo;
	IIC_S2DOS05_ESCL_Lo;
	IIC_S2DOS05_ESDA_Hi;
	IIC_S2DOS05_ESCL_Hi;
	IIC_S2DOS05_ESCL_Hi;
	/* Function <- Input (SDA) */
	IIC_S2DOS05_ESDA_INP;

	IIC_S2DOS05_SCLL_SDAL();
}

void IIC_S2DOS05_ESetport(void)
{
	/* Pull Up/Down Disable SCL, SDA */
	GPIO_PUD_S2DOS05;

	IIC_S2DOS05_ESCL_Hi;
	IIC_S2DOS05_ESDA_Hi;

	/* Function <- Output (SCL) */
	IIC_S2DOS05_ESCL_OUTP;
	/* Function <- Output (SDA) */
	IIC_S2DOS05_ESDA_OUTP;

	Delay();
}

void IIC_S2DOS05_EWrite(unsigned char ChipId,
                        unsigned char IicAddr, unsigned char IicData)
{
	unsigned long i = 0;

	IIC_S2DOS05_EStart();

	/* write chip id */
	for (i = 7; i > 0; i--) {
		if ((ChipId >> i) & 0x0001)
			IIC_S2DOS05_EHigh();
		else
			IIC_S2DOS05_ELow();
	}

	/* write */
	IIC_S2DOS05_ELow();

	/* ACK */
	IIC_S2DOS05_EAck_write();

	/* write reg. addr. */
	for (i = 8; i > 0; i--) {
		if ((IicAddr >> (i - 1)) & 0x0001)
			IIC_S2DOS05_EHigh();
		else
			IIC_S2DOS05_ELow();
	}

	/* ACK */
	IIC_S2DOS05_EAck_write();

	/* write reg. data. */
	for (i = 8; i > 0; i--) {
		if ((IicData >> (i - 1)) & 0x0001)
			IIC_S2DOS05_EHigh();
		else
			IIC_S2DOS05_ELow();
	}

	/* ACK */
	IIC_S2DOS05_EAck_write();

	IIC_S2DOS05_EEnd();
}

void IIC_S2DOS05_ERead(unsigned char ChipId,
                       unsigned char IicAddr, unsigned char *IicData)
{
	unsigned long i = 0;
	unsigned long reg = 0;
	unsigned char data = 0;

	IIC_S2DOS05_EStart();

	/* write chip id */
	for (i = 7; i > 0; i--) {
		if ((ChipId >> i) & 0x0001)
			IIC_S2DOS05_EHigh();
		else
			IIC_S2DOS05_ELow();
	}

	/* write */
	IIC_S2DOS05_ELow();

	/* ACK */
	IIC_S2DOS05_EAck_write();

	/* write reg. addr. */
	for (i = 8; i > 0; i--) {
		if ((IicAddr >> (i - 1)) & 0x0001)
			IIC_S2DOS05_EHigh();
		else
			IIC_S2DOS05_ELow();
	}

	/* ACK */
	IIC_S2DOS05_EAck_write();

	IIC_S2DOS05_EStart();

	/* write chip id */
	for (i = 7; i > 0; i--) {
		if ((ChipId >> i) & 0x0001)
			IIC_S2DOS05_EHigh();
		else
			IIC_S2DOS05_ELow();
	}

	/* read */
	IIC_S2DOS05_EHigh();
	/* ACK */
	IIC_S2DOS05_EAck_write();

	/* read reg. data. */
	IIC_S2DOS05_ESDA_INP;

	IIC_S2DOS05_ESCL_Lo;
	IIC_S2DOS05_ESCL_Lo;
	Delay();

	for (i = 8; i > 0; i--) {
		IIC_S2DOS05_ESCL_Lo;
		IIC_S2DOS05_ESCL_Lo;
		Delay();
		IIC_S2DOS05_ESCL_Hi;
		IIC_S2DOS05_ESCL_Hi;
		Delay();
		reg = GPIO_DAT_S2DOS05;
		IIC_S2DOS05_ESCL_Hi;
		IIC_S2DOS05_ESCL_Hi;
		Delay();
		IIC_S2DOS05_ESCL_Lo;
		IIC_S2DOS05_ESCL_Lo;
		Delay();

		reg = (reg >> S2DOS05_GPIO_DAT_SHIFT) & 0x1;

		data |= reg << (i - 1);
	}

	/* ACK */
	IIC_S2DOS05_EAck_read();
	IIC_S2DOS05_ESDA_OUTP;

	IIC_S2DOS05_EEnd();

	*IicData = data;
}

void pmic_init_s2dos05(void)
{
	unsigned char reg;

	IIC_S2DOS05_ESetport();

	/* Display power set up */
	IIC_S2DOS05_ERead(S2DOS05_ADDR, S2DOS05_REG_EN, &reg);
	printf("S2DOS05_REG_EN def: 0x%x\n", reg);
	reg |= (LDO1_EN | LDO4_EN | BUCK_EN);
	IIC_S2DOS05_EWrite(S2DOS05_ADDR, S2DOS05_REG_EN, reg);
	IIC_S2DOS05_ERead(S2DOS05_ADDR, S2DOS05_REG_EN, &reg);
	printf("S2DOS05_REG_EN set: 0x%x\n", reg);
}
//end

EFI_STATUS
EFIAPI
DeconDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS            Status;
  EFI_PHYSICAL_ADDRESS  FbBase;
  UINTN                 FbPages;
  INT32                 Ret;
  EFI_HANDLE            Handle;
  DECON_FRAMEBUFFER_INFO_PROTOCOL  *FbInfo;

  pmic_init_s2dos05();

  DEBUG ((DEBUG_INFO, "DeconDxe: bring-up start (S6E3HAB, %dx%d)\n",
    DECON_FB_WIDTH, DECON_FB_HEIGHT));

  //*(UINT32 *)(0x19050070) = 0x3070;

  /*
   * The exynos9830 DPU generation this CAL targets carries the
   * framebuffer address through 32-bit SFR/DMA address fields
   * (dma_addr_t is typedef'd u32 in dpu/decon.h), so the buffer must
   * be allocated below the 4GB line.
   */
  FbPages = EFI_SIZE_TO_PAGES (DECON_FB_SIZE);
  FbBase  = 0xF1000000;

  DEBUG ((DEBUG_INFO, "DeconDxe: framebuffer @ 0x%lx, size 0x%lx\n",
    FbBase, DECON_FB_SIZE));

  /* Panel reset/nRST GPIO pinmux -- was platform_early_init()'s
   * display_panel_init() call in lk3rd, ahead of display_drv_init(). */
  display_panel_init ();

  /* Tell the ported decon_core.c where the single (LOGO_DPP)
   * framebuffer lives before running the probe sequence. */
  decon_set_framebuffer_base ((unsigned int)FbBase);

  Ret = display_drv_init ();
  if (Ret < 0) {
    DEBUG ((DEBUG_ERROR, "DeconDxe: display_drv_init() failed (%d)\n", Ret));
    gBS->FreePages (FbBase, FbPages);
    return EFI_DEVICE_ERROR;
  }

  FbInfo = AllocateZeroPool (sizeof (*FbInfo));
  if (FbInfo == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  FbInfo->FramebufferBase       = FbBase;
  FbInfo->FramebufferSize       = DECON_FB_SIZE;
  FbInfo->HorizontalResolution  = DECON_FB_WIDTH;
  FbInfo->VerticalResolution    = DECON_FB_HEIGHT;
  FbInfo->PixelsPerScanLine     = DECON_FB_WIDTH;
  FbInfo->PixelFormat           = DeconPixelFormatBgra8888;

  Handle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gDeconFramebufferInfoProtocolGuid,
                  FbInfo,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "DeconDxe: failed to install framebuffer info protocol (%r)\n", Status));
    FreePool (FbInfo);
    return Status;
  }

  DEBUG ((DEBUG_INFO, "DeconDxe: panel on, framebuffer protocol installed\n"));

  *(UINT32 *)(0x19050070) = 0x1281;

  return EFI_SUCCESS;
}
