// The Store Kit's run-time Epic backend (okay/Platform/Epic/EpicService.hpp, CreateDynamicEpicService).
//
// It loads the EOS SDK library the game developer copied from their own SDK (EOSSDK-Win64-Shipping.dll; we never
// ship it) and calls the plain C functions of Epic's documented EOS C interface:
//   EOS_Initialize, EOS_Platform_Create / _Tick / _Release, EOS_Shutdown and the EOS_Platform_Get*Interface getters;
//   EOS_Auth_Login (the launcher's exchange code, or the Developer Authentication Tool), EOS_Auth_CopyIdToken,
//   EOS_Connect_Login with that Epic id token and EOS_Connect_CreateUser the first time;
//   EOS_Achievements_UnlockAchievements / _QueryPlayerAchievements; EOS_Stats_IngestStat / _QueryStats;
//   EOS_PlayerDataStorage_WriteFile / _ReadFile / _DeleteFile / _QueryFileList (cloud saves);
//   EOS_Presence_* (rich text) and EOS_UI_ShowFriends.
// Every EOS call finishes later in a callback that EOS runs from EOS_Platform_Tick, which the Store Kit calls once a
// frame (PlatformServices::Tick -> IEpicService::Tick). Calls the game makes before the login finishes are kept and
// sent once it has.
//
// Our own code from Epic's public documentation of those functions; no SDK header or sample is used (the shapes we
// pass are in EosFlat.hpp, with the details that are unconfirmed). Secrets: the client secret goes only to
// EOS_Platform_Create and the exchange code only to EOS_Auth_Login; neither is ever logged.
#include "okay/Platform/Epic/EpicService.hpp"
#include "okay/Core/Log.hpp"
#include "EosFlat.hpp"
#include "../RuntimeLibrary.hpp"

#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace okay {

const char* DefaultEosLibraryName() {
#if defined(_WIN32)
    return "EOSSDK-Win64-Shipping.dll";
#elif defined(__APPLE__)
    return "libEOSSDK-Mac-Shipping.dylib";
#else
    return "libEOSSDK-Linux-Shipping.so";
#endif
}

std::string DeriveEosEncryptionKey(const std::string& productId, const std::string& deploymentId) {
    // Four 64-bit FNV-1a hashes of the ids, each started from a different seed, as 64 hex digits. Not a secret
    // (the key ships inside every copy of the game anyway); it only has to stay the same for the game's life.
    static const char* hex = "0123456789ABCDEF";
    const std::string text = "okayspace-eos-pds|" + productId + "|" + deploymentId;
    std::string out;
    for (std::uint64_t round = 0; round < 4; ++round) {
        std::uint64_t h = 1469598103934665603ull ^ (round * 0x9E3779B97F4A7C15ull);
        for (unsigned char c : text) { h ^= c; h *= 1099511628211ull; }
        for (int b = 15; b >= 0; --b) out.push_back(hex[(h >> (b * 4)) & 15]);
    }
    return out;
}

namespace {

using namespace eosflat;

bool IsHexKey(const std::string& k) {
    if (k.size() != 64) return false;
    for (char c : k) if (!std::isxdigit((unsigned char)c)) return false;
    return true;
}

// The longest prefix of `s` of at most `max` bytes that does not cut a UTF-8 character in two.
std::string CutUtf8(const std::string& s, std::size_t max) {
    if (s.size() <= max) return s;
    std::size_t n = max;
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) --n;
    return s.substr(0, n);
}

