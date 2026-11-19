#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "launcher.h"
#undef STB_IMAGE_IMPLEMENTATION

int main()
{
    // 1. Spanish title & sentence formatting keeps connecting words and filenames natural.
    assert(ToNiceTitle("LISTA DE JUEGOS") == "Lista de Juegos");
    assert(ToNiceTitle("BOT\xC3\x93N DE AVANCE R\xC3\x81PIDO") == "Bot\xC3\xB3n de Avance R\xC3\xA1pido");
    assert(ToNiceTitle("ICONOS EN EL MEN\xC3\x9A HOME") == "Iconos en el Men\xC3\xBA HOME");
    assert(ToNiceTitle("TECLAS Y BOTONES") == "Teclas y Botones");
    assert(ToNiceTitle("CONTADOR DE FPS") == "Contador de FPS");

    const std::string sent = ToNiceSentence(
        "PON BACKGROUND.JPG O BACKGROUND.PNG EN /SWITCH/NOSPLITNX/. VERIFICA SHA256SUMS.TXT Y GAME.LOG CON JOY-CON Y STEAMGRIDDB.");
    assert(sent.find("background.jpg") != std::string::npos);
    assert(sent.find("background.png") != std::string::npos);
    assert(sent.find("SHA256SUMS.txt") != std::string::npos);
    assert(sent.find("game.log") != std::string::npos);
    assert(sent.find("Joy-Con") != std::string::npos);
    assert(sent.find("SteamGridDB") != std::string::npos);
    assert(sent.find("NoSplitNX") != std::string::npos);

    // 2. Launcher, modal, and GBA hack strings translate cleanly into Spanish.
    UiLang::Set("es");
    assert(UiLang::Tr("3 COVERS SAVED") == "3 CAR\xC3\x81TULAS GUARDADAS");
    assert(UiLang::Tr("15 CODES") == "15 C\xC3\x93" "DIGOS");
    assert(UiLang::Tr("A: CHOOSE    B: BACK") == "A: ELEGIR    B: VOLVER");
    assert(UiLang::Tr("SHORTCUTS") == "ACCESOS DIRECTOS");
    assert(UiLang::Tr("NO WIDESCREEN") == "SIN MODO PANOR\xC3\x81MICO");
    assert(UiLang::Tr("WIDESCREEN UNTESTED") == "MODO PANOR\xC3\x81MICO SIN PROBAR");
    assert(UiLang::Tr("ROM HACK: GLITCHY SIDES? TRY ASPECT RATIO 3:2").find("3:2") != std::string::npos);
    assert(UiLang::Tr("ROM HACK: GLITCHY SIDES? TRY ASPECT RATIO 3:2") != "ROM HACK: GLITCHY SIDES? TRY ASPECT RATIO 3:2");

    // 3. Changing list_lang removes any stale legacy language= entry from settings.cfg.
    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "nosplitnx_test_lang_cfg.cfg";
    {
        std::ofstream out(tmp);
        out << "list_lang=en\nlanguage=en\nvolume=80\n";
    }
    Launcher::SettingsPage page;
    page.Open(tmp.string());
    page.Enter(0); // DISPLAY group, where list_lang is item 0
    assert(!page.items.empty() && std::string(page.items[0].key) == "list_lang");
    page.selected = 0;
    page.SelectIndex(2); // "es"
    {
        std::ifstream in(tmp);
        std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        assert(all.find("list_lang=es") != std::string::npos);
        assert(all.find("language=") == std::string::npos);
    }
    std::error_code ec;
    std::filesystem::remove(tmp, ec);

    // 4. SettingsPage group preview refreshes automatically when UiLang::lang changes.
    UiLang::Set("en");
    page.ShowGroups(0);
    assert(page.groupLang == 0);
    UiLang::Set("es");
    Canvas c;
    c.Resize(1280, 720);
    page.Draw(c, "A: OPEN    B: BACK");
    assert(page.groupLang == 1);
    assert(page.groupText[0].find("Idioma") != std::string::npos);

    return 0;
}
