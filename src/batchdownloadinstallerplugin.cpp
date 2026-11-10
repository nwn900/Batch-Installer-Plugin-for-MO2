#include "batchdownloadinstallerplugin.h"

#include "batchselection.h"
#include "batchrefreshgate.h"

#include "qt_compat.h"

#include "batchprogresspanel.h"

#include <QAction>
#include <QAbstractItemView>
#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QMainWindow>
#include <QMenu>
#include <QItemSelectionModel>
#include <QMetaObject>
#include <QTimer>
#include <QWidget>

namespace {
QMainWindow* findMainWindow()
{
  QMainWindow* fallback = nullptr;
  for (QWidget* widget : QApplication::topLevelWidgets()) {
    auto* mw = qobject_cast<QMainWindow*>(widget);
    if (mw == nullptr) {
      continue;
    }
    // Prefer the window that actually owns the downloads view; MO2 can have
    // other top-level QMainWindows (dialogs) before the real one is created.
    if (mw->findChild<QAbstractItemView*>("downloadView") != nullptr) {
      return mw;
    }
    if (fallback == nullptr) {
      fallback = mw;
    }
  }
  return fallback;
}

}  // namespace

BatchDownloadInstallerPlugin::BatchDownloadInstallerPlugin() = default;

BatchDownloadInstallerPlugin::~BatchDownloadInstallerPlugin()
{
  auto& gate = batch::BatchRefreshGate::instance();
  if (m_organizer != nullptr && gate.installing()) {
    gate.setInstalling(false);
    m_organizer->refresh(false);
  }
  gate.shutdown();
}

bool BatchDownloadInstallerPlugin::init(MOBase::IOrganizer* organizer)
{
  m_organizer = organizer;

  connect(&batch::BatchRefreshGate::instance(),
          &batch::BatchRefreshGate::refreshWaitFinished, this,
          &BatchDownloadInstallerPlugin::onRefreshWaitFinished);
  connect(&batch::BatchRefreshGate::instance(),
          &batch::BatchRefreshGate::environmentChanged, this, [this]() {
            if (m_batchRunning) {
              qWarning() << "BatchDownloadInstaller: game changed; stopping after current item";
              m_stopAfterItem = true;
            }
          });

  if (organizer == nullptr) {
    return true;
  }

  // MO2 refreshes the directory structure during startup, before the main
  // window exists. The hook has to be armed here; setupUi is too late.
  batch::BatchRefreshGate::instance().initialize(organizer);

  organizer->onAboutToRun([](const QString& path) {
    if (batch::BatchRefreshGate::instance().busy()) {
      qWarning() << "BatchDownloadInstaller: blocked launch while the virtual directory is deferred"
                 << path;
      return false;
    }
    return true;
  });
  organizer->onProfileChanged([this](MOBase::IProfile*, MOBase::IProfile*) {
    if (m_batchRunning) {
      qWarning() << "BatchDownloadInstaller: profile changed; stopping after current item";
      m_stopAfterItem = true;
    }
  });

  // Plugin-only mode: always set up UI. Works on stock or combined core.
  organizer->onUserInterfaceInitialized(
      [this](QMainWindow* window) {
        qInfo() << "BatchDownloadInstaller: UI initialized, window=" << window;
        if (window == nullptr) {
          window = findMainWindow();
        }
        setupUi(window);
      });

  // Do not depend solely on the callback above: poll until the main window and
  // its downloadView exist. This also covers MO2 builds that never emit the
  // callback for this plugin.
  auto* uiPoll = new QTimer(this);
  uiPoll->setInterval(250);
  connect(uiPoll, &QTimer::timeout, this, [this, uiPoll]() {
    setupUi(findMainWindow());
    if (m_action != nullptr) {
      uiPoll->stop();
      uiPoll->deleteLater();
    }
  });
  uiPoll->start();

  return true;
}

QString BatchDownloadInstallerPlugin::name() const
{
  return "Batch Installer Plugin for MO2";
}

