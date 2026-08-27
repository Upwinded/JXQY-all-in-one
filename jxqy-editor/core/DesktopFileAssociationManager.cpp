#include "DesktopFileAssociationManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <shlobj.h>
#endif

namespace
{
constexpr quint32 registryNone = 0;
constexpr quint32 registryString = 1;
constexpr quint32 registryExpandString = 2;

const QString managedMarkerName = QStringLiteral("JxqyEditorManaged");
const QString backupValueName = QStringLiteral("Snapshot");
const QString registeredApplicationName = QStringLiteral("JXQY Editor");
const QString capabilitiesPath = QStringLiteral(
    "Software\\JXQY\\JXQY Editor\\Capabilities");
const QString capabilitiesAssociationsPath = capabilitiesPath +
    QStringLiteral("\\FileAssociations");
const QString registeredApplicationsPath = QStringLiteral(
    "Software\\RegisteredApplications");
const QString backupRootPath = QStringLiteral(
    "Software\\JXQY\\JXQY Editor\\FileAssociations\\v1");

QString associationMessage(const char* sourceText)
{
    return QCoreApplication::translate(
        "DesktopFileAssociationManager", sourceText);
}

void appendRollbackFailure(QString& error, const QString& rollbackError)
{
    if (rollbackError.isEmpty())
        return;

    error = associationMessage(QT_TRANSLATE_NOOP(
        "DesktopFileAssociationManager", "%1 回滚失败：%2"))
        .arg(error, rollbackError);
}

QString normalizeExtension(QString extension)
{
    extension = extension.trimmed().toLower();
    if (!extension.startsWith(QLatin1Char('.')))
        extension.prepend(QLatin1Char('.'));
    return extension;
}

QString extensionWithoutDot(const QString& extension)
{
    const QString normalized = normalizeExtension(extension);
    return normalized.mid(1);
}

QString normalizedExecutablePath(const QString& executablePath,
                                 QString& error)
{
    const QFileInfo executableInfo(executablePath);
    if (!executableInfo.exists() || !executableInfo.isFile())
    {
        error = associationMessage(QT_TRANSLATE_NOOP(
            "DesktopFileAssociationManager", "可执行文件不存在：%1"))
            .arg(executablePath);
        return {};
    }

    return QDir::toNativeSeparators(executableInfo.absoluteFilePath());
}

struct StoredValue
{
    bool exists = false;
    DesktopRegistryValue value;
};

struct AssociationBackup
{
    bool extensionKeyExisted = false;
    bool openWithKeyExisted = false;
    StoredValue defaultValue;
    StoredValue openWithValue;
};

QJsonObject storedValueToJson(const StoredValue& stored)
{
    QJsonObject object;
    object.insert(QStringLiteral("exists"), stored.exists);
    if (stored.exists)
    {
        object.insert(QStringLiteral("type"),
                      static_cast<qint64>(stored.value.type));
        object.insert(QStringLiteral("dataBase64"),
                      QString::fromLatin1(stored.value.data.toBase64()));
    }
    return object;
}

bool storedValueFromJson(const QJsonValue& json,
                         StoredValue& stored,
                         QString& error)
{
    if (!json.isObject())
    {
        error = associationMessage(QT_TRANSLATE_NOOP(
            "DesktopFileAssociationManager", "已存储的注册表值不是对象。"));
        return false;
    }

    const QJsonObject object = json.toObject();
    if (!object.value(QStringLiteral("exists")).isBool())
    {
        error = associationMessage(QT_TRANSLATE_NOOP(
            "DesktopFileAssociationManager",
            "已存储的注册表值缺少 exists 标志。"));
        return false;
    }

    stored.exists = object.value(QStringLiteral("exists")).toBool();
    if (!stored.exists)
    {
        stored.value = {};
        return true;
    }

    const QJsonValue typeValue = object.value(QStringLiteral("type"));
    const QJsonValue dataValue = object.value(QStringLiteral("dataBase64"));
    if (!typeValue.isDouble() || !dataValue.isString())
    {
        error = associationMessage(QT_TRANSLATE_NOOP(
            "DesktopFileAssociationManager",
            "已存储的注册表值类型或数据无效。"));
        return false;
    }

    const double typeNumber = typeValue.toDouble();
    if (typeNumber < 0 || typeNumber > std::numeric_limits<quint32>::max() ||
        typeNumber != std::floor(typeNumber))
    {
        error = associationMessage(QT_TRANSLATE_NOOP(
            "DesktopFileAssociationManager",
            "已存储的注册表值类型超出范围。"));
        return false;
    }

    const QByteArray encoded = dataValue.toString().toLatin1();
    stored.value.type = static_cast<quint32>(typeNumber);
    stored.value.data = QByteArray::fromBase64(
        encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (!encoded.isEmpty() && stored.value.data.isNull())
    {
        error = associationMessage(QT_TRANSLATE_NOOP(
            "DesktopFileAssociationManager",
            "已存储的注册表值数据不是有效的 Base64。"));
        return false;
    }
    return true;
}

DesktopRegistryValue backupRegistryValue(
    const QString& extension,
    const AssociationBackup& backup)
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("extension"), normalizeExtension(extension));
    root.insert(QStringLiteral("extensionKeyExisted"),
                backup.extensionKeyExisted);
    root.insert(QStringLiteral("openWithKeyExisted"),
                backup.openWithKeyExisted);
    root.insert(QStringLiteral("defaultValue"),
                storedValueToJson(backup.defaultValue));
    root.insert(QStringLiteral("openWithValue"),
                storedValueToJson(backup.openWithValue));
    return DesktopFileAssociationManager::stringRegistryValue(
        QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
}

bool backupFromRegistryValue(const QString& extension,
                             const DesktopRegistryValue& value,
                             AssociationBackup& backup,
                             QString& error)
{
    QString jsonText;
    if (!DesktopFileAssociationManager::registryValueToString(
            value, jsonText))
    {
        error = associationMessage(QT_TRANSLATE_NOOP(
            "DesktopFileAssociationManager", "文件关联备份不是注册表字符串。"));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        jsonText.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError ||
        !document.isObject())
    {
        error = associationMessage(QT_TRANSLATE_NOOP(
            "DesktopFileAssociationManager", "文件关联备份 JSON 无效。"));
        return false;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("version")).toInt(-1) != 1 ||
        normalizeExtension(root.value(QStringLiteral("extension")).toString()) !=
            normalizeExtension(extension) ||
        !root.value(QStringLiteral("extensionKeyExisted")).isBool() ||
        !root.value(QStringLiteral("openWithKeyExisted")).isBool())
    {
        error = associationMessage(QT_TRANSLATE_NOOP(
            "DesktopFileAssociationManager", "文件关联备份头无效。"));
        return false;
    }

