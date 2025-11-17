#include "DolphinQt/Config/Mapping/GCPadCustomPresetDialog.h"

#include <QFile>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QFrame>

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
  
  // Buttons (6 buttons)
  m_button_a_edit = new QLineEdit;
  m_button_b_edit = new QLineEdit;
  m_button_x_edit = new QLineEdit;
  m_button_y_edit = new QLineEdit;
  m_button_z_edit = new QLineEdit;
  m_button_start_edit = new QLineEdit;
  
  // D-Pad (4 directions)
  m_dpad_up_edit = new QLineEdit;
  m_dpad_down_edit = new QLineEdit;
  m_dpad_left_edit = new QLineEdit;
  m_dpad_right_edit = new QLineEdit;
  
  // Control Stick (5 inputs)
  m_control_stick_up_edit = new QLineEdit;
  m_control_stick_down_edit = new QLineEdit;
  m_control_stick_left_edit = new QLineEdit;
  m_control_stick_right_edit = new QLineEdit;
  m_control_stick_modifier_edit = new QLineEdit;
  
  // C Stick (5 inputs)
  m_c_stick_up_edit = new QLineEdit;
  m_c_stick_down_edit = new QLineEdit;
  m_c_stick_left_edit = new QLineEdit;
  m_c_stick_right_edit = new QLineEdit;
  m_c_stick_modifier_edit = new QLineEdit;
  
  // Triggers (4 triggers)
  m_trigger_l_analog_edit = new QLineEdit;
  m_trigger_r_analog_edit = new QLineEdit;
  m_trigger_l_edit = new QLineEdit;
  m_trigger_r_edit = new QLineEdit;

  m_save_button = new QPushButton(tr("Save"));
}

void GCPadCustomPresetDialog::CreateLayout()
{
  auto* layout = new QFormLayout;
  layout->addRow(tr("Game Title:"), m_title_edit);
  
  // 创建分隔线函数
  auto createSeparator = [this]() -> QFrame* {
    auto* line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    return line;
  };
  
  // 创建分区标题函数
  auto createSectionLabel = [this](const QString& text) -> QLabel* {
    auto* label = new QLabel(text);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
  };
  
  // Buttons section
  layout->addRow(createSeparator());
  layout->addRow(createSectionLabel(tr("Buttons")));
  layout->addRow(tr("A:"), m_button_a_edit);
  layout->addRow(tr("B:"), m_button_b_edit);
  layout->addRow(tr("X:"), m_button_x_edit);
  layout->addRow(tr("Y:"), m_button_y_edit);
  layout->addRow(tr("Z:"), m_button_z_edit);
  layout->addRow(tr("Start:"), m_button_start_edit);
  
  // D-Pad section
  layout->addRow(createSeparator());
  layout->addRow(createSectionLabel(tr("D-Pad")));
  layout->addRow(tr("Up:"), m_dpad_up_edit);
  layout->addRow(tr("Down:"), m_dpad_down_edit);
  layout->addRow(tr("Left:"), m_dpad_left_edit);
  layout->addRow(tr("Right:"), m_dpad_right_edit);
  
  // Control Stick section
  layout->addRow(createSeparator());
  layout->addRow(createSectionLabel(tr("Control Stick")));
  layout->addRow(tr("Up:"), m_control_stick_up_edit);
  layout->addRow(tr("Down:"), m_control_stick_down_edit);
  layout->addRow(tr("Left:"), m_control_stick_left_edit);
  layout->addRow(tr("Right:"), m_control_stick_right_edit);
  layout->addRow(tr("Modifier:"), m_control_stick_modifier_edit);
  
  // C Stick section
  layout->addRow(createSeparator());
  layout->addRow(createSectionLabel(tr("C Stick")));
  layout->addRow(tr("Up:"), m_c_stick_up_edit);
  layout->addRow(tr("Down:"), m_c_stick_down_edit);
  layout->addRow(tr("Left:"), m_c_stick_left_edit);
  layout->addRow(tr("Right:"), m_c_stick_right_edit);
  layout->addRow(tr("Modifier:"), m_c_stick_modifier_edit);
  
  // Triggers section
  layout->addRow(createSeparator());
  layout->addRow(createSectionLabel(tr("Triggers")));
  layout->addRow(tr("L Analog:"), m_trigger_l_analog_edit);
  layout->addRow(tr("R Analog:"), m_trigger_r_analog_edit);
  layout->addRow(tr("L:"), m_trigger_l_edit);
  layout->addRow(tr("R:"), m_trigger_r_edit);
  
  layout->addRow(createSeparator());
  layout->addWidget(m_save_button);

  setLayout(layout);
}

