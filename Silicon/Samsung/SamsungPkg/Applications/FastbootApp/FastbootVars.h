#ifndef __FASTBOOT_VARS_H__
#define __FASTBOOT_VARS_H__

typedef BOOLEAN (*FB_VAR_GET)(
  IN  CONST CHAR8 *Arg OPTIONAL,
  OUT CHAR8       *Out,
  IN  UINTN        OutSize
  );

typedef struct {
  CONST CHAR8 *Name;
  BOOLEAN      InAll;      // Listed by "getvar:all" and "oem device-info"
  CONST CHAR8 *Value;      // Used when Get is NULL
  FB_VAR_GET   Get;
} FB_VAR;

BOOLEAN
FbGetVar (
  IN  CONST CHAR8 *Name,
  OUT CHAR8       *Out,
  IN  UINTN        OutSize
  );

VOID
FbSendAllVars (IN BOOLEAN IncludePartitions);

#endif // __FASTBOOT_VARS_H__