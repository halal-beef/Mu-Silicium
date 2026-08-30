#ifndef _USB_MSD_DXE_H_
#define _USB_MSD_DXE_H_

#include <Uefi.h>

#include <Protocol/BlockIo.h>
#include <Protocol/UsbFunctionIo.h>
#include <Protocol/ExynosUsbMsd.h>

//
// Number of LUNs the Device exposes to the Host.
//
#define USBMSD_MAX_LUN            4

//
// Logical Endpoint Number used for the Bulk Pipes. EP0 is the Control Pipe.
//
#define USBMSD_BULK_EP            1

//
// The largest Data Payload transferred in a single Bulk Transfer. The Host is
// free to request more than this in one Command, in which case the Transfer is
// split across multiple Bulk Transfers.
//
#define USBMSD_MAX_TRANSFER_SIZE  SIZE_128KB

//
// Bulk Endpoints run at 512 Bytes per Packet on High Speed and 1024 on Super
// Speed. Receive Buffers must be a Multiple of the Max Packet Size, because the
// Controller rounds Rx Transfer Lengths up.
//
#define USBMSD_MAX_PACKET_SIZE    1024

//
// bmRequestType Fields (USB 2.0, Table 9-2). MdePkg names the Values but not
// the Masks used to extract them.
//
#define USBMSD_REQ_TYPE_MASK      0x60
#define USBMSD_REQ_TARGET_MASK    0x1F

//
// USB Mass Storage Class Requests (USB MSC Bulk-Only Transport 1.0, Section 3).
//
#define USBMSD_REQ_BULK_ONLY_RESET 0xFF
#define USBMSD_REQ_GET_MAX_LUN     0xFE

//
// Command Block / Status Wrapper (BOT 1.0, Sections 5.1 and 5.2).
//
#define USBMSD_CBW_SIGNATURE      0x43425355
#define USBMSD_CSW_SIGNATURE      0x53425355
#define USBMSD_CBW_LENGTH         31
#define USBMSD_CSW_LENGTH         13

#define USBMSD_CBW_FLAG_DATA_IN   0x80

#define USBMSD_CSW_STATUS_GOOD    0x00
#define USBMSD_CSW_STATUS_FAILED  0x01
#define USBMSD_CSW_STATUS_PHASE   0x02

#pragma pack(1)

typedef struct {
  UINT32 Signature;
  UINT32 Tag;
  UINT32 DataTransferLength;
  UINT8  Flags;
  UINT8  Lun;
  UINT8  CbLength;
  UINT8  Cb[16];
} USBMSD_CBW;

typedef struct {
  UINT32 Signature;
  UINT32 Tag;
  UINT32 DataResidue;
  UINT8  Status;
} USBMSD_CSW;

#pragma pack()

//
// Transport State Machine. The Bulk-Only Transport is a strict Command ->
// Data -> Status Loop, so the State only tracks which Phase is outstanding.
//
typedef enum {
  UsbMsdStateDetached,      ///< Not Enumerated
  UsbMsdStateWaitCbw,       ///< Waiting for a Command Block Wrapper
  UsbMsdStateResponseIn,    ///< Sending a fixed size Response to the Host
  UsbMsdStateDataIn,        ///< Sending Medium Data to the Host
  UsbMsdStateDataOut,       ///< Receiving Medium Data from the Host
  UsbMsdStateWaitCsw,       ///< Command Status Wrapper Transfer queued
  UsbMsdStateStalled        ///< Endpoint halted, waiting for Recovery
} USBMSD_STATE;

//
// Per-LUN State. SCSI requires Sense Data to persist until the next Command.
//
typedef struct {
  EFI_BLOCK_IO_PROTOCOL *BlkIo;
  BOOLEAN                Present;
  UINT8                  SenseKey;
  UINT8                  AdditionalSenseCode;
  UINT8                  AdditionalSenseCodeQualifier;
} USBMSD_LUN;

#define USBMSD_DEV_SIGNATURE SIGNATURE_32 ('E', 'M', 'S', 'D')

typedef struct {
  UINT32                   Signature;
  EXYNOS_USB_MSD_PROTOCOL  UsbMsd;
  EFI_USBFN_IO_PROTOCOL   *UsbfnIo;
  EFI_HANDLE               Handle;

  BOOLEAN                  Started;
  BOOLEAN                  Configured;
  USBMSD_STATE             State;
  EFI_USB_BUS_SPEED        Speed;

  USBMSD_LUN               Luns[USBMSD_MAX_LUN];

  //
  // DMA capable Transfer Buffers, allocated once at Start.
  //
  VOID                    *CommandBuffer;   ///< Receives the CBW
  VOID                    *StatusBuffer;    ///< Sends the CSW
  VOID                    *DataBuffer;      ///< Bulk Data Payload
  VOID                    *Ep0Buffer;       ///< Control Transfer Responses

  //
  // Current Command being processed.
  //
  USBMSD_CBW               Cbw;
  UINT8                    CswStatus;
  UINT32                   DataResidue;

  //
  // Progress through a multi Transfer Read or Write.
  //
  UINT64                   TransferLba;      ///< Next LBA to Read or Write
  UINT32                   TransferRemaining;///< Payload Bytes still outstanding
  UINT32                   TransferChunk;    ///< Bytes in the Transfer in flight
  UINT8                    TransferLun;
} USBMSD_DEV;

