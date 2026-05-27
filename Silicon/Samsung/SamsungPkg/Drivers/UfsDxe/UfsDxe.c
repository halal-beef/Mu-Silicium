#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryMapHelperLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>

#include "UfsDxe.h"

// vscode
#include <Uefi/UefiSpec.h>


//stupid hack
STATIC
INT32
UfsSendUicCmd (struct UfsHost *Ufs);

UINT8 gQueryParams[][5] = {
  /* [0] unused */
  {0,                    0,                              0,                           0, 0},
  /* FLAG_W_FDEVICEINIT */
  {UFS_STD_WRITE_REQ,    UPIU_QUERY_OPCODE_SET_FLAG,     UPIU_FLAG_ID_DEVICEINIT,     0, 0},
  /* FLAG_R_FDEVICEINIT */
  {UFS_STD_READ_REQ,     UPIU_QUERY_OPCODE_READ_FLAG,    UPIU_FLAG_ID_DEVICEINIT,     0, 0},
  /* DESC_R_DEVICE_DESC */
  {UFS_STD_READ_REQ,     UPIU_QUERY_OPCODE_READ_DESC,    UPIU_DESC_ID_DEVICE,         0, 0},
  /* DESC_W_CONFIG_DESC */
  {UFS_STD_WRITE_REQ,    UPIU_QUERY_OPCODE_WRITE_DESC,   UPIU_DESC_ID_CONFIGURATION,  0, 0},
  /* DESC_R_CONFIG_DESC */
  {UFS_STD_READ_REQ,     UPIU_QUERY_OPCODE_READ_DESC,    UPIU_DESC_ID_CONFIGURATION,  0, 0},
  /* DESC_R_UNIT_DESC */
  {UFS_STD_READ_REQ,     UPIU_QUERY_OPCODE_READ_DESC,    UPIU_DESC_ID_UNIT,           0, 0},
  /* DESC_R_GEOMETRY_DESC */
  {UFS_STD_READ_REQ,     UPIU_QUERY_OPCODE_READ_DESC,    UPIU_DESC_ID_GEOMETRY,       0, 0},
  /* ATTR_W_BOOTLUNEN */
  {UFS_STD_WRITE_REQ,    UPIU_QUERY_OPCODE_WRITE_ATTR,   UPIU_ATTR_ID_BOOTLUNEN,      0, 0},
  /* ATTR_R_BOOTLUNEN */
  {UFS_STD_READ_REQ,     UPIU_QUERY_OPCODE_READ_ATTR,    UPIU_ATTR_ID_BOOTLUNEN,      0, 0},
  /* ATTR_W_REFCLKFREQ */
  {UFS_STD_WRITE_REQ,    UPIU_QUERY_OPCODE_WRITE_ATTR,   UPIU_ATTR_ID_REFCLKFREQ,     0, 0},
  /* ATTR_R_REFCLKFREQ */
  {UFS_STD_READ_REQ,     UPIU_QUERY_OPCODE_READ_ATTR,    UPIU_ATTR_ID_REFCLKFREQ,     0, 0},
};

void ufs_lld_dme_set (void *h, UINT32 addr, UINT32 val)
{
  struct UfsHost     *Ufs = (struct UfsHost *)h;
  struct UfsUicCmd   cmd = {UIC_CMD_DME_SET, 0, 0, 0};
  cmd.Arg1 = addr;
  cmd.Arg2 = val;
  Ufs->UicCmd = &cmd;
  UfsSendUicCmd (Ufs);
}

void ufs_lld_dme_get (void *h, UINT32 addr, UINT32 *val)
{
  struct UfsHost     *Ufs = (struct UfsHost *)h;
  struct UfsUicCmd   cmd = {UIC_CMD_DME_GET, 0, 0, 0};
  cmd.Arg1 = addr;
  Ufs->UicCmd = &cmd;
  UfsSendUicCmd (Ufs);
  *val = cmd.Arg3;
}

void ufs_lld_dme_peer_set (void *h, UINT32 addr, UINT32 val)
{
  struct UfsHost     *Ufs = (struct UfsHost *)h;
  struct UfsUicCmd   cmd = {UIC_CMD_DME_PEER_SET, 0, 0, 0};
  cmd.Arg1 = addr;
  cmd.Arg3 = val;
  Ufs->UicCmd = &cmd;
  UfsSendUicCmd (Ufs);
}

void ufs_lld_pma_write (void *h, UINT32 val, UINT32 addr)
{
  struct UfsHost *Ufs = (struct UfsHost *)h;
  MmioWrite32((UINTN)(Ufs->PhyPma + addr), val);
}

UINT32 ufs_lld_pma_read (void *h, UINT32 addr)
{
  struct UfsHost *Ufs = (struct UfsHost *)h;
  return MmioRead32((UINTN)(Ufs->PhyPma + addr));
}

void ufs_lld_unipro_write (void *h, UINT32 val, UINT32 addr)
{
  struct UfsHost *Ufs = (struct UfsHost *)h;
  MmioWrite32((UINTN)(Ufs->UniProAddr + addr), val);
}

// Board Specific Start
#define WARM_RESET                  (1U << 28)
#define LITTLE_WDT_RESET            (1U << 24)
#define EXYNOS9830_EDPCSR_DUMP_EN   (1U << 0)

#define UFS_SCLK                    166000000UL
#define CNT_VAL_1US_MASK            0x3FFU
#define UFSHCI_VS_1US_TO_CNT_VAL    0x110CU
#define UFSHCI_VS_UFSHCI_V2P1_CTRL  0x118CU
#define IA_TICK_SEL                 (1U << 16)

#define MUX_CLKCMU_UFS_EMBD_CON    0x1A331098UL
#define DIV_CLKCMU_UFS_EMBD_MUX    0x1A331890UL
#define UFS_CLKCMU_TIMEOUT          100

/* UFSHCI */
#define UIC_ARG_MIB_SEL(attr, sel)	((((attr) & 0xFFFF) << 16) |\
					 ((sel) & 0xFFFF))
#define UIC_ARG_MIB(attr)		UIC_ARG_MIB_SEL(attr, 0)

/* Unipro.h */
#define IS_PWR_MODE_HS(m)        (((m) == FAST_MODE) || ((m) == FASTAUTO_MODE))
#define IS_PWR_MODE_PWM(m)       (((m) == SLOW_MODE) || ((m) == SLOWAUTO_MODE))

static void UfsVsSet1usToCnt (struct UfsHost *Ufs)
{
  UINT32 nVal = MmioRead32((UINTN)(Ufs->IoAddr + UFSHCI_VS_UFSHCI_V2P1_CTRL));
  nVal |= IA_TICK_SEL;
  MmioWrite32((UINTN)(Ufs->IoAddr + UFSHCI_VS_UFSHCI_V2P1_CTRL), nVal);
  MmioWrite32((UINTN)(Ufs->IoAddr + UFSHCI_VS_1US_TO_CNT_VAL), (UFS_SCLK / 1000000) & CNT_VAL_1US_MASK);
}

static void UfsSetUniProClk (struct UfsHost *Ufs)
{
  int timeout = 0;
  MmioWrite32(DIV_CLKCMU_UFS_EMBD_MUX, 3);
  do { timeout++; } while ((MmioRead32(DIV_CLKCMU_UFS_EMBD_MUX) & 0x10000) && timeout < UFS_CLKCMU_TIMEOUT);
  timeout = 0;
  MmioWrite32(MUX_CLKCMU_UFS_EMBD_CON, 1);
  do { timeout++; } while ((MmioRead32(MUX_CLKCMU_UFS_EMBD_CON) & 0x10000) && timeout < UFS_CLKCMU_TIMEOUT);
  UfsVsSet1usToCnt (Ufs);
}

static
EFI_STATUS
UfsBoardInit (struct UfsHost *Ufs)
{
  UINT32 reg;
  UINT32 rst_stat = MmioRead32(0x15860000 + 0x404);
  UINT32 dfd_en   = MmioRead32(0x15860000 + 0x500);

  DEBUG ((DEBUG_INFO, "UFS: Board init\n"));

  /* MMIO regions */
  Ufs->IoAddr = (VOID *)(UINTN)0x13100000;
  Ufs->VsAddr = (VOID *)(UINTN)(0x13100000 + 0x1100);
  Ufs->UniProAddr = (VOID *)(UINTN)0x13180000;
  Ufs->PhyPma = (VOID *)(UINTN)(0x13100000 + 0x4000);

  /* Power / PHY isolation addresses */
  Ufs->DevPwrAddr  = (VOID *)(UINTN)(0x10730000UL + 0xC4);
  Ufs->DevPwrShift = 0;
  Ufs->PhyIsoAddr  = (VOID *)(UINTN)(0x15860000UL + 0x724);

  Ufs->MclkRate = 166 * 1000 * 1000;
  Ufs->GearMode = 4;

  UfsSetUniProClk (Ufs);

  // TODO : Hook this in with the actual GPIO driver, instead of direct memory writes.

  /* GPIO: RST_N and REFCLK */
  reg  = *(volatile UINT32 *)0x13040048UL;
  reg &= ~0xFFU;
  *(volatile UINT32 *)0x13040048UL = reg;

  reg  = *(volatile UINT32 *)0x13040040UL;
  reg &= ~0xFFU;
  reg |= 0x22U;
  *(volatile UINT32 *)0x13040040UL = reg;

  /* XBOOTLDO GPG1[0] */
  reg  = *(volatile UINT32 *)0x107300C0UL;
  reg &= ~0x7U;
  reg |= 0x1U;
  *(volatile UINT32 *)0x107300C0UL = reg;

  /* IO coherency in SYSREG (skip if warm/wdt reset with DFD) */
  if (!((rst_stat & (WARM_RESET | LITTLE_WDT_RESET)) &&
        (dfd_en & EXYNOS9830_EDPCSR_DUMP_EN))) {
    reg  = *(volatile UINT32 *)0x13020700UL;
    reg |= ((1U << 22) | (1U << 23));
    *(volatile UINT32 *)0x13020700UL = reg;
  }

  return EFI_SUCCESS;
}

