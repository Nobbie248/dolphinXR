// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QDialog>

#include <optional>
#include <vector>

#include "VideoCommon/HideObjectEngine.h"

class QComboBox;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;

class HideObjectAddEditDialog : public QDialog
{
  Q_OBJECT
public:
  // Pass nullptr for new code, or pointer to existing code for editing.
  // parent_codes is the full list (for name uniqueness check).
  explicit HideObjectAddEditDialog(QWidget* parent,
                                   const HideObjectEngine::HideObject* existing_code,
                                   const std::vector<HideObjectEngine::HideObject>& all_codes);

  HideObjectEngine::HideObject GetResult() const { return m_result; }

private:
  void CreateWidgets();
  void ConnectWidgets();

  void OnTypeChanged();
  void OnUpClicked();
  void OnDownClicked();
  void OnRangeFinderToggled(bool enabled);
  void OnRangeBoundsChanged();
  void OnAccept();

  void UpdateValueDisplay();
  void UpdateRangeLabel();
  void UpdateRangeSlidersFromText();
  void OnRangeSliderChanged();
  bool ClampRangeBounds(bool lower_changed);
  void SetRangeBoundsForCurrentType(const HideObjectEngine::HideObjectEntry& lower,
                                    const HideObjectEngine::HideObjectEntry& upper);
  void ResetRangeBoundsForCurrentType();
  bool ParseRangeBounds(HideObjectEngine::HideObjectEntry* lower,
                        HideObjectEngine::HideObjectEntry* upper) const;
  bool ParseValueFromUI();
  void ApplyTemporarily();

  bool m_is_edit = false;
  std::string m_original_name;  // For edit mode: original name to exclude from uniqueness check

  HideObjectEngine::HideObject m_result;
  HideObjectEngine::HideObjectEntry m_current_entry;
  std::optional<size_t> m_existing_code_index;

  const std::vector<HideObjectEngine::HideObject>& m_all_codes;

  QLineEdit* m_name_edit;
  QComboBox* m_type_combo;
  QLineEdit* m_value_edit;
  QPushButton* m_up_button;
  QPushButton* m_down_button;
  QCheckBox* m_range_finder_toggle;
  QLabel* m_range_label;
  QLineEdit* m_range_lower_edit;
  QLineEdit* m_range_upper_edit;
  QSlider* m_range_lower_slider;
  QSlider* m_range_upper_slider;
};
