#include "AssetCliRunner.h"

#include "JxAssetMigrator.h"
#include "LuaScriptSyntaxValidator.h"
#include "GameProfile.h"
#include "OnlineResourcePackageExporter.h"
#include "OnlineUpdateCatalogPublisher.h"

#include <QDir>
#include <QFileInfo>
#include <QList>
#include <QSet>
#include <QTextStream>

#include <optional>

namespace
{
int migrationExitCode(MigrationResult result)
{
    return static_cast<int>(result);
}

QString normalizeCommand(QString command)
{
    if (command == "--migrate-assets")
        return "migrate-assets";
    if (command == "--validate-scripts")
        return "validate-scripts";
    if (command == "--publish-update-catalog")
        return "publish-update-catalog";
    if (command == "--export-common-package")
        return "export-common-package";
    if (command == "--export-resource-package")
        return "export-resource-package";
    return command;
}

void writeMigrationStatus(QTextStream& out, MigrationResult result)
{
    switch (result)
    {
    case MigrationResult::Success: out << "Success (0)\n"; break;
    case MigrationResult::Partial: out << "Partial (1)\n"; break;
    case MigrationResult::Failed: out << "Failed (2)\n"; break;
    }
}

QList<QString> discoverResourcePackDirectories(const QString& assetsDir)
{
    QList<QString> packDirectories;
    for (const ResourcePackInfo& pack : ResourcePackScanner::scanPacks(assetsDir))
    {
        if (!packDirectories.contains(pack.rootPath))
            packDirectories.append(pack.rootPath);
    }
    return packDirectories;
}

void appendReport(LuaScriptSyntaxReport& total, const LuaScriptSyntaxReport& part)
{
    total.totalFiles += part.totalFiles;
    total.checkedFiles += part.checkedFiles;
    total.skippedFiles += part.skippedFiles;
    total.failedFiles += part.failedFiles;
    total.cancelled = total.cancelled || part.cancelled;
    total.issues.append(part.issues);
}

void writeScriptSyntaxReport(QTextStream& out, const LuaScriptSyntaxReport& report)
{
    out << "Total script directory files: " << report.totalFiles << "\n";
    out << "Checked scripts: " << report.checkedFiles << "\n";
    out << "Skipped files: " << report.skippedFiles << "\n";
    out << "Failures: " << report.failedFiles << "\n";
    if (report.scriptRootMissing)
        out << "Warning: script directory not found\n";
    if (!report.issues.isEmpty())
    {
        out << "\nScript syntax errors:\n";
        for (const LuaScriptSyntaxIssue& issue : report.issues)
            out << "  " << issue.toString() << "\n";
    }
}

bool readOptionValue(const QStringList& arguments, int& index, QTextStream& err, QString& value)
{
    if (index + 1 >= arguments.size() || arguments[index + 1].startsWith("--"))
    {
        err << "Error: Missing value for option: " << arguments[index] << "\n\n";
        return false;
    }
    index++;
    value = arguments[index];
    return true;
}

std::optional<bool> parseBooleanOptionValue(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("true") ||
        normalized == QStringLiteral("1"))
    {
        return true;
    }
    if (normalized == QStringLiteral("false") ||
        normalized == QStringLiteral("0"))
    {
        return false;
    }
    return std::nullopt;
}

