// ============================================================
//  Sightline — Video Player UI
//  Dear ImGui + Win32 + Direct3D9
//  Single EXE, compatible Windows XP SP3 x86 -> Windows 11
//  Compiler: MinGW-w64 or MSVC (x86 / target XP)
// ============================================================

#define WINVER       0x0501
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <d3d9.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx9.h"

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

static inline ImVec4 U32toV4(ImU32 c){
    return ImVec4(((c>>0)&0xFF)/255.f,((c>>8)&0xFF)/255.f,
                  ((c>>16)&0xFF)/255.f,((c>>24)&0xFF)/255.f);
}

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
    {"Best of 80s Music Legends — Team Formidable Mix","Radio 80s Hits","2.1M views","42:18",COL32(0x1a,0x20,0x30,255)},
    {"Rick Astley — Mix de Exitos Completo","Nanojams300","890K views","31:08",COL32(0x1a,0x28,0x20,255)},
    {"Daft Punk ft. Pharrell Williams — Get Lucky (Official)","pftpremials","1.4B views","3:44",COL32(0x20,0x18,0x28,255)},
    {"A-ha — Take On Me (Official Video) 4K Remastered","A-ha","850M views","3:46",COL32(0x28,0x18,0x20,255)},
    {"Daryl Hall & John Oates — You Make My Dreams","Hall & Oates","320M views","2:56",COL32(0x14,0x28,0x28,255)},
    {"Toto — Africa (Official Music Video)","Toto","1.1B views","4:55",COL32(0x28,0x20,0x14,255)},
    {"Soft Cell — Tainted Love","Soft Cell","210M views","2:37",COL32(0x20,0x14,0x28,255)},
};
static const int g_relatedCount = 7;

// ─── D3D9 globals ────────────────────────────────────────────
static LPDIRECT3D9       g_pD3D    = NULL;
static LPDIRECT3DDEVICE9 g_pd3dDev = NULL;
static D3DPRESENT_PARAMETERS g_d3dpp= {};
static HWND g_hwnd = NULL;

