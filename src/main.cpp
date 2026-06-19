// ============================================================
//  Sightline — Video Player UI
//  Dear ImGui + Win32 + Direct3D9
//  Single EXE, compatible Windows XP SP3 x86 -> Windows 11
//  Compiler: MinGW-w64 i686
// ============================================================

#define WINVER       0x0501
#define _WIN32_WINNT 0x0501
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d9.h>
// WIC GUIDs are resolved via libuuid.a at link time (-luuid).
// Do NOT use #define INITGUID here — it does not work for WIC
// GUIDs with mingw-w64; -luuid in the link step is the fix.
#include <wincodec.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx9.h"

// ── Embedded Electrolize font ─────────────────────────────────
// font_electrolize.h is always present (stub or real).
// CI overwrites it with real TTF bytes before compiling.
#include "font_electrolize.h"
static inline bool HasElectrolize() { return electrolize_regular_ttf_len > 4; }

// ── Embedded Font Awesome 6 Solid (icons only) ───────────────
// font_icons.h is always present (stub or real).
// CI overwrites it with real TTF bytes before compiling.
#include "font_icons.h"
static inline bool HasIconFont() { return fa6_solid_ttf_len > 4; }

// ── Embedded logo PNG ─────────────────────────────────────────
#if __has_include("logo_data.h")
  #include "logo_data.h"
  static inline bool HasLogo() { return sightline_logo_png_len > 4; }
#else
  static const unsigned char sightline_logo_png[]   = {0};
  static const unsigned int  sightline_logo_png_len = 0;
  static inline bool HasLogo() { return false; }
#endif

#include <cstdio>
#include <cmath>
#include <cstring>
#include <string>

// ─── colour palette ──────────────────────────────────────────
#define COL32(r,g,b,a) IM_COL32(r,g,b,a)
static const ImU32 C_BG          = COL32(0x0C,0x10,0x14,255);
static const ImU32 C_SURFACE     = COL32(0x13,0x19,0x20,255);
static const ImU32 C_SURFACE2    = COL32(0x19,0x20,0x28,255);
static const ImU32 C_SURFACE3    = COL32(0x1E,0x26,0x2F,255);
static const ImU32 C_DIVIDER     = COL32(0x1B,0x22,0x29,255);
static const ImU32 C_BORDER      = COL32(255,255,255,18);
static const ImU32 C_BORDER_STR  = COL32(255,255,255,31);
static const ImU32 C_TEXT        = COL32(0xE6,0xED,0xF2,255);
static const ImU32 C_TEXT_MUTED  = COL32(0x8A,0x97,0xA3,255);
static const ImU32 C_TEXT_FAINT  = COL32(0x4D,0x5E,0x69,255);
static const ImU32 C_ACCENT      = COL32(0x4E,0xA8,0xA8,255);
static const ImU32 C_ACCENT_HOV  = COL32(0x62,0xBC,0xBC,255);
static const ImU32 C_ACCENT_SOFT = COL32(78,168,168,31);
static const ImU32 C_ACCENT_LINE = COL32(78,168,168,89);
static const ImU32 C_SUCCESS     = COL32(0x6B,0xAA,0x78,255);
static const ImU32 C_ERROR       = COL32(0xC9,0x6B,0x6B,255);
static const ImU32 C_BLACK       = COL32(0,0,0,255);
static const ImU32 C_WHITE       = COL32(0xFF,0xFF,0xFF,255);

static inline ImVec4 U32toV4(ImU32 c){
    return ImVec4(((c>>0)&0xFF)/255.f,((c>>8)&0xFF)/255.f,
                  ((c>>16)&0xFF)/255.f,((c>>24)&0xFF)/255.f);
}

// ─── glyph ranges (text font) ────────────────────────────────
static const ImWchar g_glyph_ranges[] = {
    0x0020,0x00FF, 0x0100,0x024F, 0x2000,0x206F, 0x20AC,0x20AC, 0,
};

// ─── glyph ranges (icon font — FA6 Solid) ────────────────────
// Font Awesome 6 Solid maps its glyphs into U+F000..U+F8FF.
// The E000 PUA block contains NO FA6 glyphs — using it caused
// every icon to render the same fallback/first glyph.
static const ImWchar g_icon_ranges[] = {
    0xF000, 0xF8FF,
    0,
};

// ─── Font Awesome 6 Solid codepoints ─────────────────────────
// UTF-8 encoding for U+F000-range: first byte is always 0xEF.
// Previous code used 0xEE (wrong block). All corrected below.
// Reference: https://fontawesome.com/icons?s=solid
#define ICO_PLAY           "\xEF\x81\x8B"   // U+F04B  fa-play
#define ICO_PAUSE          "\xEF\x81\x8C"   // U+F04C  fa-pause
#define ICO_BACKWARD       "\xEF\x81\x88"   // U+F048  fa-backward-step
#define ICO_FORWARD        "\xEF\x81\x91"   // U+F051  fa-forward-step
#define ICO_VOLUME_HIGH    "\xEF\x80\xA8"   // U+F028  fa-volume-high
#define ICO_EXPAND         "\xEF\x81\xA5"   // U+F065  fa-expand
#define ICO_THUMB_UP       "\xEF\x85\xA4"   // U+F164  fa-thumbs-up
#define ICO_THUMB_DOWN     "\xEF\x85\xA5"   // U+F165  fa-thumbs-down
#define ICO_SHARE          "\xEF\x81\xA4"   // U+F064  fa-share
#define ICO_DOWNLOAD       "\xEF\x80\x99"   // U+F019  fa-download
#define ICO_ELLIPSIS_H     "\xEF\x85\x81"   // U+F141  fa-ellipsis
#define ICO_PLAY_CIRCLE    "\xEF\x85\x84"   // U+F144  fa-circle-play
#define ICO_GEAR           "\xEF\x80\x93"   // U+F013  fa-gear

// ─── font handles ────────────────────────────────────────────
static ImFont* g_font13   = nullptr;  // Electrolize 13 — body text
static ImFont* g_font16   = nullptr;  // Electrolize 16 — titles
static ImFont* g_fontIco  = nullptr;  // FA6 Solid — icons only

// ─── logo DX9 texture ────────────────────────────────────────
static LPDIRECT3DTEXTURE9 g_logoTex    = NULL;
static int                g_logoTexW   = 0;
static int                g_logoTexH   = 0;

// ─── app state ───────────────────────────────────────────────
static float  g_seek       = 0.175f;
static float  g_vol        = 0.80f;
static bool   g_playing    = true;
static int    g_tab        = 0;
static int    g_quality    = 0;
static bool   g_subscribed = false;
static char   g_search[128]= "rickroll";
static float  g_sideScroll = 0.f;

static const char* g_qualityOpts[] = {"2160p","1080p","720p","480p"};
static const int   g_qualityCount  = 4;