using FnInitialize = Result (*)(const InitializeOptions*);
using FnShutdown = Result (*)();
using FnPlatformCreate = Handle (*)(const PlatformOptions*);
using FnHandle = void (*)(Handle);
using FnGetIface = Handle (*)(Handle);
using FnAuthLogin = void (*)(Handle, const AuthLoginOptions*, void*, AuthLoginCallback);
using FnCopyIdToken = Result (*)(Handle, const AuthCopyIdTokenOptions*, AuthIdToken**);
using FnIdTokenRelease = void (*)(AuthIdToken*);
using FnAccountToString = Result (*)(EpicAccountId, char*, std::int32_t*);
using FnConnectLogin = void (*)(Handle, const ConnectLoginOptions*, void*, ConnectLoginCallback);
using FnConnectCreate = void (*)(Handle, const ConnectCreateUserOptions*, void*, ConnectCreateUserCallback);
using FnAchUnlock = void (*)(Handle, const AchUnlockOptions*, void*, AchUnlockCallback);
using FnAchQuery = void (*)(Handle, const AchQueryPlayerOptions*, void*, AchQueryPlayerCallback);
using FnAchCount = std::uint32_t (*)(Handle, const AchGetCountOptions*);
using FnAchCopy = Result (*)(Handle, const AchCopyByIndexOptions*, PlayerAchievement**);
using FnAchRelease = void (*)(PlayerAchievement*);
using FnStatIngest = void (*)(Handle, const StatIngestOptions*, void*, StatIngestCallback);
using FnStatQuery = void (*)(Handle, const StatQueryOptions*, void*, StatQueryCallback);
using FnStatCount = std::uint32_t (*)(Handle, const StatGetCountOptions*);
using FnStatCopy = Result (*)(Handle, const StatCopyByIndexOptions*, Stat**);
using FnStatRelease = void (*)(Stat*);
using FnPdsWrite = Handle (*)(Handle, const PdsWriteOptions*, void*, PdsTransferDoneCallback);
using FnPdsRead = Handle (*)(Handle, const PdsReadOptions*, void*, PdsTransferDoneCallback);
using FnPdsDelete = void (*)(Handle, const PdsDeleteOptions*, void*, PdsDeleteCallback);
using FnPdsList = void (*)(Handle, const PdsQueryListOptions*, void*, PdsQueryListCallback);
using FnPdsMeta = Result (*)(Handle, const PdsCopyMetaAtIndexOptions*, PdsFileMetadata**);
using FnPdsMetaRelease = void (*)(PdsFileMetadata*);
using FnPresenceCreateMod = Result (*)(Handle, const PresenceCreateModOptions*, Handle*);
using FnPresenceSetStatus = Result (*)(Handle, const PresenceSetStatusOptions*);
using FnPresenceSetText = Result (*)(Handle, const PresenceSetRichTextOptions*);
using FnPresenceSet = void (*)(Handle, const PresenceSetOptions*, void*, PresenceSetCallback);
using FnShowFriends = void (*)(Handle, const UiShowFriendsOptions*, void*, UiShowFriendsCallback);

constexpr std::uint32_t kChunk = 4096;         // bytes per Player Data Storage write chunk
constexpr std::uint32_t kPrefetchFiles = 64;   // after login, files up to this many are fetched at once ...
constexpr std::uint32_t kPrefetchBytes = 4u << 20;   // ... when each is at most this big

class DynamicEpicService;

// One Player Data Storage transfer (a read or a write) in flight.
struct Transfer {
    DynamicEpicService* self = nullptr;
    std::string name, data;
    std::size_t at = 0;
    bool write = false, done = false;
    Handle request = nullptr;
};

class DynamicEpicService : public IEpicService {
public:
    ~DynamicEpicService() override { Shutdown(); }

