# Building Snow Shot on macOS

The app presets target macOS 15 or newer, with separate Apple Silicon (`arm64`)
and Intel (`x64`) builds. Each build uses one architecture throughout CMake,
vcpkg and Cargo; universal builds are not supported by these presets.

Apple Silicon OCR builds enable the `gpu` Cargo feature and compile embedded
Metal shaders. Install Xcode's Metal Toolchain after selecting the developer
directory and completing Xcode first-launch setup:

```sh
xcodebuild -downloadComponent MetalToolchain
xcrun metal --version
```

Deployed apps do not require Xcode. Hardware acceleration defaults off and uses
CPU while device discovery is pending or unavailable. Intel builds remain
CPU-only; the preference does not enable resident OCR or model hot start.

## Snow Shot Mini

Apple Silicon presets build both `snow_shot` and `snow_shot_mini` from the same
configuration. Intel presets build Snow Shot only; Snow Shot Mini requires
`arm64` on macOS. `SNOW_APPS_BUILD_SNOW_SHOT_MINI` controls the additional target
and defaults to `ON` in the Apple Silicon presets. Use an explicit `--target`
to build one edition, or configure with this option `OFF` for a full-only build.
The build wrappers select the enabled editions automatically. Paired CMake build
presets retain both target names, so a direct build with Mini disabled must use
`cmake --build --preset build-<preset> --target snow_shot`. Apple Silicon release
packaging requires both editions and rejects a Mini-disabled cache.

Mini removes QR, table, Markdown, HTML, LaTeX, translation, Extended Features,
and API Configuration. Manual text recognition remains available; its toolbar
button is hidden by default, and Pin to Screen automatic recognition is off.
Both macOS bundles include the local OCR worker and runtime. Full also bundles
the default Small V6 model; Mini downloads the selected model on first use and
reuses its verified cache afterward.

```sh
scripts/build.sh snow-shot-macos-arm64-debug --target snow_shot_mini
scripts/run-snow-shot.sh snow-shot-macos-arm64-debug --edition mini
scripts/package-snow-shot.sh snow-shot-macos-arm64-release
bash install-snow-shot-macos.sh --edition mini --lang en
```

The Apple Silicon package command produces both
`snow-shot-<version>-macos-arm64.dmg` and
`snow-shot-mini-<version>-macos-arm64.dmg`, each with a `.sha256` sidecar.
Mini installs as `Snow Shot Mini.app`, with executable `snow_shot_mini` and
bundle ID `com.snowshot.snow_shot_mini`. Its settings, launch-at-login entry,
MCP discovery, and installer state use separate product identities. For direct
staging, use `cmake --install build/<preset> --component SnowShotMini --prefix
/path/to/staging`. Both editions share a version and release tag.

## In-app updates

Full and Mini use Sparkle 2.10.1 with the existing Qt Settings and About pages.
macOS offers Manual, Check automatically, and Auto-update on next launch. The
last option is the default on both platforms. On macOS, upgrading a configuration
from schema version 6 or earlier migrates every update mode to `next_launch` once,
including Manual and Check automatically; importing an older configuration follows
the same rule. Schema version 7 records completion, so subsequent user changes and
current configuration imports retain their selected mode. Windows preserves saved
preferences and retains all four options, its updater, and its startup installation gate.

On macOS, automatic updates download and verify in the background, then wait
for the application process to quit normally. Closing the main window while
Snow Shot remains in the menu bar does not install an update. The installer
replaces the application after quit, so the next launch runs the new version.
Changing the setting after preparation leaves that prepared installation intact.
Only the explicit **Restart & update** action requests an immediate restart,
through the existing application admission and editor checks. Missing updates
and failed checks do not delay startup. Sparkle owns its native networking and
scheduling; background downloads without native byte callbacks show indeterminate
progress, while user-initiated downloads expose byte progress and cancellation.

`scripts/bootstrap-macos.sh` provisions the checksum-pinned Sparkle archive from
`snow_shot/packaging/macos/sparkle.json`. The framework, installer, XPC services,
and upstream license are embedded in both bundles, including static Qt builds.
Packaging signs the nested Sparkle components before sealing the application.
The dedicated `snow-shot-sparkle` Keychain account holds the Ed25519 signing key;
only the public key is checked in. `scripts/snow-shot-sparkle.py check-key` checks
publisher readiness. Release commands never generate a replacement key. Provision
the same private key securely on another publishing Mac using Sparkle's key tools;
do not run key initialization to replace the pinned key.

Packaging signs and verifies each final DMG and both appcasts, writing an adjacent
`.sparkle.json` receipt. Remote packaging transfers this receipt bound to the DMG's
SHA-256. After the release assets are uploaded and verified, the release publisher
updates the `sparkle-appcasts` branch on GitHub and Gitee, preserving the other
product/architecture feeds. The three feeds are `snow-shot-macos-arm64.xml`,
`snow-shot-macos-x86_64.xml`, and `snow-shot-mini-macos-arm64.xml`. Each host's
signed feed points to that host's DMG. A transport failure permits one mirror
retry; signature, extraction, and installation failures do not bypass validation
by falling back. Both signed appcasts and pre-extraction archive verification are
required. Feed publication rejects equal or older build replacements.

Publish the first signed feeds with the first Sparkle-enabled release; this
implementation does not publish releases or modify remote branches by itself.
Versions predating Sparkle need a manual upgrade once. Bundle versions must
increase for each offered build, including prereleases.

Focused validation (no production feeds, applications, or signing keys are used
by the native fixture):

```sh
cmake --build build/snow-shot-macos-arm64-debug --target snow-shot-sparkle-fixture
python3 scripts/test-snow-shot-sparkle.py
python3 scripts/test-snow-shot-sparkle-native.py
ctest --test-dir build/snow-shot-macos-arm64-debug --output-on-failure \
  -R '^snow-shot-(update-adapter|update-settings|settings-catalog|configuration-schema|about-update|mcp-application)-tests$'
```

The native fixture uses disposable signed bundles, ephemeral test keys, and a
loopback feed to exercise normal quit, closing a window, preference changes,
manual download/restart, mirror recovery, and signature failures. Before shipping,
also qualify installed Full and Mini DMGs on the target hardware with the release
signing identity and verify continuity of macOS permissions across replacement.

## Homebrew installation

