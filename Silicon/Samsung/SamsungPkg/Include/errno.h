/*
 * Compatibility shim for LK's <errno.h>. Only the codes actually
 * referenced by the ported CAL/driver sources are defined; values are
 * arbitrary (only used as plain int return codes, never passed to a
 * real libc), but match the traditional POSIX numbering for clarity.
 */
#ifndef __COMPAT_ERRNO_H__
#define __COMPAT_ERRNO_H__

#define EPERM        1
#define EIO          5
#define ENXIO        6
#define ENOMEM      12
#define EBUSY       16
#define EINVAL      22
#define ERANGE      34
#define ETIMEDOUT  110
#define EFAULT      14

#endif /* __COMPAT_ERRNO_H__ */
