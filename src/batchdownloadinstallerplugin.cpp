#include "batchdownloadinstallerplugin.h"

#include "batchselection.h"
#include "batchprogresspanel.h"
#include "batchrefreshgate.h"

#include "qt_compat.h"

#include <QAction>
#include <QAbstractItemView>
#include <QApplication>
#include <QDebug>
#include <QMainWindow>
#include <QMenu>
#include <QItemSelectionModel>
#include <QMetaObject>
#include <QTimer>

namespace {
QMainWindow* findMainWindow()
{
  for (QWidget* widget : QApplication::topLevelWidgets()) {
    if (auto* mw = qobject_cast<QMainWindow*>(widget)) {
      return mw;
    }
  }
  return nullptr;
}

}  // namespace

BatchDownloadInstallerPlugin::BatchDownloadInstallerPlugin() = default;

BatchDownloadInstallerPlugin::~BatchDownloadInstallerPlugin()
{
  batch::BatchRefreshGate::instance().shutdown();
}

bool BatchDownloadInstallerPlugin::init(MOBase::IOrganizer* organizer)
{
  m_organizer = organizer;

  if (organizer == nullptr) {
    return true;
  }

  // Plugin-only mode: always set up UI. Works on stock or combined core.
  organizer->onUserInterfaceInitialized(
      [this](QMainWindow* window) {
        qInfo() << "BatchDownloadInstaller: UI initialized, window=" << window;
        if (window == nullptr) {
          // MO2 fires this signal once during plugin startup (window null).
          // Retry shortly after; the real window appears later.
          qInfo() << "BatchDownloadInstaller: window null, retrying with timer";
          QTimer::singleShot(2000, this, [this]() {
            setupUi(findMainWindow());
          });
          return;
        }
        setupUi(window);
      });

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
  return MOBase::VersionInfo(1, 2, 0);
}

QList<MOBase::PluginSetting> BatchDownloadInstallerPlugin::settings() const
{
  return {};
}

void BatchDownloadInstallerPlugin::setupUi(QMainWindow* window)
{
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

  if (!batch::BatchRefreshGate::instance().hookOk()) {
    const bool armed =
        batch::BatchRefreshGate::instance().initialize(m_organizer);
    if (armed) {
      qInfo() << "BatchDownloadInstaller: deferred directory refresh active";
    } else {
      qInfo() << "BatchDownloadInstaller: stock refresh mode -"
              << batch::BatchRefreshGate::instance().statusMessage();
    }
  }
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
  m_batchRunning = true;

  BatchProgressPanel panel(m_view->window());
  panel.show();
  panel.raise();

  panel.onBatchStarted(downloads.size());

  batch::BatchDirectoryRefreshBatch refreshBatch(m_organizer);
  if (refreshBatch.effective()) {
    qInfo() << "BatchDownloadInstaller: batch gate armed (deferred directory tree)";
  }

  BatchSelection selection(m_view, m_organizer);

  for (int i = 0; i < downloads.size(); ++i) {
    const BatchDownloadRef& ref = downloads.at(i);

    // Let the UI process events
    QApplication::processEvents();

    if (panel.cancelRequested()) {
      break;
    }

    panel.onItemStarted(i, ref);

    BatchInstallItemResult itemResult;
    itemResult.download = ref;

    if (ref.canonicalArchivePath.isEmpty()) {
      itemResult.state = BatchInstallItemState::Failed;
      itemResult.errorText = QStringLiteral("no stable archive path");
      panel.onItemFinished(i, itemResult);
      continue;
    }

    const int row = selection.sourceRowForCanonicalArchive(ref.canonicalArchivePath);
    if (row < 0) {
      itemResult.state = BatchInstallItemState::SkippedStaleIndex;
      itemResult.errorText = QStringLiteral("download row not resolved");
      panel.onItemFinished(i, itemResult);
      continue;
    }

    const QFileInfo onDisk(ref.canonicalArchivePath);
    if (!onDisk.exists()) {
      itemResult.state = BatchInstallItemState::Failed;
      itemResult.errorText = QStringLiteral("archive missing on disk");
      panel.onItemFinished(i, itemResult);
      continue;
    }
    if (ref.expectedFileSize >= 0 && onDisk.size() != ref.expectedFileSize) {
      itemResult.state = BatchInstallItemState::Failed;
      itemResult.errorText = QStringLiteral("file size changed");
      panel.onItemFinished(i, itemResult);
      continue;
    }
    if (ref.expectedModifiedTimeUtcMs >= 0 &&
        onDisk.lastModified().toMSecsSinceEpoch() != ref.expectedModifiedTimeUtcMs) {
      itemResult.state = BatchInstallItemState::Failed;
      itemResult.errorText = QStringLiteral("file timestamp changed");
      panel.onItemFinished(i, itemResult);
      continue;
    }

    // Redirect to MO2's own install path: DownloadListView::installDownload
    // goes straight to OrganizerCore::installDownload, which does the
    // existing-mod detection (the modID -> installed mod lookup that presets
    // [NoDelete] style names) and then runs the stock Quick Install dialog.
    // Firing the signal on the view meanS batch installs behave exactly like
    // double-clicking each download. It is synchronous, so each native
    // install (dialog included) finishes before the next one starts.
    qInfo() << "BatchDownloadInstaller: native install for path"
            << ref.canonicalArchivePath << "row" << row;
    const bool invoked = QMetaObject::invokeMethod(
        m_view, "installDownload", Qt::DirectConnection, Q_ARG(int, row));

    if (!invoked) {
      itemResult.state = BatchInstallItemState::Failed;
      itemResult.errorText = QStringLiteral("native install signal not found");
      panel.onItemFinished(i, itemResult);
      continue;
    }

    itemResult.state = BatchInstallItemState::Dispatched;
    panel.onItemFinished(i, itemResult);
  }

  panel.hide();

  if (refreshBatch.effective()) {
    batch::BatchRefreshGate::instance().logCounters("batch finished");
  }

  m_batchRunning = false;
  updateActionEnabled();
}