    backup.extensionKeyExisted =
        root.value(QStringLiteral("extensionKeyExisted")).toBool();
    backup.openWithKeyExisted =
        root.value(QStringLiteral("openWithKeyExisted")).toBool();
    return storedValueFromJson(root.value(QStringLiteral("defaultValue")),
                               backup.defaultValue, error) &&
        storedValueFromJson(root.value(QStringLiteral("openWithValue")),
                            backup.openWithValue, error);
}

class RegistryTransaction
{
public:
    explicit RegistryTransaction(DesktopRegistryAccess& registryAccess)
        : registry(registryAccess)
    {
    }

    bool writeValue(const QString& path,
                    const QString& valueName,
                    const DesktopRegistryValue& value)
    {
        DesktopRegistryValue current;
        bool currentExists = false;
        if (!registry.readValue(path, valueName, current, currentExists) ||
            !captureValue(path, valueName))
        {
            return fail();
        }
        if (currentExists && current == value)
            return true;
        if (!registry.writeValue(path, valueName, value))
            return fail();
        changedFlag = true;
        return true;
    }

    bool removeValue(const QString& path, const QString& valueName)
    {
        DesktopRegistryValue current;
        bool currentExists = false;
        if (!registry.readValue(path, valueName, current, currentExists) ||
            !captureValue(path, valueName))
        {
            return fail();
        }
        if (!currentExists)
            return true;
        if (!registry.removeValue(path, valueName))
            return fail();
        changedFlag = true;
        return true;
    }

    bool removeKeyIfEmpty(const QString& path)
    {
        if (!captureKey(path))
            return fail();
        bool existedBefore = false;
        if (!registry.keyExists(path, existedBefore))
            return fail();
        if (!registry.removeKeyIfEmpty(path))
            return fail();
        bool existsAfter = false;
        if (!registry.keyExists(path, existsAfter))
            return fail();
        if (existedBefore && !existsAfter)
            changedFlag = true;
        return true;
    }

    bool changed() const
    {
        return changedFlag;
    }

    QString error() const
    {
        return failure;
    }

    bool rollback(QString& rollbackError)
    {
        bool ok = true;
        for (auto iterator = valueOrder.crbegin();
             iterator != valueOrder.crend(); ++iterator)
        {
            const ValueSnapshot snapshot = valueSnapshots.value(*iterator);
            const bool restored = snapshot.existed
                ? registry.writeValue(snapshot.path,
                                      snapshot.valueName,
                                      snapshot.value)
                : registry.removeValue(snapshot.path, snapshot.valueName);
            if (!restored)
                ok = false;
        }

        for (const QString& key : keyOrder)
        {
            if (keySnapshots.value(key))
                ok = registry.ensureKey(keyPaths.value(key)) && ok;
        }

        QList<QString> absentKeys;
        for (const QString& key : keyOrder)
        {
            if (!keySnapshots.value(key))
                absentKeys.append(key);
        }
        std::sort(absentKeys.begin(), absentKeys.end(),
                  [](const QString& left, const QString& right)
                  {
                      return left.count(QLatin1Char('\\')) >
                          right.count(QLatin1Char('\\'));
                  });
        for (const QString& key : absentKeys)
            ok = registry.removeKeyIfEmpty(keyPaths.value(key)) && ok;

        if (!ok)
            rollbackError = registry.lastError();
        return ok;
    }

private:
    struct ValueSnapshot
    {
        QString path;
        QString valueName;
        bool existed = false;
        DesktopRegistryValue value;
    };

    QString valueKey(const QString& path, const QString& valueName) const
    {
        return path.toLower() + QChar(0) + valueName.toLower();
    }

    bool captureKey(const QString& path)
    {
        const int separator = path.lastIndexOf(QLatin1Char('\\'));
        if (separator > 0 && !captureKey(path.left(separator)))
            return false;

        const QString key = path.toLower();
        if (keySnapshots.contains(key))
            return true;
        bool exists = false;
        if (!registry.keyExists(path, exists))
            return false;
        keySnapshots.insert(key, exists);
        keyPaths.insert(key, path);
        keyOrder.append(key);
        return true;
    }

