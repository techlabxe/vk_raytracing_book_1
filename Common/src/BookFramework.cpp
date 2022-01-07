#include "BookFramework.h"
#include "extensions_vk.hpp"

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720

BookFramework::BookFramework(const char* title) : m_title(title)
{

}

float BookFramework::GetAspect() const
{
    int width = 0, height = 0;
    glfwGetWindowSize(m_window, &width, &height);
    width = (std::max)(1, width);
    height = (std::max)(1, height);

    return float(width) / float(height);
}

int BookFramework::GetWidth() const
{
    int width = 0, height = 0;
    glfwGetWindowSize(m_window, &width, &height);
    width = (std::max)(1, width);
    height = (std::max)(1, height);
    return width;
}

int BookFramework::GetHeight() const
{
    int width = 0, height = 0;
    glfwGetWindowSize(m_window, &width, &height);
    return height;
}

void BookFramework::Initialize()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    int width = WINDOW_WIDTH, height = WINDOW_HEIGHT;

    m_device = std::make_unique<vk::GraphicsDevice>();
    std::vector<const char*> requiredExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_MAINTENANCE3_EXTENSION_NAME,
        VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
        //VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,    // Vulkan1.2 ���g�킸 vkGetBufferDeviceAddressKHR �g���ꍇ�ɂ͕K�v.

        // Vulkan Raytracing API �ŕK�v.
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME, // VK_KHR_acceleration_structure�ŕK�v�Ƃ���Ă���.

        // descriptor indexing �ɕK�v.
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
    };
    bool useValidationLayer = false;
#if _DEBUG
    useValidationLayer = true;
#endif
    if (!m_device->OnInit(requiredExtensions, useValidationLayer)) {
        throw std::runtime_error("GraphicsDevice OnInit() failed.");
    }

    // �E�B���h�E�̐����ƃX���b�v�`�F�C���̏���.
    m_window = glfwCreateWindow(width, height, m_title.c_str(), nullptr, nullptr);
    if (!m_device->CreateSwapchain(width, height, m_window)) {
        throw std::runtime_error("CreateSwapchain failed.");
    }
    glfwSetWindowUserPointer(m_window, this);

    glfwSetMouseButtonCallback(m_window, [](GLFWwindow* window, int button, int action, int mods) {
        auto pThis = static_cast<BookFramework*>(glfwGetWindowUserPointer(window));
        auto btn = static_cast<MouseButton>(button);
        if (pThis == nullptr) {
            return;
        }
        if (action == GLFW_PRESS) {
            pThis->OnMouseDown(btn);
        }
        if (action == GLFW_RELEASE) {
            pThis->OnMouseUp(btn);
        }
    });
    glfwSetCursorPosCallback(m_window, [](GLFWwindow* window, double x, double y) {
        auto pThis = static_cast<BookFramework*>(glfwGetWindowUserPointer(window));
        if (pThis == nullptr) {
            return;
        }
        pThis->OnMouseMove();
    });

    // �e�A�v���P�[�V�����ŗL�̏���������.
    OnInit();
}

void BookFramework::Destroy()
{
    // GPU �̏������S�ďI���܂ł�ҋ@.
    m_device->WaitForIdleGpu();

    // �e�A�v���P�[�V�������Ƃ̏I������.
    OnDestroy();

    // �f�o�C�X���������.
    m_device->OnDestroy();
    m_device.reset();
}

int BookFramework::Run()
{
    Initialize();
    while (glfwWindowShouldClose(m_window) == GLFW_FALSE) {
        glfwPollEvents();
        OnUpdate();
        OnRender();
    }
    Destroy();
    return 0;
}
