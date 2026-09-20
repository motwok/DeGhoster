// Copyright (c) Emmo Emminghaus mo2000 at mo2000 dot de
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

// Per-user autostart, always via HKCU\...\CurrentVersion\Run (never the
// machine-wide HKLM Run key). The installer invokes DeGhoster.exe with
// --register-autostart / --unregister-autostart so that every user gets the
// entry in their own hive (per-machine installs drive this through Active
// Setup, which runs the command once per user at logon).
namespace autostart {

bool registerCurrentUser();

// Returns true even if the value was already absent.
bool unregisterCurrentUser();

// If the command line requests an autostart operation, performs it and returns
// an exit code in *exitCode (0 = success, 1 = failure); the caller should then
// exit without starting the UI. Returns false if no such flag was present.
bool handleCommandLine(int* exitCode);

}  // namespace autostart
