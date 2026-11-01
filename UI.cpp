#include "Bridge.hpp"
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/ui/ColorPickPopup.hpp>
#include <Geode/binding/Slider.hpp>
#include <Geode/binding/SliderThumb.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/GameObject.hpp>

using namespace geode::prelude;

namespace {
ccColor3B C3(mg::RGB c) { return {c.r, c.g, c.b}; }
ccColor4F C4(mg::RGB c, float a) { return {c.r / 255.f, c.g / 255.f, c.b / 255.f, a}; }

// Shared state between the settings popup and the preview popup.
struct Session {
    LevelEditorLayer* lel = nullptr;
    mg::Settings s;
    std::vector<mg::SrcObj> src;
    std::vector<GameObject*> objs;
    mg::Result result;
};
Session g_session;

void notify(std::string const& m) { Notification::create(m, NotificationIcon::None, 2.f)->show(); }

// ---------------------------------------------------------------- Preview
class PreviewCanvas : public CCLayer {
public:
    CCDrawNode* m_draw = nullptr; CCClippingNode* m_clip = nullptr;
    CCSize m_size; float m_zoom = 0.35f; CCPoint m_pan = ccp(0, 0); int m_mode = 0; // 0 decorated, 1 original, 2 gameplay only
    CCLabelBMFont* m_info = nullptr; CCLabelBMFont* m_wm = nullptr;
    bool m_drag = false; CCPoint m_last;

    static PreviewCanvas* create(CCSize size) {
        auto r = new PreviewCanvas();
        if (r && r->init(size)) { r->autorelease(); return r; }
        delete r; return nullptr;
    }
    bool init(CCSize size) {
        if (!CCLayer::init()) return false;
        m_size = size; this->setContentSize(size);
        auto stencil = CCDrawNode::create();
        CCPoint rect[4] = {ccp(0, 0), ccp(size.width, 0), ccp(size.width, size.height), ccp(0, size.height)};
        stencil->drawPolygon(rect, 4, {1, 1, 1, 1}, 0, {0, 0, 0, 0});
        m_clip = CCClippingNode::create(stencil);
        this->addChild(m_clip);
        m_draw = CCDrawNode::create(); m_clip->addChild(m_draw);
        m_wm = CCLabelBMFont::create("Mgames Auto Deco", "bigFont.fnt"); m_wm->setVisible(false); this->addChild(m_wm, 5);
        m_info = CCLabelBMFont::create("", "chatFont.fnt"); m_info->setScale(.7f); m_info->setAnchorPoint(ccp(0, 1)); m_info->setPosition(ccp(4, size.height - 4)); this->addChild(m_info, 5);
        this->setTouchEnabled(true);
        this->setTouchMode(kCCTouchesOneByOne);
        // start view at left edge of the level
        float minX = 1e9f, minY = 1e9f; for (auto& o : g_session.src) { minX = std::min(minX, o.x); minY = std::min(minY, o.y); }
        if (!g_session.src.empty()) m_pan = ccp(minX, minY);
        redraw();
        return true;
    }
    void registerWithTouchDispatcher() override { CCDirector::get()->getTouchDispatcher()->addTargetedDelegate(this, -600, true); }
    bool ccTouchBegan(CCTouch* t, CCEvent*) override {
        auto p = this->convertToNodeSpace(t->getLocation());
        if (p.x < 0 || p.y < 0 || p.x > m_size.width || p.y > m_size.height) return false;
        m_drag = true; m_last = t->getLocation(); return true;
    }
    void ccTouchMoved(CCTouch* t, CCEvent*) override {
        auto p = t->getLocation(); auto d = p - m_last; m_last = p;
        m_pan.x -= d.x / m_zoom; m_pan.y -= d.y / m_zoom; redraw();
    }
    void ccTouchEnded(CCTouch*, CCEvent*) override { m_drag = false; }
    void ccTouchCancelled(CCTouch*, CCEvent*) override { m_drag = false; }

