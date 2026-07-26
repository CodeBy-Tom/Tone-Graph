#include "ui.h"
#include "nodes.h"
#include "app_state.h"
#include "audio_devices.h"
#include "stream_job.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

// UI colors

static constexpr COLORREF kBg       = RGB(52, 52, 56);
static constexpr COLORREF kDot      = RGB(38, 38, 42);
static constexpr COLORREF kNode     = RGB(68, 68, 74);
static constexpr COLORREF kNodeEdge = RGB(92, 92, 100);
static constexpr COLORREF kText     = RGB(220, 220, 224);
static constexpr COLORREF kMuted    = RGB(150, 150, 156);
static constexpr COLORREF kBtn      = RGB(62, 62, 68);
static constexpr COLORREF kBtnHot   = RGB(78, 78, 86);
static constexpr COLORREF kAccent   = RGB(110, 170, 155);

static constexpr int kToolbar = 48;
static constexpr int kDotStep = 22;

static HFONT g_fontUi = nullptr;
static HFONT g_fontTitle = nullptr;
static HBRUSH g_brBg = nullptr;

enum class MenuKind { None, Add, Settings, PickNode, DeleteNode };

struct UiState {
    NodeGraph graph;
    NodeView view;

    MenuKind menu = MenuKind::None;
    RECT menuRc{};
    int menuHover = -1;
    int pickNode = -1;

    bool dragNode = false;
    int dragIdx = -1;
    POINT dragOff{};

    bool dragWire = false;
    int wireNode = -1;
    int wirePort = 0;
    bool wireFromOutPort = true;
    POINT wireFrom{};
    POINT wireCur{};

    bool panDrag = false;
    POINT panLast{};

    bool dragEq = false;
    int eqNode = -1;
    int eqBand = -1;

    bool dragKnob = false;
    int knobNode = -1;
    int knobIdx = -1;
    int knobStartY = 0;
    float knobStartNorm = 0.f;

    bool plusHot = false;
    bool gearHot = false;
    bool runHot = false;
};

static UiState g_ui;

static NodeFonts fonts() { return { g_fontUi, g_fontTitle }; }

