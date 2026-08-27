#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

inline std::string normalizeEditorImageResourceName(std::string name)
{
    std::replace(name.begin(), name.end(), '\\', '/');
    while (!name.empty() && name.front() == '/')
        name.erase(name.begin());
    while (name.rfind("./", 0) == 0)
        name.erase(0, 2);
    if (name.find(':') != std::string::npos)
        return std::string();

    size_t position = 0;
    while (position <= name.size())
    {
        size_t separator = name.find('/', position);
        std::string segment = name.substr(position,
            separator == std::string::npos ? std::string::npos : separator - position);
        if (segment == "..")
            return std::string();
        if (separator == std::string::npos)
            break;
        position = separator + 1;
    }
    return name;
}

inline std::string lowerEditorImageResourceName(std::string name)
{
    std::transform(name.begin(), name.end(), name.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    return name;
}

inline std::string replaceEditorImageExtension(std::string name, const std::string& extension)
{
    size_t slash = name.find_last_of('/');
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
        name.erase(dot);
    return name + extension;
}

inline void appendUniqueEditorImageCandidate(
    std::vector<std::string>& candidates, const std::string& candidate)
{
    if (!candidate.empty() &&
        std::find(candidates.begin(), candidates.end(), candidate) == candidates.end())
    {
        candidates.push_back(candidate);
    }
}

/// Build the same ASF/MPC category fallback used by the runtime for NPC and
/// Object resources, without pulling runtime renderer types into the editor.
inline std::vector<std::string> buildEditorEntityImageCandidates(
    const std::string& imageName, bool isNpc)
{
    std::string normalized = normalizeEditorImageResourceName(imageName);
    if (normalized.empty())
        return {};

    const std::string category = isNpc ? "character/" : "object/";
    const std::string asfFolder = "asf/" + category;
    const std::string mpcFolder = "mpc/" + category;
    std::string lower = lowerEditorImageResourceName(normalized);
    std::vector<std::string> candidates;

    if (lower.rfind("asf/" + category, 0) == 0)
    {
        std::string suffix = normalized.substr(asfFolder.size());
        appendUniqueEditorImageCandidate(candidates, asfFolder + suffix);
        appendUniqueEditorImageCandidate(candidates,
            mpcFolder + replaceEditorImageExtension(suffix, ".mpc"));
        return candidates;
    }
    if (lower.rfind("mpc/" + category, 0) == 0)
    {
        std::string suffix = normalized.substr(mpcFolder.size());
        appendUniqueEditorImageCandidate(candidates, mpcFolder + suffix);
        appendUniqueEditorImageCandidate(candidates,
            asfFolder + replaceEditorImageExtension(suffix, ".asf"));
        return candidates;
    }
    if (lower.rfind(category, 0) == 0)
    {
        normalized = normalized.substr(category.size());
        lower = lowerEditorImageResourceName(normalized);
    }

    const bool asfFirst = lower.size() >= 4 &&
        lower.rfind(".asf") == lower.size() - 4;
    const bool mpcFirst = lower.size() >= 4 &&
        lower.rfind(".mpc") == lower.size() - 4;
    if (mpcFirst)
    {
        appendUniqueEditorImageCandidate(candidates, mpcFolder + normalized);
        appendUniqueEditorImageCandidate(candidates,
            asfFolder + replaceEditorImageExtension(normalized, ".asf"));
    }
    else
    {
        appendUniqueEditorImageCandidate(candidates, asfFolder + normalized);
        if (!asfFirst)
            appendUniqueEditorImageCandidate(candidates, asfFolder + normalized + ".asf");
        appendUniqueEditorImageCandidate(candidates,
            mpcFolder + replaceEditorImageExtension(normalized, ".mpc"));
        if (!asfFirst && !mpcFirst)
            appendUniqueEditorImageCandidate(candidates, mpcFolder + normalized + ".mpc");
    }
    return candidates;
}
