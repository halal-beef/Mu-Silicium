#ifndef __EXYNOS_SECURITY_H__
#define __EXYNOS_SECURITY_H__

#define LDFW_MAGIC 						0x10ADAB1E
#define SB_ERROR_PREFIX					(0xFDAA0000)

#define SMC_CMD_LOAD_LDFW				(-0x500)
#define SMC_CMD_LOAD_SECURE_PAYLOAD2	(-0x511)
#define SMC_CMD_LOAD_IMAGE_BY_USB		(-0x512)


struct FirmwareHeader {
	UINT32 Magic;
	UINT32 Size;
	UINT32 InitEntry;
	UINT32 EntryPoint;
	UINT32 SuspendEntry;
	UINT32 ResumeEntry;
	UINT32 StartSmcId;
	UINT32 Version;
	UINT32 SetRuntimeEntry;
	UINT32 Reversed[3];
	UINT8  FirmwareName[16];
};

#endif /* __EXYNOS_SECURITY_H__ */