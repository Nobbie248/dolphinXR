// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <gtest/gtest.h>

#include "VideoCommon/VR/VRFrameRegion.h"
#include "VideoCommon/XFMemory.h"

namespace
{
Viewport MakeViewport(int left, int top, int width, int height)
{
  return {.wd = width * 0.5f,
          .ht = height * -0.5f,
          .zRange = 0.0f,
          .xOrig = left + width * 0.5f,
          .yOrig = top + height * 0.5f,
          .farZ = 0.0f};
}

void LatchDisplayRegion(int width, int height)
{
  VR::NotifyVRXFBCopyRegion(0, 0, width, height);
  VR::NotifyVRXFBCopyRegion(0, 0, width, height);
}
}  // namespace

TEST(VRFrameRegion, TrilogyPresentationExpandsAfterEFBComposition)
{
  VR::ResetVRFrameRegion();
  LatchDisplayRegion(640, 480);

  for (int i = 0; i < 10; ++i)
    VR::ObserveVRPerspectiveViewport(MakeViewport(0, 0, 640, 448), 0, 0);

  const VR::VRFrameRegion xfb_source{0, 0, 640, 480};
  VR::VRFrameRegion presentation_source{};
  EXPECT_TRUE(VR::ConsumeVRPresentationSourceRegion(xfb_source, &presentation_source));
  EXPECT_EQ(presentation_source.left, 0);
  EXPECT_EQ(presentation_source.top, 0);
  EXPECT_EQ(presentation_source.width, 640);
  EXPECT_EQ(presentation_source.height, 448);
}

TEST(VRFrameRegion, GolfPresentationIncludesWholeOversizedScene)
{
  VR::ResetVRFrameRegion();
  LatchDisplayRegion(640, 448);

  for (int i = 0; i < 10; ++i)
    VR::ObserveVRPerspectiveViewport(MakeViewport(0, 0, 640, 480), 0, 0);

  const VR::VRFrameRegion xfb_source{0, 0, 640, 448};
  VR::VRFrameRegion presentation_source{};
  EXPECT_TRUE(VR::ConsumeVRPresentationSourceRegion(xfb_source, &presentation_source));
  EXPECT_EQ(presentation_source.left, 0);
  EXPECT_EQ(presentation_source.top, 0);
  EXPECT_EQ(presentation_source.width, 640);
  EXPECT_EQ(presentation_source.height, 480);
}

TEST(VRFrameRegion, DominantSceneWinsAndMatchingCopyNeedsNoOverride)
{
  VR::ResetVRFrameRegion();
  LatchDisplayRegion(640, 448);

  for (int i = 0; i < 10; ++i)
    VR::ObserveVRPerspectiveViewport(MakeViewport(0, 0, 640, 448), 0, 0);
  VR::ObserveVRPerspectiveViewport(MakeViewport(0, 0, 640, 480), 0, 0);

  const VR::VRFrameRegion xfb_source{0, 0, 640, 448};
  VR::VRFrameRegion presentation_source{};
  EXPECT_FALSE(VR::ConsumeVRPresentationSourceRegion(xfb_source, &presentation_source));
}

TEST(VRFrameRegion, SmallPerspectivePaneCannotDrivePresentationCopy)
{
  VR::ResetVRFrameRegion();
  LatchDisplayRegion(640, 448);

  VR::ObserveVRPerspectiveViewport(MakeViewport(32, 32, 256, 256), 0, 0);

  const VR::VRFrameRegion xfb_source{0, 0, 640, 448};
  VR::VRFrameRegion presentation_source{};
  EXPECT_FALSE(VR::ConsumeVRPresentationSourceRegion(xfb_source, &presentation_source));
}

TEST(VRFrameRegion, MKDDLetterboxedCharacterViewportCannotMoveComposedScreen)
{
  VR::ResetVRFrameRegion();
  LatchDisplayRegion(608, 448);

  for (int i = 0; i < 10; ++i)
    VR::ObserveVRPerspectiveViewport(MakeViewport(0, 100, 608, 348), 0, 0);

  const VR::VRFrameRegion xfb_source{0, 0, 608, 448};
  VR::VRFrameRegion presentation_source{};
  EXPECT_FALSE(VR::ConsumeVRPresentationSourceRegion(xfb_source, &presentation_source));
}

TEST(VRFrameRegion, NearMatchingAnimatedViewportCannotMoveComposedScreen)
{
  VR::ResetVRFrameRegion();
  LatchDisplayRegion(640, 448);

  for (int i = 0; i < 10; ++i)
    VR::ObserveVRPerspectiveViewport(MakeViewport(0, 1, 640, 446), 0, 0);

  const VR::VRFrameRegion xfb_source{0, 0, 640, 448};
  VR::VRFrameRegion presentation_source{};
  EXPECT_FALSE(VR::ConsumeVRPresentationSourceRegion(xfb_source, &presentation_source));
}

TEST(VRFrameRegion, MKDDCharacterBandRemapsToItsOriginalScreenPosition)
{
  // These are the raw values captured in the element signature. GX's 342-pixel scissor
  // offset turns the viewport into a 608x348 pane at (0,100) in the 608x448 frame.
  const Viewport viewport{.wd = 304.0f,
                          .ht = -174.0f,
                          .zRange = 0.0f,
                          .xOrig = 646.0f,
                          .yOrig = 616.0f,
                          .farZ = 0.0f};
  const VR::VRFrameRegion frame{0, 0, 608, 448, true};

  const auto remap = VR::CalculateVRPaneRemap(viewport, 342, 342, frame);
  EXPECT_FLOAT_EQ(remap[0], 1.0f);
  EXPECT_NEAR(remap[1], 348.0f / 448.0f, 0.0001f);
  EXPECT_FLOAT_EQ(remap[2], 0.0f);
  EXPECT_NEAR(remap[3], -100.0f / 448.0f, 0.0001f);
}
