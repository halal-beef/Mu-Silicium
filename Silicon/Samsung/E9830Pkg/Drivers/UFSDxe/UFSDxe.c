/**
  Copyright (C) Samsung Electronics Co. LTD

  This software is proprietary of Samsung Electronics.
  No part of this software, either material or conceptual may be copied or distributed, transmitted,
  transcribed, stored in a retrieval system or translated into any human or computer language in any form by any means,
  electronic, mechanical, manual or otherwise, or disclosed
  to third parties without the express written permission of Samsung Electronics.

  Alternatively, this program is free software in case of open source project
  you can redistribute it and/or modify
  it under the terms of the GNU General Public License version 2 as
  published by the Free Software Foundation.
**/

#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/TimerLib.h>
#include <Library/IoLib.h>

#define WARM_RESET					(1 << 28)
#define LITTLE_WDT_RESET				(1 << 24)

#define EXYNOS9830_EDPCSR_DUMP_EN			(1 << 0)

#define UFS_SCLK			166000000
#define CNT_VAL_1US_MASK		0x3ff
#define UFSHCI_VS_1US_TO_CNT_VAL	0x110C
#define UFSHCI_VS_UFSHCI_V2P1_CTRL	0X118C
#define BIT(x) (1 << (x))
#define IA_TICK_SEL			BIT(16)

#define MUX_CLKCMU_UFS_EMBD_CON		0x1A331098
#define DIV_CLKCMU_UFS_EMBD_MUX		0x1A331890

#define UFS_CLKCMU_TIMEOUT		100

enum {
	__BRD_SMDK,
	__BRD_ASB,
	__BRD_HSIE,
	__BRD_ZEBU,
	__BRD_UNIV,
	__BRD_MAX,
};

#define BRD_SMDK	(1U << __BRD_SMDK)
#define BRD_ASB		(1U << __BRD_ASB)
#define BRD_HSIE	(1U << __BRD_HSIE)
#define BRD_ZEBU	(1U << __BRD_ZEBU)
#define BRD_UNIV	(1U << __BRD_UNIV)
#define BRD_MAX		(1U << __BRD_MAX)
#define BRD_ALL		((1U << __BRD_MAX) - 1)

enum {
	REG_CONTROLLER_CAPABILITIES = 0x00,
	REG_UFS_VERSION = 0x08,
	REG_CONTROLLER_PID = 0x10,
	REG_CONTROLLER_MID = 0x14,
	REG_INTERRUPT_STATUS = 0x20,
	REG_INTERRUPT_ENABLE = 0x24,
	REG_CONTROLLER_STATUS = 0x30,
#define UPMCRS(x)		(((x) >> 8) & 7)
#define PWR_LOCAL		1
	REG_CONTROLLER_ENABLE = 0x34,
	REG_UIC_ERROR_CODE_PHY_ADAPTER_LAYER = 0x38,
	REG_UIC_ERROR_CODE_DATA_LINK_LAYER = 0x3C,
	REG_UIC_ERROR_CODE_NETWORK_LAYER = 0x40,
	REG_UIC_ERROR_CODE_TRANSPORT_LAYER = 0x44,
	REG_UIC_ERROR_CODE_DME = 0x48,
	REG_UTP_TRANSFER_REQ_INT_AGG_CONTROL = 0x4C,
	REG_UTP_TRANSFER_REQ_LIST_BASE_L = 0x50,
	REG_UTP_TRANSFER_REQ_LIST_BASE_H = 0x54,
	REG_UTP_TRANSFER_REQ_DOOR_BELL = 0x58,
	REG_UTP_TRANSFER_REQ_LIST_CLEAR = 0x5C,
	REG_UTP_TRANSFER_REQ_LIST_RUN_STOP = 0x60,
	REG_UTP_TASK_REQ_LIST_BASE_L = 0x70,
	REG_UTP_TASK_REQ_LIST_BASE_H = 0x74,
	REG_UTP_TASK_REQ_DOOR_BELL = 0x78,
	REG_UTP_TASK_REQ_LIST_CLEAR = 0x7C,
	REG_UTP_TASK_REQ_LIST_RUN_STOP = 0x80,
	REG_UIC_COMMAND = 0x90,
	REG_UIC_COMMAND_ARG_1 = 0x94,
	REG_UIC_COMMAND_ARG_2 = 0x98,
	REG_UIC_COMMAND_ARG_3 = 0x9C,