#define _UFS_CAL_

#define IS_PWR_MODE_HS(m)   (((m) == FAST_MODE) || ((m) == FASTAUTO_MODE))
#define IS_PWR_MODE_PWM(m)  (((m) == SLOW_MODE) || ((m) == SLOWAUTO_MODE))

enum { PA_HS_MODE_A = 1, PA_HS_MODE_B = 2 };
enum { FAST_MODE = 1, SLOW_MODE = 2, FASTAUTO_MODE = 4, SLOWAUTO_MODE = 5, UNCHANGED = 7 };

/* Integer-only timing macros (no float, safe for EDK2 MSVC/GCC -msoft-float) */
#define UNIPRO_MCLK_PERIOD(p)            (1000000000UL / (p)->MclkRate)
/* Round-off: (1e9 / rate + 0.5) => (1e9*2 / rate + 1) / 2  */
#define UNIPRO_MCLK_PERIOD_ROUND_OFF(p)  (UINT32)(((2000000000ULL / (p)->MclkRate) + 1) / 2)
#define UNIPRO18_MCLK_PERIOD(p)          (16ULL * 1000ULL * 1000000ULL / (p)->MclkRate)

#define TX_LINE_RESET_TIME              3200UL
#define RX_LINE_RESET_DETECT_TIME       1000UL
#define PCS_TX_LINE_RESET_PERIOD(p) \
  (UINT32)((TX_LINE_RESET_TIME * ((UINT64)(p)->MclkRate / 1000000ULL)))
#define PCS_RX_LINE_RESET_DETECT_PERIOD(p) \
  (UINT32)((RX_LINE_RESET_DETECT_TIME * ((UINT64)(p)->MclkRate / 1000000ULL)))

#define PHY_PMA_COMN_ADDR(reg)          (reg)
#define PHY_PMA_TRSV_ADDR(reg,lane)     ((reg) + (0x800 * (lane)))

#define NUM_OF_UFS_HOST  2

enum {
  PHY_CFG_NONE = 0,
  PHY_PCS_COMN, PHY_PCS_RXTX, PHY_PMA_COMN, PHY_PMA_TRSV,
  PHY_PLL_WAIT, PHY_CDR_WAIT, PHY_CDR_AFC_WAIT,
  UNIPRO_STD_MIB, UNIPRO_DBG_MIB, UNIPRO_DBG_APB,
  PHY_PCS_RX, PHY_PCS_TX, PHY_PCS_RX_PRD, PHY_PCS_TX_PRD,
  UNIPRO_DBG_PRD,
  PHY_PMA_TRSV_LANE1_SQ_OFF,
  PHY_PMA_TRSV_SQ,
  COMMON_WAIT,
  PHY_PCS_RX_LR_PRD, PHY_PCS_TX_LR_PRD,
  PHY_PCS_RX_PRD_ROUND_OFF, PHY_PCS_TX_PRD_ROUND_OFF,
  UNIPRO_ADAPT_LENGTH,
  PHY_EMB_CDR_WAIT, PHY_EMB_CAL_WAIT,
};

enum {
  __PMD_PWM_G1_L1, __PMD_PWM_G1_L2, __PMD_PWM_G2_L1, __PMD_PWM_G2_L2,
  __PMD_PWM_G3_L1, __PMD_PWM_G3_L2, __PMD_PWM_G4_L1, __PMD_PWM_G4_L2,
  __PMD_PWM_G5_L1, __PMD_PWM_G5_L2, __PMD_PWM_MAX,
  __PMD_HS_G1_L1, __PMD_HS_G1_L2, __PMD_HS_G2_L1, __PMD_HS_G2_L2,
  __PMD_HS_G3_L1, __PMD_HS_G3_L2, __PMD_HS_G4_L1, __PMD_HS_G4_L2,
  __PMD_HS_MAX,
};

#define PMD_PWM_G1_L1  (1U << __PMD_PWM_G1_L1)
#define PMD_PWM_G1_L2  (1U << __PMD_PWM_G1_L2)
#define PMD_PWM_G2_L1  (1U << __PMD_PWM_G2_L1)
#define PMD_PWM_G2_L2  (1U << __PMD_PWM_G2_L2)
#define PMD_PWM_G3_L1  (1U << __PMD_PWM_G3_L1)
#define PMD_PWM_G3_L2  (1U << __PMD_PWM_G3_L2)
#define PMD_PWM_G4_L1  (1U << __PMD_PWM_G4_L1)
#define PMD_PWM_G4_L2  (1U << __PMD_PWM_G4_L2)
#define PMD_PWM_G5_L1  (1U << __PMD_PWM_G5_L1)
#define PMD_PWM_G5_L2  (1U << __PMD_PWM_G5_L2)
#define PMD_PWM_MAX    (1U << __PMD_PWM_MAX)
#define PMD_HS_G1_L1   (1U << __PMD_HS_G1_L1)
#define PMD_HS_G1_L2   (1U << __PMD_HS_G1_L2)
#define PMD_HS_G2_L1   (1U << __PMD_HS_G2_L1)
#define PMD_HS_G2_L2   (1U << __PMD_HS_G2_L2)
#define PMD_HS_G3_L1   (1U << __PMD_HS_G3_L1)
#define PMD_HS_G3_L2   (1U << __PMD_HS_G3_L2)
#define PMD_HS_G4_L1   (1U << __PMD_HS_G4_L1)
#define PMD_HS_G4_L2   (1U << __PMD_HS_G4_L2)
#define PMD_HS_MAX     (1U << __PMD_HS_MAX)
#define PMD_ALL        (PMD_HS_MAX - 1)
#define PMD_PWM        (PMD_PWM_MAX - 1)
#define PMD_HS         (PMD_ALL ^ PMD_PWM)

struct UfsCalPhyCfg {
  UINT32 Address;
  UINT32 Value;
  UINT32 Flag;
  UINT32 Layer;
  UINT8  Board;
};

#define for_each_phy_cfg(cfg)  for (; (cfg)->Flag != PHY_CFG_NONE; (cfg)++)

static struct UfsCalParam  *ufs_cal[NUM_OF_UFS_HOST];
static unsigned long          ufs_cal_lock_timeout = 0xFFFFFFFF;

static const struct UfsCalPhyCfg init_cfg_evt0[] = {
  {0x44,   0x00,        PMD_ALL, UNIPRO_DBG_PRD,        BRD_ALL},
  {0x200,  0x40,        PMD_ALL, PHY_PCS_COMN,           BRD_ALL},
  {0x12,   0x00,        PMD_ALL, PHY_PCS_RX_PRD_ROUND_OFF, BRD_ALL},
  {0xAA,   0x00,        PMD_ALL, PHY_PCS_TX_PRD_ROUND_OFF, BRD_ALL},
  {0xA9,   0x02,        PMD_ALL, PHY_PCS_TX,             BRD_ALL},
  {0xAB,   0x00,        PMD_ALL, PHY_PCS_TX_LR_PRD,      BRD_ALL},
  {0x11,   0x00,        PMD_ALL, PHY_PCS_RX,             BRD_ALL},
  {0x1B,   0x00,        PMD_ALL, PHY_PCS_RX_LR_PRD,      BRD_ALL},
  {0x2F,   0x79,        PMD_ALL, PHY_PCS_RX,             BRD_ALL},
  {0x76,   0x03,        PMD_ALL, PHY_PCS_RX,             BRD_ZEBU},
  {0x84,   0x01,        PMD_ALL, PHY_PCS_RX,             BRD_ALL},
  {0x04,   0x01,        PMD_ALL, PHY_PCS_TX,             BRD_ALL},
  {0x25,   0xF6,        PMD_ALL, PHY_PCS_RX,             BRD_ALL},
  {0x7F,   0x00,        PMD_ALL, PHY_PCS_TX,             BRD_ALL},
  {0x200,  0x00,        PMD_ALL, PHY_PCS_COMN,           BRD_ALL},
  {0x155E, 0x00,        PMD_ALL, UNIPRO_STD_MIB,         BRD_ALL},
  {0x3000, 0x00,        PMD_ALL, UNIPRO_STD_MIB,         BRD_ALL},
  {0x3001, 0x01,        PMD_ALL, UNIPRO_STD_MIB,         BRD_ALL},
  {0x4021, 0x01,        PMD_ALL, UNIPRO_STD_MIB,         BRD_ALL},
  {0x4020, 0x01,        PMD_ALL, UNIPRO_STD_MIB,         BRD_ALL},
  {0xA006, 0x80000000,  PMD_ALL, UNIPRO_DBG_MIB,         BRD_ALL},
  {0x00,   0x3E8,       PMD_ALL, COMMON_WAIT,             BRD_ALL},
  {0x10C,  0x10,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0x118,  0x48,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0x81C,  0x0C,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xB84,  0x40,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0x974,  0x00,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0x978,  0x3F,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0x97C,  0xFF,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0x990,  0x4E,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0x9B8,  0x5E,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0x9BC,  0x70,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xBB4,  0x25,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAB0,  0x13,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xACC,  0x05,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAD8,  0x10,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xADC,  0x10,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAE0,  0x10,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAE4,  0x10,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAE8,  0x10,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAEC,  0x08,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAF0,  0x08,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAF4,  0x08,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAF8,  0x08,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xBCC,  0x80,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0x28,   0x33,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0x34,   0xB9,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0x38,   0x0F,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0x44,   0x01,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0xB0,   0x30,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0x104,  0x20,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0xB90,  0x18,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0x10C,  0x18,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0x10C,  0x00,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0xCE0,  0x08,        PMD_ALL, PHY_EMB_CAL_WAIT,       BRD_ALL},
  {0xA006, 0x00,        PMD_ALL, UNIPRO_DBG_MIB,         BRD_ALL},
  {0, 0, 0, 0, 0},
};

