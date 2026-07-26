#pragma once

#include "app_state.h"
#include <string>
#include <vector>

enum class NodeKind {
    Input = 0,
    Output = 1,
    Gain = 2,
    Eq = 3,
    Comp = 4,
    Pan = 5,
    HighPass = 6,
    Waveform = 7,
    LowPass = 8,
    Limit = 9,
    Gate = 10,
};

enum class NodePickMode {
    None,
    SourceList,
    OutputList,
    OptionList, // gain / eq menus
};

struct GraphNode {
    NodeKind kind = NodeKind::Input;
    int x = 80, y = 120;
    int sel = -1;

    std::wstring appName;
    DWORD appPid = 0;
    std::wstring deviceId;

    float gainDb = 0.f;
    float eqSubDb = 0.f;   // left end
    float eqLowDb = 0.f;
    float eqMidDb = 0.f;
    float eqHighDb = 0.f;
    float eqAirDb = 0.f;   // right end

    float compThresholdDb = -18.f;
    float compRatio = 4.f;
    float compAttackMs = 10.f;
    float compReleaseMs = 100.f;
    float compMakeupDb = 0.f;

    float pan = 0.f;   // -1 .. +1
    float hpHz = 0.f;  // 0 = off
    float lpHz = 0.f;  // 0 = off

    float limitThresholdDb = -1.f;
    float limitReleaseMs = 50.f;

    float gateThresholdDb = -40.f;
    float gateAttackMs = 5.f;
    float gateReleaseMs = 100.f;
    float gateRangeDb = -60.f;
};

struct NodeBehavior {
    void (*init)(GraphNode& n);
    void (*rebind)(GraphNode& n);
    std::wstring (*label)(const GraphNode& n);
    bool (*canRun)(const GraphNode& n);
    NodePickMode (*pickMode)();
    int (*pickCount)();
    std::wstring (*pickLabel)(int index);
    bool (*applyPick)(GraphNode& n, int index, std::wstring* status);
    bool isAudioSource;
    bool isAudioSink;
    bool isEffect;
};

struct NodeTypeInfo {
    NodeKind kind;
    const wchar_t* id;
    const wchar_t* menuLabel;
    const wchar_t* title;
    bool hasInPort;
    bool hasOutPort;
    const NodeBehavior* behavior;
};

struct Connection {
    int from = -1;
    int to = -1;
};

struct NodeGraph {
    std::vector<GraphNode> nodes;
    std::vector<Connection> links;
};

struct NodeView {
    int scrollX = 0;
    int scrollY = 0;
};

struct NodeFonts {
    HFONT ui = nullptr;
    HFONT title = nullptr;
};

// one Input → effects → Output chain
struct AudioRoute {
    int source = -1;
    int sink = -1;
    std::vector<int> effects;
};

constexpr int kNodeW = 240;
constexpr int kNodeH = 110;
constexpr int kEqNodeH = 220;
constexpr int kKnobNodeH = 128;
constexpr int kCompNodeH = 148;
constexpr int kGateNodeH = 148;
constexpr int kWaveNodeH = 160;
constexpr int kPortR = 7;
constexpr int kWireHitPx = 10;
constexpr float kEqMinDb = -12.f;
constexpr float kEqMaxDb = 12.f;
constexpr int kWavePlotSamples = 128;

const std::vector<NodeTypeInfo>& nodeCatalog();
const NodeTypeInfo& nodeType(NodeKind kind);
const NodeBehavior& nodeBehavior(NodeKind kind);
int nodeCatalogCount();

int nodeHeight(const GraphNode& n);
RECT nodeRectWorld(const GraphNode& n);
RECT nodeRectScreen(const GraphNode& n, const NodeView& view);
RECT nodeFieldRectScreen(const GraphNode& n, const NodeView& view);
RECT eqCurveRectScreen(const GraphNode& n, const NodeView& view);
RECT eqPresetRectScreen(const GraphNode& n, const NodeView& view);
RECT wavePlotRectScreen(const GraphNode& n, const NodeView& view);
RECT nodeMeterRectScreen(const GraphNode& n, const NodeView& view);
POINT outPortWorld(const GraphNode& n);
POINT inPortWorld(const GraphNode& n);
POINT outPortScreen(const GraphNode& n, const NodeView& view);
POINT inPortScreen(const GraphNode& n, const NodeView& view);
POINT worldToScreen(POINT w, const NodeView& view);
POINT screenToWorld(int sx, int sy, const NodeView& view);

bool hitRect(const RECT& r, int x, int y);
bool hitPortAt(POINT portScreen, int x, int y);
bool hitNodeField(const GraphNode& n, const NodeView& view, int sx, int sy);
bool hitEqCurve(const GraphNode& n, const NodeView& view, int sx, int sy);
int eqBandAt(const GraphNode& n, const NodeView& view, int sx, int sy); // 0..4 or -1
int hitEqHandleAt(const NodeGraph& g, const NodeView& view, int sx, int sy, int* outBand);
void applyEqBandFromY(GraphNode& n, int band, const NodeView& view, int sy);
bool nodeHasPicker(const GraphNode& n);

int nodeKnobCount(const GraphNode& n);
RECT nodeKnobRectScreen(const GraphNode& n, const NodeView& view, int knob);
int hitKnobAt(const NodeGraph& g, const NodeView& view, int sx, int sy, int* outKnob);
float knobNorm(const GraphNode& n, int knob); // 0..1
void applyKnobNorm(GraphNode& n, int knob, float norm);
std::wstring knobCaption(const GraphNode& n, int knob);
std::wstring knobValueText(const GraphNode& n, int knob);

bool copyNodeWaveform(const NodeGraph& g, int nodeIdx, float* out, int count);

GraphNode makeNode(NodeKind kind, int worldX, int worldY);
void deleteNode(NodeGraph& g, int idx);
bool addLink(NodeGraph& g, int from, int to);
void rebindNodes(NodeGraph& g);
std::wstring nodeLabel(const GraphNode& n);
bool nodeCanRun(const GraphNode& n);

void syncLiveFxFromGraph(const NodeGraph& g);

std::vector<AudioRoute> findAudioRoutes(const NodeGraph& g);
bool routeRunnable(const NodeGraph& g, const AudioRoute& route);
bool buildJobFromRoute(const NodeGraph& g, const AudioRoute& route, Job& outJob, bool forceLoopback);
bool linkOnRunnableRoute(const NodeGraph& g, int linkIdx);

// leftover helpers still used in a couple spots
bool linkRunnable(const NodeGraph& g, int linkIdx);
std::vector<int> runnableLinks(const NodeGraph& g);
bool buildJobFromLink(const NodeGraph& g, int linkIdx, Job& outJob, bool forceLoopback);

int hitNodeAt(const NodeGraph& g, const NodeView& view, int sx, int sy);
int hitWireAt(const NodeGraph& g, const NodeView& view, int sx, int sy);
int hitOutPortAt(const NodeGraph& g, const NodeView& view, int sx, int sy);
int hitInPortAt(const NodeGraph& g, const NodeView& view, int sx, int sy);

void drawNode(HDC hdc, const GraphNode& n, const NodeView& view, const NodeFonts& fonts,
              bool flowing, float meterIn, float meterOut, const NodeGraph* graph, int nodeIdx);
void drawWire(HDC hdc, POINT aScreen, POINT bScreen, bool flowing, float audioLevel);
void drawPort(HDC hdc, POINT pScreen, bool hot);
void drawGraph(HDC hdc, const NodeGraph& g, const NodeView& view, const NodeFonts& fonts,
               bool flowing, float audioLevel, float meterIn, float meterOut);
