#include "okay/Components/SteamManager.hpp"
#include "okay/Core/Log.hpp"

namespace okay {

void SteamManager::Awake() {
    m_service = CreateSteamService();
    if (m_service && m_service->Initialize(config)) {
        OKAY_INFO("SteamManager: backend '", m_service->BackendName(),
                  "' ready (available=", m_service->IsAvailable() ? "yes" : "no", ")");
    } else {
        OKAY_WARN("SteamManager: Steam service unavailable; running without it");
    }
}

void SteamManager::Update(float) {
    if (m_service) m_service->RunCallbacks();
}

void SteamManager::OnDestroy() {
    if (m_service) { m_service->Shutdown(); m_service.reset(); }
}

} // namespace okay
