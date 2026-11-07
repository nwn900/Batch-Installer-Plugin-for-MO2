#ifndef BATCHREFRESHGATE_H
#define BATCHREFRESHGATE_H

#include "imoinfo.h"

#include <QString>

#include <array>
#include <atomic>
#include <cstdint>

namespace batch {

// In-process MinHook detour on ModOrganizer.exe OrganizerCore::refreshDirectoryStructure.
// Verified per release SHA-256 + 16-byte prologue at hook RVA. Falls back to stock
// behavior when verification fails.
class BatchRefreshGate {
public:
  static BatchRefreshGate& instance();

  bool initialize(MOBase::IOrganizer* organizer);
  void shutdown();

  bool hookOk() const { return m_hookOk; }
  QString statusMessage() const { return m_status; }

  void enterBatch(MOBase::IOrganizer* organizer);
  void leaveBatch(MOBase::IOrganizer* organizer);

  bool suppressDirectoryRefresh() const { return m_batchActive.load(); }

  void logCounters(const char* where) const;

  int suppressedRefreshCalls() const
  {
    return m_suppressedRefreshCalls.load();
  }
  int forwardedRefreshCalls() const
  {
    return m_forwardedRefreshCalls.load();
  }

  QObject* findOrganizerCoreObject() const;
  void waitForDirectoryStructureReady(QObject* organizerCore, int timeoutMs);

  static void hookRefreshDirectoryStructure(void* self);
  static void* refreshDirectoryFunction() { return s_refreshDirectoryTarget; }

private:
  BatchRefreshGate() = default;

  bool resolveHostModule();
  bool matchProfile();
  bool verifyPrologue(uintptr_t address) const;
  bool installHook();
  void removeHook();

  MOBase::IOrganizer* m_organizer = nullptr;
  QString m_status;
  bool m_hookOk = false;
  bool m_mhInitialized = false;
  uintptr_t m_imageBase = 0;
  size_t m_imageSize = 0;

  uint32_t m_hookRva = 0;
  std::array<uint8_t, 16> m_expectedPrologue{};

  static inline void* s_refreshDirectoryTarget = nullptr;
  static void (*s_originalRefreshDirectory)(void* self);

  std::atomic<bool> m_batchActive{false};

  std::atomic<int> m_suppressedRefreshCalls{0};
  std::atomic<int> m_forwardedRefreshCalls{0};
};

// RAII: preflight idle tree, hold gate for sequential installs, one final refresh.
class BatchDirectoryRefreshBatch {
public:
  explicit BatchDirectoryRefreshBatch(MOBase::IOrganizer* organizer);
  ~BatchDirectoryRefreshBatch();

  BatchDirectoryRefreshBatch(const BatchDirectoryRefreshBatch&) = delete;
  BatchDirectoryRefreshBatch& operator=(const BatchDirectoryRefreshBatch&) = delete;

  bool effective() const { return m_effective; }

private:
  MOBase::IOrganizer* m_organizer = nullptr;
  bool m_effective = false;
};

}  // namespace batch

#endif
