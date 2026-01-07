#pragma once

#include <QDialog>

class QLineEdit;
class QPushButton;

class GCPadCustomPresetDialog : public QDialog
{
  Q_OBJECT
public:
  explicit GCPadCustomPresetDialog(QWidget* parent = nullptr);

private slots:
  void Save();

private:
  void CreateWidgets();
  void CreateLayout();

  QLineEdit* m_title_edit;
  
  // Buttons (6 buttons)
  QLineEdit* m_button_a_edit;
  QLineEdit* m_button_b_edit;
  QLineEdit* m_button_x_edit;
  QLineEdit* m_button_y_edit;
  QLineEdit* m_button_z_edit;
  QLineEdit* m_button_start_edit;
  
  // D-Pad (4 directions)
  QLineEdit* m_dpad_up_edit;
  QLineEdit* m_dpad_down_edit;
  QLineEdit* m_dpad_left_edit;
  QLineEdit* m_dpad_right_edit;
  
  // Control Stick (5 inputs)
  QLineEdit* m_control_stick_up_edit;
  QLineEdit* m_control_stick_down_edit;
  QLineEdit* m_control_stick_left_edit;
  QLineEdit* m_control_stick_right_edit;
  QLineEdit* m_control_stick_modifier_edit;
  
  // C Stick (5 inputs)
  QLineEdit* m_c_stick_up_edit;
  QLineEdit* m_c_stick_down_edit;
  QLineEdit* m_c_stick_left_edit;
  QLineEdit* m_c_stick_right_edit;
  QLineEdit* m_c_stick_modifier_edit;
  
  // Triggers (4 triggers)
  QLineEdit* m_trigger_l_analog_edit;
  QLineEdit* m_trigger_r_analog_edit;
  QLineEdit* m_trigger_l_edit;
  QLineEdit* m_trigger_r_edit;

  QPushButton* m_save_button;
};