    bool Load(const std::string& path) {
        if (!m_lib.Open(path)) return false;
        eosInit = m_lib.Get<FnInitialize>("EOS_Initialize");
        eosShutdown = m_lib.Get<FnShutdown>("EOS_Shutdown");
        platformCreate = m_lib.Get<FnPlatformCreate>("EOS_Platform_Create");
        platformTick = m_lib.Get<FnHandle>("EOS_Platform_Tick");
        platformRelease = m_lib.Get<FnHandle>("EOS_Platform_Release");
        getAuth = m_lib.Get<FnGetIface>("EOS_Platform_GetAuthInterface");
        getConnect = m_lib.Get<FnGetIface>("EOS_Platform_GetConnectInterface");
        getAch = m_lib.Get<FnGetIface>("EOS_Platform_GetAchievementsInterface");
        getStats = m_lib.Get<FnGetIface>("EOS_Platform_GetStatsInterface");
        getPds = m_lib.Get<FnGetIface>("EOS_Platform_GetPlayerDataStorageInterface");
        getPresence = m_lib.Get<FnGetIface>("EOS_Platform_GetPresenceInterface");
        getUi = m_lib.Get<FnGetIface>("EOS_Platform_GetUIInterface");
        authLogin = m_lib.Get<FnAuthLogin>("EOS_Auth_Login");
        copyIdToken = m_lib.Get<FnCopyIdToken>("EOS_Auth_CopyIdToken");
        idTokenRelease = m_lib.Get<FnIdTokenRelease>("EOS_Auth_IdToken_Release");
        accountToString = m_lib.Get<FnAccountToString>("EOS_EpicAccountId_ToString");
        connectLogin = m_lib.Get<FnConnectLogin>("EOS_Connect_Login");
        connectCreate = m_lib.Get<FnConnectCreate>("EOS_Connect_CreateUser");
        achUnlock = m_lib.Get<FnAchUnlock>("EOS_Achievements_UnlockAchievements");
        achQuery = m_lib.Get<FnAchQuery>("EOS_Achievements_QueryPlayerAchievements");
        achCount = m_lib.Get<FnAchCount>("EOS_Achievements_GetPlayerAchievementCount");
        achCopy = m_lib.Get<FnAchCopy>("EOS_Achievements_CopyPlayerAchievementByIndex");
        achRelease = m_lib.Get<FnAchRelease>("EOS_Achievements_PlayerAchievement_Release");
        statIngest = m_lib.Get<FnStatIngest>("EOS_Stats_IngestStat");
        statQuery = m_lib.Get<FnStatQuery>("EOS_Stats_QueryStats");
        statCount = m_lib.Get<FnStatCount>("EOS_Stats_GetStatsCount");
        statCopy = m_lib.Get<FnStatCopy>("EOS_Stats_CopyStatByIndex");
        statRelease = m_lib.Get<FnStatRelease>("EOS_Stats_Stat_Release");
        pdsWrite = m_lib.Get<FnPdsWrite>("EOS_PlayerDataStorage_WriteFile");
        pdsRead = m_lib.Get<FnPdsRead>("EOS_PlayerDataStorage_ReadFile");
        pdsDelete = m_lib.Get<FnPdsDelete>("EOS_PlayerDataStorage_DeleteFile");
        pdsList = m_lib.Get<FnPdsList>("EOS_PlayerDataStorage_QueryFileList");
        pdsMeta = m_lib.Get<FnPdsMeta>("EOS_PlayerDataStorage_CopyFileMetadataAtIndex");
        pdsMetaRelease = m_lib.Get<FnPdsMetaRelease>("EOS_PlayerDataStorage_FileMetadata_Release");
        pdsTransferRelease = m_lib.Get<FnHandle>("EOS_PlayerDataStorageFileTransferRequest_Release");
        presCreateMod = m_lib.Get<FnPresenceCreateMod>("EOS_Presence_CreatePresenceModification");
        presSetStatus = m_lib.Get<FnPresenceSetStatus>("EOS_PresenceModification_SetStatus");
        presSetText = m_lib.Get<FnPresenceSetText>("EOS_PresenceModification_SetRawRichText");
        presModRelease = m_lib.Get<FnHandle>("EOS_PresenceModification_Release");
        presSet = m_lib.Get<FnPresenceSet>("EOS_Presence_SetPresence");
        showFriends = m_lib.Get<FnShowFriends>("EOS_UI_ShowFriends");
        // The minimum: start, pump, stop, log in. Every service beyond that is optional (a call it lacks does nothing).
        return eosInit && eosShutdown && platformCreate && platformTick && platformRelease && getAuth && getConnect &&
               authLogin && copyIdToken && connectLogin;
    }

    bool Initialize(const EpicConfig& config) override {
        if (m_platform) return true;
        m_config = config;
        if (config.productId.empty() || config.sandboxId.empty() || config.deploymentId.empty() || config.clientId.empty()) {
            OKAY_WARN("Epic: the EOS product, sandbox, deployment or client id is missing (Build Settings > Stores)");
            return false;
        }
        if (config.clientSecret.empty()) {
            OKAY_WARN("Epic: the build has no EOS client secret (set OKAY_EOS_CLIENT_SECRET before building)");
            return false;
        }
        InitializeOptions io;
        io.ProductName = m_config.productName.empty() ? "OkaySpace Game" : m_config.productName.c_str();
        io.ProductVersion = m_config.productVersion.empty() ? "1.0" : m_config.productVersion.c_str();
        const Result ir = eosInit(&io);
        if (ir != kSuccess && ir != kAlreadyConfigured) {
            OKAY_WARN("Epic: EOS_Initialize failed (result ", ir, ")");
            return false;
        }
        m_initialized = true;
        m_key = IsHexKey(config.encryptionKey) ? config.encryptionKey : DeriveEosEncryptionKey(config.productId, config.deploymentId);
        std::error_code ec;
        const std::filesystem::path cache = std::filesystem::temp_directory_path(ec) / "OkaySpaceEOS" / config.productId;
        std::filesystem::create_directories(cache, ec);
        m_cacheDir = cache.string();
        PlatformOptions po;
        po.ProductId = m_config.productId.c_str();
        po.SandboxId = m_config.sandboxId.c_str();
        po.DeploymentId = m_config.deploymentId.c_str();
        po.ClientCredentials_.ClientId = m_config.clientId.c_str();
        po.ClientCredentials_.ClientSecret = m_config.clientSecret.c_str();
        po.EncryptionKey = m_key.c_str();
        po.CacheDirectory = m_cacheDir.c_str();
        m_platform = platformCreate(&po);
        if (!m_platform) {
            OKAY_WARN("Epic: EOS_Platform_Create failed (check the product, sandbox, deployment and client ids)");
            return false;
        }
        m_auth = getAuth(m_platform);
        m_connect = getConnect(m_platform);
        m_ach = getAch ? getAch(m_platform) : nullptr;
        m_stats = getStats ? getStats(m_platform) : nullptr;
        m_pds = getPds ? getPds(m_platform) : nullptr;
        m_presence = getPresence ? getPresence(m_platform) : nullptr;
        m_ui = getUi ? getUi(m_platform) : nullptr;
        OKAY_INFO("Epic: connected through the game's ", DefaultEosLibraryName(), " (Store Kit)");
        return true;
    }

