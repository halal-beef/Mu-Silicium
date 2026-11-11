#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/FdtLib.h>
#include <Library/MemoryAllocationLib.h>

#include "FastbootApp.h"
#include "FastbootStorage.h"
#include "FastbootUsb.h"
#include "DtTable.h"
#include "DtboMerge.h"

#define DT_PARTITION_NAME    "dtb"
#define DT_OVERLAY_PARTITION "dtbo"

// Extra Space for Fixups/new Strings on Top of Base + Overlays.
#define DT_MERGE_SLACK       SIZE_64KB

typedef struct {
  UINT8  *Data;       // Page Aligned
  UINTN   Pages;
  UINT32  Size;       // TotalSize from Header
  UINT32  Count;
  UINT32  EntriesOffset;
  UINT32  EntrySize;
} DT_TABLE;

STATIC
VOID
DtTableFree (IN DT_TABLE *Table)
{
  if (Table->Data != NULL) {
    FreePages (Table->Data, Table->Pages);
  }

  ZeroMem (Table, sizeof (*Table));
}

// Reads a whole Table Partition into a Page Aligned Buffer and Validates its Layout.
STATIC
EFI_STATUS
DtTableLoad (
  IN  CONST CHAR8 *Name,
  OUT DT_TABLE    *Table)
{
  FB_PARTITION    Partition;
  DT_TABLE_HEADER Header;
  UINT64          Done;
  UINTN           Chunk;
  EFI_STATUS      Status;
  UINT32          Total;
  UINT32          HeaderSize;
  UINT32          EntrySize;
  UINT32          Count;
  UINT32          EntriesOffset;

  ZeroMem (Table, sizeof (*Table));

  Status = FbFindPartition (Name, &Partition);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "DtboMerge: No '%a' Partition\n", Name));
    return EFI_NOT_FOUND;
  }

  Status = FbReadPartitionData (&Partition, 0, &Header, sizeof (Header));
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (SwapBytes32 (Header.Magic) != DT_TABLE_MAGIC) {
    DEBUG ((EFI_D_ERROR, "DtboMerge: '%a' has no DT Table Magic\n", Name));
    return EFI_COMPROMISED_DATA;
  }

  Total         = SwapBytes32 (Header.TotalSize);
  HeaderSize    = SwapBytes32 (Header.HeaderSize);
  EntrySize     = SwapBytes32 (Header.DtEntrySize);
  Count         = SwapBytes32 (Header.DtEntryCount);
  EntriesOffset = SwapBytes32 (Header.DtEntriesOffset);

  if (Total < sizeof (Header) || Total > Partition.Size ||
      HeaderSize < sizeof (Header) || EntrySize < sizeof (DT_TABLE_ENTRY) ||
      Count == 0 ||
      (UINT64)EntriesOffset + (UINT64)Count * EntrySize > Total) {
    DEBUG ((EFI_D_ERROR, "DtboMerge: '%a' has an invalid Table Header\n", Name));
    return EFI_COMPROMISED_DATA;
  }

  Table->Pages = EFI_SIZE_TO_PAGES (Total);
  Table->Data  = AllocatePages (Table->Pages);
  if (Table->Data == NULL) {
    Table->Pages = 0;
    return EFI_OUT_OF_RESOURCES;
  }

  // FbReadPartitionData is Limited to FB_IO_CHUNK per Call.
  for (Done = 0; Done < Total; Done += Chunk) {
    Chunk = (UINTN)((Total - Done > FB_IO_CHUNK) ? FB_IO_CHUNK : (Total - Done));

    Status = FbReadPartitionData (&Partition, Done, Table->Data + Done, Chunk);
    if (EFI_ERROR (Status)) {
      DtTableFree (Table);
      return Status;
    }
  }

  Table->Size          = Total;
  Table->Count         = Count;
  Table->EntriesOffset = EntriesOffset;
  Table->EntrySize     = EntrySize;

  return EFI_SUCCESS;
}