    CCPoint toView(float x, float y) const { return ccp((x - m_pan.x) * m_zoom + 10, (y - m_pan.y) * m_zoom + 10); }
    void quad(float x, float y, float rot, float w, float h, ccColor4F col) {
        float c = std::cos(-rot * 3.14159265f / 180.f), s = std::sin(-rot * 3.14159265f / 180.f);
        float hw = w / 2, hh = h / 2; CCPoint pts[4];
        const float cx[4] = {-hw, hw, hw, -hw}, cy[4] = {-hh, -hh, hh, hh};
        for (int i = 0; i < 4; i++) pts[i] = toView(x + cx[i] * c - cy[i] * s, y + cx[i] * s + cy[i] * c);
        m_draw->drawPolygon(pts, 4, col, 0, {0, 0, 0, 0});
    }
    void redraw() {
        m_draw->clear();
        auto& S = g_session; auto& st = S.s;
        // background (user color) + gameplay flash markers
        CCPoint bg[4] = {ccp(0, 0), ccp(m_size.width, 0), ccp(m_size.width, m_size.height), ccp(0, m_size.height)};
        m_draw->drawPolygon(bg, 4, m_mode == 0 ? C4(st.colors[mg::Background], 1) : ccColor4F{.1f, .1f, .12f, 1}, 0, {0, 0, 0, 0});
        float vx0 = m_pan.x - 20 / m_zoom, vx1 = m_pan.x + m_size.width / m_zoom, vy0 = m_pan.y - 20 / m_zoom, vy1 = m_pan.y + m_size.height / m_zoom;
        auto vis = [&](float x, float y) { return x > vx0 - 90 && x < vx1 + 90 && y > vy0 - 90 && y < vy1 + 90; };
        if (m_mode == 0) for (auto& d : S.result.deco) {
            if (d.zLayer >= 0 || !vis(d.x, d.y)) continue;
            auto col = C4(st.colors[d.slot], d.opacity); quad(d.x, d.y, d.rot, 30 * d.sx, 30 * d.sy, col);
        }
        if (m_mode != 1 || true) for (auto& o : S.src) {
            if (!vis(o.x, o.y)) continue;
            ccColor4F col = o.hazard ? ccColor4F{1, .2f, .2f, 1} : o.special ? ccColor4F{.3f, 1, .4f, 1} : (m_mode == 2 ? ccColor4F{.75f, .75f, .8f, 1} : ccColor4F{.55f, .55f, .6f, .9f});
            quad(o.x, o.y, o.rot, 30, 30, col);
        }
        if (m_mode == 0) for (auto& d : S.result.deco) {
            if (d.zLayer < 0 || !vis(d.x, d.y)) continue;
            quad(d.x, d.y, d.rot, 30 * d.sx, 30 * d.sy, C4(st.colors[d.slot], d.opacity));
        }
        if (m_mode == 0 && st.flashOn) for (auto& f : S.result.flashes) if (f.x > vx0 && f.x < vx1) {
            auto a = toView(f.x, 0), b = toView(f.x + f.duration * 311.58f, 0);
            CCPoint q[4] = {ccp(a.x, 0), ccp(std::max(b.x, a.x + 2), 0), ccp(std::max(b.x, a.x + 2), m_size.height), ccp(a.x, m_size.height)};
            m_draw->drawPolygon(q, 4, C4(st.colors[mg::FlashSlot], f.intensity), 0, {0, 0, 0, 0});
        }
        if (m_mode == 0 && S.result.monster.detected) {
            auto& m = S.result.monster; auto a = toView(m.minX, m.minY), b = toView(m.maxX, m.maxY);
            m_draw->drawSegment(a, ccp(b.x, a.y), 1, {1, 1, 0, 1}); m_draw->drawSegment(ccp(b.x, a.y), b, 1, {1, 1, 0, 1});
            m_draw->drawSegment(b, ccp(a.x, b.y), 1, {1, 1, 0, 1}); m_draw->drawSegment(ccp(a.x, b.y), a, 1, {1, 1, 0, 1});
        }
        static const char* modes[] = {"Decorated Level", "Original Gameplay", "Gameplay Objects Only"};
        auto txt = fmt::format("{} | {} | zoom {:.0f}% | deco {} | flashes {} | monster: {}", mg::styleName(st.style), modes[m_mode], m_zoom * 100, S.result.deco.size(),
                               S.result.flashes.size(), S.result.monster.detected ? fmt::format("YES ({:.2f})", S.result.monster.score) : fmt::format("no ({:.2f})", S.result.monster.score));
        m_info->setString(txt.c_str());
        m_wm->setVisible(st.wmPreview || st.wmLevel);
        m_wm->setScale(0.15f + st.wmSize * 0.5f); m_wm->setOpacity((GLubyte)(st.wmOpacity * 255)); m_wm->setColor(C3(st.wmColor));
        float w = m_wm->getScaledContentSize().width / 2 + 6, h = m_wm->getScaledContentSize().height / 2 + 6;
        float px[5] = {w, m_size.width - w, w, m_size.width - w, m_size.width / 2}, py[5] = {m_size.height - h - 14, m_size.height - h - 14, h, h, m_size.height / 2};
        m_wm->setPosition(ccp(px[st.wmPos], py[st.wmPos]));
    }
    void zoom(float f) { m_zoom = std::clamp(m_zoom * f, 0.05f, 3.f); redraw(); }
    void cycle() { m_mode = (m_mode + 1) % 3; redraw(); }
    void toggleOrig() { m_mode = m_mode == 0 ? 1 : 0; redraw(); }
};

class PreviewPopup : public Popup {
public:
    PreviewCanvas* m_canvas = nullptr; geode::Function<void()> m_onBack;
    static PreviewPopup* create(geode::Function<void()> onBack) {
        auto r = new PreviewPopup();
        if (r->init(440.f, 290.f)) { r->m_onBack = std::move(onBack); r->autorelease(); r->setup(); return r; }
        delete r; return nullptr;
    }
    CCMenuItemSpriteExtra* btn(CCMenu* m, const char* t, float w, SEL_MenuHandler cb, const char* tex = "GJ_button_01.png") {
        auto b = CCMenuItemSpriteExtra::create(ButtonSprite::create(t, (int)w, true, "bigFont.fnt", tex, 26.f, .5f), this, cb); m->addChild(b); return b; }
    void setup() {
        this->setTitle("Preview");
        auto mid = m_size.width / 2;
        m_canvas = PreviewCanvas::create(CCSize(m_size.width - 20, 170)); m_canvas->setPosition(ccp(10, 78)); m_mainLayer->addChild(m_canvas);
        auto top = CCMenu::create(); top->setPosition(ccp(mid, 264)); top->setContentSize(CCSize(m_size.width - 20, 24)); top->setPosition(ccp(mid, 258));
        btn(top, "Zoom +", 52, menu_selector(PreviewPopup::onZoomIn)); btn(top, "Zoom -", 52, menu_selector(PreviewPopup::onZoomOut));
        btn(top, "Orig/Deco", 66, menu_selector(PreviewPopup::onOrig)); btn(top, "View", 40, menu_selector(PreviewPopup::onCycle));
        top->setLayout(RowLayout::create()->setGap(4.f)); m_mainLayer->addChild(top);
        auto bot = CCMenu::create(); bot->setPosition(ccp(mid, 40)); bot->setContentSize(CCSize(m_size.width - 20, 30));
        btn(bot, "Apply", 60, menu_selector(PreviewPopup::onApply), "GJ_button_02.png"); btn(bot, "Regenerate", 84, menu_selector(PreviewPopup::onRegen));
        btn(bot, "Back", 50, menu_selector(PreviewPopup::onBack)); btn(bot, "Cancel", 56, menu_selector(PreviewPopup::onCancel), "GJ_button_06.png");
        bot->setLayout(RowLayout::create()->setGap(5.f)); m_mainLayer->addChild(bot);
        auto note = CCLabelBMFont::create("Temporary preview - the level is not changed until Apply. Drag to pan.", "chatFont.fnt"); note->setScale(.55f); note->setPosition(ccp(mid, 12)); m_mainLayer->addChild(note);
        auto sync = mg::verify(g_session.src, g_session.result);
        if (!sync.ok) notify("Safety check failed - Apply disabled");
    }
    void onZoomIn(CCObject*) { m_canvas->zoom(1.35f); } void onZoomOut(CCObject*) { m_canvas->zoom(1 / 1.35f); }
    void onOrig(CCObject*) { m_canvas->toggleOrig(); } void onCycle(CCObject*) { m_canvas->cycle(); }
    void regen() { g_session.s.seed = (g_session.s.seed * 1103515245 + 12345) & 0x3fffffff; mgb::saveSettings(g_session.s);
                   g_session.result = mg::generate(g_session.src, g_session.s); m_canvas->redraw(); }
    void onRegen(CCObject*) { regen(); notify("New variant (previous preview replaced)"); }
    void onApply(CCObject*) {
        // re-collect so we always verify against the *current* original objects
        std::vector<GameObject*> objs; auto cur = mgb::collect(g_session.lel, objs);
        auto rep = mgb::apply(g_session.lel, cur, objs, g_session.s, g_session.result);
        if (rep.ok) { notify(rep.msg + (rep.removedOld ? " (replaced previous)" : "")); this->onClose(nullptr); } else FLAlertLayer::create("Apply blocked", rep.msg, "OK")->show();
    }
    void onBack(CCObject*) { auto f = std::move(m_onBack); this->onClose(nullptr); if (f) f(); }
    void onCancel(CCObject*) { g_session.result = {}; this->onClose(nullptr); }
};

// ---------------------------------------------------------------- Settings
class SettingsPopup : public Popup {
public:
    int m_tab = 0; CCNode* m_content = nullptr; std::vector<float*> m_sliders; std::vector<bool*> m_toggles; int m_colorSel = 0;
    std::vector<TextInput*> m_hex; std::vector<CCSprite*> m_swatch;