    bool captureValue(const QString& path, const QString& valueName)
    {
        const QString key = valueKey(path, valueName);
        if (valueSnapshots.contains(key))
            return true;
        if (!captureKey(path))
            return false;

        ValueSnapshot snapshot;
        snapshot.path = path;
        snapshot.valueName = valueName;
        if (!registry.readValue(path, valueName,
                                snapshot.value, snapshot.existed))
        {
            return false;
        }
        valueSnapshots.insert(key, snapshot);
        valueOrder.append(key);
        return true;
    }

    bool fail()
    {
        if (failure.isEmpty())
            failure = registry.lastError();
        return false;
    }

    DesktopRegistryAccess& registry;
    QHash<QString, bool> keySnapshots;
    QHash<QString, QString> keyPaths;
    QList<QString> keyOrder;
    QHash<QString, ValueSnapshot> valueSnapshots;
    QList<QString> valueOrder;
    bool changedFlag = false;
    QString failure;
};

bool readStoredValue(DesktopRegistryAccess& registry,
                     const QString& path,
                     const QString& valueName,
                     StoredValue& stored,
                     QString& error)
{
    if (!registry.readValue(path, valueName,
                            stored.value, stored.exists))
    {
        error = registry.lastError();
        return false;
    }
    return true;
}

bool captureAssociationBackup(DesktopRegistryAccess& registry,
                              const DesktopFileAssociationDefinition& definition,
                              AssociationBackup& backup,
                              QString& error)
{
    if (!registry.keyExists(
            DesktopFileAssociationManager::extensionRegistryPath(
                definition.extension),
            backup.extensionKeyExisted) ||
        !registry.keyExists(
            DesktopFileAssociationManager::openWithRegistryPath(
                definition.extension),
            backup.openWithKeyExisted))
    {
        error = registry.lastError();
        return false;
    }

    return readStoredValue(
               registry,
               DesktopFileAssociationManager::extensionRegistryPath(
                   definition.extension),
               QString(), backup.defaultValue, error) &&
        readStoredValue(
            registry,
            DesktopFileAssociationManager::openWithRegistryPath(
                definition.extension),
            definition.programId, backup.openWithValue, error);
}

bool loadAssociationBackup(DesktopRegistryAccess& registry,
                           const DesktopFileAssociationDefinition& definition,
                           AssociationBackup& backup,
                           bool& exists,
                           QString& error)
{
    const QString path = DesktopFileAssociationManager::backupRegistryPath(
        definition.extension);
    bool keyExists = false;
    if (!registry.keyExists(path, keyExists))
    {
        error = registry.lastError();
        return false;
    }

    DesktopRegistryValue value;
    bool valueExists = false;
    if (!registry.readValue(path, backupValueName, value, valueExists))
    {
        error = registry.lastError();
        return false;
    }

    if (!keyExists && !valueExists)
    {
        exists = false;
        return true;
    }
    if (!keyExists || !valueExists)
    {
        error = associationMessage(QT_TRANSLATE_NOOP(
            "DesktopFileAssociationManager", "%1 的文件关联备份键不完整。"))
            .arg(definition.extension);
        return false;
    }

    exists = true;
    if (!backupFromRegistryValue(definition.extension,
                                 value, backup, error))
    {
        error = associationMessage(QT_TRANSLATE_NOOP(
            "DesktopFileAssociationManager", "%1 的文件关联备份无效：%2"))
            .arg(definition.extension, error);
        return false;
    }
    return true;
}

bool readRegistryString(DesktopRegistryAccess& registry,
                        const QString& path,
                        const QString& valueName,
                        QString& text,
                        bool& exists,
                        QString& error)
{
    DesktopRegistryValue value;
    if (!registry.readValue(path, valueName, value, exists))
    {
        error = registry.lastError();
        return false;
    }
    if (!exists)
    {
        text.clear();
        return true;
    }
    if (!DesktopFileAssociationManager::registryValueToString(value, text))
    {
        error = associationMessage(QT_TRANSLATE_NOOP(
            "DesktopFileAssociationManager", "注册表值不是字符串：%1\\%2"))
            .arg(path, valueName);
        return false;
    }
    return true;
}

bool programIdIsOwned(DesktopRegistryAccess& registry,
                      const DesktopFileAssociationDefinition& definition,
                      QString& error)
{
    const QString programPath =
        DesktopFileAssociationManager::programIdRegistryPath(
            definition.extension);
    bool keyExists = false;
    if (!registry.keyExists(programPath, keyExists))
    {
        error = registry.lastError();
        return false;
    }
    if (!keyExists)
        return true;

    QString marker;
    bool markerExists = false;
    if (!readRegistryString(registry, programPath, managedMarkerName,
                            marker, markerExists, error))
    {
        return false;
    }
    if (!markerExists || marker != QStringLiteral("1"))
    {
        error = associationMessage(QT_TRANSLATE_NOOP(
            "DesktopFileAssociationManager",
            "当前用户 ProgID %1 已存在，但不属于 UPEdit-JXQY。"))
            .arg(definition.programId);
        return false;
    }
    return true;
}

