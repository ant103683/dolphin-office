// Copyright 2015 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/Config/PlayerSelectionDialog.h"

#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "DolphinQt/QtUtils/ModalMessageBox.h"
#include "DolphinQt/Resources.h"

PlayerSelectionDialog::PlayerSelectionDialog(QWidget* parent) : QDialog(parent)
{
  setWindowTitle(tr("Select Player"));
  setWindowIcon(Resources::GetAppIcon());
  setModal(true);
  setFixedSize(400, 300);

  CreateLayout();
  ConnectWidgets();
}

void PlayerSelectionDialog::CreateLayout()
{
  m_main_layout = new QVBoxLayout(this);

  // setStyleSheet(QStringLiteral("PlayerSelectionDialog {"
  //                              "  background-image: url('d:/repo/dolphin-yihe/Data/Sys/Resources/wuFan.png');"
  //                              "  background-repeat: no-repeat;"
  //                              "  background-position: center;"
  //                              "}"
  //                              "QLabel {"
  //                              "  background: transparent;"
  //                              "}"
  //                              "QPushButton {"
  //                              "  background: rgba(255, 255, 255, 200);"
  //                              "  border: 1px solid #ccc;"
  //                              "  border-radius: 5px;"
  //                              "  padding: 8px;"
  //                              "}"));


  // Title label
  m_title_label = new QLabel(tr("Player Controller Setup"));
  m_title_label->setStyleSheet(QStringLiteral("font-weight: bold; font-size: 14px;"));
  m_title_label->setAlignment(Qt::AlignCenter);

  // Description label
  m_description_label = new QLabel(tr("Select the player controller you want to configure.If you need more advanced button configuration, go to the main menu, click 'Settings', then find 'Controllers' to access the full configuration options."));
  m_description_label->setAlignment(Qt::AlignCenter);
  m_description_label->setWordWrap(true);

  // Button layout (2x2 grid)
  m_button_layout = new QGridLayout();
  m_button_layout->setSpacing(15);

  // Create player buttons
  for (int i = 0; i < MAX_PLAYERS; ++i)
  {
    SetupPlayerButton(i);
  }

  // Cancel button
  m_cancel_button = new QPushButton(tr("Cancel"));
  m_cancel_button->setMinimumHeight(35);

  // Add widgets to main layout
  m_main_layout->addWidget(m_title_label);
  m_main_layout->addSpacing(10);
  m_main_layout->addWidget(m_description_label);
  m_main_layout->addSpacing(20);
  m_main_layout->addLayout(m_button_layout);
  m_main_layout->addSpacing(20);
  m_main_layout->addWidget(m_cancel_button);
  m_main_layout->addStretch();

  setLayout(m_main_layout);
}

void PlayerSelectionDialog::SetupPlayerButton(int player_index)
{
  const QString button_text = tr("Player %1").arg(player_index + 1);
  m_player_buttons[player_index] = new QPushButton(button_text);

  // Set button properties
  m_player_buttons[player_index]->setMinimumSize(120, 60);
  m_player_buttons[player_index]->setStyleSheet(QStringLiteral("QPushButton {"
                                                               "  font-size: 16px;"
                                                               "  font-weight: bold;"
                                                               "  border: 2px solid #3498db;"
                                                               "  border-radius: 8px;"
                                                               "  background-color: #ecf0f1;"
                                                               "  color: #2c3e50;"
                                                               "}"
                                                               "QPushButton:hover {"
                                                               "  background-color: #3498db;"
                                                               "  color: white;"
                                                               "}"
                                                               "QPushButton:pressed {"
                                                               "  background-color: #2980b9;"
                                                               "}"));

  // Store player index as property for easy retrieval
  m_player_buttons[player_index]->setProperty("player_index", player_index);

  // Add to grid layout (2x2)
  const int row = player_index / 2;
  const int col = player_index % 2;
  m_button_layout->addWidget(m_player_buttons[player_index], row, col);
}

void PlayerSelectionDialog::ConnectWidgets()
{
  // Connect player buttons
  for (int i = 0; i < MAX_PLAYERS; ++i)
  {
    connect(m_player_buttons[i], &QPushButton::clicked, this,
            &PlayerSelectionDialog::OnPlayerButtonClicked);
  }

  // Connect cancel button
  connect(m_cancel_button, &QPushButton::clicked, this, &QDialog::reject);
}

void PlayerSelectionDialog::OnPlayerButtonClicked()
{
  auto* button = qobject_cast<QPushButton*>(sender());
  if (!button)
    return;

  const int player_index = button->property("player_index").toInt();

  // Emit signal with player port (0-based index)
  emit PlayerSelected(player_index);

  // Close dialog with accept
  accept();
}


// #include "moc_PlayerSelectionDialog.cpp"