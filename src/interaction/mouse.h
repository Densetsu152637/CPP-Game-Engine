//
// Created by Nicholas on 26/04/26.
//

#pragma once

#include "../async/buffer.h"
#include "../async/lock.h"
#include "../core/interfaces.h"
#include "../structs/vector.h"


struct CentreTracking
{

    Vector2f center;
    Vector2f frameDelta;
    bool relative;

};


class MouseListener : public IPollable
{

    static constexpr float CENTER_EPSILON = 0.001f;

    TripleBuffer<Vector2f> m_mousePos {
        []() { return Vector2f {0, 0}; }
    };
    Syncronized<CentreTracking> m_centreTracking;

public:

    MouseListener() {}

    void poll() override;

    Vector2f position() const;
    Vector2f prevPos() const;
    Vector2f mouseMovement() const;
    bool isCentreRelative();

    void setRelativeMode(bool relative);
    void setCenter(Vector2f c);
    void setCentreFromWindowDims(const Vector2f& dims);

    void updateMousePos(Vector2f newPos);


};

