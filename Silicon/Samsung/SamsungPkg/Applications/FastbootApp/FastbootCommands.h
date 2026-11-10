#ifndef __FASTBOOT_COMMANDS_H__
#define __FASTBOOT_COMMANDS_H__

typedef VOID (*FB_COMMAND_HANDLER)(IN CONST CHAR8 *Arg);

typedef struct {
  CONST CHAR8        *Prefix;
  BOOLEAN             Exact;     // TRUE: whole command must match, no argument
  FB_COMMAND_HANDLER  Handler;
} FB_COMMAND;

typedef struct {
  UINT64 LastReport;
} FB_PROGRESS_STATE;

VOID
FbHandleRx (IN UINTN Bytes);

VOID
FbDropDownload (VOID);

#endif /* __FASTBOOT_COMMANDS_H__ */