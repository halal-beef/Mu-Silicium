#ifndef __DTBO_MERGE_H__
#define __DTBO_MERGE_H__

#define DT_PARTITION_NAME    "dtb"
#define DT_OVERLAY_PARTITION "dtbo"

#define DT_MERGE_SLACK       SIZE_64KB

#define DT_TAG_BEGIN_NODE  0x1
#define DT_TAG_END_NODE    0x2
#define DT_TAG_PROP        0x3
#define DT_TAG_NOP         0x4
#define DT_MAX_DEPTH       64

typedef struct {
  UINT8  *Data;       // Page Aligned
  UINTN   Pages;
  UINT32  Size;       // TotalSize from Header
  UINT32  Count;
  UINT32  EntriesOffset;
  UINT32  EntrySize;
} DT_TABLE;

EFI_STATUS
DtboMergeForRevision (
  IN  UINT32  Rev,
  OUT UINT8 **Merged,
  OUT UINTN  *MergedSize
  );

#endif /* __DTBO_MERGE_H__ */
