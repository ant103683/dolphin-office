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
  QLineEdit* m_buttons_edit;
  QLineEdit* m_dpad_edit;
  QLineEdit* m_control_stick_edit;
  QLineEdit* m_c_stick_edit;
  QLineEdit* m_triggers_edit;

  QPushButton* m_save_button;
};