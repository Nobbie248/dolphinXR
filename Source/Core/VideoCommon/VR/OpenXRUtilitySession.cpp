// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/VR/OpenXRUtilitySession.h"

#ifdef ENABLE_VR

#ifdef _WIN32

#define XR_USE_GRAPHICS_API_D3D11

#include <array>
#include <chrono>
#include <cstring>
#include <utility>
#include <vector>

#include <d3d11.h>
#include <dxgi.h>
#include <openxr/openxr_platform.h>
#include <wrl/client.h>

#include "Core/Config/GraphicsSettings.h"
#include "Common/Logging/Log.h"
#include "Common/VR/OpenXRInputState.h"
#include "VideoCommon/VR/OpenXRD3D11Common.h"
#include "VideoCommon/VR/OpenXRManager.h"

namespace VR
{
namespace
{
using Microsoft::WRL::ComPtr;

std::atomic<bool> s_openxr_utility_session_active{false};
constexpr float BINDING_OVERLAY_HEIGHT_SCALE = 1.0f;
constexpr float BINDING_OVERLAY_VERTICAL_OFFSET = 0.15f;

XrPosef BuildOverlayPoseFromCurrentHeadHeight(const std::array<XREyeView, 2>& eye_views,
                                              float distance)
{
  const float head_height =
      0.5f * (eye_views[0].pose.position.y + eye_views[1].pose.position.y);
  return {{0.f, 0.f, 0.f, 1.f}, {0.f, head_height + BINDING_OVERLAY_VERTICAL_OFFSET, -distance}};
}

bool SubmitFrameLayers(OpenXRManager* manager, const std::vector<XrCompositionLayerBaseHeader*>& layers)
{
  XrFrameEndInfo end_info{XR_TYPE_FRAME_END_INFO};
  end_info.displayTime = manager->GetPredictedDisplayTime();
  end_info.environmentBlendMode = manager->GetActiveBlendMode();

  if (manager->ShouldRender())
  {
    end_info.layerCount = static_cast<uint32_t>(layers.size());
    end_info.layers = layers.data();
  }

  const XrResult result = xrEndFrame(manager->GetSession(), &end_info);
  if (XR_SUCCEEDED(result))
    return true;

  char result_string[XR_MAX_RESULT_STRING_SIZE]{};
  xrResultToString(manager->GetInstance(), result, result_string);
  ERROR_LOG_FMT(OPENXR, "OpenXR utility session: xrEndFrame failed: {}", result_string);
  return false;
}

bool IsOverlayFormatSupported(DXGI_FORMAT format)
{
  switch (format)
  {
  case DXGI_FORMAT_R8G8B8A8_UNORM:
  case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
  case DXGI_FORMAT_B8G8R8A8_UNORM:
  case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    return true;
  default:
    return false;
  }
}
}  // namespace

OpenXRUtilitySession::OpenXRUtilitySession() = default;

OpenXRUtilitySession::~OpenXRUtilitySession()
{
  Stop();
}

bool OpenXRUtilitySession::Start()
{
  Stop();

  bool expected = false;
  if (!s_openxr_utility_session_active.compare_exchange_strong(expected, true))
    return false;

  {
    std::lock_guard lk(m_state_mutex);
    m_stop_requested.store(false, std::memory_order_release);
    m_running = false;
    m_start_completed = false;
    m_start_succeeded = false;
  }

  m_thread = std::thread(&OpenXRUtilitySession::ThreadMain, this);

  std::unique_lock lk(m_state_mutex);
  m_start_cv.wait(lk, [this] { return m_start_completed; });
  if (!m_start_succeeded)
    s_openxr_utility_session_active.store(false, std::memory_order_release);
  return m_start_succeeded;
}

void OpenXRUtilitySession::Stop()
{
  m_stop_requested.store(true, std::memory_order_release);

  if (m_thread.joinable())
    m_thread.join();

  std::lock_guard lk(m_state_mutex);
  m_running = false;
}

bool OpenXRUtilitySession::IsRunning() const
{
  std::lock_guard lk(m_state_mutex);
  return m_running;
}

void OpenXRUtilitySession::SetOverlayImage(std::vector<u8> pixels, uint32_t width, uint32_t height)
{
  if (pixels.empty() || width == 0 || height == 0)
    return;

  std::lock_guard lk(m_overlay_mutex);
  m_overlay_pixels = std::move(pixels);
  m_overlay_width = width;
  m_overlay_height = height;
  m_overlay_dirty = true;
}

void OpenXRUtilitySession::ThreadMain()
{
  auto finish_start = [this](bool success) {
    std::lock_guard lk(m_state_mutex);
    m_running = success;
    m_start_succeeded = success;
    m_start_completed = true;
    m_start_cv.notify_all();
  };

  Common::VR::OpenXRInputState::Reset();

  std::unique_ptr<OpenXRManager> manager = std::make_unique<OpenXRManager>();
  D3D11OpenXR::DeviceBundle device_bundle;
  XrSession session = XR_NULL_HANDLE;
  XrSwapchain overlay_swapchain = XR_NULL_HANDLE;
  std::vector<XrSwapchainImageD3D11KHR> overlay_images;
  XrCompositionLayerQuad overlay_layer{XR_TYPE_COMPOSITION_LAYER_QUAD};
  bool overlay_enabled = false;
  bool overlay_image_ready = false;
  bool overlay_pose_initialized = false;
  DXGI_FORMAT overlay_format = DXGI_FORMAT_UNKNOWN;
  uint32_t overlay_width = 1024;
  uint32_t overlay_height = 512;
  const float overlay_distance = Config::Get(Config::GFX_VR_SCREEN_DISTANCE);
  const float overlay_height_m =
      Config::Get(Config::GFX_VR_SCREEN_SIZE) * BINDING_OVERLAY_HEIGHT_SCALE;

  std::vector<const char*> util_extensions = {XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
  const auto controller_exts = VR::OpenXRManager::GetAvailableControllerExtensions();
  util_extensions.insert(util_extensions.end(), controller_exts.begin(), controller_exts.end());
  if (!manager->CreateInstance(util_extensions) ||
      !manager->InitializeSystem() || !manager->EnumerateViewConfigurations())
  {
    finish_start(false);
    return;
  }

  XrGraphicsRequirementsD3D11KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
  if (!D3D11OpenXR::QueryGraphicsRequirements(manager->GetInstance(), manager->GetSystemId(),
                                              &requirements) ||
      !D3D11OpenXR::CreateMatchingDevice(requirements, false, &device_bundle) ||
      !D3D11OpenXR::CreateSessionFromRequirements(manager->GetInstance(), manager->GetSystemId(),
                                                  requirements, device_bundle.device.Get(),
                                                  &session))
  {
    finish_start(false);
    return;
  }

  manager->SetSession(session);
  if (!manager->CreateReferenceSpace())
  {
    finish_start(false);
    return;
  }

  int64_t swapchain_format = 0;
  if (manager->GetReferenceSpace() != XR_NULL_HANDLE &&
      D3D11OpenXR::SelectSwapchainFormat(session, &swapchain_format) &&
      IsOverlayFormatSupported(static_cast<DXGI_FORMAT>(swapchain_format)))
  {
    {
      std::lock_guard lk(m_overlay_mutex);
      overlay_width = m_overlay_width;
      overlay_height = m_overlay_height;
    }
    const float overlay_width_m =
        overlay_height_m * (static_cast<float>(overlay_width) / static_cast<float>(overlay_height));

    XrSwapchainCreateInfo swapchain_info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    swapchain_info.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_info.format = swapchain_format;
    swapchain_info.sampleCount = 1;
    swapchain_info.width = overlay_width;
    swapchain_info.height = overlay_height;
    swapchain_info.faceCount = 1;
    swapchain_info.arraySize = 1;
    swapchain_info.mipCount = 1;

    if (XR_SUCCEEDED(xrCreateSwapchain(session, &swapchain_info, &overlay_swapchain)))
    {
      uint32_t image_count = 0;
      xrEnumerateSwapchainImages(overlay_swapchain, 0, &image_count, nullptr);
      overlay_images.resize(image_count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
      if (XR_SUCCEEDED(xrEnumerateSwapchainImages(
              overlay_swapchain, image_count, &image_count,
              reinterpret_cast<XrSwapchainImageBaseHeader*>(overlay_images.data()))))
      {
        overlay_enabled = true;
        overlay_format = static_cast<DXGI_FORMAT>(swapchain_format);
        overlay_layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        overlay_layer.space = manager->GetReferenceSpace();
        overlay_layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        overlay_layer.pose = {{0.f, 0.f, 0.f, 1.f}, {0.f, 0.f, -overlay_distance}};
        overlay_layer.size = {overlay_width_m, overlay_height_m};
        overlay_layer.subImage.swapchain = overlay_swapchain;
        overlay_layer.subImage.imageRect.offset = {0, 0};
        overlay_layer.subImage.imageRect.extent = {static_cast<int32_t>(overlay_width),
                                                   static_cast<int32_t>(overlay_height)};
        overlay_layer.subImage.imageArrayIndex = 0;
      }
    }

    if (!overlay_enabled)
    {
      WARN_LOG_FMT(OPENXR,
                   "OpenXR utility session: failed to create the quad overlay; continuing "
                   "without overlay.");
      if (overlay_swapchain != XR_NULL_HANDLE)
      {
        xrDestroySwapchain(overlay_swapchain);
        overlay_swapchain = XR_NULL_HANDLE;
      }
    }
  }
  else if (manager->GetReferenceSpace() != XR_NULL_HANDLE)
  {
    WARN_LOG_FMT(OPENXR,
                 "OpenXR utility session: runtime swapchain format '{}' is unsuitable for the "
                 "binding overlay; continuing without overlay.",
                 D3D11OpenXR::FormatToString(swapchain_format));
  }

  finish_start(true);

  const auto disable_overlay = [&]() {
    overlay_enabled = false;
    overlay_image_ready = false;
    overlay_images.clear();
    if (overlay_swapchain != XR_NULL_HANDLE)
    {
      xrDestroySwapchain(overlay_swapchain);
      overlay_swapchain = XR_NULL_HANDLE;
    }
  };

  while (!m_stop_requested.load(std::memory_order_acquire))
  {
    if (!manager->PollEvents())
      break;

    if (!manager->IsSessionRunning())
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    if (!manager->WaitFrame() || !manager->BeginFrame())
      break;

    if (manager->ShouldRender())
    {
      manager->LocateViews();
      if (overlay_enabled && !overlay_pose_initialized)
      {
        overlay_layer.pose =
            BuildOverlayPoseFromCurrentHeadHeight(manager->GetEyeViews(), overlay_distance);
        overlay_pose_initialized = true;
      }
    }

    std::vector<XrCompositionLayerBaseHeader*> layers;

    if (overlay_enabled)
    {
      std::vector<u8> overlay_pixels;
      {
        std::lock_guard lk(m_overlay_mutex);
        if (m_overlay_dirty)
        {
          overlay_pixels = m_overlay_pixels;
          m_overlay_dirty = false;
        }
      }

      if (!overlay_pixels.empty())
      {
        uint32_t image_index = 0;
        XrSwapchainImageAcquireInfo acquire_info{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        const XrResult acquire_result =
            xrAcquireSwapchainImage(overlay_swapchain, &acquire_info, &image_index);
        if (XR_SUCCEEDED(acquire_result))
        {
          XrSwapchainImageWaitInfo wait_info{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
          wait_info.timeout = XR_INFINITE_DURATION;
          if (XR_SUCCEEDED(xrWaitSwapchainImage(overlay_swapchain, &wait_info)))
          {
            if (image_index < overlay_images.size() && overlay_images[image_index].texture)
            {
              std::vector<u8> converted_pixels;
              const void* upload_pixels = overlay_pixels.data();
              if (overlay_format == DXGI_FORMAT_B8G8R8A8_UNORM ||
                  overlay_format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
              {
                converted_pixels = overlay_pixels;
                for (size_t i = 0; i + 3 < converted_pixels.size(); i += 4)
                  std::swap(converted_pixels[i], converted_pixels[i + 2]);
                upload_pixels = converted_pixels.data();
              }

              device_bundle.context->UpdateSubresource(overlay_images[image_index].texture, 0,
                                                       nullptr, upload_pixels, overlay_width * 4, 0);
            }
          }

          XrSwapchainImageReleaseInfo release_info{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
          xrReleaseSwapchainImage(overlay_swapchain, &release_info);
          overlay_image_ready = true;
        }
      }

      if (manager->ShouldRender() && overlay_image_ready)
      {
        layers.push_back(
            reinterpret_cast<XrCompositionLayerBaseHeader*>(&overlay_layer));
      }
    }

    if (!SubmitFrameLayers(manager.get(), layers))
    {
      if (overlay_enabled && !layers.empty())
      {
        WARN_LOG_FMT(
            OPENXR,
            "OpenXR utility session: disabling quad overlay after frame submission failed; "
            "continuing with an empty scene for controller binding.");
        disable_overlay();
        continue;
      }
      else
      {
        break;
      }
    }
  }

  disable_overlay();

  manager.reset();
  Common::VR::OpenXRInputState::Reset();
  s_openxr_utility_session_active.store(false, std::memory_order_release);

  {
    std::lock_guard lk(m_state_mutex);
    m_running = false;
    if (!m_start_completed)
    {
      m_start_completed = true;
      m_start_succeeded = false;
      m_start_cv.notify_all();
    }
  }
}
}  // namespace VR

#else

namespace VR
{
OpenXRUtilitySession::OpenXRUtilitySession() = default;
OpenXRUtilitySession::~OpenXRUtilitySession() = default;
bool OpenXRUtilitySession::Start()
{
  return false;
}
void OpenXRUtilitySession::Stop()
{
}
bool OpenXRUtilitySession::IsRunning() const
{
  return false;
}
void OpenXRUtilitySession::SetOverlayImage(std::vector<u8>, uint32_t, uint32_t)
{
}
}  // namespace VR

#endif

#endif  // ENABLE_VR
