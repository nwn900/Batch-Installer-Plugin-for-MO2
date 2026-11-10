/*
Copyright (C) 2026 MO2 Batch Download Installer contributors.

Deferred OrganizerCore::refreshDirectoryStructure via in-process MinHook.
Targets verified ModOrganizer.exe builds only (SHA-256 + prologue).
*/

#include "batchrefreshgate.h"

#include <MinHook.h>

#include <QCoreApplication>
#include <QDebug>
#include <QMetaMethod>
#include <QMetaObject>
#include <QThread>
#include <QTimer>

#include <Windows.h>
#include <bcrypt.h>
#include <psapi.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace batch {

namespace {

struct BuildProfile {
  const char* label;
  const char* exeSha256Hex;
  uint32_t refreshDirectoryRva;
  std::array<uint8_t, 16> expectedPrologue;
};

// Verified on release builds (PE image base 0x140000000, 64-bit):
constexpr BuildProfile kProfiles[] = {
    {"MO2-2.5.2",
     "442B354A8F34754DA0048654C44D27F51628FEBA54CE46C3187CF58D6C43E622", 0x001E36D0,
     {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24,
      0x18, 0x48, 0x89, 0x7c, 0x24, 0x20, 0x55}},
    {"MO2-2.5.0",
     "80AC43549D7E15249A5CBA2F1E8509612B229FFC0E949A6B940137D87C27C226", 0x001D4E40,
     {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24,
      0x18, 0x48, 0x89, 0x7c, 0x24, 0x20, 0x55}},
    {"MO2-2.4.4",
     "6992A0283677F2FF6D1FE201DF1B7762B9CEEB5718E668D13B582D725D30BD2A", 0x001C3810,
     {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24,
      0x10, 0x48, 0x89, 0x7c, 0x24, 0x18, 0x55}},
    // Nolvus Awakening MO2 reports 2.4.4 but is not the stock 2.4.4 binary.
    // Measured on ModOrganizer.exe SHA-256 F51B5970...: the "refreshing structure"
    // LEA sits in the function that starts at this RVA, after int3 padding.
    {"MO2-2.4.4-nolvus",
     "F51B5970DC8A9BA34269D6088CC6CAA6704A7FC143D978FBA3ECDDEB67B1D200", 0x001B9E30,
     {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24,
      0x10, 0x48, 0x89, 0x7c, 0x24, 0x18, 0x55}},
};

bool HexToBytes(const char* hex, std::vector<uint8_t>& out)
{
  size_t len = strlen(hex);
  if (len % 2 != 0) {
    return false;
  }
  out.resize(len / 2);
  for (size_t i = 0; i < out.size(); ++i) {
    char buf[3] = {hex[i * 2], hex[i * 2 + 1], 0};
    char* end = nullptr;
    unsigned long v = strtoul(buf, &end, 16);
    if (end != buf + 2) {
      return false;
    }
    out[i] = static_cast<uint8_t>(v);
  }
  return true;
}

bool Sha256File(const std::wstring& path, std::vector<uint8_t>& outDigest)
{
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) {
    return false;
  }
  auto size = static_cast<size_t>(f.tellg());
  f.seekg(0, std::ios::beg);
  std::vector<uint8_t> buf(size);
  if (!f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(size))) {
    return false;
  }

  BCRYPT_ALG_HANDLE hAlg = nullptr;
  if (!BCRYPT_SUCCESS(
          BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
    return false;
  }
  BCRYPT_HASH_HANDLE hHash = nullptr;
  DWORD hashLen = 0, cbData = 0;
  if (!BCRYPT_SUCCESS(BCryptGetProperty(
          hAlg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLen),
          sizeof(hashLen), &cbData, 0))) {
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return false;
  }
  if (!BCRYPT_SUCCESS(
          BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0))) {
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return false;
  }
  if (!BCRYPT_SUCCESS(BCryptHashData(hHash, buf.data(), static_cast<ULONG>(buf.size()),
                                     0))) {
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return false;
  }
  outDigest.resize(hashLen);
  if (!BCRYPT_SUCCESS(BCryptFinishHash(
          hHash, outDigest.data(), static_cast<ULONG>(outDigest.size()), 0))) {
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return false;
  }
  BCryptDestroyHash(hHash);
  BCryptCloseAlgorithmProvider(hAlg, 0);
  return true;
}

