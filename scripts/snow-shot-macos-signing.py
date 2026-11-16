#!/usr/bin/env python3
"""Publisher-only signing identity and cross-release verification. Never resets TCC."""
import argparse
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import secrets
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
CERTIFICATE = ROOT / 'snow_shot/packaging/macos/release-signing.pem'
INSTALLER = ROOT / 'scripts/install-snow-shot-macos.sh'
PRODUCTS = {'snow-shot': 'com.snowshot.snow_shot',
            'snow-shot-mini': 'com.snowshot.snow_shot_mini'}
REPOSITORY = 'mg-chao/snow-shot'
# The last ad-hoc release is the only allowed migration boundary. These are
# downloaded GitHub asset digests, not arbitrary unsigned predecessor exceptions.
LEGACY_PACKAGES = {
    'snow-shot-1.5.8-macos-arm64.dmg':
        'c113a13f3acb3321124616c4174b1d4f2418ab62787f08db4819f9985c28c160',
    'snow-shot-1.5.8-macos-x86_64.dmg':
        '6fc190392fb99c384ab7d5c1dca11b2dbad2f014db478b3ea074117835a1f636',
    'snow-shot-mini-1.5.8-macos-arm64.dmg':
        '102eed9dd44e9f91e16de4824270aef197a476a34e5d83c6d4f475c2908cc564',
}


