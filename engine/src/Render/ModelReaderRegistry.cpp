#include "okay/Render/ModelReaderRegistry.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include <vector>

namespace okay {

namespace {
std::vector<ModelReaderFn>& Table() { static std::vector<ModelReaderFn> t; return t; }
}

void RegisterModelReader(int slot, ModelReaderFn fn) {
    if (slot < 0 || slot > 255) return;
    auto& t = Table();
    if ((int)t.size() <= slot) t.resize((size_t)slot + 1);
    t[(size_t)slot] = std::move(fn);
}

const ModelReaderFn* FindModelReader(int slot) {
    (void)ModuleRegistry::Get();   // the first call lets the modules register their readers
    auto& t = Table();
    if (slot < 0 || slot >= (int)t.size() || !t[(size_t)slot]) return nullptr;
    return &t[(size_t)slot];
}

} // namespace okay
