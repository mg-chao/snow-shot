//! Remove or migrate only shell verbs owned by the target installation and edition.
use crate::error::{Result, UpdateError};
use path_clean::PathClean;
use std::ffi::{OsStr, OsString};
use std::os::windows::ffi::{OsStrExt, OsStringExt};
use std::path::{Path, PathBuf};
use windows::Win32::Foundation::{
    ERROR_FILE_NOT_FOUND, ERROR_MORE_DATA, ERROR_NO_MORE_ITEMS, ERROR_PATH_NOT_FOUND,
    ERROR_SUCCESS, ERROR_UNSUPPORTED_TYPE, WIN32_ERROR,
};
use windows::Win32::Globalization::{CSTR_EQUAL, CompareStringOrdinal};
use windows::Win32::System::Registry::{
    HKEY, HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE, HKEY_USERS, KEY_READ, KEY_WRITE,
    REG_PROCESS_APPKEY, REG_SAM_FLAGS, REG_SZ, RRF_RT_REG_EXPAND_SZ, RRF_RT_REG_SZ, RegCloseKey,
    RegDeleteTreeW, RegEnumKeyExW, RegFlushKey, RegGetValueW, RegLoadAppKeyW, RegOpenKeyExW,
    RegSetValueExW,
};
use windows::core::{PCWSTR, PWSTR};

const FAILURE_CODE: &str = "shell_registration_failed";
const FAILURE_MESSAGE: &str = "Could not update Snow Shot context menu registration";
const ASSOCIATIONS: &str = "SystemFileAssociations";
const CLASSES: &str = "Software\\Classes";
const PROFILES: &str = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList";

struct Key(HKEY);
impl Drop for Key {
    fn drop(&mut self) {
        unsafe {
            let _ = RegCloseKey(self.0);
        }
    }
}

fn wide(value: impl AsRef<OsStr>) -> Vec<u16> {
    value.as_ref().encode_wide().chain(Some(0)).collect()
}

fn error(operation: &str, status: WIN32_ERROR) -> UpdateError {
    UpdateError::new(FAILURE_CODE, FAILURE_MESSAGE)
        .detail(format!("{operation}: Win32 error {}", status.0))
}

fn check(operation: &str, status: WIN32_ERROR) -> Result<()> {
    if status == ERROR_SUCCESS {
        Ok(())
    } else {
        Err(error(operation, status))
    }
}

fn open(base: HKEY, path: impl AsRef<OsStr>, access: REG_SAM_FLAGS) -> Result<Option<Key>> {
    let path = wide(path);
    let mut key = HKEY::default();
    let status = unsafe { RegOpenKeyExW(base, PCWSTR(path.as_ptr()), None, access, &mut key) };
    if matches!(status, ERROR_FILE_NOT_FOUND | ERROR_PATH_NOT_FOUND) {
        return Ok(None);
    }
    check("RegOpenKeyExW", status)?;
    Ok(Some(Key(key)))
}

fn read(key: HKEY, name: &str, expandable: bool) -> Result<Option<OsString>> {
    let name = wide(name);
    let mut value = vec![0_u16; 32_768];
    let mut bytes = (value.len() * size_of::<u16>()) as u32;
    let flags = if expandable {
        RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ
    } else {
        RRF_RT_REG_SZ
    };
    let status = unsafe {
        RegGetValueW(
            key,
            PCWSTR::null(),
            PCWSTR(name.as_ptr()),
            flags,
            None,
            Some(value.as_mut_ptr().cast()),
            Some(&mut bytes),
        )
    };
    if matches!(
        status,
        ERROR_FILE_NOT_FOUND | ERROR_UNSUPPORTED_TYPE | ERROR_MORE_DATA
    ) {
        return Ok(None);
    }
    check("RegGetValueW", status)?;
    let length = bytes as usize / size_of::<u16>();
    if bytes < 2
        || bytes as usize > size_of_val(&*value)
        || !bytes.is_multiple_of(2)
        || value[length - 1] != 0
        || value[..length - 1].contains(&0)
    {
        return Ok(None);
    }
    Ok(Some(OsString::from_wide(&value[..length - 1])))
}

