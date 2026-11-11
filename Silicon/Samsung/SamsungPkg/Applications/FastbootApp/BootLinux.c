#include <Library/ArmLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/FdtLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#include "BootLinux.h"
#include "DtboMerge.h"
#include "FastbootApp.h"
#include "FastbootUsb.h"

#define EXIT_BS_TRIES        8
#define MEMMAP_SLACK_DESCS   16
#define BOOT_USB_SETTLE_US   150000

// arm64 Linux Image Header (Documentation/arch/arm64/booting.rst)
#define ARM64_IMAGE_SIZE_OFFSET  16
#define ARM64_IMAGE_MAGIC_OFFSET 56
#define ARM64_IMAGE_MAGIC        0x644D5241  // "ARM\x64"
#define ARM64_IMAGE_HDR_SIZE     64

STATIC_ASSERT (sizeof (ANDROID_BOOT_IMG_HDR_V1) == 1648, "Android boot header v1 must be 1648 Bytes");

typedef VOID (*BOOT_KERNEL_ENTRY)(UINT64 X0, UINT64 X1, UINT64 X2, UINT64 X3);

STATIC UINT32  mHwRev = 4;

VOID
BootLinuxSetHwRev (IN UINT32 Rev)
{
  mHwRev = Rev;
}

UINT32
BootLinuxGetHwRev (VOID)
{
  return mHwRev;
}

STATIC
UINT64
BlAlignUp (
  IN UINT64 Value,
  IN UINT64 Align)
{
  return (Value + Align - 1) & ~(Align - 1);
}

STATIC
VOID
BlFreeAll (IN BOOT_LINUX_CTX *Ctx)
{
  UINTN Index;

  for (Index = 0; Index < Ctx->AllocCount; Index++) {
    gBS->FreePages (Ctx->Allocs[Index].Base, Ctx->Allocs[Index].Pages);
  }

  ZeroMem (Ctx, sizeof (*Ctx));
}

STATIC
EFI_STATUS
BlRecord (
  IN OUT BOOT_LINUX_CTX     *Ctx,
  IN     EFI_PHYSICAL_ADDRESS Base,
  IN     UINTN                Pages)
{
  if (Ctx->AllocCount >= BOOT_MAX_ALLOCS) {
    gBS->FreePages (Base, Pages);
    return EFI_OUT_OF_RESOURCES;
  }

  Ctx->Allocs[Ctx->AllocCount].Base  = Base;
  Ctx->Allocs[Ctx->AllocCount].Pages = Pages;
  Ctx->AllocCount++;

  return EFI_SUCCESS;
}

// Allocates Loader Memory at the address used by the Samsung bootloader.
STATIC
EFI_STATUS
BlAllocate (
  IN OUT BOOT_LINUX_CTX *Ctx,
  IN     UINT64          Size,
  IN     UINT64          Alignment,
  IN     EFI_PHYSICAL_ADDRESS Address)
{
  UINTN      Pages;
  EFI_STATUS Status;

  if (Size == 0 || Address == 0 || Alignment == 0 ||
      (Alignment & (Alignment - 1)) != 0 || (Address & (Alignment - 1)) != 0 ||
      Size > MAX_UINT64 - Address) {
    return EFI_INVALID_PARAMETER;
  }

  Pages  = EFI_SIZE_TO_PAGES ((UINTN)Size);
  Status = gBS->AllocatePages (AllocateAddress, EfiLoaderData, Pages, &Address);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  DEBUG ((EFI_D_WARN, "BootLinux: Allocating 0x%lx..0x%lx (%u Pages)\n",
          Address, Address + Size, Pages));

  return BlRecord (Ctx, Address, Pages);
}

STATIC
EFI_STATUS
BlAllocateAny (
  IN OUT BOOT_LINUX_CTX *Ctx,
  IN     UINT64          Size,
  IN     UINT64          Alignment,
  OUT    EFI_PHYSICAL_ADDRESS *Address)
{
  EFI_PHYSICAL_ADDRESS RawBase;
  UINT64               AllocationSize;
  UINTN                Pages;
  EFI_STATUS           Status;

  if (Size == 0 || Alignment == 0 || (Alignment & (Alignment - 1)) != 0 || Size > MAX_UINT64 - Alignment + 1) {
    return EFI_INVALID_PARAMETER;
  }

  AllocationSize = Size + Alignment - 1;
  Pages          = EFI_SIZE_TO_PAGES ((UINTN)AllocationSize);
  RawBase        = 0;

  Status = gBS->AllocatePages (AllocateAnyPages, EfiLoaderData, Pages, &RawBase);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  *Address = ALIGN_VALUE (RawBase, Alignment);
  DEBUG ((EFI_D_WARN, "BootLinux: Allocating 0x%lx..0x%lx (%u Pages)\n",
          *Address, *Address + Size, Pages));

  Status = BlRecord (Ctx, RawBase, Pages);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return EFI_SUCCESS;
}

