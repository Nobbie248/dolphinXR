// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Common/CommonTypes.h"
#include "VideoCommon/ShaderHunter.h"

// Texture Element Override: reclassify VR draws (Skip / Screen / Fullscreen / Head Locked / Units
// per Meter) based purely on the bound texture hash, regardless of which shader draws it.
//
// Unlike ShaderHunter's per-shader texture filter (which only applies in combination with a
// specific shader hash), this matches across all shaders/elements: every draw that binds a
// listed texture receives the override's handling. Each override is a named group holding a list
// of texture hashes that share one handling.
//
// Precedence at draw time: this is a fallback — Elements Group Override and Shader Override are
// resolved first, and a Texture Element Override only applies when neither matched the draw.
//
// Thread-safety mirrors ShaderHunter: LoadOverrides() (UI thread) locks; the per-draw match path
// reads the lookup maps lock-free. The Texture Hunter capture/swap/query path is mutex-guarded
// because it runs concurrently with the UI while the browser is open.
class TextureElementManager
{
public:
  using HandlingType = ShaderHunter::HandlingType;
  using TextureUsage = ShaderHunter::TextureUsage;

  // --- Persistent overrides (per-game INI) ---
  struct TextureElementOverride
  {
    std::string name;
    std::string comments;
    HandlingType handling = HandlingType::Skip;
    int layer = -1;                 // Manual layer for Screen/HeadLocked (-1 = auto)
    float element_depth = -1.0f;    // Per-override within-element depth (-1 = global)
    float units_per_meter = -1.0f;  // Per-override UPM for UnitsPerMeter handling (-1 = global)
    bool clear_efb = false;         // Clear the next EFB copy to transparent when matched
    int clear_efb_min_width = 0;    // Min native width for EFB clear (0 = no lower bound)
    int clear_efb_max_width = 0;    // Max native width for EFB clear (0 = no upper bound)
    std::vector<u64> texture_hashes;  // The group's textures (matched when any is bound)
    bool enabled = true;
  };

  static TextureElementManager& GetInstance();

  // Static INI helpers (used by both this manager and the Qt pane).
  static std::vector<TextureElementOverride> LoadOverridesFromINI(
      const std::string& game_id, std::optional<u16> revision = std::nullopt);
  static void SaveOverridesToINI(const std::string& game_id,
                                 const std::vector<TextureElementOverride>& overrides);

  // Runtime override management.
  void LoadOverrides(const std::string& game_id);
  void LoadOverridesIfNeeded(const std::string& game_id);
  bool HasOverrides() const;
  // True when the per-draw bound texture hashes are needed (overrides exist or hunter is open).
  bool NeedsTextureHashes() const;

  // --- Video-thread match path (bound = 8 currently-bound texture hashes) ---
  // True if any bound texture maps to a Skip override.
  bool ShouldSkipByTexture(const std::array<u64, 8>& bound) const;
  // Handling for the first bound texture with a non-Skip override (Skip if none). Out-params are
  // filled for Screen/HeadLocked (layer, element_depth) and UnitsPerMeter (units_per_meter).
  HandlingType GetHandlingForTextures(const std::array<u64, 8>& bound, int* layer,
                                      float* element_depth, float* units_per_meter) const;

  // ClearEFB: arm a pending clear when a clear_efb override matches; consumed by the next EFB copy.
  void CheckClearEFBForDraw(const std::array<u64, 8>& bound);
  bool ShouldClearEFBCopy(int width);

  // --- Texture Hunter browse support (global per-frame texture capture) ---
  void SetHunterActive(bool active);
  bool IsHunterActive() const;
  void CaptureDrawTextures(const std::array<u64, 8>& hashes,
                           const std::array<std::string, 8>& names);
  void OnFrameEnd();
  std::vector<TextureUsage> GetCurrentTextures() const;

  // --- Live preview (Texture Hunter browser open) ---
  // Draws binding a preview texture are skipped or pink-highlighted in-game without being saved,
  // mirroring the Shader Override tool's texture preview. The set is cleared when the hunter is
  // deactivated.
  void SetPreviewTextures(const std::vector<u64>& hashes);
  // Preview mode: false = Skip (hide the draw), true = Pink (magenta highlight).
  void SetPreviewPink(bool pink);
  bool IsPreviewPink() const;
  // Video thread: true if any bound texture is in the live preview set.
  bool HasPreviewMatch(const std::array<u64, 8>& bound) const;

private:
  TextureElementManager() = default;

  struct ResolvedHandling
  {
    HandlingType handling = HandlingType::Skip;
    int layer = -1;
    float element_depth = -1.0f;
    float units_per_meter = -1.0f;
  };

  mutable std::mutex m_mutex;

  // Lookup maps: written by LoadOverrides, read lock-free on the video thread.
  std::vector<TextureElementOverride> m_overrides;
  std::unordered_map<u64, ResolvedHandling> m_texture_handling;  // texture hash -> handling
  std::unordered_set<u64> m_clear_efb_textures;
  std::unordered_map<u64, std::pair<int, int>> m_clear_efb_bounds;  // hash -> (min_w, max_w)
  std::string m_loaded_game_id;
  std::atomic_bool m_has_overrides = false;

  // ClearEFB pending state (video thread only).
  bool m_clear_next_efb = false;
  int m_pending_clear_min = 0;
  int m_pending_clear_max = 0;

  // Texture Hunter: double-buffered "all textures seen this frame" (mutex-guarded).
  std::atomic_bool m_hunter_active = false;
  std::unordered_map<u64, std::string> m_textures_collecting;
  std::unordered_map<u64, std::string> m_textures_display;

  // Live preview: textures checked in the browser (mutex-guarded; gated by m_has_preview).
  std::atomic_bool m_has_preview = false;
  std::atomic_bool m_preview_pink = false;  // false = Skip, true = Pink highlight
  std::unordered_set<u64> m_preview_textures;
};