fn write(key: HKEY, name: &str, value: impl AsRef<OsStr>) -> Result<()> {
    let name = wide(name);
    let value = wide(value);
    let bytes = unsafe {
        std::slice::from_raw_parts(value.as_ptr().cast::<u8>(), value.len() * size_of::<u16>())
    };
    check("RegSetValueExW", unsafe {
        RegSetValueExW(key, PCWSTR(name.as_ptr()), None, REG_SZ, Some(bytes))
    })
}

fn names(key: HKEY) -> Result<Vec<OsString>> {
    let mut result = Vec::new();
    for index in 0.. {
        let mut name = vec![0_u16; 256];
        let mut length = name.len() as u32;
        let status = unsafe {
            RegEnumKeyExW(
                key,
                index,
                Some(PWSTR(name.as_mut_ptr())),
                &mut length,
                None,
                None,
                None,
                None,
            )
        };
        if status == ERROR_NO_MORE_ITEMS {
            break;
        }
        check("RegEnumKeyExW", status)?;
        result.push(OsString::from_wide(&name[..length as usize]));
    }
    Ok(result)
}

fn equal(left: impl AsRef<OsStr>, right: impl AsRef<OsStr>) -> bool {
    let left = left.as_ref().encode_wide().collect::<Vec<_>>();
    let right = right.as_ref().encode_wide().collect::<Vec<_>>();
    unsafe { CompareStringOrdinal(&left, &right, true) == CSTR_EQUAL }
}

fn normalized(path: &Path) -> String {
    let native = path.clean().to_string_lossy().replace('/', "\\");
    if let Some(unc) = native.strip_prefix("\\\\?\\UNC\\") {
        format!("\\\\{unc}")
    } else {
        native.strip_prefix("\\\\?\\").unwrap_or(&native).to_owned()
    }
}

fn same_executable(value: &OsStr, expected: &Path) -> bool {
    Path::new(value).is_absolute() && equal(normalized(Path::new(value)), normalized(expected))
}

fn owned(key: HKEY, executable: &Path) -> Result<bool> {
    let Some(product) = read(key, "ProductId", false)? else {
        return Ok(false);
    };
    let Some(owner) = read(key, "InstallationExecutable", false)? else {
        return Ok(false);
    };
    if product != crate::edition::PRODUCT || !same_executable(&owner, executable) {
        return Ok(false);
    }
    let Some(command) = open(key, "command", KEY_READ)? else {
        return Ok(false);
    };
    let Some(value) = read(command.0, "", false)? else {
        return Ok(false);
    };
    Ok(command_targets(
        &value,
        executable,
        " --edit-with-snow-shot \"%1\"",
    ))
}

fn command_targets(value: &OsStr, executable: &Path, arguments: &str) -> bool {
    let Some((target, actual_arguments)) = value
        .to_str()
        .and_then(|value| value.strip_prefix('"'))
        .and_then(|value| value.split_once('"'))
    else {
        return false;
    };
    actual_arguments == arguments && same_executable(OsStr::new(target), executable)
}

#[derive(Clone, Copy)]
enum ActionKey<'a> {
    Verb(&'a str),
    Class(usize),
}

enum ActionRegistration {
    Verb,
    InprocServer,
    LocalServer(usize),
}