The project tap is `mg-chao/homebrew-tap`. Stable releases update `snow-shot`;
beta releases update `snow-shot@beta`, independently of the stable channel.
Tags accept `v<version>` with an optional `_snow-shot` suffix; beta versions use
`-beta` or `-beta.<number>`. Other prerelease versions are not supported.
Homebrew installation supports Apple Silicon and Intel on macOS 15 or later
when the matching release includes that architecture. Historical releases may
contain only Apple Silicon packages. Snow Shot Mini remains Apple Silicon-only.
Keep Homebrew current with `brew update`.

```sh
brew update
brew install --cask mg-chao/tap/snow-shot
brew upgrade --cask snow-shot
brew uninstall --cask snow-shot
```

For beta releases, use `brew install --cask mg-chao/tap/snow-shot@beta` and
`brew upgrade --cask snow-shot@beta`. Quit the app and uninstall the current cask
before switching channels because both install `Snow Shot.app`. User data and
the signing identity are retained. Each release needs its versioned DMG and a
GitHub SHA-256 digest or matching `.sha256` asset. When both are present, both
must match; the workflow packages only verified release assets.

If Homebrew says `Casks/snow-shot.rb` does not exist and suggests only
`snow-shot@beta`, its local tap checkout has not received the stable cask yet.
Refresh the tap before retrying:

```sh
brew update
brew info --cask mg-chao/tap/snow-shot
brew install --cask mg-chao/tap/snow-shot
```

Homebrew may defer automatic updates, and `HOMEBREW_NO_AUTO_UPDATE` disables
them. If the stable cask is still missing after `brew update`, inspect the tap
checkout and its remote before changing or removing it:

```sh
tap_dir="$(brew --repository mg-chao/tap)"
git -C "$tap_dir" status --short --branch
git -C "$tap_dir" remote -v
ls "$tap_dir/Casks/snow-shot.rb"
```

An Intel-only hardware requirement on Apple Silicon can also mean the tap was
published from an incomplete release upload, as happened with 1.4.3. This is a
cask metadata error when the matching ARM64 release asset exists. After the tap
is repaired, run `brew update` and retry the install or upgrade above. An update
cannot resolve it while the remote cask still requires Intel; maintainers should
follow the repair procedure below.

Finish recordings and quit Snow Shot before upgrading or uninstalling. For
project-signed releases after 1.5.8, Homebrew verifies the pinned project
certificate and copies the application without re-signing it. Finder drag-and-drop,
the standalone installer, and Homebrew preserve the same publisher signature.
No user signing key, certificate installation, or Keychain access is required.
The archive includes the matching DMG, checksum, and tagged installer; it never
fetches a mutable installer at installation time.

First installation or migration from the former ad-hoc/local signatures may
require granting Screen Recording and Accessibility again. The installer leaves
legacy Keychain identities and state files untouched for rollback. Self-signing
does not provide Apple notarization or bypass macOS consent policy. Use the
installer bundled with the new release: old scripts still re-sign applications.

### Migrating an existing installation

Quit Snow Shot and move `/Applications/Snow Shot.app` (or the legacy
`/Applications/snow_shot.app`) to a backup folder outside Applications before
running `brew install`. Keep legacy installer state and keys if rollback is needed. Do not use `--force` to overwrite an existing app. Confirm that the new
copy launches and can capture before deleting the backup. If installation fails,
restore the backup to its original location. Use Homebrew for subsequent upgrades.

A custom destination is supported through Homebrew's standard option:

```sh
mkdir -p "$HOME/Applications"
brew install --cask --appdir="$HOME/Applications" mg-chao/tap/snow-shot
```

If signature verification fails, obtain the matching current DMG and installer;
do not re-sign the app or delete privacy records to bypass validation. Homebrew
restores its previous verified app after a failed upgrade. Keep the backup until
the new installation has been tested.

### Repairing an incomplete Homebrew publication

Keep GitHub releases in draft until every intended asset has been uploaded and
verified. `scripts/publish-snow-shot-release.ps1` follows this ordering. The
Homebrew workflow runs on the release's `published` event; publishing first and
uploading later can expose only one architecture to that job.

The Homebrew publisher now requires every architecture and edition already
present in the channel's tap to have a valid DMG and checksum in the new release.
An incomplete snapshot fails before downloads, archive uploads, or tap changes.
After all uploads finish, rerun **Publish Snow Shot Homebrew cask** for that tag.
Single-architecture historical releases can still be packaged locally, but cannot
replace a cask that already supports both architectures.

If the incomplete cask was already published, ordinary retries reject changes at
the same version. For 1.4.3, run the workflow manually with tag
`v1.4.3_snow-shot` and `repair_architectures` enabled. This option permits only
adding the missing architecture: regenerating the existing architecture must
reproduce its complete cask byte for byte, including its archive checksum and
installer settings. The publisher also verifies existing release archives before
uploading anything or changing the tap. It never replaces published asset bytes.
Stable and beta channels are checked independently; Mini remains ARM64-only.

To review a repair locally first, download the release metadata and its DMGs and
checksum sidecars, check out the matching tagged source, and run:

```sh
python3 scripts/snow-shot-homebrew.py package \
  --release-json release.json --source release-source --assets release-assets \
  --current-cask homebrew-tap/Casks/snow-shot.rb --output repaired-homebrew \
  --repair-architectures
```

This writes the candidate cask and reproducible archives under `repaired-homebrew`
without changing the tap or publishing anything. Compare the candidate with the
current cask before running the publication workflow.

### Installer staging interface

`install-snow-shot-macos.sh --dmg PATH --prepare-app /absolute/New.app` validates
and copies a project-signed local DMG into a fresh `.app` directory without re-signing. The adjacent `PATH.sha256`
is mandatory. The destination's parent must already exist, and an existing output
or symlink is rejected. This mode verifies the pinned project signing requirement but
does not close, replace, or launch any installed application. On failure it removes
only its own incomplete output. The standalone installer still installs normally
when `--prepare-app` is absent.

Focused checks:

```sh
python3 scripts/test-snow-shot-homebrew.py
python3 scripts/test-macos-installer.py
bash -n scripts/install-snow-shot-macos.sh
```

Native release qualification uses a disposable macOS desktop account with
Homebrew: install, launch, upgrade between two stable releases, uninstall, and
repeat with `--appdir`. Check that the signing identity stays the same, the OCR
helper and manifest bytes remain unchanged, and uninstall preserves user data.
In that disposable account, test a package with the wrong signer and verify that
Homebrew restores the previous app after a failed upgrade. This qualification is
separate from signature-only regression checks and requires real release assets
and interactive privacy consent, including DMG → script → Homebrew → DMG upgrades.

