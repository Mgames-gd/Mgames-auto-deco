#include "Core.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace mg {

static const char* kNames[] = {"1.9 Catacombs","1.9 Artemis 13","Slaughterhouse","Trueffet","The Golden","Society","Sonic Wave Infinity","Nine Circles"};
const char* styleName(Style s) { return kNames[(int)s]; }

bool isHazardKey(int key) {
    if (key == kSpike || key == kSmallSpike) return true;
    for (int k : kSaw) if (k == key) return true;
    return false;
}

namespace {
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x1234567ull) { next(); next(); }
    uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    float f() { return (next() >> 11) * (1.0f / 9007199254740992.0f); }
    int i(int n) { return n <= 0 ? 0 : (int)(next() % (uint64_t)n); }
    float r(float a, float b) { return a + (b - a) * f(); }
};
constexpr float G = 30.f;
long long cellKey(int cx, int cy) { return ((long long)cx << 32) ^ (unsigned int)cy; }
int cellOf(float v, float size) { return (int)std::floor(v / size); }
} // namespace

std::vector<int> inheritedGroups(const SrcObj& s) {
    std::vector<int> g;
    for (int v : s.groups) if (v > 0 && std::find(g.begin(), g.end(), v) == g.end()) g.push_back(v);
    if (g.size() > 10) g.resize(10);
    return g;
}

