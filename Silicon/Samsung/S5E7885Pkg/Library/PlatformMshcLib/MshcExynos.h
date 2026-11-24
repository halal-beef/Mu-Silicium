/**
  Copyright (c) 2012, Samsung Electronics Co. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef _MSHC_EXYNOS_H_
#define _MSHC_EXYNOS_H_

#define MSHC_CLKSEL_DIV          (3)

//
// eMMC (dwmmc0) Timings
//
#define MSHC_EMMC_TIMING_SDR     CLKSEL_TIMING (MSHC_CLKSEL_DIV, 0, 4, 0)
#define MSHC_EMMC_TIMING_DDR     CLKSEL_TIMING (MSHC_CLKSEL_DIV, 0, 4, 2)

//
// microSD (dwmmc2) Timings
//
#define MSHC_SD_TIMING_SDR       CLKSEL_TIMING (MSHC_CLKSEL_DIV, 0, 2, 0)
#define MSHC_SD_TIMING_DDR       CLKSEL_TIMING (MSHC_CLKSEL_DIV, 0, 2, 1)
#define MSHC_SD_TIMING_SDR50     CLKSEL_TIMING (MSHC_CLKSEL_DIV, 0, 4, 2)
#define MSHC_SD_TIMING_SDR104    CLKSEL_TIMING (MSHC_CLKSEL_DIV, 0, 3, 0)

//
// Exynos extensions
//
#define MSHCI_FORCE_CLK_STOP     (0xB0)
#define MSHCI_BLOCK_DMA_FOR_CI   (0x1F8)

#define MMC_HWACG_CONTROL        BIT4
#define BLOCK_DMA_NO_EXT_TIMEOUT BIT29

//
// eMMC-only access control (SMU / "EMMCP" block at controller base + 0x1000).
// Entry 0 decides which LBA range / LUN the host is allowed to move data for;
// without a valid entry the data path stalls after the first burst.
//
#define MSHC_SMU_BASE            (0x1000)
#define MSHC_SMU_MPSBEGIN0       (MSHC_SMU_BASE + 0x200)
#define MSHC_SMU_MPSEND0         (MSHC_SMU_BASE + 0x204)
#define MSHC_SMU_MPSLUN0         (MSHC_SMU_BASE + 0x208)
#define MSHC_SMU_MPSCTRL0        (MSHC_SMU_BASE + 0x20C)

#define MSHC_SMU_CTRL_VALID      BIT0
#define MSHC_SMU_CTRL_NS_WRITE   BIT4
#define MSHC_SMU_CTRL_NS_READ    BIT5
#define MSHC_SMU_CTRL_S_WRITE    BIT6
#define MSHC_SMU_CTRL_S_READ     BIT7
#define MSHC_SMU_CTRL_BYPASS     (MSHC_SMU_CTRL_S_READ | MSHC_SMU_CTRL_S_WRITE | \
                                  MSHC_SMU_CTRL_NS_READ | MSHC_SMU_CTRL_NS_WRITE | \
                                  MSHC_SMU_CTRL_VALID)

#endif /* _MSHC_EXYNOS_H_ */
