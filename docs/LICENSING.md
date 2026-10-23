# Licensing (offline, paste-only licence keys)

Rumble uses the same licence system as Key Detector: an **offline licence key**
built on JUCE's `juce_product_unlocking`. Your website signs a key with your RSA
**private** key; the plugin checks it against the **public** key built into
it. No server is contacted at runtime — the buyer just **pastes the key** they
were sent.

The implementation is the shared module
[`modules/fuzzyaudio_licensing`](../modules/fuzzyaudio_licensing) (copied from
Key Detector, plus one fix: a locked instance re-checks the saved licence, so
unlocking Rumble in one DAW track or in the standalone app unlocks the others).

| Setting           | Value                    | Where                                   |
|-------------------|--------------------------|-----------------------------------------|
| Product ID (`app`)| `Rumble`                 | `src/LicenseKey.h`                      |
| Activation token  | `rumble-license-v1`      | `src/LicenseKey.h`                      |
| Public key        | `kPublicKey`             | `src/LicenseKey.h`                      |
| Website, store    | `fuzzyaudio.example` (TODO) | `makeLicenseConfig()` in `src/PluginProcessor.cpp` |

**Paste-only:** every key is signed with the fixed token `rumble-license-v1`
as its "machine number", so a key works on any computer and the buyer never
has to look up a machine ID. Enforce "one key per purchase" on your website.

## What the buyer sees

1. They buy on your website, which emails them a key file.
2. They install Rumble and open it. Instead of the controls, the plugin shows
   the **unlock screen**.
3. They paste the key and click **Unlock**. The controls appear, and the
   licence is remembered (saved in the user's settings folder under
   *Fuzzy Audio/Rumble.settings*).

While locked, only the editor is covered: audio still passes, so a session
that opens on an unlicensed machine never goes silent or changes sound.

## ⚠ Before releasing: replace the test key

`src/LicenseKey.h` currently holds a **throwaway test public key**. Its private
half was used to test the unlock flow and then deleted — it was never
committed — so no key anyone can make will unlock a build with it. CI refuses
to publish a tagged release while `kIsTestLicenseKey` is `true`.

Make your real pair **on your own machine** (one-time):

```sh
cmake -B build -DRUMBLE_BUILD_KEYGEN=ON
cmake --build build --config Release --target RumbleKeygen
build/RumbleKeygen_artefacts/RumbleKeygen makepair 1024
#   (on Xcode / Visual Studio builds: build/RumbleKeygen_artefacts/Release/RumbleKeygen)
```

It prints two lines:

- `public:  5,...` → paste into `kPublicKey` in `src/LicenseKey.h` and set
  `kIsTestLicenseKey = false`. Safe to commit: it can only *check* keys.
- `private: ...` → store it **only** on your website (e.g. an environment
  variable / secret). Never commit it, never put it in the plugin. Anyone who
  has it can make working keys.

Use a new pair per product; don't reuse Key Detector's.

## Making keys on your website

Ready-made signers, tested against Rumble's key checker:

- **Node / TypeScript:** `tools/website/generate-licence-keys.ts` (also
  `generate-licence-keys.mjs`, runnable directly)
- **PHP** (needs the `gmp` extension): `tools/website/sign_license.php`

```ts
import { generateKeyFile } from "./generate-licence-keys";
const key = generateKeyFile(
  { app: "Rumble", email: buyerEmail, name: buyerName,
    machine: "rumble-license-v1" },
  process.env.LICENSE_SIGNING_KEY!);   // the "private:" line, server-side only
// email `key` to the buyer
```

```sh
php sign_license.php "Rumble" "jane@example.com" "Jane Doe" \
    "rumble-license-v1" "<PRIVATE_KEY>"
```

Or with the C++ tool built above:

```sh
RumbleKeygen sign Rumble jane@example.com rumble-license-v1 "<PRIVATE_KEY>" "Jane Doe" > key.txt
RumbleKeygen verify Rumble "<PUBLIC_KEY>" rumble-license-v1 key.txt   # -> isUnlocked: YES
```

The key format and the mistakes to avoid (hashing the payload, the wrong XML
shape, PEM keys) are described in `tools/website/README.md`.

## Security notes

- The public key is safe to ship and commit; the private key is not.
- Like any offline licence check this stops casual sharing, not a determined
  cracker. For stronger protection use a wrapped scheme such as PACE/iLok.
