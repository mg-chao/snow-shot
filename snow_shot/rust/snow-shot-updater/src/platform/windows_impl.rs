use crate::error::{Result, UpdateError, io_error, require};
use path_clean::PathClean;
use std::ffi::OsStr;
use std::os::windows::ffi::OsStrExt;
use std::path::{Path, PathBuf};
use std::time::Duration;
use windows::Win32::Foundation::{
    CloseHandle, ERROR_ACCESS_DENIED, ERROR_FILE_NOT_FOUND, ERROR_PATH_NOT_FOUND, ERROR_SUCCESS,
    HANDLE, WAIT_OBJECT_0, WIN32_ERROR,
};
use windows::Win32::Globalization::{CSTR_EQUAL, CompareStringOrdinal};
use windows::Win32::Storage::FileSystem::{
    FILE_ATTRIBUTE_REPARSE_POINT, GetFileAttributesW, INVALID_FILE_ATTRIBUTES, MOVE_FILE_FLAGS,
    MOVEFILE_DELAY_UNTIL_REBOOT, MOVEFILE_REPLACE_EXISTING, MOVEFILE_WRITE_THROUGH, MoveFileExW,
};
use windows::Win32::System::Registry::{
    HKEY, HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE, KEY_CREATE_SUB_KEY, KEY_QUERY_VALUE,
    KEY_SET_VALUE, KEY_WOW64_32KEY, KEY_WOW64_64KEY, REG_OPTION_NON_VOLATILE, REG_ROUTINE_FLAGS,
    REG_SAM_FLAGS, REG_SZ, REG_VALUE_TYPE, RRF_RT_REG_SZ, RRF_SUBKEY_WOW6432KEY,
    RRF_SUBKEY_WOW6464KEY, RegCloseKey, RegCreateKeyExW, RegGetValueW, RegOpenKeyExW,
    RegSetValueExW,
};
use windows::Win32::System::Threading::{
    OpenProcess, PROCESS_ACCESS_RIGHTS, PROCESS_QUERY_LIMITED_INFORMATION,
    QueryFullProcessImageNameW, WaitForSingleObject,
};
use windows::Win32::UI::Shell::{
    SEE_MASK_NOASYNC, SEE_MASK_NOCLOSEPROCESS, SHELLEXECUTEINFOW, ShellExecuteExW,
};
use windows::Win32::UI::WindowsAndMessaging::SW_HIDE;
use windows::core::{PCWSTR, PWSTR};

// SYNCHRONIZE is a standard access right rather than a process-specific right. The
// windows crate does not expose it through every selected feature combination.
const SYNCHRONIZE_ACCESS: PROCESS_ACCESS_RIGHTS = PROCESS_ACCESS_RIGHTS(0x0010_0000);

fn wide(value: impl AsRef<OsStr>) -> Vec<u16> {
    value
        .as_ref()
        .encode_wide()
        .chain(std::iter::once(0))
        .collect()
}

fn absolute(path: &Path) -> PathBuf {
    let path = if path.is_absolute() {
        path.to_path_buf()
    } else {
        std::env::current_dir().unwrap_or_default().join(path)
    };
    path.clean()
}

pub fn path_eq(left: &Path, right: &Path) -> bool {
    let left = absolute(left).as_os_str().encode_wide().collect::<Vec<_>>();
    let right = absolute(right)
        .as_os_str()
        .encode_wide()
        .collect::<Vec<_>>();
    unsafe { CompareStringOrdinal(&left, &right, true) == CSTR_EQUAL }
}

fn move_file(
    source: &Path,
    destination: &Path,
    flags: MOVE_FILE_FLAGS,
) -> windows::core::Result<()> {
    let source = wide(absolute(source));
    let destination = wide(absolute(destination));
    unsafe { MoveFileExW(PCWSTR(source.as_ptr()), PCWSTR(destination.as_ptr()), flags) }
}

