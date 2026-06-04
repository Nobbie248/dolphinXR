// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <gtest/gtest.h>

#include "VideoCommon/TextureCacheBase.h"

TEST(TextureCacheBase, MetroidPrime1ThermalSourceCandidateMatchesStereoColorEfbCopy)
{
  EXPECT_TRUE(TextureCacheBase::IsMetroidPrime1ThermalStereoSourceCandidate(
      640, 448, 2, false, false));
  EXPECT_TRUE(TextureCacheBase::IsMetroidPrime1ThermalStereoSourceCandidate(
      640, 448, 3, false, false));
}

TEST(TextureCacheBase, MetroidPrime1ThermalSourceCandidateRejectsWrongCopies)
{
  EXPECT_FALSE(TextureCacheBase::IsMetroidPrime1ThermalStereoSourceCandidate(
      640, 448, 1, false, false));
  EXPECT_FALSE(TextureCacheBase::IsMetroidPrime1ThermalStereoSourceCandidate(
      320, 224, 2, false, false));
  EXPECT_FALSE(TextureCacheBase::IsMetroidPrime1ThermalStereoSourceCandidate(
      640, 448, 2, true, false));
  EXPECT_FALSE(TextureCacheBase::IsMetroidPrime1ThermalStereoSourceCandidate(
      640, 448, 2, false, true));
}
