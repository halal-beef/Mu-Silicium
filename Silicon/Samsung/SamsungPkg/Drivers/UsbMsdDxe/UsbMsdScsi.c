/**
  SCSI Transparent Command Set handling for the Mass Storage Function.

  Only the Commands a Host actually issues to mount and use a removable Disk are
  implemented. Anything else is rejected with ILLEGAL REQUEST, which is the
  defined way to tell the Host a Command is unsupported.
**/

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include "UsbMsdDxe.h"

//
// SCSI Operation Codes.
//
#define SCSI_TEST_UNIT_READY              0x00
#define SCSI_REQUEST_SENSE                0x03
#define SCSI_INQUIRY                      0x12
#define SCSI_MODE_SELECT6                 0x15
#define SCSI_MODE_SENSE6                  0x1A
#define SCSI_START_STOP_UNIT              0x1B
#define SCSI_PREVENT_ALLOW_MEDIUM_REMOVAL 0x1E
#define SCSI_READ_FORMAT_CAPACITIES       0x23
#define SCSI_READ_CAPACITY10              0x25
#define SCSI_READ10                       0x28
#define SCSI_WRITE10                      0x2A
#define SCSI_VERIFY10                     0x2F
#define SCSI_SYNCHRONIZE_CACHE10          0x35
#define SCSI_MODE_SENSE10                 0x5A
#define SCSI_READ16                       0x88
#define SCSI_WRITE16                      0x8A
#define SCSI_READ_CAPACITY16              0x9E
#define SCSI_REPORT_LUNS                  0xA0

//
// SCSI Sense Keys.
//
#define SCSI_SENSE_NO_SENSE               0x00
#define SCSI_SENSE_NOT_READY              0x02
#define SCSI_SENSE_MEDIUM_ERROR           0x03
#define SCSI_SENSE_ILLEGAL_REQUEST        0x05
#define SCSI_SENSE_UNIT_ATTENTION         0x06
#define SCSI_SENSE_DATA_PROTECT           0x07

//
// Additional Sense Codes.
//
#define SCSI_ASC_NO_ADDITIONAL_SENSE      0x00
#define SCSI_ASC_INVALID_COMMAND          0x20
#define SCSI_ASC_LBA_OUT_OF_RANGE         0x21
#define SCSI_ASC_INVALID_FIELD_IN_CDB     0x24
#define SCSI_ASC_WRITE_PROTECTED          0x27
#define SCSI_ASC_MEDIUM_NOT_PRESENT       0x3A

//
// Response Lengths.
//
#define SCSI_INQUIRY_LENGTH               36
#define SCSI_REQUEST_SENSE_LENGTH         18
#define SCSI_READ_CAPACITY10_LENGTH       8
#define SCSI_READ_CAPACITY16_LENGTH       32
#define SCSI_MODE_SENSE6_LENGTH           4
#define SCSI_MODE_SENSE10_LENGTH          8
#define SCSI_READ_FORMAT_CAPACITIES_LENGTH 12
#define SCSI_REPORT_LUNS_LENGTH           16

/**
  Reads a Big Endian UINT16 from a Command Descriptor Block.
**/
STATIC
UINT16
ScsiReadUint16 (IN CONST UINT8 *Buffer)
{
  return (UINT16)((Buffer[0] << 8) | Buffer[1]);
}

/**
  Reads a Big Endian UINT32 from a Command Descriptor Block.
**/
STATIC
UINT32
ScsiReadUint32 (IN CONST UINT8 *Buffer)
{
  return ((UINT32)Buffer[0] << 24) | ((UINT32)Buffer[1] << 16) |
         ((UINT32)Buffer[2] << 8)  | (UINT32)Buffer[3];
}

/**
  Reads a Big Endian UINT64 from a Command Descriptor Block.
**/
STATIC
UINT64
ScsiReadUint64 (IN CONST UINT8 *Buffer)
{
  return ((UINT64)ScsiReadUint32 (Buffer) << 32) | ScsiReadUint32 (Buffer + 4);
}