struct RelItem { const char* title; const char* channel; const char* views; const char* dur; ImU32 grad; };
static const RelItem g_related[] = {
    {"Rick Astley \xe2\x80\x94 Never Gonna Give You Up (Official Video) 4K Remaster","Rick Astley","1.7B views","3:33",COL32(0x13,0x19,0x20,255)},
    {"Best of 80s Music Legends \xe2\x80\x94 Team Formidable Mix","Radio 80s Hits","2.1M views","42:18",COL32(0x1a,0x20,0x30,255)},
    {"Rick Astley \xe2\x80\x94 Mix de Exitos Completo","Nanojams300","890K views","31:08",COL32(0x1a,0x28,0x20,255)},
    {"Daft Punk ft. Pharrell Williams \xe2\x80\x94 Get Lucky (Official)","pftpremials","1.4B views","3:44",COL32(0x20,0x18,0x28,255)},
    {"Shakira, Burna Boy \xe2\x80\x94 Dai Dai (Official Video)","shakiriaVEVO","420M views","3:59",COL32(0x28,0x18,0x20,255)},
    {"Phil Collins, Rod Stewart \xe2\x80\x94 Soulful 80s Mix","Soft Rock Soulful","3.2M views","4:07",COL32(0x18,0x20,0x28,255)},
    {"George Michael \xe2\x80\x94 Careless Whisper (Official 4K)","georgemc.feel","1.1B views","5:11",COL32(0x28,0x20,0x18,255)},
    {"Ultimate 80s Classics: Timeless Icons Mix","Flashback Grooves","5.7M views","58:01",COL32(0x18,0x18,0x28,255)},
    {"Eagles \xe2\x80\x94 Hotel California (Live 1977 Official)","Eagles","987M views","8:45",COL32(0x20,0x18,0x20,255)},
};
static const int g_relatedCount = 9;

// ─── D3D9 globals ────────────────────────────────────────────
static LPDIRECT3D9           g_pD3D    = NULL;
static LPDIRECT3DDEVICE9     g_pd3dDev = NULL;
static D3DPRESENT_PARAMETERS g_d3dpp   = {};
static HWND                  g_hwnd    = NULL;

static bool CreateDeviceD3D(HWND hWnd)
{
    if((g_pD3D = Direct3DCreate9(D3D_SDK_VERSION)) == NULL) return false;
    ZeroMemory(&g_d3dpp,sizeof(g_d3dpp));
    g_d3dpp.Windowed               = TRUE;
    g_d3dpp.SwapEffect             = D3DSWAPEFFECT_DISCARD;
    g_d3dpp.BackBufferFormat       = D3DFMT_UNKNOWN;
    g_d3dpp.EnableAutoDepthStencil = TRUE;
    g_d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
    g_d3dpp.PresentationInterval   = D3DPRESENT_INTERVAL_ONE;
    if(g_pD3D->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,hWnd,
        D3DCREATE_HARDWARE_VERTEXPROCESSING,&g_d3dpp,&g_pd3dDev)==D3D_OK) return true;
    if(g_pD3D->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,hWnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING,&g_d3dpp,&g_pd3dDev)==D3D_OK) return true;
    return false;
}
static void CleanupDevice()
{
    if(g_logoTex){ g_logoTex->Release(); g_logoTex=NULL; }
    if(g_pd3dDev){ g_pd3dDev->Release(); g_pd3dDev=NULL; }
    if(g_pD3D)   { g_pD3D->Release();    g_pD3D=NULL; }
}
static void ResetDevice()
{
    ImGui_ImplDX9_InvalidateDeviceObjects();
    g_pd3dDev->Reset(&g_d3dpp);
    ImGui_ImplDX9_CreateDeviceObjects();
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);
static LRESULT WINAPI WndProc(HWND hWnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
    if(ImGui_ImplWin32_WndProcHandler(hWnd,msg,wParam,lParam)) return true;
    switch(msg){
        case WM_SIZE:
            if(g_pd3dDev && wParam!=SIZE_MINIMIZED){
                g_d3dpp.BackBufferWidth  = LOWORD(lParam);
                g_d3dpp.BackBufferHeight = HIWORD(lParam);
                ResetDevice();
            }
            return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(hWnd,msg,wParam,lParam);
}

// ─── WIC PNG loader → DX9 texture ────────────────────────────
static bool LoadPNGFromMemory(const unsigned char* data, unsigned int len,
                               LPDIRECT3DDEVICE9 dev,
                               LPDIRECT3DTEXTURE9* outTex, int* outW, int* outH)
{
    if(!data || len==0 || !dev || !outTex || !outW || !outH) return false;

    IWICImagingFactory*    wicFactory = NULL;
    IWICStream*            wicStream  = NULL;
    IWICBitmapDecoder*     decoder    = NULL;
    IWICBitmapFrameDecode* frame      = NULL;
    IWICFormatConverter*   converter  = NULL;
    bool ok = false;

    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory, NULL,
        CLSCTX_INPROC_SERVER, IID_IWICImagingFactory,
        (void**)&wicFactory))) goto cleanup;

    if(FAILED(wicFactory->CreateStream(&wicStream))) goto cleanup;
    if(FAILED(wicStream->InitializeFromMemory(
        const_cast<BYTE*>(data), len))) goto cleanup;
    if(FAILED(wicFactory->CreateDecoderFromStream(
        wicStream, NULL,
        WICDecodeMetadataCacheOnLoad, &decoder))) goto cleanup;
    if(FAILED(decoder->GetFrame(0, &frame))) goto cleanup;

    {
        UINT w=0, h=0;
        if(FAILED(frame->GetSize(&w, &h))) goto cleanup;

        if(FAILED(wicFactory->CreateFormatConverter(&converter))) goto cleanup;
        if(FAILED(converter->Initialize(
            frame,
            GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone, NULL, 0.0,
            WICBitmapPaletteTypeCustom))) goto cleanup;

        LPDIRECT3DTEXTURE9 tex = NULL;
        if(FAILED(dev->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8,
            D3DPOOL_MANAGED, &tex, NULL))) goto cleanup;

        D3DLOCKED_RECT lr;
        if(FAILED(tex->LockRect(0, &lr, NULL, 0))){
            tex->Release(); goto cleanup;
        }
        UINT stride = w * 4;
        converter->CopyPixels(NULL, stride, h * stride, (BYTE*)lr.pBits);
        tex->UnlockRect(0);

        *outTex = tex;
        *outW   = (int)w;
        *outH   = (int)h;
        ok = true;
    }

cleanup:
    if(converter)  converter->Release();
    if(frame)      frame->Release();
    if(decoder)    decoder->Release();
    if(wicStream)  wicStream->Release();
    if(wicFactory) wicFactory->Release();
    return ok;
}

// ─── helpers ─────────────────────────────────────────────────
static void RectFill(ImDrawList* dl,ImVec2 p,ImVec2 sz,ImU32 c,float r=0.f)
{ dl->AddRectFilled(p,{p.x+sz.x,p.y+sz.y},c,r); }
static void Rect(ImDrawList* dl,ImVec2 p,ImVec2 sz,ImU32 c,float r=0.f,float t=1.f)
{ dl->AddRect(p,{p.x+sz.x,p.y+sz.y},c,r,0,t); }