static const struct UfsCalPhyCfg init_cfg_evt1[] = {
  {0x44,   0x00,        PMD_ALL, UNIPRO_DBG_PRD,        BRD_ALL},
  {0x200,  0x40,        PMD_ALL, PHY_PCS_COMN,           BRD_ALL},
  {0x12,   0x00,        PMD_ALL, PHY_PCS_RX_PRD_ROUND_OFF, BRD_ALL},
  {0xAA,   0x00,        PMD_ALL, PHY_PCS_TX_PRD_ROUND_OFF, BRD_ALL},
  {0xA9,   0x02,        PMD_ALL, PHY_PCS_TX,             BRD_ALL},
  {0xAB,   0x00,        PMD_ALL, PHY_PCS_TX_LR_PRD,      BRD_ALL},
  {0x11,   0x00,        PMD_ALL, PHY_PCS_RX,             BRD_ALL},
  {0x1B,   0x00,        PMD_ALL, PHY_PCS_RX_LR_PRD,      BRD_ALL},
  {0x2F,   0x79,        PMD_ALL, PHY_PCS_RX,             BRD_ALL},
  {0x76,   0x03,        PMD_ALL, PHY_PCS_RX,             BRD_ZEBU},
  {0x84,   0x01,        PMD_ALL, PHY_PCS_RX,             BRD_ALL},
  {0x04,   0x01,        PMD_ALL, PHY_PCS_TX,             BRD_ALL},
  {0x25,   0xF6,        PMD_ALL, PHY_PCS_RX,             BRD_ALL},
  {0x7F,   0x00,        PMD_ALL, PHY_PCS_TX,             BRD_ALL},
  {0x200,  0x00,        PMD_ALL, PHY_PCS_COMN,           BRD_ALL},
  {0x155E, 0x00,        PMD_ALL, UNIPRO_STD_MIB,         BRD_ALL},
  {0x3000, 0x00,        PMD_ALL, UNIPRO_STD_MIB,         BRD_ALL},
  {0x3001, 0x01,        PMD_ALL, UNIPRO_STD_MIB,         BRD_ALL},
  {0x4021, 0x01,        PMD_ALL, UNIPRO_STD_MIB,         BRD_ALL},
  {0x4020, 0x01,        PMD_ALL, UNIPRO_STD_MIB,         BRD_ALL},
  {0x10C,  0x10,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0x118,  0x48,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0x81C,  0x0C,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xB84,  0x40,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0x974,  0x00,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0x978,  0x3F,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0x97C,  0xFF,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0x990,  0x4E,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0x9B8,  0x5E,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0x9BC,  0x70,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xBB4,  0x25,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAB0,  0x13,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xACC,  0x05,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAD8,  0x10,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xADC,  0x10,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAE0,  0x10,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAE4,  0x10,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAE8,  0x10,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAEC,  0x08,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAF0,  0x08,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAF4,  0x08,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAF8,  0x08,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xBCC,  0x80,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0x28,   0x33,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0x34,   0xB9,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0x38,   0x0F,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0x44,   0x01,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0xB0,   0x30,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0x104,  0x20,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0xB90,  0x18,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0x10C,  0x18,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0x10C,  0x00,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0xCE0,  0x08,        PMD_ALL, PHY_EMB_CAL_WAIT,       BRD_ALL},
  {0xA006, 0x00,        PMD_ALL, UNIPRO_DBG_MIB,         BRD_ALL},
  {0, 0, 0, 0, 0},
};

static const struct UfsCalPhyCfg init_cfg_card[] = {
  {0, 0, 0, 0, 0},
};

static const struct UfsCalPhyCfg post_init_cfg_evt0_g3[] = {
  {0x00, 0x00, PMD_ALL, UNIPRO_ADAPT_LENGTH, BRD_ALL},
  {0x0B, 0x00, PMD_ALL, UNIPRO_ADAPT_LENGTH, BRD_ALL},
  {0, 0, 0, 0, 0},
};
static const struct UfsCalPhyCfg post_init_cfg_evt1_g3[] = {
  {0x00, 0x00, PMD_ALL, UNIPRO_ADAPT_LENGTH, BRD_ALL},
  {0x0B, 0x00, PMD_ALL, UNIPRO_ADAPT_LENGTH, BRD_ALL},
  {0, 0, 0, 0, 0},
};

static const struct UfsCalPhyCfg post_init_cfg_evt0_g4[] = {
  {0x00, 0x00, PMD_ALL, UNIPRO_ADAPT_LENGTH, BRD_ALL},
  {0x0B, 0x00, PMD_ALL, UNIPRO_ADAPT_LENGTH, BRD_ALL},
  {0, 0, 0, 0, 0},
};
static const struct UfsCalPhyCfg post_init_cfg_evt1_g4[] = {
  {0x00, 0x00, PMD_ALL, UNIPRO_ADAPT_LENGTH, BRD_ALL},
  {0x0B, 0x00, PMD_ALL, UNIPRO_ADAPT_LENGTH, BRD_ALL},
  {0, 0, 0, 0, 0},
};
static const struct UfsCalPhyCfg post_init_cfg_card[] = {
  {0, 0, 0, 0, 0},
};

static const struct UfsCalPhyCfg calib_of_pwm[] = {
  {0x00, 0x9E, PMD_PWM, UNIPRO_STD_MIB, BRD_ALL},
  {0, 0, 0, 0, 0},
};
static const struct UfsCalPhyCfg calib_of_pwm_card[]  = { {0,0,0,0,0} };
static const struct UfsCalPhyCfg post_calib_of_pwm[] = {
  {0x00, 0x9E, PMD_PWM, UNIPRO_STD_MIB, BRD_ALL},
  {0, 0, 0, 0, 0},
};
static const struct UfsCalPhyCfg post_calib_of_pwm_card[] = { {0,0,0,0,0} };

static const struct UfsCalPhyCfg calib_of_hs_rate_a[] = {
  {0x9D, 0x00, PMD_HS,  PHY_PCS_RX,     BRD_ALL},
  {0x9E, 0x00, PMD_HS,  PHY_PCS_RX,     BRD_ALL},
  {0x0B, 0x00, PMD_HS,  UNIPRO_STD_MIB, BRD_ALL},
  {0, 0, 0, 0, 0},
};
static const struct UfsCalPhyCfg calib_of_hs_rate_a_card[] = { {0,0,0,0,0} };
static const struct UfsCalPhyCfg post_calib_of_hs_rate_a[] = {
  {0x00, 0x76, PMD_HS,  UNIPRO_STD_MIB, BRD_ALL},
  {0, 0, 0, 0, 0},
};
static const struct UfsCalPhyCfg post_calib_of_hs_rate_a_card[] = { {0,0,0,0,0} };

static const struct UfsCalPhyCfg calib_of_hs_rate_b[] = {
  {0x9D, 0x00, PMD_HS,  PHY_PCS_RX,     BRD_ALL},
  {0x9E, 0x00, PMD_HS,  PHY_PCS_RX,     BRD_ALL},
  {0x0B, 0x00, PMD_HS,  UNIPRO_STD_MIB, BRD_ALL},
  {0, 0, 0, 0, 0},
};
static const struct UfsCalPhyCfg calib_of_hs_rate_b_card[] = { {0,0,0,0,0} };
static const struct UfsCalPhyCfg post_calib_of_hs_rate_b[] = {
  {0x00, 0x76, PMD_HS,  UNIPRO_STD_MIB, BRD_ALL},
  {0, 0, 0, 0, 0},
};
static const struct UfsCalPhyCfg post_calib_of_hs_rate_b_card[] = { {0,0,0,0,0} };

static const struct UfsCalPhyCfg post_h8_enter[] = {
  {0xCE0, 0x00, PMD_HS, PHY_PMA_TRSV, BRD_ALL},
  {0, 0, 0, 0, 0},
};
static const struct UfsCalPhyCfg post_h8_enter_card[] = { {0,0,0,0,0} };
static const struct UfsCalPhyCfg pre_h8_exit[] = {
  {0xCE0, 0x08, PMD_HS, PHY_EMB_CDR_WAIT, BRD_ALL},
  {0, 0, 0, 0, 0},
};
static const struct UfsCalPhyCfg pre_h8_exit_card[] = { {0,0,0,0,0} };

static const struct UfsCalPhyCfg lane1_sq_off[] = {
  {0x750, 0x02, PMD_ALL, PHY_PMA_TRSV_LANE1_SQ_OFF, BRD_ALL},
  {0, 0, 0, 0, 0},
};
static const struct UfsCalPhyCfg lane1_sq_off_card[] = { {0,0,0,0,0} };

static UfsCalError ufs_cal_wait_pll_lock (void *hba, UINT32 addr, UINT32 mask)
{
  UINT32 i;
  for (i = 0; i < 100; i++) {
    if ((ufs_lld_pma_read (hba, PHY_PMA_COMN_ADDR (addr)) & mask) == mask)
      return UFS_CAL_NO_ERROR;
    MicroSecondDelay(1);
  }
  DEBUG ((DEBUG_INFO, "UFS CAL: PLL lock timeout\n"));
  return UFS_CAL_ERROR;
}

static UfsCalError ufs_cal_wait_cdr_lock (void *hba, UINT32 addr, UINT32 mask, int lane)
{
  UINT32 i;
  for (i = 0; i < 1000; i++) {
    if ((ufs_lld_pma_read (hba, PHY_PMA_TRSV_ADDR (addr, lane)) & mask) == mask)
      return UFS_CAL_NO_ERROR;
    MicroSecondDelay(1);
  }
  DEBUG ((DEBUG_INFO, "UFS CAL: CDR lock timeout lane %d\n", lane));
  return UFS_CAL_ERROR;
}

static UfsCalError ufs_cal_wait_cdr_afc_check (void *hba, UINT32 addr, UINT32 mask, int lane)
{
  UINT32 i;
  for (i = 0; i < 1000; i++) {
    if ((ufs_lld_pma_read (hba, PHY_PMA_TRSV_ADDR (addr, lane)) & mask) == mask)
      return UFS_CAL_NO_ERROR;
    MicroSecondDelay(1);
  }
  return UFS_CAL_ERROR;
}