/**
  Writes a Big Endian UINT32 into a Response Buffer.
**/
STATIC
VOID
ScsiWriteUint32 (
  OUT UINT8  *Buffer,
  IN  UINT32  Value)
{
  Buffer[0] = (UINT8)(Value >> 24);
  Buffer[1] = (UINT8)(Value >> 16);
  Buffer[2] = (UINT8)(Value >> 8);
  Buffer[3] = (UINT8)Value;
}

/**
  Writes a Big Endian UINT64 into a Response Buffer.
**/
STATIC
VOID
ScsiWriteUint64 (
  OUT UINT8  *Buffer,
  IN  UINT64  Value)
{
  ScsiWriteUint32 (Buffer, (UINT32)(Value >> 32));
  ScsiWriteUint32 (Buffer + 4, (UINT32)Value);
}

VOID
UsbMsdSetSense (
  IN USBMSD_DEV *Dev,
  IN UINT8       Lun,
  IN UINT8       SenseKey,
  IN UINT8       Asc,
  IN UINT8       Ascq)
{
  if (Lun >= USBMSD_MAX_LUN) {
    return;
  }

  Dev->Luns[Lun].SenseKey                     = SenseKey;
  Dev->Luns[Lun].AdditionalSenseCode          = Asc;
  Dev->Luns[Lun].AdditionalSenseCodeQualifier = Ascq;
}

/**
  Completes the current Command without transferring any Data.

  When the Host expected Data but the Command produced none, the Bulk Pipe is
  halted first so the Host does not wait for a Transfer that will never arrive.

  @param[in] Dev           - The Device Instance.
  @param[in] Status        - CSW Status Byte.

  @return EFI_SUCCESS      - The Command was completed.
**/
STATIC
EFI_STATUS
UsbMsdCompleteCommand (
  IN USBMSD_DEV *Dev,
  IN UINT8       Status)
{
  if (Dev->Cbw.DataTransferLength != 0) {
    UsbMsdStallEndpoint (Dev, (BOOLEAN)((Dev->Cbw.Flags & USBMSD_CBW_FLAG_DATA_IN) != 0));
  }

  return UsbMsdQueueCsw (Dev, Status, Dev->Cbw.DataTransferLength);
}

/**
  Fails the current Command and records Sense Data for it.

  @param[in] Dev           - The Device Instance.
  @param[in] SenseKey      - SCSI Sense Key.
  @param[in] Asc           - Additional Sense Code.
  @param[in] Ascq          - Additional Sense Code Qualifier.

  @return EFI_SUCCESS      - The Command was completed.
**/
STATIC
EFI_STATUS
UsbMsdFailCommand (
  IN USBMSD_DEV *Dev,
  IN UINT8       SenseKey,
  IN UINT8       Asc,
  IN UINT8       Ascq)
{
  UsbMsdSetSense (Dev, Dev->Cbw.Lun, SenseKey, Asc, Ascq);

  return UsbMsdCompleteCommand (Dev, USBMSD_CSW_STATUS_FAILED);
}

/**
  Sends a Response that was staged into the Data Buffer.

  @param[in] Dev           - The Device Instance.
  @param[in] Length        - Number of Bytes staged.

  @return EFI_SUCCESS      - The Response was queued.
**/
STATIC
EFI_STATUS
UsbMsdSendResponse (
  IN USBMSD_DEV *Dev,
  IN UINT32      Length)
{
  UINT32 TransferSize = MIN (Length, Dev->Cbw.DataTransferLength);

  if (TransferSize == 0) {
    return UsbMsdQueueCsw (Dev, USBMSD_CSW_STATUS_GOOD, Dev->Cbw.DataTransferLength);
  }

  //
  // A single Transfer covers every Response here, so nothing is left over.
  //
  Dev->TransferRemaining = 0;
  Dev->DataResidue       = Dev->Cbw.DataTransferLength - TransferSize;

  return UsbMsdQueueResponse (Dev, TransferSize);
}

