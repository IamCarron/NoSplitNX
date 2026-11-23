// Pokemon Black and White (US/Europe, IRBO and IRAO): their own screen layouts, on top of the
// Black 2 ones.
//
// Black runs like Black 2 (melonDS's JIT, layouts that follow the game's own functions), with
// a function table made by tools/bw1/funcmap.py from the aletheia decompilation. The functions
// the Black 2 layouts watch carry their Black 2 names in that table (tools/bw1/aliases.txt), so
// pickSceneBw2 in main.cpp picks the scenes for Black too. Where Black's screens are drawn
// differently, Bw1Layouts::Adjust changes the scene it picked (by the scene's name), and a few
// screens Black 2 does not have get scenes of their own (by Black's function names). For any
// other game it does nothing. White is the same program as Black, its functions a few bytes
// apart (tools/bw1/funcmap.py --white): everything here is for both.
//
// Hooked into main.cpp in two places: Init before the function watch is attached (so Black's own
// functions are watched too) and Adjust on the scene pickSceneBw2 chose.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "gamecode.h"
#include <vector>

#include "funcwatch.h"
#include "gen5strip.h"
#include "merged.h"

class Bw1Layouts
{
public:
    bool on = false;
    bool regionalMenu = false;
    // The top screen darkened or whitened this frame (set before Adjust): the wide arena would
    // show at full brightness through holes cut in it, so the HP boxes are not kept then.
    bool topFaded = false;

    // The battle's buttons on a wide view (BATTLE BUTTONS in the pause menu;
    // ssrecomp.cfg key battle_buttons=strip|modern):
    //   STRIP:  authentic BW/B2W2 touch-screen plates mutually co-sized with the prompt;
    //   MODERN: live PokeDaisy battle strip across Gen 4 and Gen 5, falling back to STRIP.
    enum { BUTTONS_STRIP = 1, BUTTONS_MODERN = 5 };
    int battleButtons = BUTTONS_STRIP;

    // romPath: the game being started; names: the functions the layouts watch (Black's own
    // are added to it when the game is Black); configPath: ssrecomp.cfg, for battle_buttons
    // until the shared settings know it.
    void Init(const std::string& romPath, const FuncWatch& fw, std::vector<std::string>& names,
        const std::string& configPath = "")
    {
        if (FILE* f = configPath.empty() ? nullptr : fopen(configPath.c_str(), "r"))
        {
            char line[128];
            while (fgets(line, sizeof(line), f))
                if (!strncmp(line, "battle_buttons=", 15))
                    battleButtons = (!strncmp(line + 15, "modern", 6) || !strncmp(line + 15, "pokedaisy", 9))
                        ? BUTTONS_MODERN : BUTTONS_STRIP;
            fclose(f);
        }
        char code[5] = {};
        if (FILE* f = fopen(romPath.c_str(), "rb"))
        {
            if (fseek(f, 0x0C, SEEK_SET) != 0 || fread(code, 1, 4, f) != 4) code[0] = 0;
            fclose(f);
        }
        on = IsGame(code, "IRBO") || IsGame(code, "IRAO");
        regionalMenu = RegionalProfiles::Find(code) != nullptr || std::string(code) == "IRBS"; // (Spanish Black: as when it had a profile)
        if (!on) return;
        Fw = &fw;
        auto find = [&](const char* name) {
            names.push_back(name);
            return fw.Find(name);
        };
        // The trainer card's other side, the Badge Case (gym leaders' portraits and the badges,
        // all on the touch screen; the top screen only has the League's emblem), and editing the
        // card (the signature): TRCARD_SYS runs one of these every frame while its page is up.
        badges = find("TRCARD_SYS_StateWaitBadgeProc");
        cardEdit = find("TRCARD_SYS_StateWaitEditProc");
        // SAVE in the X menu: the question, the save's card and YES / NO are all on the touch
        // screen (Black 2 asks on the top screen).
        report = find("REPORT_EVENT_Main");
        // A Pokemon registered in the Pokedex (caught, hatched, received): its page on the top
        // screen; the other only holds a dimmed Poke Ball, until the battle's touch screen
        // asks YES / NO ("Give a nickname?"), which is polled by BTLV_INPUT_ProcessTouchInput.
        dexRegister = find("ZUKAN_TOROKU_GRAPHIC_Main");
        touchInput = find("BTLV_INPUT_ProcessTouchInput");
        // The overworld's frame and the battle's two touch-screen menus (not watched again here:
        // the shared table already does).
        field = fw.Find("FieldmapProc_Update");
        commandMenu = fw.Find("BtlvCore_WaitCommandSelect");
        moveMenu = fw.Find("BtlvCore_WaitMoveSelect");
        targetMenu = fw.Find("BtlvCore_WaitTargetSelect");
        fprintf(stderr, "[bw1] Pokemon Black: badges %d, card edit %d, save %d\n", badges, cardEdit, report);
    }

