#ifndef IBATCHINSTALLOBSERVER_H
#define IBATCHINSTALLOBSERVER_H

#include <QString>
#include "batchinstalltypes.h"

class IBatchInstallObserver {
public:
    virtual ~IBatchInstallObserver() = default;

    virtual void onBatchStarted(int itemCount) = 0;
    virtual void onItemStarted(int index, const BatchDownloadRef& item) = 0;
    virtual void onItemFinished(int index, const BatchInstallItemResult& result) = 0;
    virtual bool cancelRequested() const = 0;
};

#endif // IBATCHINSTALLOBSERVER_H
