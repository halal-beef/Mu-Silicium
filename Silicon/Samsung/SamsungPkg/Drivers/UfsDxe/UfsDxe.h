#include <Uefi/UefiSpec.h>

// vsc
#include <AArch64/ProcessorBind.h>

#define MAX_CDB_SIZE 16
#define ALIGNED_UPIU_SIZE 1024
#define SCSI_MAX_SG_SEGMENTS 128
#define UFS_SG_BLOCK_SIZE_BIT 12
#define UFS_SG_BLOCK_SIZE (1 << UFS_SG_BLOCK_SIZE_BIT)
#define UFS_NUTRS 2
#define UFS_BIT_LEN_OF_DWORD 32
#define UPIU_DATA_SIZE (ALIGNED_UPIU_SIZE - 20 - sizeof(struct UfsUpiuHeader))

#define DW_NUM_OF_TSF 20

#define	NOP_OUT_TIMEOUT	30000	/* 30ms */
#define	NOP_OUT_RETRY 10
#define	UTP_CMD_TIMEOUT	10000000/* 10sec */
#define	UIC_CMD_TIMEOUT 1500000	/* 500ms * 3 */
#define	QUERY_REQ_TIMEOUT 1500000	/* 1500ms */
#define	FORMAT_CMD_TIMEOUT 10 * 60 * 1000 * 1000 /*10min*/

#define UTP_TRANSFER_REQ_COMPL          BIT0
#define UIC_ERROR                       BIT2
#define UIC_POWER_MODE                  BIT4
#define UIC_LINK_LOST                   BIT7
#define UIC_LINK_STARTUP                BIT8
#define UTP_TASK_REQ_COMPL              BIT9
#define UIC_COMMAND_COMPL               BIT10
#define DEVICE_FATAL_ERROR              BIT11
#define CONTROLLER_FATAL_ERROR          BIT16
#define SYSTEM_BUS_FATAL_ERROR          BIT17
#define INT_FATAL_ERRORS                (DEVICE_FATAL_ERROR | CONTROLLER_FATAL_ERROR | SYSTEM_BUS_FATAL_ERROR | UIC_LINK_LOST)

#define BRD_SMDK  (1U << __BRD_SMDK)
#define BRD_ASB   (1U << __BRD_ASB)
#define BRD_HSIE  (1U << __BRD_HSIE)
#define BRD_ZEBU  (1U << __BRD_ZEBU)
#define BRD_UNIV  (1U << __BRD_UNIV)
#define BRD_MAX   (1U << __BRD_MAX)
#define BRD_ALL   ((1U << __BRD_MAX) - 1)


enum {
  __BRD_SMDK,
  __BRD_ASB,
  __BRD_HSIE,
  __BRD_ZEBU,
  __BRD_UNIV,
  __BRD_MAX,
};

typedef enum {
  UFS_CAL_NO_ERROR = 0,
  UFS_CAL_TIMEOUT,
  UFS_CAL_ERROR,
  UFS_CAL_INV_ARG,
} UfsCalError;

enum {
  TX_LANE_0 = 0,
  TX_LANE_1,
  TX_LANE_2,
  TX_LANE_3,
  RX_LANE_0,
  RX_LANE_1,
  RX_LANE_2,
  RX_LANE_3,
};

typedef struct
{
  UINT8 Cdb[16];
  UINT8 *Buf;
  UINT32 DataLen;
  UINT8 Status;
  UINT8 Sense_buf[64];
  UINT32 Lun;
} ScsiCommandMeta;

struct UfsUpiuHeader
{
  UINT8 Type;
  UINT8 Flags;
  UINT8 Lun;
  UINT8 Tag;
  UINT8 CmdType;
  UINT8 Function;
  UINT8 Response;
  UINT8 Status;
  UINT8 EhsLength;
  UINT8 DeviceInfo;
  UINT16 DataLength;
};

struct UfsUpiu
{
  struct UfsUpiuHeader Header;
  UINT8 Tsf[DW_NUM_OF_TSF];
  UINT8 Data[UPIU_DATA_SIZE];
};

struct UfsPrdt
{
  UINT32 BaseAddr;
  UINT32 UpperAddr;
  UINT32 Reserved;
  UINT32 Size;
};

