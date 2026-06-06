// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/Config/TextureElementOverrideWidget.h"

#include <algorithm>

#include <QColor>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

#include <fmt/format.h>

#include "DolphinQt/Config/TextureElementOverrideAddEditDialog.h"
#include "DolphinQt/Config/TextureHashBrowserDialog.h"
#include "DolphinQt/QtUtils/NonDefaultQPushButton.h"
#include "VideoCommon/TextureElementManager.h"

using HandlingType = TextureElementManager::HandlingType;
using TextureElementOverride = TextureElementManager::TextureElementOverride;

TextureElementOverrideWidget::TextureElementOverrideWidget(std::string game_id,
                                                           std::optional<u16> revision)
    : m_game_id(std::move(game_id)), m_revision(revision)
{
  CreateWidgets();
  ConnectWidgets();
  LoadOverrides();
}

TextureElementOverrideWidget::~TextureElementOverrideWidget() = default;

void TextureElementOverrideWidget::CreateWidgets()
{
  m_code_list = new QListWidget;

  auto* info_label = new QLabel(
      tr("Texture Element Overrides reclassify VR draws based purely on the bound texture hash,\n"
         "across all shaders and elements. Use the Texture Hunter to identify a texture, then\n"
         "apply a handling to every draw that binds it.\n"
         "Handling: Skip = hide, Screen = world-fixed, Head Locked = follows head,\n"
         "Fullscreen = no VR. Applied as a fallback after Shader and Elements Group overrides."));
  info_label->setWordWrap(true);

  m_texture_hunter = new NonDefaultQPushButton(tr("Texture Hunter"));
  m_code_add = new NonDefaultQPushButton(tr("&Add Texture"));
  m_code_edit = new NonDefaultQPushButton(tr("&Edit Texture"));
  m_code_edit->setEnabled(false);
  m_code_remove = new NonDefaultQPushButton(tr("&Remove Texture"));
  m_code_remove->setEnabled(false);
  m_code_refresh = new NonDefaultQPushButton(tr("Re&fresh List"));
  m_code_reload = new NonDefaultQPushButton(tr("Reload &Override"));

  auto* button_layout = new QHBoxLayout;
  button_layout->addWidget(m_texture_hunter);
  button_layout->addWidget(m_code_add);
  button_layout->addWidget(m_code_edit);
  button_layout->addWidget(m_code_remove);
  button_layout->addStretch();
  button_layout->addWidget(m_code_refresh);
  button_layout->addWidget(m_code_reload);

  auto* layout = new QVBoxLayout{this};
  layout->addWidget(info_label);
  layout->addWidget(m_code_list);
  layout->addLayout(button_layout);
}

void TextureElementOverrideWidget::ConnectWidgets()
{
  connect(m_code_list, &QListWidget::itemChanged, this,
          &TextureElementOverrideWidget::OnItemChanged);
  connect(m_code_list, &QListWidget::itemSelectionChanged, this,
          &TextureElementOverrideWidget::OnSelectionChanged);
  connect(m_texture_hunter, &QPushButton::clicked, this,
          &TextureElementOverrideWidget::OnTextureHunterClicked);
  connect(m_code_add, &QPushButton::clicked, this, &TextureElementOverrideWidget::OnAddClicked);
  connect(m_code_edit, &QPushButton::clicked, this, &TextureElementOverrideWidget::OnEditClicked);
  connect(m_code_remove, &QPushButton::clicked, this,
          &TextureElementOverrideWidget::OnRemoveClicked);
  connect(m_code_refresh, &QPushButton::clicked, this,
          &TextureElementOverrideWidget::OnRefreshClicked);
  connect(m_code_reload, &QPushButton::clicked, this,
          &TextureElementOverrideWidget::OnReloadClicked);
}

void TextureElementOverrideWidget::LoadOverrides()
{
  m_overrides = TextureElementManager::LoadOverridesFromINI(m_game_id, m_revision);

  m_code_list->setEnabled(!m_game_id.empty());
  m_code_remove->setEnabled(false);
  m_code_edit->setEnabled(false);

  UpdateList();
}

