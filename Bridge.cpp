#include "Bridge.hpp"
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/EffectGameObject.hpp>
#include <Geode/binding/TextGameObject.hpp>
#include <Geode/binding/GJSpriteColor.hpp>
#include <set>

using namespace geode::prelude;

namespace mgb {

mg::Settings loadSettings() { return mg::deserialize(Mod::get()->getSavedValue<std::string>("settings", "")); }
void saveSettings(const mg::Settings& s) { Mod::get()->setSavedValue<std::string>("settings", mg::serialize(s)); }
std::string loadPalette(int i) { return Mod::get()->getSavedValue<std::string>("palette" + std::to_string(i), ""); }
void savePalette(int i, const std::string& v) { Mod::get()->setSavedValue<std::string>("palette" + std::to_string(i), v); }

std::vector<mg::SrcObj> collect(LevelEditorLayer* lel, std::vector<GameObject*>& objs) {
    std::vector<mg::SrcObj> out;
    objs.clear();
    if (!lel || !lel->m_objects) return out;
    for (auto obj : CCArrayExt<GameObject*>(lel->m_objects)) {
        if (!obj) continue;
        // Skip decoration made by this mod earlier (tagged via user string in save) - handled by apply() registry.
        mg::SrcObj s;
        s.id = obj->m_objectID;
        s.x = obj->getPositionX(); s.y = obj->getPositionY(); s.rot = obj->getRotation();
        auto t = obj->m_objectType;
        s.hazard = (t == GameObjectType::Hazard) || (t == GameObjectType::AnimatedHazard);
        s.solid = (t == GameObjectType::Solid);
        s.special = !(s.hazard || s.solid || t == GameObjectType::Decoration);
        if (obj->m_groups) for (int i = 0; i < obj->m_groupCount; i++) { int g = (*obj->m_groups)[i]; if (g > 0) s.groups.push_back(g); }
        out.push_back(std::move(s));
        objs.push_back(obj);
    }
    return out;
}

static std::map<LevelEditorLayer*, std::vector<GameObject*>>& registry() { static std::map<LevelEditorLayer*, std::vector<GameObject*>> m; return m; }

static ccColor3B C(mg::RGB c) { return {c.r, c.g, c.b}; }

ApplyReport apply(LevelEditorLayer* lel, const std::vector<mg::SrcObj>& src, const std::vector<GameObject*>& objs,
                  const mg::Settings& s, const mg::Result& r) {
    ApplyReport rep;
    // 1. safety gate: refuse unsafe results
    auto v = mg::verify(src, r);
    if (!v.ok) { rep.msg = "Safety check failed: " + (v.problems.empty() ? std::string("unknown") : v.problems[0]); return rep; }

    // 2. remove deco created by a previous Apply (no duplicates)
    auto& prev = registry()[lel];
    for (auto o : prev) if (lel->m_objects->containsObject(o)) { lel->removeObject(o, true); rep.removedOld++; }
    prev.clear();

    // 3. pick 6 free color channels
    std::set<int> used;
    for (auto o : objs) { if (o->m_baseColor) used.insert(o->m_baseColor->m_colorID); if (o->m_detailColor) used.insert(o->m_detailColor->m_colorID); }
    int ch[mg::SlotCount]; int next = 900;
    for (int i = 0; i < mg::SlotCount; i++) { while (used.count(next)) next++; ch[i] = next++; used.insert(ch[i]); }
    float minX = 1e9f, topY = 0;
    for (auto& o : src) { minX = std::min(minX, o.x); topY = std::max(topY, o.y); }
    if (src.empty()) minX = 0;

    auto addColorTrigger = [&](float x, float y, int channel, ccColor3B col, float dur, float opacity = 1.f) {
        auto t = static_cast<EffectGameObject*>(lel->createObject(899, ccp(x, y), false));
        if (!t) return;
        t->m_targetGroupID = channel;           // color trigger stores its target channel here
        t->m_triggerTargetColor = col;
        t->m_duration = dur;
        t->m_opacity = opacity;
        prev.push_back(t); rep.created++;
    };
    float ty = topY + 150.f;
    for (int i = 0; i < mg::SlotCount; i++) addColorTrigger(minX, ty + i * 12, ch[i], C(s.colors[i]), 0.f);
    addColorTrigger(minX, ty + 80, 1000, C(s.colors[mg::Background]), 0.f);

    // 4. decoration
    for (auto& d : r.deco) {
        auto o = lel->createObject(d.key, ccp(d.x, d.y), false);
        if (!o) continue;
        o->setRotation(d.rot);
        o->updateCustomScaleX(d.sx); o->updateCustomScaleY(d.sy);
        o->m_isNoTouch = true;                  // no collision: player can never die from deco
        o->m_zLayer = d.zLayer < 0 ? ZLayer::B2 : ZLayer::T1;
        if (o->m_baseColor) o->m_baseColor->m_colorID = ch[d.slot];
        if (o->m_detailColor) o->m_detailColor->m_colorID = ch[d.slot];
        o->setObjectColor(C(s.colors[d.slot]));
        o->setOpacity((GLubyte)(d.opacity * 255));
        // Group Sync: inherit ALL groups of the source object (never touches the source)
        if (d.src >= 0 && d.src < (int)objs.size()) for (int g : mg::inheritedGroups(src[d.src])) o->addToGroup(g);
        prev.push_back(o); rep.created++;
    }
    // 5. flashes (color triggers on BG channel; restore to user's bg color)
    for (auto& f : r.flashes) {
        auto bg = s.colors[mg::Background], fl = s.colors[mg::FlashSlot];
        mg::RGB mix{(uint8_t)(bg.r + (fl.r - bg.r) * f.intensity), (uint8_t)(bg.g + (fl.g - bg.g) * f.intensity), (uint8_t)(bg.b + (fl.b - bg.b) * f.intensity)};
        addColorTrigger(f.x, ty + 100, 1000, C(mix), f.fade);
        addColorTrigger(f.x + f.duration * 311.58f, ty + 100, 1000, C(bg), f.fade);
    }
    // 6. level watermark
    if (s.wmLevel) {
        float x = minX + 60, y = topY + 40;
        auto t = static_cast<TextGameObject*>(lel->createObject(914, ccp(x, y), false));
        if (t) { t->updateTextObject("Mgames Auto Deco", false); float sc = 0.3f + s.wmSize * 1.2f; t->updateCustomScaleX(sc); t->updateCustomScaleY(sc);
                 t->m_isNoTouch = true; t->setOpacity((GLubyte)(s.wmOpacity * 255)); t->setObjectColor(C(s.wmColor)); prev.push_back(t); rep.created++; }
    }
    rep.ok = true; rep.msg = "Applied " + std::to_string(rep.created) + " objects";
    return rep;
}
} // namespace mgb
