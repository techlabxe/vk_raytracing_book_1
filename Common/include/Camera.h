#pragma once

#include <glm/glm.hpp>


class Camera
{
public:
    Camera();

    void SetLookAt(glm::vec3 eye, glm::vec3 target, glm::vec3 up = glm::vec3(0, 1, 0));
    void SetPerspective(
        float fovY, float aspect, float znear, float zfar);

    glm::mat4 GetViewMatrix() const { return m_mtxView; }
    glm::mat4 GetProjectionMatrix() const { return m_mtxProj; }

    glm::vec3 GetPosition() const { return m_eye; }
    glm::vec3 GetTarget() const { return m_target; }
    glm::vec3 GetUp() const { return m_up; }

    void OnMouseButtonDown(int buttonType);
    void OnMouseMove(float dx, float dy);
    void OnMouseButtonUp();
private:
    void CalcOrbit(float dx, float dy);
    void CalcDolly(float d);
private:
    glm::vec3 m_eye;
    glm::vec3 m_target;
    glm::vec3 m_up;

    glm::mat4 m_mtxView;
    glm::mat4 m_mtxProj;

    bool m_isDragged;
    int m_buttonType;
};
