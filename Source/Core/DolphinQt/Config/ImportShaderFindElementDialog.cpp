// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/Config/ImportShaderFindElementDialog.h"

#include <algorithm>

#include <QAbstractItemView>
#include <QBrush>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
QString FormatTextureSummary(const std::array<u64, 8>& textures)
{
  QStringList parts;
  for (u64 texture_hash : textures)
  {
    if (texture_hash == 0)
      continue;
    parts << QStringLiteral("%1")
                 .arg(static_cast<qulonglong>(texture_hash), 16, 16, QLatin1Char('0'));
    if (parts.size() >= 2)
      break;
  }
  return parts.isEmpty() ? QObject::tr("No textures") : parts.join(QStringLiteral(", "));
}

QString FormatProjectionLabel(const ShaderHunter::RuntimeElementSignature& sig)
{
  if (sig.perspective)
  {
    return QObject::tr("Perspective %1x%2")
        .arg(sig.perspective_hfov_x100 / 100.0, 0, 'f', 2)
        .arg(sig.perspective_vfov_x100 / 100.0, 0, 'f', 2);
  }
  return QObject::tr("Ortho L%1").arg(sig.ortho_layer);
}

bool RuntimeElementSignaturesEqual(const ShaderHunter::RuntimeElementSignature& lhs,
                                   const ShaderHunter::RuntimeElementSignature& rhs)
{
  return lhs.valid == rhs.valid && lhs.perspective == rhs.perspective &&
         lhs.perspective_hfov_x100 == rhs.perspective_hfov_x100 &&
         lhs.perspective_vfov_x100 == rhs.perspective_vfov_x100 &&
         lhs.perspective_near_x1000 == rhs.perspective_near_x1000 &&
         lhs.perspective_far_x100 == rhs.perspective_far_x100 &&
         lhs.ortho_left_x100 == rhs.ortho_left_x100 &&
         lhs.ortho_right_x100 == rhs.ortho_right_x100 &&
         lhs.ortho_top_x100 == rhs.ortho_top_x100 &&
         lhs.ortho_bottom_x100 == rhs.ortho_bottom_x100 &&
         lhs.ortho_layer == rhs.ortho_layer &&
         lhs.viewport_x == rhs.viewport_x &&
         lhs.viewport_y == rhs.viewport_y &&
         lhs.viewport_width == rhs.viewport_width &&
         lhs.viewport_height == rhs.viewport_height &&
         lhs.scissor_left == rhs.scissor_left &&
         lhs.scissor_top == rhs.scissor_top &&
         lhs.scissor_right == rhs.scissor_right &&
         lhs.scissor_bottom == rhs.scissor_bottom &&
         lhs.alpha_test_hex == rhs.alpha_test_hex &&
         lhs.ztest == rhs.ztest && lhs.zupdate == rhs.zupdate && lhs.zfunc == rhs.zfunc &&
         lhs.blend_color_update == rhs.blend_color_update &&
         lhs.blend_alpha_update == rhs.blend_alpha_update;
}

QString FormatCandidateLabel(const ImportShaderFindElementDialog::CandidateRow& row)
{
  const auto& sig = row.candidate.signature;
  const auto& draw = row.candidate.representative_draw;
  return QObject::tr("%1 | VP %2,%3 %4x%5 | SC %6,%7 %8,%9 | Draw #%10 | Score %11 | %12\n"
                     "Stage: VS %13 | PS %14 | GS %15 | Tex %16")
      .arg(FormatProjectionLabel(sig))
      .arg(sig.viewport_x)
      .arg(sig.viewport_y)
      .arg(sig.viewport_width)
      .arg(sig.viewport_height)
      .arg(sig.scissor_left)
      .arg(sig.scissor_top)
      .arg(sig.scissor_right)
      .arg(sig.scissor_bottom)
      .arg(draw.draw_index + 1)
      .arg(row.score)
      .arg(row.reason_summary)
      .arg(static_cast<qulonglong>(draw.vs_hash), 16, 16, QLatin1Char('0'))
      .arg(static_cast<qulonglong>(draw.ps_hash), 16, 16, QLatin1Char('0'))
      .arg(static_cast<qulonglong>(draw.gs_hash), 16, 16, QLatin1Char('0'))
      .arg(FormatTextureSummary(draw.textures));
}
}  // namespace

