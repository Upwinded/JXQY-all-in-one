#include "TranslationManager.h"

#include "EditorSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>

namespace
{
constexpr char LanguageSettingsKey[] = "ui/language";
constexpr char TranslationFilePrefix[] = "jxqy-editor_";
}

TranslationManager& TranslationManager::instance()
{
    static TranslationManager manager;
    return manager;
}

QList<TranslationManager::Language> TranslationManager::supportedLanguages()
{
    return {
        {"zh_CN", QString::fromUtf8("简体中文")},
        {"zh_TW", QString::fromUtf8("繁體中文")},
        {"en_US", QStringLiteral("English")}
    };
}

QString TranslationManager::defaultLocaleName()
{
    return QStringLiteral("zh_CN");
}

void TranslationManager::initialize(QCoreApplication& targetApplication)
{
    application = &targetApplication;
    QSettings settings = EditorSettings::create();
    const QString configuredLocale = settings.value(
        LanguageSettingsKey, defaultLocaleName()).toString();
    if (!applyLanguage(configuredLocale, false))
        applyLanguage(defaultLocaleName(), true);
}

bool TranslationManager::setLanguage(const QString& localeName)
{
    return applyLanguage(localeName, true);
}

QString TranslationManager::currentLocaleName() const
{
    return selectedLocaleName;
}

QString TranslationManager::lastError() const
{
    return errorMessage;
}

bool TranslationManager::isQtTranslationLoaded() const
{
    return qtTranslationLoaded;
}

QString TranslationManager::qtTranslationDirectory() const
{
    return loadedQtTranslationDirectory;
}

QString TranslationManager::qtTranslationError() const
{
    return qtTranslationErrorMessage;
}

bool TranslationManager::applyLanguage(const QString& localeName, bool persistSelection)
{
    errorMessage.clear();
    if (!application)
    {
        errorMessage = QStringLiteral("TranslationManager has not been initialized.");
        return false;
    }

    bool supported = false;
    for (const Language& language : supportedLanguages())
    {
        if (language.localeName == localeName)
        {
            supported = true;
            break;
        }
    }
    if (!supported)
    {
        errorMessage = tr("不支持的界面语言：%1").arg(localeName);
        return false;
    }

    application->removeTranslator(&applicationTranslator);
    application->removeTranslator(&qtTranslator);
    qtTranslationLoaded = false;
    loadedQtTranslationDirectory.clear();
    qtTranslationErrorMessage.clear();

    if (localeName != defaultLocaleName() && !loadApplicationTranslation(localeName))
    {
        selectedLocaleName = defaultLocaleName();
        QLocale::setDefault(QLocale(selectedLocaleName));
        if (loadQtTranslation(selectedLocaleName))
            application->installTranslator(&qtTranslator);
        if (persistSelection)
        {
            QSettings settings = EditorSettings::create();
            settings.setValue(LanguageSettingsKey, selectedLocaleName);
            settings.sync();
        }
        emit languageChanged(selectedLocaleName);
        return false;
    }

    selectedLocaleName = localeName;
    QLocale::setDefault(QLocale(localeName));

    if (localeName != defaultLocaleName())
        application->installTranslator(&applicationTranslator);

    if (loadQtTranslation(localeName))
        application->installTranslator(&qtTranslator);

    if (persistSelection)
    {
        QSettings settings = EditorSettings::create();
        settings.setValue(LanguageSettingsKey, localeName);
        settings.sync();
    }
    emit languageChanged(localeName);
    return true;
}

QStringList TranslationManager::translationSearchPaths() const
{
    QStringList paths;
    if (application)
    {
        const QString overridePath = application->property("translationsPath").toString();
        if (!overridePath.isEmpty())
        {
            paths.append(QDir::cleanPath(overridePath));
            if (application->property("translationsPathExclusive").toBool())
                return paths;
        }
        paths.append(QDir(application->applicationDirPath()).filePath("translations"));
    }
    paths.append(QStringLiteral(":/translations"));
    paths.removeDuplicates();
    return paths;
}

QStringList TranslationManager::qtTranslationSearchPaths() const
{
    QStringList paths;
    if (application)
        paths.append(QDir(application->applicationDirPath()).filePath("translations"));
    paths.append(QLibraryInfo::path(QLibraryInfo::TranslationsPath));
    paths.removeAll(QString());
    paths.removeDuplicates();
    return paths;
}

bool TranslationManager::loadApplicationTranslation(const QString& localeName)
{
    const QString fileName = QString::fromLatin1(TranslationFilePrefix) + localeName;
    for (const QString& path : translationSearchPaths())
    {
        if (applicationTranslator.load(fileName, path))
            return true;
    }

    errorMessage = tr("缺少语言包 %1.qm，已安全回退到简体中文。")
        .arg(fileName);
    return false;
}

bool TranslationManager::loadQtTranslation(const QString& localeName)
{
    qtTranslationLoaded = false;
    loadedQtTranslationDirectory.clear();
    qtTranslationErrorMessage.clear();

    const QStringList searchPaths = qtTranslationSearchPaths();
    const QLocale locale(localeName);
    for (const QString& path : searchPaths)
    {
        if (qtTranslator.load(locale, QStringLiteral("qt"), QStringLiteral("_"), path))
        {
            qtTranslationLoaded = true;
            loadedQtTranslationDirectory = QDir(path).absolutePath();
            return true;
        }
    }

    qtTranslationErrorMessage = QStringLiteral(
        "Qt translation catalog for locale %1 was not found in: %2")
        .arg(localeName, searchPaths.join(QStringLiteral("; ")));
    return false;
}