	VS_TXPRDT_ENTRY_SIZE		= 0x000,
	VS_RXPRDT_ENTRY_SIZE		= 0x004,
	VS_IS				= 0x038,
	VS_UTRL_NEXUS_TYPE		= 0x040,
	VS_UMTRL_NEXUS_TYPE		= 0x044,
	VS_SW_RST			= 0x050,
	VS_DATA_REORDER			= 0x060,
	VS_AXIDMA_RWDATA_BURST_LEN	= 0x06C,
	VS_GPIO_OUT			= 0x070,
	VS_CLKSTOP_CTRL			= 0x0B0,
	VS_FORCE_HCS			= 0x0B4,
	VS_UFS_ACG_DISABLE		= 0x0FC,
	VS_MPHY_REFCLK_SEL		= 0x108,

	UFSP_UPRSECURITY	= 0x10,
	UFSP_UPSBEGIN0		= 0x2000,
	UFSP_UPSEND0		= 0x2004,
	UFSP_UPLUN0		= 0x2008,
	UFSP_UPSCTRL0		= 0x200c,
};

struct uic_pwr_mode {
	CHAR8 lane;
	CHAR8 gear;
	CHAR8 mode;
	CHAR8 hs_series;
};

struct ufs_cal_param {
	void *host;             /* Host adaptor */
	CHAR8 available_lane;
	CHAR8 connected_tx_lane;
	CHAR8 connected_rx_lane;
	CHAR8 active_tx_lane;
	CHAR8 active_rx_lane;
	UINT32 mclk_rate;
	CHAR8 tbl;
	CHAR8 board;
	CHAR8 evt_ver;
	CHAR8 max_gear;
	struct uic_pwr_mode *pmd;
};

struct ufs_host
{
	CHAR8 host_name[16];
	UINT32 irq;
	UINTN ioaddr;
	UINTN vs_addr;
	UINTN fmp_addr;
	UINTN unipro_addr;
	UINTN ufspaddr;
	UINTN phy_pma;
	UINTN dev_pwr_addr;
	UINTN phy_iso_addr;

	INTN host_index;

	//scm *scsi_cmd;
	CHAR8 ufs_descriptor;
	CHAR8 arglist;
	UINT32 lun;
	INTN scsi_status;
	CHAR8 sense_buffer;
	UINT32 sense_buflen;

	//struct ufs_cmd_desc *cmd_desc_addr;
	//struct ufs_utrd *utrd_addr;
	//struct ufs_utmrd *utmrd_addr;
	//struct ufs_uic_cmd *uic_cmd;

	UINT32 capabilities;
	INTN nutrs;
	INTN nutmrs;
	UINT32 ufs_version;

	UINT32 int_enable_mask;

	UINT32 quirks;		/* Quirk flags */

	/* HBA Errors */
	UINT32 errors;

	/* timeout */
	UINT32 ufs_cmd_timeout;
	UINT32 uic_cmd_timeout;
	UINT32 ufs_query_req_timeout;
	UINT32 timeout;

	//union ufs_flags flags;
	//union ufs_attributes attributes;

	//struct ufs_config_desc config_desc;
	//struct ufs_device_desc device_desc;
	//struct ufs_geometry_desc geometry_desc;
	//struct ufs_unit_desc unit_desc[8];
	UINT16 data_seg_len;
	// TODO:
	//CHAR8 upiu_data[UPIU_DATA_SIZE * 4];
	struct ufs_cal_param	cal_param;
	UINT32 mclk_rate;
	struct uic_pwr_mode pmd_cxt;
	UINT32 dev_pwr_shift;
	UINT32 support_tw;
	UINT32 gear_mode;
	UINT16 wManufactureID;
	INTN swp;
};

