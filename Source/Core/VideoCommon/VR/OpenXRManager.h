// Copyright 2024 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#ifdef ENABLE_VR

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <openxr/openxr.h>

#if defined(ANDROID)
#include <jni.h>
#endif

#include "Common/Matrix.h"
#include "VideoCommon/AbstractFramebuffer.h"

// OpenXRManager owns the XrInstance, XrSystemId, XrSession, and reference XrSpace.
// It handles the OpenXR session state machine and per-frame timing.
//
// Backend-specific swapchain creation (D3D11, etc.) is handled by the respective
// backend class (e.g. D3DOpenXR), which creates the XrSession with a graphics
// binding and then calls SetSession() + SetSwapchain() to transfer ownership here.

namespace VR
{
// Per-eye pose and field-of-view returned by xrLocateViews.
struct XREyeView
{
  XrPosef pose;
  XrFovf fov;
};

// Abstract interface for backend-specific per-eye swapchain management.
// Implemented by D3DOpenXR. Used by Presenter::RenderXFBToScreen() to blit game
// frames into the HMD's eye textures without depending on D3D11-specific types.
class IOpenXRSwapchain
{
public:
  virtual ~IOpenXRSwapchain() = default;

  // Acquire the next swapchain image for the given eye (0 = left, 1 = right).
  // Returns an AbstractFramebuffer* to render into, or nullptr on error.
  // Must be paired with ReleaseEyeTexture() after rendering is complete.
  virtual AbstractFramebuffer* AcquireEyeFramebuffer(uint32_t eye) = 0;

  // Signal the runtime that rendering into the given eye's current image is done.
  virtual void ReleaseEyeTexture(uint32_t eye) = 0;

  // Optional fast path for runtimes/backends that can expose both eyes as a
  // two-layer framebuffer. The default implementation preserves the per-eye path.
  virtual bool SupportsLayeredRendering() const { return false; }
  virtual AbstractFramebuffer* AcquireLayeredFramebuffer() { return nullptr; }
  virtual void ReleaseLayeredTexture() {}

  // True when the eye framebuffers carry a fragment density map (Vulkan VR foveation).
  // PostProcessing then compiles pipeline variants against foveated render passes.
  virtual bool HasFoveatedFramebuffers() const { return false; }

  // ---- Flat mono panel path (StereoMode::Off + vr_flat_screen) ----
  // The game is rendered mono and shown on a world-locked quad in the VR scene. The default
  // implementations reuse per-eye swapchain #0, so a backend only needs to expose that image's
  // raw XrSwapchain handle via GetFlatSwapchain() to opt in.
  virtual XrSwapchain GetFlatSwapchain() const { return XR_NULL_HANDLE; }
  virtual bool SupportsFlatScreen() const { return GetFlatSwapchain() != XR_NULL_HANDLE; }
  // Acquire the swapchain image the mono game frame is blit into.
  virtual AbstractFramebuffer* AcquireFlatFramebuffer() { return AcquireEyeFramebuffer(0); }
  virtual void ReleaseFlatTexture() { ReleaseEyeTexture(0); }
  // Build the XrCompositionLayerQuad from the flat swapchain and submit via xrEndFrame.
  // Defined in OpenXRManager.cpp; delegates the quad math to OpenXRManager::SubmitFlatQuadFrame.
  virtual bool SubmitFlatFrame();

  // Vulkan-backed OpenXR calls can touch the VkQueue bound to the XrSession. Backends that need
  // external queue synchronization return a held lock here; other backends return an empty lock.
  virtual std::unique_lock<std::mutex> AcquireGraphicsQueueLock() { return {}; }

  // Backends that finish xrEndFrame asynchronously must complete the previous frame before the
  // next xrBeginFrame call. Synchronous backends have no pending work.
  virtual bool WaitForPendingFrameFinalization(std::string_view reason = {}) { return true; }

  // Build the XrCompositionLayerProjection from the current eye poses and submit
  // via xrEndFrame. Call after both eyes have been rendered and released.
  virtual bool SubmitFrame() = 0;

  // Pixel dimensions of the eye render targets (typically HMD recommended resolution).
  virtual uint32_t GetEyeWidth() const = 0;
  virtual uint32_t GetEyeHeight() const = 0;
};

class OpenXRManager
{
public:
  OpenXRManager();
  ~OpenXRManager();

  OpenXRManager(const OpenXRManager&) = delete;
  OpenXRManager& operator=(const OpenXRManager&) = delete;

