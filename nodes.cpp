#include "nodes.h"
#include "audio_fx.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>

// Colors

static constexpr COLORREF kNode     = RGB(68, 68, 74);
static constexpr COLORREF kNodeEdge = RGB(92, 92, 100);
static constexpr COLORREF kText     = RGB(220, 220, 224);
static constexpr COLORREF kMuted    = RGB(150, 150, 156);
static constexpr COLORREF kPort     = RGB(200, 200, 206);
static constexpr COLORREF kPortHot  = RGB(120, 190, 170);
static constexpr COLORREF kWire     = RGB(140, 140, 148);
static constexpr COLORREF kSelect   = RGB(58, 58, 64);

static void fillRound(HDC hdc, RECT r, COLORREF fill, COLORREF edge) {
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, edge);
    HGDIOBJ obr = SelectObject(hdc, br);
    HGDIOBJ open = SelectObject(hdc, pen);
    RoundRect(hdc, r.left, r.top, r.right, r.bottom, 14, 14);
    SelectObject(hdc, obr); SelectObject(hdc, open);
    DeleteObject(br); DeleteObject(pen);
}

static void bezierControls(POINT a, POINT b, POINT& p1, POINT& p2) {
    const int dx = (std::max)(40, abs((int)b.x - (int)a.x) / 2);
    p1 = { a.x + dx, a.y };
    p2 = { b.x - dx, b.y };
}

static POINT bezierAt(POINT p0, POINT p1, POINT p2, POINT p3, float t) {
    const float u = 1.f - t, uu = u * u, tt = t * t;
    const float uuu = uu * u, ttt = tt * t;
    return {
        (int)(uuu * p0.x + 3 * uu * t * p1.x + 3 * u * tt * p2.x + ttt * p3.x + 0.5f),
        (int)(uuu * p0.y + 3 * uu * t * p1.y + 3 * u * tt * p2.y + ttt * p3.y + 0.5f)
    };
}

static float dist2(POINT a, POINT b) {
    const float dx = (float)(a.x - b.x), dy = (float)(a.y - b.y);
    return dx * dx + dy * dy;
}

static void noopRebind(GraphNode&) {}
static bool alwaysReady(const GraphNode&) { return true; }

// INPUT

static void inputInit(GraphNode& n) {
    n.sel = -1; n.appName.clear(); n.appPid = 0; n.deviceId.clear();
    n.gainDb = 0; n.eqSubDb = n.eqLowDb = n.eqMidDb = n.eqHighDb = n.eqAirDb = 0;
}

static void inputRebind(GraphNode& n) {
    n.sel = -1;
    if (n.appName.empty()) return;
    int best = -1, bestScore = -1;
    for (int i = 0; i < (int)g_apps.size(); ++i) {
        if (_wcsicmp(g_apps[i].name.c_str(), n.appName.c_str()) != 0) continue;
        int score = 10;
        if (n.appPid && g_apps[i].pid == n.appPid) score += 100;
        if (g_apps[i].playing) score += 5;
        if (score > bestScore) { bestScore = score; best = i; }
    }
    n.sel = best;
    if (best >= 0) n.appPid = g_apps[best].pid;
}

static std::wstring inputLabel(const GraphNode& n) {
    if (n.sel >= 0 && n.sel < (int)g_apps.size()) {
        auto& a = g_apps[n.sel];
        return a.playing ? (L"\u25CF " + a.name) : (L"\u25CB " + a.name);
    }
    if (!n.appName.empty()) return n.appName + L" (offline)";
    return L"Choose source\u2026";
}

static bool inputCanRun(const GraphNode& n) {
    return n.sel >= 0 && n.sel < (int)g_apps.size() && g_apps[n.sel].pid != 0;
}

static NodePickMode inputPickMode() { return NodePickMode::SourceList; }
static int inputPickCount() { return (int)g_apps.size(); }
static std::wstring inputPickLabel(int index) {
    if (index < 0 || index >= (int)g_apps.size()) return {};
    auto& a = g_apps[index];
    return (a.playing ? L"\u25CF " : L"\u25CB ") + a.name;
}
static bool inputApplyPick(GraphNode& n, int index, std::wstring* status) {
    if (index < 0 || index >= (int)g_apps.size()) return false;
    n.sel = index; n.appName = g_apps[index].name; n.appPid = g_apps[index].pid;
    g_mediaHint = g_apps[index].name;
    if (status) *status = L"Source: " + g_apps[index].name;
    return true;
}

static const NodeBehavior kInputBehavior = {
    inputInit, inputRebind, inputLabel, inputCanRun,
    inputPickMode, inputPickCount, inputPickLabel, inputApplyPick,
    true, false, false
};

// OUTPUT

