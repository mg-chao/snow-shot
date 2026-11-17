#!/usr/bin/env python3
"""Focused release-identity tests. Native cases use a disposable Keychain, never TCC."""
from contextlib import contextmanager
import importlib.util
import json
import os
from pathlib import Path
import plistlib
import re
import secrets
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('signing', ROOT / 'scripts/snow-shot-macos-signing.py')
signing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(signing)
ID = 'com.snowshot.snow_shot'


class ReleaseSelection(unittest.TestCase):
    def release(self, version, **extra):
        return {'tag_name': f'v{version}_snow-shot',
                'assets': [{'name': f'snow-shot-{version}-macos-arm64.dmg'}], **extra}

    def test_chooses_latest_matching_published_predecessor(self):
        releases = [self.release('1.5.8'), self.release('1.6.0'),
                    self.release('1.5.9-beta.2'), self.release('1.5.9', draft=True)]
        asset = signing.select_previous(releases, 'snow-shot', 'arm64', '1.5.9')
        self.assertEqual(asset['name'], 'snow-shot-1.5.9-beta.2-macos-arm64.dmg')
        with self.assertRaisesRegex(ValueError, 'No previous'):
            signing.select_previous(releases, 'snow-shot', 'x86_64', '1.5.9')

    def test_initialization_never_replaces_existing_project_identity(self):
        with self.assertRaisesRegex(ValueError, 'already exists'):
            signing.initialize()

    def test_installer_pin_must_match_the_committed_certificate(self):
        self.assertEqual(signing.project_identity(), signing.fingerprint())
        with tempfile.TemporaryDirectory() as directory:
            installer = Path(directory) / 'installer.sh'
            installer.write_text("PROJECT_SIGNING_CERTIFICATE_SHA1='" + '0' * 40 + "'\n")
            with patch.object(signing, 'INSTALLER', installer):
                with self.assertRaisesRegex(ValueError, 'pin differs'):
                    signing.project_identity()

    def test_unsafe_identifiers_are_rejected(self):
        for identity, identifier in [('A' * 40, 'other.app'), ('not a hash', ID)]:
            with self.assertRaises(ValueError):
                signing.requirement(identity, identifier)

    def test_missing_private_key_never_generates_a_new_identity(self):
        with patch.object(sys, 'argv', ['signing', 'identity']), \
                patch.object(signing, 'project_identity', return_value='A' * 40), \
                patch.object(signing, 'run', return_value='0 identities'), \
                patch.object(signing, 'initialize') as initialize:
            with self.assertRaisesRegex(ValueError, 'private key is missing'):
                signing.main()
            initialize.assert_not_called()

    def test_failed_detach_preserves_inspection_tree(self):
        with tempfile.TemporaryDirectory() as directory:
            inspection = Path(directory) / 'inspection'
            inspection.mkdir()
            def command(*args):
                if 'attach' in args:
                    (inspection / 'volume/Snow Shot.app').mkdir()
                if 'detach' in args:
                    raise ValueError('busy')
                return ''
            with patch.object(signing.tempfile, 'mkdtemp', return_value=str(inspection)), \
                    patch.object(signing, 'run', side_effect=command):
                with self.assertRaisesRegex(ValueError, 'busy'):
                    with signing.mounted_app(Path(directory) / 'test.dmg'):
                        pass
            self.assertTrue((inspection / 'volume/Snow Shot.app').exists())


