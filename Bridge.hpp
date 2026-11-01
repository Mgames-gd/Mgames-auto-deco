#pragma once
#include <Geode/Geode.hpp>
#include "Core.hpp"

namespace mgb {
mg::Settings loadSettings();
void saveSettings(const mg::Settings&);
std::string loadPalette(int slot);
void savePalette(int slot, const std::string& v);
// Snapshot of the editor level (read-only).
std::vector<mg::SrcObj> collect(LevelEditorLayer* lel, std::vector<GameObject*>& objs);
struct ApplyReport { int created = 0; int removedOld = 0; bool ok = false; std::string msg; };
ApplyReport apply(LevelEditorLayer* lel, const std::vector<mg::SrcObj>& src, const std::vector<GameObject*>& objs,
                  const mg::Settings& s, const mg::Result& r);
void openSettings(LevelEditorLayer* lel);
}