// ── Text font helpers (Electrolize — body text only) ─────────
static void Txt(ImDrawList* dl,ImVec2 p,ImU32 c,const char* s,float wrap=0.f)
{
    if(!g_font13) return;
    if(wrap>0.f) dl->AddText(g_font13,13.f,p,c,s,NULL,wrap);
    else         dl->AddText(g_font13,13.f,p,c,s);
}
static void Txt16(ImDrawList* dl,ImVec2 p,ImU32 c,const char* s,float wrap=0.f)
{
    ImFont* f  = (g_font16 && g_font16!=g_font13) ? g_font16 : g_font13;
    float   sz = (g_font16 && g_font16!=g_font13) ? 16.f : 13.f;
    if(!f) return;
    if(wrap>0.f) dl->AddText(f,sz,p,c,s,NULL,wrap);
    else         dl->AddText(f,sz,p,c,s);
}
static ImVec2 TS(const char* s,float wrap=0.f)
{
    if(!g_font13) return ImGui::CalcTextSize(s);
    return g_font13->CalcTextSizeA(13.f,FLT_MAX,wrap,s);
}
static ImVec2 TS16(const char* s,float wrap=0.f)
{
    ImFont* f  = (g_font16 && g_font16!=g_font13) ? g_font16 : g_font13;
    float   sz = (g_font16 && g_font16!=g_font13) ? 16.f : 13.f;
    if(!f) return ImGui::CalcTextSize(s);
    return f->CalcTextSizeA(sz,FLT_MAX,wrap,s);
}

// ── Icon font helpers (FA6 Solid — icons only) ───────────────
static void IcoTxt(ImDrawList* dl, ImVec2 centre, float sz, ImU32 col,
                   const char* glyph)
{
    if(g_fontIco && fa6_solid_ttf_len > 4) {
        ImVec2 gsz = g_fontIco->CalcTextSizeA(sz, FLT_MAX, 0.f, glyph);
        ImVec2 pos = { centre.x - gsz.x * 0.5f, centre.y - gsz.y * 0.5f };
        dl->AddText(g_fontIco, sz, pos, col, glyph);
    } else {
        dl->AddCircleFilled(centre, sz * 0.35f, col);
    }
}
static ImVec2 IcoSz(float sz, const char* glyph)
{
    if(g_fontIco && fa6_solid_ttf_len > 4)
        return g_fontIco->CalcTextSizeA(sz, FLT_MAX, 0.f, glyph);
    return { sz * 0.7f, sz };
}

// ─── seekbar / volbar ─────────────────────────────────────────
static bool SeekBar(ImDrawList* dl,ImVec2 pos,float width,float height,
                    float* val,float buf,ImU32 cRail,ImU32 cBuf,ImU32 cFill,ImU32 cThumb)
{
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton("##seek",{width,height});
    bool hov=ImGui::IsItemHovered();
    bool act=ImGui::IsItemActive();
    bool changed=false;

    const float padX   = 8.f;
    const float railX0 = pos.x + padX;
    const float railW  = width - padX * 2.f;

    if(act){
        float mx=ImGui::GetIO().MousePos.x;
        *val=(mx - railX0) / railW;
        if(*val<0.f)*val=0.f; if(*val>1.f)*val=1.f;
        changed=true;
    }
    float cy  = pos.y + height * .5f;
    float rh  = (hov||act) ? 5.f : 3.f;
    float ry  = cy - rh * .5f;
    dl->AddRectFilled({railX0,ry},{railX0+railW,         ry+rh},cRail,rh*.5f);
    dl->AddRectFilled({railX0,ry},{railX0+railW*buf,     ry+rh},cBuf, rh*.5f);
    dl->AddRectFilled({railX0,ry},{railX0+railW*(*val),  ry+rh},cFill,rh*.5f);
    if(hov||act){
        float tx = railX0 + railW * (*val);
        dl->AddCircleFilled({tx,cy},5.5f,cThumb);
    }
    return changed;
}

static bool VolBar(ImDrawList* dl,ImVec2 pos,float width,float height,float* val)
{
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton("##vol",{width,height});
    bool act=ImGui::IsItemActive();
    bool hov=ImGui::IsItemHovered();
    bool changed=false;

    const float padX   = 5.f;
    const float railX0 = pos.x + padX;
    const float railW  = width - padX * 2.f;

    if(act){
        float mx=ImGui::GetIO().MousePos.x;
        *val=(mx - railX0) / railW;
        if(*val<0.f)*val=0.f; if(*val>1.f)*val=1.f;
        changed=true;
    }
    float cy=pos.y+height*.5f;
    float rh=3.f,ry=cy-rh*.5f;
    dl->AddRectFilled({railX0,ry},{railX0+railW,         ry+rh},C_SURFACE3,rh*.5f);
    dl->AddRectFilled({railX0,ry},{railX0+railW*(*val),  ry+rh},C_ACCENT,  rh*.5f);
    if(hov||act){
        float tx=railX0+railW*(*val);
        dl->AddCircleFilled({tx,cy},5.f,C_TEXT);
    }
    return changed;
}

// ─── IconBtn ─────────────────────────────────────────────────
static bool IconBtn(ImDrawList* dl,const char* id,ImVec2 pos,float size)
{
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id,{size,size});
    if(ImGui::IsItemHovered()){
        dl->AddRectFilled(pos,{pos.x+size,pos.y+size},COL32(255,255,255,20),4.f);
    }
    return ImGui::IsItemActivated();
}

static bool TabBtn(ImDrawList* dl,const char* label,ImVec2 pos,bool active)
{
    ImVec2 ts=TS(label);
    float padX=12.f,h=26.f,w=ts.x+padX*2;
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(label,{w,h});
    bool hov=ImGui::IsItemHovered();
    if(active){
        dl->AddRectFilled(pos,{pos.x+w,pos.y+h},C_ACCENT,5.f);
        dl->AddRect(pos,{pos.x+w,pos.y+h},C_ACCENT,5.f,0,1.f);
        Txt(dl,{pos.x+padX,pos.y+(h-ts.y)*.5f},C_WHITE,label);
    } else {
        ImU32 bg=hov?C_SURFACE3:C_SURFACE2;
        dl->AddRectFilled(pos,{pos.x+w,pos.y+h},bg,5.f);
        dl->AddRect(pos,{pos.x+w,pos.y+h},C_BORDER,5.f,0,1.f);
        Txt(dl,{pos.x+padX,pos.y+(h-ts.y)*.5f},hov?C_TEXT:C_TEXT_MUTED,label);
    }
    return ImGui::IsItemActivated();
}

// ─── logo drawing ────────────────────────────────────────────
static void DrawLogoArea(ImDrawList* dl, ImVec2 pos, float availH)
{
    if(g_logoTex && g_logoTexW > 0 && g_logoTexH > 0){
        float scale = availH / (float)g_logoTexH;
        float dstW  = (float)g_logoTexW * scale;
        if(dstW > 140.f){ scale = 140.f / (float)g_logoTexW; dstW = 140.f; }
        float dstH  = (float)g_logoTexH * scale;
        float yOff  = (availH - dstH) * 0.5f;
        dl->AddImage((ImTextureID)(intptr_t)g_logoTex,
            {pos.x, pos.y + yOff}, {pos.x+dstW, pos.y+yOff+dstH},
            {0,0}, {1,1}, C_WHITE);
        ImGui::SetCursorScreenPos(pos);
        ImGui::InvisibleButton("##logo",{dstW,availH});
    } else {
        float r  = availH * 0.45f;
        ImVec2 c = {pos.x + r + 2.f, pos.y + availH * 0.5f};
        dl->AddCircle(c, r, C_ACCENT, 0, 1.5f);
        float r5 = r * 0.55f;
        int   nd = 8;
        float da = 3.14159f*2.f/(float)(nd*2);
        for(int i=0;i<nd;i++){
            float a0=(float)i*2.f*da, a1=a0+da*0.85f;
            dl->PathArcTo(c,r5,a0,a1,4);
            dl->PathStroke(C_ACCENT,false,1.f);
        }
        dl->AddCircleFilled(c, r*0.22f, C_ACCENT);
        float lw=1.5f, tick=r*0.2f;
        dl->AddLine({c.x,c.y-r},{c.x,c.y-r+tick},C_ACCENT,lw);
        dl->AddLine({c.x,c.y+r-tick},{c.x,c.y+r},C_ACCENT,lw);
        dl->AddLine({c.x-r,c.y},{c.x-r+tick,c.y},C_ACCENT,lw);
        dl->AddLine({c.x+r-tick,c.y},{c.x+r,c.y},C_ACCENT,lw);
        ImGui::SetCursorScreenPos(pos);
        ImGui::InvisibleButton("##logo",{r*2.f+4.f,availH});
    }
}