/**
  Handles INQUIRY.
**/
STATIC
EFI_STATUS
UsbMsdScsiInquiry (IN USBMSD_DEV *Dev)
{
  UINT8 *Data = (UINT8 *)Dev->DataBuffer;

  //
  // An EVPD Request asks for a Vital Product Data Page, none of which are
  // supported.
  //
  if ((Dev->Cbw.Cb[1] & BIT0) != 0) {
    return UsbMsdFailCommand (Dev, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_INVALID_FIELD_IN_CDB, 0);
  }

  ZeroMem (Data, SCSI_INQUIRY_LENGTH);

  Data[0] = 0x00;                       // Direct Access Block Device
  Data[1] = 0x80;                       // Removable Medium
  Data[2] = 0x05;                       // Claims conformance to SPC-3
  Data[3] = 0x02;                       // Response Data Format
  Data[4] = SCSI_INQUIRY_LENGTH - 5;    // Additional Length

  //
  // Vendor, Product and Revision are space padded, not Null terminated.
  //
  CopyMem (&Data[8],  "SAMSUNG ", 8);
  CopyMem (&Data[16], "UEFI Mass Storage", 16);
  CopyMem (&Data[32], "1.00", 4);

  return UsbMsdSendResponse (Dev, SCSI_INQUIRY_LENGTH);
}

/**
  Handles REQUEST SENSE.
**/
STATIC
EFI_STATUS
UsbMsdScsiRequestSense (IN USBMSD_DEV *Dev)
{
  UINT8      *Data = (UINT8 *)Dev->DataBuffer;
  USBMSD_LUN *Lun  = &Dev->Luns[Dev->Cbw.Lun];

  ZeroMem (Data, SCSI_REQUEST_SENSE_LENGTH);

  Data[0]  = 0x70;                      // Current Error, Fixed Format
  Data[2]  = Lun->SenseKey;
  Data[7]  = SCSI_REQUEST_SENSE_LENGTH - 8;
  Data[12] = Lun->AdditionalSenseCode;
  Data[13] = Lun->AdditionalSenseCodeQualifier;

  //
  // Sense Data is consumed once, so it is cleared after being reported.
  //
  UsbMsdSetSense (Dev, Dev->Cbw.Lun, SCSI_SENSE_NO_SENSE, SCSI_ASC_NO_ADDITIONAL_SENSE, 0);

  return UsbMsdSendResponse (Dev, SCSI_REQUEST_SENSE_LENGTH);
}

/**
  Handles READ CAPACITY (10).
**/
STATIC
EFI_STATUS
UsbMsdScsiReadCapacity10 (IN USBMSD_DEV *Dev)
{
  UINT8                 *Data  = (UINT8 *)Dev->DataBuffer;
  EFI_BLOCK_IO_MEDIA    *Media = Dev->Luns[Dev->Cbw.Lun].BlkIo->Media;
  UINT64                 LastBlock;

  //
  // READ CAPACITY (10) reports the last addressable Block. Disks larger than
  // what a UINT32 can address report the saturated Value, which is the defined
  // signal for the Host to switch to READ CAPACITY (16).
  //
  LastBlock = Media->LastBlock;

  if (LastBlock > MAX_UINT32) {
    LastBlock = MAX_UINT32;
  }

  ScsiWriteUint32 (&Data[0], (UINT32)LastBlock);
  ScsiWriteUint32 (&Data[4], Media->BlockSize);

  return UsbMsdSendResponse (Dev, SCSI_READ_CAPACITY10_LENGTH);
}

/**
  Handles READ CAPACITY (16).
**/
STATIC
EFI_STATUS
UsbMsdScsiReadCapacity16 (IN USBMSD_DEV *Dev)
{
  UINT8              *Data  = (UINT8 *)Dev->DataBuffer;
  EFI_BLOCK_IO_MEDIA *Media = Dev->Luns[Dev->Cbw.Lun].BlkIo->Media;

  ZeroMem (Data, SCSI_READ_CAPACITY16_LENGTH);

  ScsiWriteUint64 (&Data[0], Media->LastBlock);
  ScsiWriteUint32 (&Data[8], Media->BlockSize);

  return UsbMsdSendResponse (Dev, SCSI_READ_CAPACITY16_LENGTH);
}