void TextureElementOverrideWidget::SaveOverrides()
{
  TextureElementManager::SaveOverridesToINI(m_game_id, m_overrides);
}

void TextureElementOverrideWidget::ReloadRuntime()
{
  TextureElementManager::GetInstance().LoadOverrides(m_game_id);
}

void TextureElementOverrideWidget::UpdateList()
{
  // Block signals to prevent itemChanged → SaveOverrides() during list construction.
  const QSignalBlocker blocker(m_code_list);
  m_code_list->clear();

  for (size_t idx = 0; idx < m_overrides.size(); idx++)
  {
    const auto& ovr = m_overrides[idx];

    const char* handling_str =
        ovr.handling == HandlingType::Screen     ? "screen" :
        ovr.handling == HandlingType::Fullscreen ||
                ovr.handling == HandlingType::FullscreenMono ? "fullscreen" :
        ovr.handling == HandlingType::HeadLocked    ? "headlocked" :
        ovr.handling == HandlingType::UnitsPerMeter ? "units_per_meter" :
                                                      "skip";

    QString label = QStringLiteral("%1  (%2)  [%3 tex]")
                        .arg(QString::fromStdString(ovr.name))
                        .arg(QString::fromLatin1(handling_str))
                        .arg(ovr.texture_hashes.size());

    if (ovr.handling == HandlingType::Screen || ovr.handling == HandlingType::HeadLocked)
    {
      if (ovr.layer >= 0)
        label += QStringLiteral(" L%1").arg(ovr.layer);
      if (ovr.element_depth >= 0.0f)
        label += QStringLiteral(" D%1").arg(ovr.element_depth, 0, 'f', 4);
    }
    else if (ovr.handling == HandlingType::UnitsPerMeter && ovr.units_per_meter > 0.0f)
    {
      label += QStringLiteral(" UPM%1").arg(ovr.units_per_meter, 0, 'f', 2);
    }

    if (ovr.clear_efb)
    {
      if (ovr.clear_efb_min_width > 0 || ovr.clear_efb_max_width > 0)
        label += QStringLiteral(" +clearEFB(%1-%2)")
                     .arg(ovr.clear_efb_min_width)
                     .arg(ovr.clear_efb_max_width > 0 ? ovr.clear_efb_max_width : 640);
      else
        label += QStringLiteral(" +clearEFB");
    }

    if (!ovr.texture_hashes.empty())
    {
      QStringList texture_hashes;
      const int shown = std::min<int>(3, static_cast<int>(ovr.texture_hashes.size()));
      for (int i = 0; i < shown; i++)
      {
        texture_hashes.append(
            QString::number(ovr.texture_hashes[static_cast<size_t>(i)], 16)
                .rightJustified(16, QLatin1Char('0')));
      }
      if (static_cast<int>(ovr.texture_hashes.size()) > shown)
        texture_hashes.append(
            tr("+%1 more").arg(static_cast<int>(ovr.texture_hashes.size()) - shown));
      label += QStringLiteral(": %1").arg(texture_hashes.join(QStringLiteral(", ")));
    }

    auto* item = new QListWidgetItem(label);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
    item->setCheckState(ovr.enabled ? Qt::Checked : Qt::Unchecked);
    item->setData(Qt::UserRole, static_cast<int>(idx));
    m_code_list->addItem(item);
  }
}

std::string TextureElementOverrideWidget::MakeUniqueName(const std::string& base) const
{
  const auto name_taken = [this](const std::string& name) {
    return std::any_of(m_overrides.begin(), m_overrides.end(),
                       [&name](const TextureElementOverride& ovr) { return ovr.name == name; });
  };

  if (!name_taken(base))
    return base;
  for (int i = 2;; i++)
  {
    const std::string candidate = fmt::format("{} {}", base, i);
    if (!name_taken(candidate))
      return candidate;
  }
}

void TextureElementOverrideWidget::OnItemChanged(QListWidgetItem* item)
{
  const int idx = item->data(Qt::UserRole).toInt();
  if (idx < 0 || idx >= static_cast<int>(m_overrides.size()))
    return;

  m_overrides[idx].enabled = (item->checkState() == Qt::Checked);
  SaveOverrides();
  ReloadRuntime();
}

