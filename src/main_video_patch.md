# main.cpp — patch guide for DrawVideoArea and URL dialog

This file documents the two changes needed in `src/main.cpp`.
Apply them manually (or automate with a build step).

---

## 1. Add near the top (after the existing `#include` block)

```cpp
#include "video_player.h"

// ── Global player instance ────────────────────────────────
static VideoPlayer g_player;
static char        g_ytUrl[512] = "https://www.youtube.com/watch?v=dQw4w9WgXcQ";
static bool        g_showUrlDialog = false;
```

---

## 2. Replace DrawVideoArea entirely

```cpp
static void DrawVideoArea(ImDrawList* dl, ImVec2 pos, float w, float h)
{
    RectFill(dl, pos, {w, h}, C_BLACK);

    VideoState vs = g_player.GetState();

    if(vs == VS_PLAYING || vs == VS_PAUSED){
        LPDIRECT3DTEXTURE9 tex = g_player.GetTexture();
        if(tex){
            // Fit video inside the area, keeping aspect ratio
            float vw = (float)g_player.GetWidth();
            float vh = (float)g_player.GetHeight();
            float scale = (vw > 0 && vh > 0)
                          ? std::min(w / vw, h / vh)
                          : 1.f;
            float dw = vw * scale;
            float dh = vh * scale;
            float ox = pos.x + (w - dw) * 0.5f;
            float oy = pos.y + (h - dh) * 0.5f;
            dl->AddImage(
                (ImTextureID)(intptr_t)tex,
                {ox, oy}, {ox + dw, oy + dh}
            );
        }
        // Paused overlay
        if(vs == VS_PAUSED){
            dl->AddRectFilled(pos, {pos.x+w, pos.y+h},
                              COL32(0,0,0,100));
            ImVec2 cc = {pos.x+w*0.5f, pos.y+h*0.5f};
            IcoTxt(dl, cc, 36.f, COL32(255,255,255,180), ICO_PAUSE);
        }
    } else if(vs == VS_LOADING){
        ImVec2 cc = {pos.x+w*0.5f, pos.y+h*0.5f};
        // Spinning dots (simple pulsing circle as XP-safe fallback)
        float t  = (float)(GetTickCount() % 1000) / 1000.f;
        float r  = 20.f;
        for(int i = 0; i < 8; i++){
            float a = (float)i / 8.f * 6.2832f + t * 6.2832f;
            float alpha = (float)(i) / 8.f;
            dl->AddCircleFilled(
                {cc.x + cosf(a)*r, cc.y + sinf(a)*r},
                3.f,
                COL32(78,168,168,(int)(255*alpha)));
        }
        Txt(dl, {pos.x + w*0.5f - 45.f, pos.y + h*0.5f + 30.f},
            C_TEXT_MUTED, "Resolving...");
    } else if(vs == VS_ERROR){
        ImVec2 cc = {pos.x+w*0.5f, pos.y+h*0.5f};
        Txt(dl, {cc.x - 80.f, cc.y - 10.f},
            C_ERROR, g_player.GetError().c_str(), w - 40.f);
    } else {
        // IDLE / EOF — show click-to-open hint
        ImVec2 cc = {pos.x+w*0.5f, pos.y+h*0.5f};
        dl->AddCircle(cc, 28.f, COL32(78,168,168,38), 0, 1.f);
        IcoTxt(dl, cc, 24.f, COL32(78,168,168,38), ICO_PLAY_CIRCLE);
        const char* hint = "Double-click to open a YouTube URL";
        ImVec2 hsz = TS(hint);
        Txt(dl, {cc.x - hsz.x*0.5f, cc.y + 38.f}, C_TEXT_FAINT, hint);
        // Double-click opens URL dialog
        ImGui::SetCursorScreenPos(pos);
        ImGui::InvisibleButton("##videoclick", {w, h});
        if(ImGui::IsItemHovered() &&
           ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            g_showUrlDialog = true;
    }
}
```

---

## 3. Add after DrawVideoArea, a URL dialog function

```cpp
static void DrawUrlDialog(LPDIRECT3DDEVICE9 dev)
{
    if(!g_showUrlDialog) return;

    ImGuiIO& io = ImGui::GetIO();
    float dw = 480.f, dh = 110.f;
    float dx = (io.DisplaySize.x - dw) * 0.5f;
    float dy = (io.DisplaySize.y - dh) * 0.5f;

    ImGui::SetNextWindowPos({dx, dy}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({dw, dh}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.97f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize   |
        ImGuiWindowFlags_NoMove     |
        ImGuiWindowFlags_NoScrollbar;

    if(ImGui::Begin("##urldlg", NULL, flags)){
        ImGui::TextColored(ImVec4(0.30f,0.66f,0.66f,1.f), "YouTube URL");
        ImGui::SetNextItemWidth(dw - 16.f);
        ImGui::InputText("##url", g_ytUrl, sizeof(g_ytUrl));
        ImGui::Spacing();

        float bw = 90.f;
        if(ImGui::Button("Play", {bw, 0})){
            g_player.Close();
            g_player.Open(g_ytUrl, dev, "yt-dlp.exe");
            g_showUrlDialog = false;
        }
        ImGui::SameLine();
        if(ImGui::Button("Cancel", {bw, 0}))
            g_showUrlDialog = false;

        if(ImGui::IsKeyPressed(ImGuiKey_Escape))
            g_showUrlDialog = false;
    }
    ImGui::End();
}
```

---

## 4. In the per-frame render block (inside the `while(!done)` loop)

Before `ImGui::Render()`, add:

```cpp
// Update video texture (must run every frame on render thread)
if(g_player.GetState() != VS_IDLE)
    g_player.Update(g_pd3dDev);

// Sync play/pause button to player state
if(g_player.GetState() == VS_PLAYING && !g_playing){
    g_player.SetPaused(true);
} else if(g_player.GetState() == VS_PAUSED && g_playing){
    g_player.SetPaused(false);
}

// Sync seek bar position from player
if(g_player.GetDur() > 0.0)
    g_seek = g_player.GetProgress();
```

And inside `DrawFrame` (or wherever `DrawControls` is called), pass seek changes back:

```cpp
// If user dragged the seekbar, seek the player
if(seekChanged) // seekChanged = return value of SeekBar()
    g_player.Seek(g_seek);
```

Also call `DrawUrlDialog(g_pd3dDev)` after all other `Draw*` calls,
but before `ImGui::Render()`.

---

## 5. On device reset (in ResetDevice())

```cpp
static void ResetDevice()
{
    g_player.OnDeviceLost();
    ImGui_ImplDX9_InvalidateDeviceObjects();
    g_pd3dDev->Reset(&g_d3dpp);
    ImGui_ImplDX9_CreateDeviceObjects();
    g_player.OnDeviceReset(g_pd3dDev);
}
```
