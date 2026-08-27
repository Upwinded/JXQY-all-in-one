#include "DesktopRunDocumentSnapshot.h"

#include <utility>

QList<DesktopRunDocumentSnapshot>
genericDesktopRunMapDocumentSnapshots(
    const QList<DesktopRunDocumentSnapshot>& bundleSnapshots)
{
    QList<DesktopRunDocumentSnapshot> snapshots;
    snapshots.reserve(bundleSnapshots.size());
    for (DesktopRunDocumentSnapshot snapshot : bundleSnapshots)
    {
        if (snapshot.type == ProjectDocumentType::Image)
        {
            if (!snapshot.includeInOverlay ||
                snapshot.ownerMapFilePath.trimmed().isEmpty())
            {
                continue;
            }
        }
        else
        {
            // Clean companions loaded from save/game are pinned only for
            // "run current map", not while collecting unrelated open MAP
            // windows for a saved-scene/current-script target.
            snapshot.includeInOverlay = false;
        }
        snapshots.append(std::move(snapshot));
    }
    return snapshots;
}