QString DigestToHex(const std::vector<uint8_t>& d)
{
  static const char* hex = "0123456789ABCDEF";
  QString s;
  s.reserve(static_cast<int>(d.size() * 2));
  for (uint8_t b : d) {
    s.append(QLatin1Char(hex[b >> 4]));
    s.append(QLatin1Char(hex[b & 0xF]));
  }
  return s;
}

bool guiThread()
{
  QCoreApplication* app = QCoreApplication::instance();
  return app != nullptr && QThread::currentThread() == app->thread();
}

}  // namespace

void (*BatchRefreshGate::s_originalRefreshDirectory)(void* self) = nullptr;

BatchRefreshGate::BatchRefreshGate() : QObject(nullptr) {}

BatchRefreshGate& BatchRefreshGate::instance()
{
  static BatchRefreshGate g;
  return g;
}

bool BatchRefreshGate::resolveHostModule()
{
  HMODULE mod = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, nullptr,
                          &mod) ||
      mod == nullptr) {
    m_status = QStringLiteral("GetModuleHandleEx failed");
    return false;
  }
  MODULEINFO mi{};
  if (!GetModuleInformation(GetCurrentProcess(), mod, &mi, sizeof(mi))) {
    m_status = QStringLiteral("GetModuleInformation failed");
    return false;
  }
  m_imageBase = reinterpret_cast<uintptr_t>(mi.lpBaseOfDll);
  m_imageSize = mi.SizeOfImage;
  return true;
}

bool BatchRefreshGate::matchProfile()
{
  wchar_t pathBuf[MAX_PATH * 4]{};
  const DWORD n = GetModuleFileNameW(nullptr, pathBuf, MAX_PATH * 4);
  if (n == 0) {
    m_status = QStringLiteral("GetModuleFileName failed");
    return false;
  }
  std::wstring path(pathBuf);

  std::vector<uint8_t> digest;
  if (!Sha256File(path, digest)) {
    m_status = QStringLiteral("SHA-256 of ModOrganizer.exe failed");
    return false;
  }
  const QString hash = DigestToHex(digest);

  for (const BuildProfile& p : kProfiles) {
    std::vector<uint8_t> expected;
    if (!HexToBytes(p.exeSha256Hex, expected) || expected.size() != digest.size()) {
      continue;
    }
    bool match = true;
    for (size_t i = 0; i < digest.size(); ++i) {
      if (digest[i] != expected[i]) {
        match = false;
        break;
      }
    }
    if (match) {
      m_hookRva = p.refreshDirectoryRva;
      m_expectedPrologue = p.expectedPrologue;
      m_profileLabel = QString::fromLatin1(p.label);
      m_status = QStringLiteral("matched %1 SHA256 verified; hook RVA 0x%2")
                     .arg(m_profileLabel, QString::number(p.refreshDirectoryRva, 16));
      qInfo() << "BatchRefreshGate:" << m_status << "hash" << hash;
      return true;
    }
  }
  m_status = QStringLiteral("unsupported ModOrganizer.exe (SHA256 ") + hash +
             QStringLiteral("); deferred directory refresh disabled");
  qWarning() << "BatchRefreshGate:" << m_status;
  return false;
}

bool BatchRefreshGate::verifyPrologue(uintptr_t address) const
{
  if (m_hookRva == 0) {
    return false;
  }
  const uint8_t* p = reinterpret_cast<const uint8_t*>(address);
  for (size_t i = 0; i < m_expectedPrologue.size(); ++i) {
    if (p[i] != m_expectedPrologue[i]) {
      return false;
    }
  }
  return true;
}

