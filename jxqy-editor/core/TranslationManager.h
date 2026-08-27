#pragma once

#include <QObject>
#include <QTranslator>

class QCoreApplication;

class TranslationManager : public QObject
{
    Q_OBJECT

public:
    struct Language
    {
        QString localeName;
        QString nativeName;
    };

    static TranslationManager& instance();

    static QList<Language> supportedLanguages();
    static QString defaultLocaleName();

    void initialize(QCoreApplication& application);
    bool setLanguage(const QString& localeName);

    QString currentLocaleName() const;
    QString lastError() const;
    bool isQtTranslationLoaded() const;
    QString qtTranslationDirectory() const;
    QString qtTranslationError() const;

signals:
    void languageChanged(const QString& localeName);

private:
    TranslationManager() = default;

    bool applyLanguage(const QString& localeName, bool persistSelection);
    QStringList translationSearchPaths() const;
    QStringList qtTranslationSearchPaths() const;
    bool loadApplicationTranslation(const QString& localeName);
    bool loadQtTranslation(const QString& localeName);

    QCoreApplication* application = nullptr;
    QTranslator applicationTranslator;
    QTranslator qtTranslator;
    QString selectedLocaleName = defaultLocaleName();
    QString errorMessage;
    bool qtTranslationLoaded = false;
    QString loadedQtTranslationDirectory;
    QString qtTranslationErrorMessage;
};