struct UfsCmdDesc
{
  struct UfsUpiu CommandUpiu;
  struct UfsUpiu ResponseUpiu;
  struct UfsPrdt PrdTable[SCSI_MAX_SG_SEGMENTS];
};

struct UfsUtrd
{
  UINT32 dw[4];
  UINT32 cmd_desc_addr_l;
  UINT32 cmd_desc_addr_h;
  UINT16 rsp_upiu_len;
  UINT16 rsp_upiu_off;
  UINT16 prdt_len;
  UINT16 prdt_off;
};

struct UfsUtmrd
{
  UINT32 dw[8];
};

struct UfsFlagBit
{
  UINT8 Reserved0;
  UINT8 fDeviceInit;
  UINT8 fPermanentWPEn;
  UINT8 fPowerOnWPEn;
  UINT8 fBackgroundOpsEn;
  UINT8 Reserved5;
  UINT8 fPurgeEnable;
  UINT8 Reserved7[25];
};

union UfsFlags
{
  UINT8 Array[32];
  struct UfsFlagBit flag;
};

union UfsAttributes
{
  UINT32 Array[18];
};

struct UfsConfigDescHeader
{
  UINT8 bLength;
  UINT8 bDescriptorType;
  UINT8 bConfDescContinue;
  UINT8 bBootEnable;
  UINT8 bDescrAccessEn;
  UINT8 bInitPowerMode;
  UINT8 bHighPriorityLUN;
  UINT8 bSecureRemovalType;
  UINT8 bInitActiveICCLevel;
  UINT16 wPeriodicRTCUpdate;
  UINT8 reserved[5];
  UINT8 bTurboWriteBufferNoUserSpaceReductionEn;
  UINT8 bTurboWriteBufferType;
};

struct UfsUnitDescParam
{
  UINT8 bLUEnable;
  UINT8 bBootLunID;
  UINT8 bLUWriteProtect;
  UINT8 bMemoryType;
  UINT32 dNumAllocUnits;
  UINT8 bDataReliability;
  UINT8 bLogicalBlockSize;
  UINT8 bProvisioningType;
  UINT16 wContextCapabilities;
  UINT8 Reserved[3];
  UINT8 Reserved1[6];
  UINT32 dLUNumTurboWriteBufferAllocUnits;
};

struct UfsConfigDesc
{
  struct UfsConfigDescHeader Header;
  struct UfsUnitDescParam Unit[128];
};

struct UfsDeviceDesc
{
  UINT8 bLength;
  UINT8 bDescriptorType;
  UINT8 bDevice;
  UINT8 bDeviceClass;
  UINT8 bDeviceSubClass;
  UINT8 bProtocol;
  UINT8 bNumberLU;
  UINT8 iNumberWLU;
  UINT8 bBootEnable;
  UINT8 bDescrAccessEn;
  UINT8 bInitPowerMode;
  UINT8 bHighPriorityLUN;
  UINT8 bSecureRemovalType;
  UINT8 bSecurityLU;
  UINT8 reserved;
  UINT8 bInitActiveICCLevel;
  UINT16 wSpecVersion;
  UINT16 wManufactureData;
  UINT8 iManufacturerName;
  UINT8 iProductName;
  UINT8 iSerialNumber;
  UINT8 iOemID;
  UINT16 wManufacturerID;
  UINT8 bUD0BaseOffset;
  UINT8 bUDConfigPlength;
  UINT8 bDeviceRTTCap;
  UINT16 wPeriodicRTCUpdate;
  UINT8 Reserved1[48];
  UINT32 dExtendedUFSFeaturesSupport;
  UINT8 Reserved2[4];
};

struct UfsUnitDesc
{
  UINT8 bLength;
  UINT8 bDescriptorType;
  UINT8 bUnitIndex;
  UINT8 bLUEnable;
  UINT8 bBootLunID;
  UINT8 bLUWriteProtect;
  UINT8 bLUQueueDepth;
  UINT8 Reserved;
  UINT8 bMemoryType;
  UINT8 bDataReliability;
  UINT8 bLogicalBlockSize;
  UINT32 qLogicalBlockCount_h;
  UINT32 qLogicalBlockCount_l;
  UINT32 dEraseBlockSize;
  UINT8 bProvisioningType;
  UINT32 qPhyMemResourceCount_h;
  UINT32 qPhyMemResourceCount_l;
  UINT16 wContextCapabilities;
  UINT8 bLargeUnitSize_M1;
};