bool BatchRefreshGate::installHook()
{
  if (m_hookRva == 0) {
    return false;
  }
  const uintptr_t target = m_imageBase + m_hookRva;
  if (target < m_imageBase ||
      target + 32 > m_imageBase + static_cast<uintptr_t>(m_imageSize)) {
    m_status = QStringLiteral("RVA outside image; hook aborted");
    qWarning() << "BatchRefreshGate:" << m_status;
    return false;
  }
  if (!verifyPrologue(target)) {
    m_status = QStringLiteral("prologue mismatch at RVA; hook aborted (binary differs)");
    qWarning() << "BatchRefreshGate:" << m_status;
    return false;
  }

  if (MH_Initialize() != MH_OK) {
    m_status = QStringLiteral("MinHook MH_Initialize failed");
    qWarning() << "BatchRefreshGate:" << m_status;
    return false;
  }
  m_mhInitialized = true;

  s_refreshDirectoryTarget = reinterpret_cast<void*>(target);
  const MH_STATUS st =
      MH_CreateHook(reinterpret_cast<LPVOID>(target),
                    reinterpret_cast<LPVOID>(&BatchRefreshGate::hookRefreshDirectoryStructure),
                    reinterpret_cast<LPVOID*>(&s_originalRefreshDirectory));
  if (st != MH_OK) {
    m_status = QStringLiteral("MH_CreateHook failed");
    qWarning() << "BatchRefreshGate:" << m_status << "status" << static_cast<int>(st);
    MH_Uninitialize();
    m_mhInitialized = false;
    return false;
  }
  if (MH_EnableHook(reinterpret_cast<LPVOID>(target)) != MH_OK) {
    m_status = QStringLiteral("MH_EnableHook failed");
    qWarning() << "BatchRefreshGate:" << m_status;
    MH_RemoveHook(reinterpret_cast<LPVOID>(target));
    MH_Uninitialize();
    m_mhInitialized = false;
    return false;
  }
  return true;
}

void BatchRefreshGate::removeHook()
{
  // MinHook's disable path suspends every thread. Doing that while Mod
  // Organizer is leaving aboutToQuit crashes the process. The install path
  // already verified these 16 bytes, which cover MinHook's 14-byte jump.
  if (s_refreshDirectoryTarget != nullptr) {
    DWORD oldProtect = 0;
    if (VirtualProtect(s_refreshDirectoryTarget, m_expectedPrologue.size(),
                       PAGE_EXECUTE_READWRITE, &oldProtect)) {
      std::memcpy(s_refreshDirectoryTarget, m_expectedPrologue.data(),
                  m_expectedPrologue.size());
      DWORD ignored = 0;
      VirtualProtect(s_refreshDirectoryTarget, m_expectedPrologue.size(), oldProtect,
                     &ignored);
      FlushInstructionCache(GetCurrentProcess(), s_refreshDirectoryTarget,
                            m_expectedPrologue.size());
    }
    s_refreshDirectoryTarget = nullptr;
  }
  s_originalRefreshDirectory = nullptr;
  m_mhInitialized = false;
}

bool BatchRefreshGate::connectSignalByName(QObject* sender, const char* name,
                                           const char* slot)
{
  if (sender == nullptr) {
    return false;
  }
  const int slotIndex = metaObject()->indexOfSlot(slot);
  const QMetaMethod target = metaObject()->method(slotIndex);
  if (!target.isValid()) {
    qWarning() << "BatchRefreshGate: missing slot" << slot;
    return false;
  }
  const QMetaObject* mo = sender->metaObject();
  for (int i = 0; i < mo->methodCount(); ++i) {
    const QMetaMethod method = mo->method(i);
    if (method.methodType() != QMetaMethod::Signal) {
      continue;
    }
    if (method.name() != QByteArray(name)) {
      continue;
    }
    const Qt::ConnectionType type =
        (qstrcmp(slot, "onDirectoryStructureReady()") == 0) ? Qt::QueuedConnection
                                                            : Qt::AutoConnection;
    if (QObject::connect(sender, method, this, target, type)) {
      qInfo() << "BatchRefreshGate: connected" << sender->metaObject()->className()
              << method.methodSignature();
      return true;
    }
  }
  qWarning() << "BatchRefreshGate: signal not found" << name << "on"
             << sender->metaObject()->className();
  return false;
}

