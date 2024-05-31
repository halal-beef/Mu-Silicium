#include <Uefi.h>

#include <Library/BaseMemoryLib.h>
#include <Library/BitmapLib.h>
#include <Library/KeypadDeviceLib.h>
#include <Library/TimerLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/PMUCalLib.h>

STATIC UINT32 pmucal_rae_seq_idx;

STATIC
BOOLEAN
PMUCalRaeCheckValue (struct pmucal_seq *seq)
{
	UINT32 reg;

	if (seq->access_type == PMUCAL_WRITE_WAIT)
		reg = MmioRead32(seq->base_pa + 0x4);
	else
		reg = MmioRead32(seq->base_pa);
	reg &= seq->mask;
	if (reg == seq->value)
		return TRUE;
	else
		return FALSE;
}

STATIC
INT32
PMUCalRaeWait (struct pmucal_seq *seq)
{
	UINT32 timeout = 0;

	while (1) {
		if (PMUCalRaeCheckValue(seq))
			break;
		timeout++;
		MicroSecondDelay(1000);
		if (timeout > 10) {
			UINT32 reg;
			reg = MmioRead32(seq->base_pa);
			DEBUG((EFI_D_ERROR, "%s %s:timed out during wait. (value:0x%x, seq_idx = %d)\n",
						PMUCAL_PREFIX, __func__, reg, pmucal_rae_seq_idx));
			return -2;
		}
	}

	return 0;
}

STATIC 
VOID PMUCalRaeWrite (struct pmucal_seq *seq)
{
	if (seq->mask == 0xFFFFFFFF)
		MmioWrite32(seq->base_pa, seq->value);
	else {
		UINT32 reg;
		reg = MmioRead32(seq->base_pa);
		reg = (reg & ~seq->mask) | (seq->value & seq->mask);
		MmioWrite32(seq->base_pa, reg);
	}
}

INT32
PMUCalRaeHandleSeq
(
struct pmucal_seq *seq,
UINT32 seq_size
)
{
	INT32 ret;
	UINT32 i;

	for (i = 0; i < seq_size; i++) {
		pmucal_rae_seq_idx = i;

		switch (seq[i].access_type) {
		case PMUCAL_WRITE:
			PMUCalRaeWrite(&seq[i]);
			break;
		case PMUCAL_WAIT:
		case PMUCAL_WAIT_TWO:
			ret = PMUCalRaeWait(&seq[i]);
			if (ret)
				return ret;
			break;
		case PMUCAL_WRITE_WAIT:
			PMUCalRaeWrite(&seq[i]);
			ret = PMUCalRaeWait(&seq[i]);
			if (ret)
				return ret;
			break;
		case PMUCAL_WRITE_RETURN:
			PMUCalRaeWrite(&seq[i]);
			return 0;
		case PMUCAL_DELAY:
			MicroSecondDelay(seq[i].value * 1000);
			break;
		default:
			DEBUG((EFI_D_ERROR,"%s %s:invalid PMUCAL access type\n", PMUCAL_PREFIX, __func__));
			return -1;
		}
	}

	return 0;
}