static void outputInit(GraphNode& n) {
    n.sel = -1; n.appName.clear(); n.appPid = 0; n.deviceId.clear();
    n.gainDb = 0; n.eqSubDb = n.eqLowDb = n.eqMidDb = n.eqHighDb = n.eqAirDb = 0;
    for (size_t i = 0; i < g_outs.size(); ++i) {
        if (g_outs[i].isDefault) { n.sel = (int)i; n.deviceId = g_outs[i].id; break; }
    }
}

static void outputRebind(GraphNode& n) {
    n.sel = -1;
    if (n.deviceId.empty()) return;
    for (int i = 0; i < (int)g_outs.size(); ++i)
        if (g_outs[i].id == n.deviceId) { n.sel = i; break; }
}

static std::wstring outputLabel(const GraphNode& n) {
    if (n.sel >= 0 && n.sel < (int)g_outs.size()) return g_outs[n.sel].name;
    if (!n.deviceId.empty()) return L"(device missing)";
    return L"Choose output\u2026";
}

static bool outputCanRun(const GraphNode& n) {
    return n.sel >= 0 && n.sel < (int)g_outs.size();
}

static NodePickMode outputPickMode() { return NodePickMode::OutputList; }
static int outputPickCount() { return (int)g_outs.size(); }
static std::wstring outputPickLabel(int index) {
    if (index < 0 || index >= (int)g_outs.size()) return {};
    std::wstring t = g_outs[index].name;
    if (g_outs[index].isDefault) t += L"  (default)";
    return t;
}
static bool outputApplyPick(GraphNode& n, int index, std::wstring* status) {
    if (index < 0 || index >= (int)g_outs.size()) return false;
    n.sel = index; n.deviceId = g_outs[index].id;
    if (status) *status = L"Output: " + g_outs[index].name;
    return true;
}

static const NodeBehavior kOutputBehavior = {
    outputInit, outputRebind, outputLabel, outputCanRun,
    outputPickMode, outputPickCount, outputPickLabel, outputApplyPick,
    false, true, false
};

// GAIN

static const float kGainSteps[] = {
    -12.f, -9.f, -6.f, -3.f, 0.f, 3.f, 6.f, 9.f, 12.f
};
static constexpr int kGainStepCount = 9;

static void gainInit(GraphNode& n) {
    n.sel = 4; // flat / 0 dB
    n.gainDb = 0.f;
    n.appName.clear(); n.appPid = 0; n.deviceId.clear();
    n.eqSubDb = n.eqLowDb = n.eqMidDb = n.eqHighDb = n.eqAirDb = 0;
}

static std::wstring gainLabel(const GraphNode& n) {
    std::wstring s = L"Gain  ";
    if (n.gainDb > 0) s += L"+";
    s += std::to_wstring((int)n.gainDb) + L" dB";
    return s;
}

static NodePickMode gainPickMode() { return NodePickMode::OptionList; }
static int gainPickCount() { return kGainStepCount; }
static std::wstring gainPickLabel(int index) {
    if (index < 0 || index >= kGainStepCount) return {};
    std::wstring s;
    if (kGainSteps[index] > 0) s += L"+";
    s += std::to_wstring((int)kGainSteps[index]) + L" dB";
    return s;
}
static bool gainApplyPick(GraphNode& n, int index, std::wstring* status) {
    if (index < 0 || index >= kGainStepCount) return false;
    n.sel = index;
    n.gainDb = kGainSteps[index];
    if (status) *status = gainLabel(n);
    return true;
}

static const NodeBehavior kGainBehavior = {
    gainInit, noopRebind, gainLabel, alwaysReady,
    gainPickMode, gainPickCount, gainPickLabel, gainApplyPick,
    false, false, true
};

// EQ

struct EqPreset {
    const wchar_t* name;
    float sub, low, mid, high, air;
};

static const EqPreset kEqPresets[] = {
    { L"Flat",         0.f,  0.f,  0.f,  0.f,  0.f },
    { L"Bass Boost",   5.f,  6.f,  0.f,  0.f,  0.f },
    { L"Treble Boost", 0.f,  0.f,  0.f,  5.f,  6.f },
    { L"Mid Scoop",    2.f,  2.f, -5.f,  2.f,  2.f },
    { L"Warm",         4.f,  3.f,  1.f, -2.f, -3.f },
    { L"Bright",      -2.f, -2.f,  0.f,  4.f,  5.f },
    { L"Voice",       -3.f, -2.f,  4.f,  1.f,  0.f },
};
static constexpr int kEqPresetCount = 7;

static void eqInit(GraphNode& n) {
    n.sel = 0;
    n.gainDb = 0;
    n.eqSubDb = n.eqLowDb = n.eqMidDb = n.eqHighDb = n.eqAirDb = 0;
    n.appName.clear(); n.appPid = 0; n.deviceId.clear();
}

