/*
 * DECON Framebuffer Info Protocol
 *
 * This DXE driver deliberately does NOT publish EFI_GRAPHICS_OUTPUT_PROTOCOL
 * itself -- it only brings up DECON0/DSIM0/the S6E3HAB panel (exynos9830,
 * x1s target) and hands back the physical address/geometry of the single
 * framebuffer it created. A separate GOP driver is expected to consume
 * this protocol and do the actual EFI_GRAPHICS_OUTPUT_PROTOCOL Blt/mode
 * plumbing on top of this buffer.
 */
#ifndef __DECON_FRAMEBUFFER_INFO_H__
#define __DECON_FRAMEBUFFER_INFO_H__

#include <Uefi.h>

#define DECON_FRAMEBUFFER_INFO_PROTOCOL_GUID \
  { \
    0x8f5c7b1a, 0x2e6d, 0x4a3b, { 0x9c, 0x1d, 0x6a, 0x7e, 0x4f, 0x2b, 0x0c, 0x55 } \
  }

typedef enum {
  DeconPixelFormatBgra8888 = 0,  /* B,G,R,A byte order in memory, 32bpp */
} DECON_FRAMEBUFFER_PIXEL_FORMAT;

typedef struct _DECON_FRAMEBUFFER_INFO_PROTOCOL DECON_FRAMEBUFFER_INFO_PROTOCOL;

struct _DECON_FRAMEBUFFER_INFO_PROTOCOL {
  EFI_PHYSICAL_ADDRESS              FramebufferBase;
  UINT64                            FramebufferSize;   /* bytes */
  UINT32                            HorizontalResolution;
  UINT32                            VerticalResolution;
  UINT32                            PixelsPerScanLine;  /* == HorizontalResolution, no padding */
  DECON_FRAMEBUFFER_PIXEL_FORMAT    PixelFormat;
};

extern EFI_GUID  gDeconFramebufferInfoProtocolGuid;

#endif /* __DECON_FRAMEBUFFER_INFO_H__ */
