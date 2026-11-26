#![cfg(windows)]

// Predefined hives are redirected only in this dedicated integration-test process.
// No registration is written under the real user, machine, or loaded-profile hives.
use snow_shot_updater::{edition, platform, transaction};
use std::path::Path;
use windows::Win32::Foundation::{ERROR_FILE_NOT_FOUND, HLOCAL, LocalFree};
use windows::Win32::Security::Authorization::ConvertStringSecurityDescriptorToSecurityDescriptorW;
use windows::Win32::Security::{DACL_SECURITY_INFORMATION, PSECURITY_DESCRIPTOR};
use windows::Win32::System::Registry::*;
use windows::core::PCWSTR;

fn wide(text: &str) -> Vec<u16> {
    text.encode_utf16().chain(Some(0)).collect()
}

struct Key(HKEY);
impl Drop for Key {
    fn drop(&mut self) {
        unsafe { RegCloseKey(self.0).ok().unwrap() };
    }
}

fn create(base: HKEY, path: &str) -> Key {
    let path = wide(path);
    let mut key = HKEY::default();
    unsafe {
        RegCreateKeyExW(
            base,
            PCWSTR(path.as_ptr()),
            None,
            PCWSTR::null(),
            REG_OPTION_NON_VOLATILE,
            KEY_ALL_ACCESS,
            None,
            &mut key,
            None,
        )
        .ok()
        .unwrap();
    }
    Key(key)
}

fn set(key: HKEY, name: &str, value: &str) {
    let name = wide(name);
    let value = wide(value);
    unsafe {
        RegSetValueExW(
            key,
            PCWSTR(name.as_ptr()),
            None,
            REG_SZ,
            Some(std::slice::from_raw_parts(
                value.as_ptr().cast(),
                value.len() * 2,
            )),
        )
        .ok()
        .unwrap();
    }
}

fn read(key: HKEY, name: &str) -> String {
    let name = wide(name);
    let mut value = [0_u16; 4096];
    let mut bytes = (value.len() * 2) as u32;
    unsafe {
        RegGetValueW(
            key,
            PCWSTR::null(),
            PCWSTR(name.as_ptr()),
            RRF_RT_REG_SZ,
            None,
            Some(value.as_mut_ptr().cast()),
            Some(&mut bytes),
        )
        .ok()
        .unwrap();
    }
    String::from_utf16(&value[..bytes as usize / 2 - 1]).unwrap()
}

fn deny_writes(key: HKEY, denied: bool) {
    let sddl = wide(if denied {
        "D:P(D;;KW;;;WD)(A;;KA;;;WD)"
    } else {
        "D:P(A;;KA;;;WD)"
    });
    let mut descriptor = PSECURITY_DESCRIPTOR::default();
    unsafe {
        ConvertStringSecurityDescriptorToSecurityDescriptorW(
            PCWSTR(sddl.as_ptr()),
            1,
            &mut descriptor,
            None,
        )
        .unwrap();
        let status = RegSetKeySecurity(key, DACL_SECURITY_INFORMATION, descriptor);
        LocalFree(Some(HLOCAL(descriptor.0)));
        status.ok().unwrap();
    }
}

fn exists(base: HKEY, path: &str) -> bool {
    let path = wide(path);
    let mut key = HKEY::default();
    let status = unsafe { RegOpenKeyExW(base, PCWSTR(path.as_ptr()), None, KEY_READ, &mut key) };
    if status == ERROR_FILE_NOT_FOUND {
        return false;
    }
    status.ok().unwrap();
    drop(Key(key));
    true
}

fn verb_path(extension: &str, verb: &str) -> String {
    format!("SystemFileAssociations\\{extension}\\shell\\{verb}")
}

fn register(classes: HKEY, extension: &str, executable: &Path, verb: &str, product: &str) {
    let key = create(classes, &verb_path(extension, verb));
    let executable = executable.display().to_string().replace('/', "\\");
    set(key.0, "ProductId", product);
    set(key.0, "InstallationExecutable", &executable);
    set(key.0, "Icon", &format!("\"{executable}\",0"));
    set(key.0, "", "Edit with Snow Shot");
    let command = create(key.0, "command");
    set(
        command.0,
        "",
        &format!("\"{executable}\" --edit-with-snow-shot \"%1\""),
    );
}

