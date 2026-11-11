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

//
// libfdt's FdtAddSubnode places every new Node directly behind its Parent's Properties,
// which is in Front of the Parent's existing Children. Applying an Overlay therefore
// Reverses the Order of every Group of Nodes it adds (and of the Fragments themselves).
// Order is Significant for some Drivers (the Exynos7885 Sound Card matches the Children
// of /sound to its DAI Link Table by Position), so the Children inside each Fragment are
// Reversed once Up Front, which makes the Insertion Order come out as the Overlay Defines it.
// The Fragments themselves are not Touched, their Order decides which one Wins on Conflicts.
//
#define DT_TAG_BEGIN_NODE  0x1
#define DT_TAG_END_NODE    0x2
#define DT_TAG_PROP        0x3
#define DT_TAG_NOP         0x4
#define DT_MAX_DEPTH       64

STATIC
UINT32
DtTagAt (
  IN CONST UINT8 *Base,
  IN UINT32       Pos)
{
  return SwapBytes32 (*(CONST UINT32 *)(Base + Pos));
}

// Length of a BEGIN_NODE Tag incl. its padded Name, 0 if Malformed.
STATIC
UINT32
DtBeginLen (
  IN CONST UINT8 *Base,
  IN UINT32       Size,
  IN UINT32       Pos)
{
  UINT32  Len;

  if ((UINT64)Pos + 4 > Size) {
    return 0;
  }

  for (Len = 0; (UINT64)Pos + 4 + Len < Size && Base[Pos + 4 + Len] != 0; Len++) {
  }

  if ((UINT64)Pos + 4 + Len >= Size) {
    return 0;
  }

  Len = 4 + ALIGN_VALUE (Len + 1, 4);

  return ((UINT64)Pos + Len > Size) ? 0 : Len;
}

// Length of a PROP Tag incl. its Data, 0 if Malformed.
STATIC
UINT32
DtPropLen (
  IN CONST UINT8 *Base,
  IN UINT32       Size,
  IN UINT32       Pos)
{
  UINT64  Len;

  if ((UINT64)Pos + 12 > Size) {
    return 0;
  }

  Len = 12 + ALIGN_VALUE ((UINT64)SwapBytes32 (*(CONST UINT32 *)(Base + Pos + 4)), 4);

  return ((UINT64)Pos + Len > Size) ? 0 : (UINT32)Len;
}

// Skips one complete Node (BEGIN_NODE .. matching END_NODE) at *Pos, FALSE if Malformed.
STATIC
BOOLEAN
DtSkipNode (
  IN     CONST UINT8 *Base,
  IN     UINT32       Size,
  IN OUT UINT32      *Pos)
{
  UINT32  Scan  = *Pos;
  UINT32  Level = 0;
  UINT32  Tag;
  UINT32  Len;

  do {
    if ((UINT64)Scan + 4 > Size) {
      return FALSE;
    }

    Tag = DtTagAt (Base, Scan);

    if (Tag == DT_TAG_BEGIN_NODE) {
      Len = DtBeginLen (Base, Size, Scan);
      Level++;
    } else if (Tag == DT_TAG_PROP) {
      Len = DtPropLen (Base, Size, Scan);
    } else if (Tag == DT_TAG_END_NODE) {
      Len = 4;
      if (Level == 0) {
        return FALSE;
      }

      Level--;
    } else if (Tag == DT_TAG_NOP) {
      Len = 4;
    } else {
      return FALSE;
    }

    if (Len == 0) {
      return FALSE;
    }

    Scan += Len;
  } while (Level != 0);

  *Pos = Scan;

  return TRUE;
}

