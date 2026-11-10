#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include <Protocol/PartitionInfo.h>

#include "FastbootApp.h"
#include "FastbootStorage.h"

STATIC
BOOLEAN
FbReadPartition (
  IN  EFI_HANDLE    Handle,
  OUT CHAR8        *Name,
  IN  UINTN         NameSize,
  OUT FB_PARTITION *Partition)
{
  EFI_STATUS                Status;
  EFI_PARTITION_INFO_PROTOCOL *Info;
  EFI_BLOCK_IO_PROTOCOL    *BlockIo;

  Status = gBS->HandleProtocol (Handle, &gEfiPartitionInfoProtocolGuid, (VOID **)&Info);
  if (EFI_ERROR (Status) || Info->Type != PARTITION_TYPE_GPT) {
    return FALSE;
  }

  if (Info->Info.Gpt.PartitionName[0] == 0) {
    return FALSE;
  }

  if (EFI_ERROR (UnicodeStrToAsciiStrS (Info->Info.Gpt.PartitionName, Name, NameSize))) {
    return FALSE;
  }

  Status = gBS->HandleProtocol (Handle, &gEfiBlockIoProtocolGuid, (VOID **)&BlockIo);
  if (EFI_ERROR (Status) || BlockIo->Media == NULL || !BlockIo->Media->LogicalPartition) {
    return FALSE;
  }

  Partition->BlockIo = BlockIo;
  Partition->Size    = MultU64x32 (BlockIo->Media->LastBlock + 1, BlockIo->Media->BlockSize);

  if (EFI_ERROR (gBS->HandleProtocol (Handle, &gEfiEraseBlockProtocolGuid, (VOID **)&Partition->Erase))) {
    Partition->Erase = NULL;
  }

  return TRUE;
}

EFI_STATUS
FbFindPartition (
  IN  CONST CHAR8  *Name,
  OUT FB_PARTITION *Partition)
{
  EFI_STATUS  Status;
  EFI_HANDLE *Handles;
  UINTN       Count;
  UINTN       Index;
  CHAR8       PartName[FB_MAX_NAME];

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiPartitionInfoProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    return EFI_NOT_FOUND;
  }

  for (Index = 0; Index < Count; Index++) {
    if (FbReadPartition (Handles[Index], PartName, sizeof (PartName), Partition) &&
        AsciiStriCmp (PartName, Name) == 0) {
      FreePool (Handles);
      return EFI_SUCCESS;
    }
  }

  FreePool (Handles);

  return EFI_NOT_FOUND;
}

VOID
FbEnumeratePartitions (
  IN FB_PARTITION_CALLBACK Callback,
  IN VOID                 *Context)
{
  EFI_STATUS   Status;
  EFI_HANDLE  *Handles;
  UINTN        Count;
  UINTN        Index;
  CHAR8        PartName[FB_MAX_NAME];
  FB_PARTITION Partition;

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiPartitionInfoProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    return;
  }

  for (Index = 0; Index < Count; Index++) {
    if (FbReadPartition (Handles[Index], PartName, sizeof (PartName), &Partition)) {
      Callback (Context, PartName, Partition.Size);
    }
  }

  FreePool (Handles);
}

STATIC
EFI_STATUS
FbIoInit (
  OUT FB_IO       *Io,
  IN  FB_PARTITION *Partition,
  IN  FB_PROGRESS  Progress,
  IN  VOID        *Context)
{
  Io->Partition   = Partition;
  Io->Progress    = Progress;
  Io->Context     = Context;
  Io->BouncePages = EFI_SIZE_TO_PAGES (FB_IO_CHUNK + SIZE_4KB);
  Io->Bounce      = AllocateAlignedPages (Io->BouncePages, SIZE_4KB);

  return (Io->Bounce != NULL) ? EFI_SUCCESS : EFI_OUT_OF_RESOURCES;
}

STATIC
VOID
FbIoFree (IN FB_IO *Io)
{
  if (Io->Bounce != NULL) {
    FreeAlignedPages (Io->Bounce, Io->BouncePages);
    Io->Bounce = NULL;
  }
}