// A minimal empty registry hive permits real RegLoadAppKeyW coverage without
// SeBackupPrivilege, RegSaveKey, an administrator account, or a real profile hive.
fn empty_hive(path: &Path) {
    let mut bytes = vec![0_u8; 8192];
    fn u32_at(bytes: &mut [u8], offset: usize, value: u32) {
        bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
    }
    bytes[..4].copy_from_slice(b"regf");
    for (offset, value) in [
        (4, 1),
        (8, 1),
        (0x14, 1),
        (0x18, 3),
        (0x20, 1),
        (0x24, 0x20),
        (0x28, 0x1000),
        (0x2c, 1),
    ] {
        u32_at(&mut bytes, offset, value);
    }
    bytes[0x1000..0x1004].copy_from_slice(b"hbin");
    u32_at(&mut bytes, 0x1008, 4096);
    u32_at(&mut bytes, 0x1020, (-88_i32) as u32);
    let nk = 0x1024;
    bytes[nk..nk + 2].copy_from_slice(b"nk");
    bytes[nk + 2..nk + 4].copy_from_slice(&0x2c_u16.to_le_bytes());
    for offset in [0x10, 0x1c, 0x20, 0x28, 0x30] {
        u32_at(&mut bytes, nk + offset, u32::MAX);
    }
    u32_at(&mut bytes, nk + 0x2c, 0x78);
    bytes[nk + 0x48..nk + 0x4a].copy_from_slice(&4_u16.to_le_bytes());
    bytes[nk + 0x4c..nk + 0x50].copy_from_slice(b"ROOT");
    u32_at(&mut bytes, 0x1078, (-48_i32) as u32);
    let sk = 0x107c;
    bytes[sk..sk + 2].copy_from_slice(b"sk");
    u32_at(&mut bytes, sk + 4, 0x78);
    u32_at(&mut bytes, sk + 8, 0x78);
    u32_at(&mut bytes, sk + 0xc, 1);
    u32_at(&mut bytes, sk + 0x10, 20);
    bytes[sk + 0x14] = 1;
    bytes[sk + 0x16..sk + 0x18].copy_from_slice(&0x8004_u16.to_le_bytes());
    u32_at(&mut bytes, 0x10a8, 3928);
    let mut checksum = bytes[..0x1fc].chunks_exact(4).fold(0, |checksum, word| {
        checksum ^ u32::from_le_bytes(word.try_into().unwrap())
    });
    if checksum == 0 {
        checksum = 1;
    }
    if checksum == u32::MAX {
        checksum -= 1;
    }
    u32_at(&mut bytes, 0x1fc, checksum);
    std::fs::create_dir_all(path.parent().unwrap()).unwrap();
    std::fs::write(path, bytes).unwrap();
}

fn load_hive(path: &Path) -> Key {
    let path = wide(path.to_str().unwrap());
    let mut key = HKEY::default();
    unsafe {
        RegLoadAppKeyW(
            PCWSTR(path.as_ptr()),
            &mut key,
            KEY_ALL_ACCESS.0,
            REG_PROCESS_APPKEY,
            None,
        )
        .ok()
        .unwrap();
    }
    Key(key)
}

struct Sandbox {
    name: Vec<u16>,
    real_user: Key,
    _root: Key,
    _user: Key,
    _users: Key,
    _machine: Key,
}
impl Sandbox {
    fn new() -> Self {
        let real_user = create(HKEY_CURRENT_USER, "Software");
        let name = format!("SnowShotShellTest-{}", uuid::Uuid::new_v4());
        let root = create(real_user.0, &name);
        let user = create(root.0, "user");
        let users = create(root.0, "users");
        let machine = create(root.0, "machine");
        unsafe {
            RegOverridePredefKey(HKEY_CURRENT_USER, Some(user.0))
                .ok()
                .unwrap();
            RegOverridePredefKey(HKEY_USERS, Some(users.0))
                .ok()
                .unwrap();
            RegOverridePredefKey(HKEY_LOCAL_MACHINE, Some(machine.0))
                .ok()
                .unwrap();
        }
        Self {
            name: wide(&name),
            real_user,
            _root: root,
            _user: user,
            _users: users,
            _machine: machine,
        }
    }
}
impl Drop for Sandbox {
    fn drop(&mut self) {
        unsafe {
            RegOverridePredefKey(HKEY_CURRENT_USER, None).ok().unwrap();
            RegOverridePredefKey(HKEY_USERS, None).ok().unwrap();
            RegOverridePredefKey(HKEY_LOCAL_MACHINE, None).ok().unwrap();
            RegDeleteTreeW(self.real_user.0, PCWSTR(self.name.as_ptr()))
                .ok()
                .unwrap();
        }
    }
}

