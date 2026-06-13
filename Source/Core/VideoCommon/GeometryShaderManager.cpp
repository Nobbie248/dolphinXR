// Copyright 2014 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/GeometryShaderManager.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "Common/ChunkFile.h"
#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"
#include "Core/System.h"
#include "VideoCommon/BPMemory.h"
#include "VideoCommon/FreeLookCamera.h"
#include "VideoCommon/RenderState.h"
#include "VideoCommon/VertexShaderManager.h"
#include "VideoCommon/VideoConfig.h"
#include "VideoCommon/XFMemory.h"

#ifdef ENABLE_VR
#include "VideoCommon/OpenXROpcodeReplay.h"
#include "VideoCommon/VR/OpenXRManager.h"
#endif

static constexpr int LINE_PT_TEX_OFFSETS[8] = {0, 16, 8, 4, 2, 1, 1, 1};

namespace
{
static constexpr float METROID_PERSPECTIVE_HUD_FORWARD_OFFSET = 1.0f;

struct PerspectiveHudTransform
{
  std::array<float, 3> scale{};
  std::array<float, 3> position{};
  float zobj = 0.0f;
  float distance = 0.0f;
  float origin_z = 0.0f;
  float ref_minus_one_z = 0.0f;
  float ref_plus_one_z = 0.0f;
  float hydra_like_position_z = 0.0f;
  float hydra_like_origin_z = 0.0f;
  float znear = 0.0f;
  float zfar = 0.0f;
  float hydra_default_zobj = 0.0f;
  bool valid = false;
};

void ApplyRowTransform(std::array<std::array<float, 4>, 4>* rows, const Common::Matrix44& matrix)
{
  for (auto& row : *rows)
  {
    const std::array<float, 4> src = row;
    row[0] = src[0] * matrix.data[0] + src[1] * matrix.data[4] + src[2] * matrix.data[8] +
             src[3] * matrix.data[12];
    row[1] = src[0] * matrix.data[1] + src[1] * matrix.data[5] + src[2] * matrix.data[9] +
             src[3] * matrix.data[13];
    row[2] = src[0] * matrix.data[2] + src[1] * matrix.data[6] + src[2] * matrix.data[10] +
             src[3] * matrix.data[14];
    row[3] = src[0] * matrix.data[3] + src[1] * matrix.data[7] + src[2] * matrix.data[11] +
             src[3] * matrix.data[15];
  }
}

void ApplyRowTransform(std::array<std::array<float, 4>, 2>* rows, const Common::Matrix44& matrix)
{
  for (auto& row : *rows)
  {
    const std::array<float, 4> src = row;
    row[0] = src[0] * matrix.data[0] + src[1] * matrix.data[4] + src[2] * matrix.data[8] +
             src[3] * matrix.data[12];
    row[1] = src[0] * matrix.data[1] + src[1] * matrix.data[5] + src[2] * matrix.data[9] +
             src[3] * matrix.data[13];
    row[2] = src[0] * matrix.data[2] + src[1] * matrix.data[6] + src[2] * matrix.data[10] +
             src[3] * matrix.data[14];
    row[3] = src[0] * matrix.data[3] + src[1] * matrix.data[7] + src[2] * matrix.data[11] +
             src[3] * matrix.data[15];
  }
}

bool IsFinite(float value)
{
  return std::isfinite(value);
}

PerspectiveHudTransform CalculatePerspectiveHudTransform(const Projection::Raw& projection,
                                                         float units_per_meter,
                                                         float reference_view_z)
{
  PerspectiveHudTransform result;

  if (projection[0] == 0.0f || projection[2] == 0.0f || reference_view_z >= 0.0f)
    return result;  // need a valid frustum and a HUD model in front of the camera

  // Reference depth = the HUD model's ACTUAL view-space distance (|reference_view_z|), not the
  // frustum mid-point.  Metroid's HUD perspective frustum is enormous (znear~1, zfar~4000+), so a
  // frustum-interpolated reference put zobj in the thousands while the models actually sit only
  // ~20 units away.  That made scale = size_reference/zobj ~100x too small, shrinking the HUD to a
  // dot.  Using the real model depth makes the apparent size depend only on size_reference/distance
  // (i.e. the Size/Distance sliders), independent of the model's view-space depth.
  const float zobj = -reference_view_z;

  if (!IsFinite(zobj) || zobj < 1.0e-4f)
    return result;

  result.zobj = zobj;
  if (projection[4] != 0.0f && projection[4] != 1.0f)
  {
    result.zfar = projection[5] / projection[4];
    result.znear = result.zfar * projection[4] / (projection[4] - 1.0f);
    result.hydra_default_zobj = result.znear + (result.zfar - result.znear) * 0.5f;
  }

  const float left = (-(projection[1] + 1.0f) / projection[0]) * zobj;
  const float right = left + (2.0f / projection[0]) * zobj;
  const float bottom = (-(projection[3] + 1.0f) / projection[2]) * zobj;
  const float top = bottom + (2.0f / projection[2]) * zobj;
  const float width = right - left;
  const float height = top - bottom;

  if (width == 0.0f || height == 0.0f || !IsFinite(width) || !IsFinite(height))
    return result;

  result.distance = units_per_meter * g_ActiveConfig.vr_screen_distance;
  const float size_reference = units_per_meter * g_ActiveConfig.vr_screen_size;
  const float hud_width = std::abs((2.0f / projection[0]) * size_reference);
  const float hud_height = std::abs((2.0f / projection[2]) * size_reference);

  result.scale[0] = hud_width / width;
  result.scale[1] = hud_height / height;
  result.scale[2] = result.scale[0];
  result.position[0] = result.scale[0] * (-(right + left) * 0.5f);
  result.position[1] = result.scale[1] * (-(top + bottom) * 0.5f);
  // Centre the HUD model near the screen-distance plane, independent of Size, while biasing the
  // perspective HUD models slightly toward the viewer.  This preserves the original depth direction
  // but keeps the Prime beam/visor boxes in front of the flat HUD reference, matching Hydra.
  // reference_view_z is the model origin's view-space z (the position-matrix translation), so the
  // un-biased model sits exactly `distance` in front regardless of scale; the per-vertex
  // scale[2] * (viewPos.z - reference_view_z) deviation still supplies the relative model depth.
  // This decouples the sliders (Distance = forward placement, Size = scale).  The earlier
  // `scale[2] * zobj - distance` coupled them and assumed the model sat at view-z == -zobj;
  // a plain -distance assumed view-z == 0.  Neither held, so the HUD floated too far away.
  result.position[2] = result.scale[2] * (zobj + METROID_PERSPECTIVE_HUD_FORWARD_OFFSET) -
                       result.distance;
  result.origin_z = result.scale[2] * reference_view_z + result.position[2];
  result.ref_minus_one_z = result.scale[2] * (reference_view_z - 1.0f) + result.position[2];
  result.ref_plus_one_z = result.scale[2] * (reference_view_z + 1.0f) + result.position[2];
  result.hydra_like_position_z = result.scale[2] * zobj - result.distance;
  result.hydra_like_origin_z = result.scale[2] * reference_view_z + result.hydra_like_position_z;

  result.valid = IsFinite(result.scale[0]) && IsFinite(result.scale[1]) &&
                 IsFinite(result.scale[2]) && IsFinite(result.position[0]) &&
                 IsFinite(result.position[1]) && IsFinite(result.position[2]) &&
                 IsFinite(result.distance) && IsFinite(result.origin_z) &&
                 IsFinite(result.ref_minus_one_z) && IsFinite(result.ref_plus_one_z);
  return result;
}
}  // namespace