static std::wstring eqLabel(const GraphNode& n) {
    if (n.sel >= 0 && n.sel < kEqPresetCount)
        return std::wstring(L"EQ  ") + kEqPresets[n.sel].name;
    return L"EQ  Custom";
}

static NodePickMode eqPickMode() { return NodePickMode::OptionList; }
static int eqPickCount() { return kEqPresetCount; }
static std::wstring eqPickLabel(int index) {
    if (index < 0 || index >= kEqPresetCount) return {};
    return kEqPresets[index].name;
}
static bool eqApplyPick(GraphNode& n, int index, std::wstring* status) {
    if (index < 0 || index >= kEqPresetCount) return false;
    n.sel = index;
    n.eqSubDb = kEqPresets[index].sub;
    n.eqLowDb = kEqPresets[index].low;
    n.eqMidDb = kEqPresets[index].mid;
    n.eqHighDb = kEqPresets[index].high;
    n.eqAirDb = kEqPresets[index].air;
    if (status) *status = eqLabel(n);
    return true;
}

static const NodeBehavior kEqBehavior = {
    eqInit, noopRebind, eqLabel, alwaysReady,
    eqPickMode, eqPickCount, eqPickLabel, eqApplyPick,
    false, false, true
};

// Catalog

static const NodeTypeInfo kCatalog[] = {
    { NodeKind::Input,  L"input",  L"Input",  L"INPUT",  false, true,  &kInputBehavior  },
    { NodeKind::Output, L"output", L"Output", L"OUTPUT", true,  false, &kOutputBehavior },
    { NodeKind::Gain,   L"gain",   L"Gain",   L"GAIN",   true,  true,  &kGainBehavior   },
    { NodeKind::Eq,     L"eq",     L"EQ",     L"EQ",     true,  true,  &kEqBehavior     },
};

const std::vector<NodeTypeInfo>& nodeCatalog() {
    static const std::vector<NodeTypeInfo> cat(std::begin(kCatalog), std::end(kCatalog));
    return cat;
}

const NodeTypeInfo& nodeType(NodeKind kind) {
    for (auto& t : kCatalog)
        if (t.kind == kind) return t;
    return kCatalog[0];
}

const NodeBehavior& nodeBehavior(NodeKind kind) {
    return *nodeType(kind).behavior;
}

int nodeCatalogCount() {
    return (int)(sizeof(kCatalog) / sizeof(kCatalog[0]));
}

// Geometry

int nodeHeight(const GraphNode& n) {
    return n.kind == NodeKind::Eq ? kEqNodeH : kNodeH;
}

POINT worldToScreen(POINT w, const NodeView& view) {
    return { w.x - view.scrollX, w.y - view.scrollY };
}
POINT screenToWorld(int sx, int sy, const NodeView& view) {
    return { sx + view.scrollX, sy + view.scrollY };
}
RECT nodeRectWorld(const GraphNode& n) {
    const int h = nodeHeight(n);
    return { n.x, n.y, n.x + kNodeW, n.y + h };
}
RECT nodeRectScreen(const GraphNode& n, const NodeView& view) {
    POINT tl = worldToScreen({ n.x, n.y }, view);
    const int h = nodeHeight(n);
    return { tl.x, tl.y, tl.x + kNodeW, tl.y + h };
}
RECT eqPresetRectScreen(const GraphNode& n, const NodeView& view) {
    RECT r = nodeRectScreen(n, view);
    return { r.left + 14, r.bottom - 32, r.right - 14, r.bottom - 10 };
}
RECT eqCurveRectScreen(const GraphNode& n, const NodeView& view) {
    RECT r = nodeRectScreen(n, view);
    return { r.left + 14, r.top + 36, r.right - 14, r.bottom - 38 };
}
RECT nodeMeterRectScreen(const GraphNode& n, const NodeView& view) {
    RECT r = nodeRectScreen(n, view);
    return { r.right - 22, r.top + 38, r.right - 10, r.bottom - 12 };
}
RECT nodeFieldRectScreen(const GraphNode& n, const NodeView& view) {
    RECT r = nodeRectScreen(n, view);
    if (n.kind == NodeKind::Eq)
        return eqPresetRectScreen(n, view);
    const int rightPad = (n.kind == NodeKind::Output || n.kind == NodeKind::Input) ? 28 : 14;
    return { r.left + 14, r.top + 42, r.right - rightPad, r.top + 74 };
}
POINT outPortWorld(const GraphNode& n) {
    // ports stay in the title so they don't steal EQ handle clicks
    const int cy = (n.kind == NodeKind::Eq) ? 18 : nodeHeight(n) / 2;
    return { n.x + kNodeW, n.y + cy };
}
POINT inPortWorld(const GraphNode& n) {
    const int cy = (n.kind == NodeKind::Eq) ? 18 : nodeHeight(n) / 2;
    return { n.x, n.y + cy };
}
POINT outPortScreen(const GraphNode& n, const NodeView& view) { return worldToScreen(outPortWorld(n), view); }
POINT inPortScreen(const GraphNode& n, const NodeView& view) { return worldToScreen(inPortWorld(n), view); }

