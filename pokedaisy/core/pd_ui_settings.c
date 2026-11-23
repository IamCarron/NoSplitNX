// Overlays shared by the companion tabs: TAB BAR, HUD, MORE, and choice pick-lists.
#include "pd_ui_internal.h"
#include "pd_tr.h"

#define TITLE_H 24

int pd_ui_list_step(int selected, int count, int step) {
    if (count <= 0) return -1;
    int next = selected + step;
    return next < 0 ? 0 : next >= count ? count - 1 : next;
}

static void title_window(struct pd_canvas* c, int y, const char* title) {
    int x = MARGIN, w = c->w - 2 * MARGIN;
    int f = pd_title_box(c, x, y, w, TITLE_H, PD_TITLE_FILL);
    pd_text(c, x + f + 6, pd_text_y(y, TITLE_H), pd_tr(title), PD_TITLE_TEXT, PD_TITLE_SHADOW);
}

static void pick_list(struct pd_ui* ui, struct pd_canvas* c, const char* title, const char* const* options, int n,
                      int current) {
    int x = MARGIN, w = c->w - 2 * MARGIN;
    int y = CONTENT_Y + 8;
    title_window(c, y, title);
    y += TITLE_H + 4;
    int lf = pd_list_box(c, x, y, w, n * ROW_H + 2 * 7 + 4);
    int ix = x + lf + 4, iw = w - 2 * (lf + 4);
    int ry = y + lf + 2;
    ui->startId = HIT_OPTION + current; // the D-pad starts on the current choice
    for (int i = 0; i < n; i++) {
        bool on = i == current || ui->pressedId == HIT_OPTION + i;
        if (on) pd_fill(c, ix - 4, ry, iw + 8, ROW_H, PD_ROW_SELECTED);
        pd_text(c, ix + 10, pd_text_y(ry, ROW_H), pd_tr(options[i]), on ? PD_VALUE : PD_LABEL,
                on ? PD_VALUE_SHADOW : PD_LABEL_SHADOW);
        if (i == current) pd_triangle(c, ix, ry + 4, 5, PD_RIGHT, PD_VALUE);
        ui_add_hit(ui, ix - 4, ry, iw + 8, ROW_H, HIT_OPTION + i);
        if (i < n - 1) ui_row_divider(c, ix - 2, ry + ROW_H - 1, iw + 4);
        ry += ROW_H;
    }
}

// A page of ON / OFF rows (TAB BAR's, HUD's), each flipped by A / a tap.
static void toggle_page(struct pd_ui* ui, struct pd_canvas* c, const char* title, const char* const* labels,
                        const bool* on, const bool* muted, int n, const char* note, const char* const* values) {
    int x = MARGIN, w = c->w - 2 * MARGIN;
    int y = CONTENT_Y + 4;
    title_window(c, y, title);
    y += TITLE_H + 4;
    int lf = pd_list_box(c, x, y, w, n * ROW_H + 2 * 7 + 4 + (note ? 16 : 0));
    int ix = x + lf + 4, iw = w - 2 * (lf + 4);
    int ry = y + lf + 2;
    if (ui->startId < 0) ui->startId = HIT_OPTION;
    for (int i = 0; i < n; i++) {
        bool sel = ui->pressedId == HIT_OPTION + i;
        if (on) {
            const char* v = values && values[i] ? values[i] : on[i] ? "ON" : "OFF";
            ui_row(c, ix, ry, iw, labels[i], v, sel);
            if (muted && muted[i]) {
                pd_fill(c, ix + iw * 42 / 100, ry + 1, iw - iw * 42 / 100, ROW_H - 2, sel ? PD_ROW_SELECTED : PD_LIST_FILL);
                pd_text(c, ix + iw * 42 / 100, pd_text_y(ry, ROW_H), v, PD_MUTED, PD_MUTED_SHADOW);
            }
        } else {
            if (sel) pd_fill(c, ix - 4, ry, iw + 8, ROW_H, PD_ROW_SELECTED);
            pd_text(c, ix + 10, pd_text_y(ry, ROW_H), labels[i], sel ? PD_VALUE : PD_LABEL,
                    sel ? PD_VALUE_SHADOW : PD_LABEL_SHADOW);
        }
        ui_add_hit(ui, ix - 4, ry, iw + 8, ROW_H, HIT_OPTION + i);
        if (i < n - 1) ui_row_divider(c, ix - 2, ry + ROW_H - 1, iw + 4);
        ry += ROW_H;
    }
    if (note) pd_text_fit(c, ix, ry + 2, iw, note, PD_MUTED, PD_MUTED_SHADOW);
}