static UfsCalError ufs30_cal_wait_cdr_lock (void *hba, UINT32 addr, UINT32 mask, int lane)
{
  return ufs_cal_wait_cdr_lock (hba, addr, mask, lane);
}

static UfsCalError ufs30_cal_done_wait (void *hba, UINT32 addr, UINT32 mask, int lane)
{
  UINT32 i;
  for (i = 0; i < 500; i++) {
    if ((ufs_lld_pma_read (hba, PHY_PMA_TRSV_ADDR (addr, lane)) & mask) == 0)
      return UFS_CAL_NO_ERROR;
    MicroSecondDelay(1);
  }
  DEBUG ((DEBUG_INFO, "UFS CAL: EMB CAL timeout lane %d\n", lane));
  return UFS_CAL_ERROR;
}

static UfsCalError ufs_cal_config_uic (
  struct UfsCalParam *p,
  const struct UfsCalPhyCfg *cfg,
  struct UicPwrMode *Pmd)
{
  void *hba = p->Host;
  UINT8 Lane;
  UINT8 NumLanes = p->AvailableLane;
  UINT32 PmdMask  = PMD_ALL;

  if (Pmd) {
    if (IS_PWR_MODE_HS (Pmd->Mode)) {
      if (Pmd->HsSeries == PA_HS_MODE_A) {
        PmdMask = (1U << (__PMD_HS_G1_L1 + ((Pmd->Gear - 1) * 2) + (Pmd->Lane - 1)));
      } else {
        PmdMask = (1U << (__PMD_HS_G1_L1 + ((Pmd->Gear - 1) * 2) + (Pmd->Lane - 1)));
      }
      PmdMask = PMD_HS;  /* simplified: apply all HS entries */
    } else {
      PmdMask = PMD_PWM;
    }
  }

  for_each_phy_cfg (cfg) {
    if (!(cfg->Board & p->Board))
      continue;

    for (Lane = 0; Lane < NumLanes; Lane++) {
      if (!(cfg->Flag & PmdMask) && Pmd)
        continue;

      switch (cfg->Layer) {
      case UNIPRO_STD_MIB:
      case UNIPRO_DBG_MIB:
        if (Lane == 0)
          ufs_lld_dme_set (hba, UIC_ARG_MIB (cfg->Address), cfg->Value);
        break;
      case UNIPRO_DBG_PRD:
        if (Lane == 0)
          ufs_lld_dme_set (hba, UIC_ARG_MIB (cfg->Address), UNIPRO_MCLK_PERIOD (p));
        break;
      case UNIPRO_ADAPT_LENGTH:
        if (Lane == 0)
          ufs_lld_dme_set (hba, UIC_ARG_MIB (cfg->Address), cfg->Value);
        break;
      case PHY_PCS_COMN:
        if (Lane == 0)
          ufs_lld_dme_set (hba, UIC_ARG_MIB (cfg->Address), cfg->Value);
        break;
      case PHY_PCS_RXTX:
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->Address, TX_LANE_0 + Lane), cfg->Value);
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->Address, RX_LANE_0 + Lane), cfg->Value);
        break;
      case PHY_PCS_RX:
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->Address, RX_LANE_0 + Lane), cfg->Value);
        break;
      case PHY_PCS_TX:
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->Address, TX_LANE_0 + Lane), cfg->Value);
        break;
      case PHY_PCS_RX_PRD:
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->Address, RX_LANE_0 + Lane),
                         UNIPRO_MCLK_PERIOD (p));
        break;
      case PHY_PCS_TX_PRD:
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->Address, TX_LANE_0 + Lane),
                         UNIPRO_MCLK_PERIOD (p));
        break;
      case PHY_PCS_RX_PRD_ROUND_OFF:
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->Address, RX_LANE_0 + Lane),
                         UNIPRO_MCLK_PERIOD_ROUND_OFF (p));
        break;
      case PHY_PCS_TX_PRD_ROUND_OFF:
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->Address, TX_LANE_0 + Lane),
                         UNIPRO_MCLK_PERIOD_ROUND_OFF (p));
        break;
      case PHY_PCS_RX_LR_PRD:
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->Address,   RX_LANE_0+Lane), (PCS_RX_LINE_RESET_DETECT_PERIOD(p)>>16)&0xFF);
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->Address+1, RX_LANE_0+Lane), (PCS_RX_LINE_RESET_DETECT_PERIOD(p)>>8)&0xFF);
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->Address+2, RX_LANE_0+Lane), (PCS_RX_LINE_RESET_DETECT_PERIOD(p)>>0)&0xFF);
        break;
      case PHY_PCS_TX_LR_PRD:
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->Address,   TX_LANE_0+Lane), (PCS_TX_LINE_RESET_PERIOD(p)>>16)&0xFF);
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->Address+1, TX_LANE_0+Lane), (PCS_TX_LINE_RESET_PERIOD(p)>>8)&0xFF);
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->Address+2, TX_LANE_0+Lane), (PCS_TX_LINE_RESET_PERIOD(p)>>0)&0xFF);
        break;
      case PHY_PMA_COMN:
        if (Lane == 0)
          ufs_lld_pma_write (hba, cfg->Value, PHY_PMA_COMN_ADDR (cfg->Address));
        break;
      case PHY_PMA_TRSV:
        ufs_lld_pma_write (hba, cfg->Value, PHY_PMA_TRSV_ADDR (cfg->Address, Lane));
        break;
      case PHY_PMA_TRSV_LANE1_SQ_OFF:
        if (Lane == 1) {
          if (p->ConnectedRxLane < p->AvailableLane)
            ufs_lld_pma_write (hba, cfg->Value, PHY_PMA_TRSV_ADDR (cfg->Address, Lane));
        }
        break;
      case PHY_PMA_TRSV_SQ:
        if (Lane < p->ConnectedRxLane)
          ufs_lld_pma_write (hba, cfg->Value, PHY_PMA_TRSV_ADDR (cfg->Address, Lane));
        break;
      case UNIPRO_DBG_APB:
        if (Lane == 0)
          ufs_lld_unipro_write (hba, cfg->Value, cfg->Address);
        break;
      case PHY_PLL_WAIT:
        if (Lane == 0) {
          if (ufs_cal_wait_pll_lock (hba, cfg->Address, cfg->Value) == UFS_CAL_ERROR)
            return UFS_CAL_TIMEOUT;
        }
        break;
      case PHY_CDR_WAIT:
        if (Lane < p->ActiveRxLane) {
          if (ufs_cal_wait_cdr_lock (hba, cfg->Address, cfg->Value, Lane) == UFS_CAL_ERROR)
            return UFS_CAL_TIMEOUT;
        }
        break;
      case PHY_EMB_CDR_WAIT:
        if (Lane < p->ActiveRxLane) {
          if (ufs30_cal_wait_cdr_lock (hba, cfg->Address, cfg->Value, Lane) == UFS_CAL_ERROR)
            return UFS_CAL_TIMEOUT;
        }
        break;
      case PHY_CDR_AFC_WAIT:
        if (Lane < p->ActiveRxLane) {
          if (p->Table == HOST_CARD) {
            if (ufs_cal_wait_cdr_afc_check (hba, cfg->Address, cfg->Value, Lane) == UFS_CAL_ERROR)
              return UFS_CAL_TIMEOUT;
          }
        }
        break;
      case COMMON_WAIT:
        if (Lane == 0)
          MicroSecondDelay(cfg->Value);
        break;
      case PHY_EMB_CAL_WAIT:
        if (ufs30_cal_done_wait (hba, cfg->Address, cfg->Value, Lane) == UFS_CAL_ERROR)
          return UFS_CAL_TIMEOUT;
        break;
      default:
        break;
      }
    }
  }
  return UFS_CAL_NO_ERROR;
}

/* ─── Hibern8 calibration (from lk3rd) ──────────────────────────────────────*/
static void ufs_cal_calib_hibern8_values (void *hba)
{
  UINT32 hw_cap_min_tactivate;
  UINT32 peer_rx_min_actv_time_cap;
  UINT32 max_rx_hibern8_time_cap;

  ufs_lld_dme_get (hba, UIC_ARG_MIB_SEL (0x8F, RX_LANE_0), &hw_cap_min_tactivate);
  ufs_lld_dme_get (hba, UIC_ARG_MIB (0x15A8), &peer_rx_min_actv_time_cap);
  ufs_lld_dme_get (hba, UIC_ARG_MIB (0x15A7), &max_rx_hibern8_time_cap);

  if (peer_rx_min_actv_time_cap >= hw_cap_min_tactivate)
    ufs_lld_dme_peer_set (hba, UIC_ARG_MIB (0x15A8), peer_rx_min_actv_time_cap + 1);
  ufs_lld_dme_set (hba, UIC_ARG_MIB (0x15A7), max_rx_hibern8_time_cap + 1);
}

/* ─── Public CAL API ─────────────────────────────────────────────────────────*/

UfsCalError ufs_cal_post_h8_enter (struct UfsCalParam *p)
{
  const struct UfsCalPhyCfg *cfg;
  cfg = (p->Table == HOST_CARD) ? post_h8_enter_card : post_h8_enter;
  return ufs_cal_config_uic (p, cfg, p->Pmd);
}

UfsCalError ufs_cal_pre_h8_exit (struct UfsCalParam *p)
{
  const struct UfsCalPhyCfg *cfg;
  cfg = (p->Table == HOST_CARD) ? pre_h8_exit_card : pre_h8_exit;
  return ufs_cal_config_uic (p, cfg, p->Pmd);
}