fn owned_action(
    key: HKEY,
    executable: &Path,
    action: ActionKey<'_>,
    replacement: Option<&Path>,
) -> Result<Option<ActionRegistration>> {
    if read(key, "ProductId", false)?.as_deref() != Some(OsStr::new(crate::edition::PRODUCT))
        || !read(key, "InstallationExecutable", false)?
            .is_some_and(|owner| same_executable(&owner, executable))
    {
        return Ok(None);
    }
    match action {
        ActionKey::Verb(clsid) => {
            let handler = if let Some(handler) = read(key, "ExplorerCommandHandler", false)? {
                Some(handler)
            } else if let Some(command) = open(key, "command", KEY_READ)? {
                read(command.0, "DelegateExecute", false)?
            } else {
                None
            };
            Ok(handler
                .is_some_and(|handler| equal(handler, clsid))
                .then_some(ActionRegistration::Verb))
        }
        ActionKey::Class(index) => {
            // The ownership marker is written last. A retried migration may see server values
            // already changed to its exact destination.
            let targets = std::iter::once(executable).chain(replacement);
            if let Some(server) = open(key, "InprocServer32", KEY_READ)? {
                return Ok(read(server.0, "", false)?
                    .is_some_and(|path| {
                        targets.clone().any(|target| {
                            same_executable(
                                &path,
                                &target.with_file_name("SnowShotExplorerCommand.dll"),
                            )
                        })
                    })
                    .then_some(ActionRegistration::InprocServer));
            }
            let Some(server) = open(key, "LocalServer32", KEY_READ)? else {
                return Ok(None);
            };
            let arguments = format!(" --explorer-command-server {index}");
            let owned = read(server.0, "ServerExecutable", false)?
                .is_some_and(|path| targets.clone().any(|target| same_executable(&path, target)))
                && read(server.0, "", false)?.is_some_and(|command| {
                    targets
                        .clone()
                        .any(|target| command_targets(&command, target, &arguments))
                });
            Ok(owned.then_some(ActionRegistration::LocalServer(index)))
        }
    }
}

fn update_action_key(
    classes: HKEY,
    parent_path: &Path,
    name: &str,
    executable: &Path,
    replacement: Option<&Path>,
    action: ActionKey<'_>,
) -> Result<bool> {
    let relative = parent_path.join(name);
    let Some(key) = open(classes, &relative, KEY_READ)? else {
        return Ok(false);
    };
    if owned_action(key.0, executable, action, replacement)?.is_none() {
        return Ok(false);
    }
    drop(key);
    if let Some(replacement) = replacement {
        let Some(key) = open(classes, &relative, KEY_READ | KEY_WRITE)? else {
            return Ok(false);
        };
        let Some(registration) = owned_action(key.0, executable, action, Some(replacement))? else {
            return Ok(false);
        };
        let replacement_path = normalized(replacement);
        match registration {
            ActionRegistration::Verb => write(key.0, "Icon", format!("\"{replacement_path}\",0"))?,
            ActionRegistration::InprocServer => {
                let Some(server) = open(key.0, "InprocServer32", KEY_READ | KEY_WRITE)? else {
                    return Ok(false);
                };
                write(
                    server.0,
                    "",
                    normalized(&replacement.with_file_name("SnowShotExplorerCommand.dll")),
                )?;
            }
            ActionRegistration::LocalServer(index) => {
                let Some(server) = open(key.0, "LocalServer32", KEY_READ | KEY_WRITE)? else {
                    return Ok(false);
                };
                write(
                    server.0,
                    "",
                    format!("\"{replacement_path}\" --explorer-command-server {index}"),
                )?;
                write(server.0, "ServerExecutable", &replacement_path)?;
            }
        }
        write(key.0, "InstallationExecutable", replacement_path)?;
    } else {
        let Some(parent) = open(classes, parent_path, KEY_READ | KEY_WRITE)? else {
            return Ok(false);
        };
        let Some(key) = open(parent.0, name, KEY_READ)? else {
            return Ok(false);
        };
        if owned_action(key.0, executable, action, None)?.is_none() {
            return Ok(false);
        }
        drop(key);
        let name = wide(name);
        check("RegDeleteTreeW", unsafe {
            RegDeleteTreeW(parent.0, PCWSTR(name.as_ptr()))
        })?;
    }
    Ok(true)
}

