#include "../core/AuthoringMutationGate.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <atomic>
#include <iostream>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
constexpr int CrossThreadReaderCount = 8;
constexpr int CrossThreadReadCount = 20000;

bool check(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) &&
        file.write(bytes) == bytes.size();
}

bool runDefaultAndLifetimeTests()
{
    AuthoringMutationGate gate;
    const AuthoringMutationGateSnapshot initial = gate.snapshot();
    bool ok = check(
        !initial.mutationBlocked &&
            initial.activeLeaseCount == 0 &&
            initial.activeMutationLeaseCount == 0 &&
            initial.reason.empty() &&
            !gate.isMutationBlocked() &&
            gate.mutationBlockReason().empty(),
        "gate allows authoring mutation by default");

    {
        auto lease = gate.acquireLease("resource snapshot is active");
        const AuthoringMutationGateSnapshot blocked = gate.snapshot();
        ok = check(
            lease.active() &&
                blocked.mutationBlocked &&
                blocked.activeLeaseCount == 1 &&
                blocked.reason == "resource snapshot is active" &&
                gate.isMutationBlocked(),
            "one live read lease blocks mutation with its reason") && ok;
    }

    const AuthoringMutationGateSnapshot released = gate.snapshot();
    ok = check(
        !released.mutationBlocked &&
            released.activeLeaseCount == 0 &&
            released.reason.empty(),
        "lease destruction restores the default allowing state") && ok;

    {
        auto lease = gate.acquireLease({});
        ok = check(
            lease.active() &&
                !gate.mutationBlockReason().empty(),
            "an empty reason receives a nonempty fallback") && ok;
    }
    return ok;
}

bool runNestedAndMoveTests()
{
    static_assert(std::is_default_constructible_v<
        AuthoringMutationGate::Lease>);
    static_assert(!std::is_copy_constructible_v<
        AuthoringMutationGate::Lease>);
    static_assert(!std::is_copy_assignable_v<
        AuthoringMutationGate::Lease>);
    static_assert(std::is_nothrow_move_constructible_v<
        AuthoringMutationGate::Lease>);
    static_assert(std::is_nothrow_move_assignable_v<
        AuthoringMutationGate::Lease>);

    AuthoringMutationGate gate;
    auto first = gate.acquireLease("first workflow");
    auto second = gate.acquireLease("second workflow");
    bool ok = check(
        gate.snapshot().activeLeaseCount == 2 &&
            gate.mutationBlockReason() == "first workflow",
        "nested read leases retain the earliest reason");

    second.release();
    second.release();
    ok = check(
        !second.active() && gate.snapshot().activeLeaseCount == 1,
        "explicit release is idempotent") && ok;

    AuthoringMutationGate::Lease moved(std::move(first));
    ok = check(
        !first.active() && moved.active() &&
            gate.snapshot().activeLeaseCount == 1,
        "move construction transfers one lease") && ok;

    std::optional<AuthoringMutationGate::Lease> optionalLease;
    optionalLease.emplace(std::move(moved));
    ok = check(
        !moved.active() && optionalLease->active(),
        "lease can be retained in an optional") && ok;
    optionalLease.reset();
    return check(
        !gate.isMutationBlocked(),
        "destroying the final moved lease releases the gate") && ok;
}

bool runMutationExclusionTests()
{
    AuthoringMutationGate gate;
    auto firstMutation = gate.acquireInProcessMutationLease();
    auto secondMutation = gate.acquireMutationLeaseForPath(
        QStringLiteral("script/entry.txt"));
    bool ok = check(
        firstMutation.active() && secondMutation.active() &&
            gate.hasActiveMutations() &&
            gate.snapshot().activeMutationLeaseCount == 2,
        "ordinary authoring mutations may overlap");

    auto rejectedRead = gate.acquireLease("stable read");
    auto rejectedExclusive = gate.acquireExclusiveMutationLease();
    ok = check(
        !rejectedRead.active() && !rejectedExclusive.active(),
        "read and exclusive recovery cannot begin during mutation") && ok;

    firstMutation.release();
    secondMutation.release();
    auto exclusive = gate.acquireExclusiveMutationLease();
    ok = check(
        exclusive.active() &&
            !gate.acquireInProcessMutationLease().active() &&
            !gate.acquireLease("read").active(),
        "exclusive recovery excludes readers and writers") && ok;

    ok = check(
        exclusive.downgradeExclusiveMutationToBlock(
            "recovered snapshot") &&
            gate.isMutationBlocked() &&
            !gate.hasActiveMutations() &&
            gate.mutationBlockReason() == "recovered snapshot",
        "exclusive recovery can become a retained coherent read") && ok;
    auto nestedRead = gate.acquireLease("nested read");
    ok = check(
        nestedRead.active() &&
            !gate.acquireInProcessMutationLease().active(),
        "downgraded and nested reads continue to block mutation") && ok;
    nestedRead.release();
    exclusive.release();
    return check(
        gate.acquireInProcessMutationLease().active(),
        "mutation resumes after coherent reads end") && ok;
}