static bool CreateDeviceD3D(HWND hWnd)
{
    if((g_pD3D = Direct3DCreate9(D3D_SDK_VERSION)) == NULL) return false;
    ZeroMemory(&g_d3dpp, sizeof(g_d3dpp));
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
    if(g_pd3dDev){ g_pd3dDev->Release(); g_pd3dDev=NULL; }
    if(g_pD3D)   { g_pD3D->Release();    g_pD3D=NULL;    }
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

// ─── helpers ─────────────────────────────────────────────────
static void RectFill(ImDrawList* dl,ImVec2 p,ImVec2 sz,ImU32 c,float r=0.f)
{ dl->AddRectFilled(p,{p.x+sz.x,p.y+sz.y},c,r); }
static void Rect(ImDrawList* dl,ImVec2 p,ImVec2 sz,ImU32 c,float r=0.f,float t=1.f)
{ dl->AddRect(p,{p.x+sz.x,p.y+sz.y},c,r,0,t); }
static void Txt(ImDrawList* dl,ImVec2 p,ImU32 c,const char* s,float wrap=0.f)
{ if(wrap>0.f) dl->AddText(NULL,0.f,p,c,s,NULL,wrap); else dl->AddText(p,c,s); }
static ImVec2 TS(const char* s){ return ImGui::CalcTextSize(s); }

// ─── icon drawing ────────────────────────────────────────────
static void IconPlay(ImDrawList* dl,ImVec2 c,float r,ImU32 col)
{
    dl->AddTriangleFilled({c.x-r*.4f,c.y-r*.6f},{c.x+r*.7f,c.y},{c.x-r*.4f,c.y+r*.6f},col);
}
static void IconPause(ImDrawList* dl,ImVec2 c,float r,ImU32 col)
{
    float w=r*.28f,h=r*.65f,g=r*.22f;
    dl->AddRectFilled({c.x-g-w,c.y-h},{c.x-g,c.y+h},col,1.f);
    dl->AddRectFilled({c.x+g,c.y-h},{c.x+g+w,c.y+h},col,1.f);
}
static void IconVolume(ImDrawList* dl,ImVec2 c,float r,ImU32 col)
{
    float hh=r*.5f;
    dl->AddRectFilled({c.x-r*.5f,c.y-hh*.55f},{c.x-r*.1f,c.y+hh*.55f},col);
    dl->PathLineTo({c.x-r*.12f,c.y-hh});
    dl->PathLineTo({c.x+r*.45f,c.y-hh*.5f});
    dl->PathLineTo({c.x+r*.45f,c.y+hh*.5f});
    dl->PathLineTo({c.x-r*.12f,c.y+hh});
    dl->PathFillConvex(col);
    float cx2 = c.x + r * .15f;
    float cy2 = c.y;
    float rc   = r * .55f;
    dl->PathArcTo({cx2, cy2}, rc, -3.14f * .4f, 3.14f * .4f, 8);
    dl->PathStroke(col, false, 1.2f);
}
static void IconFullscreen(ImDrawList* dl,ImVec2 c,float r,ImU32 col)
{
    float s=r*.6f,d=r*.25f;
    float lx=c.x-s,rx=c.x+s,ty=c.y-s,by=c.y+s;
    dl->AddLine({lx,ty},{lx+d,ty},col,1.3f); dl->AddLine({lx,ty},{lx,ty+d},col,1.3f);
    dl->AddLine({rx,ty},{rx-d,ty},col,1.3f); dl->AddLine({rx,ty},{rx,ty+d},col,1.3f);
    dl->AddLine({lx,by},{lx+d,by},col,1.3f); dl->AddLine({lx,by},{lx,by-d},col,1.3f);
    dl->AddLine({rx,by},{rx-d,by},col,1.3f); dl->AddLine({rx,by},{rx,by-d},col,1.3f);
}
// Share: curved arrow (like the HTML .icon-share path)
static void IconShare(ImDrawList* dl,ImVec2 c,float r,ImU32 col)
{
    // arrow shaft pointing right-up
    dl->PathArcTo({c.x-r*.1f,c.y+r*.2f}, r*.55f, -3.14f*0.55f, -3.14f*0.05f, 10);
    dl->PathStroke(col,false,1.4f);
    // arrowhead
    float ax=c.x+r*.42f, ay=c.y-r*.25f;
    dl->AddLine({ax,ay},{ax-r*.32f,ay+r*.1f},col,1.4f);
    dl->AddLine({ax,ay},{ax-r*.1f,ay+r*.38f},col,1.4f);
}
// Download: down-arrow with tray
static void IconDownload(ImDrawList* dl,ImVec2 c,float r,ImU32 col)
{
    float hw=r*.3f;
    // vertical line
    dl->AddLine({c.x,c.y-r*.55f},{c.x,c.y+r*.1f},col,1.4f);
    // arrowhead pointing down
    dl->AddLine({c.x,c.y+r*.1f},{c.x-hw,c.y-r*.15f},col,1.4f);
    dl->AddLine({c.x,c.y+r*.1f},{c.x+hw,c.y-r*.15f},col,1.4f);
    // tray
    dl->AddLine({c.x-r*.55f,c.y+r*.55f},{c.x+r*.55f,c.y+r*.55f},col,1.4f);
}
static void IconDots(ImDrawList* dl,ImVec2 c,float r,ImU32 col)
{
    float rr=r*.18f;
    dl->AddCircleFilled({c.x-r*.6f,c.y},rr,col);
    dl->AddCircleFilled(c,rr,col);
    dl->AddCircleFilled({c.x+r*.6f,c.y},rr,col);
}
// Thumb up (like/dislike)
static void IconThumbUp(ImDrawList* dl,ImVec2 c,float r,ImU32 col)
{
    float hw=r*.45f,hh=r*.55f;
    ImVec2 pts[6]={ {c.x-hw,c.y+hh},{c.x-hw,c.y},{c.x-hw*.2f,c.y-hh*.1f},
                    {c.x+hw*.3f,c.y-hh},{c.x+hw,c.y-hh*.3f},{c.x+hw,c.y+hh} };
    for(int i=0;i<6;i++) dl->PathLineTo(pts[i]);
    dl->PathStroke(col,true,1.3f);
    // handle
    dl->AddRectFilled({c.x-hw,c.y+hh*.1f},{c.x-hw+r*.22f,c.y+hh},col,1.f);
}
static void IconThumbDown(ImDrawList* dl,ImVec2 c,float r,ImU32 col)
{
    // mirror of thumb up
    float hw=r*.45f,hh=r*.55f;
    ImVec2 pts[6]={ {c.x-hw,c.y-hh},{c.x-hw,c.y},{c.x-hw*.2f,c.y+hh*.1f},
                    {c.x+hw*.3f,c.y+hh},{c.x+hw,c.y+hh*.3f},{c.x+hw,c.y-hh} };
    for(int i=0;i<6;i++) dl->PathLineTo(pts[i]);
    dl->PathStroke(col,true,1.3f);
    dl->AddRectFilled({c.x-hw,c.y-hh},{c.x-hw+r*.22f,c.y-hh*.1f},col,1.f);
}
static void DrawLogo(ImDrawList* dl,ImVec2 c,float r,ImU32 col)
{
    dl->AddCircle(c,r,col,0,1.5f);
    dl->AddCircle(c,r*.56f,col,0,1.f);
    dl->AddCircleFilled(c,r*.22f,col);
    float tk=r*.22f;
    dl->AddLine({c.x,c.y-r},{c.x,c.y-r+tk},col,1.5f);
    dl->AddLine({c.x,c.y+r-tk},{c.x,c.y+r},col,1.5f);
    dl->AddLine({c.x-r,c.y},{c.x-r+tk,c.y},col,1.5f);
    dl->AddLine({c.x+r-tk,c.y},{c.x+r,c.y},col,1.5f);
}

// Replay-10 / Forward-10 circular arrow with number (like HTML .icon-skip-back / .icon-skip-forward)
static void IconSkip(ImDrawList* dl,ImVec2 c,float r,ImU32 col,bool forward)
{
    // Arc: ~270 degrees
    float start = forward ? (-3.14f*0.25f) : (-3.14f*1.25f);
    float end   = forward ? (-3.14f*1.25f + 3.14f*2.f) : (-3.14f*0.25f + 3.14f*2.f);
    // clamp for partial arc
    if(forward){
        dl->PathArcTo(c, r*.65f, 3.14f*0.3f, 3.14f*2.3f, 14);
    } else {
        dl->PathArcTo(c, r*.65f, 3.14f*2.7f, 3.14f*0.7f + 3.14f*2.f, 14);
    }
    dl->PathStroke(col,false,1.4f);
    // arrowhead at end of arc
    float ang = forward ? (3.14f*2.3f) : (3.14f*2.7f);
    float ax = c.x + r*.65f * cosf(ang);
    float ay = c.y + r*.65f * sinf(ang);
    float perp = forward ? (ang + 3.14f*0.5f) : (ang - 3.14f*0.5f);
    float as = r*.25f;
    float tip_ang = forward ? (ang - 3.14f*0.35f) : (ang + 3.14f*0.35f);
    // simple arrowhead triangle
    ImVec2 tip = {ax, ay};
    ImVec2 p1  = {ax + as*cosf(ang + 3.14f*0.7f), ay + as*sinf(ang + 3.14f*0.7f)};
    ImVec2 p2  = {ax + as*cosf(ang - 3.14f*0.7f), ay + as*sinf(ang - 3.14f*0.7f)};
    dl->AddTriangleFilled(tip,p1,p2,col);
    // number "10" centered
    const char* num = "10";
    ImVec2 ns = ImGui::CalcTextSize(num);
    dl->AddText({c.x - ns.x*.5f, c.y - ns.y*.5f}, col, num);
}

// ─── seekbar / volbar ─────────────────────────────────────────
static bool SeekBar(ImDrawList* dl,ImVec2 pos,float width,float height,
                    float* val,float buf,ImU32 cRail,ImU32 cBuf,ImU32 cFill,ImU32 cThumb)
{
    ImVec2 p1=pos;
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton("##seek",{width,height});
    bool hov=ImGui::IsItemHovered();
    bool act=ImGui::IsItemActive();
    bool changed=false;
    if(act){
        float mx=ImGui::GetIO().MousePos.x;
        *val=(mx-pos.x)/width;
        if(*val<0.f)*val=0.f;
        if(*val>1.f)*val=1.f;
        changed=true;
    }
    float cy=pos.y+height*.5f;
    float rh=(hov||act)?height:height*.35f;
    float ry=cy-rh*.5f;
    dl->AddRectFilled({pos.x,ry},{pos.x+width,ry+rh},cRail,rh*.5f);
    dl->AddRectFilled({pos.x,ry},{pos.x+width*buf,ry+rh},cBuf,rh*.5f);
    dl->AddRectFilled({pos.x,ry},{pos.x+width*(*val),ry+rh},cFill,rh*.5f);
    if(hov||act){
        float tx=pos.x+width*(*val);
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
    if(act){
        float mx=ImGui::GetIO().MousePos.x;
        *val=(mx-pos.x)/width;
        if(*val<0.f)*val=0.f;
        if(*val>1.f)*val=1.f;
        changed=true;
    }
    float cy=pos.y+height*.5f;
    float rh=height*.35f;
    float ry=cy-rh*.5f;
    dl->AddRectFilled({pos.x,ry},{pos.x+width,ry+rh},C_SURFACE3,rh*.5f);
    dl->AddRectFilled({pos.x,ry},{pos.x+width*(*val),ry+rh},C_ACCENT,rh*.5f);
    if(hov||act){
        float tx=pos.x+width*(*val);
        dl->AddCircleFilled({tx,cy},5.f,C_TEXT);
    }
    return changed;
}

static bool IconBtn(const char* id,ImVec2 pos,float size)
{
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id,{size,size});
    if(ImGui::IsItemHovered()){
        ImDrawList* dl=ImGui::GetWindowDrawList();
        dl->AddCircleFilled({pos.x+size*.5f,pos.y+size*.5f},size*.5f,COL32(255,255,255,20));
    }
    return ImGui::IsItemClicked();
}

// Pill action button (like HTML .action-btn)
// Returns true if clicked. Draws a rounded-rect pill with icon + label.
static bool ActionPillBtn(ImDrawList* dl,const char* id,ImVec2 pos,
                           const char* label,ImU32 labelCol)
{
    ImVec2 ls = TS(label);
    float w = ls.x + 24.f;
    float h = 28.f;
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id,{w,h});
    bool hov = ImGui::IsItemHovered();
    ImU32 bg = hov ? COL32(255,255,255,18) : COL32(255,255,255,8);
    dl->AddRectFilled(pos,{pos.x+w,pos.y+h},bg,h*.5f);
    dl->AddRect(pos,{pos.x+w,pos.y+h},C_BORDER,h*.5f,0,1.f);
    dl->AddText({pos.x+12.f, pos.y+(h-ls.y)*.5f}, hov ? C_TEXT : labelCol, label);
    return ImGui::IsItemClicked();
}