    // White bottom-edge strip ("Look at the touch screen ▼") on the top screen during overworld
    // submenus (such as SAVE in Spanish Black) where the game expects interaction on the bottom.
    static bool TopHasBottomArrowBanner(const uint32_t* px, const uint8_t* flags = nullptr)
    {
        if (!px) return false;
        if (flags)
        {
            for (int y : { 180, 185 })
                for (int x : { 24, 72, 184, 232 })
                {
                    const uint8_t lc = LayerClass(flags[y * 256 + x]);
                    if (lc == LAYER_3D || lc == LAYER_BACKDROP) return false;
                }
        }
        int whiteRows = 0, darkAbove = 0, cyan = 0;
        for (int y = 176; y <= 188; y += 3)
        {
            bool rowWhite = true;
            for (int x : { 16, 48, 80, 176, 208, 240 })
            {
                const uint32_t c = px[y * 256 + x];
                if (((c >> 16) & 0xFF) <= 215 || ((c >> 8) & 0xFF) <= 215 || (c & 0xFF) <= 215)
                {
                    rowWhite = false;
                    break;
                }
            }
            if (rowWhite) whiteRows++;
        }
        for (int x = 32; x <= 224; x += 48)
        {
            const uint32_t c = px[160 * 256 + x];
            if (((c >> 16) & 0xFF) + ((c >> 8) & 0xFF) + (c & 0xFF) < 645) darkAbove++;
        }
        for (int y = 170; y <= 190; y++)
            for (int x = 118; x <= 138; x++)
            {
                const uint32_t c = px[y * 256 + x];
                const int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
                if (b > r + 40 && g > r + 30) cyan++;
            }
        return whiteRows >= 3 && darkAbove >= 3 && cyan >= 12;
    }

    // Save card / save prompt drawn on the Gen 5 touch screen (SaveScreenPieces reads the same).
    static bool BottomScreenIsSave(const uint32_t* px)
    {
        if (!px) return false;
        auto lum = [&](int x, int y) {
            const uint32_t c = px[y * 256 + x];
            return (((c >> 16) & 0xFF) + ((c >> 8) & 0xFF) + (c & 0xFF)) / 3;
        };
        return (lum(5, 24) > 220 && lum(250, 24) > 220) || (lum(20, 108) > 200 && lum(200, 108) > 200);
    }

