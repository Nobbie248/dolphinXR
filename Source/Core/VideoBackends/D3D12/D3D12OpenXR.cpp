// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef ENABLE_VR

// Must define the D3D12 platform before any OpenXR includes.
#define XR_USE_GRAPHICS_API_D3D12

#include "VideoBackends/D3D12/D3D12OpenXR.h"

#include <algorithm>
#include <array>
#include <vector>

#include "Common/Assert.h"
#include "Common/Logging/Log.h"

#include "VideoBackends/D3D12/D3D12Gfx.h"
#include "VideoBackends/D3D12/DX12Context.h"
#include "VideoBackends/D3D12/DX12Texture.h"
#include "VideoCommon/TextureConfig.h"
#include "VideoCommon/VideoConfig.h"
// Only used for the DXGI swapchain-format helpers, which are shared between the D3D11 and
// D3D12 bindings (both enumerate DXGI_FORMAT values). D3D12OpenXR.h enables both OpenXR
// platform blocks so this header's D3D11-typed declarations compile here.
#include "VideoCommon/VR/OpenXRD3D11Common.h"
#include "VideoCommon/VR/OpenXRManager.h"

namespace DX12
{
std::unique_ptr<D3D12OpenXR> g_openxr_d3d12;

D3D12OpenXR::D3D12OpenXR() = default;

D3D12OpenXR::~D3D12OpenXR()
{
  Shutdown();
}

bool D3D12OpenXR::Initialize()
{
  INFO_LOG_FMT(VIDEO, "OpenXR D3D12: Starting initialization...");
  ASSERT_MSG(VIDEO, !VR::g_openxr, "OpenXRManager already initialized.");

  auto mgr = std::make_unique<VR::OpenXRManager>();

  // The D3D12 graphics binding extension is mandatory.
  // Also enable optional controller profile extensions (Meta, Pico, etc.) when available.
  std::vector<const char*> extensions = {XR_KHR_D3D12_ENABLE_EXTENSION_NAME};
  if (VR::OpenXRManager::IsRuntimeExtensionSupported(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME))
  {
    extensions.push_back(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
    INFO_LOG_FMT(VIDEO, "OpenXR: Enabling XR_FB_display_refresh_rate.");
  }
  const auto controller_exts = VR::OpenXRManager::GetAvailableControllerExtensions();
  extensions.insert(extensions.end(), controller_exts.begin(), controller_exts.end());
  if (!mgr->CreateInstance(extensions))
    return false;

  if (!mgr->InitializeSystem())
    return false;

  if (!mgr->EnumerateViewConfigurations())
    return false;

  // Publish the manager globally so CreateSessionD3D12 can access instance/system IDs.
  VR::g_openxr = std::move(mgr);

  if (!CreateSessionD3D12())
  {
    VR::g_openxr.reset();
    return false;
  }

  if (!VR::g_openxr->CreateReferenceSpace())
  {
    VR::g_openxr.reset();
    return false;
  }

  if (!CreateSwapchains())
  {
    // Destroy any partially-created swapchains while the session still exists.
    DestroySwapchains();
    VR::g_openxr.reset();
    return false;
  }

  // Register this object as the swapchain provider so Presenter can acquire eye images.
  VR::g_openxr->SetSwapchain(this);

  INFO_LOG_FMT(VIDEO, "OpenXR D3D12: Initialization complete.");
  return true;
}

void D3D12OpenXR::Shutdown()
{
  // Clear swapchain pointer before destroying swapchains so no dangling use occurs.
  if (VR::g_openxr)
    VR::g_openxr->SetSwapchain(nullptr);

  DestroySwapchains();
  VR::g_openxr.reset();
  INFO_LOG_FMT(VIDEO, "OpenXR D3D12: Shut down.");
}

bool D3D12OpenXR::CreateSessionD3D12()
{
  ASSERT(g_dx_context != nullptr && g_dx_context->GetDevice() != nullptr);
  ASSERT(VR::g_openxr != nullptr);

  PFN_xrGetD3D12GraphicsRequirementsKHR get_requirements = nullptr;
  const XrResult addr_result =
      xrGetInstanceProcAddr(VR::g_openxr->GetInstance(), "xrGetD3D12GraphicsRequirementsKHR",
                            reinterpret_cast<PFN_xrVoidFunction*>(&get_requirements));
  if (XR_FAILED(addr_result) || get_requirements == nullptr)
  {
    ERROR_LOG_FMT(VIDEO, "OpenXR: Could not load xrGetD3D12GraphicsRequirementsKHR.");
    return false;
  }

  XrGraphicsRequirementsD3D12KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR};
  const XrResult req_result =
      get_requirements(VR::g_openxr->GetInstance(), VR::g_openxr->GetSystemId(), &requirements);
  if (XR_FAILED(req_result))
  {
    ERROR_LOG_FMT(VIDEO, "OpenXR: xrGetD3D12GraphicsRequirementsKHR failed ({}).",
                  static_cast<int>(req_result));
    return false;
  }

  INFO_LOG_FMT(VIDEO,
               "OpenXR: D3D12 requirements — adapter LUID {:#010x}{:08x}, "
               "min feature level {:#x}",
               requirements.adapterLuid.HighPart, requirements.adapterLuid.LowPart,
               static_cast<int>(requirements.minFeatureLevel));

  // Runtimes require the session's device to live on the HMD's adapter. Dolphin created its
  // device from the adapter selected in the graphics settings, so warn when they differ —
  // xrCreateSession is then expected to fail.
  const LUID device_luid = g_dx_context->GetDevice()->GetAdapterLuid();
  if (device_luid.HighPart != requirements.adapterLuid.HighPart ||
      device_luid.LowPart != requirements.adapterLuid.LowPart)
  {
    WARN_LOG_FMT(VIDEO,
                 "OpenXR: Dolphin's D3D12 device is on adapter LUID {:#010x}{:08x}, but the "
                 "runtime wants {:#010x}{:08x}. Select the headset's GPU as the adapter in the "
                 "graphics settings.",
                 device_luid.HighPart, device_luid.LowPart, requirements.adapterLuid.HighPart,
                 requirements.adapterLuid.LowPart);
  }

  XrGraphicsBindingD3D12KHR d3d12_binding{XR_TYPE_GRAPHICS_BINDING_D3D12_KHR};
  d3d12_binding.device = g_dx_context->GetDevice();
  d3d12_binding.queue = g_dx_context->GetCommandQueue();

  XrSessionCreateInfo session_info{XR_TYPE_SESSION_CREATE_INFO};
  session_info.next = &d3d12_binding;
  session_info.systemId = VR::g_openxr->GetSystemId();

  XrSession session = XR_NULL_HANDLE;
  const XrResult result = xrCreateSession(VR::g_openxr->GetInstance(), &session_info, &session);
  if (XR_FAILED(result))
  {
    ERROR_LOG_FMT(VIDEO, "OpenXR: xrCreateSession (D3D12) failed ({}).",
                  static_cast<int>(result));
    return false;
  }

  VR::g_openxr->SetSession(session);
  INFO_LOG_FMT(VIDEO, "OpenXR D3D12: Session created successfully.");
  return true;
}

bool D3D12OpenXR::CreateSwapchains()
{
  ASSERT(VR::g_openxr != nullptr);

  const auto& view_cfgs = VR::g_openxr->GetViewConfigViews();
  // Both the D3D11 and D3D12 bindings enumerate DXGI_FORMAT values, so the D3D11-named
  // format-selection helper applies unchanged.
  int64_t swapchain_format = 0;
  if (!VR::D3D11OpenXR::SelectSwapchainFormat(VR::g_openxr->GetSession(), &swapchain_format))
    return false;

  for (uint32_t eye = 0; eye < 2; ++eye)
  {
    auto& sc = m_eye_swapchains[eye];
    sc.width = view_cfgs[eye].recommendedImageRectWidth;
    sc.height = view_cfgs[eye].recommendedImageRectHeight;

    // Format must come from xrEnumerateSwapchainFormats.
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.arraySize = 1;
    info.format = swapchain_format;
    info.width = sc.width;
    info.height = sc.height;
    info.mipCount = 1;
    info.faceCount = 1;
    info.sampleCount = 1;
    // We render into this swapchain image as a color target, but never sample from it.
    // Some runtimes reject the extra SAMPLED usage for certain formats.
    //
    // MUTABLE_FORMAT lets us keep the swapchain declared as sRGB (so the compositor
    // decodes correctly) while creating a UNORM RTV on the underlying texture, so
    // BlitFromTexture writes raw sRGB-encoded bytes without a double gamma encode.
    // With this flag the runtime must back the texture with a DXGI _TYPELESS format.
    info.usageFlags =
        XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT;

    XrResult result = xrCreateSwapchain(VR::g_openxr->GetSession(), &info, &sc.swapchain);
    if (XR_FAILED(result))
    {
      ERROR_LOG_FMT(VIDEO, "OpenXR: xrCreateSwapchain failed for eye {} ({}).", eye,
                    static_cast<int>(result));
      return false;
    }

    // Enumerate the D3D12 resources backing this swapchain.
    uint32_t image_count = 0;
    xrEnumerateSwapchainImages(sc.swapchain, 0, &image_count, nullptr);

    std::vector<XrSwapchainImageD3D12KHR> images(image_count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
    xrEnumerateSwapchainImages(sc.swapchain, image_count, &image_count,
                               reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));

    sc.textures.resize(image_count);
    sc.framebuffers.resize(image_count);

    for (uint32_t i = 0; i < image_count; ++i)
    {
      // Adopt the runtime-owned resource. DXTexture::CreateAdopted reads the resource
      // descriptor to build the TextureConfig and AddRefs the underlying resource.
      // The runtime guarantees acquired color images are in a RENDER_TARGET-compatible
      // state and expects them back in that state on release, so start tracking there.
      sc.textures[i] =
          DXTexture::CreateAdopted(images[i].texture, D3D12_RESOURCE_STATE_RENDER_TARGET);
      if (!sc.textures[i])
      {
        ERROR_LOG_FMT(VIDEO, "OpenXR: DXTexture::CreateAdopted failed for eye {}, image {}.",
                      eye, i);
        return false;
      }

      // No depth attachment; the compositor only consumes the color layer.
      sc.framebuffers[i] = DXFramebuffer::Create(sc.textures[i].get(), nullptr, {});
      if (!sc.framebuffers[i])
      {
        ERROR_LOG_FMT(VIDEO, "OpenXR: DXFramebuffer::Create failed for eye {}, image {}.",
                      eye, i);
        return false;
      }
    }

    INFO_LOG_FMT(VIDEO, "OpenXR: Eye {} swapchain ready: {}x{}, {} images.", eye, sc.width,
                 sc.height, image_count);
  }

  return true;
}

void D3D12OpenXR::DestroySwapchains()
{
  bool had_wrappers = false;

  for (uint32_t eye = 0; eye < 2; ++eye)
  {
    auto& sc = m_eye_swapchains[eye];

    if (m_image_acquired[eye] && sc.swapchain != XR_NULL_HANDLE)
    {
      XrSwapchainImageReleaseInfo release_info{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
      const XrResult release_result = xrReleaseSwapchainImage(sc.swapchain, &release_info);
      if (XR_FAILED(release_result))
      {
        WARN_LOG_FMT(VIDEO,
                     "OpenXR: xrReleaseSwapchainImage during shutdown failed for eye {} ({}).",
                     eye, static_cast<int>(release_result));
      }
      m_image_acquired[eye] = false;
    }

    // Release Dolphin wrappers before destroying the swapchain. The DXTexture destructor
    // defers the release of its reference until the current command list's fence passes,
    // so an explicit flush below drops them before the runtime frees the resources.
    had_wrappers |= !sc.textures.empty() || !sc.framebuffers.empty();
    sc.framebuffers.clear();
    sc.textures.clear();
  }

  // Wait for the GPU and destroy the deferred references on the runtime-owned resources.
  if (had_wrappers && g_dx_context)
  {
    if (g_gfx)
      Gfx::GetInstance()->ExecuteCommandList(true);
    else
      g_dx_context->ExecuteCommandList(true);
  }

  for (uint32_t eye = 0; eye < 2; ++eye)
  {
    auto& sc = m_eye_swapchains[eye];
    if (sc.swapchain != XR_NULL_HANDLE)
    {
      const XrResult destroy_result = xrDestroySwapchain(sc.swapchain);
      if (XR_FAILED(destroy_result))
      {
        WARN_LOG_FMT(VIDEO, "OpenXR: xrDestroySwapchain failed for eye {} ({}).", eye,
                     static_cast<int>(destroy_result));
      }
      sc.swapchain = XR_NULL_HANDLE;
    }
  }
}

AbstractFramebuffer* D3D12OpenXR::AcquireEyeFramebuffer(uint32_t eye_index)
{
  ASSERT(eye_index < 2);
  auto& sc = m_eye_swapchains[eye_index];

  XrSwapchainImageAcquireInfo acquire_info{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
  if (XR_FAILED(xrAcquireSwapchainImage(sc.swapchain, &acquire_info,
                                         &m_acquired_image_index[eye_index])))
  {
    ERROR_LOG_FMT(VIDEO, "OpenXR: xrAcquireSwapchainImage failed for eye {}.", eye_index);
    return nullptr;
  }
  m_image_acquired[eye_index] = true;

  // Block until the acquired image is safe to write.
  XrSwapchainImageWaitInfo wait_info{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
  wait_info.timeout = XR_INFINITE_DURATION;
  if (XR_FAILED(xrWaitSwapchainImage(sc.swapchain, &wait_info)))
  {
    ERROR_LOG_FMT(VIDEO, "OpenXR: xrWaitSwapchainImage failed for eye {}.", eye_index);

    // Ensure we don't leak an acquired image if waiting fails.
    XrSwapchainImageReleaseInfo release_info{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    const XrResult release_result = xrReleaseSwapchainImage(sc.swapchain, &release_info);
    if (XR_FAILED(release_result))
    {
      WARN_LOG_FMT(VIDEO,
                   "OpenXR: xrReleaseSwapchainImage after wait failure failed for eye {} ({}).",
                   eye_index, static_cast<int>(release_result));
    }
    m_image_acquired[eye_index] = false;
    return nullptr;
  }

  // The runtime hands acquired color images over in a RENDER_TARGET-compatible state;
  // resync our tracking without recording a barrier.
  const uint32_t idx = m_acquired_image_index[eye_index];
  sc.textures[idx]->OverrideState(D3D12_RESOURCE_STATE_RENDER_TARGET);

  return sc.framebuffers[idx].get();
}

void D3D12OpenXR::ReleaseEyeTexture(uint32_t eye_index)
{
  ASSERT(eye_index < 2);
  if (!m_image_acquired[eye_index])
    return;

  auto& sc = m_eye_swapchains[eye_index];

  // The runtime interprets released color images as RENDER_TARGET; record a barrier if the
  // blit left the image in another state.
  sc.textures[m_acquired_image_index[eye_index]]->TransitionToState(
      D3D12_RESOURCE_STATE_RENDER_TARGET);

  // The spec only requires the image writes to be *submitted* to the queue in the graphics
  // binding before xrReleaseSwapchainImage; GPU-side ordering is the runtime's job. Dolphin
  // batches commands into open command lists, so kick the current one now.
  Gfx::GetInstance()->ExecuteCommandList(false);

  XrSwapchainImageReleaseInfo release_info{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
  const XrResult result = xrReleaseSwapchainImage(sc.swapchain, &release_info);
  if (XR_FAILED(result))
  {
    WARN_LOG_FMT(VIDEO, "OpenXR: xrReleaseSwapchainImage failed for eye {} ({}).", eye_index,
                 static_cast<int>(result));
  }
  m_image_acquired[eye_index] = false;
}

bool D3D12OpenXR::SubmitFrame()
{
  ASSERT(VR::g_openxr != nullptr);

  // Use the submit snapshot captured when the GS pose cache was last refreshed. Using
  // live m_eye_views here would pick up LocateViews that ran between the last draw and
  // xrEndFrame, causing ATW to reproject against the wrong pose.
  const auto& eye_views = VR::g_openxr->GetSubmittedEyeViews();

  for (uint32_t eye = 0; eye < 2; ++eye)
  {
    auto& pv = m_projection_views[eye];
    pv = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
    pv.pose = eye_views[eye].pose;
    pv.fov = eye_views[eye].fov;
    pv.subImage.swapchain = m_eye_swapchains[eye].swapchain;
    pv.subImage.imageArrayIndex = 0;
    pv.subImage.imageRect = {
        {0, 0},
        {static_cast<int32_t>(m_eye_swapchains[eye].width),
         static_cast<int32_t>(m_eye_swapchains[eye].height)}};
  }

  if (VR::g_openxr->IsFrameThreadActive())
  {
    // The pacing thread owns xrEndFrame; hand it the rendered views (poses included).
    VR::g_openxr->PublishFrame(m_projection_views, VR::g_openxr->GetProjectionLayerExtraFlags());
    return true;
  }

  m_projection_layer = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
  m_projection_layer.space = VR::g_openxr->GetReferenceSpace();
  m_projection_layer.viewCount = 2;
  m_projection_layer.views = m_projection_views.data();
  m_projection_layer.layerFlags = VR::g_openxr->GetProjectionLayerExtraFlags();

  const std::vector<XrCompositionLayerBaseHeader*> layers = {
      reinterpret_cast<XrCompositionLayerBaseHeader*>(&m_projection_layer)};

  return VR::g_openxr->EndFrame(layers);
}

}  // namespace DX12

#endif  // ENABLE_VR
