//
// Created by Nicholas on 01/05/26.
//

#pragma once
#include "interfaces.h"
#include "async/lock.h"
#include "GLFW/glfw3.h"

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

class GLFWDisplay : public IDisplayManager {

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

    GLFWDisplay(const Vector2i dims) : GLFWDisplay(dims, {}, "Hello World!") {}
    GLFWDisplay(Vector2i dims, ScreenSettings&& settings, const std::string&& title);
    ~GLFWDisplay() override;
    GLFWDisplay(const GLFWDisplay&) = delete;
    GLFWDisplay& operator=(const GLFWDisplay&) = delete;

    bool createGLFWDisplay();

    bool createDisplay() override;
    void closeDisplay() override;
    Vector2i getScreenSize() override;
    bool isClosed() override;
    void show() override;
    void hide() override;
    void centerCursor() override;
    int refreshRate() override;
    WindowEventManager& getWindowEventManager() override;

};

#define glfw_callback(name, type) \
    inline void name(GLFWwindow* window, int key, int scancode, int action, int mods) \
    { \
        auto* display = static_cast<GLFWDisplay*>(glfwGetWindowUserPointer(window)); \
        GLFWCallback callback = { \
            key, \
            scancode, \
            action, \
            mods, \
            type \
        }; \
        display->getWindowEventManager().processWindowEvent(callback); \
    } \
    \

namespace GLFWCALLBACK
{

    void errorCallback();

    glfw_callback(windowMoveCallback, WINDOW_POSITION)
    glfw_callback(windowSizeCallback, WINDOW_SIZE)
    glfw_callback(windowCloseCallback, WINDOW_CLOSE)
    glfw_callback(windowRefreshCallback, WINDOW_REFRESH)
    glfw_callback(windowFocusCallback, WINDOW_FOCUS)
    glfw_callback(windowIconifyCallback, WINDOW_ICONIFY)
    glfw_callback(windowMaximiseCallback, WINDOW_MAXIMIZE)
    glfw_callback(windowContentScaleCallback, WINDOW_CONTENT_SCALE)
    glfw_callback(frameBufferResizeCallback, FRAMEBUFFER_SIZE)
    glfw_callback(keyboardInputCallback, KEYBOARD_INPUT)
    glfw_callback(charInputCallback, CHAR_INPUT)
    glfw_callback(moddedCharInputCallback, CHAR_WITH_MODS)
    glfw_callback(mouseButtonPressCallback, MOUSE_BUTTON)
    glfw_callback(mouseMovementCallback, MOUSE_MOVEMENT)
    glfw_callback(cursorBorderCrossCallback, CURSOR_BORDER_CROSS)
    glfw_callback(mouseScrollWheelCallback, MOUSE_SCROL_WHEEL)
    glfw_callback(fileDropCallback, FILE_DROP)
    glfw_callback(joystickConnectionCallback, JOYSTICK_CONNECTION)
    glfw_callback(monitorPlugCallback, MONITOR_PLUG)

}

#undef glfw_callback