    void Adjust(Scene& sc, float viewWidth, const ScreenImage* screens)
    {
        if (!on || !Fw) return;
        // (The player's choice of screen, and the C-Gear's question answered unseen, stay.)
        if (!strcmp(sc.name, "manual") || !strcmp(sc.name, "continue")) return;
        const bool wideView = viewWidth >= 300;
        const float viewLeft = 128 - viewWidth / 2, viewRight = 128 + viewWidth / 2;
        // (It skips a few frames as it opens; and while the save is written, for a second or
        // two, the game does nothing else, the overworld included: the save stays until the
        // overworld runs again. Also match the bottom arrow banner + touch-screen save UI so
        // versions whose REPORT_EVENT_Main address missed never show the cut-off ▼ strip.)
        const bool pixelSave = screens && TopHasBottomArrowBanner(screens[0].pixels, screens[0].flags) && BottomScreenIsSave(screens[1].pixels);
        if (Fw->Ran(report, 10) || (saving && !Fw->Ran(field, 1)) || pixelSave)
        {
            // Laid out as Black 2's (merged.h, SaveScreenPieces): the overworld, without the
            // strip on its bottom that points at the other screen, the message box along the
            // bottom and the rest of the touch screen in the upper right corner.
            saving = true;
            const bool weather = sc.weatherObj;
            const bool narrow = sc.narrow;
            const bool softBars = sc.softBars;
            Only(sc, "save", 0);
            sc.wide = wideView;
            sc.narrow = narrow;
            sc.softBars = softBars;
            sc.weatherObj = weather;
            sc.items[0].drop = LAYER_BG1;
            if (sc.items[0].holeCount < 4)
            {
                const int h[4] = { 0, 166, 256, 192 };
                std::copy(h, h + 4, sc.items[0].holes[sc.items[0].holeCount++]);
            }
            SaveScreenPieces(sc, wideView, viewRight, screens);
            return;
        }
        saving = false;
        if (!strcmp(sc.name, "top") && screens && TopHasBottomArrowBanner(screens[0].pixels, screens[0].flags) && !sc.items.empty())
        {
            sc.items[0].drop = LAYER_BG1;
            if (sc.items[0].holeCount < 4)
            {
                const int h[4] = { 0, 166, 256, 192 };
                std::copy(h, h + 4, sc.items[0].holes[sc.items[0].holeCount++]);
            }
        }
        if (Fw->Ran(dexRegister, 2))
        {
            if (Fw->Ran(touchInput, 2))
                sc = Stack("dex-register-ask", { Crop(0, 0, 0, 256, 192), Crop(1, 0, 0, 256, 192) }, true, viewWidth);
            else
            {
                Only(sc, "dex-register", 0);
                sc.softBars = true;
            }
            return;
        }
        if (Fw->Ran(badges, 2) || Fw->Ran(cardEdit, 2))
        {
            Only(sc, "trainer-card", 1);
            return;
        }
        if (!strcmp(sc.name, "launch-menu"))
        {
            // Black's start menu (CONTINUE with the saved game's card, then NEW GAME, MYSTERY
            // GIFT, ... in a list) is all on the top screen; the touch screen is empty. The list
            // goes on below the screen (the DS shows its fourth entry, BATTLE COMPETITION, cut
            // off; there are eight in all). On a wide view the card (x 24-229, rows 15-127) and
            // the list (x 32-223, rows 128-296, drawn on below the screen's last line:
            // frontend/belowscreen.h) go side by side across the whole view, on the dark grey of
            // the menu's own background, without the Poke Ball and stripes behind them (BG3, also
            // left out of the buttons it shows through: Scene::redrawSkip). Otherwise the top
            // screen alone, with soft bars.
            // (With no saved game there is no card: the list alone, NEW GAME first, rows 15-110,
            // nothing on the screen below it but the background.)
            bool listBelow = false;
            if (screens && screens[0].flags)
                for (int y = 135; y < 186 && !listBelow; y += 2)
                    for (int x = 40; x < 216 && !listBelow; x += 4)
                    {
                        const uint8_t c = LayerClass(screens[0].flags[y * 256 + x]);
                        listBelow = c != LAYER_BACKDROP && c != LAYER_BG3;
                    }
            if (wideView && !listBelow)
            {
                // Regional menu labels can extend past the English crop on either side.
                Element list = Crop(0, regionalMenu ? 0 : 32, 14, regionalMenu ? 256 : 194, 98);
                list.drop = LAYER_BACKDROP;
                sc = CleanMenu("launch-menu", { list }, viewWidth, 0x1c1c1c);
                sc.redrawSkip[0] = 1 << 3;
                return;
            }
            if (wideView)
            {
                Element card = Crop(0, regionalMenu ? 0 : 24, 15, regionalMenu ? 256 : 206, 113),
                        list = Crop(0, regionalMenu ? 0 : 32, 128, regionalMenu ? 256 : 192, 169);
                card.drop = list.drop = LAYER_BACKDROP;
                sc = CleanMenu("launch-menu", { card, list }, viewWidth, 0x1c1c1c);
                sc.extraRows[0] = 296 + 1 - 192;
                sc.redrawSkip[0] = 1 << 3;
                sc.redrawUnscroll[0] = 2; // (the list scrolls as the cursor goes down it)
                return;
            }
            Only(sc, "launch-menu", 0);
            sc.softBars = true;
            return;
        }
        boxesAge++;
        if (!strcmp(sc.name, "battle"))
        {
            const bool many = BattleBoxes(sc, viewLeft, viewRight);
            // (Kept for a choice in the battle, which pickSceneBw2 lays out without them.)
            boxesTop = sc.items.empty() ? Element{} : sc.items[0];
            boxes.clear();
            for (const Element& e : sc.items)
                if (e.wideOnly) boxes.push_back(e);
            boxesAge = 0;
            if (battleButtons == BUTTONS_MODERN)
            {
                std::vector<Element> keep;
                for (const Element& e : sc.items)
                    if (e.screen != 1 && !(e.screen == -1 && e.border)) keep.push_back(e);
                sc.items = std::move(keep);
                if (!sc.items.empty() && (Fw->Ran(moveMenu, 4) || Fw->Ran(commandMenu, 4) || Fw->Ran(targetMenu, 4)))
                {
                    Element& top = sc.items[0];
                    if (top.holeCount < 4)
                    {
                        const int h[4] = { 0, 145, 256, 192 };
                        std::copy(h, h + 4, top.holes[top.holeCount++]);
                    }
                }
                return;
            }
            if (!screens) return;
            const bool moves = Fw->Ran(moveMenu, 1), command = !moves && Fw->Ran(commandMenu, 1);
            // (Choosing a move's target, in a double, triple or rotation battle: the foes'
            // buttons above, the player's side below, touch screen rows 32-134.)
            const bool target = !moves && !command && Fw->Ran(targetMenu, 1);
            const bool strip = battleButtons == BUTTONS_STRIP &&
                (Fw->Ran(moveMenu, 4) || Fw->Ran(commandMenu, 4) || Fw->Ran(targetMenu, 4));
            if (!strip)
                return;
            std::vector<Element> keep;
            for (const Element& e : sc.items)
                if (e.screen != 1 && !(e.screen == -1 && e.border)) keep.push_back(e);
            sc.items = std::move(keep);
            Gen5Strip::PartyPlaces party;
            const Element* piece[2] = {};
            for (const Element& e : sc.items)
                if (e.wideOnly && !piece[1]) (piece[0] ? piece[1] : piece[0]) = &e;
            if (piece[1] && sc.items[0].holeCount == 2)
            {
                if (piece[0]->sw > 0)
                {
                    party.foeRight = piece[0]->dx + (many ? 120 : 109);
                    party.foeTop = piece[0]->dy + (many ? 60 : 27);
                }
                if (piece[1]->sw > 0 && !many && piece[1]->sy != 20)
                {
                    party.mineRight = piece[1]->dx + piece[1]->sw - 4;
                    party.mineTop = piece[1]->dy + 37.5f;
                }
            }
            float availRoom = -1.0f;
            bool gridArg = moves || target || !command;
            bool readyMoves = gridArg;
            const bool ready = !screens[1].flags || BlackButtonsReady(screens[1], readyMoves);
            if (wideView && (moves || command || target || ready))
            {
                float promptRight = viewLeft + 4.0f;
                float minBtnW = 92.5f, idealBtnW = 113.0f, maxBtnW = 115.5f;
                Gen5BattleButtonBounds(1, readyMoves, target, 103, minBtnW, idealBtnW, maxBtnW);
                float targetBtnW = idealBtnW;
                AdaptGen5BattlePrompt(sc, viewWidth, screens, minBtnW, idealBtnW, maxBtnW,
                    promptRight, targetBtnW);
                availRoom = std::max(84.0f, viewRight - 2.0f - (promptRight + 5.0f));
            }
            BattleStrip(sc, wideView ? viewRight : 256, screens, gridArg, party, availRoom, true);
            return;
        }
        if (!strcmp(sc.name, "battle-list") && screens)
        {
            // As the party or Bag fades in and out the touch screen is one colour for a few frames:
            // keep the battle on screen (with its HP boxes) rather than flashing black.
            if (Gen5Strip::OneColour(screens[1]))
            {
                sc.items = { FullScreen(0) };
                sc.wide = viewWidth > 256.5f;
                if (boxesAge < 600 && !topFaded) Gen5Strip::KeepBoxes(sc, boxesTop, boxes, 1);
            }
            return;
        }
        if (!strcmp(sc.name, "battle-choice") && screens && !sc.items.empty())
        {
            if (boxesAge < 600 && !topFaded) Gen5Strip::KeepBoxes(sc, boxesTop, boxes, 1);
            if (battleButtons != BUTTONS_STRIP) return;
            const Scene cut = sc;
            float availRoom = -1.0f;
            if (wideView && !topFaded)
            {
                float promptRight = viewLeft + 4.0f, usedBtnW = 113.0f;
                if (AdaptGen5BattlePrompt(sc, viewWidth, screens, 92.5f, 113.0f, 115.5f, promptRight, usedBtnW))
                    availRoom = std::max(84.0f, viewRight - 2.0f - (promptRight + 5.0f));
            }
            if (Gen5Strip::ChoiceBars(sc, (wideView ? viewRight : 256) - 2, screens, 147, 42, availRoom))
                choiceMissing = 0;
            else if (++choiceMissing > 30)
                sc = cut;
            else
                sc = cut;
            return;
        }
        if (!strcmp(sc.name, "pokedex-list"))
        {
            // Black's Pokedex list is on the touch screen; the top one only has the title and
            // the SEEN / OBTAINED counts (rows 0-50) above an empty grid (Black 2's list runs on
            // from one screen into the other, which pickSceneBw2 shows as one column). The
            // counts above the list, as large as fits the view's height.
            sc = Stack("pokedex-list", { Crop(0, 0, 0, 256, 51), Crop(1, 0, 0, 256, 192) }, false, viewWidth);
            for (Element& el : sc.items) el.crisp = true;
            return;
        }
        if (!strcmp(sc.name, "start-menu") && screens)
        {
            XMenu(sc, wideView, viewWidth, screens);
            return;
        }
    }

private:
    const FuncWatch* Fw = nullptr;
    int badges = -1, cardEdit = -1, report = -1, field = -1, dexRegister = -1, touchInput = -1;
    int commandMenu = -1, moveMenu = -1, targetMenu = -1;
    bool saving = false; // the save's screen was shown last
    // The battle's top piece (its holes) and HP boxes as BattleBoxes placed them last, and how
    // many frames ago.
    Element boxesTop = {};
    std::vector<Element> boxes;
    unsigned boxesAge = 1000;
    int choiceMissing = 0; // frames a battle's choice has not had its bars in their places