fn replacement_error(
    source: &Path,
    destination: &Path,
    error: impl std::fmt::Display,
) -> UpdateError {
    io_error(
        "update_file_replace_failed",
        "Could not replace an update file; close applications using it",
        format!("{} -> {}: {error}", source.display(), destination.display()),
    )
}

pub fn replace_file(source: &Path, destination: &Path) -> Result<()> {
    move_file(
        source,
        destination,
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH,
    )
    .map_err(|error| replacement_error(source, destination, error))
}

pub fn replace_application_file(source: &Path, destination: &Path, retired: &Path) -> Result<()> {
    match move_file(
        source,
        destination,
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH,
    ) {
        Ok(()) => return Ok(()),
        Err(error) if error.code() == ERROR_ACCESS_DENIED.to_hresult() && destination.is_file() => {
        }
        Err(error) => return Err(replacement_error(source, destination, error)),
    }
    // Windows cannot delete a mapped executable, but it can rename it. The transaction's
    // durable backup and journal cover the gap between retiring the old name and publishing
    // its replacement. Existing external clients keep their original mapped image.
    move_file(destination, retired, MOVEFILE_WRITE_THROUGH)
        .map_err(|error| replacement_error(destination, retired, error))?;
    if let Err(error) = move_file(source, destination, MOVEFILE_WRITE_THROUGH) {
        if let Err(restore) = move_file(retired, destination, MOVEFILE_WRITE_THROUGH) {
            return Err(replacement_error(
                source,
                destination,
                format!("{error}; restoring the retired file failed: {restore}"),
            ));
        }
        return Err(replacement_error(source, destination, error));
    }
    // Mapped images are retained until their clients exit. prune_update_work retries only
    // these reserved files on later service starts; their lifetime never blocks commit or recovery.
    let _ = std::fs::remove_file(retired);
    Ok(())
}

pub fn remove_application_file(destination: &Path, retired: &Path) -> Result<()> {
    match std::fs::remove_file(destination) {
        Ok(()) => return Ok(()),
        Err(error) if error.kind() == std::io::ErrorKind::PermissionDenied => {}
        Err(error) => {
            return Err(io_error(
                "obsolete_file_remove_failed",
                "Could not remove obsolete application file",
                error,
            ));
        }
    }
    move_file(destination, retired, MOVEFILE_WRITE_THROUGH)
        .map_err(|error| replacement_error(destination, retired, error))?;
    let _ = std::fs::remove_file(retired);
    Ok(())
}

pub fn delete_on_restart(path: &Path) -> Result<()> {
    let path = wide(absolute(path));
    unsafe {
        MoveFileExW(
            PCWSTR(path.as_ptr()),
            PCWSTR::null(),
            MOVEFILE_DELAY_UNTIL_REBOOT,
        )
    }
    .map_err(|error| {
        io_error(
            "owned_file_remove_failed",
            "Could not remove an owned application file",
            error,
        )
    })
}

pub fn path_has_reparse(path: &Path) -> bool {
    let path = wide(absolute(path));
    let attributes = unsafe { GetFileAttributesW(PCWSTR(path.as_ptr())) };
    attributes != INVALID_FILE_ATTRIBUTES && attributes & FILE_ATTRIBUTE_REPARSE_POINT.0 != 0
}

const INSTALL_KEY: &str = crate::edition::INSTALL_KEY;
const UNINSTALL_KEY: &str = crate::edition::UNINSTALL_KEY;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum RegistryView {
    Native64,
    Legacy32,
}

impl RegistryView {
    fn access(self) -> REG_SAM_FLAGS {
        match self {
            Self::Native64 => KEY_WOW64_64KEY,
            Self::Legacy32 => KEY_WOW64_32KEY,
        }
    }

    fn read(self) -> REG_ROUTINE_FLAGS {
        match self {
            Self::Native64 => RRF_SUBKEY_WOW6464KEY,
            Self::Legacy32 => RRF_SUBKEY_WOW6432KEY,
        }
    }