bool runPathRegistrationTests()
{
    AuthoringMutationGate gate;
    auto empty = gate.acquireMutationLeaseForPath({});
    auto mutation = gate.acquireMutationLeaseForPath(
        QStringLiteral("script/entry.txt"));
    bool ok = check(
        !empty.active() && mutation.active(),
        "path-aware mutation rejects an empty target and accepts a file");
    ok = check(
        mutation.addResourcePath(QStringLiteral("map/map001.map")) &&
            !mutation.addResourcePath({}),
        "one operation may register additional nonempty paths") && ok;
    mutation.release();

    QTemporaryDir temporary;
    ok = check(temporary.isValid(), "temporary directory is available") && ok;
    const QString collection = QDir(temporary.path()).filePath("assets");
    const QString package = QDir(collection).filePath("custom-pack");
    ok = check(writeFile(
            QDir(collection).filePath("resources.ini"),
            "[Collection]\nCommonPath=common\n") &&
        writeFile(
            QDir(package).filePath("game_profile.ini"),
            "[Game]\nId=CUSTOM\n"),
        "resource collection fixture writes") && ok;
    ok = check(
        AuthoringMutationGate::wouldReplaceResourceCollection(collection) &&
            AuthoringMutationGate::wouldReplaceResourceCollection(
                temporary.path()) &&
            !AuthoringMutationGate::wouldReplaceResourceCollection(package) &&
            AuthoringMutationGate::isInsideResource(package) &&
            AuthoringMutationGate::isInsideResource(
                QDir(package).filePath("script/entry.txt")) &&
            !AuthoringMutationGate::isInsideResource(collection),
        "whole-collection replacement stays blocked without classifying packs") && ok;
    return ok;
}

bool runCrossThreadReadTests()
{
    AuthoringMutationGate gate;
    auto stableLease = gate.acquireLease("cross-thread stable reason");
    std::atomic_bool start{false};
    std::atomic_bool failed{false};
    std::vector<std::thread> readers;
    readers.reserve(CrossThreadReaderCount);
    for (int index = 0; index < CrossThreadReaderCount; ++index)
    {
        readers.emplace_back([&gate, &start, &failed]()
        {
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            for (int readIndex = 0;
                 readIndex < CrossThreadReadCount;
                 ++readIndex)
            {
                const auto current = gate.snapshot();
                if (!current.mutationBlocked ||
                    current.activeLeaseCount != 1 ||
                    current.reason != "cross-thread stable reason" ||
                    !gate.isMutationBlocked())
                {
                    failed.store(true, std::memory_order_relaxed);
                    return;
                }
            }
        });
    }
    start.store(true, std::memory_order_release);
    for (std::thread& reader : readers)
        reader.join();

    bool ok = check(
        !failed.load(std::memory_order_relaxed),
        "concurrent readers observe a coherent gate snapshot");
    stableLease.release();
    return check(
        !gate.isMutationBlocked(),
        "gate remains usable after concurrent readers finish") && ok;
}

bool runProcessInstanceTests()
{
    AuthoringMutationGate& first = AuthoringMutationGate::instance();
    AuthoringMutationGate& second = AuthoringMutationGate::instance();
    bool ok = check(
        &first == &second && !first.isMutationBlocked(),
        "instance returns the process-local gate");
    {
        auto lease = first.acquireLease("process-local workflow");
        ok = check(
            second.isMutationBlocked() &&
                second.mutationBlockReason() == "process-local workflow",
            "all callers observe the same live lease") && ok;
    }
    return check(
        !second.isMutationBlocked(),
        "process-local gate is restored after lease destruction") && ok;
}
}

int main()
{
    const bool ok =
        runDefaultAndLifetimeTests() &&
        runNestedAndMoveTests() &&
        runMutationExclusionTests() &&
        runPathRegistrationTests() &&
        runCrossThreadReadTests() &&
        runProcessInstanceTests();
    if (!ok)
        return 1;
    std::cout << "Authoring mutation gate tests passed\n";
    return 0;
}
