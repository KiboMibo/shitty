/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "ui_imgui.h"

#include "brand.h"
#include "composer.h"
#include "fatal.h"
#include "listener.h"
#include "session.h"
#include "ui.h"

#include <plt/input.h>
#include <plt/platform.h>
#include <plt/window.h>

#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/str/builder.h>
#include <std/sys/crt.h>

#include <math.h>
#include <stdint.h>
#include <string.h>

#if defined(HAVE_VULKAN_WAYLAND)
    #define VK_USE_PLATFORM_WAYLAND_KHR 1
    #include "backends/imgui_impl_vulkan.h"
    #include "imgui.h"
#endif

using namespace stl;

#if defined(HAVE_VULKAN_WAYLAND)

namespace {
    struct ImguiUi;

    // The chrome hears the tab model move through this proxy and asks
    // for a frame; the projection happens inside that frame.
    struct CallChromeDirty final: public Listener {
        explicit CallChromeDirty(ImguiUi* parent_);

        void onListen(void*) override;

        ImguiUi* parent;
    };

    struct ImguiUi final: public Ui, public plt::InputSink {
        ImguiUi(ObjPool& owner, Composer& composer_);
        ~ImguiUi();

        plt::RenderContext terminalContext() override;
        void beginFrame(const plt::WindowInfo& info) override;
        void endFrame() override;

        void key(const plt::KeyInput& input) override;
        void text(const plt::TextInput& input) override;
        void preedit(StringView text, i32 cursorBegin, i32 cursorEnd) override;
        void pointerMotion(const plt::PointerMotionInput& input) override;
        void pointerButton(const plt::PointerButtonInput& input) override;
        void scroll(const plt::ScrollInput& input) override;
        void focus(bool focused) override;
        void pointerPresence(bool present) override;
        void flush() override;

        void markDirty();
        void ensureBackend(const plt::WindowInfo& info);
        void renderChrome(const plt::WindowInfo& info);
        void drawTabs();
        u32 barHeightPixels() const;

        Composer& composer;
        plt::WindowLayer* terminalLayer = nullptr;
        CallChromeDirty sessionsChanged{this};

        ImGuiContext* context = nullptr;
        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physical = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue queue = VK_NULL_HANDLE;
        VkDescriptorPool descriptors = VK_NULL_HANDLE;
        u32 queueFamily = 0;
        ImGui_ImplVulkanH_Window chrome{};
        bool backendReady = false;
        bool rebuildChrome = false;
        u32 chromeWidth = 0;
        u32 chromeHeight = 0;
        float scale = 1.0f;
        u64 lastFrameUs = 0;
        bool dirty = true;
        bool barHovered = false;
        u64 shownGeneration = 0;
        u64 modelGeneration = 1;
    };

    static void checkVk(VkResult result, const char* what) {
        if (result != VK_SUCCESS) {
            raiseError(StringView(u8"imgui vulkan: "), StringView(what));
        }
    }
}

CallChromeDirty::CallChromeDirty(ImguiUi* parent_)
    : parent(parent_)
{
}

void CallChromeDirty::onListen(void*) {
    ++parent->modelGeneration;
    parent->markDirty();
}

ImguiUi::ImguiUi(ObjPool& owner, Composer& composer_)
    : composer(composer_)
{
    // The terminal keeps the production input path; the window surface -
    // the chrome - reroutes here, and everything that is not chrome
    // input forwards to the same router below.
    plt::WindowLayerOptions layer{};
    layer.input = composer.input;
    terminalLayer = composer.window->createLayer(owner, layer);
    composer.window->setInput(this);
    composer.sessionsChangedListeners.pushBack(&sessionsChanged);
}