def run(*args):
    result = subprocess.run(args, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        # Do not echo argv: identity provisioning commands carry import passwords.
        raise ValueError(f'{Path(args[0]).name} failed: {result.stderr.strip()}')
    return result.stdout + result.stderr


def digest(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def fingerprint(certificate=None):
    certificate = certificate or CERTIFICATE
    output = run('/usr/bin/openssl', 'x509', '-in', str(certificate),
                 '-noout', '-fingerprint', '-sha1')
    value = output.strip().split('=')[-1].replace(':', '').upper()
    if not re.fullmatch('[A-F0-9]{40}', value):
        raise ValueError('Invalid project certificate fingerprint')
    return value


def project_identity():
    if not CERTIFICATE.is_file():
        raise ValueError('Project certificate is missing; restore release-signing.pem')
    value = fingerprint()
    match = re.search(r"^PROJECT_SIGNING_CERTIFICATE_SHA1='([A-F0-9]{40})'$",
                      INSTALLER.read_text(), re.MULTILINE)
    if not match or match[1] != value:
        raise ValueError('Installer certificate pin differs from the project certificate')
    return value


def requirement(identity, identifier):
    if not re.fullmatch('[A-F0-9]{40}', identity) or identifier not in PRODUCTS.values():
        raise ValueError('Invalid signing identity or product identifier')
    return f'identifier "{identifier}" and anchor H"{identity}"'


def designated_requirement(app):
    output = run('/usr/bin/codesign', '-d', '-r-', str(app))
    match = re.search(r'(?m)^(?:# )?designated => (.+)$', output)
    if not match:
        raise ValueError('Missing designated requirement')
    return match[1]


def satisfies(app, expression):
    run('/usr/bin/codesign', '--verify', '--strict', '-R', '=' + expression, str(app))


def verify_app(app, identifier, identity=None):
    app = Path(app)
    if app.is_symlink() or not app.is_dir():
        raise ValueError('Expected a real application bundle')
    info = plistlib.loads((app / 'Contents/Info.plist').read_bytes())
    if info.get('CFBundleIdentifier') != identifier:
        raise ValueError('Application bundle identifier differs from its product')
    if info.get('CFBundleExecutable') != identifier.rsplit('.', 1)[-1]:
        raise ValueError('Unexpected application executable')
    run('/usr/bin/codesign', '--verify', '--deep', '--strict', str(app))
    satisfies(app, requirement(identity or project_identity(), identifier))
    dr = designated_requirement(app)
    if 'cdhash' in dr:
        raise ValueError('Build-specific designated requirements are forbidden in releases')
    return {'bundleIdentifier': identifier, 'version': info.get('CFBundleShortVersionString'),
            'designatedRequirement': dr}


def check_previous(new_app, previous_app, identifier, identity=None):
    current = verify_app(new_app, identifier, identity)
    previous = verify_app(previous_app, identifier, identity)
    if version_key(previous['version']) > version_key(current['version']):
        raise ValueError('Previous application is newer than the candidate release')
    # The previous app's *actual* DR may contain constraints beyond our public pin.
    # Check both directions so mixed installation channels and reinstalls agree.
    satisfies(new_app, previous['designatedRequirement'])
    satisfies(previous_app, current['designatedRequirement'])
    return previous


@contextmanager
def mounted_app(dmg):
    directory = Path(tempfile.mkdtemp(prefix='snow-release-signature-'))
    attached = False
    try:
        mount = directory / 'volume'
        mount.mkdir()
        try:
            run('/usr/bin/hdiutil', 'attach', '-readonly', '-nobrowse', '-noautoopen',
                '-mountpoint', str(mount), str(Path(dmg).resolve()))
            attached = True
            apps = list(mount.glob('*.app'))
            if len(apps) != 1:
                raise ValueError('DMG must contain exactly one application')
            yield apps[0]
        finally:
            if attached:
                run('/usr/bin/hdiutil', 'detach', str(mount))
                attached = False
    finally:
        if attached:
            print(f'Could not detach inspection volume; eject it and retain {directory}', file=sys.stderr)
        else:
            shutil.rmtree(directory)


def version_key(version):
    match = re.fullmatch(r'(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z.-]+))?', version)
    if not match:
        raise ValueError('Invalid release version')
    pre = match[4]
    parts = tuple((0, int(p)) if p.isdigit() else (1, p) for p in pre.split('.')) if pre else ()
    return tuple(map(int, match.groups()[:3])), pre is None, parts


def download(url, path):
    # Both initial and redirected URLs must remain HTTPS. curl also supplies
    # bounded timeouts and handles GitHub's signed release-asset redirects.
    run('/usr/bin/curl', '--fail', '--silent', '--show-error', '--location',
        '--proto', '=https', '--proto-redir', '=https', '--connect-timeout', '20',
        '--max-time', '600', '--output', str(path), url)


def select_previous(releases, product, architecture, version):
    candidates = []
    for release in releases:
        match = re.fullmatch(r'v(.+)_snow-shot', release.get('tag_name', ''))
        if release.get('draft') or not match:
            continue
        try:
            key = version_key(match[1])
        except ValueError:
            continue
        if key > version_key(version):
            continue
        name = f'{product}-{match[1]}-macos-{architecture}.dmg'
        assets = [a for a in release.get('assets', []) if a.get('name') == name]
        if len(assets) == 1:
            candidates.append((key, assets[0]))
    if not candidates:
        raise ValueError('No previous published macOS package; supply --previous-dmg explicitly')
    return max(candidates, key=lambda item: item[0])[1]


def previous_package(product, architecture, version, directory):
    metadata = directory / 'releases.json'
    download(f'https://api.github.com/repos/{REPOSITORY}/releases?per_page=100', metadata)
    asset = select_previous(json.loads(metadata.read_text()), product, architecture, version)
    expected = asset.get('digest', '')
    if not re.fullmatch('sha256:[a-f0-9]{64}', expected):
        raise ValueError('Previous release must have a GitHub SHA-256 asset digest')
    name = asset['name']
    url = asset['browser_download_url']
    if not url.startswith(f'https://github.com/{REPOSITORY}/releases/download/'):
        raise ValueError('Unexpected previous release asset URL')
    package = directory / name
    download(url, package)
    if digest(package) != expected[7:]:
        raise ValueError('Previous release checksum mismatch')
    return package


def audit_release(dmg, product, architecture, version, previous_dmg=None):
    identity = project_identity()
    identifier = PRODUCTS[product]
    with tempfile.TemporaryDirectory(prefix='snow-release-predecessor-') as directory:
        previous_dmg = Path(previous_dmg) if previous_dmg else previous_package(
            product, architecture, version, Path(directory))
        previous_hash = digest(previous_dmg)
        with mounted_app(dmg) as app, mounted_app(previous_dmg) as previous_app:
            current = verify_app(app, identifier, identity)
            if current['version'] != version:
                raise ValueError('Packaged app version differs from release version')
            for inspected in (app, previous_app):
                executable = inspected / 'Contents/MacOS' / identifier.rsplit('.', 1)[-1]
                description = run('/usr/bin/file', '-b', str(executable))
                if 'Mach-O' not in description or architecture not in description:
                    raise ValueError('Application architecture differs from release architecture')
            legacy = LEGACY_PACKAGES.get(previous_dmg.name) == previous_hash
            if legacy:
                run('/usr/bin/codesign', '--verify', '--deep', '--strict', str(previous_app))
                info = plistlib.loads((previous_app / 'Contents/Info.plist').read_bytes())
                if info.get('CFBundleIdentifier') != identifier:
                    raise ValueError('Legacy predecessor product mismatch')
                if version_key(version) <= version_key('1.5.8'):
                    raise ValueError('Bump the release version before migrating from published 1.5.8')
                if 'cdhash' not in designated_requirement(previous_app):
                    raise ValueError('Legacy exception must refer to an ad-hoc predecessor')
                status = 'legacy-1.5.8-migration-requires-consent'
            else:
                check_previous(app, previous_app, identifier, identity)
                status = 'compatible'
            return {'schema': 1, **current, 'product': product, 'architecture': architecture,
                    'certificateSha1': identity, 'packageSha256': digest(dmg),
                    'previousPackage': previous_dmg.name, 'previousSha256': previous_hash,
                    'previousRequirement': designated_requirement(previous_app),
                    'continuity': status}


def initialize():
    """Explicit one-time creation; missing publisher keys never cause regeneration."""
    if CERTIFICATE.exists():
        raise ValueError('Project certificate already exists. Restore its original private key; do not regenerate.')
    keychain = Path.home() / 'Library/Keychains/login.keychain-db'
    if not keychain.is_file():
        raise ValueError('Unlock the publishing account login Keychain first')
    state = Path.home() / 'Library/Application Support/Snow Shot/ReleaseSigning'
    if (state / 'release-signing.pem').exists():
        raise ValueError('A release identity already exists on this Mac; restore its public certificate')
    state.mkdir(mode=0o700, parents=True, exist_ok=True)
    marker = state / 'creating'
    with marker.open('x') as stream:
        stream.write('Restore an interrupted identity from the login Keychain before retrying.\n')
    with tempfile.TemporaryDirectory(prefix='snow-release-identity-') as directory:
        work = Path(directory)
        os.chmod(work, 0o700)
        config = work / 'certificate.cnf'
        config.write_text('[req]\nprompt=no\ndistinguished_name=subject\nx509_extensions=signing\n'
                          '[subject]\nCN=Snow Shot Project Release\n'
                          '[signing]\nbasicConstraints=critical,CA:true\n'
                          'keyUsage=critical,digitalSignature,keyCertSign\n'
                          'extendedKeyUsage=codeSigning\n')
        cert = work / 'certificate.pem'
        run('/usr/bin/openssl', 'req', '-new', '-newkey', 'rsa:3072', '-nodes', '-x509',
            '-days', '10950', '-config', str(config), '-keyout', str(work / 'private.pem'),
            '-out', str(cert))
        password = secrets.token_hex(32)
        run('/usr/bin/openssl', 'pkcs12', '-export', '-inkey', str(work / 'private.pem'),
            '-in', str(cert), '-out', str(work / 'identity.p12'), '-passout', 'pass:' + password)
        run('/usr/bin/security', 'import', str(work / 'identity.p12'), '-k', str(keychain),
            '-P', password, '-T', '/usr/bin/codesign')
        value = fingerprint(cert)
        # Preserve public recovery material even if the following repo write fails.
        shutil.copyfile(cert, state / 'release-signing.pem')
        CERTIFICATE.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(cert, CERTIFICATE)
        source = INSTALLER.read_text()
        source, count = re.subn(r"^PROJECT_SIGNING_CERTIFICATE_SHA1='[^']*'$",
                               f"PROJECT_SIGNING_CERTIFICATE_SHA1='{value}'", source, flags=re.MULTILINE)
        if count != 1:
            raise ValueError('Missing installer certificate pin slot; restore it before retrying')
        INSTALLER.write_text(source)
    marker.unlink()
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest='action', required=True)
    actions.add_parser('initialize', help='Create the project identity once in the login Keychain')
    actions.add_parser('identity', help='Resolve and require the existing publisher private key')
    verify = actions.add_parser('verify-app')
    verify.add_argument('app', type=Path)
    verify.add_argument('--product', choices=PRODUCTS, required=True)
    audit = actions.add_parser('audit-release')
    audit.add_argument('dmg', type=Path)
    audit.add_argument('--product', choices=PRODUCTS, required=True)
    audit.add_argument('--architecture', choices=('arm64', 'x86_64'), required=True)
    audit.add_argument('--version', required=True)
    audit.add_argument('--previous-dmg', type=Path)
    audit.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    if args.action == 'initialize':
        print(initialize())
    elif args.action == 'identity':
        value = project_identity()
        identities = run('/usr/bin/security', 'find-identity', '-p', 'codesigning')
        if value not in identities:
            raise ValueError('Project release private key is missing. Import the original identity; never regenerate it.')
        print(value)
    elif args.action == 'verify-app':
        print(json.dumps(verify_app(args.app, PRODUCTS[args.product])))
    else:
        # A failed audit must not leave an old success report behind.
        args.report.unlink(missing_ok=True)
        result = audit_release(args.dmg, args.product, args.architecture, args.version, args.previous_dmg)
        args.report.write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps(result))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print(f'macOS release signing: {error}', file=sys.stderr)
        sys.exit(1)