bool hitRect(const RECT& r, int x, int y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}
bool hitPortAt(POINT portScreen, int x, int y) {
    const int dx = x - portScreen.x, dy = y - portScreen.y;
    return dx * dx + dy * dy <= (kPortR + 4) * (kPortR + 4);
}
bool nodeHasPicker(const GraphNode& n) {
    return nodeBehavior(n.kind).pickMode() != NodePickMode::None;
}
bool hitNodeField(const GraphNode& n, const NodeView& view, int sx, int sy) {
    return nodeHasPicker(n) && hitRect(nodeFieldRectScreen(n, view), sx, sy);
}
bool hitEqCurve(const GraphNode& n, const NodeView& view, int sx, int sy) {
    return n.kind == NodeKind::Eq && hitRect(eqCurveRectScreen(n, view), sx, sy);
}

static constexpr int kEqBands = 5;

static float eqBandFreq(int band) {
    float freqs[kEqControls];
    eqControlFreqs(freqs);
    if (band < 0) band = 0;
    if (band >= kEqBands) band = kEqBands - 1;
    return freqs[band];
}

static float* eqBandPtr(GraphNode& n, int band) {
    switch (band) {
    case 0: return &n.eqSubDb;
    case 1: return &n.eqLowDb;
    case 2: return &n.eqMidDb;
    case 3: return &n.eqHighDb;
    default: return &n.eqAirDb;
    }
}

static float eqBandDb(const GraphNode& n, int band) {
    switch (band) {
    case 0: return n.eqSubDb;
    case 1: return n.eqLowDb;
    case 2: return n.eqMidDb;
    case 3: return n.eqHighDb;
    default: return n.eqAirDb;
    }
}

static float eqResp(const GraphNode& n, float freq) {
    return eqCurveDb(freq, 48000.f, n.eqSubDb, n.eqLowDb, n.eqMidDb, n.eqHighDb, n.eqAirDb);
}

static float eqFreqToX(float freq, const RECT& curve) {
    const float t = logf((std::max)(freq, 20.f) / 20.f) / logf(20000.f / 20.f);
    return curve.left + t * (float)(curve.right - curve.left);
}

static float eqDbToY(float db, const RECT& curve) {
    // screen Y grows down, so flip dB
    const float t = (db - kEqMaxDb) / (kEqMinDb - kEqMaxDb);
    return curve.top + t * (float)(curve.bottom - curve.top);
}

static float eqYToDb(int y, const RECT& curve) {
    const float h = (float)(curve.bottom - curve.top);
    if (h < 1.f) return 0.f;
    const float t = (y - curve.top) / h;
    float db = kEqMaxDb + t * (kEqMinDb - kEqMaxDb);
    if (db < kEqMinDb) db = kEqMinDb;
    if (db > kEqMaxDb) db = kEqMaxDb;
    return db;
}

static float clampEqDb(float db) {
    if (db < kEqMinDb) return kEqMinDb;
    if (db > kEqMaxDb) return kEqMaxDb;
    return db;
}

// drag knob sits on the curve value
static POINT eqHandleScreen(const GraphNode& n, const NodeView& view, int band) {
    RECT curve = eqCurveRectScreen(n, view);
    return {
        (int)(eqFreqToX(eqBandFreq(band), curve) + 0.5f),
        (int)(eqDbToY(clampEqDb(eqBandDb(n, band)), curve) + 0.5f)
    };
}

int eqBandAt(const GraphNode& n, const NodeView& view, int sx, int sy) {
    if (n.kind != NodeKind::Eq) return -1;

    constexpr float kHitR = 16.f;
    int best = -1;
    float bestD = kHitR * kHitR;
    for (int b = 0; b < kEqBands; ++b) {
        POINT h = eqHandleScreen(n, view, b);
        const float dx = (float)(sx - h.x), dy = (float)(sy - h.y);
        const float d = dx * dx + dy * dy;
        if (d <= bestD) { bestD = d; best = b; }
    }
    if (best >= 0) return best;

    if (!hitEqCurve(n, view, sx, sy)) return -1;
    RECT curve = eqCurveRectScreen(n, view);
    const float t = (sx - curve.left) / (float)(std::max)(1, (int)(curve.right - curve.left));
    best = 0;
    float bestT = 1e9f;
    for (int b = 0; b < kEqBands; ++b) {
        const float bt = logf(eqBandFreq(b) / 20.f) / logf(20000.f / 20.f);
        const float d = fabsf(t - bt);
        if (d < bestT) { bestT = d; best = b; }
    }
    return best;
}

