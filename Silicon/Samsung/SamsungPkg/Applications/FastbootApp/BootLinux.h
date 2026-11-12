#ifndef __BOOT_LINUX_H__
#define __BOOT_LINUX_H__

#include <Uefi.h>

#define MEMMAP_SLACK_DESCS   16
#define BOOT_USB_SETTLE_US   150000

#define ARM64_IMAGE_SIZE_OFFSET  16
#define ARM64_IMAGE_MAGIC_OFFSET 56
#define ARM64_IMAGE_MAGIC        0x644D5241  // "ARM\x64"
#define ARM64_IMAGE_HDR_SIZE     64

#define BOOT_DTB_SLACK           SIZE_16KB
#define BOOT_CMDLINE_MAX_SIZE    4096

#define BOOT_KERNEL_ADDRESS      0x80080000
#define BOOT_RAMDISK_ADDRESS     0x89000000
#define BOOT_DTB_ADDRESS         0x8A000000

#define BOOT_MAX_KERNEL_FOOTPRINT  SIZE_1GB

#define BOOT_MAX_ALLOCS          4

#define ANDROID_BOOT_MAGIC       "ANDROID!"
#define ANDROID_BOOT_MAGIC_SIZE  8

#define ANDROID_BOOT_HDR_V0_SIZE  OFFSET_OF (ANDROID_BOOT_IMG_HDR_V1, RecoveryDtboSize)
#define ANDROID_BOOT_HDR_V1_SIZE  sizeof (ANDROID_BOOT_IMG_HDR_V1)

#pragma pack(1)
typedef struct {
  UINT8   Magic[ANDROID_BOOT_MAGIC_SIZE];
  UINT32  KernelSize;
  UINT32  KernelAddr;
  UINT32  RamdiskSize;
  UINT32  RamdiskAddr;
  UINT32  SecondSize;
  UINT32  SecondAddr;
  UINT32  TagsAddr;
  UINT32  PageSize;
  UINT32  HeaderVersion;
  UINT32  OsVersion;
  CHAR8   Name[16];
  CHAR8   Cmdline[512];
  UINT32  Id[8];
  CHAR8   ExtraCmdline[1024];

  // Version 1
  UINT32  RecoveryDtboSize;
  UINT64  RecoveryDtboOffset;
  UINT32  HeaderSize;
} ANDROID_BOOT_IMG_HDR_V1;
#pragma pack()

typedef struct {
  EFI_PHYSICAL_ADDRESS  Base;
  UINTN                 Pages;
} BOOT_ALLOC;

typedef struct {
  EFI_PHYSICAL_ADDRESS  Entry;                  
  EFI_PHYSICAL_ADDRESS  Dtb;                    
  BOOT_ALLOC            Allocs[BOOT_MAX_ALLOCS];
  UINTN                 AllocCount;
} BOOT_LINUX_CTX;

extern CHAR8  gBootArgs[BOOT_CMDLINE_MAX_SIZE];
extern UINTN  gBootArgsSize;

typedef VOID (*BOOT_KERNEL_ENTRY)(UINT64 X0, UINT64 X1, UINT64 X2, UINT64 X3);

VOID
BootLinuxSetHwRev (IN UINT32 Rev);

UINT32
BootLinuxGetHwRev (VOID);

EFI_STATUS
BootLinuxPrepare (
  IN  CONST VOID      *Image,
  IN  UINTN            ImageSize,
  OUT BOOT_LINUX_CTX  *Ctx,
  OUT CONST CHAR8    **Reason
  );

VOID
BootLinuxExecute (IN BOOT_LINUX_CTX *Ctx);

#endif /* __BOOT_LINUX_H__ */