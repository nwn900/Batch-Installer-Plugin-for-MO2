/*
Copyright (C) 2026 MO2 Batch Download Installer contributors.

This file is part of the MO2 Batch Download Installer.

MO2 Batch Download Installer is free software: you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.
*/

#ifndef BATCHPROGRESSPANEL_H
#define BATCHPROGRESSPANEL_H

#include "uibase/ibatchinstallobserver.h"
#include "uibase/batchinstalltypes.h"

#include <QWidget>

class QLabel;
class QPushButton;

// Plan 13.6: one non-modal progress panel.
//
// Shows the current item position, the current archive name, the completed
// count, the pending count, and the current item result. Offers "Cancel
// after current item". Cancellation applies between items only; the plugin
// never cancels an archive extraction itself.
class BatchProgressPanel : public QWidget, public IBatchInstallObserver {
  Q_OBJECT

public:
  explicit BatchProgressPanel(QWidget* parent = nullptr);

  // IBatchInstallObserver
  void onBatchStarted(int itemCount) override;
  void onItemStarted(int index, const BatchDownloadRef& item) override;
  void onItemFinished(int index, const BatchInstallItemResult& result) override;
  bool cancelRequested() const override;
  void markFinished(const QString& text);

private:
  QLabel* m_position = nullptr;
  QLabel* m_name = nullptr;
  QLabel* m_counts = nullptr;
  QLabel* m_lastResult = nullptr;
  QPushButton* m_cancelButton = nullptr;

  int m_total = 0;
  int m_completed = 0;
  bool m_cancel = false;
  bool m_finished = false;
};

#endif  // BATCHPROGRESSPANEL_H
