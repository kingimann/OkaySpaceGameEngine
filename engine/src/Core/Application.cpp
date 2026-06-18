#include "okay/Core/Application.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Render/ConsoleRenderer.hpp"

#include <chrono>
#include <thread>

namespace okay {

using clock = std::chrono::steady_clock;

Application::Application(Config config) : m_config(std::move(config)) {
    m_renderer = std::make_unique<ConsoleRenderer>(
        m_config.width, m_config.height, m_config.clearScreen);
    OKAY_INFO("Application '", m_config.title, "' created (",
              m_config.width, "x", m_config.height, ")");
}

Application::~Application() = default;

void Application::SetRenderer(std::unique_ptr<IRenderer> renderer) {
    if (renderer) m_renderer = std::move(renderer);
}

void Application::Run(Scene& scene) {
    m_running = true;
    Input::BeginSession();

    OKAY_INFO("Entering main loop for scene '", scene.Name(), "'");
    scene.Start();

    const float targetFrameTime =
        m_config.targetFps > 0.0f ? 1.0f / m_config.targetFps : 0.0f;

    auto previous = clock::now();
    int frame = 0;

    while (m_running) {
        auto now = clock::now();
        float dt = std::chrono::duration<float>(now - previous).count();
        previous = now;
        // Clamp the first/huge frames so physics and movement stay sane.
        if (dt > 0.25f) dt = targetFrameTime > 0.0f ? targetFrameTime : 0.016f;

        Input::Poll();
        Time::Advance(dt);

        scene.Update(Time::DeltaTime());
        scene.Render(*m_renderer);

        if (Input::GetKeyDown('q')) m_running = false;

        ++frame;
        if (m_config.maxFrames > 0 && frame >= m_config.maxFrames) m_running = false;

        if (targetFrameTime > 0.0f) {
            auto frameEnd = clock::now();
            float elapsed = std::chrono::duration<float>(frameEnd - now).count();
            float remaining = targetFrameTime - elapsed;
            if (remaining > 0.0f)
                std::this_thread::sleep_for(std::chrono::duration<float>(remaining));
        }
    }

    Input::EndSession();
    OKAY_INFO("Main loop exited after ", frame, " frames");
}

} // namespace okay