fn extension_menu_paths(classes: HKEY) -> Result<Vec<PathBuf>> {
    let mut extension_menus = Vec::new();
    if let Some(associations) = open(classes, ASSOCIATIONS, KEY_READ)? {
        for extension in names(associations.0)? {
            if extension.to_string_lossy().starts_with('.') {
                extension_menus.push(Path::new(ASSOCIATIONS).join(extension).join("shell"));
            }
        }
    }
    Ok(extension_menus)
}

fn update_classes(classes: HKEY, executable: &Path, replacement: Option<&Path>) -> Result<bool> {
    let mut changed = false;
    let mut failure = None;
    let extension_menus = extension_menu_paths(classes).unwrap_or_else(|error| {
        failure = Some(error);
        Vec::new()
    });
    let mut record = |result: Result<bool>| match result {
        Ok(updated) => changed |= updated,
        Err(error) => {
            failure.get_or_insert(error);
        }
    };
    let edition = if crate::edition::MINI { 'C' } else { 'B' };
    for (index, action) in [
        "pin_to_screen",
        "pin_to_screen_and_annotate",
        "stitch_images",
        "split_images",
        "edit_images",
        "compress_images",
    ]
    .iter()
    .enumerate()
    {
        let clsid = format!("{{3F1E90A2-7D4B-4F21-9D8A-6B0C5E2F70{edition}{index}}}");
        // Earlier builds registered all-file verbs. Remove them even when relocating an install.
        let old_wildcard = format!("{}.{action}", crate::edition::REGISTRY_NAME);
        record(update_action_key(
            classes,
            Path::new(r"*\shell"),
            &old_wildcard,
            executable,
            None,
            ActionKey::Verb(&clsid),
        ));
        let file_verb = format!("{}.File.{action}", crate::edition::REGISTRY_NAME);
        for parent in &extension_menus {
            record(update_action_key(
                classes,
                parent,
                &file_verb,
                executable,
                replacement,
                ActionKey::Verb(&clsid),
            ));
        }
        record(update_action_key(
            classes,
            Path::new("CLSID"),
            &clsid,
            executable,
            replacement,
            ActionKey::Class(index),
        ));
    }
    record(update_legacy_classes(classes, executable, replacement));
    failure.map_or(Ok(changed), Err)
}

fn update_legacy_classes(
    classes: HKEY,
    executable: &Path,
    replacement: Option<&Path>,
) -> Result<bool> {
    let Some(associations) = open(classes, ASSOCIATIONS, KEY_READ)? else {
        return Ok(false);
    };
    let mut changed = false;
    let mut failure = None;
    for extension in names(associations.0)? {
        if !extension.to_string_lossy().starts_with('.') {
            continue;
        }
        let relative = PathBuf::from(extension)
            .join("shell")
            .join(crate::edition::SHELL_VERB);
        let operation = (|| -> Result<bool> {
            let Some(verb) = open(associations.0, relative.as_os_str(), KEY_READ)? else {
                return Ok(false);
            };
            if !owned(verb.0, executable)? {
                return Ok(false);
            }
            drop(verb);
            if let Some(replacement) = replacement {
                let Some(verb) = open(associations.0, relative.as_os_str(), KEY_READ | KEY_WRITE)?
                else {
                    return Ok(false);
                };
                // Recheck after acquiring write access; another installation can claim the verb.
                if !owned(verb.0, executable)? {
                    return Ok(false);
                }
                let replacement = normalized(replacement);
                let Some(command) = open(verb.0, "command", KEY_READ | KEY_WRITE)? else {
                    return Ok(false);
                };
                write(
                    command.0,
                    "",
                    format!("\"{replacement}\" --edit-with-snow-shot \"%1\""),
                )?;
                write(verb.0, "Icon", format!("\"{replacement}\",0"))?;
                write(verb.0, "InstallationExecutable", replacement)?;
            } else {
                let Some(associations) = open(classes, ASSOCIATIONS, KEY_READ | KEY_WRITE)? else {
                    return Ok(false);
                };
                let Some(verb) = open(associations.0, relative.as_os_str(), KEY_READ)? else {
                    return Ok(false);
                };
                if !owned(verb.0, executable)? {
                    return Ok(false);
                }
                drop(verb);
                let relative = wide(relative.as_os_str());
                check("RegDeleteTreeW", unsafe {
                    RegDeleteTreeW(associations.0, PCWSTR(relative.as_ptr()))
                })?;
            }
            Ok(true)
        })();
        match operation {
            Ok(updated) => changed |= updated,
            Err(error) => {
                failure.get_or_insert(error);
            }
        }
    }
    failure.map_or(Ok(changed), Err)
}

