#!/usr/bin/env python3
"""Opt-in native Sparkle integration tests using disposable apps and a loopback feed."""
import argparse
import base64
from functools import partial
import http.server
import importlib.util
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile
import threading
import time
import unittest
import uuid
from unittest.mock import patch
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / '.tools/sparkle/2.10.1/bin'
FIXTURE = ROOT / 'build/snow-shot-macos-arm64-debug/snow_shot/snow-shot-sparkle-fixture.app'
NS = 'http://www.andymatuschak.org/xml-namespaces/sparkle'
ET.register_namespace('sparkle', NS)


def run(*args):
    return subprocess.check_output([str(a) for a in args], stderr=subprocess.PIPE).decode().strip()


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


class NativeSparkle(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='snow-sparkle-native-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), partial(QuietHandler, directory=str(self.root)))
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        self.addCleanup(self.server.server_close)
        self.addCleanup(self.server.shutdown)
        self.base = f'http://127.0.0.1:{self.server.server_port}'
        self.identifier = 'com.snowshot.tests.sparkle.' + uuid.uuid4().hex
        self.addCleanup(lambda: subprocess.run(['defaults', 'delete', self.identifier], capture_output=True))
        # Independent, ephemeral test key; production Keychain entries are never used.
        keys = json.loads(run('xcrun', 'swift', '-e',
            'import Foundation; import CryptoKit; let k = Curve25519.Signing.PrivateKey(); '
            'print(String(data: try! JSONSerialization.data(withJSONObject: '
            '["seed": k.rawRepresentation.base64EncodedString(), "public": '
            'k.publicKey.rawRepresentation.base64EncodedString()]), encoding: .utf8)!)'))
        self.key = self.root / 'test-key'
        self.key.write_text(keys['seed'])
        self.key.chmod(0o600)
        self.public = keys['public']
        self.app = self.bundle(self.root / 'installed', '1.0.0')
        self.processes = []
        self.addCleanup(self.stop)

    def stop(self):
        # Sparkle's explicit relaunch starts an independent successor process.
        successor = self.status().get('pid')
        if successor and all(process.pid != successor for process in self.processes):
            self.command('quit')
            for _ in range(50):
                try:
                    os.kill(successor, 0)
                except ProcessLookupError:
                    break
                time.sleep(0.1)
            else:
                os.kill(successor, 15)
        for process in self.processes:
            if process.poll() is None:
                self.command('quit')
                try:
                    process.wait(5)
                except subprocess.TimeoutExpired:
                    process.terminate()
                    process.wait(5)

    def bundle(self, directory, version):
        directory.mkdir()
        app = directory / 'Sparkle Fixture.app'
        run('/usr/bin/ditto', FIXTURE, app)
        plist = app / 'Contents/Info.plist'
        info = plistlib.loads(plist.read_bytes())
        info.update(CFBundleIdentifier=self.identifier, CFBundleName='Sparkle Fixture',
                    CFBundleVersion=version, CFBundleShortVersionString=version, LSUIElement=True,
                    SUFeedURL=self.base + '/feed.xml', SnowSparkleMirrorFeedURL=self.base + '/mirror.xml',
                    SUPublicEDKey=self.public, SUEnableAutomaticChecks=True, SUAutomaticallyUpdate=True,
                    SUAllowsAutomaticUpdates=True, SURequireSignedFeed=True, SUVerifyUpdateBeforeExtraction=True,
                    NSAppTransportSecurity={'NSAllowsLocalNetworking': True})
        plist.write_bytes(plistlib.dumps(info))
        configuration = app / 'Contents/Resources/fixture.json'
        configuration.parent.mkdir(exist_ok=True)
        configuration.write_text(json.dumps({'root': str(self.root)}))
        run('python3', ROOT / 'scripts/snow-shot-sparkle.py', 'sign-framework', '--framework',
            app / 'Contents/Frameworks/Sparkle.framework', '--identity', '-')
        run('/usr/bin/codesign', '--force', '--sign', '-', app)
        return app

    def feed(self, version='2.0.0', corrupt=False, mirror_only=False, bad_download=False, disk_image=False):
        update = self.bundle(self.root / 'update', version)
        archive = self.root / ('update.dmg' if disk_image else 'update.zip')
        if disk_image:
            run('/usr/bin/hdiutil', 'create', '-quiet', '-fs', 'HFS+', '-format', 'UDZO',
                '-srcfolder', update.parent, archive)
        else:
            run('/usr/bin/ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', update, archive)
        signature = run(TOOLS / 'sign_update', '--ed-key-file', self.key, '-p', archive)
        if corrupt:
            signature = base64.b64encode(bytes(64)).decode()
        root = ET.Element('rss', {'version': '2.0'})
        item = ET.SubElement(ET.SubElement(root, 'channel'), 'item')
        ET.SubElement(item, '{' + NS + '}version').text = version
        ET.SubElement(item, '{' + NS + '}shortVersionString').text = version
        ET.SubElement(item, 'enclosure', {'url': self.base + '/' + archive.name, 'length': str(archive.stat().st_size),
                                        'type': 'application/octet-stream', '{' + NS + '}edSignature': signature})
        path = self.root / ('mirror.xml' if mirror_only else 'feed.xml')
        path.write_bytes(ET.tostring(root, xml_declaration=True, encoding='utf-8'))
        run(TOOLS / 'sign_update', '--ed-key-file', self.key, path)
        if not mirror_only:
            shutil.copyfile(path, self.root / 'mirror.xml')
        if bad_download:
            item.find('enclosure').set('url', self.base + '/missing.zip')
            path.write_bytes(ET.tostring(root, xml_declaration=True, encoding='utf-8'))
            run(TOOLS / 'sign_update', '--ed-key-file', self.key, path)

    def launch(self, mode='next_launch'):
        (self.root / 'status.json').unlink(missing_ok=True)
        environment = dict(os.environ, SNOW_SPARKLE_FIXTURE_DIR=str(self.root), SNOW_SPARKLE_FIXTURE_MODE=mode)
        log = (self.root / 'application.log').open('a')
        self.addCleanup(log.close)
        process = subprocess.Popen([str(self.app / 'Contents/MacOS/snow-shot-sparkle-fixture')], env=environment,
                                   stdout=log, stderr=log)
        self.processes.append(process)
        return process

    def status(self):
        path = self.root / 'status.json'
        return json.loads(path.read_text()) if path.exists() else {}

    def wait(self, predicate, seconds=40):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if predicate():
                return
            time.sleep(0.1)
        self.fail('Native update timed out: ' + str(self.status()) + '\n' + (self.root / 'application.log').read_text()[-6000:])

    def command(self, command):
        (self.root / 'command').write_text(command)

    def version(self):
        return plistlib.loads((self.app / 'Contents/Info.plist').read_bytes())['CFBundleVersion']

    def test_prepared_update_waits_for_quit_and_next_launch(self):
        self.feed(disk_image=True)
        process = self.launch()
        self.wait(lambda: self.status().get('ready'))
        self.assertEqual(self.version(), '1.0.0')
        self.command('close')
        self.wait(lambda: self.status().get('windowVisible') is False)
        time.sleep(0.5)
        self.assertIsNone(process.poll(), 'closing the last window must not terminate the application')
        self.command('manual')
        time.sleep(0.3)
        self.assertTrue(self.status()['ready'])
        self.command('quit')
        process.wait(10)
        self.wait(lambda: self.version() == '2.0.0')
        successor = self.launch('manual')
        self.wait(lambda: self.status().get('state') == 1)
        self.assertIsNone(successor.poll())

    def test_no_update_and_failed_check_keep_application_running(self):
        self.feed(version='1.0.0')
        process = self.launch()
        self.wait(lambda: self.status().get('state') == 1)
        self.assertIsNone(process.poll())
        (self.root / 'feed.xml').unlink()
        (self.root / 'mirror.xml').unlink()
        self.command('check')
        self.wait(lambda: self.status().get('state') == 8)
        self.assertIsNone(process.poll())
        self.assertEqual(self.version(), '1.0.0')

    def test_mirror_recovers_transport_failure(self):
        self.feed(mirror_only=True)
        process = self.launch()
        self.wait(lambda: self.status().get('ready'))
        self.assertIsNone(process.poll())
        self.command('quit')
        process.wait(10)
        self.wait(lambda: self.version() == '2.0.0')

    def test_bad_archive_signature_does_not_install(self):
        self.feed(corrupt=True)
        process = self.launch()
        self.wait(lambda: self.status().get('state') == 8)
        self.assertIsNone(process.poll())
        self.command('quit')
        process.wait(10)
        self.assertEqual(self.version(), '1.0.0')

    def test_manual_download_waits_for_explicit_restart(self):
        self.feed()
        process = self.launch('manual')
        self.wait(lambda: self.status().get('state') == 1)
        self.command('check')
        self.wait(lambda: self.status().get('state') == 3)
        self.assertEqual(self.version(), '1.0.0')
        completed = self.status()['completedOperations']
        self.command('check')
        self.wait(lambda: self.status()['completedOperations'] > completed)
        self.command('download')
        self.wait(lambda: self.status().get('ready'))
        self.assertIsNone(process.poll())
        self.command('restart')
        process.wait(10)
        self.wait(lambda: self.version() == '2.0.0')
        self.wait(lambda: self.status().get('pid') != process.pid and self.status().get('state') == 1)

    def test_bad_feed_signature_does_not_fall_back(self):
        self.feed()
        path = self.root / 'feed.xml'
        path.write_bytes(path.read_bytes().replace(b'<channel>', b'<channel>\n', 1))
        process = self.launch()
        self.wait(lambda: self.status().get('state') == 8)
        self.assertIsNone(process.poll())
        self.assertFalse(self.status()['ready'], 'a signature error must not try the valid mirror')

    def test_manual_download_recovers_through_mirror(self):
        self.feed(bad_download=True)
        process = self.launch('manual')
        self.wait(lambda: self.status().get('state') == 1)
        self.command('check')
        self.wait(lambda: self.status().get('state') == 3)
        self.command('download')
        self.wait(lambda: self.status().get('ready') and self.status().get('operation') == 'download'
                  and self.status().get('outcome') == 'success')
        self.assertIsNone(process.poll())

    def test_release_preparation_verifies_final_dmg_and_signed_feeds(self):
        spec = importlib.util.spec_from_file_location('sparkle_release', ROOT / 'scripts/snow-shot-sparkle.py')
        release = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(release)
        spec = importlib.util.spec_from_file_location('sparkle_publisher', ROOT / 'scripts/publish-snow-shot-sparkle.py')
        publisher = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(publisher)

        def sign(path, verify=False, signature=None):
            args = [TOOLS / 'sign_update', '--ed-key-file', self.key, '--verify' if verify else '-p', path]
            if signature:
                args.append(signature)
            return run(*args)

        # Keep production identities confined to bundles that are never launched.
        for product, identifier in [('snow-shot', 'com.snowshot.snow_shot'),
                                    ('snow-shot-mini', 'com.snowshot.snow_shot_mini')]:
            with self.subTest(product=product):
                plist = self.app / 'Contents/Info.plist'
                info = plistlib.loads(plist.read_bytes())
                info['CFBundleIdentifier'] = identifier
                plist.write_bytes(plistlib.dumps(info))
                run('/usr/bin/codesign', '--force', '--sign', '-', self.app)
                image = self.root / (product + '.dmg')
                run('/usr/bin/hdiutil', 'create', '-quiet', '-fs', 'HFS+', '-format', 'UDZO',
                    '-srcfolder', self.app.parent, image)
                with patch.object(release, 'public_key', return_value=self.public), \
                        patch.object(release, 'sign', side_effect=sign):
                    report = release.prepare(image, product, '1.0.0', 'arm64', self.root / 'receipt.json')
                publisher.validate_report(report, image, self.public)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--fixture', type=Path, default=FIXTURE)
    args, remaining = parser.parse_known_args()
    FIXTURE = args.fixture.resolve()
    unittest.main(argv=[__file__] + remaining)
