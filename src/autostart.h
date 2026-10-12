#pragma once

// Per-user "launch at login" registration:
// Windows HKCU Run key, macOS LaunchAgent, Linux XDG autostart entry.
namespace AutoStart {
bool isEnabled();
bool setEnabled(bool enabled);
}