    void Shutdown() override {
        if (m_platform) {
            for (auto& t : m_transfers) if (t->request && pdsTransferRelease) pdsTransferRelease(t->request);
            m_transfers.clear();
            platformRelease(m_platform);
            m_platform = nullptr;
            m_auth = m_connect = m_ach = m_stats = m_pds = m_presence = m_ui = nullptr;
        }
        if (m_initialized) { eosShutdown(); m_initialized = false; }
        m_state = EpicLoginState::LoggedOut;
        m_epicUser = nullptr; m_productUser = nullptr;
    }

    void Tick() override {
        if (!m_platform) return;
        platformTick(m_platform);   // runs the completion callbacks below
        for (auto it = m_transfers.begin(); it != m_transfers.end();) {
            if ((*it)->done) {
                if ((*it)->request && pdsTransferRelease) pdsTransferRelease((*it)->request);
                it = m_transfers.erase(it);
            } else ++it;
        }
        if (m_state != EpicLoginState::LoggedIn) return;
        for (const std::string& n : m_wantRead) StartRead(n);
        m_wantRead.clear();
        if (m_statsStale && !m_statsQuerying) QueryStats();
    }

    bool IsAvailable() const override { return m_platform != nullptr; }
    const char* BackendName() const override { return "eos-runtime"; }

    // ---- Account ------------------------------------------------------------------------------------------------
    bool Login() override {
        if (!m_platform) return false;
        if (m_state == EpicLoginState::Pending || m_state == EpicLoginState::LoggedIn) return true;
        AuthCredentials c;
        std::string id, token;
        if (!m_config.launch.exchangeCode.empty()) {
            c.Type = kLoginExchangeCode;
            token = m_config.launch.exchangeCode;
        } else if (!m_config.devAuth.empty()) {
            const std::size_t bar = m_config.devAuth.find('|');
            if (bar == std::string::npos) { OKAY_WARN("Epic: OKAY_EOS_DEV_AUTH must be host:port|credential"); return false; }
            c.Type = kLoginDeveloper;
            id = m_config.devAuth.substr(0, bar);
            token = m_config.devAuth.substr(bar + 1);
        } else {
            return false;
        }
        c.Id = id.empty() ? nullptr : id.c_str();
        c.Token = token.c_str();
        AuthLoginOptions o;
        o.Credentials = &c;
        o.ScopeFlags = kScopeBasicProfile | kScopeFriendsList | kScopePresence;
        m_state = EpicLoginState::Pending;
        OKAY_INFO("Epic: logging in (", c.Type == kLoginExchangeCode ? "the launcher's exchange code" : "the developer login tool", ")");
        authLogin(m_auth, &o, this, &OnAuthLogin);
        return true;
    }
    EpicLoginState LoginState() const override { return m_state; }
    std::string DisplayName() const override {
        return m_config.launch.userName.empty() ? std::string("Player") : m_config.launch.userName;
    }
    std::string AccountId() const override {
        if (!m_epicUser || !accountToString) return {};
        char buf[64] = {0};
        std::int32_t len = (std::int32_t)sizeof buf;
        return accountToString(m_epicUser, buf, &len) == kSuccess ? std::string(buf) : std::string();
    }

    // ---- Achievements -------------------------------------------------------------------------------------------
    bool UnlockAchievement(const std::string& id) override {
        if (id.empty() || !m_platform) return false;
        m_unlocked.insert(id);
        if (m_state == EpicLoginState::LoggedIn) SendUnlocks({id});
        else m_pendingUnlocks.push_back(id);
        return true;
    }
    bool IsAchievementUnlocked(const std::string& id) const override { return m_unlocked.count(id) != 0; }

    // ---- Stats --------------------------------------------------------------------------------------------------
    bool IngestStat(const std::string& name, std::int32_t amount) override {
        if (name.empty() || !m_platform) return false;
        if (m_state == EpicLoginState::LoggedIn) SendIngest(name, amount);
        else m_pendingIngest.emplace_back(name, amount);
        return true;
    }
    std::int32_t GetStat(const std::string& name) const override {
        auto it = m_statValues.find(name);
        return it == m_statValues.end() ? 0 : it->second;
    }