ImguiUi::~ImguiUi() {
    sessionsChanged.unlink();
    if (!backendReady) {
        return;
    }
    vkDeviceWaitIdle(device);
    ImGui_ImplVulkan_Shutdown();
    ImGui::DestroyContext(context);
    ImGui_ImplVulkanH_DestroyWindow(instance, device, &chrome, nullptr);
    vkDestroyDescriptorPool(device, descriptors, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
}

plt::RenderContext ImguiUi::terminalContext() {
    return terminalLayer->renderContext();
}

u32 ImguiUi::barHeightPixels() const {
    // One tab row: the docking header is a frame of text plus padding,
    // scaled with the surface.
    return (u32)(ceilf(26.0f * scale));
}

void ImguiUi::ensureBackend(const plt::WindowInfo& info) {
    if (backendReady) {
        return;
    }
    const plt::RenderContext target = composer.window->renderContext();

    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.apiVersion = VK_API_VERSION_1_1;
    const char* extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
    };
    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &application;
    instanceInfo.enabledExtensionCount = 2;
    instanceInfo.ppEnabledExtensionNames = extensions;
    checkVk(vkCreateInstance(&instanceInfo, nullptr, &instance), "vkCreateInstance");

    VkWaylandSurfaceCreateInfoKHR surfaceInfo{};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.display = (struct wl_display*)(target.connection);
    surfaceInfo.surface = (struct wl_surface*)(target.window);
    checkVk(vkCreateWaylandSurfaceKHR(instance, &surfaceInfo, nullptr, &chrome.Surface), "vkCreateWaylandSurfaceKHR");

    u32 gpuCount = 0;
    checkVk(vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr), "vkEnumeratePhysicalDevices");
    if (gpuCount == 0) {
        raiseError(StringView(u8"imgui vulkan: no devices"));
    }
    Vector<VkPhysicalDevice> gpus;
    gpus.grow(gpuCount);
    for (u32 at = 0; at < gpuCount; ++at) {
        gpus.pushBack(VK_NULL_HANDLE);
    }
    checkVk(vkEnumeratePhysicalDevices(instance, &gpuCount, gpus.mutData()), "vkEnumeratePhysicalDevices");

    bool found = false;
    for (u32 gpu = 0; gpu < gpuCount && !found; ++gpu) {
        u32 familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(gpus[gpu], &familyCount, nullptr);
        Vector<VkQueueFamilyProperties> families;
        families.grow(familyCount);
        for (u32 at = 0; at < familyCount; ++at) {
            families.pushBack({});
        }
        vkGetPhysicalDeviceQueueFamilyProperties(gpus[gpu], &familyCount, families.mutData());
        for (u32 family = 0; family < familyCount; ++family) {
            if ((families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) {
                continue;
            }
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(gpus[gpu], family, chrome.Surface, &present);
            if (present != VK_TRUE) {
                continue;
            }
            physical = gpus[gpu];
            queueFamily = family;
            found = true;
            break;
        }
    }
    if (!found) {
        raiseError(StringView(u8"imgui vulkan: no graphics+present queue"));
    }

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    const char* deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = 1;
    deviceInfo.ppEnabledExtensionNames = deviceExtensions;
    checkVk(vkCreateDevice(physical, &deviceInfo, nullptr, &device), "vkCreateDevice");
    vkGetDeviceQueue(device, queueFamily, 0, &queue);

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 8;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    checkVk(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptors), "vkCreateDescriptorPool");

    chrome.SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(physical, chrome.Surface, nullptr, 0, VK_COLORSPACE_SRGB_NONLINEAR_KHR);
    const VkPresentModeKHR modes[] = {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_FIFO_KHR};
    chrome.PresentMode = ImGui_ImplVulkanH_SelectPresentMode(physical, chrome.Surface, modes, 2);
    chromeWidth = info.width;
    chromeHeight = barHeightPixels();
    ImGui_ImplVulkanH_CreateOrResizeWindow(instance, physical, device, &chrome, queueFamily, nullptr, (int)(chromeWidth), (int)(chromeHeight), 2, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

    context = ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.BackendPlatformName = "imtty-plt";
    ImGui::GetStyle().ScaleAllSizes(scale);

    ImGui_ImplVulkan_InitInfo init{};
    init.ApiVersion = VK_API_VERSION_1_1;
    init.Instance = instance;
    init.PhysicalDevice = physical;
    init.Device = device;
    init.QueueFamily = queueFamily;
    init.Queue = queue;
    init.DescriptorPool = descriptors;
    init.MinImageCount = 2;
    init.ImageCount = chrome.ImageCount;
    init.PipelineInfoMain.RenderPass = chrome.RenderPass;
    init.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    if (!ImGui_ImplVulkan_Init(&init)) {
        raiseError(StringView(u8"imgui vulkan: backend init failed"));
    }

    lastFrameUs = monotonicNowUs();
    backendReady = true;
}

