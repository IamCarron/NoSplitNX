// GPL-3.0. Android encounter and item data in the DS companion.
#pragma once
#include "companion_style.h"

struct DsGuideEncounter {
  uint8_t method;
  uint16_t species;
  uint8_t low,high,chance;
};
struct DsGuideHere {
  const DsGuideEncounter* entries=nullptr;
  int count=0;
};
struct DsGuideSpot {
  int map;
  DsGuideEncounter encounter;
  unsigned variants;
};
struct DsWildRates {const int32_t* triples=nullptr; int count=0;};
struct DsFlySpot {int header,x,z,flag,nameMap,region;};
DsWildRates ds_wild_rates(DsGame,int month,bool soulSilver);
const std::vector<DsFlySpot>& ds_fly_spots(DsGame);
bool ds_fly_unlocked(const DsCompanion&,const DsFlySpot&);
std::vector<DsGuideSpot> ds_guide_spots(DsGame,int species,bool soulSilver=false);
const char* ds_guide_method(int method);
