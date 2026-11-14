/*
    Copyright 2016-2025 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#pragma once

#include "GPU2D.h"

namespace melonDS
{
class GPU;

namespace GPU2D
{

class SoftRenderer : public Renderer2D
{
public:
    SoftRenderer(melonDS::GPU& gpu);
    ~SoftRenderer() override {}

    void DrawScanline(u32 line, Unit* unit) override;
    void DrawSprites(u32 line, Unit* unit) override;
    void DrawSpritesLine(u32 line); // ssrecomp: DrawSprites without its cache
    void VBlankEnd(Unit* unitA, Unit* unitB) override;

    // ssrecomp: which layer supplied the topmost pixel, per 2D engine, for the frame being drawn.
    // Bits 0-3 are BG0-BG3, 0x20 the backdrop, 0x40 the 3D layer, 0x80 a sprite.
    u8 LayerFlags[2][256*192] = {};
    // ssrecomp: the first and last pixel of each line (index line * 2 + side), for continuing
    // them beside the screen in a wide view (frontend/merged.h). EdgeColour: the topmost
    // layer's own colour before any blending (XRGB8888, as the finished line has it).
    // EdgeBlend: how it mixes with the 3D world, eva | evb << 8 (in 16ths, as the DS blends:
    // own * eva + 3D * evb): 16 if it covers it (or there is no 3D below), 16 << 8 if the 3D
    // layer is on top. EdgeFlat: bit 0 if the 8 pixels at that edge all have that colour and
    // layer (a fill, as opposed to a picture), bit 1 if that layer is a bitmap background.
    u32 EdgeColour[2][192 * 2] = {};
    u16 EdgeBlend[2][192 * 2] = {};
    u8 EdgeFlat[2][192 * 2] = {};
    // ssrecomp: each line's background scroll registers (BG0-3 x and y), per 2D engine.
    u16 LineScroll[2][192][4][2] = {};
    // ssrecomp: each line's colour effect and master brightness registers (BLDCNT, BLDY,
    // MASTER_BRIGHT), per 2D engine: what the line was drawn with, not what the game has
    // set for the next frame by the time it is finished.
    u16 LineBlend[2][192][3] = {};
    // ssrecomp: OAM as it was when the frame being drawn started.
    alignas(8) u8 OAMSnap[2][0x400] = {};
    // ssrecomp: the palettes (BG, then sprites) as they were when the frame being drawn
    // started: fades are often made by changing them, for the next frame, before this one
    // is shown.
    alignas(8) u8 PalSnap[2][0x400] = {};
    // ssrecomp: the priority of the sprite pixel at each point of the frame (4: none).
    u8 ObjPrio[2][256*192] = {};
private:
    melonDS::GPU& GPU;
    alignas(8) u32 BGOBJLine[256*3];
    u32* _3DLine;

    alignas(8) u8 WindowMask[256];

    alignas(8) u32 OBJLine[2][256];
    alignas(8) u8 OBJWindow[2][256];
    // ssrecomp: tracks whether OBJLine/OBJWindow are already known all-zero for each 2D engine
    // so zero-sprite scanlines avoid repeated clears, memcmps, and SpriteCache/LineCache copies.
    bool LineObjEmpty[2] = { false, false };

    u32 NumSprites[2];

    u8* CurBGXMosaicTable;
    array2d<u8, 16, 256> MosaicTable = []() constexpr
    {
        array2d<u8, 16, 256> table {};
        // initialize mosaic table
        for (int m = 0; m < 16; m++)
        {
            for (int x = 0; x < 256; x++)
            {
                int offset = x % (m+1);
                table[m][x] = offset;
            }
        }

        return table;
    }();

    static constexpr u32 ColorBlend4(u32 val1, u32 val2, u32 eva, u32 evb) noexcept
    {
        u32 r =  (((val1 & 0x00003F) * eva) + ((val2 & 0x00003F) * evb) + 0x000008) >> 4;
        u32 g = ((((val1 & 0x003F00) * eva) + ((val2 & 0x003F00) * evb) + 0x000800) >> 4) & 0x007F00;
        u32 b = ((((val1 & 0x3F0000) * eva) + ((val2 & 0x3F0000) * evb) + 0x080000) >> 4) & 0x7F0000;

        if (r > 0x00003F) r = 0x00003F;
        if (g > 0x003F00) g = 0x003F00;
        if (b > 0x3F0000) b = 0x3F0000;

        return r | g | b | 0xFF000000;
    }

    static constexpr u32 ColorBlend5(u32 val1, u32 val2) noexcept
    {
        u32 eva = ((val1 >> 24) & 0x1F) + 1;
        u32 evb = 32 - eva;

        if (eva == 32) return val1;

        u32 r =  (((val1 & 0x00003F) * eva) + ((val2 & 0x00003F) * evb) + 0x000010) >> 5;
        u32 g = ((((val1 & 0x003F00) * eva) + ((val2 & 0x003F00) * evb) + 0x001000) >> 5) & 0x007F00;
        u32 b = ((((val1 & 0x3F0000) * eva) + ((val2 & 0x3F0000) * evb) + 0x100000) >> 5) & 0x7F0000;

        if (r > 0x00003F) r = 0x00003F;
        if (g > 0x003F00) g = 0x003F00;
        if (b > 0x3F0000) b = 0x3F0000;

        return r | g | b | 0xFF000000;
    }

    static constexpr u32 ColorBrightnessUp(u32 val, u32 factor, u32 bias) noexcept
    {
        u32 rb = val & 0x3F003F;
        u32 g = val & 0x003F00;

        rb += (((((0x3F003F - rb) * factor) + (bias*0x010001)) >> 4) & 0x3F003F);
        g +=  (((((0x003F00 - g ) * factor) + (bias*0x000100)) >> 4) & 0x003F00);

        return rb | g | 0xFF000000;
    }

    static constexpr u32 ColorBrightnessDown(u32 val, u32 factor, u32 bias) noexcept
    {
        u32 rb = val & 0x3F003F;
        u32 g = val & 0x003F00;

        rb -= ((((rb * factor) + (bias*0x010001)) >> 4) & 0x3F003F);
        g -=  ((((g  * factor) + (bias*0x000100)) >> 4) & 0x003F00);

        return rb | g | 0xFF000000;
    }
    u32 ColorComposite(int i, u32 val1, u32 val2) const;
    // ssrecomp: fills EdgeColour and EdgeFlat for a line (before it is composited).
    void RecordEdges(u32 line);

    template<u32 bgmode> void DrawScanlineBGMode(u32 line);
    void DrawScanlineBGMode6(u32 line);
    void DrawScanlineBGMode7(u32 line);
    void DrawScanline_BGOBJ(u32 line);

    // ssrecomp: defined here so that it is inlined into the drawing loops.
    static inline __attribute__((always_inline)) void DrawPixel_Normal(u32* dst, u16 color, u32 flag)
    {
        u8 r = (color & 0x001F) << 1;
        u8 g = (color & 0x03E0) >> 4;
        u8 b = (color & 0x7C00) >> 9;

        *(dst+256) = *dst;
        *dst = r | (g << 8) | (b << 16) | flag;
    }
    static void DrawPixel_Accel(u32* dst, u16 color, u32 flag);

    typedef void (*DrawPixel)(u32* dst, u16 color, u32 flag);

    void DrawBG_3D();
    template<bool mosaic, DrawPixel drawPixel> void DrawBG_Text(u32 line, u32 bgnum);
    template<bool mosaic, DrawPixel drawPixel> void DrawBG_Affine(u32 line, u32 bgnum);
    template<bool mosaic, DrawPixel drawPixel> void DrawBG_Extended(u32 line, u32 bgnum);
    template<bool mosaic, DrawPixel drawPixel> void DrawBG_Large(u32 line);

    void ApplySpriteMosaicX();
    template<DrawPixel drawPixel>
    void InterleaveSprites(u32 prio);
    template<bool window> void DrawSprite_Rotscale(u32 num, u32 boundwidth, u32 boundheight, u32 width, u32 height, s32 xpos, s32 ypos);
    template<bool window> void DrawSprite_Normal(u32 num, u32 width, u32 height, s32 xpos, s32 ypos);

    void DoCapture(u32 line, u32 width);
};

}

}