STATIC
CONST DT_TABLE_ENTRY *
DtTableEntry (
  IN CONST DT_TABLE *Table,
  IN UINT32          Index)
{
  return (CONST DT_TABLE_ENTRY *)(Table->Data + Table->EntriesOffset + (UINTN)Index * Table->EntrySize);
}

// Entry Matches when MinRev <= Rev <= MaxRev (Custom[0] / Custom[1], both Inclusive).
STATIC
BOOLEAN
DtEntryMatches (
  IN CONST DT_TABLE_ENTRY *Entry,
  IN UINT32                Rev)
{
  return (Rev >= SwapBytes32 (Entry->Custom[0]) && Rev <= SwapBytes32 (Entry->Custom[1]));
}

// Copies an Entry's DTB out of the Table into its own Page Aligned Buffer.
// Entries are only 4 Byte Aligned inside the Table, libfdt wants 8.
STATIC
EFI_STATUS
DtEntryCopy (
  IN  CONST DT_TABLE       *Table,
  IN  CONST DT_TABLE_ENTRY *Entry,
  IN  UINTN                 ExtraSize,
  OUT UINT8               **Buffer,
  OUT UINTN                *Pages,
  OUT UINT32               *DtbSize)
{
  UINT32  Offset = SwapBytes32 (Entry->DtOffset);
  UINT32  Size   = SwapBytes32 (Entry->DtSize);
  UINT32  FdtSize;
  UINT8  *Aligned;
  UINTN   AllocPages;

  if (Size < sizeof (FDT_HEADER) || (UINT64)Offset + Size > Table->Size) {
    return EFI_COMPROMISED_DATA;
  }

  // Header is only 4 Byte Aligned here, FdtCheckHeader reads u32 Fields so copy first.
  AllocPages = EFI_SIZE_TO_PAGES ((UINTN)Size + ExtraSize);
  Aligned    = AllocatePages (AllocPages);
  if (Aligned == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  ZeroMem (Aligned, EFI_PAGES_TO_SIZE (AllocPages));
  CopyMem (Aligned, Table->Data + Offset, Size);

  if (FdtCheckHeader (Aligned) != 0) {
    FreePages (Aligned, AllocPages);
    return EFI_COMPROMISED_DATA;
  }

  FdtSize = FdtTotalSize (Aligned);
  if (FdtSize > Size) {
    FreePages (Aligned, AllocPages);
    return EFI_COMPROMISED_DATA;
  }

  *Buffer  = Aligned;
  *Pages   = AllocPages;
  *DtbSize = FdtSize;

  return EFI_SUCCESS;
}

EFI_STATUS
DtboMergeForRevision (
  IN  UINT32  Rev,
  OUT UINT8 **Merged,
  OUT UINTN  *MergedSize)
{
  DT_TABLE               Dt       = { 0 };
  DT_TABLE               Dtbo     = { 0 };
  CONST DT_TABLE_ENTRY  *BaseEntry = NULL;
  UINT32                 BaseIndex = 0;
  UINT32                 Index;
  UINT32                 Matches   = 0;
  UINT64                 Room;
  UINT8                 *Work      = NULL;
  UINTN                  WorkPages = 0;
  UINT32                 BaseSize  = 0;
  UINT8                 *Overlay   = NULL;
  UINTN                  OverlayPages;
  UINT32                 OverlaySize;
  UINT8                 *Out       = NULL;
  UINTN                  OutSize;
  INT32                  FdtStatus;
  EFI_STATUS             Status;

  *Merged     = NULL;
  *MergedSize = 0;

  Status = DtTableLoad (DT_PARTITION_NAME, &Dt);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = DtTableLoad (DT_OVERLAY_PARTITION, &Dtbo);
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  // Pick the first Base DTB that covers this Revision.
  for (Index = 0; Index < Dt.Count; Index++) {
    CONST DT_TABLE_ENTRY *Entry = DtTableEntry (&Dt, Index);

    if (DtEntryMatches (Entry, Rev)) {
      BaseEntry = Entry;
      BaseIndex = Index;
      break;
    }
  }

  if (BaseEntry == NULL) {
    DEBUG ((EFI_D_ERROR, "DtboMerge: No Base DTB for Rev %u\n", Rev));
    Status = EFI_NOT_FOUND;
    goto Exit;
  }

  // Worst Case Room: Base + every matching Overlay + Slack.
  Room = SwapBytes32 (BaseEntry->DtSize) + DT_MERGE_SLACK;

  for (Index = 0; Index < Dtbo.Count; Index++) {
    CONST DT_TABLE_ENTRY *Entry = DtTableEntry (&Dtbo, Index);

    if (DtEntryMatches (Entry, Rev)) {
      Room += SwapBytes32 (Entry->DtSize);
    }
  }

  if (Room > SIZE_16MB) {
    Status = EFI_BAD_BUFFER_SIZE;
    goto Exit;
  }

  Status = DtEntryCopy (&Dt, BaseEntry, (UINTN)(Room - SwapBytes32 (BaseEntry->DtSize)), &Work, &WorkPages, &BaseSize);
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  FB_INFO ("base: dt entry %u (%u bytes)", BaseIndex, BaseSize);

  // Let libfdt Grow the Blob into the Spare Room. It Sets TotalSize to the Buffer Size.
  FdtStatus = FdtOpenInto (Work, Work, EFI_PAGES_TO_SIZE (WorkPages));
  if (FdtStatus < 0) {
    DEBUG ((EFI_D_ERROR, "DtboMerge: FdtOpenInto: %a\n", FdtStrerror (FdtStatus)));
    Status = EFI_DEVICE_ERROR;
    goto Exit;
  }

  for (Index = 0; Index < Dtbo.Count; Index++) {
    CONST DT_TABLE_ENTRY *Entry = DtTableEntry (&Dtbo, Index);

    if (!DtEntryMatches (Entry, Rev)) {
      continue;
    }

    // The Overlay gets Modified by FdtOverlayApply, so it needs its own Aligned Copy.
    Status = DtEntryCopy (&Dtbo, Entry, 0, &Overlay, &OverlayPages, &OverlaySize);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "DtboMerge: Bad Overlay %u: %r\n", Index, Status));
      goto Exit;
    }

    FdtStatus = FdtOverlayApply (Work, Overlay);

    FreePages (Overlay, OverlayPages);
    Overlay = NULL;

    if (FdtStatus < 0) {
      DEBUG ((EFI_D_ERROR, "DtboMerge: Overlay %u: %a\n", Index, FdtStrerror (FdtStatus)));
      FB_INFO ("overlay: dtbo entry %u FAILED (%a)", Index, FdtStrerror (FdtStatus));
      Status = EFI_COMPROMISED_DATA;
      goto Exit;
    }

    FB_INFO ("overlay: dtbo entry %u (%u bytes)", Index, OverlaySize);
    Matches++;
  }

  FdtStatus = FdtPack (Work);
  if (FdtStatus < 0) {
    Status = EFI_DEVICE_ERROR;
    goto Exit;
  }

  OutSize = FdtTotalSize (Work);

  Status = gFb.Usb->AllocateTransferBuffer (gFb.Usb, ALIGN_VALUE (OutSize, SIZE_4KB), (VOID **)&Out);
  if (EFI_ERROR (Status)) {
    Out    = NULL;
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  ZeroMem (Out, ALIGN_VALUE (OutSize, SIZE_4KB));
  CopyMem (Out, Work, OutSize);

  FB_INFO ("merged rev %u: %u overlays, %u bytes", Rev, Matches, (UINT32)OutSize);

  *Merged     = Out;
  *MergedSize = OutSize;
  Status      = EFI_SUCCESS;

Exit:
  if (Work != NULL) {
    FreePages (Work, WorkPages);
  }

  DtTableFree (&Dtbo);
  DtTableFree (&Dt);

  return Status;
}