UfsCalError ufs_cal_pre_pmc (struct UfsCalParam *p)
{
  const struct UfsCalPhyCfg *cfg;

  if ((p->Pmd->Mode == SLOW_MODE) || (p->Pmd->Mode == SLOWAUTO_MODE))
    cfg = (p->Table == HOST_CARD) ? calib_of_pwm_card : calib_of_pwm;
  else if (p->Pmd->HsSeries == PA_HS_MODE_B)
    cfg = (p->Table == HOST_CARD) ? calib_of_hs_rate_b_card : calib_of_hs_rate_b;
  else if (p->Pmd->HsSeries == PA_HS_MODE_A)
    cfg = (p->Table == HOST_CARD) ? calib_of_hs_rate_a_card : calib_of_hs_rate_a;
  else
    return UFS_CAL_INV_ARG;

  return ufs_cal_config_uic (p, cfg, p->Pmd);
}

UfsCalError ufs_cal_post_pmc (struct UfsCalParam *p)
{
  const struct UfsCalPhyCfg *cfg;

  if ((p->Pmd->Mode == SLOWAUTO_MODE) || (p->Pmd->Mode == SLOW_MODE))
    cfg = (p->Table == HOST_CARD) ? post_calib_of_pwm_card : post_calib_of_pwm;
  else if (p->Pmd->HsSeries == PA_HS_MODE_B)
    cfg = (p->Table == HOST_CARD) ? post_calib_of_hs_rate_b_card : post_calib_of_hs_rate_b;
  else if (p->Pmd->HsSeries == PA_HS_MODE_A)
    cfg = (p->Table == HOST_CARD) ? post_calib_of_hs_rate_a_card : post_calib_of_hs_rate_a;
  else
    return UFS_CAL_INV_ARG;

  return ufs_cal_config_uic (p, cfg, p->Pmd);
}

UfsCalError ufs_cal_post_link (struct UfsCalParam *p)
{
  UfsCalError                 ret = UFS_CAL_NO_ERROR;
  const struct UfsCalPhyCfg *cfg;

  ufs_cal_calib_hibern8_values (p->Host);

  switch (p->MaxGear) {
  case GEAR_1:
  case GEAR_2:
  case GEAR_3:
    cfg = (p->EvtVer == 0) ? post_init_cfg_evt0_g3 : post_init_cfg_evt1_g3;
    if (p->Table == HOST_CARD) cfg = post_init_cfg_card;
    break;
  case GEAR_4:
    cfg = (p->EvtVer == 0) ? post_init_cfg_evt0_g4 : post_init_cfg_evt1_g4;
    if (p->Table == HOST_CARD) cfg = post_init_cfg_card;
    break;
  default:
    return UFS_CAL_INV_ARG;
  }

  ret = ufs_cal_config_uic (p, cfg, NULL);

  if (ret == UFS_CAL_NO_ERROR) {
    if ((p->AvailableLane == 2) && (p->ConnectedRxLane == 1)) {
      cfg = (p->Table == HOST_CARD) ? lane1_sq_off_card : lane1_sq_off;
      ret = ufs_cal_config_uic (p, cfg, NULL);
    }
  }

  return ret;
}

UfsCalError ufs_cal_pre_link (struct UfsCalParam *p)
{
  const struct UfsCalPhyCfg *cfg;

  if (p->Table == HOST_CARD) {
    cfg = init_cfg_card;
  } else {
    cfg = (p->EvtVer == 0) ? init_cfg_evt0 : init_cfg_evt1;
  }

  return ufs_cal_config_uic (p, cfg, NULL);
}

UfsCalError UfsCalInit (struct UfsCalParam *p)
{
  ufs_cal[0] = p;
  ufs_cal_lock_timeout = 1000;

  return UFS_CAL_NO_ERROR;
}

// Board Specific End

static
EFI_STATUS
UfsInitCal (struct UfsHost *Ufs)
{
  Ufs->CalParam->Host = Ufs;
  Ufs->CalParam->Board = BRD_UNIV;
  // TODO: Derive from ChipInfo driver.
  Ufs->CalParam->EvtVer  = (MmioRead32(0x10000010UL) >> 20) & 0xf;
  DEBUG((EFI_D_ERROR, "UFS: EVT version %d\n", Ufs->CalParam->EvtVer));

  if (UfsCalInit(Ufs->CalParam) != UFS_CAL_NO_ERROR) {
    DEBUG ((EFI_D_ERROR, "UFS: ufs_cal_init failed\n"));
    return EFI_DEVICE_ERROR;
  }
  return EFI_SUCCESS;
}

STATIC
VOID
UfsDeviceReset (
  struct UfsHost *Ufs
)
{
  MmioWrite32((UINTN)(Ufs->VsAddr + VS_GPIO_OUT), 0);
  MicroSecondDelay(5);
  MmioWrite32((UINTN)(Ufs->VsAddr + VS_GPIO_OUT), 1);
}

STATIC
VOID
UfsDevicePower (
  struct UfsHost *Ufs,
  UINT8 On
)
{
  UINT32 Register;
  UINT32 Mask;

  Mask = 1 << (Ufs->DevPwrShift);

  Register = MmioRead32((UINTN)Ufs->DevPwrAddr);
  Register &= ~Mask;
  Register |= (On << Ufs->DevPwrShift);

  MmioWrite32((UINTN)Ufs->DevPwrAddr, Register);
}

STATIC
EFI_STATUS
UfsPreSetup (
  struct UfsHost *Ufs
)
{
  UINT32 Register;
  DEBUG((EFI_D_ERROR, "UFS pre-setup\n"));
  
  /* UFS_PHY_CONTROL : 1 = Isolation bypassed, PMU MPHY ON */
  Register = MmioRead32((UINTN)Ufs->PhyIsoAddr);
  if (!(Register & 1))
  {
    Register |= 1;
    MmioWrite32((UINTN)Ufs->PhyIsoAddr, Register);
  }

  /* VS_SW_RST */
  if((MmioRead32((UINTN)(Ufs->VsAddr + VS_FORCE_HCS)) >> 4) & 0xF)
    MmioWrite32((UINTN)(Ufs->VsAddr + VS_FORCE_HCS), 0);

  MmioWrite32((UINTN)(Ufs->VsAddr + VS_SW_RST), 3);
  while (MmioRead32((UINTN)(Ufs->VsAddr + VS_SW_RST))); // Wait for reset

  /* VENDOR_SPECIFIC_IS[20] : clear UFS_IDLE_Indicator bit (if UFS_LINK is reset, this bit is asserted) */
  Register = MmioRead32((UINTN)(Ufs->VsAddr + VS_IS));
  if ((Register >> 20) & 1)
    MmioWrite32((UINTN)(Ufs->VsAddr + VS_IS), Register);

  UfsDevicePower(Ufs, 1);
  MicroSecondDelay(1000);
  UfsDeviceReset(Ufs);

  // TODO: Handle broken HCE for boards that have it
  // Send reset host command and enable command when that quirk is present.

  MmioWrite32((UINTN)(Ufs->IoAddr + REG_CONTROLLER_ENABLE), 1);
  while(!(MmioRead32((UINTN)(Ufs->IoAddr + REG_CONTROLLER_ENABLE)) & 1))
    MicroSecondDelay(1);

  /* ctrl refclkout */
  MmioWrite32((UINTN)(Ufs->VsAddr + VS_CLKSTOP_CTRL), MmioRead32((UINTN)(Ufs->VsAddr + VS_CLKSTOP_CTRL)) & ~(1 << 4));

  /* Set clock gating */
  MmioWrite32((UINTN)(Ufs->VsAddr + VS_FORCE_HCS), 0x0DE0);
  MmioWrite32((UINTN)(Ufs->VsAddr + VS_UFS_ACG_DISABLE), MmioRead32((UINTN)(Ufs->VsAddr + VS_UFS_ACG_DISABLE)) | 1);

  ZeroMem (Ufs->CmdDescAddr, UFS_NUTRS * sizeof (struct UfsCmdDesc));
  ZeroMem (Ufs->UtrdAddr, UFS_NUTRS * sizeof (struct UfsUtrd));

  Ufs->UtrdAddr->cmd_desc_addr_l = (UINT32)(UINTN)Ufs->CmdDescAddr;
  Ufs->UtrdAddr->cmd_desc_addr_h = (UINT32)((UINTN)Ufs->CmdDescAddr >> 32);
  Ufs->UtrdAddr->rsp_upiu_off = OFFSET_OF (struct UfsCmdDesc, ResponseUpiu);
  Ufs->UtrdAddr->rsp_upiu_len = ALIGNED_UPIU_SIZE;

  MmioWrite32((UINTN)(Ufs->IoAddr + REG_UTP_TASK_REQ_LIST_BASE_L), (UINT32)(UINTN)Ufs->UtmrdAddr);
  MmioWrite32((UINTN)(Ufs->IoAddr + REG_UTP_TASK_REQ_LIST_BASE_H), (UINT32)((UINTN)Ufs->UtmrdAddr >> 32));

  MmioWrite32((UINTN)(Ufs->IoAddr + REG_UTP_TRANSFER_REQ_LIST_BASE_L), (UINT32)(UINTN)Ufs->UtrdAddr);
  MmioWrite32((UINTN)(Ufs->IoAddr + REG_UTP_TRANSFER_REQ_LIST_BASE_H), (UINT32)((UINTN)Ufs->UtrdAddr >> 32));

  // TODO: cport
	MmioWrite32((UINTN)(Ufs->VsAddr + 0x0114), 0x22);
	MmioWrite32((UINTN)(Ufs->VsAddr + 0x0110), 1);

  return EFI_SUCCESS;
}

STATIC
INT32
UfsHandleUicInt (
  struct UfsHost *Ufs,
  UINT32 stat
)
{
  INT32 Ret = UFS_IN_PROGRESS;
  struct UfsUicCmd *Cmd = Ufs->UicCmd;

  if (stat & UIC_COMMAND_COMPL) {
    if (Cmd->uiccmdr == UIC_CMD_DME_LINK_STARTUP)
      Ret = UFS_NO_ERROR;
    else if ((Cmd->uiccmdr == UIC_CMD_DME_SET) && (Cmd->Arg1 == (0x1571U << 16))) {
      if (stat & UIC_POWER_MODE) Ret = UFS_NO_ERROR;
    }
    else
      Ret = UFS_NO_ERROR;
  }

  if ((stat & UIC_ERROR) && (Cmd->uiccmdr != UIC_CMD_DME_LINK_STARTUP)) {
    DEBUG ((EFI_D_ERROR, "UFS: UIC ERROR 0x%08x\n", stat));
    Ret = UFS_ERROR;
  }
  return Ret;
}