    static void Only(Scene& sc, const char* name, int screen)
    {
        Scene n;
        n.name = name;
        n.smartEdges = sc.smartEdges;
        n.items = { FullScreen(screen) };
        sc = n;
    }

    // A battle on a wide view (pickSceneBw2 has made holes for Black 2's HP boxes and put them
    // against the view's edges). Black's boxes are drawn against the screen's edges and stay
    // against those edges, now the view's. In a single battle the foe's runs along the left edge
    // (x 0-123, y 30-48), its bars running off it, and the player's along the right edge
    // (x 136-255, y 110-138): the foe's goes to the view's left, 4 pixels higher, the player's to
    // its right, 10 pixels higher (still in the lower half), so that it clears the buttons in the
    // lower right corner. In a double, triple or rotation battle (pickSceneBw2's foes' hole starts
    // at row 4) the foes' boxes are stacked down the left edge (a double's x 0-126, y 18-62; a
    // triple's centred on rows 19, 40 and 61) and the player's down the right edge (a double's
    // x 136-255, y 90-146, just above the message box): they keep their rows. Returns whether
    // the battle has more than one Pokemon a side.
    static bool BattleBoxes(Scene& sc, float viewLeft, float viewRight)
    {
        if (sc.items.empty() || sc.items[0].holeCount != 2) return false; // no boxes moved
        Element& top = sc.items[0];
        // pickSceneBw2's pieces: the foes' first, then the player's. Their sources tell the kind
        // of battle (the foes' piece starts at row 4 with more than one Pokemon a side) and
        // whether the level-up window is open (the player's starts at row 20); a side whose
        // boxes are put away (a Poke Ball being thrown) has no hole and a piece of no size, and
        // stays so.
        Element* piece[2] = {};
        for (Element& e : sc.items)
            if (e.wideOnly && !piece[1]) (piece[0] ? piece[1] : piece[0]) = &e;
        if (!piece[1]) return false;
        const bool many = piece[0]->sy == 4, levelUp = piece[1]->sy == 20;
        const bool shown[2] = { top.holes[0][2] > 0, top.holes[1][2] > 0 };
        static const int enemy1[4] = { 0, 26, 128, 54 }, mine1[4] = { 132, 106, 256, 142 };
        const int enemyN[4] = { 0, 8, 128, 72 }, mineN[4] = { 134, 82, 256, 147 };
        const int* enemy = many ? enemyN : enemy1;
        int mine[4];
        std::copy(many ? mineN : mine1, (many ? mineN : mine1) + 4, mine);
        // While the level-up window is open (over the player's box, inside its columns) it moves
        // with the box, from row 20: cut through at the box's top, its last rows went right with
        // the box and the rest stayed.
        if (levelUp) mine[1] = 20;
        std::copy(enemy, enemy + 4, top.holes[0]);
        std::copy(mine, mine + 4, top.holes[1]);
        for (int i = 0; i < 2; i++)
            if (!shown[i]) std::fill(top.holes[i], top.holes[i] + 4, 0);
        for (int n = 0; n < 2; n++)
        {
            Element& e = *piece[n];
            if (!shown[n]) continue;
            const int* r = n == 0 ? enemy : mine;
            e.sx = r[0];
            e.sy = r[1];
            e.sw = r[2] - r[0];
            e.sh = r[3] - r[1];
            if (n == 0)
            {
                e.dx = viewLeft;
                e.dy = enemy[1] - (many ? 0 : 4);
            }
            else
            {
                e.dx = viewRight - e.sw;
                e.dy = mine[1] - (many ? 0 : 10);
            }
        }
        return many;
    }