void GeometryShaderManager::Init()
{
  constants = {};
  m_vr_hud_shared_reference_valid = false;
  m_vr_hud_stable_reference_valid = false;
  m_vr_hud_frame_anchor_candidate_valid = false;

  // Init any initial constants which aren't zero when bpmem is zero.
  SetViewportChanged();
  SetProjectionChanged();

  dirty = true;
}

void GeometryShaderManager::Dirty()
{
  // This function is called after a savestate is loaded.
  // Any constants that can changed based on settings should be re-calculated
  m_projection_changed = true;

  // Uses EFB scale config
  SetLinePtWidthChanged();

  dirty = true;
}

void GeometryShaderManager::SetVSExpand(VSExpand expand)
{
  if (constants.vs_expand != expand)
  {
    constants.vs_expand = expand;
    dirty = true;
  }
}

void GeometryShaderManager::SetConstants(PrimitiveType prim)
{
  if (g_ActiveConfig.stereo_mode != StereoMode::Off)
  {
#ifdef ENABLE_VR
    const bool openxr_mode = (g_ActiveConfig.stereo_mode == StereoMode::OpenXR);
#else
    const bool openxr_mode = false;
#endif

    if (m_projection_changed || openxr_mode)
    {
      m_projection_changed = false;

#ifdef ENABLE_VR
      if (openxr_mode)
      {
        auto& system = Core::System::GetInstance();
        auto& vertex_shader_manager = system.GetVertexShaderManager();
        const bool perspective = xfmem.projection.type == ProjectionType::Perspective;
        constants.stereoparams = {0.0f, 0.0f, 0.0f, 0.0f};
        constants.eye_projection = {};
        constants.eye_z_row = {};
        constants.depth_params = {};
        constants.vr_screen = {};
        constants.head_projection = {};
        constants.head_locked_params = {};
        constants.pixel_center_correction = {};
        const float upm_override = vr_units_per_meter_override;
        vr_units_per_meter_override = -1.0f;  // consume
        const float headlocked_projection_scale_x = vr_headlocked_projection_scale_x;
        const float headlocked_projection_scale_y = vr_headlocked_projection_scale_y;
        const float headlocked_projection_offset_x = vr_headlocked_projection_offset_x;
        const float headlocked_projection_offset_y = vr_headlocked_projection_offset_y;
        const bool metroid_hud_self_center = vr_metroid_hud_self_center;
        vr_metroid_hud_self_center = false;  // consume
        const bool metroid_hud_anchor_candidate = vr_metroid_hud_anchor_candidate;
        vr_metroid_hud_anchor_candidate = false;  // consume
        vr_headlocked_projection_scale_x = 1.0f;
        vr_headlocked_projection_scale_y = 1.0f;
        vr_headlocked_projection_offset_x = 0.0f;
        vr_headlocked_projection_offset_y = 0.0f;
        // Hoisted out of the session block so it is also available when finalizing the
        // ortho/head-locked depth_params (HUD thickness) below.
        const float upm = std::max(
            upm_override > 0.0f ? upm_override : g_ActiveConfig.vr_units_per_meter, 0.0001f);

        if (VR::g_openxr && VR::g_openxr->IsSessionRunning())
        {

          // When vr_lock_head_pose is ON, only re-fetch the head pose from OpenXR when
          // we've been explicitly invalidated (at the XFB-copy frame boundary).  This
          // prevents mid-frame LocateViews() updates from desynchronising different draw
          // calls within the same game frame.  When OFF, refetch every call (legacy
          // behavior — kept as an escape hatch).
          // During opcode replay, never refresh: the real frame's cache is already
          // correct, and BPStructs' XFB-copy LocateViews has mutated m_eye_views
          // between real-frame draws and replay-frame draws.  Refreshing here would
          // render the replay with a different pose than the real frame it pairs
          // with, producing alternating-frame flicker on head rotation.
          const bool is_replay = VideoCommon::OpenXROpcodeReplay::IsReplaying();
          const bool upm_changed = std::abs(upm - m_cached_units_per_meter) > 0.0001f;
          const bool need_refresh = !is_replay && (upm_changed ||
                                                   !g_ActiveConfig.vr_lock_head_pose ||
                                                   m_vr_pose_needs_refresh);
          if (need_refresh)
          {
            std::array<std::array<float, 4>, 4> eye_projection_rows{};
            std::array<std::array<float, 4>, 2> eye_z_rows{};
            VR::g_openxr->GetEyeProjectionRows(upm, eye_projection_rows, eye_z_rows);

            // OpenXR stereo path bypasses the classic cproj path, so apply freelook here too.
            if (perspective && g_freelook_camera.IsActive())
            {
              const Common::Matrix44 freelook_view = g_freelook_camera.GetView();
              ApplyRowTransform(&eye_projection_rows, freelook_view);
              ApplyRowTransform(&eye_z_rows, freelook_view);
            }

            m_cached_eye_projection = eye_projection_rows;
            m_cached_eye_z_row = eye_z_rows;
            m_cached_units_per_meter = upm;

            // Unrotated per-eye projection rows for head-locked content.
            std::array<std::array<float, 4>, 4> head_proj_rows{};
            VR::g_openxr->GetRawEyeProjectionRows(upm, head_proj_rows);
            m_cached_head_projection = head_proj_rows;

            // Snapshot the pose the cache was built from.  SubmitFrame will use
            // this snapshot so render_pose == submit_pose regardless of any
            // later LocateViews that may clobber m_eye_views before xrEndFrame.
            VR::g_openxr->RecordRenderedEyeViews();

            m_vr_pose_needs_refresh = false;
          }

          constants.eye_projection[0] = m_cached_eye_projection[0];
          constants.eye_projection[1] = m_cached_eye_projection[1];
          constants.eye_projection[2] = m_cached_eye_projection[2];
          constants.eye_projection[3] = m_cached_eye_projection[3];
          constants.eye_z_row[0] = m_cached_eye_z_row[0];
          constants.eye_z_row[1] = m_cached_eye_z_row[1];

          // Unrotated per-eye projection rows for head-locked content (cached above).
          constants.head_projection[0] = m_cached_head_projection[0];
          constants.head_projection[1] = m_cached_head_projection[1];
          constants.head_projection[2] = m_cached_head_projection[2];
          constants.head_projection[3] = m_cached_head_projection[3];
          for (u32 eye = 0; eye < 2; ++eye)
          {
            auto& row0 = constants.head_projection[eye * 2 + 0];
            auto& row1 = constants.head_projection[eye * 2 + 1];
            row0[0] *= headlocked_projection_scale_x;
            row0[3] *= headlocked_projection_scale_x;
            row0[2] -= headlocked_projection_offset_x;
            row1[1] *= headlocked_projection_scale_y;
            row1[3] *= headlocked_projection_scale_y;
            row1[2] -= headlocked_projection_offset_y;
          }
          constants.head_locked_params = {g_ActiveConfig.vr_head_locked_curvature, 0.0f, 0.0f,
                                          0.0f};
          constants.pixel_center_correction = vertex_shader_manager.constants.pixelcentercorrection;

          // Virtual screen params (for ortho draws: menus, FMV, HUD).
          const float dist = upm * g_ActiveConfig.vr_screen_distance;
          const float half_h = upm * g_ActiveConfig.vr_screen_size * 0.5f;
          const float half_w = half_h * (16.0f / 9.0f);
          // Layer index: use manual override if set, otherwise auto counter.
          const int layer = (vr_ortho_layer_override >= 0) ? vr_ortho_layer_override
                                                           : vr_ortho_draw_counter;
          vr_ortho_layer_override = -1;  // consume
          constants.vr_screen = {half_w, half_h, dist, static_cast<float>(layer)};

          if (perspective)
          {
            // Detect if the game's projection flips the X axis (e.g. mirror mode in
            // Mario Kart).  The VR GS path replaces the game's projection entirely with
            // the VR eye projection, which always has a positive X scale.  If the game's
            // projection has a negative X scale, triangle winding reverses and the game
            // adjusts its cull mode accordingly — but our VR projection doesn't reproduce
            // the flip, so the cull mode becomes wrong and back-faces are shown instead of
            // front-faces.  Pass the sign to the GS so it can negate the output X when
            // needed, restoring correct winding AND the intended mirrored view.
            const float proj_x_sign =
                (xfmem.projection.rawProjection[0] < 0.0f) ? -1.0f : 1.0f;
            constants.stereoparams[0] = proj_x_sign;

            float depth_scale = 1.0f;
            float depth_offset = 0.0f;
            if (VertexShaderManager::UseVertexDepthRange())
            {
              if (g_backend_info.bSupportsReversedDepthRange)
              {
                depth_scale = std::fabs(xfmem.viewport.zRange) / 16777215.0f;
                if (xfmem.viewport.zRange < 0.0f)
                  depth_offset = xfmem.viewport.farZ / 16777215.0f;
                else
                  depth_offset = 1.0f - xfmem.viewport.farZ / 16777215.0f;
              }
              else
              {
                depth_scale = xfmem.viewport.zRange / 16777215.0f;
                depth_offset = 1.0f - xfmem.viewport.farZ / 16777215.0f;
              }
            }
            constants.depth_params = {xfmem.projection.rawProjection[4],
                                      xfmem.projection.rawProjection[5],
                                      depth_scale, depth_offset};

            // Perspective flag consumed in the OpenXR GS path.
            constants.stereoparams[3] = 1.0f;

            // Skybox heuristic (Hydra "Detect Skybox"): an object drawn at the camera origin
            // (translation 0,0,0) with a non-identity matrix is treated as a skybox.  Render it
            // with the eye position locked at 0,0,0 (rotation only, no IPD/positional offset) so
            // it sits at infinity instead of appearing too close.  cstereo.z is the per-draw
            // world-position weight consumed in the GS perspective path: 1.0 = normal (apply eye
            // position), 0.0 = skybox (rotation only).  This must be set for EVERY perspective VR
            // draw, otherwise normal geometry would lose its per-eye offset and stereo would break.
            bool is_skybox = false;
            if (g_ActiveConfig.vr_detect_skybox)
            {
              const auto& pnm = vertex_shader_manager.constants.posnormalmatrix;
              if (pnm[0][3] == 0.0f && pnm[1][3] == 0.0f && pnm[2][3] == 0.0f &&
                  pnm[0][0] != 1.0f)
              {
                is_skybox = true;
              }
            }
            constants.stereoparams[2] = is_skybox ? 0.0f : 1.0f;
          }
          else if (g_ActiveConfig.vr_virtual_screen)
          {
            // Orthographic VR flag — signals GS to use virtual screen path.
            constants.stereoparams[3] = -1.0f;
          }

        }

        // Per-draw VR stereo override (screen/fullscreen handling from shader overrides)
        if (!std::isnan(vr_stereo_override))
        {
          constants.stereoparams[3] = vr_stereo_override;
          vr_stereo_override = std::numeric_limits<float>::quiet_NaN();
        }

        const bool perspective_hud =
            perspective && VR::g_openxr && VR::g_openxr->IsSessionRunning() &&
            constants.stereoparams[3] < -2.5f;
        if (perspective_hud)
        {
          // Model origin's view-space z (position-matrix translation).
          const float reference_view_z = vertex_shader_manager.constants.posnormalmatrix[2][3];
          // The radar/minimap (METROID_VISOR_RADAR_HINT / METROID_RADAR_DOT) is a flat overlay whose
          // blip geometry spans a large depth.  Under the SHARED scene transform that spread clips
          // behind the camera and the whole minimap vanishes at low HUD distances.  Self-centre it on
          // its own origin depth (its own, smaller scale — the pre-shared-reference behaviour) so it
          // stays at `distance` and visible.  Coherent 3D content (Samus + hook, beam boxes) keeps the
          // shared reference so its relative depth survives.
          // (The radar/minimap draws are NOT classified as a RADAR layer in practice — combat-visor
          // logs show them under METROID_SCAN_TEXT — so the depth-outlier check below is what
          // actually catches them.  The flag is kept as a cheap explicit opt-out.)
          const bool self_center_layer = metroid_hud_self_center;
          const bool valid_reference =
              std::isfinite(reference_view_z) && reference_view_z < 0.0f;
          if (metroid_hud_anchor_candidate && !self_center_layer && valid_reference)
          {
            // Pick the farthest stable HUD-body origin seen this frame as the anchor for the next
            // frame. Free-aim adds/moves nearer reticle pieces, but those are not anchor candidates
            // and cannot pull the whole 3D HUD plane toward the camera.
            if (!m_vr_hud_frame_anchor_candidate_valid ||
                reference_view_z < m_vr_hud_frame_anchor_candidate_z)
            {
              m_vr_hud_frame_anchor_candidate_z = reference_view_z;
              m_vr_hud_frame_anchor_candidate_valid = true;
            }
          }
          // Reuse ONE shared reference for all coherent -3 draws in this frame, so every Prime
          // perspective-HUD model shares a single headlocked transform and keeps its relative scene
          // depth (the grappling hook behind Samus's arm, stacked beam boxes). The reference comes
          // from the previous frame's stable HUD body anchor when available. First-frame fallback uses
          // the first coherent draw only until a stable anchor is promoted at the frame boundary.
          if (!m_vr_hud_shared_reference_valid && !self_center_layer)
          {
            if (m_vr_hud_stable_reference_valid)
            {
              m_vr_hud_shared_reference_z = m_vr_hud_stable_reference_z;
              m_vr_hud_shared_reference_valid = true;
            }
            else if (valid_reference)
            {
              m_vr_hud_shared_reference_z = reference_view_z;
              m_vr_hud_shared_reference_valid = true;
            }
          }
          const float shared_reference_z =
              m_vr_hud_shared_reference_valid ? m_vr_hud_shared_reference_z : reference_view_z;
          // Depth-outlier draws self-centre on their own origin (their own, smaller/larger scale —
          // the pre-shared-reference behaviour).  Under the SHARED transform, a draw sitting much
          // closer to the camera than the reference (the minimap surface/blips at view-z ~-1..-5 vs
          // the ~-20 HUD reference) slides behind the near plane whenever Distance < Size
          // (pos.z = sizeRef - dist) and progressively vanishes.  Coherent draws (Samus + hook,
          // beam boxes — all within ~0.7-1.2x of the reference) keep the shared scene so their
          // relative depth survives.
          bool depth_outlier = false;
          if (std::isfinite(reference_view_z) && reference_view_z < 0.0f &&
              shared_reference_z < 0.0f)
          {
            const float depth_ratio = reference_view_z / shared_reference_z;
            depth_outlier = depth_ratio < 0.5f || depth_ratio > 2.0f;
          }
          const float ref_for_transform =
              (self_center_layer || depth_outlier) ? reference_view_z : shared_reference_z;
          const PerspectiveHudTransform transform = CalculatePerspectiveHudTransform(
              xfmem.projection.rawProjection, upm, ref_for_transform);
          if (transform.valid)
          {
            constants.vr_screen[0] = transform.position[0];
            constants.vr_screen[1] = transform.position[1];
            constants.vr_screen[2] = transform.position[2];
            constants.head_locked_params[1] = transform.scale[0];
            constants.head_locked_params[2] = transform.scale[1];
            constants.head_locked_params[3] = transform.scale[2];
            constants.depth_params[3] = transform.distance;
          }
          else
          {
            constants.stereoparams[3] = -2.0f;
          }
        }

        // For ortho/screen draws, pass depth params via depth_params
        // (depth_params is otherwise unused for non-perspective draws).
        // .x = layer offset (between draw calls), .y = element depth (within draw call),
        // .z = HUD thickness in game units (world-space depth spread across the layer's ortho-Z)
        if (constants.stereoparams[3] < -0.5f)
        {
          constants.depth_params[0] = g_ActiveConfig.vr_layer_offset;
          // Per-override element depth if set, otherwise global
          constants.depth_params[1] = (vr_element_depth_override >= 0.0f)
                                          ? vr_element_depth_override
                                          : g_ActiveConfig.vr_element_depth;
          vr_element_depth_override = -1.0f;  // consume
          // Gives a 2D layer (HUD/menu) real 3D depth: ortho-Z elements spread across this
          // many game units of world-space thickness (Hydra "HudThickness").  0 = flat.
          constants.depth_params[2] = upm * g_ActiveConfig.vr_hud_thickness;
          if (perspective_hud)
            constants.depth_params[3] = constants.depth_params[3] > 0.0f ?
                                            constants.depth_params[3] :
                                            upm * g_ActiveConfig.vr_screen_distance;
        }

        dirty = true;
      }
      else
#endif
      if (xfmem.projection.type == ProjectionType::Perspective)
      {
        const float offset = g_ActiveConfig.stereo_depth;
        constants.stereoparams[0] = g_ActiveConfig.bStereoSwapEyes ? offset : -offset;
        constants.stereoparams[1] = g_ActiveConfig.bStereoSwapEyes ? -offset : offset;
        constants.stereoparams[2] = g_ActiveConfig.stereo_convergence;
        constants.stereoparams[3] = 0.0f;
        dirty = true;
      }
      else
      {
        constants.stereoparams[0] = 0.0f;
        constants.stereoparams[1] = 0.0f;
        constants.stereoparams[2] = 0.0f;
        constants.stereoparams[3] = 0.0f;
        dirty = true;
      }
    }
  }

  if (g_ActiveConfig.UseVSForLinePointExpand())
  {
    if (prim == PrimitiveType::Points)
      SetVSExpand(VSExpand::Point);
    else if (prim == PrimitiveType::Lines)
      SetVSExpand(VSExpand::Line);
    else
      SetVSExpand(VSExpand::None);
  }

  if (m_viewport_changed)
  {
    m_viewport_changed = false;

    constants.lineptparams[0] = 2.0f * xfmem.viewport.wd;
    constants.lineptparams[1] = -2.0f * xfmem.viewport.ht;

    dirty = true;
  }
}

