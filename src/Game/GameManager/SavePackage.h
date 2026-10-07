#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace SavePackage
{
// 1..7 are manual slots; 8 names rpg_auto only inside the transfer interface.
constexpr int AutomaticSlot = 8;
constexpr int AllSlots = 0;

struct GameIdentity
{
	std::string id;
	std::string name;
	std::string saveNamespace;
	std::string resourceVersion;
	std::string minimumResourceVersion = "1.0.0";
};

struct Slot
{
	int index = 0;
	std::string engineVersion;
	std::string resourceVersion;
};

class Package
{
public:
	const GameIdentity& game() const
	{
		return identity;
	}
	const std::vector<Slot>& slots() const
	{
		return savedSlots;
	}

private:
	struct Entry
	{
		std::string path;
		std::vector<char> bytes;
	};
	GameIdentity identity;
	std::vector<Slot> savedSlots;
	std::vector<Entry> entries;
	friend bool read(const std::string&, Package&, std::string&);
	friend bool importTo(const Package&, const GameIdentity&,
		const std::filesystem::path&, int, std::string&);
};

std::string slotDirectory(int slot);
std::string slotLabel(int slot);
std::vector<int> listSlots(const std::filesystem::path& namespaceRoot);

// Paths selected by SDL may be Android content:// URIs. All external package
// I/O goes through SDL_IOFromFile; namespaceRoot is an application-owned path.
bool write(const std::filesystem::path& namespaceRoot,
	const GameIdentity& game, int slot, const std::string& destination,
	std::string& error);
bool read(const std::string& source, Package& package, std::string& error);
bool compatible(const Package& package, const GameIdentity& game,
	std::string& error);
// targetSlot=AllSlots preserves slot numbers. A single-slot package can be
// assigned to another slot. Only included targets and their thumbnails change.
bool importTo(const Package& package, const GameIdentity& game,
	const std::filesystem::path& namespaceRoot, int targetSlot, std::string& error);
}