static void tab_bar_page(struct pd_ui* ui, struct pd_canvas* c) {
    const char* labels[UI_BAR_CHOICES];
    bool on[UI_BAR_CHOICES];
    enum pd_tab tabs[UI_BAR_CHOICES];
    const int n = ui_configurable_tabs(ui, tabs);
    for (int i = 0; i < n; i++) {
        labels[i] = ui_tab_label(tabs[i]);
        on[i] = (ui->settings->tabBar >> tabs[i]) & 1;
    }
    toggle_page(ui, c, "TAB BAR", labels, on, NULL, n, "At most 5 on the bar; the rest open from MORE.", NULL);
}

static void hud_page(struct pd_ui* ui, struct pd_canvas* c) {
    enum { N = 4 + PD_HUD_PARTS };
    const struct pd_settings* s = ui->settings;
    const char* labels[N] = { "HUD", "LAYOUT", "LOOK", "SIZE" };
    const char* values[N] = { NULL, pd_hud_layout_name(s->hudLayout), pd_hud_look_name(s->hudLook),
                              pd_hud_size_name(s->hudSize) };
    bool on[N] = { s->hud, true, true, true }, muted[N] = { false, !s->hud, !s->hud, !s->hud };
    if (ui->hudLayoutFixed) {
        values[1] = "N/A";
        muted[1] = true;
    }
    for (int i = 0; i < PD_HUD_PARTS; i++) {
        labels[4 + i] = pd_hud_part_name(i);
        on[4 + i] = !((s->hudHidden >> i) & 1);
        muted[4 + i] = !s->hud;
    }
    toggle_page(ui, c, "HUD", labels, on, muted, N, "Shown over the overworld, off in menus and battles.", values);
}

