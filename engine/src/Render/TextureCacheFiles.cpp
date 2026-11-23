// Reloading a picture that changed on disk (the editor's Project panel calls this for each changed or reimported file).
// Written on claude2/upgrade-import against the old per-name texture cache; on claude2/texture-system it goes through the
// shared texture library instead (textures::ReloadFile, Render/TextureAsset.hpp): every name the file was drawn under is
// decoded again in place, so there is one reload path (the same one hot reload and Reload use).
#include "okay/Render/TextureCacheFiles.hpp"
#include "okay/Render/SoftwareRenderer.hpp"   // TextureCacheGeneration
#include "okay/Render/TextureAsset.hpp"

namespace okay {

int InvalidateTextureCacheForFile(const std::string& file) {
    const unsigned before = textures::Generation();
    const int names = textures::ReloadFile(file);
    if (textures::Generation() != before) ++TextureCacheGeneration();   // GPU renderers that cache by name look again
    return names;
}

} // namespace okay