void BatchRefreshGate::ensureCoreWatch(void* self)
{
  if (m_core != nullptr || self == nullptr) {
    return;
  }
  auto* obj = reinterpret_cast<QObject*>(self);
  if (qstrcmp(obj->metaObject()->className(), "OrganizerCore") != 0) {
    qWarning() << "BatchRefreshGate: detour this is" << obj->metaObject()->className()
               << "; deferred refresh disabled";
    return;
  }
  m_core = obj;
  qInfo() << "BatchRefreshGate: OrganizerCore captured from detour this";
  connectSignalByName(m_core, "directoryStructureReady", "onDirectoryStructureReady()");
  m_modSignal =
      connectSignalByName(m_core, "modInstalled", "onModInstalled(QString)");
  connectSignalByName(m_core, "managedGameChanged", "onEnvironmentChanged()");

  const QList<QObject*> kids =
      m_core->findChildren<QObject*>(QString(), Qt::FindChildrenRecursively);
  for (QObject* kid : kids) {
    if (kid != nullptr &&
        qstrcmp(kid->metaObject()->className(), "DirectoryRefresher") == 0) {
      connectSignalByName(kid, "refreshed", "onWorkerRefreshed()");
      break;
    }
  }
}

bool BatchRefreshGate::initialize(MOBase::IOrganizer* organizer)
{
  if (m_hookOk) {
    return true;
  }
  m_organizer = organizer;
  if (organizer == nullptr) {
    return false;
  }
  if (m_timeout == nullptr) {
    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, &BatchRefreshGate::onWaitTimeout);
  }
  if (!resolveHostModule()) {
    qWarning() << "BatchRefreshGate:" << m_status;
    return false;
  }
  if (!matchProfile()) {
    return false;
  }
  if (!installHook()) {
    return false;
  }
  m_hookOk = true;
  qWarning() << "BatchRefreshGate: hook installed;" << m_status;
  // Unhook from aboutToQuit. The plugin destructor runs inside FreeLibrary,
  // and MinHook's thread freeze is not safe under the loader lock.
  if (QCoreApplication::instance() != nullptr) {
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this,
            [this]() { shutdown(); });
  }
  return true;
}

void BatchRefreshGate::shutdown()
{
  m_installing.store(false);
  m_waiting.store(false);
  m_readyAcceptable = false;
  if (m_timeout != nullptr) {
    m_timeout->stop();
  }
  if (m_core != nullptr) {
    QObject::disconnect(m_core, nullptr, this, nullptr);
    m_core = nullptr;
  }
  removeHook();
  m_hookOk = false;
  m_modSignal = false;
  m_hookRva = 0;
  m_expectedPrologue = {};
  m_organizer = nullptr;
}

void BatchRefreshGate::setInstalling(bool active)
{
  if (active) {
    m_suppressedRefreshCalls.store(0);
    m_forwardedRefreshCalls.store(0);
    m_workerDuringBatch.store(0);
  }
  m_installing.store(active);
  qInfo() << "BatchRefreshGate: installing" << active;
}

void BatchRefreshGate::beginWait(int timeoutMs)
{
  m_readyAcceptable = false;
  m_refreshOnNextReady = false;
  m_barrierHandled = false;
  m_waitId.store(m_waitId.load() + 1);
  m_waiting.store(true);
  if (m_timeout != nullptr) {
    m_timeout->start(timeoutMs);
  }
}

void BatchRefreshGate::onRefreshBarrier(MOBase::IOrganizer* organizer)
{
  if (!m_waiting.load() || organizer == nullptr) {
    return;
  }
  m_barrierHandled = true;
  // Synchronous callback means nothing was in progress, so this call must
  // start the refresh. A later callback is MO2 finishing an older refresh
  // just before directoryStructureReady.
  if (m_insideRefreshAndWait) {
    qInfo() << "BatchRefreshGate: refresh(false) while waiting for directoryStructureReady";
    organizer->refresh(false);
    return;
  }
  if (!m_commit) {
    const int id = m_waitId.load();
    QMetaObject::invokeMethod(this, "markReadyAcceptable", Qt::QueuedConnection,
                              Q_ARG(int, id));
    return;
  }
  m_refreshOnNextReady = true;
  qInfo() << "BatchRefreshGate: waiting for the in-flight refresh before the commit refresh";
}