void ui_more_page(struct pd_ui* ui, struct pd_canvas* c, const struct pd_game* g) {
    enum pd_tab more[UI_BAR_CHOICES];
    int n = ui_more_tabs(ui, g, more);
    int x = MARGIN, w = c->w - 2 * MARGIN;
    title_window(c, CONTENT_Y, "MORE");
    int y = CONTENT_Y + TITLE_H + 3, h = c->h - y - 2;
    const bool smallText = pd_font_text_small();
    const int gap = 6, bottomH = 2 + 2 * gap + (smallText ? 28 : 40);
    int f = 0;
    for (int i = 0; i < pd_cur->listCount; i++) f += pd_cur->list[i].width;
    int ix = x + f + gap, iy = y + f + gap, iw = w - 2 * (f + gap), gridH = h - 2 * f - gap - bottomH;
    int cols = (iw + gap) / (64 + gap);
    if (cols > 4) cols = 4;
    if (cols < 1) cols = 1;
    int tile = (iw - gap * (cols - 1)) / cols;
    while ((n + cols - 1) / cols * (tile + gap) - gap > gridH && cols < 6) {
        cols++;
        tile = (iw - gap * (cols - 1)) / cols;
    }
    int rows = n > 0 ? (n + cols - 1) / cols : 1, tileH = tile;
    if (smallText) {
        int want = 2 * f + gap + rows * (tile + gap) + bottomH;
        if (want < h) h = want;
        gridH = h - 2 * f - gap - bottomH;
    }
    f = pd_list_box(c, x, y, w, h);
    const int capTop = smallText ? 5 : 4, capH = smallText ? 8 : 9;
    uint32_t idle = pd_mix(pd_cur->tabIdleFill, PD_TITLE_FILL, 230);
    for (int i = 0; i < n; i++) {
        int tx = ix + (i % cols) * (tile + gap), ty = iy + (i / cols) * (tileH + gap);
        bool pressed = ui->pressedId == HIT_OPTION + i;
        uint32_t fill = pressed ? pd_cur->tabSelectedFill : idle;
        pd_layered_two_tone(c, tx, ty, tile, tileH, 3, pd_cur->chip, pd_cur->chipCount, fill,
                            pd_mix(fill, pd_two_tone_low(fill), 128));
        const struct pd_art* art = ui_tab_art(more[i]);
        int icon = tileH - capH - 18;
        if (icon > 28) icon = 28;
        if (icon > tile - 12) icon = tile - 12;
        if (icon < 12) icon = 12;
        int block = (art ? icon + 5 : 0) + capH, top = ty + (tileH - block) / 2;
        if (art) {
            if (smallText) ui_icon_smooth(c, art, NULL, 0, tx + (tile - icon) / 2, top, icon);
            else {
                int iconScale = tile >= 52 ? 2 : 1;
                ui_art(c, art, tx + (tile - 16 * iconScale) / 2, ty + (tile - 16 - 16 * iconScale) / 2, iconScale);
            }
        }
        const char* label = ui_tab_label(more[i]);
        int ly = smallText ? top + (art ? icon + 5 : 0) - capTop : ty + tile - 17;
        pd_text_fit(c, tx + 2 + (tile - 4 - pd_text_width(label) > 0 ? (tile - 4 - pd_text_width(label)) / 2 : 0), ly,
                    tile - 4, label, pressed ? PD_VALUE : PD_TITLE_TEXT, pressed ? PD_VALUE_SHADOW : PD_TITLE_SHADOW);
        ui_add_hit(ui, tx, ty, tile, tileH, HIT_OPTION + i);
    }
    int ry = y + h - f - bottomH;
    pd_separator(c, ix, ry, iw, false);
    int bw = 86, bh = 22, by = ry + 2 + gap + ((pd_font_text_small() ? 28 : 40) - bh) / 2;
    pd_text_wrap(c, ix, ry + 2 + gap - (pd_font_text_small() ? 4 : 3), iw - bw - gap, pd_lh(13), pd_font_text_small() ? 2 : 3,
                 pd_tr("Tabs that aren't on the tab bar are here. Pick the bar's tabs in TAB BAR."), PD_LABEL, PD_LABEL_SHADOW);
    int bx = ix + iw - bw;
    ui_button(ui, c, bx, by, bw, bh, "", HIT_MORE_TAB_BAR);
    const int gear = pd_font_text_small() ? 12 : PD_ICON_GEAR.w;
    int lw = gear + 4 + pd_text_width(pd_tr("TAB BAR")), lx = bx + (bw - lw) / 2;
    if (gear != PD_ICON_GEAR.w) ui_icon_smooth(c, NULL, &PD_ICON_GEAR, PD_TITLE_TEXT, lx, by + (bh - gear) / 2, gear);
    else ui_mono(c, &PD_ICON_GEAR, lx, by + (bh - PD_ICON_GEAR.h) / 2, 1, PD_TITLE_TEXT);
    pd_text(c, lx + gear + 4, pd_text_y(by, bh), pd_tr("TAB BAR"), PD_TITLE_TEXT, PD_TITLE_SHADOW);
}

void ui_overlay(struct pd_ui* ui, struct pd_canvas* c) {
    if (ui->overlay == PD_OVERLAY_NONE) return;
    pd_dim(c, 0xE6);
    ui_add_hit(ui, 0, 0, c->w, c->h, HIT_SCRIM);
    switch (ui->overlay) {
    case PD_OVERLAY_TAB_BAR:
        tab_bar_page(ui, c);
        break;
    case PD_OVERLAY_HUD:
        hud_page(ui, c);
        break;
    default:
        break;
    }
}

void pd_ui_choices_view_ui(struct pd_ui* ui, struct pd_canvas* c, const char* title, const char* const* choices, int count,
                           int selected) {
    if (count < 1 || count > 8) return;
    ui->pressedId = -1;
    ui->hitCount = 0;
    pd_dim(c, 230);
    pick_list(ui, c, title, choices, count, selected);
}

void pd_ui_choices_view(struct pd_canvas* c, const char* title, const char* const* choices, int count, int selected) {
    struct pd_ui ui = {0};
    pd_ui_choices_view_ui(&ui, c, title, choices, count, selected);
}