void ImguiUi::markDirty() {
    dirty = true;
    if (composer.window != nullptr) {
        composer.window->requestFrame();
    }
}

void ImguiUi::beginFrame(const plt::WindowInfo& info) {
    if (isfinite(info.contentScale) && info.contentScale > 0.0f && info.contentScale != scale) {
        scale = info.contentScale;
        composer.setContentScale(scale);
        dirty = true;
    }
    ensureBackend(info);

    const u32 bar = barHeightPixels();
    const u32 terminalHeight = info.height > bar ? info.height - bar : 1;
    composer.resize((u16)(info.width), (u16)(terminalHeight));
    const double logical = (double)(scale);
    terminalLayer->setGeometry(0, (i32)((double)(bar) / logical + 0.5), (u32)((double)(info.width) / logical + 0.5), (u32)((double)(terminalHeight) / logical + 0.5));

    if (chromeWidth != info.width || rebuildChrome) {
        chromeWidth = info.width;
        chromeHeight = bar;
        ImGui_ImplVulkanH_CreateOrResizeWindow(instance, physical, device, &chrome, queueFamily, nullptr, (int)(chromeWidth), (int)(chromeHeight), 2, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
        rebuildChrome = false;
        dirty = true;
    }

    if (dirty) {
        renderChrome(info);
        dirty = false;
    }
}

void ImguiUi::endFrame() {
}

void ImguiUi::drawTabs() {
    SessionSet* const sessions = composer.sessions;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)(chromeWidth), (float)(chromeHeight)));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f * scale, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_TitleBgActive));
    ImGui::Begin("##header", nullptr, flags);

    if (sessions != nullptr && ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_FittingPolicyResizeDown)) {
        const size_t count = sessions->count();
        const size_t active = sessions->activeIndex();
        for (size_t at = 0; at < count; ++at) {
            StringBuilder label;
            const StringView title = sessions->title(at);
            if (title.length() != 0) {
                label << title;
            } else {
                label << composer.brand->displayName() << StringView(u8" ") << (at + 1);
            }
            label << StringView(u8"###tab") << at;
            ImGui::PushID((int)(at));
            bool open = true;
            ImGuiTabItemFlags itemFlags = 0;
            if (at == active && shownGeneration != modelGeneration) {
                itemFlags |= ImGuiTabItemFlags_SetSelected;
            }
            if (ImGui::BeginTabItem(label.cStr(), &open, itemFlags)) {
                if (at != active && shownGeneration == modelGeneration) {
                    sessions->activate(at);
                }
                ImGui::EndTabItem();
            }
            if (!open) {
                if (!sessions->close(at)) {
                    composer.window->requestClose();
                }
            }
            ImGui::PopID();
        }
        if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip)) {
            sessions->newSession();
        }
        ImGui::EndTabBar();
    }
    shownGeneration = modelGeneration;

    barHovered = ImGui::IsAnyItemHovered();
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void ImguiUi::renderChrome(const plt::WindowInfo&) {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)(chromeWidth), (float)(chromeHeight));
    const u64 now = monotonicNowUs();
    io.DeltaTime = lastFrameUs == 0 ? 1.0f / 60.0f : (float)((double)(now - lastFrameUs) / 1e6);
    if (io.DeltaTime <= 0.0f) {
        io.DeltaTime = 1.0f / 600.0f;
    }
    lastFrameUs = now;

    ImGui::NewFrame();
    drawTabs();
    ImGui::Render();
    ImDrawData* const drawData = ImGui::GetDrawData();

    ImGui_ImplVulkanH_Frame* const frame = &chrome.Frames[chrome.FrameIndex];
    checkVk(vkWaitForFences(device, 1, &frame->Fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");

    VkSemaphore acquired = chrome.FrameSemaphores[chrome.SemaphoreIndex].ImageAcquiredSemaphore;
    VkSemaphore completed = chrome.FrameSemaphores[chrome.SemaphoreIndex].RenderCompleteSemaphore;
    VkResult result = vkAcquireNextImageKHR(device, chrome.Swapchain, UINT64_MAX, acquired, VK_NULL_HANDLE, &chrome.FrameIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        rebuildChrome = true;
        markDirty();
        return;
    }
    checkVk(result, "vkAcquireNextImageKHR");
    ImGui_ImplVulkanH_Frame* const target = &chrome.Frames[chrome.FrameIndex];
    checkVk(vkWaitForFences(device, 1, &target->Fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
    checkVk(vkResetFences(device, 1, &target->Fence), "vkResetFences");
    checkVk(vkResetCommandPool(device, target->CommandPool, 0), "vkResetCommandPool");

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    checkVk(vkBeginCommandBuffer(target->CommandBuffer, &begin), "vkBeginCommandBuffer");

    VkClearValue clear{};
    VkRenderPassBeginInfo pass{};
    pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    pass.renderPass = chrome.RenderPass;
    pass.framebuffer = target->Framebuffer;
    pass.renderArea.extent.width = chromeWidth;
    pass.renderArea.extent.height = chromeHeight;
    pass.clearValueCount = 1;
    pass.pClearValues = &clear;
    vkCmdBeginRenderPass(target->CommandBuffer, &pass, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(drawData, target->CommandBuffer);
    vkCmdEndRenderPass(target->CommandBuffer);
    checkVk(vkEndCommandBuffer(target->CommandBuffer), "vkEndCommandBuffer");

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &acquired;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &target->CommandBuffer;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &completed;
    checkVk(vkQueueSubmit(queue, 1, &submit, target->Fence), "vkQueueSubmit");

    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &completed;
    present.swapchainCount = 1;
    present.pSwapchains = &chrome.Swapchain;
    present.pImageIndices = &chrome.FrameIndex;
    result = vkQueuePresentKHR(queue, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        rebuildChrome = true;
        markDirty();
        return;
    }
    checkVk(result, "vkQueuePresentKHR");
    chrome.SemaphoreIndex = (chrome.SemaphoreIndex + 1) % chrome.SemaphoreCount;
}

void ImguiUi::key(const plt::KeyInput& input) {
    composer.input->key(input);
}

void ImguiUi::text(const plt::TextInput& input) {
    composer.input->text(input);
}

void ImguiUi::preedit(StringView text, i32 cursorBegin, i32 cursorEnd) {
    composer.input->preedit(text, cursorBegin, cursorEnd);
}

void ImguiUi::pointerMotion(const plt::PointerMotionInput& input) {
    if (!backendReady) {
        return;
    }
    ImGui::GetIO().AddMousePosEvent((float)(input.pixelX), (float)(input.pixelY));
    markDirty();
}

void ImguiUi::pointerButton(const plt::PointerButtonInput& input) {
    if (!backendReady) {
        return;
    }
    int button = -1;
    switch (input.button) {
        case plt::PointerButton::Primary:
            button = 0;
            break;
        case plt::PointerButton::Secondary:
            button = 1;
            break;
        case plt::PointerButton::Middle:
            button = 2;
            break;
        default:
            break;
    }
    if (button < 0) {
        return;
    }
    // A press on the header background - no tab, no button - is the
    // window drag handle, exactly like a native title bar.
    if (button == 0 && input.pressed && !barHovered) {
        composer.window->startInteractiveMove();
        return;
    }
    ImGui::GetIO().AddMouseButtonEvent(button, input.pressed);
    markDirty();
}

void ImguiUi::scroll(const plt::ScrollInput& input) {
    if (!backendReady) {
        return;
    }
    ImGui::GetIO().AddMouseWheelEvent((float)(-input.x), (float)(-input.y));
    markDirty();
}

void ImguiUi::focus(bool focused) {
    composer.input->focus(focused);
    if (backendReady) {
        ImGui::GetIO().AddFocusEvent(focused);
        markDirty();
    }
}

void ImguiUi::pointerPresence(bool present) {
    if (backendReady && !present) {
        ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        markDirty();
    }
}

void ImguiUi::flush() {
    composer.input->flush();
}

Ui* createImguiUi(ObjPool& owner, Composer& composer) {
    if (composer.window == nullptr || composer.window->renderContext().backend != plt::RenderBackend::Wayland) {
        // Headless adapters and the Cocoa build run bare until the Metal
        // chrome exists.
        return createRawUi(owner, composer);
    }
    return owner.make<ImguiUi>(owner, composer);
}

#else

Ui* createImguiUi(ObjPool& owner, Composer& composer) {
    return createRawUi(owner, composer);
}

#endif