  // Query which optional controller-profile extensions are available on this system.
  // Returns string literal pointers (valid indefinitely). Call before CreateInstance().
  static std::vector<const char*> GetAvailableControllerExtensions();

  // Returns true if the named OpenXR extension is advertised by the current runtime.
  // Call before CreateInstance(); does not require an active instance.
  static bool IsRuntimeExtensionSupported(std::string_view ext_name);

  // Returns true if the named extension was enabled on this manager's XrInstance.
  bool IsExtensionEnabled(std::string_view ext_name) const;

#if defined(ANDROID)
  static void SetAndroidAppInfo(JavaVM* vm, JNIEnv* env, jobject activity);
  static void ClearAndroidAppInfo(JNIEnv* env);

  enum class AndroidThreadType
  {
    ApplicationMain,
    ApplicationWorker,
    RendererMain,
    RendererWorker,
  };

  // Register the calling thread with runtimes that expose XR_KHR_android_thread_settings.
  // Returns false when the extension is unavailable or the runtime rejects the request.
  bool RegisterCurrentAndroidThread(AndroidThreadType type, std::string_view label = {});

  // Ask Quest/OpenXR runtimes for the highest available CPU/GPU performance level.
  bool RequestAndroidHighPerformanceLevel();
#endif

  // Step 1: Create XrInstance.
  // extra_extensions must include the graphics API extension (e.g. XR_KHR_D3D11_ENABLE_EXTENSION_NAME).
  bool CreateInstance(const std::vector<const char*>& extra_extensions = {});

  // Step 2: Locate the HMD system.
  bool InitializeSystem();

  // Step 2b: Query recommended per-eye render resolution.
  // Must be called before the backend creates swapchains.
  bool EnumerateViewConfigurations();

  // Step 3 (called by backend after creating session with graphics binding).
  void SetSession(XrSession session);

  // Step 3b: Register the backend swapchain implementation for use by the presenter.
  // Raw pointer — the backend object outlives the manager.
  void SetSwapchain(IOpenXRSwapchain* swapchain) { m_swapchain = swapchain; }
  IOpenXRSwapchain* GetSwapchain() const { return m_swapchain; }

  // Step 4: Create the local reference space used for head tracking.
  bool CreateReferenceSpace();

  // ---- Per-frame interface ----

  // Poll XrEvents and drive the session state machine.
  // Returns false if the render loop should stop.
  bool PollEvents();

  // xrWaitFrame — blocks until the runtime wants a new frame rendered.
  // Call at the start of each rendered frame.
  bool WaitFrame();

  // xrBeginFrame — signals the runtime that rendering has started.
  bool BeginFrame();

  // xrEndFrame — submits the completed layer stack to the compositor.
  bool EndFrame(const std::vector<XrCompositionLayerBaseHeader*>& layers);
  bool EndFrameDetached(XrTime display_time, XrEnvironmentBlendMode environment_blend_mode,
                        bool should_render,
                        const std::vector<XrCompositionLayerBaseHeader*>& layers,
                        bool lock_graphics_queue = true);

  // xrLocateViews — fills m_eye_views with the predicted head pose for each eye.
  // Call between BeginFrame and rendering.
  bool LocateViews();

  // Request recentering of the VR home position.
  // Applied on the OpenXR render thread during LocateViews.
  void RequestRecenter();

  // ---- Flat mono panel (vr_flat_screen) ----

  // Aspect ratio (width / height) of the flat game panel. Set by the presenter each frame
  // before SubmitFlatFrame so the quad's world size matches the game's output.
  void SetFlatScreenAspect(float aspect) { m_flat_screen_aspect = aspect; }

  // Build a world-locked XrCompositionLayerQuad from an already-rendered mono swapchain image
  // and submit it via EndFrame(). Distance/size come from vr_screen_distance/vr_screen_size;
  // pose is captured on first use and recomputed on recenter. Backends call this from
  // SubmitFlatFrame(); passthrough layers are prepended centrally in EndFrameDetached.
  bool SubmitFlatQuadFrame(XrSwapchain swapchain, uint32_t width, uint32_t height);

  // ---- Accessors ----

  XrInstance GetInstance() const { return m_instance; }
  XrSystemId GetSystemId() const { return m_system_id; }
  XrSession GetSession() const { return m_session; }
  XrSpace GetReferenceSpace() const { return m_reference_space; }

