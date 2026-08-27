#include "AuthoringMutationGate.h"

#include "EditorAssetPath.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <system_error>
#include <utility>

namespace
{
constexpr auto DefaultMutationBlockReason =
    "Authoring resource mutation is temporarily blocked";
constexpr char ResourceCollectionManifestName[] = "resources.ini";
constexpr char ResourceProfileName[] = "game_profile.ini";

std::filesystem::path qtFileSystemPath(const QString& path)
{
#if defined(Q_OS_WIN)
    return std::filesystem::path(path.toStdWString());
#else
    const QByteArray bytes = QFile::encodeName(path);
    return std::filesystem::path(
        std::string(
            bytes.constData(),
            static_cast<std::size_t>(bytes.size())));
#endif
}

bool isRegularFileWithoutSymlink(const std::filesystem::path& path)
{
    std::error_code error;
    const std::filesystem::file_status status =
        std::filesystem::symlink_status(path, error);
    return !error && std::filesystem::is_regular_file(status) &&
        !std::filesystem::is_symlink(status);
}

bool canonicalizePathWithMissingTail(
    const QString& absolutePath,
    QString& resolvedPath)
{
    QString probe = absolutePath;
    QStringList missingSegments;
    while (!QFileInfo::exists(probe) &&
           !QFileInfo(probe).isSymLink())
    {
        const QFileInfo probeInfo(probe);
        const QString segment = probeInfo.fileName();
        const QString parent = probeInfo.dir().absolutePath();
        if (!segment.isEmpty())
            missingSegments.prepend(segment);
        if (parent == probe)
            break;
        probe = parent;
    }

    const QString canonicalProbe = QFileInfo(probe).canonicalFilePath();
    if (canonicalProbe.isEmpty())
    {
        resolvedPath.clear();
        return false;
    }
    resolvedPath = canonicalProbe;
    for (const QString& segment : missingSegments)
        resolvedPath = QDir(resolvedPath).filePath(segment);
    resolvedPath = QDir::cleanPath(resolvedPath);
    return true;
}

bool isResourceCollectionOrDirectParent(const std::filesystem::path& path)
{
    if (isRegularFileWithoutSymlink(
            path / ResourceCollectionManifestName))
    {
        return true;
    }
    std::error_code iterationError;
    std::filesystem::directory_iterator iterator(path, iterationError);
    const std::filesystem::directory_iterator end;
    while (!iterationError && iterator != end)
    {
        std::error_code statusError;
        const std::filesystem::file_status status =
            iterator->symlink_status(statusError);
        if (!statusError && std::filesystem::is_directory(status) &&
            !std::filesystem::is_symlink(status) &&
            isRegularFileWithoutSymlink(
                iterator->path() / ResourceCollectionManifestName))
        {
            return true;
        }
        iterator.increment(iterationError);
    }
    return iterationError &&
        iterationError != std::errc::no_such_file_or_directory;
}

bool wouldReplaceResourceCollectionPath(const QString& path)
{
    const std::filesystem::path fileSystemPath =
        qtFileSystemPath(path).lexically_normal();
    if (fileSystemPath.empty())
        return true;
    if (isResourceCollectionOrDirectParent(fileSystemPath))
        return true;
    QString resolvedPath;
    return canonicalizePathWithMissingTail(path, resolvedPath) &&
        isResourceCollectionOrDirectParent(
            qtFileSystemPath(resolvedPath).lexically_normal());
}

bool isInsideResourcePath(const QString& path)
{
    std::filesystem::path current =
        qtFileSystemPath(path).lexically_normal();
    std::error_code statusError;
    const std::filesystem::file_status status =
        std::filesystem::symlink_status(current, statusError);
    if (!statusError && !std::filesystem::is_directory(status))
        current = current.parent_path();
    while (!current.empty())
    {
        if (isRegularFileWithoutSymlink(current / ResourceProfileName))
            return true;
        const std::filesystem::path parent = current.parent_path();
        if (parent.empty() || parent == current)
            break;
        current = parent;
    }
    return false;
}
}