    // ---- Player Data Storage ------------------------------------------------------------------------------------
    bool WriteFile(const std::string& name, const std::string& data) override {
        if (name.empty() || !m_platform) return false;
        m_files[name] = data;
        m_listed.insert(name);
        m_missing.erase(name);
        m_deleted.erase(name);
        m_pendingDeletes.erase(name);
        if (m_state == EpicLoginState::LoggedIn) StartWrite(name, data);
        else m_pendingWrites[name] = data;
        return true;
    }
    std::string ReadFile(const std::string& name) const override {
        auto it = m_files.find(name);
        if (it != m_files.end()) return it->second;
        if (!name.empty() && m_platform && !m_missing.count(name) && !m_reading.count(name)) m_wantRead.insert(name);
        return {};
    }
    bool HasFile(const std::string& name) const override { return m_files.count(name) != 0 || m_listed.count(name) != 0; }
    bool DeleteFile(const std::string& name) override {
        if (name.empty() || !m_platform) return false;
        const bool had = HasFile(name);
        m_files.erase(name);
        m_listed.erase(name);
        m_missing.insert(name);
        m_deleted.insert(name);
        m_pendingWrites.erase(name);
        m_wantRead.erase(name);
        if (m_state == EpicLoginState::LoggedIn) StartDelete(name);
        else m_pendingDeletes.insert(name);
        return had;
    }

    // ---- Presence / overlay -------------------------------------------------------------------------------------
    void SetPresence(const std::string& text) override {
        m_presenceText = CutUtf8(text, 255);
        if (m_state == EpicLoginState::LoggedIn) SendPresence();
        else m_presenceDirty = true;
    }
    std::string Presence() const override { return m_presenceText; }
    void ShowFriends() override {
        if (m_state != EpicLoginState::LoggedIn || !m_ui || !showFriends || !m_epicUser) return;
        UiShowFriendsOptions o;
        o.LocalUserId = m_epicUser;
        showFriends(m_ui, &o, this, &OnShowFriends);
    }

private:
    // ---- login steps ----------------------------------------------------------------------------------------------
    static void OnAuthLogin(const AuthLoginCallbackInfo* i) {
        auto* self = static_cast<DynamicEpicService*>(i->ClientData);
        if (!self || !self->m_platform) return;
        if (i->ResultCode != kSuccess || !i->LocalUserId) {
            OKAY_WARN("Epic: the Epic account login failed (result ", i->ResultCode, "); the game goes on offline");
            self->m_state = EpicLoginState::Failed;
            return;
        }
        self->m_epicUser = i->LocalUserId;
        // Game services use a product user: log in to Connect with the Epic account's id token.
        AuthCopyIdTokenOptions to;
        to.AccountId = i->LocalUserId;
        AuthIdToken* token = nullptr;
        if (self->copyIdToken(self->m_auth, &to, &token) != kSuccess || !token || !token->JsonWebToken) {
            OKAY_WARN("Epic: no id token for the Epic account; game services stay offline");
            self->m_state = EpicLoginState::Failed;
            return;
        }
        ConnectCredentials cc;
        cc.Token = token->JsonWebToken;
        cc.Type = kExternalEpicIdToken;
        ConnectLoginOptions co;
        co.Credentials = &cc;
        self->connectLogin(self->m_connect, &co, self, &OnConnectLogin);
        if (self->idTokenRelease) self->idTokenRelease(token);
    }
    static void OnConnectLogin(const ConnectLoginCallbackInfo* i) {
        auto* self = static_cast<DynamicEpicService*>(i->ClientData);
        if (!self || !self->m_platform) return;
        if (i->ResultCode == kSuccess && i->LocalUserId) { self->LoggedIn(i->LocalUserId); return; }
        if (i->ResultCode == kInvalidUser && i->ContinuanceToken_ && self->connectCreate) {
            // First time this Epic account plays the game: make its product user.
            ConnectCreateUserOptions o;
            o.ContinuanceToken_ = i->ContinuanceToken_;
            self->connectCreate(self->m_connect, &o, self, &OnCreateUser);
            return;
        }
        OKAY_WARN("Epic: the game services login failed (result ", i->ResultCode, ")");
        self->m_state = EpicLoginState::Failed;
    }
    static void OnCreateUser(const ConnectCreateUserCallbackInfo* i) {
        auto* self = static_cast<DynamicEpicService*>(i->ClientData);
        if (!self || !self->m_platform) return;
        if (i->ResultCode == kSuccess && i->LocalUserId) { self->LoggedIn(i->LocalUserId); return; }
        OKAY_WARN("Epic: making the game services user failed (result ", i->ResultCode, ")");
        self->m_state = EpicLoginState::Failed;
    }
    void LoggedIn(ProductUserId user) {
        m_productUser = user;
        m_state = EpicLoginState::LoggedIn;
        OKAY_INFO("Epic: logged in as '", DisplayName(), "'");
        // What the server already has ...
        if (m_ach && achQuery) {
            AchQueryPlayerOptions o;
            o.TargetUserId = user; o.LocalUserId = user;
            achQuery(m_ach, &o, this, &OnAchQuery);
        }
        QueryStats();
        if (m_pds && pdsList) {
            PdsQueryListOptions o;
            o.LocalUserId = user;
            pdsList(m_pds, &o, this, &OnFileList);
        }
        // ... and what the game did before the login finished.
        if (!m_pendingUnlocks.empty()) { SendUnlocks(m_pendingUnlocks); m_pendingUnlocks.clear(); }
        for (const auto& p : m_pendingIngest) SendIngest(p.first, p.second);
        m_pendingIngest.clear();
        for (const auto& w : m_pendingWrites) StartWrite(w.first, w.second);
        m_pendingWrites.clear();
        for (const auto& d : m_pendingDeletes) StartDelete(d);
        m_pendingDeletes.clear();
        if (m_presenceDirty) { SendPresence(); m_presenceDirty = false; }
    }

