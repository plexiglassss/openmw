// Shared ImGui drawing for the overlay: theme, fps readout, config menu.
// Included inside the anonymous namespace of overlay.cpp (Win32) and
// overlay_linux.cpp (SDL); the including file must define, before inclusion:
//   const char* keyName(int code);   // display name for a key code
//   void armBinding(int row);        // start rebind capture for a hotkey row
//   void closeMenu();                // close the menu, restore cursor state
// and after inclusion own the g_binding lifecycle (see pollBinding there).

std::atomic<bool> g_menuOpen{ false };

ImFont* g_fontBody = nullptr;
ImFont* g_fontBig = nullptr;
double g_savedFlashUntil = 0.0;

// "Morrowind codex" palette: parchment text, burnished gold accents
const ImVec4 kGold{ 0.95f, 0.79f, 0.44f, 1.f };
const ImVec4 kGoldDim{ 0.68f, 0.55f, 0.30f, 1.f };
const ImVec4 kParchment{ 0.89f, 0.84f, 0.72f, 1.f };
const ImVec4 kFaded{ 0.55f, 0.49f, 0.39f, 1.f };

// hotkey rebinding: index into g_bindTargets, -1 = idle. While a bind is
// armed, the hotkey poller routes all keys here instead of acting on them.
int g_binding = -1;
int* const g_bindTargets[3] = { &g_cfg.menuKey, &g_cfg.fpsKey, &g_cfg.hotkey };
const char* const g_bindLabels[3] = { "Open this menu", "FPS overlay", "Toggle frame generation" };

const char* keyName(int code);
void armBinding(int row);
void closeMenu();

void applyTheme()
{
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    st.WindowRounding = 7.f;
    st.ChildRounding = 5.f;
    st.FrameRounding = 4.f;
    st.PopupRounding = 5.f;
    st.GrabRounding = 3.f;
    st.WindowBorderSize = 1.f;
    st.FrameBorderSize = 1.f;
    st.WindowPadding = ImVec2(18.f, 16.f);
    st.FramePadding = ImVec2(10.f, 6.f);
    st.ItemSpacing = ImVec2(10.f, 9.f);
    st.ItemInnerSpacing = ImVec2(8.f, 6.f);
    st.GrabMinSize = 14.f;
    st.SeparatorTextBorderSize = 1.f;
    st.SeparatorTextAlign = ImVec2(0.f, 0.5f);
    st.SeparatorTextPadding = ImVec2(24.f, 6.f);
    st.ScaleAllSizes(g_cfg.menuScale);

    ImVec4* c = st.Colors;
    c[ImGuiCol_Text] = kParchment;
    c[ImGuiCol_TextDisabled] = kFaded;
    c[ImGuiCol_WindowBg] = ImVec4(0.075f, 0.060f, 0.042f, 0.96f);
    c[ImGuiCol_PopupBg] = ImVec4(0.090f, 0.072f, 0.050f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.68f, 0.55f, 0.30f, 0.45f);
    c[ImGuiCol_BorderShadow] = ImVec4(0.f, 0.f, 0.f, 0.35f);
    c[ImGuiCol_FrameBg] = ImVec4(0.155f, 0.125f, 0.085f, 1.f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.215f, 0.175f, 0.115f, 1.f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.270f, 0.215f, 0.135f, 1.f);
    c[ImGuiCol_TitleBg] = ImVec4(0.055f, 0.045f, 0.032f, 1.f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.105f, 0.082f, 0.052f, 1.f);
    c[ImGuiCol_TitleBgCollapsed] = c[ImGuiCol_TitleBg];
    c[ImGuiCol_CheckMark] = kGold;
    c[ImGuiCol_SliderGrab] = ImVec4(0.78f, 0.63f, 0.35f, 1.f);
    c[ImGuiCol_SliderGrabActive] = kGold;
    c[ImGuiCol_Button] = ImVec4(0.24f, 0.19f, 0.12f, 1.f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.34f, 0.27f, 0.16f, 1.f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.43f, 0.34f, 0.19f, 1.f);
    c[ImGuiCol_Header] = ImVec4(0.30f, 0.24f, 0.14f, 0.60f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.36f, 0.29f, 0.17f, 0.80f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.43f, 0.34f, 0.19f, 0.90f);
    c[ImGuiCol_Separator] = ImVec4(0.68f, 0.55f, 0.30f, 0.35f);
    c[ImGuiCol_SeparatorHovered] = kGoldDim;
    c[ImGuiCol_SeparatorActive] = kGold;
    c[ImGuiCol_ResizeGrip] = ImVec4(0.68f, 0.55f, 0.30f, 0.25f);
    c[ImGuiCol_ResizeGripHovered] = kGoldDim;
    c[ImGuiCol_ResizeGripActive] = kGold;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.060f, 0.050f, 0.035f, 0.60f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.30f, 0.24f, 0.14f, 1.f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.32f, 0.18f, 1.f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.50f, 0.40f, 0.22f, 1.f);
    c[ImGuiCol_NavCursor] = ImVec4(0.95f, 0.79f, 0.44f, 0.80f);
}

