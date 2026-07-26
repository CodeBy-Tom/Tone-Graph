#include "nodes.h"
#include "audio_fx.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Colors

static constexpr COLORREF kNode     = RGB(68, 68, 74);
static constexpr COLORREF kNodeEdge = RGB(92, 92, 100);
static constexpr COLORREF kText     = RGB(220, 220, 224);
static constexpr COLORREF kMuted    = RGB(150, 150, 156);
static constexpr COLORREF kPort     = RGB(200, 200, 206);
static constexpr COLORREF kPortHot  = RGB(120, 190, 170);
static constexpr COLORREF kWire     = RGB(140, 140, 148);
static constexpr COLORREF kSelect   = RGB(58, 58, 64);
static constexpr COLORREF kAccent   = RGB(110, 190, 170);

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
static NodePickMode noPickMode() { return NodePickMode::None; }
static int noPickCount() { return 0; }
static std::wstring noPickLabel(int) { return {}; }
static bool noApplyPick(GraphNode&, int, std::wstring*) { return false; }

// INPUT

static void clearFxParams(GraphNode& n) {
    n.gainDb = 0;
    n.eqSubDb = n.eqLowDb = n.eqMidDb = n.eqHighDb = n.eqAirDb = 0;
    n.compThresholdDb = -18.f;
    n.compRatio = 4.f;
    n.compAttackMs = 10.f;
    n.compReleaseMs = 100.f;
    n.compMakeupDb = 0.f;
    n.pan = 0.f;
    n.hpHz = 0.f;
    n.lpHz = 0.f;
    n.limitThresholdDb = -1.f;
    n.limitReleaseMs = 50.f;
    n.gateThresholdDb = -40.f;
    n.gateAttackMs = 5.f;
    n.gateReleaseMs = 100.f;
    n.gateRangeDb = -60.f;
}

