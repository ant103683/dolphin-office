#include "SelectHashDialog.h"

#include <QDialogButtonBox>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>

namespace DolphinQt
{
SelectHashDialog::SelectHashDialog(const std::vector<std::string>& hashes, QWidget* parent)
    : QDialog(parent)
{
  setWindowTitle(tr("Select Save Version"));
  resize(300, 400);

  auto* layout = new QVBoxLayout(this);

  // Add explanatory label so users understand why they need to pick a version.
  auto* info_label = new QLabel(tr("This save file corresponds to multiple versions of the game. "
                                   "Please select the version you want to use with this save file. "
                                   "Each game version number can be viewed on the emulator's main interface."),
                               this);
  info_label->setWordWrap(true);
  layout->addWidget(info_label);

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