// ─── now-playing card ────────────────────────────────────────
static void DrawNowPlayingCard(ImDrawList* dl, ImVec2 pos, float w)
{
    const float cardH  = 68.f;
    const float padL   = 5.f+3.f;
    const float padR   = 8.f;
    const float padT   = 8.f;
    const float thumbW = 80.f;
    const float thumbH = 45.f;

    dl->AddRectFilled(pos,{pos.x+w,pos.y+cardH},COL32(78,168,168,20));
    dl->AddRectFilled(pos,{pos.x+3.f,pos.y+cardH},C_ACCENT);
    dl->AddLine({pos.x,pos.y+cardH},{pos.x+w,pos.y+cardH},COL32(78,168,168,46),1.f);

    float tx=pos.x+padL, ty=pos.y+padT;
    dl->AddRectFilled({tx,ty},{tx+thumbW,ty+thumbH},g_related[0].grad,3.f);
    dl->AddRectFilled({tx,ty},{tx+thumbW,ty+thumbH},COL32(78,168,168,25),3.f);
    ImVec2 cc={tx+thumbW*.5f,ty+thumbH*.5f};
    dl->AddCircleFilled(cc,11.f,COL32(0,0,0,120));
    IcoTxt(dl, cc, 10.f, C_ACCENT, ICO_PLAY);

    float ix=tx+thumbW+8.f;
    float iw=w-padL-thumbW-8.f-padR;
    float iy=pos.y+padT;

    dl->PushClipRect({ix,pos.y},{pos.x+w-padR,pos.y+cardH},true);
    const char* badge="NOW PLAYING";
    ImVec2 bsz=TS(badge);
    Txt(dl,{ix,iy},C_ACCENT,badge);
    iy+=bsz.y+2.f;
    Txt(dl,{ix,iy},C_TEXT,g_related[0].title,iw);
    ImVec2 tsz=TS(g_related[0].title,iw);
    iy+=tsz.y+4.f;
    Txt(dl,{ix,iy},C_TEXT_FAINT,g_related[0].channel);
    dl->PopClipRect();
}

// ─── related item ────────────────────────────────────────────
static void DrawRelatedItem(ImDrawList* dl,ImVec2 pos,float w,const RelItem& item,int)
{
    const float thumbW=100.f,thumbH=56.f;
    const float padT=8.f,padL=8.f,padR=8.f;
    const float itemH=thumbH+padT*2.f;

    float tx=pos.x+padL, ty=pos.y+padT;
    dl->AddRectFilled({tx,ty},{tx+thumbW,ty+thumbH},item.grad,3.f);
    ImVec2 ds=TS(item.dur);
    float bx=tx+thumbW-ds.x-7.f, by2=ty+thumbH-14.f;
    dl->AddRectFilled({bx-3.f,by2},{tx+thumbW-3.f,by2+12.f},COL32(0,0,0,200),2.f);
    Txt(dl,{bx,by2+1.f},C_TEXT,item.dur);

    float ix=tx+thumbW+8.f;
    float iw=w-padL-thumbW-8.f-padR;
    float iy=pos.y+padT;

    dl->PushClipRect({ix,pos.y},{pos.x+w-padR,pos.y+itemH},true);
    Txt(dl,{ix,iy},C_TEXT,item.title,iw);
    ImVec2 titSz=TS(item.title,iw);
    float titleH=titSz.y; if(titleH>13.f*2.f+4.f) titleH=13.f*2.f+4.f;
    iy+=titleH+4.f;
    Txt(dl,{ix,iy},C_TEXT_MUTED,item.channel);
    ImVec2 chSz=TS(item.channel);
    iy+=chSz.y+2.f;
    Txt(dl,{ix,iy},C_TEXT_FAINT,item.views);
    dl->PopClipRect();

    dl->AddLine({pos.x+padL,pos.y+itemH-1.f},{pos.x+w-padR,pos.y+itemH-1.f},C_BORDER,1.f);
}

// ─── titlebar ────────────────────────────────────────────────
static void DrawTitlebar(ImDrawList* dl,ImVec2 pos,float w)
{
    float h=38.f;
    RectFill(dl,pos,{w,h},C_SURFACE);
    dl->AddLine({pos.x,pos.y+h},{pos.x+w,pos.y+h},C_BORDER,1.f);

    float cy=pos.y+h*.5f;
    float x =pos.x+8.f;

    // ── Logo ────────────────────────────────────────────────
    const float logoAreaH = h;
    const float logoAreaY = pos.y + 1.f;
    DrawLogoArea(dl, {x, logoAreaY}, logoAreaH);
    if(g_logoTex && g_logoTexH > 0){
        float scale = logoAreaH / (float)g_logoTexH;
        float dstW  = (float)g_logoTexW * scale;
        if(dstW > 140.f) dstW = 140.f;
        x += dstW + 12.f;
    } else {
        x += logoAreaH + 12.f;
    }

    // ── Nav back / forward ──────────────────────────────────
    {
        float btnSz=28.f, by=cy-btnSz*.5f;
        ImGui::SetCursorScreenPos({x,by});
        ImGui::InvisibleButton("##navback",{btnSz,btnSz});
        bool hovB=ImGui::IsItemHovered();
        if(hovB) dl->AddRectFilled({x,by},{x+btnSz,by+btnSz},C_SURFACE3,4.f);
        dl->AddLine({x+btnSz*.58f,cy-4.f},{x+btnSz*.38f,cy},C_TEXT_MUTED,1.5f);
        dl->AddLine({x+btnSz*.38f,cy},{x+btnSz*.58f,cy+4.f},C_TEXT_MUTED,1.5f);
        x+=btnSz+2.f;
        ImGui::SetCursorScreenPos({x,by});
        ImGui::InvisibleButton("##navfwd",{btnSz,btnSz});
        bool hovF=ImGui::IsItemHovered();
        if(hovF) dl->AddRectFilled({x,by},{x+btnSz,by+btnSz},C_SURFACE3,4.f);
        dl->AddLine({x+btnSz*.42f,cy-4.f},{x+btnSz*.62f,cy},C_TEXT_MUTED,1.5f);
        dl->AddLine({x+btnSz*.62f,cy},{x+btnSz*.42f,cy+4.f},C_TEXT_MUTED,1.5f);
        x+=btnSz+8.f;
    }

    // ── Nav tabs ─────────────────────────────────────────────
    {
        const char* navs[]={"Home","Trending","Library","History"};
        for(int i=0;i<4;i++){
            ImVec2 ts2=TS(navs[i]);
            float tw=ts2.x+8.f;
            ImGui::SetCursorScreenPos({x,pos.y+6.f});
            char navId[32]; snprintf(navId,sizeof(navId),"##nav%d",i);
            ImGui::InvisibleButton(navId,{tw,h-6.f});
            bool hov=ImGui::IsItemHovered();
            ImU32 tc=(i==0)?C_TEXT:(hov?C_TEXT:C_TEXT_MUTED);
            Txt(dl,{x,cy-ts2.y*.5f},tc,navs[i]);
            if(i==0) dl->AddRectFilled({x,pos.y+h-2.f},{x+ts2.x,pos.y+h},C_ACCENT);
            x+=tw+4.f;
        }
    }

    // ── Search bar ───────────────────────────────────────────
    float rightPad=12.f, settingsSz=28.f, searchW=160.f, searchH=26.f;
    float searchX=pos.x+w-rightPad-settingsSz-4.f-searchW;
    float searchY=cy-searchH*.5f;
    RectFill(dl,{searchX,searchY},{searchW,searchH},C_SURFACE3,5.f);
    Rect(dl,{searchX,searchY},{searchW,searchH},C_BORDER,5.f);
    ImVec2 pt=TS(g_search);
    Txt(dl,{searchX+8.f,searchY+(searchH-pt.y)*.5f},C_TEXT_FAINT,g_search);
    float sbW=52.f, sbX=searchX+searchW-sbW;
    dl->AddRectFilled({sbX,searchY+1.f},{searchX+searchW-1.f,searchY+searchH-1.f},C_ACCENT,4.f);
    const char* srchLbl="Search";
    ImVec2 sl=TS(srchLbl);
    Txt(dl,{sbX+(sbW-sl.x)*.5f,searchY+(searchH-sl.y)*.5f},C_WHITE,srchLbl);

    // ── Settings button (icon font: gear) ────────────────────
    float sx2=pos.x+w-rightPad-settingsSz, sy2=cy-settingsSz*.5f;
    ImGui::SetCursorScreenPos({sx2,sy2});
    ImGui::InvisibleButton("##settings",{settingsSz,settingsSz});
    bool sHov=ImGui::IsItemHovered();
    if(sHov) dl->AddRectFilled({sx2,sy2},{sx2+settingsSz,sy2+settingsSz},C_SURFACE3,4.f);
    IcoTxt(dl, {sx2+settingsSz*.5f, cy}, 14.f, C_TEXT_MUTED, ICO_GEAR);
}

