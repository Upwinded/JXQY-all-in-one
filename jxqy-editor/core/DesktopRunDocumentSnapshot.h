#pragma once

#include "ProjectDocumentRegistry.h"

#include <QByteArray>
#include <QList>
#include <QString>

// One immutable, side-effect-free view of an open authoring document. A
// dirty document may participate in desktop-run only when serialization is
// supported and bytes contain the exact content that would be written to the
// document's stable project path.
struct DesktopRunDocumentSnapshot
{
    QString filePath;
    // A target-specific private-overlay path for content that has not acquired
    // a durable project path yet. It is never written into the formal resource
    // root and must be a normalized safe virtual resource path.
    QString overlayVirtualPath;
    // Supplemental MAP-owned resources, currently pending MPC payloads, carry
    // the stable absolute MAP path that referenced them when the snapshot was
    // captured. Launch preparation compares this provenance with the locked
    // formal baseline MAP for scene/script targets, or with the explicit
    // captured current-MAP authority for a map target.
    QString ownerMapFilePath;
    ProjectDocumentType type = ProjectDocumentType::Script;
    bool dirty = false;
    // Set only by a target-specific snapshot bundle for resources that must
    // be pinned into the private overlay (for example a pending MPC or a
    // clean companion loaded from runtime save). Ordinary open-document
    // enumeration leaves this false.
    bool includeInOverlay = false;
    bool serializationSupported = false;
    QByteArray bytes;
    QString diagnosticCode;
};

// Sanitizes one map-window bundle for saved-scene/current-script collection.
// Direct documents remain available for exact reference matching, but only a
// pending Image with explicit MAP provenance keeps supplemental overlay
// pinning. Current-map launch deliberately bypasses this helper and pins its
// complete target bundle.
QList<DesktopRunDocumentSnapshot>
genericDesktopRunMapDocumentSnapshots(
    const QList<DesktopRunDocumentSnapshot>& bundleSnapshots);
