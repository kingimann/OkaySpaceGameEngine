#pragma once
#include "okay/Render/Renderer.hpp"
#include <memory>
#include <string>

namespace okay {

class Scene;

/// The engine entry point: owns the renderer and runs the game loop, pumping
/// Time, Input, Update, and Render each frame. Comparable to Unity's player
/// loop, distilled to its essentials.
class Application {
public:
    struct Config {
        std::string title  = "OkaySpace";
        int   width        = 80;     // renderer width (chars for the console backend)
        int   height       = 30;     // renderer height
        float targetFps    = 30.0f;  // frame pacing; <= 0 means run uncapped
        int   maxFrames    = 0;      // 0 = run until quit; >0 = stop after N frames
        bool  clearScreen  = true;   // console backend clears the terminal each frame
    };

    explicit Application(Config config);
    ~Application();

    /// Provide a custom renderer backend (defaults to the console renderer).
    void SetRenderer(std::unique_ptr<IRenderer> renderer);

    /// Run the main loop against the given scene until it quits.
    void Run(Scene& scene);

    /// Request a graceful shutdown of the loop.
    void Quit() { m_running = false; }

    IRenderer& renderer() { return *m_renderer; }
    const Config& config() const { return m_config; }

private:
    Config m_config;
    std::unique_ptr<IRenderer> m_renderer;
    bool m_running = false;
};

} // namespace okay
