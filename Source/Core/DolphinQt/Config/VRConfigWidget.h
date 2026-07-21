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

  enum class ForcedVBIFrequencyMode
  {
    Inherit = -1,
    Auto = -2,
    Off = 0,
    Hz72 = 72,
    Hz90 = 90,
    Hz120 = 120,
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
  static void SetBoolMode(QComboBox* combo, BoolMode mode);
  static BoolMode GetBoolMode(const QComboBox* combo);
  static void PopulateBoolModeCombo(QComboBox* combo);
  static ForcedVBIFrequencyMode ParseForcedVBIFrequencyMode(const ValueMap& values);
  static void SetForcedVBIFrequencyMode(QComboBox* combo, ForcedVBIFrequencyMode mode);
  static ForcedVBIFrequencyMode GetForcedVBIFrequencyMode(const QComboBox* combo);
  static void PopulateForcedVBIFrequencyModeCombo(QComboBox* combo);

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
  QComboBox* m_detect_skybox_mode = nullptr;
  QComboBox* m_forced_vbi_frequency_mode = nullptr;

  QTabWidget* m_default_tab = nullptr;
  QTabWidget* m_local_tab = nullptr;
  int m_prev_tab_index = 0;
};