#define USBMSD_DEV_FROM_PROTOCOL(a) \
  BASE_CR (a, USBMSD_DEV, UsbMsd)

//
// UsbMsdDesc.c
//
extern EFI_USB_DEVICE_INFO UsbMsdDeviceInfo;

/**
  Builds the Response for a Standard GET_DESCRIPTOR Request.

  @param[in]  Dev          - The Device Instance.
  @param[in]  Value        - wValue from the Setup Packet (Type and Index).
  @param[out] Buffer       - Receives a Pointer to the Descriptor Data.
  @param[out] Length       - Receives the Descriptor Length.

  @return EFI_SUCCESS      - The Descriptor was built.
  @return EFI_NOT_FOUND    - The requested Descriptor is not supported.
**/
EFI_STATUS
UsbMsdGetDescriptor (
  IN  USBMSD_DEV *Dev,
  IN  UINT16      Value,
  OUT VOID      **Buffer,
  OUT UINTN      *Length
  );

//
// UsbMsdScsi.c
//
/**
  Executes the SCSI Command in the current CBW.

  On return the Device is either sending Data, receiving Data, or ready to send
  the Status Wrapper.

  @param[in] Dev           - The Device Instance.

  @return EFI_SUCCESS      - The Command was dispatched.
**/
EFI_STATUS
UsbMsdHandleScsiCommand (IN USBMSD_DEV *Dev);

/**
  Continues a Read or Write once the previous Data Transfer completed.

  @param[in] Dev           - The Device Instance.
  @param[in] BytesHandled  - Bytes moved by the completed Transfer.

  @return EFI_SUCCESS      - The next Transfer was queued, or the Command is done.
**/
EFI_STATUS
UsbMsdContinueDataTransfer (
  IN USBMSD_DEV *Dev,
  IN UINT32      BytesHandled
  );

/**
  Records SCSI Sense Data for a LUN, to be returned by the next REQUEST SENSE.

  @param[in] Dev           - The Device Instance.
  @param[in] Lun           - The LUN the Error applies to.
  @param[in] SenseKey      - SCSI Sense Key.
  @param[in] Asc           - Additional Sense Code.
  @param[in] Ascq          - Additional Sense Code Qualifier.
**/
VOID
UsbMsdSetSense (
  IN USBMSD_DEV *Dev,
  IN UINT8       Lun,
  IN UINT8       SenseKey,
  IN UINT8       Asc,
  IN UINT8       Ascq
  );

//
// UsbMsdBot.c
//
/**
  Queues a Receive for the next Command Block Wrapper.

  @param[in] Dev           - The Device Instance.

  @return EFI_SUCCESS      - The Receive was queued.
**/
EFI_STATUS
UsbMsdQueueCbw (IN USBMSD_DEV *Dev);

/**
  Queues the Command Status Wrapper for the current Command.

  @param[in] Dev           - The Device Instance.
  @param[in] Status        - CSW Status Byte.
  @param[in] Residue       - Difference between expected and actual Data Length.

  @return EFI_SUCCESS      - The Status Wrapper was queued.
**/
EFI_STATUS
UsbMsdQueueCsw (
  IN USBMSD_DEV *Dev,
  IN UINT8       Status,
  IN UINT32      Residue
  );

/**
  Queues a Bulk Medium Data Transfer for the current Command.

  @param[in] Dev           - The Device Instance.
  @param[in] Length        - Number of Bytes to Transfer.
  @param[in] HostIn        - TRUE to Send to the Host, FALSE to Receive.

  @return EFI_SUCCESS      - The Transfer was queued.
**/
EFI_STATUS
UsbMsdQueueData (
  IN USBMSD_DEV *Dev,
  IN UINT32      Length,
  IN BOOLEAN     HostIn
  );

/**
  Queues a fixed size Response staged in the Data Buffer.

  Unlike a Medium Transfer this always completes in one Transfer and never
  touches BlockIo, so it is retired straight into the Status Phase.

  @param[in] Dev           - The Device Instance.
  @param[in] Length        - Number of Bytes to Send.

  @return EFI_SUCCESS      - The Response was queued.
**/
EFI_STATUS
UsbMsdQueueResponse (
  IN USBMSD_DEV *Dev,
  IN UINT32      Length
  );

/**
  Handles a completed Bulk Transfer.

  @param[in] Dev           - The Device Instance.
  @param[in] Result        - The Transfer Result reported by the Controller.

  @return EFI_SUCCESS      - The Event was handled.
**/
EFI_STATUS
UsbMsdHandleTransferComplete (
  IN USBMSD_DEV                *Dev,
  IN EFI_USBFN_TRANSFER_RESULT *Result
  );

/**
  Handles a Setup Packet forwarded by the Controller.

  @param[in] Dev           - The Device Instance.
  @param[in] Request       - The Setup Packet.

  @return EFI_SUCCESS      - The Request was handled.
**/
EFI_STATUS
UsbMsdHandleSetupPacket (
  IN USBMSD_DEV            *Dev,
  IN EFI_USB_DEVICE_REQUEST *Request
  );

/**
  Halts the Bulk Endpoints and parks the State Machine until the Host recovers.

  @param[in] Dev           - The Device Instance.
  @param[in] HostIn        - TRUE to halt the IN Endpoint, FALSE for OUT.
**/
VOID
UsbMsdStallEndpoint (
  IN USBMSD_DEV *Dev,
  IN BOOLEAN     HostIn
  );

#endif /* _USB_MSD_DXE_H_ */
