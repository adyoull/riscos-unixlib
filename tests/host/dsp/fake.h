struct fake {
  int modules, open, opens, closes, paused, stall, rate, blocksize, sm_limit, blocks, yields, ints_toggles;
  unsigned volume; char name[64]; char *env_dsp;
  long long now_us, first_unpause_us, underruns_us;
  int added, played, max_queued; double frac;
  unsigned char *out; int out_len, out_cap;
};
extern struct fake F;
extern int dr_state, dr_nbuf, dr_activations, dr_deactivations, dr_numbuf_calls, dr_streamed;
void fake_reset (void);
int fake_yield (void);
