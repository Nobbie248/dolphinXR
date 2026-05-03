// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <map>
#include <optional>
#include <string>

#include <QWidget>

#include "Common/CommonTypes.h"
#include "Common/StringUtil.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QTabWidget;

class VRConfigWidget final : public QWidget
{
  Q_OBJECT

public:
  explicit VRConfigWidget(std::string game_id, std::optional<u16> revision = std::nullopt,
                          QWidget* parent = nullptr);

private:
  enum class BoolMode
  {
    Inherit = 0,
    Enabled = 1,
    Disabled = 2,
  };

  enum class ReplayMode
  {
    Inherit = -1,
    Off = 0,
    Replay60To90 = 1,
    Replay30To90 = 2,
  };

  using ValueMap = std::map<std::string, std::string, Common::CaseInsensitiveLess>;

  void CreateWidgets();
  void LoadFromFile();
  void SaveToFile();
  void RefreshEditorTabs();

  std::string GetLocalINIPath() const;
  ValueMap ReadMergedVRSectionValues() const;
  static bool IsVRSectionHeader(std::string_view line);
  static std::string ReadFileWithoutVRSection(const std::string& path);
  static ValueMap ReadVRSectionValues(const std::string& path);
  static BoolMode ParseBoolMode(const ValueMap& values, const char* key);
  static ReplayMode ParseReplayMode(const ValueMap& values, const char* key);
  static void SetBoolMode(QComboBox* combo, BoolMode mode);
  static BoolMode GetBoolMode(const QComboBox* combo);
  static void PopulateBoolModeCombo(QComboBox* combo);
  static void SetReplayMode(QComboBox* combo, ReplayMode mode);
  static ReplayMode GetReplayMode(const QComboBox* combo);
  static void PopulateReplayModeCombo(QComboBox* combo);

  const std::string m_game_id;
  const std::optional<u16> m_revision;
  bool m_updating = false;

  QCheckBox* m_override_units_per_meter = nullptr;
  QDoubleSpinBox* m_units_per_meter = nullptr;
  QCheckBox* m_override_lean_back_angle = nullptr;
  QDoubleSpinBox* m_lean_back_angle = nullptr;
  QCheckBox* m_override_camera_forward = nullptr;
  QDoubleSpinBox* m_camera_forward = nullptr;
  QCheckBox* m_override_camera_height = nullptr;
  QDoubleSpinBox* m_camera_height = nullptr;
  QCheckBox* m_override_head_locked_curvature = nullptr;
  QDoubleSpinBox* m_head_locked_curvature = nullptr;
  QCheckBox* m_override_element_depth = nullptr;
  QDoubleSpinBox* m_element_depth = nullptr;
  QComboBox* m_virtual_screen_mode = nullptr;
  QComboBox* m_dont_clear_screen_mode = nullptr;
  QComboBox* m_opcode_replay_mode = nullptr;
  QComboBox* m_force_vbi_90hz_mode = nullptr;

  QTabWidget* m_default_tab = nullptr;
  QTabWidget* m_local_tab = nullptr;
  int m_prev_tab_index = 0;
};
