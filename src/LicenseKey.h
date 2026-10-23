// Rumble - a Fuzzy Audio plug-in.  (c) 2026 Fuzzy Audio LLC. All rights reserved.
// This software is licensed, not sold; use is governed by the End User License
// Agreement (see the EULA PDF in the repository root). Unauthorised copying,
// distribution, or modification is prohibited.

#pragma once

// The RSA PUBLIC key licence keys are checked against: the half of the
// Fuzzy Audio key pair that can only verify keys, never create them, so it
// is safe to ship and to commit. Its private half lives only where keys are
// signed (the website) and must never be committed or built into anything.
//
// This is the same pair Key Detector uses; keys still cannot cross between
// the products because each key names its product (kProductId).
//
// To change pairs: build RumbleKeygen (-DRUMBLE_BUILD_KEYGEN=ON), run
// `RumbleKeygen makepair 1024`, paste the "public:" line below and give the
// "private:" line to the website. Keys signed with the old pair stop working.
// See docs/LICENSING.md.
namespace rumble_licence {

inline constexpr bool kIsTestLicenseKey = false;   // CI blocks releases while true

inline constexpr const char* kPublicKey =
    "5,8598c288e14265057f8aadf9fbdf54184fa5ae012829eddbe4825293ace2cdde"
    "1044ee631478b021ad22b028d9a75dd1dfe145e48fea53ad9d67da19394d4c97"
    "ddfed409ccb63ee14a9a0a43c117fb5af10f67d3adc33b01a2d009cffe38fe30"
    "e2e67b41c7e74ff0cab6e07079b1eca596d7bb22b638c194d4bfe7eb8437f083";

// Must match how the website signs keys: "app" field and fixed machine token.
inline constexpr const char* kProductId = "Rumble";
inline constexpr const char* kActivationToken = "rumble-license-v1";

} // namespace rumble_licence