fn user_sid(sid: &str) -> bool {
    sid.starts_with("S-1-5-21-") || sid.starts_with("S-1-12-1-")
}

fn update_installation_shell_verbs(root: &Path, replacement: Option<&Path>) -> Result<()> {
    let executable = root.join(crate::edition::APP_PATH);
    let replacement = replacement.map(|root| root.join(crate::edition::APP_PATH));
    let replacement = replacement.as_deref();
    let mut failure = None;
    let mut record = |result: Result<()>| {
        if let Err(error) = result {
            failure.get_or_insert(error);
        }
    };
    record((|| {
        if let Some(classes) = open(HKEY_CURRENT_USER, CLASSES, KEY_READ)? {
            update_classes(classes.0, &executable, replacement)?;
        }
        Ok(())
    })());
    let mut loaded = std::collections::HashSet::new();
    let users = match names(HKEY_USERS) {
        Ok(users) => users,
        Err(error) => {
            record(Err(error));
            Vec::new()
        }
    };
    for name in users {
        let text = name.to_string_lossy();
        let Some(sid) = text.strip_suffix("_Classes").filter(|sid| user_sid(sid)) else {
            continue;
        };
        loaded.insert(sid.to_owned());
        record((|| {
            if let Some(classes) = open(HKEY_USERS, &name, KEY_READ)? {
                update_classes(classes.0, &executable, replacement)?;
            }
            Ok(())
        })());
    }
    let profiles = match open(HKEY_LOCAL_MACHINE, PROFILES, KEY_READ) {
        Ok(profiles) => profiles,
        Err(error) => {
            record(Err(error));
            None
        }
    };
    if let Some(profiles) = profiles {
        let names = match names(profiles.0) {
            Ok(names) => names,
            Err(error) => {
                record(Err(error));
                Vec::new()
            }
        };
        for sid in names {
            let text = sid.to_string_lossy();
            if !user_sid(&text) || loaded.contains(text.as_ref()) {
                continue;
            }
            record((|| {
                let Some(profile) = open(profiles.0, &sid, KEY_READ)? else {
                    return Ok(());
                };
                let Some(directory) = read(profile.0, "ProfileImagePath", true)? else {
                    return Ok(());
                };
                let hive =
                    PathBuf::from(directory).join("AppData/Local/Microsoft/Windows/UsrClass.dat");
                if !hive.is_file() {
                    return Ok(());
                }
                let hive_path = wide(hive.as_os_str());
                let mut hive = HKEY::default();
                check("RegLoadAppKeyW", unsafe {
                    RegLoadAppKeyW(
                        PCWSTR(hive_path.as_ptr()),
                        &mut hive,
                        (KEY_READ | KEY_WRITE).0,
                        REG_PROCESS_APPKEY,
                        None,
                    )
                })?;
                let hive = Key(hive);
                if update_classes(hive.0, &executable, replacement)? {
                    check("RegFlushKey", unsafe { RegFlushKey(hive.0) })?;
                }
                Ok(())
            })());
        }
    }
    failure.map_or(Ok(()), Err)
}

pub(super) fn remove_installation_shell_verbs(root: &Path) -> Result<()> {
    update_installation_shell_verbs(root, None)
}

pub(super) fn migrate_installation_shell_verbs(previous: &Path, root: &Path) -> Result<()> {
    if equal(normalized(previous), normalized(root)) {
        return Ok(());
    }
    update_installation_shell_verbs(previous, Some(root))
}