// Pill with icon on left
static bool ActionPillBtnIcon(ImDrawList* dl,const char* id,ImVec2 pos,
                               float iconW, // space reserved for icon
                               const char* label,ImU32 labelCol)
{
    ImVec2 ls = TS(label);
    float w = ls.x + iconW + 20.f;
    float h = 28.f;
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id,{w,h});
    bool hov = ImGui::IsItemHovered();
    ImU32 bg = hov ? COL32(255,255,255,18) : COL32(255,255,255,8);
    dl->AddRectFilled(pos,{pos.x+w,pos.y+h},bg,h*.5f);
    dl->AddRect(pos,{pos.x+w,pos.y+h},C_BORDER,h*.5f,0,1.f);
    dl->AddText({pos.x+iconW+10.f, pos.y+(h-ls.y)*.5f}, hov ? C_TEXT : labelCol, label);
    return ImGui::IsItemClicked();
}

// Tab button with pill active style (matches HTML .tab-btn.active)
static bool TabBtn(ImDrawList* dl,const char* label,ImVec2 pos,bool active)
{
    ImVec2 ts=TS(label);
    float padX=16.f, padY=7.f;
    float w=ts.x+padX*2, h=ts.y+padY*2;
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(label,{w,h});
    bool hov=ImGui::IsItemHovered();
    if(active){
        // filled pill
        dl->AddRectFilled(pos,{pos.x+w,pos.y+h},C_SURFACE3,h*.5f);
        dl->AddText({pos.x+padX,pos.y+padY},C_TEXT,label);
    } else {
        ImU32 tc= hov ? C_TEXT_MUTED : C_TEXT_FAINT;
        if(hov) dl->AddRectFilled(pos,{pos.x+w,pos.y+h},COL32(255,255,255,8),h*.5f);
        dl->AddText({pos.x+padX,pos.y+padY},tc,label);
    }
    return ImGui::IsItemClicked();
}

