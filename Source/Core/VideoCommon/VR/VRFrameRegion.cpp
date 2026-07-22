// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/VR/VRFrameRegion.h"

#include <algorithm>
#include <cmath>

#include "Common/Logging/Log.h"
#include "VideoCommon/XFMemory.h"

namespace VR
{
namespace
{
// Union of the XFB copy rects seen since the current frame's first copy.
VRFrameRegion s_pending{};
// Previous completed frame's display region — what consumers read.
VRFrameRegion s_current{};

bool SameRegion(const VRFrameRegion& a, const VRFrameRegion& b)
{
  return a.left == b.left && a.top == b.top && a.width == b.width && a.height == b.height;
}
}  // namespace

void NotifyVRXFBCopyRegion(int left, int top, int right, int bottom)
{
  const int width = right - left;
  const int height = bottom - top;
  if (width <= 0 || height <= 0)
    return;

  if (!s_pending.valid || top <= s_pending.top)
  {
    // First copy of a new frame (games emit strips top-down; a copy starting at or above
    // the pending union restarts the accumulation). Commit the finished union.
    if (s_pending.valid)
    {
      if (!SameRegion(s_pending, s_current))
      {
        INFO_LOG_FMT(VIDEO, "VR_FRAME: display region {}x{} at ({},{})", s_pending.width,
                     s_pending.height, s_pending.left, s_pending.top);
      }
      s_current = s_pending;
      s_current.valid = true;
    }
    s_pending = VRFrameRegion{left, top, width, height, true};
  }
  else
  {
    // Additional strip of the same frame (hybrid-XFB games copy the display in pieces).
    const int new_left = std::min(s_pending.left, left);
    const int new_top = std::min(s_pending.top, top);
    const int new_right = std::max(s_pending.left + s_pending.width, right);
    const int new_bottom = std::max(s_pending.top + s_pending.height, bottom);
    s_pending = VRFrameRegion{new_left, new_top, new_right - new_left, new_bottom - new_top, true};
  }
}

VRFrameRegion GetVRFrameRegion()
{
  return s_current;
}

void ResetVRFrameRegion()
{
  s_pending = VRFrameRegion{};
  s_current = VRFrameRegion{};
}

// Port of Hydra's SetViewportType (VertexShaderManager.cpp), measured against the XFB
// display region instead of Hydra's g_final_screen_region. Thresholds kept identical —
// they are field-proven across Hydra's game library:
//  - "full" means >= 90% of the frame extent, anchored within the remaining 10%.
//  - Full-width bands that aren't split-screen halves are LETTERBOXED (this includes
//    Metroid Prime's morph-ball 640x358 viewport, which must stay head-tracked).
//  - Square, edge-anchored, multiple-of-8 viewports are render-to-texture passes.
VRViewportClass ClassifyVRViewport(const Viewport& v, int x_off, int y_off)
{
  const VRFrameRegion frame = GetVRFrameRegion();
  if (!frame.valid)
    return VRViewportClass::MainScene;

  const float width = 2.0f * std::fabs(v.wd);
  const float height = 2.0f * std::fabs(v.ht);
  // Position relative to the frame region's origin (EFB coords).
  const float left = (v.xOrig - std::fabs(v.wd)) - static_cast<float>(x_off + frame.left);
  const float top = (v.yOrig - std::fabs(v.ht)) - static_cast<float>(y_off + frame.top);
  const float screen_width = static_cast<float>(frame.width);
  const float screen_height = static_cast<float>(frame.height);
  const float min_screen_width = 0.9f * screen_width;
  const float min_screen_height = 0.9f * screen_height;
  const float max_left = screen_width - min_screen_width;
  const float max_top = screen_height - min_screen_height;

  // Square texture on any screen edge with size a multiple of 8 (Hydra's relaxed rule:
  // catches shadow/env maps incl. Twilight Princess's 216x216 and 384x384), except the
  // 512x512-on-512x512 games that really render like that.
  if (width == height &&
      (width == 1.0f || width == 2.0f || width == 4.0f ||
       (width >= 8.0f && std::fmod(width, 8.0f) == 0.0f)) &&
      (left == 0.0f || top == 0.0f || top == screen_height - height ||
       left == screen_width - width) &&
      !(width == 512.0f && screen_width == 512.0f && screen_height == 512.0f))
  {
    return VRViewportClass::RenderToTexture;
  }
  // Zelda Twilight Princess renders the map screen's coloured highlights with this
  // strange viewport (makes no sense as a real one).
  if (width == 457.0f && height == 341.0f && left == 0.0f && top == 0.0f)
    return VRViewportClass::RenderToTexture;

  // Full width: fullscreen, letterboxed, or top/bottom split-screen.
  if (width >= min_screen_width)
  {
    if (left > max_left)
      return VRViewportClass::Offscreen;
    if (height >= min_screen_height)
    {
      if (top > max_top)
        return VRViewportClass::Offscreen;
      if (width == screen_width && height == screen_height)
        return VRViewportClass::MainScene;
      return VRViewportClass::Letterboxed;
    }
    if (height >= min_screen_height * 0.5f && height <= screen_height * 0.5f)
    {
      if (top <= max_top)
        return VRViewportClass::SplitScreen;  // top half
      if (top >= height && top <= height + max_top)
        return VRViewportClass::SplitScreen;  // bottom half
      return VRViewportClass::Letterboxed;    // band across the middle
    }
    return VRViewportClass::Letterboxed;  // full-width band (cinematic bars, morph ball)
  }

  // Full height: left/right split-screen or a column.
  if (height >= min_screen_height)
  {
    if (top > max_top)
      return VRViewportClass::Offscreen;
    if (width >= min_screen_width * 0.5f)
    {
      if (left <= max_left)
        return VRViewportClass::SplitScreen;  // left half
      if (left >= width)
        return VRViewportClass::SplitScreen;  // right half
      return VRViewportClass::HudElement;     // column down the middle
    }
    return VRViewportClass::Letterboxed;  // narrow column (Hydra kept these head-tracked)
  }

  // Quadrants (4-player split-screen) — must be corner-anchored half-size viewports.
  if (width >= min_screen_width * 0.5f && height >= min_screen_height * 0.5f &&
      width <= screen_width * 0.5f && height <= screen_height * 0.5f)
  {
    const bool left_col = left <= max_left;
    const bool right_col = left >= width;
    const bool top_row = top <= max_top;
    const bool bottom_row = top >= height;
    if ((left_col || right_col) && (top_row || bottom_row))
      return VRViewportClass::SplitScreen;
    return VRViewportClass::HudElement;
  }

  // Entirely outside the displayed region.
  if (left >= screen_width || top >= screen_height || left + width <= 0.0f ||
      top + height <= 0.0f)
  {
    return VRViewportClass::Offscreen;
  }

  return VRViewportClass::HudElement;
}

const char* GetVRViewportClassName(VRViewportClass vclass)
{
  switch (vclass)
  {
  case VRViewportClass::MainScene:
    return "MainScene";
  case VRViewportClass::Letterboxed:
    return "Letterboxed";
  case VRViewportClass::SplitScreen:
    return "SplitScreen";
  case VRViewportClass::HudElement:
    return "HudElement";
  case VRViewportClass::RenderToTexture:
    return "RenderToTexture";
  case VRViewportClass::Offscreen:
    return "Offscreen";
  }
  return "Unknown";
}
}  // namespace VR
