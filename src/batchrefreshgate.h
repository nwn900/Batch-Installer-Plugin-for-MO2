#ifndef BATCHREFRESHGATE_H
#define BATCHREFRESHGATE_H

#include "imoinfo.h"

#include <QObject>
#include <QString>

#include <array>
#include <atomic>
#include <cstdint>

class QTimer;

namespace batch {

// In-process MinHook detour on ModOrganizer.exe OrganizerCore::refreshDirectoryStructure.
// Verified per release SHA-256 + 16-byte prologue at the hook RVA. Unknown builds keep
// stock refresh behavior. OrganizerCore is parentless, so the core QObject is taken
// from the detour's this pointer on the GUI thread, not from findChildren.
class BatchRefreshGate : public QObject {
  Q_OBJECT

public:
  static BatchRefreshGate& instance();

  bool initialize(MOBase::IOrganizer* organizer);
  void shutdown();

  bool hookOk() const { return m_hookOk; }
  bool modSignalArmed() const { return m_modSignal; }
  bool busy() const { return m_installing.load() || m_waiting.load(); }
  QString statusMessage() const { return m_status; }

  void setInstalling(bool active);
  bool installing() const { return m_installing.load(); }

  // Quiesce (commit == false) waits until no directory rebuild is in flight.
  // Commit waits until a rebuild started after the call has finished, so an
  // older in-flight refresh cannot satisfy the final tree update.
  // Timeout completes with false.
  void refreshAndWait(MOBase::IOrganizer* organizer, int timeoutMs, bool commit);

  void resetInstallObservation();
  bool modInstalledObserved() const { return m_sawModInstalled; }
  QString lastInstalledModName() const { return m_lastModName; }

  void logCounters(const char* where) const;

  int suppressedRefreshCalls() const { return m_suppressedRefreshCalls.load(); }
  int forwardedRefreshCalls() const { return m_forwardedRefreshCalls.load(); }
  int workerRefreshesDuringBatch() const { return m_workerDuringBatch.load(); }

  static void hookRefreshDirectoryStructure(void* self);

signals:
  void refreshWaitFinished(bool directoryReady);
  void environmentChanged();

private slots:
  void onDirectoryStructureReady();
  void onWorkerRefreshed();
  void onModInstalled(const QString& modName);
  void onEnvironmentChanged();
  void onWaitTimeout();
  void markReadyAcceptable(int waitId);

private:
  BatchRefreshGate();

  bool resolveHostModule();
  bool matchProfile();
  bool verifyPrologue(uintptr_t address) const;
  bool installHook();
  void removeHook();
  void ensureCoreWatch(void* self);
  void beginWait(int timeoutMs);
  void onRefreshBarrier(MOBase::IOrganizer* organizer);
  bool connectSignalByName(QObject* sender, const char* name, const char* slot);

  MOBase::IOrganizer* m_organizer = nullptr;
  QString m_status;
  QString m_profileLabel;
  bool m_hookOk = false;
  bool m_modSignal = false;
  bool m_commit = false;
  bool m_insideRefreshAndWait = false;
  bool m_barrierHandled = false;
  bool m_refreshOnNextReady = false;
  bool m_mhInitialized = false;
  bool m_sawModInstalled = false;
  bool m_readyAcceptable = false;
  uintptr_t m_imageBase = 0;
  size_t m_imageSize = 0;

  uint32_t m_hookRva = 0;
  std::array<uint8_t, 16> m_expectedPrologue{};

  QObject* m_core = nullptr;
  QTimer* m_timeout = nullptr;
  QString m_lastModName;

  static inline void* s_refreshDirectoryTarget = nullptr;
  static void (*s_originalRefreshDirectory)(void* self);

  std::atomic<bool> m_installing{false};
  std::atomic<bool> m_waiting{false};
  std::atomic<int> m_waitId{0};

  std::atomic<int> m_suppressedRefreshCalls{0};
  std::atomic<int> m_forwardedRefreshCalls{0};
  std::atomic<int> m_workerDuringBatch{0};
};

}  // namespace batch

#endif