QString BatchDownloadInstallerPlugin::author() const
{
  return "MO2 Batch Download Installer contributors";
}

QString BatchDownloadInstallerPlugin::description() const
{
  return "Installs multiple selected downloads one at a time by handing each "
         "to MO2's own install path. On supported ModOrganizer builds, "
         "defers expensive directory-tree rebuilds until one final refresh.";
}

MOBase::VersionInfo BatchDownloadInstallerPlugin::version() const
{
  return MOBase::VersionInfo(1, 2, 2);
}

QList<MOBase::PluginSetting> BatchDownloadInstallerPlugin::settings() const
{
  return {MOBase::PluginSetting(
      QStringLiteral("defer_directory_refresh"),
      QStringLiteral("On a supported Mod Organizer build, rebuild the virtual "
                     "directory once after the batch instead of after every archive."),
      true)};
}

bool BatchDownloadInstallerPlugin::deferRequested() const
{
  if (m_organizer == nullptr || !batch::BatchRefreshGate::instance().hookOk()) {
    return false;
  }
  const QVariant value =
      m_organizer->pluginSetting(name(), QStringLiteral("defer_directory_refresh"));
  return !value.isValid() || value.toBool();
}

void BatchDownloadInstallerPlugin::setupUi(QMainWindow* window)
{
  if (m_action != nullptr) {
    return;
  }

  qInfo() << "BatchDownloadInstaller: setupUi called, window=" << window
          << "organizer=" << m_organizer;

  if (m_organizer == nullptr || window == nullptr) {
    qInfo() << "BatchDownloadInstaller: setupUi early return, m_organizer="
            << m_organizer << "window=" << window;
    return;
  }

  m_view = window->findChild<QAbstractItemView*>("downloadView");
  if (m_view == nullptr) {
    qInfo() << "BatchDownloadInstaller: downloadView not found";
    return;
  }

  qInfo() << "BatchDownloadInstaller: downloadView found, class="
          << m_view->metaObject()->className();

  m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_view->setSelectionBehavior(QAbstractItemView::SelectRows);

  // Always-update selection observer
  m_action = new QAction(tr("Install selected downloads"), m_view);
  m_action->setEnabled(false);
  m_view->addAction(m_action);

  connect(m_action, &QAction::triggered, this,
          &BatchDownloadInstallerPlugin::onBatchRequested);

  connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged,
          this, &BatchDownloadInstallerPlugin::updateActionEnabled);

  // Keep the stock context menu; inject our batch action when the stock menu
  // shows. The stock handler builds its QMenu and calls exec(); an app-level
  // filter sees the menu's QEvent::Show after it is fully populated and
  // before it is interactive.
  qApp->installEventFilter(this);

  updateActionEnabled();
}

bool BatchDownloadInstallerPlugin::eventFilter(QObject* obj, QEvent* event)
{
  if (event->type() == QEvent::Show) {
    if (auto* menu = qobject_cast<QMenu*>(obj)) {
      if (menu->parentWidget() == m_view) {
        injectBatchAction(menu);
      }
    }
  }
  return QObject::eventFilter(obj, event);
}

void BatchDownloadInstallerPlugin::injectBatchAction(QMenu* menu)
{
  if (menu == nullptr || m_action == nullptr) {
    return;
  }
  BatchSelection sel(m_view, m_organizer);
  const int count = sel.canCapture() ? sel.capture().size() : 0;
  if (count < 2) {
    return;
  }

  menu->addSeparator();
  menu->addAction(m_action);
  m_action->setEnabled(true);
}

void BatchDownloadInstallerPlugin::updateActionEnabled()
{
  if (m_action == nullptr || m_view == nullptr || m_batchRunning) {
    return;
  }

  BatchSelection selection(m_view, m_organizer);
  const bool canCapture = selection.canCapture();
  const int count = canCapture ? selection.capture().size() : 0;
  const bool enable = canCapture && count >= 2;
  m_action->setEnabled(enable);
}

