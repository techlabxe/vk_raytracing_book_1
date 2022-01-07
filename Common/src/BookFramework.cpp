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
        //VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,    // Vulkan1.2 を使わず vkGetBufferDeviceAddressKHR 使う場合には必要.

        // Vulkan Raytracing API で必要.
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME, // VK_KHR_acceleration_structureで必要とされている.

        // descriptor indexing に必要.
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
    };
    bool useValidationLayer = false;
#if _DEBUG
    useValidationLayer = true;
#endif
    if (!m_device->OnInit(requiredExtensions, useValidationLayer)) {
        throw std::runtime_error("GraphicsDevice OnInit() failed.");
    }

    // ウィンドウの生成とスワップチェインの準備.
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

    // 各アプリケーション固有の初期化処理.
    OnInit();
}

void BookFramework::Destroy()
{
    // GPU の処理が全て終わるまでを待機.
    m_device->WaitForIdleGpu();

    // 各アプリケーションごとの終了処理.
    OnDestroy();

    // デバイスを解放する.
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