struct AuthoringMutationGate::State
{
    mutable std::mutex mutex;
    std::map<std::uint64_t, std::string> reasons;
    std::set<std::uint64_t> mutationIdentifiers;
    std::uint64_t exclusiveMutationIdentifier = 0;
    std::uint64_t nextIdentifier = 1;
};

AuthoringMutationGate::Lease::Lease(
    std::shared_ptr<State> state,
    std::uint64_t identifier,
    Kind kind)
    : state(std::move(state)),
      identifier(identifier),
      kind(kind)
{
}

AuthoringMutationGate::Lease::~Lease()
{
    release();
}

AuthoringMutationGate::Lease::Lease(Lease&& other) noexcept
    : state(std::move(other.state)),
      identifier(other.identifier),
      kind(other.kind)
{
    other.identifier = 0;
    other.kind = Kind::None;
}

AuthoringMutationGate::Lease&
AuthoringMutationGate::Lease::operator=(Lease&& other) noexcept
{
    if (this == &other)
        return *this;

    release();
    state = std::move(other.state);
    identifier = other.identifier;
    kind = other.kind;
    other.identifier = 0;
    other.kind = Kind::None;
    return *this;
}

bool AuthoringMutationGate::Lease::active() const noexcept
{
    return state != nullptr && identifier != 0;
}

AuthoringMutationGate::Lease::operator bool() const noexcept
{
    return active();
}

bool AuthoringMutationGate::Lease::addResourcePath(const QString& path)
{
    return active() && !path.trimmed().isEmpty();
}

bool AuthoringMutationGate::Lease::downgradeExclusiveMutationToBlock(
    std::string reason)
{
    if (!active() || kind != Kind::ExclusiveMutation)
        return false;
    if (reason.empty())
        reason = DefaultMutationBlockReason;

    const std::lock_guard<std::mutex> lock(state->mutex);
    if (state->exclusiveMutationIdentifier != identifier)
        return false;
    state->exclusiveMutationIdentifier = 0;
    state->reasons.emplace(identifier, std::move(reason));
    kind = Kind::Block;
    return true;
}

void AuthoringMutationGate::Lease::release() noexcept
{
    std::shared_ptr<State> retainedState = std::move(state);
    const std::uint64_t retainedIdentifier = identifier;
    const Kind retainedKind = kind;
    identifier = 0;
    kind = Kind::None;
    if (retainedState == nullptr || retainedIdentifier == 0)
        return;

    const std::lock_guard<std::mutex> lock(retainedState->mutex);
    if (retainedKind == Kind::Mutation)
    {
        retainedState->mutationIdentifiers.erase(retainedIdentifier);
    }
    else if (retainedKind == Kind::ExclusiveMutation)
    {
        if (retainedState->exclusiveMutationIdentifier == retainedIdentifier)
            retainedState->exclusiveMutationIdentifier = 0;
    }
    else if (retainedKind == Kind::Block)
    {
        retainedState->reasons.erase(retainedIdentifier);
    }
}

AuthoringMutationGate::AuthoringMutationGate()
    : state(std::make_shared<State>())
{
}

AuthoringMutationGate::~AuthoringMutationGate() = default;

AuthoringMutationGate& AuthoringMutationGate::instance()
{
    static AuthoringMutationGate gate;
    return gate;
}

bool AuthoringMutationGate::wouldReplaceResourceCollection(
    const QString& path)
{
    if (path.trimmed().isEmpty())
        return true;
    const QString absolutePath =
        EditorAssetPath::normalizedAbsolutePath(path);
    if (wouldReplaceResourceCollectionPath(absolutePath))
        return true;
    QString resolvedPath;
    if (!canonicalizePathWithMissingTail(absolutePath, resolvedPath))
        return true;
    return wouldReplaceResourceCollectionPath(resolvedPath);
}