// ---------------- Monster detection ----------------
MonsterInfo detectMonster(const std::vector<SrcObj>& src, float sens) {
    MonsterInfo m;
    std::unordered_map<long long, int> cells;
    for (int i = 0; i < (int)src.size(); i++)
        cells[cellKey(cellOf(src[i].x, G), cellOf(src[i].y, G))] = i;
    // largest 8-connected component
    std::unordered_set<long long> seen; std::vector<std::pair<int,int>> best;
    for (auto& [k, idx] : cells) {
        if (seen.count(k)) continue;
        std::vector<std::pair<int,int>> comp; std::queue<std::pair<int,int>> q;
        int cx = cellOf(src[idx].x, G), cy = cellOf(src[idx].y, G);
        q.push({cx, cy}); seen.insert(k);
        while (!q.empty()) {
            auto [x, y] = q.front(); q.pop(); comp.push_back({x, y});
            for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) {
                long long kk = cellKey(x + dx, y + dy);
                if (cells.count(kk) && !seen.count(kk)) { seen.insert(kk); q.push({x + dx, y + dy}); }
            }
        }
        if (comp.size() > best.size()) best = std::move(comp);
    }
    if (best.size() < 40) return m;
    int minx = 1<<30, maxx = -(1<<30), miny = 1<<30, maxy = -(1<<30);
    for (auto& [x, y] : best) { minx = std::min(minx, x); maxx = std::max(maxx, x); miny = std::min(miny, y); maxy = std::max(maxy, y); }
    int W = maxx - minx + 1, H = maxy - miny + 1;
    if (W < 8 || H < 8 || W * H > 40000) return m;
    float asp = (float)W / H; if (asp < 0.5f || asp > 3.f) return m;
    std::vector<char> filled(W * H, 0);
    for (auto& [x, y] : best) filled[(y - miny) * W + (x - minx)] = 1;
    // symmetry about vertical axis
    int mirrorHit = 0;
    for (auto& [x, y] : best) {
        int mx = minx + maxx - x;
        if (filled[(y - miny) * W + (mx - minx)]) mirrorHit++;
    }
    float sym = (float)mirrorHit / best.size();
    // enclosed empty components
    std::vector<int> label(W * H, 0); int nl = 0; std::vector<int> csize; std::vector<char> border;
    std::vector<float> ccx, ccy; std::vector<int> cminx, cmaxx;
    for (int sy = 0; sy < H; sy++) for (int sx = 0; sx < W; sx++) {
        if (filled[sy * W + sx] || label[sy * W + sx]) continue;
        nl++; int sz = 0; bool bord = false; double sxm = 0, sym2 = 0; int mn = W, mx2 = 0;
        std::queue<std::pair<int,int>> q; q.push({sx, sy}); label[sy * W + sx] = nl;
        while (!q.empty()) {
            auto [x, y] = q.front(); q.pop(); sz++; sxm += x; sym2 += y; mn = std::min(mn, x); mx2 = std::max(mx2, x);
            if (x == 0 || y == 0 || x == W - 1 || y == H - 1) bord = true;
            const int d[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
            for (auto& dd : d) { int nx = x + dd[0], ny = y + dd[1];
                if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                if (filled[ny * W + nx] || label[ny * W + nx]) continue;
                label[ny * W + nx] = nl; q.push({nx, ny}); }
        }
        csize.push_back(sz); border.push_back(bord); ccx.push_back(sxm / sz); ccy.push_back(sym2 / sz); cminx.push_back(mn); cmaxx.push_back(mx2);
    }
    float eyes = 0, mouth = 0;
    std::vector<int> enc;
    for (int i = 0; i < nl; i++) if (!border[i] && csize[i] >= 3) enc.push_back(i);
    for (size_t a = 0; a < enc.size(); a++) for (size_t b = a + 1; b < enc.size(); b++) {
        int i = enc[a], j = enc[b];
        bool upper = ccy[i] > H * 0.45f && ccy[j] > H * 0.45f; // y grows upward in GD
        float ratio = (float)std::min(csize[i], csize[j]) / std::max(csize[i], csize[j]);
        float cxm = (ccx[i] + ccx[j]) / 2;
        float symOff = std::fabs(cxm - (W - 1) / 2.f) / W;
        float dy = std::fabs(ccy[i] - ccy[j]);
        if (!upper || ratio < 0.5f || symOff > 0.2f || dy > 2.5f || std::fabs(ccx[i] - ccx[j]) < 2) continue;
        eyes = std::max(eyes, std::min(1.f, 0.6f + 0.4f * ratio));
    }
    for (int i : enc) {
        int wd = cmaxx[i] - cminx[i] + 1;
        if (ccy[i] < H * 0.55f && wd >= W * 0.25f && csize[i] >= 6) mouth = std::max(mouth, std::min(1.f, (float)wd / (W * 0.5f)));
    }
    int teeth = 0;
    for (auto& s : src) if ((s.id == kSpike || s.id == kSmallSpike) && cellOf(s.x, G) >= minx && cellOf(s.x, G) <= maxx && cellOf(s.y, G) >= miny && cellOf(s.y, G) <= maxy) teeth++;
    float teethS = std::min(1.f, teeth / 12.f);
    m.score = 0.3f * sym + 0.3f * eyes + 0.25f * mouth + 0.15f * teethS;
    float thr = 0.85f - 0.45f * std::clamp(sens, 0.f, 1.f);
    m.detected = m.score >= thr && eyes > 0 && mouth > 0; // eyes AND mouth required: avoids false positives
    m.minX = minx * G; m.maxX = (maxx + 1) * G; m.minY = miny * G; m.maxY = (maxy + 1) * G;
    return m;
}

// ---------------- Style generators ----------------
namespace {
struct Ctx {
    const std::vector<SrcObj>& src; const Settings& s; Result& r; Rng rng;
    std::unordered_map<long long, std::vector<int>> danger; // spatial hash of hazards/special
    Ctx(const std::vector<SrcObj>& a, const Settings& b, Result& c) : src(a), s(b), r(c), rng(b.seed) {}

    bool nearDanger(float x, float y, float rad) const {
        int cx = cellOf(x, 60), cy = cellOf(y, 60);
        for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) {
            auto it = danger.find(cellKey(cx + dx, cy + dy)); if (it == danger.end()) continue;
            for (int i : it->second) if (std::fabs(src[i].x - x) < rad && std::fabs(src[i].y - y) < rad) return true;
        }
        return false;
    }
    // dx,dy relative to anchor block center
    void add(int si, int key, float dx, float dy, float rot, float sx, float sy, int z, int slot, float op, bool glow = false) {
        if ((int)r.deco.size() >= kMaxDeco) return;
        Deco d; d.key = key; d.x = src[si].x + dx; d.y = src[si].y + dy; d.rot = rot; d.sx = sx; d.sy = sy;
        d.zLayer = z; d.slot = slot; d.opacity = std::clamp(op, 0.f, 1.f); d.src = si; d.glow = glow;
        d.isHazardKey = isHazardKey(key); d.noTouch = true;
        if (d.isHazardKey && nearDanger(d.x, d.y, 24.f)) return;           // never next to real hazards
        if (z >= 0 && nearDanger(d.x, d.y, 20.f)) return;                   // never cover gameplay objects
        r.deco.push_back(d);
    }
    int block() { return kBlock[rng.i(4)]; }
    float gl() const { return s.glow; }
};

