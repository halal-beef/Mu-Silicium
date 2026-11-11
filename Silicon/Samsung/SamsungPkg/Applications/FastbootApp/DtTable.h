/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * This is from the Android Project,
 * Repository: https://android.googlesource.com/platform/system/libufdt
 * File: utils/src/dt_table.h
 * Commit: 2626d8b9e4d8e8c6cc67ceb1dc4e05a47779785c
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Adapted to EDK2 types. All Fields are Stored Big Endian on Flash.
 */
#ifndef DT_TABLE_H
#define DT_TABLE_H

#include <Uefi.h>

#define DT_TABLE_MAGIC                0xd7b7ab1e
#define DT_TABLE_DEFAULT_PAGE_SIZE    2048
#define DT_TABLE_DEFAULT_VERSION      0

#pragma pack(1)
typedef struct {
  UINT32 Magic;            /* DT_TABLE_MAGIC */
  UINT32 TotalSize;        /* includes dt_table_header + all dt_table_entry
                            * and all dtb/dtbo
                            */
  UINT32 HeaderSize;       /* sizeof(dt_table_header) */
  UINT32 DtEntrySize;      /* sizeof(dt_table_entry) */
  UINT32 DtEntryCount;     /* number of dt_table_entry */
  UINT32 DtEntriesOffset;  /* offset to the first dt_table_entry
                            * from head of dt_table_header.
                            * The value will be equal to header_size if
                            * no padding is appended
                            */
  UINT32 PageSize;         /* flash page size we assume */
  UINT32 Version;          /* DTBO image version, the current version is 0.
                            * The version will be incremented when the
                            * dt_table_header struct is updated.
                            */
} DT_TABLE_HEADER;

typedef struct {
  UINT32 DtSize;
  UINT32 DtOffset;         /* offset from head of dt_table_header */
  UINT32 Id;               /* optional, must be zero if unused */
  UINT32 Rev;              /* optional, must be zero if unused */
  UINT32 Custom[4];        /* optional, must be zero if unused.
                            * Samsung: Custom[0] = min HW rev, Custom[1] = max HW rev (inclusive)
                            */
} DT_TABLE_ENTRY;
#pragma pack()

#endif
