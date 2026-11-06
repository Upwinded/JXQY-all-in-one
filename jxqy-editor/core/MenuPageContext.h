#pragma once

#include "../../src/Resource/ResourceManifest.h"
#include <QMap>
#include <QStringList>

// Fixed runtime page entries and the shared catalog's materialized UI roots.
struct MenuPageContext
{
    ResourceManifest manifest;
    QString activeRoot;
    QString collectionRoot;
    QStringList readRoots;
    QStringList contentRoots;
    QStringList recoveryErrors;
    QString problem;
    QMap<QString, QString> providers;
    bool valid = false;
    QString teamInfoText;
    QString teamInfoPath;
    QString teamVideoPath;
    QString teamContentProblem;

    static MenuPageContext resolve(const QString& activePackRoot);
    static QStringList pages();
    QStringList menuPages(const QString& theme) const;
    QString menuPageForRole(const QString& page, const QString& theme) const;
    static bool isUiResource(const QString& resource);
    const QStringList& rootsFor(const QString& resource) const;
    QStringList allReadRoots() const;
    QString readable(const QString& resource) const;
    bool hasComponentData(const QString& resource) const;
    bool qingyu(const QString& theme) const;
    QString themedResource(const QString& resource, bool themed) const;
    bool partnerHeadsAvailable() const;
    bool practiceAvailable() const;
    bool hudTopButtons(const QString& theme) const;
    QString hudColumnHost(const QString& theme) const;
    QString mapThumbnailBaseResource() const;
    QString mapThumbnailLayout(const QString& theme) const;
    bool yesNoFallback(const QString& theme) const;
    QString resource(const QString& page, const QString& theme, bool atTitle = false) const;
    QString source(const QString& page, const QString& theme, bool atTitle = false) const;
    bool canLoad(const QString& page, const QString& theme, bool atTitle = false) const;
};
