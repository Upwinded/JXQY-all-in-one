#pragma once

#include <QVector>

enum class CloseDecision
{
    Ready,
    Save,
    Discard,
    Cancelled
};

struct ClosePlan
{
    QVector<CloseDecision> decisions;

    bool isCancelled() const
    {
        return decisions.contains(CloseDecision::Cancelled);
    }
};

// Coordinates MainWindow shutdown without closing earlier MDI documents before
// later documents have had a chance to cancel. Preparation only gathers user
// choices. Resolution may save and fail. Commit applies approved discard-only
// cleanup and arms the next close event to bypass a second prompt.
class CloseTransactionParticipant
{
public:
    virtual ~CloseTransactionParticipant() = default;

    virtual ClosePlan prepareCloseTransaction() const = 0;
    virtual bool resolveCloseTransaction(const ClosePlan& plan) = 0;
    virtual void commitCloseTransaction(const ClosePlan& plan) = 0;

protected:
    void allowPreparedClose()
    {
        preparedCloseAllowed = true;
    }

    bool consumePreparedClose()
    {
        const bool allowed = preparedCloseAllowed;
        preparedCloseAllowed = false;
        return allowed;
    }

private:
    bool preparedCloseAllowed = false;
};
