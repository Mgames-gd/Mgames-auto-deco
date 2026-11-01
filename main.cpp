#include <Geode/Geode.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>
#include "Bridge.hpp"

using namespace geode::prelude;

class $modify(MgEditorPause, EditorPauseLayer) {
    bool init(LevelEditorLayer* lel) {
        if (!EditorPauseLayer::init(lel)) return false;
        auto spr = ButtonSprite::create("Auto Deco", 80, true, "bigFont.fnt", "GJ_button_01.png", 30.f, .6f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(MgEditorPause::onMgames));
        auto menu = CCMenu::create(); menu->addChild(btn);
        auto win = CCDirector::get()->getWinSize();
        menu->setPosition(ccp(win.width - 55.f, 30.f)); menu->setID("mgames-autodeco-menu"_spr);
        this->addChild(menu, 10);
        return true;
    }
    void onMgames(CCObject*) { mgb::openSettings(m_editorLayer); }
};
