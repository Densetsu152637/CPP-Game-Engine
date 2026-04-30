//
// Created by Nicholas on 01/05/26.
//

#include "display.h"

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