VOID UFSVsSet1usToCnt(struct ufs_host *ufs)
{
  UINT32 nVal;
  UINT32 cnt_val;

  /* IA_TICK_SEL : 1(1us_TO_CNT_VAL) */
  nVal = MmioRead32(ufs->ioaddr + UFSHCI_VS_UFSHCI_V2P1_CTRL);
  nVal |= IA_TICK_SEL;
  MmioWrite32(ufs->ioaddr + UFSHCI_VS_UFSHCI_V2P1_CTRL, nVal);

  cnt_val = UFS_SCLK / 1000000;
  MmioWrite32(ufs->ioaddr + UFSHCI_VS_1US_TO_CNT_VAL, cnt_val & CNT_VAL_1US_MASK);
}

void UfsSetUniproClk(struct ufs_host *ufs)
{
  INTN timeout = 0;

  MmioWrite32(DIV_CLKCMU_UFS_EMBD_MUX, 3);

  do {
    timeout += 1;
  } while ((MmioRead32(DIV_CLKCMU_UFS_EMBD_MUX) & 0x10000) && timeout < UFS_CLKCMU_TIMEOUT);

  if (timeout == UFS_CLKCMU_TIMEOUT)
    DEBUG((EFI_D_ERROR,"ERROR(UFS): DIV_CLKCMU_UFS_EMBD_MUX setting timeout occured\n"));

  timeout = 0;
  MmioWrite32(MUX_CLKCMU_UFS_EMBD_CON, 1);

  do {
    timeout += 1;
  } while ((MmioRead32(MUX_CLKCMU_UFS_EMBD_CON) & 0x10000) && timeout < UFS_CLKCMU_TIMEOUT);

  if (timeout == UFS_CLKCMU_TIMEOUT)
    DEBUG((EFI_D_ERROR, "ERROR(UFS): MUX_CLKCMU_UFS_EMBD_CON setting timeout occured\n"));

  UFSVsSet1usToCnt(ufs);
}

INTN
UFSBoardInit(INTN HostIndex, struct ufs_host *ufs) {
  UINT32 reg;
  UINT32 RST_STAT = MmioRead32(0x15860000 + 0x0404);
  UINT32 DFD_EN = MmioRead32(0x15860000 + 0x0500);

  if(HostIndex) {
    DEBUG((EFI_D_ERROR, "MULTI UFS HOST IS NOT SUPPORTED!\n"));
    ASSERT(FALSE);
  }

  ufs->host_name[0] = 'u';
  ufs->host_name[1] = 'f';
  ufs->host_name[2] = 's';
  ufs->host_name[3] = '0';
  ufs->host_name[4] = '\0';

  ufs->irq = 249;
  ufs->ioaddr = 0x13100000;
  ufs->vs_addr = 0x13100000 + 0x1100;
  ufs->fmp_addr = 0x132A0000;
  ufs->unipro_addr = 0x13180000;
  ufs->phy_pma = 0x13100000 + 0x4000;

  ufs->dev_pwr_addr = 0x10730000 + 0xc4;
  ufs->dev_pwr_shift = 0;

  ufs->phy_iso_addr = 0x15860000 + 0x724;

  ufs->host_index = HostIndex;

  ufs->mclk_rate = 166 * (1000 * 1000);
  UfsSetUniproClk(ufs);

  ufs->gear_mode = 4;

  /* GPIO configurations, rst_n and refclk */
  reg = *(volatile UINT32 *)0x13040048;
  reg &= ~(0xFF);
  *(volatile UINT32 *)0x13040048 = reg;

  reg = *(volatile UINT32 *)0x13040040;
  reg &= ~(0xFF);
  reg |= 0x22;
  *(volatile UINT32 *)0x13040040 = reg;

  reg = *(volatile UINT32 *)0x107300c0;
  reg &= ~(0x7);
  reg |= 0x1;
  *(volatile UINT32 *)0x107300c0 = reg;

  if (!((RST_STAT & (WARM_RESET | LITTLE_WDT_RESET)) &&
     DFD_EN & EXYNOS9830_EDPCSR_DUMP_EN)) {
       /* Enable UFS IO cache coherency in SYSREG */
       reg = *(volatile UINT32 *)0x13020700;
       reg |= ((1 << 22) | (1<<23));
       *(volatile UINT32 *)0x13020700 = reg;
   }

	return 0;
}

