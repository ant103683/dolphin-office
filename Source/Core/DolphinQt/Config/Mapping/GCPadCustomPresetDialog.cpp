#include "DolphinQt/Config/Mapping/GCPadCustomPresetDialog.h"

#include <QFile>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QPushButton>

#include "Common/FileUtil.h"

GCPadCustomPresetDialog::GCPadCustomPresetDialog(QWidget* parent)
    : QDialog(parent)
{
  setWindowTitle(tr("Custom GameCube Controller Preset"));
  CreateWidgets();
  CreateLayout();

  connect(m_save_button, &QPushButton::clicked, this, &GCPadCustomPresetDialog::Save);
}

void GCPadCustomPresetDialog::CreateWidgets()
{
  m_title_edit = new QLineEdit;
  m_buttons_edit = new QLineEdit;
  m_dpad_edit = new QLineEdit;
  m_control_stick_edit = new QLineEdit;
  m_c_stick_edit = new QLineEdit;
  m_triggers_edit = new QLineEdit;

  m_save_button = new QPushButton(tr("Save"));
}

void GCPadCustomPresetDialog::CreateLayout()
{
  auto* layout = new QFormLayout;
  layout->addRow(tr("Game Title:"), m_title_edit);
  layout->addRow(tr("Buttons:"), m_buttons_edit);
  layout->addRow(tr("D-Pad:"), m_dpad_edit);
  layout->addRow(tr("Control Stick:"), m_control_stick_edit);
  layout->addRow(tr("C Stick:"), m_c_stick_edit);
  layout->addRow(tr("Triggers:"), m_triggers_edit);
  layout->addWidget(m_save_button);

  setLayout(layout);
}

void GCPadCustomPresetDialog::Save()
{
  std::string path = File::GetSysDirectory() + "/Profiles/GCPadPresets.json";
  QFile file(QString::fromStdString(path));

  QJsonDocument doc;
  if (file.open(QIODevice::ReadWrite))
  {
    doc = QJsonDocument::fromJson(file.readAll());
  }

  QJsonObject root = doc.object();
  QJsonArray presets = root[QStringLiteral("presets")].toArray();

  QJsonObject new_preset;
  new_preset[QStringLiteral("title")] = m_title_edit->text();

  QJsonObject mappings;
  mappings[QStringLiteral("Buttons")] = QJsonArray::fromStringList(m_buttons_edit->text().split(u','));
  mappings[QStringLiteral("D-Pad")] = QJsonArray::fromStringList(m_dpad_edit->text().split(u','));
  mappings[QStringLiteral("Control Stick")] =
      QJsonArray::fromStringList(m_control_stick_edit->text().split(u','));
  mappings[QStringLiteral("C Stick")] = QJsonArray::fromStringList(m_c_stick_edit->text().split(u','));
  mappings[QStringLiteral("Triggers")] =
      QJsonArray::fromStringList(m_triggers_edit->text().split(u','));

  new_preset[QStringLiteral("mappings")] = mappings;

  presets.append(new_preset);
  root[QStringLiteral("presets")] = presets;

  file.resize(0);
  file.write(QJsonDocument(root).toJson());
  file.close();

  accept();
}

#include "moc_GCPadCustomPresetDialog.cpp"