// migrate-assets accepts value-less flags and key/value options. Unknown
// options are rejected with a usage error so callers do not silently get a
// standalone pack when they expected a mod profile.
bool applyMigrateOptions(const QStringList& arguments, int startIndex,
    AssetMigrationOptions& options, QTextStream& err)
{
    bool convertAllImages = false;
    bool convertCharacterImages = false;
    bool convertEffectImages = false;
    bool convertObjectImages = false;
    bool noCropTransparentAlias = false;
    bool scriptsOnlyAlias = false;
    bool imagesOnlyAlias = false;
    bool hasExplicitResourceTypes = false;
    QList<AssetResourceType> explicitResourceTypes;
    QList<LegacyImageCategory> explicitImageCategories;
    QList<LegacyImageMode> explicitImageModes;
    std::optional<bool> explicitCropTransparent;
    QSet<QString> knownBooleanFlags;
    knownBooleanFlags << "--no-crop-transparent" << "--no-mod-profile" << "--scripts-only" << "--images-only"
        << "--convert-images" << "--convert-character-images"
        << "--convert-effect-images" << "--convert-object-images";

    QSet<QString> knownValueFlags;
    knownValueFlags << "--mod-id" << "--mod-name" << "--mod-type"
        << "--dependency-id"
        << "--ui-base" << "--ui-profile" << "--feature"
        << "--save-namespace" << "--include-prefix" << "--source-encoding"
        << "--minimum-magic-damage"
        << "--resource-type" << "--image-category" << "--image-mode"
        << "--crop-transparent";

    for (int i = startIndex; i < arguments.size(); i++)
    {
        QString arg = arguments[i];

        if (!arg.startsWith("--"))
        {
            err << "Error: Unexpected argument: " << arg << "\n\n";
            return false;
        }

        if (!knownBooleanFlags.contains(arg))
        {
            if (!knownValueFlags.contains(arg))
            {
                err << "Error: Unknown option: " << arg << "\n\n";
                return false;
            }

            QString value;
            if (!readOptionValue(arguments, i, err, value))
                return false;

            if (arg == "--mod-id")
                options.modId = value;
            else if (arg == "--mod-name")
                options.modName = value;
            else if (arg == "--mod-type")
            {
                bool ok = false;
                int type = value.toInt(&ok);
                if (!ok || type < 0 || type > 3)
                {
                    err << "Error: --mod-type requires an integer from 0 to 3\n\n";
                    return false;
                }
                options.modType = type;
            }
            else if (arg == "--dependency-id")
                options.dependencyId = value;
            else if (arg == "--ui-base")
                options.uiBaseId = value;
            else if (arg == "--ui-profile")
                options.uiProfile = value;
            else if (arg == "--feature")
            {
                const qsizetype separator = value.indexOf('=');
                if (separator <= 0)
                {
                    err << "Error: --feature requires Name=0 or Name=1\n\n";
                    return false;
                }
                const QString featureName = value.left(separator).trimmed();
                const QString featureValue = value.mid(separator + 1).trimmed().toLower();
                if (featureName.isEmpty() ||
                    (featureValue != "0" && featureValue != "1" &&
                     featureValue != "false" && featureValue != "true"))
                {
                    err << "Error: --feature requires Name=0 or Name=1\n\n";
                    return false;
                }
                options.features.insert(featureName, featureValue == "1" || featureValue == "true");
            }
            else if (arg == "--save-namespace")
                options.saveNamespace = value;
            else if (arg == "--minimum-magic-damage")
            {
                bool ok = false;
                const int minimumDamage = value.toInt(&ok);
                if (!ok || minimumDamage < 0)
                {
                    err << "Error: --minimum-magic-damage requires a non-negative integer\n\n";
                    return false;
                }
                options.minimumMagicDamage = minimumDamage;
                options.minimumMagicDamageDefined = true;
            }
            else if (arg == "--include-prefix")
                options.includePrefix = value;
            else if (arg == "--source-encoding")
            {
                QString encoding = value.trimmed().toLower();
                if (encoding != "gbk" && encoding != "utf8")
                {
                    err << "Error: --source-encoding requires gbk or utf8\n\n";
                    return false;
                }
                options.sourceEncoding = encoding;
            }
            else if (arg == "--resource-type")
            {
                AssetResourceType resourceType = AssetResourceType::All;
                if (!parseAssetResourceType(value, resourceType))
                {
                    err << "Error: --resource-type requires all, scripts, maps, images, or audio\n\n";
                    return false;
                }
                hasExplicitResourceTypes = true;
                if (!explicitResourceTypes.contains(resourceType))
                    explicitResourceTypes.append(resourceType);
            }
            else if (arg == "--image-category")
            {
                const std::optional<LegacyImageCategory> category =
                    LegacyImageMigrationPolicy::categoryFromId(value);
                if (!category)
                {
                    QStringList ids;
                    for (const LegacyImageCategoryDefinition& item :
                         LegacyImageMigrationPolicy::definitions())
                    {
                        ids.append(item.id);
                    }
                    err << "Error: --image-category requires one of: "
                        << ids.join(", ") << "\n\n";
                    return false;
                }
                explicitImageCategories.append(*category);
            }
            else if (arg == "--image-mode")
            {
                const std::optional<LegacyImageMode> mode =
                    LegacyImageMigrationPolicy::modeFromId(
                        value.trimmed().toLower());
                if (!mode)
                {
                    err << "Error: --image-mode requires convert, preserve, or exclude\n\n";
                    return false;
                }
                explicitImageModes.append(*mode);
            }
            else if (arg == "--crop-transparent")
            {
                const std::optional<bool> enabled =
                    parseBooleanOptionValue(value);
                if (!enabled)
                {
                    err << "Error: --crop-transparent requires true, false, 1, or 0\n\n";
                    return false;
                }
                if (explicitCropTransparent.has_value() &&
                    *explicitCropTransparent != *enabled)
                {
                    err << "Error: conflicting repeated --crop-transparent values\n\n";
                    return false;
                }
                explicitCropTransparent = *enabled;
            }

            continue;
        }

        if (arg == "--no-crop-transparent")
            noCropTransparentAlias = true;
        else if (arg == "--no-mod-profile")
            options.writeModProfile = false;
        else if (arg == "--scripts-only")
            scriptsOnlyAlias = true;
        else if (arg == "--images-only")
            imagesOnlyAlias = true;
        else if (arg == "--convert-images")
            convertAllImages = true;
        else if (arg == "--convert-character-images")
            convertCharacterImages = true;
        else if (arg == "--convert-effect-images")
            convertEffectImages = true;
        else if (arg == "--convert-object-images")
            convertObjectImages = true;
    }

    const bool hasLegacyImageSelection = convertAllImages ||
        convertCharacterImages || convertEffectImages || convertObjectImages;
    const bool hasNewImageOverrides = !explicitImageCategories.isEmpty() ||
        !explicitImageModes.isEmpty();
    if (explicitImageCategories.size() != explicitImageModes.size())
    {
        err << "Error: --image-category and --image-mode must be supplied the same number of times\n\n";
        return false;
    }
    if (hasNewImageOverrides && hasLegacyImageSelection)
    {
        err << "Error: --image-category/--image-mode cannot be combined with legacy image conversion flags\n\n";
        return false;
    }
    if (explicitCropTransparent.has_value() && noCropTransparentAlias)
    {
        err << "Error: --crop-transparent cannot be combined with --no-crop-transparent\n\n";
        return false;
    }

    // Legacy compatibility mapping remains order-independent. The broad flag
    // wins over narrow flags; one or more narrow flags preserve every other
    // convertible category before enabling their selected main categories.
    if (convertAllImages)
    {
        options.legacyImages.setAllConvertible(LegacyImageMode::Convert);
    }
    else if (convertCharacterImages || convertEffectImages ||
             convertObjectImages)
    {
        options.legacyImages.setAllConvertible(LegacyImageMode::Preserve);
        if (convertCharacterImages)
            options.legacyImages.setMode(
                LegacyImageCategory::Character, LegacyImageMode::Convert);
        if (convertEffectImages)
            options.legacyImages.setMode(
                LegacyImageCategory::Effect, LegacyImageMode::Convert);
        if (convertObjectImages)
            options.legacyImages.setMode(
                LegacyImageCategory::Object, LegacyImageMode::Convert);
    }

    for (qsizetype i = 0; i < explicitImageCategories.size(); i++)
    {
        bool duplicate = false;
        for (qsizetype previous = 0; previous < i; previous++)
        {
            if (explicitImageCategories[previous] !=
                explicitImageCategories[i])
            {
                continue;
            }
            if (explicitImageModes[previous] != explicitImageModes[i])
            {
                const QString categoryId =
                    LegacyImageMigrationPolicy::definition(
                        explicitImageCategories[i]).id;
                err << "Error: conflicting --image-mode values for --image-category "
                    << categoryId << "\n\n";
                return false;
            }
            duplicate = true;
        }
        if (duplicate)
            continue;

        if (!options.legacyImages.setMode(
                explicitImageCategories[i], explicitImageModes[i]))
        {
            const QString categoryId =
                LegacyImageMigrationPolicy::definition(
                    explicitImageCategories[i]).id;
            const QString modeId = LegacyImageMigrationPolicy::modeId(
                explicitImageModes[i]);
            err << "Error: --image-mode " << modeId
                << " is not allowed for --image-category "
                << categoryId << "\n\n";
            return false;
        }
    }
    if (explicitCropTransparent.has_value())
        options.legacyImages.setCropTransparent(*explicitCropTransparent);
    else if (noCropTransparentAlias)
        options.legacyImages.setCropTransparent(false);

    if (scriptsOnlyAlias && imagesOnlyAlias)
    {
        err << "Error: --scripts-only and --images-only cannot be used together\n\n";
        return false;
    }
    if (hasExplicitResourceTypes && (scriptsOnlyAlias || imagesOnlyAlias))
    {
        err << "Error: --resource-type cannot be combined with --scripts-only or --images-only\n\n";
        return false;
    }
    if (explicitResourceTypes.contains(AssetResourceType::All) &&
        explicitResourceTypes.size() > 1)
    {
        err << "Error: --resource-type all cannot be combined with a specific resource type\n\n";
        return false;
    }
    if (!options.includePrefix.isEmpty() && !imagesOnlyAlias)
    {
        err << "Error: --include-prefix is only supported with --images-only\n\n";
        return false;
    }
    if (imagesOnlyAlias && options.includePrefix.trimmed().isEmpty())
    {
        err << "Error: --images-only requires --include-prefix <relativePath>\n\n";
        return false;
    }
    if (scriptsOnlyAlias)
        options.resourceTypes = {AssetResourceType::Scripts};
    else if (imagesOnlyAlias)
        options.resourceTypes = {AssetResourceType::Images};
    else if (hasExplicitResourceTypes)
        options.resourceTypes = explicitResourceTypes;
    const bool hasAnyImagePolicyOption = hasLegacyImageSelection ||
        hasNewImageOverrides || explicitCropTransparent.has_value() ||
        noCropTransparentAlias;
    const bool selectsImages =
        options.resourceTypes.contains(AssetResourceType::All) ||
        options.resourceTypes.contains(AssetResourceType::Images);
    if (hasAnyImagePolicyOption && !selectsImages)
    {
        err << "Error: image policy options require --resource-type all or images\n\n";
        return false;
    }
    if (!options.uiProfile.trimmed().isEmpty())
    {
        const QString profile = options.uiProfile.trimmed().toLower();
        if (profile != "jxqy2" && profile != "yycs" && profile != "xjxqy")
        {
            err << "Error: --ui-profile requires JXQY2, YYCS, or XJXQY\n\n";
            return false;
        }
    }

    return true;
}
}

