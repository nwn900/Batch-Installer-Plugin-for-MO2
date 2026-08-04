#ifndef BATCHINSTALLTYPES_H
#define BATCHINSTALLTYPES_H

#include <QString>
#include <QList>

struct BatchDownloadRef {
    QString canonicalArchivePath;
    QString displayName;
    QString sourceGame;
    qint64 expectedFileSize = -1;
    qint64 expectedModifiedTimeUtcMs = -1;
    int modId = 0;
    int fileId = 0;

    bool operator==(const BatchDownloadRef& other) const {
        return QString::compare(canonicalArchivePath, other.canonicalArchivePath, Qt::CaseInsensitive) == 0;
    }

    bool operator!=(const BatchDownloadRef& other) const {
        return !(*this == other);
    }
};

enum class BatchInstallItemState {
    Installed,
    Updated,
    Skipped,
    Canceled,
    Failed,
    Invalid
};

struct BatchInstallItemResult {
    BatchDownloadRef download;
    QString installedModName;
    BatchInstallItemState state = BatchInstallItemState::Invalid;
    bool diskChanged = false;
    bool fullRefreshRequired = false;
    QString errorText;
};

struct BatchInstallOptions {
    bool continueAfterCancel = true;
    bool continueAfterFailure = true;
    bool hideInstalledDownloads = true;
    bool verifyFileFacts = true;
};

struct BatchInstallResult {
    QString batchId;
    QList<BatchInstallItemResult> itemResults;
    bool fatalError = false;
    QString errorText;
    int refreshStartCount = 0;
    int refreshCompleteCount = 0;
};

#endif // BATCHINSTALLTYPES_H