void GCPadCustomPresetDialog::Save()
{
  const std::string user_dir = File::GetUserPath(D_USER_IDX) + std::string("Profiles/");
  const std::string user_path = user_dir + std::string("GCPadPresets.json");
  File::CreateDir(user_dir);
  QFile file(QString::fromStdString(user_path));

  QJsonDocument doc;
  if (file.open(QIODevice::ReadOnly))
  {
    QByteArray fileData = file.readAll();
    file.close();
    if (!fileData.isEmpty())
    {
      doc = QJsonDocument::fromJson(fileData);
    }
  }

  // 如果文档为空或无效，创建默认结构
  if (doc.isNull() || !doc.isObject())
  {
    QJsonObject root;
    root[QStringLiteral("presets")] = QJsonArray();
    doc.setObject(root);
  }

  QJsonObject root = doc.object();
  QJsonArray presets = root[QStringLiteral("presets")].toArray();

  QJsonObject new_preset;
  // 使用默认值兜底
  new_preset[QStringLiteral("title")] = m_title_edit->text().isEmpty() ? QStringLiteral("Custom Preset") : m_title_edit->text();

  QJsonObject mappings;
  
  // Buttons array with default values
  QJsonArray buttons;
  buttons.append(m_button_a_edit->text().isEmpty() ? QStringLiteral("A") : m_button_a_edit->text());
  buttons.append(m_button_b_edit->text().isEmpty() ? QStringLiteral("B") : m_button_b_edit->text());
  buttons.append(m_button_x_edit->text().isEmpty() ? QStringLiteral("X") : m_button_x_edit->text());
  buttons.append(m_button_y_edit->text().isEmpty() ? QStringLiteral("Y") : m_button_y_edit->text());
  buttons.append(m_button_z_edit->text().isEmpty() ? QStringLiteral("Z") : m_button_z_edit->text());
  buttons.append(m_button_start_edit->text().isEmpty() ? QStringLiteral("Start") : m_button_start_edit->text());
  mappings[QStringLiteral("Buttons")] = buttons;
  
  // D-Pad array with default values
  QJsonArray dpad;
  dpad.append(m_dpad_up_edit->text().isEmpty() ? QStringLiteral("D-Up") : m_dpad_up_edit->text());
  dpad.append(m_dpad_down_edit->text().isEmpty() ? QStringLiteral("D-Down") : m_dpad_down_edit->text());
  dpad.append(m_dpad_left_edit->text().isEmpty() ? QStringLiteral("D-Left") : m_dpad_left_edit->text());
  dpad.append(m_dpad_right_edit->text().isEmpty() ? QStringLiteral("D-Right") : m_dpad_right_edit->text());
  mappings[QStringLiteral("D-Pad")] = dpad;
  
  // Control Stick array with default values
  QJsonArray control_stick;
  control_stick.append(m_control_stick_up_edit->text().isEmpty() ? QStringLiteral("M-Up") : m_control_stick_up_edit->text());
  control_stick.append(m_control_stick_down_edit->text().isEmpty() ? QStringLiteral("M-Down") : m_control_stick_down_edit->text());
  control_stick.append(m_control_stick_left_edit->text().isEmpty() ? QStringLiteral("M-Left") : m_control_stick_left_edit->text());
  control_stick.append(m_control_stick_right_edit->text().isEmpty() ? QStringLiteral("M-Right") : m_control_stick_right_edit->text());
  control_stick.append(m_control_stick_modifier_edit->text().isEmpty() ? QStringLiteral("M-Mod") : m_control_stick_modifier_edit->text());
  mappings[QStringLiteral("Main Stick")] = control_stick;
  
  // C Stick array with default values
  QJsonArray c_stick;
  c_stick.append(m_c_stick_up_edit->text().isEmpty() ? QStringLiteral("C-Up") : m_c_stick_up_edit->text());
  c_stick.append(m_c_stick_down_edit->text().isEmpty() ? QStringLiteral("C-Down") : m_c_stick_down_edit->text());
  c_stick.append(m_c_stick_left_edit->text().isEmpty() ? QStringLiteral("C-Left") : m_c_stick_left_edit->text());
  c_stick.append(m_c_stick_right_edit->text().isEmpty() ? QStringLiteral("C-Right") : m_c_stick_right_edit->text());
  c_stick.append(m_c_stick_modifier_edit->text().isEmpty() ? QStringLiteral("C-Mod") : m_c_stick_modifier_edit->text());
  mappings[QStringLiteral("C-Stick")] = c_stick;
  
  // Triggers array with default values
  QJsonArray triggers;
  triggers.append(m_trigger_l_analog_edit->text().isEmpty() ? QStringLiteral("L-An") : m_trigger_l_analog_edit->text());
  triggers.append(m_trigger_r_analog_edit->text().isEmpty() ? QStringLiteral("R-An") : m_trigger_r_analog_edit->text());
  triggers.append(m_trigger_l_edit->text().isEmpty() ? QStringLiteral("L-Tr") : m_trigger_l_edit->text());
  triggers.append(m_trigger_r_edit->text().isEmpty() ? QStringLiteral("R-Tr") : m_trigger_r_edit->text());
  mappings[QStringLiteral("Triggers")] = triggers;

  new_preset[QStringLiteral("mappings")] = mappings;

  presets.append(new_preset);
  root[QStringLiteral("presets")] = presets;

  // 安全地写入文件
  if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
  {
    file.write(QJsonDocument(root).toJson());
    file.close();
  }

  accept();
}
// SelectHashDialog
// #include "moc_GCPadCustomPresetDialog.cpp"
