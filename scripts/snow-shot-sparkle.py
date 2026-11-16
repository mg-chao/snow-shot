#!/usr/bin/env python3
"""Pinned Sparkle provisioning, bundle signing, and signed release metadata.

Private signing material stays in the publisher's Keychain. No command except
initialize-key can create a key; release preparation verifies the pinned public key.
"""
import argparse
import base64
import hashlib
import json
import plistlib
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from urllib.request import urlopen
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / 'snow_shot/packaging/macos/sparkle.json'
SPARKLE = 'http://www.andymatuschak.org/xml-namespaces/sparkle'
HOSTS = {'github': 'https://github.com/mg-chao/snow-shot/releases/download',
         'gitee': 'https://gitee.com/mg-chao/snow-apps/releases/download'}
ET.register_namespace('sparkle', SPARKLE)


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def manifest():
    return json.loads(MANIFEST.read_text())


def distribution():
    return ROOT / '.tools/sparkle' / manifest()['version']


def run(*args):
    return subprocess.check_output([str(a) for a in args], text=True).strip()


def bootstrap():
    config = manifest()
    cache = distribution().parent
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / ('Sparkle-' + config['version'] + '.zip')
    if not archive.exists():
        with tempfile.NamedTemporaryFile(dir=cache, delete=False) as output:
            temporary = Path(output.name)
            try:
                with urlopen(config['url'], timeout=60) as response:
                    shutil.copyfileobj(response, output)
                output.close()
                if digest(temporary) != config['sha256']:
                    raise ValueError('Sparkle archive checksum mismatch')
                temporary.replace(archive)
            finally:
                temporary.unlink(missing_ok=True)
    if digest(archive) != config['sha256']:
        raise ValueError('Sparkle archive checksum mismatch')
    if not (distribution() / 'bin/sign_update').exists():
        with tempfile.TemporaryDirectory(dir=cache) as temporary:
            run('/usr/bin/ditto', '-x', '-k', archive, temporary)
            Path(temporary).rename(distribution())
    return distribution()


def public_key(initialize=False):
    config = manifest()
    tool = distribution() / 'bin/generate_keys'
    if initialize:
        # Sparkle reuses an existing account; never request export of the private key.
        subprocess.run([str(tool), '--account', config['keychainAccount']], check=True)
    key = run(tool, '--account', config['keychainAccount'], '-p')
    if len(base64.b64decode(key, validate=True)) != 32:
        raise ValueError('Invalid Sparkle public key')
    if config['publicKey'] and key != config['publicKey']:
        raise ValueError('Keychain key differs from the pinned Sparkle public key')
    if initialize and not config['publicKey']:
        config['publicKey'] = key
        MANIFEST.write_text(json.dumps(config, indent=2) + '\n')
    elif not config['publicKey']:
        raise ValueError('Initialize and pin the Sparkle release key first')
    return key


def sign(path, verify=False, signature=None):
    args = [distribution() / 'bin/sign_update', '--account', manifest()['keychainAccount']]
    if verify:
        args.append('--verify')
    else:
        args.append('-p')
    args.append(path)
    if signature:
        args.append(signature)
    return run(*args)