    static SettingsPopup* create() {
        auto r = new SettingsPopup();
        if (r->init(440.f, 290.f)) { r->autorelease(); r->setup(); return r; }
        delete r; return nullptr;
    }
    void save() { mgb::saveSettings(g_session.s); }
    void onClose(CCObject* o) override { save(); Popup::onClose(o); }

    void setup() {
        this->setTitle("Mgames Auto Deco");
        auto mid = m_size.width / 2;
        auto tabs = CCMenu::create(); tabs->setPosition(ccp(mid, 244)); tabs->setContentSize(CCSize(m_size.width - 16, 24));
        const char* names[] = {"Styles", "Colors", "Advanced", "Flash", "Monster", "Watermark"}; const int w[] = {52, 52, 66, 46, 58, 70};
        for (int i = 0; i < 6; i++) { auto b = CCMenuItemSpriteExtra::create(ButtonSprite::create(names[i], w[i], true, "bigFont.fnt", i == m_tab ? "GJ_button_02.png" : "GJ_button_04.png", 24.f, .45f), this, menu_selector(SettingsPopup::onTab)); b->setTag(i); tabs->addChild(b); }
        tabs->setLayout(RowLayout::create()->setGap(3.f)); tabs->setID("tabs"); m_mainLayer->addChild(tabs);
        auto bot = CCMenu::create(); bot->setPosition(ccp(mid, 24)); bot->setContentSize(CCSize(m_size.width - 20, 30));
        auto g = CCMenuItemSpriteExtra::create(ButtonSprite::create("Generate / Preview", 130, true, "goldFont.fnt", "GJ_button_01.png", 30.f, .6f), this, menu_selector(SettingsPopup::onGenerate)); bot->addChild(g);
        auto rs = CCMenuItemSpriteExtra::create(ButtonSprite::create("Reset Settings", 100, true, "bigFont.fnt", "GJ_button_06.png", 30.f, .45f), this, menu_selector(SettingsPopup::onReset)); bot->addChild(rs);
        bot->setLayout(RowLayout::create()->setGap(8.f)); m_mainLayer->addChild(bot);
        buildTab();
    }
    void onTab(CCObject* s) { m_tab = static_cast<CCNode*>(s)->getTag(); save(); refreshTabs(); buildTab(); }
    void refreshTabs() {
        if (auto tabs = m_mainLayer->getChildByID("tabs")) tabs->removeFromParent();
        const char* names[] = {"Styles", "Colors", "Advanced", "Flash", "Monster", "Watermark"}; const int w[] = {52, 52, 66, 46, 58, 70};
        auto nt = CCMenu::create(); nt->setPosition(ccp(m_size.width / 2, 244)); nt->setContentSize(CCSize(m_size.width - 16, 24)); nt->setID("tabs");
        for (int k = 0; k < 6; k++) { auto b = CCMenuItemSpriteExtra::create(ButtonSprite::create(names[k], w[k], true, "bigFont.fnt", k == m_tab ? "GJ_button_02.png" : "GJ_button_04.png", 24.f, .45f), this, menu_selector(SettingsPopup::onTab)); b->setTag(k); nt->addChild(b); }
        nt->setLayout(RowLayout::create()->setGap(3.f)); m_mainLayer->addChild(nt);
    }
    // --- widgets
    void label(const char* t, CCPoint p, float sc = .5f) { auto l = CCLabelBMFont::create(t, "bigFont.fnt"); l->setScale(sc); l->setAnchorPoint(ccp(0, .5f)); l->setPosition(p); m_content->addChild(l); }
    void slider(const char* t, float* v, CCPoint p) {
        label(t, ccp(p.x, p.y + 12), .4f);
        auto s = Slider::create(this, menu_selector(SettingsPopup::onSlider), .55f); s->setPosition(ccp(p.x + 80, p.y - 2)); s->setValue(*v);
        s->getThumb()->setTag((int)m_sliders.size()); m_sliders.push_back(v); m_content->addChild(s);
    }
    void onSlider(CCObject* s) { int t = static_cast<CCNode*>(s)->getTag(); float v = static_cast<SliderThumb*>(s)->getValue(); *m_sliders[t] = v; limitFlash(); save(); }
    void limitFlash() { auto& f = g_session.s; f.flashIntensity = std::min(f.flashIntensity, mg::kMaxFlashIntensity); f.flashDuration = std::clamp(f.flashDuration, 0.05f, mg::kMaxFlashDur); f.flashFade = std::min(f.flashFade, f.flashDuration); }
    void toggle(const char* t, bool* v, CCPoint p) {
        auto m = CCMenu::create(); m->setPosition(ccp(0, 0)); auto tg = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(SettingsPopup::onToggle), .6f);
        tg->setPosition(p); tg->toggle(*v); tg->setTag((int)m_toggles.size()); m_toggles.push_back(v); m->addChild(tg); m_content->addChild(m); label(t, ccp(p.x + 16, p.y), .4f);
    }
    void onToggle(CCObject* s) { auto t = static_cast<CCMenuItemToggler*>(s); *m_toggles[t->getTag()] = !t->isToggled(); save(); }
    CCMenuItemSpriteExtra* button(const char* t, int w, CCPoint p, SEL_MenuHandler cb, int tag = 0, const char* tex = "GJ_button_04.png") {
        auto m = CCMenu::create(); m->setPosition(ccp(0, 0)); auto b = CCMenuItemSpriteExtra::create(ButtonSprite::create(t, w, true, "bigFont.fnt", tex, 24.f, .45f), this, cb);
        b->setPosition(p); b->setTag(tag); m->addChild(b); m_content->addChild(m); return b; }