void catacombs(Ctx& c, int i) {
    auto& R = c.rng; float det = c.s.detail;
    c.add(i, 1, 0, 0, 0, 1.f, 1.f, -1, Secondary, 0.9f);                       // dark base layer
    c.add(i, 1, 0, 0, 0, 0.72f, 0.72f, -1, Background, 0.8f);                  // inner recess
    const float t = 0.12f, o = 10.f;                                           // thin frame lines
    c.add(i, kSlab, 0, o, 0, 0.8f, t, 0, Primary, 0.85f); c.add(i, kSlab, 0, -o, 0, 0.8f, t, 0, Primary, 0.85f);
    c.add(i, kSlab, o, 0, 90, 0.8f, t, 0, Primary, 0.85f); c.add(i, kSlab, -o, 0, 90, 0.8f, t, 0, Primary, 0.85f);
    if (R.f() < 0.4f + 0.5f * det) { float p = R.r(.3f, .5f); c.add(i, 1, 0, 0, 0, p, p, 0, Primary, 0.35f); }
    if (R.f() < det) for (int k = 0; k < 4; k++) c.add(i, 1, (k & 1 ? 12.f : -12.f), (k & 2 ? 12.f : -12.f), 0, 0.14f, 0.14f, 0, Extra, 0.7f);
    if (R.f() < 0.12f * c.s.variation + 0.05f) c.add(i, 1, R.r(-45, 45), R.r(-45, 45), 0, 1.f, 1.f, -1, Secondary, 0.5f); // atmospheric back block
}
void artemis(Ctx& c, int i) {
    auto& R = c.rng; float gl = c.gl();
    c.add(i, 1, 0, 0, 0, 1.25f, 1.25f, -1, Glow, 0.25f + 0.45f * gl, true);   // outer glow halo
    c.add(i, 1, 0, 0, 0, 1.08f, 1.08f, -1, Glow, 0.4f * gl + 0.1f, true);
    c.add(i, 1, 0, 0, 0, 0.9f, 0.9f, 0, Secondary, 0.95f);                     // crisp contour block
    c.add(i, 1, 0, 0, 45, 0.55f, 0.55f, 0, Primary, 0.9f);                     // diamond pattern
    c.add(i, 1, 0, 0, 45, 0.3f, 0.3f, 0, Extra, 0.8f);
    if (R.f() < 0.5f + 0.4f * c.s.detail) for (int k = 0; k < 4; k++) c.add(i, 1, (k & 1 ? 11.f : -11.f), (k & 2 ? 11.f : -11.f), 45, 0.18f, 0.18f, 0, Glow, 0.9f, true); // light accents
    if (R.f() < 0.3f * c.s.variation + 0.1f) c.add(i, kSlab, 0, 0, 90 * R.i(2), 1.f, 0.2f, 0, Glow, 0.7f, true);
}
void slaughter(Ctx& c, int i) {
    auto& R = c.rng; float rnd = c.s.random;
    c.add(i, 1, 0, 0, 0, 1.f, 1.f, -1, Secondary, 1.f);                        // dark silhouette
    int n = 2 + (int)(c.s.detail * 3);
    for (int k = 0; k < n; k++) {                                              // jagged torn edges: spikes poking out
        int side = R.i(4); float len = R.r(0.5f, 1.2f) * (0.6f + rnd); float off = R.r(-12, 12);
        float rot = side * 90.f + R.r(-15.f, 15.f) * rnd;
        float dx = side == 1 ? 15 + 6 : side == 3 ? -21 : off, dy = side == 0 ? 21 : side == 2 ? -21 : off;
        c.add(i, R.f() < .6f ? kSpike : kSmallSpike, dx, dy, rot, len, len, -1, R.f() < .5f ? Primary : Extra, 0.95f);
    }
    for (int k = 0; k < 2; k++) c.add(i, kSpike, R.r(-9, 9), R.r(-9, 9), R.r(0, 360), 0.45f, 0.45f, 0, Primary, 0.8f); // triangles inside block
    if (R.f() < 0.25f + 0.3f * c.s.variation) c.add(i, 1, R.r(-6, 6), R.r(-6, 6), R.r(0, 90), 0.5f, 0.5f, 0, Glow, 0.4f * c.gl() + .1f, true); // local glow
    if (R.f() < 0.15f) c.add(i, kSaw[2], R.r(-8, 8), R.r(-8, 8), R.r(0, 360), 0.5f, 0.5f, 0, Extra, 0.9f);
}
void trueffet(Ctx& c, int i) {
    auto& R = c.rng; int v = R.i(1 + (int)(c.s.variation * 4));              // not all blocks the same
    c.add(i, c.block(), 0, 0, 0, 1.f, 1.f, -1, Secondary, 0.95f);
    switch (v) {
        case 0: for (float s = 0.8f; s > 0.3f; s -= 0.2f) c.add(i, 1, 0, 0, 0, s, s, 0, (R.f() < .5f) ? Primary : Extra, 0.75f); break;
        case 1: c.add(i, kSlab, 0, 0, 0, 1.f, 0.25f, 0, Primary, 0.9f); c.add(i, kSlab, 0, 0, 90, 1.f, 0.25f, 0, Primary, 0.9f); c.add(i, 1, 0, 0, 45, 0.3f, 0.3f, 0, Glow, 0.9f, true); break;
        case 2: for (int k = -2; k <= 2; k++) c.add(i, kSlab, k * 6.f, 0, 45, 1.1f, 0.12f, 0, k % 2 ? Primary : Extra, 0.8f); break;
        case 3: c.add(i, kSaw[1], 0, 0, R.r(0, 360), R.r(.5f, .9f), R.r(.5f, .9f), 0, Primary, 0.9f); c.add(i, 1, 0, 0, 0, .3f, .3f, 0, Glow, .8f, true); break;
        default: c.add(i, 1, 0, 0, 22.5f, 0.7f, 0.7f, 0, Extra, .7f); c.add(i, 1, 0, 0, 67.5f, 0.7f, 0.7f, 0, Primary, .7f); break;
    }
    if (R.f() < 0.2f + 0.3f * c.s.detail) {                                      // artwork outside the main blocks
        float d = R.r(40, 120); float a = R.r(0, 6.283f);
        c.add(i, c.block(), std::cos(a) * d, std::sin(a) * d, R.r(0, 90), R.r(.3f, 1.4f), R.r(.3f, 1.4f), -1, R.f() < .5f ? Secondary : Glow, R.r(.2f, .6f), R.f() < .4f);
    }
}
void golden(Ctx& c, int i) {
    auto& R = c.rng;
    c.add(i, 1, 0, 0, 0, 1.f, 1.f, -1, Secondary, 1.f);
    c.add(i, 1, 0, 0, 0, 0.85f, 0.85f, 0, Primary, 0.35f);                    // multi-layer frames
    c.add(i, kSaw[R.i(3)], R.r(-6, 6), R.r(-6, 6), R.r(0, 360), R.r(.5f, .9f), R.r(.5f, .9f), 0, Primary, 0.95f); // gears
    c.add(i, kSaw[2], 0, 0, R.r(0, 360), 0.5f, 0.5f, 1, Extra, 0.9f);        // front gear layer
    c.add(i, 1, 0, 0, 45, 0.28f, 0.28f, 1, Glow, 0.6f + 0.4f * c.gl(), true);   // round glowing core (diamond-ish)
    if (R.f() < .5f) c.add(i, kSpike, 0, 19, 0, 0.45f, 0.45f, -1, Primary, 0.9f); // triangular protrusions
    if (R.f() < .35f * c.s.detail + .1f) c.add(i, kSlab, 0, 0, 0, 0.9f, 0.1f, 0, Extra, 0.8f);
}
void society(Ctx& c, int i) {
    auto& R = c.rng; float d = c.s.density;
    if (R.f() < 0.12f + 0.25f * c.s.variation) {                                  // drawn faces / creatures
        float fx = R.r(-20, 20), fy = R.r(-10, 10); float es = R.r(.4f, .8f);
        c.add(i, kSaw[0], fx - 7, fy + 4, 0, es * .5f, es * .5f, 1, Extra, 0.9f);   // eyes: uneven sizes (asymmetric)
        c.add(i, kSaw[1], fx + 7, fy + R.r(2, 8), 0, es * .35f, es * .35f, 1, Extra, 0.9f);
        c.add(i, 1, fx - 7, fy + 4, 0, .1f, .1f, 1, Background, 1.f);               // pupils
        for (int k = 0; k < 5; k++) c.add(i, kSmallSpike, fx - 10 + k * 5, fy - 8, 180, .22f, R.r(.2f, .4f), 1, Extra, 0.9f); // crooked teeth
    } else {
        c.add(i, 1, R.r(-8, 8), R.r(-8, 8), R.r(-20, 20), R.r(.6f, 1.1f), R.r(.3f, 1.3f), 0, R.f() < .5f ? Primary : Secondary, R.r(.4f, .9f)); // collage scraps
        c.add(i, kSlab, R.r(-10, 10), R.r(-10, 10), R.r(0, 180), R.r(.5f, 1.2f), 0.18f, 1, Extra, 0.7f);
    }
    if (R.f() < 0.2f + 0.4f * d) c.add(i, 1, R.r(-60, 60), R.r(-60, 60), 0, R.r(2.f, 4.f), R.r(2.f, 4.f), -1, R.f() < .5f ? Glow : Background, 0.08f + 0.12f * c.gl(), true); // fog
    if (R.f() < 0.1f) c.add(i, 1, R.r(-90, 90), R.r(30, 120), R.r(-10, 10), 0.5f, R.r(1.f, 3.f), -1, Secondary, 0.8f); // odd silhouette architecture
}
void sonic(Ctx& c, int i) {
    auto& R = c.rng;
    c.add(i, 1, 0, 0, 0, 1.f, 1.f, -1, Secondary, 1.f);
    c.add(i, 1, 0, 0, 0, 0.8f, 0.8f, 0, Background, 0.8f);                      // stacked rectangles
    c.add(i, kSlab, 0, 0, 45, 1.3f, 0.1f, 0, Primary, 0.9f);                     // diagonal lines
    c.add(i, kSlab, 0, 0, -45, 1.3f, 0.1f, 0, Primary, 0.5f);
    const float o = 12.f; for (int k = 0; k < 4; k++) c.add(i, kSpike, (k & 1 ? o : -o), (k & 2 ? o : -o), 45 + 90 * k, 0.3f, 0.3f, 0, Extra, 0.9f); // angular frame corners
    if (R.f() < 0.35f + 0.4f * c.s.detail) c.add(i, kSaw[2], 0, 0, R.r(0, 360), 0.55f, 0.55f, 0, Extra, 0.9f); // gear
    c.add(i, 1, 0, 0, 0, 0.98f, 0.98f, 1, Glow, 0.1f + 0.3f * c.gl(), true);     // glowing outline layer
}
void nine(Ctx& c, int i, int pulse) {
    auto& R = c.rng;
    c.add(i, 1, 0, 0, 0, 1.f, 1.f, -1, Secondary, 1.f);
    int rings = 2 + (int)(c.s.detail * 3);
    for (int k = 0; k < rings; k++) {                                              // concentric diamonds
        float s = 0.9f - k * (0.7f / rings);
        c.add(i, 1, 0, 0, 45, s, s, 0, ((k + pulse) & 1) ? Primary : Extra, 0.55f + 0.1f * k, k == 0);
    }
    for (int k = 0; k < 4; k++) c.add(i, kSpike, 0, 0, 90 * k, 0.28f, 0.28f, 0, Glow, 0.8f * c.gl() + .2f, true); // triangular elements
    if (R.f() < 0.3f + 0.4f * c.s.variation) c.add(i, kSlab, 0, 0, 45 * (1 + 2 * R.i(2)), 1.2f, 0.12f, 1, Glow, 0.7f, true); // diagonal corridor lines
    if (R.f() < 0.1f) c.add(i, 1, R.r(-50, 50), R.r(-50, 50), 45, R.r(1.f, 2.f), R.r(1.f, 2.f), -1, Primary, 0.15f); // repeating background diamonds
}

