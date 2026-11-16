#!/bin/bash
set -euo pipefail
source "$(dirname "$0")/snow-build-environment.sh"
if [[ "${1:-}" == --help ]]; then
    snow_load_qt_policy
    echo 'Usage: bootstrap-macos.sh [macOS-preset] [--skip-dependency-install] [--skip-qt-validation]'
    printf 'Requires Xcode command-line tools, Rust (rustup), Qt %s, CMake >= 4.2, Ninja and pkg-config.\n' "$snow_qt_version"
    exit 0
fi
snow_require_macos
preset=''
if [[ $# -gt 0 && "$1" != --* ]]; then preset="$1"; shift; fi
snow_select_preset "$preset"
skip_dependencies=0
skip_qt=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --skip-dependency-install) skip_dependencies=1 ;;
        --skip-qt-validation) skip_qt=1 ;;
        *) snow_die "Unknown argument: $1" ;;
    esac
    shift
done
xcrun --find clang >/dev/null
if [[ "$skip_qt" == 1 ]]; then
    export PATH="$snow_repo_root/.tools/macos-dev/bin:$snow_repo_root/.tools/macos-media/host/bin:$PATH"
    for tool in cmake ninja cargo rustc rustup pkg-config; do
        command -v "$tool" >/dev/null || snow_die "Missing $tool. Install the prerequisites listed in docs-macos-build.md."
    done
    snow_require_target_tools "$snow_arch"
else
    snow_setup_tools
fi
for tool in git python3; do
    command -v "$tool" >/dev/null || snow_die "Missing $tool. Install the prerequisites listed in docs-macos-build.md."
done
cmake_version="$(cmake --version | head -n 1)"
if [[ ! "$cmake_version" =~ ^cmake[[:space:]]version[[:space:]]([0-9]+)\.([0-9]+) ]] ||
    (( BASH_REMATCH[1] < 4 || (BASH_REMATCH[1] == 4 && BASH_REMATCH[2] < 2) )); then
    snow_die "CMake 4.2 or newer is required; found: ${cmake_version:-unknown}"
fi
cd "$snow_repo_root"
snow_require_rust
# Inspect the active toolchain locally. Builds call bootstrap too, so a complete
# installation must not invoke rustup's distribution-syncing add/install commands.
rust_info="$(rustc -vV)" || snow_die 'Unable to determine the active Rust host.'
rust_host="$(sed -n 's/^host: //p' <<< "$rust_info")"
[[ -n "$rust_host" ]] || snow_die 'Unable to determine the active Rust host.'
installed_components="$(rustup component list --installed)" || snow_die 'Unable to list installed Rust components.'
missing_components=()
for component in cargo rustc rust-std rustfmt clippy; do
    if ! grep -Fqx "$component-$rust_host" <<< "$installed_components"; then
        missing_components+=("$component")
    fi
done
if [[ ${#missing_components[@]} -gt 0 ]]; then
    rustup component add "${missing_components[@]}"
fi
# The host standard library is covered above; only a missing cross target needs
# another rustup operation. All additions use the active toolchain as well.
if [[ "$snow_rust_target" != "$rust_host" ]] &&
    ! grep -Fqx "rust-std-$snow_rust_target" <<< "$installed_components"; then
    rustup target add "$snow_rust_target"
fi
printf 'Using the active Rust toolchain with target %s.\n' "$snow_rust_target"
ocr_pin="$snow_repo_root/snow-crates/crates/simd-paddle-ocr-rs/rust-toolchain.toml"
[[ -f "$ocr_pin" ]] || snow_die 'Initialize Git submodules before building OCR.'
ocr_toolchain="$(sed -n 's/^channel *= *"\([^"]*\)".*/\1/p' "$ocr_pin")"
[[ "$ocr_toolchain" == nightly-* ]] || snow_die 'Invalid OCR toolchain pin.'
if ! rustup toolchain list | grep -Fq "$ocr_toolchain-$rust_host"; then
    rustup toolchain install "$ocr_toolchain" --profile minimal --component rustfmt,clippy --target "$snow_rust_target"
else
    ocr_components="$(rustup component list --toolchain "$ocr_toolchain" --installed)"
    ocr_missing=()
    for component in cargo rustc rust-std rustfmt clippy; do
        if ! grep -Fqx "$component-$rust_host" <<< "$ocr_components"; then ocr_missing+=("$component"); fi
    done
    if [[ ${#ocr_missing[@]} -gt 0 ]]; then rustup component add --toolchain "$ocr_toolchain" "${ocr_missing[@]}"; fi
    if ! grep -Fqx "rust-std-$snow_rust_target" <<< "$ocr_components"; then
        rustup target add --toolchain "$ocr_toolchain" "$snow_rust_target"
    fi
fi
# Use the same pinned registry baseline and isolated dynamic/static install roots as Windows.
vcpkg_root="$snow_repo_root/.tools/vcpkg"
vcpkg_baseline=4497409a47f19db373a410a0efb84eca4747adbf
if [[ ! -d "$vcpkg_root/.git" ]]; then
    [[ ! -e "$vcpkg_root" ]] || snow_die "Repository-local vcpkg must be a Git checkout: $vcpkg_root"
    git clone https://github.com/microsoft/vcpkg.git "$vcpkg_root"
fi
vcpkg_head="$(git -C "$vcpkg_root" rev-parse HEAD 2>/dev/null || true)"
if [[ "$vcpkg_head" != "$vcpkg_baseline" ]]; then
    git -C "$vcpkg_root" fetch origin "$vcpkg_baseline" --depth=1
    git -C "$vcpkg_root" checkout --detach "$vcpkg_baseline"
fi
if [[ ! -x "$vcpkg_root/vcpkg" ]]; then
    "$vcpkg_root/bootstrap-vcpkg.sh" -disableMetrics
fi
if [[ "$skip_dependencies" == 0 ]]; then
    "$vcpkg_root/vcpkg" install \
        "--x-manifest-root=$snow_repo_root" \
        "--x-install-root=$snow_vcpkg_installed" \
        "--triplet=$snow_vcpkg_triplet" \
        --x-feature=snow-shot \
        "--overlay-ports=$snow_repo_root/cmake/vcpkg-overlay-ports" \
        "--overlay-triplets=$snow_repo_root/cmake/vcpkg-overlay-triplets" \
        --clean-after-build
fi

python3 "$snow_repo_root/scripts/snow-shot-sparkle.py" bootstrap