## Install a packaged release (no build tools required)

The standalone installer supports macOS 15+, Apple Silicon (including a terminal
running under Rosetta), and Intel. It uses only macOS system tools. After the
release assets are published, choose a Snow Shot release tag and run its installer
asset as your normal desktop user:

```sh
curl --fail --location --proto '=https' --proto-redir '=https' \
  --output install-snow-shot-macos.sh \
  "https://github.com/mg-chao/snow-shot/releases/download/v<version>_snow-shot/install-snow-shot-macos.sh"
bash install-snow-shot-macos.sh --lang en
```

The installer verifies the downloaded package and its pinned project signature,
gracefully closes Snow Shot, and installs `/Applications/Snow Shot.app` without
changing the signature. Publish this installer only with matching project-signed
DMGs; it intentionally rejects the old ad-hoc packages, including 1.5.8.
Existing `/Applications/snow_shot.app` installations migrate to the new name,
with the old path restored if installation fails.
It asks for administrator access only when installation requires it. Do not run
the whole command as root; an invocation through `sudo` hands execution back to
the desktop user. Existing app data and preferences are preserved. Failed
replacement checks restore the previous app; failed recovery prints the backup
directory instead of deleting it.

Language is detected automatically without `--lang`. Supported overrides are
`en`, `zh-CN`, and `zh-TW`. Use `--no-launch` to skip launch after installation, or
`--dmg "/path/to/package.dmg"` to use a local DMG with a required adjacent
`package.dmg.sha256`. Use `--help` for the complete interface. System diagnostics
and macOS authentication dialogs use the system's own language. Missing uploads,
invalid packages, and release API limits produce a nonzero exit without replacing
the installed application.

### 中文安装说明

无需安装 Homebrew、Python、Xcode 或购买 Apple 开发者会员。使用上面的 `curl`
命令下载安装脚本后，以日常桌面用户运行：

```sh
bash install-snow-shot-macos.sh --lang zh-CN
```

脚本会自动识别芯片架构，并行检查 GitHub 和 Gitee Releases，优先使用先完成验证的渠道，
验证后安装到 `/Applications/Snow Shot.app`（已有旧路径安装会迁移到新名称）。仅在替换应用需要管理员权限时请求密码，
无需在命令前添加 `sudo`。1.5.8 之后采用项目统一签名的发布版本不再创建本地证书或访问用户钥匙串。
DMG 拖拽、安装脚本和 Homebrew 均保留发布者签名。请使用新版安装脚本，旧脚本仍会重新签名。
首次安装或从旧签名迁移后，仍需按系统提示授予屏幕录制和辅助功能权限。
跨 macOS 版本的权限保留仍需实机验证。脚本不会重置隐私权限，也不会关闭系统安全机制。

本地安装使用 `--dmg "/路径/安装包.dmg"`，并将对应的 `.dmg.sha256` 放在同一目录。
`--no-launch` 表示安装后不启动应用。失败时请根据诊断信息重试；若显示恢复目录，
请保留该目录中的 `previous.app`，并先恢复到安装器提示的安装路径。

### 繁體中文安裝說明

使用上面的 `curl` 命令下載指令碼後，以日常桌面使用者執行：

```sh
bash install-snow-shot-macos.sh --lang zh-TW
```

不需要 Homebrew、Python、Xcode 或 Apple 開發者會員。指令碼會自動辨識晶片架構，
並行檢查 GitHub 與 Gitee Releases，優先使用先完成驗證的管道，驗證後安裝到
`/Applications/Snow Shot.app`（既有舊路徑安裝會遷移至新名稱）。不必加上 `sudo`；需要管理員權限時才會要求密碼。
1.5.8 之後採用專案統一簽署的發行版本不再建立本機憑證或存取使用者鑰匙圈。
DMG 拖移、安裝指令碼和 Homebrew 均保留發行者簽章。請使用新版指令碼，舊指令碼仍會重新簽署。
首次安裝或從舊簽署移轉後，請依系統提示授予螢幕錄製和輔助使用權限。
跨 macOS 版本的權限保留仍需實機驗證，系統仍可能要求授權。
本機安裝使用 `--dmg "/路徑/安裝套件.dmg"`，並提供同目錄的 `.dmg.sha256`。
`--no-launch` 可略過啟動。若還原失敗，請保留顯示的目錄，先將其中的 `previous.app`
還原到安裝程式提示的安裝路徑再重試。

### Installer publishing contract

The local publisher includes `install-snow-shot-macos.sh`, a versioned DMG,
and its matching `.sha256` asset in the GitHub release when macOS packaging
is configured. The local Gitee publisher uploads these exact installer/download
bytes after GitHub publication. Homebrew distribution archives remain on GitHub;
Gitee hosts only update and installer downloads within a 1 GB attachment budget.

| Architecture | Release asset |
| --- | --- |
| Apple Silicon | `snow-shot-<version>-macos-arm64.dmg` |
| Intel | `snow-shot-<version>-macos-x86_64.dmg` |

Append `.sha256` for each checksum asset. A sidecar has exactly one nonempty
line beginning with the 64-character SHA-256 digest. The standalone installer
races the GitHub and Gitee release lists, includes published previews, and
ignores drafts. It selects the newest valid SemVer release within each channel.
A channel is ready only when it has exact architecture DMG and sidecar asset
names and release download URLs. The first ready channel provides the package;
if download or validation fails, the installer tries the same version on the
other channel. The checksum and DMG come from one channel over HTTPS.

The DMG contains `Snow Shot.app` at its root (the installer also accepts legacy
`snow_shot.app` packages), with bundle ID
`com.snowshot.snow_shot`, executable `snow_shot`, a minimum macOS version, and a
valid bundle signature from the pinned project release certificate (ad-hoc is rejected). Publishing these assets and this
installer remains a separate release operation. Nothing is uploaded by the script.

### DMG installation window

The release DMG opens a branded Finder window with Snow Shot on the left, an
Applications shortcut on the right, and drag-to-install instructions in English,
Simplified Chinese, and Traditional Chinese. Finder backgrounds are static images,
so all three languages appear together rather than selecting the packaging
machine’s language. Native privacy prompts use macOS language selection. Drag the
app to Applications, eject the image, and open Snow Shot from Applications.
The bundle includes its icon, full release version, copyright, Productivity
category, supported languages, and localized macOS privacy prompts. The internal
executable and settings identifier remain `snow_shot`.