bool AssetCliRunner::shouldHandle(const QStringList& arguments)
{
    if (arguments.size() < 2)
        return false;

    QString command = normalizeCommand(arguments.value(1));
    return command == "migrate-assets" ||
        command == "validate-scripts" ||
        command == "export-resource-package" ||
        command == "export-common-package" ||
        command == "publish-update-catalog" ||
        command == "--help" ||
        command == "-h";
}

int AssetCliRunner::run(const QStringList& arguments, FILE* stdoutFile, FILE* stderrFile)
{
    QString appName = arguments.value(0);

    if (arguments.size() < 2)
        return printUsage(appName, stdoutFile, 2);

    QString command = normalizeCommand(arguments.value(1));

    if (command == "--help" || command == "-h")
        return printUsage(appName, stdoutFile, 0);

    QTextStream out(stdoutFile);
    QTextStream err(stderrFile);

    if (command == "migrate-assets")
    {
        if (arguments.size() < 4)
        {
            err << "Error: migrate-assets requires <sourceDir> and <outputDir>\n\n";
            return printUsage(appName, stdoutFile, 2);
        }

        QString sourceDir = arguments.value(2);
        QString outputDir = arguments.value(3);

        AssetMigrationOptions options;
        if (!applyMigrateOptions(arguments, 4, options, err))
            return printUsage(appName, stdoutFile, 2);

        if (!QDir(sourceDir).exists())
        {
            err << "Error: Source directory does not exist: " << sourceDir << "\n";
            return 2;
        }

        AssetMigrationReport report;
        JxAssetMigrator migrator;
        MigrationResult result = migrator.migrate(sourceDir, outputDir, options, report,
            [&](const QString& message)
            {
                out << message << "\n";
                out.flush();
            });

        out << "\n--- Migration Summary ---\n";
        out << "Resource types: " << report.selectedResourceTypes.join(", ") << "\n";
        out << "Complete project: " << (report.completeProject ? "yes" : "no") << "\n";
        out << "Resource domains:\n";
        for (AssetResourceType domain : assetResourceDomainTypes())
        {
            const QString id = assetResourceTypeId(domain);
            const AssetResourceDomainReport domainReport =
                report.resourceDomains.value(id);
            out << "  " << id
                << ": selected=" << (domainReport.selected ? "yes" : "no")
                << ", processed=" << domainReport.processedFiles
                << ", written=" << domainReport.writtenFiles
                << ", failed=" << domainReport.failedFiles << "\n";
        }
        QStringList imageModes;
        for (const LegacyImageCategoryDefinition& item :
             LegacyImageMigrationPolicy::definitions())
        {
            imageModes.append(QStringLiteral("%1=%2")
                .arg(item.id, report.legacyImageModes.value(item.id)));
        }
        out << "Legacy image modes: " << imageModes.join(", ") << "\n";
        out << "Crop transparent: requested="
            << (report.cropTransparentRequested ? "true" : "false")
            << ", effective="
            << (report.cropTransparentEffective ? "true" : "false") << "\n";
        out << "Processed files: " << report.processedFiles << "\n";
        out << "Written files: " << report.writtenFiles << "\n";
        out << "Dependency duplicate files: "
            << report.dependencyDuplicateFiles << "\n";
        out << "Dependency duplicate bytes: "
            << report.dependencyDuplicateBytes << "\n";
        out << "Warnings: " << report.warningCount << "\n";
        out << "Errors: " << report.errorCount << "\n";
        out << "Unsupported script APIs: " << report.unsupportedScriptApis.size() << "\n";
        out << "Unhandled script statements: " << report.unhandledScriptStatements.size() << "\n";
        out << "Script syntax total files: " << report.scriptSyntaxTotalFiles << "\n";
        out << "Script syntax checked files: " << report.scriptSyntaxCheckedFiles << "\n";
        out << "Script syntax skipped files: " << report.scriptSyntaxSkippedFiles << "\n";
        out << "Script syntax errors: " << report.scriptSyntaxErrors.size() << "\n";
        if (!report.reportFilePath.isEmpty())
            out << "Report: " << report.reportFilePath << "\n";
        else
            out << "Report: <write failed>\n";
        out << "Status: ";
        writeMigrationStatus(out, result);
        out.flush();

        return migrationExitCode(result);
    }

    if (command == "validate-scripts")
    {
        if (arguments.size() != 3)
        {
            err << "Error: validate-scripts requires <assetsDir>\n\n";
            return printUsage(appName, stdoutFile, 2);
        }

        QString assetsDir = arguments.value(2);
        if (!QDir(assetsDir).exists())
        {
            err << "Error: Assets directory does not exist: " << assetsDir << "\n";
            return 2;
        }

        QDir assetsRoot(assetsDir);
        LuaScriptSyntaxReport report;
        QList<QString> packDirectories;
        QList<LuaScriptSyntaxReport> packReports;
        if (assetsRoot.exists("script"))
        {
            report = LuaScriptSyntaxValidator::validateAssetsScripts(assetsDir);
        }
        else
        {
            packDirectories = discoverResourcePackDirectories(assetsDir);
            if (packDirectories.isEmpty())
            {
                report = LuaScriptSyntaxValidator::validateAssetsScripts(assetsDir);
            }
            else
            {
                for (const QString& packDir : packDirectories)
                {
                    LuaScriptSyntaxReport packReport =
                        LuaScriptSyntaxValidator::validateAssetsScripts(packDir);
                    appendReport(report, packReport);
                    packReports.append(packReport);
                }
            }
        }

        out << "--- Script Syntax Validation ---\n";
        out << "Assets: " << QDir(assetsDir).absolutePath() << "\n";
        if (!packDirectories.isEmpty())
        {
            out << "Resource packs: " << packDirectories.size() << "\n";
            for (qsizetype index = 0; index < packDirectories.size(); index++)
            {
                const QString& packDir = packDirectories[index];
                const LuaScriptSyntaxReport& packReport = packReports[index];
                out << "  " << QFileInfo(packDir).fileName()
                    << ": total " << packReport.totalFiles
                    << ", checked " << packReport.checkedFiles
                    << ", skipped " << packReport.skippedFiles
                    << ", failures " << packReport.failedFiles;
                if (packReport.scriptRootMissing)
                    out << ", script directory not found";
                out << "\n";
            }
        }
        writeScriptSyntaxReport(out, report);
        out << "Status: " << (report.hasErrors() ? "Failed" : "Success") << "\n";
        out.flush();

        return report.hasErrors() ? 1 : 0;
    }

    if (command == "publish-update-catalog")
    {
        if (arguments.size() != 5)
        {
            err << "Error: publish-update-catalog requires "
                << "<templateCatalog> <artifactRoot> <outputCatalog>\n\n";
            return printUsage(appName, stdoutFile, 2);
        }

        const OnlineUpdateCatalogPublisher::Result result =
            OnlineUpdateCatalogPublisher::publish(
                arguments.value(2),
                arguments.value(3),
                arguments.value(4));
        if (!result.succeeded())
        {
            err << "Error: update catalog publishing failed (status "
                << static_cast<int>(result.status) << ")";
            if (!result.detail.isEmpty())
            {
                err << ": " << result.detail;
            }
            err << "\n";
            return 1;
        }
        out << "Published catalog: " << result.catalogPath << "\n";
        out << "Artifacts: " << result.artifactCount << "\n";
        if (!result.detail.isEmpty())
        {
            out << "Warning: " << result.detail << "\n";
        }
        out.flush();
        return 0;
    }

    if (command == "export-resource-package")
    {
        if (arguments.size() != 4)
        {
            err << "Error: export-resource-package requires "
                << "<resourceDir> <outputZip>\n\n";
            return printUsage(appName, stdoutFile, 2);
        }

        const OnlineResourcePackageExporter::Result result =
            OnlineResourcePackageExporter::exportPackage(
                arguments.value(2),
                arguments.value(3));
        if (!result.succeeded())
        {
            err << "Error: resource package export failed (status "
                << static_cast<int>(result.status) << ")";
            if (!result.errorPath.isEmpty())
                err << ": " << result.errorPath;
            err << "\n";
            return 1;
        }
        out << "Published resource package: " << arguments.value(3) << "\n";
        out << "Catalog fragment: " << result.catalogPath << "\n";
        out << "Files: " << result.fileCount << "\n";
        out << "Archive bytes: " << result.archiveSize << "\n";
        out << "CRC32: " << result.crc32Hex << "\n";
        out.flush();
        return 0;
    }

    if (command == "export-common-package")
    {
        if (arguments.size() != 5)
        {
            err << "Error: export-common-package requires "
                << "<commonDir> <version> <outputZip>\n\n";
            return printUsage(appName, stdoutFile, 2);
        }

        const OnlineResourcePackageExporter::Result result =
            OnlineResourcePackageExporter::exportCommonPackage(
                arguments.value(2),
                arguments.value(3),
                arguments.value(4));
        if (!result.succeeded())
        {
            err << "Error: common package export failed (status "
                << static_cast<int>(result.status) << ")";
            if (!result.errorPath.isEmpty())
                err << ": " << result.errorPath;
            err << "\n";
            return 1;
        }
        out << "Published common package: " << arguments.value(4) << "\n";
        out << "Catalog fragment: " << result.catalogPath << "\n";
        out << "Files: " << result.fileCount << "\n";
        out << "Archive bytes: " << result.archiveSize << "\n";
        out << "CRC32: " << result.crc32Hex << "\n";
        out.flush();
        return 0;
    }

    err << "Error: Unknown command: " << arguments.value(1) << "\n\n";
    return printUsage(appName, stdoutFile, 2);
}

