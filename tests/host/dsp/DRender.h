/* Fake DigitalRenderer for host tests. */
#include "kernel.h"
#define DRState_Active 1
#define DRStream_OverrunNull 1
#define DRActivate_Restore 1
#define DRFORMAT_S16LR 0
extern int dr_state, dr_nbuf, dr_activations, dr_deactivations, dr_numbuf_calls, dr_streamed;
static inline const _kernel_oserror *DRender_LoadModule (const char *p) { (void)p; dr_state = 0; return 0; }
static inline int DRender_ReadState (void) { return dr_state; }
static inline const _kernel_oserror *DRender_Deactivate (void) { dr_deactivations++; dr_state = 0; return 0; }
static inline int DRender_NumBuffers (int n) { dr_numbuf_calls++; if (n) dr_nbuf = n; return dr_nbuf; }
static inline void DRender_StreamFlags (int a, int b) { (void)a; (void)b; }
static inline const _kernel_oserror *DRender_Activate16 (int c, int s, int f, int fl) { (void)c;(void)s;(void)f;(void)fl; dr_activations++; dr_state = 1; return 0; }
static inline const _kernel_oserror *DRender_Activate (int c, int s, int p, void *x) { (void)c;(void)s;(void)p;(void)x; dr_activations++; dr_state = 1; return 0; }
static inline int DRender_GetFrequency (void) { return 44100; }
static inline void DRender_SampleFormat (int f) { (void)f; }
static inline int DRender_StreamStatistics (void) { return 0; }
static inline const _kernel_oserror *DRender_Stream16BitSamples (const void *d, int n) { (void)d; dr_streamed += n; return 0; }
static inline const _kernel_oserror *DRender_StreamSamples (const void *d, int n) { (void)d; dr_streamed += n; return 0; }
