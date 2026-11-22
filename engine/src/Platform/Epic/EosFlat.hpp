#pragma once
// The few shapes of Epic Online Services' plain C interface that the Store Kit's run-time Epic backend
// (DynamicEpicService.cpp) passes to the developer's EOSSDK-Win64-Shipping.dll, and that the test stand-in library
// (tests/fake_eos_sdk.cpp) receives.
//
// Written by us from Epic's public EOS documentation (the C API reference pages for Platform, Auth, Connect,
// Achievements, Stats, Player Data Storage, Presence and UI). No SDK header was copied or consulted while writing it.
// Every structure starts with an ApiVersion number; the SDK accepts older versions of a structure, so we always
// send the oldest version that has every field we use.
//
// UNCONFIRMED (written from memory of the documentation, never checked against the real SDK): the ApiVersion
// numbers below, the order of fields in EOS_Platform_Options after DeploymentId, the numeric values of
// EOS_EResult beyond Success, EOS_ECT_EPIC_ID_TOKEN (16) and the read/write result values. Each is one constant
// here, so a correction is a one-line change. docs/release/epic_release.md lists them.
#include <cstdint>

namespace okay::eosflat {

using Result = std::int32_t;                 // EOS_EResult
using Bool = std::int32_t;                   // EOS_Bool (1 = true)
using Handle = void*;                        // EOS_HPlatform, EOS_HAuth, ... (opaque)
using EpicAccountId = void*;                 // EOS_EpicAccountId (opaque)
using ProductUserId = void*;                 // EOS_ProductUserId (opaque)
using ContinuanceToken = void*;              // EOS_ContinuanceToken (opaque)

constexpr Result kSuccess = 0;
constexpr Result kInvalidUser = 3;           // Connect login: no product user yet (create one with the token)
constexpr Result kAlreadyConfigured = 15;    // EOS_Initialize was already called in this process
constexpr Result kNotFound = 18;

constexpr std::int64_t kTimeUndefined = -1;  // EOS_STATS_TIME_UNDEFINED, EOS_ACHIEVEMENTS_ACHIEVEMENT_UNLOCKTIME_UNDEFINED

// ---- Initialize / Platform ------------------------------------------------------------------------------------
struct InitializeOptions {                   // EOS_InitializeOptions, ApiVersion 4
    std::int32_t ApiVersion = 4;
    void* AllocateMemoryFunction = nullptr;
    void* ReallocateMemoryFunction = nullptr;
    void* ReleaseMemoryFunction = nullptr;
    const char* ProductName = nullptr;
    const char* ProductVersion = nullptr;
    void* Reserved = nullptr;
    void* SystemInitializeOptions = nullptr;
    void* OverrideThreadAffinity = nullptr;
};

struct ClientCredentials {                   // EOS_Platform_ClientCredentials
    const char* ClientId = nullptr;
    const char* ClientSecret = nullptr;
};

struct PlatformOptions {                     // EOS_Platform_Options, ApiVersion 12
    std::int32_t ApiVersion = 12;
    void* Reserved = nullptr;
    const char* ProductId = nullptr;
    const char* SandboxId = nullptr;
    ClientCredentials ClientCredentials_;
    Bool bIsServer = 0;
    const char* EncryptionKey = nullptr;     // 64 hex digits; Player Data Storage needs it
    const char* OverrideCountryCode = nullptr;
    const char* OverrideLocaleCode = nullptr;
    const char* DeploymentId = nullptr;
    std::uint64_t Flags = 0;
    const char* CacheDirectory = nullptr;
    std::uint32_t TickBudgetInMilliseconds = 0;
    const void* RTCOptions = nullptr;
    void* IntegratedPlatformOptionsContainerHandle = nullptr;
};

// ---- Auth (Epic account) --------------------------------------------------------------------------------------
constexpr std::int32_t kLoginExchangeCode = 1;   // EOS_LCT_ExchangeCode
constexpr std::int32_t kLoginDeveloper = 4;      // EOS_LCT_Developer (the Developer Authentication Tool)
constexpr std::int32_t kScopeBasicProfile = 0x1, kScopeFriendsList = 0x2, kScopePresence = 0x4;

struct AuthCredentials {                     // EOS_Auth_Credentials, ApiVersion 3
    std::int32_t ApiVersion = 3;
    const char* Id = nullptr;                // developer tool: "host:port"; exchange code: unused
    const char* Token = nullptr;             // the exchange code, or the developer tool's credential name
    std::int32_t Type = 0;                   // EOS_ELoginCredentialType
    void* SystemAuthCredentialsOptions = nullptr;
    std::int32_t ExternalType = 0;
};
struct AuthLoginOptions {                    // EOS_Auth_LoginOptions, ApiVersion 2
    std::int32_t ApiVersion = 2;
    const AuthCredentials* Credentials = nullptr;
    std::int32_t ScopeFlags = 0;
};
struct AuthLoginCallbackInfo {               // EOS_Auth_LoginCallbackInfo (the first fields)
    Result ResultCode;
    void* ClientData;
    EpicAccountId LocalUserId;
};
using AuthLoginCallback = void (*)(const AuthLoginCallbackInfo*);

struct AuthCopyIdTokenOptions {              // EOS_Auth_CopyIdTokenOptions, ApiVersion 1
    std::int32_t ApiVersion = 1;
    EpicAccountId AccountId = nullptr;
};
struct AuthIdToken {                         // EOS_Auth_IdToken
    std::int32_t ApiVersion;
    EpicAccountId AccountId;
    const char* JsonWebToken;
};

// ---- Connect (the product user every game service uses) -------------------------------------------------------
constexpr std::int32_t kExternalEpicIdToken = 16;   // EOS_ECT_EPIC_ID_TOKEN

struct ConnectCredentials {                  // EOS_Connect_Credentials, ApiVersion 1
    std::int32_t ApiVersion = 1;
    const char* Token = nullptr;
    std::int32_t Type = 0;                   // EOS_EExternalCredentialType
};
struct ConnectLoginOptions {                 // EOS_Connect_LoginOptions, ApiVersion 2
    std::int32_t ApiVersion = 2;
    const ConnectCredentials* Credentials = nullptr;
    const void* UserLoginInfo = nullptr;
};
struct ConnectLoginCallbackInfo {            // EOS_Connect_LoginCallbackInfo
    Result ResultCode;
    void* ClientData;
    ProductUserId LocalUserId;
    ContinuanceToken ContinuanceToken_;
};
using ConnectLoginCallback = void (*)(const ConnectLoginCallbackInfo*);

struct ConnectCreateUserOptions {            // EOS_Connect_CreateUserOptions, ApiVersion 1
    std::int32_t ApiVersion = 1;
    ContinuanceToken ContinuanceToken_ = nullptr;
};
struct ConnectCreateUserCallbackInfo {       // EOS_Connect_CreateUserCallbackInfo
    Result ResultCode;
    void* ClientData;
    ProductUserId LocalUserId;
};
using ConnectCreateUserCallback = void (*)(const ConnectCreateUserCallbackInfo*);

// ---- Achievements ---------------------------------------------------------------------------------------------
struct AchUnlockOptions {                    // EOS_Achievements_UnlockAchievementsOptions, ApiVersion 1
    std::int32_t ApiVersion = 1;
    ProductUserId UserId = nullptr;
    const char** AchievementIds = nullptr;
    std::uint32_t AchievementsCount = 0;
};
struct AchUnlockCallbackInfo {               // EOS_Achievements_OnUnlockAchievementsCompleteCallbackInfo
    Result ResultCode;
    void* ClientData;
    ProductUserId UserId;
    std::uint32_t AchievementsCount;
};
using AchUnlockCallback = void (*)(const AchUnlockCallbackInfo*);

struct AchQueryPlayerOptions {               // EOS_Achievements_QueryPlayerAchievementsOptions, ApiVersion 2
    std::int32_t ApiVersion = 2;
    ProductUserId TargetUserId = nullptr;
    ProductUserId LocalUserId = nullptr;
};
struct AchQueryPlayerCallbackInfo {          // EOS_Achievements_OnQueryPlayerAchievementsCompleteCallbackInfo
    Result ResultCode;
    void* ClientData;
    ProductUserId TargetUserId;
    ProductUserId LocalUserId;
};
using AchQueryPlayerCallback = void (*)(const AchQueryPlayerCallbackInfo*);

struct AchGetCountOptions {                  // EOS_Achievements_GetPlayerAchievementCountOptions, ApiVersion 1
    std::int32_t ApiVersion = 1;
    ProductUserId UserId = nullptr;
};
struct AchCopyByIndexOptions {               // EOS_Achievements_CopyPlayerAchievementByIndexOptions, ApiVersion 2
    std::int32_t ApiVersion = 2;
    ProductUserId TargetUserId = nullptr;
    std::uint32_t AchievementIndex = 0;
    ProductUserId LocalUserId = nullptr;
};
struct PlayerAchievement {                   // EOS_Achievements_PlayerAchievement (the first fields)
    std::int32_t ApiVersion;
    const char* AchievementId;
    double Progress;
    std::int64_t UnlockTime;                 // kTimeUndefined while locked
};

// ---- Stats ----------------------------------------------------------------------------------------------------
struct StatIngestData {                      // EOS_Stats_IngestData, ApiVersion 1
    std::int32_t ApiVersion = 1;
    const char* StatName = nullptr;
    std::int32_t IngestAmount = 0;
};
struct StatIngestOptions {                   // EOS_Stats_IngestStatOptions, ApiVersion 3
    std::int32_t ApiVersion = 3;
    ProductUserId LocalUserId = nullptr;
    const StatIngestData* Stats = nullptr;
    std::uint32_t StatsCount = 0;
    ProductUserId TargetUserId = nullptr;
};
struct StatIngestCallbackInfo {              // EOS_Stats_IngestStatCompleteCallbackInfo
    Result ResultCode;
    void* ClientData;
    ProductUserId LocalUserId;
    ProductUserId TargetUserId;
};
using StatIngestCallback = void (*)(const StatIngestCallbackInfo*);

struct StatQueryOptions {                    // EOS_Stats_QueryStatsOptions, ApiVersion 3
    std::int32_t ApiVersion = 3;
    ProductUserId LocalUserId = nullptr;
    std::int64_t StartTime = kTimeUndefined;
    std::int64_t EndTime = kTimeUndefined;
    const char** StatNames = nullptr;        // nullptr = every stat
    std::uint32_t StatNamesCount = 0;
    ProductUserId TargetUserId = nullptr;
};
struct StatQueryCallbackInfo {               // EOS_Stats_OnQueryStatsCompleteCallbackInfo
    Result ResultCode;
    void* ClientData;
    ProductUserId LocalUserId;
    ProductUserId TargetUserId;
};
using StatQueryCallback = void (*)(const StatQueryCallbackInfo*);

struct StatGetCountOptions {                 // EOS_Stats_GetStatCountOptions, ApiVersion 1
    std::int32_t ApiVersion = 1;
    ProductUserId TargetUserId = nullptr;
};
struct StatCopyByIndexOptions {              // EOS_Stats_CopyStatByIndexOptions, ApiVersion 1
    std::int32_t ApiVersion = 1;
    ProductUserId TargetUserId = nullptr;
    std::uint32_t StatIndex = 0;
};
struct Stat {                                // EOS_Stats_Stat
    std::int32_t ApiVersion;
    const char* Name;
    std::int64_t StartTime;
    std::int64_t EndTime;
    std::int32_t Value;
};

// ---- Player Data Storage (cloud saves) ------------------------------------------------------------------------
constexpr std::int32_t kReadContinue = 1, kReadFail = 2;              // EOS_PlayerDataStorage_EReadResult
constexpr std::int32_t kWriteContinue = 1, kWriteComplete = 2, kWriteFail = 3;   // EOS_PlayerDataStorage_EWriteResult

struct PdsReadDataInfo {                     // EOS_PlayerDataStorage_ReadFileDataCallbackInfo
    void* ClientData;
    ProductUserId LocalUserId;
    const char* Filename;
    std::uint32_t TotalFileSizeBytes;
    Bool bIsLastChunk;
    std::uint32_t DataChunkLengthBytes;
    const void* DataChunk;
};
using PdsReadDataCallback = std::int32_t (*)(const PdsReadDataInfo*);
struct PdsWriteDataInfo {                    // EOS_PlayerDataStorage_WriteFileDataCallbackInfo
    void* ClientData;
    ProductUserId LocalUserId;
    const char* Filename;
    std::uint32_t DataBufferLengthBytes;
};
using PdsWriteDataCallback = std::int32_t (*)(const PdsWriteDataInfo*, void* outBuffer, std::uint32_t* outWritten);

struct PdsReadOptions {                      // EOS_PlayerDataStorage_ReadFileOptions, ApiVersion 1
    std::int32_t ApiVersion = 1;
    ProductUserId LocalUserId = nullptr;
    const char* Filename = nullptr;
    std::uint32_t ReadChunkLengthBytes = 0;
    PdsReadDataCallback ReadFileDataCallback = nullptr;
    void* FileTransferProgressCallback = nullptr;
};
struct PdsWriteOptions {                     // EOS_PlayerDataStorage_WriteFileOptions, ApiVersion 1
    std::int32_t ApiVersion = 1;
    ProductUserId LocalUserId = nullptr;
    const char* Filename = nullptr;
    std::uint32_t ChunkLengthBytes = 0;
    PdsWriteDataCallback WriteFileDataCallback = nullptr;
    void* FileTransferProgressCallback = nullptr;
};
struct PdsTransferDoneInfo {                 // EOS_PlayerDataStorage_ReadFileCallbackInfo / WriteFileCallbackInfo
    Result ResultCode;
    void* ClientData;
    ProductUserId LocalUserId;
    const char* Filename;
};
using PdsTransferDoneCallback = void (*)(const PdsTransferDoneInfo*);

struct PdsDeleteOptions {                    // EOS_PlayerDataStorage_DeleteFileOptions, ApiVersion 1
    std::int32_t ApiVersion = 1;
    ProductUserId LocalUserId = nullptr;
    const char* Filename = nullptr;
};
struct PdsDeleteInfo {                       // EOS_PlayerDataStorage_DeleteFileCallbackInfo
    Result ResultCode;
    void* ClientData;
    ProductUserId LocalUserId;
};
using PdsDeleteCallback = void (*)(const PdsDeleteInfo*);

struct PdsQueryListOptions {                 // EOS_PlayerDataStorage_QueryFileListOptions, ApiVersion 1
    std::int32_t ApiVersion = 1;
    ProductUserId LocalUserId = nullptr;
};
struct PdsQueryListInfo {                    // EOS_PlayerDataStorage_QueryFileListCallbackInfo
    Result ResultCode;
    void* ClientData;
    ProductUserId LocalUserId;
    std::uint32_t FileCount;
};
using PdsQueryListCallback = void (*)(const PdsQueryListInfo*);
struct PdsCopyMetaAtIndexOptions {           // EOS_PlayerDataStorage_CopyFileMetadataAtIndexOptions, ApiVersion 1
    std::int32_t ApiVersion = 1;
    ProductUserId LocalUserId = nullptr;
    std::uint32_t Index = 0;
};
struct PdsFileMetadata {                     // EOS_PlayerDataStorage_FileMetadata (the first fields)
    std::int32_t ApiVersion;
    std::uint32_t FileSizeBytes;
    const char* MD5Hash;
    const char* Filename;
};

// ---- Presence -------------------------------------------------------------------------------------------------
constexpr std::int32_t kPresenceOnline = 1;   // EOS_Presence_EStatus

struct PresenceCreateModOptions {            // EOS_Presence_CreatePresenceModificationOptions, ApiVersion 1
    std::int32_t ApiVersion = 1;
    EpicAccountId LocalUserId = nullptr;
};
struct PresenceSetStatusOptions {            // EOS_PresenceModification_SetStatusOptions, ApiVersion 1
    std::int32_t ApiVersion = 1;
    std::int32_t Status = kPresenceOnline;
};
struct PresenceSetRichTextOptions {          // EOS_PresenceModification_SetRawRichTextOptions, ApiVersion 1
    std::int32_t ApiVersion = 1;
    const char* RichText = nullptr;          // at most 255 characters
};
struct PresenceSetOptions {                  // EOS_Presence_SetPresenceOptions, ApiVersion 1
    std::int32_t ApiVersion = 1;
    EpicAccountId LocalUserId = nullptr;
    Handle PresenceModificationHandle = nullptr;
};
struct PresenceSetInfo {                     // EOS_Presence_SetPresenceCallbackInfo
    Result ResultCode;
    void* ClientData;
    EpicAccountId LocalUserId;
};
using PresenceSetCallback = void (*)(const PresenceSetInfo*);

// ---- UI -------------------------------------------------------------------------------------------------------
struct UiShowFriendsOptions {                // EOS_UI_ShowFriendsOptions, ApiVersion 1
    std::int32_t ApiVersion = 1;
    EpicAccountId LocalUserId = nullptr;
};
struct UiShowFriendsInfo {                   // EOS_UI_ShowFriendsCallbackInfo
    Result ResultCode;
    void* ClientData;
    EpicAccountId LocalUserId;
};
using UiShowFriendsCallback = void (*)(const UiShowFriendsInfo*);

}  // namespace okay::eosflat
