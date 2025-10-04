#pragma once

#include <QDialog>
#include <QString>
#include <vector>

class QListWidget;
class QPushButton;

namespace DolphinQt
{
class SelectHashDialog : public QDialog
{
  Q_OBJECT
public:
  explicit SelectHashDialog(const std::vector<std::string>& hashes, QWidget* parent = nullptr);
  QString GetSelectedHash() const;

private:
  QListWidget* m_list;
  QPushButton* m_ok;
};
}  // namespace DolphinQt