    // The battle's buttons (STRIP / STANDARD / SUNMOON): in the lower right of the view, on the
    // message box's band (rows 147-189) right of its text.
public:
    static bool BlackButtonsReady(const ScreenImage& touch, bool& gridOut)
    {
        const uint8_t* fl = touch.flags;
        if (!fl) return true;
        auto layer = [&](int x, int y) { return LayerClass(fl[y * 256 + x]); };
        const bool commands = layer(39, 170) == LAYER_BG0 && layer(127, 180) == LAYER_BG0 &&
                              layer(216, 170) == LAYER_BG0 && layer(128, 100) == LAYER_BACKDROP &&
                              layer(39, 133) == LAYER_BG0 && layer(127, 149) == LAYER_BG0 && layer(216, 133) == LAYER_BG0;
        const bool hexagons = layer(40, 74) == LAYER_BG1 && layer(216, 74) == LAYER_BG1 &&
                              layer(40, 120) == LAYER_BG1 && layer(216, 120) == LAYER_BG1 &&
                              layer(0, 80) == LAYER_BACKDROP && layer(255, 80) == LAYER_BACKDROP;
        if (!commands && !hexagons) return false;
        gridOut = hexagons;
        return true;
    }

    static void BattleStrip(Scene& sc, float viewRight, const ScreenImage* screens, bool grid,
        const Gen5Strip::PartyPlaces& party, float availRoom = -1.0f, bool authentic = true)
    {
        // Which menu the touch screen shows, by the layers at a few places: the command menu's
        // octagons (BG0: BAG, RUN, POKEMON) below the lens, inside which the backdrop shows; or
        // hexagons (BG1) with the backdrop beside them: the moves, two by two, or the Pokemon a
        // move can be aimed at in a double or triple battle (the foes above, the player's side
        // below, two or three across), which the game lays out in the same rows (32-127). While
        // one menu turns into another the next one's function already runs, over a few frames of
        // the Ball closing on the commands and opening on the moves (or the hexagons shrinking
        // away): nothing is drawn until the buttons are in their places.
        const uint8_t* fl = screens[1].flags;
        auto layer = [&](int x, int y) { return fl ? LayerClass(fl[y * 256 + x]) : 0; };
        if (fl && !BlackButtonsReady(screens[1], grid)) return;
        const float right = viewRight - 2;
        const float room = availRoom > 0.0f ? availRoom : (right - (Gen5Strip::MessageEnd(screens[0]) + 4));
        auto piece = [&](int x, int y, int w, int h, float s, float left, float top) {
            Element e = Crop(1, x, y, w, h);
            e.scale = s;
            e.dx = left + x * s;
            e.dy = top + y * s;
            return e;
        };
        Gen5Strip::PartyRows(sc, party, Crop(1, 104, grid ? 18 : 42, 50, 11), Crop(1, 78, 128, 94, 16));
        if (!grid)
        {
            const float s = std::clamp(room / 256, 0.36f, 0.44f);
            Gen5Strip::Commands(sc, screens[1], Gen5Strip::BlackCommands(), right - 256 * s,
                147 + (42 - 96 * s) / 2 - 32 * s, s, authentic);
            return;
        }
        const Gen5Strip::BelowButton* below[3];
        int n = 0;
        if (layer(39, 175) == LAYER_BG0 && layer(39, 140) == LAYER_BG0) below[n++] = &Gen5Strip::BlackShift();
        else if (layer(39, 175) == LAYER_BG0) below[n++] = &Gen5Strip::BlackTurn(0);
        if (layer(120, 175) == LAYER_BG0) below[n++] = &Gen5Strip::BlackTurn(1);
        const bool back = layer(216, 175) == LAYER_BG0;
        const float gap = 3;
        auto fit = [&](bool withBack) {
            float w = 256, k = 0;
            for (int i = 0; i < n; i++) { w += below[i]->width; k++; }
            if (withBack) { w += Gen5Strip::BlackBack().width; k++; }
            return (room - gap * k) / w;
        };
        const bool drawBack = back && (fit(true) >= 0.36f || !fl);
        const float s = std::max(0.36f, std::min(0.44f, fit(drawBack)));
        if (drawBack) below[n++] = &Gen5Strip::BlackBack();
        const float left = right - 256 * s, top = 147 + (42 - 96 * s) / 2 - 32 * s;
        Element hexagons = piece(0, 32, 256, 96, s, left, top);
        hexagons.drop = LAYER_BACKDROP;
        sc.items.push_back(hexagons);
        Gen5Strip::Below(sc, screens[1], below, n, left + s, top + 34 * s, 92 * s, s, gap, 0xFB0092, authentic);
    }