Packaging uses Finder through AppleScript to save the disk image layout. Run it
in a logged-in macOS desktop session; macOS may request permission for the
packaging terminal to control Finder. Deployment and project signing happen before
the packaging hook renames the distributable bundle. Development build/run paths
remain unchanged. Developer ID signing and notarization are still separate release
steps.

### Project signing identity, recovery, and qualification

The publisher owns one persistent self-signed certificate, `Snow Shot Project
Release`. Its public certificate is tracked in
`snow_shot/packaging/macos/release-signing.pem`; the standalone installer pins its
SHA-1 certificate fingerprint. The SHA-1 value identifies the certificate for
macOS code requirements; signatures and package digests use SHA-256. Full and
Mini share the certificate and retain separate bundle identifiers.

Release presets use `SNOW_MACOS_CODESIGN_IDENTITY=PROJECT`. Configuration and
packaging fail if the original private key is missing or the installer pin does
not match the public certificate. They never generate a replacement identity or
fall back to ad-hoc signing. Development `AUTO` identities remain separate; the
fast preset is still ad-hoc and is not a distribution preset.

The identity was initialized once with:

```sh
python3 scripts/snow-shot-macos-signing.py initialize
```

This command refuses to overwrite an existing certificate. It imports the private
key into the publishing account's login Keychain with access for `/usr/bin/codesign`,
removes temporary private-key files, and changes no certificate trust settings.
Its public recovery copy lives under
`~/Library/Application Support/Snow Shot/ReleaseSigning`. An interrupted operation
leaves a `creating` marker; recover the existing key and certificate before retrying.
Do not delete the marker to generate a different identity.

Back up the certificate **and private key** as a password-protected `.p12` using
Keychain Access, and keep that backup outside the repository and release assets.
Import that same identity into each publishing Mac's login Keychain. Never create
a new certificate on a replacement machine, and never commit a private key,
`.p12`, or password. To verify that a publishing machine has the expected key:

```sh
python3 scripts/snow-shot-macos-signing.py identity
```

CPack checks the application inside every completed Full/Mini DMG before it can
publish a successful package. It verifies the pinned signer, bundle identity,
resource seal, and actual DR against the previous published application in both
directions. The previous matching GitHub release is downloaded with a verified
asset SHA-256. Network failure, a changed certificate, an incompatible DR, or an
ad-hoc successor stops packaging. Cached remote release packages are audited again.
Results are written to `<package-name>.signing.json` and included in remote receipts.
Do not overwrite the already-published 1.5.8 assets; bump the version for the first
project-signed release.

For offline builds, provide the immediately preceding published DMGs:

```sh
export SNOW_MACOS_PREVIOUS_FULL_DMG=/absolute/path/snow-shot-PREVIOUS-macos-arm64.dmg
export SNOW_MACOS_PREVIOUS_MINI_DMG=/absolute/path/snow-shot-mini-PREVIOUS-macos-arm64.dmg
scripts/package-snow-shot.sh snow-shot-macos-arm64-release
```

For Intel use the preceding x86_64 Full DMG. The only migration exception is the
exact SHA-256-pinned 1.5.8 release for each supported product/architecture. Its audit
explicitly records `legacy-1.5.8-migration-requires-consent`, never `compatible`.
Later project-signed releases must satisfy their predecessor's actual DR.

Installers and Homebrew only verify and copy the publisher's bundle; no private
key is distributed. They do not read or overwrite old per-user identity/requirement
files. A first migration from ad-hoc or per-user signatures may require removing
an obsolete privacy entry and authorizing the new app once. Old installation
scripts still re-sign apps and must not be used with the new release.

Self-signing provides stable identity; it does not provide Apple notarization or
remove Gatekeeper's unidentified-developer checks. TCC behavior must still be
qualified in a disposable macOS account: grant Screen Recording and Accessibility
to build A, replace it with a different build B via DMG, script, and Homebrew,
then exercise both capabilities, OCR, and same-version reinstall. Repeat on
supported macOS versions and architectures. Signature tests alone do not prove
consent retention; never perform these experiments on personal privacy grants.

Focused checks (no full application build or test suite):

```sh
python3 scripts/test-macos-release-signing.py
python3 scripts/test-macos-installer.py
python3 scripts/test-snow-shot-homebrew.py
python3 scripts/test-remote-macos-release.py
```

## Prerequisites

- Xcode 16+ or Apple Command Line Tools (`xcode-select --install`), with a macOS 15+ SDK.
  The static Qt builder permits Command Line Tools without full Xcode and retains
  Qt's SDK version checks.
- Rust 1.97.1 or newer installed through rustup. Builds use the active toolchain;
  bootstrap adds its required components and target without selecting an older version.
- CMake 4.2+, Ninja, pkg-config, Git and Python 3. Intel codec builds also need NASM.
- The official **Qt 6.12.0 LTS macOS** kit, including Qt SVG and Linguist tools,
  for Debug and performance builds. Release and fast builds use the repository's
  audited static Qt build for the selected architecture.

For example, install host tools with `brew install cmake ninja pkgconf nasm`.
The scripts also recognize tools already installed under `.tools/macos-dev/bin`
and `.tools/macos-media/host/bin`. They do not modify global tool installations.

Set `Qt6_DIR` if Qt is not at `$HOME/Qt/6.12.0/macos/lib/cmake/Qt6`:

```sh
export Qt6_DIR=/path/to/Qt/6.12.0/macos/lib/cmake/Qt6
scripts/bootstrap-macos.sh
scripts/build.sh
scripts/run-snow-shot.sh
```

Bootstrap checks the host tools, installs the matching Rust target, bootstraps
repository-local vcpkg at the registry baseline, and installs the selected
manifest dependency set. `build.sh` validates that setup before configuring and
automatically refreshes a stale CMake cache; pass `--skip-bootstrap` only after
provisioning the selected preset. Compiler build changes (including Xcode updates
at the same compiler path) refresh compiler detection and clean incompatible
precompiled headers and objects automatically. Existing build trees without a
recorded compiler signature are cleaned once on their next build.
A complete active Rust toolchain meeting the minimum version is reused without
contacting the Rust distribution server;
only missing components or targets are downloaded into that toolchain.
`--clean` removes that preset's build tree, matching the Windows wrapper, and
`--parallel N` controls build concurrency.
The run script rebuilds the `snow_shot` target before launching by default; pass
`--no-build` to launch an existing build or `--clean` to request a clean rebuild.
CMake installs native dependencies from the manifest. The first run builds
FFmpeg, image codecs, and OpenCV and can take considerable time.
Dynamic and static dependencies live in `.tools/macos/installed/{dynamic,static}`,
isolated from Windows and the standalone macOS media harness. `LIBCLANG_PATH`
can override Xcode's libclang.