@unittest.skipUnless(sys.platform == 'darwin', 'Native macOS code signing required')
class NativeSigning(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='snow-signing-tests-')
        cls.root = Path(cls.temporary.name)
        cls.keychain = cls.root / 'fixture.keychain-db'
        password = secrets.token_hex(24)
        cls.addClassCleanup(cls.temporary.cleanup)
        search_list = ('/usr/bin/security', 'list-keychains', '-d', 'user')
        original_keychains = shlex.split(signing.run(*search_list))
        cls.addClassCleanup(lambda: subprocess.run(
            ['/usr/bin/security', 'delete-keychain', str(cls.keychain)], capture_output=True))
        cls.addClassCleanup(signing.run, *search_list, '-s', *original_keychains)
        signing.run('/usr/bin/security', 'create-keychain', '-p', password, str(cls.keychain))
        signing.run('/usr/bin/security', 'unlock-keychain', '-p', password, str(cls.keychain))
        cls.identities = []
        for index in range(2):
            config = cls.root / f'certificate-{index}.cnf'
            config.write_text('[req]\nprompt=no\ndistinguished_name=subject\nx509_extensions=signing\n'
                              f'[subject]\nCN=Snow Disposable Signing Fixture {index}\n'
                              '[signing]\nbasicConstraints=critical,CA:true\n'
                              'keyUsage=critical,digitalSignature,keyCertSign\nextendedKeyUsage=codeSigning\n')
            cert, key, p12 = (cls.root / f'{index}.{ext}' for ext in ('pem', 'key', 'p12'))
            signing.run('/usr/bin/openssl', 'req', '-new', '-newkey', 'rsa:2048', '-nodes',
                        '-x509', '-days', '1', '-config', str(config), '-keyout', str(key), '-out', str(cert))
            signing.run('/usr/bin/openssl', 'pkcs12', '-export', '-inkey', str(key),
                        '-in', str(cert), '-out', str(p12), '-passout', 'pass:' + password)
            signing.run('/usr/bin/security', 'import', str(p12), '-k', str(cls.keychain),
                        '-P', password, '-T', '/usr/bin/codesign')
            cls.identities.append(signing.fingerprint(cert))
        signing.run('/usr/bin/security', 'set-key-partition-list', '-S', 'apple-tool:,apple:',
                    '-s', '-k', password, str(cls.keychain))
        # codesign uses the user search list to build the certificate chain even with --keychain.
        signing.run(*search_list, '-s', str(cls.keychain), *original_keychains)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=self.root)
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)

    def app(self, name, version, identity=0, identifier=ID, custom_requirement=None):
        app = self.directory / (name + '.app')
        executable = identifier.rsplit('.', 1)[-1]
        (app / 'Contents/MacOS').mkdir(parents=True)
        (app / 'Contents/Resources').mkdir()
        shutil.copyfile('/usr/bin/true', app / 'Contents/MacOS' / executable)
        (app / 'Contents/MacOS' / executable).chmod(0o755)
        (app / 'Contents/Info.plist').write_bytes(plistlib.dumps({
            'CFBundleIdentifier': identifier, 'CFBundleExecutable': executable,
            'CFBundlePackageType': 'APPL', 'CFBundleVersion': version,
            'CFBundleShortVersionString': version}))
        (app / 'Contents/Resources/payload').write_text('Different payload for ' + version)
        args = ['/usr/bin/codesign', '--force', '--sign',
                '-' if identity is None else self.identities[identity], '--keychain', str(self.keychain)]
        if custom_requirement:
            args += ['-r', '=designated => ' + custom_requirement]
        signing.run(*args, str(app))
        return app

    def test_changed_builds_with_same_certificate_satisfy_each_other(self):
        a, b = self.app('old', '1.5.9'), self.app('new', '1.5.10')
        self.assertNotEqual(signing.digest(a / 'Contents/MacOS/snow_shot'),
                            signing.digest(b / 'Contents/MacOS/snow_shot'))
        self.assertEqual(signing.check_previous(b, a, ID, self.identities[0])['version'], '1.5.9')

    def test_rejects_other_certificate_adhoc_and_wrong_product(self):
        for name, identity, identifier in [('other', 1, ID), ('adhoc', None, ID),
                                           ('wrong-product', 0, 'com.snowshot.snow_shot_mini')]:
            with self.subTest(name=name):
                app = self.app(name, '1.5.9', identity, identifier)
                with self.assertRaises(ValueError):
                    signing.verify_app(app, ID, self.identities[0])

    def test_rejects_tampered_signed_resources(self):
        app = self.app('tampered', '1.5.9')
        (app / 'Contents/Resources/payload').write_text('tampered')
        with self.assertRaises(ValueError):
            signing.verify_app(app, ID, self.identities[0])

    def test_mini_retains_its_identity_without_accepting_full(self):
        identifier = signing.PRODUCTS['snow-shot-mini']
        old = self.app('old-mini', '1.5.9', identifier=identifier)
        new = self.app('new-mini', '1.5.10', identifier=identifier)
        self.assertEqual(signing.check_previous(new, old, identifier, self.identities[0])['version'],
                         '1.5.9')
        with self.assertRaises(ValueError):
            signing.verify_app(self.app('full', '1.5.10'), identifier, self.identities[0])

    def test_checks_actual_previous_requirement_in_both_directions(self):
        a = self.app('old', '1.5.9')
        restricted = signing.requirement(self.identities[0], ID) + ' and info[CFBundleVersion] = "1.5.10"'
        b = self.app('new', '1.5.10', custom_requirement=restricted)
        with self.assertRaises(ValueError):
            signing.check_previous(b, a, ID, self.identities[0])

    def test_installer_and_homebrew_preserve_publisher_bytes(self):
        original = self.app('publisher', '1.5.9')
        work = self.directory / 'work'
        work.mkdir()
        staged = work / 'snow_shot.app'
        shutil.copytree(original, staged)
        installer = self.directory / 'install-snow-shot-macos.sh'
        source = (ROOT / 'scripts/install-snow-shot-macos.sh').read_text()
        source = re.sub(r"PROJECT_SIGNING_CERTIFICATE_SHA1='[A-F0-9]+'",
                        f"PROJECT_SIGNING_CERTIFICATE_SHA1='{self.identities[0]}'", source)
        installer.write_text(source)
        wrapper = self.directory / 'prepare-snow-shot-homebrew.sh'
        shutil.copyfile(ROOT / 'scripts/prepare-snow-shot-homebrew.sh', wrapper)
        prepared = self.directory / 'Prepared.app'
        installed = self.directory / 'Installed.app'
        code = f'''source {shlex.quote(str(installer))}
work={shlex.quote(str(work))}
local_dmg=fixture.dmg
prepare_app={shlex.quote(str(prepared))}
destination={shlex.quote(str(installed))}
launch=0
installed_pids() {{ :; }}
prepare_project_application
prepare_application
install_application
'''
        subprocess.run(['/bin/bash', '-c', code], check=True, capture_output=True)
        subprocess.run(['/bin/bash', str(wrapper), 'unused.dmg', str(prepared)],
                       check=True, capture_output=True)
        for copy in (prepared, installed):
            signing.check_previous(copy, original, ID, self.identities[0])
            for file in original.rglob('*'):
                if file.is_file():
                    self.assertEqual(file.read_bytes(), (copy / file.relative_to(original)).read_bytes())

    def test_audit_rejects_unapproved_legacy_package(self):
        a, b = self.app('old', '1.5.8', None), self.app('new', '1.5.9')
        old, new = self.directory / 'previous.dmg', self.directory / 'current.dmg'
        old.write_bytes(b'unapproved legacy'); new.write_bytes(b'project signed')
        @contextmanager
        def mounted(path):
            yield a if path == old else b
        with patch.object(signing, 'project_identity', return_value=self.identities[0]), \
                patch.object(signing, 'mounted_app', side_effect=mounted):
            with self.assertRaises(ValueError):
                signing.audit_release(new, 'snow-shot', 'arm64', '1.5.9', old)

    def test_exact_legacy_boundary_is_recorded_without_claiming_compatibility(self):
        a, b = self.app('old', '1.5.8', None), self.app('new', '1.5.9')
        old, new = self.directory / 'previous.dmg', self.directory / 'current.dmg'
        old.write_bytes(b'approved fixture'); new.write_bytes(b'project signed')
        @contextmanager
        def mounted(path):
            yield a if path == old else b
        with patch.object(signing, 'project_identity', return_value=self.identities[0]), \
                patch.object(signing, 'mounted_app', side_effect=mounted), \
                patch.dict(signing.LEGACY_PACKAGES, {old.name: signing.digest(old)}):
            report = signing.audit_release(new, 'snow-shot', 'arm64', '1.5.9', old)
            self.assertEqual(report['continuity'], 'legacy-1.5.8-migration-requires-consent')

    @unittest.skipUnless(shutil.which('cmake'), 'CPack hook requires CMake')
    def test_finished_dmg_hook_checks_the_contained_app_and_previous_release(self):
        old = self.app('old-package', '1.5.9')
        new = self.app('new-package', '1.5.10')
        packages = []
        for version, app in [('1.5.9', old), ('1.5.10', new)]:
            source = self.directory / version
            source.mkdir()
            shutil.copytree(app, source / 'Snow Shot.app')
            dmg = self.directory / f'snow-shot-{version}-macos-arm64.dmg'
            signing.run('/usr/bin/hdiutil', 'create', '-srcfolder', str(source),
                        '-format', 'UDZO', '-volname', 'Snow Signing Fixture', str(dmg))
            packages.append(dmg)
        fixture_repo = self.directory / 'repository'
        (fixture_repo / 'scripts').mkdir(parents=True)
        (fixture_repo / 'snow_shot/packaging/macos').mkdir(parents=True)
        tool = fixture_repo / 'scripts/snow-shot-macos-signing.py'
        shutil.copyfile(ROOT / 'scripts/snow-shot-macos-signing.py', tool)
        shutil.copyfile(self.root / '0.pem', fixture_repo / 'snow_shot/packaging/macos/release-signing.pem')
        (fixture_repo / 'scripts/install-snow-shot-macos.sh').write_text(
            f"PROJECT_SIGNING_CERTIFICATE_SHA1='{self.identities[0]}'\n")
        variables = {
            'CPACK_PACKAGE_FILES': packages[1], 'CPACK_PACKAGE_NAME': 'snow-shot',
            'CPACK_PACKAGE_FILE_NAME': packages[1].stem,
            'CPACK_PACKAGE_DIRECTORY': self.directory,
            'CPACK_SNOW_SHOT_SIGNING_PYTHON': sys.executable,
            'CPACK_SNOW_SHOT_SIGNING_TOOL': tool,
            'CPACK_SNOW_SHOT_RELEASE_VERSION': '1.5.10',
            'CPACK_SNOW_SHOT_RELEASE_ARCHITECTURE': 'arm64'}
        result = subprocess.run([shutil.which('cmake'), *[f'-D{k}={v}' for k, v in variables.items()],
                                 '-P', str(ROOT / 'cmake/SignSnowShotMacOSDmg.cmake')],
                                env=dict(os.environ, SNOW_MACOS_PREVIOUS_FULL_DMG=str(packages[0])),
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        report = json.loads(packages[1].with_suffix('.signing.json').read_text())
        self.assertEqual(report['continuity'], 'compatible')
        self.assertEqual(report['packageSha256'], signing.digest(packages[1]))
        self.assertEqual(report['previousSha256'], signing.digest(packages[0]))


if __name__ == '__main__':
    unittest.main()
