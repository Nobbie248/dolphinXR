// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/Config/TextureElementOverrideAddEditDialog.h"

#include <algorithm>
#include <cctype>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

#include <fmt/format.h>

#include "DolphinQt/Config/TextureHashBrowserDialog.h"

namespace
{
constexpr double UPM_OVERRIDE_MIN = 0.01;
constexpr double UPM_OVERRIDE_MAX = 1000.0;
constexpr double UPM_OVERRIDE_STEP = 0.01;
constexpr int MAX_VISIBLE_TEXTURE_HASH_ROWS = 10;
constexpr int TEXTURE_HASH_ROW_HEIGHT = 28;
}  // namespace

using HandlingType = TextureElementManager::HandlingType;
using TextureElementOverride = TextureElementManager::TextureElementOverride;

TextureElementOverrideAddEditDialog::TextureElementOverrideAddEditDialog(
    QWidget* parent, const TextureElementOverride* edit_override)
    : QDialog(parent)
{
  setWindowTitle(edit_override ? tr("Edit Texture Element Override") :
                                 tr("Add Texture Element Override"));
  setMinimumWidth(350);

  m_name_edit = new QLineEdit;
  m_name_edit->setPlaceholderText(tr("Override name..."));

  m_comments_edit = new QPlainTextEdit;
  m_comments_edit->setPlaceholderText(tr("Optional notes..."));
  m_comments_edit->setTabChangesFocus(true);
  m_comments_edit->setMinimumHeight(70);

  m_handling_combo = new QComboBox;
  m_handling_combo->addItem(tr("Skip"), static_cast<int>(HandlingType::Skip));
  m_handling_combo->addItem(tr("Screen"), static_cast<int>(HandlingType::Screen));
  m_handling_combo->addItem(tr("Fullscreen"), static_cast<int>(HandlingType::Fullscreen));
  m_handling_combo->addItem(tr("Head Locked"), static_cast<int>(HandlingType::HeadLocked));
  m_handling_combo->addItem(tr("Units per Meter"), static_cast<int>(HandlingType::UnitsPerMeter));
  m_handling_combo->setToolTip(
      tr("How every draw that binds a listed texture is handled in VR.\n"
         "Skip = hide, Screen = world-fixed, Head Locked = follows head, Fullscreen = no VR."));

  m_layer_label = new QLabel(tr("Layer:"));
  m_layer_spin = new QSpinBox;
  m_layer_spin->setRange(-1, 999);
  m_layer_spin->setSpecialValueText(tr("Auto"));
  m_layer_spin->setValue(-1);
  m_layer_spin->setToolTip(tr("Manual depth layer for Screen/Head Locked handling.\n"
                              "-1 (Auto) = automatic per-draw counter.\n"
                              "Higher values are closer to camera."));

  m_element_depth_label = new QLabel(tr("Element Depth:"));
  m_element_depth_spin = new QDoubleSpinBox;
  m_element_depth_spin->setRange(-1.0, 0.01);
  m_element_depth_spin->setDecimals(4);
  m_element_depth_spin->setSingleStep(0.0001);
  m_element_depth_spin->setSpecialValueText(tr("Global"));
  m_element_depth_spin->setValue(-1.0);
  m_element_depth_spin->setToolTip(tr("Within-element depth range for these textures.\n"
                                      "-1 (Global) = use the global Element Depth setting."));

  m_units_per_meter_label = new QLabel(tr("Units per Meter:"));
  m_units_per_meter_spin = new QDoubleSpinBox;
  m_units_per_meter_spin->setRange(UPM_OVERRIDE_MIN, UPM_OVERRIDE_MAX);
  m_units_per_meter_spin->setDecimals(2);
  m_units_per_meter_spin->setSingleStep(UPM_OVERRIDE_STEP);
  m_units_per_meter_spin->setValue(1.0);
  m_units_per_meter_spin->setToolTip(tr("Temporary per-texture scale override for VR.\n"
                                        "Higher values make these textures appear larger."));

  m_clear_efb_check = new QCheckBox(tr("Clear EFB Copy"));
  m_clear_efb_check->setToolTip(
      tr("When a listed texture is drawn, the next EFB copy will be cleared to\n"
         "transparent instead of copying the framebuffer content.\n"
         "Use this to remove post-processing effects (bloom, blur) in VR.\n"
         "Can be combined with any handling type (Skip, Screen, etc.)."));

  m_clear_efb_min_label = new QLabel(tr("EFB Min Width:"));
  m_clear_efb_min_spin = new QSpinBox;
  m_clear_efb_min_spin->setRange(0, 640);
  m_clear_efb_min_spin->setSingleStep(10);
  m_clear_efb_min_spin->setSpecialValueText(tr("Any"));
  m_clear_efb_min_spin->setValue(0);
  m_clear_efb_min_spin->setToolTip(tr("Only clear EFB copies whose native width >= this value.\n"
                                      "0 (Any) = no lower bound."));

  m_clear_efb_max_label = new QLabel(tr("EFB Max Width:"));
  m_clear_efb_max_spin = new QSpinBox;
  m_clear_efb_max_spin->setRange(0, 640);
  m_clear_efb_max_spin->setSingleStep(10);
  m_clear_efb_max_spin->setSpecialValueText(tr("Any"));
  m_clear_efb_max_spin->setValue(0);
  m_clear_efb_max_spin->setToolTip(tr("Only clear EFB copies whose native width <= this value.\n"
                                      "0 (Any) = no upper bound."));

  m_view_textures_button = new QPushButton(tr("Texture Hunter"));
  m_view_textures_button->setToolTip(
      tr("Browse currently-loaded textures and check the ones to add to this override."));

  m_texture_hash_container = new QWidget;
  m_texture_hash_layout = new QVBoxLayout(m_texture_hash_container);
  m_texture_hash_layout->setContentsMargins(0, 0, 0, 0);
  m_texture_hash_layout->setSpacing(4);
  m_texture_hash_scroll = new QScrollArea;
  m_texture_hash_scroll->setWidgetResizable(true);
  m_texture_hash_scroll->setFrameShape(QFrame::NoFrame);
  m_texture_hash_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_texture_hash_scroll->setWidget(m_texture_hash_container);
  AddTextureHashField(QString());

  // Pre-fill fields in edit mode.
  if (edit_override)
  {
    m_name_edit->setText(QString::fromStdString(edit_override->name));
    m_comments_edit->setPlainText(QString::fromStdString(edit_override->comments));

    const int handling_idx = m_handling_combo->findData(static_cast<int>(edit_override->handling));
    if (handling_idx >= 0)
      m_handling_combo->setCurrentIndex(handling_idx);

    m_layer_spin->setValue(edit_override->layer);
    m_element_depth_spin->setValue(edit_override->element_depth);
    if (edit_override->units_per_meter > 0.0f)
      m_units_per_meter_spin->setValue(edit_override->units_per_meter);

    m_clear_efb_check->setChecked(edit_override->clear_efb);
    m_clear_efb_min_spin->setValue(edit_override->clear_efb_min_width);
    m_clear_efb_max_spin->setValue(edit_override->clear_efb_max_width);

    m_updating_texture_hash_fields = true;
    while (m_texture_hash_edits.size() < edit_override->texture_hashes.size())
      AddTextureHashField(QString());
    for (size_t i = 0; i < edit_override->texture_hashes.size(); i++)
    {
      m_texture_hash_edits[i]->setText(
          QString::fromStdString(fmt::format("{:016x}", edit_override->texture_hashes[i])));
    }
    for (size_t i = edit_override->texture_hashes.size(); i < m_texture_hash_edits.size(); i++)
      m_texture_hash_edits[i]->clear();
    m_updating_texture_hash_fields = false;
    EnsureTextureHashFieldRows();
  }

  auto* form = new QFormLayout;
  form->addRow(tr("Name:"), m_name_edit);
  form->addRow(tr("Handling:"), m_handling_combo);
  form->addRow(m_layer_label, m_layer_spin);
  form->addRow(m_element_depth_label, m_element_depth_spin);
  form->addRow(m_units_per_meter_label, m_units_per_meter_spin);
  form->addRow(QString(), m_clear_efb_check);
  form->addRow(m_clear_efb_min_label, m_clear_efb_min_spin);
  form->addRow(m_clear_efb_max_label, m_clear_efb_max_spin);
  form->addRow(QString(), m_view_textures_button);
  form->addRow(tr("Texture Hashes:"), m_texture_hash_scroll);
  form->addRow(tr("Comments:"), m_comments_edit);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  connect(buttons, &QDialogButtonBox::accepted, this,
          &TextureElementOverrideAddEditDialog::OnAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(m_handling_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &TextureElementOverrideAddEditDialog::OnHandlingChanged);
  connect(m_clear_efb_check, &QCheckBox::toggled, this,
          &TextureElementOverrideAddEditDialog::OnClearEFBChanged);
  connect(m_view_textures_button, &QPushButton::clicked, this,
          &TextureElementOverrideAddEditDialog::ShowTextureBrowser);

  auto* layout = new QVBoxLayout;
  layout->addLayout(form);
  layout->addWidget(buttons);
  setLayout(layout);

  OnHandlingChanged();
  OnClearEFBChanged();
}

TextureElementOverride TextureElementOverrideAddEditDialog::GetResult() const
{
  TextureElementOverride result;
  result.name = m_name_edit->text().toStdString();
  result.comments = m_comments_edit->toPlainText().trimmed().toStdString();
  result.handling = static_cast<HandlingType>(m_handling_combo->currentData().toInt());
  result.layer = (result.handling == HandlingType::Screen ||
                  result.handling == HandlingType::HeadLocked) ?
                     m_layer_spin->value() :
                     -1;
  result.element_depth = (result.handling == HandlingType::Screen ||
                          result.handling == HandlingType::HeadLocked) ?
                             static_cast<float>(m_element_depth_spin->value()) :
                             -1.0f;
  result.units_per_meter = result.handling == HandlingType::UnitsPerMeter ?
                               static_cast<float>(m_units_per_meter_spin->value()) :
                               -1.0f;
  result.clear_efb = m_clear_efb_check->isChecked();
  result.clear_efb_min_width = result.clear_efb ? m_clear_efb_min_spin->value() : 0;
  result.clear_efb_max_width = result.clear_efb ? m_clear_efb_max_spin->value() : 0;

  for (const std::string& token : CollectTextureHashTokens())
  {
    const u64 parsed = std::strtoull(token.c_str(), nullptr, 16);
    if (parsed != 0)
      result.texture_hashes.push_back(parsed);
  }
  std::sort(result.texture_hashes.begin(), result.texture_hashes.end());
  result.texture_hashes.erase(
      std::unique(result.texture_hashes.begin(), result.texture_hashes.end()),
      result.texture_hashes.end());

  result.enabled = true;
  return result;
}

void TextureElementOverrideAddEditDialog::OnAccept()
{
  if (m_name_edit->text().trimmed().isEmpty())
  {
    QMessageBox::warning(this, tr("Validation Error"), tr("Name cannot be empty."));
    return;
  }

  const auto texture_tokens = CollectTextureHashTokens();
  if (texture_tokens.empty())
  {
    QMessageBox::warning(this, tr("Validation Error"),
                         tr("Add at least one texture hash."));
    return;
  }
  for (const std::string& token : texture_tokens)
  {
    for (char c : token)
    {
      if (!std::isxdigit(static_cast<unsigned char>(c)))
      {
        QMessageBox::warning(this, tr("Validation Error"),
                             tr("Texture hashes must contain only hexadecimal characters "
                                "(0-9, a-f)."));
        return;
      }
    }
    if (token.size() > 16)
    {
      QMessageBox::warning(this, tr("Validation Error"),
                           tr("Each texture hash must be at most 16 hex digits."));
      return;
    }
  }

  const auto handling = static_cast<HandlingType>(m_handling_combo->currentData().toInt());
  if (handling == HandlingType::UnitsPerMeter && m_units_per_meter_spin->value() <= 0.0)
  {
    QMessageBox::warning(this, tr("Validation Error"),
                         tr("Units per Meter must be greater than 0."));
    return;
  }

  accept();
}

void TextureElementOverrideAddEditDialog::OnHandlingChanged()
{
  const auto handling = static_cast<HandlingType>(m_handling_combo->currentData().toInt());
  const bool show_layer =
      (handling == HandlingType::Screen || handling == HandlingType::HeadLocked);
  const bool show_units_per_meter = (handling == HandlingType::UnitsPerMeter);

  m_layer_label->setVisible(show_layer);
  m_layer_spin->setVisible(show_layer);
  m_element_depth_label->setVisible(show_layer);
  m_element_depth_spin->setVisible(show_layer);
  m_units_per_meter_label->setVisible(show_units_per_meter);
  m_units_per_meter_spin->setVisible(show_units_per_meter);
}

void TextureElementOverrideAddEditDialog::OnClearEFBChanged()
{
  const bool show = m_clear_efb_check->isChecked();
  m_clear_efb_min_label->setVisible(show);
  m_clear_efb_min_spin->setVisible(show);
  m_clear_efb_max_label->setVisible(show);
  m_clear_efb_max_spin->setVisible(show);
}

void TextureElementOverrideAddEditDialog::ShowTextureBrowser()
{
  TextureHashBrowserConfig browser_config;
  browser_config.title = tr("Texture Hunter");
  browser_config.empty_info_text =
      tr("No textures captured yet.\n"
         "Start a game in VR so its textures can be enumerated.");
  browser_config.current_label = tr("currently loaded");
  browser_config.initial_selected_hashes = CollectTextureHashValues();
  browser_config.fetch_current_entries = []() {
    std::vector<TextureHashBrowserEntry> entries;
    for (const auto& tex : TextureElementManager::GetInstance().GetCurrentTextures())
      entries.push_back(TextureHashBrowserEntry{.hash = tex.hash, .name = tex.name});
    return entries;
  };
  browser_config.apply_selected_hashes = [this](const std::vector<u64>& hashes) {
    SetTextureHashFields(hashes);
  };

  // Enable global texture capture while the browser is open; restore on close.
  TextureElementManager::GetInstance().SetHunterActive(true);
  auto* dlg = ShowTextureHashBrowserDialog(this, browser_config);
  connect(dlg, &QObject::destroyed, this,
          []() { TextureElementManager::GetInstance().SetHunterActive(false); });
}

std::vector<std::string> TextureElementOverrideAddEditDialog::CollectTextureHashTokens() const
{
  std::vector<std::string> tokens;
  const QRegularExpression separators(QStringLiteral("[,;\\s]+"));
  const QRegularExpression hex_exact(QStringLiteral("^[0-9A-Fa-f]{1,16}$"));
  const QRegularExpression hex_16_anywhere(QStringLiteral("([0-9A-Fa-f]{16})"));
  for (QLineEdit* edit : m_texture_hash_edits)
  {
    if (edit == nullptr)
      continue;
    const auto parts = edit->text().trimmed().split(separators, Qt::SkipEmptyParts);
    for (const QString& part : parts)
    {
      if (hex_exact.match(part).hasMatch())
      {
        tokens.push_back(part.toLower().toStdString());
        continue;
      }

      const auto match = hex_16_anywhere.match(part);
      if (match.hasMatch())
      {
        tokens.push_back(match.captured(1).toLower().toStdString());
        continue;
      }

      tokens.push_back(part.toStdString());
    }
  }
  return tokens;
}

std::vector<u64> TextureElementOverrideAddEditDialog::CollectTextureHashValues() const
{
  std::vector<u64> hashes;
  for (const std::string& token : CollectTextureHashTokens())
  {
    if (token.empty() || token.size() > 16)
      continue;
    bool valid = true;
    for (char c : token)
    {
      if (!std::isxdigit(static_cast<unsigned char>(c)))
      {
        valid = false;
        break;
      }
    }
    if (!valid)
      continue;
    const u64 parsed = std::strtoull(token.c_str(), nullptr, 16);
    if (parsed != 0)
      hashes.push_back(parsed);
  }
  std::sort(hashes.begin(), hashes.end());
  hashes.erase(std::unique(hashes.begin(), hashes.end()), hashes.end());
  return hashes;
}

void TextureElementOverrideAddEditDialog::SetTextureHashFields(const std::vector<u64>& hashes)
{
  m_updating_texture_hash_fields = true;
  while (m_texture_hash_edits.size() < hashes.size())
    AddTextureHashField(QString());

  for (size_t i = 0; i < hashes.size(); i++)
  {
    m_texture_hash_edits[i]->setText(QString::fromStdString(fmt::format("{:016x}", hashes[i])));
  }
  for (size_t i = hashes.size(); i < m_texture_hash_edits.size(); i++)
    m_texture_hash_edits[i]->clear();

  m_updating_texture_hash_fields = false;
  EnsureTextureHashFieldRows();
}

void TextureElementOverrideAddEditDialog::AddTextureHashField(const QString& text)
{
  auto* edit = new QLineEdit;
  edit->setPlaceholderText(tr("Hex hash"));
  edit->setToolTip(
      tr("Texture hash to match.\n"
         "Enter one hash per line; a new line is added automatically.\n"
         "You can also paste multiple hashes separated by comma, semicolon, or spaces."));
  edit->setText(text);
  m_texture_hash_layout->addWidget(edit);
  m_texture_hash_edits.push_back(edit);

  connect(edit, &QLineEdit::textChanged, this, [this, edit](const QString&) {
    SanitizeTextureHashField(edit);
    EnsureTextureHashFieldRows();
  });
}

void TextureElementOverrideAddEditDialog::EnsureTextureHashFieldRows()
{
  if (m_updating_texture_hash_fields)
    return;

  m_updating_texture_hash_fields = true;

  int last_non_empty = -1;
  for (int i = 0; i < static_cast<int>(m_texture_hash_edits.size()); i++)
  {
    if (!m_texture_hash_edits[i]->text().trimmed().isEmpty())
      last_non_empty = i;
  }

  const int desired_rows = std::max(1, last_non_empty + 2);

  while (static_cast<int>(m_texture_hash_edits.size()) < desired_rows)
    AddTextureHashField(QString());

  while (static_cast<int>(m_texture_hash_edits.size()) > desired_rows)
  {
    QLineEdit* edit = m_texture_hash_edits.back();
    m_texture_hash_edits.pop_back();
    m_texture_hash_layout->removeWidget(edit);
    delete edit;
  }

  const int visible_rows = std::min(desired_rows, MAX_VISIBLE_TEXTURE_HASH_ROWS);
  const int viewport_height = visible_rows * TEXTURE_HASH_ROW_HEIGHT;
  m_texture_hash_scroll->setMinimumHeight(viewport_height);
  m_texture_hash_scroll->setMaximumHeight(viewport_height);

  m_updating_texture_hash_fields = false;
}

void TextureElementOverrideAddEditDialog::SanitizeTextureHashField(QLineEdit* edit)
{
  if (edit == nullptr || m_updating_texture_hash_fields)
    return;

  const QRegularExpression separators(QStringLiteral("[,;\\s]+"));
  const QRegularExpression hex_exact(QStringLiteral("^[0-9A-Fa-f]{1,16}$"));
  const QRegularExpression hex_16_anywhere(QStringLiteral("([0-9A-Fa-f]{16})"));

  const auto parts = edit->text().trimmed().split(separators, Qt::SkipEmptyParts);
  if (parts.isEmpty())
    return;

  QStringList sanitized_parts;
  bool changed = false;
  for (const QString& part : parts)
  {
    if (hex_exact.match(part).hasMatch())
    {
      const QString lowered = part.toLower();
      sanitized_parts.push_back(lowered);
      if (lowered != part)
        changed = true;
      continue;
    }

    const auto match = hex_16_anywhere.match(part);
    if (match.hasMatch())
    {
      sanitized_parts.push_back(match.captured(1).toLower());
      changed = true;
      continue;
    }

    sanitized_parts.push_back(part);
  }

  if (!changed)
    return;

  const QString new_text = sanitized_parts.join(QStringLiteral(" "));
  if (new_text == edit->text())
    return;

  const QSignalBlocker blocker(edit);
  edit->setText(new_text);
}