// ─── sidebar related item ────────────────────────────────────
static void DrawRelatedItem(ImDrawList* dl,ImVec2 pos,float w,const RelItem& item,bool nowPlaying)
{
    float th=68.f, tw=112.f, pad=10.f;
    // thumbnail
    dl->AddRectFilled(pos,{pos.x+tw,pos.y+th},item.grad,4.f);
    if(nowPlaying){
        dl->AddRectFilled(pos,{pos.x+tw,pos.y+th},COL32(78,168,168,30),4.f);
        ImVec2 cc={pos.x+tw*.5f,pos.y+th*.5f};
        dl->AddCircleFilled(cc,14.f,COL32(0,0,0,120));
        IconPlay(dl,cc,8.f,C_ACCENT);
        dl->AddRectFilled({pos.x+tw-36.f,pos.y+th-18.f},{pos.x+tw-2.f,pos.y+th-2.f},COL32(0,0,0,180),3.f);
        dl->AddText({pos.x+tw-33.f,pos.y+th-16.f},C_ACCENT,item.dur);
    } else {
        ImVec2 ds=TS(item.dur);
        float bx=pos.x+tw-ds.x-8.f;
        dl->AddRectFilled({bx-3.f,pos.y+th-18.f},{pos.x+tw-2.f,pos.y+th-2.f},COL32(0,0,0,180),3.f);
        dl->AddText({bx,pos.y+th-16.f},C_TEXT,item.dur);
    }
    // info column
    float ix=pos.x+tw+pad;
    float iw=w-tw-pad;
    // title
    dl->AddText(NULL,0.f,{ix,pos.y},C_TEXT,item.title,NULL,iw);
    // channel name
    dl->AddText({ix,pos.y+30.f},C_TEXT_MUTED,item.channel);
    // views
    dl->AddText({ix,pos.y+46.f},C_TEXT_FAINT,item.views);

    if(nowPlaying){
        // "▶ Now Playing" pill badge below channel name (matches HTML .now-playing-badge)
        const char* badge = "NOW PLAYING";
        ImVec2 bs = TS(badge);
        float bpx = ix;
        float bpy = pos.y + 30.f - bs.y - 4.f; // above channel name
        float bw  = bs.x + 12.f;
        float bh  = bs.y + 4.f;
        dl->AddRectFilled({bpx,bpy},{bpx+bw,bpy+bh},C_ACCENT_SOFT,bh*.5f);
        dl->AddRect({bpx,bpy},{bpx+bw,bpy+bh},C_ACCENT_LINE,bh*.5f,0,1.f);
        dl->AddText({bpx+6.f,bpy+2.f},C_ACCENT,badge);
    }
}

// ─── titlebar ────────────────────────────────────────────────
static void DrawTitlebar(ImDrawList* dl,ImVec2 pos,float w)
{
    float h=52.f;
    RectFill(dl,pos,{w,h},C_SURFACE);
    dl->AddLine({pos.x,pos.y+h},{pos.x+w,pos.y+h},C_BORDER,1.f);
    float lx=pos.x+18.f, ly=pos.y+h*.5f;
    DrawLogo(dl,{lx,ly},11.f,C_ACCENT);
    dl->AddText({lx+18.f,ly-7.f},C_TEXT,"SIGHTLINE");
    const char* navs[]={">  Home","Trending","Library","History"};
    float nx=lx+90.f;
    for(int i=0;i<4;i++){
        ImVec2 ts=TS(navs[i]);
        ImU32 nc= (i==0)? C_TEXT : C_TEXT_MUTED;
        dl->AddText({nx,ly-ts.y*.5f},nc,navs[i]);
        if(i==0) dl->AddRectFilled({nx,pos.y+h-2.f},{nx+ts.x,pos.y+h},C_ACCENT);
        nx+=ts.x+22.f;
    }
    float sw=220.f, sh=30.f;
    float sx=pos.x+w*.5f-sw*.5f, sy=pos.y+h*.5f-sh*.5f;
    RectFill(dl,{sx,sy},{sw,sh},C_SURFACE2,15.f);
    Rect(dl,{sx,sy},{sw,sh},C_BORDER,15.f);
    dl->AddText({sx+12.f,sy+(sh-13.f)*.5f},C_TEXT_FAINT,g_search);
    ImVec2 sc={sx+sw-16.f,sy+sh*.5f};
    dl->AddCircle(sc,5.5f,C_TEXT_MUTED,0,1.3f);
    dl->AddLine({sc.x+4.f,sc.y+4.f},{sc.x+7.f,sc.y+7.f},C_TEXT_MUTED,1.3f);
    float rx=pos.x+w-20.f;
    IconDots(dl,{rx,ly},9.f,C_TEXT_MUTED);
}

// ─── video area ──────────────────────────────────────────────
static void DrawVideoArea(ImDrawList* dl,ImVec2 pos,float w,float h)
{
    RectFill(dl,pos,{w,h},C_BLACK);
    dl->AddRectFilledMultiColor(pos,{pos.x+w,pos.y+h},
        COL32(0,0,0,0),COL32(0,0,0,0),COL32(0,0,0,180),COL32(0,0,0,180));
}