// Loads a serif body + big font from the given candidate paths (first hit
// wins); falls back to the scaled default font.
void loadFonts(const char* const* candidates, int count)
{
    ImGuiIO& io = ImGui::GetIO();
    for (int i = 0; i < count; ++i)
    {
        FILE* probe = fopen(candidates[i], "rb");
        if (!probe)
            continue;
        fclose(probe);
        g_fontBody = io.Fonts->AddFontFromFileTTF(candidates[i], 17.f * g_cfg.menuScale);
        if (g_fontBody)
        {
            g_fontBig = io.Fonts->AddFontFromFileTTF(candidates[i], 30.f * g_cfg.menuScale);
            break;
        }
    }
    if (!g_fontBody)
    {
        io.Fonts->AddFontDefault();
        io.FontGlobalScale = g_cfg.menuScale;
        Log("overlay: no system serif font found, using ImGui default");
    }
}

void drawFpsOverlay(float inFps, float outFps, const Diagnostics& diagnostics, bool fgOn, bool paused)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float rawOx = g_cfg.fpsOverlayOffsetX < 0.f ? 0.f : g_cfg.fpsOverlayOffsetX;
    const float rawOy = g_cfg.fpsOverlayOffsetY < 0.f ? 0.f : g_cfg.fpsOverlayOffsetY;
    const float ox = rawOx > vp->WorkSize.x ? vp->WorkSize.x : rawOx;
    const float oy = rawOy > vp->WorkSize.y ? vp->WorkSize.y : rawOy;

    ImVec2 pos{};
    ImVec2 pivot{};
    switch (g_cfg.fpsOverlayAnchor)
    {
        case 1: // top-left
            pos = ImVec2(vp->WorkPos.x + ox, vp->WorkPos.y + oy);
            pivot = ImVec2(0.f, 0.f);
            break;
        case 2: // bottom-right
            pos = ImVec2(vp->WorkPos.x + vp->WorkSize.x - ox,
                vp->WorkPos.y + vp->WorkSize.y - oy);
            pivot = ImVec2(1.f, 1.f);
            break;
        case 3: // bottom-left
            pos = ImVec2(vp->WorkPos.x + ox, vp->WorkPos.y + vp->WorkSize.y - oy);
            pivot = ImVec2(0.f, 1.f);
            break;
        default: // top-right
            pos = ImVec2(vp->WorkPos.x + vp->WorkSize.x - ox, vp->WorkPos.y + oy);
            pivot = ImVec2(1.f, 0.f);
            break;
    }

    // Keep the FPS HUD fully passive, exactly like upstream 0.2.0. Position is
    // controlled only from the existing F8 menu, so gameplay mouse handling
    // never has to change to make this window movable.
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, pivot);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::Begin("##fps", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs
            | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings);
    if (fgOn && paused)
    {
        ImGui::TextColored(kGold, "%.0f", outFps);
        ImGui::SameLine();
        ImGui::TextDisabled("fg idle");
    }
    else if (fgOn)
    {
        ImGui::TextColored(kFaded, "%.0f", inFps);
        ImGui::SameLine();
        ImGui::TextColored(kGoldDim, "\xC2\xBB");
        ImGui::SameLine();
        ImGui::TextColored(kGold, "%.0f", outFps);
    }
    else
    {
        ImGui::TextDisabled("%.0f  fg off", outFps);
    }

    if (g_cfg.diagnosticsOverlay)
    {
        const float ratio = inFps > 0.01f ? outFps / inFps : 0.f;
        ImGui::Separator();
        ImGui::Text("Native FPS:     %.1f", inFps);
        ImGui::Text("Presented FPS:  %.1f", outFps);
        ImGui::Text("FG ratio:       %.3fx", ratio);
        ImGui::Separator();
        ImGui::Text("Native frames:    %llu", (unsigned long long)diagnostics.nativeFrames);
        ImGui::Text("Generated frames: %llu", (unsigned long long)diagnostics.generatedFrames);
        ImGui::Text("Presented frames: %llu", (unsigned long long)diagnostics.presentedFrames);
        ImGui::Text("Dropped inputs:   %llu", (unsigned long long)diagnostics.droppedFrames);
    }
    ImGui::End();
}