    fn bits(self) -> u32 {
        match self {
            Self::Native64 => 64,
            Self::Legacy32 => 32,
        }
    }
}

#[derive(Clone, Copy)]
struct RegisteredInstallation {
    hive: HKEY,
    view: RegistryView,
}

fn registry_error(
    registration: RegisteredInstallation,
    key: &str,
    operation: &str,
    status: WIN32_ERROR,
) -> UpdateError {
    let hive = if registration.hive == HKEY_LOCAL_MACHINE {
        "HKLM"
    } else {
        "HKCU"
    };
    UpdateError::new(
        "registered_version_update_failed",
        "Could not update the registered application version",
    )
    .detail(format!(
        "{operation} {hive}\\{key} ({}-bit view): Win32 error {}",
        registration.view.bits(),
        status.0
    ))
}

fn read_registry_string(
    registration: RegisteredInstallation,
    key: &str,
    name: &str,
) -> Result<Option<String>> {
    let subkey = wide(key);
    let name = wide(name);
    let mut buffer = vec![0_u16; 32_768];
    let mut bytes = (buffer.len() * size_of::<u16>()) as u32;
    let status = unsafe {
        RegGetValueW(
            registration.hive,
            PCWSTR(subkey.as_ptr()),
            PCWSTR(name.as_ptr()),
            RRF_RT_REG_SZ | registration.view.read(),
            None,
            Some(buffer.as_mut_ptr().cast()),
            Some(&mut bytes),
        )
    };
    if status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND {
        return Ok(None);
    }
    if status != ERROR_SUCCESS {
        return Err(registry_error(registration, key, "RegGetValueW", status));
    }
    let length = bytes as usize / size_of::<u16>();
    Ok(Some(String::from_utf16_lossy(
        &buffer[..length.saturating_sub(1)],
    )))
}

fn registered_installation_for_key(
    root: &Path,
    install_key: &str,
) -> Result<Option<RegisteredInstallation>> {
    let mut discovery_error = None;
    for hive in [HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER] {
        // The installer uses SetRegView 64. Preserve older 32-bit registrations too,
        // carrying the selected view through permission checks and version writes.
        for view in [RegistryView::Native64, RegistryView::Legacy32] {
            let registration = RegisteredInstallation { hive, view };
            match read_registry_string(registration, install_key, "") {
                Ok(Some(registered)) if path_eq(Path::new(&registered), root) => {
                    return Ok(Some(registration));
                }
                Err(error) => discovery_error = Some(error),
                _ => {}
            }
        }
    }
    // An inaccessible registration is not evidence of a portable installation.
    match discovery_error {
        Some(error) => Err(error),
        None => Ok(None),
    }
}

pub fn registered_target_matches(root: &Path) -> Result<bool> {
    registered_installation_for_key(root, INSTALL_KEY).map(|registration| registration.is_some())
}

// No registry mutation: for a missing key, check creation rights on its nearest
// existing ancestor. A successful check is repeated in the elevated bootstrap.
pub fn registered_version_requires_elevation(root: &Path) -> Result<bool> {
    let Some(registration) = registered_installation_for_key(root, INSTALL_KEY)? else {
        return Ok(false);
    };
    registered_version_requires_elevation_for_key(registration, UNINSTALL_KEY)
}

fn registered_version_requires_elevation_for_key(
    registration: RegisteredInstallation,
    mut path: &str,
) -> Result<bool> {
    let mut rights = KEY_QUERY_VALUE | KEY_SET_VALUE;
    loop {
        let subkey = wide(path);
        let mut key = HKEY::default();
        let status = unsafe {
            RegOpenKeyExW(
                registration.hive,
                PCWSTR(subkey.as_ptr()),
                None,
                rights | registration.view.access(),
                &mut key,
            )
        };
        if status == ERROR_SUCCESS {
            let _ = unsafe { RegCloseKey(key) };
            return Ok(false);
        }
        if status == ERROR_ACCESS_DENIED {
            return Ok(true);
        }
        if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND)
            && let Some((parent, _)) = path.rsplit_once('\\')
        {
            path = parent;
            rights = KEY_CREATE_SUB_KEY;
            continue;
        }
        return Err(registry_error(registration, path, "RegOpenKeyExW", status));
    }
}

