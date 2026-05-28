#include <Library/IoLib.h>

#include "UfsDxe.h"

// vscode
#include <Uefi/UefiSpec.h>

void ufs_lld_dme_set (void *h, UINT32 addr, UINT32 val)
{
  struct UfsHost     *Ufs = (struct UfsHost *)h;
  struct UfsUicCmd   cmd = {UIC_CMD_DME_SET, 0, 0, 0};
  cmd.Arg1 = addr;
  cmd.Arg3 = val;
  Ufs->UicCmd = &cmd;
  UfsSendUicCmd (Ufs);
}

void ufs_lld_dme_get (void *h, UINT32 addr, UINT32 *val)
{
  struct UfsHost     *Ufs = (struct UfsHost *)h;
  struct UfsUicCmd   cmd = {UIC_CMD_DME_GET, 0, 0, 0};
  cmd.Arg1 = addr;
  Ufs->UicCmd = &cmd;
  UfsSendUicCmd (Ufs);
  *val = cmd.Arg3;
}

void ufs_lld_dme_peer_set (void *h, UINT32 addr, UINT32 val)
{
  struct UfsHost     *Ufs = (struct UfsHost *)h;
  struct UfsUicCmd   cmd = {UIC_CMD_DME_PEER_SET, 0, 0, 0};
  cmd.Arg1 = addr;
  cmd.Arg3 = val;
  Ufs->UicCmd = &cmd;
  UfsSendUicCmd (Ufs);
}

void ufs_lld_pma_write (void *h, UINT32 val, UINT32 addr)
{
  struct UfsHost *Ufs = (struct UfsHost *)h;
  MmioWrite32((UINTN)(Ufs->PhyPma + addr), val);
}

UINT32 ufs_lld_pma_read (void *h, UINT32 addr)
{
  struct UfsHost *Ufs = (struct UfsHost *)h;
  return MmioRead32((UINTN)(Ufs->PhyPma + addr));
}

void ufs_lld_unipro_write (void *h, UINT32 val, UINT32 addr)
{
  struct UfsHost *Ufs = (struct UfsHost *)h;
  MmioWrite32((UINTN)(Ufs->UniProAddr + addr), val);
}