VOID
UFSDisableUFSP (struct ufs_host *ufs) {
  DEBUG((EFI_D_ERROR, "0x%x\n", ufs->fmp_addr));
  DEBUG((EFI_D_ERROR, "0x%x\n", ufs->fmp_addr + UFSP_UPSBEGIN0));
  DEBUG((EFI_D_ERROR, "0x%x\n", ufs->fmp_addr + UFSP_UPSEND0));
  DEBUG((EFI_D_ERROR, "0x%x\n", ufs->fmp_addr + UFSP_UPLUN0));
  DEBUG((EFI_D_ERROR, "0x%x\n", ufs->fmp_addr + UFSP_UPSCTRL0));

DEBUG((EFI_D_ERROR, "1\n"));
  MmioWrite32(ufs->fmp_addr + UFSP_UPSBEGIN0, 0x0);
DEBUG((EFI_D_ERROR, "2\n"));
  MmioWrite32(ufs->fmp_addr + UFSP_UPSEND0, 0xFFFFFFFF);
DEBUG((EFI_D_ERROR, "3\n"));
  MmioWrite32(ufs->fmp_addr + UFSP_UPLUN0, 0xFF);
DEBUG((EFI_D_ERROR, "4\n"));
  MmioWrite32(ufs->fmp_addr + UFSP_UPSCTRL0, 0xF1);
}

INTN
UFSCalInit(struct ufs_cal_param _, INTN __)
{
return 0;
}

INTN
UFSInitCal (
  struct ufs_host *ufs, INTN index
  )
{
  INTN ret;

  ufs->cal_param.host = ufs;

  ufs->cal_param.board = BRD_UNIV;

  ufs->cal_param.evt_ver = (MmioRead32(0x10000010) >> 20) & 0xF;

  DEBUG((EFI_D_WARN, "UFS EVT Version %d\n", ufs->cal_param.evt_ver));

  ret = UFSCalInit(ufs->cal_param, index);

  if(ret != 0) {
    DEBUG((EFI_D_ERROR, "UFS CAL INIT FAILED WITH %d\n", ret));
    return 1;
  }

  return 0;
}

EFI_STATUS
EFIAPI
InitUFSDriver (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  struct ufs_host ufs;

  ufs.ufs_cmd_timeout = 10000000;
  ufs.uic_cmd_timeout = 1500000;
  ufs.ufs_query_req_timeout = 1500000;

  if(UFSBoardInit(0, &ufs))
  {
    DEBUG((EFI_D_WARN, "BOARD INIT FAILED!\n"));
    ASSERT(FALSE);
  }

  ufs.capabilities = MmioRead32(ufs.ioaddr);
  ufs.ufs_version = MmioRead32(ufs.ioaddr + 0x08);

  DEBUG((EFI_D_WARN, "%a\n\tcaps(0x%p) 0x%08x\n\tver(0x%p)  0x%08x\n\tPID(0x%p)  0x%08x\n\tMID(0x%p)  0x%08x\n",
		ufs.host_name, ufs.ioaddr, ufs.capabilities,
		ufs.ioaddr + 0x08, ufs.ufs_version, ufs.ioaddr + 0x10,
		MmioRead32(ufs.ioaddr + 0x10),
		ufs.ioaddr + 0x14, MmioRead32(ufs.ioaddr + 0x14)));

//  DEBUG((EFI_D_ERROR, "INIT UFSP\n"));
  //UFSDisableUFSP(&ufs);

  DEBUG((EFI_D_ERROR, "INIT CAL\n"));
  if(UFSInitCal(&ufs, 0))
  {
    DEBUG((EFI_D_ERROR, "ERROR INITIALISING CALIBRATION!\n"));
    ASSERT(FALSE);
  }

  while(1);
  return EFI_SUCCESS;
}
