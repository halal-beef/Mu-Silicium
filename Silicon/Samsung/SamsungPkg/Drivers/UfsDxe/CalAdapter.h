#ifndef __CAL_ADAPTER_H__
#define __CAL_ADAPTER_H__

void ufs_lld_dme_set (void *h, UINT32 addr, UINT32 val);
void ufs_lld_dme_get (void *h, UINT32 addr, UINT32 *val);
void ufs_lld_dme_peer_set (void *h, UINT32 addr, UINT32 val);
void ufs_lld_pma_write (void *h, UINT32 val, UINT32 addr);
UINT32 ufs_lld_pma_read (void *h, UINT32 addr);
void ufs_lld_unipro_write (void *h, UINT32 val, UINT32 addr);

#endif /* __CAL_ADAPTER_H__ */