void monsterDeco(Ctx& c, const MonsterInfo& m, float detail) {
    auto nearest = [&](float x, float y) { int bi = -1; float bd = 1e30f;
        for (int i = 0; i < (int)c.src.size(); i++) { if (c.src[i].special) continue; float d = (c.src[i].x - x) * (c.src[i].x - x) + (c.src[i].y - y) * (c.src[i].y - y); if (d < bd) { bd = d; bi = i; } }
        return bi; };
    float w = m.maxX - m.minX, h = m.maxY - m.minY;
    auto put = [&](float x, float y, int key, float rot, float sx, float sy, int z, int slot, float op, bool glow) {
        int si = nearest(x, y); if (si < 0) return; c.add(si, key, x - c.src[si].x, y - c.src[si].y, rot, sx, sy, z, slot, op, glow); };
    put(m.minX + w / 2, m.minY + h / 2, 1, 0, w / G * 1.15f, h / G * 1.15f, -1, Glow, 0.18f + 0.2f * c.gl(), true); // silhouette halo
    for (int e = 0; e < 2; e++) {                                                 // glowing eyes
        float ex = m.minX + w * (e ? 0.7f : 0.3f), ey = m.minY + h * 0.68f;
        put(ex, ey, kSaw[0], 0, 1.4f, 1.4f, 1, Extra, 0.9f, false);
        put(ex, ey, 1, 45, 1.2f, 1.2f, 1, Glow, 0.5f + 0.4f * c.gl(), true);
    }
    int teeth = 6 + (int)(detail * 10); float y0 = m.minY + h * 0.38f;
    for (int t = 0; t < teeth; t++) {                                             // teeth row (no-touch)
        float tx = m.minX + w * (0.2f + 0.6f * t / std::max(1, teeth - 1));
        put(tx, y0, kSpike, 0, .6f, .8f, 1, Extra, .95f, false);
        put(tx, y0 + 24, kSpike, 180, .6f, .8f, 1, Extra, .95f, false);
    }
}
} // namespace