// ─── video area ──────────────────────────────────────────────
static void DrawVideoArea(ImDrawList* dl,ImVec2 pos,float w,float h)
{
    RectFill(dl,pos,{w,h},C_BLACK);
    dl->AddRectFilledMultiColor(pos,{pos.x+w,pos.y+h},
        COL32(0,0,0,0),COL32(0,0,0,0),COL32(0,0,0,160),COL32(0,0,0,160));
    ImVec2 cc={pos.x+w*.5f,pos.y+h*.5f};
    dl->AddCircle(cc,28.f,COL32(78,168,168,38),0,1.f);
    IcoTxt(dl, cc, 24.f, COL32(78,168,168,38), ICO_PLAY_CIRCLE);
}

// ─── controls bar ────────────────────────────────────────────
static void DrawControls(ImDrawList* dl,ImVec2 pos,float w)
{
    const float h      = 76.f;
    const float seekH  = 14.f;
    const float seekY  = pos.y + 8.f;

    RectFill(dl,pos,{w,h},C_SURFACE);
    dl->AddLine(pos,{pos.x+w,pos.y},C_BORDER,1.f);

    // ── Seek rail ─────────────────────────────────────────────
    SeekBar(dl,{pos.x,seekY},w,seekH,&g_seek,0.42f,
        COL32(255,255,255,25),COL32(78,168,168,60),C_ACCENT,C_TEXT);

    // ── Time labels ───────────────────────────────────────────
    {
        const float timeY = seekY + seekH + 2.f;
        int totalSec=600, curSec=(int)(g_seek*totalSec);
        char cur[16]; snprintf(cur,sizeof(cur),"%d:%02d",curSec/60,curSec%60);
        const char* tot="10:00";
        dl->PushClipRect({pos.x,pos.y},{pos.x+w,pos.y+h},true);
        Txt(dl,{pos.x+6.f, timeY},C_TEXT_FAINT,cur);
        ImVec2 ts2=TS(tot);
        Txt(dl,{pos.x+w-ts2.x-6.f, timeY},C_TEXT_FAINT,tot);
        dl->PopClipRect();
    }

    // ── Button row ───────────────────────────────────────────
    const float cy = pos.y + 58.f;
    float x = pos.x + 12.f;

    // Play / Pause
    {
        const float btnSz = 28.f;
        const float bhalf = btnSz * 0.5f;
        ImVec2 bpos = {x, cy - bhalf};
        ImGui::SetCursorScreenPos(bpos);
        ImGui::InvisibleButton("##pp",{btnSz,btnSz});
        bool hov = ImGui::IsItemHovered();
        if(ImGui::IsItemActivated()) g_playing = !g_playing;
        ImU32 bg = hov ? C_SURFACE3 : C_SURFACE2;
        dl->AddRectFilled(bpos,{bpos.x+btnSz,bpos.y+btnSz},bg,5.f);
        dl->AddRect(bpos,{bpos.x+btnSz,bpos.y+btnSz},hov?C_BORDER_STR:C_BORDER,5.f,0,1.f);
        IcoTxt(dl, {bpos.x+btnSz*.5f, bpos.y+btnSz*.5f}, 12.f, C_TEXT,
               g_playing ? ICO_PAUSE : ICO_PLAY);
        x += btnSz + 4.f;
    }

    // Rewind -10s
    {
        const float bsz = 28.f;
        ImVec2 bpos = {x, cy - bsz * 0.5f};
        ImGui::SetCursorScreenPos(bpos);
        ImGui::InvisibleButton("##m10",{bsz,bsz});
        bool hov     = ImGui::IsItemHovered();
        bool clicked = ImGui::IsItemActivated();
        if(hov) dl->AddRectFilled(bpos,{bpos.x+bsz,bpos.y+bsz},COL32(255,255,255,20),4.f);
        IcoTxt(dl, {x + bsz * 0.5f, cy}, 14.f, C_TEXT_MUTED, ICO_BACKWARD);
        if(clicked){ g_seek -= 10.f/600.f; if(g_seek<0.f) g_seek=0.f; }
        x += bsz + 4.f;
    }

    // Forward +10s
    {
        const float bsz = 28.f;
        ImVec2 bpos = {x, cy - bsz * 0.5f};
        ImGui::SetCursorScreenPos(bpos);
        ImGui::InvisibleButton("##p10",{bsz,bsz});
        bool hov     = ImGui::IsItemHovered();
        bool clicked = ImGui::IsItemActivated();
        if(hov) dl->AddRectFilled(bpos,{bpos.x+bsz,bpos.y+bsz},COL32(255,255,255,20),4.f);
        IcoTxt(dl, {x + bsz * 0.5f, cy}, 14.f, C_TEXT_MUTED, ICO_FORWARD);
        if(clicked){ g_seek += 10.f/600.f; if(g_seek>1.f) g_seek=1.f; }
        x += bsz + 4.f;
    }

    // Volume icon + slider
    {
        const float iconW = 22.f;
        IcoTxt(dl, {x+iconW*.5f, cy}, 13.f, C_TEXT_MUTED, ICO_VOLUME_HIGH);
        x += iconW + 4.f;
    }
    VolBar(dl,{x,cy-7.f},72.f,14.f,&g_vol);
    x += 72.f + 6.f;

    float rx=pos.x+w-12.f;

    // Fullscreen
    rx-=26.f;
    if(IconBtn(dl,"##fs",{rx,cy-13.f},26.f)){}
    IcoTxt(dl, {rx+13.f, cy}, 13.f, C_TEXT_MUTED, ICO_EXPAND);

    // Quality badge
    rx-=66.f;
    {
        const float qW=60.f, qH=22.f;
        const float qX=rx, qY=cy-qH*.5f;
        RectFill(dl,{qX,qY},{qW,qH},C_SURFACE2,5.f);
        Rect(dl,{qX,qY},{qW,qH},C_BORDER,5.f);
        ImGui::SetCursorScreenPos({qX,qY});
        ImGui::InvisibleButton("##qual",{qW,qH});
        if(ImGui::IsItemActivated()) g_quality=(g_quality+1)%g_qualityCount;
        const char* qlbl=g_qualityOpts[g_quality];
        ImVec2 qts=TS(qlbl);
        Txt(dl,{qX+(qW-qts.x)*.5f, qY+(qH-qts.y)*.5f},C_ACCENT,qlbl);
    }
}