bool writeProgramRegistration(
    RegistryTransaction& transaction,
    const DesktopFileAssociationDefinition& definition,
    const QString& executablePath)
{
    const QString programPath =
        DesktopFileAssociationManager::programIdRegistryPath(
            definition.extension);
    return transaction.writeValue(
               programPath, QString(),
               DesktopFileAssociationManager::stringRegistryValue(
                   definition.registryDescription)) &&
        transaction.writeValue(
            programPath, managedMarkerName,
            DesktopFileAssociationManager::stringRegistryValue(
                QStringLiteral("1"))) &&
        transaction.writeValue(
            programPath + QStringLiteral("\\DefaultIcon"), QString(),
            DesktopFileAssociationManager::stringRegistryValue(
                QStringLiteral("\"") + executablePath +
                QStringLiteral("\",0"))) &&
        transaction.writeValue(
            programPath + QStringLiteral("\\shell\\open\\command"),
            QString(),
            DesktopFileAssociationManager::stringRegistryValue(
                DesktopFileAssociationManager::openCommandForExecutable(
                    executablePath))) &&
        transaction.writeValue(
            capabilitiesAssociationsPath, definition.extension,
            DesktopFileAssociationManager::stringRegistryValue(
                definition.programId));
}

bool writeCommonRegistration(RegistryTransaction& transaction)
{
    return transaction.writeValue(
               capabilitiesPath, QStringLiteral("ApplicationName"),
               DesktopFileAssociationManager::stringRegistryValue(
                   QStringLiteral("UPEdit-JXQY"))) &&
        transaction.writeValue(
            capabilitiesPath, QStringLiteral("ApplicationDescription"),
            DesktopFileAssociationManager::stringRegistryValue(
                QStringLiteral("JXQY content editor"))) &&
        transaction.writeValue(
            registeredApplicationsPath, registeredApplicationName,
            DesktopFileAssociationManager::stringRegistryValue(
                capabilitiesPath));
}

bool removeProgramRegistration(
    RegistryTransaction& transaction,
    const DesktopFileAssociationDefinition& definition)
{
    const QString programPath =
        DesktopFileAssociationManager::programIdRegistryPath(
            definition.extension);
    const QString commandPath = programPath +
        QStringLiteral("\\shell\\open\\command");
    const QString openPath = programPath + QStringLiteral("\\shell\\open");
    const QString shellPath = programPath + QStringLiteral("\\shell");
    const QString iconPath = programPath + QStringLiteral("\\DefaultIcon");
    return transaction.removeValue(commandPath, QString()) &&
        transaction.removeKeyIfEmpty(commandPath) &&
        transaction.removeKeyIfEmpty(openPath) &&
        transaction.removeKeyIfEmpty(shellPath) &&
        transaction.removeValue(iconPath, QString()) &&
        transaction.removeKeyIfEmpty(iconPath) &&
        transaction.removeValue(programPath, QString()) &&
        transaction.removeValue(programPath, managedMarkerName) &&
        transaction.removeKeyIfEmpty(programPath) &&
        transaction.removeValue(
            capabilitiesAssociationsPath, definition.extension);
}

bool removeCommonRegistration(RegistryTransaction& transaction)
{
    return transaction.removeValue(
               registeredApplicationsPath, registeredApplicationName) &&
        transaction.removeValue(
            capabilitiesPath, QStringLiteral("ApplicationName")) &&
        transaction.removeValue(
            capabilitiesPath, QStringLiteral("ApplicationDescription")) &&
        transaction.removeKeyIfEmpty(capabilitiesAssociationsPath) &&
        transaction.removeKeyIfEmpty(capabilitiesPath) &&
        transaction.removeKeyIfEmpty(backupRootPath) &&
        transaction.removeKeyIfEmpty(
            QStringLiteral("Software\\JXQY\\JXQY Editor\\FileAssociations")) &&
        transaction.removeKeyIfEmpty(
            QStringLiteral("Software\\JXQY\\JXQY Editor")) &&
        transaction.removeKeyIfEmpty(QStringLiteral("Software\\JXQY"));
}

bool restoreStoredValue(RegistryTransaction& transaction,
                        const QString& path,
                        const QString& valueName,
                        const StoredValue& stored)
{
    return stored.exists
        ? transaction.writeValue(path, valueName, stored.value)
        : transaction.removeValue(path, valueName);
}

bool userChoiceSelectsEditor(
    DesktopRegistryAccess& registry,
    const DesktopFileAssociationDefinition& definition,
    bool& selectsEditor,
    QString& error)
{
    QString programId;
    bool exists = false;
    if (!readRegistryString(
            registry,
            DesktopFileAssociationManager::userChoiceRegistryPath(
                definition.extension),
            QStringLiteral("ProgId"), programId, exists, error))
    {
        return false;
    }
    selectsEditor = exists &&
        programId.compare(definition.programId,
                          Qt::CaseInsensitive) == 0;
    return true;
}

