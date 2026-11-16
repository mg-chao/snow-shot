"""Focused deterministic Sparkle release and packaging checks; no production access."""
import base64
import hashlib
import importlib.util
import json
import copy
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'scripts' / file)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


sparkle = load('sparkle', 'snow-shot-sparkle.py')
publisher = load('publisher', 'publish-snow-shot-sparkle.py')


class SparkleRelease(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.image = self.root / 'snow-shot-2.0.0-macos-arm64.dmg'
        self.image.write_bytes(b'audited DMG')
        self.key = base64.b64encode(bytes(32)).decode()
        self.signature = base64.b64encode(bytes(64)).decode()
        self.report = {'schema': 1, 'product': 'snow-shot', 'version': '2.0.0', 'architecture': 'arm64',
                       'size': self.image.stat().st_size, 'packageSha256': sparkle.digest(self.image),
                       'publicKey': self.key, 'edSignature': self.signature, 'feeds': {}}
        for host in publisher.REPOSITORIES:
            content = sparkle.feed_xml('snow-shot', '2.0.0', 'arm64', self.report['size'],
                                       self.signature, host, '2.0.0', '15.0')
            content += f'<!-- sparkle-signatures:\nedSignature: {self.signature}\nlength: {len(content)}\n-->\n'.encode()
            self.report['feeds'][host] = {'name': 'snow-shot-macos-arm64.xml', 'content': content.decode(),
                                         'sha256': hashlib.sha256(content).hexdigest()}

    def test_receipt_binds_packages_and_both_signed_feeds(self):
        publisher.validate_report(self.report, self.image, self.key)
        self.image.write_bytes(b'changed DMG')
        with self.assertRaisesRegex(ValueError, 'audited package'):
            publisher.validate_report(self.report, self.image, self.key)

    def test_feed_transfer_corruption_is_rejected(self):
        self.report['feeds']['gitee']['content'] += '\n'
        with self.assertRaisesRegex(ValueError, 'feed bytes'):
            publisher.validate_report(self.report, self.image, self.key)

    def test_wrong_public_key_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'pinned key'):
            publisher.validate_report(self.report, self.image, 'different')

    def test_invalid_product_and_cross_host_assets_are_rejected(self):
        with self.assertRaises(ValueError):
            sparkle.feed_xml('bad', '2.0.0', 'arm64', 1, self.signature, 'github', '2.0.0', '15.0')
        self.report['feeds']['gitee'] = dict(self.report['feeds']['github'])
        with self.assertRaisesRegex(ValueError, 'audited release'):
            publisher.validate_report(self.report, self.image, self.key)

    def test_upstream_version_and_archive_are_pinned(self):
        manifest = sparkle.manifest()
        self.assertEqual(manifest['version'], '2.10.1')
        self.assertRegex(manifest['sha256'], r'^[0-9a-f]{64}$')
        self.assertEqual(len(base64.b64decode(manifest['publicKey'], validate=True)), 32)

    def test_native_components_are_signed_before_framework(self):
        framework = self.root / 'Sparkle.framework'
        binary = framework / 'Versions/B/Sparkle'
        binary.parent.mkdir(parents=True)
        binary.write_bytes(b'\xcf\xfa\xed\xfe')
        (framework / 'Sparkle').symlink_to('Versions/B/Sparkle')
        app = framework / 'Versions/B/Updater.app'
        nested = app / 'Contents/MacOS/Updater'
        nested.parent.mkdir(parents=True)
        nested.write_bytes(b'\xcf\xfa\xed\xfe')
        with patch.object(sparkle, 'run') as calls:
            sparkle.sign_framework(framework, '-')
        paths = [call.args[-1] for call in calls.call_args_list]
        self.assertLess(paths.index(nested), paths.index(app))
        self.assertEqual(paths[-2:], [framework, framework])

    def test_feed_publication_is_idempotent_and_preserves_other_architectures(self):
        remote = self.root / 'feeds.git'
        subprocess.run(['git', 'init', '--bare', '--quiet', str(remote)], check=True)
        with patch.dict(publisher.REPOSITORIES, {'github': str(remote)}):
            publisher.publish('github', [self.report], token='fixture')
            first = subprocess.check_output(['git', '-C', str(remote), 'rev-parse', publisher.BRANCH])
            publisher.publish('github', [self.report], token='fixture')
            self.assertEqual(first, subprocess.check_output(['git', '-C', str(remote), 'rev-parse', publisher.BRANCH]))
            other = copy.deepcopy(self.report)
            other['feeds']['github']['name'] = 'snow-shot-macos-x86_64.xml'
            publisher.publish('github', [other], token='fixture')
            publisher.publish('github', [self.report, other], token='fixture', verify_only=True)
            changed = copy.deepcopy(self.report)
            changed['feeds']['github']['content'] += '\n'
            with self.assertRaisesRegex(ValueError, 'equal or older'):
                publisher.publish('github', [changed], token='fixture')
            with self.assertRaisesRegex(ValueError, 'audited bytes'):
                publisher.publish('github', [changed], token='fixture', verify_only=True)


if __name__ == '__main__':
    unittest.main()