  XrTime GetPredictedDisplayTime() const { return m_frame_state.predictedDisplayTime; }
  double GetEstimatedDisplayPeriodMs() const { return m_estimated_display_period_ms; }
  float GetStartupDisplayRefreshRateHz() const { return m_startup_display_refresh_rate_hz; }
  // HMD's native display period from XrFrameState (e.g. 11.11ms for 90Hz)
  double GetNativeDisplayPeriodMs() const
  {
    return static_cast<double>(m_frame_state.predictedDisplayPeriod) / 1000000.0;
  }
  bool IsSessionRunning() const { return m_session_running; }
  bool IsSessionFocused() const { return m_session_focused; }
  bool ShouldRender() const { return m_frame_state.shouldRender == XR_TRUE; }

  const std::array<XREyeView, 2>& GetEyeViews() const { return m_eye_views; }
  // The pose actually used by the GS cache to render the current game frame.
  const std::array<XREyeView, 2>& GetRenderedEyeViews() const { return m_rendered_eye_views; }
  // The pose submitted to OpenXR for the current game frame. Usually this matches
  // GetRenderedEyeViews(), but no-tracking mode submits the real HMD pose so the
  // fixed rendered view stays locked to the headset instead of the play space.
  const std::array<XREyeView, 2>& GetSubmittedEyeViews() const { return m_submitted_eye_views; }
  void RecordRenderedEyeViews();
  const std::array<XrViewConfigurationView, 2>& GetViewConfigViews() const
  {
    return m_view_config_views;
  }

  // Compute per-eye projection rows with head rotation and eye position baked in.
  //
  // out_proj_rows[0..1] = left-eye rows 0,1; [2..3] = right-eye rows 0,1.
  //   x_clip = dot(row0, viewPos),  y_clip = dot(row1, viewPos)
  //   Head rotation and eye position (scaled by units_per_meter) are baked in.
  //
  // out_z_rows[0..1] = left/right eye z-axis row.
  //   z_eye = dot(z_row, viewPos) gives eye-space depth for perspective divide
  //   (f.pos.w = -z_eye) and depth buffer recomputation.
  void GetEyeProjectionRows(
      float units_per_meter,
      std::array<std::array<float, 4>, 4>& out_proj_rows,
      std::array<std::array<float, 4>, 2>& out_z_rows) const;

  // Compute per-eye projection rows WITHOUT head rotation (for head-locked content).
  // Same layout as GetEyeProjectionRows but only includes the raw asymmetric frustum
  // projection and per-eye IPD offset. Content rendered with these rows follows
  // the user's head movements.
  void GetRawEyeProjectionRows(
      float units_per_meter,
      std::array<std::array<float, 4>, 4>& out_proj_rows) const;

  // True for Virtual Desktop / Quest-class runtimes (strict about runtime-owned Vulkan
  // swapchain image synchronization). Used to scope the release-path GPU-drain fallback.
  bool IsQuestOrVirtualDesktopRuntime() const;

  // ---- XR_FB_foveation (fixed foveated rendering) ----

  // Foveation extensions the runtime advertises, for the backend's CreateInstance list:
  // XR_FB_foveation, XR_FB_foveation_configuration, XR_FB_swapchain_update_state, and
  // (when for_vulkan) XR_FB_foveation_vulkan. Empty when the base extensions are missing.
  // Call before CreateInstance().
  static std::vector<const char*> GetAvailableFoveationExtensions(bool for_vulkan);

  // True when the foveation extensions are enabled on this instance and the user's
  // FoveationLevel setting is not Off. Backends check this before creating swapchains
  // with a foveation profile request.
  bool IsFoveationUsable() const;

  // Create a foveation profile from the FoveationLevel/DynamicFoveation settings and
  // apply it to the swapchain via xrUpdateSwapchainFB. Call after xrCreateSwapchain
  // (the swapchain must have been created with XrSwapchainCreateInfoFoveationFB).
  bool ApplyFoveationToSwapchain(XrSwapchain swapchain);

  // True when the runtime supports XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND.
  bool SupportsAlphaBlend() const;

  // True when the headset can show its camera feed behind transparent projection-layer
  // pixels, either via XR_FB_passthrough (Quest family, standalone and Link) or via the
  // ALPHA_BLEND environment blend mode (Varjo, HoloLens, ...).
  bool SupportsPassthrough() const;

  // Blend mode for xrEndFrame. ALPHA_BLEND only when passthrough is on and the runtime
  // has no XR_FB_passthrough (the FB layer composites behind an OPAQUE projection layer).
  XrEnvironmentBlendMode GetActiveBlendMode() const;