pub fn write_registered_version(root: &Path, version: &str) -> Result<()> {
    let Some(registration) = registered_installation_for_key(root, INSTALL_KEY)? else {
        return Ok(());
    };
    write_registered_version_for_key(registration, UNINSTALL_KEY, version)
}

fn write_registered_version_for_key(
    registration: RegisteredInstallation,
    uninstall_key: &str,
    version: &str,
) -> Result<()> {
    // Also lets legacy recovery finish when a denied write left the old value intact.
    if read_registry_string(registration, uninstall_key, "DisplayVersion")?.as_deref()
        == Some(version)
    {
        return Ok(());
    }
    let subkey = wide(uninstall_key);
    let mut key = HKEY::default();
    let status = unsafe {
        RegCreateKeyExW(
            registration.hive,
            PCWSTR(subkey.as_ptr()),
            None,
            PCWSTR::null(),
            REG_OPTION_NON_VOLATILE,
            KEY_SET_VALUE | registration.view.access(),
            None,
            &mut key,
            None,
        )
    };
    if status != ERROR_SUCCESS {
        return Err(registry_error(
            registration,
            uninstall_key,
            "RegCreateKeyExW",
            status,
        ));
    }
    let name = wide("DisplayVersion");
    let value = wide(version);
    let status = unsafe {
        RegSetValueExW(
            key,
            PCWSTR(name.as_ptr()),
            None,
            REG_VALUE_TYPE(REG_SZ.0),
            Some(std::slice::from_raw_parts(
                value.as_ptr().cast::<u8>(),
                value.len() * 2,
            )),
        )
    };
    let _ = unsafe { RegCloseKey(key) };
    if status != ERROR_SUCCESS {
        return Err(registry_error(
            registration,
            uninstall_key,
            "RegSetValueExW",
            status,
        ));
    }
    Ok(())
}

struct Handle(HANDLE);

impl Drop for Handle {
    fn drop(&mut self) {
        if !self.0.is_invalid() {
            unsafe { CloseHandle(self.0).ok() };
        }
    }
}

fn process_path(process: HANDLE) -> Option<PathBuf> {
    let mut buffer = vec![0_u16; 32_768];
    let mut length = buffer.len() as u32;
    unsafe {
        QueryFullProcessImageNameW(
            process,
            Default::default(),
            PWSTR(buffer.as_mut_ptr()),
            &mut length,
        )
    }
    .ok()
    .map(|_| PathBuf::from(String::from_utf16_lossy(&buffer[..length as usize])))
}

pub fn process_path_for_pid(pid: u32) -> Result<PathBuf> {
    let process = unsafe { OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, false, pid) }
        .map(Handle)
        .map_err(|error| {
            io_error(
                "application_process_open_failed",
                "Could not open the application process",
                error,
            )
        })?;
    process_path(process.0).ok_or_else(|| {
        UpdateError::new(
            "application_process_mismatch",
            "The update target does not match its application process",
        )
    })
}

pub struct ValidatedProcess(Handle);

impl ValidatedProcess {
    pub fn wait_for_exit(&self, timeout: Duration) -> Result<()> {
        let result = unsafe {
            WaitForSingleObject(self.0.0, timeout.as_millis().min(u32::MAX as u128) as u32)
        };
        require(
            result == WAIT_OBJECT_0,
            "application_exit_timeout",
            "The application did not exit; the update was cancelled",
        )
    }
}

