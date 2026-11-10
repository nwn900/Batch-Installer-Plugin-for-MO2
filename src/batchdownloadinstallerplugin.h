#ifndef BATCHDOWNLOADINSTALLERPLUGIN_H
#define BATCHDOWNLOADINSTALLERPLUGIN_H

#include "iplugin.h"
#include "uibase/batchinstalltypes.h"

#include <QObject>

#ifdef Q_MOC_RUN
Q_DECLARE_INTERFACE(MOBase::IPlugin, "com.tannin.ModOrganizer.Plugin/2.0")
#endif

#include <memory>

class QAction;
class QAbstractItemView;
class QMainWindow;
class QCloseEvent;

class BatchDownloadInstallerPlugin : public QObject, public MOBase::IPlugin
{
  Q_OBJECT
  Q_PLUGIN_METADATA(IID "org.modorganizer.BatchDownloadInstallerPlugin")
  Q_INTERFACES(MOBase::IPlugin)

public:
  BatchDownloadInstallerPlugin();
  ~BatchDownloadInstallerPlugin() override;

  bool init(MOBase::IOrganizer* organizer) override;
  QString name() const override;
  QString author() const override;
  QString description() const override;
  MOBase::VersionInfo version() const override;
  QList<MOBase::PluginSetting> settings() const override;

private slots:
  void onBatchRequested();
  void updateActionEnabled();
  void onRefreshWaitFinished(bool directoryReady);

private:
  enum class BatchPhase { Idle, Installing, FinalRefresh };

  void setupUi(QMainWindow* window);
  void startBatch(const QList<BatchDownloadRef>& downloads);
  void installNext();
  void beginFinalRefresh();
  void finishBatch(const QString& status);
  void injectBatchAction(QMenu* menu);
  bool eventFilter(QObject* obj, QEvent* event) override;
  bool deferRequested() const;

  MOBase::IOrganizer* m_organizer = nullptr;
  QAbstractItemView* m_view = nullptr;
  QAction* m_action = nullptr;
  class BatchProgressPanel* m_panel = nullptr;
  QList<BatchDownloadRef> m_items;
  BatchPhase m_phase = BatchPhase::Idle;
  int m_index = 0;
  qint64 m_batchStartedMs = 0;
  int m_suppressedSnapshot = 0;
  bool m_batchRunning = false;
  bool m_stopAfterItem = false;
  bool m_defer = false;
  bool m_finalRetried = false;
};

#endif