  // Extra XrCompositionLayerFlags the backends must set on the projection layer so the
  // compositor reads its per-pixel alpha. 0 when passthrough is off or unsupported.
  XrCompositionLayerFlags GetProjectionLayerExtraFlags() const;

private:
  // XR_FB_passthrough: usable when the extension loaded and the system reports support.
  bool IsFBPassthroughUsable() const;
  // Create/start or pause the FB passthrough feed to match the Passthrough setting.
  // Requires an XrSession; called once per submitted frame from EndFrameDetached.
  void UpdateFBPassthrough(bool enable);
  void DestroyFBPassthrough();

  bool InitializeInputActions();
  void DestroyInputActions();
  void UpdateInputActions();
  void UpdateHaptics();
  void EnsureHomePositionFromCurrentViews() const;
  std::array<XREyeView, 2> GetTrackingAdjustedEyeViews() const;
  void ResetInputActionsState();
  void HandleSessionStateChange(XrSessionState new_state);
  // World-locked quad pose for the flat panel, in reference space. Captured lazily from the
  // current head pose and invalidated on recenter.
  XrPosef GetFlatScreenPose() const;
  void CaptureStartupDisplayRefreshRateFromExtension();
  void SetStartupDisplayRefreshRate(float refresh_rate_hz, std::string_view source);
  void RequestConfiguredDisplayRefreshRate();

  XrInstance m_instance = XR_NULL_HANDLE;
  XrSystemId m_system_id = XR_NULL_SYSTEM_ID;
  XrSession m_session = XR_NULL_HANDLE;
  XrSpace m_reference_space = XR_NULL_HANDLE;
  XrReferenceSpaceType m_reference_space_type = XR_REFERENCE_SPACE_TYPE_LOCAL;

  // Non-owning pointer; lifetime managed by the backend (D3DOpenXR).
  IOpenXRSwapchain* m_swapchain = nullptr;

  std::vector<std::string> m_enabled_extensions;
  std::string m_runtime_name;
  std::string m_system_name;
  // Cached IsQuestOrVirtualDesktopRuntime() result. Derived from the runtime/system names
  // (fixed after instance/system init) and queried per draw on the Vulkan path — the name
  // scans must not run per draw (profiled hot).
  mutable std::optional<bool> m_quest_or_vd_runtime;
  uint32_t m_system_vendor_id = 0;
  PFN_xrGetDisplayRefreshRateFB m_xrGetDisplayRefreshRateFB = nullptr;
  PFN_xrRequestDisplayRefreshRateFB m_xrRequestDisplayRefreshRateFB = nullptr;
  float m_requested_display_refresh_rate_hz = 0.0f;
  bool m_output_refresh_rate_unavailable_logged = false;

  // XR_FB_foveation entry points (null when the extension is unavailable).
  PFN_xrCreateFoveationProfileFB m_xrCreateFoveationProfileFB = nullptr;
  PFN_xrDestroyFoveationProfileFB m_xrDestroyFoveationProfileFB = nullptr;
  PFN_xrUpdateSwapchainFB m_xrUpdateSwapchainFB = nullptr;

  // XR_FB_passthrough entry points (null when the extension is unavailable).
  PFN_xrCreatePassthroughFB m_xrCreatePassthroughFB = nullptr;
  PFN_xrDestroyPassthroughFB m_xrDestroyPassthroughFB = nullptr;
  PFN_xrPassthroughStartFB m_xrPassthroughStartFB = nullptr;
  PFN_xrPassthroughPauseFB m_xrPassthroughPauseFB = nullptr;
  PFN_xrCreatePassthroughLayerFB m_xrCreatePassthroughLayerFB = nullptr;
  PFN_xrDestroyPassthroughLayerFB m_xrDestroyPassthroughLayerFB = nullptr;
  PFN_xrPassthroughLayerPauseFB m_xrPassthroughLayerPauseFB = nullptr;
  PFN_xrPassthroughLayerResumeFB m_xrPassthroughLayerResumeFB = nullptr;

  // XR_FB_passthrough state. The composition layer member gives the struct stable
  // storage across the xrEndFrame call that references it.
  bool m_system_supports_fb_passthrough = false;
  XrPassthroughFB m_fb_passthrough = XR_NULL_HANDLE;
  XrPassthroughLayerFB m_fb_passthrough_layer = XR_NULL_HANDLE;
  bool m_fb_passthrough_running = false;
  bool m_fb_passthrough_create_failed = false;
  XrCompositionLayerPassthroughFB m_fb_passthrough_composition_layer{
      XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB};

#if defined(ANDROID)
  PFN_xrVoidFunction m_xrSetAndroidApplicationThreadKHR = nullptr;
  PFN_xrVoidFunction m_xrPerfSettingsSetPerformanceLevelEXT = nullptr;

