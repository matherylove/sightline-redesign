# GUI Integration Hooks (TODO in main.cpp)

This file documents every change needed in `main.cpp` to wire `video_player` into the GUI.
Once all hooks are implemented this file can be deleted.

---

## 1. Includes & global init (top of main.cpp)

```cpp
#include "video_player.h"   // ← add after other includes
```

In `WinMain`, after `CreateDeviceD3D` succeeds:
```cpp
// Init player — paths relative to the .exe
g_player.Init(".\\tools\\yt-dlp.exe", ".\\ffmpeg-xp");
```

---

## 2. Quality options — extend g_qualityOpts

Change existing:
```cpp
static const char* g_qualityOpts[] = {"2160p","1080p","720p","480p"};
static const int   g_qualityCount  = 4;
static int         g_quality       = 0;
```
To:
```cpp
static const char* g_qualityOpts[] = {"360p","480p","720p","1080p","2160p"};
static const int   g_qualityCount  = 5;
static int         g_quality       = 0;  // default = 360p (index 0)
```
Map index → VideoQuality: `(VideoQuality)g_quality`

---

## 3. DrawVideoArea() — render frame texture

Replace the body of `DrawVideoArea()` with:

```cpp
static void DrawVideoArea(ImDrawList* dl, ImVec2 pos, float w, float h)
{
    RectFill(dl, pos, {w, h}, C_BLACK);

    VideoState vs = g_player.GetState();

    if(vs == VS_LOADING) {
        // Simple spinner text
        const char* lbl = "Loading...";
        ImVec2 sz = TS(lbl);
        Txt(dl, {pos.x + w*0.5f - sz.x*0.5f, pos.y + h*0.5f - sz.y*0.5f},
            C_TEXT_MUTED, lbl);
        return;
    }

    if(vs == VS_ERROR) {
        const char* lbl = g_player.GetError();
        ImVec2 sz = TS(lbl, w - 32.f);
        Txt(dl, {pos.x + 16.f, pos.y + h*0.5f - sz.y*0.5f},
            C_ERROR, lbl, w - 32.f);
        return;
    }

    LPDIRECT3DTEXTURE9 tex = g_player.GetTexture();
    if(tex && (vs == VS_PLAYING || vs == VS_PAUSED || vs == VS_EOF)) {
        float vidW = (float)g_player.GetWidth();
        float vidH = (float)g_player.GetHeight();
        if(vidW > 0.f && vidH > 0.f) {
            float scale = (w / vidW < h / vidH) ? w / vidW : h / vidH;
            float dw = vidW * scale;
            float dh = vidH * scale;
            float ox = pos.x + (w - dw) * 0.5f;
            float oy = pos.y + (h - dh) * 0.5f;
            dl->AddImage((ImTextureID)(intptr_t)tex,
                         {ox, oy}, {ox + dw, oy + dh});
            return;
        }
    }

    // Default: idle placeholder
    dl->AddRectFilledMultiColor(pos, {pos.x+w, pos.y+h},
        COL32(0,0,0,0), COL32(0,0,0,0),
        COL32(0,0,0,160), COL32(0,0,0,160));
    ImVec2 cc = {pos.x + w*0.5f, pos.y + h*0.5f};
    dl->AddCircle(cc, 28.f, COL32(78,168,168,38), 0, 1.f);
    IcoTxt(dl, cc, 24.f, COL32(78,168,168,38), ICO_PLAY_CIRCLE);
}
```

---

## 4. Search bar → Open()

In `DrawTitlebar()`, find the Search button `InvisibleButton` and add after `ImGui::IsItemActivated()`:

```cpp
if(ImGui::IsItemActivated()) {
    // g_search contains the URL or search term
    std::string url = "https://www.youtube.com/watch?v=";
    // If user typed a full URL use it directly, else treat as search
    if(strncmp(g_search, "http", 4) == 0)
        url = g_search;
    else
        url += g_search;  // naive — replace with proper search later
    g_player.Open(url, (VideoQuality)g_quality);
    g_playing = true;
}
```

---

## 5. Play/Pause button → SetPaused()

In `DrawControls()`, find `if(ImGui::IsItemActivated()) g_playing = !g_playing;`
Change to:
```cpp
if(ImGui::IsItemActivated()) {
    g_playing = !g_playing;
    g_player.SetPaused(!g_playing);
}
```

---

## 6. Quality selector → SetQuality()

In `DrawControls()`, find `if(ImGui::IsItemActivated()) g_quality=(g_quality+1)%g_qualityCount;`
Change to:
```cpp
if(ImGui::IsItemActivated()) {
    g_quality = (g_quality + 1) % g_qualityCount;
    g_player.SetQuality((VideoQuality)g_quality);
}
```

---

## 7. Seekbar → Seek() + live position

In `DrawControls()`, the `SeekBar` call already writes `g_seek`.
After `SeekBar(...)` returns true (changed), add:
```cpp
if(seekChanged)
    g_player.Seek(g_seek);
// Sync g_seek from player when not scrubbing
if(!ImGui::IsItemActive() && g_player.GetDur() > 0.0)
    g_seek = (float)(g_player.GetPos() / g_player.GetDur());
```

For time labels, replace hardcoded `totalSec=600` with:
```cpp
int totalSec = (g_player.GetDur() > 0.0) ? (int)g_player.GetDur() : 600;
int curSec   = (int)(g_seek * totalSec);
```

---

## 8. Update() call in RenderFrame()

At the very beginning of `RenderFrame()`, before anything is drawn:
```cpp
g_player.Update(g_pd3dDev);
```

---

## 9. ResetDevice() hooks

Change:
```cpp
static void ResetDevice()
{
    ImGui_ImplDX9_InvalidateDeviceObjects();
    g_pd3dDev->Reset(&g_d3dpp);
    ImGui_ImplDX9_CreateDeviceObjects();
}
```
To:
```cpp
static void ResetDevice()
{
    ImGui_ImplDX9_InvalidateDeviceObjects();
    g_player.OnLostDevice();
    g_pd3dDev->Reset(&g_d3dpp);
    ImGui_ImplDX9_CreateDeviceObjects();
    g_player.OnResetDevice(g_pd3dDev);
}
```

---

## 10. Shutdown in WinMain (before CleanupDevice)

```cpp
g_player.Close();
```