void BatchRefreshGate::refreshAndWait(MOBase::IOrganizer* organizer, int timeoutMs,
                                      bool commit)
{
  m_organizer = organizer;
  m_commit = commit;
  if (organizer == nullptr || !m_hookOk) {
    QMetaObject::invokeMethod(
        this, [this]() { emit refreshWaitFinished(false); }, Qt::QueuedConnection);
    return;
  }
  beginWait(timeoutMs);
#if defined(BATCH_MO2_HAS_NEXT_REFRESH)
  // Idle: MO2 runs the callback before returning and reports no connection.
  // Busy: the callback is stored and runs from the refresh that is already going.
  m_insideRefreshAndWait = true;
  const bool connected = organizer->onNextRefresh(
      [this, organizer]() { onRefreshBarrier(organizer); }, true);
  m_insideRefreshAndWait = false;
  if (!connected && !m_barrierHandled) {
    qInfo() << "BatchRefreshGate: onNextRefresh unavailable; refreshing directly";
    organizer->refresh(false);
  }
#else
  qInfo() << "BatchRefreshGate: refresh(false) while waiting for directoryStructureReady";
  organizer->refresh(false);
#endif
}

void BatchRefreshGate::resetInstallObservation()
{
  m_sawModInstalled = false;
  m_lastModName.clear();
}

void BatchRefreshGate::logCounters(const char* where) const
{
  qInfo() << "BatchRefreshGate:" << where
          << "profile=" << m_profileLabel
          << "suppressed=" << m_suppressedRefreshCalls.load()
          << "forwarded=" << m_forwardedRefreshCalls.load()
          << "workerDuringBatch=" << m_workerDuringBatch.load();
}

void BatchRefreshGate::onDirectoryStructureReady()
{
  if (!m_waiting.load()) {
    return;
  }
  if (m_refreshOnNextReady) {
    m_refreshOnNextReady = false;
    qInfo() << "BatchRefreshGate: in-flight refresh finished; starting commit refresh";
    if (m_organizer != nullptr) {
      m_organizer->refresh(false);
    }
    return;
  }
  if (!m_readyAcceptable) {
    return;
  }
  m_waiting.store(false);
  m_readyAcceptable = false;
  if (m_timeout != nullptr) {
    m_timeout->stop();
  }
  qInfo() << "BatchRefreshGate: directoryStructureReady matched in-flight refresh";
  emit refreshWaitFinished(true);
}

void BatchRefreshGate::onWorkerRefreshed()
{
  if (!m_installing.load()) {
    return;
  }
  m_workerDuringBatch.fetch_add(1);
  qWarning() << "BatchRefreshGate: DirectoryRefresher::refreshed during batch";
}

void BatchRefreshGate::onModInstalled(const QString& modName)
{
  m_sawModInstalled = true;
  m_lastModName = modName;
  qInfo() << "BatchRefreshGate: modInstalled" << modName;
}

void BatchRefreshGate::onEnvironmentChanged()
{
  qWarning() << "BatchRefreshGate: managed game changed during organizer session";
  emit environmentChanged();
}

void BatchRefreshGate::onWaitTimeout()
{
  if (!m_waiting.load()) {
    return;
  }
  m_waiting.store(false);
  m_readyAcceptable = false;
  m_refreshOnNextReady = false;
  qWarning() << "BatchRefreshGate: timed out waiting for directoryStructureReady";
  emit refreshWaitFinished(false);
}

void BatchRefreshGate::markReadyAcceptable(int waitId)
{
  if (m_waiting.load() && waitId == m_waitId.load()) {
    m_readyAcceptable = true;
  }
}

void BatchRefreshGate::hookRefreshDirectoryStructure(void* self)
{
  auto& g = instance();
  if (guiThread()) {
    g.ensureCoreWatch(self);
    const bool sameCore = g.m_core != nullptr && self == static_cast<void*>(g.m_core);
    if (sameCore && g.m_installing.load() && !g.m_waiting.load()) {
      g.m_suppressedRefreshCalls.fetch_add(1);
      return;
    }
    if (g.m_waiting.load()) {
      const int id = g.m_waitId.load();
      QMetaObject::invokeMethod(&g, "markReadyAcceptable", Qt::QueuedConnection,
                                Q_ARG(int, id));
    }
  }

  g.m_forwardedRefreshCalls.fetch_add(1);
  if (s_originalRefreshDirectory != nullptr) {
    s_originalRefreshDirectory(self);
  }
}

}  // namespace batch
