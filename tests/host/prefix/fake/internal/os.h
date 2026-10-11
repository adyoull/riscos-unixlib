/* Host test shim: DDEUtils_ReadPrefix and the OS variable fallback.  */
typedef struct { int errnum; char errmess[252]; } _kernel_oserror;
extern const _kernel_oserror *SWI_DDEUtils_ReadPrefix (int, const char **);
extern char *__getenv_from_os (const char *, char *, int, char **);