ImportShaderFindElementDialog::ImportShaderFindElementDialog(
    const std::string& game_id, const ShaderHunter::ShaderOverride& source_shader, QWidget* parent)
    : QDialog(parent), m_game_id(game_id), m_source_shader(source_shader)
{
  setWindowTitle(tr("Import Shader -> Find Element"));
  setMinimumWidth(620);

  auto& manager = ElementsGroupManager::GetInstance();
  manager.SetPopupOpen(true);
  if (!m_game_id.empty())
    manager.LoadOverridesIfNeeded(m_game_id);

  CreateWidgets();
  ConnectWidgets();

  m_update_timer = new QTimer(this);
  connect(m_update_timer, &QTimer::timeout, this, &ImportShaderFindElementDialog::UpdateDisplay);
  m_update_timer->start(100);
  UpdateDisplay();
}

ImportShaderFindElementDialog::~ImportShaderFindElementDialog() = default;

void ImportShaderFindElementDialog::CreateWidgets()
{
  auto* layout = new QVBoxLayout(this);

  const QString shader_name = QString::fromStdString(m_source_shader.name);
  m_info_label = new QLabel(
      tr("Find a live runtime element seed candidate that corresponds to the saved shader '%1'.\n"
         "Candidates are scored using the shader anchor and copied texture filters.")
          .arg(shader_name));
  m_info_label->setWordWrap(true);
  layout->addWidget(m_info_label);

  m_matching_only_check = new QCheckBox(tr("Show Matching Candidates Only"));
  m_matching_only_check->setChecked(true);
  layout->addWidget(m_matching_only_check);

  m_candidate_list = new QListWidget;
  m_candidate_list->setSelectionMode(QAbstractItemView::SingleSelection);
  layout->addWidget(m_candidate_list);

  m_status_label = new QLabel(tr("Waiting for live seed candidates..."));
  m_status_label->setWordWrap(true);
  layout->addWidget(m_status_label);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
  m_open_hunting_button = buttons->addButton(tr("Open Elements Hunting"),
                                             QDialogButtonBox::ActionRole);
  m_use_candidate_button =
      buttons->addButton(tr("Use Selected Candidate"), QDialogButtonBox::AcceptRole);
  m_use_candidate_button->setEnabled(false);

  layout->addWidget(buttons);

  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(m_use_candidate_button, &QPushButton::clicked, this, &QDialog::accept);
}

void ImportShaderFindElementDialog::ConnectWidgets()
{
  connect(m_matching_only_check, &QCheckBox::toggled, this,
          &ImportShaderFindElementDialog::UpdateDisplay);
  connect(m_candidate_list, &QListWidget::itemSelectionChanged, this,
          &ImportShaderFindElementDialog::OnSelectionChanged);
  connect(m_open_hunting_button, &QPushButton::clicked, this, [this]() {
    m_open_elements_hunting = true;
    reject();
  });
}

std::optional<ElementsGroupManager::SeedCandidate> ImportShaderFindElementDialog::GetSelectedCandidate() const
{
  const auto items = m_candidate_list->selectedItems();
  if (items.empty())
    return std::nullopt;

  const int row = items[0]->data(Qt::UserRole).toInt();
  if (row < 0 || row >= static_cast<int>(m_rows.size()))
    return std::nullopt;
  return m_rows[static_cast<size_t>(row)].candidate;
}

bool ImportShaderFindElementDialog::ShouldOpenElementsHunting() const
{
  return m_open_elements_hunting;
}

void ImportShaderFindElementDialog::closeEvent(QCloseEvent* event)
{
  ElementsGroupManager::GetInstance().SetPopupOpen(false);
  QDialog::closeEvent(event);
}