/**
  Handles READ FORMAT CAPACITIES.
**/
STATIC
EFI_STATUS
UsbMsdScsiReadFormatCapacities (IN USBMSD_DEV *Dev)
{
  UINT8              *Data  = (UINT8 *)Dev->DataBuffer;
  EFI_BLOCK_IO_MEDIA *Media = Dev->Luns[Dev->Cbw.Lun].BlkIo->Media;
  UINT64              Blocks;

  ZeroMem (Data, SCSI_READ_FORMAT_CAPACITIES_LENGTH);

  //
  // One Capacity Descriptor follows the four Byte Header.
  //
  Data[3] = 8;

  Blocks = Media->LastBlock + 1;

  if (Blocks > MAX_UINT32) {
    Blocks = MAX_UINT32;
  }

  ScsiWriteUint32 (&Data[4], (UINT32)Blocks);

  Data[8] = 0x02;                       // Formatted Media

  Data[9]  = (UINT8)(Media->BlockSize >> 16);
  Data[10] = (UINT8)(Media->BlockSize >> 8);
  Data[11] = (UINT8)Media->BlockSize;

  return UsbMsdSendResponse (Dev, SCSI_READ_FORMAT_CAPACITIES_LENGTH);
}

/**
  Handles MODE SENSE (6) and MODE SENSE (10).

  Only an empty Mode Parameter Header is returned. The Host uses it to learn
  whether the Medium is Write Protected.
**/
STATIC
EFI_STATUS
UsbMsdScsiModeSense (
  IN USBMSD_DEV *Dev,
  IN BOOLEAN     TenByte)
{
  UINT8              *Data  = (UINT8 *)Dev->DataBuffer;
  EFI_BLOCK_IO_MEDIA *Media = Dev->Luns[Dev->Cbw.Lun].BlkIo->Media;
  UINT8               Length;

  Length = TenByte ? SCSI_MODE_SENSE10_LENGTH : SCSI_MODE_SENSE6_LENGTH;

  ZeroMem (Data, Length);

  if (TenByte) {
    Data[1] = Length - 2;               // Mode Data Length
    Data[3] = Media->ReadOnly ? 0x80 : 0x00;
  } else {
    Data[0] = Length - 1;               // Mode Data Length
    Data[2] = Media->ReadOnly ? 0x80 : 0x00;
  }

  return UsbMsdSendResponse (Dev, Length);
}

/**
  Handles REPORT LUNS.
**/
STATIC
EFI_STATUS
UsbMsdScsiReportLuns (IN USBMSD_DEV *Dev)
{
  UINT8 *Data = (UINT8 *)Dev->DataBuffer;

  ZeroMem (Data, SCSI_REPORT_LUNS_LENGTH);

  //
  // Only LUN 0 is reported. Hosts that drive more than one use the Class
  // specific GET_MAX_LUN Request instead, which covers every assigned LUN.
  //
  ScsiWriteUint32 (&Data[0], 8);        // LUN List Length, one Entry

  return UsbMsdSendResponse (Dev, SCSI_REPORT_LUNS_LENGTH);
}

/**
  Starts a READ (10) or (16) Data Transfer.
**/
STATIC
EFI_STATUS
UsbMsdScsiRead (
  IN USBMSD_DEV *Dev,
  IN UINT64      Lba,
  IN UINT32      Blocks)
{
  EFI_BLOCK_IO_MEDIA *Media = Dev->Luns[Dev->Cbw.Lun].BlkIo->Media;
  UINT64              Total;

  if (Blocks == 0) {
    return UsbMsdCompleteCommand (Dev, USBMSD_CSW_STATUS_GOOD);
  }

  if (Lba > Media->LastBlock || Blocks > (Media->LastBlock - Lba + 1)) {
    return UsbMsdFailCommand (Dev, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_LBA_OUT_OF_RANGE, 0);
  }

  Total = MultU64x32 (Blocks, Media->BlockSize);

  //
  // The Host must have asked for exactly as much Data as the Command produces.
  // A mismatch is a Phase Error, which the Host recovers from with a Reset.
  //
  if (Total != Dev->Cbw.DataTransferLength) {
    return UsbMsdCompleteCommand (Dev, USBMSD_CSW_STATUS_PHASE);
  }

  Dev->TransferLun       = Dev->Cbw.Lun;
  Dev->TransferLba       = Lba;
  Dev->TransferRemaining = (UINT32)Total;
  Dev->DataResidue       = (UINT32)Total;

  return UsbMsdContinueDataTransfer (Dev, 0);
}

