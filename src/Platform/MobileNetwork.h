#pragma once

namespace MobileNetwork
{
enum class ConnectionCost
{
	Unknown,
	Unmetered,
	Metered
};

// Returns whether the active mobile connection is metered. Desktop platforms
// and platforms that cannot determine the current transport return Unknown.
ConnectionCost getActiveConnectionCost();
}
