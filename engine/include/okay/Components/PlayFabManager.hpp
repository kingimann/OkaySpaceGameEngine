#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Platform/PlayFab/PlayFabService.hpp"
#include <memory>
#include <string>

namespace okay {

/// Component that owns an IPlayFabService, creating it on Awake and optionally
/// logging in automatically. Access the service through Service() for player
/// data, statistics, and leaderboards.
class PlayFabManager : public Behaviour {
public:
    PlayFabConfig config;
    /// If set, the manager logs in with this custom id on Awake.
    std::string autoLoginCustomId;

    IPlayFabService* Service() const { return m_service.get(); }
    bool IsLoggedIn() const { return m_service && m_service->IsLoggedIn(); }

    void Awake() override;
    void OnDestroy() override;

private:
    std::unique_ptr<IPlayFabService> m_service;
};

} // namespace okay