STATIC
EFI_STATUS
FbIoWrite (
  IN FB_IO        *Io,
  IN UINT64        Off,
  IN CONST UINT8  *Data,
  IN UINT64        Size)
{
  EFI_BLOCK_IO_MEDIA *Media = Io->Partition->BlockIo->Media;
  UINT32              Bs    = Media->BlockSize;
  UINTN               Len;
  UINTN               Padded;
  CONST UINT8        *Src;
  EFI_STATUS          Status;

  if ((Off % Bs) != 0) {
    return EFI_INVALID_PARAMETER;
  }

  if (Off + Size > Io->Partition->Size) {
    return EFI_VOLUME_FULL;
  }

  while (Size != 0) {
    Len    = (Size > FB_IO_CHUNK) ? FB_IO_CHUNK : (UINTN)Size;
    Padded = ALIGN_VALUE (Len, Bs);
    Src    = Data;

    if (Padded != Len || (Media->IoAlign > 1 && (((UINTN)Data) & (Media->IoAlign - 1)) != 0)) {
      ZeroMem (Io->Bounce, Padded);
      CopyMem (Io->Bounce, Data, Len);
      Src = Io->Bounce;
    }

    Status = Io->Partition->BlockIo->WriteBlocks (Io->Partition->BlockIo, Media->MediaId, DivU64x32 (Off, Bs), Padded, (VOID *)Src);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "Fastboot: WriteBlocks failed at 0x%lx! Status = %r\n", Off, Status));
      return Status;
    }

    Off  += Len;
    Data += Len;
    Size -= Len;

    if (Io->Progress != NULL) {
      Io->Progress (Io->Context, Off, Io->Partition->Size);
    }
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
FbIoFill (
  IN FB_IO  *Io,
  IN UINT64  Off,
  IN UINT32  Pattern,
  IN UINT64  Size)
{
  EFI_BLOCK_IO_MEDIA *Media = Io->Partition->BlockIo->Media;
  UINT32              Bs    = Media->BlockSize;
  UINT32             *Words = (UINT32 *)Io->Bounce;
  UINTN               Len;
  UINTN               Index;
  EFI_STATUS          Status;

  if ((Off % Bs) != 0 || (Size % Bs) != 0) {
    return EFI_INVALID_PARAMETER;
  }

  if (Off + Size > Io->Partition->Size) {
    return EFI_VOLUME_FULL;
  }

  for (Index = 0; Index < FB_IO_CHUNK / sizeof (UINT32); Index++) {
    Words[Index] = Pattern;
  }

  while (Size != 0) {
    Len = (Size > FB_IO_CHUNK) ? FB_IO_CHUNK : (UINTN)Size;

    Status = Io->Partition->BlockIo->WriteBlocks (Io->Partition->BlockIo, Media->MediaId, DivU64x32 (Off, Bs), Len, Io->Bounce);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "Fastboot: WriteBlocks (fill) failed at 0x%lx! Status = %r\n", Off, Status));
      return Status;
    }

    Off  += Len;
    Size -= Len;

    if (Io->Progress != NULL) {
      Io->Progress (Io->Context, Off, Io->Partition->Size);
    }
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
FbFlashSparse (
  IN FB_IO       *Io,
  IN CONST UINT8 *Image,
  IN UINTN        ImageSize)
{
  CONST SPARSE_HEADER       *Header = (CONST SPARSE_HEADER *)Image;
  CONST SPARSE_CHUNK_HEADER *Chunk;
  UINT32                     Bs = Io->Partition->BlockIo->Media->BlockSize;
  UINT64                     Pos;
  UINT64                     Off = 0;
  UINT64                     DataBytes;
  UINT64                     PayloadSize;
  UINT32                     Pattern;
  UINT32                     Index;
  EFI_STATUS                 Status;

  if (ImageSize < sizeof (SPARSE_HEADER) ||
      Header->FileHeaderSize < sizeof (SPARSE_HEADER) ||
      Header->ChunkHeaderSize < sizeof (SPARSE_CHUNK_HEADER) ||
      Header->BlockSize == 0 ||
      (Header->BlockSize % Bs) != 0) {
    return EFI_COMPROMISED_DATA;
  }

  Pos = Header->FileHeaderSize;

  for (Index = 0; Index < Header->TotalChunks; Index++) {
    if (Pos + Header->ChunkHeaderSize > ImageSize) {
      return EFI_COMPROMISED_DATA;
    }

    Chunk = (CONST SPARSE_CHUNK_HEADER *)(Image + Pos);

    if (Chunk->TotalSize < Header->ChunkHeaderSize || Pos + Chunk->TotalSize > ImageSize) {
      return EFI_COMPROMISED_DATA;
    }

    DataBytes   = (UINT64)Chunk->ChunkSize * Header->BlockSize;
    PayloadSize = Chunk->TotalSize - Header->ChunkHeaderSize;

    switch (Chunk->ChunkType) {
      case CHUNK_TYPE_RAW:
        if (PayloadSize != DataBytes) {
          return EFI_COMPROMISED_DATA;
        }

        Status = FbIoWrite (Io, Off, Image + Pos + Header->ChunkHeaderSize, DataBytes);
        break;

      case CHUNK_TYPE_FILL:
        if (PayloadSize < sizeof (UINT32)) {
          return EFI_COMPROMISED_DATA;
        }

        CopyMem (&Pattern, Image + Pos + Header->ChunkHeaderSize, sizeof (UINT32));
        Status = FbIoFill (Io, Off, Pattern, DataBytes);
        break;

      case CHUNK_TYPE_DONT_CARE:
        Status = (Off + DataBytes > Io->Partition->Size) ? EFI_VOLUME_FULL : EFI_SUCCESS;
        break;

      case CHUNK_TYPE_CRC32:
        DataBytes = 0;
        Status    = EFI_SUCCESS;
        break;

      default:
        return EFI_COMPROMISED_DATA;
    }

    if (EFI_ERROR (Status)) {
      return Status;
    }

    Off += DataBytes;
    Pos += Chunk->TotalSize;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
FbFlashImage (
  IN FB_PARTITION *Partition,
  IN VOID         *Image,
  IN UINTN         Size,
  IN FB_PROGRESS   Progress,
  IN VOID         *Context)
{
  FB_IO      Io;
  EFI_STATUS Status;
  UINT32     Magic = 0;

  if (Partition->BlockIo->Media->ReadOnly) {
    return EFI_WRITE_PROTECTED;
  }

  Status = FbIoInit (&Io, Partition, Progress, Context);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (Size >= sizeof (UINT32)) {
    CopyMem (&Magic, Image, sizeof (UINT32));
  }

  if (Magic == SPARSE_HEADER_MAGIC) {
    Status = FbFlashSparse (&Io, Image, Size);
  } else {
    Status = FbIoWrite (&Io, 0, Image, Size);
  }

  if (!EFI_ERROR (Status)) {
    Status = Partition->BlockIo->FlushBlocks (Partition->BlockIo);
  }

  FbIoFree (&Io);

  return Status;
}

EFI_STATUS
FbErasePartition (
  IN FB_PARTITION *Partition,
  IN FB_PROGRESS   Progress,
  IN VOID         *Context)
{
  FB_IO      Io;
  EFI_STATUS Status;
  UINT32     Bs;
  UINT64     Blocks;

  if (Partition->BlockIo->Media->ReadOnly) {
    return EFI_WRITE_PROTECTED;
  }

  Bs     = Partition->BlockIo->Media->BlockSize;
  Blocks = DivU64x32 (Partition->Size, Bs);

  if (Partition->Erase != NULL &&
      Partition->Erase->EraseLengthGranularity != 0 &&
      (Blocks % Partition->Erase->EraseLengthGranularity) == 0) {
    Status = Partition->Erase->EraseBlocks (Partition->Erase, Partition->BlockIo->Media->MediaId, 0, NULL, (UINTN)Partition->Size);
    if (!EFI_ERROR (Status)) {
      return Partition->BlockIo->FlushBlocks (Partition->BlockIo);
    }

    DEBUG ((EFI_D_WARN, "Fastboot: EraseBlocks failed (%r), falling back to zero fill\n", Status));
  }

  Status = FbIoInit (&Io, Partition, Progress, Context);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = FbIoFill (&Io, 0, 0, Partition->Size);

  if (!EFI_ERROR (Status)) {
    Status = Partition->BlockIo->FlushBlocks (Partition->BlockIo);
  }

  FbIoFree (&Io);

  return Status;
}
