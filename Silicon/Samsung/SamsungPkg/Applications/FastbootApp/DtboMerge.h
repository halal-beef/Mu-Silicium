#ifndef __DTBO_MERGE_H__
#define __DTBO_MERGE_H__

//
// Builds the final DTB for a Hardware Revision from the "dt" Partition (Base DTBs)
// and the "dtbo" Partition (Overlays), both in Android DT Table Format.
//
// On Success *Merged is a Transfer Buffer (gFb.Usb->AllocateTransferBuffer), the Caller
// owns it (FbDropDownload frees it) and *MergedSize is the exact DTB Size.
//
EFI_STATUS
DtboMergeForRevision (
  IN  UINT32  Rev,
  OUT UINT8 **Merged,
  OUT UINTN  *MergedSize
  );

#endif /* __DTBO_MERGE_H__ */
