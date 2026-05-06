//
// Created by Nicholas on 26/04/26.
//

#include "mouse.h"

void MouseListener::poll()
{
    Vector2f frameDeltaRef;
    m_centreTracking.use([&frameDeltaRef](CentreTracking& tracking)
    {
        if (!tracking.relative) return;
        frameDeltaRef = tracking.frameDelta;
        tracking.frameDelta = Vector2f {};
    });

    // In centered mode, publish integrated delta as pseudo-position so
    // mouseMovement() stays meaningful and stable.
    m_mousePos.write(m_mousePos.read() + frameDeltaRef);
    m_mousePos.swap();
}

Vector2f MouseListener::position() const
{
    return m_mousePos.read();
}

Vector2f MouseListener::prevPos() const
{
    return m_mousePos.prev();
}

Vector2f MouseListener::mouseMovement() const
{
    auto [current, previous] = m_mousePos.readLast();
    return current - previous;
}

bool MouseListener::isCentreRelative()
{
    return m_centreTracking.map(
        [](CentreTracking& tracking)
        { return tracking.relative; }
    );
}

void MouseListener::setRelativeMode(const bool relative)
{
    return m_centreTracking.use(
        [&relative](CentreTracking& tracking)
        { tracking.relative = relative; }
    );
}
void MouseListener::setCenter(Vector2f c)
{
    return m_centreTracking.use(
        [&c](CentreTracking& tracking)
        { tracking.center = c; }
    );
}

void MouseListener::setCentreFromWindowDims(const Vector2f& dims)
{
    setCenter({ dims.x / 2.0f, dims.y / 2.0f} );
}

void MouseListener::updateMousePos(Vector2f newPos)
{
    if (!isCentreRelative()) {
        m_mousePos.write(std::move(newPos));
        return;
    }

    const Vector2f center = m_centreTracking.map(
        [](CentreTracking& t)
        { return t.center; }
    );
    const Vector2f dPos = newPos - center;

    // Ignore synthetic recenter callback jitter.
    if (std::abs(dPos.x) <= CENTER_EPSILON && std::abs(dPos.y) <= CENTER_EPSILON) return;

    m_centreTracking.use([&dPos](CentreTracking& t)
    {
        t.frameDelta += dPos;
    });
}
