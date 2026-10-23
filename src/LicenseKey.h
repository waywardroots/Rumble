// Rumble - a Fuzzy Audio plug-in.  (c) 2026 Fuzzy Audio LLC. All rights reserved.
// This software is licensed, not sold; use is governed by the End User License
// Agreement (see the EULA PDF in the repository root). Unauthorised copying,
// distribution, or modification is prohibited.

#pragma once

// The RSA PUBLIC key licence keys are checked against. A public key can only
// verify keys, never create them, so it is safe to ship. Its private half
// lives only where keys are signed (your website) and never in this repo.
//
// !! THIS IS A THROWAWAY TEST KEY. !!
// Its private half was discarded after testing, so no real customer key can
// ever match it. Before the first release, generate your own pair:
//
//   cmake -B build -DRUMBLE_BUILD_KEYGEN=ON
//   cmake --build build --target RumbleKeygen
//   build/RumbleKeygen_artefacts/RumbleKeygen makepair 1024   (Release/ on Xcode, VS)
//
// paste the "public:" line below, set kIsTestLicenseKey to false, and keep
// the "private:" line on your server. CI refuses to publish a release while
// kIsTestLicenseKey is true. See docs/LICENSING.md.
namespace rumble_licence {

inline constexpr bool kIsTestLicenseKey = true;

inline constexpr const char* kPublicKey =
    "5,82a92b73cdefa2175614b158bb115d781ab73f8e69802d7b56c764113bd3e6e3"
    "108633b868230887eaa7e933b916e5add5f97fb0c59ef1306c792b177bce44c6"
    "485a80e87096e8986e01a8fdbfb1da9a642a8dee754348aa64b538e36e7c342f"
    "1eb8055dfd65fa88ec1c4edf35d845164be888aa03fb4d77075c0b0649a72637";

// Must match how the website signs keys: "app" field and fixed machine token.
inline constexpr const char* kProductId = "Rumble";
inline constexpr const char* kActivationToken = "rumble-license-v1";

} // namespace rumble_licence
