/* WILD ENCOUNTERS' switch in a ROM (wild.c). GPL-3.0. */
#ifndef POCKET_WILD_H
#define POCKET_WILD_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* sWildEncountersDisabled's EWRAM address in rom (its bytes, size long), or 0 when its code isn't a known shape. */
uint32_t pk_wild_switch_find(const uint8_t *rom, size_t size);
#endif