Provision a production static kit once per architecture. The first bootstrap
installs the static libpng/zlib packages used by Qt as well as the application
dependency closure:

```sh
scripts/bootstrap-macos.sh snow-shot-macos-arm64-release --skip-qt-validation
scripts/build-static-qt.sh \
  --arch arm64 \
  --install-prefix "$HOME/Qt/6.12.0/macos-static-arm64" \
  --parallel 8
export SNOW_QT_STATIC_DIR="$HOME/Qt/6.12.0/macos-static-arm64/lib/cmake/Qt6"
scripts/build.sh snow-shot-macos-arm64-release --parallel 8
```

`SNOW_QT_STATIC_DIR` takes precedence over `Qt6_DIR` for release and fast builds.
Keep `Qt6_DIR` pointed at the shared kit for Debug and performance builds.

For an Intel build on either Intel or Apple Silicon, provision the x64 static
dependencies and Qt kit separately:

```sh
scripts/bootstrap-macos.sh snow-shot-macos-x64-release --skip-qt-validation
scripts/build-static-qt.sh --arch x64 \
  --install-prefix "$HOME/Qt/6.12.0/macos-static-x64" --parallel 4
export SNOW_QT_STATIC_DIR="$HOME/Qt/6.12.0/macos-static-x64/lib/cmake/Qt6"
scripts/package-snow-shot.sh snow-shot-macos-x64-release --parallel 4
```

Apple Silicon x64 builds require Rosetta because Qt build tools and the OCR
worker execute during the build. The entry points verify Rosetta before starting;
if it is missing, install it with
`softwareupdate --install-rosetta --agree-to-license`. The static Qt builder
disables Qt's implicit cross-compilation mode for this direction and builds its
own x64 tools. Its fresh configure avoids reusing a failed cross-compilation cache.
Native Intel builds do not require Rosetta. The resulting package is
`snow-shot-<version>-macos-x86_64.dmg` with an adjacent `.sha256` file; Mini is
not built for Intel.

The Intel static codec build keeps x264's assembly and 64-byte stack alignment,
but emits native x264 objects to avoid conflicting LLVM LTO module flags. Qt,
the application, and the other dependencies retain their LTO settings.

The static Qt script pins
the architecture and Qt's supported 14.4 library deployment target (the Snow Shot
app still targets macOS 15.0), enables LTO and system libpng/zlib, installs Qt
source-license metadata, disables the macOS-27-only `dup3` path for compatibility
with the supported deployment range, and records an audited build stamp. It
reuses a matching installation; pass `--force` only when intentionally replacing
that prefix. Release and fast entry points reject a shared, unstamped, wrong-arch,
or wrong-version Qt kit.

`scripts/qt-toolchain.json` pins the Qt version, official source-archive
SHA-256, and macOS Qt library deployment target. The static builder verifies the
archive before extracting it and checks an existing source tree's qtbase version
and supported macOS runtime floor before configuring. Shared and static entry
points check the installed Qt package
versions and the actual Release binary's architecture, independently of an
audited stamp. A kit from the previous Qt release must be rebuilt in its own
6.12.0 install prefix.

The feature-policy fingerprint and installed Qt target exports are also checked
before reusing a production kit. Time-zone handling remains enabled; Cocoa
continues to provide native time-zone names, and Qt's optional CLDR name tables
remain disabled on Apple platforms. Use a distinct install prefix to preserve
an older kit when refreshing the audited schema.

## Presets and targeted checks

Names follow `snow-shot-macos-{arm64|x64}-{debug|performance|release|fast}`.
The default architecture follows the current shell's `uname -m` (including
Rosetta); pass a preset explicitly to select another architecture.

| Mode | CMake configuration | Tests | Benchmarks | Packaging |
| --- | --- | --- | --- | --- |
| debug | Debug | Enabled | Disabled | Disabled |
| performance | Release | Enabled | Enabled | Disabled |
| release | Static Release with LTO | Disabled | Disabled | Static DMG |
| fast | Static Release without app C++ LTO, Cargo release-fast | Disabled | Disabled | Disabled |

```sh
scripts/build.sh snow-shot-macos-arm64-debug --target snow-shot-ocr-layout-tests
ctest --preset test-snow-shot-macos-arm64-debug -R '^snow-shot-ocr-layout-tests$'
scripts/build.sh snow-shot-macos-arm64-fast --clean
scripts/build.sh snow-shot-macos-x64-release
scripts/run-snow-shot.sh snow-shot-macos-arm64-debug -- --help
python3 scripts/test-macos-build-support.py
# Optional native bundle/DMG fixture after provisioning Qt and FFmpeg:
SNOW_TEST_MACOS_BUNDLE=1 python3 scripts/test-macos-build-support.py MacOSBundle
```

The native deployment fixture tests the packaging rules with a small Qt executable
and a simulated versioned OCR library; it does not validate the full app.
It requires CMake, Ninja, pkg-config and CPack on PATH.

The x264 overlay fixes pointer aliasing in the shared frame/macroblock preallocator.
Optimized Apple Silicon builds could retain a buffer offset after allocation and
crash in `memset` while opening the software H.264 encoder. The patch keeps offsets
separate and writes final pointers with `memcpy`, preserving LTO and assembly.
Reprovision the codec dependencies and rebuild the app after updating the overlay;
an existing binary still contains the old allocator.

Verify the production x264 archive without Qt or screen permissions using the
standalone fixture below. It encodes and drains 8-bit and 10-bit frames with normal
and zero-latency presets and multiple thread counts. Static app builds with the
general tests enabled also register these tests.

```sh
cmake --fresh -S test-support/x264 -B build/x264-preallocation-arm64 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DSNOW_X264_ROOT="$PWD/.tools/macos/installed/static/arm64-osx-snow-shot-static"
cmake --build build/x264-preallocation-arm64 --target snow-x264-preallocation-tests
ctest --test-dir build/x264-preallocation-arm64 --output-on-failure \
  -R '^snow-x264-preallocation-'
```

For Intel, use a separate build directory, `CMAKE_OSX_ARCHITECTURES=x86_64`, and the
`x64-osx-snow-shot-static` prefix. Run the resulting fixture on native Intel hardware
as part of release validation; Rosetta is also useful for local regression checks.