void BatchDownloadInstallerPlugin::onBatchRequested()
{
  if (m_organizer == nullptr || m_view == nullptr || m_batchRunning) {
    return;
  }

  BatchSelection selection(m_view, m_organizer);
  const QList<BatchDownloadRef> downloads = selection.capture();
  if (downloads.size() < 2) {
    return;
  }

  startBatch(downloads);
}

void BatchDownloadInstallerPlugin::startBatch(
    const QList<BatchDownloadRef>& downloads)
{
  if (m_batchRunning) {
    return;
  }
  if (m_panel != nullptr) {
    QWidget* previous = m_panel;
    m_panel = nullptr;
    previous->close();
  }
  m_batchRunning = true;
  m_stopAfterItem = false;
  m_finalRetried = false;
  m_items = downloads;
  m_index = 0;
  m_batchStartedMs = QDateTime::currentMSecsSinceEpoch();
  m_defer = deferRequested();

  m_panel = new BatchProgressPanel(m_view->window());
  m_panel->setAttribute(Qt::WA_DeleteOnClose);
  connect(m_panel, &QObject::destroyed, this, [this]() {
    m_panel = nullptr;
    if (m_phase != BatchPhase::Idle) {
      m_stopAfterItem = true;
    }
  });
  m_panel->show();
  m_panel->raise();
  m_panel->onBatchStarted(downloads.size());

  qInfo() << "BatchDownloadInstaller: batch start count" << downloads.size()
          << "defer" << m_defer
          << "gate" << batch::BatchRefreshGate::instance().statusMessage();

  if (m_defer) {
    m_phase = BatchPhase::Installing;
    auto& gate = batch::BatchRefreshGate::instance();
    gate.setInstalling(true);
    qInfo() << "BatchDownloadInstaller: batch gate armed; refresh deferred to the end";
    QTimer::singleShot(0, this, [this]() { installNext(); });
    return;
  }

  m_phase = BatchPhase::Installing;
  QTimer::singleShot(0, this, [this]() { installNext(); });
}

void BatchDownloadInstallerPlugin::installNext()
{
  if (!m_batchRunning || m_phase != BatchPhase::Installing) {
    return;
  }
  if (m_panel == nullptr || m_panel->cancelRequested() || m_stopAfterItem ||
      m_index >= m_items.size()) {
    beginFinalRefresh();
    return;
  }

  const BatchDownloadRef& ref = m_items.at(m_index);
  if (m_panel != nullptr) {
    m_panel->onItemStarted(m_index, ref);
  }

  BatchInstallItemResult itemResult;
  itemResult.download = ref;

  auto finishItem = [this, &itemResult]() {
    if (m_panel != nullptr) {
      m_panel->onItemFinished(m_index, itemResult);
    }
    ++m_index;
    QTimer::singleShot(0, this, [this]() { installNext(); });
  };

  if (ref.canonicalArchivePath.isEmpty()) {
    itemResult.state = BatchInstallItemState::Failed;
    itemResult.errorText = QStringLiteral("no stable archive path");
    finishItem();
    return;
  }

  BatchSelection selection(m_view, m_organizer);
  const int row = selection.sourceRowForCanonicalArchive(ref.canonicalArchivePath);
  if (row < 0) {
    itemResult.state = BatchInstallItemState::SkippedStaleIndex;
    itemResult.errorText = QStringLiteral("download row not resolved");
    finishItem();
    return;
  }

  const QFileInfo onDisk(ref.canonicalArchivePath);
  if (!onDisk.exists()) {
    itemResult.state = BatchInstallItemState::Failed;
    itemResult.errorText = QStringLiteral("archive missing on disk");
    finishItem();
    return;
  }
  if (ref.expectedFileSize >= 0 && onDisk.size() != ref.expectedFileSize) {
    itemResult.state = BatchInstallItemState::Failed;
    itemResult.errorText = QStringLiteral("file size changed");
    finishItem();
    return;
  }
  if (ref.expectedModifiedTimeUtcMs >= 0 &&
      onDisk.lastModified().toMSecsSinceEpoch() != ref.expectedModifiedTimeUtcMs) {
    itemResult.state = BatchInstallItemState::Failed;
    itemResult.errorText = QStringLiteral("file timestamp changed");
    finishItem();
    return;
  }

  qInfo() << "BatchDownloadInstaller: native install for path"
          << ref.canonicalArchivePath << "row" << row;
  bool invoked = false;
  try {
    batch::BatchRefreshGate::instance().resetInstallObservation();
    invoked = QMetaObject::invokeMethod(m_view, "installDownload", Qt::DirectConnection,
                                        Q_ARG(int, row));
  } catch (...) {
    itemResult.state = BatchInstallItemState::Failed;
    itemResult.errorText = QStringLiteral("native install threw");
    finishItem();
    return;
  }

  auto& gate = batch::BatchRefreshGate::instance();
  if (!invoked) {
    itemResult.state = BatchInstallItemState::Failed;
    itemResult.errorText = QStringLiteral("native install signal not found");
  } else if (gate.modSignalArmed() && gate.modInstalledObserved()) {
    itemResult.state = BatchInstallItemState::Installed;
    itemResult.installedModName = gate.lastInstalledModName();
    itemResult.diskChanged = true;
  } else if (gate.modSignalArmed()) {
    itemResult.state = BatchInstallItemState::Skipped;
    itemResult.errorText = QStringLiteral("installer did not report modInstalled");
  } else {
    itemResult.state = BatchInstallItemState::Dispatched;
  }
  finishItem();
}