struct UfsGeometryDesc
{
  UINT8 bLength;
  UINT8 bDescriptorType;
  UINT8 bMediaTechnology;
  UINT8 Reserved03;
  UINT32 qTotalRawDeviceCapacity_h;
  UINT32 qTotalRawDeviceCapacity_l;
  UINT8 Reserved0c;
  UINT32 dSegmentSize;
  UINT8 bAllocationUnitSize;
  UINT8 bMinAddrBlockSize;
  UINT8 bOptimalReadBlockSize;
  UINT8 bOptimalWriteBlockSize;
  UINT8 bMaxInBufferSize;
  UINT8 bMaxOutBufferSize;
  UINT8 bRPMB_ReadWriteSize;
  UINT8 Reserved18;
  UINT8 bDataOrdering;
  UINT8 bMaxContexIDNumber;
  UINT8 bSysDataTagUnitSize;
  UINT8 bSysDataTagResSize;
  UINT8 bSupportedSecRTypes;
  UINT16 wSupportedMemoryTypes;
  UINT32 dSystemCodeMaxNAllocU;
  UINT16 wSystemCodeCapAdjFac;
  UINT32 dNonPersistMaxNAllocU;
  UINT16 wNonPersistCapAdjFac;
  UINT32 dEnhanced1MaxNAllocU;
  UINT16 wEnhanced1CapAdjFac;
  UINT32 dEnhanced2MaxNAllocU;
  UINT16 wEnhanced2CapAdjFac;
  UINT32 dEnhanced3MaxNAllocU;
  UINT16 wEnhanced3CapAdjFac;
  UINT32 dEnhanced4MaxNAllocU;
  UINT16 wEnhanced4CapAdjFac;
  UINT32 Reserved44;
  UINT8 Reserved[7];
  UINT32 dTurboWriteBufferMaxNAllocUnits;
  UINT8 Reserved1[2];
  UINT8 bSupportedTurboWriteBufferUserSpaceReductionTypes;
  UINT8 bSupportedTurboWriteBufferTypes;
  UINT8 Reserved2;
};

struct UfsUicCmd
{
  UINT32 uiccmdr;
  UINT32 Arg1;
  UINT32 Arg2;
  UINT32 Arg3;
};

struct UicPwrMode
{
  UINT8 Lane;
  UINT8 Gear;
  UINT8 Mode;
  UINT8 HsSeries;
};

enum
{
  HOST_EMBD = 0,
  HOST_CARD = 1
};
enum
{
  GEAR_1 = 1,
  GEAR_2,
  GEAR_3,
  GEAR_4
};

struct UfsCalParam
{
  void *Host;
  UINT8 AvailableLane;
  UINT8 ConnectedTxLane;
  UINT8 ConnectedRxLane;
  UINT8 ActiveTxLane;
  UINT8 ActiveRxLane;
  UINT32 MclkRate;
  UINT8 Tbl;
  UINT8 Board;
  UINT8 EvtVer;
  UINT8 MaxGear;
  struct UicPwrMode *Pmd;
};

struct UfsHost
{
  VOID *IoAddr;
  VOID *VsAddr;
  VOID *UniProAddr;
  VOID *UfsPaddr;
  VOID *PhyPma;
  VOID *DevPwrAddr;
  VOID *PhyIsoAddr;
  
  ScsiCommandMeta *ScsiCmd;
  UINT32 Lun;
  
  struct UfsCmdDesc *CmdDescAddr;
  struct UfsUtrd *UtrdAddr;
  struct UfsUtmrd *UtmrdAddr;
  struct UfsUicCmd *UicCmd;
  
  UINT32 Quirks;
  
  UINT32 UfsCmdTimeout;
  UINT32 UicCmdTimeout;
  UINT32 Timeout;
  
  union UfsFlags Flags;
  union UfsAttributes Attributes;
  