// Rewrites the Node at *Pos into Dst with the Children of every Node below the Root Reversed.
// The Output has exactly the Size of the Input. Advances *Pos behind the Node.
STATIC
BOOLEAN
DtReverseNode (
  IN     CONST UINT8 *Base,
  IN     UINT32       Size,
  IN OUT UINT32      *Pos,
  OUT    UINT8       *Dst,
  OUT    UINT32      *Written,
  IN     UINT32       Depth)
{
  UINT32   P = *Pos;
  UINT32   W;
  UINT32   Len;
  UINT32   Tag;
  UINT32   Count   = 0;
  UINT32   NopCnt  = 0;
  UINT32   Index;
  UINT32   Scan;
  UINT32  *Kids    = NULL;
  BOOLEAN  Result  = FALSE;

  if (Depth > DT_MAX_DEPTH) {
    return FALSE;
  }

  Len = DtBeginLen (Base, Size, P);
  if (Len == 0) {
    return FALSE;
  }

  CopyMem (Dst, Base + P, Len);
  W  = Len;
  P += Len;

  // Properties (and NOPs among them) stay in Place.
  for (;;) {
    if ((UINT64)P + 4 > Size) {
      return FALSE;
    }

    Tag = DtTagAt (Base, P);

    if (Tag == DT_TAG_NOP) {
      Len = 4;
    } else if (Tag == DT_TAG_PROP) {
      Len = DtPropLen (Base, Size, P);
      if (Len == 0) {
        return FALSE;
      }
    } else {
      break;
    }

    CopyMem (Dst + W, Base + P, Len);
    W += Len;
    P += Len;
  }

  // Pass 1: Count the Children, find the END_NODE of this Node.
  for (Scan = P; ; ) {
    if ((UINT64)Scan + 4 > Size) {
      return FALSE;
    }

    Tag = DtTagAt (Base, Scan);

    if (Tag == DT_TAG_NOP) {
      NopCnt++;
      Scan += 4;
    } else if (Tag == DT_TAG_BEGIN_NODE) {
      if (!DtSkipNode (Base, Size, &Scan)) {
        return FALSE;
      }

      Count++;
    } else if (Tag == DT_TAG_END_NODE) {
      break;
    } else {
      return FALSE;
    }
  }

  // Scan is now at this Node's END_NODE.
  if (Count != 0) {
    Kids = AllocatePool (Count * sizeof (UINT32));
    if (Kids == NULL) {
      return FALSE;
    }

    // Pass 2: Remember where each Child starts.
    Index = 0;
    for (Len = P; Len < Scan; ) {
      if (DtTagAt (Base, Len) == DT_TAG_NOP) {
        Len += 4;
      } else {
        Kids[Index++] = Len;
        DtSkipNode (Base, Size, &Len);
      }
    }

    // Emit Last to First. The Root's Children (the Fragments) keep their Order, they
    // have to be Applied as Written (a later Fragment overrides an earlier one).
    for (Index = 0; Index < Count; Index++) {
      UINT32  KidPos = Kids[(Depth == 0) ? Index : (Count - 1 - Index)];
      UINT32  Used   = 0;

      if (!DtReverseNode (Base, Size, &KidPos, Dst + W, &Used, Depth + 1)) {
        goto Done;
      }

      W += Used;
    }
  }

  // NOPs between Children are moved behind them, so the Size stays exact.
  for (Index = 0; Index < NopCnt; Index++) {
    *(UINT32 *)(Dst + W) = SwapBytes32 (DT_TAG_NOP);
    W += 4;
  }

  CopyMem (Dst + W, Base + Scan, 4);
  W += 4;

  *Pos     = Scan + 4;
  *Written = W;
  Result   = TRUE;

Done:
  if (Kids != NULL) {
    FreePool (Kids);
  }

  return Result;
}

// Reverses the Children of every Node below the Root in the Blob, in Place. The Blob keeps its Size
// and Layout, only the Order of the Nodes inside the Structure Block changes.
STATIC
EFI_STATUS
DtReverseChildren (IN OUT VOID *Fdt)
{
  UINT8    *Struct = (UINT8 *)Fdt + FdtOffsetDtStruct (Fdt);
  UINT32    Size   = FdtSizeDtStruct (Fdt);
  UINT32    Pos    = 0;
  UINT32    Start;
  UINT32    Used   = 0;
  UINT8    *Copy;
  UINTN     Pages;
  BOOLEAN   Ok;

  while ((UINT64)Pos + 4 <= Size && DtTagAt (Struct, Pos) == DT_TAG_NOP) {
    Pos += 4;
  }

  if ((UINT64)Pos + 4 > Size || DtTagAt (Struct, Pos) != DT_TAG_BEGIN_NODE) {
    return EFI_COMPROMISED_DATA;
  }

  Pages = EFI_SIZE_TO_PAGES (Size);
  Copy  = AllocatePages (Pages);
  if (Copy == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  ZeroMem (Copy, EFI_PAGES_TO_SIZE (Pages));

  Start = Pos;
  Ok    = DtReverseNode (Struct, Size, &Pos, Copy, &Used, 0) && Used == Pos - Start;

  // The Tail behind the Root (FDT_END) is Untouched.
  if (Ok) {
    CopyMem (Struct + Start, Copy, Used);
  }

  FreePages (Copy, Pages);

  return Ok ? EFI_SUCCESS : EFI_COMPROMISED_DATA;
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

    // Pre-Reverse the Overlay, FdtOverlayApply's Insertion Reverses it Back.
    Status = DtReverseChildren (Overlay);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "DtboMerge: Overlay %u: Child Reorder failed: %r\n", Index, Status));
      FreePages (Overlay, OverlayPages);
      Overlay = NULL;
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
