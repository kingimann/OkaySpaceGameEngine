// OkayShader: not written yet (the test in tests/test_okayshader.cpp comes first).
#include "okay/Render/OkayShader.hpp"

namespace okay {
namespace oshader {

const char* TypeName(Type) { return "?"; }
int Width(Type) { return 0; }
std::string Diagnostic::ToString(const std::string&) const { return message; }
std::string Compiled::FirstError(const std::string&) const { return errors.empty() ? std::string() : errors[0].message; }
Compiled Compile(const std::string&) { Compiled c; c.errors.push_back({1, 1, "not implemented"}); return c; }
void RunCpu(const CpuProgram&, const SurfaceIn&, const float*, SampleFn, const void*, SurfaceOut&) {}
int CpuInstructionCount(const CpuProgram&) { return 0; }
std::vector<float> ResolveProperties(const Compiled&, const std::string&) { return {}; }
std::string ResolveTexture(const Compiled&, int, const std::string&) { return {}; }
bool GetParam(const std::string&, const std::string&, float*, int) { return false; }
std::string SetParam(const std::string& t, const std::string&, const float*, int) { return t; }
std::string GetTextParam(const std::string&, const std::string&) { return {}; }
std::string SetTextParam(const std::string& t, const std::string&, const std::string&) { return t; }
ShaderFile LoadShaderFile(const std::string&, int) { ShaderFile f; f.compiled = std::make_shared<Compiled>(); return f; }
void ClearShaderFileCache() {}

} // namespace oshader
} // namespace okay
