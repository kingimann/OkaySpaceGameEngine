#pragma once
// The seam between the engine (native) and the managed C# side. Everything that crosses it is plain
// data (integers, floats, UTF-8 text, function pointers), so the managed assembly never needs a
// native library and no marshalling is involved. Tests plug in a fake backend; the real one is
// CSharpHostfxr.cpp (OKAY_WITH_CSHARP).
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace okay::csharp {

/// Function table the host fills and hands to OkayScripting.dll once (Host.Init). Blittable: only
/// integers, floats and pointers. Versioned by `size`; the managed side refuses a table that is too small.
/// `go` is a GameObject instance id (0 = none). Text in/out is UTF-8, out buffers are caller-sized.
extern "C" {
/// One script value crossing the seam (the binding layer's value, ScriptBindings.hpp), blittable.
/// kind: 0 none, 1 bool (x), 2 number (x), 3 text, 4 vector (x y z), 5 colour / Vector4 (x y z w),
/// 6 object (id), 7 component handle (id, classKey, index), 8 rotation (x y z w; in only, the engine
/// turns it into Euler degrees). `text` is UTF-8; text the engine returns stays valid until its next call.
struct OkayValue {
    std::int32_t kind;
    std::uint32_t classKey;
    std::uint32_t index;
    std::uint32_t pad;
    double x, y, z, w;
    std::uint64_t id;
    const char* text;
};
struct OkayNativeApi {
    std::uint32_t size;
    std::uint32_t version;
    void   (*Log)(int level, const char* msg);                              // 0 info, 1 warning, 2 error
    // what: 0 position, 1 local position, 2 euler degrees, 3 scale, 4 rotation (x y z w), 5 local euler. Returns 1 on success.
    int    (*GetVec)(std::uint64_t go, int what, float* out);
    int    (*SetVec)(std::uint64_t go, int what, const float* in);
    // what: 0 deltaTime, 1 time, 2 timeScale, 3 unscaledDeltaTime, 4 fixedDeltaTime, 5 frameCount
    double (*GetTime)(int what);
    void   (*SetTimeScale)(float scale);
    int    (*InputKey)(const char* name, int mode);                          // mode 0 held, 1 down, 2 up; -1 unknown key
    int    (*InputMouse)(int button, int mode);
    void   (*MousePosition)(float* out2);
    float  (*InputAxis)(const char* name);
    std::uint64_t (*Find)(const char* name);
    std::uint64_t (*Create)(const char* name);
    void   (*Destroy)(std::uint64_t go, float delaySeconds);
    std::uint64_t (*Instantiate)(std::uint64_t prefab, const float* pos3OrNull, const float* rotXyzwOrNull);
    int    (*IsAlive)(std::uint64_t go);
    int    (*GetName)(std::uint64_t go, char* buf, int cap);                 // length (excluding NUL), -1 if dead
    int    (*GetActive)(std::uint64_t go);
    void   (*SetActive)(std::uint64_t go, int active);
    int    (*Raycast)(const float* origin3, const float* dir3, float maxDistance,
                      float* outPoint3, float* outNormal3, float* outDistance, std::uint64_t* outGo);
    int    (*HasComponent)(std::uint64_t go, const char* type);
    int    (*GetProperty)(std::uint64_t go, const char* path, char* buf, int cap);   // text of the value, -1 if missing
    int    (*SetProperty)(std::uint64_t go, const char* path, const char* text);     // 1 ok
    // ---- version 2: the generic binding layer (scriptbind::*), which the generated C# classes call.
    // Results: 1 ok, 0 not found (no such class / member / component), -1 error (text in err).
    int    (*FindComponent)(std::uint64_t go, const char* cls, int index, OkayValue* out);
    int    (*AddComponent)(std::uint64_t go, const char* cls, OkayValue* out, char* err, int errCap);
    int    (*GetMember)(const OkayValue* handle, const char* name, OkayValue* out, char* err, int errCap);
    int    (*SetMember)(const OkayValue* handle, const char* name, const OkayValue* value, char* err, int errCap);
    int    (*CallMethod)(const OkayValue* handle, const char* name, const OkayValue* args, int argc, OkayValue* out,
                         char* err, int errCap);
    int    (*ClassName)(std::uint32_t classKey, char* buf, int cap);              // length, -1 unknown
};
}
constexpr std::uint32_t kNativeApiVersion = 2;
OkayNativeApi* GetNativeApi();   ///< the engine's table (filled once)

/// One script instance on the managed side.
class IManagedScript {
public:
    virtual ~IManagedScript() = default;
    virtual void SetOwner(std::uint64_t goInstanceId) = 0;
    /// Does the script class override `method` (Start, Update, OnTriggerEnter ...)?
    virtual bool Has(const std::string& method) = 0;
    /// Call it. `num` is the float argument (Update: delta time), `str` the text argument, `obj` the other object's
    /// instance id for collision and trigger messages (0 = none; then `str` names it).
    /// An exception inside the script comes back as false with a plain error line "line N: Type: message".
    virtual bool Invoke(const std::string& method, double num, const std::string& str, std::uint64_t obj,
                        std::string* error) = 0;
    /// Public fields as lines "name<TAB>type<TAB>value\n" (type: float, int, bool, string, Vector3, other).
    virtual std::string Fields() = 0;
    virtual bool SetField(const std::string& name, const std::string& value, std::string* error) = 0;
};

/// Compiles C# source and creates script instances. The real one needs .NET; tests use a fake.
class IManagedBackend {
public:
    virtual ~IManagedBackend() = default;
    /// Compile `source` (cached by content hash). On failure `errors` holds "line N: message" lines.
    virtual bool Compile(const std::string& source, std::string* assemblyPath, std::vector<std::string>* errors) = 0;
    /// Instantiate `className` from the compiled assembly (a collectible load context on the real backend).
    virtual std::unique_ptr<IManagedScript> Create(const std::string& assemblyPath, const std::string& className,
                                                   std::string* error) = 0;
};

/// The backend scripts use: a test override if set, else the real one (null + `error` when C# is
/// not enabled in this build or no runtime is installed).
IManagedBackend* GetManagedBackend(std::string* error);
void SetManagedBackendForTests(IManagedBackend* backend);   ///< nullptr restores the default
IManagedBackend* CreateHostfxrBackend(std::string* error);  ///< CSharpHostfxr.cpp

/// The C# classes for every component the binding table knows (scriptbind::AllClasses and the
/// PropertyRegistry): one `public partial class <Name> : Component` per class with its properties and
/// methods, plus the name -> constructor table. Generated at run time from the same table OkayScript uses,
/// so the C# API is never written a second time by hand. CSharpApiGen.cpp.
std::string GenerateApiSource();
/// What GenerateApiSource covered, for tests and the build log.
struct ApiGenStats { int classes = 0, methods = 0, properties = 0, skipped = 0; };
ApiGenStats LastApiGenStats();

} // namespace okay::csharp
