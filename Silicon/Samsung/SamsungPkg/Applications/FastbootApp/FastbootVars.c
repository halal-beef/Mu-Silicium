#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/PcdLib.h>
#include <Library/PrintLib.h>

#include "FastbootApp.h"
#include "FastbootStorage.h"
#include "FastbootUsb.h"
#include "FastbootVars.h"

STATIC
BOOLEAN
GetProduct (
  IN  CONST CHAR8 *Arg OPTIONAL,
  OUT CHAR8       *Out,
  IN  UINTN        OutSize)
{
  AsciiSPrint (Out, OutSize, "%a", (CONST CHAR8 *)FixedPcdGetPtr (PcdSmbiosSystemRetailModel));

  return TRUE;
}

STATIC
BOOLEAN
GetVariant (
  IN  CONST CHAR8 *Arg OPTIONAL,
  OUT CHAR8       *Out,
  IN  UINTN        OutSize)
{
  AsciiSPrint (Out, OutSize, "%a", (CONST CHAR8 *)FixedPcdGetPtr (PcdSmbiosSystemModel));

  return TRUE;
}

STATIC
BOOLEAN
GetSerial (
  IN  CONST CHAR8 *Arg OPTIONAL,
  OUT CHAR8       *Out,
  IN  UINTN        OutSize)
{
  AsciiSPrint (Out, OutSize, "%a", gFb.Serial);

  return TRUE;
}

STATIC
BOOLEAN
GetMaxDownloadSize (
  IN  CONST CHAR8 *Arg OPTIONAL,
  OUT CHAR8       *Out,
  IN  UINTN        OutSize)
{
  AsciiSPrint (Out, OutSize, "0x%08x", FixedPcdGet32 (PcdFastbootMaxDownloadSize));

  return TRUE;
}

STATIC
BOOLEAN
GetPartitionSize (
  IN  CONST CHAR8 *Arg OPTIONAL,
  OUT CHAR8       *Out,
  IN  UINTN        OutSize)
{
  FB_PARTITION Partition;

  if (Arg == NULL || EFI_ERROR (FbFindPartition (Arg, &Partition))) {
    return FALSE;
  }

  AsciiSPrint (Out, OutSize, "0x%lx", Partition.Size);

  return TRUE;
}

STATIC
BOOLEAN
PartitionExists (IN CONST CHAR8 *Arg OPTIONAL)
{
  FB_PARTITION Partition;

  return (Arg != NULL && !EFI_ERROR (FbFindPartition (Arg, &Partition)));
}

STATIC
BOOLEAN
GetPartitionType (
  IN  CONST CHAR8 *Arg OPTIONAL,
  OUT CHAR8       *Out,
  IN  UINTN        OutSize)
{
  if (!PartitionExists (Arg)) {
    return FALSE;
  }

  AsciiSPrint (Out, OutSize, "raw");

  return TRUE;
}

STATIC
BOOLEAN
GetHasSlot (
  IN  CONST CHAR8 *Arg OPTIONAL,
  OUT CHAR8       *Out,
  IN  UINTN        OutSize)
{
  if (!PartitionExists (Arg)) {
    return FALSE;
  }

  AsciiSPrint (Out, OutSize, "no");

  return TRUE;
}

STATIC CONST FB_VAR  mVars[] = {
  // Name                  InAll  Value              Get
  { "version",             TRUE,  FB_VERSION,        NULL               },
  { "version-bootloader",  TRUE,  FB_APP_VERSION,    NULL               },
  { "product",             TRUE,  NULL,              GetProduct         },
  { "variant",             TRUE,  NULL,              GetVariant         },
  { "serialno",            TRUE,  NULL,              GetSerial          },
  { "max-download-size",   TRUE,  NULL,              GetMaxDownloadSize },
  { "secure",              TRUE,  "no",              NULL               },
  { "unlocked",            TRUE,  "yes",             NULL               },
  { "is-userspace",        TRUE,  "no",              NULL               },
  { "slot-count",          TRUE,  "0",               NULL               },
  { "kernel",              TRUE,  "uefi",            NULL               },
  { "partition-size",      FALSE, NULL,              GetPartitionSize   },
  { "partition-type",      FALSE, NULL,              GetPartitionType   },
  { "has-slot",            FALSE, NULL,              GetHasSlot         },
};

STATIC
BOOLEAN
FbReadVar (
  IN  CONST FB_VAR *Var,
  IN  CONST CHAR8  *Arg OPTIONAL,
  OUT CHAR8        *Out,
  IN  UINTN         OutSize)
{
  if (Var->Get != NULL) {
    return Var->Get (Arg, Out, OutSize);
  }

  AsciiSPrint (Out, OutSize, "%a", Var->Value);

  return TRUE;
}

BOOLEAN
FbGetVar (
  IN  CONST CHAR8 *Name,
  OUT CHAR8       *Out,
  IN  UINTN        OutSize)
{
  CONST CHAR8 *Arg = NULL;
  UINTN        NameLength;
  UINTN        Index;

  for (NameLength = 0; Name[NameLength] != 0 && Name[NameLength] != ':'; NameLength++) {
  }

  if (Name[NameLength] == ':') {
    Arg = Name + NameLength + 1;
  }

  for (Index = 0; Index < ARRAY_SIZE (mVars); Index++) {
    if (AsciiStrnCmp (mVars[Index].Name, Name, NameLength) == 0 && mVars[Index].Name[NameLength] == 0) {
      return FbReadVar (&mVars[Index], Arg, Out, OutSize);
    }
  }

  return FALSE;
}

STATIC
VOID
FbSendPartitionSize (
  IN VOID        *Context,
  IN CONST CHAR8 *Name,
  IN UINT64       Size)
{
  FB_INFO ("partition-size:%a: 0x%lx", Name, Size);
}

VOID
FbSendAllVars (IN BOOLEAN IncludePartitions)
{
  CHAR8 Value[64];
  UINTN Index;

  for (Index = 0; Index < ARRAY_SIZE (mVars); Index++) {
    if (mVars[Index].InAll && FbReadVar (&mVars[Index], NULL, Value, sizeof (Value))) {
      FB_INFO ("%a: %a", mVars[Index].Name, Value);
    }
  }

  if (IncludePartitions) {
    FbEnumeratePartitions (FbSendPartitionSize, NULL);
  }
}