Result generate(const std::vector<SrcObj>& src, const Settings& s) {
    Result r; Ctx c(src, s, r);
    for (int i = 0; i < (int)src.size(); i++)
        if (src[i].hazard || src[i].special) c.danger[cellKey(cellOf(src[i].x, 60), cellOf(src[i].y, 60))].push_back(i);
    // monster
    r.monster = detectMonster(src, s.monsterSensitivity);
    if (s.monsterForce && !r.monster.detected && !src.empty()) {
        r.monster.detected = true; r.monster.score = 1;
        r.monster.minX = r.monster.minY = 1e30f; r.monster.maxX = r.monster.maxY = -1e30f;
        for (auto& o : src) { r.monster.minX = std::min(r.monster.minX, o.x); r.monster.maxX = std::max(r.monster.maxX, o.x); r.monster.minY = std::min(r.monster.minY, o.y); r.monster.maxY = std::max(r.monster.maxY, o.y); }
        r.warnings.push_back("Force Clubstep Monster: using bounding box of the whole level.");
    }
    bool monsterMode = (s.monsterForce || (s.monsterAuto && r.monster.detected)) && r.monster.detected;
    float chance = 0.15f + 0.85f * std::clamp(s.density, 0.f, 1.f);
    int pulse = 0;
    for (int i = 0; i < (int)src.size(); i++) {
        if (!src[i].solid || src[i].hazard || src[i].special) continue;
        if (c.rng.f() > chance) continue;
        switch (s.style) {
            case Style::Catacombs: catacombs(c, i); break;
            case Style::Artemis13: artemis(c, i); break;
            case Style::Slaughterhouse: slaughter(c, i); break;
            case Style::Trueffet: trueffet(c, i); break;
            case Style::Golden: golden(c, i); break;
            case Style::Society: society(c, i); break;
            case Style::SonicWave: sonic(c, i); break;
            case Style::NineCircles: nine(c, i, pulse++ / 3); break;
        }
        if ((int)r.deco.size() >= kMaxDeco) { r.warnings.push_back("Decoration limit reached; density was capped."); break; }
    }
    if (monsterMode) monsterDeco(c, r.monster, s.monsterDetail);
    // group overflow check: groups beyond GD's 10 are dropped
    for (auto& d : r.deco) if (d.src >= 0 && (int)src[d.src].groups.size() > 10) r.skippedGroupOverflow++;
    if (r.skippedGroupOverflow) r.warnings.push_back("Some objects have >10 groups; extra groups cannot be inherited.");
    // flashes
    if (s.flashOn && !src.empty()) {
        float endX = 0; for (auto& o : src) endX = std::max(endX, o.x);
        const float speed = 311.58f; // normal speed units/sec (speed portals are not modeled)
        float intervalSec = s.flashOnBeat ? 60.f / std::max(40.f, s.flashBpm) * std::max(1.f, s.flashEveryBeats) : std::max(0.5f, s.flashEveryBeats);
        intervalSec = std::max(intervalSec, kMinFlashGap);
        float dur = std::clamp(s.flashDuration, 0.05f, kMaxFlashDur);
        float inten = std::clamp(s.flashIntensity, 0.f, kMaxFlashIntensity);
        float fade = std::clamp(s.flashFade, 0.f, dur);
        Rng fr(s.seed + 77);
        float t = intervalSec;
        for (int n = 0; n < 2000 && t * speed < endX; n++) {
            float tt = t;
            if (s.flashRandom) { tt += fr.r(-0.3f, 0.3f) * intervalSec; if (fr.f() < 0.3f) { t += intervalSec; continue; } }
            if (!r.flashes.empty() && tt * speed - r.flashes.back().x < kMinFlashGap * speed) { t += intervalSec; continue; }
            r.flashes.push_back({tt * speed, dur, inten, fade}); t += intervalSec;
        }
        r.warnings.push_back("Flash timing assumes normal speed; add a BPM matching the song for Flash on Beat.");
    }
    return r;
}

