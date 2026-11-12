#ifndef __BOOT_LINUX_H__
#define __BOOT_LINUX_H__

#include <Uefi.h>

#define MEMMAP_SLACK_DESCS   16
#define BOOT_USB_SETTLE_US   150000

#define ARM64_IMAGE_SIZE_OFFSET  16
#define ARM64_IMAGE_MAGIC_OFFSET 56
#define ARM64_IMAGE_MAGIC        0x644D5241  // "ARM\x64"
#define ARM64_IMAGE_HDR_SIZE     64

#define BOOT_HARDCODED_BOOTARGS  "console=ram loglevel=4 bootmode=2 sec_debug.level=0 sec_watchdog.sec_pet=5 rcupdate.rcu_expedited=1 androidboot.debug_level=0x4f4c androidboot.force_upload=0x0 androidboot.upload_offset=6880000 softdog.soft_margin=100 softdog.soft_panic=1 androidboot.dram_info=FF,04,10,2G sec_debug.chipidfail_cnt=0 sec_debug.lpitimeout_cnt=0 sec_debug.cache_err_cnt=0 sec_debug.codediff_cnt=0 sec_debug.pcb_offset=7343872 sec_debug.smd_offset=7344896 sec_debug.lpddr4_size=2 sec_debug.sjl=1 androidboot.muic_1k=0 androidboot.sn.param.offset=7342000 androidboot.prototype.param.offset=7345920 androidboot.recovery_offset=7347968 androidboot.fmm_lock=48 sec_debug.fmm_lock_offset=7340680 ess_setup=0x91200000 tima_log=0x200000@0xb8000000 sec_avc_log=0x40000@0x9150a000 sec_tsp_log=0x40000@0x9154c000 sec_summary_log=0x10000@0x9158e000 sec_debug.base=0x100000@0x915a0000 auto_summary_log=0x10000@0x916a2000 s3cfb.bootloaderfb=0xec000000 lcdtype=8550668 androidboot.carrierid.param.offset=7340608 androidboot.carrierid=BTU consoleblank=0 vmalloc=384m sec_debug.reset_reason=7 sec_reset.reset_reason=7 ehci_hcd.park=3 oops=panic pmic_info=91 ccic_info=1 fg_reset=0 androidboot.emmc_checksum=3 androidboot.sales.param.offset=7340572 sales_code=BTU androidboot.odin_download=1 androidboot.bootloader=T515XXU8CVL1 androidboot.selinux=enforcing androidboot.ucs_mode=0 kaslr_region=0x1000@0x80001000 androidboot.revision=4 androidboot.hardware=exynos7904 androidboot.warranty_bit=1 androidboot.wb.hs=0303 androidboot.wb.snapQB=CUSTOM sec_debug.bin=A androidboot.hmac_mismatch=0 androidboot.sec_atd.tty=/dev/ttySAC2 androidboot.serialno=R52M70F334F androidboot.ap_serial=0x478C4D0AB81C snd_soc_core.pmdown_time=1000 androidboot.cp_reserved_mem=off androidboot.dtbo_idx=1 androidboot.em.did=478c4d0ab81c androidboot.em.model=SM-T515 androidboot.em.status=0x0 root=PARTUUID=52444E41-494F-2044-7379-7374656D0000 androidboot.verifiedbootstate=orange androidboot.svb.ver=SVB1.0 bcm_setup=0xffffff80f8e00000 reserve-fimc=0xffffff80fa000000 firmware_class.path=/vendor/firmware ecd_setup=disable factory_mode=0 androidboot.ulcnt=1 androidboot.lassen.apfuse=8 epx_activate=true kpti=n slub_debug=- androidboot.cpefs_wp=1"

#define BOOT_DTB_SLACK           SIZE_16KB

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