    void buildTab() {
        if (m_content) m_content->removeFromParent();
        m_sliders.clear(); m_toggles.clear(); m_hex.clear(); m_swatch.clear();
        m_content = CCNode::create(); m_mainLayer->addChild(m_content);
        auto& s = g_session.s; float L = 20, W = m_size.width;
        switch (m_tab) {
        case 0: { // Styles
            label("Choose a style (each has its own generator):", ccp(L, 218), .4f);
            for (int i = 0; i < mg::kStyleCount; i++) {
                float x = 75 + (i % 4) * 100, y = 175 - (i / 4) * 55;
                bool on = (int)s.style == i;
                button(mg::styleName((mg::Style)i), 92, ccp(x, y), menu_selector(SettingsPopup::onStyle), i, on ? "GJ_button_02.png" : "GJ_button_04.png");
            }
            label("1.9 = Catacombs + Artemis 13 sub-styles", ccp(L, 78), .38f);
            label("Seed changes on Regenerate. Same settings + seed = same result.", ccp(L, 62), .33f);
            break; }
        case 1: { // Colors
            const char* n[] = {"Primary", "Secondary", "Glow", "Background", "Extra", "Flash"};
            for (int i = 0; i < mg::SlotCount; i++) {
                float y = 206 - i * 24; label(n[i], ccp(L, y), .4f);
                auto sp = CCSprite::create("square02_001.png"); sp->setScale(.3f); sp->setColor(C3(s.colors[i])); sp->setPosition(ccp(120, y)); m_content->addChild(sp); m_swatch.push_back(sp);
                auto in = TextInput::create(80, "#RRGGBB", "chatFont.fnt"); in->setPosition(ccp(185, y)); in->setScale(.7f); in->setFilter("#0123456789abcdefABCDEF"); in->setMaxCharCount(7);
                in->setString(mg::toHex(s.colors[i])); int idx = i;
                in->setCallback([this, idx](std::string const& str) { mg::RGB c; if (mg::fromHex(str, c)) { g_session.s.colors[idx] = c; m_swatch[idx]->setColor(C3(c)); save(); } });
                m_content->addChild(in); m_hex.push_back(in);
                button("RGB picker", 70, ccp(270, y), menu_selector(SettingsPopup::onPick), i);
            }
            for (int p = 0; p < 3; p++) {
                button(("Save " + std::to_string(p + 1)).c_str(), 40, ccp(345, 206 - p * 24 - 0), menu_selector(SettingsPopup::onSavePal), p);
                button(("Load " + std::to_string(p + 1)).c_str(), 40, ccp(390, 206 - p * 24), menu_selector(SettingsPopup::onLoadPal), p);
            }
            button("Reset colors", 76, ccp(370, 120), menu_selector(SettingsPopup::onResetColors), 0, "GJ_button_06.png");
            break; }
        case 2: // Advanced
            slider("Decoration Density", &s.density, ccp(L, 205)); slider("Glow Intensity", &s.glow, ccp(L, 165)); slider("Detail Level", &s.detail, ccp(L, 125));
            slider("Object Variation", &s.variation, ccp(L, 85)); slider("Randomization", &s.random, ccp(220, 205));
            label("Max 6000 deco objects per generation.", ccp(220, 130), .33f); break;
        case 3: // Flash
            toggle("Enable flashes", &s.flashOn, ccp(L + 8, 215)); toggle("Flash on Beat (BPM)", &s.flashOnBeat, ccp(L + 8, 190)); toggle("Random Flash", &s.flashRandom, ccp(L + 8, 165));
            slider("Intensity (max 60%)", &s.flashIntensity, ccp(230, 215)); slider("Duration (max 0.6s)", &s.flashDuration, ccp(230, 175)); slider("Transition", &s.flashFade, ccp(230, 135));
            label("BPM:", ccp(L, 130), .4f); {
                auto in = TextInput::create(60, "120", "chatFont.fnt"); in->setPosition(ccp(L + 70, 130)); in->setScale(.7f); in->setFilter("0123456789"); in->setMaxCharCount(3); in->setString(std::to_string((int)s.flashBpm));
                in->setCallback([this](std::string const& str) { try { g_session.s.flashBpm = std::clamp(std::stof(str), 40.f, 300.f); save(); } catch (...) {} }); m_content->addChild(in); }
            label("Every N beats:", ccp(L, 100), .4f); {
                auto in = TextInput::create(50, "4", "chatFont.fnt"); in->setPosition(ccp(L + 100, 100)); in->setScale(.7f); in->setFilter("0123456789"); in->setMaxCharCount(2); in->setString(std::to_string((int)s.flashEveryBeats));
                in->setCallback([this](std::string const& str) { try { g_session.s.flashEveryBeats = std::clamp(std::stof(str), 1.f, 32.f); save(); } catch (...) {} }); m_content->addChild(in); }
            label("Flash color: Colors tab. Safe limits are enforced (min 0.5s gap).", ccp(L, 70), .33f); break;
        case 4: // Monster
            toggle("Auto Detect", &s.monsterAuto, ccp(L + 8, 215)); toggle("Force Clubstep Monster", &s.monsterForce, ccp(L + 8, 190));
            slider("Detection Sensitivity", &s.monsterSensitivity, ccp(L, 150)); slider("Monster Detail", &s.monsterDetail, ccp(L, 110));
            label("Detects eyes+mouth+teeth from object layout, not images.", ccp(L, 75), .33f); break;
        case 5: { // Watermark
            toggle("Preview watermark", &s.wmPreview, ccp(L + 8, 215)); toggle("Level watermark (adds text object)", &s.wmLevel, ccp(L + 8, 190));
            slider("Size", &s.wmSize, ccp(L, 150)); slider("Opacity", &s.wmOpacity, ccp(L, 110));
            static const char* pos[] = {"Top Left", "Top Right", "Bottom Left", "Bottom Right", "Center"};
            button(pos[s.wmPos], 90, ccp(310, 195), menu_selector(SettingsPopup::onWmPos)); button("Watermark color", 110, ccp(310, 160), menu_selector(SettingsPopup::onWmColor));
            break; }
        }
    }
    void onStyle(CCObject* o) { g_session.s.style = (mg::Style)static_cast<CCNode*>(o)->getTag(); save(); buildTab(); }
    void onPick(CCObject* o) {
        int i = static_cast<CCNode*>(o)->getTag(); auto c = g_session.s.colors[i]; auto p = ColorPickPopup::create(ccColor3B{c.r, c.g, c.b});
        p->setCallback([this, i](ccColor4B const& col) { g_session.s.colors[i] = {col.r, col.g, col.b}; save(); this->buildTab(); }); p->show();
    }
    void onSavePal(CCObject* o) { int p = static_cast<CCNode*>(o)->getTag(); std::string v; for (int i = 0; i < mg::SlotCount; i++) v += mg::toHex(g_session.s.colors[i]) + ","; mgb::savePalette(p, v); notify("Palette saved"); }
    void onLoadPal(CCObject* o) {
        auto v = mgb::loadPalette(static_cast<CCNode*>(o)->getTag()); if (v.empty()) return notify("Empty slot");
        size_t a = 0; for (int i = 0; i < mg::SlotCount; i++) { auto b = v.find(',', a); if (b == std::string::npos) break; mg::fromHex(v.substr(a, b - a), g_session.s.colors[i]); a = b + 1; }
        save(); buildTab(); }
    void onResetColors(CCObject*) { mg::Settings d; for (int i = 0; i < mg::SlotCount; i++) g_session.s.colors[i] = d.colors[i]; save(); buildTab(); }
    void onWmPos(CCObject*) { g_session.s.wmPos = (g_session.s.wmPos + 1) % 5; save(); buildTab(); }
    void onWmColor(CCObject*) { auto c = g_session.s.wmColor; auto p = ColorPickPopup::create(ccColor3B{c.r, c.g, c.b}); p->setCallback([this](ccColor4B const& col) { g_session.s.wmColor = {col.r, col.g, col.b}; save(); }); p->show(); }
    void onReset(CCObject*) {
        createQuickPopup("Reset", "Reset all Mgames Auto Deco settings?", "No", "Yes", [this](FLAlertLayer*, bool yes) { if (yes) { g_session.s = mg::Settings(); save(); buildTab(); } });
    }
    void onGenerate(CCObject*) {
        save(); auto& S = g_session;
        S.src = mgb::collect(S.lel, S.objs);
        if (S.src.empty()) return notify("The level has no objects");
        S.result = mg::generate(S.src, S.s);
        for (auto& w : S.result.warnings) log::info("[Mgames] {}", w);
        auto v = mg::verify(S.src, S.result);
        if (!v.ok) { FLAlertLayer::create("Safety check", "Generated decoration failed verification:\n" + v.problems[0] + "\nPlease Regenerate.", "OK")->show(); }
        if (S.result.skippedGroupOverflow) notify("Warning: some objects have >10 groups");
        auto pop = PreviewPopup::create([]() { openSettingsAgain(); }); pop->show();
    }
    static void openSettingsAgain() { SettingsPopup::create()->show(); }
};
} // namespace

namespace mgb {
void openSettings(LevelEditorLayer* lel) {
    g_session.lel = lel; g_session.s = loadSettings();
    SettingsPopup::create()->show();
}
}
