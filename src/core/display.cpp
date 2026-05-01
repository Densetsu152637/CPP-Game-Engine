//
// Created by Nicholas on 01/05/26.
//

#include "display.h"

#include <format>

void initialiseMonitorEnvironment()
{
    // clears any old content
    monitorEnvironment.monitors.clear();

    int monitorCount;
    GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
    for (int i = 0; i < monitorCount; i++)
    {
        GLFWmonitor* monitor = monitors[i];
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        std::string name = glfwGetMonitorName(monitor);
        monitorEnvironment.monitors.emplace(monitor, mode, name);
    }
}

GLFWDisplay::GLFWDisplay(ScreenSettings&& settings, const std::string& title)
{
    m_screenSettings = settings;
    m_title = title;
}

GLFWDisplay::~GLFWDisplay() = default;

bool GLFWDisplay::createDisplay()
{
    return createGLFWDisplay();
}

bool GLFWDisplay::createGLFWDisplay()
{
    Vector2i dims;
    const Monitor& target = monitorEnvironment.monitors[0];
    GLFWmonitor* targetMonitor = target.monitor; // primary monitor

    {
        std::unique_lock lock(m_screenSettings.lock());
        dims = m_screenSettings.ref().dims;
    }

    if (dims.x == 0 && dims.y == 0)
    {
        // get half of the displays if dims is invalid
        const GLFWvidmode* mode = target.videoMode;
        dims = { mode->width / 2, mode->height / 2 };
    }

    m_window = glfwCreateWindow(
        dims.x, dims.y,
        m_title.c_str(),
        targetMonitor, nullptr
    );

    if (!m_window) return m_window;

    _set_internal_callbacks();

    return m_window != nullptr;
}

void GLFWDisplay::_set_internal_callbacks()
{
    glfwSetWindowUserPointer(m_window, this);

    // callbacks
    glfwSetWindowPosCallback(m_window, GLFW_CALLBACK::windowMoveCallback);
    glfwSetWindowSizeCallback(m_window, GLFW_CALLBACK::windowSizeCallback);
    glfwSetWindowCloseCallback(m_window, GLFW_CALLBACK::windowCloseCallback);
    glfwSetWindowRefreshCallback(m_window, GLFW_CALLBACK::windowRefreshCallback);
    glfwSetWindowFocusCallback(m_window, GLFW_CALLBACK::windowFocusCallback);
    glfwSetWindowIconifyCallback(m_window, GLFW_CALLBACK::windowIconifyCallback);
    glfwSetWindowMaximizeCallback(m_window, GLFW_CALLBACK::windowMaximiseCallback);
    glfwSetWindowContentScaleCallback(m_window, GLFW_CALLBACK::windowContentScaleCallback);
    glfwSetFramebufferSizeCallback(m_window, GLFW_CALLBACK::frameBufferResizeCallback);
    glfwSetKeyCallback(m_window, GLFW_CALLBACK::keyboardInputCallback);
    glfwSetCharCallback(m_window, GLFW_CALLBACK::charInputCallback);
    glfwSetCharModsCallback(m_window, GLFW_CALLBACK::moddedCharInputCallback);
    glfwSetMouseButtonCallback(m_window, GLFW_CALLBACK::mouseButtonPressCallback);
    glfwSetCursorPosCallback(m_window, GLFW_CALLBACK::mouseMovementCallback);
    glfwSetCursorEnterCallback(m_window, GLFW_CALLBACK::cursorBorderCrossCallback);
    glfwSetScrollCallback(m_window, GLFW_CALLBACK::mouseScrollWheelCallback);

    // stuff that is unfortunately global
    glfwSetJoystickCallback(GLFW_CALLBACK::joystickConnectionCallback);
    glfwSetMonitorCallback(GLFW_CALLBACK::monitorPlugCallback);
    glfwSetErrorCallback(GLFW_CALLBACK::errorCallback);
}

void GLFWDisplay::closeDisplay()
{

    m_visible = false;
    m_shouldClose = false;
    m_focused = false;
    m_iconified = false;

    glfwDestroyWindow(m_window);
}

Vector2i GLFWDisplay::getScreenSize()
{
    return m_screenSettings.get().dims;
}