/**
  Starts a WRITE (10) or (16) Data Transfer.
**/
STATIC
EFI_STATUS
UsbMsdScsiWrite (
  IN USBMSD_DEV *Dev,
  IN UINT64      Lba,
  IN UINT32      Blocks)
{
  EFI_BLOCK_IO_MEDIA *Media = Dev->Luns[Dev->Cbw.Lun].BlkIo->Media;
  UINT64              Total;

  if (Blocks == 0) {
    return UsbMsdCompleteCommand (Dev, USBMSD_CSW_STATUS_GOOD);
  }

  if (Media->ReadOnly) {
    return UsbMsdFailCommand (Dev, SCSI_SENSE_DATA_PROTECT, SCSI_ASC_WRITE_PROTECTED, 0);
  }

  if (Lba > Media->LastBlock || Blocks > (Media->LastBlock - Lba + 1)) {
    return UsbMsdFailCommand (Dev, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_LBA_OUT_OF_RANGE, 0);
  }

  Total = MultU64x32 (Blocks, Media->BlockSize);

  if (Total != Dev->Cbw.DataTransferLength) {
    return UsbMsdCompleteCommand (Dev, USBMSD_CSW_STATUS_PHASE);
  }

  Dev->TransferLun       = Dev->Cbw.Lun;
  Dev->TransferLba       = Lba;
  Dev->TransferRemaining = (UINT32)Total;
  Dev->DataResidue       = (UINT32)Total;

  return UsbMsdContinueDataTransfer (Dev, 0);
}

EFI_STATUS
UsbMsdContinueDataTransfer (
  IN USBMSD_DEV *Dev,
  IN UINT32      BytesHandled)
{
  EFI_STATUS             Status;
  EFI_BLOCK_IO_PROTOCOL *BlkIo = Dev->Luns[Dev->TransferLun].BlkIo;
  EFI_BLOCK_IO_MEDIA    *Media = BlkIo->Media;
  BOOLEAN                HostIn;
  UINT32                 Chunk;

  HostIn = (BOOLEAN)((Dev->Cbw.Flags & USBMSD_CBW_FLAG_DATA_IN) != 0);

  //
  // Retire the Transfer that just finished. On a Write the received Data still
  // has to reach the Medium before the next Chunk overwrites the Buffer.
  //
  if (BytesHandled != 0) {
    if (!HostIn) {
      Status = BlkIo->WriteBlocks (BlkIo,
                                   Media->MediaId,
                                   Dev->TransferLba,
                                   Dev->TransferChunk,
                                   Dev->DataBuffer);
      if (EFI_ERROR (Status)) {
        DEBUG ((EFI_D_ERROR, "%a: WriteBlocks Failed! Status = %r\n", __FUNCTION__, Status));

        return UsbMsdFailCommand (Dev, SCSI_SENSE_MEDIUM_ERROR, SCSI_ASC_NO_ADDITIONAL_SENSE, 0);
      }
    }

    Dev->TransferLba       += Dev->TransferChunk / Media->BlockSize;
    Dev->TransferRemaining -= Dev->TransferChunk;
    Dev->DataResidue       -= Dev->TransferChunk;
  }

  if (Dev->TransferRemaining == 0) {
    return UsbMsdQueueCsw (Dev, Dev->CswStatus, Dev->DataResidue);
  }

  //
  // Chunks are whole Blocks, because BlockIo cannot transfer a partial one.
  //
  Chunk = MIN (Dev->TransferRemaining, USBMSD_MAX_TRANSFER_SIZE);
  Chunk -= Chunk % Media->BlockSize;

  if (Chunk == 0) {
    return UsbMsdFailCommand (Dev, SCSI_SENSE_MEDIUM_ERROR, SCSI_ASC_NO_ADDITIONAL_SENSE, 0);
  }

  if (HostIn) {
    Status = BlkIo->ReadBlocks (BlkIo,
                                Media->MediaId,
                                Dev->TransferLba,
                                Chunk,
                                Dev->DataBuffer);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "%a: ReadBlocks Failed! Status = %r\n", __FUNCTION__, Status));

      return UsbMsdFailCommand (Dev, SCSI_SENSE_MEDIUM_ERROR, SCSI_ASC_NO_ADDITIONAL_SENSE, 0);
    }
  }

  return UsbMsdQueueData (Dev, Chunk, HostIn);
}

