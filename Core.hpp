#pragma once
// Pure C++ core (no Geode dependency): style generators, safety checks, monster detection,
// flash planning. Unit-tested natively (tests/core_test.cpp).
#include <vector>
#include <string>
#include <cstdint>

namespace mg {

struct RGB { uint8_t r = 255, g = 255, b = 255; };

enum class Style { Catacombs, Artemis13, Slaughterhouse, Trueffet, Golden, Society, SonicWave, NineCircles };
inline constexpr int kStyleCount = 8;
const char* styleName(Style s);

// Color slots used by decoration objects.
enum Slot { Primary = 0, Secondary, Glow, Background, Extra, FlashSlot, SlotCount };

struct Settings {
    Style style = Style::Catacombs;
    RGB colors[SlotCount] = { {70,130,255}, {20,30,70}, {120,200,255}, {8,8,20}, {255,255,255}, {255,255,255} };
    // Advanced (0..1 unless noted)
    float density = 0.5f, glow = 0.6f, detail = 0.5f, variation = 0.5f, random = 0.5f;
    int seed = 1;
    // Flash
    bool flashOn = false, flashOnBeat = true, flashRandom = false;
    float flashBpm = 120.f, flashEveryBeats = 4.f, flashDuration = 0.25f, flashIntensity = 0.4f, flashFade = 0.1f;
    // Monster
    bool monsterAuto = true, monsterForce = false;
    float monsterSensitivity = 0.5f, monsterDetail = 0.5f;
    // Watermark
    bool wmPreview = true, wmLevel = false;
    int wmPos = 3; float wmSize = 0.5f, wmOpacity = 0.5f; RGB wmColor{255,255,255};
};

// Source (original) object snapshot. The mod never modifies these.
struct SrcObj {
    int id = 0; float x = 0, y = 0, rot = 0;
    bool hazard = false;   // spike/saw etc. (real danger)
    bool solid = false;    // plain block, safe decoration anchor
    bool special = false;  // portal/pad/orb/trigger/other gameplay-relevant
    std::vector<int> groups;
};

// One generated decoration object (becomes a real GD object on Apply).
struct Deco {
    int key = 1;           // GD object id (visual only)
    float x = 0, y = 0, rot = 0, sx = 1, sy = 1;
    int zLayer = -1;       // -1 behind gameplay, +1 in front (visual order)
    int slot = Primary;    // color slot
    float opacity = 1.f;
    int src = -1;          // index of source object it follows (groups inherited)
    bool isHazardKey = false; // key is a spike/saw -> MUST have noTouch
    bool noTouch = true;
    bool glow = false;
};

struct FlashEvt { float x; float duration; float intensity; float fade; };

struct MonsterInfo { bool detected = false; float score = 0; float minX=0,maxX=0,minY=0,maxY=0; };

struct Result {
    std::vector<Deco> deco;
    std::vector<FlashEvt> flashes;
    MonsterInfo monster;
    std::vector<std::string> warnings;
    int skippedGroupOverflow = 0;
};

// Object ids used (visual only; hazards ONLY with noTouch, verified by verify()).
inline constexpr int kBlock[] = {1, 2, 3, 4};
inline constexpr int kSlab = 40;
inline constexpr int kSpike = 8, kSmallSpike = 39;
inline constexpr int kSaw[] = {88, 89, 98};
bool isHazardKey(int key);

MonsterInfo detectMonster(const std::vector<SrcObj>& src, float sensitivity);
Result generate(const std::vector<SrcObj>& src, const Settings& s);

struct Verify { bool ok = true; std::vector<std::string> problems; };
// Checks: every hazard-key deco has noTouch; no deco is placed in front of a source hazard/special
// object; every deco refers to a valid source (so groups are inherited); flashes are within limits.
Verify verify(const std::vector<SrcObj>& src, const Result& r);

// Group inheritance: returns groups for a deco (copy of the source's groups, max 10 total GD limit).
std::vector<int> inheritedGroups(const SrcObj& src);

// Serialization of settings (for Geode saved value).
std::string serialize(const Settings& s);
Settings deserialize(const std::string& str);
std::string toHex(RGB c);
bool fromHex(const std::string& h, RGB& out);

constexpr float kMaxFlashIntensity = 0.6f, kMinFlashGap = 0.5f, kMaxFlashDur = 0.6f;
constexpr int kMaxDeco = 6000;

} // namespace mg