// ─── action bar ──────────────────────────────────────────────
static void DrawActionBar(ImDrawList* dl,ImVec2 pos,float w)
{
    const float h=46.f,btnH=30.f,iconSz=13.f,padLR=10.f,gap=4.f;
    RectFill(dl,pos,{w,h},C_SURFACE);
    dl->AddLine({pos.x,pos.y},{pos.x+w,pos.y},C_BORDER,1.f);
    dl->AddLine({pos.x,pos.y+h},{pos.x+w,pos.y+h},C_BORDER,1.f);
    const float cy=pos.y+h*.5f, by=cy-btnH*.5f;
    float x=pos.x+12.f;

    // Like pill
    {
        const char* cnt="248K";
        ImVec2 cs=TS(cnt);
        ImVec2 isz=IcoSz(iconSz, ICO_THUMB_UP);
        float pillW=padLR+isz.x+4.f+1.f+4.f+cs.x+padLR;
        ImGui::SetCursorScreenPos({x,by});
        ImGui::InvisibleButton("##like",{pillW,btnH});
        bool hov=ImGui::IsItemHovered();
        ImU32 bg=hov?C_ACCENT_SOFT:COL32(78,168,168,20);
        dl->AddRectFilled({x,by},{x+pillW,by+btnH},bg,8.f);
        dl->AddRect({x,by},{x+pillW,by+btnH},hov?C_ACCENT:C_ACCENT_LINE,8.f,0,1.f);
        float iconCX=x+padLR+isz.x*.5f;
        IcoTxt(dl,{iconCX,cy},iconSz,C_ACCENT,ICO_THUMB_UP);
        float divX=x+padLR+isz.x+4.f;
        dl->AddLine({divX,by+6.f},{divX,by+btnH-6.f},C_ACCENT_LINE,1.f);
        Txt(dl,{divX+4.f,cy-cs.y*.5f},C_ACCENT,cnt);
        x+=pillW+gap;
    }
    // Dislike pill
    {
        const char* lbl="Dislike"; ImVec2 ls=TS(lbl);
        ImVec2 isz=IcoSz(iconSz, ICO_THUMB_DOWN);
        float pillW=padLR+isz.x+6.f+ls.x+padLR;
        ImGui::SetCursorScreenPos({x,by}); ImGui::InvisibleButton("##dis",{pillW,btnH});
        bool hov=ImGui::IsItemHovered();
        ImU32 bg=hov?COL32(255,255,255,18):COL32(255,255,255,8);
        dl->AddRectFilled({x,by},{x+pillW,by+btnH},bg,8.f);
        dl->AddRect({x,by},{x+pillW,by+btnH},C_BORDER,8.f,0,1.f);
        IcoTxt(dl,{x+padLR+isz.x*.5f,cy},iconSz,hov?C_ERROR:C_TEXT_MUTED,ICO_THUMB_DOWN);
        Txt(dl,{x+padLR+isz.x+6.f,cy-ls.y*.5f},hov?C_TEXT:C_TEXT_MUTED,lbl);
        x+=pillW+gap;
    }
    dl->AddLine({x+2.f,by+6.f},{x+2.f,by+btnH-6.f},C_BORDER,1.f); x+=8.f;
    // Share pill
    {
        const char* lbl="Share"; ImVec2 ls=TS(lbl);
        ImVec2 isz=IcoSz(iconSz, ICO_SHARE);
        float pillW=padLR+isz.x+6.f+ls.x+padLR;
        ImGui::SetCursorScreenPos({x,by}); ImGui::InvisibleButton("##shr",{pillW,btnH});
        bool hov=ImGui::IsItemHovered();
        ImU32 bg=hov?COL32(255,255,255,18):COL32(255,255,255,8);
        dl->AddRectFilled({x,by},{x+pillW,by+btnH},bg,8.f);
        dl->AddRect({x,by},{x+pillW,by+btnH},C_BORDER,8.f,0,1.f);
        IcoTxt(dl,{x+padLR+isz.x*.5f,cy},iconSz,hov?C_TEXT:C_TEXT_MUTED,ICO_SHARE);
        Txt(dl,{x+padLR+isz.x+6.f,cy-ls.y*.5f},hov?C_TEXT:C_TEXT_MUTED,lbl);
        x+=pillW+gap;
    }
    // Download pill
    {
        const char* lbl="Download"; ImVec2 ls=TS(lbl);
        ImVec2 isz=IcoSz(iconSz, ICO_DOWNLOAD);
        float pillW=padLR+isz.x+6.f+ls.x+padLR;
        ImGui::SetCursorScreenPos({x,by}); ImGui::InvisibleButton("##dl",{pillW,btnH});
        bool hov=ImGui::IsItemHovered();
        ImU32 bg=hov?COL32(255,255,255,18):COL32(255,255,255,8);
        dl->AddRectFilled({x,by},{x+pillW,by+btnH},bg,8.f);
        dl->AddRect({x,by},{x+pillW,by+btnH},C_BORDER,8.f,0,1.f);
        IcoTxt(dl,{x+padLR+isz.x*.5f,cy},iconSz,hov?C_TEXT:C_TEXT_MUTED,ICO_DOWNLOAD);
        Txt(dl,{x+padLR+isz.x+6.f,cy-ls.y*.5f},hov?C_TEXT:C_TEXT_MUTED,lbl);
        x+=pillW+gap;
    }
    dl->AddLine({x+2.f,by+6.f},{x+2.f,by+btnH-6.f},C_BORDER,1.f); x+=8.f;
    // More pill
    {
        float pillW=34.f;
        ImGui::SetCursorScreenPos({x,by}); ImGui::InvisibleButton("##more",{pillW,btnH});
        bool hov=ImGui::IsItemHovered();
        dl->AddRectFilled({x,by},{x+pillW,by+btnH},hov?COL32(255,255,255,18):COL32(255,255,255,8),8.f);
        dl->AddRect({x,by},{x+pillW,by+btnH},C_BORDER,8.f,0,1.f);
        IcoTxt(dl,{x+pillW*.5f,cy},13.f,hov?C_TEXT:C_TEXT_MUTED,ICO_ELLIPSIS_H);
    }
    const char* meta="1.7B views  \xe2\x80\xa2  Jul 28, 1987";
    ImVec2 ms=TS(meta);
    Txt(dl,{pos.x+w-ms.x-12.f,cy-ms.y*.5f},C_TEXT_FAINT,meta);
}

