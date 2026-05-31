#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/IoLib.h>
#include <Library/UfsHostBridge.h>

#include <Protocol/EfiGpio.h>

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

STATIC EFI_EXYNOS_GPIO_PROTOCOL *mGpioProtocol;

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
  UINT32 Register;
  UINT32 rst_stat = MmioRead32(0x15860000 + 0x404);
  UINT32 dfd_en = MmioRead32(0x15860000 + 0x500);
  EFI_STATUS Status;

  Status = gBS->LocateProtocol (&gEfiExynosGpioProtocolGuid, NULL, (VOID *)&mGpioProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate GPIO Protocol! Status = %r\n", Status));
    return Status;
  }

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
  Status = mGpioProtocol->SetPull(2, GPIO_BANK_ID_F, 0, GPIO_PULL_NONE);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to set GPIO pull for RST_N! Status = %r\n", Status));
    return Status;
  }

  Status = mGpioProtocol->SetPull(2, GPIO_BANK_ID_F, 1, GPIO_PULL_NONE);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to set GPIO pull for REFCLK! Status = %r\n", Status));
    return Status;
  }

  Status = mGpioProtocol->ConfigurePin(2, GPIO_BANK_ID_F, 0, 2);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to configure GPIO pin for RST_N! Status = %r\n", Status));
    return Status;
  }

  Status = mGpioProtocol->ConfigurePin(2, GPIO_BANK_ID_F, 1, 2);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to configure GPIO pin for REFCLK! Status = %r\n", Status));
    return Status;
  }

  /* XBOOTLDO GPG1[0] */
  Status = mGpioProtocol->ConfigurePin(1, GPIO_BANK_ID_G, 0, GPIO_OUTPUT);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to configure GPG1-0! Status = %r\n", Status));
    return Status;
  }

  /* IO coherency in SYSREG (skip if warm/wdt reset with DFD) */
  if (!((rst_stat & (WARM_RESET | LITTLE_WDT_RESET)) && (dfd_en & EXYNOS9830_EDPCSR_DUMP_EN)))
  {
    Register = MmioRead32(0x13020700);
    Register |= (BIT22 | BIT23);
    MmioWrite32(0x13020700, Register);
  }

  return EFI_SUCCESS;
}