    // The X menu. Black's buttons are 123 x 41 on a light grid, two to a row, rows 48 apart
    // from y 24 (x 3 and 131); a slot is used when most of its middle row is not the light
    // grid. They go in a panel at the right edge in the same grid as Black 2's (pickSceneBw2
    // measured Black 2's buttons, which are larger and elsewhere).
    static void XMenu(Scene& sc, bool wideView, float viewWidth, const ScreenImage* screens)
    {
        if (sc.items.empty()) return;
        sc.items.resize(1); // the overworld (and its settings) as pickSceneBw2 chose them
        struct Slot { Element crop; int row, col; };
        std::vector<Slot> buttons;
        int rows = 0;
        const uint32_t* px = screens[1].pixels;
        for (int row = 0; row < 3; row++)
            for (int col = 0; col < 2; col++)
            {
                const int bx = col ? 131 : 3, by = 24 + row * 48;
                int dark = 0;
                for (int x = bx; x < bx + 123; x++)
                {
                    const uint32_t c = px[(by + 20) * 256 + x];
                    const int lum = (((c >> 16) & 0xFF) + ((c >> 8) & 0xFF) + (c & 0xFF)) / 3;
                    if (lum <= 150) dark++;
                }
                // (And something light on it, its icon or name: while the menu opens its buttons
                // are empty dark boxes for a few frames, which showed as a grid of black boxes.)
                int light = 0;
                for (int y = by + 8; y < by + 35; y += 2)
                    for (int x = bx + 8; x < bx + 115; x += 2)
                    {
                        const uint32_t c = px[y * 256 + x];
                        if ((((c >> 16) & 0xFF) + ((c >> 8) & 0xFF) + (c & 0xFF)) / 3 > 200) light++;
                    }
                if (dark * 10 >= 123 * 3 && light >= 8)
                {
                    buttons.push_back({ Crop(1, bx, by, 123, 41), row, col });
                    rows = row + 1;
                }
            }
        if (buttons.empty()) return; // opening or closing: the overworld alone
        const float scale = wideView ? 0.46f : 0.41f, gap = 2, pad = 3;
        const float bw = 123 * scale, bh = 41 * scale;
        const float w = 2 * bw + gap, h = rows * bh + (rows - 1) * gap;
        const float x = (wideView ? 128 + viewWidth / 2 : 256.0f) - w - pad - 2;
        const float y = (192 - h) / 2;
        sc.items.push_back(FillRect(x - pad, y - pad, (int)(w + 2 * pad), (int)(h + 2 * pad), 0x14141C));
        sc.items.back().border = true;
        sc.items.back().borderRadius = 3.0f;
        sc.items.back().borderColor = 0x2A2E38;
        for (Slot b : buttons)
        {
            b.crop.dx = x + b.col * (bw + gap);
            b.crop.dy = y + b.row * (bh + gap);
            b.crop.scale = scale;
            // (The grid's white and its lines show above the right part of each button, beside
            // the tab at its top left: not drawn, within a few pixels of the piece's edge.)
            b.crop.keys[0] = 0xFBFBFB;
            b.crop.keys[1] = 0xD3D3D3;
            b.crop.keyRing = 7;
            sc.items.push_back(b.crop);
        }
    }
};
