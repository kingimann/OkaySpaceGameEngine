#include "ImGuiPlatformFeed.hpp"
#include <cstdio>
int main(){ ImGui::CreateContext(); ImGuiIO& io=ImGui::GetIO(); io.IniFilename=nullptr; unsigned char*p;int w,h; io.Fonts->GetTexDataAsRGBA32(&p,&w,&h);
 io.DisplaySize=ImVec2(800,600); io.DeltaTime=0.016f;
 io.AddKeyEvent(ImGuiKey_LeftCtrl,true); io.AddKeyEvent(ImGuiKey_C,true); io.AddMouseWheelEvent(0,3);
 for(int i=0;i<3;i++){ ImGui::NewFrame(); std::printf("f%d C=%d lctrl=%d ctrl=%d mod=%d wheel=%f\n",i,ImGui::IsKeyDown(ImGuiKey_C),ImGui::IsKeyDown(ImGuiKey_LeftCtrl),io.KeyCtrl,ImGui::IsKeyDown(ImGuiMod_Ctrl),io.MouseWheel); ImGui::Render(); } }
