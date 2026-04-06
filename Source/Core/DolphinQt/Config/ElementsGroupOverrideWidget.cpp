// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/Config/ElementsGroupOverrideWidget.h"

#include <algorithm>
#include <optional>

#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "Common/Logging/Log.h"
#include "DolphinQt/Config/ElementsGroupOverrideAddEditDialog.h"
#include "DolphinQt/Config/ElementsHuntingDialog.h"
#include "DolphinQt/Config/ImportShaderFindElementDialog.h"
#include "DolphinQt/QtUtils/NonDefaultQPushButton.h"
#include "VideoCommon/ShaderHunter.h"

namespace
{
QString FormatShaderOverrideImportLabel(const ShaderHunter::ShaderOverride& entry)
{
  const QString type =
      entry.type == ShaderHunter::ShaderType::Vertex   ? QStringLiteral("VS") :
      entry.type == ShaderHunter::ShaderType::Geometry ? QStringLiteral("GS") :
                                                         QStringLiteral("PS");
  const QString handling =
      entry.handling == ShaderHunter::HandlingType::Screen         ? QStringLiteral("screen") :
      entry.handling == ShaderHunter::HandlingType::Fullscreen     ? QStringLiteral("fullscreen") :
      entry.handling == ShaderHunter::HandlingType::FullscreenMono ? QStringLiteral("fullscreen_mono") :
      entry.handling == ShaderHunter::HandlingType::HeadLocked     ? QStringLiteral("headlocked") :
      entry.handling == ShaderHunter::HandlingType::Flag           ? QStringLiteral("flag") :
      entry.handling == ShaderHunter::HandlingType::UnitsPerMeter  ? QStringLiteral("units_per_meter") :
                                                                     QStringLiteral("skip");
  QString label = QStringLiteral("%1 [%2 %3]")
                      .arg(QString::fromStdString(entry.name), type, handling);
  if (!entry.texture_hashes.empty())
    label += QStringLiteral(" tex[%1]").arg(entry.texture_hashes.size());
  if (!entry.enabled)
    label += QStringLiteral(" off");
  return label;
}

std::optional<ShaderHunter::ShaderOverride> SelectShaderOverrideForImport(
    QWidget* parent, const std::vector<ShaderHunter::ShaderOverride>& overrides)
{
  if (overrides.empty())
    return std::nullopt;

  QDialog dialog(parent);
  dialog.setWindowTitle(QObject::tr("Import From Shader"));
  dialog.setMinimumWidth(420);

  auto* list = new QListWidget(&dialog);
  for (size_t i = 0; i < overrides.size(); ++i)
  {
    auto* item = new QListWidgetItem(FormatShaderOverrideImportLabel(overrides[i]), list);
    item->setData(Qt::UserRole, static_cast<int>(i));
  }
  list->setSelectionMode(QAbstractItemView::SingleSelection);
  if (list->count() > 0)
    list->setCurrentRow(0);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  auto* ok = buttons->button(QDialogButtonBox::Ok);
  if (ok != nullptr)
    ok->setText(QObject::tr("Import"));

  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  auto* layout = new QVBoxLayout(&dialog);
  layout->addWidget(new QLabel(QObject::tr("Choose a saved Shader Override to import."), &dialog));
  layout->addWidget(list);
  layout->addWidget(buttons);

  if (dialog.exec() != QDialog::Accepted || list->currentItem() == nullptr)
    return std::nullopt;

  const int index = list->currentItem()->data(Qt::UserRole).toInt();
  if (index < 0 || index >= static_cast<int>(overrides.size()))
    return std::nullopt;

  return overrides[static_cast<size_t>(index)];
}
}  // namespace

ElementsGroupOverrideWidget::ElementsGroupOverrideWidget(std::string game_id)
    : m_game_id(std::move(game_id))
{
  CreateWidgets();
  ConnectWidgets();
  LoadOverrides();
}

ElementsGroupOverrideWidget::~ElementsGroupOverrideWidget() = default;