ImportShaderFindElementDialog::CandidateRow ImportShaderFindElementDialog::BuildCandidateRow(
    const ElementsGroupManager::SeedCandidate& candidate) const
{
  CandidateRow row;
  row.candidate = candidate;

  const auto& draw = candidate.representative_draw;
  const u64 draw_hash = draw.GetHash(m_source_shader.type);
  const u64 draw_family = draw.GetFamily(m_source_shader.type);

  if (m_source_shader.hash_family_match && m_source_shader.family_signature != 0)
  {
    row.shader_match = (draw_family != 0 && draw_family == m_source_shader.family_signature);
    row.reason_summary = row.shader_match ? tr("shader family match") : tr("shader family mismatch");
  }
  else
  {
    row.shader_match = (draw_hash != 0 && draw_hash == m_source_shader.hash);
    row.reason_summary = row.shader_match ? tr("shader exact match") : tr("shader exact mismatch");
  }

  row.texture_match = true;
  if (!m_source_shader.texture_hashes.empty())
  {
    bool any_texture_match = false;
    for (u64 texture_hash : draw.textures)
    {
      if (texture_hash == 0)
        continue;
      if (std::find(m_source_shader.texture_hashes.begin(), m_source_shader.texture_hashes.end(),
                    texture_hash) != m_source_shader.texture_hashes.end())
      {
        any_texture_match = true;
        break;
      }
    }

    row.texture_match =
        m_source_shader.texture_hashes_excluded ? !any_texture_match : any_texture_match;
    row.reason_summary += row.texture_match ?
                              (m_source_shader.texture_hashes_excluded ?
                                   tr(", excluded textures clear") :
                                   tr(", texture match")) :
                              (m_source_shader.texture_hashes_excluded ?
                                   tr(", excluded texture present") :
                                   tr(", texture mismatch"));
  }

  row.full_match = row.shader_match && row.texture_match;
  row.score = 0;
  if (row.shader_match)
    row.score += 100;
  if (row.texture_match)
    row.score += m_source_shader.texture_hashes.empty() ? 0 : 25;
  row.score += std::min(candidate.occurrence_count, 9);
  return row;
}

void ImportShaderFindElementDialog::UpdateDisplay()
{
  std::optional<ShaderHunter::RuntimeElementSignature> selected_signature;
  if (const auto selected = GetSelectedCandidate(); selected.has_value())
    selected_signature = selected->signature;

  std::vector<CandidateRow> rows;
  rows.reserve(ElementsGroupManager::GetInstance().GetSeedCandidates().size());

  for (const auto& candidate : ElementsGroupManager::GetInstance().GetSeedCandidates())
  {
    CandidateRow row = BuildCandidateRow(candidate);
    if (!m_matching_only_check->isChecked() || row.full_match)
      rows.push_back(std::move(row));
  }

  std::sort(rows.begin(), rows.end(), [](const CandidateRow& lhs, const CandidateRow& rhs) {
    if (lhs.full_match != rhs.full_match)
      return lhs.full_match > rhs.full_match;
    if (lhs.score != rhs.score)
      return lhs.score > rhs.score;
    return lhs.candidate.representative_draw.draw_index < rhs.candidate.representative_draw.draw_index;
  });

  m_rows = rows;

  {
    const QSignalBlocker blocker(m_candidate_list);
    m_candidate_list->clear();
    int row_to_select = -1;
    for (size_t i = 0; i < m_rows.size(); ++i)
    {
      auto* item = new QListWidgetItem(FormatCandidateLabel(m_rows[i]));
      item->setData(Qt::UserRole, static_cast<int>(i));
      if (!m_rows[i].full_match)
        item->setForeground(QBrush(QColor(150, 150, 150)));
      m_candidate_list->addItem(item);
      if (selected_signature.has_value() &&
          RuntimeElementSignaturesEqual(m_rows[i].candidate.signature, *selected_signature))
      {
        row_to_select = static_cast<int>(i);
      }
    }
    if (row_to_select >= 0)
      m_candidate_list->setCurrentRow(row_to_select);
    else if (!m_rows.empty())
      m_candidate_list->setCurrentRow(0);
  }

  if (m_rows.empty())
  {
    m_status_label->setText(
        tr("No live seed candidates currently match this shader import.\n"
           "Keep the game rendering this scene, disable 'Show Matching Candidates Only', or open "
           "Elements Hunting for manual inspection."));
  }
  else
  {
    const int matching_count = static_cast<int>(std::count_if(
        m_rows.begin(), m_rows.end(), [](const CandidateRow& row) { return row.full_match; }));
    m_status_label->setText(
        tr("Candidates: %1 total, %2 matching.\nSelect one candidate to use as the runtime "
           "element seed.")
            .arg(m_rows.size())
            .arg(matching_count));
  }

  OnSelectionChanged();
}

void ImportShaderFindElementDialog::OnSelectionChanged()
{
  m_use_candidate_button->setEnabled(!m_candidate_list->selectedItems().empty());
}
