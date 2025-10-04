#include "SelectHashDialog.h"

#include <QDialogButtonBox>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace DolphinQt
{
SelectHashDialog::SelectHashDialog(const std::vector<std::string>& hashes, QWidget* parent)
    : QDialog(parent)
{
  setWindowTitle(tr("Select Save Directory"));
  resize(300, 400);

  auto* layout = new QVBoxLayout(this);

  m_list = new QListWidget(this);
  for (const std::string& h : hashes)
    m_list->addItem(QString::fromStdString(h));
  if (!hashes.empty())
    m_list->setCurrentRow(0);
  layout->addWidget(m_list);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  m_ok = buttons->button(QDialogButtonBox::Ok);
  m_ok->setEnabled(!hashes.empty());

  connect(m_list, &QListWidget::currentRowChanged, this,
          [this](int row) { m_ok->setEnabled(row >= 0); });
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);
}

QString SelectHashDialog::GetSelectedHash() const
{
  const QListWidgetItem* item = m_list->currentItem();
  return item ? item->text() : QString();
}
}  // namespace DolphinQt