void ElementsGroupOverrideWidget::CreateWidgets()
{
  m_code_list = new QListWidget;

  auto* info_label = new QLabel(
      tr("Elements Group Overrides use captured runtime draw signatures instead of only shader "
         "hashes.\nUse Elements Hunting to isolate frame-local draw groups, then save a new "
         "override."));
  info_label->setWordWrap(true);

  m_code_add = new NonDefaultQPushButton(tr("&Add Override"));
  m_code_import = new NonDefaultQPushButton(tr("Import from Shader"));
  m_code_edit = new NonDefaultQPushButton(tr("&Edit Override"));
  m_code_remove = new NonDefaultQPushButton(tr("&Remove Override"));
  m_code_refresh = new NonDefaultQPushButton(tr("Re&fresh List"));
  m_code_reload = new NonDefaultQPushButton(tr("Reload &Overrides"));
  m_elements_hunting = new NonDefaultQPushButton(tr("Elements Hunting"));

  m_code_edit->setEnabled(false);
  m_code_remove->setEnabled(false);

  auto* button_layout = new QHBoxLayout;
  button_layout->addWidget(m_elements_hunting);
  button_layout->addWidget(m_code_add);
  button_layout->addWidget(m_code_import);
  button_layout->addWidget(m_code_edit);
  button_layout->addWidget(m_code_remove);
  button_layout->addStretch();
  button_layout->addWidget(m_code_refresh);
  button_layout->addWidget(m_code_reload);

  auto* layout = new QVBoxLayout(this);
  layout->addWidget(info_label);
  layout->addWidget(m_code_list);
  layout->addLayout(button_layout);
}

void ElementsGroupOverrideWidget::ConnectWidgets()
{
  connect(m_code_list, &QListWidget::itemChanged, this, &ElementsGroupOverrideWidget::OnItemChanged);
  connect(m_code_list, &QListWidget::itemSelectionChanged, this,
          &ElementsGroupOverrideWidget::OnSelectionChanged);
  connect(m_code_add, &QPushButton::clicked, this, &ElementsGroupOverrideWidget::OnAddClicked);
  connect(m_code_import, &QPushButton::clicked, this, &ElementsGroupOverrideWidget::OnImportClicked);
  connect(m_code_edit, &QPushButton::clicked, this, &ElementsGroupOverrideWidget::OnEditClicked);
  connect(m_code_remove, &QPushButton::clicked, this, &ElementsGroupOverrideWidget::OnRemoveClicked);
  connect(m_code_refresh, &QPushButton::clicked, this, &ElementsGroupOverrideWidget::OnRefreshClicked);
  connect(m_code_reload, &QPushButton::clicked, this, &ElementsGroupOverrideWidget::OnReloadClicked);
  connect(m_elements_hunting, &QPushButton::clicked, this, &ElementsGroupOverrideWidget::OnHuntClicked);
}

void ElementsGroupOverrideWidget::LoadOverrides()
{
  m_overrides = ElementsGroupManager::LoadOverridesFromINI(m_game_id);
  UpdateList();
  m_code_edit->setEnabled(false);
  m_code_remove->setEnabled(false);
}

void ElementsGroupOverrideWidget::SaveOverrides()
{
  ElementsGroupManager::SaveOverridesToINI(m_game_id, m_overrides);
}