bool GLFWDisplay::isClosed()
{
    return glfwWindowShouldClose(m_window);
}

bool GLFWDisplay::isFocused()
{
    return m_focused;
};

void GLFWDisplay::show()
{
    glfwShowWindow(m_window);
}

void GLFWDisplay::hide()
{
    glfwHideWindow(m_window);
}

void GLFWDisplay::centerCursor()
{
    if (!m_visible || m_shouldClose || !m_focused || m_iconified) return;

    const Vector2i dims = getScreenSize();
    glfwSetCursorPos(m_window, dims.x / 2.0, dims.y / 2.0);
}

int GLFWDisplay::refreshRate()
{
    return m_screenSettings.get().refresh_rate;
}

WindowEventManager& GLFWDisplay::getWindowEventManager()
{
    return m_windowEventManager;
}

// callback functions

void GLFW_CALLBACK::setGlobalLogger(Logger* l)
{
    if (!l) return;
    globalLogger = l;
}

void GLFW_CALLBACK::errorCallback(int error_code, const char* description)
{
    if (!globalLogger) return;

    const std::exception e ((std::format("GLFW Error {}: {}", error_code, description).data()));
    globalLogger->error(e);
}

void GLFW_CALLBACK::windowMoveCallback(GLFWwindow* window, const int xpos, const int ypos)
{
    auto* display = static_cast<GLFWDisplay*>(glfwGetWindowUserPointer(window));
    GLFWCallback cb = {
        WINDOW_POSITION
    };
    cb.vector2_i = {xpos, ypos};

    if (display->isFocused())
        display->getWindowEventManager().processWindowEvent(cb);
}

void GLFW_CALLBACK::windowSizeCallback(GLFWwindow* window, const int width, const int height)
{
    auto* display = static_cast<GLFWDisplay*>(glfwGetWindowUserPointer(window));
    GLFWCallback cb = {
        WINDOW_SIZE
    };
    cb.vector2_i = {width, height};

    if (display->isFocused())
        display->getWindowEventManager().processWindowEvent(cb);
}

void GLFW_CALLBACK::windowCloseCallback(GLFWwindow* window)
{
    auto* display = static_cast<GLFWDisplay*>(glfwGetWindowUserPointer(window));
    constexpr GLFWCallback cb = {
        WINDOW_CLOSE
    };
    display->getWindowEventManager().processWindowEvent(cb);
}

void GLFW_CALLBACK::windowRefreshCallback(GLFWwindow* window)
{
    auto* display = static_cast<GLFWDisplay*>(glfwGetWindowUserPointer(window));
    constexpr GLFWCallback cb = {
        WINDOW_REFRESH
    };

    if (display->isFocused())
        display->getWindowEventManager().processWindowEvent(cb);
}

void GLFW_CALLBACK::windowFocusCallback(GLFWwindow* window, const int focused)
{
    auto* display = static_cast<GLFWDisplay*>(glfwGetWindowUserPointer(window));
    GLFWCallback cb = {
        WINDOW_FOCUS
    };
    cb.togglable = { focused };

    if (display->isFocused())
        display->getWindowEventManager().processWindowEvent(cb);
}

void GLFW_CALLBACK::windowIconifyCallback(GLFWwindow* window, const int iconified)
{
    auto* display = static_cast<GLFWDisplay*>(glfwGetWindowUserPointer(window));
    GLFWCallback cb = {
        WINDOW_ICONIFY
    };
    cb.togglable = { iconified };

    if (display->isFocused())
        display->getWindowEventManager().processWindowEvent(cb);
}

void GLFW_CALLBACK::windowMaximiseCallback(GLFWwindow* window, const int maximized)
{
    auto* display = static_cast<GLFWDisplay*>(glfwGetWindowUserPointer(window));
    GLFWCallback cb = {
        WINDOW_MAXIMIZE
    };
    cb.togglable = { maximized };

    if (display->isFocused())
        display->getWindowEventManager().processWindowEvent(cb);
}