// ─── controls bar ────────────────────────────────────────────
static void DrawControls(ImDrawList* dl,ImVec2 pos,float w)
{
    float h=52.f;
    RectFill(dl,pos,{w,h},C_SURFACE);
    dl->AddLine(pos,{pos.x+w,pos.y},C_BORDER,1.f);

    float cy=pos.y+h*.5f;
    float x=pos.x+14.f;

    // play/pause
    if(IconBtn("##pp",{x,cy-16.f},32.f)){
        g_playing=!g_playing;
    }
    ImDrawList* wdl=ImGui::GetWindowDrawList();
    ImVec2 ppc={x+16.f,cy};
    if(g_playing) IconPause(wdl,ppc,10.f,C_TEXT);
    else          IconPlay (wdl,ppc,10.f,C_TEXT);
    x+=44.f;

    // -10s (circular arrow icon with "10")
    if(IconBtn("##m10",{x,cy-16.f},32.f)){
        g_seek-=10.f/600.f; if(g_seek<0.f)g_seek=0.f;
    }
    IconSkip(wdl,{x+16.f,cy},10.f,C_TEXT_MUTED,false);
    x+=40.f;

    // +10s (circular arrow icon with "10")
    if(IconBtn("##p10",{x,cy-16.f},32.f)){
        g_seek+=10.f/600.f; if(g_seek>1.f)g_seek=1.f;
    }
    IconSkip(wdl,{x+16.f,cy},10.f,C_TEXT_MUTED,true);
    x+=42.f;

    // volume icon
    IconVolume(wdl,{x+10.f,cy},10.f,C_TEXT_MUTED);
    x+=24.f;

    // volume slider
    VolBar(wdl,{x,cy-8.f},72.f,16.f,&g_vol);
    x+=80.f;

    // time display
    int totalSec=600;
    int curSec=(int)(g_seek*totalSec);
    char timeBuf[32];
    snprintf(timeBuf,sizeof(timeBuf),"%d:%02d / 10:00",curSec/60,curSec%60);
    wdl->AddText({x,cy-7.f},C_TEXT_MUTED,timeBuf);
    x+=TS(timeBuf).x+14.f;

    // right side
    float rx=pos.x+w-14.f;

    // fullscreen
    rx-=28.f;
    if(IconBtn("##fs",{rx,cy-14.f},28.f)){}
    IconFullscreen(wdl,{rx+14.f,cy},10.f,C_TEXT_MUTED);

    // quality dropdown
    rx-=58.f;
    RectFill(wdl,{rx,cy-12.f},{52.f,24.f},C_SURFACE2,4.f);
    Rect(wdl,{rx,cy-12.f},{52.f,24.f},C_BORDER,4.f);
    ImGui::SetCursorScreenPos({rx,cy-12.f});
    ImGui::InvisibleButton("##qual",{52.f,24.f});
    if(ImGui::IsItemClicked()){
        g_quality=(g_quality+1)%g_qualityCount;
    }
    wdl->AddText({rx+6.f,cy-7.f},C_ACCENT,g_qualityOpts[g_quality]);
}

