#include <Library/DebugLib.h>
#include <Library/ArmSmcLib.h>

#include "ExynosSecurity.h"

UINT64
LoadImageByUsb (
  UINT64 BinaryList,
  UINT64 BinaryAddress,
  UINT64 BinarySize)
{
  // I am going to kill whoever designed the Smc lib like this, this is actually terrible.
  UINT64 Arg1 = BinaryList;
  UINT64 Arg2 = BinaryAddress;
  UINT64 Arg3 = BinarySize;

  switch(BinaryList) {
    case 1:
      DEBUG((EFI_D_ERROR, "Loading LDFW via USB...\n"));
      break;
    case 2:
      DEBUG((EFI_D_ERROR, "Loading SecurePayload via USB...\n"));
      break;
  }

  UINT64 Ret = (UINT64)ArmCallSmc3(SMC_CMD_LOAD_IMAGE_BY_USB, &Arg1, &Arg2, &Arg3);
  return Ret;
}

INTN
InitLDFW (VOID)
{
  INTN Ret = -1, LdfwCount = 0;
  UINT64 TotalSize = 0;

  if(LoadImageByUsb(1, 0x80000000, (6 * 1024 * 1024)) != 0)
  {
    DEBUG((EFI_D_ERROR, "Failed to load LDFW via USB.\n"));
    return -1;
  }

  struct FirmwareHeader *Ldfw = (struct FirmwareHeader *)0x80000000;
  if (Ldfw->Magic != LDFW_MAGIC)
  {
    DEBUG((EFI_D_ERROR, "Invalid LDFW Magic: 0x%x\n", Ldfw->Magic));
    return -1;
  }

  while(Ldfw->Magic == LDFW_MAGIC)
  {
    DEBUG((EFI_D_ERROR, "LDFW[%d] : %a, Size: 0x%x\n", LdfwCount, Ldfw->FirmwareName, Ldfw->Size));
    TotalSize += Ldfw->Size;

    Ldfw = (struct FirmwareHeader *)((UINT8 *)Ldfw + Ldfw->Size);
    LdfwCount++;
  }

  // SignerVer03, smh
  TotalSize += 0x100;

  UINT64 Arg1 = 0x80000000;
  UINT64 Arg2 = TotalSize + 0x210; // Sig, meh
  Ret = (INTN)ArmCallSmc3(SMC_CMD_LOAD_LDFW, &Arg1, &Arg2, 0);
  
  switch (Ret) {
    case 0:
      DEBUG((EFI_D_ERROR, "No LDFW has been loaded.\n"));
      Ret = -1;
      break;
    case -1:
      DEBUG((EFI_D_ERROR, "Don't Load LDFW, DUMP_GPR State. (0x%x)\n", Ret));
      break;
    case 0x1230:
      DEBUG((EFI_D_ERROR, "Don't Load LDFW, RAMDUMP State. (0x%x)\n", Ret));
      break;
    case 0x1231:
      DEBUG((EFI_D_ERROR, "Don't Load LDFW, KernelPanic State. (0x%x)\n", Ret));
      break;
    default:
      if (Ret & SB_ERROR_PREFIX)
      {
        DEBUG((EFI_D_ERROR, "LDFW SecureBoot Error: 0x%x\n", Ret));
        return -1;
      }

      UINT32 LoadsTried = Ret & 0xFFFF;
      UINT32 LoadsFailed = (Ret >> 16) & 0xFFFF;

      DEBUG((EFI_D_ERROR, "LDFW Load Result: Loads Tried: %d, Loads Failed: %d\n", LoadsTried, LoadsFailed));
      if(LoadsFailed == 0)
      {
        DEBUG((EFI_D_ERROR, "LDFW has been loaded successfully.\n"));
        Ret = 0;
      }
      else
      {
        DEBUG((EFI_D_ERROR, "LDFW has been loaded with errors.\n"));
        Ret = -1;
      }
      break;
  }

  return Ret;
}

INTN
InitSecurePayload (VOID)
{
  INTN Ret = -1;
  UINT64 Status = -1;

  if(LoadImageByUsb(2, 0x80000000, (1.5 * 1024 * 1024)) != 0) // part size is 1.5MiB
  {
    DEBUG((EFI_D_ERROR, "Failed to load SecurePayload via USB.\n"));
    return -1;
  }

  UINT64 Arg1 = 0x80000000;
  UINT64 Arg2 = (1 * 1024 * 1024) + 0x210 + 0x100; // SignerVer03 + Sig, smh

  Status = ArmCallSmc3(SMC_CMD_LOAD_SECURE_PAYLOAD2, &Arg1, &Arg2, 0);

  switch (Status) {
    case 0:
      DEBUG((EFI_D_ERROR, "SecurePayload has been loaded successfully.\n"));
      Ret = 0;
      break;
    case -1:
      DEBUG((EFI_D_ERROR, "Don't Load SecurePayload, DUMP_GPR State. (0x%llx)\n", Status));
      break;
    case 0x1230:
      DEBUG((EFI_D_ERROR, "Don't Load SecurePayload, RAMDUMP State. (0x%llx)\n", Status));
      break;
    case 0x1231:
      DEBUG((EFI_D_ERROR, "Don't Load SecurePayload, KernelPanic State. (0x%llx)\n", Status));
      break;
    default:
      DEBUG((EFI_D_ERROR, "SecurePayload SecureBoot Error: 0x%llx\n", Status));
      Ret = -1;
  }

  return Ret;
}

EFI_STATUS
EFIAPI
InitSecurityDriver (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  DEBUG((EFI_D_ERROR, "Hello, LDFW.\n"));
  if(InitLDFW() != 0)
  {
    DEBUG((EFI_D_ERROR, "Failed to initialize LDFW.\n"));
  }

  DEBUG((EFI_D_ERROR, "Hello, SecurePayload.\n"));
  if(InitSecurePayload() != 0)
  {
    DEBUG((EFI_D_ERROR, "Failed to initialize SecurePayload.\n"));
  }

  UINTN ret = ArmCallSmc3(0xc3000001, 0, 0, 0);

  DEBUG((EFI_D_ERROR, "Returned 0x%llx\n", ret));

//  while(1);

  return EFI_SUCCESS;
}