EFI_STATUS
UsbMsdHandleScsiCommand (IN USBMSD_DEV *Dev)
{
  USBMSD_LUN *Lun = &Dev->Luns[Dev->Cbw.Lun];
  UINT8      *Cdb = Dev->Cbw.Cb;

  //
  // Every Command below needs Media. The LUN is only marked Present once a
  // BlockIo Protocol has been assigned to it, so this guards against the Media
  // disappearing afterwards.
  //
  if (Lun->BlkIo == NULL || !Lun->BlkIo->Media->MediaPresent) {
    return UsbMsdFailCommand (Dev, SCSI_SENSE_NOT_READY, SCSI_ASC_MEDIUM_NOT_PRESENT, 0);
  }

  switch (Cdb[0]) {
    case SCSI_TEST_UNIT_READY:
      return UsbMsdCompleteCommand (Dev, USBMSD_CSW_STATUS_GOOD);

    case SCSI_REQUEST_SENSE:
      return UsbMsdScsiRequestSense (Dev);

    case SCSI_INQUIRY:
      return UsbMsdScsiInquiry (Dev);

    case SCSI_READ_CAPACITY10:
      return UsbMsdScsiReadCapacity10 (Dev);

    case SCSI_READ_CAPACITY16:
      //
      // READ CAPACITY (16) is a Service Action of the SERVICE ACTION IN Opcode.
      //
      if ((Cdb[1] & 0x1F) != 0x10) {
        return UsbMsdFailCommand (Dev, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_INVALID_FIELD_IN_CDB, 0);
      }

      return UsbMsdScsiReadCapacity16 (Dev);

    case SCSI_READ_FORMAT_CAPACITIES:
      return UsbMsdScsiReadFormatCapacities (Dev);

    case SCSI_MODE_SENSE6:
      return UsbMsdScsiModeSense (Dev, FALSE);

    case SCSI_MODE_SENSE10:
      return UsbMsdScsiModeSense (Dev, TRUE);

    case SCSI_REPORT_LUNS:
      return UsbMsdScsiReportLuns (Dev);

    case SCSI_READ10:
      return UsbMsdScsiRead (Dev, ScsiReadUint32 (&Cdb[2]), ScsiReadUint16 (&Cdb[7]));

    case SCSI_WRITE10:
      return UsbMsdScsiWrite (Dev, ScsiReadUint32 (&Cdb[2]), ScsiReadUint16 (&Cdb[7]));

    case SCSI_READ16:
      return UsbMsdScsiRead (Dev, ScsiReadUint64 (&Cdb[2]), ScsiReadUint32 (&Cdb[10]));

    case SCSI_WRITE16:
      return UsbMsdScsiWrite (Dev, ScsiReadUint64 (&Cdb[2]), ScsiReadUint32 (&Cdb[10]));

    case SCSI_SYNCHRONIZE_CACHE10:
      //
      // BlockIo Writes are not cached by this Driver, so there is nothing to
      // flush. Reporting Success keeps the Host from retrying.
      //
      return UsbMsdCompleteCommand (Dev, USBMSD_CSW_STATUS_GOOD);

    case SCSI_VERIFY10:
    case SCSI_START_STOP_UNIT:
    case SCSI_PREVENT_ALLOW_MEDIUM_REMOVAL:
    case SCSI_MODE_SELECT6:
      return UsbMsdCompleteCommand (Dev, USBMSD_CSW_STATUS_GOOD);

    default:
      DEBUG ((EFI_D_WARN, "%a: Unsupported SCSI Opcode 0x%02x\n", __FUNCTION__, Cdb[0]));

      return UsbMsdFailCommand (Dev, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_INVALID_COMMAND, 0);
  }
}