def macho(path):
    if path.is_symlink() or not path.is_file():
        return False
    with path.open('rb') as stream:
        return stream.read(4) in (b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf',
                                  b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca')


def sign_framework(framework, identity):
    framework = Path(framework)
    if framework.name != 'Sparkle.framework' or not (framework / 'Sparkle').exists():
        raise ValueError('Expected the complete Sparkle.framework')
    binaries = [p for p in framework.rglob('*') if macho(p)]
    bundles = sorted((p for p in framework.rglob('*') if p.suffix in ('.app', '.xpc') and p.is_dir()),
                     key=lambda p: len(p.parts), reverse=True)
    for path in binaries + bundles + [framework]:
        run('/usr/bin/codesign', '--force', '--sign', identity,
            '--preserve-metadata=identifier,entitlements,flags,runtime', path)
    run('/usr/bin/codesign', '--verify', '--deep', '--strict', framework)


def feed_xml(product, version, architecture, size, signature, host, bundle_version, minimum_os):
    if product not in ('snow-shot', 'snow-shot-mini') or architecture not in ('arm64', 'x86_64'):
        raise ValueError('Invalid macOS product or architecture')
    if not re.fullmatch(r'\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?', version):
        raise ValueError('Invalid release version')
    root = ET.Element('rss', {'version': '2.0'})
    channel = ET.SubElement(root, 'channel')
    ET.SubElement(channel, 'title').text = product
    item = ET.SubElement(channel, 'item')
    ET.SubElement(item, 'title').text = 'Version ' + version
    ET.SubElement(item, '{' + SPARKLE + '}version').text = bundle_version
    ET.SubElement(item, '{' + SPARKLE + '}shortVersionString').text = version
    ET.SubElement(item, '{' + SPARKLE + '}minimumSystemVersion').text = minimum_os
    name = f'{product}-{version}-macos-{architecture}.dmg'
    ET.SubElement(item, 'enclosure', {'url': f'{HOSTS[host]}/v{version}_snow-shot/{name}',
                                    'length': str(size), 'type': 'application/octet-stream',
                                    '{' + SPARKLE + '}edSignature': signature})
    return ET.tostring(root, encoding='utf-8', xml_declaration=True) + b'\n'


def prepare(image, product, version, architecture, output):
    image, output = Path(image), Path(output)
    key = public_key()
    # Inspect the sealed bundle inside the final DMG rather than trusting build metadata.
    with tempfile.TemporaryDirectory(prefix='snow-sparkle-image-') as temporary:
        run('/usr/bin/hdiutil', 'attach', '-readonly', '-nobrowse', '-mountpoint', temporary, image)
        try:
            apps = list(Path(temporary).glob('*.app'))
            if len(apps) != 1:
                raise ValueError('Update DMG must contain exactly one application')
            with (apps[0] / 'Contents/Info.plist').open('rb') as stream:
                info = plistlib.load(stream)
            expected = 'com.snowshot.snow_shot' + ('_mini' if product == 'snow-shot-mini' else '')
            if info.get('CFBundleIdentifier') != expected or info.get('SUPublicEDKey') != key:
                raise ValueError('Update bundle identity or Sparkle key differs')
            if not info.get('SURequireSignedFeed') or not info.get('SUVerifyUpdateBeforeExtraction'):
                raise ValueError('Release is missing Sparkle verification requirements')
            if info['CFBundleVersion'] != version.split('-')[0]:
                raise ValueError('Update bundle version differs')
            run('/usr/bin/codesign', '--verify', '--deep', '--strict', apps[0])
        finally:
            run('/usr/bin/hdiutil', 'detach', temporary)
    signature = sign(image)
    sign(image, verify=True, signature=signature)
    report = {'schema': 1, 'product': product, 'version': version, 'architecture': architecture,
              'packageSha256': digest(image), 'size': image.stat().st_size, 'publicKey': key,
              'edSignature': signature, 'feeds': {}}
    with tempfile.TemporaryDirectory(prefix='snow-sparkle-feed-') as temporary:
        for host in HOSTS:
            name = f'{product}-macos-{architecture}.xml'
            path = Path(temporary) / name
            path.write_bytes(feed_xml(product, version, architecture, report['size'], signature, host,
                                      info['CFBundleVersion'], info.get('LSMinimumSystemVersion', '15.0')))
            sign(path)
            sign(path, verify=True)
            report['feeds'][host] = {'name': name, 'content': path.read_text(), 'sha256': digest(path)}
    output.write_text(json.dumps(report, indent=2) + '\n')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    for command in ('bootstrap', 'initialize-key', 'check-key'):
        sub.add_parser(command)
    signer = sub.add_parser('sign-framework')
    signer.add_argument('--framework', type=Path, required=True)
    signer.add_argument('--identity', required=True)
    release = sub.add_parser('prepare-release')
    release.add_argument('--dmg', type=Path, required=True)
    release.add_argument('--product', required=True)
    release.add_argument('--version', required=True)
    release.add_argument('--architecture', required=True)
    release.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'bootstrap':
        print(bootstrap())
    elif args.command == 'initialize-key':
        bootstrap()
        public_key(initialize=True)
    elif args.command == 'check-key':
        public_key()
    elif args.command == 'sign-framework':
        sign_framework(args.framework, args.identity)
    elif args.command == 'prepare-release':
        prepare(args.dmg, args.product, args.version, args.architecture, args.output)


if __name__ == '__main__':
    main()
