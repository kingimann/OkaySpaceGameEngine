#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Platform/Steam/SteamService.hpp"
#include <memory>

namespace okay {

/// Drop-in component that owns an ISteamService, initializes it on Awake, pumps
/// its callbacks every frame, and shuts it down on destroy. Access the service
/// through Service() to unlock achievements, set stats, and so on.
class SteamManager : public Behaviour {
public:
    SteamConfig config;

    ISteamService* Service() const { return m_service.get(); }
    bool IsAvailable() const { return m_service && m_service->IsAvailable(); }

    /// Convenience pass-throughs for the most common calls.
    bool UnlockAchievement(const std::string& id) {
        return m_service && m_service->UnlockAchievement(id);
    }
    void SetStat(const std::string& name, float value) {
        if (m_service) m_service->SetStat(name, value);
    }

    void Awake() override;
    void Update(float dt) override;
    void OnDestroy() override;

private:
    std::unique_ptr<ISteamService> m_service;
};

} // namespace okay
