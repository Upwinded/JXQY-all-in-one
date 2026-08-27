#pragma once

#include <string>

namespace MacProgramUpdate
{
// Returns true only when the app bundle contains a valid Ed25519 public key.
// Open-source builds intentionally leave the key unset unless their publisher
// injects an update identity.
bool isConfigured();

// Starts Sparkle's standard user-initiated update check. The appcast URL is
// supplied by the already parsed online catalog and must remain HTTPS.
bool requestUpdateCheck(const std::string& appcastUrl);
}