bool restoreManagedAssociation(
    DesktopRegistryAccess& registry,
    RegistryTransaction& transaction,
    const DesktopFileAssociationDefinition& definition,
    const AssociationBackup& backup,
    QString& error)
{
    bool explicitEditorChoice = false;
    if (!userChoiceSelectsEditor(registry, definition,
                                 explicitEditorChoice, error))
    {
        return false;
    }
    if (explicitEditorChoice)
    {
        error = associationMessage(QT_TRANSLATE_NOOP(
            "DesktopFileAssociationManager",
            "Windows 默认应用仍为 %1 选择了 UPEdit-JXQY。"
            "请先在 Windows 设置中选择其他默认应用，再恢复关联。"))
            .arg(definition.extension);
        return false;
    }

    const QString extensionPath =
        DesktopFileAssociationManager::extensionRegistryPath(
            definition.extension);
    QString currentProgramId;
    bool currentProgramIdExists = false;
    if (!readRegistryString(registry, extensionPath, QString(),
                            currentProgramId, currentProgramIdExists,
                            error))
    {
        return false;
    }

    if (currentProgramIdExists &&
        currentProgramId.compare(definition.programId,
                                 Qt::CaseInsensitive) == 0 &&
        !restoreStoredValue(transaction, extensionPath, QString(),
                            backup.defaultValue))
    {
        return false;
    }

    const QString openWithPath =
        DesktopFileAssociationManager::openWithRegistryPath(
            definition.extension);
    if (!restoreStoredValue(transaction, openWithPath,
                            definition.programId,
                            backup.openWithValue) ||
        !transaction.removeValue(
            DesktopFileAssociationManager::backupRegistryPath(
                definition.extension),
            backupValueName) ||
        !transaction.removeKeyIfEmpty(
            DesktopFileAssociationManager::backupRegistryPath(
                definition.extension)) ||
        !removeProgramRegistration(transaction, definition))
    {
        return false;
    }

    if (!backup.openWithKeyExisted &&
        !transaction.removeKeyIfEmpty(openWithPath))
    {
        return false;
    }
    if (!backup.extensionKeyExisted &&
        !transaction.removeKeyIfEmpty(extensionPath))
    {
        return false;
    }
    return true;
}

DesktopFileAssociationResult failedResult(const QString& error)
{
    DesktopFileAssociationResult result;
    result.error = error;
    return result;
}

#ifdef Q_OS_WIN
QString windowsRegistryError(LONG errorCode)
{
    return associationMessage(QT_TRANSLATE_NOOP(
        "DesktopFileAssociationManager", "Windows 注册表错误 %1"))
        .arg(static_cast<qulonglong>(errorCode));
}

class WindowsDesktopRegistryAccess final : public DesktopRegistryAccess
{
public:
    bool isAvailable() const override
    {
        return true;
    }

    bool keyExists(const QString& path, bool& exists) override
    {
        HKEY key = nullptr;
        const LONG result = RegOpenKeyExW(
            HKEY_CURRENT_USER,
            reinterpret_cast<LPCWSTR>(path.utf16()),
            0, KEY_READ, &key);
        if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND)
        {
            exists = false;
            return true;
        }
        if (result != ERROR_SUCCESS)
            return setError(result);
        RegCloseKey(key);
        exists = true;
        return true;
    }

    bool readValue(const QString& path,
                   const QString& valueName,
                   DesktopRegistryValue& value,
                   bool& exists) override
    {
        HKEY key = nullptr;
        LONG result = RegOpenKeyExW(
            HKEY_CURRENT_USER,
            reinterpret_cast<LPCWSTR>(path.utf16()),
            0, KEY_QUERY_VALUE, &key);
        if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND)
        {
            exists = false;
            value = {};
            return true;
        }
        if (result != ERROR_SUCCESS)
            return setError(result);

        DWORD type = 0;
        DWORD size = 0;
        const LPCWSTR name = valueName.isEmpty()
            ? nullptr : reinterpret_cast<LPCWSTR>(valueName.utf16());
        result = RegQueryValueExW(key, name, nullptr, &type,
                                  nullptr, &size);
        if (result == ERROR_FILE_NOT_FOUND)
        {
            RegCloseKey(key);
            exists = false;
            value = {};
            return true;
        }
        if (result != ERROR_SUCCESS)
        {
            RegCloseKey(key);
            return setError(result);
        }

        QByteArray data(static_cast<int>(size), Qt::Uninitialized);
        result = RegQueryValueExW(
            key, name, nullptr, &type,
            size == 0 ? nullptr
                      : reinterpret_cast<LPBYTE>(data.data()),
            &size);
        RegCloseKey(key);
        if (result != ERROR_SUCCESS)
            return setError(result);
        data.resize(static_cast<int>(size));
        value.type = type;
        value.data = data;
        exists = true;
        return true;
    }

    bool writeValue(const QString& path,
                    const QString& valueName,
                    const DesktopRegistryValue& value) override
    {
        HKEY key = nullptr;
        DWORD disposition = 0;
        LONG result = RegCreateKeyExW(
            HKEY_CURRENT_USER,
            reinterpret_cast<LPCWSTR>(path.utf16()),
            0, nullptr, REG_OPTION_NON_VOLATILE,
            KEY_SET_VALUE, nullptr, &key, &disposition);
        if (result != ERROR_SUCCESS)
            return setError(result);
        const LPCWSTR name = valueName.isEmpty()
            ? nullptr : reinterpret_cast<LPCWSTR>(valueName.utf16());
        result = RegSetValueExW(
            key, name, 0, value.type,
            value.data.isEmpty()
                ? nullptr
                : reinterpret_cast<const BYTE*>(value.data.constData()),
            static_cast<DWORD>(value.data.size()));
        RegCloseKey(key);
        return result == ERROR_SUCCESS || setError(result);
    }

    bool removeValue(const QString& path,
                     const QString& valueName) override
    {
        HKEY key = nullptr;
        LONG result = RegOpenKeyExW(
            HKEY_CURRENT_USER,
            reinterpret_cast<LPCWSTR>(path.utf16()),
            0, KEY_SET_VALUE, &key);
        if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND)
            return true;
        if (result != ERROR_SUCCESS)
            return setError(result);
        const LPCWSTR name = valueName.isEmpty()
            ? nullptr : reinterpret_cast<LPCWSTR>(valueName.utf16());
        result = RegDeleteValueW(key, name);
        RegCloseKey(key);
        if (result == ERROR_FILE_NOT_FOUND)
            return true;
        return result == ERROR_SUCCESS || setError(result);
    }

    bool ensureKey(const QString& path) override
    {
        HKEY key = nullptr;
        DWORD disposition = 0;
        const LONG result = RegCreateKeyExW(
            HKEY_CURRENT_USER,
            reinterpret_cast<LPCWSTR>(path.utf16()),
            0, nullptr, REG_OPTION_NON_VOLATILE,
            KEY_SET_VALUE, nullptr, &key, &disposition);
        if (key)
            RegCloseKey(key);
        return result == ERROR_SUCCESS || setError(result);
    }

    bool removeKeyIfEmpty(const QString& path) override
    {
        HKEY key = nullptr;
        LONG result = RegOpenKeyExW(
            HKEY_CURRENT_USER,
            reinterpret_cast<LPCWSTR>(path.utf16()),
            0, KEY_READ, &key);
        if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND)
            return true;
        if (result != ERROR_SUCCESS)
            return setError(result);

        DWORD subKeyCount = 0;
        DWORD valueCount = 0;
        result = RegQueryInfoKeyW(
            key, nullptr, nullptr, nullptr,
            &subKeyCount, nullptr, nullptr,
            &valueCount, nullptr, nullptr, nullptr, nullptr);
        RegCloseKey(key);
        if (result != ERROR_SUCCESS)
            return setError(result);
        if (subKeyCount != 0 || valueCount != 0)
            return true;

        result = RegDeleteKeyW(
            HKEY_CURRENT_USER,
            reinterpret_cast<LPCWSTR>(path.utf16()));
        if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND)
            return true;
        return result == ERROR_SUCCESS || setError(result);
    }

    void notifyAssociationsChanged() override
    {
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST,
                       nullptr, nullptr);
    }

    QString lastError() const override
    {
        return errorText;
    }

