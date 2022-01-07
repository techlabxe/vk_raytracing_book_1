#include "Camera.h"
#include <glm/gtx/transform.hpp>

Camera::Camera()
{
    m_mtxView = glm::mat4(1.0f);
    m_mtxProj = glm::mat4(1.0f);
    m_isDragged = false;
    m_buttonType = -1;
}

void Camera::SetLookAt(glm::vec3 eye, glm::vec3 target, glm::vec3 up)
{
    m_eye = eye;
    m_target = target;
    m_up = up;
    m_mtxView = glm::lookAtRH(m_eye, m_target, m_up);
}

void Camera::SetPerspective(float fovY, float aspect, float znear, float zfar)
{
    m_mtxProj = glm::perspectiveRH(fovY, aspect, znear, zfar);
}

void Camera::OnMouseButtonDown(int buttonType)
{
    m_buttonType = buttonType;
}

void Camera::OnMouseMove(float dx, float dy)
{
    if (m_buttonType < 0) {
        return;
    }
    if (m_buttonType == 0) {
        CalcOrbit(dx, dy);
    }
    if (m_buttonType == 1) {
        CalcDolly(dy);
    }
}

void Camera::OnMouseButtonUp()
{
    m_buttonType = -1;
}

void Camera::CalcOrbit(float dx, float dy)
{
    auto toEye = m_eye - m_target;
    auto toEyeLength = glm::length(toEye);
    toEye = glm::normalize(toEye);

    auto phi = std::atan2(toEye.x, toEye.z); // 方位角.
    auto theta = std::acos(toEye.y);  // 仰角.

    const auto twoPI = glm::two_pi<float>();
    const auto PI = glm::pi<float>();

    // ウィンドウのサイズ移動時には
    //  - 方位角は 360度分回る
    //  - 仰角は 約180度分回る.
    auto x = (PI + phi) / twoPI;
    auto y = theta / PI;

    x += dx;
    y -= dy;
    y = std::fmax(0.02f, std::fmin(y, 0.98f));

    // 割合からラジアン角へ変換.
    phi = x * twoPI;
    theta = y * PI;

    auto st = std::sinf(theta);
    auto sp = std::sinf(phi);
    auto ct = std::cosf(theta);
    auto cp = std::cosf(phi);

    // 各成分より新カメラ位置への3次元ベクトルを生成.
    auto newToEye = glm::normalize(glm::vec3(-st * sp, ct, -st * cp));
    newToEye *= toEyeLength;
    m_eye = m_target + newToEye;

    m_mtxView = glm::lookAtRH(m_eye, m_target, m_up);
}

void Camera::CalcDolly(float d)
{
    auto toTarget = m_target - m_eye;
    auto toTargetLength = glm::length(toTarget);
    if (toTargetLength < FLT_EPSILON) {
        return;
    }
    toTarget = glm::normalize(toTarget);

    auto delta = toTargetLength * d;
    m_eye += toTarget * delta;
    m_mtxView = glm::lookAtRH(m_eye, m_target, m_up);
}

