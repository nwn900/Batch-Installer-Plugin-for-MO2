/*
Minimal VersionInfo implementation for builds without uibase.lib.
At runtime MO2's own uibase.dll provides the full types; this stub satisfies
the linker for VersionInfo symbols used by the plugin.
*/

#ifndef _NODLL
#define _NODLL
#endif

#include "versioninfo.h"

#include <QStringList>
#include <QVector>
#include <QVersionNumber>

namespace MOBase {

VersionInfo::VersionInfo()
    : m_Scheme(SCHEME_REGULAR), m_Valid(false), m_ReleaseType(RELEASE_FINAL),
      m_Major(0), m_Minor(0), m_SubMinor(0), m_SubSubMinor(0), m_DecimalPositions(0)
{}

VersionInfo::VersionInfo(int major, int minor, int subminor, ReleaseType releaseType)
    : m_Scheme(SCHEME_REGULAR), m_Valid(true), m_ReleaseType(releaseType),
      m_Major(major), m_Minor(minor), m_SubMinor(subminor), m_SubSubMinor(0),
      m_DecimalPositions(0)
{}

VersionInfo::VersionInfo(int major, int minor, int subminor, int subsubminor,
                         ReleaseType releaseType)
    : m_Scheme(SCHEME_REGULAR), m_Valid(true), m_ReleaseType(releaseType),
      m_Major(major), m_Minor(minor), m_SubMinor(subminor), m_SubSubMinor(subsubminor),
      m_DecimalPositions(0)
{}

VersionInfo::VersionInfo(const QString& versionString, VersionScheme scheme)
{
  parse(versionString, scheme, false);
}

VersionInfo::VersionInfo(const QString& versionString, VersionScheme scheme,
                         bool manualInput)
{
  parse(versionString, scheme, manualInput);
}

void VersionInfo::clear()
{
  m_Valid = false;
  m_Major = m_Minor = m_SubMinor = m_SubSubMinor = 0;
  m_DecimalPositions = 0;
  m_Rest.clear();
}

void VersionInfo::parse(const QString& versionString, VersionScheme scheme,
                        bool manualInput)
{
  (void)manualInput;
  m_Scheme = scheme;
  const auto parts = versionString.split(QLatin1Char('.'));
  m_Major = parts.size() > 0 ? parts.at(0).toInt() : 0;
  m_Minor = parts.size() > 1 ? parts.at(1).toInt() : 0;
  m_SubMinor = parts.size() > 2 ? parts.at(2).toInt() : 0;
  m_SubSubMinor = parts.size() > 3 ? parts.at(3).toInt() : 0;
  m_Valid = !versionString.isEmpty();
  m_ReleaseType = RELEASE_FINAL;
}

QString VersionInfo::canonicalString() const
{
  return QStringLiteral("%1.%2.%3")
      .arg(m_Major)
      .arg(m_Minor)
      .arg(m_SubMinor);
}

QString VersionInfo::displayString(int forcedVersionSegments) const
{
  return canonicalString();
}

QVersionNumber VersionInfo::asQVersionNumber() const
{
  QVector<int> segs{m_Major, m_Minor, m_SubMinor};
  if (m_SubSubMinor != 0) {
    segs.append(m_SubSubMinor);
  }
  return QVersionNumber(segs);
}

QString VersionInfo::parseReleaseType(QString versionString)
{
  return versionString;
}

bool operator<(const VersionInfo& lhs, const VersionInfo& rhs)
{
  return QVersionNumber::compare(lhs.asQVersionNumber(), rhs.asQVersionNumber()) < 0;
}

bool operator>(const VersionInfo& lhs, const VersionInfo& rhs) { return rhs < lhs; }
bool operator<=(const VersionInfo& lhs, const VersionInfo& rhs) { return !(rhs < lhs); }
bool operator>=(const VersionInfo& lhs, const VersionInfo& rhs) { return !(lhs < rhs); }
bool operator!=(const VersionInfo& lhs, const VersionInfo& rhs) { return !(lhs == rhs); }

bool operator==(const VersionInfo& lhs, const VersionInfo& rhs)
{
  return QVersionNumber::compare(lhs.asQVersionNumber(), rhs.asQVersionNumber()) == 0;
}

}  // namespace MOBase
