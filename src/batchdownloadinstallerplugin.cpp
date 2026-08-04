#include "batchdownloadinstallerplugin.h"

#include "batchselection.h"
#include "batchprogresspanel.h"
#include "imodinterface.h"

#include "qt_compat.h"

#include <QAction>
#include <QAbstractItemView>
#include <QApplication>
#include <QDebug>
#include <QFileInfo>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QItemSelectionModel>
#include <QThread>
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

// Stock MO2 names an installed download after the download's display name
// from its .meta file, falling back to the archive filename without
// extension. Replicate that instead of passing the raw filename.
QString modNameForArchive(const QString& archivePath)
{
  const QFileInfo metaInfo(archivePath + ".meta");
  if (metaInfo.isFile()) {
    QFile file(metaInfo.filePath());
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      bool inGeneral = false;
      while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty()) {
          continue;
        }
        if (line.startsWith('[') && line.endsWith(']')) {
          inGeneral = line.compare(QStringLiteral("[General]"),
                                   Qt::CaseInsensitive) == 0;
          continue;
        }
        if (inGeneral && line.startsWith(QStringLiteral("name="),
                                          Qt::CaseInsensitive)) {
          QString name = line.mid(5).trimmed();
          // Strip surrounding quotes if present.
          if (name.size() >= 2 && name.startsWith('"') && name.endsWith('"')) {
            name = name.mid(1, name.size() - 2);
          }
          if (!name.isEmpty()) {
            return name;
          }
        }
      }
    }
  }
  return QFileInfo(archivePath).completeBaseName();
}
}  // namespace

BatchDownloadInstallerPlugin::BatchDownloadInstallerPlugin() = default;
BatchDownloadInstallerPlugin::~BatchDownloadInstallerPlugin() = default;

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
  return "Installs multiple downloaded archives one at a time via sequential "
         "installMod calls. Works with stock or combined MO2 core.";
}

MOBase::VersionInfo BatchDownloadInstallerPlugin::version() const
{
  return MOBase::VersionInfo(1, 0, 0);
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

  int installed = 0;
  int failed = 0;
  int skipped = 0;

  for (int i = 0; i < downloads.size(); ++i) {
    const BatchDownloadRef& ref = downloads.at(i);

    // Let the UI process events
    QApplication::processEvents();

    if (panel.cancelRequested()) {
      break;
    }

    panel.onItemStarted(i, ref);

    const QString path = ref.canonicalArchivePath;
    if (!QFileInfo::exists(path)) {
      BatchInstallItemResult res;
      res.download = ref;
      res.state = BatchInstallItemState::Failed;
      res.errorText = QStringLiteral("File not found");
      panel.onItemFinished(i, res);
      ++failed;
      continue;
    }

    const QString modName = modNameForArchive(path);
    qInfo() << "BatchDownloadInstaller: installing" << path << "as" << modName;
    MOBase::IModInterface* mod =
        m_organizer->installMod(path, modName);

    BatchInstallItemResult itemResult;
    itemResult.download = ref;

    if (mod != nullptr) {
      itemResult.state = BatchInstallItemState::Installed;
      itemResult.installedModName = mod->name();
      m_organizer->refresh(false);
      QApplication::processEvents();
      panel.onItemFinished(i, itemResult);
      ++installed;
    } else {
      itemResult.state = BatchInstallItemState::Failed;
      itemResult.errorText = QStringLiteral("installMod returned null");
      panel.onItemFinished(i, itemResult);
      ++failed;
    }
  }

  // Final refresh after all installs
  m_organizer->refresh(true);
  QApplication::processEvents();

  panel.onBatchStarted(0);  // signal completion to panel
  panel.hide();

  m_batchRunning = false;
  updateActionEnabled();
}
