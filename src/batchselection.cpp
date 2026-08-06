#include "batchselection.h"

#include "imoinfo.h"

#include <QAbstractItemView>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QItemSelectionModel>
#include <QModelIndex>
#include <QSortFilterProxyModel>

#include <algorithm>

namespace {

QString canonicalPath(const QString& path)
{
  return QDir::cleanPath(QDir::fromNativeSeparators(path)).toLower();
}

}  // namespace

BatchSelection::BatchSelection(QAbstractItemView* view,
                               MOBase::IOrganizer* organizer)
    : m_view(view), m_organizer(organizer)
{
}

bool BatchSelection::canCapture() const
{
  return m_view != nullptr && m_organizer != nullptr &&
         m_view->selectionModel() != nullptr;
}

QString BatchSelection::downloadPathForRow(int sourceRow) const
{
  QAbstractItemModel* model = m_view->model();
  if (model == nullptr) {
    return {};
  }

  // Resolve source model: if the view has a proxy, get the real model.
  QAbstractItemModel* source = model;
  if (const auto* proxy = qobject_cast<const QSortFilterProxyModel*>(model)) {
    source = proxy->sourceModel();
  }

  if (source == nullptr) {
    return {};
  }

  const QModelIndex idx = source->index(sourceRow, 0);
  if (!idx.isValid()) {
    return {};
  }

  // Plan A: the model stores the absolute file path in UserRole+1
  // (convention used by MO2 DownloadListModel).
  static constexpr int kPathRole = Qt::UserRole + 1;
  QVariant pathVar = source->data(idx, kPathRole);
  if (pathVar.isValid() && !pathVar.toString().isEmpty()) {
    const QString path = pathVar.toString();
    if (QFileInfo::exists(path)) {
      return QDir::toNativeSeparators(path);
    }
  }

  // Plan B: construct from the downloads directory + display name.
  const QString displayName = source->data(idx, Qt::DisplayRole).toString();
  if (!displayName.isEmpty()) {
    const QString downloadsDir = m_organizer->downloadsPath();
    const QString candidate =
        downloadsDir + QDir::separator() + displayName;
    if (QFileInfo::exists(candidate)) {
      return QDir::toNativeSeparators(candidate);
    }
  }

  return {};
}

QString BatchSelection::pathAt(const QModelIndex& proxyIndex) const
{
  QModelIndex source = proxyIndex;
  if (const auto* proxy =
          qobject_cast<const QSortFilterProxyModel*>(m_view->model())) {
    source = proxy->mapToSource(proxyIndex);
  }
  return downloadPathForRow(source.row());
}

QList<BatchDownloadRef> BatchSelection::capture() const
{
  QList<BatchDownloadRef> refs;

  if (!canCapture()) {
    return refs;
  }

  const QModelIndexList selected = m_view->selectionModel()->selectedRows();

  struct ProxyRow {
    int proxyRow;
    int sourceRow;
  };
  QList<ProxyRow> rows;
  rows.reserve(selected.size());

  for (const QModelIndex& index : selected) {
    QModelIndex sourceIdx = index;
    if (const auto* proxy =
            qobject_cast<const QSortFilterProxyModel*>(m_view->model())) {
      sourceIdx = proxy->mapToSource(index);
    }
    rows.append({index.row(), sourceIdx.row()});
  }

  std::sort(rows.begin(), rows.end(),
            [](const ProxyRow& a, const ProxyRow& b) {
              return a.proxyRow < b.proxyRow;
            });

  QStringList seen;
  for (const ProxyRow& row : rows) {
    const QString path = downloadPathForRow(row.sourceRow);
    const QString key = path.isEmpty() ? QStringLiteral("row:%1").arg(row.sourceRow)
                                       : canonicalPath(path);
    if (seen.contains(key)) {
      continue;
    }
    seen.append(key);

    const QAbstractItemModel* source = m_view->model();
    if (const auto* proxy =
            qobject_cast<const QSortFilterProxyModel*>(m_view->model())) {
      source = proxy->sourceModel();
    }
    const QString displayName =
        source != nullptr
            ? source->index(row.sourceRow, 0).data(Qt::DisplayRole).toString()
            : QString();

    BatchDownloadRef ref;
    ref.canonicalArchivePath = path;
    ref.displayName = displayName.isEmpty()
                          ? QFileInfo(path).fileName()
                          : displayName;
    ref.sourceGame = QString();
    if (!path.isEmpty()) {
      const QFileInfo info(path);
      ref.expectedFileSize = info.size();
      ref.expectedModifiedTimeUtcMs = info.lastModified().toMSecsSinceEpoch();
    }
    ref.modId = 0;
    ref.fileId = 0;
    ref.sourceIndex = row.sourceRow;
    refs.append(ref);
  }

  return refs;
}
