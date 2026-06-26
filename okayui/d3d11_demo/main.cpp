// Standalone Direct3D 11 demo for OkayUI (Windows only). Opens a window, creates a
// D3D11 device + swapchain, and renders an OkayUI panel through the raw DX11 backend
// every frame. This is the runnable proof that OkayUI works on real DirectX — no SDL.
#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>

#include "okay/UI/OkayUI.hpp"
#include "okay/UI/OkayUI_D3D11.hpp"

// Per-frame keyboard capture for the OkayUI text field (filled by WndProc).
static char g_typed[64];
static int  g_typedLen = 0;
static bool g_backspace = false;

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_CHAR:
        if (w == '\b') { g_backspace = true; }                 // Backspace
        else if (w >= 32 && w < 127 && g_typedLen < 62) {      // printable ASCII
            g_typed[g_typedLen++] = (char)w; g_typed[g_typedLen] = '\0';
        }
        return 0;
    }
    return DefWindowProc(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    const int W = 540, H = 460;
    WNDCLASSA wc; ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = WndProc; wc.hInstance = hInst;
    wc.lpszClassName = "OkayUID3D11Demo"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassA(&wc);
    RECT r = {0, 0, W, H}; AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowA(wc.lpszClassName, "OkayUI - Direct3D 11 demo",
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              r.right - r.left, r.bottom - r.top, nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, SW_SHOW);

    DXGI_SWAP_CHAIN_DESC scd; ZeroMemory(&scd, sizeof(scd));
    scd.BufferCount = 1;
    scd.BufferDesc.Width = W; scd.BufferDesc.Height = H;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd; scd.SampleDesc.Count = 1; scd.Windowed = TRUE;

    ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr; IDXGISwapChain* sc = nullptr;
    D3D_FEATURE_LEVEL fl;
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                nullptr, 0, D3D11_SDK_VERSION, &scd, &sc, &dev, &fl, &ctx))) {
        MessageBoxA(hwnd, "Failed to create a Direct3D 11 device.", "OkayUI", MB_OK);
        return 1;
    }
    ID3D11Texture2D* back = nullptr;
    sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back);
    ID3D11RenderTargetView* rtv = nullptr;
    dev->CreateRenderTargetView(back, nullptr, &rtv);
    back->Release();

    OkayUI::D3D11Backend ui;
    if (!ui.Init(dev)) { MessageBoxA(hwnd, "OkayUI D3D11 backend init failed.", "OkayUI", MB_OK); return 1; }

    bool  sound = true; float vol = 60.0f; int mode = 0; char name[24] = "Player";
    bool running = true;
    while (running) {
        g_typedLen = 0; g_typed[0] = '\0'; g_backspace = false;
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg); DispatchMessage(&msg);
        }
        if (!running) break;

        D3D11_VIEWPORT vp; ZeroMemory(&vp, sizeof(vp));
        vp.Width = (FLOAT)W; vp.Height = (FLOAT)H; vp.MaxDepth = 1.0f;
        ctx->RSSetViewports(1, &vp);
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        const float clear[4] = {0.10f, 0.11f, 0.13f, 1.0f};
        ctx->ClearRenderTargetView(rtv, clear);

        POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt);
        OkayUI::Input in;
        in.mouseX = (float)pt.x; in.mouseY = (float)pt.y;
        in.mouseDown = (GetKeyState(VK_LBUTTON) & 0x8000) != 0;
        in.text = g_typedLen ? g_typed : nullptr;
        in.backspace = g_backspace;

        OkayUI::BeginFrame(in);
        OkayUI::Begin("Direct3D 11", 40, 40, 460, 360);
        OkayUI::Text("OkayUI rendered via raw DirectX 11");
        OkayUI::Separator();
        OkayUI::InputText("Name", name, (int)sizeof(name));
        OkayUI::RadioButton("Easy", &mode, 0);
        OkayUI::SameLine();
        OkayUI::RadioButton("Hard", &mode, 1);
        OkayUI::SliderFloat("Volume", &vol, 0.0f, 100.0f);
        OkayUI::Checkbox("Sound", &sound);
        OkayUI::Separator();
        OkayUI::Text("Health"); OkayUI::ProgressBar(0.8f);
        OkayUI::Spacing();
        if (OkayUI::Button("Quit")) running = false;
        OkayUI::End();
        OkayUI::EndFrameData();

        ui.Render(ctx, W, H);
        sc->Present(1, 0);
    }

    ui.Shutdown();
    if (rtv) rtv->Release();
    if (sc)  sc->Release();
    if (ctx) ctx->Release();
    if (dev) dev->Release();
    return 0;
}

#else
int main() { return 0; }   // non-Windows: nothing to do
#endif
