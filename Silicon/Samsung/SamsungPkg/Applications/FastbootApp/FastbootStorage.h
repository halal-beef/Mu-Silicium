#include <Protocol/BlockIo.h>
#include <Protocol/EraseBlock.h>

#ifndef __FASTBOOT_STORAGE_H__
#define __FASTBOOT_STORAGE_H__

#define FB_IO_CHUNK            SIZE_1MB

// Sparse
#define SPARSE_HEADER_MAGIC    0xED26FF3A
#define CHUNK_TYPE_RAW         0xCAC1
#define CHUNK_TYPE_FILL        0xCAC2
#define CHUNK_TYPE_DONT_CARE   0xCAC3
#define CHUNK_TYPE_CRC32       0xCAC4

typedef VOID (*FB_PROGRESS)(VOID *Context, UINT64 Done, UINT64 Total);
typedef VOID (*FB_PARTITION_CALLBACK)(VOID *Context, CONST CHAR8 *Name, UINT64 Size);

typedef struct {
  EFI_BLOCK_IO_PROTOCOL    *BlockIo;
  EFI_ERASE_BLOCK_PROTOCOL *Erase;
  UINT64                    Size;
} FB_PARTITION;

typedef struct {
  FB_PARTITION *Partition;
  UINT8        *Bounce;
  UINTN         BouncePages;
  FB_PROGRESS   Progress;
  VOID         *Context;
} FB_IO;

// Sparse
#pragma pack(1)
typedef struct {
  UINT32 Magic;
  UINT16 MajorVersion;
  UINT16 MinorVersion;
  UINT16 FileHeaderSize;
  UINT16 ChunkHeaderSize;
  UINT32 BlockSize;
  UINT32 TotalBlocks;
  UINT32 TotalChunks;
  UINT32 ImageChecksum;
} SPARSE_HEADER;

typedef struct {
  UINT16 ChunkType;
  UINT16 Reserved;
  UINT32 ChunkSize;     // In Sparse Blocks
  UINT32 TotalSize;     // Header + Payload, in Bytes
} SPARSE_CHUNK_HEADER;
#pragma pack()

EFI_STATUS
FbFindPartition (
  IN  CONST CHAR8  *Name,
  OUT FB_PARTITION *Partition
  );

VOID
FbEnumeratePartitions (
  IN FB_PARTITION_CALLBACK Callback,
  IN VOID                 *Context
  );

EFI_STATUS
FbFlashImage (
  IN FB_PARTITION *Partition,
  IN VOID         *Image,
  IN UINTN         Size,
  IN FB_PROGRESS   Progress,
  IN VOID         *Context
  );

EFI_STATUS
FbErasePartition (
  IN FB_PARTITION *Partition,
  IN FB_PROGRESS   Progress,
  IN VOID         *Context
  );

#endif // __FASTBOOT_STORAGE_H__