STATIC
INT32
BlSetU64 (
  IN VOID        *Fdt,
  IN INT32        Node,
  IN CONST CHAR8 *Name,
  IN UINT64       Value)
{
  UINT64 Be = SwapBytes64 (Value);

  return FdtSetProp (Fdt, Node, Name, &Be, sizeof (Be));
}

// Sets /chosen/bootargs and, when a Ramdisk is Loaded, /chosen/linux,initrd-start and -end.
STATIC
INT32
BlFillChosen (
  IN VOID   *Fdt,
  IN UINT64  RamdiskStart,
  IN UINT64  RamdiskSize)
{
  INT32 Node;
  INT32 Status;

  Node = FdtPathOffset (Fdt, "/chosen");
  if (Node < 0) {
    Node = FdtAddSubnode (Fdt, 0, "chosen");
    if (Node < 0) {
      return Node;
    }
  }

  Status = FdtSetProp (Fdt, Node, "bootargs", BOOT_HARDCODED_BOOTARGS, sizeof (BOOT_HARDCODED_BOOTARGS));
  if (Status < 0) {
    return Status;
  }

  if (RamdiskSize == 0) {
    return 0;
  }

  // 64 Bit Cells, the End is the first Byte after the Ramdisk.
  Status = BlSetU64 (Fdt, Node, "linux,initrd-start", RamdiskStart);
  if (Status < 0) {
    return Status;
  }

  return BlSetU64 (Fdt, Node, "linux,initrd-end", RamdiskStart + RamdiskSize);
}

// Builds the Merged DTB for the Current HW Revision at Samsung's fixed address.
STATIC
EFI_STATUS
BlBuildDtb (
  IN OUT BOOT_LINUX_CTX *Ctx,
  IN     UINT64          RamdiskStart,
  IN     UINT64           RamdiskSize,
  OUT    CONST CHAR8    **Reason)
{
  UINT8                *Merged     = NULL;
  UINTN                 MergedSize = 0;
  UINTN                 Room;
  EFI_PHYSICAL_ADDRESS  Dtb        = 0;
  INT32                 FdtStatus;
  EFI_STATUS            Status;

  Status = DtboMergeForRevision (mHwRev, &Merged, &MergedSize);
  if (EFI_ERROR (Status)) {
    *Reason = "dtb merge failed";
    return Status;
  }

  Room  = ALIGN_VALUE (MergedSize + BOOT_DTB_SLACK, SIZE_4KB);

  Status = BlAllocate (Ctx, Room, SIZE_4KB, BOOT_DTB_ADDRESS);

  if (EFI_ERROR (Status)) {
    gFb.Usb->FreeTransferBuffer (gFb.Usb, Merged);
    *Reason = "no memory for dtb";
    return Status;
  }

  Dtb = BOOT_DTB_ADDRESS;

  ZeroMem ((VOID *)(UINTN)Dtb, Room);
  CopyMem ((VOID *)(UINTN)Dtb, Merged, MergedSize);
  gFb.Usb->FreeTransferBuffer (gFb.Usb, Merged);

  // Grow the Packed Blob into the Slack, Edit it, Pack it again.
  FdtStatus = FdtOpenInto ((VOID *)(UINTN)Dtb, (VOID *)(UINTN)Dtb, (INT32)Room);
  if (FdtStatus < 0) {
    DEBUG ((EFI_D_ERROR, "BootLinux: FdtOpenInto: %a\n", FdtStrerror (FdtStatus)));
    *Reason = "dtb open failed";
    return EFI_DEVICE_ERROR;
  }

  FdtStatus = BlFillChosen ((VOID *)(UINTN)Dtb, RamdiskStart, RamdiskSize);
  if (FdtStatus < 0) {
    DEBUG ((EFI_D_ERROR, "BootLinux: /chosen: %a\n", FdtStrerror (FdtStatus)));
    *Reason = "dtb /chosen failed";
    return EFI_DEVICE_ERROR;
  }

  FdtStatus = FdtPack ((VOID *)(UINTN)Dtb);
  if (FdtStatus < 0) {
    *Reason = "dtb pack failed";
    return EFI_DEVICE_ERROR;
  }

  Ctx->Dtb = Dtb;

  return EFI_SUCCESS;
}