void GLFW_CALLBACK::windowContentScaleCallback(GLFWwindow* window, const float xscale, const float yscale)
{
    auto* display = static_cast<GLFWDisplay*>(glfwGetWindowUserPointer(window));
    GLFWCallback cb = {
        WINDOW_CONTENT_SCALE
    };
    cb.vector2_f = { xscale, yscale };

    if (display->isFocused())
        display->getWindowEventManager().processWindowEvent(cb);
}

void GLFW_CALLBACK::frameBufferResizeCallback(GLFWwindow* window, const int width, const int height)
{
    auto* display = static_cast<GLFWDisplay*>(glfwGetWindowUserPointer(window));
    GLFWCallback cb = {
        FRAMEBUFFER_SIZE
    };
    cb.vector2_i = {width, height};

    if (display->isFocused())
        display->getWindowEventManager().processWindowEvent(cb);
}

void GLFW_CALLBACK::keyboardInputCallback(GLFWwindow* window, const int key, const int scancode, const int action, const int mods)
{
    auto* display = static_cast<GLFWDisplay*>(glfwGetWindowUserPointer(window));
    GLFWCallback cb = {
        KEYBOARD_INPUT
    };
    cb.vector4_i = { key, scancode, action, mods };

    if (display->isFocused())
        display->getWindowEventManager().processWindowEvent(cb);
}

void GLFW_CALLBACK::charInputCallback(GLFWwindow* window, const unsigned int codepoint)
{
    auto* display = static_cast<GLFWDisplay*>(glfwGetWindowUserPointer(window));
    GLFWCallback cb = {
        CHAR_INPUT
    };
    cb.togglable = { static_cast<int>(codepoint) };

    if (display->isFocused())
        display->getWindowEventManager().processWindowEvent(cb);
}

void GLFW_CALLBACK::moddedCharInputCallback(GLFWwindow* window, const unsigned int codepoint, const int mods)
{
    auto* display = static_cast<GLFWDisplay*>(glfwGetWindowUserPointer(window));
    GLFWCallback cb = {
        CHAR_WITH_MODS
    };
    cb.vector2_i = { static_cast<int>(codepoint), mods };

    if (display->isFocused())
        display->getWindowEventManager().processWindowEvent(cb);
}

void GLFW_CALLBACK::mouseButtonPressCallback(GLFWwindow* window, const int button, const int action, const int mods)
{
    auto* display = static_cast<GLFWDisplay*>(glfwGetWindowUserPointer(window));
    GLFWCallback cb = {
        MOUSE_BUTTON
    };
    cb.vector3_i = { button, action, mods };

    if (display->isFocused())
        display->getWindowEventManager().processWindowEvent(cb);
}

void GLFW_CALLBACK::mouseMovementCallback(GLFWwindow* window, const double xpos, const double ypos)
{
    auto* display = static_cast<GLFWDisplay*>(glfwGetWindowUserPointer(window));
    GLFWCallback cb = {
        MOUSE_MOVEMENT
    };
    cb.vector2_f = {static_cast<float>(xpos), static_cast<float>(ypos)};

    if (display->isFocused())
        display->getWindowEventManager().processWindowEvent(cb);
}

void GLFW_CALLBACK::cursorBorderCrossCallback(GLFWwindow* window, const int entered)
{
    auto* display = static_cast<GLFWDisplay*>(glfwGetWindowUserPointer(window));
    GLFWCallback cb = {
        CURSOR_BORDER_CROSS
    };
    cb.togglable = {entered};

    if (display->isFocused())
        display->getWindowEventManager().processWindowEvent(cb);
}

void GLFW_CALLBACK::mouseScrollWheelCallback(GLFWwindow* window, const double xoffset, const double yoffset)
{
    auto* display = static_cast<GLFWDisplay*>(glfwGetWindowUserPointer(window));
    GLFWCallback cb = {
        MOUSE_SCROL_WHEEL
    };
    cb.vector2_f = {static_cast<float>(xoffset), static_cast<float>(yoffset)};

    if (display->isFocused())
        display->getWindowEventManager().processWindowEvent(cb);
}

void GLFW_CALLBACK::joystickConnectionCallback(int jid, int event)
{
    //JOYSTICK_CONNECTION TODO
}

void GLFW_CALLBACK::monitorPlugCallback(GLFWmonitor* monitor, int event)
{
    //MONITOR_PLUG TODO
}
