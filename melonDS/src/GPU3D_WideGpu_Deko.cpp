// ssrecomp: the widescreen view's polygons drawn by deko3D, the Switch's own graphics API
// (GPU3D_WideGpu.h). Tested first with frontend/dekoprobe.cpp: beside SDL's OpenGL, a
// 1280x720 picture took 0.05 ms to send, 1.8 ms on the graphics chip and 1.5 ms to read.
//
// Memory: the frame's data (constants, vertices, polygons, the DS's texture memory) is written
// by the processor into memory the graphics chip reads; the targets are images; the finished
// picture is copied to memory the processor reads through its cache. One frame is drawn at a
// time (GPU3D_Wide.cpp fetches each picture before it hands over the next frame), so nothing
// is double-buffered.
#ifdef SSR_DEKO3D
#include "GPU3D_WideGpu.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deko3d.h>
#include <switch.h>

#include "deko_shaders/wide_vsh_dksh.h"
#include "deko_shaders/wide_fsh_dksh.h"
#include "deko_shaders/wide_fsh_texbuf_dksh.h"
#include "deko_shaders/wide_fsh_early_dksh.h"
#include "deko_shaders/wide_fsh_texbuf_early_dksh.h"
#include "deko_shaders/final_vsh_dksh.h"
#include "deko_shaders/final_fsh_dksh.h"