int AssetCliRunner::printUsage(const QString& appName, FILE* outputFile, int exitCode)
{
    QTextStream out(outputFile);
    out << "Usage:\n";
    out << "  " << appName << " --help\n";
    out << "  " << appName << " migrate-assets <sourceDir> <outputDir> [options]\n";
    out << "  " << appName << " --migrate-assets <sourceDir> <outputDir> [options]\n";
    out << "  " << appName << " validate-scripts <assetsDir>\n";
    out << "  " << appName << " --validate-scripts <assetsDir>\n";
    out << "  " << appName << " export-resource-package <resourceDir> <outputZip>\n";
    out << "  " << appName << " export-common-package <commonDir> <version> <outputZip>\n";
    out << "  " << appName << " publish-update-catalog <templateCatalog> <artifactRoot> <outputCatalog>\n";
    out << "\n";
    out << "Commands:\n";
    out << "  migrate-assets    Migrate legacy JX assets to C++ runtime format\n";
    out << "  validate-scripts  Check converted script syntax with the embedded Lua parser\n";
    out << "  export-resource-package  Export one resource package as ZIP plus a catalog fragment\n";
    out << "  export-common-package  Export common as ZIP plus a [Common] catalog fragment\n";
    out << "  publish-update-catalog  Hash artifacts and write one canonical catalog\n";
    out << "\n";
    out << "Options:\n";
    out << "  --image-category <id>  Override a legacy image category; may be repeated\n";
    out << "                         IDs: character, effect, object, ui, goods, magic,\n";
    out << "                         portrait, interlude, map, unknown\n";
    out << "  --image-mode <mode>    Pair by occurrence with --image-category\n";
    out << "                         Modes: convert or preserve; locked categories reject unsupported modes\n";
    out << "  --crop-transparent <b> Request transparent crop: true, false, 1, or 0\n";
    out << "  --no-crop-transparent  Legacy alias that disables eligible cropping\n";
    out << "  --no-mod-profile       Do not write game_profile.ini for the converted mod\n";
    out << "  --resource-type <type> Migrate all, scripts, maps, images, or audio\n";
    out << "                         Repeat for multiple domains; defaults to all\n";
    out << "  --scripts-only         Legacy alias for --resource-type scripts\n";
    out << "  --images-only          Legacy image-subtree mode; requires --include-prefix\n";
    out << "  --convert-images       Legacy: convert every eligible category to IMP/IMG\n";
    out << "  --convert-character-images  Legacy: convert only character images\n";
    out << "  --convert-effect-images     Legacy: convert only effect images\n";
    out << "  --convert-object-images     Legacy: convert only object images\n";
    out << "                         Without a flag, character/effect/object convert by default\n";
    out << "                         map and unknown are always preserved\n";
    out << "                         UI/goods/magic/portrait/interlude are never cropped\n";
    out << "  --include-prefix <p>   Limit --images-only to a relative source subdirectory\n";
    out << "  --mod-id <id>          Resource pack id; defaults to the output folder name\n";
    out << "  --mod-name <name>      Display name; defaults to --mod-id\n";
    out << "  --mod-type <0..3>      Explicit Game.Type; omitted by default so the content base is inherited\n";
    out << "  --dependency-id <ids>  Ordered content bases; defaults to JXQY2 and accepts comma-separated IDs\n";
    out << "  --minimum-magic-damage <n>  Resource-wide magic damage floor; defaults to the content base\n";
    out << "  --ui-base <id>         UI-only fallback pack id; independent from content base\n";
    out << "  --ui-profile <name>    Layout family: JXQY2, YYCS, or XJXQY\n";
    out << "  --feature <Name=0|1>   Override one Game.Type feature; may be repeated\n";
    out << "  --save-namespace <ns>  Save namespace; defaults to --mod-id\n";
    out << "  --source-encoding <e>  Source text encoding: gbk or utf8; defaults to gbk\n";
    out << "\n";
    out << "Exit codes:\n";
    out << "  0  Success — migration completed, no errors recorded\n";
    out << "  1  Partial — migration completed with warnings\n";
    out << "  2  Failed or usage error\n";
    out.flush();
    return exitCode;
}
