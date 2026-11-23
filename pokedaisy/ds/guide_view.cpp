// GPL-3.0. Encounter, habitat, and Fly-spot tables for DS companions.
#include "guide_view.h"
#include <algorithm>
#include <array>
#include <cstring>

namespace ds_guide_data {
using Encounter=DsGuideEncounter;
struct Location {int key,offset,count;};
struct AreaItem {int place; bool hidden; int item,flag,qty; const char* where;};
struct HeaderPlace {int map,place;};
struct GuideTip {const char* section;const char* title;const char* hint;const char* answer;int item,flag;const char* areas;};
struct BossMon {int level,species,item;std::array<int,4> moves;};
struct BossTeam {
  const char *title,*where,*variant;
  int done,doneAfter,rematchFlag,offset,count,rematchOffset,rematchCount;
};
}
#include "guide_gen.h"

namespace {
using namespace ds_guide_data;
template<size_t N>
constexpr DsWildRates rates(const int32_t (&data)[N]){
  static_assert(N%3==0);
  return {data,int(N/3)};
}
template<size_t N>
std::vector<DsGuideSpot> spots(const Location (&locations)[N],const Encounter* entries,DsGame game,int species){
  std::vector<DsGuideSpot> result;
  for(const auto& loc:locations){
    const int map=loc.key/4;
    const unsigned variant=1u<<(loc.key%4);
    for(int i=0;i<loc.count;++i){
      const auto& e=entries[loc.offset+i];
      if(e.species!=species)continue;
      const auto* name=ds_place(map,game);
      const auto match=std::find_if(result.begin(),result.end(),[&](const DsGuideSpot& spot){
        const auto& other=spot.encounter;
        const bool samePlace=spot.map==map || (*name!='?' && std::strcmp(name,ds_place(spot.map,game))==0);
        return samePlace && other.method==e.method && other.low==e.low && other.high==e.high && other.chance==e.chance;
      });
      if(match==result.end())result.push_back({map,e,variant});
      else match->variants|=variant;
    }
  }
  return result;
}
}

DsWildRates ds_wild_rates(DsGame game,int month,bool soulSilver){
  using namespace ds_guide_data;
  const int season=(std::clamp(month,1,12)-1)%4;
  switch(game){
    case DsGame::Platinum:return rates(ptRates0);
    case DsGame::Johto:return soulSilver?rates(ssRates0):rates(hgRates0);
    case DsGame::Black:{
      constexpr DsWildRates seasons[]={rates(blackRates0),rates(blackRates1),rates(blackRates2),rates(blackRates3)};
      return seasons[season];
    }
    case DsGame::White:{
      constexpr DsWildRates seasons[]={rates(whiteRates0),rates(whiteRates1),rates(whiteRates2),rates(whiteRates3)};
      return seasons[season];
    }
    case DsGame::Black2:{
      constexpr DsWildRates seasons[]={rates(black2Rates0),rates(black2Rates1),rates(black2Rates2),rates(black2Rates3)};
      return seasons[season];
    }
    case DsGame::White2:{
      constexpr DsWildRates seasons[]={rates(white2Rates0),rates(white2Rates1),rates(white2Rates2),rates(white2Rates3)};
      return seasons[season];
    }
    default:return {};
  }
}

const std::vector<DsFlySpot>& ds_fly_spots(DsGame game){
  using namespace ds_guide_data;
  static const std::vector<DsFlySpot> platinum(std::begin(platinumFly),std::end(platinumFly));
  static const std::vector<DsFlySpot> johto(std::begin(heartgoldFly),std::end(heartgoldFly));
  static const std::vector<DsFlySpot> black(std::begin(blackFly),std::end(blackFly));
  static const std::vector<DsFlySpot> white(std::begin(whiteFly),std::end(whiteFly));
  static const std::vector<DsFlySpot> white2(std::begin(white2Fly),std::end(white2Fly));
  static const std::vector<DsFlySpot> empty;
  switch(game){
    case DsGame::Platinum:return platinum;
    case DsGame::Johto:return johto;
    case DsGame::Black:return black;
    case DsGame::White:return white;
    case DsGame::White2:return white2;
    default:return empty;
  }
}

bool ds_fly_unlocked(const DsCompanion& s,const DsFlySpot& spot){
  if(spot.flag<0 || !ds_event_flag(s,spot.flag))return false;
  if(s.game!=DsGame::Johto || spot.region==2 || s.mapX<0 || s.mapY<2)return true;
  // Android's Pokégear region rule uses the cartridge Y, two cells above the rendered map Y.
  const int x=s.mapX,y=s.mapY-2;
  const int here=x<=21 || (x==25 && y==8)?0:x==28 && (y==6 || (y>=9 && y<=12))?2:1;
  return here==2 || spot.region==here;
}

std::vector<DsGuideSpot> ds_guide_spots(DsGame game,int species,bool soulSilver){
  using namespace ds_guide_data;
  switch(game){
    case DsGame::Platinum:return spots(ptLocations,ptEncounters,game,species);
    case DsGame::Johto:return soulSilver?spots(ssLocations,ssEncounters,game,species):spots(hgLocations,hgEncounters,game,species);
    case DsGame::Black:return spots(blackLocations,blackEncounters,game,species);
    case DsGame::White:return spots(whiteLocations,whiteEncounters,game,species);
    case DsGame::Black2:return spots(black2Locations,black2Encounters,game,species);
    case DsGame::White2:return spots(white2Locations,white2Encounters,game,species);
    default:return {};
  }
}

const char* ds_guide_method(int method){
  static constexpr const char* methods[]={"LAND","DARK GRASS","RUSTLING GRASS","SURF","RIPPLING WATER",
    "SUPER ROD","RIPPLING FISHING","OLD ROD","GOOD ROD","ROCK SMASH"};
  return method>=0 && method<int(sizeof methods/sizeof *methods)?methods[method]:"";
}