namespace melonDS
{
namespace
{
constexpr u32 CmdSize = 4 * 1024 * 1024;
constexpr u32 MaxVertices = 4096 * 8 * 3; // (the wide lists' polygons, up to 8 triangles each)
constexpr u32 MaxPolygons = 4096;

// Where things are in the data memory.
constexpr u32 OffFrame = 0;                            // uniform buffer 0
constexpr u32 FrameSize = 0x200;
constexpr u32 OffDraw = 0x200;                         // uniform buffer 1, set per draw
constexpr u32 DrawSize = 0x100;
constexpr u32 OffImageDesc = 0x300;                    // image descriptors (see ImageIds)
constexpr u32 OffSamplerDesc = 0x400;                  // one sampler descriptor
// The images in the descriptor set: the final pass's two textures (what was drawn), and the
// texture and palette memory as buffer textures (the WIDE_TEXBUF shader, see Shared).
enum ImageIds : u32 { ImgColor, ImgAttr, ImgTex8, ImgTex16, ImgPal16, ImgAtlas, ImageCount };
static_assert(OffImageDesc + ImageCount * 32 <= OffSamplerDesc, "the image descriptors fit");
// What the graphics chip reports for each picture, 16 bytes each (a count, then a time).
enum Reports : u32 { RepStart, RepCleared, RepDrawn, RepFragments, RepSamples, RepFinal, RepCopied, ReportCount };
constexpr u32 OffReports = 0x600;                      // the graphics chip's timings (see Reports)
constexpr u32 OffPolygons = 0x1000;
static_assert(OffReports + ReportCount * 16 <= OffPolygons, "the reports fit");
constexpr u32 OffTex = OffPolygons + MaxPolygons * sizeof(WideGpuPolygon);
constexpr u32 OffPal = OffTex + 512 * 1024;
constexpr u32 OffVertices = OffPal + 128 * 1024;
constexpr u32 OffAtlas = (OffVertices + MaxVertices * sizeof(WideGpuVertex) + 0xFFF) & ~0xFFFu;
constexpr u32 DataSize = OffAtlas + WideGpuAtlas::Capacity * 4;
static_assert(sizeof(WideGpuFrameConstants) <= FrameSize, "the frame constants fit");

constexpr u32 Align(u32 v, u32 a)
{
    return (v + a - 1) & ~(a - 1);
}

void Debug(void*, const char* context, DkResult result, const char* message)
{
    static int reported = 0;
    if (reported++ < 20)
    {
        fprintf(stderr, "[widegpu] deko3D error %d in %s: %s\n", (int)result, context ? context : "?", message ? message : "");
        fflush(stderr);
    }
}

struct DkshHeader
{
    u32 Magic, HeaderSize, ControlSize, CodeSize, ProgramsOffset, NumPrograms;
};

// What every renderer shares: the device, the queue its commands go to, the shaders. (Two
// renderers are in use at once: the widescreen view's and the DS-resolution picture's.)
struct Shared
{
    DkDevice Device = nullptr;
    DkMemBlock CodeMem = nullptr;
    DkShader MainVsh, MainFsh, FinalVsh, FinalFsh;
    DkShader EarlyFsh;     // the fragment shader with early tests (WideGpuDraw::Early)
    bool HaveEarly = false;
    int Users = 0;
    // The fragment shader reads the texture and palette memory as buffer textures, through the
    // texture cache (measured in the city: 0.4 ms less a picture than storage buffers, the same
    // picture); SSR_WIDEGPU_SHADER=ssbo in the environment goes back to storage buffers.
    // For measuring on the Switch (in the environment, e.g. through a test run's --env):
    // SSR_WIDEGPU_ZCULL=1, the graphics chip's coarse depth test (Zcull), which rejects hidden
    // pixels in blocks before they are shaded (no gain measured in the city).
    // And to find what the time goes on, with wrong pictures: SSR_WIDEGPU_PLAIN=1, no textures;
    // SSR_WIDEGPU_NOATTR=1, the second target (depth and fog flag, for the fog) not written;
    // SSR_WIDEGPU_DEPTH=24, a 32-bit depth buffer (24-bit fixed point) instead of 64 bits.
    bool TexBuf = false, Zcull = false, Plain = false, NoAttr = false, Depth24 = false;
    // The graphics chip's own compression of the targets (less memory traffic, nearly free
    // clears), and the final pass written straight into the memory the processor reads (a
    // pitch-linear target over the slot) without a copy after it. Measured in a battle, a
    // 720-line picture every frame: 12.7 ms instead of 15.2, frames on time 79% instead of 53%,
    // the same picture. (SSR_WIDEGPU_COMPRESS=0, SSR_WIDEGPU_DIRECT=0: without them.)
    bool Compress = true, Direct = true;
    // The textures decoded beforehand (WideGpuAtlas); SSR_WIDEGPU_ATLAS=0: decoded a pixel at a
    // time, as before.
    bool NoAtlas = false;
};
Shared* Sh = nullptr;

DkMemBlock MakeBlock(u32 size, u32 flags)
{
    DkMemBlockMaker m;
    dkMemBlockMakerDefaults(&m, Sh->Device, Align(size, DK_MEMBLOCK_ALIGNMENT));
    m.flags = flags;
    return dkMemBlockCreate(&m);
}

bool LoadShader(DkShader& shader, const unsigned char* file, size_t size, u32& used)
{
    DkshHeader h;
    if (size < sizeof(h)) return false;
    memcpy(&h, file, sizeof(h));
    if (h.ControlSize + h.CodeSize > size) return false;
    const u32 at = Align(used, DK_SHADER_CODE_ALIGNMENT);
    if (at + h.CodeSize + DK_SHADER_CODE_UNUSABLE_SIZE > dkMemBlockGetSize(Sh->CodeMem)) return false;
    memcpy((u8*)dkMemBlockGetCpuAddr(Sh->CodeMem) + at, file + h.ControlSize, h.CodeSize);
    used = at + h.CodeSize;
    DkShaderMaker m;
    dkShaderMakerDefaults(&m, Sh->CodeMem, at);
    m.control = file;
    m.programId = 0;
    dkShaderInitialize(&shader, &m);
    return dkShaderIsValid(&shader);
}

void ReleaseShared()
{
    if (!Sh || --Sh->Users > 0) return;
    if (Sh->CodeMem) dkMemBlockDestroy(Sh->CodeMem);
    if (Sh->Device) dkDeviceDestroy(Sh->Device);
    delete Sh;
    Sh = nullptr;
}

bool AcquireShared(std::string& info)
{
    if (Sh)
    {
        Sh->Users++;
        return true;
    }
    Sh = new Shared();
    Sh->Users = 1;
    DkDeviceMaker dm;
    dkDeviceMakerDefaults(&dm);
    dm.cbDebug = Debug;
    Sh->Device = dkDeviceCreate(&dm);
    if (!Sh->Device)
    {
        info = "no deko3D device";
        ReleaseShared();
        return false;
    }
    const char* shader = getenv("SSR_WIDEGPU_SHADER");
    const char* zcull = getenv("SSR_WIDEGPU_ZCULL");
    Sh->TexBuf = !(shader && !strcmp(shader, "ssbo"));
    Sh->Zcull = zcull && !strcmp(zcull, "1");
    auto on = [](const char* name, const char* value) {
        const char* v = getenv(name);
        return v && !strcmp(v, value);
    };
    Sh->Plain = on("SSR_WIDEGPU_PLAIN", "1");
    Sh->NoAttr = on("SSR_WIDEGPU_NOATTR", "1");
    Sh->Depth24 = on("SSR_WIDEGPU_DEPTH", "24");
    Sh->Compress = !on("SSR_WIDEGPU_COMPRESS", "0");
    Sh->Direct = !on("SSR_WIDEGPU_DIRECT", "0");
    Sh->NoAtlas = on("SSR_WIDEGPU_ATLAS", "0");
    if (Sh->NoAtlas) fprintf(stderr, "[widegpu] deko3D: textures decoded a pixel at a time\n");
    if (!Sh->Compress || !Sh->Direct)
        fprintf(stderr, "[widegpu] deko3D:%s%s\n", Sh->Compress ? "" : " targets not compressed", Sh->Direct ? "" : " the picture copied out");
    if (Sh->Plain || Sh->NoAttr || Sh->Depth24)
        fprintf(stderr, "[widegpu] deko3D, for measuring (the pictures are wrong):%s%s%s\n", Sh->Plain ? " no textures" : "",
            Sh->NoAttr ? " no depth/fog target" : "", Sh->Depth24 ? " 32-bit depth buffer" : "");
    Sh->CodeMem = MakeBlock(256 * 1024, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code);
    u32 used = 0;
    // (The buffer-texture variant is left empty by the build if it did not compile.)
    if (Sh->CodeMem && Sh->TexBuf && !LoadShader(Sh->MainFsh, wide_fsh_texbuf_dksh, sizeof(wide_fsh_texbuf_dksh), used))
    {
        fprintf(stderr, "[widegpu] deko3D: the buffer-texture fragment shader is not in this build\n");
        Sh->TexBuf = false;
    }
    // (Without the early-test variant, the draws marked early are drawn with the usual shader:
    // the same picture, only slower.)
    if (Sh->CodeMem)
        Sh->HaveEarly = Sh->TexBuf ? LoadShader(Sh->EarlyFsh, wide_fsh_texbuf_early_dksh, sizeof(wide_fsh_texbuf_early_dksh), used)
                                   : LoadShader(Sh->EarlyFsh, wide_fsh_early_dksh, sizeof(wide_fsh_early_dksh), used);
    if (!Sh->HaveEarly) fprintf(stderr, "[widegpu] deko3D: the early-test fragment shader is not in this build\n");
    fprintf(stderr, "[widegpu] deko3D: %s fragment shader, Zcull %s\n", Sh->TexBuf ? "buffer-texture" : "storage-buffer",
        Sh->Zcull ? "on" : "off");
    if (!Sh->CodeMem || !LoadShader(Sh->MainVsh, wide_vsh_dksh, sizeof(wide_vsh_dksh), used) ||
        !(Sh->TexBuf || LoadShader(Sh->MainFsh, wide_fsh_dksh, sizeof(wide_fsh_dksh), used)) ||
        !LoadShader(Sh->FinalVsh, final_vsh_dksh, sizeof(final_vsh_dksh), used) ||
        !LoadShader(Sh->FinalFsh, final_fsh_dksh, sizeof(final_fsh_dksh), used))
    {
        info = "the deko3D shaders could not be loaded";
        ReleaseShared();
        return false;
    }
    return true;
}

class WideGpuDeko : public WideGpuBackend
{
public:
    const char* Name() const override { return "deko3D"; }