int hitEqHandleAt(const NodeGraph& g, const NodeView& view, int sx, int sy, int* outBand) {
    if (outBand) *outBand = -1;
    for (int i = (int)g.nodes.size() - 1; i >= 0; --i) {
        const auto& n = g.nodes[i];
        if (n.kind != NodeKind::Eq) continue;
        const int band = eqBandAt(n, view, sx, sy);
        if (band < 0) continue;
        POINT h = eqHandleScreen(n, view, band);
        const float dx = (float)(sx - h.x), dy = (float)(sy - h.y);
        if (dx * dx + dy * dy <= 16.f * 16.f) {
            if (outBand) *outBand = band;
            return i;
        }
    }
    return -1;
}

void applyEqBandFromY(GraphNode& n, int band, const NodeView& view, int sy) {
    if (band < 0 || band >= kEqBands || n.kind != NodeKind::Eq) return;
    RECT curve = eqCurveRectScreen(n, view);
    *eqBandPtr(n, band) = eqYToDb(sy, curve);
    n.sel = -1; // not a preset anymore
}

// Graph ops

GraphNode makeNode(NodeKind kind, int worldX, int worldY) {
    GraphNode n;
    n.kind = kind;
    n.x = worldX;
    n.y = worldY;
    nodeBehavior(kind).init(n);
    return n;
}

void deleteNode(NodeGraph& g, int idx) {
    if (idx < 0 || idx >= (int)g.nodes.size()) return;
    g.links.erase(std::remove_if(g.links.begin(), g.links.end(),
        [idx](const Connection& c) { return c.from == idx || c.to == idx; }), g.links.end());
    for (auto& c : g.links) {
        if (c.from > idx) --c.from;
        if (c.to > idx) --c.to;
    }
    g.nodes.erase(g.nodes.begin() + idx);
}

bool addLink(NodeGraph& g, int from, int to) {
    if (from < 0 || to < 0) return false;
    if (from >= (int)g.nodes.size() || to >= (int)g.nodes.size()) return false;
    if (from == to) return false;
    if (!nodeType(g.nodes[from].kind).hasOutPort) return false;
    if (!nodeType(g.nodes[to].kind).hasInPort) return false;
    for (auto& c : g.links)
        if (c.from == from && c.to == to) return false;
    g.links.push_back({ from, to });
    return true;
}

void rebindNodes(NodeGraph& g) {
    for (auto& n : g.nodes) nodeBehavior(n.kind).rebind(n);
}

std::wstring nodeLabel(const GraphNode& n) {
    return nodeBehavior(n.kind).label(n);
}

bool nodeCanRun(const GraphNode& n) {
    return nodeBehavior(n.kind).canRun(n);
}

static void collectOutgoing(const NodeGraph& g, int from, std::vector<int>& tos) {
    tos.clear();
    for (auto& c : g.links)
        if (c.from == from) tos.push_back(c.to);
}

static void walkRoutes(const NodeGraph& g, int cur, AudioRoute curRoute,
                       std::vector<AudioRoute>& out, std::set<int>& visiting) {
    // DFS from input through effects until we hit an output
    if (visiting.count(cur)) return;
    visiting.insert(cur);

    const auto& beh = nodeBehavior(g.nodes[cur].kind);
    if (beh.isAudioSink) {
        curRoute.sink = cur;
        if (curRoute.source >= 0) out.push_back(curRoute);
        visiting.erase(cur);
        return;
    }

    if (beh.isEffect)
        curRoute.effects.push_back(cur);

    std::vector<int> next;
    collectOutgoing(g, cur, next);
    for (int to : next) {
        if (to < 0 || to >= (int)g.nodes.size()) continue;
        walkRoutes(g, to, curRoute, out, visiting);
    }

    if (beh.isEffect && !curRoute.effects.empty())
        curRoute.effects.pop_back();
    visiting.erase(cur);
}

std::vector<AudioRoute> findAudioRoutes(const NodeGraph& g) {
    std::vector<AudioRoute> routes;
    for (int i = 0; i < (int)g.nodes.size(); ++i) {
        if (!nodeBehavior(g.nodes[i].kind).isAudioSource) continue;
        AudioRoute seed;
        seed.source = i;
        std::set<int> visiting;
        std::vector<int> next;
        collectOutgoing(g, i, next);
        for (int to : next) {
            if (to < 0 || to >= (int)g.nodes.size()) continue;
            walkRoutes(g, to, seed, routes, visiting);
        }
    }
    return routes;
}