Verify verify(const std::vector<SrcObj>& src, const Result& r) {
    Verify v;
    auto fail = [&](std::string m) { v.ok = false; if (v.problems.size() < 10) v.problems.push_back(std::move(m)); };
    for (size_t i = 0; i < r.deco.size(); i++) {
        auto& d = r.deco[i];
        if (d.src < 0 || d.src >= (int)src.size()) { fail("decoration without source (group sync impossible)"); continue; }
        if (isHazardKey(d.key) && !d.noTouch) fail("hazard-type decoration without noTouch");
        if (!d.noTouch) fail("decoration collides");
    }
    std::unordered_map<long long, std::vector<int>> danger;
    for (int i = 0; i < (int)src.size(); i++) if (src[i].hazard || src[i].special) danger[cellKey(cellOf(src[i].x, 60), cellOf(src[i].y, 60))].push_back(i);
    for (auto& d : r.deco) {
        float rad = isHazardKey(d.key) ? 24.f : (d.zLayer >= 0 ? 20.f : 0.f);
        if (rad == 0) continue;
        int cx = cellOf(d.x, 60), cy = cellOf(d.y, 60);
        for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) {
            auto it = danger.find(cellKey(cx + dx, cy + dy)); if (it == danger.end()) continue;
            for (int i : it->second) if (std::fabs(src[i].x - d.x) < rad && std::fabs(src[i].y - d.y) < rad) fail("decoration overlaps a gameplay object");
        }
    }
    for (size_t i = 0; i < r.flashes.size(); i++) {
        auto& f = r.flashes[i];
        if (f.intensity > kMaxFlashIntensity + 1e-4f || f.duration > kMaxFlashDur + 1e-4f) fail("flash exceeds safe limits");
        if (i && f.x - r.flashes[i - 1].x < kMinFlashGap * 311.58f - 1.f) fail("flashes too frequent");
    }
    return v;
}