  // Threads that called RegisterCurrentAndroidThread before the XrSession existed.
  // Replayed by FlushPendingAndroidThreadRegistrations() from SetSession().
  // Reason: Meta's runtime needs threads tagged via XR_KHR_android_thread_settings to
  // apply big.LITTLE pinning + DVFS escalation; without tags it keeps clocks idle.
  struct PendingAndroidThreadRegistration
  {
    uint32_t thread_id;
    AndroidThreadType type;
    std::string label;
  };
  std::mutex m_pending_thread_registrations_mutex;
  std::vector<PendingAndroidThreadRegistration> m_pending_thread_registrations;

  void FlushPendingAndroidThreadRegistrations();
#endif

  XrSessionState m_session_state = XR_SESSION_STATE_UNKNOWN;
  bool m_session_running = false;
  bool m_session_focused = false;
  bool m_exit_render_loop = false;

  XrFrameState m_frame_state{XR_TYPE_FRAME_STATE};
  XrViewStateFlags m_view_state_flags = 0;

  // OpenXR input action set used to expose VR controller input to Dolphin's
  // regular controller mapping UI.
  XrActionSet m_input_action_set = XR_NULL_HANDLE;
  std::array<XrPath, 2> m_input_hand_paths{XR_NULL_PATH, XR_NULL_PATH};
  XrAction m_action_primary_click = XR_NULL_HANDLE;
  XrAction m_action_secondary_click = XR_NULL_HANDLE;
  XrAction m_action_menu_click = XR_NULL_HANDLE;
  XrAction m_action_thumbstick_click = XR_NULL_HANDLE;
  XrAction m_action_trigger_click = XR_NULL_HANDLE;
  XrAction m_action_squeeze_click = XR_NULL_HANDLE;
  XrAction m_action_trigger_value = XR_NULL_HANDLE;
  XrAction m_action_squeeze_value = XR_NULL_HANDLE;
  XrAction m_action_squeeze_force = XR_NULL_HANDLE;
  XrAction m_action_thumbstick_x = XR_NULL_HANDLE;
  XrAction m_action_thumbstick_y = XR_NULL_HANDLE;
  XrAction m_action_aim_pose = XR_NULL_HANDLE;
  XrAction m_action_grip_pose = XR_NULL_HANDLE;
  XrAction m_action_haptic = XR_NULL_HANDLE;
  std::array<XrSpace, 2> m_aim_spaces{XR_NULL_HANDLE, XR_NULL_HANDLE};
  std::array<XrSpace, 2> m_grip_spaces{XR_NULL_HANDLE, XR_NULL_HANDLE};
  std::array<bool, 2> m_haptics_active{false, false};
  std::array<XrPath, 2> m_logged_interaction_profiles{XR_NULL_PATH, XR_NULL_PATH};

  std::array<XrViewConfigurationView, 2> m_view_config_views{};
  std::array<XrView, 2> m_views{};
  std::vector<XrEnvironmentBlendMode> m_supported_blend_modes;
  std::array<XREyeView, 2> m_eye_views{};
  std::array<XREyeView, 2> m_rendered_eye_views{};
  std::array<XREyeView, 2> m_submitted_eye_views{};
  XrTime m_last_predicted_display_time = 0;
  double m_estimated_display_period_ms = 0.0;
  float m_startup_display_refresh_rate_hz = 0.0f;

  // "Home" head-center position, recorded from the first stable tracked pose.
  // All positional tracking is relative to this to avoid floor-level offsets.
  mutable bool m_home_set{false};
  mutable XrVector3f m_home_position{0.f, 0.f, 0.f};
  std::atomic<bool> m_recenter_requested{false};

  // Flat mono panel state. The quad pose is captured lazily and invalidated on recenter; the
  // composition layer member gives stable storage across the xrEndFrame call that references it.
  float m_flat_screen_aspect = 16.0f / 9.0f;
  mutable bool m_flat_screen_pose_valid = false;
  mutable XrPosef m_flat_screen_pose{{0.f, 0.f, 0.f, 1.f}, {0.f, 0.f, 0.f}};
  XrCompositionLayerQuad m_flat_quad_layer{XR_TYPE_COMPOSITION_LAYER_QUAD};
};

// Global instance — created by the backend during VideoBackend::Initialize().
extern std::unique_ptr<OpenXRManager> g_openxr;

}  // namespace VR

#endif  // ENABLE_VR