private:
    bool setError(LONG errorCode)
    {
        errorText = windowsRegistryError(errorCode);
        return false;
    }

    QString errorText;
};
#else
class WindowsDesktopRegistryAccess final : public DesktopRegistryAccess
{
public:
    bool isAvailable() const override { return false; }
    bool keyExists(const QString&, bool&) override { return unsupported(); }
    bool readValue(const QString&, const QString&,
                   DesktopRegistryValue&, bool&) override
    {
        return unsupported();
    }
    bool writeValue(const QString&, const QString&,
                    const DesktopRegistryValue&) override
    {
        return unsupported();
    }
    bool removeValue(const QString&, const QString&) override
    {
        return unsupported();
    }
    bool ensureKey(const QString&) override { return unsupported(); }
    bool removeKeyIfEmpty(const QString&) override { return unsupported(); }
    void notifyAssociationsChanged() override {}
    QString lastError() const override { return errorText; }

private:
    bool unsupported()
    {
        errorText = associationMessage(QT_TRANSLATE_NOOP(
            "DesktopFileAssociationManager", "桌面文件关联仅在 Windows 上可用。"));
        return false;
    }
    QString errorText;
};
#endif
}

DesktopFileAssociationManager::DesktopFileAssociationManager()
    : ownedRegistry(std::make_unique<WindowsDesktopRegistryAccess>())
    , registry(ownedRegistry.get())
{
}

DesktopFileAssociationManager::DesktopFileAssociationManager(
    DesktopRegistryAccess* registryAccess)
    : registry(registryAccess)
{
}

DesktopFileAssociationManager::~DesktopFileAssociationManager() = default;

bool DesktopFileAssociationManager::isSupported() const
{
    return registry && registry->isAvailable();
}

QList<DesktopFileAssociationDefinition>
DesktopFileAssociationManager::supportedAssociations()
{
    return {
#include "DesktopFileTypesGenerated.inc"
    };
}

QString DesktopFileAssociationManager::programIdForExtension(
    const QString& extension)
{
    const QString normalized = normalizeExtension(extension);
    for (const DesktopFileAssociationDefinition& definition :
         supportedAssociations())
    {
        if (definition.extension == normalized)
            return definition.programId;
    }
    return {};
}

QString DesktopFileAssociationManager::extensionRegistryPath(
    const QString& extension)
{
    return QStringLiteral("Software\\Classes\\") +
        normalizeExtension(extension);
}

QString DesktopFileAssociationManager::openWithRegistryPath(
    const QString& extension)
{
    return extensionRegistryPath(extension) +
        QStringLiteral("\\OpenWithProgids");
}

QString DesktopFileAssociationManager::programIdRegistryPath(
    const QString& extension)
{
    return QStringLiteral("Software\\Classes\\") +
        programIdForExtension(extension);
}

QString DesktopFileAssociationManager::backupRegistryPath(
    const QString& extension)
{
    return backupRootPath + QLatin1Char('\\') +
        extensionWithoutDot(extension);
}