The clipboard DIB byte-access fixture also runs without Qt or screen permissions.
It checks unaligned source addresses, pixel counts around SIMD boundaries, and
output bounds. With Clang or GCC, enable UBSan to catch alignment regressions:

```sh
cmake -S test-support/clipboard-dib -B build/clipboard-dib-ubsan -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DSNOW_DIB_ENABLE_UBSAN=ON
cmake --build build/clipboard-dib-ubsan --target snow-shot-clipboard-dib-decoder-tests
ctest --test-dir build/clipboard-dib-ubsan --output-on-failure \
  -R '^snow-shot-clipboard-dib-decoder-tests$'
```

The fixture always checks scalar decoding and runs AVX2 kernels when the CPU
supports them. Windows clipboard-content tests additionally cover borrowed DIB
headers and mask tables; macOS does not exercise the Windows clipboard API.

To validate a production static kit without QtTest or unrelated UI tests, use a
separate tree with only the shared OCR and crash diagnostics tests enabled:

```sh
cmake --preset snow-shot-macos-x64-release \
  -B build/snow-shot-macos-x64-validation \
  -DQt6_DIR="$SNOW_QT_STATIC_DIR" \
  -DSNOW_APPS_BUILD_TESTS=OFF -DSNOW_SHOT_BUILD_RUNTIME_TESTS=ON \
  -DSNOW_APPS_PACKAGE_SNOW_SHOT=OFF
cmake --build build/snow-shot-macos-x64-validation --parallel 4 \
  --target snow-shot-ocr-assets-tests snow-shot-ocr-recognition-service-tests \
  snow-shot-diagnostics-crash-tests
ctest --test-dir build/snow-shot-macos-x64-validation --output-on-failure \
  --no-tests=error \
  -R '^(snow-shot-ocr-(assets|managed-runtime|cpu-recognition-service|process-lifecycle|storage-relocation)-tests|snow-shot-diagnostics-crash-tests)$'
```

Set `SNOW_QT_STATIC_DIR` to the audited x64 kit first and put the build tools on
PATH, as for direct CMake use. The x64 test executables run through Rosetta on
Apple Silicon; passing them does not qualify native Intel hardware. The focused
option defaults to `OFF` and is independent of the general test option.