// ─── action bar ──────────────────────────────────────────────
static void DrawActionBar(ImDrawList* dl,ImVec2 pos,float w)
{
    float h=48.f;
    RectFill(dl,pos,{w,h},C_SURFACE);
    dl->AddLine({pos.x,pos.y},{pos.x+w,pos.y},C_BORDER,1.f);

    float cy=pos.y+h*.5f;
    float x=pos.x+16.f;
    float btnH=28.f;
    float by=cy-btnH*.5f;

    ImDrawList* wdl=ImGui::GetWindowDrawList();

    // ── Like pill (icon + count, combined pill) ──
    {
        float iconSz=14.f;
        const char* cnt="248K";
        ImVec2 cs=TS(cnt);
        float pillW=iconSz+cs.x+28.f;
        ImGui::SetCursorScreenPos({x,by});
        ImGui::InvisibleButton("##like",{pillW,btnH});
        bool hov=ImGui::IsItemHovered();
        ImU32 bg=hov?COL32(255,255,255,18):COL32(255,255,255,8);
        wdl->AddRectFilled({x,by},{x+pillW,by+btnH},bg,btnH*.5f);
        wdl->AddRect({x,by},{x+pillW,by+btnH},C_BORDER,btnH*.5f,0,1.f);
        ImU32 ic=hov?C_ACCENT:C_TEXT_MUTED;
        IconThumbUp(wdl,{x+iconSz*.5f+8.f,cy},iconSz*.5f,ic);
        wdl->AddText({x+iconSz+14.f,cy-TS(cnt).y*.5f},ic,cnt);
        // vertical divider between like count and dislike
        float dx=x+pillW;
        // separator inside pill: draw thin vertical line
        wdl->AddLine({x+iconSz+8.f+cs.x+6.f,by+6.f},{x+iconSz+8.f+cs.x+6.f,by+btnH-6.f},C_BORDER,1.f);
        x+=pillW+6.f;
    }

    // ── Dislike pill ──
    {
        float iconSz=14.f;
        const char* lbl="Dislike";
        ImVec2 ls=TS(lbl);
        float pillW=iconSz+ls.x+22.f;
        ImGui::SetCursorScreenPos({x,by});
        ImGui::InvisibleButton("##dis",{pillW,btnH});
        bool hov=ImGui::IsItemHovered();
        ImU32 bg=hov?COL32(255,255,255,18):COL32(255,255,255,8);
        wdl->AddRectFilled({x,by},{x+pillW,by+btnH},bg,btnH*.5f);
        wdl->AddRect({x,by},{x+pillW,by+btnH},C_BORDER,btnH*.5f,0,1.f);
        ImU32 ic=hov?C_ERROR:C_TEXT_MUTED;
        IconThumbDown(wdl,{x+iconSz*.5f+7.f,cy},iconSz*.5f,ic);
        wdl->AddText({x+iconSz+12.f,cy-ls.y*.5f},hov?C_TEXT:C_TEXT_MUTED,lbl);
        x+=pillW+8.f;
    }

    // ── Share pill ──
    {
        float iconSz=14.f;
        const char* lbl="Share";
        ImVec2 ls=TS(lbl);
        float pillW=iconSz+ls.x+22.f;
        ImGui::SetCursorScreenPos({x,by});
        ImGui::InvisibleButton("##shr",{pillW,btnH});
        bool hov=ImGui::IsItemHovered();
        ImU32 bg=hov?COL32(255,255,255,18):COL32(255,255,255,8);
        wdl->AddRectFilled({x,by},{x+pillW,by+btnH},bg,btnH*.5f);
        wdl->AddRect({x,by},{x+pillW,by+btnH},C_BORDER,btnH*.5f,0,1.f);
        ImU32 ic=hov?C_TEXT:C_TEXT_MUTED;
        IconShare(wdl,{x+iconSz*.5f+7.f,cy},iconSz*.5f,ic);
        wdl->AddText({x+iconSz+12.f,cy-ls.y*.5f},ic,lbl);
        x+=pillW+8.f;
    }

    // ── Download pill ──
    {
        float iconSz=14.f;
        const char* lbl="Download";
        ImVec2 ls=TS(lbl);
        float pillW=iconSz+ls.x+22.f;
        ImGui::SetCursorScreenPos({x,by});
        ImGui::InvisibleButton("##dl",{pillW,btnH});
        bool hov=ImGui::IsItemHovered();
        ImU32 bg=hov?COL32(255,255,255,18):COL32(255,255,255,8);
        wdl->AddRectFilled({x,by},{x+pillW,by+btnH},bg,btnH*.5f);
        wdl->AddRect({x,by},{x+pillW,by+btnH},C_BORDER,btnH*.5f,0,1.f);
        ImU32 ic=hov?C_TEXT:C_TEXT_MUTED;
        IconDownload(wdl,{x+iconSz*.5f+7.f,cy},iconSz*.5f,ic);
        wdl->AddText({x+iconSz+12.f,cy-ls.y*.5f},ic,lbl);
        x+=pillW+8.f;
    }

    // ── More (dots) pill ──
    {
        float pillW=34.f;
        ImGui::SetCursorScreenPos({x,by});
        ImGui::InvisibleButton("##more",{pillW,btnH});
        bool hov=ImGui::IsItemHovered();
        ImU32 bg=hov?COL32(255,255,255,18):COL32(255,255,255,8);
        wdl->AddRectFilled({x,by},{x+pillW,by+btnH},bg,btnH*.5f);
        wdl->AddRect({x,by},{x+pillW,by+btnH},C_BORDER,btnH*.5f,0,1.f);
        IconDots(wdl,{x+pillW*.5f,cy},8.f,hov?C_TEXT:C_TEXT_MUTED);
        x+=pillW+8.f;
    }

    // right: view count + date
    float rx=pos.x+w-16.f;
    const char* meta="1.4B views  •  Sep 3, 2009";
    ImVec2 ms=TS(meta);
    wdl->AddText({rx-ms.x,cy-ms.y*.5f},C_TEXT_FAINT,meta);
}