    // ---- achievements ---------------------------------------------------------------------------------------------
    void SendUnlocks(const std::vector<std::string>& ids) {
        if (!m_ach || !achUnlock || ids.empty()) return;
        std::vector<const char*> p;
        for (const auto& s : ids) p.push_back(s.c_str());
        AchUnlockOptions o;
        o.UserId = m_productUser;
        o.AchievementIds = p.data();
        o.AchievementsCount = (std::uint32_t)p.size();
        achUnlock(m_ach, &o, this, &OnUnlocked);
    }
    static void OnUnlocked(const AchUnlockCallbackInfo* i) {
        if (i->ResultCode != kSuccess) OKAY_WARN("Epic: an achievement unlock failed (result ", i->ResultCode, ")");
    }
    static void OnAchQuery(const AchQueryPlayerCallbackInfo* i) {
        auto* self = static_cast<DynamicEpicService*>(i->ClientData);
        if (!self || !self->m_platform) return;
        if (i->ResultCode != kSuccess || !self->achCount || !self->achCopy) return;
        AchGetCountOptions co;
        co.UserId = self->m_productUser;
        const std::uint32_t n = self->achCount(self->m_ach, &co);
        for (std::uint32_t k = 0; k < n; ++k) {
            AchCopyByIndexOptions o;
            o.TargetUserId = self->m_productUser; o.LocalUserId = self->m_productUser; o.AchievementIndex = k;
            PlayerAchievement* a = nullptr;
            if (self->achCopy(self->m_ach, &o, &a) != kSuccess || !a) continue;
            if (a->AchievementId && a->UnlockTime != kTimeUndefined) self->m_unlocked.insert(a->AchievementId);
            if (self->achRelease) self->achRelease(a);
        }
    }

    // ---- stats ----------------------------------------------------------------------------------------------------
    void SendIngest(const std::string& name, std::int32_t amount) {
        if (!m_stats || !statIngest) return;
        StatIngestData d;
        d.StatName = name.c_str();
        d.IngestAmount = amount;
        StatIngestOptions o;
        o.LocalUserId = m_productUser; o.TargetUserId = m_productUser;
        o.Stats = &d; o.StatsCount = 1;
        statIngest(m_stats, &o, this, &OnIngested);
    }
    static void OnIngested(const StatIngestCallbackInfo* i) {
        auto* self = static_cast<DynamicEpicService*>(i->ClientData);
        if (!self || !self->m_platform) return;
        if (i->ResultCode != kSuccess) { OKAY_WARN("Epic: sending a stat failed (result ", i->ResultCode, ")"); return; }
        self->m_statsStale = true;   // read the totals back (the portal decides how each stat adds up)
    }
    void QueryStats() {
        if (!m_stats || !statQuery || !m_productUser) return;
        StatQueryOptions o;
        o.LocalUserId = m_productUser; o.TargetUserId = m_productUser;
        m_statsStale = false;
        m_statsQuerying = true;
        statQuery(m_stats, &o, this, &OnStatsQueried);
    }
    static void OnStatsQueried(const StatQueryCallbackInfo* i) {
        auto* self = static_cast<DynamicEpicService*>(i->ClientData);
        if (!self || !self->m_platform) return;
        self->m_statsQuerying = false;
        if (i->ResultCode != kSuccess || !self->statCount || !self->statCopy) return;
        StatGetCountOptions co;
        co.TargetUserId = self->m_productUser;
        const std::uint32_t n = self->statCount(self->m_stats, &co);
        for (std::uint32_t k = 0; k < n; ++k) {
            StatCopyByIndexOptions o;
            o.TargetUserId = self->m_productUser; o.StatIndex = k;
            Stat* s = nullptr;
            if (self->statCopy(self->m_stats, &o, &s) != kSuccess || !s) continue;
            if (s->Name) self->m_statValues[s->Name] = s->Value;
            if (self->statRelease) self->statRelease(s);
        }
    }

