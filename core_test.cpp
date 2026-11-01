#include "../src/Core.hpp"
#include <cstdio>
#include <set>
#include <cmath>
#include <algorithm>
using namespace mg;
static int fails = 0;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); fails++; } } while (0)

static std::vector<SrcObj> level() {
    std::vector<SrcObj> v;
    for (int x = 0; x < 60; x++) for (int y = 0; y < 3; y++) { SrcObj o; o.id = 1; o.x = x * 30.f; o.y = y * 30.f; o.solid = true; if (x % 7 == 0) o.groups = {5, 9, 12}; v.push_back(o); }
    for (int x = 5; x < 60; x += 6) { SrcObj o; o.id = 8; o.x = x * 30.f; o.y = 90; o.hazard = true; v.push_back(o); }
    SrcObj p; p.id = 12; p.x = 600; p.y = 150; p.special = true; v.push_back(p);
    return v;
}
// Synthetic "monster": filled blob with two eye holes and a mouth hole, teeth spikes.
static std::vector<SrcObj> monster() {
    std::vector<SrcObj> v; int W = 24, H = 20;
    for (int x = 0; x < W; x++) for (int y = 0; y < H; y++) {
        bool eyeL = x >= 5 && x <= 7 && y >= 12 && y <= 14, eyeR = x >= 16 && x <= 18 && y >= 12 && y <= 14;
        bool mouth = x >= 6 && x <= 17 && y >= 4 && y <= 7;
        if (eyeL || eyeR || mouth) continue;
        SrcObj o; o.id = 1; o.x = x * 30.f; o.y = y * 30.f; o.solid = true; v.push_back(o);
    }
    for (int x = 6; x <= 17; x++) { SrcObj o; o.id = 8; o.x = x * 30.f; o.y = 5 * 30.f; o.hazard = true; v.push_back(o); }
    return v;
}
int main() {
    auto src = level();
    std::set<std::string> sig;
    for (int st = 0; st < kStyleCount; st++) {
        Settings s; s.style = (Style)st; s.density = 0.9f; s.flashOn = true; s.flashRandom = true;
        auto r = generate(src, s);
        auto v = verify(src, r);
        printf("%-22s deco=%5zu flashes=%zu verify=%s\n", styleName(s.style), r.deco.size(), r.flashes.size(), v.ok ? "OK" : "FAIL");
        CHECK(r.deco.size() > 50, "style produced decoration"); CHECK(v.ok, "verify passes");
        for (auto& d : r.deco) { CHECK(d.src >= 0 && d.src < (int)src.size(), "valid src"); if (d.isHazardKey) CHECK(d.noTouch, "hazard noTouch"); }
        // groups inherited
        for (auto& d : r.deco) if (!src[d.src].groups.empty()) CHECK(inheritedGroups(src[d.src]) == src[d.src].groups, "groups inherited fully");
        std::string key; for (size_t i = 0; i < r.deco.size() && i < 40; i++) key += std::to_string(r.deco[i].key) + "," + std::to_string((int)r.deco[i].rot) + ";";
        CHECK(sig.insert(key).second, "styles differ"); // different algorithms -> different output
    }
    // settings influence generation
    Settings a, b; a.density = 0.1f; b.density = 1.f;
    CHECK(generate(src, a).deco.size() < generate(src, b).deco.size(), "density affects count");
    Settings c1, c2; c1.detail = 0; c2.detail = 1; c1.style = c2.style = Style::NineCircles; c1.density = c2.density = 1;
    CHECK(generate(src, c1).deco.size() < generate(src, c2).deco.size(), "detail affects count");
    // seed -> regenerate gives different variant
    Settings d1, d2; d1.seed = 1; d2.seed = 2; d1.style = d2.style = Style::Trueffet; d1.density = d2.density = 1;
    auto r1 = generate(src, d1), r2 = generate(src, d2); bool diff = r1.deco.size() != r2.deco.size();
    for (size_t i = 0; !diff && i < std::min(r1.deco.size(), r2.deco.size()); i++) diff = r1.deco[i].key != r2.deco[i].key || r1.deco[i].rot != r2.deco[i].rot;
    CHECK(diff, "regenerate (new seed) differs");
    // determinism
    CHECK(generate(src, d1).deco.size() == r1.deco.size(), "same seed deterministic");
    // gameplay safety: deco never lands on source hazards
    { Settings s; s.style = Style::Slaughterhouse; s.density = 1; auto r = generate(src, s);
      for (auto& d : r.deco) for (auto& o : src) if ((o.hazard || o.special) && d.isHazardKey) CHECK(!(std::fabs(o.x - d.x) < 24 && std::fabs(o.y - d.y) < 24), "no hazard-deco near real hazard"); }
    // tampered result must fail verification
    { Settings s; auto r = generate(src, s); r.deco[0].key = 8; r.deco[0].isHazardKey = true; r.deco[0].noTouch = false; CHECK(!verify(src, r).ok, "verify catches unsafe hazard"); }
    { Settings s; s.flashOn = true; auto r = generate(src, s); if (!r.flashes.empty()) { r.flashes[0].intensity = 1.f; CHECK(!verify(src, r).ok, "verify catches unsafe flash"); } }
    // flash limits
    { Settings s; s.flashOn = true; s.flashIntensity = 1.f; s.flashDuration = 5.f; s.flashEveryBeats = 1; s.flashBpm = 300; auto r = generate(src, s); CHECK(verify(src, r).ok, "flash clamped to safe limits"); for (auto& f : r.flashes) CHECK(f.intensity <= kMaxFlashIntensity + 1e-4f, "intensity clamped"); }
    // monster detection
    { auto m = monster(); auto mi = detectMonster(m, 0.5f); printf("monster score=%.2f detected=%d\n", mi.score, mi.detected); CHECK(mi.detected, "monster detected");
      auto flat = level(); auto fi = detectMonster(flat, 0.5f); printf("flat level score=%.2f detected=%d\n", fi.score, fi.detected); CHECK(!fi.detected, "normal level not a monster");
      Settings s; auto r = generate(m, s); CHECK(r.monster.detected, "generate uses detection"); CHECK(verify(m, r).ok, "monster deco verify");
      Settings f; f.monsterForce = true; auto rf = generate(flat, f); CHECK(rf.monster.detected, "force works"); CHECK(verify(flat, rf).ok, "forced verify");
      Settings off; off.monsterAuto = false; auto ro = generate(m, off); CHECK(ro.deco.size() < r.deco.size(), "auto-off gives plain deco"); }
    // serialization round trip
    { Settings s; s.style = Style::Society; s.colors[0] = {1, 2, 3}; s.density = 0.77f; s.flashOn = true; s.wmLevel = true; s.flashIntensity = 0.5f;
      auto t = deserialize(serialize(s)); CHECK(t.style == Style::Society && t.colors[0].g == 2 && t.flashOn && t.wmLevel, "roundtrip");
      CHECK(std::fabs(t.density - 0.77f) < 1e-3, "float roundtrip"); CHECK(deserialize("garbage;;=x;style=999").style == Style::NineCircles, "garbage clamps"); }
    { RGB c; CHECK(fromHex("#1A2b3C", c) && c.r == 0x1A && c.b == 0x3C, "hex parse"); CHECK(!fromHex("#12", c), "bad hex"); }
    // big level cap/perf
    { std::vector<SrcObj> big; for (int i = 0; i < 40000; i++) { SrcObj o; o.id = 1; o.x = (i % 400) * 30.f; o.y = (i / 400) * 30.f; o.solid = true; big.push_back(o); }
      Settings s; s.density = 1; s.style = Style::Trueffet; auto r = generate(big, s); CHECK((int)r.deco.size() <= kMaxDeco, "cap respected"); printf("big level deco=%zu\n", r.deco.size()); }
    printf(fails ? "\n%d FAILURES\n" : "\nALL TESTS PASSED\n", fails);
    return fails != 0;
}