static HFONT makeFont(int size, int weight, const wchar_t* face) {
    return CreateFontW(size, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}

static RECT plusBtn() { return { 12, 8, 40, 36 }; }
static RECT gearBtn(HWND hwnd) {
    RECT rc{}; GetClientRect(hwnd, &rc);
    return { rc.right - 44, 8, rc.right - 12, 36 };
}
static RECT runBtn(HWND hwnd) {
    RECT rc{}; GetClientRect(hwnd, &rc);
    return { rc.right - 140, 8, rc.right - 52, 36 };
}

static void clampScroll() {
    constexpr int kLimit = 100000;
    g_ui.view.scrollX = (std::max)(-kLimit, (std::min)(g_ui.view.scrollX, kLimit));
    g_ui.view.scrollY = (std::max)(-kLimit, (std::min)(g_ui.view.scrollY, kLimit));
}

static void refreshDevices() {
    g_apps = listApps();
    g_outs = listOutputs();
    if (g_cableId.empty()) {
        for (auto& o : g_outs) {
            if (isCable(o.name)) { g_cableId = o.id; g_cableName = o.name; break; }
        }
    }
    rebindNodes(g_ui.graph);
}

static void closeMenus() {
    g_ui.menu = MenuKind::None;
    g_ui.menuHover = -1;
    g_ui.pickNode = -1;
}

static void openAddMenu() {
    g_ui.menu = MenuKind::Add;
    RECT b = plusBtn();
    const int rows = nodeCatalogCount();
    g_ui.menuRc = { b.left, b.bottom + 6, b.left + 160, b.bottom + 6 + 4 + rows * 30 };
    g_ui.menuHover = -1;
}

static void openSettingsMenu(HWND hwnd) {
    refreshDevices();
    g_ui.menu = MenuKind::Settings;
    RECT b = gearBtn(hwnd);
    int count = 0;
    for (auto& o : g_outs) if (isCable(o.name)) ++count;
    if (count == 0) count = 1;
    const int h = 28 + count * 28;
    g_ui.menuRc = { b.right - 280, b.bottom + 6, b.right, b.bottom + 6 + h };
    g_ui.menuHover = -1;
}

static void openPickMenu(int nodeIdx) {
    if (nodeIdx < 0 || nodeIdx >= (int)g_ui.graph.nodes.size()) return;
    GraphNode& n = g_ui.graph.nodes[nodeIdx];
    if (!nodeHasPicker(n)) return;
    refreshDevices();
    RECT nr = nodeRectScreen(n, g_ui.view);
    const int count = nodeBehavior(n.kind).pickCount();
    const int rows = (std::max)(1, (std::min)(count, 10));
    g_ui.pickNode = nodeIdx;
    g_ui.menu = MenuKind::PickNode;
    g_ui.menuRc = { nr.left, nr.bottom - 8, nr.right, nr.bottom - 8 + rows * 26 + 8 };
    g_ui.menuHover = -1;
}

static void openDeleteMenu(int nodeIdx, int sx, int sy) {
    g_ui.pickNode = nodeIdx;
    g_ui.menu = MenuKind::DeleteNode;
    g_ui.menuRc = { sx, sy, sx + 140, sy + 40 };
    g_ui.menuHover = -1;
}

static void spawnNode(HWND hwnd, NodeKind kind) {
    RECT rc{}; GetClientRect(hwnd, &rc);
    const int stagger = (int)g_ui.graph.nodes.size() * 28;
    int x, y;
    if (nodeType(kind).hasOutPort && !nodeType(kind).hasInPort) {
        x = g_ui.view.scrollX + 80 + (stagger % 120);
        y = g_ui.view.scrollY + kToolbar + 40 + (stagger % 160);
    } else {
        x = g_ui.view.scrollX + (std::max)(80, (int)rc.right - kNodeW - 80) - (stagger % 80);
        y = g_ui.view.scrollY + kToolbar + 40 + (stagger % 160);
    }
    g_ui.graph.nodes.push_back(makeNode(kind, x, y));
    clampScroll();
    setStatus(std::wstring(L"Added ") + nodeType(kind).menuLabel);
}

static float audioLevel() {
    float sum = 0.f;
    std::lock_guard lock(g_barsMu);
    for (int i = 0; i < kBars; ++i) sum += g_bars[i];
    return (std::min)(1.f, sum / (float)kBars * 18.f);
}

// Stop audio if the graph got ripped apart.
static void stopIfRunning(HWND hwnd, const wchar_t* reason) {
    if (!g_busy && !g_run) return;
    g_run = false;
    clearLiveFx();
    setStatus(reason ? reason : L"Stopped.");
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void tryStartStop(HWND hwnd) {
    if (g_busy) {
        g_run = false;
        clearLiveFx();
        setStatus(L"Stopping\u2026");
        return;
    }
    if (g_ui.graph.links.empty()) { setStatus(L"Connect Input \u2192 [effects] \u2192 Output."); return; }

    refreshDevices();
    auto routes = findAudioRoutes(g_ui.graph);
    std::vector<AudioRoute> ready;
    for (auto& r : routes)
        if (routeRunnable(g_ui.graph, r)) ready.push_back(r);

    if (ready.empty()) {
        setStatus(L"Wire Input through optional effects to an Output, then pick devices.");
        return;
    }

    joinStreams();
    clearLiveFx();
    { std::lock_guard lock(g_barsMu); memset(g_bars, 0, sizeof(g_bars)); }
    g_meterIn.store(0.f);
    g_meterOut.store(0.f);

    const bool multi = ready.size() > 1;
    g_run = true;
    g_busy = true;
    g_activeStreams = (int)ready.size();

    for (auto& r : ready) {
        Job job{};
        if (!buildJobFromRoute(g_ui.graph, r, job, multi)) {
            --g_activeStreams;
            continue;
        }
        {
            std::lock_guard lock(g_liveFxMu);
            LiveFxBinding b;
            b.fx = job.fx;
            if (r.hasSplit) {
                b.effectNodes = r.preEffects;
                b.effectNodes.insert(b.effectNodes.end(), r.leftEffects.begin(), r.leftEffects.end());
                b.effectNodes.insert(b.effectNodes.end(), r.rightEffects.begin(), r.rightEffects.end());
                b.effectNodes.insert(b.effectNodes.end(), r.postEffects.begin(), r.postEffects.end());
            } else {
                b.effectNodes = r.effects;
            }
            g_liveFx.push_back(std::move(b));
        }
        g_threads.emplace_back(runJob, job);
    }

    if (g_activeStreams <= 0) {
        g_busy = false;
        setStatus(L"Stream failed.");
        return;
    }

    setStatus(multi
        ? L"Streaming " + std::to_wstring((int)g_activeStreams) + L" routes\u2026"
        : L"Streaming\u2026");
    InvalidateRect(hwnd, nullptr, FALSE);
}

// Drawing

static void fillRound(HDC hdc, RECT r, COLORREF fill, COLORREF edge) {
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, edge);
    HGDIOBJ obr = SelectObject(hdc, br);
    HGDIOBJ open = SelectObject(hdc, pen);
    RoundRect(hdc, r.left, r.top, r.right, r.bottom, 14, 14);
    SelectObject(hdc, obr); SelectObject(hdc, open);
    DeleteObject(br); DeleteObject(pen);
}

static void drawDots(HDC hdc, const RECT& rc) {
    HBRUSH br = CreateSolidBrush(kDot);
    const int x0 = rc.left - ((g_ui.view.scrollX % kDotStep) + kDotStep) % kDotStep;
    const int y0 = rc.top - ((g_ui.view.scrollY % kDotStep) + kDotStep) % kDotStep;
    for (int y = y0; y < rc.bottom; y += kDotStep) {
        if (y < rc.top) continue;
        for (int x = x0; x < rc.right; x += kDotStep) {
            if (x < rc.left) continue;
            RECT d{ x - 1, y - 1, x + 1, y + 1 };
            FillRect(hdc, &d, br);
        }
    }
    DeleteObject(br);
}

static void drawToolbarButton(HDC hdc, RECT r, bool hot, const wchar_t* text) {
    fillRound(hdc, r, hot ? kBtnHot : kBtn, kNodeEdge);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, kText);
    SelectObject(hdc, g_fontUi);
    DrawTextW(hdc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void drawMenu(HDC hdc) {
    if (g_ui.menu == MenuKind::None) return;
    fillRound(hdc, g_ui.menuRc, kNode, kNodeEdge);
    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, g_fontUi);

    if (g_ui.menu == MenuKind::Add) {
        auto& cat = nodeCatalog();
        for (int i = 0; i < (int)cat.size(); ++i) {
            RECT row{ g_ui.menuRc.left + 4, g_ui.menuRc.top + 4 + i * 30,
                      g_ui.menuRc.right - 4, g_ui.menuRc.top + 4 + i * 30 + 28 };
            if (g_ui.menuHover == i) fillRound(hdc, row, kBtnHot, kNodeEdge);
            SetTextColor(hdc, kText);
            RECT tr = row; InflateRect(&tr, -8, 0);
            DrawTextW(hdc, cat[i].menuLabel, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        return;
    }

    if (g_ui.menu == MenuKind::DeleteNode) {
        RECT row{ g_ui.menuRc.left + 4, g_ui.menuRc.top + 4,
                  g_ui.menuRc.right - 4, g_ui.menuRc.bottom - 4 };
        if (g_ui.menuHover == 0) fillRound(hdc, row, kBtnHot, kNodeEdge);
        SetTextColor(hdc, RGB(230, 140, 140));
        RECT tr = row; InflateRect(&tr, -8, 0);
        DrawTextW(hdc, L"Delete", -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    if (g_ui.menu == MenuKind::Settings) {
        RECT tip = g_ui.menuRc;
        tip.bottom = tip.top + 26;
        InflateRect(&tip, -10, 0);
        SetTextColor(hdc, kMuted);
        DrawTextW(hdc, L"Virtual cable", -1, &tip, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        int i = 0;
        bool any = false;
        for (size_t oi = 0; oi < g_outs.size(); ++oi) {
            if (!isCable(g_outs[oi].name)) continue;
            any = true;
            RECT row{ g_ui.menuRc.left + 4, g_ui.menuRc.top + 28 + i * 28,
                      g_ui.menuRc.right - 4, g_ui.menuRc.top + 28 + i * 28 + 26 };
            if (g_ui.menuHover == i) fillRound(hdc, row, kBtnHot, kNodeEdge);
            const bool sel = (g_outs[oi].id == g_cableId);
            SetTextColor(hdc, sel ? kAccent : kText);
            RECT tr = row; InflateRect(&tr, -8, 0);
            std::wstring t = (sel ? L"\u2713  " : L"    ") + g_outs[oi].name;
            DrawTextW(hdc, t.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            ++i;
        }
        if (!any) {
            RECT row{ g_ui.menuRc.left + 10, g_ui.menuRc.top + 32, g_ui.menuRc.right - 10, g_ui.menuRc.bottom - 6 };
            SetTextColor(hdc, kMuted);
            DrawTextW(hdc, L"No virtual cable found", -1, &row, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        return;
    }

    // Source / device / preset picker rows
    if (g_ui.menu == MenuKind::PickNode) {
        if (g_ui.pickNode < 0 || g_ui.pickNode >= (int)g_ui.graph.nodes.size()) return;
        const auto& beh = nodeBehavior(g_ui.graph.nodes[g_ui.pickNode].kind);
        const int count = beh.pickCount();
        if (count == 0) {
            RECT row = g_ui.menuRc; InflateRect(&row, -10, -8);
            SetTextColor(hdc, kMuted);
            DrawTextW(hdc, L"Nothing available", -1, &row, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            return;
        }
        const int rows = (std::min)(count, 10);
        for (int i = 0; i < rows; ++i) {
            RECT row{ g_ui.menuRc.left + 4, g_ui.menuRc.top + 4 + i * 26,
                      g_ui.menuRc.right - 4, g_ui.menuRc.top + 4 + i * 26 + 24 };
            if (g_ui.menuHover == i) fillRound(hdc, row, kBtnHot, kNodeEdge);
            SetTextColor(hdc, kText);
            RECT tr = row; InflateRect(&tr, -8, 0);
            std::wstring t = beh.pickLabel(i);
            DrawTextW(hdc, t.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        return;
    }
}

static void paint(HWND hwnd) {
    PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc{}; GetClientRect(hwnd, &rc);

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);

    FillRect(mem, &rc, g_brBg);
    RECT canvas{ 0, kToolbar, rc.right, rc.bottom - 28 };
    IntersectClipRect(mem, canvas.left, canvas.top, canvas.right, canvas.bottom);
    drawDots(mem, canvas);

    const bool flowing = g_busy && g_run;
    drawGraph(mem, g_ui.graph, g_ui.view, fonts(), flowing, audioLevel(),
              g_meterIn.load(std::memory_order_relaxed),
              g_meterOut.load(std::memory_order_relaxed));
    if (g_ui.dragWire)
        drawWire(mem, g_ui.wireFrom, g_ui.wireCur, false, 0.f);

    SelectClipRgn(mem, nullptr);

    RECT bar{ 0, 0, rc.right, kToolbar };
    HBRUSH brBar = CreateSolidBrush(RGB(46, 46, 50));
    FillRect(mem, &bar, brBar);
    DeleteObject(brBar);
    HPEN sep = CreatePen(PS_SOLID, 1, RGB(36, 36, 40));
    HGDIOBJ osep = SelectObject(mem, sep);
    MoveToEx(mem, 0, kToolbar - 1, nullptr);
    LineTo(mem, rc.right, kToolbar - 1);
    SelectObject(mem, osep); DeleteObject(sep);

    drawToolbarButton(mem, plusBtn(), g_ui.plusHot || g_ui.menu == MenuKind::Add, L"+");
    drawToolbarButton(mem, gearBtn(hwnd), g_ui.gearHot || g_ui.menu == MenuKind::Settings, L"\u2699");
    drawToolbarButton(mem, runBtn(hwnd), g_ui.runHot, g_busy ? L"Stop" : L"Run");

    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, kMuted);
    SelectObject(mem, g_fontUi);
    RECT brand{ 52, 8, 280, 36 };
    DrawTextW(mem, L"Tone Graph", -1, &brand, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    std::wstring st;
    { std::lock_guard lock(g_statusMu); st = g_status; }
    SetTextColor(mem, kMuted);
    RECT sr{ 16, rc.bottom - 28, rc.right - 16, rc.bottom - 8 };
    FillRect(mem, &sr, g_brBg);
    DrawTextW(mem, st.c_str(), -1, &sr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    drawMenu(mem);

    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

// Mouse / keys

static int menuIndexAt(int x, int y) {
    if (g_ui.menu == MenuKind::None || !hitRect(g_ui.menuRc, x, y)) return -1;
    if (g_ui.menu == MenuKind::Add) {
        const int n = nodeCatalogCount();
        for (int i = 0; i < n; ++i) {
            RECT row{ g_ui.menuRc.left + 4, g_ui.menuRc.top + 4 + i * 30,
                      g_ui.menuRc.right - 4, g_ui.menuRc.top + 4 + i * 30 + 28 };
            if (hitRect(row, x, y)) return i;
        }
        return -1;
    }
    if (g_ui.menu == MenuKind::DeleteNode) {
        RECT row{ g_ui.menuRc.left + 4, g_ui.menuRc.top + 4,
                  g_ui.menuRc.right - 4, g_ui.menuRc.bottom - 4 };
        return hitRect(row, x, y) ? 0 : -1;
    }
    if (g_ui.menu == MenuKind::Settings) {
        int i = 0;
        for (size_t oi = 0; oi < g_outs.size(); ++oi) {
            if (!isCable(g_outs[oi].name)) continue;
            RECT row{ g_ui.menuRc.left + 4, g_ui.menuRc.top + 28 + i * 28,
                      g_ui.menuRc.right - 4, g_ui.menuRc.top + 28 + i * 28 + 26 };
            if (hitRect(row, x, y)) return i;
            ++i;
        }
        return -1;
    }
    if (g_ui.menu == MenuKind::PickNode) {
        if (g_ui.pickNode < 0 || g_ui.pickNode >= (int)g_ui.graph.nodes.size()) return -1;
        const int count = nodeBehavior(g_ui.graph.nodes[g_ui.pickNode].kind).pickCount();
        const int rows = (std::min)(count, 10);
        for (int i = 0; i < rows; ++i) {
            RECT row{ g_ui.menuRc.left + 4, g_ui.menuRc.top + 4 + i * 26,
                      g_ui.menuRc.right - 4, g_ui.menuRc.top + 4 + i * 26 + 24 };
            if (hitRect(row, x, y)) return i;
        }
        return -1;
    }
    return -1;
}

static void onMenuClick(HWND hwnd, int idx) {
    if (idx < 0) { closeMenus(); return; }

    if (g_ui.menu == MenuKind::Add) {
        closeMenus();
        auto& cat = nodeCatalog();
        if (idx >= 0 && idx < (int)cat.size())
            spawnNode(hwnd, cat[idx].kind);
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    if (g_ui.menu == MenuKind::DeleteNode) {
        const int node = g_ui.pickNode;
        closeMenus();
        if (idx == 0 && node >= 0) {
            if (g_busy) stopIfRunning(hwnd, L"Node deleted \u2014 stopped.");
            deleteNode(g_ui.graph, node);
            if (!g_busy) setStatus(L"Node deleted.");
            clampScroll();
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    if (g_ui.menu == MenuKind::Settings) {
        int i = 0;
        for (auto& o : g_outs) {
            if (!isCable(o.name)) continue;
            if (i == idx) {
                g_cableId = o.id;
                g_cableName = o.name;
                setStatus(L"Virtual cable: " + o.name);
                break;
            }
            ++i;
        }
        closeMenus();
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    if (g_ui.pickNode < 0 || g_ui.pickNode >= (int)g_ui.graph.nodes.size()) {
        closeMenus();
        return;
    }

    if (g_ui.menu == MenuKind::PickNode) {
        GraphNode& n = g_ui.graph.nodes[g_ui.pickNode];
        std::wstring status;
        if (nodeBehavior(n.kind).applyPick(n, idx, &status)) {
            setStatus(status);
            if (g_busy) syncLiveFxFromGraph(g_ui.graph);
        }
        closeMenus();
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

static void bringNodeToFront(int i) {
    if (i < 0 || i + 1 >= (int)g_ui.graph.nodes.size()) return;
    GraphNode moved = g_ui.graph.nodes[i];
    g_ui.graph.nodes.erase(g_ui.graph.nodes.begin() + i);
    for (auto& c : g_ui.graph.links) {
        if (c.from == i) c.from = (int)g_ui.graph.nodes.size();
        else if (c.from > i) --c.from;
        if (c.to == i) c.to = (int)g_ui.graph.nodes.size();
        else if (c.to > i) --c.to;
    }
    g_ui.graph.nodes.push_back(moved);
    g_ui.dragIdx = (int)g_ui.graph.nodes.size() - 1;
}

static void onLButtonDown(HWND hwnd, int x, int y) {
    if (g_ui.menu != MenuKind::None) {
        if (hitRect(g_ui.menuRc, x, y)) {
            onMenuClick(hwnd, menuIndexAt(x, y));
            return;
        }
        closeMenus();
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    if (y < kToolbar) {
        if (hitRect(plusBtn(), x, y)) {
            if (g_ui.menu == MenuKind::Add) closeMenus();
            else openAddMenu();
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        if (hitRect(gearBtn(hwnd), x, y)) {
            if (g_ui.menu == MenuKind::Settings) closeMenus();
            else openSettingsMenu(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        if (hitRect(runBtn(hwnd), x, y)) {
            tryStartStop(hwnd);
            return;
        }
        return;
    }

    int eqBand = -1;
    int eqNi = hitEqHandleAt(g_ui.graph, g_ui.view, x, y, &eqBand);
    if (eqNi >= 0 && eqBand >= 0) {
        g_ui.dragEq = true;
        g_ui.eqNode = eqNi;
        g_ui.eqBand = eqBand;
        applyEqBandFromY(g_ui.graph.nodes[eqNi], eqBand, g_ui.view, y);
        if (g_busy) syncLiveFxFromGraph(g_ui.graph);
        SetCapture(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    int knobIdx = -1;
    int knobNi = hitKnobAt(g_ui.graph, g_ui.view, x, y, &knobIdx);
    if (knobNi >= 0 && knobIdx >= 0) {
        g_ui.dragKnob = true;
        g_ui.knobNode = knobNi;
        g_ui.knobIdx = knobIdx;
        g_ui.knobStartY = y;
        g_ui.knobStartNorm = knobNorm(g_ui.graph.nodes[knobNi], knobIdx);
        SetCapture(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    int outPort = 0;
    int oi = hitOutPortAt(g_ui.graph, g_ui.view, x, y, &outPort);
    if (oi >= 0) {
        g_ui.dragWire = true;
        g_ui.wireNode = oi;
        g_ui.wirePort = outPort;
        g_ui.wireFromOutPort = true;
        g_ui.wireFrom = outPortScreen(g_ui.graph.nodes[oi], g_ui.view, outPort);
        g_ui.wireCur = { x, y };
        SetCapture(hwnd);
        return;
    }
    int inPort = 0;
    int ii = hitInPortAt(g_ui.graph, g_ui.view, x, y, &inPort);
    if (ii >= 0) {
        g_ui.dragWire = true;
        g_ui.wireNode = ii;
        g_ui.wirePort = inPort;
        g_ui.wireFromOutPort = false;
        g_ui.wireFrom = inPortScreen(g_ui.graph.nodes[ii], g_ui.view, inPort);
        g_ui.wireCur = { x, y };
        SetCapture(hwnd);
        return;
    }

    int ni = hitNodeAt(g_ui.graph, g_ui.view, x, y);
    if (ni >= 0) {
        auto& n = g_ui.graph.nodes[ni];
        if (n.kind == NodeKind::Eq && hitEqCurve(n, g_ui.view, x, y)) {
            const int band = eqBandAt(n, g_ui.view, x, y);
            if (band >= 0) {
                g_ui.dragEq = true;
                g_ui.eqNode = ni;
                g_ui.eqBand = band;
                applyEqBandFromY(n, band, g_ui.view, y);
                if (g_busy) syncLiveFxFromGraph(g_ui.graph);
                SetCapture(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            }
        }
        if (hitNodeField(n, g_ui.view, x, y)) {
            openPickMenu(ni);
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        POINT w = screenToWorld(x, y, g_ui.view);
        g_ui.dragNode = true;
        g_ui.dragIdx = ni;
        g_ui.dragOff = { w.x - n.x, w.y - n.y };
        bringNodeToFront(ni);
        SetCapture(hwnd);
        return;
    }

    g_ui.panDrag = true;
    g_ui.panLast = { x, y };
    SetCapture(hwnd);
}

static void onMouseMove(HWND hwnd, int x, int y) {
    bool ph = hitRect(plusBtn(), x, y);
    bool gh = hitRect(gearBtn(hwnd), x, y);
    bool rh = hitRect(runBtn(hwnd), x, y);
    int mh = menuIndexAt(x, y);
    bool changed = (ph != g_ui.plusHot || gh != g_ui.gearHot || rh != g_ui.runHot || mh != g_ui.menuHover);
    g_ui.plusHot = ph; g_ui.gearHot = gh; g_ui.runHot = rh; g_ui.menuHover = mh;

    if (g_ui.panDrag) {
        g_ui.view.scrollX -= (x - g_ui.panLast.x);
        g_ui.view.scrollY -= (y - g_ui.panLast.y);
        g_ui.panLast = { x, y };
        clampScroll();
        changed = true;
    }
    if (g_ui.dragWire) {
        g_ui.wireFrom = g_ui.wireFromOutPort
            ? outPortScreen(g_ui.graph.nodes[g_ui.wireNode], g_ui.view, g_ui.wirePort)
            : inPortScreen(g_ui.graph.nodes[g_ui.wireNode], g_ui.view, g_ui.wirePort);
        g_ui.wireCur = { x, y };
        changed = true;
    }
    if (g_ui.dragEq && g_ui.eqNode >= 0 && g_ui.eqNode < (int)g_ui.graph.nodes.size()) {
        applyEqBandFromY(g_ui.graph.nodes[g_ui.eqNode], g_ui.eqBand, g_ui.view, y);
        if (g_busy) syncLiveFxFromGraph(g_ui.graph);
        changed = true;
    }
    if (g_ui.dragKnob && g_ui.knobNode >= 0 && g_ui.knobNode < (int)g_ui.graph.nodes.size()) {
        // drag up increases; ~120px spans full range
        const float delta = (float)(g_ui.knobStartY - y) / 120.f;
        applyKnobNorm(g_ui.graph.nodes[g_ui.knobNode], g_ui.knobIdx, g_ui.knobStartNorm + delta);
        if (g_busy) syncLiveFxFromGraph(g_ui.graph);
        changed = true;
    }
    if (g_ui.dragNode && g_ui.dragIdx >= 0 && g_ui.dragIdx < (int)g_ui.graph.nodes.size()) {
        POINT w = screenToWorld(x, y, g_ui.view);
        auto& n = g_ui.graph.nodes[g_ui.dragIdx];
        n.x = w.x - g_ui.dragOff.x;
        n.y = w.y - g_ui.dragOff.y;
        clampScroll();
        changed = true;
    }
    if (changed) InvalidateRect(hwnd, nullptr, FALSE);
}

static void onLButtonUp(HWND hwnd, int x, int y) {
    if (g_ui.panDrag) {
        g_ui.panDrag = false;
        ReleaseCapture();
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    if (g_ui.dragWire) {
        const int fromNode = g_ui.wireNode;
        const int fromPort = g_ui.wirePort;
        const bool fromOut = g_ui.wireFromOutPort;
        g_ui.dragWire = false;
        g_ui.wireNode = -1;
        g_ui.wirePort = 0;
        ReleaseCapture();

        if (fromOut) {
            int toPort = 0;
            int to = hitInPortAt(g_ui.graph, g_ui.view, x, y, &toPort);
            if (to >= 0 && addLink(g_ui.graph, fromNode, to, fromPort, toPort))
                setStatus(L"Connected. Choose devices, then Run.");
        } else {
            int outPort = 0;
            int from = hitOutPortAt(g_ui.graph, g_ui.view, x, y, &outPort);
            if (from >= 0 && addLink(g_ui.graph, from, fromNode, outPort, fromPort))
                setStatus(L"Connected. Choose devices, then Run.");
        }
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    if (g_ui.dragEq) {
        g_ui.dragEq = false;
        g_ui.eqNode = -1;
        g_ui.eqBand = -1;
        ReleaseCapture();
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    if (g_ui.dragKnob) {
        g_ui.dragKnob = false;
        g_ui.knobNode = -1;
        g_ui.knobIdx = -1;
        ReleaseCapture();
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    if (g_ui.dragNode) {
        g_ui.dragNode = false;
        g_ui.dragIdx = -1;
        ReleaseCapture();
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

static void onRButtonDown(HWND hwnd, int x, int y) {
    if (g_ui.menu != MenuKind::None) {
        closeMenus();
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    const int ni = hitNodeAt(g_ui.graph, g_ui.view, x, y);
    if (ni >= 0) {
        openDeleteMenu(ni, x, y);
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    const int wi = hitWireAt(g_ui.graph, g_ui.view, x, y);
    if (wi >= 0) {
        g_ui.graph.links.erase(g_ui.graph.links.begin() + wi);
        if (g_busy) stopIfRunning(hwnd, L"Connection removed \u2014 stopped.");
        else setStatus(L"Connection removed.");
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_wnd = hwnd;
        g_brBg = CreateSolidBrush(kBg);
        g_fontUi = makeFont(15, FW_NORMAL, L"Segoe UI");
        g_fontTitle = makeFont(12, FW_SEMIBOLD, L"Segoe UI");
        refreshDevices();
        setStatus(L"Wheel to scroll, drag empty canvas or middle-mouse to pan.");
        SetTimer(hwnd, 1, 33, nullptr);
        return 0;
    }
    case WM_SIZE:
        clampScroll();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_TIMER:
        if (wp == 1) {
            if (!g_busy) {
                float in = g_meterIn.load(std::memory_order_relaxed);
                float out = g_meterOut.load(std::memory_order_relaxed);
                if (in > 0.001f) g_meterIn.store(in * 0.82f, std::memory_order_relaxed);
                if (out > 0.001f) g_meterOut.store(out * 0.82f, std::memory_order_relaxed);
            }
            if (g_busy || g_ui.dragEq || g_ui.dragKnob ||
                g_meterIn.load(std::memory_order_relaxed) > 0.001f ||
                g_meterOut.load(std::memory_order_relaxed) > 0.001f)
                InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSEWHEEL: {
        const int delta = GET_WHEEL_DELTA_WPARAM(wp);
        const int step = (delta * 48) / WHEEL_DELTA;
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (shift) g_ui.view.scrollX -= step;
        else g_ui.view.scrollY -= step;
        clampScroll();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEHWHEEL: {
        const int delta = GET_WHEEL_DELTA_WPARAM(wp);
        g_ui.view.scrollX += (delta * 48) / WHEEL_DELTA;
        clampScroll();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_MBUTTONDOWN:
        g_ui.panDrag = true;
        g_ui.panLast = { (short)LOWORD(lp), (short)HIWORD(lp) };
        SetCapture(hwnd);
        return 0;
    case WM_MBUTTONUP:
        if (g_ui.panDrag) {
            g_ui.panDrag = false;
            ReleaseCapture();
        }
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: paint(hwnd); return 0;
    case WM_LBUTTONDOWN:
        onLButtonDown(hwnd, (short)LOWORD(lp), (short)HIWORD(lp));
        return 0;
    case WM_MOUSEMOVE:
        onMouseMove(hwnd, (short)LOWORD(lp), (short)HIWORD(lp));
        return 0;
    case WM_LBUTTONUP:
        onLButtonUp(hwnd, (short)LOWORD(lp), (short)HIWORD(lp));
        return 0;
    case WM_RBUTTONDOWN:
        onRButtonDown(hwnd, (short)LOWORD(lp), (short)HIWORD(lp));
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { closeMenus(); InvalidateRect(hwnd, nullptr, FALSE); }
        if (wp == VK_F5) { refreshDevices(); setStatus(L"Devices refreshed."); InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    case WM_APP + 1:
    case WM_APP + 2:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        g_run = false;
        joinStreams();
        clearMedia();
        if (g_fontUi) DeleteObject(g_fontUi);
        if (g_fontTitle) DeleteObject(g_fontTitle);
        if (g_brBg) DeleteObject(g_brBg);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
