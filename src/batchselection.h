#ifndef BATCHSELECTION_H
#define BATCHSELECTION_H

#include "uibase/batchinstalltypes.h"

#include <QDir>
#include <QList>

class QAbstractItemView;

namespace MOBase {
class IOrganizer;
}

class BatchSelection {
public:
  explicit BatchSelection(QAbstractItemView* view,
                          MOBase::IOrganizer* organizer);

  bool canCapture() const;
  QList<BatchDownloadRef> capture() const;
  QString pathAt(const QModelIndex& proxyIndex) const;

  // Current row index for an absolute archive path (stable after list mutations).
  int sourceRowForCanonicalArchive(const QString& nativePath) const;

private:
  QString downloadPathForRow(int sourceRow) const;

  QAbstractItemView* m_view;
  MOBase::IOrganizer* m_organizer;
};

#endif