// ─── info / description zone ─────────────────────────────────
static void DrawInfoZone(ImDrawList* dl,ImVec2 pos,float w,float& contentH)
{
    float x=pos.x+16.f, y=pos.y+12.f;
    float iw=w-32.f;

    // title
    const char* title="Never Gonna Give You Up (Official Music Video)";
    dl->AddText(NULL,17.f,{x,y},C_TEXT,title,NULL,iw);
    y+=44.f;

    // meta row  (views • date)
    const char* meta2="1,412,485,006 views  •  Sep 3, 2009";
    dl->AddText({x,y},C_TEXT_MUTED,meta2); y+=20.f;

    // divider
    dl->AddLine({pos.x,y+6.f},{pos.x+w,y+6.f},C_DIVIDER,1.f); y+=16.f;

    // tabs  (pill active style)
    float tx=x;
    if(TabBtn(dl,"Description",{tx,y},g_tab==0)){g_tab=0;}
    tx+=TS("Description").x+34.f;
    if(TabBtn(dl,"Comments (4.2K)",{tx,y},g_tab==1)){g_tab=1;}
    y+=36.f;
    dl->AddLine({pos.x,y},{pos.x+w,y},C_DIVIDER,1.f); y+=14.f;

    // channel row
    float avatarR=18.f;
    ImVec2 avatarC={x+avatarR,y+avatarR};
    dl->AddCircleFilled(avatarC,avatarR,COL32(0x2a,0x3a,0x4a,255));
    dl->AddCircle(avatarC,avatarR,C_BORDER);
    ImVec2 initSz=TS("RA");
    dl->AddText({avatarC.x-initSz.x*.5f,avatarC.y-initSz.y*.5f},C_ACCENT,"RA");

    float cx2=x+avatarR*2.f+10.f;
    dl->AddText({cx2,y+2.f},C_TEXT,"RickAstleyVEVO");
    dl->AddText({cx2,y+18.f},C_TEXT_MUTED,"3.2M subscribers");

    // subscribe button
    float sbx=pos.x+w-130.f;
    ImGui::SetCursorScreenPos({sbx,y+4.f});
    ImGui::InvisibleButton("##sub",{110.f,28.f});
    bool subHov=ImGui::IsItemHovered();
    if(ImGui::IsItemClicked()) g_subscribed=!g_subscribed;
    ImDrawList* wdl=ImGui::GetWindowDrawList();
    ImU32 subbg= g_subscribed? C_SURFACE3 : (subHov? C_ACCENT_HOV : C_ACCENT);
    ImU32 subtc= g_subscribed? C_TEXT_MUTED : C_BLACK;
    wdl->AddRectFilled({sbx,y+4.f},{sbx+110.f,y+32.f},subbg,14.f);
    const char* sublbl= g_subscribed? "Subscribed":"Subscribe";
    ImVec2 sls=TS(sublbl);
    wdl->AddText({sbx+(110.f-sls.x)*.5f,y+4.f+(28.f-sls.y)*.5f},subtc,sublbl);
    y+=avatarR*2.f+12.f;

    // description / comments
    if(g_tab==0){
        const char* desc=
            "Rick Astley — Never Gonna Give You Up (Official Music Video)\n"
            "Listen On Spotify: http://smarturl.it/AstleySpotify\n"
            "Buy On iTunes: http://smarturl.it/AstleyGHiTunes\n"
            "Amazon: http://smarturl.it/AstleyGHAmazon\n\n"
            "'Never Gonna Give You Up' was a worldwide smash on its release\n"
            "in July 1987, topping the charts in 25 countries including the\n"
            "UK and US. Rick Astley was born on 6 February 1966, in Newton-le-Willows,\n"
            "Lancashire, England...";
        dl->AddText(NULL,0.f,{x,y},C_TEXT_MUTED,desc,NULL,iw);
        y+=160.f;
    } else {
        dl->AddText({x,y},C_TEXT_MUTED,"Comments are disabled for this video.");
        y+=24.f;
    }
    contentH = y - pos.y;
}

// ─── sidebar ─────────────────────────────────────────────────
static void DrawSidebar(ImDrawList* dl,ImVec2 pos,float w,float viewH)
{
    float y=pos.y;

    // "Up Next" header row
    RectFill(dl,{pos.x,y},{w,40.f},C_SURFACE);
    dl->AddText({pos.x+14.f,y+12.f},C_TEXT,"Up Next");

    // Autoplay toggle on right (matches HTML .autoplay-toggle)
    const char* autoTxt="Autoplay";
    ImVec2 ats=TS(autoTxt);
    float aax=pos.x+w-14.f-ats.x-36.f;
    dl->AddText({aax,y+12.f},C_TEXT_MUTED,autoTxt);
    // pill toggle track
    float togX=aax+ats.x+6.f;
    float togY=y+12.f;
    float togW=32.f, togH=16.f;
    // track (teal when ON)
    dl->AddRectFilled({togX,togY},{togX+togW,togY+togH},C_ACCENT,togH*.5f);
    // thumb
    dl->AddCircleFilled({togX+togW-togH*.5f-1.f,togY+togH*.5f},togH*.5f-2.f,C_TEXT);
    // "ON" label
    dl->AddText({togX+4.f,togY+1.f},C_BLACK,"ON");
    y+=44.f;

    // now-playing card (first item)
    RectFill(dl,{pos.x,y},{w,100.f},C_SURFACE2);
    // left accent stripe (3px wide, full height)
    dl->AddRectFilled({pos.x,y},{pos.x+3.f,y+100.f},C_ACCENT);
    // teal border outline
    Rect(dl,{pos.x,y},{w,100.f},C_ACCENT_LINE,0.f,1.f);
    DrawRelatedItem(dl,{pos.x+14.f,y+16.f},w-28.f,g_related[0],true);
    y+=104.f;

    float itemH=88.f;
    for(int i=1;i<g_relatedCount;i++){
        float iy=y+(i-1)*itemH - g_sideScroll;
        if(iy+itemH<pos.y || iy>pos.y+viewH) continue;
        ImGui::SetCursorScreenPos({pos.x,iy});
        char relId[32];
        snprintf(relId, sizeof(relId), "##rel%d", i);
        ImGui::InvisibleButton(relId,{w,itemH-4.f});
        if(ImGui::IsItemHovered())
            RectFill(dl,{pos.x,iy},{w,itemH-4.f},COL32(255,255,255,8));
        DrawRelatedItem(dl,{pos.x+14.f,iy+10.f},w-28.f,g_related[i],false);
        dl->AddLine({pos.x+14.f,iy+itemH-5.f},{pos.x+w-14.f,iy+itemH-5.f},C_DIVIDER,1.f);
    }

    // mouse-wheel scroll in sidebar
    ImVec2 mp=ImGui::GetIO().MousePos;
    if(mp.x>=pos.x && mp.x<=pos.x+w && mp.y>=pos.y && mp.y<=pos.y+viewH){
        float wheel=ImGui::GetIO().MouseWheel;
        g_sideScroll-=wheel*20.f;
        float maxS=(float)(g_relatedCount-2)*itemH;
        if(g_sideScroll<0.f)g_sideScroll=0.f;
        if(g_sideScroll>maxS)g_sideScroll=maxS;
    }
}

