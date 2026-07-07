// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#ifdef ENABLE_VR

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include "Common/CommonTypes.h"

namespace VR
{
class OpenXRUtilitySession final
{
public:
  OpenXRUtilitySession();
  ~OpenXRUtilitySession();

  OpenXRUtilitySession(const OpenXRUtilitySession&) = delete;
  OpenXRUtilitySession& operator=(const OpenXRUtilitySession&) = delete;

  bool Start();
  void Stop();
  bool IsRunning() const;

  void SetOverlayImage(std::vector<u8> pixels, uint32_t width, uint32_t height);

private:
  void ThreadMain();

  mutable std::mutex m_state_mutex;
  std::condition_variable m_start_cv;
  std::thread m_thread;
  std::atomic<bool> m_stop_requested{false};
  bool m_running = false;
  bool m_start_completed = false;
  bool m_start_succeeded = false;

  std::mutex m_overlay_mutex;
  std::vector<u8> m_overlay_pixels;
  uint32_t m_overlay_width = 1024;
  uint32_t m_overlay_height = 704;
  bool m_overlay_dirty = false;
};
}  // namespace VR

#endif  // ENABLE_VR
