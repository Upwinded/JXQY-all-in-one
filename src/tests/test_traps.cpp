#include "../Game/Data/Traps.h"
#include "../Game/GameManager/SaveFileManager.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

namespace
{
bool check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

std::shared_ptr<INIReader> makeReader(const std::string& text)
{
    auto buffer = std::make_unique<char[]>(text.size() + 1);
    std::memcpy(buffer.get(), text.data(), text.size());
    buffer[text.size()] = '\0';
    return std::make_shared<INIReader>(buffer);
}

class ScopedSaveRoot final
{
public:
    ScopedSaveRoot()
        : previousSaveNamespace(File::getActiveSaveNamespace())
    {
        const auto suffix = std::chrono::steady_clock::now().
            time_since_epoch().count();
        root = std::filesystem::temp_directory_path() /
            ("jxqy-trap-test-" + std::to_string(suffix));
        std::error_code error;
        std::filesystem::create_directories(root, error);
        ready = !error;
        if (ready)
        {
            File::setPlatformStateParentForTests(root.u8string());
            File::setActiveSaveNamespace("trap-tests");
        }
    }

    ~ScopedSaveRoot()
    {
        File::setActiveSaveNamespace(previousSaveNamespace);
        File::setPlatformStateParentForTests("");
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    bool valid() const
    {
        return ready;
    }

private:
    std::filesystem::path root;
    std::string previousSaveNamespace;
    bool ready = false;
};
}

int main()
{
    bool ok = true;
    ok = check(!Traps::isValidIndex(-1), "negative trap index is invalid") && ok;
    ok = check(!Traps::isValidIndex(0), "trap index 0 is no trap") && ok;
    ok = check(Traps::isValidIndex(1), "trap index 1 is valid") && ok;
    ok = check(Traps::isValidIndex(19), "trap index 19 is valid") && ok;
    ok = check(Traps::isValidIndex(255),
               "the byte-sized maximum trap index is valid") && ok;
    ok = check(!Traps::isValidIndex(256),
               "trap indices outside the map byte are invalid") && ok;

    std::shared_ptr<INIReader> reader = makeReader(
        "[scene01]\n"
        "0=must-not-survive.txt\n"
        "1=trap01.txt\n");
    Traps traps(reader);

    ok = check(reader->Get("scene01", "0", "").empty(),
               "loading traps removes legacy key 0") && ok;
    ok = check(traps.get("scene01", 0).empty(),
               "runtime never queries a script for index 0") && ok;
    ok = check(traps.get("scene01", 1) == "trap01.txt",
               "runtime queries valid trap index") && ok;

    traps.markTriggered(1);
    ok = check(traps.hasTriggered(1),
               "running a trap records its index as triggered") && ok;
    ok = check(traps.get("scene01", 1) == "trap01.txt",
               "triggering a trap keeps its script definition") && ok;
    traps.reactivate(1);
    ok = check(!traps.hasTriggered(1),
               "setting a current-map trap can reactivate its index") && ok;
    traps.markTriggered(1);
    traps.beginMapVisit();
    ok = check(!traps.hasTriggered(1),
               "entering a map clears the previous map triggered list") && ok;
    ok = check(traps.get("scene01", 1) == "trap01.txt",
               "entering a map does not reload or clear trap definitions") && ok;

    traps.set("scene01", 0, "invalid.txt");
    ok = check(reader->Get("scene01", "0", "").empty(),
               "runtime set ignores index 0") && ok;

    reader->Set("scene01", "0", "legacy-again.txt");
    traps.set("scene01", 2, "trap02.txt");
    ok = check(reader->Get("scene01", "0", "").empty(),
               "saving a valid section edit removes key 0") && ok;
    ok = check(traps.get("scene01", 2) == "trap02.txt",
               "runtime stores valid trap index") && ok;
    ok = check(reader->saveToString().find("0=") == std::string::npos,
               "serialized traps data never contains key 0") && ok;

    traps.set("scene01", 255, "trap255.txt");
    ok = check(traps.get("scene01", 255) == "trap255.txt",
               "runtime stores the byte-sized maximum trap index") && ok;
    traps.set("scene01", 255, "");
    ok = check(traps.get("scene01", 255).empty() &&
                   reader->saveToString().find("255=") == std::string::npos,
               "an empty trap file removes the mapping instead of saving an empty key") && ok;

    traps.set("scene01", 256, "invalid256.txt");
    ok = check(traps.get("scene01", 256).empty(),
               "runtime rejects out-of-range trap index") && ok;

    ScopedSaveRoot saveRoot;
    ok = check(saveRoot.valid(),
               "trap persistence test created an isolated save root") && ok;
    if (!saveRoot.valid())
    {
        return 1;
    }
    const std::string trapDefinitions =
        "[scene01]\n"
        "1=trap01.txt\n"
        "2=trap02.txt\n";
    ok = check(File::writeFileChecked(
                   std::string(SAVE_CURRENT_FOLDER) + TRAPS_INI,
                   trapDefinitions.data(),
                   static_cast<int>(trapDefinitions.size())),
               "trap persistence test wrote definitions without a triggered-index file") && ok;

    Traps loadedTraps;
    loadedTraps.load();
    ok = check(loadedTraps.get("scene01", 1) == "trap01.txt" &&
                   !loadedTraps.hasTriggered(1),
               "an older save without triggered indices loads with every trap active") && ok;
    loadedTraps.markTriggered(2);
    ok = check(loadedTraps.saveDefinitions() &&
                   !File::fileExist(
                       std::string(SAVE_CURRENT_FOLDER) +
                       TRAP_TRIGGERED_INDICES_INI),
               "SaveMapTrap-style persistence writes definitions without prematurely saving visit state") && ok;
    ok = check(loadedTraps.save(),
               "saving writes trap definitions and the triggered-index list") && ok;

    Traps restoredTraps;
    restoredTraps.load();
    ok = check(restoredTraps.hasTriggered(2) &&
                   restoredTraps.get("scene01", 2) == "trap02.txt",
               "loading the same map restores triggered indices without deleting definitions") && ok;
    restoredTraps.beginMapVisit();
    ok = check(!restoredTraps.hasTriggered(2) &&
                   restoredTraps.get("scene01", 2) == "trap02.txt",
               "entering another map clears triggered indices but preserves edited definitions") && ok;

    const std::string malformedTriggeredIndices = "[init\n0=2\n";
    ok = check(File::writeFileChecked(
                   std::string(SAVE_CURRENT_FOLDER) +
                       TRAP_TRIGGERED_INDICES_INI,
                   malformedTriggeredIndices.data(),
                   static_cast<int>(malformedTriggeredIndices.size())),
               "trap persistence test wrote a malformed optional triggered-index file") && ok;
    Traps malformedStateTraps;
    malformedStateTraps.load();
    ok = check(!malformedStateTraps.hasTriggered(2) &&
                   malformedStateTraps.get("scene01", 2) == "trap02.txt",
               "malformed optional triggered indices degrade to an empty list without losing definitions") && ok;

    return ok ? 0 : 1;
}
