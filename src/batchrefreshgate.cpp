/*
Copyright (C) 2026 MO2 Batch Download Installer contributors.

Deferred OrganizerCore::refreshDirectoryStructure via in-process MinHook.
Targets verified ModOrganizer.exe builds only (SHA-256 + prologue).
*/

#include "batchrefreshgate.h"

#include <imoinfo.h>

#include <MinHook.h>

#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QtGlobal>
#include <QObject>
#include <QStringList>
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

}  // namespace

void (*BatchRefreshGate::s_originalRefreshDirectory)(void* self) = nullptr;

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
    if (!HexToBytes(p.exeSha256Hex, expected) ||
        expected.size() != digest.size()) {
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
      m_status =
          QStringLiteral("matched %1 SHA256 verified; hook RVA 0x%2")
              .arg(QString::fromLatin1(p.label), QString::number(p.refreshDirectoryRva, 16));
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
    m_status =
        QStringLiteral("prologue mismatch at RVA; hook aborted (binary differs)");
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
  if (s_refreshDirectoryTarget != nullptr) {
    MH_DisableHook(s_refreshDirectoryTarget);
    MH_RemoveHook(s_refreshDirectoryTarget);
    s_refreshDirectoryTarget = nullptr;
  }
  s_originalRefreshDirectory = nullptr;
  if (m_mhInitialized) {
    MH_Uninitialize();
    m_mhInitialized = false;
  }
}

QObject* BatchRefreshGate::findOrganizerCoreObject() const
{
  QCoreApplication* app = QCoreApplication::instance();
  if (app == nullptr) {
    return nullptr;
  }
  const QList<QObject*> objects =
      app->findChildren<QObject*>(QString(), Qt::FindChildrenRecursively);
  for (QObject* o : objects) {
    if (o != nullptr && qstrcmp(o->metaObject()->className(), "OrganizerCore") == 0) {
      return o;
    }
  }
  return nullptr;
}

void BatchRefreshGate::waitForDirectoryStructureReady(QObject* organizerCore,
                                                      int timeoutMs)
{
  if (organizerCore == nullptr) {
    return;
  }
  QEventLoop loop;
  QTimer timer;
  timer.setSingleShot(true);
  QObject::connect(&timer, SIGNAL(timeout()), &loop, SLOT(quit()));
  QObject::connect(organizerCore, SIGNAL(directoryStructureReady()), &loop,
                   SLOT(quit()), Qt::QueuedConnection);
  timer.start(timeoutMs);
  loop.exec();
  QObject::disconnect(organizerCore, SIGNAL(directoryStructureReady()), &loop,
                        SLOT(quit()));
}

bool BatchRefreshGate::initialize(MOBase::IOrganizer* organizer)
{
  m_organizer = organizer;
  if (organizer == nullptr) {
    return false;
  }

  if (!resolveHostModule()) {
    qWarning() << "BatchRefreshGate:" << m_status;
    return false;
  }
  if (!matchProfile()) {
    return false;
  }
  QObject* core = findOrganizerCoreObject();
  if (core == nullptr) {
    m_status =
        QStringLiteral("OrganizerCore QObject not found; deferred refresh disabled");
    qWarning() << "BatchRefreshGate:" << m_status;
    return false;
  }
  if (qstrcmp(core->metaObject()->className(), "OrganizerCore") != 0) {
    m_status = QStringLiteral("unexpected core type; deferred refresh disabled");
    qWarning() << "BatchRefreshGate:" << m_status;
    return false;
  }

  if (!installHook()) {
    return false;
  }
  m_hookOk = true;
  qInfo() << "BatchRefreshGate: hook installed;"
          << "directory refresh will defer during batch installs";
  return true;
}

void BatchRefreshGate::shutdown()
{
  m_batchActive = false;
  removeHook();
  m_hookOk = false;
  m_hookRva = 0;
  m_expectedPrologue = {};
  m_organizer = nullptr;
}

void BatchRefreshGate::enterBatch(MOBase::IOrganizer* organizer)
{
  (void)organizer;
  m_suppressedRefreshCalls = 0;
  m_forwardedRefreshCalls = 0;
  m_batchActive = true;
}

void BatchRefreshGate::leaveBatch(MOBase::IOrganizer* organizer)
{
  m_batchActive = false;
  if (!m_hookOk || organizer == nullptr) {
    return;
  }
  QObject* core = findOrganizerCoreObject();
  if (core == nullptr) {
    qWarning() << "BatchRefreshGate: OrganizerCore missing for final refresh";
    return;
  }
  qInfo() << "BatchRefreshGate: final refresh after batch (suppressed"
          << m_suppressedRefreshCalls.load() << "directory scheduler calls)";
  organizer->refresh(false);
  waitForDirectoryStructureReady(core, 300000);
}

void BatchRefreshGate::logCounters(const char* where) const
{
  qInfo() << "BatchRefreshGate:" << where
          << "suppressed=" << m_suppressedRefreshCalls.load()
          << "forwarded=" << m_forwardedRefreshCalls.load();
}

void BatchRefreshGate::hookRefreshDirectoryStructure(void* self)
{
  auto& g = instance();
  if (g.suppressDirectoryRefresh()) {
    g.m_suppressedRefreshCalls.fetch_add(1);
    return;
  }
  g.m_forwardedRefreshCalls.fetch_add(1);
  if (s_originalRefreshDirectory != nullptr) {
    s_originalRefreshDirectory(self);
  }
}

BatchDirectoryRefreshBatch::BatchDirectoryRefreshBatch(MOBase::IOrganizer* organizer)
    : m_organizer(organizer)
{
  auto& gate = BatchRefreshGate::instance();
  if (!gate.hookOk()) {
    return;
  }

  QObject* core = gate.findOrganizerCoreObject();
  if (core == nullptr) {
    qWarning() << "BatchDirectoryRefreshBatch: OrganizerCore not found";
    return;
  }

  // Preflight: one full refresh path with gate off so no directory refresh is in flight.
  qInfo() << "BatchRefreshGate: preflight refresh before batch";
  organizer->refresh(false);
  gate.waitForDirectoryStructureReady(core, 300000);

  gate.enterBatch(organizer);
  m_effective = true;
}

BatchDirectoryRefreshBatch::~BatchDirectoryRefreshBatch()
{
  if (!m_effective) {
    return;
  }
  BatchRefreshGate::instance().leaveBatch(m_organizer);
  BatchRefreshGate::instance().logCounters("batch end");
}

}  // namespace batch
