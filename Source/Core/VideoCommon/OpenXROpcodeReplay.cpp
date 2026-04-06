// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/OpenXROpcodeReplay.h"

#include <cmath>
#include <mutex>
#include <vector>

#include "VideoCommon/VideoConfig.h"

namespace VideoCommon::OpenXROpcodeReplay
{
namespace
{
struct ReplayClearState
{
  bool color_enable = false;
  bool alpha_enable = false;
  bool z_enable = false;
  PixelFormat pixel_format = PixelFormat::RGBA6_Z24;
  u32 color = 0;
  u32 clear_z_value = 0;
  bool valid = false;
};

struct ReplayState
{
  std::mutex mutex;
  std::vector<u8> capture_preprocess;
  std::vector<u8> capture_main;
  std::vector<u8> replay_preprocess;
  std::vector<u8> replay_main;
  u64 replay_frame_index = 0;
  u64 next_frame_index = 0;
  bool capture_enabled = false;
  bool replaying = false;
  bool replay_frame_active = false;
  bool replay_log_frame_active = false;
  bool pending_immediate_swap = false;
  bool pending_immediate_swap_is_frame_boundary = false;
  u32 pending_xfb_addr = 0;
  u32 pending_fb_width = 0;
  u32 pending_fb_stride = 0;
  u32 pending_fb_height = 0;
  u32 replay_xfb_count = 0;
  ReplayClearState clear_states[2];
};

ReplayState s_state;

bool IsConfiguredUnlocked()
{
  return g_ActiveConfig.stereo_mode == StereoMode::OpenXR &&
         g_ActiveConfig.vr_opcode_replay_mode != OpenXROpcodeReplayMode::Off;
}

void ClearUnlocked(ReplayState& state)
{
  state.capture_preprocess.clear();
  state.capture_main.clear();
  state.replay_preprocess.clear();
  state.replay_main.clear();
  state.capture_enabled = false;
  state.replaying = false;
  state.replay_frame_active = false;
  state.replay_log_frame_active = false;
  state.pending_immediate_swap = false;
  state.pending_immediate_swap_is_frame_boundary = false;
  state.pending_xfb_addr = 0;
  state.pending_fb_width = 0;
  state.pending_fb_stride = 0;
  state.pending_fb_height = 0;
  state.replay_xfb_count = 0;
  state.clear_states[0] = {};
  state.clear_states[1] = {};
}
}  // namespace

bool IsCaptureEnabled()
{
  std::scoped_lock lock(s_state.mutex);
  s_state.capture_enabled = IsConfiguredUnlocked();
  return s_state.capture_enabled;
}

bool IsReplaying()
{
  std::scoped_lock lock(s_state.mutex);
  return s_state.replaying;
}

bool IsReplayFrameActive()
{
  std::scoped_lock lock(s_state.mutex);
  return s_state.replay_frame_active;
}

bool IsReplayLogFrameActive()
{
  std::scoped_lock lock(s_state.mutex);
  return s_state.replay_log_frame_active;
}

void EnableCaptureForNextFrame()
{
  std::scoped_lock lock(s_state.mutex);
  s_state.capture_enabled = IsConfiguredUnlocked();
  if (s_state.capture_enabled && !s_state.replaying)
    s_state.replay_log_frame_active = true;
}

void CaptureCommand(bool is_preprocess, const u8* data, u32 size)
{
  if (data == nullptr || size == 0)
    return;

  std::scoped_lock lock(s_state.mutex);
  s_state.capture_enabled = IsConfiguredUnlocked();
  if (!s_state.capture_enabled || s_state.replaying || !s_state.replay_log_frame_active)
    return;

  auto& stream = is_preprocess ? s_state.capture_preprocess : s_state.capture_main;
  stream.insert(stream.end(), data, data + size);
}

void NotifyFrameBoundary()
{
  std::scoped_lock lock(s_state.mutex);

  if (s_state.replaying)
    return;

  s_state.capture_enabled = IsConfiguredUnlocked();
  if (!s_state.capture_enabled)
  {
    ClearUnlocked(s_state);
    return;
  }

  if (s_state.capture_preprocess.empty() && s_state.capture_main.empty())
    return;

  s_state.replay_preprocess = std::move(s_state.capture_preprocess);
  s_state.replay_main = std::move(s_state.capture_main);
  s_state.capture_preprocess.clear();
  s_state.capture_main.clear();
  s_state.replay_frame_index = s_state.next_frame_index++;
  s_state.replay_log_frame_active = false;
}

void Clear()
{
  std::scoped_lock lock(s_state.mutex);
  ClearUnlocked(s_state);
}

bool HasReplayData()
{
  std::scoped_lock lock(s_state.mutex);
  return !s_state.replay_preprocess.empty() || !s_state.replay_main.empty();
}

int GetReplayCount(double display_period_ms)
{
  std::scoped_lock lock(s_state.mutex);

  s_state.capture_enabled = IsConfiguredUnlocked();
  if (!s_state.capture_enabled || s_state.replaying ||
      (s_state.replay_preprocess.empty() && s_state.replay_main.empty()))
  {
    return 0;
  }

  constexpr double target_period_ms = 1000.0 / 90.0;
  constexpr double tolerance_ms = 0.8;
  if (!(display_period_ms > 0.0 &&
        std::abs(display_period_ms - target_period_ms) <= tolerance_ms))
  {
    return 0;
  }

  switch (g_ActiveConfig.vr_opcode_replay_mode)
  {
  case OpenXROpcodeReplayMode::Replay60To90:
    return (s_state.replay_frame_index % 2) == 1 ? 1 : 0;
  case OpenXROpcodeReplayMode::Replay30To90:
    return 2;
  case OpenXROpcodeReplayMode::Off:
  default:
    return 0;
  }
}

bool BeginReplayIteration()
{
  std::scoped_lock lock(s_state.mutex);
  if (s_state.replaying || (s_state.replay_preprocess.empty() && s_state.replay_main.empty()))
    return false;

  s_state.replaying = true;
  s_state.capture_enabled = IsConfiguredUnlocked();
  s_state.replay_frame_active = true;
  s_state.replay_log_frame_active = false;
  s_state.pending_immediate_swap = false;
  s_state.pending_immediate_swap_is_frame_boundary = false;
  s_state.pending_xfb_addr = 0;
  s_state.pending_fb_width = 0;
  s_state.pending_fb_stride = 0;
  s_state.pending_fb_height = 0;
  s_state.replay_xfb_count = 0;
  return true;
}

void EndReplayIteration()
{
  std::scoped_lock lock(s_state.mutex);
  s_state.replaying = false;
  s_state.replay_frame_active = false;
  s_state.replay_log_frame_active = false;
  s_state.pending_immediate_swap = false;
  s_state.pending_immediate_swap_is_frame_boundary = false;
  s_state.pending_xfb_addr = 0;
  s_state.pending_fb_width = 0;
  s_state.pending_fb_stride = 0;
  s_state.pending_fb_height = 0;
  s_state.replay_xfb_count = 0;
}

void DiscardReplayFrame()
{
  std::scoped_lock lock(s_state.mutex);
  s_state.replay_preprocess.clear();
  s_state.replay_main.clear();
  s_state.pending_immediate_swap = false;
  s_state.pending_immediate_swap_is_frame_boundary = false;
  s_state.pending_xfb_addr = 0;
  s_state.pending_fb_width = 0;
  s_state.pending_fb_stride = 0;
  s_state.pending_fb_height = 0;
  s_state.replay_xfb_count = 0;
}

void ApplyReplayClearState(bool frame_just_rendered, bool* color_enable, bool* alpha_enable,
                           bool* z_enable, PixelFormat* pixel_format, u32* color,
                           u32* clear_z_value)
{
  std::scoped_lock lock(s_state.mutex);
  s_state.capture_enabled = IsConfiguredUnlocked();
  if (!s_state.capture_enabled || color_enable == nullptr || alpha_enable == nullptr ||
      z_enable == nullptr || pixel_format == nullptr || color == nullptr ||
      clear_z_value == nullptr)
    return;

  ReplayClearState current = {
      .color_enable = *color_enable,
      .alpha_enable = *alpha_enable,
      .z_enable = *z_enable,
      .pixel_format = *pixel_format,
      .color = *color,
      .clear_z_value = *clear_z_value,
      .valid = true,
  };

  if (s_state.replay_frame_active && frame_just_rendered)
  {
    s_state.clear_states[0] = current;
    return;
  }

  if (!s_state.replay_frame_active && !frame_just_rendered)
  {
    s_state.clear_states[1] = current;
    return;
  }

  const ReplayClearState& selected =
      !s_state.replay_frame_active && frame_just_rendered ? s_state.clear_states[0] :
                                                            s_state.clear_states[1];
  if (!selected.valid)
    return;

  *color_enable = selected.color_enable;
  *alpha_enable = selected.alpha_enable;
  *z_enable = selected.z_enable;
  *pixel_format = selected.pixel_format;
  *color = selected.color;
  *clear_z_value = selected.clear_z_value;
}

void RecordReplayImmediateSwap(u32 xfb_addr, u32 fb_width, u32 fb_stride, u32 fb_height,
                               bool frame_just_rendered)
{
  std::scoped_lock lock(s_state.mutex);
  if (!s_state.replaying)
    return;

  s_state.replay_xfb_count++;

  if (s_state.pending_immediate_swap && (s_state.pending_immediate_swap_is_frame_boundary ||
                                         !frame_just_rendered))
  {
    return;
  }

  s_state.pending_immediate_swap = true;
  s_state.pending_immediate_swap_is_frame_boundary = frame_just_rendered;
  s_state.pending_xfb_addr = xfb_addr;
  s_state.pending_fb_width = fb_width;
  s_state.pending_fb_stride = fb_stride;
  s_state.pending_fb_height = fb_height;
}

bool ConsumeReplayImmediateSwap(u32* xfb_addr, u32* fb_width, u32* fb_stride, u32* fb_height)
{
  std::scoped_lock lock(s_state.mutex);
  if (!s_state.replaying || !s_state.pending_immediate_swap)
    return false;

  if (xfb_addr)
    *xfb_addr = s_state.pending_xfb_addr;
  if (fb_width)
    *fb_width = s_state.pending_fb_width;
  if (fb_stride)
    *fb_stride = s_state.pending_fb_stride;
  if (fb_height)
    *fb_height = s_state.pending_fb_height;

  s_state.pending_immediate_swap = false;
  s_state.pending_immediate_swap_is_frame_boundary = false;
  s_state.pending_xfb_addr = 0;
  s_state.pending_fb_width = 0;
  s_state.pending_fb_stride = 0;
  s_state.pending_fb_height = 0;
  return true;
}

u32 GetReplayXFBCount()
{
  std::scoped_lock lock(s_state.mutex);
  return s_state.replay_xfb_count;
}

std::span<const u8> GetReplayCommands(bool is_preprocess)
{
  std::scoped_lock lock(s_state.mutex);
  const auto& stream = is_preprocess ? s_state.replay_preprocess : s_state.replay_main;
  return std::span<const u8>(stream.data(), stream.size());
}
}  // namespace VideoCommon::OpenXROpcodeReplay