void ElementsGroupOverrideWidget::UpdateList()
{
  const QSignalBlocker blocker(m_code_list);
  m_code_list->clear();

  for (size_t i = 0; i < m_overrides.size(); ++i)
  {
    const auto& entry = m_overrides[i];
    QString label = QStringLiteral("%1  (%2)")
                        .arg(QString::fromStdString(entry.name))
                        .arg(QString::fromLatin1(entry.handling == ShaderHunter::HandlingType::Screen       ? "screen" :
                                                 entry.handling == ShaderHunter::HandlingType::Fullscreen   ? "fullscreen" :
                                                 entry.handling == ShaderHunter::HandlingType::FullscreenMono ? "fullscreen_mono" :
                                                 entry.handling == ShaderHunter::HandlingType::HeadLocked   ? "headlocked" :
                                                 entry.handling == ShaderHunter::HandlingType::Flag         ? "flag" :
                                                 entry.handling == ShaderHunter::HandlingType::UnitsPerMeter ? "units_per_meter" :
                                                                                                             "skip"));
    if (entry.runtime_element.use_projection)
      label += QStringLiteral(" proj");
    if (entry.runtime_element.use_layer)
      label += QStringLiteral(" layer");
    if (entry.runtime_element.use_viewport)
      label += QStringLiteral(" vp");
    if (entry.runtime_element.use_scissor)
      label += QStringLiteral(" scissor");
    if (entry.runtime_element.use_render_state)
      label += QStringLiteral(" state");
    if (!entry.texture_hashes.empty())
      label += QStringLiteral(" tex");
    if (!entry.selected_match_filter.empty())
    {
      label += QStringLiteral(" %1[%2]")
                   .arg(entry.selected_match_filter_excluded ? QStringLiteral("match_exclude") :
                                                               QStringLiteral("matches"))
                   .arg(entry.selected_match_filter.size());
    }
    if (entry.element_start >= 0 && entry.element_end >= 0)
      label += QStringLiteral(" draw[%1-%2]").arg(entry.element_start).arg(entry.element_end);
    if (!entry.condition_flag.empty())
      label += QStringLiteral(" when(%1%2)")
                   .arg(entry.condition_inverted ? QStringLiteral("!") : QString())
                   .arg(QString::fromStdString(entry.condition_flag));
    if (entry.refinement_enabled)
    {
      const QString refine_type =
          entry.refinement_type == ShaderHunter::ShaderType::Vertex   ? QStringLiteral("VS") :
          entry.refinement_type == ShaderHunter::ShaderType::Geometry ? QStringLiteral("GS") :
                                                                        QStringLiteral("PS");
      label += QStringLiteral(" refine[%1 %2]")
                   .arg(refine_type)
                   .arg(entry.refinement_family_match ? QStringLiteral("family") :
                                                       QStringLiteral("exact"));
    }

    auto* item = new QListWidgetItem(label);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
    item->setCheckState(entry.enabled ? Qt::Checked : Qt::Unchecked);
    item->setData(Qt::UserRole, static_cast<int>(i));
    m_code_list->addItem(item);
  }
}

std::vector<std::string> ElementsGroupOverrideWidget::CollectAvailableFlags() const
{
  std::vector<std::string> flags;
  const auto shader_overrides = ShaderHunter::LoadOverridesFromINI(m_game_id);
  for (const auto& entry : shader_overrides)
  {
    if (!entry.flag_group.empty() &&
        std::find(flags.begin(), flags.end(), entry.flag_group) == flags.end())
    {
      flags.push_back(entry.flag_group);
    }
  }
  for (const auto& entry : m_overrides)
  {
    if (!entry.flag_group.empty() &&
        std::find(flags.begin(), flags.end(), entry.flag_group) == flags.end())
    {
      flags.push_back(entry.flag_group);
    }
  }
  return flags;
}

void ElementsGroupOverrideWidget::OnItemChanged(QListWidgetItem* item)
{
  const int idx = item->data(Qt::UserRole).toInt();
  if (idx < 0 || idx >= static_cast<int>(m_overrides.size()))
    return;
  m_overrides[idx].enabled = item->checkState() == Qt::Checked;
  INFO_LOG_FMT(VIDEO, "ElementsGroup: override '{}' {}", m_overrides[idx].name,
               m_overrides[idx].enabled ? "enabled" : "disabled");
  SaveOverrides();
  ElementsGroupManager::GetInstance().LoadOverrides(m_game_id);
}

void ElementsGroupOverrideWidget::OnSelectionChanged()
{
  const bool has_selection = !m_code_list->selectedItems().empty();
  m_code_edit->setEnabled(has_selection);
  m_code_remove->setEnabled(has_selection);
}

void ElementsGroupOverrideWidget::OnAddClicked()
{
  ElementsGroupOverrideAddEditDialog dialog(this, nullptr, CollectAvailableFlags());
  if (dialog.exec() != QDialog::Accepted)
    return;
  auto result = dialog.GetResult();
  result.enabled = true;
  result.user_defined = true;
  m_overrides.push_back(std::move(result));
  SaveOverrides();
  LoadOverrides();
  ElementsGroupManager::GetInstance().LoadOverrides(m_game_id);
}

