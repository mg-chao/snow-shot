#!/usr/bin/env python3
"""Publish audited Sparkle feeds only after the corresponding release assets are verified."""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
NAMESPACE = {'s': 'http://www.andymatuschak.org/xml-namespaces/sparkle'}
REPOSITORIES = {'github': 'https://github.com/mg-chao/snow-shot.git',
                'gitee': 'https://gitee.com/mg-chao/snow-apps.git'}
BRANCH = 'sparkle-appcasts'


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def validate_report(report, image, public_key):
    product, arch, version = report.get('product'), report.get('architecture'), report.get('version', '')
    if (report.get('schema') != 1 or product not in ('snow-shot', 'snow-shot-mini') or
            arch not in ('arm64', 'x86_64') or (product == 'snow-shot-mini' and arch != 'arm64') or
            not re.fullmatch(r'\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?', version)):
        raise ValueError('Invalid Sparkle release identity')
    if (not public_key or report.get('publicKey') != public_key or
            report.get('packageSha256') != sha256(image) or report.get('size') != Path(image).stat().st_size):
        raise ValueError('Sparkle signing receipt differs from the audited package or pinned key')
    signature = report.get('edSignature', '')
    if len(base64.b64decode(signature, validate=True)) != 64:
        raise ValueError('Invalid Sparkle archive signature')
    for host in REPOSITORIES:
        feed = report['feeds'][host]
        content = feed['content'].encode('utf-8')
        if feed['name'] != f'{product}-macos-{arch}.xml' or hashlib.sha256(content).hexdigest() != feed['sha256']:
            raise ValueError('Signed Sparkle feed bytes differ from the signing receipt')
        # sign_update verified this signed content on the publishing Mac. Validate the
        # envelope and transfer hashes here without requiring its private Keychain on Windows.
        signing = re.search(rb'<!-- sparkle-signatures:\nedSignature: ([A-Za-z0-9+/=]+)\nlength: ([0-9]+)\n-->\n?$', content)
        if (not signing or int(signing[2]) != signing.start() or
                len(base64.b64decode(signing[1], validate=True)) != 64):
            raise ValueError('Missing or malformed signed appcast envelope')
        item = ET.fromstring(content).find('channel/item')
        enclosure = item.find('enclosure') if item is not None else None
        repo = REPOSITORIES[host].removesuffix('.git')
        expected = f'{repo}/releases/download/v{version}_snow-shot/{product}-{version}-macos-{arch}.dmg'
        if (enclosure is None or enclosure.get('url') != expected or
                enclosure.get('length') != str(report['size']) or
                enclosure.get('{' + NAMESPACE['s'] + '}edSignature') != signature or
                item.findtext('s:version', namespaces=NAMESPACE) != version.split('-')[0]):
            raise ValueError('Appcast does not describe the audited release package')
    return report


def run(*args, environment=None):
    return subprocess.check_output(args, env=environment, text=True, stderr=subprocess.PIPE).strip()


def publish(host, reports, token=None, username=None, verify_only=False):
    if not reports:
        return
    if host == 'github':
        token = token or run('gh', 'auth', 'token', '--hostname', 'github.com')
        username = 'x-access-token'
    if not token or not username:
        raise ValueError('Feed publication requires the existing release-host credentials')
    header = base64.b64encode(f'{username}:{token}'.encode()).decode()
    environment = dict(os.environ, GIT_CONFIG_COUNT='1',
                       GIT_CONFIG_KEY_0=f'http.https://{host}.com/.extraheader',
                       GIT_CONFIG_VALUE_0=f'Authorization: Basic {header}')
    with tempfile.TemporaryDirectory(prefix='snow-sparkle-publish-') as temporary:
        directory = Path(temporary)
        def git(*args):
            return run('git', '-C', str(directory), *args, environment=environment)
        git('init', '--quiet')
        git('remote', 'add', 'origin', REPOSITORIES[host])
        exists = git('ls-remote', '--heads', 'origin', 'refs/heads/' + BRANCH)
        if exists:
            git('fetch', '--depth=1', 'origin', BRANCH)
            git('checkout', '-b', BRANCH, 'FETCH_HEAD')
        else:
            if verify_only:
                raise ValueError('The published Sparkle feed branch is missing')
            git('checkout', '--orphan', BRANCH)
        names = set()
        for report in reports:
            feed = report['feeds'][host]
            if feed['name'] in names:
                raise ValueError('Duplicate Sparkle feed in publication')
            names.add(feed['name'])
            path = directory / feed['name']
            content = feed['content'].encode('utf-8')
            if verify_only:
                if not path.exists() or path.read_bytes() != content:
                    raise ValueError('Published Sparkle feed differs from audited bytes')
                continue
            if path.exists() and path.read_bytes() != content:
                previous = ET.fromstring(path.read_bytes()).findtext('channel/item/s:version', namespaces=NAMESPACE)
                current = report['version'].split('-')[0]
                if not previous or tuple(map(int, current.split('.'))) <= tuple(map(int, previous.split('.'))):
                    raise ValueError('Refusing to replace a Sparkle feed with an equal or older build')
            path.write_bytes(content)
            git('add', '--', feed['name'])
        if not verify_only and git('status', '--porcelain'):
            git('-c', 'user.name=Snow Shot releases', '-c', 'user.email=releases@snowshot.top',
                'commit', '-m', 'build(updates): publish verified Sparkle appcasts')
            # No force push: a concurrent release must be reconciled and reverified.
            git('push', 'origin', 'HEAD:refs/heads/' + BRANCH)
        if not verify_only:
            git('fetch', '--depth=1', 'origin', BRANCH)
            for report in reports:
                feed = report['feeds'][host]
                actual = subprocess.check_output(['git', '-C', str(directory), 'show', 'FETCH_HEAD:' + feed['name']], env=environment)
                if actual != feed['content'].encode():
                    raise ValueError('Published feed verification failed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', choices=REPOSITORIES, required=True)
    parser.add_argument('--report', type=Path, action='append', required=True)
    parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    key = json.loads((ROOT / 'snow_shot/packaging/macos/sparkle.json').read_text())['publicKey']
    reports = []
    for path in args.report:
        image = Path(str(path).removesuffix('.sparkle.json') + '.dmg')
        reports.append(validate_report(json.loads(path.read_text()), image, key))
    publish(args.host, reports, os.environ.get('GITEE_TOKEN') if args.host == 'gitee' else None,
            os.environ.get('GITEE_USERNAME'), args.verify)


if __name__ == '__main__':
    main()
