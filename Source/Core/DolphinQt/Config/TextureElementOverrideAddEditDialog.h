// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <vector>

#include <QDialog>

#include "VideoCommon/TextureElementManager.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QString;
class QSpinBox;
class QVBoxLayout;
class QWidget;

// Add/Edit dialog for a single Texture Element Override (a named group of texture hashes that
// share one handling). Modeled on ShaderOverrideAddEditDialog, minus the shader-specific fields.
class TextureElementOverrideAddEditDialog : public QDialog
{
  Q_OBJECT
public:
  // edit_override != nullptr → edit mode (pre-fills fields); nullptr → add mode.
  explicit TextureElementOverrideAddEditDialog(
      QWidget* parent,
      const TextureElementManager::TextureElementOverride* edit_override = nullptr);

  TextureElementManager::TextureElementOverride GetResult() const;

private:
  std::vector<std::string> CollectTextureHashTokens() const;
  std::vector<u64> CollectTextureHashValues() const;
  void SetTextureHashFields(const std::vector<u64>& hashes);
  void AddTextureHashField(const QString& text);
  void EnsureTextureHashFieldRows();
  void SanitizeTextureHashField(QLineEdit* edit);
  void ShowTextureBrowser();

  void OnAccept();
  void OnHandlingChanged();

  QLineEdit* m_name_edit;
  QPlainTextEdit* m_comments_edit;
  QComboBox* m_handling_combo;
  QSpinBox* m_layer_spin;
  QLabel* m_layer_label;
  QDoubleSpinBox* m_element_depth_spin;
  QLabel* m_element_depth_label;
  QDoubleSpinBox* m_units_per_meter_spin;
  QLabel* m_units_per_meter_label;
  QPushButton* m_view_textures_button;
  QWidget* m_texture_hash_container;
  QScrollArea* m_texture_hash_scroll;
  QVBoxLayout* m_texture_hash_layout;
  std::vector<QLineEdit*> m_texture_hash_edits;
  bool m_updating_texture_hash_fields = false;
};