QString DesktopFileAssociationManager::userChoiceRegistryPath(
    const QString& extension)
{
    return QStringLiteral(
               "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\") +
        normalizeExtension(extension) + QStringLiteral("\\UserChoice");
}

QString DesktopFileAssociationManager::capabilitiesRegistryPath()
{
    return capabilitiesPath;
}

QString DesktopFileAssociationManager::registeredApplicationsRegistryPath()
{
    return registeredApplicationsPath;
}

QString DesktopFileAssociationManager::openCommandForExecutable(
    const QString& executablePath)
{
    return QStringLiteral("\"") +
        QDir::toNativeSeparators(executablePath) +
        QStringLiteral("\" \"%1\"");
}

DesktopRegistryValue DesktopFileAssociationManager::stringRegistryValue(
    const QString& value)
{
    DesktopRegistryValue registryValue;
    registryValue.type = registryString;
    registryValue.data.resize((value.size() + 1) *
                              static_cast<int>(sizeof(char16_t)));
    std::memcpy(registryValue.data.data(), value.utf16(),
                value.size() * sizeof(char16_t));
    std::memset(registryValue.data.data() + value.size() * sizeof(char16_t),
                0, sizeof(char16_t));
    return registryValue;
}

bool DesktopFileAssociationManager::registryValueToString(
    const DesktopRegistryValue& value,
    QString& text)
{
    if ((value.type != registryString &&
         value.type != registryExpandString) ||
        value.data.size() % static_cast<int>(sizeof(char16_t)) != 0)
    {
        return false;
    }

    const int characterCount = value.data.size() /
        static_cast<int>(sizeof(char16_t));
    QVector<char16_t> characters(characterCount);
    if (!value.data.isEmpty())
    {
        std::memcpy(characters.data(), value.data.constData(),
                    value.data.size());
    }
    int length = characterCount;
    while (length > 0 && characters[length - 1] == 0)
        --length;
    text = QString::fromUtf16(characters.constData(), length);
    return true;
}

DesktopFileAssociationResult DesktopFileAssociationManager::queryStates(
    const QString& executablePath)
{
    if (!isSupported())
    {
        return failedResult(
            associationMessage(QT_TRANSLATE_NOOP(
                "DesktopFileAssociationManager",
                "桌面文件关联仅在 Windows 上可用。")));
    }

    const QString normalizedExecutable = QDir::toNativeSeparators(
        QFileInfo(executablePath).absoluteFilePath());
    DesktopFileAssociationResult result;

    for (const DesktopFileAssociationDefinition& definition :
         supportedAssociations())
    {
        AssociationBackup backup;
        bool backupExists = false;
        QString error;
        if (!loadAssociationBackup(*registry, definition, backup,
                                   backupExists, error))
        {
            return failedResult(error);
        }

        DesktopFileAssociationState state;
        state.definition = definition;
        state.managed = backupExists;

        QString defaultProgramId;
        bool defaultExists = false;
        if (!readRegistryString(
                *registry, extensionRegistryPath(definition.extension),
                QString(), defaultProgramId, defaultExists, error))
        {
            return failedResult(error);
        }
        state.currentUserFallbackIsEditor = defaultExists &&
            defaultProgramId.compare(definition.programId,
                                     Qt::CaseInsensitive) == 0;

        if (!readRegistryString(
                *registry, userChoiceRegistryPath(definition.extension),
                QStringLiteral("ProgId"), state.userChoiceProgramId,
                state.userChoicePresent, error))
        {
            return failedResult(error);
        }

        if (state.managed)
        {
            QString command;
            bool commandExists = false;
            if (!readRegistryString(
                    *registry,
                    programIdRegistryPath(definition.extension) +
                        QStringLiteral("\\shell\\open\\command"),
                    QString(), command, commandExists, error))
            {
                return failedResult(error);
            }
            state.needsRepair = !commandExists ||
                command != openCommandForExecutable(normalizedExecutable);
        }
        result.states.append(state);
    }

    result.success = true;
    return result;
}

DesktopFileAssociationResult DesktopFileAssociationManager::applySelection(
    const QSet<QString>& selectedExtensions,
    const QString& executablePath)
{
    if (!isSupported())
    {
        return failedResult(
            associationMessage(QT_TRANSLATE_NOOP(
                "DesktopFileAssociationManager",
                "桌面文件关联仅在 Windows 上可用。")));
    }

    QSet<QString> selected;
    for (const QString& extension : selectedExtensions)
        selected.insert(normalizeExtension(extension));

    QHash<QString, DesktopFileAssociationDefinition> definitions;
    for (const DesktopFileAssociationDefinition& definition :
         supportedAssociations())
    {
        definitions.insert(definition.extension, definition);
    }
    for (const QString& extension : selected)
    {
        if (!definitions.contains(extension))
        {
            return failedResult(associationMessage(QT_TRANSLATE_NOOP(
                "DesktopFileAssociationManager",
                "不支持的文件关联扩展名：%1"))
                .arg(extension));
        }
    }

    QString error;
    const QString normalizedExecutable = normalizedExecutablePath(
        executablePath, error);
    if (normalizedExecutable.isEmpty())
        return failedResult(error);

    QHash<QString, AssociationBackup> backups;
    QSet<QString> managed;
    for (const DesktopFileAssociationDefinition& definition :
         supportedAssociations())
    {
        AssociationBackup backup;
        bool exists = false;
        if (!loadAssociationBackup(*registry, definition,
                                   backup, exists, error))
        {
            return failedResult(error);
        }
        if (exists)
        {
            backups.insert(definition.extension, backup);
            managed.insert(definition.extension);
        }
        if (selected.contains(definition.extension) &&
            !programIdIsOwned(*registry, definition, error))
        {
            return failedResult(error);
        }
        if (!selected.contains(definition.extension) && exists)
        {
            bool explicitEditorChoice = false;
            if (!userChoiceSelectsEditor(*registry, definition,
                                         explicitEditorChoice, error))
            {
                return failedResult(error);
            }
            if (explicitEditorChoice)
            {
                return failedResult(associationMessage(QT_TRANSLATE_NOOP(
                    "DesktopFileAssociationManager",
                    "Windows 默认应用仍为 %1 选择了 UPEdit-JXQY。"
                    "请先在 Windows 设置中选择其他默认应用，再恢复关联。"))
                    .arg(definition.extension));
            }
        }
    }

    RegistryTransaction transaction(*registry);
    for (const DesktopFileAssociationDefinition& definition :
         supportedAssociations())
    {
        if (selected.contains(definition.extension))
        {
            AssociationBackup backup;
            if (!managed.contains(definition.extension))
            {
                if (!captureAssociationBackup(*registry, definition,
                                              backup, error) ||
                    !transaction.writeValue(
                        backupRegistryPath(definition.extension),
                        backupValueName,
                        backupRegistryValue(definition.extension, backup)))
                {
                    if (error.isEmpty())
                        error = transaction.error();
                    QString rollbackError;
                    transaction.rollback(rollbackError);
                    appendRollbackFailure(error, rollbackError);
                    return failedResult(error);
                }
                backups.insert(definition.extension, backup);
            }

            if (!writeProgramRegistration(transaction, definition,
                                          normalizedExecutable) ||
                !transaction.writeValue(
                    extensionRegistryPath(definition.extension),
                    QString(), stringRegistryValue(definition.programId)) ||
                !transaction.writeValue(
                    openWithRegistryPath(definition.extension),
                    definition.programId,
                    DesktopRegistryValue{registryNone, QByteArray()}))
            {
                error = transaction.error();
                QString rollbackError;
                transaction.rollback(rollbackError);
                appendRollbackFailure(error, rollbackError);
                return failedResult(error);
            }
        }
        else if (managed.contains(definition.extension))
        {
            if (!restoreManagedAssociation(
                    *registry, transaction, definition,
                    backups.value(definition.extension), error))
            {
                if (error.isEmpty())
                    error = transaction.error();
                QString rollbackError;
                transaction.rollback(rollbackError);
                appendRollbackFailure(error, rollbackError);
                return failedResult(error);
            }
        }
    }

    const bool commonOk = selected.isEmpty()
        ? removeCommonRegistration(transaction)
        : writeCommonRegistration(transaction);
    if (!commonOk)
    {
        error = transaction.error();
        QString rollbackError;
        transaction.rollback(rollbackError);
        appendRollbackFailure(error, rollbackError);
        return failedResult(error);
    }

    DesktopFileAssociationResult result = queryStates(normalizedExecutable);
    if (!result.success)
    {
        QString rollbackError;
        transaction.rollback(rollbackError);
        appendRollbackFailure(result.error, rollbackError);
        return result;
    }
    result.changed = transaction.changed();
    if (result.changed)
        registry->notifyAssociationsChanged();
    return result;
}

DesktopFileAssociationResult DesktopFileAssociationManager::restoreAll(
    const QString& executablePath)
{
    return applySelection({}, executablePath);
}

DesktopFileAssociationResult
DesktopFileAssociationManager::repairManagedAssociations(
    const QString& executablePath)
{
    if (!isSupported())
    {
        return failedResult(
            associationMessage(QT_TRANSLATE_NOOP(
                "DesktopFileAssociationManager",
                "桌面文件关联仅在 Windows 上可用。")));
    }

    QSet<QString> managed;
    QString error;
    for (const DesktopFileAssociationDefinition& definition :
         supportedAssociations())
    {
        AssociationBackup backup;
        bool exists = false;
        if (!loadAssociationBackup(*registry, definition,
                                   backup, exists, error))
        {
            return failedResult(error);
        }
        if (exists)
        {
            managed.insert(definition.extension);
            if (!programIdIsOwned(*registry, definition, error))
                return failedResult(error);
        }
    }

    if (managed.isEmpty())
        return queryStates(executablePath);

    const QString normalizedExecutable = normalizedExecutablePath(
        executablePath, error);
    if (normalizedExecutable.isEmpty())
        return failedResult(error);

    RegistryTransaction transaction(*registry);
    for (const DesktopFileAssociationDefinition& definition :
         supportedAssociations())
    {
        if (managed.contains(definition.extension) &&
            !writeProgramRegistration(transaction, definition,
                                      normalizedExecutable))
        {
            error = transaction.error();
            QString rollbackError;
            transaction.rollback(rollbackError);
            appendRollbackFailure(error, rollbackError);
            return failedResult(error);
        }
    }
    if (!writeCommonRegistration(transaction))
    {
        error = transaction.error();
        QString rollbackError;
        transaction.rollback(rollbackError);
        appendRollbackFailure(error, rollbackError);
        return failedResult(error);
    }

    DesktopFileAssociationResult result = queryStates(normalizedExecutable);
    if (!result.success)
    {
        QString rollbackError;
        transaction.rollback(rollbackError);
        appendRollbackFailure(result.error, rollbackError);
        return result;
    }
    result.changed = transaction.changed();
    if (result.changed)
        registry->notifyAssociationsChanged();
    return result;
}