void ElementsGroupOverrideWidget::OnImportClicked()
{
  std::vector<ShaderHunter::ShaderOverride> supported_overrides;
  for (const auto& entry : ShaderHunter::LoadOverridesFromINI(m_game_id))
  {
    if (entry.match_mode == ShaderHunter::MatchMode::RuntimeElement)
      continue;
    supported_overrides.push_back(entry);
  }

  if (supported_overrides.empty())
  {
    QMessageBox::information(this, tr("Import from Shader"),
                             tr("No compatible saved Shader Overrides were found."));
    return;
  }

  const auto selected = SelectShaderOverrideForImport(this, supported_overrides);
  if (!selected.has_value())
    return;

  ImportShaderFindElementDialog find_dialog(m_game_id, *selected, this);
  const int result_code = find_dialog.exec();
  if (find_dialog.ShouldOpenElementsHunting())
  {
    OnHuntClicked();
    return;
  }
  if (result_code != QDialog::Accepted)
    return;

  const auto selected_candidate = find_dialog.GetSelectedCandidate();
  if (!selected_candidate.has_value())
  {
    QMessageBox::warning(this, tr("Import from Shader"),
                         tr("No runtime element seed candidate was selected."));
    return;
  }

  auto import_runtime_element =
      ElementsGroupManager::MakeSelectedMatchFilterSignature(selected_candidate->signature);
  ElementsGroupOverrideAddEditDialog dialog(this, nullptr, CollectAvailableFlags(), &*selected,
                                            &import_runtime_element);
  if (dialog.exec() != QDialog::Accepted)
    return;

  auto result = dialog.GetResult();
  result.enabled = true;
  result.user_defined = true;
  m_overrides.push_back(std::move(result));
  SaveOverrides();
  LoadOverrides();
  ElementsGroupManager::GetInstance().LoadOverrides(m_game_id);
}

void ElementsGroupOverrideWidget::OnEditClicked()
{
  const auto items = m_code_list->selectedItems();
  if (items.empty())
    return;
  const int idx = items[0]->data(Qt::UserRole).toInt();
  if (idx < 0 || idx >= static_cast<int>(m_overrides.size()))
    return;

  ElementsGroupOverrideAddEditDialog dialog(this, &m_overrides[idx], CollectAvailableFlags());
  if (dialog.exec() != QDialog::Accepted)
    return;

  auto result = dialog.GetResult();
  result.enabled = m_overrides[idx].enabled;
  m_overrides[idx] = std::move(result);
  SaveOverrides();
  LoadOverrides();
  ElementsGroupManager::GetInstance().LoadOverrides(m_game_id);
}

void ElementsGroupOverrideWidget::OnRemoveClicked()
{
  const auto items = m_code_list->selectedItems();
  if (items.empty())
    return;
  const int idx = items[0]->data(Qt::UserRole).toInt();
  if (idx < 0 || idx >= static_cast<int>(m_overrides.size()))
    return;

  m_overrides.erase(m_overrides.begin() + idx);
  SaveOverrides();
  LoadOverrides();
  ElementsGroupManager::GetInstance().LoadOverrides(m_game_id);
}

void ElementsGroupOverrideWidget::OnRefreshClicked()
{
  LoadOverrides();
}

void ElementsGroupOverrideWidget::OnReloadClicked()
{
  INFO_LOG_FMT(VIDEO, "ElementsGroup: reload requested for {}", m_game_id);
  ElementsGroupManager::GetInstance().LoadOverrides(m_game_id);
}

void ElementsGroupOverrideWidget::OnHuntClicked()
{
  if (m_hunting_dialog)
  {
    m_hunting_dialog->show();
    m_hunting_dialog->raise();
    m_hunting_dialog->activateWindow();
    return;
  }

  m_hunting_dialog = new ElementsHuntingDialog(m_game_id, this);
  m_hunting_dialog->setAttribute(Qt::WA_DeleteOnClose, true);
  connect(m_hunting_dialog, &ElementsHuntingDialog::OverridesChanged, this,
          [this]() {
            LoadOverrides();
            ElementsGroupManager::GetInstance().LoadOverrides(m_game_id);
          });
  connect(m_hunting_dialog, &QObject::destroyed, this,
          [this]() { m_hunting_dialog = nullptr; });
  m_hunting_dialog->show();
}