bool AuthoringMutationGate::isInsideResource(const QString& path)
{
    if (path.trimmed().isEmpty())
        return false;
    const QString absolutePath =
        EditorAssetPath::normalizedAbsolutePath(path);
    if (isInsideResourcePath(absolutePath))
        return true;
    QString resolvedPath;
    return canonicalizePathWithMissingTail(absolutePath, resolvedPath) &&
        isInsideResourcePath(resolvedPath);
}

AuthoringMutationGate::Lease AuthoringMutationGate::acquireLease(
    std::string reason)
{
    if (reason.empty())
        reason = DefaultMutationBlockReason;

    std::uint64_t identifier = 0;
    {
        const std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->mutationIdentifiers.empty() ||
            state->exclusiveMutationIdentifier != 0)
        {
            return {};
        }
        do
        {
            identifier = state->nextIdentifier++;
        }
        while (identifier == 0 ||
               state->reasons.find(identifier) != state->reasons.end() ||
               state->mutationIdentifiers.find(identifier) !=
                   state->mutationIdentifiers.end());
        state->reasons.emplace(identifier, std::move(reason));
    }
    return Lease(state, identifier, Lease::Kind::Block);
}

AuthoringMutationGate::Lease
AuthoringMutationGate::acquireInProcessMutationLease()
{
    std::uint64_t identifier = 0;
    {
        const std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->reasons.empty() ||
            state->exclusiveMutationIdentifier != 0)
        {
            return {};
        }
        do
        {
            identifier = state->nextIdentifier++;
        }
        while (identifier == 0 ||
               state->reasons.find(identifier) != state->reasons.end() ||
               state->mutationIdentifiers.find(identifier) !=
                   state->mutationIdentifiers.end());
        state->mutationIdentifiers.insert(identifier);
    }
    return Lease(state, identifier, Lease::Kind::Mutation);
}

AuthoringMutationGate::Lease
AuthoringMutationGate::acquireMutationLeaseForPath(
    const QString& targetPath)
{
    Lease lease = acquireInProcessMutationLease();
    if (!lease.addResourcePath(targetPath))
    {
        lease.release();
        return {};
    }
    return lease;
}

AuthoringMutationGate::Lease
AuthoringMutationGate::acquireExclusiveMutationLease()
{
    std::uint64_t identifier = 0;
    {
        const std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->reasons.empty() ||
            !state->mutationIdentifiers.empty() ||
            state->exclusiveMutationIdentifier != 0)
        {
            return {};
        }
        do
        {
            identifier = state->nextIdentifier++;
        }
        while (identifier == 0 ||
               state->reasons.find(identifier) != state->reasons.end() ||
               state->mutationIdentifiers.find(identifier) !=
                   state->mutationIdentifiers.end());
        state->exclusiveMutationIdentifier = identifier;
    }
    return Lease(state, identifier, Lease::Kind::ExclusiveMutation);
}

bool AuthoringMutationGate::isMutationBlocked() const
{
    const std::lock_guard<std::mutex> lock(state->mutex);
    return !state->reasons.empty();
}

bool AuthoringMutationGate::hasActiveMutations() const
{
    const std::lock_guard<std::mutex> lock(state->mutex);
    return !state->mutationIdentifiers.empty() ||
        state->exclusiveMutationIdentifier != 0;
}

std::string AuthoringMutationGate::mutationBlockReason() const
{
    const std::lock_guard<std::mutex> lock(state->mutex);
    if (state->reasons.empty())
        return {};
    return state->reasons.begin()->second;
}

AuthoringMutationGateSnapshot AuthoringMutationGate::snapshot() const
{
    AuthoringMutationGateSnapshot result;
    const std::lock_guard<std::mutex> lock(state->mutex);
    result.mutationBlocked = !state->reasons.empty();
    result.activeLeaseCount = state->reasons.size();
    result.activeMutationLeaseCount =
        state->mutationIdentifiers.size() +
        (state->exclusiveMutationIdentifier != 0 ? 1U : 0U);
    if (result.mutationBlocked)
        result.reason = state->reasons.begin()->second;
    return result;
}
