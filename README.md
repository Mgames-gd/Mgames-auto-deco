# Mgames Auto Deco (Geode, Android, GD 2.2081)

## Как получить .geode (бесплатно, без установки NDK)
1. Создай пустой репозиторий на GitHub и залей туда содержимое этой папки (включая `.github`).
2. Вкладка **Actions → Build Mod → Run workflow** (или просто push в main).
3. Когда сборка станет зелёной, открой **Releases** репозитория и скачай `mgames.autodeco.geode` напрямую (НЕ переименовывай .zip в .geode!).
4. Положи `.geode` в папку `Android/media/com.geode.launcher/game/geode/mods/` и запусти GD через Geode Launcher.
Если сборка упала — пришли мне лог (шаг "Build"), я исправлю.

## Тесты ядра
`g++ -std=c++20 -fsanitize=address,undefined src/Core.cpp tests/core_test.cpp -o t && ./t`
