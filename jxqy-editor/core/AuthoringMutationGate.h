#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

class QString;

struct AuthoringMutationGateSnapshot
{
    bool mutationBlocked = false;
    std::size_t activeLeaseCount = 0;
    std::size_t activeMutationLeaseCount = 0;
    std::string reason;
};

// Process-local coordination gate for short authoring reads, writes and
// transaction recovery. It does not persist resource ownership or editing
// sessions and it does not classify resources by their source.
class AuthoringMutationGate final
{
private:
    struct State;

public:
    class Lease final
    {
    public:
        Lease() = default;
        ~Lease();

        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;

        Lease(Lease&& other) noexcept;
        Lease& operator=(Lease&& other) noexcept;

        bool active() const noexcept;
        explicit operator bool() const noexcept;
        bool addResourcePath(const QString& path);
        bool downgradeExclusiveMutationToBlock(std::string reason);
        void release() noexcept;

    private:
        friend class AuthoringMutationGate;

        enum class Kind : std::uint8_t
        {
            None,
            Block,
            Mutation,
            ExclusiveMutation
        };

        Lease(
            std::shared_ptr<State> state,
            std::uint64_t identifier,
            Kind kind);

        std::shared_ptr<State> state;
        std::uint64_t identifier = 0;
        Kind kind = Kind::None;
    };

    AuthoringMutationGate();
    ~AuthoringMutationGate();

    AuthoringMutationGate(const AuthoringMutationGate&) = delete;
    AuthoringMutationGate& operator=(const AuthoringMutationGate&) = delete;
    AuthoringMutationGate(AuthoringMutationGate&&) = delete;
    AuthoringMutationGate& operator=(AuthoringMutationGate&&) = delete;

    static AuthoringMutationGate& instance();
    static bool wouldReplaceResourceCollection(const QString& path);
    static bool isInsideResource(const QString& path);

    [[nodiscard]] Lease acquireLease(std::string reason);
    [[nodiscard]] Lease acquireInProcessMutationLease();
    [[nodiscard]] Lease acquireMutationLeaseForPath(
        const QString& targetPath);
    [[nodiscard]] Lease acquireExclusiveMutationLease();

    bool isMutationBlocked() const;
    bool hasActiveMutations() const;
    std::string mutationBlockReason() const;
    AuthoringMutationGateSnapshot snapshot() const;

private:
    std::shared_ptr<State> state;
};