    bool Init(std::string& info, bool small)
    {
        Small = small;
        if (!AcquireShared(info)) return false;
        HaveShared = true;
        // A queue of its own (the widescreen view's and the DS-resolution picture's renderers
        // each have one): the graphics chip takes turns between queues, so that the small DS
        // picture, wanted within the frame, is not stuck behind a large widescreen picture that
        // may be shown a frame later (GPU3D_Wide.cpp's OnRender). The turns were long: in a
        // battle the DS picture waited 2-3 ms to start, and the emulation with it (the frame
        // at 17-18 ms, about 52 fps). Its queue has high priority and the widescreen view's low
        // (SSR_WIDEGPU_QUEUES=plain: both medium, as before, to compare).
        DkQueueMaker qm;
        dkQueueMakerDefaults(&qm, Sh->Device);
        const char* queues = getenv("SSR_WIDEGPU_QUEUES");
        const bool plain = queues && !strcmp(queues, "plain");
        const u32 prio = plain ? DkQueueFlags_MediumPrio : small ? DkQueueFlags_HighPrio : DkQueueFlags_LowPrio;
        qm.flags = DkQueueFlags_Graphics | prio | (Sh->Zcull ? DkQueueFlags_EnableZcull : DkQueueFlags_DisableZcull);
        Queue = dkQueueCreate(&qm);
        if (!Queue)
        {
            info = "no deko3D queue";
            return false;
        }
        CmdMem = Block(CmdSize, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
        DataMem = Block(DataSize, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
        if (!CmdMem || !DataMem)
        {
            info = "deko3D memory could not be had";
            return false;
        }
        DkCmdBufMaker cm;
        dkCmdBufMakerDefaults(&cm, Sh->Device);
        Cmd = dkCmdBufCreate(&cm);
        if (!Cmd)
        {
            info = "no deko3D command buffer";
            return false;
        }
        Data = (u8*)dkMemBlockGetCpuAddr(DataMem);
        DataAddr = dkMemBlockGetGpuAddr(DataMem);

        // The final pass reads the targets texel by texel.
        DkSampler sampler;
        dkSamplerDefaults(&sampler);
        sampler.minFilter = DkFilter_Nearest;
        sampler.magFilter = DkFilter_Nearest;
        sampler.wrapMode[0] = sampler.wrapMode[1] = sampler.wrapMode[2] = DkWrapMode_ClampToEdge;
        DkSamplerDescriptor sd;
        dkSamplerDescriptorInitialize(&sd, &sampler);
        memcpy(Data + OffSamplerDesc, &sd, sizeof(sd));

        // The texture memory (as bytes and as 16-bit texels) and the palette memory (16-bit) as
        // buffer textures over the data memory.
        auto buffer = [&](DkImage& image, DkImageFormat format, u32 offset, u32 texels, u32 id) {
            DkImageLayoutMaker lm;
            dkImageLayoutMakerDefaults(&lm, Sh->Device);
            lm.type = DkImageType_Buffer;
            lm.format = format;
            lm.dimensions[0] = texels;
            DkImageLayout layout;
            dkImageLayoutInitialize(&layout, &lm);
            dkImageInitialize(&image, &layout, DataMem, offset);
            DkImageView view;
            dkImageViewDefaults(&view, &image);
            DkImageDescriptor desc;
            dkImageDescriptorInitialize(&desc, &view, false, false);
            memcpy(Data + OffImageDesc + id * sizeof(desc), &desc, sizeof(desc));
        };
        buffer(Tex8Image, DkImageFormat_R8_Uint, OffTex, 512 * 1024, ImgTex8);
        buffer(Tex16Image, DkImageFormat_R16_Uint, OffTex, 256 * 1024, ImgTex16);
        buffer(Pal16Image, DkImageFormat_R16_Uint, OffPal, 64 * 1024, ImgPal16);
        buffer(AtlasImage, DkImageFormat_R32_Uint, OffAtlas, WideGpuAtlas::Capacity, ImgAtlas);
        return true;
    }

    ~WideGpuDeko() override
    {
        if (!HaveShared) return;
        if (Pending) dkFenceWait(&Fence, 1000000000ll);
        if (Queue)
        {
            dkQueueWaitIdle(Queue);
            dkQueueDestroy(Queue);
        }
        FreeTargets();
        for (Slot& s : Slots)
            if (s.Mem) dkMemBlockDestroy(s.Mem);
        if (Cmd) dkCmdBufDestroy(Cmd);
        if (DataMem) dkMemBlockDestroy(DataMem);
        if (CmdMem) dkMemBlockDestroy(CmdMem);
        ReleaseShared();
    }

    u32* Target(int slot, int w, int h) override
    {
        Slot& s = Slots[slot];
        const u32 size = (u32)w * h * 4;
        if (s.Mem && s.Cap < size)
        {
            // (This slot is not the one on show, nor the one before: GPU3D_Wide.cpp.)
            dkMemBlockDestroy(s.Mem);
            s.Mem = nullptr;
            s.Cap = 0;
            s.ImageW = s.ImageH = 0;
        }
        if (!s.Mem)
        {
            s.Mem = Block(size, DkMemBlockFlags_CpuCached | DkMemBlockFlags_GpuCached);
            s.Cap = s.Mem ? size : 0;
            s.ImageW = s.ImageH = 0;
        }
        s.Size = s.Mem ? size : 0;
        return s.Mem ? (u32*)dkMemBlockGetCpuAddr(s.Mem) : nullptr;
    }

    bool Submit(WideGpuFrame& f, int slot) override
    {
        if (f.Vertices.size() > MaxVertices || f.Polygons.size() > MaxPolygons) return false;
        if (!Slots[slot].Mem || Slots[slot].Size != (u32)f.W * f.H * 4) return false;
        const double t0 = Now();
        if (f.W != W || f.H != H)
        {
            if (!MakeTargets(f.W, f.H)) return false;
        }
        // deko3D's rows run from the top, as in memory (see wide_vsh.glsl).
        f.Constants.Scale[2] = 1.0f;
        f.Constants.Scale[3] = 0.0f; // depth from 0 to 1
        if (Sh->Plain) f.Constants.Fog[3] |= 4;

        memcpy(Data + OffFrame, &f.Constants, sizeof(f.Constants));
        memcpy(Data + OffPolygons, f.Polygons.data(), f.Polygons.size() * sizeof(WideGpuPolygon));
        // (The texture memory only when it may have changed.)
        if (!f.TexVersion || f.TexVersion != TexVersion)
        {
            memcpy(Data + OffTex, f.TexMem, 512 * 1024);
            memcpy(Data + OffPal, f.PalMem, 128 * 1024);
            TexVersion = f.TexVersion;
        }
        memcpy(Data + OffVertices, f.Vertices.data(), f.Vertices.size() * sizeof(WideGpuVertex));
        // The textures decoded this frame (all of them, the first time this renderer sees the
        // atlas).
        if (f.AtlasOn)
        {
            const WideGpuAtlas& a = f.Atlas;
            if (!AtlasSynced)
            {
                memcpy(Data + OffAtlas, a.Texels.data(), (size_t)a.Used * 4);
                AtlasSynced = true;
            }
            else
                for (const auto& [first, n] : a.Uploads)
                {
                    memcpy(Data + OffAtlas + (size_t)first * 4, a.Texels.data() + first, (size_t)n * 4);
                    T.Decoded += n;
                }
        }
        const double t1 = Now();

        dkCmdBufClear(Cmd);
        dkCmdBufAddMemory(Cmd, CmdMem, 0, CmdSize);
        // What the processor wrote since the last frame is read afresh.
        dkCmdBufBarrier(Cmd, DkBarrier_None, DkInvalidateFlags_Shader | DkInvalidateFlags_Image |
            DkInvalidateFlags_Descriptors | DkInvalidateFlags_L2Cache);
        // Where the graphics chip's time goes: times at the start and after each stage, and how
        // many pixels were shaded and passed the depth test.
        auto report = [&](DkCounter counter, Reports at) { dkCmdBufReportCounter(Cmd, counter, DataAddr + OffReports + at * 16); };
        dkCmdBufResetCounter(Cmd, DkCounter_FragmentShaderInvocations);
        dkCmdBufResetCounter(Cmd, DkCounter_SamplesPassed);
        report(DkCounter_TimestampPipelineTop, RepStart);

        DkImageView colorView, attrView, depthView, finalView;
        dkImageViewDefaults(&colorView, &ColorImage);
        dkImageViewDefaults(&attrView, &AttrImage);
        dkImageViewDefaults(&depthView, &DepthImage);
        // (The final pass's target: the slot's own memory as a pitch-linear image, or an image
        // of its own whose picture is copied there after. A pitch-linear target needs rows a
        // multiple of 32 bytes, and a height a multiple of 16 here: at 752x423, the 423 lines
        // LOWER DETAIL WHEN BUSY comes down to from 720, the console crashed at once; 1280x720
        // and 1152x648 drew fine, and every size does through the copy.)
        const bool direct = Sh->Direct && ((u32)W * 4) % DK_IMAGE_LINEAR_STRIDE_ALIGNMENT == 0 && H % 16 == 0 &&
            SlotImage(slot);
        dkImageViewDefaults(&finalView, direct ? &Slots[slot].Image : &FinalImage);
        const DkImageView* targets[2] = { &colorView, &attrView };
        dkCmdBufBindRenderTargets(Cmd, targets, 2, &depthView);
        const DkViewport vp = { 0, 0, (float)W, (float)H, 0, 1 };
        dkCmdBufSetViewports(Cmd, 0, &vp, 1);
        const DkScissor sc = { 0, 0, (u32)W, (u32)H };
        dkCmdBufSetScissors(Cmd, 0, &sc, 1);

        DkColorWriteState all;
        dkColorWriteStateDefaults(&all);
        dkCmdBufBindColorWriteState(Cmd, &all);
        dkCmdBufClearColor(Cmd, 0, DkColorMask_RGBA, f.ClearColor);
        const u32 clearAttr[4] = { f.ClearDepth, f.ClearFog, 0, 0 };
        dkCmdBufClearColor(Cmd, 1, DkColorMask_RGBA, clearAttr);
        dkCmdBufClearDepthStencil(Cmd, true, f.ClearDepthValue, 0xFF, f.ClearStencil);
        report(DkCounter_Timestamp, RepCleared);

        const DkShader* main[2] = { &Sh->MainVsh, &Sh->MainFsh };
        const DkShader* early[2] = { &Sh->MainVsh, &Sh->EarlyFsh };
        dkCmdBufBindShaders(Cmd, DkStageFlag_GraphicsMask, main, 2);
        bool earlyBound = false;
        const DkBufExtents frame = { DataAddr + OffFrame, FrameSize };
        const DkBufExtents draw = { DataAddr + OffDraw, DrawSize };
        const DkBufExtents ubos[2] = { frame, draw };
        dkCmdBufBindUniformBuffers(Cmd, DkStage_Vertex, 0, ubos, 2);
        dkCmdBufBindUniformBuffers(Cmd, DkStage_Fragment, 0, ubos, 2);
        const DkBufExtents vertices = { DataAddr + OffVertices, Align((u32)std::max<size_t>(16, f.Vertices.size() * sizeof(WideGpuVertex)), 16) };
        const DkBufExtents polygons = { DataAddr + OffPolygons, MaxPolygons * (u32)sizeof(WideGpuPolygon) };
        const DkBufExtents vert[2] = { vertices, polygons };
        dkCmdBufBindStorageBuffers(Cmd, DkStage_Vertex, 0, vert, 2);
        dkCmdBufBindImageDescriptorSet(Cmd, DataAddr + OffImageDesc, ImageCount);
        dkCmdBufBindSamplerDescriptorSet(Cmd, DataAddr + OffSamplerDesc, 1);
        if (Sh->TexBuf)
        {
            const DkResHandle mem[4] = { dkMakeTextureHandle(ImgTex8, 0), dkMakeTextureHandle(ImgTex16, 0),
                dkMakeTextureHandle(ImgPal16, 0), dkMakeTextureHandle(ImgAtlas, 0) };
            dkCmdBufBindTextures(Cmd, DkStage_Fragment, 0, mem, 4);
        }
        else
        {
            const DkBufExtents frag[3] = {
                polygons,
                { DataAddr + OffTex, 512 * 1024 },
                { DataAddr + OffPal, 128 * 1024 },
            };
            dkCmdBufBindStorageBuffers(Cmd, DkStage_Fragment, 1, frag, 3);
        }

        DkRasterizerState rs;
        dkRasterizerStateDefaults(&rs);
        rs.cullMode = DkFace_None;
        dkCmdBufBindRasterizerState(Cmd, &rs);
        DkBlendState blend;
        dkBlendStateDefaults(&blend);
        dkBlendStateSetOps(&blend, DkBlendOp_Add, DkBlendOp_Max);
        dkBlendStateSetFactors(&blend, DkBlendFactor_SrcAlpha, DkBlendFactor_InvSrcAlpha, DkBlendFactor_One, DkBlendFactor_One);
        dkCmdBufBindBlendStates(Cmd, 0, &blend, 1);

        for (const WideGpuDraw& d : f.Draws)
        {
            const bool wantEarly = d.Early && Sh->HaveEarly;
            if (wantEarly != earlyBound)
            {
                dkCmdBufBindShaders(Cmd, DkStageFlag_GraphicsMask, wantEarly ? early : main, 2);
                earlyBound = wantEarly;
            }
            const u32 params[4] = { d.First, d.Pixels, (u32)d.DepthBias, f.WDepth ? 1u : 0u };
            dkCmdBufPushConstants(Cmd, draw.addr, draw.size, 0, sizeof(params), params);
            DkDepthStencilState ds;
            dkDepthStencilStateDefaults(&ds);
            ds.depthTestEnable = true;
            ds.depthWriteEnable = d.DepthWrite;
            ds.depthCompareOp = f.WDepth ? (d.DepthLequal ? DkCompareOp_Gequal : DkCompareOp_Greater)
                                         : (d.DepthLequal ? DkCompareOp_Lequal : DkCompareOp_Less);
            ds.stencilTestEnable = true;
            const DkCompareOp op = d.StencilTest == WideGpuStencil::Always ? DkCompareOp_Always :
                d.StencilTest == WideGpuStencil::Equal ? DkCompareOp_Equal : DkCompareOp_NotEqual;
            ds.stencilFrontFailOp = ds.stencilBackFailOp = DkStencilOp_Keep;
            ds.stencilFrontDepthFailOp = ds.stencilBackDepthFailOp = DkStencilOp_Keep;
            ds.stencilFrontPassOp = ds.stencilBackPassOp = DkStencilOp_Replace;
            ds.stencilFrontCompareOp = ds.stencilBackCompareOp = op;
            dkCmdBufBindDepthStencilState(Cmd, &ds);
            dkCmdBufSetStencil(Cmd, DkFace_FrontAndBack, 0xFF, d.StencilRef, d.StencilMask);
            DkColorState cs;
            dkColorStateDefaults(&cs);
            dkColorStateSetBlendEnable(&cs, 0, d.Blend);
            dkCmdBufBindColorState(Cmd, &cs);
            DkColorWriteState cw;
            dkColorWriteStateDefaults(&cw);
            dkColorWriteStateSetMask(&cw, 0, DkColorMask_RGBA);
            dkColorWriteStateSetMask(&cw, 1, Sh->NoAttr ? 0 : (d.WriteDepthAttr ? DkColorMask_R : 0) | (d.WriteFogAttr ? DkColorMask_G : 0));
            dkCmdBufBindColorWriteState(Cmd, &cw);
            dkCmdBufDraw(Cmd, DkPrimitive_Triangles, d.Count, 1, 0, 0);
        }

        report(DkCounter_Timestamp, RepDrawn);
        report(DkCounter_FragmentShaderInvocations, RepFragments);
        report(DkCounter_SamplesPassed, RepSamples);

        // The final pass reads what was drawn.
        dkCmdBufBarrier(Cmd, DkBarrier_Fragments, DkInvalidateFlags_Image);
        const DkImageView* finalTarget[1] = { &finalView };
        dkCmdBufBindRenderTargets(Cmd, finalTarget, 1, nullptr);
        DkDepthStencilState off;
        dkDepthStencilStateDefaults(&off);
        off.depthTestEnable = false;
        off.depthWriteEnable = false;
        off.stencilTestEnable = false;
        dkCmdBufBindDepthStencilState(Cmd, &off);
        DkColorState plain;
        dkColorStateDefaults(&plain);
        dkCmdBufBindColorState(Cmd, &plain);
        dkCmdBufBindColorWriteState(Cmd, &all);
        const DkShader* fin[2] = { &Sh->FinalVsh, &Sh->FinalFsh };
        dkCmdBufBindShaders(Cmd, DkStageFlag_GraphicsMask, fin, 2);
        const DkResHandle tex[2] = { dkMakeTextureHandle(ImgColor, 0), dkMakeTextureHandle(ImgAttr, 0) };
        dkCmdBufBindTextures(Cmd, DkStage_Fragment, 0, tex, 2);
        dkCmdBufDraw(Cmd, DkPrimitive_Triangles, 3, 1, 0, 0);
        report(DkCounter_Timestamp, RepFinal);

        // The picture to memory the processor reads.
        dkCmdBufBarrier(Cmd, DkBarrier_Fragments, 0);
        if (!direct)
        {
            const DkImageRect rect = { 0, 0, 0, (u32)W, (u32)H, 1 };
            const DkCopyBuf dst = { dkMemBlockGetGpuAddr(Slots[slot].Mem), (u32)W * 4, 0 };
            dkCmdBufCopyImageToBuffer(Cmd, &finalView, &rect, &dst, 0);
        }
        dkCmdBufBarrier(Cmd, DkBarrier_Full, 0);
        report(DkCounter_Timestamp, RepCopied);
        dkCmdBufBarrier(Cmd, DkBarrier_Full, DkInvalidateFlags_L2Cache);
        dkCmdBufSignalFence(Cmd, &Fence, true);
        dkQueueSubmitCommands(Queue, dkCmdBufFinishList(Cmd));
        SubmitTime = dkDeviceGetCurrentTimestamp(Sh->Device);
        dkQueueFlush(Queue);
        Pending = true;
        const double t2 = Now();
        T.Copy += t1 - t0;
        T.Record += t2 - t1;
        T.Draws += (u32)f.Draws.size();
        T.Triangles += (u32)f.Vertices.size() / 3;
        return true;
    }

    bool TakesAtlas() const override { return Sh->TexBuf && !Sh->NoAtlas; }

    bool Done(int slot) override
    {
        return !Pending || dkFenceWait(&Fence, 0) == DkResult_Success;
    }

    void Fetch(int slot) override
    {
        if (!Pending) return;
        Pending = false;
        const double t0 = Now();
        if (dkFenceWait(&Fence, 1000000000ll) != DkResult_Success)
        {
            static int reported = 0;
            if (reported++ < 5) fprintf(stderr, "[widegpu] the graphics chip did not finish a picture within a second\n");
            return;
        }
        const double t1 = Now();
        // (Cached memory: lines the processor read before are dropped, so that it reads the
        // graphics chip's picture.)
        dkMemBlockFlushCpuCache(Slots[slot].Mem, 0, Slots[slot].Size);
        const double t2 = Now();
        T.Wait += t1 - t0;
        T.Flush += t2 - t1;
        // (The reports: the data memory is not cached by the processor.)
        u64 rep[ReportCount][2];
        memcpy(rep, Data + OffReports, sizeof(rep));
        auto ms = [](u64 from, u64 to) { return to > from ? (double)dkTimestampToNs(to - from) / 1e6 : 0.0; };
        auto gpuMs = [&](Reports a, Reports b) { return ms(rep[a][1], rep[b][1]); };
        // (From the commands' submission to the graphics chip starting on them: the time it
        // spent on other work first, such as SDL's drawing of the last frame.)
        T.GpuQueue += ms(SubmitTime, rep[RepStart][1]);
        T.GpuClear += gpuMs(RepStart, RepCleared);
        T.GpuDraw += gpuMs(RepCleared, RepDrawn);
        T.GpuFinal += gpuMs(RepDrawn, RepFinal);
        T.GpuCopy += gpuMs(RepFinal, RepCopied);
        T.GpuTotal += gpuMs(RepStart, RepCopied);
        if (!Small) WideGpuBusyNs += (u64)(gpuMs(RepStart, RepCopied) * 1e6);
        T.Fragments += rep[RepFragments][0];
        T.Samples += rep[RepSamples][0];
        // Where the time goes, now and then in the log (every 240 pictures: a test run of a few
        // hundred frames gets some).
        if (++T.Pictures == 240)
        {
            const double n = T.Pictures;
            fprintf(stderr, "[widegpu] per picture (%dx%d, over %u): copying the data %.2f ms, making the commands %.2f ms, "
                "waiting for the graphics chip %.2f ms, cache %.2f ms; %u triangles, %u draws; %u texels decoded\n", W, H, T.Pictures,
                T.Copy / n, T.Record / n, T.Wait / n, T.Flush / n, (u32)(T.Triangles / n), (u32)(T.Draws / n), (u32)(T.Decoded / n));
            fprintf(stderr, "[widegpu]   on the graphics chip: %.2f ms before it started, %.2f ms from starting to the picture copied: clearing %.2f, "
                "polygons %.2f, final pass %.2f, copy %.2f ms; %.2f million pixels shaded, %.2f million passed the depth test "
                "(the picture has %.2f million)\n", T.GpuQueue / n, T.GpuTotal / n, T.GpuClear / n, T.GpuDraw / n, T.GpuFinal / n, T.GpuCopy / n,
                T.Fragments / n / 1e6, T.Samples / n / 1e6, (double)W * H / 1e6);
            T = Times();
        }
    }

private:
    static DkMemBlock Block(u32 size, u32 flags) { return MakeBlock(size, flags); }

    bool Image(DkImage& image, DkMemBlock& mem, u32& cap, DkImageFormat format, int w, int h, u32 flags)
    {
        DkImageLayoutMaker lm;
        dkImageLayoutMakerDefaults(&lm, Sh->Device);
        lm.flags = flags;
        lm.format = format;
        lm.dimensions[0] = (u32)w;
        lm.dimensions[1] = (u32)h;
        DkImageLayout layout;
        dkImageLayoutInitialize(&layout, &lm);
        const u32 need = (u32)dkImageLayoutGetSize(&layout) + dkImageLayoutGetAlignment(&layout);
        if (mem && cap < need)
        {
            dkMemBlockDestroy(mem);
            mem = nullptr;
            cap = 0;
        }
        if (!mem)
        {
            mem = Block(need, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
            cap = mem ? need : 0;
        }
        if (!mem) return false;
        dkImageInitialize(&image, &layout, mem, 0);
        return true;
    }

    static double Now()
    {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void FreeTargets()
    {
        for (DkMemBlock* m : { &ColorMem, &AttrMem, &DepthMem, &FinalMem })
        {
            if (*m) dkMemBlockDestroy(*m);
            *m = nullptr;
        }
        ColorCap = AttrCap = DepthCap = FinalCap = 0;
        W = H = 0;
    }

    bool MakeTargets(int w, int h)
    {
        dkQueueWaitIdle(Queue);
        const u32 render = DkImageFlags_UsageRender | (Sh->Compress ? DkImageFlags_HwCompression : 0);
        if (!Image(ColorImage, ColorMem, ColorCap, DkImageFormat_RGBA8_Unorm, w, h, render) ||
            !Image(AttrImage, AttrMem, AttrCap, DkImageFormat_RG32_Uint, w, h, render) ||
            !Image(DepthImage, DepthMem, DepthCap, Sh->Depth24 ? DkImageFormat_Z24S8 : DkImageFormat_ZF32_X24S8, w, h, render) ||
            !Image(FinalImage, FinalMem, FinalCap, DkImageFormat_BGRA8_Unorm, w, h, DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine))
        {
            fprintf(stderr, "[widegpu] deko3D images of %dx%d could not be had\n", w, h);
            FreeTargets();
            return false;
        }
        // The final pass's two textures.
        DkImageView colorView, attrView;
        dkImageViewDefaults(&colorView, &ColorImage);
        dkImageViewDefaults(&attrView, &AttrImage);
        DkImageDescriptor desc[2];
        dkImageDescriptorInitialize(&desc[0], &colorView, false, false);
        dkImageDescriptorInitialize(&desc[1], &attrView, false, false);
        memcpy(Data + OffImageDesc + ImgColor * sizeof(desc[0]), desc, sizeof(desc));
        W = w;
        H = h;
        return true;
    }

    bool HaveShared = false;
    u64 TexVersion = 0;
    DkMemBlock CmdMem = nullptr, DataMem = nullptr;
    DkMemBlock ColorMem = nullptr, AttrMem = nullptr, DepthMem = nullptr, FinalMem = nullptr;
    u32 ColorCap = 0, AttrCap = 0, DepthCap = 0, FinalCap = 0;
    // The pictures (see Target).
    struct Slot
    {
        DkMemBlock Mem = nullptr;
        u32 Size = 0, Cap = 0;
        DkImage Image;            // its memory as a pitch-linear render target (SSR_WIDEGPU_DIRECT)
        int ImageW = 0, ImageH = 0;
    };
    // Sets up slot's memory as a pitch-linear BGRA8 render target of the current size.
    bool SlotImage(int slot)
    {
        Slot& s = Slots[slot];
        if (!s.Mem || s.Size != (u32)W * H * 4) return false;
        if (s.ImageW == W && s.ImageH == H) return true;
        DkImageLayoutMaker lm;
        dkImageLayoutMakerDefaults(&lm, Sh->Device);
        lm.flags = DkImageFlags_PitchLinear | DkImageFlags_UsageRender;
        lm.format = DkImageFormat_BGRA8_Unorm;
        lm.dimensions[0] = (u32)W;
        lm.dimensions[1] = (u32)H;
        lm.pitchStride = (u32)W * 4;
        DkImageLayout layout;
        dkImageLayoutInitialize(&layout, &lm);
        if (dkImageLayoutGetSize(&layout) > s.Cap) return false;
        dkImageInitialize(&s.Image, &layout, s.Mem, 0);
        s.ImageW = W;
        s.ImageH = H;
        return true;
    }
    Slot Slots[3];
    struct Times
    {
        double Copy = 0, Record = 0, Wait = 0, Flush = 0;
        double GpuQueue = 0, GpuClear = 0, GpuDraw = 0, GpuFinal = 0, GpuCopy = 0, GpuTotal = 0;
        u64 Fragments = 0, Samples = 0, Decoded = 0;
        u64 Triangles = 0, Draws = 0;
        u32 Pictures = 0;
    };
    Times T;
    DkCmdBuf Cmd = nullptr;
    DkImage ColorImage, AttrImage, DepthImage, FinalImage;
    DkImage Tex8Image, Tex16Image, Pal16Image, AtlasImage;
    bool AtlasSynced = false; // the atlas in memory matches the frame's (see Submit)
    DkFence Fence;
    u8* Data = nullptr;
    DkGpuAddr DataAddr = 0;
    int W = 0, H = 0;
    bool Pending = false;
    u64 SubmitTime = 0; // the graphics chip's clock at the last Submit
    DkQueue Queue = nullptr;
    bool Small = false; // the DS-resolution picture's renderer
};
}

std::unique_ptr<WideGpuBackend> WideGpuMakeDeko(std::string& info, bool small)
{
    std::unique_ptr<WideGpuDeko> r(new WideGpuDeko());
    if (!r->Init(info, small)) return nullptr;
    return r;
}
}
#endif
