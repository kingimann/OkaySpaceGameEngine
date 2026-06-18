#include "okay/Components/PlayFabManager.hpp"
#include "okay/Core/Log.hpp"

namespace okay {

void PlayFabManager::Awake() {
    m_service = CreatePlayFabService();
    if (!m_service || !m_service->Initialize(config)) {
        OKAY_WARN("PlayFabManager: service unavailable");
        return;
    }
    OKAY_INFO("PlayFabManager: backend '", m_service->BackendName(), "' ready");
    if (!autoLoginCustomId.empty())
        m_service->LoginWithCustomId(autoLoginCustomId, /*createAccount=*/true);
}

void PlayFabManager::OnDestroy() {
    if (m_service) { m_service->Shutdown(); m_service.reset(); }
}

} // namespace okay