void BatchDownloadInstallerPlugin::beginFinalRefresh()
{
  if (m_phase == BatchPhase::FinalRefresh || m_phase == BatchPhase::Idle) {
    return;
  }
  m_phase = BatchPhase::FinalRefresh;
  auto& gate = batch::BatchRefreshGate::instance();
  m_suppressedSnapshot = gate.suppressedRefreshCalls();
  gate.setInstalling(false);

  if (!m_defer) {
    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_batchStartedMs;
    qInfo() << "BatchDownloadInstaller: batch finished in stock refresh mode, ms" << elapsed;
    finishBatch(QStringLiteral("Batch finished. Each install refreshed the directory."));
    return;
  }

  qInfo() << "BatchDownloadInstaller: final refresh (suppressed scheduler calls"
          << m_suppressedSnapshot << ")";
  gate.logCounters("before final refresh");
  gate.refreshAndWait(m_organizer, 120000, true);
}

void BatchDownloadInstallerPlugin::onRefreshWaitFinished(bool directoryReady)
{
  if (!m_batchRunning) {
    return;
  }
  auto& gate = batch::BatchRefreshGate::instance();
  if (m_phase != BatchPhase::FinalRefresh) {
    return;
  }
  if (!directoryReady && !m_finalRetried) {
    m_finalRetried = true;
    qWarning() << "BatchDownloadInstaller: final directoryStructureReady missed; retrying once";
    gate.refreshAndWait(m_organizer, 120000, true);
    return;
  }

  const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_batchStartedMs;
  gate.logCounters("batch end");
  qInfo() << "BatchDownloadInstaller: batch wall ms" << elapsed
          << "suppressedDuringInstall" << m_suppressedSnapshot
          << "workerDuringInstall" << gate.workerRefreshesDuringBatch()
          << "finalReady" << directoryReady;
  if (directoryReady) {
    finishBatch(QStringLiteral(
        "Batch finished. Directory tree rebuilt once at the end."));
  } else {
    finishBatch(QStringLiteral(
        "Batch finished. Final directory refresh did not complete; press F5."));
  }
}

void BatchDownloadInstallerPlugin::finishBatch(const QString& status)
{
  m_phase = BatchPhase::Idle;
  m_batchRunning = false;
  batch::BatchRefreshGate::instance().setInstalling(false);
  if (m_panel != nullptr) {
    m_panel->markFinished(status);
  }
  m_items.clear();
  updateActionEnabled();
}