Crash diagnostics need particular care under Rosetta. A [Crashpad maintainer
documented that Rosetta 2 crash collection was unsupported in December
2021](https://groups.google.com/a/chromium.org/g/crashpad-dev/c/kJSzcSaqqoY).
In the current x64 validation on Apple Silicon with macOS 27.0.1, Crashpad startup
succeeded but access and abort fixtures produced no minidump; the unchanged
`snow-shot-diagnostics-crash-tests` failed its one-dump assertion. The same
failure occurred with CTest explicitly running as x86_64. This is consistent
with the documented limitation, although the precise current exception handoff
failure has not been established. Keep this test enabled and run it on native
Intel hardware before qualifying Intel crash capture. Passing OCR tests and
package checks under Rosetta does not establish that qualification.

Only run tests covering your change. Test presets exclude Windows-only,
interactive, end-to-end and benchmark labels. Build a benchmark explicitly with
`--target` using the performance preset. Running an Intel executable on Apple
Silicon requires Rosetta. `build.sh` defaults to the application target; use
`--target snow-all` only when all enabled targets are needed. On Apple Silicon,
the default application build includes both Snow Shot editions.

For direct CMake use, put the host tools on PATH and supply `-DQt6_DIR=...`:
`cmake --preset snow-shot-macos-arm64-debug`, then
`cmake --build --preset build-snow-shot-macos-arm64-debug`.
The shell wrapper also sets Xcode libclang discovery for Rust bindgen.

### Idle memory optimization

Both editions expose **Memory Optimization Policy** under **General > Application
performance**. **Smart Control** is the default; **Disabled** stops automatic
reclamation immediately. On macOS, Smart Control calls
`malloc_zone_pressure_relief(nullptr, 0)` to return unused allocator pages to the
system. Live images, capture data, and other allocations retain their contents.
This is best-effort reclamation and may release zero bytes; it does not evict
live process pages like Windows `EmptyWorkingSet`.

The controller waits for background idle time, blocks reclamation during active
work or UI interaction, and limits attempts to one per idle period with a
five-minute cooldown. Memory pressure notifications shorten the idle wait after
the initial startup grace. Logs report macOS physical footprint (including
compressed memory) and allocator-reported released bytes separately.

Focused checks:

```sh
scripts/build.sh snow-shot-macos-arm64-debug \
  --target snow-shot-memory-optimization-controller-tests \
  --target snow-shot-macos-memory-optimization-tests \
  --target snow-shot-memory-optimization-settings-tests \
  --target snow-shot-memory-optimization-settings-mini-tests \
  --target snow-shot-settings-catalog-tests
ctest --preset test-snow-shot-macos-arm64-debug -R '^snow-shot-(macos-memory-optimization|memory-optimization-(controller|settings|settings-mini|catalog))-tests$'
```

Formatting and lint entry points are `scripts/check-cpp-format.sh [--fix]` and
`scripts/check-rust.sh PACKAGE [-- CARGO_OPTIONS...]`. The `snow-format` and
`snow-lint` CMake targets use native shell scripts on macOS; `snow-lint` checks
the CPU OCR helper. Other changed Rust crates should be linted individually.

## App bundles and packaging

```sh
scripts/package-snow-shot.sh snow-shot-macos-arm64-release
```

Full and Mini DMGs use LZMA compression (`ULMO`) for the macOS 15+ deployment target.
Compression changes the distribution image while preserving the signed app bundle.
CPack signs and verifies each image before computing its SHA-256 checksum. Validate
DMG mounting and app installation on the supported macOS versions before publishing.

The release script builds and produces edition DMGs plus SHA-256 checksums under the
preset's build directory. Pass `--parallel N` to control the build or
`--skip-build` to package an already validated release cache. CPack installs only
the corresponding Snow Shot component and verifies the bundle signature. Static packages import
the Cocoa, macOS style, Secure Transport, SVG, and offscreen Qt plugins at link
time and reject every non-system Mach-O dependency. The bundle includes the OCR
helper, shutter audio, and project license notices; the full edition also includes
QR models. Executable-relative
asset lookup paths remain under `Contents/MacOS`; deployment moves data into
`Contents/Resources` and creates relative `assets`/`audios` directory links so
code signing seals them as resources. Static Qt and vcpkg source-license notices
are included under `Contents/Resources/snow-shot/licenses/third-party`. Installation
also collects original Rust dependency notices offline from each edition's compiled
Cargo targets and feature selections. Missing notices stop packaging. A plain
`cmake --install build/<preset> --component SnowShot --prefix /path/to/staging`
performs the same deployment and static-link audit without a DMG. Shared Debug
and performance deployments continue to put libraries in `Contents/Frameworks`.

Debug and performance presets automatically create `Snow Shot Development (Local)`,
a self-signed code-signing identity in the user's default keychain. The private key
is restricted to `/usr/bin/codesign`. CMake signs the raw app executable for direct
IDE debugging, and the run script uses the same certificate for its deployed copy.
Because the certificate remains stable across rebuilds, macOS can retain privacy
grants after the app's code changes. The certificate is only for local development;
it is not the project release identity. Release presets require the project
certificate; fast presets remain ad-hoc signed.

To use another code-signing certificate from the keychain, pass its name or SHA-1:

```sh
scripts/run-snow-shot.sh snow-shot-macos-arm64-debug \
  --codesign-identity "Your code-signing certificate name or SHA-1"
```

The override applies to that run and is stored in the selected preset's CMake
cache. Later default runs restore the local development identity, or `PROJECT`
for a release preset. On non-release presets, pass `--codesign-identity -` to
explicitly use ad-hoc signing for one run. A missing or
unusable override fails deployment; it does not fall back to ad-hoc signing.
Project self-signing does not provide Developer ID trust or Apple notarization.
Gatekeeper may still require the user to approve opening the downloaded app.
The run script deploys a signed development copy under `build/<preset>/run`
before launching; this also resolves the OCR helper's dynamically loaded libraries.
Launch through the run script/Finder and grant Screen Recording, Accessibility,
and microphone permissions when using the corresponding capabilities.

### Accessibility enabled in Settings but reported missing

Ad-hoc signatures identify a particular build by its code hash. After rebuilding,
TCC can reject the old grant while System Settings still shows Snow Shot enabled.
The system log then reports `Failed to match existing code requirement` for
`com.snowshot.snow_shot` and `kTCCServiceAccessibility`.

Quit Snow Shot, remove its old entry from System Settings > Privacy & Security >
Accessibility with the minus button, then add and enable the exact deployed app:
`build/<preset>/run/snow_shot.app`. Restart that copy with
`scripts/run-snow-shot.sh <preset> --no-build`. This option launches the existing
deployment without rebuilding, reinstalling, or signing it again. The raw app
under `build/<preset>/snow_shot` has a different signature and is not the copy
launched by the run script. Ad-hoc builds may need this recovery again after a
rebuild; use the run script's local signing identity for subsequent builds to
avoid that identity change. Switching from ad-hoc to certificate signing requires
granting permission to the new identity once; later rebuilds retain that grant.

macOS OCR uses CPU inference with the three existing V6 models. Full on
both architectures and Mini on ARM64 bundle their CPU worker.
Full also bundles Small V6; Mini bundles no model files. Missing selected models download
on demand into application storage. No OCR runtime code
is downloaded on macOS. An Intel package built on Apple Silicon is validated
through Rosetta; that validation does not establish native Intel hardware qualification.
The Windows updater/installer remain Windows-only.

macOS diagnostics use the shared rotating JSON log service and a bundled Crashpad
handler. The handler captures native faults, stack overflow, fatal Qt errors,
C++ termination, and Rust panics. The OCR worker inherits the Mach exception
handler and adds its own role, session, and operation breadcrumbs. Reports stay
local; automatic uploads are disabled. Daily log export includes crash summaries,
while binary dumps remain in the `crashes` database under the selected log directory.
Permission transitions and display geometry/scaling changes are recorded without
hardware serial numbers or captured content.
Fatal/terminate/panic paths also write bounded emergency evidence independently
of the asynchronous log queue. Forced termination and power loss may leave only
the prior logs and an unexpected-session-termination marker.

Packaging builds `snow-shot-diagnostics-symbols`, placing UUID-matched application
and OCR worker dSYMs under `build/<preset>/symbols/`. Archive these beside each
release; they are deliberately outside the shipped app. The same target can be
built manually for development builds. The handler is deployed and signed with
the other nested executables before the outer bundle is sealed.
Run the focused diagnostics checks with
`ctest --test-dir build/<debug-preset> -R '^snow-shot-diagnostics(-crash)?-tests$' --output-on-failure`.
The crash fixtures use temporary storage and intentionally crash isolated children.
The standalone `bootstrap-macos-media.sh` and `build-macos-media.sh` workflows
remain available for media harness development.

## macOS OCR validation

The runtime uses protocol 6. Its generated schema-3 manifest records
`macos-arm64` or `macos-x64`, `delivery: bundled`, static linkage, the executable
name, and the size/SHA-256 of the worker. The worker uses simd-paddle-ocr-rs CPU inference without external inference
libraries in either development or release builds. The
existing Windows schema-2 manifest remains the
source of the three pinned model contracts; its Windows runtime is never staged
on macOS. Runtime binaries live in `Contents/MacOS`; models and the manifest are
accessed through `Contents/MacOS/assets/ocr` (a resource link in signed bundles).
The worker uses the submodule-pinned nightly compiler; other Rust packages
continue using their existing compiler. Bootstrap provisions both toolchains. A damaged runtime requires reinstalling
the ARM64 app; verified downloaded models remain reusable.

Build staging checks content on every relevant target build, including a worker-only
change followed by a host build. Model downloads are verified before promotion and
cached under `artifacts/`. Full packaging requires a complete Small V6 payload;
Mini staging removes previously bundled models and packaging rejects model files
or directories. Deployment
resolves the pinned native dependency closure, rewrites relocatable Mach-O loads,
and signs nested code, generates hashes of the finalized
runtime, then seals the enclosing app. CPack signs and verifies the DMG before
calculating its SHA-256 checksum. No runtime binary may be modified afterward.
The installer writes `macos-ocr-verification.json` in the build directory, listing
verified Mach-O binaries and runtime hashes. This is local integrity validation;
project self-signing is not Developer ID signing or notarization.

Focused deterministic checks (the asset executable needs no worker or real models):

```sh
scripts/build.sh snow-shot-macos-arm64-performance --target snow-shot-ocr-assets-tests
ctest --preset test-snow-shot-macos-arm64-performance -R '^snow-shot-ocr-assets-tests$'
python3 scripts/test-snow-shot-macos-ocr.py
scripts/check-rust.sh snow-ocr-process
```

Build the recognition test target and fetch all verified models for the opt-in
native checks. Use only the performance preset when passing `--measure`:

```sh
scripts/build.sh snow-shot-macos-arm64-performance --target snow-shot-ocr-recognition-service-tests
ctest --preset test-snow-shot-macos-arm64-performance \
  -R '^snow-shot-ocr-(cpu-recognition-service|process-lifecycle|managed-runtime)-tests$'
python3 scripts/snow-shot-macos-ocr.py fetch-models --model-root build/ocr-versioned-models
export SNOW_TEST_OCR_TEXT_FIXTURE="$PWD/snow_shot/tests/baselines/ocr-model-versions.png"
# Use the recognition test executable emitted by this build:
OCR_TEST="$PWD/build/snow-shot-macos-arm64-performance/snow_shot/test-bin/snow-shot-ocr-recognition-service-tests"
"$OCR_TEST" --model-root="$PWD/build/ocr-versioned-models" --measure
"$OCR_TEST" --resident-text-fixture
"$OCR_TEST" --model-root="$PWD/build/ocr-versioned-models" --resident-models
```

The three-model run checks bilingual text, geometry, FIFO callbacks and repeated
inference. Measurements report the first cold request, the following request using
the same session, and the worker's resident bytes sampled after each result (not
peak RSS). Cold means a new worker/model session, without flushing the operating
system's file cache. They are hardware-dependent observations, not a latency guarantee.