bool routeRunnable(const NodeGraph& g, const AudioRoute& route) {
    if (route.source < 0 || route.sink < 0) return false;
    if (route.source >= (int)g.nodes.size() || route.sink >= (int)g.nodes.size()) return false;
    if (!nodeCanRun(g.nodes[route.source])) return false;
    if (!nodeCanRun(g.nodes[route.sink])) return false;
    for (int ei : route.effects) {
        if (ei < 0 || ei >= (int)g.nodes.size()) return false;
        if (!nodeCanRun(g.nodes[ei])) return false;
    }
    return true;
}

static FxStep stepFromNode(const GraphNode& n) {
    FxStep s;
    if (n.kind == NodeKind::Gain) {
        s.kind = FxKind::Gain;
        s.gainDb = n.gainDb;
    } else {
        s.kind = FxKind::Eq;
        s.eqSubDb = n.eqSubDb;
        s.eqLowDb = n.eqLowDb;
        s.eqMidDb = n.eqMidDb;
        s.eqHighDb = n.eqHighDb;
        s.eqAirDb = n.eqAirDb;
    }
    return s;
}

bool buildJobFromRoute(const NodeGraph& g, const AudioRoute& route, Job& outJob, bool forceLoopback) {
    if (!routeRunnable(g, route)) return false;
    auto& src = g.nodes[route.source];
    auto& sink = g.nodes[route.sink];
    outJob.app = g_apps[src.sel];
    outJob.out = g_outs[sink.sel];
    outJob.cableId = g_cableId;
    outJob.cableName = g_cableName;
    outJob.forceLoopback = forceLoopback;
    outJob.fx = std::make_shared<FxChain>();
    for (int ei : route.effects)
        outJob.fx->steps.push_back(stepFromNode(g.nodes[ei]));
    return true;
}

void syncLiveFxFromGraph(const NodeGraph& g) {
    std::lock_guard lock(g_liveFxMu);
    for (auto& b : g_liveFx) {
        if (!b.fx) continue;
        std::vector<FxStep> steps;
        steps.reserve(b.effectNodes.size());
        for (int ei : b.effectNodes) {
            if (ei < 0 || ei >= (int)g.nodes.size()) continue;
            if (!nodeBehavior(g.nodes[ei].kind).isEffect) continue;
            steps.push_back(stepFromNode(g.nodes[ei]));
        }
        b.fx->setSteps(std::move(steps));
    }
}

bool linkOnRunnableRoute(const NodeGraph& g, int linkIdx) {
    if (linkIdx < 0 || linkIdx >= (int)g.links.size()) return false;
    auto& link = g.links[linkIdx];
    for (auto& r : findAudioRoutes(g)) {
        if (!routeRunnable(g, r)) continue;
        // is this wire on a live Input→…→Output path?
        std::vector<int> path;
        path.push_back(r.source);
        path.insert(path.end(), r.effects.begin(), r.effects.end());
        path.push_back(r.sink);
        for (size_t i = 0; i + 1 < path.size(); ++i)
            if (link.from == path[i] && link.to == path[i + 1]) return true;
    }
    return false;
}

bool linkRunnable(const NodeGraph& g, int linkIdx) {
    return linkOnRunnableRoute(g, linkIdx);
}

std::vector<int> runnableLinks(const NodeGraph& g) {
    std::vector<int> out;
    for (int i = 0; i < (int)g.links.size(); ++i)
        if (linkRunnable(g, i)) out.push_back(i);
    return out;
}

bool buildJobFromLink(const NodeGraph& g, int linkIdx, Job& outJob, bool forceLoopback) {
    // old helper — prefer full routes when we can
    if (linkIdx < 0 || linkIdx >= (int)g.links.size()) return false;
    auto& c = g.links[linkIdx];
    if (c.from < 0 || c.to < 0) return false;
    AudioRoute r;
    r.source = c.from;
    r.sink = c.to;
    return buildJobFromRoute(g, r, outJob, forceLoopback);
}

int hitNodeAt(const NodeGraph& g, const NodeView& view, int sx, int sy) {
    for (int i = (int)g.nodes.size() - 1; i >= 0; --i)
        if (hitRect(nodeRectScreen(g.nodes[i], view), sx, sy)) return i;
    return -1;
}

int hitWireAt(const NodeGraph& g, const NodeView& view, int sx, int sy) {
    POINT cur{ sx, sy };
    int best = -1;
    float bestD = (float)(kWireHitPx * kWireHitPx);
    for (int i = 0; i < (int)g.links.size(); ++i) {
        auto& c = g.links[i];
        if (c.from < 0 || c.to < 0) continue;
        if (c.from >= (int)g.nodes.size() || c.to >= (int)g.nodes.size()) continue;
        POINT a = outPortScreen(g.nodes[c.from], view);
        POINT b = inPortScreen(g.nodes[c.to], view);
        POINT c1, c2; bezierControls(a, b, c1, c2);
        for (int s = 0; s <= 24; ++s) {
            float d = dist2(bezierAt(a, c1, c2, b, s / 24.f), cur);
            if (d < bestD) { bestD = d; best = i; }
        }
    }
    return best;
}

