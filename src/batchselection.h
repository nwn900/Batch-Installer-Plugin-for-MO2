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

private:
  QString downloadPathForRow(int sourceRow) const;

  QAbstractItemView* m_view;
  MOBase::IOrganizer* m_organizer;
};

#endif
