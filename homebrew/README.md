# Snow Shot Homebrew tap

Snow Shot supports Apple Silicon and Intel Macs running macOS 15 or later when
the release provides the corresponding package. Snow Shot Mini requires Apple
Silicon. After the first stable cask is published:

```sh
brew update
brew install --cask mg-chao/tap/snow-shot
brew upgrade --cask snow-shot
brew uninstall --cask snow-shot
```

If Homebrew reports that `Casks/snow-shot.rb` is missing and offers only
`snow-shot@beta`, run `brew update` and retry the stable install. This means
the local tap checkout has not received the published stable cask. See the
[macOS Homebrew troubleshooting steps](https://github.com/mg-chao/snow-shot/blob/main/docs-macos-build.md#homebrew-installation)
if it is still missing after the update.

Beta releases use a separate cask:

```sh
brew install --cask mg-chao/tap/snow-shot@beta
brew upgrade --cask snow-shot@beta
```

Both channels install `Snow Shot.app`. Quit the app and uninstall the current
cask before switching channels; uninstall preserves user data and signing state.

Project-signed releases after 1.5.8 keep the same publisher identity across DMG,
script, and Homebrew installation. Homebrew verifies the pinned project signature
and copies the bundle unchanged; it needs no user signing key or Keychain access.
First installation or migration from an ad-hoc/local signature may require privacy
consent again. Use the current installer: historical scripts still re-sign apps.
Self-signing does not provide Apple notarization. Legacy installer state and keys
are left untouched for rollback, and uninstall preserves user data.

For an existing manual installation, quit Snow Shot and move `Snow Shot.app` (or
legacy `snow_shot.app`) out of Applications to a backup folder before installing.
Keep legacy signing state and keys if rollback is needed. Do not use `--force` to overwrite it.
Delete the backup only after confirming the Homebrew installation works.

Use `brew install --cask --appdir="$HOME/Applications" mg-chao/tap/snow-shot` for
another application directory (create it first). Use the same desktop account for
upgrades. Quit Snow Shot and finish recordings before upgrading or uninstalling.

Release and recovery details: [Snow Shot macOS documentation](https://github.com/mg-chao/snow-shot/blob/main/docs-macos-build.md).

The publisher verifies the downloaded DMG against GitHub's asset SHA-256 digest
and the `.sha256` sidecar when present. At least one checksum source is required;
if both exist, both must match. The Homebrew archive always includes a checksum
sidecar for the installer, even when the original release uses only GitHub's digest.
Each architecture has its own reproducible archive. Full casks select the Apple
Silicon or Intel archive automatically when both are present; historical releases
containing only one architecture retain that requirement. Publication refuses to
remove an architecture or omit an edition already present in that release channel.
Keep a GitHub release in draft until all assets have been uploaded and verified;
the Homebrew workflow starts as soon as the release is published. If a partial
upload is rejected, finish uploading and rerun the workflow for the same tag.

Existing archives cannot change at the same version. To repair a cask published
with a missing architecture, manually run **Publish Snow Shot Homebrew cask** with
the same tag and `repair_architectures` enabled. This permits only extending a
single-architecture cask to both architectures when its existing archive checksum,
URL, installer arguments, and all other settings are identical. All archives are
verified before the tap changes. Ordinary publication still rejects same-version
cask changes. See the [repair procedure](https://github.com/mg-chao/snow-shot/blob/main/docs-macos-build.md#repairing-an-incomplete-homebrew-publication).