  struct UfsConfigDesc ConfigDesc;
  struct UfsDeviceDesc DeviceDesc;
  struct UfsGeometryDesc GeometryDesc;
  struct UfsUnitDesc UnitDesc[8];
  UINT16 DataSegLen;
  
  struct UfsCalParam *CalParam;
  UINT32 MclkRate;
  struct UicPwrMode PmdCxt;
  UINT32 DevPwrShift;
  UINT32 GearMode;
};

/* UFSHCI Registers */
enum
{
	REG_CONTROLLER_CAPABILITIES = 0x00,
	REG_UFS_VERSION = 0x08,
	REG_CONTROLLER_PID = 0x10,
	REG_CONTROLLER_MID = 0x14,
	REG_INTERRUPT_STATUS = 0x20,
	REG_INTERRUPT_ENABLE = 0x24,
	REG_CONTROLLER_STATUS = 0x30,
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
	REG_UTP_TRANSFER_REQ_LIST_CNR	= 0x64,
	REG_UTP_TASK_REQ_LIST_BASE_L = 0x70,
	REG_UTP_TASK_REQ_LIST_BASE_H = 0x74,
	REG_UTP_TASK_REQ_DOOR_BELL = 0x78,
	REG_UTP_TASK_REQ_LIST_CLEAR = 0x7C,
	REG_UTP_TASK_REQ_LIST_RUN_STOP = 0x80,
	REG_UIC_COMMAND = 0x90,
	REG_UIC_COMMAND_ARG_1 = 0x94,
	REG_UIC_COMMAND_ARG_2 = 0x98,
	REG_UIC_COMMAND_ARG_3 = 0x9C,
	REG_CRYPTO_CAPABILITY = 0x100,
	VS_TXPRDT_ENTRY_SIZE = 0x000,
	VS_RXPRDT_ENTRY_SIZE = 0x004,
	VS_IS = 0x038,
	VS_UTRL_NEXUS_TYPE = 0x040,
	VS_UMTRL_NEXUS_TYPE = 0x044,
	VS_SW_RST = 0x050,
	VS_DATA_REORDER = 0x060,
	VS_AXIDMA_RWDATA_BURST_LEN = 0x06C,
	VS_GPIO_OUT = 0x070,
	VS_CLKSTOP_CTRL = 0x0B0,
	VS_FORCE_HCS = 0x0B4,
	VS_UFS_ACG_DISABLE = 0x0FC,
	VS_MPHY_REFCLK_SEL = 0x108,
	UFSP_UPRSECURITY = 0x10,
	UFSP_UPSBEGIN0 = 0x2000,
	UFSP_UPSEND0 = 0x2004,
	UFSP_UPLUN0	= 0x2008,
	UFSP_UPSCTRL0 = 0x200c,
};

/* UIC Commands */
enum
{
	UIC_CMD_DME_GET = 0x01,
	UIC_CMD_DME_SET = 0x02,
	UIC_CMD_DME_PEER_GET = 0x03,
	UIC_CMD_DME_PEER_SET = 0x04,
	UIC_CMD_DME_POWERON = 0x10,
	UIC_CMD_DME_POWEROFF = 0x11,
	UIC_CMD_DME_ENABLE = 0x12,
	UIC_CMD_DME_RESET = 0x14,
	UIC_CMD_DME_END_PT_RST = 0x15,
	UIC_CMD_DME_LINK_STARTUP = 0x16,
	UIC_CMD_DME_HIBER_ENTER = 0x17,
	UIC_CMD_DME_HIBER_EXIT = 0x18,
	UIC_CMD_DME_TEST_MODE = 0x1A,
	UIC_CMD_WAIT = 0x80,
	UIC_CMD_WAIT_ISR = 0x90,
	PHY_PMA_COMN_SET = 0xf0,
	PHY_PMA_TRSV_SET = 0xf1,
	PHY_PMA_COMN_WAIT = 0xf2,
	PHY_PMA_TRSV_WAIT = 0xf3,
	UIC_CMD_REGISTER_SET = 0xff,
};

enum
{
  UFS_NO_ERROR = 0,
  UFS_TIMEOUT,
  UFS_ERROR,
  UFS_IN_PROGRESS,
};