pub fn open_validated_process(pid: u32, expected: &Path) -> Result<ValidatedProcess> {
    let process = unsafe {
        OpenProcess(
            SYNCHRONIZE_ACCESS | PROCESS_QUERY_LIMITED_INFORMATION,
            false,
            pid,
        )
    }
    .map(Handle)
    .map_err(|error| {
        io_error(
            "application_process_open_failed",
            "Could not open the application process",
            error,
        )
    })?;
    let actual = process_path(process.0).unwrap_or_default();
    require(
        path_eq(&actual, expected),
        "application_process_mismatch",
        "The update target does not match its application process",
    )?;
    Ok(ValidatedProcess(process))
}

fn quote_argument(argument: &str) -> Result<String> {
    require(
        !argument.contains('"'),
        "invalid_updater_argument",
        "Invalid updater command argument",
    )?;
    Ok(format!("\"{}\"", argument.trim_end_matches('\\')))
}

pub fn launch_elevated(executable: &Path, arguments: &[String]) -> Result<bool> {
    let executable = wide(executable);
    let verb = wide("runas");
    let parameters = arguments
        .iter()
        .map(|argument| quote_argument(argument))
        .collect::<Result<Vec<_>>>()?
        .join(" ");
    let parameters = wide(parameters);
    let mut info = SHELLEXECUTEINFOW {
        cbSize: size_of::<SHELLEXECUTEINFOW>() as u32,
        fMask: SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC,
        lpVerb: PCWSTR(verb.as_ptr()),
        lpFile: PCWSTR(executable.as_ptr()),
        lpParameters: PCWSTR(parameters.as_ptr()),
        nShow: SW_HIDE.0,
        ..Default::default()
    };
    if unsafe { ShellExecuteExW(&mut info) }.is_err() {
        return Ok(false);
    }
    if !info.hProcess.is_invalid() {
        unsafe { CloseHandle(info.hProcess).ok() };
    }
    Ok(true)
}

pub fn remove_installation_startup(_root: &Path) -> Result<()> {
    super::windows_startup::remove_installation_startup(_root)
}

pub fn remove_installation_shell_verbs(root: &Path) -> Result<()> {
    super::windows_shell::remove_installation_shell_verbs(root)
}

pub fn migrate_installation_shell_verbs(previous: &Path, root: &Path) -> Result<()> {
    super::windows_shell::migrate_installation_shell_verbs(previous, root)
}

pub fn migrate_installation_startup(previous: &Path, root: &Path) -> Result<()> {
    let startup = super::windows_startup::migrate_installation_startup(previous, root);
    let shell = migrate_installation_shell_verbs(previous, root);
    startup.and(shell)
}