EFI_STATUS
BootLinuxPrepare (
  IN  CONST VOID      *Image,
  IN  UINTN            ImageSize,
  OUT BOOT_LINUX_CTX  *Ctx,
  OUT CONST CHAR8    **Reason)
{
  CONST ANDROID_BOOT_IMG_HDR_V1 *Hdr   = (CONST ANDROID_BOOT_IMG_HDR_V1 *)Image;
  CONST UINT8                   *Bytes = (CONST UINT8 *)Image;
  UINT64                         PageSize;
  UINT64                         KernelSize;
  UINT64                         RamdiskSize;
  UINT64                         SecondSize;
  UINT64                         KernelOff;
  UINT64                         RamdiskOff;
  UINT64                         SecondOff;
  UINT64                         Footprint;
  //EFI_PHYSICAL_ADDRESS           KernelBase;
  EFI_PHYSICAL_ADDRESS           RamdiskBase;
  EFI_PHYSICAL_ADDRESS           SecondBase;
  EFI_STATUS                     Status;

  ZeroMem (Ctx, sizeof (*Ctx));
  *Reason = "";

  //
  // Header
  //
  if (ImageSize < ANDROID_BOOT_HDR_V0_SIZE || CompareMem (Hdr->Magic, ANDROID_BOOT_MAGIC, ANDROID_BOOT_MAGIC_SIZE) != 0) {
    *Reason = "not an android boot image";
    return EFI_COMPROMISED_DATA;
  }

  if (Hdr->HeaderVersion > 1) {
    *Reason = "unsupported boot header version";
    return EFI_UNSUPPORTED;
  }

  if (Hdr->HeaderVersion == 1 && ImageSize < ANDROID_BOOT_HDR_V1_SIZE) {
    *Reason = "truncated v1 header";
    return EFI_COMPROMISED_DATA;
  }

  PageSize    = Hdr->PageSize;
  KernelSize  = Hdr->KernelSize;
  RamdiskSize = Hdr->RamdiskSize;
  SecondSize  = Hdr->SecondSize;

  if (PageSize < ANDROID_BOOT_HDR_V1_SIZE || (PageSize & (PageSize - 1)) != 0) {
    *Reason = "bad page size";
    return EFI_COMPROMISED_DATA;
  }

  //
  // Layout, every Section Starts on a Page Boundary
  //
  KernelOff  = PageSize;
  RamdiskOff = KernelOff + BlAlignUp (KernelSize, PageSize);
  SecondOff  = RamdiskOff + BlAlignUp (RamdiskSize, PageSize);

  if (KernelSize == 0 || KernelOff + KernelSize > ImageSize) {
    *Reason = "kernel out of bounds";
    return EFI_COMPROMISED_DATA;
  }

  if (RamdiskSize != 0 && RamdiskOff + RamdiskSize > ImageSize) {
    *Reason = "ramdisk out of bounds";
    return EFI_COMPROMISED_DATA;
  }

  if (SecondSize != 0 && SecondOff + SecondSize > ImageSize) {
    *Reason = "second stage out of bounds";
    return EFI_COMPROMISED_DATA;
  }

  //
  // Kernel. Not Required to be Linux, but a arm64 Linux Image Tells how much Memory it Needs
  // past its File Size (BSS), keep that Free so nothing else gets Placed There.
  //
  Footprint = KernelSize;

  if (KernelSize >= ARM64_IMAGE_HDR_SIZE &&
      ReadUnaligned32 ((CONST UINT32 *)(Bytes + KernelOff + ARM64_IMAGE_MAGIC_OFFSET)) == ARM64_IMAGE_MAGIC)
  {
    UINT64 ImageSizeHint = ReadUnaligned64 ((CONST UINT64 *)(Bytes + KernelOff + ARM64_IMAGE_SIZE_OFFSET));

    if (ImageSizeHint > Footprint && ImageSizeHint <= BOOT_MAX_KERNEL_FOOTPRINT) {
      Footprint = ImageSizeHint;
    }
  }

  Status = BlAllocate (Ctx, Footprint, EFI_PAGE_SIZE, BOOT_KERNEL_ADDRESS);
  if (EFI_ERROR (Status)) {
    *Reason = "kernel address unavailable";
    goto Fail;
  }

  CopyMem ((VOID *)(UINTN)BOOT_KERNEL_ADDRESS, Bytes + KernelOff, (UINTN)KernelSize);
  Ctx->Entry = BOOT_KERNEL_ADDRESS;

  FB_INFO ("kernel: 0x%lx (%u bytes)", Ctx->Entry, (UINT32)KernelSize);

  //
  // Ramdisk
  //
  if (RamdiskSize != 0) {
    Status = BlAllocate (Ctx, RamdiskSize, EFI_PAGE_SIZE, BOOT_RAMDISK_ADDRESS);
    if (EFI_ERROR (Status)) {
      *Reason = "ramdisk address unavailable";
      goto Fail;
    }

    RamdiskBase = BOOT_RAMDISK_ADDRESS;
    CopyMem ((VOID *)(UINTN)RamdiskBase, Bytes + RamdiskOff, (UINTN)RamdiskSize);

    FB_INFO ("ramdisk: 0x%lx (%u bytes)", RamdiskBase, (UINT32)RamdiskSize);
  } else {
    RamdiskBase = 0;
  }

  //
  // Second Stage, Loaded as is for Kernels that want it
  //
  if (SecondSize != 0) {
    Status = BlAllocateAny (Ctx, SecondSize, EFI_PAGE_SIZE, &SecondBase);
    if (EFI_ERROR (Status)) {
      *Reason = "no memory for second stage";
      goto Fail;
    }

    CopyMem ((VOID *)(UINTN)SecondBase, Bytes + SecondOff, (UINTN)SecondSize);

    FB_INFO ("second: 0x%lx (%u bytes)", SecondBase, (UINT32)SecondSize);
  }

  //
  // DTB
  //
  Status = BlBuildDtb (Ctx, RamdiskBase, RamdiskSize, Reason);
  if (EFI_ERROR (Status)) {
    goto Fail;
  }

  FB_INFO ("dtb: 0x%lx (hw rev %u)", Ctx->Dtb, mHwRev);

  return EFI_SUCCESS;

Fail:
  BlFreeAll (Ctx);
  return Status;
}

