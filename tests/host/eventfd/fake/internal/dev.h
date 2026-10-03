/* Host test shim (the real one brings in fd_set).  */
#include <sys/select.h>
#define DEV_EVENTFD 99
