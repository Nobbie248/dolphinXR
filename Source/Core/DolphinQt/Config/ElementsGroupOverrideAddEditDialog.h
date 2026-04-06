// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QDialog>

#include <vector>

#include "VideoCommon/ElementsGroupManager.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QListWidget;
class QScrollArea;
class QSpinBox;
class QToolButton;
class QVBoxLayout;
class QWidget;

class ElementsGroupOverrideAddEditDialog : public QDialog
{
  Q_OBJECT
public:
  explicit ElementsGroupOverrideAddEditDialog(
      QWidget* parent,
      const ElementsGroupManager::ElementGroupOverride* edit_override = nullptr,
      const std::vector<std::string>& available_flags = {},
      const ShaderHunter::ShaderOverride* import_shader = nullptr,
      const ElementsGroupManager::RuntimeElementSignature* import_runtime_element = nullptr);

  ElementsGroupManager::ElementGroupOverride GetResult() const;

private:
  std::vector<u64> CollectTextureHashValues() const;
  void SetTextureHashValues(const std::vector<u64>& hashes);
  void AddTextureHashField(const QString& text = {});
  void EnsureTextureHashFieldRows();
  void SanitizeTextureHashField(QLineEdit* edit);
  void ShowTextureBrowser();
  void RefreshRuntimeElementSummary();
  void RefreshHandlingUi();
  void RefreshRefinementUi();
  void CaptureCurrentSeed();
  void AddCurrentHuntMatch();
  void RemoveSelectedMatchFilter();
  void OnAccept();

  std::vector<std::string> m_available_flags;
  int m_edit_element_reference_total = 0;
  bool m_updating_texture_hash_fields = false;
  bool m_refinement_widgets_visible = false;

  QLineEdit* m_name_edit = nullptr;
  QPlainTextEdit* m_comments_edit = nullptr;
  QLineEdit* m_credits_edit = nullptr;
  QComboBox* m_handling_combo = nullptr;
  QSpinBox* m_layer_spin = nullptr;
  QLabel* m_layer_label = nullptr;
  QDoubleSpinBox* m_element_depth_spin = nullptr;
  QLabel* m_element_depth_label = nullptr;
  QDoubleSpinBox* m_units_per_meter_spin = nullptr;
  QLabel* m_units_per_meter_label = nullptr;
  QLineEdit* m_flag_edit = nullptr;
  QLabel* m_flag_label = nullptr;
  QComboBox* m_condition_combo = nullptr;
  QLabel* m_condition_label = nullptr;
  QComboBox* m_condition_mode_combo = nullptr;
  QLabel* m_condition_mode_label = nullptr;
  QCheckBox* m_element_filter_check = nullptr;
  QSpinBox* m_element_start_spin = nullptr;
  QLabel* m_element_start_label = nullptr;
  QSpinBox* m_element_end_spin = nullptr;
  QLabel* m_element_end_label = nullptr;
  QPushButton* m_capture_seed_button = nullptr;
  QLabel* m_runtime_element_summary_label = nullptr;
  QCheckBox* m_runtime_use_projection_check = nullptr;
  QCheckBox* m_runtime_use_layer_check = nullptr;
  QCheckBox* m_runtime_use_viewport_check = nullptr;
  QCheckBox* m_runtime_use_scissor_check = nullptr;
  QCheckBox* m_runtime_use_render_state_check = nullptr;
  QComboBox* m_texture_mode_combo = nullptr;
  QComboBox* m_selected_match_mode_combo = nullptr;
  QPushButton* m_view_textures_button = nullptr;
  QWidget* m_texture_hash_container = nullptr;
  QScrollArea* m_texture_hash_scroll = nullptr;
  QVBoxLayout* m_texture_hash_layout = nullptr;
  std::vector<QLineEdit*> m_texture_hash_edits;
  QListWidget* m_selected_match_list = nullptr;
  QPushButton* m_add_current_match_button = nullptr;
  QPushButton* m_remove_selected_match_button = nullptr;
  QToolButton* m_refinement_toggle = nullptr;
  QFrame* m_refinement_frame = nullptr;
  QCheckBox* m_refinement_enable_check = nullptr;
  QComboBox* m_refinement_type_combo = nullptr;
  QCheckBox* m_refinement_family_check = nullptr;
  QLineEdit* m_refinement_hash_edit = nullptr;
  QLabel* m_refinement_hash_label = nullptr;
  QLineEdit* m_refinement_family_signature_edit = nullptr;
  QLabel* m_refinement_family_signature_label = nullptr;

  ElementsGroupManager::RuntimeElementSignature m_runtime_element;
  std::vector<ElementsGroupManager::SelectedSubgroupSignature> m_selected_match_filters;
  bool m_selected_match_filters_excluded = false;
};
