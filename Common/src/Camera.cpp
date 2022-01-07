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

    auto phi = std::atan2(toEye.x, toEye.z); // ���ʊp.
    auto theta = std::acos(toEye.y);  // �p.

    const auto twoPI = glm::two_pi<float>();
    const auto PI = glm::pi<float>();

    // �E�B���h�E�̃T�C�Y�ړ����ɂ�
    //  - ���ʊp�� 360�x�����
    //  - �p�� ��180�x�����.
    auto x = (PI + phi) / twoPI;
    auto y = theta / PI;

    x += dx;
    y -= dy;
    y = std::fmax(0.02f, std::fmin(y, 0.98f));

    // �������烉�W�A���p�֕ϊ�.
    phi = x * twoPI;
    theta = y * PI;

    auto st = std::sinf(theta);
    auto sp = std::sinf(phi);
    auto ct = std::cosf(theta);
    auto cp = std::cosf(phi);

    // �e�������V�J�����ʒu�ւ�3�����x�N�g���𐶐�.
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