    // ---- player data storage --------------------------------------------------------------------------------------
    void StartWrite(const std::string& name, const std::string& data) {
        if (!m_pds || !pdsWrite) return;
        auto t = std::make_unique<Transfer>();
        t->self = this; t->name = name; t->data = data; t->write = true;
        PdsWriteOptions o;
        o.LocalUserId = m_productUser;
        o.Filename = t->name.c_str();
        o.ChunkLengthBytes = kChunk;
        o.WriteFileDataCallback = &OnWriteData;
        Transfer* raw = t.get();
        m_transfers.push_back(std::move(t));
        raw->request = pdsWrite(m_pds, &o, raw, &OnTransferDone);
    }
    void StartRead(const std::string& name) {
        if (!m_pds || !pdsRead || m_reading.count(name) || m_files.count(name)) return;
        auto t = std::make_unique<Transfer>();
        t->self = this; t->name = name;
        PdsReadOptions o;
        o.LocalUserId = m_productUser;
        o.Filename = t->name.c_str();
        o.ReadChunkLengthBytes = kChunk;
        o.ReadFileDataCallback = &OnReadData;
        Transfer* raw = t.get();
        m_transfers.push_back(std::move(t));
        m_reading.insert(name);
        raw->request = pdsRead(m_pds, &o, raw, &OnTransferDone);
    }
    void StartDelete(const std::string& name) {
        if (!m_pds || !pdsDelete) return;
        PdsDeleteOptions o;
        o.LocalUserId = m_productUser;
        o.Filename = name.c_str();
        pdsDelete(m_pds, &o, this, &OnDeleted);
    }
    static std::int32_t OnWriteData(const PdsWriteDataInfo* i, void* out, std::uint32_t* written) {
        auto* t = static_cast<Transfer*>(i->ClientData);
        if (!t || !out || !written) return kWriteFail;
        const std::size_t left = t->data.size() - t->at;
        const std::size_t n = left < i->DataBufferLengthBytes ? left : i->DataBufferLengthBytes;
        if (n) std::memcpy(out, t->data.data() + t->at, n);
        *written = (std::uint32_t)n;
        t->at += n;
        return t->at >= t->data.size() ? kWriteComplete : kWriteContinue;
    }
    static std::int32_t OnReadData(const PdsReadDataInfo* i) {
        auto* t = static_cast<Transfer*>(i->ClientData);
        if (!t) return kReadFail;
        if (t->data.empty() && i->TotalFileSizeBytes) t->data.reserve(i->TotalFileSizeBytes);
        if (i->DataChunk && i->DataChunkLengthBytes) t->data.append(static_cast<const char*>(i->DataChunk), i->DataChunkLengthBytes);
        return kReadContinue;
    }
    static void OnTransferDone(const PdsTransferDoneInfo* i) {
        auto* t = static_cast<Transfer*>(i->ClientData);
        if (!t || t->done) return;
        t->done = true;
        DynamicEpicService* self = t->self;
        if (t->write) {
            if (i->ResultCode != kSuccess) OKAY_WARN("Epic: saving cloud file '", t->name, "' failed (result ", i->ResultCode, ")");
            return;
        }
        self->m_reading.erase(t->name);
        if (i->ResultCode == kSuccess) {
            if (!self->m_files.count(t->name)) self->m_files[t->name] = t->data;   // a newer local write wins
            self->m_listed.insert(t->name);
        } else {
            self->m_missing.insert(t->name);   // not asked again until it is written or listed
            if (i->ResultCode != kNotFound) OKAY_WARN("Epic: reading cloud file '", t->name, "' failed (result ", i->ResultCode, ")");
        }
    }
    static void OnDeleted(const PdsDeleteInfo* i) {
        if (i->ResultCode != kSuccess && i->ResultCode != kNotFound) OKAY_WARN("Epic: deleting a cloud file failed (result ", i->ResultCode, ")");
    }
    static void OnFileList(const PdsQueryListInfo* i) {
        auto* self = static_cast<DynamicEpicService*>(i->ClientData);
        if (!self || !self->m_platform || i->ResultCode != kSuccess || !self->pdsMeta) return;
        for (std::uint32_t k = 0; k < i->FileCount; ++k) {
            PdsCopyMetaAtIndexOptions o;
            o.LocalUserId = self->m_productUser; o.Index = k;
            PdsFileMetadata* m = nullptr;
            if (self->pdsMeta(self->m_pds, &o, &m) != kSuccess || !m) continue;
            if (m->Filename && !self->m_deleted.count(m->Filename)) {   // not one deleted this session
                const std::string name = m->Filename;
                self->m_listed.insert(name);
                self->m_missing.erase(name);
                // Saves are small: fetch them now so CloudSave.Read has them without waiting.
                if (i->FileCount <= kPrefetchFiles && m->FileSizeBytes <= kPrefetchBytes) self->m_wantRead.insert(name);
            }
            if (self->pdsMetaRelease) self->pdsMetaRelease(m);
        }
    }