void GeometryShaderManager::SetViewportChanged()
{
  m_viewport_changed = true;
}

void GeometryShaderManager::SetProjectionChanged()
{
  m_projection_changed = true;
}

void GeometryShaderManager::InvalidateVRHeadPose()
{
  m_vr_pose_needs_refresh = true;
  if (m_vr_hud_frame_anchor_candidate_valid)
  {
    m_vr_hud_stable_reference_z = m_vr_hud_frame_anchor_candidate_z;
    m_vr_hud_stable_reference_valid = true;
    m_vr_hud_frame_anchor_candidate_valid = false;
  }
  // Reuse the promoted stable perspective-HUD reference on the next frame.
  m_vr_hud_shared_reference_valid = false;
}

void GeometryShaderManager::SetLinePtWidthChanged()
{
  constants.lineptparams[2] = bpmem.lineptwidth.linesize / 6.f;
  constants.lineptparams[3] = bpmem.lineptwidth.pointsize / 6.f;
  constants.texoffset[2] = LINE_PT_TEX_OFFSETS[bpmem.lineptwidth.lineoff];
  constants.texoffset[3] = LINE_PT_TEX_OFFSETS[bpmem.lineptwidth.pointoff];
  dirty = true;
}

void GeometryShaderManager::SetTexCoordChanged(u8 texmapid)
{
  TCoordInfo& tc = bpmem.texcoords[texmapid];
  int bitmask = 1 << texmapid;
  constants.texoffset[0] &= ~bitmask;
  constants.texoffset[0] |= tc.s.line_offset << texmapid;
  constants.texoffset[1] &= ~bitmask;
  constants.texoffset[1] |= tc.s.point_offset << texmapid;
  dirty = true;
}

void GeometryShaderManager::DoState(PointerWrap& p)
{
  p.Do(m_projection_changed);
  p.Do(m_viewport_changed);

  p.Do(constants);

  if (p.IsReadMode())
  {
    // Fixup the current state from global GPU state
    // NOTE: This requires that all GPU memory has been loaded already.
    Dirty();
  }
}
