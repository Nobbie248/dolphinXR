// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/Config/HideObjectAddEditDialog.h"

#include <algorithm>
#include <vector>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

#include "VideoCommon/HideObjectEngine.h"

namespace
{
QString StripHexPrefix(QString text)
{
  text = text.trimmed();
  if (text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
    text.remove(0, 2);
  return text;
}

QString FormatEntryValue(const HideObjectEngine::HideObjectEntry& entry)
{
  const int char_len = HideObjectEngine::GetByteCount(entry.type) * 2;

  if (char_len <= 16)
  {
    return QStringLiteral("%1")
        .arg(entry.value_lower, char_len, 16, QLatin1Char('0'))
        .toUpper();
  }

  const int upper_chars = char_len - 16;
  return QStringLiteral("%1%2")
      .arg(entry.value_upper, upper_chars, 16, QLatin1Char('0'))
      .arg(entry.value_lower, 16, 16, QLatin1Char('0'))
      .toUpper();
}

bool TryParseEntryValue(const QString& text, HideObjectEngine::HideObjectType type,
                        HideObjectEngine::HideObjectEntry* entry)
{
  const QString value = StripHexPrefix(text);
  const int expected_chars = HideObjectEngine::GetByteCount(type) * 2;
  if (value.isEmpty() || value.length() > expected_chars)
    return false;

  for (const QChar c : value)
  {
    const ushort ch = c.toUpper().unicode();
    if (!((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F')))
      return false;
  }

  HideObjectEngine::HideObjectEntry parsed;
  parsed.type = type;

  bool ok = false;
  if (expected_chars <= 16)
  {
    parsed.value_lower = value.toULongLong(&ok, 16);
    parsed.value_upper = 0;
  }
  else
  {
    if (value.length() > 16)
    {
      const QString upper_str = value.left(value.length() - 16);
      const QString lower_str = value.right(16);
      parsed.value_upper = upper_str.toULongLong(&ok, 16);
      if (ok)
        parsed.value_lower = lower_str.toULongLong(&ok, 16);
    }
    else
    {
      parsed.value_upper = 0;
      parsed.value_lower = value.toULongLong(&ok, 16);
    }
  }

  if (!ok)
    return false;

  *entry = parsed;
  return true;
}

bool EntryLess(const HideObjectEngine::HideObjectEntry& lhs,
               const HideObjectEngine::HideObjectEntry& rhs)
{
  if (lhs.value_upper != rhs.value_upper)
    return lhs.value_upper < rhs.value_upper;
  return lhs.value_lower < rhs.value_lower;
}

std::vector<u8> EntryToBytes(const HideObjectEngine::HideObjectEntry& entry)
{
  const int byte_count = HideObjectEngine::GetByteCount(entry.type);
  std::vector<u8> bytes;
  bytes.reserve(static_cast<size_t>(byte_count));

  if (byte_count > 8)
  {
    const int upper_bytes = byte_count - 8;
    for (int j = upper_bytes; j > 0; --j)
      bytes.push_back(static_cast<u8>((entry.value_upper >> ((j - 1) * 8)) & 0xFF));
    for (int j = 8; j > 0; --j)
      bytes.push_back(static_cast<u8>((entry.value_lower >> ((j - 1) * 8)) & 0xFF));
  }
  else
  {
    for (int j = byte_count; j > 0; --j)
      bytes.push_back(static_cast<u8>((entry.value_lower >> ((j - 1) * 8)) & 0xFF));
  }

  return bytes;
}

HideObjectEngine::HideObjectEntry EntryFromBytes(HideObjectEngine::HideObjectType type,
                                                 const std::vector<u8>& bytes)
{
  HideObjectEngine::HideObjectEntry entry;
  entry.type = type;

  const int byte_count = static_cast<int>(bytes.size());
  if (byte_count > 8)
  {
    const int upper_bytes = byte_count - 8;
    for (int i = 0; i < upper_bytes; ++i)
      entry.value_upper = (entry.value_upper << 8) | bytes[i];
    for (int i = upper_bytes; i < byte_count; ++i)
      entry.value_lower = (entry.value_lower << 8) | bytes[i];
  }
  else
  {
    for (const u8 byte : bytes)
      entry.value_lower = (entry.value_lower << 8) | byte;
  }

  return entry;
}

int GetRangeSliderMaximum(HideObjectEngine::HideObjectType type)
{
  const int slider_bytes = std::min(HideObjectEngine::GetByteCount(type), 3);
  return (1 << (slider_bytes * 8)) - 1;
}

int RangeSliderValueFromEntry(const HideObjectEngine::HideObjectEntry& entry)
{
  const std::vector<u8> bytes = EntryToBytes(entry);
  const size_t slider_bytes = std::min<size_t>(bytes.size(), 3);
  const size_t start = bytes.size() - slider_bytes;

  int value = 0;
  for (size_t i = 0; i < slider_bytes; ++i)
    value = (value << 8) | bytes[start + i];
  return value;
}

HideObjectEngine::HideObjectEntry EntryFromRangeSliderValue(HideObjectEngine::HideObjectType type,
                                                            int slider_value,
                                                            const HideObjectEngine::HideObjectEntry& base)
{
  const int byte_count = HideObjectEngine::GetByteCount(type);
  const int slider_bytes = std::min(byte_count, 3);
  std::vector<u8> bytes = EntryToBytes(base);
  if (static_cast<int>(bytes.size()) != byte_count)
    bytes.assign(static_cast<size_t>(byte_count), 0);

  const int start = byte_count - slider_bytes;
  for (int i = 0; i < slider_bytes; ++i)
  {
    const int shift = (slider_bytes - i - 1) * 8;
    bytes[static_cast<size_t>(start + i)] = static_cast<u8>((slider_value >> shift) & 0xFF);
  }

  return EntryFromBytes(type, bytes);
}

HideObjectEngine::HideObjectEntry MakeFilledEntry(HideObjectEngine::HideObjectType type,
                                                  u8 fill_byte)
{
  return EntryFromBytes(type, std::vector<u8>(HideObjectEngine::GetByteCount(type), fill_byte));
}

HideObjectEngine::HideObjectEntry ResizeEntryForType(const HideObjectEngine::HideObjectEntry& entry,
                                                     HideObjectEngine::HideObjectType new_type,
                                                     u8 expand_fill_byte)
{
  std::vector<u8> bytes = EntryToBytes(entry);
  const size_t new_size = static_cast<size_t>(HideObjectEngine::GetByteCount(new_type));

  if (bytes.size() < new_size)
    bytes.resize(new_size, expand_fill_byte);
  else if (bytes.size() > new_size)
    bytes.resize(new_size);

  return EntryFromBytes(new_type, bytes);
}

}  // namespace

HideObjectAddEditDialog::HideObjectAddEditDialog(
    QWidget* parent, const HideObjectEngine::HideObject* existing_code,
    const std::vector<HideObjectEngine::HideObject>& all_codes)
    : QDialog(parent), m_all_codes(all_codes)
{
  if (existing_code)
  {
    m_is_edit = true;
    m_original_name = existing_code->name;
    m_result = *existing_code;
    if (!m_result.entries.empty())
      m_current_entry = m_result.entries[0];

    for (size_t i = 0; i < m_all_codes.size(); ++i)
    {
      if (&m_all_codes[i] == existing_code)
      {
        m_existing_code_index = i;
        break;
      }
    }
  }
  else
  {
    m_current_entry.type = HideObjectEngine::HideObjectType::Bits8;
    m_current_entry.value_upper = 0;
    m_current_entry.value_lower = 0;
  }

  setWindowTitle(m_is_edit ? tr("Edit Hide Object Code") : tr("Add Hide Object Code"));
  setMinimumWidth(400);

  CreateWidgets();
  ConnectWidgets();
  UpdateValueDisplay();
}

void HideObjectAddEditDialog::CreateWidgets()
{
  auto* name_label = new QLabel(tr("Name:"));
  m_name_edit = new QLineEdit;
  if (m_is_edit)
    m_name_edit->setText(QString::fromStdString(m_result.name));
  else
    m_name_edit->setPlaceholderText(tr("Enter code name..."));

  auto* type_label = new QLabel(tr("Size:"));
  m_type_combo = new QComboBox;
  for (int i = 0; i < static_cast<int>(HideObjectEngine::HideObjectType::Count); i++)
    m_type_combo->addItem(QString::fromLatin1(
        HideObjectEngine::GetTypeName(static_cast<HideObjectEngine::HideObjectType>(i))));
  m_type_combo->setCurrentIndex(static_cast<int>(m_current_entry.type));

  auto* value_label = new QLabel(tr("Value (hex):"));
  m_value_edit = new QLineEdit;
  m_value_edit->setFont(QFont(QStringLiteral("Courier New"), 10));

  m_up_button = new QPushButton(tr("Up"));
  m_down_button = new QPushButton(tr("Down"));
  m_range_finder_toggle = new QCheckBox(tr("Range Finder"));
  m_range_label = new QLabel;
  m_range_lower_edit = new QLineEdit;
  m_range_upper_edit = new QLineEdit;
  m_range_lower_slider = new QSlider(Qt::Horizontal);
  m_range_upper_slider = new QSlider(Qt::Horizontal);
  m_range_lower_edit->setFont(QFont(QStringLiteral("Courier New"), 10));
  m_range_upper_edit->setFont(QFont(QStringLiteral("Courier New"), 10));

  const QString range_tooltip =
      tr("Sliders are exact through 24 bits. For larger sizes, sliders adjust the last 24 bits. "
         "Use the text boxes for exact full-width bounds.");
  m_range_lower_edit->setToolTip(range_tooltip);
  m_range_upper_edit->setToolTip(range_tooltip);
  m_range_lower_slider->setToolTip(range_tooltip);
  m_range_upper_slider->setToolTip(range_tooltip);

  const QString tooltip =
      tr("The Up/Down buttons can be used to find new codes.\n"
         "While the game is playing, find an object you want to hide and select '8bits'.\n"
         "Keep clicking Up until the object disappears.\n"
         "Now choose '16bits' and continue clicking Up until the object is hidden again.\n"
         "Repeat until the code is long enough to be unique.\n"
         "Warning: Too short of a code may hide other objects too.");
  m_up_button->setToolTip(tooltip);
  m_down_button->setToolTip(tooltip);

  auto* button_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

  auto* grid = new QGridLayout;
  grid->addWidget(name_label, 0, 0);
  grid->addWidget(m_name_edit, 0, 1, 1, 3);
  grid->addWidget(type_label, 1, 0);
  grid->addWidget(m_type_combo, 1, 1, 1, 3);
  grid->addWidget(value_label, 2, 0);
  grid->addWidget(m_value_edit, 2, 1);
  grid->addWidget(m_up_button, 2, 2);
  grid->addWidget(m_down_button, 2, 3);
  grid->addWidget(m_range_finder_toggle, 3, 0, 1, 4);
  grid->addWidget(m_range_label, 4, 0, 1, 4);
  grid->addWidget(new QLabel(tr("Lower Bound:")), 5, 0);
  grid->addWidget(m_range_lower_edit, 5, 1, 1, 3);
  grid->addWidget(m_range_lower_slider, 6, 1, 1, 3);
  grid->addWidget(new QLabel(tr("Upper Bound:")), 7, 0);
  grid->addWidget(m_range_upper_edit, 7, 1, 1, 3);
  grid->addWidget(m_range_upper_slider, 8, 1, 1, 3);

  m_range_label->setEnabled(false);
  m_range_lower_edit->setEnabled(false);
  m_range_upper_edit->setEnabled(false);
  m_range_lower_slider->setEnabled(false);
  m_range_upper_slider->setEnabled(false);
  ResetRangeBoundsForCurrentType();

  auto* layout = new QVBoxLayout{this};
  layout->addLayout(grid);
  layout->addWidget(button_box);

  connect(button_box, &QDialogButtonBox::accepted, this, &HideObjectAddEditDialog::OnAccept);
  connect(button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void HideObjectAddEditDialog::ConnectWidgets()
{
  connect(m_type_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &HideObjectAddEditDialog::OnTypeChanged);
  connect(m_up_button, &QPushButton::clicked, this, &HideObjectAddEditDialog::OnUpClicked);
  connect(m_down_button, &QPushButton::clicked, this, &HideObjectAddEditDialog::OnDownClicked);
  connect(m_range_finder_toggle, &QCheckBox::toggled, this,
          &HideObjectAddEditDialog::OnRangeFinderToggled);
  connect(m_range_lower_edit, &QLineEdit::textChanged, this,
          &HideObjectAddEditDialog::OnRangeBoundsChanged);
  connect(m_range_upper_edit, &QLineEdit::textChanged, this,
          &HideObjectAddEditDialog::OnRangeBoundsChanged);
  connect(m_range_lower_slider, &QSlider::valueChanged, this,
          &HideObjectAddEditDialog::OnRangeSliderChanged);
  connect(m_range_upper_slider, &QSlider::valueChanged, this,
          &HideObjectAddEditDialog::OnRangeSliderChanged);
}

void HideObjectAddEditDialog::UpdateValueDisplay()
{
  m_value_edit->setText(FormatEntryValue(m_current_entry));
}

bool HideObjectAddEditDialog::ParseValueFromUI()
{
  const QString text = m_value_edit->text().trimmed();
  if (text.isEmpty())
  {
    QMessageBox::warning(this, tr("Error"), tr("Value cannot be empty."));
    return false;
  }

  HideObjectEngine::HideObjectEntry parsed;
  if (!TryParseEntryValue(text, m_current_entry.type, &parsed))
  {
    QMessageBox::warning(this, tr("Error"),
                         tr("Invalid hex value. Use characters 0-9 and A-F only, with no more "
                            "digits than the selected size."));
    return false;
  }

  m_current_entry = parsed;
  return true;
}

void HideObjectAddEditDialog::OnTypeChanged()
{
  HideObjectEngine::HideObjectEntry previous_range_lower;
  HideObjectEngine::HideObjectEntry previous_range_upper;
  const bool had_valid_range = ParseRangeBounds(&previous_range_lower, &previous_range_upper);

  if (!ParseValueFromUI())
    return;

  const auto new_type = static_cast<HideObjectEngine::HideObjectType>(m_type_combo->currentIndex());
  m_current_entry = ResizeEntryForType(m_current_entry, new_type, 0x00);

  UpdateValueDisplay();

  if (had_valid_range)
  {
    HideObjectEngine::HideObjectEntry new_lower =
        ResizeEntryForType(previous_range_lower, new_type, 0x00);
    HideObjectEngine::HideObjectEntry new_upper =
        ResizeEntryForType(previous_range_upper, new_type, 0xFF);
    if (EntryLess(new_upper, new_lower))
      new_upper = new_lower;

    SetRangeBoundsForCurrentType(new_lower, new_upper);
  }
  else
  {
    ResetRangeBoundsForCurrentType();
  }

  ApplyTemporarily();
}

void HideObjectAddEditDialog::OnUpClicked()
{
  if (!ParseValueFromUI())
    return;

  // Brute-force only the last byte (two hex chars), without carrying into higher bytes.
  const u8 low_byte = static_cast<u8>(m_current_entry.value_lower & 0xFFULL);
  const u8 next_low_byte = static_cast<u8>(low_byte + 1);
  m_current_entry.value_lower = (m_current_entry.value_lower & ~0xFFULL) | next_low_byte;

  UpdateValueDisplay();
  ApplyTemporarily();
}

void HideObjectAddEditDialog::OnDownClicked()
{
  if (!ParseValueFromUI())
    return;

  // Brute-force only the last byte (two hex chars), without borrowing from higher bytes.
  const u8 low_byte = static_cast<u8>(m_current_entry.value_lower & 0xFFULL);
  const u8 prev_low_byte = static_cast<u8>(low_byte - 1);
  m_current_entry.value_lower = (m_current_entry.value_lower & ~0xFFULL) | prev_low_byte;

  UpdateValueDisplay();
  ApplyTemporarily();
}

void HideObjectAddEditDialog::OnRangeFinderToggled(bool enabled)
{
  if (enabled)
  {
    HideObjectEngine::HideObjectEntry current_entry;
    if (TryParseEntryValue(m_value_edit->text(), m_current_entry.type, &current_entry))
    {
      m_current_entry = current_entry;
      {
        const QSignalBlocker blocker(m_range_lower_edit);
        m_range_lower_edit->setText(FormatEntryValue(m_current_entry));
      }
      ClampRangeBounds(true);
      UpdateRangeSlidersFromText();
    }
  }

  m_range_label->setEnabled(enabled);
  m_range_lower_edit->setEnabled(enabled);
  m_range_upper_edit->setEnabled(enabled);
  m_range_lower_slider->setEnabled(enabled);
  m_range_upper_slider->setEnabled(enabled);
  UpdateRangeLabel();
  ApplyTemporarily();
}

void HideObjectAddEditDialog::OnRangeBoundsChanged()
{
  if (sender() == m_range_lower_edit || sender() == m_range_upper_edit)
    ClampRangeBounds(sender() == m_range_lower_edit);

  UpdateRangeLabel();
  UpdateRangeSlidersFromText();
  if (!m_range_finder_toggle->isChecked())
    return;

  ApplyTemporarily();
}

void HideObjectAddEditDialog::OnRangeSliderChanged()
{
  const bool lower_changed = sender() == m_range_lower_slider;
  QLineEdit* const edit = lower_changed ? m_range_lower_edit : m_range_upper_edit;
  QSlider* const slider = lower_changed ? m_range_lower_slider : m_range_upper_slider;

  HideObjectEngine::HideObjectEntry base;
  if (!TryParseEntryValue(edit->text(), m_current_entry.type, &base))
    return;

  {
    const QSignalBlocker blocker(edit);
    edit->setText(
        FormatEntryValue(EntryFromRangeSliderValue(m_current_entry.type, slider->value(), base)));
  }

  ClampRangeBounds(lower_changed);
  UpdateRangeSlidersFromText();
  UpdateRangeLabel();
  if (m_range_finder_toggle->isChecked())
    ApplyTemporarily();
}

void HideObjectAddEditDialog::UpdateRangeLabel()
{
  HideObjectEngine::HideObjectEntry lower;
  HideObjectEngine::HideObjectEntry upper;
  if (!ParseRangeBounds(&lower, &upper))
  {
    m_range_label->setText(tr("Invalid range"));
    return;
  }

  m_range_label->clear();
}

void HideObjectAddEditDialog::UpdateRangeSlidersFromText()
{
  HideObjectEngine::HideObjectEntry lower;
  HideObjectEngine::HideObjectEntry upper;

  if (TryParseEntryValue(m_range_lower_edit->text(), m_current_entry.type, &lower))
  {
    const QSignalBlocker blocker(m_range_lower_slider);
    m_range_lower_slider->setValue(RangeSliderValueFromEntry(lower));
  }

  if (TryParseEntryValue(m_range_upper_edit->text(), m_current_entry.type, &upper))
  {
    const QSignalBlocker blocker(m_range_upper_slider);
    m_range_upper_slider->setValue(RangeSliderValueFromEntry(upper));
  }
}

bool HideObjectAddEditDialog::ClampRangeBounds(bool lower_changed)
{
  HideObjectEngine::HideObjectEntry lower;
  HideObjectEngine::HideObjectEntry upper;
  if (!TryParseEntryValue(m_range_lower_edit->text(), m_current_entry.type, &lower) ||
      !TryParseEntryValue(m_range_upper_edit->text(), m_current_entry.type, &upper))
  {
    return false;
  }

  if (!EntryLess(upper, lower))
    return true;

  QLineEdit* const edit_to_update = lower_changed ? m_range_upper_edit : m_range_lower_edit;
  const HideObjectEngine::HideObjectEntry& value_to_copy = lower_changed ? lower : upper;
  const QSignalBlocker blocker(edit_to_update);
  edit_to_update->setText(FormatEntryValue(value_to_copy));
  return true;
}

void HideObjectAddEditDialog::SetRangeBoundsForCurrentType(
    const HideObjectEngine::HideObjectEntry& lower, const HideObjectEngine::HideObjectEntry& upper)
{
  const int char_len = HideObjectEngine::GetByteCount(m_current_entry.type) * 2;
  const QSignalBlocker lower_blocker(m_range_lower_edit);
  const QSignalBlocker upper_blocker(m_range_upper_edit);
  const QSignalBlocker lower_slider_blocker(m_range_lower_slider);
  const QSignalBlocker upper_slider_blocker(m_range_upper_slider);
  const int slider_max = GetRangeSliderMaximum(m_current_entry.type);

  m_range_lower_edit->setMaxLength(char_len + 2);
  m_range_upper_edit->setMaxLength(char_len + 2);
  m_range_lower_edit->setText(FormatEntryValue(lower));
  m_range_upper_edit->setText(FormatEntryValue(upper));
  m_range_lower_slider->setRange(0, slider_max);
  m_range_upper_slider->setRange(0, slider_max);
  m_range_lower_slider->setValue(RangeSliderValueFromEntry(lower));
  m_range_upper_slider->setValue(RangeSliderValueFromEntry(upper));

  UpdateRangeLabel();
}

void HideObjectAddEditDialog::ResetRangeBoundsForCurrentType()
{
  SetRangeBoundsForCurrentType(MakeFilledEntry(m_current_entry.type, 0x00),
                               MakeFilledEntry(m_current_entry.type, 0xFF));
}

bool HideObjectAddEditDialog::ParseRangeBounds(HideObjectEngine::HideObjectEntry* lower,
                                               HideObjectEngine::HideObjectEntry* upper) const
{
  HideObjectEngine::HideObjectEntry parsed_lower;
  HideObjectEngine::HideObjectEntry parsed_upper;
  if (!TryParseEntryValue(m_range_lower_edit->text(), m_current_entry.type, &parsed_lower) ||
      !TryParseEntryValue(m_range_upper_edit->text(), m_current_entry.type, &parsed_upper))
  {
    return false;
  }

  if (EntryLess(parsed_upper, parsed_lower))
    return false;

  *lower = parsed_lower;
  *upper = parsed_upper;
  return true;
}

void HideObjectAddEditDialog::ApplyTemporarily()
{
  std::vector<HideObjectEngine::HideObject> temp_list;
  temp_list.reserve(m_all_codes.size() + 1);

  // Keep saved active codes applied while overlaying the search candidate.
  for (size_t i = 0; i < m_all_codes.size(); ++i)
  {
    if (m_existing_code_index && i == *m_existing_code_index)
      continue;

    temp_list.push_back(m_all_codes[i]);
  }

  if (m_range_finder_toggle->isChecked())
  {
    HideObjectEngine::HideObjectRange range;
    if (ParseRangeBounds(&range.lower, &range.upper))
    {
      HideObjectEngine::Engine::GetInstance().ApplyCodes(temp_list, {range});
      return;
    }

    HideObjectEngine::Engine::GetInstance().ApplyCodes(temp_list);
    return;
  }
  else
  {
    // Single-value brute force: add only the current temporary entry.
    HideObjectEngine::HideObject temp_code;
    temp_code.name = "temp_brute_force";
    temp_code.entries.push_back(m_current_entry);
    temp_code.active = true;
    temp_code.user_defined = false;
    temp_list.push_back(std::move(temp_code));
  }

  HideObjectEngine::Engine::GetInstance().ApplyCodes(temp_list);
}

void HideObjectAddEditDialog::OnAccept()
{
  const QString name = m_name_edit->text().trimmed();
  if (name.isEmpty())
  {
    QMessageBox::warning(this, tr("Error"), tr("Please enter a name for this code."));
    return;
  }

  // Check name uniqueness
  for (const auto& code : m_all_codes)
  {
    if (code.name == name.toStdString() && code.name != m_original_name)
    {
      QMessageBox::warning(this, tr("Error"),
                           tr("Name is already in use. Please choose a unique name."));
      return;
    }
  }

  if (!ParseValueFromUI())
    return;

  m_result.name = name.toStdString();
  m_result.entries.clear();
  m_result.entries.push_back(m_current_entry);
  m_result.active = true;
  m_result.user_defined = true;

  accept();
}
