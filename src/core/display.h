//
// Created by Nicholas on 01/05/26.
//

#pragma once
#include "interfaces.h"
#include "async/lock.h"
#include "GLFW/glfw3.h"
#include "logging/logger.h"

struct ScreenSettings
{

    Vector2i dims;
    int refresh_rate = 0;
    int fov = 0;
    float view_near = 0.1f;
    float view_far = 1000.0f;
    bool mouse_visible_in_bounds = true;
    bool center_mouse_when_hidden = false;

};

struct Monitor
{

    GLFWmonitor* monitor = nullptr;
    const GLFWvidmode* videoMode = nullptr;
    std::string name;

    Monitor() = default;
    Monitor(GLFWmonitor* m, const GLFWvidmode* vm, std::string title) :
        monitor(m),
        videoMode(vm),
        name(std::move(title))
    {};

};

struct MonitorEnvironment
{
    ArrayList<Monitor> monitors;
};

inline MonitorEnvironment monitorEnvironment;

void initialiseMonitorEnvironment();

class GLFWDisplay : public IDisplayManager {

    constexpr static const char* DEFAULT_TITLE = "Hello World!";

    GLFWwindow* m_window = nullptr;
    WindowEventManager m_windowEventManager;
    Syncronized<ScreenSettings> m_screenSettings;
    std::string m_title;

    // STATE
    std::atomic<bool> m_visible = false;
    std::atomic<bool> m_shouldClose = false;
    std::atomic<bool> m_focused = false;
    std::atomic<bool> m_iconified = false;

    void _set_internal_callbacks();

public:

    GLFWDisplay() : GLFWDisplay({}, DEFAULT_TITLE) {}
    GLFWDisplay(ScreenSettings&& settings, const std::string& title);
    ~GLFWDisplay() override;
    GLFWDisplay(const GLFWDisplay&) = delete;
    GLFWDisplay& operator=(const GLFWDisplay&) = delete;

    bool createGLFWDisplay();

    bool createDisplay() override;
    void closeDisplay() override;
    Vector2i getScreenSize() override;
    bool isClosed() override;
    bool isFocused() override;
    void show() override;
    void hide() override;
    void centerCursor() override;
    int refreshRate() override;
    WindowEventManager& getWindowEventManager() override;

};

namespace GLFW_CALLBACK
{
    inline Logger* globalLogger;
    void setGlobalLogger(Logger* l);
    void errorCallback(int error_code, const char* description);

    void windowMoveCallback(GLFWwindow* window, int xpos, int ypos);
    void windowSizeCallback(GLFWwindow* window, int width, int height);
    void windowCloseCallback(GLFWwindow* window);
    void windowRefreshCallback(GLFWwindow* window);
    void windowFocusCallback(GLFWwindow* window, int focused);
    void windowIconifyCallback(GLFWwindow* window, int iconified);
    void windowMaximiseCallback(GLFWwindow* window, int maximized);
    void windowContentScaleCallback(GLFWwindow* window, float xscale, float yscale);
    void frameBufferResizeCallback(GLFWwindow* window, int width, int height);
    void keyboardInputCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    void charInputCallback(GLFWwindow* window, unsigned int codepoint);
    void moddedCharInputCallback(GLFWwindow* window, unsigned int codepoint, int mods);
    void mouseButtonPressCallback(GLFWwindow* window, int button, int action, int mods);
    void mouseMovementCallback(GLFWwindow* window, double xpos, double ypos);
    void cursorBorderCrossCallback(GLFWwindow* window, int entered);
    void mouseScrollWheelCallback(GLFWwindow* window, double xoffset, double yoffset);
    void joystickConnectionCallback(int jid, int event);
    void monitorPlugCallback(GLFWmonitor* monitor, int event);

}

#undef glfw_callback


