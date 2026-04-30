//
// Created by Nicholas on 01/05/26.
//

#include "display.h"

#include <format>

// callback functions

void GLFW_CALLBACK::errorCallback(int error_code, const char* description)
{
    if (!globalLogger) return;

    const std::exception e ((std::format("GLFW Error {}: {}", error_code, description).data()));
    globalLogger->error(e);
}

void GLFW_CALLBACK::windowMoveCallback(GLFWwindow* window, const int xpos, const int ypos)
{
    auto* display = reinterpret_cast<GLFWDisplay*>(window);
    GLFWCallback cb = {
        WINDOW_POSITION
    };
    cb.vector2_i = {xpos, ypos};
    display->getWindowEventManager().processWindowEvent(cb);

}
void GLFW_CALLBACK::windowSizeCallback(GLFWwindow* window, const int width, const int height)
{
    auto* display = reinterpret_cast<GLFWDisplay*>(window);
    GLFWCallback cb = {
        WINDOW_SIZE
    };
    cb.vector2_i = {width, height};
    display->getWindowEventManager().processWindowEvent(cb);
}

void GLFW_CALLBACK::windowCloseCallback(GLFWwindow* window)
{
    auto* display = reinterpret_cast<GLFWDisplay*>(window);
    constexpr GLFWCallback cb = {
        WINDOW_CLOSE
    };
    display->getWindowEventManager().processWindowEvent(cb);
}

void GLFW_CALLBACK::windowRefreshCallback(GLFWwindow* window)
{
    auto* display = reinterpret_cast<GLFWDisplay*>(window);
    constexpr GLFWCallback cb = {
        WINDOW_REFRESH
    };
    display->getWindowEventManager().processWindowEvent(cb);
}

void GLFW_CALLBACK::windowFocusCallback(GLFWwindow* window, const int focused)
{
    auto* display = reinterpret_cast<GLFWDisplay*>(window);
    GLFWCallback cb = {
        WINDOW_FOCUS
    };
    cb.togglable = { focused };
    display->getWindowEventManager().processWindowEvent(cb);
}

void GLFW_CALLBACK::windowIconifyCallback(GLFWwindow* window, const int iconified)
{
    auto* display = reinterpret_cast<GLFWDisplay*>(window);
    GLFWCallback cb = {
        WINDOW_ICONIFY
    };
    cb.togglable = { iconified };
    display->getWindowEventManager().processWindowEvent(cb);
}

void GLFW_CALLBACK::windowMaximiseCallback(GLFWwindow* window, const int maximized)
{
    auto* display = reinterpret_cast<GLFWDisplay*>(window);
    GLFWCallback cb = {
        WINDOW_MAXIMIZE
    };
    cb.togglable = { maximized };
    display->getWindowEventManager().processWindowEvent(cb);
}

void GLFW_CALLBACK::windowContentScaleCallback(GLFWwindow* window, const float xscale, const float yscale)
{
    auto* display = reinterpret_cast<GLFWDisplay*>(window);
    GLFWCallback cb = {
        WINDOW_CONTENT_SCALE
    };
    cb.vector2_f = { xscale, yscale };
    display->getWindowEventManager().processWindowEvent(cb);
}

void GLFW_CALLBACK::frameBufferResizeCallback(GLFWwindow* window, const int width, const int height)
{
    auto* display = reinterpret_cast<GLFWDisplay*>(window);
    GLFWCallback cb = {
        FRAMEBUFFER_SIZE
    };
    cb.vector2_i = {width, height};
    display->getWindowEventManager().processWindowEvent(cb);
}

void GLFW_CALLBACK::keyboardInputCallback(GLFWwindow* window, const int key, const int scancode, const int action, const int mods)
{
    auto* display = reinterpret_cast<GLFWDisplay*>(window);
    GLFWCallback cb = {
        KEYBOARD_INPUT
    };
    cb.vector4_i = { key, scancode, action, mods };
    display->getWindowEventManager().processWindowEvent(cb);
}

void GLFW_CALLBACK::charInputCallback(GLFWwindow* window, const unsigned int codepoint)
{
    auto* display = reinterpret_cast<GLFWDisplay*>(window);
    GLFWCallback cb = {
        CHAR_INPUT
    };
    cb.togglable = { static_cast<int>(codepoint) };
    display->getWindowEventManager().processWindowEvent(cb);
}

void GLFW_CALLBACK::moddedCharInputCallback(GLFWwindow* window, const unsigned int codepoint, const int mods)
{
    auto* display = reinterpret_cast<GLFWDisplay*>(window);
    GLFWCallback cb = {
        CHAR_WITH_MODS
    };
    cb.vector2_i = { static_cast<int>(codepoint), mods };
    display->getWindowEventManager().processWindowEvent(cb);
}

void GLFW_CALLBACK::mouseButtonPressCallback(GLFWwindow* window, const int button, const int action, const int mods)
{
    auto* display = reinterpret_cast<GLFWDisplay*>(window);
    GLFWCallback cb = {
        MOUSE_BUTTON
    };
    cb.vector3_i = { button, action, mods };
    display->getWindowEventManager().processWindowEvent(cb);
}

void GLFW_CALLBACK::mouseMovementCallback(GLFWwindow* window, const double xpos, const double ypos)
{
    auto* display = reinterpret_cast<GLFWDisplay*>(window);
    GLFWCallback cb = {
        MOUSE_MOVEMENT
    };
    cb.vector2_f = {static_cast<float>(xpos), static_cast<float>(ypos)};
    display->getWindowEventManager().processWindowEvent(cb);
}

void GLFW_CALLBACK::cursorBorderCrossCallback(GLFWwindow* window, const int entered)
{
    auto* display = reinterpret_cast<GLFWDisplay*>(window);
    GLFWCallback cb = {
        CURSOR_BORDER_CROSS
    };
    cb.togglable = {entered};
    display->getWindowEventManager().processWindowEvent(cb);
}

void GLFW_CALLBACK::mouseScrollWheelCallback(GLFWwindow* window, const double xoffset, const double yoffset)
{
    auto* display = reinterpret_cast<GLFWDisplay*>(window);
    GLFWCallback cb = {
        MOUSE_SCROL_WHEEL
    };
    cb.vector2_f = {static_cast<float>(xoffset), static_cast<float>(yoffset)};
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

// display

GLFWDisplay::GLFWDisplay(const Vector2i dims, ScreenSettings&& settings, const std::string&& title)
{
    settings.dims = dims;
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

    {
        std::unique_lock lock(m_screenSettings.lock());
        dims = m_screenSettings.ref().dims;
    }

    m_window = glfwCreateWindow(dims.x, dims.y, m_title.c_str(), nullptr, nullptr);
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

    //
    glfwSetJoystickCallback(GLFW_CALLBACK::joystickConnectionCallback);
    glfwSetMonitorCallback(GLFW_CALLBACK::monitorPlugCallback);
    glfwSetErrorCallback(GLFW_CALLBACK::errorCallback);
}

void GLFWDisplay::closeDisplay()
{
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

void GLFWDisplay::show()
{

}

void GLFWDisplay::hide()
{

}

void GLFWDisplay::centerCursor()
{

}

int GLFWDisplay::refreshRate()
{
    return m_screenSettings.get().refresh_rate;
}

WindowEventManager& GLFWDisplay::getWindowEventManager()
{
    return m_windowEventManager;
}