    // ---- presence -------------------------------------------------------------------------------------------------
    void SendPresence() {
        if (!m_presence || !presCreateMod || !presSetText || !presSet || !m_epicUser) return;
        PresenceCreateModOptions co;
        co.LocalUserId = m_epicUser;
        Handle mod = nullptr;
        if (presCreateMod(m_presence, &co, &mod) != kSuccess || !mod) return;
        if (presSetStatus) { PresenceSetStatusOptions so; presSetStatus(mod, &so); }
        PresenceSetRichTextOptions to;
        to.RichText = m_presenceText.c_str();
        presSetText(mod, &to);
        PresenceSetOptions o;
        o.LocalUserId = m_epicUser;
        o.PresenceModificationHandle = mod;
        presSet(m_presence, &o, this, &OnPresenceSet);
        if (presModRelease) presModRelease(mod);
    }
    static void OnPresenceSet(const PresenceSetInfo* i) {
        if (i->ResultCode != kSuccess) OKAY_WARN("Epic: setting presence failed (result ", i->ResultCode, ")");
    }
    static void OnShowFriends(const UiShowFriendsInfo* i) {
        if (i->ResultCode != kSuccess) OKAY_INFO("Epic: the overlay could not open (result ", i->ResultCode, ")");
    }

    detail::RuntimeLibrary m_lib;
    EpicConfig m_config;
    std::string m_key, m_cacheDir;
    bool m_initialized = false;
    Handle m_platform = nullptr, m_auth = nullptr, m_connect = nullptr, m_ach = nullptr, m_stats = nullptr, m_pds = nullptr,
           m_presence = nullptr, m_ui = nullptr;
    EpicAccountId m_epicUser = nullptr;
    ProductUserId m_productUser = nullptr;
    EpicLoginState m_state = EpicLoginState::LoggedOut;

    std::set<std::string> m_unlocked;
    std::vector<std::string> m_pendingUnlocks;
    std::map<std::string, std::int32_t> m_statValues;
    std::vector<std::pair<std::string, std::int32_t>> m_pendingIngest;
    bool m_statsStale = false, m_statsQuerying = false;
    std::map<std::string, std::string> m_files, m_pendingWrites;
    std::set<std::string> m_listed, m_reading, m_pendingDeletes, m_deleted;
    mutable std::set<std::string> m_wantRead, m_missing;
    std::list<std::unique_ptr<Transfer>> m_transfers;
    std::string m_presenceText;
    bool m_presenceDirty = false;

    FnInitialize eosInit = nullptr; FnShutdown eosShutdown = nullptr; FnPlatformCreate platformCreate = nullptr;
    FnHandle platformTick = nullptr, platformRelease = nullptr, pdsTransferRelease = nullptr, presModRelease = nullptr;
    FnGetIface getAuth = nullptr, getConnect = nullptr, getAch = nullptr, getStats = nullptr, getPds = nullptr,
               getPresence = nullptr, getUi = nullptr;
    FnAuthLogin authLogin = nullptr; FnCopyIdToken copyIdToken = nullptr; FnIdTokenRelease idTokenRelease = nullptr;
    FnAccountToString accountToString = nullptr; FnConnectLogin connectLogin = nullptr; FnConnectCreate connectCreate = nullptr;
    FnAchUnlock achUnlock = nullptr; FnAchQuery achQuery = nullptr; FnAchCount achCount = nullptr; FnAchCopy achCopy = nullptr;
    FnAchRelease achRelease = nullptr;
    FnStatIngest statIngest = nullptr; FnStatQuery statQuery = nullptr; FnStatCount statCount = nullptr;
    FnStatCopy statCopy = nullptr; FnStatRelease statRelease = nullptr;
    FnPdsWrite pdsWrite = nullptr; FnPdsRead pdsRead = nullptr; FnPdsDelete pdsDelete = nullptr; FnPdsList pdsList = nullptr;
    FnPdsMeta pdsMeta = nullptr; FnPdsMetaRelease pdsMetaRelease = nullptr;
    FnPresenceCreateMod presCreateMod = nullptr; FnPresenceSetStatus presSetStatus = nullptr;
    FnPresenceSetText presSetText = nullptr; FnPresenceSet presSet = nullptr; FnShowFriends showFriends = nullptr;
};

}  // namespace

std::unique_ptr<IEpicService> CreateDynamicEpicService(const std::string& libraryPath) {
    auto s = std::make_unique<DynamicEpicService>();
    if (!s->Load(libraryPath.empty() ? std::string(DefaultEosLibraryName()) : libraryPath)) return nullptr;
    return s;
}

}  // namespace okay
