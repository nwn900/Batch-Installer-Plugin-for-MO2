/*
Copyright (C) 2026 MO2 Batch Download Installer contributors.

This file is part of the MO2 Batch Download Installer.

MO2 Batch Download Installer is free software: you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.
*/

#include "batchprogresspanel.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

BatchProgressPanel::BatchProgressPanel(QWidget* parent) : QWidget(parent)
{
  setWindowTitle(tr("Batch install progress"));
  setAttribute(Qt::WA_DeleteOnClose, false);

  auto* layout = new QVBoxLayout(this);

  m_position = new QLabel(this);
  m_name = new QLabel(this);
  m_counts = new QLabel(this);
  m_lastResult = new QLabel(this);
  layout->addWidget(m_position);
  layout->addWidget(m_name);
  layout->addWidget(m_counts);
  layout->addWidget(m_lastResult);

  m_cancelButton = new QPushButton(tr("Cancel after current item"), this);
  layout->addWidget(m_cancelButton);

  connect(m_cancelButton, &QPushButton::clicked, this, [this] {
    if (m_finished) {
      close();
      return;
    }
    m_cancel = true;
    m_cancelButton->setEnabled(false);
  });
}

void BatchProgressPanel::markFinished(const QString& text)
{
  m_finished = true;
  m_lastResult->setText(text);
  m_cancelButton->setText(tr("Close"));
  m_cancelButton->setEnabled(true);
}

void BatchProgressPanel::onBatchStarted(int itemCount)
{
  m_total = itemCount;
  m_completed = 0;
  m_position->setText(tr("Position: 0 of %1").arg(m_total));
  m_counts->setText(tr("Pending: %1").arg(m_total));
}

void BatchProgressPanel::onItemStarted(int index, const BatchDownloadRef& item)
{
  m_position->setText(tr("Position: %1 of %2").arg(index + 1).arg(m_total));
  m_name->setText(tr("Archive: %1").arg(item.displayName));
  m_lastResult->clear();
}

void BatchProgressPanel::onItemFinished(int index,
                                        const BatchInstallItemResult& result)
{
  (void)index;
  ++m_completed;
  m_counts->setText(tr("Completed: %1 of %2, pending: %3")
                        .arg(m_completed)
                        .arg(m_total)
                        .arg(m_total - m_completed));

  QString state;
  switch (result.state) {
    case BatchInstallItemState::Installed:
      state = tr("Installed");
      break;
    case BatchInstallItemState::Updated:
      state = tr("Updated");
      break;
    case BatchInstallItemState::Skipped:
      state = tr("Skipped");
      break;
    case BatchInstallItemState::Canceled:
      state = tr("Canceled");
      break;
    case BatchInstallItemState::Failed:
      state = tr("Failed");
      break;
    case BatchInstallItemState::Dispatched:
      state = tr("Handed to MO2 installer");
      break;
    case BatchInstallItemState::SkippedStaleIndex:
      state = tr("Skipped (ambiguous download row)");
      break;
    case BatchInstallItemState::Invalid:
      state = tr("Invalid");
      break;
    default:
      Q_UNREACHABLE();
      break;
  }

  QString text = tr("Result: %1").arg(state);
  if (!result.errorText.isEmpty()) {
    text += tr(" - %1").arg(result.errorText);
  }
  m_lastResult->setText(text);
}

bool BatchProgressPanel::cancelRequested() const { return m_cancel; }