// ─── status bar ──────────────────────────────────────────────
static void DrawStatusBar(ImDrawList* dl,ImVec2 pos,float w)
{
    float h=24.f;
    RectFill(dl,pos,{w,h},C_SURFACE);
    dl->AddLine(pos,{pos.x+w,pos.y},C_DIVIDER,1.f);
    float cy=pos.y+h*.5f;
    ImU32 dotc= g_playing? C_SUCCESS : C_ERROR;
    dl->AddCircleFilled({pos.x+12.f,cy},4.f,dotc);
    const char* stateLabel= g_playing? "Playing":"Paused";
    dl->AddText({pos.x+20.f,cy-6.f},C_TEXT_MUTED,stateLabel);
    const char* codec="H.264 / AAC — 2160p60 HDR";
    ImVec2 cs=TS(codec);
    dl->AddText({pos.x+w-cs.x-12.f,cy-6.f},C_TEXT_FAINT,codec);
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

    const float TB_H  = 52.f;
    const float SB_H  = 24.f;
    const float CTRL_H= 52.f;
    const float ACT_H = 48.f;
    const float SIDE_W= 310.f;
    const float MAIN_W= sw - SIDE_W;

    float contentTop = TB_H;
    float contentH   = sh - TB_H - SB_H;

    float vidH = MAIN_W * 9.f / 16.f;
    float maxVidH = contentH - CTRL_H - ACT_H - 260.f;
    if(vidH>maxVidH) vidH=maxVidH;
    if(vidH<120.f) vidH=120.f;

    // titlebar
    DrawTitlebar(dl,{0.f,0.f},sw);

    // video
    float mainY = contentTop;
    DrawVideoArea(dl,{0.f,mainY},MAIN_W,vidH);
    float sy=mainY+vidH;

    // seekbar overlapping video/controls boundary
    {
        ImDrawList* sdl=ImGui::GetWindowDrawList();
        SeekBar(sdl,{0.f,sy-3.f},MAIN_W,6.f,&g_seek,0.35f,
            C_SURFACE3,
            COL32(78,168,168,60),
            C_ACCENT,
            C_TEXT);
    }

    // controls
    DrawControls(dl,{0.f,sy+4.f},MAIN_W);

    // action bar
    DrawActionBar(dl,{0.f,sy+4.f+CTRL_H},MAIN_W);

    // info zone
    float infoY=sy+4.f+CTRL_H+ACT_H;
    float infoH=0.f;
    DrawInfoZone(dl,{0.f,infoY},MAIN_W,infoH);

    // sidebar
    float sideX=MAIN_W;
    RectFill(dl,{sideX,contentTop},{SIDE_W,sh-TB_H-SB_H},C_SURFACE);
    dl->AddLine({sideX,contentTop},{sideX,sh-SB_H},C_BORDER,1.f);
    DrawSidebar(dl,{sideX,contentTop},SIDE_W,sh-TB_H-SB_H);

    // status bar
    DrawStatusBar(dl,{0.f,sh-SB_H},sw);

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(1);
}

// ─── WinMain ─────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInst,HINSTANCE,LPSTR,int nCmdShow)
{
    WNDCLASSEXA wc={sizeof(wc)};
    wc.style=CS_CLASSDC;
    wc.lpfnWndProc=WndProc;
    wc.hInstance=hInst;
    wc.lpszClassName="SightlinePlayer";
    RegisterClassExA(&wc);

    g_hwnd=CreateWindowExA(0,"SightlinePlayer","Sightline Player",
        WS_OVERLAPPEDWINDOW,100,100,1280,800,NULL,NULL,hInst,NULL);

    if(!CreateDeviceD3D(g_hwnd)){
        CleanupDevice();
        UnregisterClassA(wc.lpszClassName,hInst);
        return 1;
    }

    ShowWindow(g_hwnd,nCmdShow);
    UpdateWindow(g_hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io=ImGui::GetIO();
    io.IniFilename=NULL;

    ImGui::StyleColorsDark();
    ImGuiStyle& st=ImGui::GetStyle();
    st.WindowBorderSize=0.f;
    st.WindowRounding=0.f;
    st.ScrollbarRounding=3.f;
    st.ScrollbarSize=6.f;
    st.FrameRounding=4.f;

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX9_Init(g_pd3dDev);

    ImVec4 clearCol=U32toV4(C_BG);
    MSG msg; ZeroMemory(&msg,sizeof(msg));
    while(msg.message!=WM_QUIT){
        if(PeekMessageA(&msg,NULL,0U,0U,PM_REMOVE)){
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
            continue;
        }
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        RenderFrame();

        ImGui::EndFrame();
        g_pd3dDev->SetRenderState(D3DRS_ZENABLE,FALSE);
        g_pd3dDev->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
        g_pd3dDev->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
        D3DCOLOR cc=D3DCOLOR_RGBA(
            (int)(clearCol.x*255),(int)(clearCol.y*255),
            (int)(clearCol.z*255),(int)(clearCol.w*255));
        g_pd3dDev->Clear(0,NULL,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,cc,1.f,0);
        if(g_pd3dDev->BeginScene()==D3D_OK){
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            g_pd3dDev->EndScene();
        }
        HRESULT hr=g_pd3dDev->Present(NULL,NULL,NULL,NULL);
        if(hr==D3DERR_DEVICELOST && g_pd3dDev->TestCooperativeLevel()==D3DERR_DEVICENOTRESET)
            ResetDevice();
    }

    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDevice();
    UnregisterClassA(wc.lpszClassName,hInst);
    return 0;
}
