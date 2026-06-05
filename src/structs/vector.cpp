//
// Created by Nicholas on 17/04/26.
//

#include <cmath>

#include "vector.h"

// VECTOR

// MATRIX

Matrix4f Matrix4f::identity()
{
    return identity(1.0f);
}
Matrix4f Matrix4f::identity(float scale)
{
    Matrix4f result;
    result.m00 = scale;
    result.m11 = scale;
    result.m22 = scale;
    result.m33 = 1.0f;
    return result;
}

Matrix4f Matrix4f::mat_mult(const Matrix4f& other) const
{
    Matrix4f result;
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            result.arr[col * 4 + row] =
                other.arr[0 * 4 + row] * this->arr[col * 4 + 0] +
                other.arr[1 * 4 + row] * this->arr[col * 4 + 1] +
                other.arr[2 * 4 + row] * this->arr[col * 4 + 2] +
                other.arr[3 * 4 + row] * this->arr[col * 4 + 3];
        }
    }
    return result;
}

Matrix4f Matrix4f::translate(const Vector3i& v) const
{
    Matrix4f result = Matrix4f::identity();
    result.m30 = static_cast<float>(v.x);
    result.m31 = static_cast<float>(v.y);
    result.m32 = static_cast<float>(v.z);
    return mat_mult(result);
}

Matrix4f Matrix4f::rotate(const float rad, const Vector3i& axis) const
{
    float x = static_cast<float>(axis.x);
    float y = static_cast<float>(axis.y);
    float z = static_cast<float>(axis.z);

    float len = std::sqrt(x*x + y*y + z*z);
    if (len != 0.0f) {
        x /= len;
        y /= len;
        z /= len;
    }

    float c = std::cos(rad);
    float s = std::sin(rad);
    float omc = 1.0f - c;

    Matrix4f result;

    result.m00 = x*x*omc + c;
    result.m01 = y*x*omc + z*s;
    result.m02 = x*z*omc - y*s;
    result.m03 = 0;

    result.m10 = x*y*omc - z*s;
    result.m11 = y*y*omc + c;
    result.m12 = y*z*omc + x*s;
    result.m13 = 0;

    result.m20 = x*z*omc + y*s;
    result.m21 = y*z*omc - x*s;
    result.m22 = z*z*omc + c;
    result.m23 = 0;

    result.m30 = 0;
    result.m31 = 0;
    result.m32 = 0;
    result.m33 = 1;

    return mat_mult(result);
}

Matrix4f Matrix4f::scale(const float scale) const
{
    const Matrix4f sc = Matrix4f::identity(scale);
    return mat_mult(sc);
}

