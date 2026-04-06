// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QDialog>
#include <QString>

#include <optional>
#include <string>
#include <vector>

#include "VideoCommon/ElementsGroupManager.h"
#include "VideoCommon/ShaderHunter.h"

class QCheckBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QTimer;

class ImportShaderFindElementDialog : public QDialog
{
  Q_OBJECT
public:
  struct CandidateRow
  {
    ElementsGroupManager::SeedCandidate candidate;
    bool shader_match = false;
    bool texture_match = false;
    bool full_match = false;
    int score = 0;
    QString reason_summary;
  };

  explicit ImportShaderFindElementDialog(const std::string& game_id,
                                         const ShaderHunter::ShaderOverride& source_shader,
                                         QWidget* parent = nullptr);
  ~ImportShaderFindElementDialog() override;

  std::optional<ElementsGroupManager::SeedCandidate> GetSelectedCandidate() const;
  bool ShouldOpenElementsHunting() const;

private:
  void closeEvent(QCloseEvent* event) override;
  void CreateWidgets();
  void ConnectWidgets();
  void UpdateDisplay();
  void OnSelectionChanged();
  CandidateRow BuildCandidateRow(const ElementsGroupManager::SeedCandidate& candidate) const;

  std::string m_game_id;
  ShaderHunter::ShaderOverride m_source_shader;
  QLabel* m_info_label = nullptr;
  QCheckBox* m_matching_only_check = nullptr;
  QListWidget* m_candidate_list = nullptr;
  QLabel* m_status_label = nullptr;
  QPushButton* m_open_hunting_button = nullptr;
  QPushButton* m_use_candidate_button = nullptr;
  QTimer* m_update_timer = nullptr;
  bool m_open_elements_hunting = false;
  std::vector<CandidateRow> m_rows;
};
