/**
  Copyright (C) Samsung Electronics Co. LTD

  This software is proprietary of Samsung Electronics.
  No part of this software, either material or conceptual may be copied or distributed, transmitted,
  transcribed, stored in a retrieval system or translated into any human or computer language in any form by any means,
  electronic, mechanical, manual or otherwise, or disclosed
  to third parties without the express written permission of Samsung Electronics.
**/

#ifndef _EXYNOS_PMUCAL_LIB_H_
#define _EXYNOS_PMUCAL_LIB_H_

enum pmucal_seq_acctype {
        PMUCAL_READ = 0,
        PMUCAL_WRITE,
        PMUCAL_COND_READ,
        PMUCAL_COND_WRITE,
        PMUCAL_SAVE_RESTORE,
        PMUCAL_COND_SAVE_RESTORE,
        PMUCAL_WAIT,
        PMUCAL_WAIT_TWO,
        PMUCAL_CHECK_SKIP,
        PMUCAL_COND_CHECK_SKIP,
        PMUCAL_WRITE_WAIT,
        PMUCAL_WRITE_RETRY,
        PMUCAL_WRITE_RETRY_INV,
        PMUCAL_WRITE_RETURN,
        PMUCAL_SET_BIT_ATOMIC,
        PMUCAL_CLR_BIT_ATOMIC,
        PMUCAL_DELAY,
        PMUCAL_CLEAR_PEND,
};

/* represents each row in the PMU sequence guide */
struct pmucal_seq {
        UINT32 access_type;
        CONST CHAR8 *sfr_name;
        UINT32 base_pa;
        UINT32 mask;
        UINT32 value;
};

struct pmucal_pd {
	UINT32 id;
	CONST CHAR8 *name;
	struct pmucal_seq *on;
	struct pmucal_seq *save;
	struct pmucal_seq *off;
	struct pmucal_seq *status;
	UINT32 num_on;
	UINT32 num_save;
	UINT32 num_off;
	UINT32 num_status;
};

#define PMUCAL_SEQ_DESC(_access_type, _sfr_name, _base_pa, _offset,     \
                        _mask, _value, _cond_base_pa, _cond_offset,     \
                        _cond_mask, _cond_value) {                      \
        .access_type = _access_type,                                    \
        .sfr_name = _sfr_name,                                          \
        .base_pa = _base_pa | _offset,                                          \
        .mask = _mask,                                                  \
        .value = _value,                                                \
}

#define PMUCAL_PD_DESC(_pd_id, _name, _on, _save, _off, _status)	\
	[_pd_id] = {							\
		.id = _pd_id,						\
		.name = _name,						\
		.on = _on,						\
		.save = _save,						\
		.off = _off,						\
		.status = _status,					\
		.num_on = ARRAY_SIZE(_on),				\
		.num_save = ARRAY_SIZE(_save),				\
		.num_off = ARRAY_SIZE(_off),				\
		.num_status = ARRAY_SIZE(_status),			\
	}

#define PMUCAL_PREFIX                   "PMUCAL:  "

/*STATIC
BOOLEAN
PMUCalRaeCheckValue (struct pmucal_seq *seq);

STATIC
INT32
PMUCalRaeWait (struct pmucal_seq *seq);

STATIC 
VOID PMUCalRaeWrite (struct pmucal_seq *seq);*/

INT32
PMUCalRaeHandleSeq
(
struct pmucal_seq *seq,
UINT32 seq_size
);

#endif /* _EXYNOS_PMUCAL_LIB_H_ */
