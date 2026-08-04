/*
Copyright (C) 2026 MO2 Batch Download Installer contributors.

This file is part of the MO2 Batch Download Installer.

MO2 Batch Download Installer is free software: you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.
*/

#ifndef QT_COMPAT_H
#define QT_COMPAT_H

// Small Qt 5 / Qt 6 compatibility header (plan 16.2).
//
// Keep this header small. Put Qt version differences here, never inside the
// batch business rules.

#include <QtGlobal>
#include <QString>

namespace batchcompat {

// Qt 5 removed SkipEmptyParts from QString in Qt 6; the split option moved
// to Qt::SkipEmptyParts.
inline QStringList splitNonEmpty(const QString& text, const QString& sep)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  return text.split(sep, Qt::SkipEmptyParts);
#else
  return text.split(sep, QString::SkipEmptyParts);
#endif
}

}  // namespace batchcompat

#endif  // QT_COMPAT_H