void TextureElementOverrideWidget::OnSelectionChanged()
{
  const bool has_selection = !m_code_list->selectedItems().empty();
  m_code_remove->setEnabled(has_selection);
  m_code_edit->setEnabled(has_selection);
}

void TextureElementOverrideWidget::OnTextureHunterClicked()
{
  if (m_texture_hunter_dialog)
  {
    m_texture_hunter_dialog->show();
    m_texture_hunter_dialog->raise();
    m_texture_hunter_dialog->activateWindow();
    return;
  }

  TextureHashBrowserConfig browser_config;
  browser_config.title = tr("Texture Hunter");
  browser_config.empty_info_text =
      tr("No textures captured yet.\n"
         "Start a game in VR so its textures can be enumerated.");
  browser_config.current_label = tr("currently loaded");
  browser_config.fetch_current_entries = []() {
    std::vector<TextureHashBrowserEntry> entries;
    for (const auto& tex : TextureElementManager::GetInstance().GetCurrentTextures())
      entries.push_back(TextureHashBrowserEntry{.hash = tex.hash, .name = tex.name});
    return entries;
  };
  // Apply checked textures by creating a new Screen override (editable afterwards).
  browser_config.apply_selected_hashes = [this](const std::vector<u64>& hashes) {
    if (hashes.empty())
      return;
    TextureElementOverride ovr;
    ovr.name = MakeUniqueName(tr("Texture Override").toStdString());
    ovr.handling = HandlingType::Screen;
    ovr.texture_hashes = hashes;
    std::sort(ovr.texture_hashes.begin(), ovr.texture_hashes.end());
    ovr.texture_hashes.erase(std::unique(ovr.texture_hashes.begin(), ovr.texture_hashes.end()),
                             ovr.texture_hashes.end());
    ovr.enabled = true;
    m_overrides.push_back(std::move(ovr));
    SaveOverrides();
    UpdateList();
    ReloadRuntime();
  };

  // Enable global texture capture while the browser is open; restore on close.
  TextureElementManager::GetInstance().SetHunterActive(true);
  m_texture_hunter_dialog = ShowTextureHashBrowserDialog(this, browser_config);
  connect(m_texture_hunter_dialog, &QObject::destroyed, this,
          [] { TextureElementManager::GetInstance().SetHunterActive(false); });
}

void TextureElementOverrideWidget::OnAddClicked()
{
  TextureElementOverrideAddEditDialog dialog(this, nullptr);
  if (dialog.exec() != QDialog::Accepted)
    return;

  m_overrides.push_back(dialog.GetResult());
  SaveOverrides();
  UpdateList();
  ReloadRuntime();
}

void TextureElementOverrideWidget::OnEditClicked()
{
  const auto items = m_code_list->selectedItems();
  if (items.empty())
    return;

  const int idx = items[0]->data(Qt::UserRole).toInt();
  if (idx < 0 || idx >= static_cast<int>(m_overrides.size()))
    return;

  TextureElementOverrideAddEditDialog dialog(this, &m_overrides[idx]);
  if (dialog.exec() != QDialog::Accepted)
    return;

  auto result = dialog.GetResult();
  result.enabled = m_overrides[idx].enabled;  // Preserve enabled state
  m_overrides[idx] = result;
  SaveOverrides();
  UpdateList();
  ReloadRuntime();
}

void TextureElementOverrideWidget::OnRemoveClicked()
{
  const auto items = m_code_list->selectedItems();
  if (items.empty())
    return;

  const int idx = items[0]->data(Qt::UserRole).toInt();
  if (idx < 0 || idx >= static_cast<int>(m_overrides.size()))
    return;

  m_overrides.erase(m_overrides.begin() + idx);

  SaveOverrides();
  UpdateList();
  ReloadRuntime();

  m_code_remove->setEnabled(false);
  m_code_edit->setEnabled(false);
}

void TextureElementOverrideWidget::OnRefreshClicked()
{
  LoadOverrides();
}

void TextureElementOverrideWidget::OnReloadClicked()
{
  ReloadRuntime();
}
