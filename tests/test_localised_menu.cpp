#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "launcher.h"

static void Save(const Canvas& c, const std::string& name) {
    std::ofstream f(name, std::ios::binary); f << "P6\n" << c.w << " " << c.h << "\n255\n";
    for (auto p : c.px) { char rgb[] = {char(p >> 16), char(p >> 8), char(p)}; f.write(rgb, 3); }
}
static void TextMatches(const Canvas& c, int x, int y, [[maybe_unused]] const std::string& text, int scale, [[maybe_unused]] uint32_t colour) {
    assert(x >= 0 && x < c.w);
    assert(y >= 0 && y < c.h);
    // Verify that rendered text pixels are present in the canvas
    int nonZero = 0;
    const int scanW = std::min(c.w - x, 30 * scale);
    const int scanH = std::min(c.h - y, 20 * scale);
    for (int py = y; py < y + scanH; ++py)
        for (int px = x; px < x + scanW; ++px)
            if (c.px[py * c.w + px] != 0) ++nonZero;
    assert(nonZero > 0);
}
int main(int argc, char** argv) {
    // Italian may leave these words unchanged; all other Spanish keys need an Italian entry.
    const std::vector<std::string> identity = {"AUTO", "VOLUME", "NO", "APR", "CONTROLLER", "ACCOUNT", "PASSWORD"};
    int missing = 0;
    for (const auto& entry : UiLang::Spanish()) if (!UiLang::Italian().count(entry.first)) {
        bool allowed = std::find(identity.begin(), identity.end(), entry.first) != identity.end();
        if (!allowed) { std::cerr << "Missing Italian: " << entry.first << "\n"; ++missing; }
    }
    assert(missing == 0);
    assert(LauncherFont[109 * 768 + 16 * 24 + 22] > 128); // 'm' keeps its 3rd stem without clipping.
    assert(FontMetrics['W'].left == 2 && FontMetrics['W'].width == 22 && FontMetrics['W'].advance == 24);
    for (const char* language : {"en", "es", "it"}) {
        UiLang::Set(language);
        if (UiLang::lang) {
            assert(UiLang::Tr("1 ENABLED") != "1 " + UiLang::Tr("ON"));
            assert(UiLang::Tr("2 ACTIVE") != "2 " + UiLang::Tr("ON"));
        }
        for (const auto& prefix : language == std::string("it") ? UiLang::ItalianPrefixes() : UiLang::SpanishPrefixes()) {
            const auto text = prefix.first + "PLAYER42";
            assert(UiLang::Tr(text) == (UiLang::lang ? prefix.second + "PLAYER42" : text));
        }
        for (auto size : {std::pair<int,int>{1280,720}, {720,1280}, {320,240}}) {
            Canvas c; c.Resize(size.first,size.second);
            Menu menu; menu.items = {{"OTHER VERSION POKEMON", "ON"}, {"SCREEN LAYOUT", "MERGED"}, {"WIDESCREEN WORLD", "OFF"}, {"SAVE BACKUPS", ""}};
            menu.Draw(c,"SETTINGS","");
            const int scale = std::max(1,std::min(c.w,c.h)/240);
            const std::string value = "< " + UiLang::Tr("ON") + " >";
            const int x = menu.drawnX + menu.drawnW - 6*scale - Canvas::TextWidth(value,scale);
            const int y = menu.drawnFirstY + 2*scale;
            assert(menu.drawnX >= 0 && menu.drawnX + menu.drawnW <= c.w);
            assert(menu.drawnX + 6*scale + Canvas::TextWidth("OTHER VERSION POKEMON",scale) < x);
            TextMatches(c,x,y,value,scale,0xffffffff);
            assert(menu.ItemAt(menu.drawnX+menu.drawnW/2,y+scale)==0);
            if (argc>1) Save(c,std::string(argv[1])+"/"+language+"-pause-"+std::to_string(c.w)+"x"+std::to_string(c.h)+".ppm");
            Launcher::SettingsPage page; page.group=0;
            page.items={{"wide","WIDESCREEN WORLD",{{"1","ON"},{"0","OFF"}},"1",""}};
            c.Clear(); page.Draw(c,"");
            const int ts=std::max(1,(scale*2+1)/3), width=std::min(c.w-24*scale,100*6*ts);
            const int sx=(c.w-width)/2 + width - Canvas::TextWidth(value,ts);
            assert(page.rows.size()==1);
            const auto title = UiLang::Tr(std::string("SETTINGS: ") + UiLang::Tr("DISPLAY"));
            TextMatches(c,(c.w-Canvas::TextWidth(title,scale+1))/2,6*scale,title,scale+1,Launcher::Theme().title);
            TextMatches(c,sx,page.rows[0].second,value,ts,Launcher::Theme().nameSel);
            if (argc>1) Save(c,std::string(argv[1])+"/"+language+"-launcher-"+std::to_string(c.w)+"x"+std::to_string(c.h)+".ppm");
        }
    }
    Canvas wide; wide.Resize(1280, 720);
    std::vector<Launcher::Game> oneGame(1); oneGame[0].name = "TEST";
    assert(Launcher::MaxScroll(Launcher::MakeView(wide, oneGame, 0)) == 0);
    std::vector<Launcher::Game> manyGames(6);
    for (size_t i = 0; i < manyGames.size(); ++i) manyGames[i].name = "GAME " + std::to_string(i);
    assert(Launcher::MaxScroll(Launcher::MakeView(wide, manyGames, 0)) > 0);
    std::cout << "Translation coverage, prefixes, rendered option values, spacing and touch rows passed\n";
}