For a shared performance deployment, relocate the app to a path containing
spaces or Unicode and test the exact bundle with the matching shared-runtime
recognition helper, without modifying its signature:

```sh
"$OCR_TEST" --bundle="/path/to/Relocated Snow Shot.app" --offline
"$OCR_TEST" --bundle="/path/to/Relocated Snow Shot.app" --model=extra_small --cache="/tmp/snow-ocr-cache"
"$OCR_TEST" --bundle="/path/to/Relocated Snow Shot.app" --model=extra_small --cache="/tmp/snow-ocr-cache" --offline
```

For a static Release package, verify its finalized runtime and startup separately:

```sh
python3 scripts/snow-shot-macos-ocr.py verify \
  --static-runtime \
  --runtime-dir="/path/to/Relocated Snow Shot.app/Contents/MacOS" \
  --app="/path/to/Relocated Snow Shot.app"
"/path/to/Relocated Snow Shot.app/Contents/MacOS/snow_shot" --startup-probe
```

The recognition helper's first run uses a fresh temporary cache and an unreachable
download proxy. The second acquires another model; the third proves cache reuse
without network access.
Run with `DYLD_LIBRARY_PATH`, `DYLD_FALLBACK_LIBRARY_PATH`, and `ORT_DYLIB_PATH` unset.
Also launch the packaged app through Finder and check its screenshot-to-OCR flow.

For Mini, add `--runtime-only` to the Python verification command and use
`Snow Shot Mini.app` / `snow_shot_mini` for the app and executable paths. A fresh Mini
cache needs the selected model download before offline recognition can work;
repeat recognition with the same cache to verify offline reuse.

## Screenshots

See [macOS screenshot support and validation](snow_shot/tests/macos_screenshots.md)
for supported workflows, point-based editing, mixed-display export sizing,
permission recovery, targeted checks and native hardware acceptance steps.
Screenshot capture uses ScreenCaptureKit; Windows-only backend preferences remain
stored but are not used on macOS.

See [macOS cursor coordination](snow_shot/tests/macos_cursors.md) for the shared
screenshot, recording and pinned-window cursor contract and the native checks
required when upgrading Qt or macOS.

## Pin to Screen

See [macOS pinned-window validation](snow_shot/tests/macos_pinned_windows.md) for
placement semantics, targeted tests, and the hardware qualification checklist.
Pinned windows use logical desktop pixels for position, size, interaction, and
restore. A 300×200 selection stays 300×200 on both Retina and non-Retina displays;
its full-resolution image remains independent of window geometry. Pins and their
controls join all Spaces. The unreleased format 2 schema in `pinned_windows_v2`
stores explicit geometry units and window sizes; earlier development schemas are
not migrated.

## Launch at login

**Settings > System > General > Open at login** controls whether Snow Shot starts
in the background when you log in to your Mac. The setting is off by default.
Turning it on registers the app with macOS; turning it off unregisters it without
quitting the running app. Resetting the General section turns it off.

Starting Snow Shot never changes login-item registration. Existing registrations
are preserved when upgrading, and an old enabled preference does not restore an
item removed in System Settings. The app reads native status when its settings
page opens or it becomes active, so the toggle reflects changes made elsewhere.
Existing `macos-login-item.ini` initialization markers are ignored.

Registration uses `SMAppService.mainAppService` directly, without Snow Shot imposing
an Applications-folder requirement. macOS determines whether registration succeeds;
native errors, including signing failures, are displayed by the setting. Both
`SMAppServiceStatusNotRegistered` and `SMAppServiceStatusNotFound` allow an explicit
registration attempt. A preference write failure does not undo a successful native
change: the toggle continues to show the actual macOS registration state.

If macOS reports that approval is pending, the toggle shows the requested
registration with an inline hint directing you to **System Settings > General >
Login Items** (the panel name can vary by macOS version). Turning the toggle off
also removes pending registration. Login startup stays in the background, but
missing required permissions still open the existing permission guide.

### Manual launch-at-login acceptance check

Record the app's original login-item state before checking. Automated tests use
injected operations and do not register real login items.

1. With fresh preferences and no existing registration, launch Snow Shot and open
   settings. Confirm **Open at login** is off and no Login Items entry was added.
2. Turn it on and verify one Snow Shot entry appears in Login Items. If macOS
   reports pending approval, verify the inline hint and approve it in System
   Settings. Return to Snow Shot and verify the refreshed state.
3. Turn it off and verify the entry is removed while Snow Shot keeps running.
   Turn it on again, then reset General settings and confirm it is unregistered.
4. Enable it, then remove or disable it in System Settings. Reopen Snow Shot and
   confirm registration is not restored. Also verify that an existing enabled
   registration survives an app upgrade or restart.
5. With launch at login enabled, log out and back in. Confirm one running instance,
   the menu bar icon, and no main window when permissions are granted. With a required
   permission missing, confirm the permission guide still opens. Manually reopen from
   Finder or the Dock and confirm the main window opens.
6. Test a signed copy outside Applications and confirm the toggle attempts native
   registration instead of requiring a move. Report any actual macOS error and
   restore the original login-item state when finished.