pub fn launch_on_interactive_desktop(executable: &Path) -> Result<bool> {
    super::windows_startup::launch_on_interactive_desktop(executable)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::{BufRead, BufReader};
    use std::process::Command;
    use std::process::Stdio;
    use windows::Win32::Foundation::{HLOCAL, LocalFree};
    use windows::Win32::Security::Authorization::ConvertStringSecurityDescriptorToSecurityDescriptorW;
    use windows::Win32::Security::{DACL_SECURITY_INFORMATION, PSECURITY_DESCRIPTOR};
    use windows::Win32::System::Registry::{KEY_ALL_ACCESS, RegDeleteKeyExW, RegSetKeySecurity};

    const VIEWS: [RegistryView; 2] = [RegistryView::Native64, RegistryView::Legacy32];

    struct RegistrationFixture {
        path: String,
        keys: [HKEY; 2],
    }

    impl RegistrationFixture {
        fn new() -> Self {
            // CLSID is redirected in HKCU too, so both views are genuinely distinct.
            // RegOverridePredefKey sandboxes alias the views and cannot catch this bug.
            let path = format!("Software\\Classes\\CLSID\\{{{}}}", uuid::Uuid::new_v4());
            let subkey = wide(&path);
            let keys = VIEWS.map(|view| {
                let mut key = HKEY::default();
                unsafe {
                    RegCreateKeyExW(
                        HKEY_CURRENT_USER,
                        PCWSTR(subkey.as_ptr()),
                        None,
                        PCWSTR::null(),
                        REG_OPTION_NON_VOLATILE,
                        KEY_ALL_ACCESS | view.access(),
                        None,
                        &mut key,
                        None,
                    )
                    .ok()
                    .unwrap();
                }
                key
            });
            Self { path, keys }
        }

        fn set(&self, view: RegistryView, name: &str, value: &str) {
            let index = VIEWS
                .iter()
                .position(|candidate| *candidate == view)
                .unwrap();
            let name = wide(name);
            let value = wide(value);
            unsafe {
                RegSetValueExW(
                    self.keys[index],
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
    }

    impl RegistrationFixture {
        fn deny_version_write(&self, view: RegistryView, denied: bool) {
            let index = VIEWS
                .iter()
                .position(|candidate| *candidate == view)
                .unwrap();
            let rights = if denied { KEY_SET_VALUE.0 } else { 0 };
            let sddl = wide(format!("D:P(D;;0x{rights:x};;;WD)(A;;KA;;;WD)"));
            let mut descriptor = PSECURITY_DESCRIPTOR::default();
            unsafe {
                ConvertStringSecurityDescriptorToSecurityDescriptorW(
                    PCWSTR(sddl.as_ptr()),
                    1,
                    &mut descriptor,
                    None,
                )
                .unwrap();
                let status =
                    RegSetKeySecurity(self.keys[index], DACL_SECURITY_INFORMATION, descriptor);
                LocalFree(Some(HLOCAL(descriptor.0)));
                status.ok().unwrap();
            }
        }
    }

    impl Drop for RegistrationFixture {
        fn drop(&mut self) {
            let path = wide(&self.path);
            for (view, key) in VIEWS.into_iter().zip(self.keys) {
                unsafe {
                    RegCloseKey(key).ok().unwrap();
                    RegDeleteKeyExW(
                        HKEY_CURRENT_USER,
                        PCWSTR(path.as_ptr()),
                        view.access().0,
                        None,
                    )
                    .ok()
                    .unwrap();
                }
            }
        }
    }

    #[test]
    fn registered_installation_preserves_the_matching_registry_view() {
        let fixture = RegistrationFixture::new();
        let directory = tempfile::tempdir().unwrap();
        let root = directory.path().join("SnowShot");
        let other_root = directory.path().join("other");
        assert!(
            registered_installation_for_key(&root, &fixture.path)
                .unwrap()
                .is_none()
        );
        for expected_view in VIEWS {
            for view in VIEWS {
                fixture.set(
                    view,
                    "",
                    if view == expected_view {
                        &root
                    } else {
                        &other_root
                    }
                    .to_str()
                    .unwrap(),
                );
                fixture.set(view, "DisplayVersion", "1.0.0");
            }
            let registration = registered_installation_for_key(&root, &fixture.path)
                .unwrap()
                .expect("an installed target must be recognized in either registry view");
            assert_eq!(registration.hive, HKEY_CURRENT_USER);
            assert_eq!(registration.view, expected_view);
            assert!(
                !registered_version_requires_elevation_for_key(registration, &fixture.path)
                    .unwrap()
            );
            fixture.deny_version_write(expected_view, true);
            assert!(
                registered_version_requires_elevation_for_key(registration, &fixture.path).unwrap()
            );
            // Rollback to an unchanged version does not require write access.
            write_registered_version_for_key(registration, &fixture.path, "1.0.0").unwrap();
            let error =
                write_registered_version_for_key(registration, &fixture.path, "2.0.0").unwrap_err();
            assert_eq!(error.code, "registered_version_update_failed");
            assert!(
                error
                    .detail
                    .unwrap()
                    .contains(&format!("{}-bit view", expected_view.bits()))
            );
            fixture.deny_version_write(expected_view, false);
            write_registered_version_for_key(registration, &fixture.path, "2.0.0").unwrap();
            for view in VIEWS {
                assert_eq!(
                    read_registry_string(
                        RegisteredInstallation {
                            hive: HKEY_CURRENT_USER,
                            view
                        },
                        &fixture.path,
                        "DisplayVersion",
                    )
                    .unwrap()
                    .as_deref(),
                    Some(if view == expected_view {
                        "2.0.0"
                    } else {
                        "1.0.0"
                    }),
                );
            }
        }
        // Match the installer's native view when both registrations point to the target.
        for view in VIEWS {
            fixture.set(view, "", root.to_str().unwrap());
        }
        assert_eq!(
            registered_installation_for_key(&root, &fixture.path)
                .unwrap()
                .unwrap()
                .view,
            RegistryView::Native64,
        );
    }

    #[test]
    fn application_file_changes_preserve_running_images() {
        for remove in [false, true] {
            let directory = tempfile::tempdir().unwrap();
            let destination = directory.path().join("running image.exe");
            let source = directory.path().join("replacement.exe");
            let command = std::env::var_os("ComSpec").unwrap_or_else(|| "cmd.exe".into());
            std::fs::copy(command, &destination).unwrap();
            let mut child = Command::new(&destination)
                .args(["/d", "/q", "/c", "echo ready & set /p input="])
                .stdin(Stdio::piped())
                .stdout(Stdio::piped())
                .spawn()
                .unwrap();
            let mut ready = String::new();
            BufReader::new(child.stdout.take().unwrap())
                .read_line(&mut ready)
                .unwrap();
            assert_eq!(ready.trim(), "ready");
            std::fs::write(&source, b"new image").unwrap();
            let retired = directory.path().join("retired.exe");
            let result = if remove {
                remove_application_file(&destination, &retired)
            } else {
                replace_application_file(&source, &destination, &retired)
            };
            let still_running = child.try_wait().unwrap().is_none();
            let retained_image = retired.exists();
            drop(child.stdin.take());
            child.wait().unwrap();
            result.unwrap();
            assert!(still_running);
            assert!(retained_image);
            if remove {
                assert!(!destination.exists());
            } else {
                assert_eq!(std::fs::read(destination).unwrap(), b"new image");
                assert!(!source.exists());
            }
            std::fs::remove_file(retired).unwrap();
        }
    }

    #[test]
    fn application_file_changes_respect_handles_that_deny_delete_sharing() {
        use std::os::windows::fs::OpenOptionsExt;
        use windows::Win32::Storage::FileSystem::FILE_SHARE_READ;

        let directory = tempfile::tempdir().unwrap();
        let destination = directory.path().join("locked.dll");
        let source = directory.path().join("replacement.dll");
        let retired = directory.path().join("retired.file");
        std::fs::write(&destination, b"original").unwrap();
        std::fs::write(&source, b"replacement").unwrap();
        let locked = std::fs::OpenOptions::new()
            .read(true)
            .share_mode(FILE_SHARE_READ.0)
            .open(&destination)
            .unwrap();
        assert!(replace_application_file(&source, &destination, &retired).is_err());
        assert!(remove_application_file(&destination, &retired).is_err());
        assert_eq!(std::fs::read(&destination).unwrap(), b"original");
        assert_eq!(std::fs::read(&source).unwrap(), b"replacement");
        assert!(!retired.exists());
        drop(locked);
    }

    #[test]
    fn validated_process_handle_survives_process_exit() {
        let command = std::env::var_os("ComSpec").unwrap_or_else(|| "cmd.exe".into());
        let mut child = Command::new(command)
            .args(["/d", "/c", "ping -n 2 127.0.0.1 >nul"])
            .spawn()
            .unwrap();
        let expected = process_path_for_pid(child.id()).unwrap();
        let process = open_validated_process(child.id(), &expected).unwrap();
        assert!(child.wait().unwrap().success());
        drop(child);
        process.wait_for_exit(Duration::from_secs(1)).unwrap();
    }
}
