/*
 * Compatibility shim: maps the lk3rd bootloader's libc-style integer/bool
 * typedefs onto EDK2's Base.h types so the ported DPU/DSIM/DPP CAL code
 * (originally written against LK's sys/types.h) compiles unmodified under
 * EDK2.
 */
#ifndef __COMPAT_SYS_TYPES_H__
#define __COMPAT_SYS_TYPES_H__

#include <Base.h>

typedef UINT8   u8;
typedef UINT16  u16;
typedef UINT32  u32;
typedef UINT64  u64;

typedef INT8    s8;
typedef INT16   s16;
typedef INT32   s32;
typedef INT64   s64;

typedef UINT8   uint8_t;
typedef UINT16  uint16_t;
typedef UINT32  uint32_t;
typedef UINT64  uint64_t;

typedef UINTN   size_t;
typedef UINTN   uintptr_t;

#ifndef __cplusplus
typedef UINT8   bool;
#define true    1
#define false   0
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x)  (sizeof(x) / sizeof((x)[0]))
#endif

#endif /* __COMPAT_SYS_TYPES_H__ */