#[test]
fn shell_cleanup_and_migration_are_scoped_to_owned_loaded_and_offline_profiles() {
    let _sandbox = Sandbox::new();
    let temporary = tempfile::tempdir().unwrap();
    let previous = temporary.path().join("previous Snow Shot 安");
    let destination = temporary.path().join("destination Snow Shot 安");
    let previous_executable = previous.join(edition::APP_PATH);
    let destination_executable = destination.join(edition::APP_PATH);
    let other_executable = temporary
        .path()
        .join("unrelated installation")
        .join(edition::APP_PATH);
    let current = create(HKEY_CURRENT_USER, "Software\\Classes");
    let loaded = create(HKEY_USERS, "S-1-5-21-100_Classes");
    let profile = temporary.path().join("offline profile 安");
    let hive_path = profile.join("AppData/Local/Microsoft/Windows/UsrClass.dat");
    empty_hive(&hive_path);
    let offline = load_hive(&hive_path);
    for classes in [current.0, loaded.0, offline.0] {
        for extension in [".png", ".obsolete-format"] {
            register(
                classes,
                extension,
                &previous_executable,
                edition::SHELL_VERB,
                edition::PRODUCT,
            );
        }
        register(
            classes,
            ".different-owner",
            &other_executable,
            edition::SHELL_VERB,
            edition::PRODUCT,
        );
        register(
            classes,
            ".different-command",
            &previous_executable,
            edition::SHELL_VERB,
            edition::PRODUCT,
        );
        let command = create(
            classes,
            &format!(
                "{}\\command",
                verb_path(".different-command", edition::SHELL_VERB)
            ),
        );
        set(
            command.0,
            "",
            "\"C:\\Other\\app.exe\" --edit-with-snow-shot \"%1\"",
        );
        register(
            classes,
            ".other-edition",
            &previous_executable,
            if edition::MINI {
                "SnowShot.Edit"
            } else {
                "SnowShotMini.Edit"
            },
            if edition::MINI {
                "snow-shot"
            } else {
                "snow-shot-mini"
            },
        );
        register(
            classes,
            ".wrong-product",
            &previous_executable,
            edition::SHELL_VERB,
            "another-product",
        );
        register(
            classes,
            ".png",
            &previous_executable,
            "OtherApplication.Edit",
            edition::PRODUCT,
        );
    }
    unsafe { RegFlushKey(offline.0).ok().unwrap() };
    drop(offline);
    let profile_key = create(
        HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList\\S-1-5-21-200",
    );
    set(profile_key.0, "ProfileImagePath", profile.to_str().unwrap());
    let class_id = |index: u8| {
        let edition = if edition::MINI { 'C' } else { 'B' };
        format!("{{3F1E90A2-7D4B-4F21-9D8A-6B0C5E2F70{edition}{index}}}")
    };
    let id = class_id(2);
    let previous_path = previous_executable.display().to_string().replace('/', "\\");
    let destination_path = destination_executable
        .display()
        .to_string()
        .replace('/', "\\");
    let register_owner = |path: &str| {
        let key = create(current.0, path);
        set(key.0, "ProductId", edition::PRODUCT);
        set(key.0, "InstallationExecutable", &previous_path);
        key
    };
    let old_wildcard = format!(r"*\shell\{}.stitch_images", edition::REGISTRY_NAME);
    let scoped_verb = verb_path(
        ".obsolete-format",
        &format!("{}.File.stitch_images", edition::REGISTRY_NAME),
    );
    let modern_class = format!(r"CLSID\{id}");
    for path in [&old_wildcard, &scoped_verb, &modern_class] {
        let key = register_owner(path);
        if path != &modern_class {
            set(key.0, "ExplorerCommandHandler", &id);
        } else {
            let server = create(key.0, "InprocServer32");
            set(
                server.0,
                "",
                &previous_executable
                    .with_file_name("SnowShotExplorerCommand.dll")
                    .display()
                    .to_string(),
            );
        }
    }
    let blocked_path = verb_path(
        ".protected",
        &format!("{}.File.pin_to_screen", edition::REGISTRY_NAME),
    );
    let blocked = register_owner(&blocked_path);
    set(blocked.0, "ExplorerCommandHandler", &class_id(0));

    let delegate_path = verb_path(
        ".png",
        &format!("{}.File.compress_images", edition::REGISTRY_NAME),
    );
    let delegate = register_owner(&delegate_path);
    set(
        create(delegate.0, "command").0,
        "DelegateExecute",
        &class_id(5),
    );
    drop(delegate);
    let local_class = format!(r"CLSID\{}", class_id(5));
    let local_server = format!(r"{local_class}\LocalServer32");
    drop(register_owner(&local_class));
    let server = create(current.0, &local_server);
    set(
        server.0,
        "",
        &format!("\"{previous_path}\" --explorer-command-server 5"),
    );
    set(server.0, "ServerExecutable", &previous_path);
    drop(server);
    let interrupted_class = format!(r"CLSID\{}", class_id(3));
    let interrupted_server = format!(r"{interrupted_class}\LocalServer32");
    drop(register_owner(&interrupted_class));
    let server = create(current.0, &interrupted_server);
    set(
        server.0,
        "",
        &format!("\"{destination_path}\" --explorer-command-server 3"),
    );
    set(server.0, "ServerExecutable", &previous_path);
    drop(server);
    let foreign_class = format!(r"CLSID\{}", class_id(4));
    let server = create(register_owner(&foreign_class).0, "LocalServer32");
    set(server.0, "", &format!("\"{previous_path}\" --other-tool"));
    set(server.0, "ServerExecutable", &previous_path);
    drop(server);

    let denied = create(current.0, &verb_path(".png", edition::SHELL_VERB));
    deny_writes(denied.0, true);
    deny_writes(blocked.0, true);
    let failed = platform::migrate_installation_shell_verbs(&previous, &destination).unwrap_err();
    deny_writes(blocked.0, false);
    deny_writes(denied.0, false);
    assert_eq!(failed.code, "shell_registration_failed");
    assert_eq!(read(blocked.0, "InstallationExecutable"), previous_path);
    // A denied modern key must not strand later actions or legacy verbs in the same profile.
    assert_eq!(
        read(create(current.0, &scoped_verb).0, "InstallationExecutable"),
        destination_path
    );
    assert_eq!(
        read(
            create(current.0, &delegate_path).0,
            "InstallationExecutable"
        ),
        destination_path
    );
    let server = create(current.0, &local_server);
    assert_eq!(
        read(server.0, ""),
        format!("\"{destination_path}\" --explorer-command-server 5")
    );
    assert_eq!(read(server.0, "ServerExecutable"), destination_path);
    drop(server);
    assert_eq!(
        read(
            create(current.0, &interrupted_class).0,
            "InstallationExecutable"
        ),
        destination_path
    );
    assert_eq!(
        read(create(current.0, &interrupted_server).0, "ServerExecutable"),
        destination_path
    );
    assert_eq!(
        read(
            create(current.0, &foreign_class).0,
            "InstallationExecutable"
        ),
        previous_path
    );
    assert_eq!(
        read(
            create(
                current.0,
                &verb_path(".obsolete-format", edition::SHELL_VERB)
            )
            .0,
            "InstallationExecutable"
        ),
        destination_path
    );
    assert_eq!(
        read(denied.0, "InstallationExecutable"),
        previous_executable.display().to_string().replace('/', "\\")
    );
    // A denied current-user verb must not prevent another profile from migrating.
    let migrated = create(loaded.0, &verb_path(".png", edition::SHELL_VERB));
    assert_eq!(
        read(migrated.0, "InstallationExecutable"),
        destination_executable
            .display()
            .to_string()
            .replace('/', "\\")
    );
    platform::migrate_installation_shell_verbs(&previous, &destination).unwrap();
    let offline = load_hive(&hive_path);
    for classes in [current.0, loaded.0, offline.0] {
        let migrated = create(classes, &verb_path(".png", edition::SHELL_VERB));
        assert_eq!(
            read(migrated.0, "InstallationExecutable"),
            destination_executable
                .display()
                .to_string()
                .replace('/', "\\")
        );
        assert_eq!(
            read(migrated.0, "Icon"),
            format!(
                "\"{}\",0",
                destination_executable
                    .display()
                    .to_string()
                    .replace('/', "\\")
            )
        );
        let command = create(migrated.0, "command");
        assert_eq!(
            read(command.0, ""),
            format!(
                "\"{}\" --edit-with-snow-shot \"%1\"",
                destination_executable
                    .display()
                    .to_string()
                    .replace('/', "\\")
            )
        );
    }
    drop(offline);
    assert!(!exists(current.0, &old_wildcard));
    let scoped = create(current.0, &scoped_verb);
    assert_eq!(
        read(scoped.0, "Icon"),
        format!(
            "\"{}\",0",
            destination_executable
                .display()
                .to_string()
                .replace('/', "\\")
        )
    );
    drop(scoped);
    // Old-root uninstall cannot remove registrations migrated to the new root.
    platform::remove_installation_shell_verbs(&previous).unwrap();
    assert!(exists(current.0, &verb_path(".png", edition::SHELL_VERB)));
    assert!(exists(current.0, &foreign_class));
    assert!(exists(current.0, &delegate_path));
    assert!(exists(current.0, &local_class));
    std::fs::create_dir_all(&destination).unwrap();
    transaction::uninstall(&destination, false).unwrap();
    assert!(exists(current.0, &verb_path(".png", edition::SHELL_VERB)));
    let blocked_parent = create(current.0, r"SystemFileAssociations\.protected\shell");
    deny_writes(blocked_parent.0, true);
    let cleanup = platform::remove_installation_shell_verbs(&destination);
    deny_writes(blocked_parent.0, false);
    drop(blocked);
    assert_eq!(cleanup.unwrap_err().code, "shell_registration_failed");
    assert!(exists(current.0, &blocked_path));
    assert!(!exists(current.0, &verb_path(".png", edition::SHELL_VERB)));
    assert!(!exists(current.0, &old_wildcard));
    assert!(!exists(current.0, &scoped_verb));
    assert!(!exists(current.0, &modern_class));
    assert!(!exists(current.0, &delegate_path));
    assert!(!exists(current.0, &local_class));
    assert!(!exists(current.0, &interrupted_class));
    transaction::uninstall(&destination, true).unwrap();
    assert!(!exists(current.0, &blocked_path));
    let offline = load_hive(&hive_path);
    for classes in [current.0, loaded.0, offline.0] {
        for extension in [".png", ".obsolete-format"] {
            assert!(!exists(classes, &verb_path(extension, edition::SHELL_VERB)));
        }
        assert!(exists(
            classes,
            &verb_path(".different-command", edition::SHELL_VERB)
        ));
        assert!(exists(
            classes,
            &verb_path(".different-owner", edition::SHELL_VERB)
        ));
        assert!(exists(
            classes,
            &verb_path(".wrong-product", edition::SHELL_VERB)
        ));
        assert!(exists(classes, &verb_path(".png", "OtherApplication.Edit")));
        assert!(exists(classes, "SystemFileAssociations\\.png"));
        assert!(exists(
            classes,
            &verb_path(
                ".other-edition",
                if edition::MINI {
                    "SnowShot.Edit"
                } else {
                    "SnowShotMini.Edit"
                }
            )
        ));
    }
    drop(offline);
    // Damaged and incomplete ownership data cannot authorize deleting an entry.
    for extension in [".missing-owner", ".wrong-type", ".extra-command-arguments"] {
        register(
            current.0,
            extension,
            &destination_executable,
            edition::SHELL_VERB,
            edition::PRODUCT,
        );
    }
    let missing = create(current.0, &verb_path(".missing-owner", edition::SHELL_VERB));
    let owner_name = wide("InstallationExecutable");
    unsafe {
        RegDeleteValueW(missing.0, PCWSTR(owner_name.as_ptr()))
            .ok()
            .unwrap()
    };
    let wrong_type = create(current.0, &verb_path(".wrong-type", edition::SHELL_VERB));
    let product_name = wide("ProductId");
    unsafe {
        RegSetValueExW(
            wrong_type.0,
            PCWSTR(product_name.as_ptr()),
            None,
            REG_DWORD,
            Some(&1_u32.to_le_bytes()),
        )
        .ok()
        .unwrap()
    };
    let extra = create(
        current.0,
        &format!(
            "{}\\command",
            verb_path(".extra-command-arguments", edition::SHELL_VERB)
        ),
    );
    set(
        extra.0,
        "",
        &format!(
            "\"{}\" --edit-with-snow-shot \"%1\" --other",
            destination_executable
                .display()
                .to_string()
                .replace('/', "\\")
        ),
    );
    platform::remove_installation_shell_verbs(&destination).unwrap();
    for extension in [".missing-owner", ".wrong-type", ".extra-command-arguments"] {
        assert!(exists(
            current.0,
            &verb_path(extension, edition::SHELL_VERB)
        ));
    }
}