int hitOutPortAt(const NodeGraph& g, const NodeView& view, int sx, int sy) {
    for (int i = (int)g.nodes.size() - 1; i >= 0; --i) {
        if (!nodeType(g.nodes[i].kind).hasOutPort) continue;
        if (hitPortAt(outPortScreen(g.nodes[i], view), sx, sy)) return i;
    }
    return -1;
}

int hitInPortAt(const NodeGraph& g, const NodeView& view, int sx, int sy) {
    for (int i = (int)g.nodes.size() - 1; i >= 0; --i) {
        if (!nodeType(g.nodes[i].kind).hasInPort) continue;
        if (hitPortAt(inPortScreen(g.nodes[i], view), sx, sy)) return i;
    }
    return -1;
}

// Drawing

void drawPort(HDC hdc, POINT p, bool hot) {
    HBRUSH br = CreateSolidBrush(hot ? kPortHot : kPort);
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(30, 30, 34));
    HGDIOBJ obr = SelectObject(hdc, br);
    HGDIOBJ open = SelectObject(hdc, pen);
    Ellipse(hdc, p.x - kPortR, p.y - kPortR, p.x + kPortR, p.y + kPortR);
    SelectObject(hdc, obr); SelectObject(hdc, open);
    DeleteObject(br); DeleteObject(pen);
}

void drawWire(HDC hdc, POINT a, POINT b, bool flowing, float audioLevel) {
    POINT c1, c2;
    bezierControls(a, b, c1, c2);
    POINT pts[4] = { a, c1, c2, b };
    const float lvl = flowing ? audioLevel : 0.f;
    const int baseW = flowing ? 2 + (int)(lvl * 2.5f) : 2;
    COLORREF baseCol = flowing
        ? RGB(90 + (int)(lvl * 40), 150 + (int)(lvl * 30), 140 + (int)(lvl * 20))
        : kWire;
    HPEN pen = CreatePen(PS_SOLID, baseW, baseCol);
    HGDIOBJ old = SelectObject(hdc, pen);
    PolyBezier(hdc, pts, 4);
    SelectObject(hdc, old);
    DeleteObject(pen);
}

static void drawVolumeMeter(HDC hdc, const RECT& m, float level) {
    HBRUSH bg = CreateSolidBrush(RGB(40, 40, 44));
    HPEN edge = CreatePen(PS_SOLID, 1, kNodeEdge);
    HGDIOBJ obr = SelectObject(hdc, bg);
    HGDIOBJ open = SelectObject(hdc, edge);
    Rectangle(hdc, m.left, m.top, m.right, m.bottom);
    SelectObject(hdc, obr); SelectObject(hdc, open);
    DeleteObject(bg); DeleteObject(edge);

    float lvl = (std::max)(0.f, (std::min)(1.f, level));
    const int h = m.bottom - m.top - 4;
    const int filled = (int)(h * lvl + 0.5f);
    if (filled <= 0) return;

    const int greenH = (int)(h * 0.60f);
    const int yellowH = (int)(h * 0.25f);

    int y = m.bottom - 2;
    int remain = filled;

    auto paintSeg = [&](int segH, COLORREF col) {
        if (remain <= 0 || segH <= 0) return;
        const int use = (std::min)(remain, segH);
        RECT s{ m.left + 2, y - use, m.right - 2, y };
        HBRUSH br = CreateSolidBrush(col);
        FillRect(hdc, &s, br);
        DeleteObject(br);
        y -= use;
        remain -= use;
    };

    paintSeg(greenH, RGB(70, 180, 90));
    paintSeg(yellowH, RGB(220, 190, 50));
    paintSeg(h - greenH - yellowH, RGB(210, 70, 60));
}