void drawMenu(float inFps, float outFps, const Diagnostics& diagnostics, std::atomic<bool>& fgEnabled, bool paused)
{
    ImGui::SetNextWindowSize(ImVec2(330.f * g_cfg.menuScale, 0.f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(60.f, 60.f), ImGuiCond_FirstUseEver);
    bool open = true;
    ImGui::Begin("Frame Generation", &open, ImGuiWindowFlags_NoCollapse);

    bool en = fgEnabled.load();

    // fps readout: game » presented
    if (g_fontBig)
        ImGui::PushFont(g_fontBig);
    ImGui::TextColored(kFaded, "%.0f", inFps);
    ImGui::SameLine();
    ImGui::TextColored(kGoldDim, "\xC2\xBB");
    ImGui::SameLine();
    ImGui::TextColored(en ? kGold : kFaded, "%.0f", outFps);
    if (g_fontBig)
        ImGui::PopFont();
    ImGui::TextDisabled(en ? "game fps \xC2\xBB presented fps" : "frame generation is off");
    if (en && paused)
        ImGui::TextColored(kFaded, "idle: game already at display refresh");

    ImGui::SeparatorText("Display");
    if (ImGui::Checkbox("Frame generation", &en))
    {
        fgEnabled = en;
        Log("fg: frame generation %s (menu)", en ? "ENABLED" : "DISABLED");
    }
    bool fps = g_cfg.fpsOverlay != 0;
    if (ImGui::Checkbox("FPS overlay", &fps))
        g_cfg.fpsOverlay = fps ? 1 : 0;
    if (g_cfg.fpsOverlay)
    {
        bool diag = g_cfg.diagnosticsOverlay != 0;
        if (ImGui::Checkbox("Detailed verification stats", &diag))
            g_cfg.diagnosticsOverlay = diag ? 1 : 0;
        if (g_cfg.diagnosticsOverlay)
        {
            const float ratio = inFps > 0.01f ? outFps / inFps : 0.f;
            ImGui::TextDisabled("live %.1f -> %.1f fps (%.3fx), generated %llu, dropped %llu",
                (double)inFps, (double)outFps, (double)ratio,
                (unsigned long long)diagnostics.generatedFrames,
                (unsigned long long)diagnostics.droppedFrames);
        }

        const char* anchors[] = { "Top right", "Top left", "Bottom right", "Bottom left" };
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::Combo("##fpsAnchor", &g_cfg.fpsOverlayAnchor, anchors, 4);
        ImGui::TextDisabled("FPS overlay anchor");

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const float maxX = vp && vp->WorkSize.x > 24.f ? vp->WorkSize.x - 12.f : 1000.f;
        const float maxY = vp && vp->WorkSize.y > 24.f ? vp->WorkSize.y - 12.f : 1000.f;
        ImGui::SliderFloat("Horizontal offset", &g_cfg.fpsOverlayOffsetX, 0.f, maxX, "%.0f px");
        ImGui::SliderFloat("Vertical offset", &g_cfg.fpsOverlayOffsetY, 0.f, maxY, "%.0f px");
        ImGui::TextDisabled("Position updates live; Save Settings to persist.");
        if (ImGui::Button("Reset FPS overlay position"))
        {
            g_cfg.fpsOverlayAnchor = 0;
            g_cfg.fpsOverlayOffsetX = 12.f;
            g_cfg.fpsOverlayOffsetY = 12.f;
        }
    }
    bool vs = g_cfg.presentVsync != 0;
    if (ImGui::Checkbox("Present vsync", &vs))
        g_cfg.presentVsync = vs ? 1 : 0; // applied by presenter loop
    bool ap = g_cfg.autoPause != 0;
    if (ImGui::Checkbox("Pause when at refresh cap", &ap))
        g_cfg.autoPause = ap ? 1 : 0;

    ImGui::SeparatorText("Quality");
    ImGui::SliderFloat("Scene-cut threshold", &g_cfg.sceneChangeThreshold, 0.02f, 0.5f, "%.2f");
    ImGui::SliderInt("Search radius", &g_cfg.searchRadius, 1, 4);
    ImGui::SliderInt("Pyramid levels", &g_cfg.maxLevels, 3, 8);

    ImGui::SeparatorText("Hotkeys");
    const float btnW = 110.f * g_cfg.menuScale;
    for (int i = 0; i < 3; ++i)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(g_bindLabels[i]);
        ImGui::SameLine(0.f, 0.f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - btnW);
        const bool armed = g_binding == i;
        char label[80];
        snprintf(label, sizeof(label), "%s##bind%d", armed ? "press a key..." : keyName(*g_bindTargets[i]), i);
        if (armed)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, kGold);
            ImGui::PushStyleColor(ImGuiCol_Border, kGold);
        }
        if (ImGui::Button(label, ImVec2(btnW, 0.f)) && !armed)
            armBinding(i);
        if (armed)
            ImGui::PopStyleColor(2);
    }
    if (g_binding >= 0)
        ImGui::TextDisabled("Esc cancels rebinding.");

    ImGui::Dummy(ImVec2(0.f, 4.f * g_cfg.menuScale));
    if (ImGui::Button("Save Settings", ImVec2(-FLT_MIN, 0.f)))
    {
        ConfigSave();
        g_savedFlashUntil = ImGui::GetTime() + 2.0;
    }
    if (ImGui::GetTime() < g_savedFlashUntil)
        ImGui::TextColored(kGold, "saved to openmw_fg.ini");
    ImGui::TextDisabled("Esc or the menu key closes. Game input is paused.");
    ImGui::End();

    if (!open)
        closeMenu();
}