STATIC
INT32
UfsHandleUtpInt (
  struct UfsHost *Ufs,
  UINT32 stat
)
{
  if (stat & UTP_TRANSFER_REQ_COMPL) {
    if (!(MmioRead32((UINTN)(Ufs->IoAddr + REG_UTP_TRANSFER_REQ_DOOR_BELL)) & 1))
      return UFS_NO_ERROR;
  }
  return UFS_IN_PROGRESS;
}

STATIC
INT32
UfsHandleInt (
  struct UfsHost *Ufs,
  BOOLEAN IsUic
)
{
  UINT32 Stat = MmioRead32((UINTN)(Ufs->IoAddr + REG_INTERRUPT_STATUS));
  INT32 Ret;

  Ret = IsUic ? UfsHandleUicInt(Ufs, Stat) : UfsHandleUtpInt (Ufs, Stat);

  if (Stat & INT_FATAL_ERRORS) {
    DEBUG ((EFI_D_ERROR, "UFS: Fatal error 0x%08x\n", Stat));
    Ret = UFS_ERROR;
  }

  if (Ret == UFS_IN_PROGRESS) {
    if (Ufs->Timeout--)
      MicroSecondDelay(1);
    else {
      Ret = UFS_TIMEOUT;
      DEBUG ((EFI_D_ERROR, "UFS Timeout\n"));
    }
  }

  return Ret;
}

STATIC
INT32
UfsSendUicCmd (struct UfsHost *Ufs)
{
  INT32 Error, ErrorCode;

  MmioWrite32((UINTN)(Ufs->IoAddr + REG_UIC_COMMAND_ARG_1), Ufs->UicCmd->Arg1);
  MmioWrite32((UINTN)(Ufs->IoAddr + REG_UIC_COMMAND_ARG_2), Ufs->UicCmd->Arg2);
  MmioWrite32((UINTN)(Ufs->IoAddr + REG_UIC_COMMAND_ARG_3), Ufs->UicCmd->Arg3);
  MmioWrite32((UINTN)(Ufs->IoAddr + REG_UIC_COMMAND), Ufs->UicCmd->uiccmdr);

  Ufs->Timeout = Ufs->UicCmdTimeout;
  while (UFS_IN_PROGRESS == (Error = UfsHandleInt(Ufs, TRUE)));

  MmioWrite32((UINTN)(Ufs->IoAddr + REG_INTERRUPT_STATUS), MmioRead32((UINTN)(Ufs->IoAddr + REG_INTERRUPT_STATUS)));

  ErrorCode = MmioRead32((UINTN)(Ufs->IoAddr + REG_UIC_COMMAND_ARG_2));

  if (Ufs->UicCmd->uiccmdr == UIC_CMD_DME_GET ||
      Ufs->UicCmd->uiccmdr == UIC_CMD_DME_PEER_GET) {
    Ufs->UicCmd->Arg3 = MmioRead32((UINTN)(Ufs->IoAddr + REG_UIC_COMMAND_ARG_3));
  }

  return ErrorCode | Error;
}

STATIC
EFI_STATUS
UfsPreLink (
  struct UfsHost *Ufs,
  UINT8 lane
)
{
  struct UfsCalParam *p = Ufs->CalParam;

  p->MclkRate = Ufs->MclkRate;
  p->AvailableLane = lane;
  p->Table = HOST_EMBD;

  if (ufs_cal_pre_link(Ufs->CalParam) != UFS_CAL_NO_ERROR) {
    DEBUG ((EFI_D_ERROR, "UFS pre-link calibration failed\n"));
    return EFI_DEVICE_ERROR;
  }

  DEBUG ((EFI_D_ERROR, "UFS pre-link calibration passed\n"));
  return EFI_SUCCESS;
}

static void UfsVendorSetup (struct UfsHost *Ufs)
{
  MmioWrite32((UINTN)(Ufs->VsAddr + VS_DATA_REORDER), 0xA);
  MmioWrite32((UINTN)(Ufs->IoAddr + REG_UTP_TASK_REQ_LIST_RUN_STOP), 1);
  MmioWrite32((UINTN)(Ufs->IoAddr + REG_UTP_TRANSFER_REQ_LIST_RUN_STOP), 1);
  MmioWrite32((UINTN)(Ufs->VsAddr + VS_TXPRDT_ENTRY_SIZE), UFS_SG_BLOCK_SIZE_BIT);
  MmioWrite32((UINTN)(Ufs->VsAddr + VS_RXPRDT_ENTRY_SIZE), UFS_SG_BLOCK_SIZE_BIT);
  MmioWrite32((UINTN)(Ufs->VsAddr + VS_UTRL_NEXUS_TYPE), 0xFFFFFFFF);
  MmioWrite32((UINTN)(Ufs->VsAddr + VS_UMTRL_NEXUS_TYPE), 0xFFFFFFFF);
}

STATIC
EFI_STATUS
UfsUpdateMaxGear (
  struct UfsHost *Ufs)
{
  /* PA_MaxRxHSGear = 0x1587 */
  struct UfsUicCmd cmd = {UIC_CMD_DME_GET, UIC_ARG_MIB (0x1587), 0, 0};

  Ufs->UicCmd = &cmd;

  if (UfsSendUicCmd (Ufs))
    return EFI_DEVICE_ERROR;

  Ufs->CalParam->MaxGear = (UINT8)MIN(cmd.Arg3, (UINT32)Ufs->GearMode);

  DEBUG ((EFI_D_ERROR, "UFS Max Gear: %d\n", Ufs->CalParam->MaxGear));
  return 0;
}

STATIC
EFI_STATUS
UfsUpdateActiveLane (struct UfsHost *Ufs)
{
  struct UfsUicCmd tx = {UIC_CMD_DME_GET, UIC_ARG_MIB (0x1560), 0, 0}; /* PA_ActiveTxDataLanes */
  struct UfsUicCmd rx = {UIC_CMD_DME_GET, UIC_ARG_MIB (0x1580), 0, 0}; /* PA_ActiveRxDataLanes */

  Ufs->UicCmd = &tx;
  if (UfsSendUicCmd (Ufs)) return EFI_DEVICE_ERROR;
  Ufs->CalParam->ActiveTxLane = (UINT8)tx.Arg3;

  Ufs->UicCmd = &rx;
  if (UfsSendUicCmd (Ufs)) return EFI_DEVICE_ERROR;
  Ufs->CalParam->ActiveRxLane = (UINT8)rx.Arg3;

  DEBUG ((EFI_D_ERROR, "UFS active TX=%d RX=%d\n", Ufs->CalParam->ActiveTxLane, Ufs->CalParam->ActiveRxLane));
  return 0;
}

static void UfsUtpInit (struct UfsHost *Ufs, UINT32 Lun)
{
  Ufs->Lun = Lun;
  Ufs->ScsiCmd = NULL;

  ZeroMem(Ufs->CmdDescAddr, sizeof (struct UfsCmdDesc));
  ZeroMem(Ufs->UtrdAddr, sizeof (struct UfsUtrd));

  Ufs->UtrdAddr->cmd_desc_addr_l = (UINT32)(UINTN)Ufs->CmdDescAddr;
  Ufs->UtrdAddr->cmd_desc_addr_h = (UINT32)((UINTN)Ufs->CmdDescAddr >> 32);
  Ufs->UtrdAddr->rsp_upiu_off    = OFFSET_OF(struct UfsCmdDesc, ResponseUpiu);
  Ufs->UtrdAddr->rsp_upiu_len    = ALIGNED_UPIU_SIZE;
}

STATIC
VOID
UfsUtpSend (
  struct UfsHost *Ufs,
  UINT32 Type
)
{
  switch (Type) {
  case UPIU_TRANSACTION_NOP_OUT:
  case UPIU_TRANSACTION_QUERY_REQ:
    MmioWrite32((UINTN)(Ufs->VsAddr + VS_UTRL_NEXUS_TYPE), 0x0);
    break;
  case UPIU_TRANSACTION_COMMAND:
    MmioWrite32((UINTN)(Ufs->VsAddr + VS_UTRL_NEXUS_TYPE), 0xFFFFFFFF);
    break;
  default:
    break;
  }

  switch (Type & 0xFF) {
  case UPIU_TRANSACTION_NOP_OUT:
  case UPIU_TRANSACTION_COMMAND:
  case UPIU_TRANSACTION_QUERY_REQ:
    MmioWrite32((UINTN)(Ufs->IoAddr + REG_UTP_TRANSFER_REQ_DOOR_BELL), 1);
    break;
  default:
    break;
  }
}

STATIC
INT32
UfsUtpWaitResponse (
  struct UfsHost *Ufs,
  UINT32 Type
)
{
  int err;

  if (Type == UPIU_TRANSACTION_COMMAND && Ufs->ScsiCmd->Cdb[0] == SCSI_OP_FORMAT_UNIT)
    Ufs->Timeout = FORMAT_CMD_TIMEOUT;
  else if (Type == UPIU_TRANSACTION_NOP_OUT)
    Ufs->Timeout = NOP_OUT_TIMEOUT;
  else
    Ufs->Timeout = Ufs->UfsCmdTimeout;

  while (UFS_IN_PROGRESS == (err = UfsHandleInt (Ufs, 0)));

  MmioWrite32((UINTN)(Ufs->IoAddr + REG_INTERRUPT_STATUS), MmioRead32((UINTN)(Ufs->IoAddr + REG_INTERRUPT_STATUS)));

  /* Nexus clear for NOP / Query */
  if (Type == UPIU_TRANSACTION_NOP_OUT || Type == UPIU_TRANSACTION_QUERY_REQ)
    MmioWrite32((UINTN)(Ufs->IoAddr + 0x140), (MmioRead32((UINTN)(Ufs->IoAddr + 0x140)) | 0x01));

  return err;
}

