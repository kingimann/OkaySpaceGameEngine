#pragma once
#include "okay/Scripting/ScriptVM.hpp"

namespace okay {

/// C# scripting backend. Scripts are compiled with the Mono C# compiler (mcs)
/// and executed through an embedded Mono runtime. Only compiled when the engine
/// is built with -DOKAY_WITH_CSHARP=ON and the Mono development packages are
/// present; `mcs` and the Mono runtime must also be available at run time.
///
/// A script defines a `public static class Script` with `static void Start()`
/// and `static void Update(float dt)`, and calls the host API via the provided
/// `Okay` class (Okay.Move, Okay.Rotate, Okay.Time, Okay.Key, Okay.Print, ...).
class CSharpScriptVM : public IScriptVM {
public:
    CSharpScriptVM();
    ~CSharpScriptVM() override;

    const char* Language() const override { return "csharp"; }
    bool Load(const std::string& source, std::string* error = nullptr) override;
    void Bind(ScriptHost* host) override;
    void CallStart() override;
    void CallUpdate(float deltaTime) override;
    vs::VsValue GetGlobal(const std::string& name) const override;

private:
    struct State;
    State* m_state = nullptr;
    ScriptHost* m_host = nullptr;
    bool m_loaded = false;
};

} // namespace okay
