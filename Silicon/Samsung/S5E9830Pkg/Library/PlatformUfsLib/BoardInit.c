#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/IoLib.h>
#include <Library/UfsHostBridge.h>

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
#define UFS_CLKCMU_TIMEOUT         100

STATIC
VOID
UfsVsSet1usToCnt (struct UfsHost *Ufs)
{
  UINT32 nVal = MmioRead32((UINTN)(Ufs->IoAddr + UFSHCI_VS_UFSHCI_V2P1_CTRL));
  nVal |= IA_TICK_SEL;
  MmioWrite32((UINTN)(Ufs->IoAddr + UFSHCI_VS_UFSHCI_V2P1_CTRL), nVal);
  MmioWrite32((UINTN)(Ufs->IoAddr + UFSHCI_VS_1US_TO_CNT_VAL), (UFS_SCLK / 1000000) & CNT_VAL_1US_MASK);
}

STATIC
VOID
UfsSetUniProClk (struct UfsHost *Ufs)
{
  int timeout = 0;
  MmioWrite32(DIV_CLKCMU_UFS_EMBD_MUX, 3);
  do { timeout++; } while ((MmioRead32(DIV_CLKCMU_UFS_EMBD_MUX) & 0x10000) && timeout < UFS_CLKCMU_TIMEOUT);
  timeout = 0;
  MmioWrite32(MUX_CLKCMU_UFS_EMBD_CON, 1);
  do { timeout++; } while ((MmioRead32(MUX_CLKCMU_UFS_EMBD_CON) & 0x10000) && timeout < UFS_CLKCMU_TIMEOUT);
  UfsVsSet1usToCnt (Ufs);
}

EFI_STATUS
UfsBoardInit (struct UfsHost *Ufs)
{
  UINT32 reg;
  UINT32 rst_stat = MmioRead32(0x15860000 + 0x404);
  UINT32 dfd_en = MmioRead32(0x15860000 + 0x500);

  DEBUG ((EFI_D_INFO, "UFS: Board init\n"));

  /* UFS Addrs */
  Ufs->IoAddr = (VOID *)(UINTN)0x13100000;
  Ufs->VsAddr = (VOID *)(UINTN)(0x13100000 + 0x1100);
  Ufs->UniProAddr = (VOID *)(UINTN)0x13180000;
  Ufs->PhyPma = (VOID *)(UINTN)(0x13100000 + 0x4000);

  /* Power / PHY isolation addresses */
  Ufs->DevPwrAddr = (VOID *)(UINTN)(0x10730000UL + 0xC4);
  Ufs->DevPwrShift = 0;
  Ufs->PhyIsoAddr = (VOID *)(UINTN)(0x15860000UL + 0x724);

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