// ─── info / description zone ─────────────────────────────────
static void DrawInfoZone(ImDrawList* dl,ImVec2 pos,float w,float& contentH)
{
    float x=pos.x+12.f, y=pos.y+10.f, iw=w-24.f;

    const char* title="Rick Astley \xe2\x80\x94 Never Gonna Give You Up (Official Video) 4K Remaster";
    ImVec2 titleSz=TS16(title,iw);
    Txt16(dl,{x,y},C_TEXT,title,iw);
    y+=titleSz.y+8.f;

    const char* meta2="1,782,034,159 views  \xe2\x80\xa2  Jul 28, 1987  \xe2\x80\xa2  #RickAstley #80s";
    ImVec2 metaSz=TS(meta2);
    Txt(dl,{x,y},C_TEXT_FAINT,meta2); y+=metaSz.y+6.f;

    dl->AddLine({pos.x,y+4.f},{pos.x+w,y+4.f},C_DIVIDER,1.f); y+=12.f;

    float tx=x;
    if(TabBtn(dl,"Description",{tx,y},g_tab==0)){g_tab=0;} tx+=TS("Description").x+26.f;
    if(TabBtn(dl,"Comments",{tx,y},g_tab==1)){g_tab=1;}
    y+=32.f;
    dl->AddLine({pos.x,y},{pos.x+w,y},C_DIVIDER,1.f); y+=10.f;
    dl->AddLine({pos.x,y},{pos.x+w,y},C_BORDER,1.f); y+=8.f;

    float avatarR=18.f;
    ImVec2 avatarC={x+avatarR,y+avatarR};
    dl->AddCircleFilled(avatarC,avatarR,COL32(0x19,0x20,0x28,255));
    dl->AddCircle(avatarC,avatarR,C_ACCENT_LINE,0,1.5f);
    const char* ini="R"; ImVec2 initSz=TS(ini);
    Txt(dl,{avatarC.x-initSz.x*.5f,avatarC.y-initSz.y*.5f},C_ACCENT,ini);

    float cx2=x+avatarR*2.f+12.f;
    Txt(dl,{cx2,y+3.f},C_TEXT,"Rick Astley");
    Txt(dl,{cx2,y+19.f},C_TEXT_FAINT,"14.2M subscribers");

    float sbx=pos.x+w-140.f;
    ImGui::SetCursorScreenPos({sbx,y+5.f});
    ImGui::InvisibleButton("##sub",{112.f,28.f});
    bool subHov=ImGui::IsItemHovered();
    if(ImGui::IsItemActivated()) g_subscribed=!g_subscribed;
    ImU32 subbg=g_subscribed?C_SURFACE3:(subHov?C_ACCENT_HOV:C_ACCENT);
    dl->AddRectFilled({sbx,y+5.f},{sbx+112.f,y+33.f},subbg,14.f);
    const char* sublbl=g_subscribed?"Subscribed":"Subscribe";
    ImVec2 sls=TS(sublbl);
    Txt(dl,{sbx+(112.f-sls.x)*.5f,y+5.f+(28.f-sls.y)*.5f},g_subscribed?C_TEXT_MUTED:C_WHITE,sublbl);
    y+=avatarR*2.f+10.f;
    dl->AddLine({pos.x,y},{pos.x+w,y},C_BORDER,1.f); y+=10.f;

    if(g_tab==0){
        float dbY=y, dbW=iw;
        const char* desc=
            "The official video for \"Never Gonna Give You Up\" by Rick Astley.\n\n"
            "Never Gonna Give You Up was a global smash on its release in July 1987,\n"
            "topping the charts in 25 countries including Rick's native UK and the US\n"
            "Billboard Hot 100. It also won the Brit Award for Best single in 1988.\n\n"
            "Stock Aitken and Waterman wrote and produced the track which was the\n"
            "lead-off single from Rick's debut LP 'Whenever You Need Somebody'.";
        ImVec2 dsz=TS(desc,dbW-24.f);
        float dbH=dsz.y+24.f;
        dl->AddRectFilled({x,dbY},{x+dbW,dbY+dbH},C_SURFACE2,8.f);
        dl->AddRect({x,dbY},{x+dbW,dbY+dbH},C_BORDER,8.f,0,1.f);
        Txt(dl,{x+12.f,dbY+12.f},C_TEXT_MUTED,desc,dbW-24.f);
        y+=dbH+10.f;
    } else {
        Txt(dl,{x,y},C_TEXT_MUTED,"Comments are disabled for this video.");
        y+=22.f;
    }
    contentH=y-pos.y;
}

// ─── sidebar ─────────────────────────────────────────────────
static void DrawSidebar(ImDrawList* dl,ImVec2 pos,float w,float viewH)
{
    float y=pos.y;

    RectFill(dl,{pos.x,y},{w,40.f},C_SURFACE);
    dl->AddLine({pos.x,y+40.f},{pos.x+w,y+40.f},C_BORDER,1.f);
    const char* upnext="UP NEXT";
    ImVec2 unsz=TS(upnext);
    Txt(dl,{pos.x+12.f,y+20.f-unsz.y*.5f},C_TEXT_FAINT,upnext);

    const char* autoTxt="Autoplay"; ImVec2 ats=TS(autoTxt);
    float togW=32.f,togH=16.f,togX=pos.x+w-12.f-togW,togY=y+12.f;
    Txt(dl,{togX-ats.x-6.f,togY},C_TEXT_MUTED,autoTxt);
    dl->AddRectFilled({togX,togY},{togX+togW,togY+togH},C_ACCENT,togH*.5f);
    dl->AddCircleFilled({togX+togW-togH*.5f-1.f,togY+togH*.5f},togH*.5f-2.f,C_WHITE);
    y+=40.f;

    DrawNowPlayingCard(dl,{pos.x,y},w);
    y+=68.f+2.f;

    dl->PushClipRect({pos.x,y},{pos.x+w,pos.y+viewH},true);

    const float itemH=72.f;
    for(int i=1;i<g_relatedCount;i++){
        float iy=y+(float)(i-1)*itemH-g_sideScroll;
        if(iy+itemH < pos.y || iy > pos.y+viewH) continue;

        ImGui::SetCursorScreenPos({pos.x,iy});
        char relId[32]; snprintf(relId,sizeof(relId),"##rel%d",i);
        ImGui::InvisibleButton(relId,{w,itemH-1.f});
        bool hov=ImGui::IsItemHovered();
        if(hov) RectFill(dl,{pos.x,iy},{w,itemH-1.f},COL32(255,255,255,8));
        DrawRelatedItem(dl,{pos.x,iy},w,g_related[i],i);
    }

    dl->PopClipRect();

    ImVec2 mp=ImGui::GetIO().MousePos;
    if(mp.x>=pos.x && mp.x<=pos.x+w && mp.y>=pos.y && mp.y<=pos.y+viewH){
        float wheel=ImGui::GetIO().MouseWheel;
        g_sideScroll-=wheel*24.f;
        float maxS=(float)(g_relatedCount-2)*itemH;
        if(g_sideScroll<0.f) g_sideScroll=0.f;
        if(g_sideScroll>maxS) g_sideScroll=maxS;
    }
}