static void inputInit(GraphNode& n) {
    n.sel = -1; n.appName.clear(); n.appPid = 0; n.deviceId.clear();
    clearFxParams(n);
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
    clearFxParams(n);
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

static void gainInit(GraphNode& n) {
    n.sel = -1;
    n.appName.clear(); n.appPid = 0; n.deviceId.clear();
    clearFxParams(n);
    n.gainDb = 0.f;
}

static std::wstring gainLabel(const GraphNode& n) {
    wchar_t buf[48];
    if (fabsf(n.gainDb) < 0.25f) return L"Gain  0 dB";
    swprintf(buf, 48, L"Gain  %+.0f dB", n.gainDb);
    return buf;
}

static const NodeBehavior kGainBehavior = {
    gainInit, noopRebind, gainLabel, alwaysReady,
    noPickMode, noPickCount, noPickLabel, noApplyPick,
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
    n.appName.clear(); n.appPid = 0; n.deviceId.clear();
    clearFxParams(n);
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

// COMPRESSOR

static void compInit(GraphNode& n) {
    n.sel = -1;
    n.appName.clear(); n.appPid = 0; n.deviceId.clear();
    clearFxParams(n);
    n.compThresholdDb = -18.f;
    n.compRatio = 4.f;
    n.compAttackMs = 10.f;
    n.compReleaseMs = 100.f;
    n.compMakeupDb = 3.f;
}

static std::wstring compLabel(const GraphNode&) {
    return L"Compressor";
}

static const NodeBehavior kCompBehavior = {
    compInit, noopRebind, compLabel, alwaysReady,
    noPickMode, noPickCount, noPickLabel, noApplyPick,
    false, false, true
};

// PAN

static void panInit(GraphNode& n) {
    n.sel = -1;
    n.appName.clear(); n.appPid = 0; n.deviceId.clear();
    clearFxParams(n);
    n.pan = 0.f;
}

static std::wstring panLabel(const GraphNode& n) {
    const int pct = (int)(n.pan * 100.f + (n.pan >= 0.f ? 0.5f : -0.5f));
    if (pct == 0) return L"Pan  Center";
    if (pct < 0) return L"Pan  L" + std::to_wstring(-pct);
    return L"Pan  R" + std::to_wstring(pct);
}

static const NodeBehavior kPanBehavior = {
    panInit, noopRebind, panLabel, alwaysReady,
    noPickMode, noPickCount, noPickLabel, noApplyPick,
    false, false, true
};

// HIGH-PASS

static void hpInit(GraphNode& n) {
    n.sel = -1;
    n.appName.clear(); n.appPid = 0; n.deviceId.clear();
    clearFxParams(n);
    n.hpHz = 0.f;
}

static std::wstring hpLabel(const GraphNode& n) {
    if (n.hpHz <= 0.f) return L"HP  Off";
    return L"HP  " + std::to_wstring((int)lroundf(n.hpHz)) + L" Hz";
}

static const NodeBehavior kHpBehavior = {
    hpInit, noopRebind, hpLabel, alwaysReady,
    noPickMode, noPickCount, noPickLabel, noApplyPick,
    false, false, true
};

// LOW-PASS

static void lpInit(GraphNode& n) {
    n.sel = -1;
    n.appName.clear(); n.appPid = 0; n.deviceId.clear();
    clearFxParams(n);
    n.lpHz = 0.f;
}

static std::wstring lpLabel(const GraphNode& n) {
    if (n.lpHz <= 0.f) return L"LP  Off";
    return L"LP  " + std::to_wstring((int)lroundf(n.lpHz)) + L" Hz";
}

static const NodeBehavior kLpBehavior = {
    lpInit, noopRebind, lpLabel, alwaysReady,
    noPickMode, noPickCount, noPickLabel, noApplyPick,
    false, false, true
};

// LIMITER

static void limitInit(GraphNode& n) {
    n.sel = -1;
    n.appName.clear(); n.appPid = 0; n.deviceId.clear();
    clearFxParams(n);
    n.limitThresholdDb = -1.f;
    n.limitReleaseMs = 50.f;
}

static std::wstring limitLabel(const GraphNode&) {
    return L"Limiter";
}

static const NodeBehavior kLimitBehavior = {
    limitInit, noopRebind, limitLabel, alwaysReady,
    noPickMode, noPickCount, noPickLabel, noApplyPick,
    false, false, true
};

// GATE

static void gateInit(GraphNode& n) {
    n.sel = -1;
    n.appName.clear(); n.appPid = 0; n.deviceId.clear();
    clearFxParams(n);
    n.gateThresholdDb = -40.f;
    n.gateAttackMs = 5.f;
    n.gateReleaseMs = 100.f;
    n.gateRangeDb = -60.f;
}

static std::wstring gateLabel(const GraphNode&) {
    return L"Gate";
}

static const NodeBehavior kGateBehavior = {
    gateInit, noopRebind, gateLabel, alwaysReady,
    noPickMode, noPickCount, noPickLabel, noApplyPick,
    false, false, true
};

// SPLIT / MERGE (L/R routing)

static void splitInit(GraphNode& n) {
    n.sel = -1;
    n.appName.clear(); n.appPid = 0; n.deviceId.clear();
    clearFxParams(n);
}
static std::wstring splitLabel(const GraphNode&) { return L"L / R split"; }
static const NodeBehavior kSplitBehavior = {
    splitInit, noopRebind, splitLabel, alwaysReady,
    noPickMode, noPickCount, noPickLabel, noApplyPick,
    false, false, false
};

static void mergeInit(GraphNode& n) {
    n.sel = -1;
    n.appName.clear(); n.appPid = 0; n.deviceId.clear();
    clearFxParams(n);
}
static std::wstring mergeLabel(const GraphNode&) { return L"L / R merge"; }
static const NodeBehavior kMergeBehavior = {
    mergeInit, noopRebind, mergeLabel, alwaysReady,
    noPickMode, noPickCount, noPickLabel, noApplyPick,
    false, false, false
};

// WAVEFORM (pass-through analyzer)

static void waveInit(GraphNode& n) {
    n.sel = -1;
    n.appName.clear(); n.appPid = 0; n.deviceId.clear();
    clearFxParams(n);
}

static std::wstring waveLabel(const GraphNode&) {
    return L"Waveform";
}

static const NodeBehavior kWaveBehavior = {
    waveInit, noopRebind, waveLabel, alwaysReady,
    noPickMode, noPickCount, noPickLabel, noApplyPick,
    false, false, true
};

// Catalog

static const NodeTypeInfo kCatalog[] = {
    { NodeKind::Input,    L"input",    L"Input",      L"INPUT",  false, true,  &kInputBehavior  },
    { NodeKind::Output,   L"output",   L"Output",     L"OUTPUT", true,  false, &kOutputBehavior },
    { NodeKind::Gain,     L"gain",     L"Gain",       L"GAIN",   true,  true,  &kGainBehavior   },
    { NodeKind::Eq,       L"eq",       L"EQ",         L"EQ",     true,  true,  &kEqBehavior     },
    { NodeKind::Comp,     L"comp",     L"Compressor", L"COMP",   true,  true,  &kCompBehavior   },
    { NodeKind::Pan,      L"pan",      L"Pan",        L"PAN",    true,  true,  &kPanBehavior    },
    { NodeKind::HighPass, L"hipass",   L"High-pass",  L"HP",     true,  true,  &kHpBehavior     },
    { NodeKind::LowPass,  L"lopass",   L"Low-pass",   L"LP",     true,  true,  &kLpBehavior     },
    { NodeKind::Limit,    L"limit",    L"Limiter",    L"LIMIT",  true,  true,  &kLimitBehavior  },
    { NodeKind::Gate,     L"gate",     L"Gate",       L"GATE",   true,  true,  &kGateBehavior   },
    { NodeKind::Split,    L"split",    L"Split L/R",  L"SPLIT",  true,  true,  &kSplitBehavior  },
    { NodeKind::Merge,    L"merge",    L"Merge L/R",  L"MERGE",  true,  true,  &kMergeBehavior  },
    { NodeKind::Waveform, L"wave",     L"Waveform",   L"WAVE",   true,  true,  &kWaveBehavior   },
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
    switch (n.kind) {
    case NodeKind::Eq: return kEqNodeH;
    case NodeKind::Comp:
    case NodeKind::Gate: return kCompNodeH;
    case NodeKind::Waveform: return kWaveNodeH;
    case NodeKind::Split:
    case NodeKind::Merge: return kSplitNodeH;
    case NodeKind::Gain:
    case NodeKind::Pan:
    case NodeKind::HighPass:
    case NodeKind::LowPass:
    case NodeKind::Limit: return kKnobNodeH;
    default: return kNodeH;
    }
}

int nodeInPortCount(const GraphNode& n) {
    if (n.kind == NodeKind::Merge) return 2;
    return nodeType(n.kind).hasInPort ? 1 : 0;
}

int nodeOutPortCount(const GraphNode& n) {
    if (n.kind == NodeKind::Split) return 2;
    return nodeType(n.kind).hasOutPort ? 1 : 0;
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
RECT wavePlotRectScreen(const GraphNode& n, const NodeView& view) {
    RECT r = nodeRectScreen(n, view);
    return { r.left + 14, r.top + 36, r.right - 14, r.bottom - 14 };
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
static bool nodeUsesTitlePorts(const GraphNode& n) {
    return n.kind == NodeKind::Eq
        || n.kind == NodeKind::Gain
        || n.kind == NodeKind::Pan
        || n.kind == NodeKind::HighPass
        || n.kind == NodeKind::LowPass
        || n.kind == NodeKind::Comp
        || n.kind == NodeKind::Limit
        || n.kind == NodeKind::Gate
        || n.kind == NodeKind::Waveform
        || n.kind == NodeKind::Split
        || n.kind == NodeKind::Merge;
}

POINT outPortWorld(const GraphNode& n, int port) {
    const int h = nodeHeight(n);
    int cy;
    if (n.kind == NodeKind::Split) {
        cy = (port == 0) ? (h / 3) : (2 * h / 3);
    } else if (nodeUsesTitlePorts(n) && n.kind != NodeKind::Merge) {
        cy = 18;
    } else {
        cy = h / 2;
    }
    return { n.x + kNodeW, n.y + cy };
}
POINT inPortWorld(const GraphNode& n, int port) {
    const int h = nodeHeight(n);
    int cy;
    if (n.kind == NodeKind::Merge) {
        cy = (port == 0) ? (h / 3) : (2 * h / 3);
    } else if (nodeUsesTitlePorts(n) && n.kind != NodeKind::Split) {
        cy = 18;
    } else if (n.kind == NodeKind::Split) {
        cy = h / 2;
    } else {
        cy = h / 2;
    }
    return { n.x, n.y + cy };
}
POINT outPortScreen(const GraphNode& n, const NodeView& view, int port) {
    return worldToScreen(outPortWorld(n, port), view);
}
POINT inPortScreen(const GraphNode& n, const NodeView& view, int port) {
    return worldToScreen(inPortWorld(n, port), view);
}

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

// Knobs — vertical drag, 270° dial

enum class KnobScale { Linear, Log };

struct KnobSpec {
    const wchar_t* name;
    KnobScale scale;
    float minV;
    float maxV;
};

static const KnobSpec kGainKnobs[] = {
    { L"Gain", KnobScale::Linear, -12.f, 12.f },
};
static const KnobSpec kPanKnobs[] = {
    { L"Pan", KnobScale::Linear, -1.f, 1.f },
};
static const KnobSpec kHpKnobs[] = {
    { L"Freq", KnobScale::Linear, 0.f, 200.f },
};
static const KnobSpec kLpKnobs[] = {
    { L"Freq", KnobScale::Linear, 0.f, 16000.f },
};
static const KnobSpec kCompKnobs[] = {
    { L"Thr", KnobScale::Linear, -40.f, 0.f },
    { L"Rat", KnobScale::Log, 1.f, 20.f },
    { L"Atk", KnobScale::Log, 0.5f, 50.f },
    { L"Rel", KnobScale::Log, 20.f, 500.f },
    { L"Mk",  KnobScale::Linear, 0.f, 12.f },
};
static const KnobSpec kLimitKnobs[] = {
    { L"Thr", KnobScale::Linear, -24.f, 0.f },
    { L"Rel", KnobScale::Log, 10.f, 500.f },
};
static const KnobSpec kGateKnobs[] = {
    { L"Thr", KnobScale::Linear, -60.f, -10.f },
    { L"Atk", KnobScale::Log, 0.5f, 50.f },
    { L"Rel", KnobScale::Log, 20.f, 500.f },
    { L"Rng", KnobScale::Linear, -80.f, -6.f },
};

static const KnobSpec* knobSpecs(NodeKind kind, int* outCount) {
    switch (kind) {
    case NodeKind::Gain:
        if (outCount) *outCount = 1;
        return kGainKnobs;
    case NodeKind::Pan:
        if (outCount) *outCount = 1;
        return kPanKnobs;
    case NodeKind::HighPass:
        if (outCount) *outCount = 1;
        return kHpKnobs;
    case NodeKind::LowPass:
        if (outCount) *outCount = 1;
        return kLpKnobs;
    case NodeKind::Comp:
        if (outCount) *outCount = 5;
        return kCompKnobs;
    case NodeKind::Limit:
        if (outCount) *outCount = 2;
        return kLimitKnobs;
    case NodeKind::Gate:
        if (outCount) *outCount = 4;
        return kGateKnobs;
    default:
        if (outCount) *outCount = 0;
        return nullptr;
    }
}

static float* knobValuePtr(GraphNode& n, int knob) {
    switch (n.kind) {
    case NodeKind::Gain: return &n.gainDb;
    case NodeKind::Pan: return &n.pan;
    case NodeKind::HighPass: return &n.hpHz;
    case NodeKind::LowPass: return &n.lpHz;
    case NodeKind::Comp:
        switch (knob) {
        case 0: return &n.compThresholdDb;
        case 1: return &n.compRatio;
        case 2: return &n.compAttackMs;
        case 3: return &n.compReleaseMs;
        case 4: return &n.compMakeupDb;
        default: return nullptr;
        }
    case NodeKind::Limit:
        switch (knob) {
        case 0: return &n.limitThresholdDb;
        case 1: return &n.limitReleaseMs;
        default: return nullptr;
        }
    case NodeKind::Gate:
        switch (knob) {
        case 0: return &n.gateThresholdDb;
        case 1: return &n.gateAttackMs;
        case 2: return &n.gateReleaseMs;
        case 3: return &n.gateRangeDb;
        default: return nullptr;
        }
    default: return nullptr;
    }
}

static float knobValueOf(const GraphNode& n, int knob) {
    GraphNode tmp = n;
    float* p = knobValuePtr(tmp, knob);
    return p ? *p : 0.f;
}

static float clamp01(float t) {
    if (t < 0.f) return 0.f;
    if (t > 1.f) return 1.f;
    return t;
}

static float valueToNorm(float v, const KnobSpec& s) {
    if (s.scale == KnobScale::Log) {
        const float a = logf((std::max)(s.minV, 1e-4f));
        const float b = logf((std::max)(s.maxV, 1e-4f));
        v = (std::max)(s.minV, (std::min)(s.maxV, v));
        return clamp01((logf(v) - a) / (b - a));
    }
    if (fabsf(s.maxV - s.minV) < 1e-6f) return 0.f;
    return clamp01((v - s.minV) / (s.maxV - s.minV));
}

static float normToValue(float t, const KnobSpec& s) {
    t = clamp01(t);
    if (s.scale == KnobScale::Log) {
        const float a = logf((std::max)(s.minV, 1e-4f));
        const float b = logf((std::max)(s.maxV, 1e-4f));
        return expf(a + t * (b - a));
    }
    return s.minV + t * (s.maxV - s.minV);
}

int nodeKnobCount(const GraphNode& n) {
    int c = 0;
    knobSpecs(n.kind, &c);
    return c;
}

RECT nodeKnobRectScreen(const GraphNode& n, const NodeView& view, int knob) {
    RECT r = nodeRectScreen(n, view);
    const int count = nodeKnobCount(n);
    if (count <= 0 || knob < 0 || knob >= count) return { 0, 0, 0, 0 };

    // content box inside the node (title above, caption strip below)
    const int contentL = r.left + 14;
    const int contentR = r.right - 14;
    const int contentT = r.top + 36;
    const int contentB = r.bottom - 20;
    const int contentW = contentR - contentL;
    const int cellW = contentW / count;
    const int originX = contentL + (contentW - cellW * count) / 2;

    // odd diameter so the circle has a true center pixel
    const int diam = (count == 1) ? 45 : 31;
    const int rad = diam / 2; // 22 or 15
    const int cx = originX + knob * cellW + cellW / 2;
    const int cy = contentT + (contentB - contentT) / 2;
    // store as inclusive pixel bounds (width = diam = 2*rad+1)
    return { cx - rad, cy - rad, cx + rad, cy + rad };
}

int hitKnobAt(const NodeGraph& g, const NodeView& view, int sx, int sy, int* outKnob) {
    if (outKnob) *outKnob = -1;
    for (int i = (int)g.nodes.size() - 1; i >= 0; --i) {
        const int count = nodeKnobCount(g.nodes[i]);
        for (int k = 0; k < count; ++k) {
            RECT kr = nodeKnobRectScreen(g.nodes[i], view, k);
            const int diam = kr.right - kr.left + 1;
            const int rad = diam / 2;
            const int cx = kr.left + rad;
            const int cy = kr.top + rad;
            const int hitR = rad + 6;
            const int dx = sx - cx, dy = sy - cy;
            if (dx * dx + dy * dy <= hitR * hitR) {
                if (outKnob) *outKnob = k;
                return i;
            }
        }
    }
    return -1;
}

float knobNorm(const GraphNode& n, int knob) {
    int count = 0;
    const KnobSpec* specs = knobSpecs(n.kind, &count);
    if (!specs || knob < 0 || knob >= count) return 0.f;
    return valueToNorm(knobValueOf(n, knob), specs[knob]);
}

void applyKnobNorm(GraphNode& n, int knob, float norm) {
    int count = 0;
    const KnobSpec* specs = knobSpecs(n.kind, &count);
    float* p = knobValuePtr(n, knob);
    if (!specs || !p || knob < 0 || knob >= count) return;
    float v = normToValue(norm, specs[knob]);
    if (n.kind == NodeKind::HighPass && v < 8.f) v = 0.f; // snap near-zero to Off
    if (n.kind == NodeKind::LowPass && v < 500.f) v = 0.f;
    if (n.kind == NodeKind::Gain) v = roundf(v * 2.f) / 2.f; // 0.5 dB steps
    if (n.kind == NodeKind::Pan) v = roundf(v * 100.f) / 100.f;
    *p = v;
}

std::wstring knobCaption(const GraphNode& n, int knob) {
    int count = 0;
    const KnobSpec* specs = knobSpecs(n.kind, &count);
    if (!specs || knob < 0 || knob >= count) return {};
    return specs[knob].name;
}

std::wstring knobValueText(const GraphNode& n, int knob) {
    wchar_t buf[48];
    switch (n.kind) {
    case NodeKind::Gain:
        if (fabsf(n.gainDb) < 0.25f) return L"0 dB";
        swprintf(buf, 48, L"%+.0f dB", n.gainDb);
        return buf;
    case NodeKind::Pan: {
        const int pct = (int)(n.pan * 100.f + (n.pan >= 0.f ? 0.5f : -0.5f));
        if (pct == 0) return L"Center";
        if (pct < 0) { swprintf(buf, 48, L"L%d", -pct); return buf; }
        swprintf(buf, 48, L"R%d", pct);
        return buf;
    }
    case NodeKind::HighPass:
        if (n.hpHz <= 0.f) return L"Off";
        swprintf(buf, 48, L"%d Hz", (int)lroundf(n.hpHz));
        return buf;
    case NodeKind::LowPass:
        if (n.lpHz <= 0.f) return L"Off";
        if (n.lpHz >= 1000.f)
            swprintf(buf, 48, L"%.1fk", n.lpHz / 1000.f);
        else
            swprintf(buf, 48, L"%d Hz", (int)lroundf(n.lpHz));
        return buf;
    case NodeKind::Comp:
        switch (knob) {
        case 0:
            if (fabsf(n.compThresholdDb) < 0.5f) return L"0";
            swprintf(buf, 48, L"%.0f", n.compThresholdDb);
            return buf;
        case 1: swprintf(buf, 48, L"%.1f:1", n.compRatio); return buf;
        case 2: swprintf(buf, 48, L"%.0fms", n.compAttackMs); return buf;
        case 3: swprintf(buf, 48, L"%.0fms", n.compReleaseMs); return buf;
        case 4:
            if (fabsf(n.compMakeupDb) < 0.25f) return L"0";
            swprintf(buf, 48, L"%+.0f", n.compMakeupDb);
            return buf;
        default: break;
        }
        break;
    case NodeKind::Limit:
        switch (knob) {
        case 0:
            if (fabsf(n.limitThresholdDb) < 0.25f) return L"0";
            swprintf(buf, 48, L"%.0f", n.limitThresholdDb);
            return buf;
        case 1: swprintf(buf, 48, L"%.0fms", n.limitReleaseMs); return buf;
        default: break;
        }
        break;
    case NodeKind::Gate:
        switch (knob) {
        case 0: swprintf(buf, 48, L"%.0f", n.gateThresholdDb); return buf;
        case 1: swprintf(buf, 48, L"%.0fms", n.gateAttackMs); return buf;
        case 2: swprintf(buf, 48, L"%.0fms", n.gateReleaseMs); return buf;
        case 3: swprintf(buf, 48, L"%.0f", n.gateRangeDb); return buf;
        default: break;
        }
        break;
    default: break;
    }
    return {};
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

bool addLink(NodeGraph& g, int from, int to, int fromPort, int toPort) {
    if (from < 0 || to < 0) return false;
    if (from >= (int)g.nodes.size() || to >= (int)g.nodes.size()) return false;
    if (from == to) return false;
    if (fromPort < 0 || fromPort >= nodeOutPortCount(g.nodes[from])) return false;
    if (toPort < 0 || toPort >= nodeInPortCount(g.nodes[to])) return false;
    for (auto& c : g.links)
        if (c.from == from && c.to == to && c.fromPort == fromPort && c.toPort == toPort) return false;
    // one wire per destination port (Merge L/R each take one)
    for (auto& c : g.links)
        if (c.to == to && c.toPort == toPort) return false;
    g.links.push_back({ from, to, fromPort, toPort });
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

static void collectOutgoing(const NodeGraph& g, int from, int fromPort, std::vector<Connection>& outs) {
    outs.clear();
    for (auto& c : g.links)
        if (c.from == from && c.fromPort == fromPort) outs.push_back(c);
}

static bool walkBranchToMerge(const NodeGraph& g, const Connection& first,
                              int expectedMergePort, int& outMerge, std::vector<int>& effects) {
    effects.clear();
    outMerge = -1;
    int cur = first.to;
    int inPort = first.toPort;
    std::set<int> visiting;
    while (cur >= 0 && cur < (int)g.nodes.size()) {
        if (visiting.count(cur)) return false;
        visiting.insert(cur);
        const auto kind = g.nodes[cur].kind;
        if (kind == NodeKind::Merge) {
            if (inPort != expectedMergePort) return false;
            outMerge = cur;
            return true;
        }
        if (kind == NodeKind::Split || nodeBehavior(kind).isAudioSink || nodeBehavior(kind).isAudioSource)
            return false;
        if (nodeBehavior(kind).isEffect)
            effects.push_back(cur);

        std::vector<Connection> outs;
        collectOutgoing(g, cur, 0, outs);
        if (outs.size() != 1) return false;
        inPort = outs[0].toPort;
        cur = outs[0].to;
    }
    return false;
}

static bool walkPostMerge(const NodeGraph& g, int merge, int& sink, std::vector<int>& post) {
    post.clear();
    sink = -1;
    std::vector<Connection> outs;
    collectOutgoing(g, merge, 0, outs);
    if (outs.size() != 1) return false;
    int cur = outs[0].to;
    std::set<int> visiting;
    while (cur >= 0 && cur < (int)g.nodes.size()) {
        if (visiting.count(cur)) return false;
        visiting.insert(cur);
        const auto& beh = nodeBehavior(g.nodes[cur].kind);
        if (beh.isAudioSink) {
            sink = cur;
            return true;
        }
        if (g.nodes[cur].kind == NodeKind::Split || g.nodes[cur].kind == NodeKind::Merge)
            return false;
        if (beh.isEffect)
            post.push_back(cur);
        collectOutgoing(g, cur, 0, outs);
        if (outs.size() != 1) return false;
        cur = outs[0].to;
    }
    return false;
}

static void walkRoutes(const NodeGraph& g, int cur, AudioRoute curRoute,
                       std::vector<AudioRoute>& out, std::set<int>& visiting) {
    if (visiting.count(cur)) return;
    visiting.insert(cur);

    const auto kind = g.nodes[cur].kind;
    const auto& beh = nodeBehavior(kind);

    if (beh.isAudioSink) {
        curRoute.sink = cur;
        if (curRoute.source >= 0) out.push_back(curRoute);
        visiting.erase(cur);
        return;
    }

    if (kind == NodeKind::Split) {
        // diamond: Split → L branch + R branch → same Merge → Output
        AudioRoute r = curRoute;
        r.hasSplit = true;
        r.splitNode = cur;
        r.preEffects = curRoute.effects;
        r.effects.clear();

        std::vector<Connection> lOuts, rOuts;
        collectOutgoing(g, cur, 0, lOuts);
        collectOutgoing(g, cur, 1, rOuts);
        if (lOuts.size() == 1 && rOuts.size() == 1) {
            int mergeL = -1, mergeR = -1;
            if (walkBranchToMerge(g, lOuts[0], 0, mergeL, r.leftEffects)
                && walkBranchToMerge(g, rOuts[0], 1, mergeR, r.rightEffects)
                && mergeL >= 0 && mergeL == mergeR) {
                r.mergeNode = mergeL;
                int sink = -1;
                if (walkPostMerge(g, mergeL, sink, r.postEffects) && sink >= 0) {
                    r.sink = sink;
                    out.push_back(r);
                }
            }
        }
        visiting.erase(cur);
        return;
    }

    if (kind == NodeKind::Merge) {
        // Merge without matching Split walk — ignore (handled via Split)
        visiting.erase(cur);
        return;
    }

    if (beh.isEffect)
        curRoute.effects.push_back(cur);

    std::vector<Connection> next;
    collectOutgoing(g, cur, 0, next);
    for (auto& c : next) {
        if (c.to < 0 || c.to >= (int)g.nodes.size()) continue;
        walkRoutes(g, c.to, curRoute, out, visiting);
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
        std::vector<Connection> next;
        collectOutgoing(g, i, 0, next);
        for (auto& c : next) {
            if (c.to < 0 || c.to >= (int)g.nodes.size()) continue;
            walkRoutes(g, c.to, seed, routes, visiting);
        }
    }
    return routes;
}

bool routeRunnable(const NodeGraph& g, const AudioRoute& route) {
    if (route.source < 0 || route.sink < 0) return false;
    if (route.source >= (int)g.nodes.size() || route.sink >= (int)g.nodes.size()) return false;
    if (!nodeCanRun(g.nodes[route.source])) return false;
    if (!nodeCanRun(g.nodes[route.sink])) return false;

    auto checkList = [&](const std::vector<int>& list) {
        for (int ei : list) {
            if (ei < 0 || ei >= (int)g.nodes.size()) return false;
            if (!nodeCanRun(g.nodes[ei])) return false;
        }
        return true;
    };

    if (route.hasSplit) {
        if (route.splitNode < 0 || route.mergeNode < 0) return false;
        if (!checkList(route.preEffects)) return false;
        if (!checkList(route.leftEffects)) return false;
        if (!checkList(route.rightEffects)) return false;
        if (!checkList(route.postEffects)) return false;
        return true;
    }
    return checkList(route.effects);
}

static FxStep stepFromNode(const GraphNode& n, FxChannel channel = FxChannel::Both) {
    FxStep s;
    s.channel = channel;
    switch (n.kind) {
    case NodeKind::Gain:
        s.kind = FxKind::Gain;
        s.gainDb = n.gainDb;
        break;
    case NodeKind::Eq:
        s.kind = FxKind::Eq;
        s.eqSubDb = n.eqSubDb;
        s.eqLowDb = n.eqLowDb;
        s.eqMidDb = n.eqMidDb;
        s.eqHighDb = n.eqHighDb;
        s.eqAirDb = n.eqAirDb;
        break;
    case NodeKind::Comp:
        s.kind = FxKind::Comp;
        s.compThresholdDb = n.compThresholdDb;
        s.compRatio = n.compRatio;
        s.compAttackMs = n.compAttackMs;
        s.compReleaseMs = n.compReleaseMs;
        s.compMakeupDb = n.compMakeupDb;
        break;
    case NodeKind::Pan:
        s.kind = FxKind::Pan;
        s.pan = n.pan;
        break;
    case NodeKind::HighPass:
        s.kind = FxKind::HighPass;
        s.hpHz = n.hpHz;
        break;
    case NodeKind::LowPass:
        s.kind = FxKind::LowPass;
        s.lpHz = n.lpHz;
        break;
    case NodeKind::Limit:
        s.kind = FxKind::Limit;
        s.limitThresholdDb = n.limitThresholdDb;
        s.limitReleaseMs = n.limitReleaseMs;
        break;
    case NodeKind::Gate:
        s.kind = FxKind::Gate;
        s.gateThresholdDb = n.gateThresholdDb;
        s.gateAttackMs = n.gateAttackMs;
        s.gateReleaseMs = n.gateReleaseMs;
        s.gateRangeDb = n.gateRangeDb;
        break;
    case NodeKind::Waveform:
        s.kind = FxKind::Waveform;
        break;
    default:
        s.kind = FxKind::Gain;
        s.gainDb = 0.f;
        break;
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

    auto pushList = [&](const std::vector<int>& list, FxChannel ch) {
        for (int ei : list)
            outJob.fx->steps.push_back(stepFromNode(g.nodes[ei], ch));
    };

    if (route.hasSplit) {
        pushList(route.preEffects, FxChannel::Both);
        pushList(route.leftEffects, FxChannel::Left);
        pushList(route.rightEffects, FxChannel::Right);
        pushList(route.postEffects, FxChannel::Both);
    } else {
        pushList(route.effects, FxChannel::Both);
    }
    return true;
}

void syncLiveFxFromGraph(const NodeGraph& g) {
    std::lock_guard lock(g_liveFxMu);
    for (auto& b : g_liveFx) {
        if (!b.fx) continue;
        // Rebuild from the live route topology stored as effectNodes:
        // convention: nodes listed in process order; channel inferred by re-finding routes.
        // Simpler: re-find matching route by source/sink in effectNodes path.
        std::vector<FxStep> steps;
        for (auto& r : findAudioRoutes(g)) {
            if (!routeRunnable(g, r)) continue;
            auto matches = [&]() {
                // binding lists every effect node on that route
                std::vector<int> want;
                if (r.hasSplit) {
                    want.insert(want.end(), r.preEffects.begin(), r.preEffects.end());
                    want.insert(want.end(), r.leftEffects.begin(), r.leftEffects.end());
                    want.insert(want.end(), r.rightEffects.begin(), r.rightEffects.end());
                    want.insert(want.end(), r.postEffects.begin(), r.postEffects.end());
                } else {
                    want = r.effects;
                }
                if (want.size() != b.effectNodes.size()) return false;
                for (size_t i = 0; i < want.size(); ++i)
                    if (want[i] != b.effectNodes[i]) return false;
                return true;
            };
            if (!matches()) continue;
            auto pushList = [&](const std::vector<int>& list, FxChannel ch) {
                for (int ei : list)
                    steps.push_back(stepFromNode(g.nodes[ei], ch));
            };
            if (r.hasSplit) {
                pushList(r.preEffects, FxChannel::Both);
                pushList(r.leftEffects, FxChannel::Left);
                pushList(r.rightEffects, FxChannel::Right);
                pushList(r.postEffects, FxChannel::Both);
            } else {
                pushList(r.effects, FxChannel::Both);
            }
            break;
        }
        if (steps.empty()) {
            // fallback: both-channel steps in listed order
            for (int ei : b.effectNodes) {
                if (ei < 0 || ei >= (int)g.nodes.size()) continue;
                if (!nodeBehavior(g.nodes[ei].kind).isEffect) continue;
                steps.push_back(stepFromNode(g.nodes[ei]));
            }
        }
        b.fx->setSteps(std::move(steps));
    }
}

bool linkOnRunnableRoute(const NodeGraph& g, int linkIdx) {
    if (linkIdx < 0 || linkIdx >= (int)g.links.size()) return false;
    auto& link = g.links[linkIdx];
    for (auto& r : findAudioRoutes(g)) {
        if (!routeRunnable(g, r)) continue;
        std::vector<int> path;
        path.push_back(r.source);
        if (r.hasSplit) {
            path.insert(path.end(), r.preEffects.begin(), r.preEffects.end());
            path.push_back(r.splitNode);
            // L branch
            std::vector<int> lpath = path;
            lpath.insert(lpath.end(), r.leftEffects.begin(), r.leftEffects.end());
            lpath.push_back(r.mergeNode);
            lpath.insert(lpath.end(), r.postEffects.begin(), r.postEffects.end());
            lpath.push_back(r.sink);
            for (size_t i = 0; i + 1 < lpath.size(); ++i)
                if (link.from == lpath[i] && link.to == lpath[i + 1]) return true;
            // R branch
            std::vector<int> rpath = path;
            rpath.insert(rpath.end(), r.rightEffects.begin(), r.rightEffects.end());
            rpath.push_back(r.mergeNode);
            rpath.insert(rpath.end(), r.postEffects.begin(), r.postEffects.end());
            rpath.push_back(r.sink);
            for (size_t i = 0; i + 1 < rpath.size(); ++i)
                if (link.from == rpath[i] && link.to == rpath[i + 1]) return true;
        } else {
            path.insert(path.end(), r.effects.begin(), r.effects.end());
            path.push_back(r.sink);
            for (size_t i = 0; i + 1 < path.size(); ++i)
                if (link.from == path[i] && link.to == path[i + 1]) return true;
        }
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
        POINT a = outPortScreen(g.nodes[c.from], view, c.fromPort);
        POINT b = inPortScreen(g.nodes[c.to], view, c.toPort);
        POINT c1, c2; bezierControls(a, b, c1, c2);
        for (int s = 0; s <= 24; ++s) {
            float d = dist2(bezierAt(a, c1, c2, b, s / 24.f), cur);
            if (d < bestD) { bestD = d; best = i; }
        }
    }
    return best;
}

int hitOutPortAt(const NodeGraph& g, const NodeView& view, int sx, int sy, int* outPort) {
    if (outPort) *outPort = 0;
    for (int i = (int)g.nodes.size() - 1; i >= 0; --i) {
        const int nPorts = nodeOutPortCount(g.nodes[i]);
        for (int p = 0; p < nPorts; ++p) {
            if (hitPortAt(outPortScreen(g.nodes[i], view, p), sx, sy)) {
                if (outPort) *outPort = p;
                return i;
            }
        }
    }
    return -1;
}

int hitInPortAt(const NodeGraph& g, const NodeView& view, int sx, int sy, int* outPort) {
    if (outPort) *outPort = 0;
    for (int i = (int)g.nodes.size() - 1; i >= 0; --i) {
        const int nPorts = nodeInPortCount(g.nodes[i]);
        for (int p = 0; p < nPorts; ++p) {
            if (hitPortAt(inPortScreen(g.nodes[i], view, p), sx, sy)) {
                if (outPort) *outPort = p;
                return i;
            }
        }
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

static void drawKnob(HDC hdc, const RECT& kr, float norm, const wchar_t* caption,
                     const wchar_t* value, HFONT font) {
    // kr is inclusive bounds with odd width/height → true center pixel
    const int diam = kr.right - kr.left + 1;
    const int rad = diam / 2;
    const int cx = kr.left + rad;
    const int cy = kr.top + rad;

    HBRUSH face = CreateSolidBrush(RGB(52, 52, 58));
    HPEN rim = CreatePen(PS_SOLID, 1, RGB(110, 110, 118));
    HGDIOBJ obr = SelectObject(hdc, face);
    HGDIOBJ open = SelectObject(hdc, rim);
    // exclusive right/bottom = inclusive + 1
    Ellipse(hdc, kr.left, kr.top, kr.right + 1, kr.bottom + 1);
    SelectObject(hdc, obr); SelectObject(hdc, open);
    DeleteObject(face); DeleteObject(rim);

    // 270° sweep from 7:30 to 4:30 — tip sits on the rim, hub at exact center
    const float t = clamp01(norm);
    const float angle = (float)M_PI * 0.75f + t * (float)M_PI * 1.5f;
    const float tipLen = (float)(rad - 5);
    const int ix = cx + (int)lroundf(cosf(angle) * tipLen);
    const int iy = cy + (int)lroundf(sinf(angle) * tipLen);

    HPEN needle = CreatePen(PS_SOLID, 2, RGB(120, 200, 180));
    open = SelectObject(hdc, needle);
    MoveToEx(hdc, cx, cy, nullptr);
    LineTo(hdc, ix, iy);
    SelectObject(hdc, open);
    DeleteObject(needle);

    // 7×7 hub, odd size, same center pixel
    HBRUSH hub = CreateSolidBrush(RGB(200, 200, 206));
    obr = SelectObject(hdc, hub);
    open = SelectObject(hdc, GetStockObject(NULL_PEN));
    Ellipse(hdc, cx - 3, cy - 3, cx + 4, cy + 4);
    SelectObject(hdc, obr); SelectObject(hdc, open);
    DeleteObject(hub);

    SetBkMode(hdc, TRANSPARENT);
    if (font) SelectObject(hdc, font);
    SetTextColor(hdc, kMuted);
    RECT cap{ cx - 28, kr.bottom + 3, cx + 29, kr.bottom + 18 };
    DrawTextW(hdc, caption, -1, &cap, DT_CENTER | DT_SINGLELINE | DT_TOP);

    SetTextColor(hdc, kText);
    RECT val{ cx - 36, kr.top - 16, cx + 37, kr.top - 1 };
    DrawTextW(hdc, value, -1, &val, DT_CENTER | DT_SINGLELINE | DT_BOTTOM);
}

bool copyNodeWaveform(const NodeGraph& g, int nodeIdx, float* out, int count) {
    if (!out || count <= 0 || nodeIdx < 0 || nodeIdx >= (int)g.nodes.size()) return false;
    if (g.nodes[nodeIdx].kind != NodeKind::Waveform) return false;

    std::lock_guard liveLock(g_liveFxMu);
    for (auto& b : g_liveFx) {
        if (!b.fx) continue;
        int waveOrd = -1;
        int waveI = 0;
        for (int ei : b.effectNodes) {
            if (ei < 0 || ei >= (int)g.nodes.size()) continue;
            if (!nodeBehavior(g.nodes[ei].kind).isEffect) continue;
            if (g.nodes[ei].kind != NodeKind::Waveform) continue;
            if (ei == nodeIdx) waveOrd = waveI;
            ++waveI;
        }
        if (waveOrd < 0) continue;

        std::lock_guard fxLock(b.fx->mu);
        if (waveOrd >= (int)b.fx->waveStates.size()) continue;
        const auto& st = b.fx->waveStates[waveOrd];
        for (int i = 0; i < count; ++i) {
            const int src = (i * (kWaveCapture - 1)) / (std::max)(1, count - 1);
            out[i] = st.samples[src];
        }
        return true;
    }
    return false;
}

static void drawWavePlot(HDC hdc, const NodeGraph& g, int nodeIdx, const NodeView& view) {
    if (nodeIdx < 0 || nodeIdx >= (int)g.nodes.size()) return;
    const GraphNode& n = g.nodes[nodeIdx];
    RECT plot = wavePlotRectScreen(n, view);
    fillRound(hdc, plot, RGB(48, 48, 54), RGB(70, 70, 78));

    const int midY = (plot.top + plot.bottom) / 2;
    HPEN guide = CreatePen(PS_DOT, 1, RGB(90, 90, 98));
    HGDIOBJ oldPen = SelectObject(hdc, guide);
    MoveToEx(hdc, plot.left + 4, midY, nullptr);
    LineTo(hdc, plot.right - 4, midY);
    SelectObject(hdc, oldPen);
    DeleteObject(guide);

    float samples[kWavePlotSamples]{};
    copyNodeWaveform(g, nodeIdx, samples, kWavePlotSamples);

    const int w = (std::max)(1, (int)(plot.right - plot.left));
    const int h = (std::max)(1, (int)(plot.bottom - plot.top));
    const float amp = h * 0.40f;
    POINT pts[kWavePlotSamples];
    for (int i = 0; i < kWavePlotSamples; ++i) {
        float v = samples[i];
        if (v > 1.f) v = 1.f;
        if (v < -1.f) v = -1.f;
        pts[i].x = plot.left + (int)((i / (float)(kWavePlotSamples - 1)) * w + 0.5f);
        pts[i].y = midY - (int)(v * amp + 0.5f);
    }

    HPEN wavePen = CreatePen(PS_SOLID, 2, RGB(110, 190, 170));
    oldPen = SelectObject(hdc, wavePen);
    Polyline(hdc, pts, kWavePlotSamples);
    SelectObject(hdc, oldPen);
    DeleteObject(wavePen);
}

void drawNode(HDC hdc, const GraphNode& n, const NodeView& view, const NodeFonts& fonts,
              bool flowing, float meterIn, float meterOut, const NodeGraph* graph, int nodeIdx) {
    const auto& t = nodeType(n.kind);
    RECT r = nodeRectScreen(n, view);
    fillRound(hdc, r, kNode, kNodeEdge);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, kMuted);
    if (fonts.title) SelectObject(hdc, fonts.title);
    RECT title{ r.left + 14, r.top + 10, r.right - 14, r.top + 30 };
    DrawTextW(hdc, t.title, -1, &title, DT_LEFT | DT_SINGLELINE);

    // port labels for Split / Merge
    if (n.kind == NodeKind::Split || n.kind == NodeKind::Merge) {
        SetTextColor(hdc, kMuted);
        if (fonts.ui) SelectObject(hdc, fonts.ui);
        if (n.kind == NodeKind::Split) {
            POINT p0 = outPortScreen(n, view, 0);
            POINT p1 = outPortScreen(n, view, 1);
            RECT l0{ p0.x - 28, p0.y - 8, p0.x - 10, p0.y + 8 };
            RECT l1{ p1.x - 28, p1.y - 8, p1.x - 10, p1.y + 8 };
            DrawTextW(hdc, L"L", -1, &l0, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            DrawTextW(hdc, L"R", -1, &l1, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            RECT body{ r.left + 14, r.top + 40, r.right - 14, r.bottom - 14 };
            SetTextColor(hdc, kText);
            DrawTextW(hdc, L"Split stereo \u2192 L / R", -1, &body,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        } else {
            POINT p0 = inPortScreen(n, view, 0);
            POINT p1 = inPortScreen(n, view, 1);
            RECT l0{ p0.x + 10, p0.y - 8, p0.x + 28, p0.y + 8 };
            RECT l1{ p1.x + 10, p1.y - 8, p1.x + 28, p1.y + 8 };
            DrawTextW(hdc, L"L", -1, &l0, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            DrawTextW(hdc, L"R", -1, &l1, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            RECT body{ r.left + 36, r.top + 40, r.right - 14, r.bottom - 14 };
            SetTextColor(hdc, kText);
            DrawTextW(hdc, L"Merge L / R \u2192 stereo", -1, &body,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    } else if (n.kind == NodeKind::Eq) {
        drawEqCurve(hdc, n, view);
        RECT chip = eqPresetRectScreen(n, view);
        fillRound(hdc, chip, kSelect, kNodeEdge);
        SetTextColor(hdc, kText);
        if (fonts.ui) SelectObject(hdc, fonts.ui);
        std::wstring label = nodeLabel(n);
        RECT tr = chip; InflateRect(&tr, -8, 0);
        DrawTextW(hdc, label.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    } else if (n.kind == NodeKind::Waveform && graph) {
        drawWavePlot(hdc, *graph, nodeIdx, view);
    } else if (nodeKnobCount(n) > 0) {
        if (fonts.ui) SelectObject(hdc, fonts.ui);
        const int count = nodeKnobCount(n);
        for (int k = 0; k < count; ++k) {
            RECT kr = nodeKnobRectScreen(n, view, k);
            std::wstring cap = knobCaption(n, k);
            std::wstring val = knobValueText(n, k);
            drawKnob(hdc, kr, knobNorm(n, k), cap.c_str(), val.c_str(), fonts.ui);
        }
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

    for (int p = 0; p < nodeOutPortCount(n); ++p)
        drawPort(hdc, outPortScreen(n, view, p), false);
    for (int p = 0; p < nodeInPortCount(n); ++p)
        drawPort(hdc, inPortScreen(n, view, p), false);
}

void drawGraph(HDC hdc, const NodeGraph& g, const NodeView& view, const NodeFonts& fonts,
               bool flowing, float audioLevel, float meterIn, float meterOut) {
    for (int i = 0; i < (int)g.links.size(); ++i) {
        auto& c = g.links[i];
        if (c.from < 0 || c.to < 0) continue;
        if (c.from >= (int)g.nodes.size() || c.to >= (int)g.nodes.size()) continue;
        const bool flow = flowing && linkOnRunnableRoute(g, i);
        drawWire(hdc, outPortScreen(g.nodes[c.from], view, c.fromPort),
                 inPortScreen(g.nodes[c.to], view, c.toPort), flow, audioLevel);
    }
    for (int i = 0; i < (int)g.nodes.size(); ++i)
        drawNode(hdc, g.nodes[i], view, fonts, flowing, meterIn, meterOut, &g, i);

    if (flowing) {
        for (int i = 0; i < (int)g.links.size(); ++i) {
            if (!linkOnRunnableRoute(g, i)) continue;
            auto& c = g.links[i];
            drawPort(hdc, outPortScreen(g.nodes[c.from], view, c.fromPort), true);
            drawPort(hdc, inPortScreen(g.nodes[c.to], view, c.toPort), true);
        }
    }
}