STATIC
EFI_STATUS
UfsUtpCheckResult (struct UfsHost *Ufs)
{
  struct UfsUtrd *utrd = Ufs->UtrdAddr;
  struct UfsUpiu *resp = &Ufs->CmdDescAddr->ResponseUpiu;
  struct UfsUpiuHeader *hdr = &resp->Header;

  if (Ufs->ScsiCmd)
    Ufs->ScsiCmd->Status = hdr->Status;

  if (utrd->dw[2] != OCS_SUCCESS)
  {
    DEBUG ((EFI_D_ERROR, "UFS: OCS=0x%02x response=0x%02x type=0x%02x\n", utrd->dw[2], hdr->Response, hdr->Type));
  
    return (hdr->Response != 0) ? EFI_DEVICE_ERROR : EFI_SUCCESS;
  }

  /* OCS is good, but also check SCSI status for COMMAND UPIUs */
  if (Ufs->ScsiCmd && hdr->Status != SCSI_STATUS_GOOD)
  {
    DEBUG ((EFI_D_ERROR, "UFS: SCSI CHECK CONDITION status=0x%02x cdb0=0x%02x\n", hdr->Status, Ufs->ScsiCmd->Cdb[0]));
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

static int UfsWriteQueryUcd (struct UfsHost *Ufs, QueryIndex qry)
{
  struct UfsUpiu *cmd = &Ufs->CmdDescAddr->CommandUpiu;
  struct UfsUpiuHeader*Header = &cmd->Header;
  UINT8 *Tsf = cmd->Tsf;
  UINT16 dlen;
  UINT32 info;
  UINT32 lun;

  Header->Type = UPIU_TRANSACTION_QUERY_REQ;
  Header->Flags = UPIU_CMD_FLAGS_NONE;
  Header->Tag = 0;
  Header->Function = gQueryParams[qry][0];

  Tsf[0] = gQueryParams[qry][1];  /* OPCODE */
  Tsf[1] = gQueryParams[qry][2];  /* IDN */
  Tsf[2] = gQueryParams[qry][3];  /* INDEX */
  Tsf[3] = gQueryParams[qry][4];  /* SELECTOR */

  if (Header->Function == UFS_STD_WRITE_REQ && Tsf[0] == UPIU_QUERY_OPCODE_WRITE_DESC && Tsf[1] == UPIU_DESC_ID_CONFIGURATION)
  {
    Ufs->DataSegLen = Ufs->DeviceDesc.bUD0BaseOffset + (8 * Ufs->DeviceDesc.bUDConfigPlength);
    dlen = Ufs->DataSegLen;
    if (dlen > UPIU_DATA_SIZE) return -1;
    Header->DataLength = SwapBytes16(dlen);
    info = SwapBytes16(dlen);
    CopyMem(&Tsf[6], &info, sizeof (UINT16));
  } else if (Header->Function == UFS_STD_READ_REQ) {
    info = SwapBytes16(UPIU_DATA_SIZE);
    CopyMem(&Tsf[6], &info, sizeof (UINT16));
  } else if (Header->Function == UFS_STD_WRITE_REQ) {
    Header->DataLength = 0;
  }

  if (Tsf[0] == UPIU_QUERY_OPCODE_WRITE_ATTR) {
    info = SwapBytes32(Ufs->Attributes.Array[Tsf[1]]);
    CopyMem(&Tsf[8], &info, sizeof(UINT32));
  } else if (Tsf[0] == UPIU_QUERY_OPCODE_SET_FLAG)
    Tsf[11] = Ufs->Flags.Array[Tsf[1]];

  if (Tsf[0] == UPIU_QUERY_OPCODE_WRITE_DESC && Tsf[1] == UPIU_DESC_ID_CONFIGURATION) {
    CopyMem(cmd->Data, &Ufs->ConfigDesc.Header, Ufs->DeviceDesc.bUD0BaseOffset);
    for (lun = 0; lun < 8; lun++)
      CopyMem(cmd->Data + Ufs->DeviceDesc.bUD0BaseOffset + lun * Ufs->DeviceDesc.bUDConfigPlength, &Ufs->ConfigDesc.Unit[lun], Ufs->DeviceDesc.bUDConfigPlength);
  }
  return 0;
}

static void UfsQueryReadInfo (struct UfsHost *Ufs, UINT8 idn)
{
  struct UfsUpiu *resp = &Ufs->CmdDescAddr->ResponseUpiu;
  UINT8 *data = resp->Data;
  VOID *dst  = NULL;
  UINTN len  = 0;
  UINT32 lun;

  switch (idn) {
  case UPIU_DESC_ID_UNIT:
    if (data[2] >= 8) {
      DEBUG((EFI_D_ERROR, "UFS: unit desc response INDEX %d out of range\n", data[2]));
      return;
    }
    dst = &Ufs->UnitDesc[data[2]];
    len = MIN(data[0], sizeof (struct UfsUnitDesc));
    break;
  case UPIU_DESC_ID_DEVICE:
    dst = &Ufs->DeviceDesc;
    len = MIN(data[0], sizeof (struct UfsDeviceDesc));
    break;
  case UPIU_DESC_ID_CONFIGURATION:
    Ufs->DataSegLen = Ufs->DeviceDesc.bUD0BaseOffset + 8 * Ufs->DeviceDesc.bUDConfigPlength;
    len = MIN(data[0], (UINT8)Ufs->DataSegLen);

    CopyMem (&Ufs->ConfigDesc.Header, resp->Data, Ufs->DeviceDesc.bUD0BaseOffset);

    for (lun = 0; lun < 8; lun++)
      CopyMem (&Ufs->ConfigDesc.Unit[lun], resp->Data + Ufs->DeviceDesc.bUD0BaseOffset + lun * Ufs->DeviceDesc.bUDConfigPlength, Ufs->DeviceDesc.bUDConfigPlength);
    return;
  case UPIU_DESC_ID_GEOMETRY:
    dst = &Ufs->GeometryDesc;
    len = MIN (data[0], sizeof (struct UfsGeometryDesc));
    break;
  default:
    return;
  }

  if (dst)
    CopyMem (dst, resp->Data, len);
}

static void UfsQueryGetData (struct UfsHost *Ufs, QueryIndex qry)
{
  struct UfsUpiu *resp = &Ufs->CmdDescAddr->ResponseUpiu;
  UINT8 opcode = gQueryParams[qry][1];
  UINT8 *tsf = resp->Tsf;
  UINT32 val;

  switch (opcode) {
  case UPIU_QUERY_OPCODE_READ_DESC:
    UfsQueryReadInfo(Ufs, gQueryParams[qry][2]);
    break;
  case UPIU_QUERY_OPCODE_READ_ATTR:
    val = (UINT32)(((UINT32)tsf[8] << 24) | ((UINT32)tsf[9] << 16) | ((UINT32)tsf[10] << 8) | ((UINT32)tsf[11]));
    Ufs->Attributes.Array[gQueryParams[qry][2]] = val;
    break;
  case UPIU_QUERY_OPCODE_READ_FLAG:
    Ufs->Flags.Array[gQueryParams[qry][2]] = (UINT32)tsf[11];
    break;
  default:
    break;
  }
}

STATIC
UINT32
UfsCmdGetDir (ScsiCommandMeta *Cmd)
{
  if (!Cmd->DataLen) return UTP_NO_DATA_TRANSFER;
  switch (Cmd->Cdb[0]) {
  case SCSI_OP_WRITE_10:
  case SCSI_OP_WRITE_BUFFER:
  case SCSI_OP_FORMAT_UNIT:
  case SCSI_OP_UNMAP:
  case SCSI_OP_SECU_PROT_OUT:
  case SCSI_OP_START_STOP_UNIT:
    return UTP_HOST_TO_DEVICE;
  default:
    return UTP_DEVICE_TO_HOST;
  }
}

/*STATIC
UINT32
UfsCmdGetFlags (ScsiCommandMeta *Cmd)
{
  if (!Cmd->DataLen) return UPIU_CMD_FLAGS_NONE;
  switch (Cmd->Cdb[0]) {
  case SCSI_OP_WRITE_10:
  case SCSI_OP_WRITE_BUFFER:
  case SCSI_OP_FORMAT_UNIT:
  case SCSI_OP_UNMAP:
  case SCSI_OP_SECU_PROT_OUT:
  case SCSI_MODE_SEL10:
    return UPIU_CMD_FLAGS_WRITE;
  case SCSI_OP_START_STOP_UNIT:
    return UPIU_CMD_FLAGS_NONE;
  default:
    return UPIU_CMD_FLAGS_READ;
  }
}*/

static int UfsWriteUtrd (struct UfsHost *Ufs, UINT32 type)
{
  struct UfsUtrd *utrd = Ufs->UtrdAddr;
  UINT32 Length, SgSegs;

  switch (type) {
  case UPIU_TRANSACTION_COMMAND:
    Length = Ufs->ScsiCmd->DataLen;
    SgSegs = (Length + UFS_SG_BLOCK_SIZE - 1) / UFS_SG_BLOCK_SIZE;

    utrd->dw[0] = UfsCmdGetDir (Ufs->ScsiCmd) | UTP_SCSI_COMMAND | UTP_REQ_DESC_INT_CMD;
    utrd->dw[2] = OCS_INVALID_COMMAND_STATUS;
    if (Length) {
      utrd->prdt_len = (UINT16)(SgSegs * sizeof (struct UfsPrdt));
      utrd->prdt_off = ALIGNED_UPIU_SIZE * 2;
    } else {
      utrd->prdt_len = 0;
      utrd->prdt_off = 0;
    }
    break;
  case UPIU_TRANSACTION_QUERY_REQ:
  case UPIU_TRANSACTION_NOP_OUT:
    utrd->dw[0] = UTP_REQ_DESC_INT_CMD;
    utrd->dw[2] = OCS_INVALID_COMMAND_STATUS;
    break;
  default:
    return -1;
  }
  return 0;
}

STATIC
EFI_STATUS
UfsUtpNopProcess (struct UfsHost *Ufs)
{
  UfsUtpInit(Ufs, 0);
  MemoryFence();
  UfsUtpSend(Ufs, UPIU_TRANSACTION_NOP_OUT);
  if (UfsUtpWaitResponse(Ufs, UPIU_TRANSACTION_NOP_OUT))
    return -1;
  return UfsUtpCheckResult(Ufs);
}

STATIC
EFI_STATUS
UfsUtpQueryProcess (struct UfsHost *Ufs, QueryIndex qry, UINT32 lun)
{
  UfsUtpInit (Ufs, lun);
  if (UfsWriteQueryUcd (Ufs, qry)) return -1;
  if (UfsWriteUtrd (Ufs, UPIU_TRANSACTION_QUERY_REQ)) return -1;
  MemoryFence();
  UfsUtpSend (Ufs, UPIU_TRANSACTION_QUERY_REQ);
  if (UfsUtpWaitResponse (Ufs, UPIU_TRANSACTION_QUERY_REQ)) return -1;
  if (EFI_ERROR(UfsUtpCheckResult (Ufs))) return EFI_DEVICE_ERROR;
  UfsQueryGetData (Ufs, qry);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
UfsUtpQueryRetry (struct UfsHost *Ufs, QueryIndex qry, UINT32 lun)
{
  EFI_STATUS Status;
  int retries;
  for (retries = QUERY_REQ_RETRIES; retries > 0; retries--)
  {
    Status = UfsUtpQueryProcess (Ufs, qry, lun);
    if (Status == EFI_SUCCESS) break;
    DEBUG ((DEBUG_WARN, "UFS: Query retry (left=%d)\n", retries - 1));
  }
  return Status;
}

STATIC
EFI_STATUS 
UfsEndBootMode (struct UfsHost *Ufs)
{
  int Retry = 100;
  UINT8 Flag;
  int res = -1;

  for (int i = 0; i < NOP_OUT_RETRY; i++) {
    res = UfsUtpNopProcess (Ufs);
    if (!res) break;
  }
  if (res) {
    DEBUG ((EFI_D_ERROR, "UFS: NOP OUT failed\n"));
    return res;
  }

  Ufs->Flags.Array[UPIU_FLAG_ID_DEVICEINIT] = 1;
  res = UfsUtpQueryRetry (Ufs, FLAG_W_FDEVICEINIT, 0);
  if (res) return res;

  do {
    res = UfsUtpQueryRetry (Ufs, FLAG_R_FDEVICEINIT, 0);
    if (res)
      return res;
  
    Flag = Ufs->Flags.Array[UPIU_FLAG_ID_DEVICEINIT];
    if (!Flag)
      break;
    MicroSecondDelay (1000);
  } while (Retry-- > 0);

  if (Flag)
  {
    DEBUG ((DEBUG_INFO, "UFS: fDeviceInit timeout\n"));
    return EFI_DEVICE_ERROR;
  }
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
UfsInitInterface (
  struct UfsHost *Ufs
)
{
  struct UfsUicCmd LinkCmd = {UIC_CMD_DME_LINK_STARTUP, 0, 0, 0};
  struct UfsUicCmd LaneCmd = {UIC_CMD_DME_GET, (0x1540U << 16), 0, 0};
  //struct UicPwrMode *Pmd = &Ufs->PmdCxt;
  EFI_STATUS Status;

  DEBUG((EFI_D_ERROR, "UFS Host interface init\n"));

  Status = UfsPreSetup(Ufs);
  if (EFI_ERROR(Status))
  {
    DEBUG((EFI_D_ERROR, "UFS pre-setup failed\n"));
    return Status;
  }

  DEBUG((EFI_D_ERROR, "UFS pre-setup pass\n"));

  // UfsPreVendorSetup stubbed out cause its empty on all plats

  DEBUG((EFI_D_ERROR, "Getting UFS Lanes\n"));

  Ufs->UicCmd = &LaneCmd;
  if(UfsSendUicCmd(Ufs))
  {
    DEBUG((EFI_D_ERROR, "UFS get lane count failed\n"));
    return EFI_DEVICE_ERROR;
  }

  DEBUG((EFI_D_ERROR, "UFS get lane count pass\n"));

  DEBUG((EFI_D_ERROR, "UFS lane count: %d\n", LaneCmd.Arg3));
  Ufs->CalParam->AvailableLane = (UINT8)LaneCmd.Arg3;

  Status = UfsPreLink(Ufs, Ufs->CalParam->AvailableLane);
  if (EFI_ERROR(Status))
  {
    DEBUG((EFI_D_ERROR, "UFS pre-link failed\n"));
    return Status;
  }

  DEBUG((EFI_D_ERROR, "UFS pre-link pass\n"));

  Ufs->UicCmd = &LinkCmd;
  if(UfsSendUicCmd(Ufs))
  {
    DEBUG((EFI_D_ERROR, "UFS link startup failed\n"));
    return EFI_DEVICE_ERROR;
  }

  DEBUG ((EFI_D_ERROR, "UFS link is up\n"));

  Status = UfsUpdateMaxGear(Ufs);
  if (EFI_ERROR(Status))
  {
    DEBUG((EFI_D_ERROR, "UFS update max gear failed\n"));
    return Status;
  }

  DEBUG((EFI_D_ERROR, "UFS update max gear pass\n"));

  if(ufs_cal_post_link(Ufs->CalParam) != UFS_CAL_NO_ERROR)
  {
    DEBUG((EFI_D_ERROR, "UFS post-link calibration failed\n"));
    return EFI_DEVICE_ERROR;
  }

  DEBUG((EFI_D_ERROR, "UFS post-link calibration pass\n"));

  Status = UfsUpdateActiveLane(Ufs);
  if (EFI_ERROR(Status))
  {
    DEBUG((EFI_D_ERROR, "UFS update active lane failed\n"));
    return Status;
  }
  
  DEBUG((EFI_D_ERROR, "UFS Active lanes updated\n"));

  UfsVendorSetup(Ufs);

  DEBUG((EFI_D_ERROR, "UFS vendor setup done\n"));

  Status = UfsEndBootMode(Ufs);
  if (EFI_ERROR(Status))
  {
    DEBUG((EFI_D_ERROR, "UFS end boot mode failed\n"));
    return Status;
  }

  DEBUG((EFI_D_ERROR, "UFS end boot mode done\n"));
  DEBUG((EFI_D_ERROR, "UFS device initialised\n"));

  while(1);
}

struct UfsHost *UfsAllocHost (VOID)
{
  struct UfsHost *Ufs;
  struct UfsCmdDesc *CmdDesc = NULL;
  struct UfsUtrd *Utrd = NULL;
  struct UfsUtmrd *Utmrd = NULL;
  struct UfsCalParam *Cal = NULL;

  Ufs = AllocateZeroPool (sizeof (struct UfsHost));
  if (!Ufs) return NULL;

  Cal = AllocateZeroPool (sizeof (struct UfsCalParam));
  if (!Cal) goto Error;

  Ufs->CalParam = Cal;

  CmdDesc = AllocateAlignedPages(EFI_SIZE_TO_PAGES (UFS_NUTRS * sizeof (struct UfsCmdDesc)), SIZE_4KB);
  if (!CmdDesc) goto Error;

  ZeroMem (CmdDesc, UFS_NUTRS * sizeof (struct UfsCmdDesc));
  Ufs->CmdDescAddr = CmdDesc;

  Utrd = AllocateAlignedPages(EFI_SIZE_TO_PAGES (UFS_NUTRS * sizeof (struct UfsUtrd)), SIZE_4KB);
  if (!Utrd) goto Error;

  ZeroMem (Utrd, UFS_NUTRS * sizeof (struct UfsUtrd));
  Ufs->UtrdAddr = Utrd;

  Utmrd = AllocateAlignedPages(EFI_SIZE_TO_PAGES (sizeof (struct UfsUtmrd)), SIZE_4KB);
  if (!Utmrd) goto Error;

  ZeroMem (Utmrd, sizeof (struct UfsUtmrd));
  Ufs->UtmrdAddr = Utmrd;

  return Ufs;

Error:
  FreePool (Ufs);
  if (Utmrd) FreeAlignedPages (Utmrd, EFI_SIZE_TO_PAGES (sizeof (struct UfsUtmrd)));
  if (Utrd) FreeAlignedPages (Utrd, EFI_SIZE_TO_PAGES (UFS_NUTRS * sizeof (struct UfsUtrd)));
  if (CmdDesc) FreeAlignedPages (CmdDesc, EFI_SIZE_TO_PAGES (UFS_NUTRS * sizeof (struct UfsCmdDesc)));
  if (Cal) FreePool (Cal);
  return NULL;
}

STATIC
EFI_STATUS
UfsInitHost (
  struct UfsHost *Ufs
)
{
  DEBUG((DEBUG_INFO, "UFS Host init\n"));

	Ufs->UfsCmdTimeout = UTP_CMD_TIMEOUT;
	Ufs->UicCmdTimeout = UIC_CMD_TIMEOUT;

  // TODO: Make SoC lib
  UfsBoardInit(Ufs);
  return UfsInitCal(Ufs);
}

EFI_STATUS
EFIAPI
InitUfsDriver (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  struct UfsHost *Ufs = UfsAllocHost();
  if (!Ufs) {
    DEBUG((EFI_D_ERROR, "Failed to allocate UFS host\n"));
    ASSERT(FALSE);
  }
  UfsInitHost(Ufs);
  UfsInitInterface(Ufs);
  return EFI_SUCCESS;
}