// ─── status bar ──────────────────────────────────────────────
static void DrawStatusBar(ImDrawList* dl,ImVec2 pos,float w)
{
    float h=22.f;
    RectFill(dl,pos,{w,h},C_SURFACE);
    dl->AddLine(pos,{pos.x+w,pos.y},C_DIVIDER,1.f);
    float cy=pos.y+h*.5f, ty=cy-TS("X").y*.5f;
    ImU32 dotc=g_playing?C_SUCCESS:COL32(0xC9,0xA9,0x6B,255);
    dl->AddCircleFilled({pos.x+10.f,cy},3.5f,dotc);
    dl->AddCircle({pos.x+10.f,cy},6.f,g_playing?COL32(107,170,120,51):COL32(201,169,107,51),0,1.f);
    const char* stateLabel=g_playing?"Playing":"Paused";
    Txt(dl,{pos.x+20.f,ty},C_TEXT_MUTED,stateLabel);
    const char* titleStatus="Rick Astley \xe2\x80\x94 Never Gonna Give You Up (Official Video) 4K Remaster";
    ImVec2 ts2=TS(titleStatus);
    Txt(dl,{pos.x+w*.5f-ts2.x*.5f,ty},C_TEXT_MUTED,titleStatus);
    const char* codec="2160p  \xe2\x80\xa2  0:33 / 3:13";
    ImVec2 cs=TS(codec);
    Txt(dl,{pos.x+w-cs.x-10.f,ty},C_TEXT_FAINT,codec);
}

// ─── main render frame ───────────────────────────────────────
static void RenderFrame()
{
    ImGuiIO& io=ImGui::GetIO();
    float sw=io.DisplaySize.x, sh=io.DisplaySize.y;

    ImGui::SetNextWindowPos({0,0});
    ImGui::SetNextWindowSize({sw,sh});
    ImGui::PushStyleColor(ImGuiCol_WindowBg,U32toV4(C_BG));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,0.f);
    ImGui::Begin("##root",NULL,
        ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|
        ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoScrollbar|
        ImGuiWindowFlags_NoScrollWithMouse|ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImDrawList* dl=ImGui::GetWindowDrawList();

    const float TB_H  =38.f, SB_H=22.f, CTRL_H=76.f, ACT_H=46.f;
    const float SIDE_W=300.f, MAIN_W=sw-SIDE_W;

    float contentH=sh-TB_H-SB_H;
    float videoH=contentH*0.48f;
    if(videoH<180.f) videoH=180.f;
    if(videoH>420.f) videoH=420.f;

    float y=TB_H;
    DrawTitlebar(dl,{0,0},sw);
    DrawVideoArea(dl,{0,y},MAIN_W,videoH);
    DrawSidebar(dl,{MAIN_W,y},SIDE_W,contentH);
    y+=videoH;
    DrawControls(dl,{0,y},MAIN_W);
    y+=CTRL_H;
    DrawActionBar(dl,{0,y},MAIN_W);
    y+=ACT_H;

    float infoH=0.f;
    DrawInfoZone(dl,{0,y},MAIN_W,infoH);
    y+=infoH;

    DrawStatusBar(dl,{0,sh-SB_H},sw);

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::End();
}

// ─── WinMain ─────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInst,HINSTANCE,LPSTR,int)
{
    CoInitialize(NULL);

    WNDCLASSEXA wc={sizeof(wc),CS_CLASSDC,WndProc,0,0,hInst,
        LoadIconA(hInst,MAKEINTRESOURCEA(1)),
        NULL,NULL,NULL,"SightlineWnd",NULL};
    RegisterClassExA(&wc);
    g_hwnd=CreateWindowExA(0,"SightlineWnd","Sightline",
        WS_OVERLAPPEDWINDOW,100,100,1280,720,NULL,NULL,hInst,NULL);

    if(!CreateDeviceD3D(g_hwnd)){ DestroyWindow(g_hwnd); return 1; }
    ShowWindow(g_hwnd,SW_SHOWDEFAULT);
    UpdateWindow(g_hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io=ImGui::GetIO();
    io.IniFilename=NULL;

    ImGui::StyleColorsDark();
    ImGuiStyle& style=ImGui::GetStyle();
    style.WindowRounding=0; style.FrameRounding=4; style.ScrollbarRounding=4;
    style.FramePadding={4,4}; style.ItemSpacing={6,4};

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX9_Init(g_pd3dDev);

    // ── Font loading ──────────────────────────────────────────
    ImFontConfig cfg;
    cfg.OversampleH=2; cfg.OversampleV=2;
    if(HasElectrolize()){
        cfg.FontDataOwnedByAtlas=false;
        memcpy(cfg.Name,"Electrolize13",sizeof(cfg.Name));
        g_font13=io.Fonts->AddFontFromMemoryTTF(
            const_cast<unsigned char*>(electrolize_regular_ttf),
            (int)electrolize_regular_ttf_len,
            13.f,&cfg,g_glyph_ranges);
        cfg.FontDataOwnedByAtlas=false;
        memcpy(cfg.Name,"Electrolize16",sizeof(cfg.Name));
        g_font16=io.Fonts->AddFontFromMemoryTTF(
            const_cast<unsigned char*>(electrolize_regular_ttf),
            (int)electrolize_regular_ttf_len,
            16.f,&cfg,g_glyph_ranges);
    }
    if(!g_font13) g_font13=io.Fonts->AddFontDefault();
    if(!g_font16) g_font16=g_font13;

    // Icon font: FA6 Solid, glyph range F000-F8FF only.
    if(HasIconFont()){
        ImFontConfig icoCfg;
        icoCfg.OversampleH          = 2;
        icoCfg.OversampleV          = 2;
        icoCfg.FontDataOwnedByAtlas = false;
        icoCfg.GlyphMinAdvanceX     = 13.f;
        memcpy(icoCfg.Name,"FA6Solid",sizeof(icoCfg.Name));
        g_fontIco = io.Fonts->AddFontFromMemoryTTF(
            const_cast<unsigned char*>(fa6_solid_ttf),
            (int)fa6_solid_ttf_len,
            13.f, &icoCfg, g_icon_ranges);
    }

    io.Fonts->Build();
    io.FontDefault = g_font13;

    // ── Logo texture ──────────────────────────────────────────
    if(HasLogo()){
        LoadPNGFromMemory(sightline_logo_png, sightline_logo_png_len,
                          g_pd3dDev, &g_logoTex, &g_logoTexW, &g_logoTexH);
    }

    MSG msg; ZeroMemory(&msg,sizeof(msg));
    while(msg.message!=WM_QUIT){
        if(PeekMessageA(&msg,NULL,0,0,PM_REMOVE)){
            TranslateMessage(&msg); DispatchMessageA(&msg); continue;
        }

        HRESULT hr=g_pd3dDev->TestCooperativeLevel();
        if(hr==D3DERR_DEVICELOST){ Sleep(10); continue; }
        if(hr==D3DERR_DEVICENOTRESET){ ResetDevice(); continue; }

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        RenderFrame();

        ImGui::EndFrame();
        g_pd3dDev->Clear(0,NULL,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,
            D3DCOLOR_RGBA(12,16,20,255),1.f,0);
        if(g_pd3dDev->BeginScene()==D3D_OK){
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            g_pd3dDev->EndScene();
        }
        g_pd3dDev->Present(NULL,NULL,NULL,NULL);
    }

    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDevice();
    DestroyWindow(g_hwnd);
    UnregisterClassA("SightlineWnd",hInst);
    CoUninitialize();
    return 0;
}
