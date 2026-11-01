#include <Geode/Geode.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include "Bridge.hpp"

using namespace geode::prelude;

$on_mod(Loaded) {
    log::info("Mgames Auto Deco: loaded, hooks active");
}

static CCMenuItemSpriteExtra* makeAutoDecoButton(CCObject* target, SEL_MenuHandler sel) {
    auto spr = ButtonSprite::create("Auto Deco", 80, true, "bigFont.fnt", "GJ_button_01.png", 30.f, .6f);
    return CCMenuItemSpriteExtra::create(spr, target, sel);
}

// Primary entry: pause menu of the editor (bottom-right corner).
class $modify(MgEditorPause, EditorPauseLayer) {
    bool init(LevelEditorLayer* lel) {
        if (!EditorPauseLayer::init(lel)) return false;
        auto btn = makeAutoDecoButton(this, menu_selector(MgEditorPause::onMgames));
        auto menu = CCMenu::create();
        menu->addChild(btn);
        auto win = CCDirector::get()->getWinSize();
        menu->setPosition(ccp(win.width - 55.f, 30.f));
        menu->setID("mgames-autodeco-menu"_spr);
        this->addChild(menu, 10);
        log::info("Mgames Auto Deco: pause button added");
        return true;
    }
    void onMgames(CCObject*) { mgb::openSettings(m_editorLayer); }
};

// Fallback entry: editor top bar, so the button exists even if the pause hook fails.
class $modify(MgEditorUI, EditorUI) {
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;
        auto btn = makeAutoDecoButton(this, menu_selector(MgEditorUI::onMgames));
        auto menu = CCMenu::create();
        menu->addChild(btn);
        auto win = CCDirector::get()->getWinSize();
        menu->setPosition(ccp(win.width / 2, win.height - 22.f));
        menu->setID("mgames-autodeco-topmenu"_spr);
        this->addChild(menu, 100);
        log::info("Mgames Auto Deco: editor top button added");
        return true;
    }
    void onMgames(CCObject*) { mgb::openSettings(this->m_editorLayer); }
};
