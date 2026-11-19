// FullDex plays no Game Boy games: pk_wide.c's Game Boy half (PokeDaisy's pk_wide_gb.c) answers that it knows none.
// Based on PokeDaisy by Lidor Itzhari - https://github.com/lidor30/pokedaisy (GNU GPL version 3).
#include "pk_wide.h"

int pk_wide_gb_knows(uint32_t crc) { (void) crc; return 0; }
int pk_wide_gb_configure(uint32_t crc) { (void) crc; return 0; }
void pk_wide_gb_reset(void) {}
void pk_wide_gb_snap(void) {}
int pk_wide_gb_view(struct mCore* core, const uint32_t* frame, uint32_t* out, struct PkWideView view, uint32_t* centre)
{ (void) core; (void) frame; (void) out; (void) view; (void) centre; return 0; }
int pk_wide_gb_glow(struct mCore* core, const uint32_t* frame, uint32_t* out, struct PkWideView view)
{ (void) core; (void) frame; (void) out; (void) view; return 0; }
int pk_wide_gb_scene(void) { return PK_WIDE_SCENE_OFF; }
int pk_wide_gb_wants_close(void) { return 0; }
int pk_wide_gb_last_mode(void) { return 0; }
int pk_wide_gb_last_score(void) { return 0; }
int pk_wide_gb_last_torn(void) { return 0; }
