#pragma once

#include <string>
#include <memory>
#include "GraphicsDevice.h"
#include "VkrayBookUtility.h"

struct GLFWwindow;

class BookFramework {
public:
    BookFramework(const char* title);

    int Run();

    enum MouseButton {
        LBUTTON = 0,
        RBUTTON,
        MBUTTON,
    };
    virtual void OnMouseDown(MouseButton button) {}
    virtual void OnMouseUp(MouseButton button) {}
    virtual void OnMouseMove() {}

protected:
    virtual void OnInit() = 0;
    virtual void OnDestroy() = 0;

    virtual void OnUpdate() = 0;
    virtual void OnRender() = 0;

    template<class T> T Align(T size, uint32_t align)
    {
        return (size + align - 1) & ~static_cast<T>((align-1));
    }
    float GetAspect() const;
    int GetWidth() const;
    int GetHeight() const;
protected:
    std::unique_ptr<vk::GraphicsDevice> m_device;
    GLFWwindow* m_window = nullptr;

private:
    void Initialize();
    void Destroy();

    std::string m_title;
};