// ---------------- Serialization ----------------
std::string toHex(RGB c) { char b[8]; snprintf(b, sizeof b, "#%02X%02X%02X", c.r, c.g, c.b); return b; }
bool fromHex(const std::string& h, RGB& o) {
    std::string t = h; if (!t.empty() && t[0] == '#') t.erase(0, 1);
    if (t.size() != 6) return false;
    for (char ch : t) if (!isxdigit((unsigned char)ch)) return false;
    unsigned v = std::stoul(t, nullptr, 16); o = {(uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v}; return true;
}
std::string serialize(const Settings& s) {
    std::ostringstream o;
    o << "style=" << (int)s.style << ";";
    for (int i = 0; i < SlotCount; i++) o << "c" << i << "=" << toHex(s.colors[i]) << ";";
    o << "den=" << s.density << ";glow=" << s.glow << ";det=" << s.detail << ";var=" << s.variation << ";rnd=" << s.random << ";seed=" << s.seed
      << ";fon=" << s.flashOn << ";fbeat=" << s.flashOnBeat << ";frnd=" << s.flashRandom << ";fbpm=" << s.flashBpm << ";fevery=" << s.flashEveryBeats
      << ";fdur=" << s.flashDuration << ";fint=" << s.flashIntensity << ";ffade=" << s.flashFade
      << ";mauto=" << s.monsterAuto << ";mforce=" << s.monsterForce << ";msens=" << s.monsterSensitivity << ";mdet=" << s.monsterDetail
      << ";wmp=" << s.wmPreview << ";wml=" << s.wmLevel << ";wmpos=" << s.wmPos << ";wmsize=" << s.wmSize << ";wmop=" << s.wmOpacity << ";wmcol=" << toHex(s.wmColor) << ";";
    return o.str();
}
Settings deserialize(const std::string& str) {
    Settings s; std::map<std::string, std::string> kv; std::istringstream in(str); std::string tok;
    while (std::getline(in, tok, ';')) { auto p = tok.find('='); if (p != std::string::npos) kv[tok.substr(0, p)] = tok.substr(p + 1); }
    auto F = [&](const char* k, float& v, float lo, float hi) { if (kv.count(k)) { try { v = std::clamp(std::stof(kv[k]), lo, hi); } catch (...) {} } };
    auto B = [&](const char* k, bool& v) { if (kv.count(k)) v = kv[k] == "1"; };
    auto I = [&](const char* k, int& v, int lo, int hi) { if (kv.count(k)) { try { v = std::clamp(std::stoi(kv[k]), lo, hi); } catch (...) {} } };
    int st = (int)s.style; I("style", st, 0, kStyleCount - 1); s.style = (Style)st;
    for (int i = 0; i < SlotCount; i++) { std::string k = "c" + std::to_string(i); if (kv.count(k)) fromHex(kv[k], s.colors[i]); }
    F("den", s.density, 0, 1); F("glow", s.glow, 0, 1); F("det", s.detail, 0, 1); F("var", s.variation, 0, 1); F("rnd", s.random, 0, 1); I("seed", s.seed, 0, 1 << 30);
    B("fon", s.flashOn); B("fbeat", s.flashOnBeat); B("frnd", s.flashRandom); F("fbpm", s.flashBpm, 40, 300); F("fevery", s.flashEveryBeats, 1, 32);
    F("fdur", s.flashDuration, 0.05f, kMaxFlashDur); F("fint", s.flashIntensity, 0, kMaxFlashIntensity); F("ffade", s.flashFade, 0, kMaxFlashDur);
    B("mauto", s.monsterAuto); B("mforce", s.monsterForce); F("msens", s.monsterSensitivity, 0, 1); F("mdet", s.monsterDetail, 0, 1);
    B("wmp", s.wmPreview); B("wml", s.wmLevel); I("wmpos", s.wmPos, 0, 4); F("wmsize", s.wmSize, 0, 1); F("wmop", s.wmOpacity, 0, 1);
    if (kv.count("wmcol")) fromHex(kv["wmcol"], s.wmColor);
    return s;
}
} // namespace mg
