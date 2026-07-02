/*
 * Compatibility shim for LK's <malloc.h>. The ported decon_core.c /
 * dsim_drv.c / dpp_drv.c probe routines calloc() a single fixed-size
 * driver struct at probe time and never free the "happy path" object
 * (mirrors upstream lk3rd, which also never tears these down), so a
 * trivial UEFI pool-backed calloc/free pair is enough.
 */
#ifndef __COMPAT_MALLOC_H__
#define __COMPAT_MALLOC_H__

#include <sys/types.h>
#include <Library/MemoryAllocationLib.h>

static inline VOID *
compat_calloc (
  IN UINTN  Count,
  IN UINTN  Size
  )
{
  return AllocateZeroPool (Count * Size);
}

static inline VOID
compat_free (
  IN VOID  *Ptr
  )
{
  if (Ptr != NULL) {
    FreePool (Ptr);
  }
}

#define calloc(n, sz)  compat_calloc((n), (sz))
#define free(p)        compat_free((p))

#endif /* __COMPAT_MALLOC_H__ */
