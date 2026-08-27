#pragma once

#include <QString>

// A three-phase contract for changing the global assets root across every
// open editor. Preparation may ask the user for a decision, but must not
// modify the document or its current root. Resolution may save the existing
// document and can fail. Commit is called only after every participant has
// prepared and resolved successfully, and must not fail or prompt.
class AssetsPathSwitchParticipant
{
public:
    enum class PathScope
    {
        ActiveContentRoot,
        ResourceCollectionRoot
    };

    enum class Decision
    {
        Ready,
        Save,
        Discard,
        Cancelled
    };

    virtual ~AssetsPathSwitchParticipant() = default;

    virtual PathScope assetsPathScope() const
    {
        return PathScope::ActiveContentRoot;
    }

    virtual Decision prepareAssetsPathSwitch(const QString& path) const = 0;
    virtual bool resolveAssetsPathSwitch(Decision decision) = 0;
    virtual void commitAssetsPathSwitch(const QString& path) = 0;
    virtual QString currentAssetsPath() const = 0;
};