static void drawEqCurve(HDC hdc, const GraphNode& n, const NodeView& view) {
    RECT curve = eqCurveRectScreen(n, view);
    fillRound(hdc, curve, RGB(48, 48, 54), RGB(70, 70, 78));

    // 0 dB line
    const int zeroY = (int)(eqDbToY(0.f, curve) + 0.5f);
    HPEN guide = CreatePen(PS_DOT, 1, RGB(90, 90, 98));
    HGDIOBJ oldPen = SelectObject(hdc, guide);
    MoveToEx(hdc, curve.left + 4, zeroY, nullptr);
    LineTo(hdc, curve.right - 4, zeroY);
    SelectObject(hdc, oldPen);
    DeleteObject(guide);

    constexpr int kPts = 64;
    POINT pts[kPts];
    for (int i = 0; i < kPts; ++i) {
        const float t = i / (float)(kPts - 1);
        const float freq = 20.f * powf(1000.f, t); // log sweep 20Hz→20k
        const float db = clampEqDb(eqResp(n, freq));
        pts[i].x = (int)(curve.left + t * (curve.right - curve.left) + 0.5f);
        pts[i].y = (int)(eqDbToY(db, curve) + 0.5f);
    }

    HPEN curvePen = CreatePen(PS_SOLID, 2, RGB(110, 190, 170));
    oldPen = SelectObject(hdc, curvePen);
    Polyline(hdc, pts, kPts);
    SelectObject(hdc, oldPen);
    DeleteObject(curvePen);

    // the little drag dots
    for (int b = 0; b < kEqBands; ++b) {
        POINT h = eqHandleScreen(n, view, b);
        HBRUSH br = CreateSolidBrush(RGB(230, 230, 234));
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(40, 40, 44));
        HGDIOBJ obr = SelectObject(hdc, br);
        HGDIOBJ open = SelectObject(hdc, pen);
        Ellipse(hdc, h.x - 6, h.y - 6, h.x + 6, h.y + 6);
        SelectObject(hdc, obr); SelectObject(hdc, open);
        DeleteObject(br); DeleteObject(pen);
    }
}

void drawNode(HDC hdc, const GraphNode& n, const NodeView& view, const NodeFonts& fonts,
              bool flowing, float meterIn, float meterOut) {
    const auto& t = nodeType(n.kind);
    RECT r = nodeRectScreen(n, view);
    fillRound(hdc, r, kNode, kNodeEdge);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, kMuted);
    if (fonts.title) SelectObject(hdc, fonts.title);
    RECT title{ r.left + 14, r.top + 10, r.right - 14, r.top + 30 };
    DrawTextW(hdc, t.title, -1, &title, DT_LEFT | DT_SINGLELINE);

    if (n.kind == NodeKind::Eq) {
        drawEqCurve(hdc, n, view);
        RECT chip = eqPresetRectScreen(n, view);
        fillRound(hdc, chip, kSelect, kNodeEdge);
        SetTextColor(hdc, kText);
        if (fonts.ui) SelectObject(hdc, fonts.ui);
        std::wstring label = nodeLabel(n);
        RECT tr = chip; InflateRect(&tr, -8, 0);
        DrawTextW(hdc, label.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    } else if (nodeHasPicker(n)) {
        RECT field = nodeFieldRectScreen(n, view);
        fillRound(hdc, field, kSelect, kNodeEdge);
        SetTextColor(hdc, kText);
        if (fonts.ui) SelectObject(hdc, fonts.ui);
        std::wstring label = nodeLabel(n);
        RECT tr = field; InflateRect(&tr, -10, 0);
        DrawTextW(hdc, label.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    if (flowing) {
        if (n.kind == NodeKind::Input)
            drawVolumeMeter(hdc, nodeMeterRectScreen(n, view), meterIn);
        else if (n.kind == NodeKind::Output)
            drawVolumeMeter(hdc, nodeMeterRectScreen(n, view), meterOut);
    } else if (n.kind == NodeKind::Output || n.kind == NodeKind::Input) {
        drawVolumeMeter(hdc, nodeMeterRectScreen(n, view), 0.f);
    }

    if (t.hasOutPort) drawPort(hdc, outPortScreen(n, view), false);
    if (t.hasInPort) drawPort(hdc, inPortScreen(n, view), false);
}

void drawGraph(HDC hdc, const NodeGraph& g, const NodeView& view, const NodeFonts& fonts,
               bool flowing, float audioLevel, float meterIn, float meterOut) {
    for (int i = 0; i < (int)g.links.size(); ++i) {
        auto& c = g.links[i];
        if (c.from < 0 || c.to < 0) continue;
        if (c.from >= (int)g.nodes.size() || c.to >= (int)g.nodes.size()) continue;
        const bool flow = flowing && linkOnRunnableRoute(g, i);
        drawWire(hdc, outPortScreen(g.nodes[c.from], view), inPortScreen(g.nodes[c.to], view), flow, audioLevel);
    }
    for (auto& n : g.nodes)
        drawNode(hdc, n, view, fonts, flowing, meterIn, meterOut);

    if (flowing) {
        for (int i = 0; i < (int)g.links.size(); ++i) {
            if (!linkOnRunnableRoute(g, i)) continue;
            auto& c = g.links[i];
            drawPort(hdc, outPortScreen(g.nodes[c.from], view), true);
            drawPort(hdc, inPortScreen(g.nodes[c.to], view), true);
        }
    }
}
