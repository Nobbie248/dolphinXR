// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <QPointer>
#include <QWidget>

#include "VideoCommon/TextureElementManager.h"

class QDialog;
class QListWidget;
class QListWidgetItem;
class QPushButton;

// Per-game Properties pane for Texture Element Overrides: reclassify VR draws based purely on the
// bound texture hash, across all shaders/elements. Modeled on ShaderOverrideWidget.
class TextureElementOverrideWidget : public QWidget
{
  Q_OBJECT
public:
  explicit TextureElementOverrideWidget(std::string game_id,
                                        std::optional<u16> revision = std::nullopt);
  ~TextureElementOverrideWidget() override;

private:
  void CreateWidgets();
  void ConnectWidgets();
  void UpdateList();
  void LoadOverrides();
  void SaveOverrides();
  void ReloadRuntime();

  void OnItemChanged(QListWidgetItem* item);
  void OnSelectionChanged();
  void OnTextureHunterClicked();
  void OnAddClicked();
  void OnEditClicked();
  void OnRemoveClicked();
  void OnRefreshClicked();
  void OnReloadClicked();

  std::string MakeUniqueName(const std::string& base) const;

  std::string m_game_id;
  std::optional<u16> m_revision;

  QListWidget* m_code_list;
  QPushButton* m_texture_hunter;
  QPushButton* m_code_add;
  QPushButton* m_code_edit;
  QPushButton* m_code_remove;
  QPushButton* m_code_refresh;
  QPushButton* m_code_reload;
  QPointer<QDialog> m_texture_hunter_dialog;

  std::vector<TextureElementManager::TextureElementOverride> m_overrides;
};