VOID
BootLinuxExecute (IN BOOT_LINUX_CTX *Ctx)
{
  EFI_MEMORY_DESCRIPTOR *Map;
  UINTN                  MapSize;
  UINTN                  MapKey;
  UINTN                  DescSize;
  UINT32                 DescVersion;
  UINTN                  Try;
  UINTN                  Index;
  EFI_STATUS             Status;
  BOOT_KERNEL_ENTRY      Entry = (BOOT_KERNEL_ENTRY)(UINTN)Ctx->Entry;
  UINT64                 Dtb   = Ctx->Dtb;

  Print (L"  booting kernel at 0x%lx, dtb at 0x%lx\n", Ctx->Entry, Ctx->Dtb);

  // Stop USB before handing the controller and its state to Linux.
  FbUsbCleanup ();
  gBS->Stall (BOOT_USB_SETTLE_US);

  // Size the Memory Map once with Slack, nothing may Allocate between GetMemoryMap and ExitBootServices.
  MapSize = 0;
  Status  = gBS->GetMemoryMap (&MapSize, NULL, &MapKey, &DescSize, &DescVersion);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    goto Reset;
  }

  MapSize += MEMMAP_SLACK_DESCS * DescSize;
  Map      = AllocatePool (MapSize);
  if (Map == NULL) {
    goto Reset;
  }

  Status = EFI_ABORTED;

  for (Try = 0; Try < EXIT_BS_TRIES; Try++) {
    UINTN Size = MapSize;

    Status = gBS->GetMemoryMap (&Size, Map, &MapKey, &DescSize, &DescVersion);
    if (EFI_ERROR (Status)) {
      continue;
    }

    Status = gBS->ExitBootServices (gImageHandle, MapKey);
    if (!EFI_ERROR (Status)) {
      break;
    }
  }

  if (EFI_ERROR (Status)) {
    goto Reset;
  }

  //
  // No Firmware Services from here on. Everything the Kernel and DTB Occupy has to Reach
  // Memory, then MMU and Caches go off and the Kernel gets Control.
  //
  ArmDisableInterrupts ();
  for (Index = 0; Index < Ctx->AllocCount; Index++) {
    WriteBackInvalidateDataCacheRange (
      (VOID *)(UINTN)Ctx->Allocs[Index].Base,
      EFI_PAGES_TO_SIZE (Ctx->Allocs[Index].Pages)
      );
  }

  *(UINT32 *)(0x80001004) = 0;
  *(UINT32 *)(0x14860000 + 0x70) = 0x3070; // decon disable

  ArmInvalidateInstructionCache ();
  ArmDisableCachesAndMmu ();

  Entry (Dtb, 0, 0, 0);

  CpuDeadLoop ();
  return;

Reset:
  // The USB Side is Already Down, there is no Way back into Fastboot.
  Print (L"  could not leave boot services (%r), resetting\n", Status);
  gBS->Stall (3000000);
  gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
  CpuDeadLoop ();
}