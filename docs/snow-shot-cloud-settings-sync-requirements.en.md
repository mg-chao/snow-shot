# Snow Shot cloud settings sync requirements

## 1. Feature and scope

The full edition of Snow Shot syncs preferences across Windows and macOS devices signed in to the
same account. When a user changes the screenshot delay, a color, or a shortcut on one device, other
devices with sync enabled can pick up the change in the background. The app always loads local
settings first. Startup, screenshot shortcuts, screenshot editing, pinning content to the screen,
screen recording, and export all work independently of sync requests.

- **Editions and services.** The full edition supports the official account service and custom
  account services that implement settings sync. Snow Shot Mini has no settings sync entry and sends
  no settings sync requests.
- **Backend implementation.** The cloud settings sync backend is in `E:\SnowShotApi`.
- **Account boundaries.** The client contacts only the service that hosts the signed-in account.
  Accounts with the same name on different services have separate data. Switching to a custom
  service never forwards settings to the official service. If the service does not support sync, the
  app silently continues using local settings.
- **Settings in scope.** The first release uses local configuration schema v9 and allowlists 291
  keys. Of these, 145 cover general settings and 146 cover keyboard bindings, global mouse bindings,
  and rules for disabling input in fullscreen windows. General settings apply according to the
  target device's capabilities. The cloud stores separate Windows and macOS records for bindings and
  fullscreen rules. The appendix lists every key.
- **Device data.** Settings snapshots exclude screenshots, clipboard content, annotations, history,
  pinned content, screen recording files, and account profile data. Each device manages its save
  paths, skin and custom icon files and their paths, window and floating toolbar positions, system
  integrations, permissions, capture APIs and hardware options, proxies, service addresses,
  credentials, and history retention policies.

## 2. Settings entry and controls

Place **Cloud settings sync** at the top of **Data storage** in Settings. When the user is signed
out, show **Sign in to sync settings** on the right. Selecting it opens the account sign-in page.
When the user is signed in, show **Settings** on the right. Selecting it opens the sync settings
dialog.

| Dialog item | Behavior |
| --- | --- |
| Sync on this device | The toggle applies to the current account on the current service and device. It defaults to on at first use, including after an upgrade for users who are already signed in. If the user turns it off, it stays off after a restart or another sign-in to the same account. |
| Last successful sync | Show the last time this account completed a successful sync on this device. Keep the time across restarts. Show **Not synced yet** until the first success. Sync activity on other devices does not change this time. |
| Sync now | Available while sync is on. Selecting it immediately checks the cloud and processes pending changes in the background, even after a recent automatic check or while edits are waiting to be batched. Repeated clicks share one sync operation. Disable the button while sync is paused. |

Update **Last successful sync** only after the service confirms the check, acknowledges all changes
due for upload, and the device successfully saves all changes due for download. A check that
confirms neither side has changes also counts as a success. Keep the previous time if a request or
local save fails, or if a change due for upload is still rejected. Device-local settings, items
unsupported by the current version, and shortcut registration failures caused by the local system do
not prevent the remaining settings from syncing successfully.

The dialog contains only these three items. Enabling sync by default, background sync, and the
success or failure of a manual sync do not show additional notifications or confirmation dialogs.
Turning off sync on this device preserves the account's cloud snapshot. The first release has no
action for deleting cloud settings.

## 3. Sync timing and service load

| Trigger | Requirement |
| --- | --- |
| Successful sign-in or session restoration at startup | Read the sync toggle saved for this account on this device. Start sync in the background if it is on. Renewing the same session continues the work already in progress. |
| Local edit | Apply the edit locally at once. About 3 seconds after the last edit in a series, upload the changed eligible keys in one batch. For repeated edits to the same key, send only the user's final operation. |
| Opening Settings or regaining connectivity | Check for cloud updates in the background. If Settings is reopened soon after a check, reuse its result or the request already in progress. When connectivity returns, also process edits queued while offline. |
| App running in the system tray | While the user is signed in and sync is on, check about every 30 minutes. Stagger checks across devices. Stop background checks while sync is paused or the user is signed out. |
| Selecting **Sync now** | Honor the request even if an automatic check just completed. If sync is already in progress, combine the work and retain any new local edits made during the request. |

A routine check first compares the cloud revision. If the revision matches and there are no pending
changes, the service only needs to confirm that nothing has changed. Upload local edits
incrementally. Read the relevant snapshot only to initialize sync, fetch new cloud values, or
resolve a concurrent edit. Local writes made to apply remote changes, convert data for
compatibility, fall back to an available resource, or resolve a conflict do not trigger another
upload.

The server keeps one current snapshot per account. It contains shared settings and any platform
records that have been initialized. The full snapshot has a **64 KiB** limit after decompression.
The same limit applies to the full snapshot after merging an incremental update. The server
validates full keys against the allowlist and checks data types, allowed ranges, and the length of
free-text fields such as filename templates. If it rejects an item, other changes can still be
committed independently if they fit within the limit.

A compact UTF-8 JSON sample containing the Windows full edition's defaults for all 291 keys is about
17.2 KB, or 3.9 KB with gzip. Adding an equally sized set of bindings for a second platform gives an
estimate of 25.8 KB. At that size, raw snapshots for 1 million accounts take about 26 GB before
indexes, backups, and replicas. In practice, initialization writes only eligible non-default
preferences.

## 4. Sign-in, account switching, and pausing sync

### 4.1 Which account owns a setting

The device keeps preferences established while signed out separately from the allowlisted settings
each account has used on each service. Applying one account's cloud values preserves the preferences
for other accounts and for signed-out use.

| Action | Local and cloud behavior |
| --- | --- |
| The user signs out | Keep the current settings in effect on this device. Cancel uncommitted changes from the previous session. Late responses to requests already sent must not alter signed-out state or another account. Record subsequent manual edits in the device's independent signed-out preferences. |
| Signing in to a different account for the first time on this device | Start with the device's independent preferences from before sign-in, including explicit edits made after sign-out, then apply the new account's cloud settings. Exclude values received from the previous account and that account's pending changes from the new account's initial data. |
| Signing back in to a previously used account | Start with the settings that account last used on this device. If sync is on, update them using the account's cloud snapshot and revision. If sync is paused, keep it paused. |
| Temporary loss of connectivity, normal shutdown, or restart after a crash | Keep this account's pending edits made while sync was on, along with the values against which to compare them. Resume processing when the same account and service are available again. |
| Turning off sync on this device | Immediately stop syncing this account on the device and prevent responses already in flight from changing local state. Other devices continue syncing independently. Edits made during the pause apply only on this device. |
| Turning sync back on | Read the latest cloud state first. For a key edited during the pause, apply the cloud value if the key has a cloud record. If the record is missing, keep the local value and wait until the user edits the key again before uploading it. Resuming sync does not upload edits made during the pause as first-time contributions. |

For example, a device uses the light theme before sign-in. Account A syncs the dark theme. After
signing out of A, the user still sees the dark theme and changes only the font size. When account B
signs in for the first time with no cloud preferences, it gets the light theme and the new font
size. If B already has a cloud theme, that theme applies.

### 4.2 First sync and initialization

1. When sync is enabled, first read the account's snapshot and revision from its current service.
   Create a snapshot only if the service explicitly confirms that none exists. Loss of connectivity,
   authentication failure, an unsupported endpoint, and other read errors do not establish that the
   snapshot is empty.
2. Initialize a new snapshot only with preferences that belong to the current account, are eligible
   for sync, are supported on the device, and differ from its defaults. Initialize bindings and
   fullscreen rules only for the current platform. An empty snapshot or platform record counts as
   initialized even if there are no non-default preferences.
3. When a device first syncs with the account, use the cloud record for each key already set there.
   The device may supply a non-default local preference for a key that has never been set in the
   cloud. An explicit value, an explicit clear, and a reset-to-default marker all count as having
   been set. The first Mac to join can contribute its AirDrop export preferences and macOS bindings.
4. When creating a record or filling in a missing key, the server checks again at commit time that
   it is still missing. If two devices try to supply the same key, the first successful commit wins.
   The other device reads that result and continues processing any remaining missing keys.
5. Save new edits made during the signed-in session while the first read is pending. Apply the
   initial snapshot, then retain and submit those new edits. Handle them separately from preferences
   that already existed before sign-in.

## 5. Edits, setting operations, and concurrency

Explicit edits made through Settings, the screenshot or screen recording toolbar, quick actions, and
user-authorized Model Context Protocol (MCP) operations are eligible for sync. When importing a
configuration file, submit only allowlisted keys that the file explicitly provides and that pass
validation. Defaults filled in locally during import, migration repairs, and data outside the
allowlist, including credentials, stay on the device.

| Setting operation or state | Meaning in the cloud and on the receiving device |
| --- | --- |
| Unset | The cloud has no record for the key. The receiving device keeps its value. On first sync, it may supply a non-default preference as described in section 4.2. |
| Set an explicit value | Save the specific value the user chose. Other devices use that value even if it happens to match the current default on the originating device. |
| Clear explicitly | Save the empty value accepted by the key. For example, `[]` is an empty keyboard binding list and `{}` is an empty global mouse combination. The action remains unbound on the receiving device. A new device must not fill in its own binding. |
| Reset to default | Keep a reset-to-default marker in the cloud. Each device resolves the default for its own version, platform, and configuration mode. Resetting a section to its defaults sends this operation for every eligible setting in the section, even if the value displayed locally has not changed. |

Each incremental update carries the cloud revision it was based on. After validation, the server
commits each independent change or group of interdependent changes atomically. It updates only the
relevant keys or platform records for that account. On a revision conflict, the client reads the
latest snapshot and compares each affected key with the last value and operation confirmed by both
sides.

- If the two sides changed different keys, retain both changes and retry the pending local changes
  against the new revision.
- If both sides changed the same key to the same result, treat it as confirmed. If the results
  differ, use the value already committed in the cloud. Compare an explicit value, a clear, and a
  reset to default as distinct operations.
- If the user edits the same key again after a request was sent, its response confirms only the
  change that request carried. The newer edit remains pending.
- Shortcut swaps, layouts, and export tasks also follow the scope and grouping rules in section 6.
  Apply remote updates only to the affected keys that pass validation. Preserve the values of other
  local settings.

## 6. Rules for specific settings

### 6.1 Keyboard and mouse bindings and fullscreen rules

The cloud stores separate Windows and macOS records for global shortcuts, global mouse combinations,
application shortcuts, and rules for automatically disabling input in fullscreen windows. Each
device reads and writes only its platform's record. For each action, preserve the complete binding
list and any platform-specific key information, or the full mouse combination. Apply only actions
supported by the installed version. Check application shortcuts for conflicts in the scopes where
they actually apply. The same key can be reused in scopes that do not conflict.

- **Conflicts within the configuration.** If two devices on the same platform assign the same key to
  different actions whose scopes prohibit sharing that key, keep the first binding successfully
  committed. Apply each action's full binding list as one unit. When two actions swap bindings,
  validate the final assignments and commit or roll back the swap as a unit. Other independent
  bindings continue to sync.
- **System registration failures.** Keep a valid binding in the saved configuration even if another
  application occupies the key or the local system cannot register it. That binding may remain
  unregistered on the device while other bindings work normally. The failure leaves the cloud
  binding intact, shows no additional prompt, and does not affect the last successful sync time.
- **Temporarily disabled input.** Each device manages whether its global keyboard shortcuts or
  global mouse input are temporarily disabled. Respect the current state when applying cloud
  settings. Registration can wait until input is enabled again. The bindings for toggling these
  states and the fullscreen rules that control when input is automatically disabled still sync in
  their platform records.

### 6.2 Toolbar layouts and tray menus

The cloud keeps the complete layout, including item IDs, groups, order, and visibility. Each device
displays only supported items. If an older version recognizes only part of a layout, the user's
edits to that layout apply locally. Other devices continue to use the complete cloud layout. Routine
downloads must preserve the local edits. Once the device upgrades to a version that recognizes the
whole layout, attempt a merge using the last layout confirmed by both sides. Edits stay local if the
merge cannot preserve grouping, relative order, or visibility. This requires no user notification.

Sync the floating toolbar's enabled state, icon or toolbar mode, drop behavior, opacity, and layout.
Store its screen position locally.

### 6.3 Export tasks

Each export trigger has its own set of follow-up tasks. `copy_to_clipboard` copies the image,
`copy_file` copies the file, `auto_save` saves automatically, and `pin_to_screen` pins the content
to the screen. A receiving device merges only the tasks supported by that trigger and always
preserves its local `upload_to_cloud` (automatic upload) choice. A remote clear or reset to default
also applies only to tasks eligible for sync. Changing only the local automatic upload option sends
no settings upload request.

| Trigger | Follow-up tasks eligible for sync |
| --- | --- |
| Copy a screenshot | `auto_save`, `pin_to_screen` |
| Save a screenshot | `copy_file`, `pin_to_screen` |
| Pin a screenshot to the screen | `copy_to_clipboard`, `auto_save` |
| Print a screenshot | `copy_to_clipboard`, `pin_to_screen` |
| Successfully share a screenshot via AirDrop (macOS) | `copy_to_clipboard`, `auto_save`, `pin_to_screen` |
| Successfully upload a screenshot manually | `auto_save`, `pin_to_screen` |
| Copy pinned content | `auto_save` |
| Save pinned content | `copy_file` |
| Print pinned content | `copy_to_clipboard` |
| Successfully share pinned content via AirDrop (macOS) | `copy_to_clipboard`, `auto_save` |
| Successfully upload pinned content manually | `auto_save` |

The manual upload triggers define only what happens after a user-initiated upload succeeds. Syncing
a trigger or its manual upload shortcut does not upload an image.

### 6.4 Device resources and work in progress

| Item | Behavior when a cloud preference arrives |
| --- | --- |
| Language, theme, font, and font size | Update the interface through the app's runtime settings handlers. If the selected font is missing, temporarily use the system font and remember the preferred font. Use it when the app detects that it is installed. Apply this rule to both the interface font and the default font for pinned text. |
| Skins and tray icons | Apply skin display settings to images already on the device. Sync built-in tray icon choices. Continue using locally configured custom icons according to the device's own settings. |
| Text recognition (OCR) model | Use the selected model when the device supports it and its resources are ready. If the resources have not been downloaded, continue using an available local model. Use the normal model download flow only when the user actually uses OCR. A model selection unsupported on this device stays in the cloud for devices that support it. |
| Translation service | Sync only supported built-in service IDs. If the user explicitly selected a custom translation service on this device, keep using it until the user selects a built-in service again. Snapshots exclude custom service IDs, addresses, and credentials. |
| Encoding and platform capabilities | Apply an encoding preset only if the target encoder supports it. Apply Windows-only actions, macOS AirDrop, and feature-dependent actions such as translation, QR code recognition, and table recognition only where the platform and feature are available. |
| Translation, recording, or export already in progress | Save the new preferences. Keep the current results and let work in progress continue with the settings it started with. A remote language or model change must not resend existing text, clear the current translation, or trigger automatic retranslation. Use the new preferences when the user next enters text, requests retranslation, or starts a new task. Recordings and exports in progress also keep their original parameters. |

Skip keys the device cannot apply and continue processing other valid settings in the batch. A
temporary local fallback never replaces the preferred cloud value. Saving a preference and using it
can happen at different times. For example, an encoding preset saved during a recording can take
effect at the next recording.

## 7. Failure and recovery

- For recoverable errors such as network failures, timeouts, or a temporarily unreadable snapshot,
  keep using local settings and increase the interval between retries after consecutive failures.
  Check again when connectivity returns or the user selects **Sync now**. A temporary failure must
  not create a snapshot or repeat initialization.
- If a custom service does not support sync, silently end the check and keep the local settings and
  last successful sync time. Initialize data only when the service explicitly confirms that it
  supports sync and has no snapshot for the account.
- If local storage is read-only or a write fails, retain the last confirmed progress in applying
  changes. Save the remaining items when storage is writable again. A downloaded change counts as
  complete only once it has been saved locally.
- Keep permanently invalid or oversized values on the device and stop resubmitting the same rejected
  value. The item becomes eligible again when the user corrects it or the reason for rejection, such
  as a size limit, no longer applies. Preserve the previous valid cloud value for that item and
  continue submitting independent changes.
- After sign-out, an account or service switch, or a pause, responses to earlier requests must not
  change the current settings, pending changes, or last successful sync time. Repeating a check or
  retrying the same operation must converge on the same result.

## 8. Acceptance scenarios

| Scenario | Expected result |
| --- | --- |
| A signed-in user upgrades, or a new user signs in for the first time. | The full edition enables sync by default and starts it in the background in both cases. Mini has no sync entry. If the user previously turned sync off for the account on this device, it stays off after a restart or another sign-in. |
| The user repeatedly adjusts a slider and then takes a screenshot using a shortcut. | The slider takes effect immediately and the shortcut responds normally. About 3 seconds after the edits end, only the final value is submitted. Receiving devices do not upload it again. |
| Two devices are running in the system tray. The user changes a setting on one device. | The other device can receive the update when Settings opens. If it stays in the tray, it discovers the change during its roughly 30-minute background check cycle. An unchanged cloud snapshot is not transferred again. |
| The user completes a manual sync with no changes, the service goes offline, and the user selects **Sync now** again. | The first check advances the last successful sync time. The second keeps the previous time. Repeated clicks do not start parallel sync operations. Neither outcome opens an additional dialog. |
| Sync is paused on one device. A second device changes the theme. On the paused device, the user edits both the theme and a preference missing from the cloud. | **Sync now** is disabled on the paused device while the second device continues to sync. On resuming, the first device adopts the cloud theme and keeps its local value for the missing preference. It uploads that preference only when the user next edits it. Each device retains its own toggle and sync time. |
| A new Windows device creates an empty snapshot. An existing device or the account's first Mac signs in later. | The empty snapshot remains initialized. The existing device can supply its non-default preferences. The Mac can contribute its bindings and AirDrop tasks. Neither device fills in items that have been explicitly cleared or reset to default. |
| On first sign-in, the user changes the font size before the cloud responds. The device then goes offline and restarts. | The new font size is retained for submission. Processing continues when the same session is restored. If the user explicitly signs out and switches accounts, the previous account's queue and late responses do not carry over. |
| A device switches from the official service to a sync-capable custom service. The accounts on the two services have the same name. | Use the custom service account's snapshot and its separate preferences on this device. Pending changes, the last successful sync time, and late responses from the official service do not carry over. |
| Two devices edit different keys while offline. On another occasion, both edit the same general setting while offline. | Edits to different keys are both retained. If the edits to the same key differ, use the value already committed in the cloud. If they produce the same result, there is no duplicate write. |
| The user imports colors from a file that also contains credentials, then resets a section of bindings through MCP. | Only eligible keys explicitly present in the file are uploaded by the import. The section reset syncs as a reset to default, so each device resolves its own default bindings. An explicit unbind keeps the binding empty. |
| Two actions swap keys. In a separate case, two devices claim the same key concurrently. Another application also occupies a key on the target device. | The whole swap takes effect or is rolled back. The first successful commit wins a configuration conflict. An external application occupying a key causes only a local registration failure. The cloud configuration and the criteria for updating the last successful sync time are unaffected. |
| A new version adds a toolbar button. An older version rearranges the groups it can display and checks for updates again. | The cloud retains the new button, grouping, and visibility. The older version keeps its local edits across routine downloads. The user sees no additional prompt. |
| Automatic upload after export is enabled locally. Another device clears the follow-up tasks for the same trigger. | Keep the local automatic upload choice and clear the tasks eligible for sync. Toggling automatic upload alone sends no settings sync request. |
| A translation is on screen, or a recording or export is in progress, when a new model, language, or encoding preset arrives. | The app keeps the current translation and continues the current task without resending the text. New work uses the new settings. Syncing an OCR model preference does not start a model download. |
| The device lacks a font, uses a custom translation service, or is connected to a server that does not support sync. | Use a temporary font fallback where needed and preserve the local translation service choice. An unsupported sync service must not be treated as an empty snapshot or cause settings to be sent to another service. |
| A batch includes an oversized filename template and a valid theme, or the device's local storage is read-only during a download. | The valid theme can be committed independently. The client stops resubmitting the oversized template. If a local save fails, retry when storage is writable. Keep the last successful sync time while an item due for sync is still failing. |
| An initial snapshot exceeds 64 KiB after decompression, or merging an incremental update would push the existing snapshot over 64 KiB. | The server rejects the oversized write and preserves the last valid committed snapshot. The client can still use its local settings and submit independent changes that fit within the limit. |

## Appendix. Configuration allowlist

Form each full key by joining the prefix and one key name from the same row. For example,
`screenshot/` and `delay_seconds` form `screenshot/delay_seconds`. Both server and client require an
exact match on the full key. A listed prefix alone does not allow a key. The tables list the
candidate keys. The platform, feature, and resource conditions in section 6 also apply.

### General settings (145 keys)

| Setting or action | Key prefix | Key name |
| --- | --- | --- |
| General · Interface language | `interface/` | `language` |
| General · Opening files for editing (Windows) | `system/` | `annotation_mode_when_opening_for_editing` |
| Appearance · Theme and font size | `interface/` | `theme_mode`, `theme_primary_color`, `app_font_size` |
| Appearance · Interface font | `interface/` | `app_font` |
| Appearance · Skin display | `interface/` | `skin_position`, `toolbar_skin_position`, `tray_menu_skin_position`, `skin_display_mode`, `skin_opacity`, `skin_blur_level`, `skin_mask_opacity` |
| Appearance · Toolbar size | `screenshot_ui/` | `toolbar_size` |
| Screenshot · Smart selection | `screenshot_selection/` | `smart_selection` |
| Screenshot · Screenshot delay in seconds | `screenshot/` | `delay_seconds` |
| Screenshot · Selection and pointer actions | `screenshot/` | `selection_resize_mode`, `auto_execute_after_text_recognition`, `double_click_action`, `middle_mouse_button_action`, `quick_selection_modification`, `hidpi_image_scaling`, `keep_annotations_editable_after_pinning_to_screen` |
| Screenshot · Notifications, cursor, and capture content | `screenshot/` | `shutter_sound_notification`, `confirm_before_exiting_via_shortcut`, `auto_recognize_qr_code`, `capture_cursor`, `capture_ui_in_scrolling_screenshot` |
| Screenshot · Selection and color picker UI | `screenshot_ui/` | `selection_transition_animation`, `color_picker_display_mode`, `selection_border_color`, `selection_mask_color`, `shortcut_hint_opacity`, `area_type_hint_enabled`, `show_edit_selection_toolbar` |
| Screenshot · Guides | `screenshot_ui/` | `show_guides_by_default`, `adaptive_guide_colors`, `cursor_guide_line_color`, `selection_center_guide_line_color`, `monitor_center_guide_line_color`, `color_picker_center_guide_line_color` |
| Screenshot · Annotation tool behavior | `drawing/` | `automatically_select_newly_created_elements`, `automatic_selection_excluded_tools`, `quick_selection_disabled_tools`, `delete_text_after_cursor`, `remember_last_used_tool`, `always_show_first_toolbar_group_button`, `sync_tool_colors` |
| Screenshot · Color palettes | `screenshot/` | `stroke_color_presets`, `fill_color_presets`, `highlight_color_presets` |
| Screenshot · Annotation and action toolbar layouts | `screenshot_toolbar/` | `layout`, `action_tools_layout` |
| Pin to screen · Mouse, zoom, and window behavior | `pin_to_screen/` | `mouse_wheel_zoom_mode`, `mouse_wheel_opacity_step`, `mouse_wheel_zoom_step`, `double_click_action`, `middle_mouse_button_action`, `auto_resize_window`, `duplicate_content_action`, `shake_window_to_hide_others`, `show_window_buttons` |
| Pin to screen · Text recognition, selection, and close confirmations | `pin_to_screen/` | `automatic_text_recognition`, `text_selection_on_recognition_results`, `quick_annotation_for_selected_text`, `exit_annotation_mode_before_closing`, `confirm_before_closing_window`, `confirm_before_destroying_window` |
| Pin to screen · Borders and shadows | `pin_to_screen/` | `show_border_by_default`, `show_shadow_by_default`, `border_color`, `border_active_color`, `locked_border_color`, `shadow_color`, `shadow_active_color`, `locked_shadow_color` |
| Pin to screen · Default text style | `pin_to_screen/` | `text_maximum_width`, `text_font_family`, `text_font_size`, `text_color`, `text_background_color` |
| Pin to screen · Action toolbar layout | `pin_to_screen/` | `action_tools_layout` |
| Screen recording · Video and audio | `screen_recording/` | `clarity`, `frame_rate`, `video_quality`, `separate_audio_tracks`, `encoding_preset` |
| Screen recording · Animated image output | `screen_recording/` | `animated_image_clarity`, `animated_image_frame_rate`, `loop_animated_images` |
| Screen recording · Capture content and toolbar | `screen_recording/` | `capture_toolbar_in_recording`, `action_tools_layout`, `auto_hide_toolbar` |
| Screen recording · Interaction and notifications | `screen_recording/` | `auto_exit_after_recording_ends`, `notify_after_export_completes`, `show_status_prompts` |
| Text recognition · Output and display | `text_recognition/` | `save_recognition_result_as_image`, `default_formatting`, `default_punctuation`, `show_original_image_preview`, `fill_style`, `show_text_highlighting` |
| Text recognition · Model | `text_recognition/` | `model_type` |
| Translation · Languages and results | `screenshot_translation/` | `source_language`, `target_language`, `secondary_target_language`, `language_detection_confidence_threshold`, `original_image_translation`, `copy_translation_results_as_image`, `layout_processing` |
| Translation · Built-in service selection | `screenshot_translation/` | `model` |
| Translation · Extended features | `extended_features/` | `translation_page_enabled`, `jump_to_translation_page`, `standalone_translation_window` |
| System tray · Built-in icons | `tray/` | `icon`, `dark_icon` |
| System tray · Click actions and menu | `tray/` | `left_click_action`, `middle_click_action`, `menu_options` |
| Floating toolbar · Enablement, mode, and display | `floating_toolbar/` | `enabled`, `mode`, `drop_behavior`, `opacity`, `layout` |
| Export · Image formats | `screenshot/` | `image_format`, `compression_level`, `image_quality`, `pdf_page_size` |
| Export · Saving and image filenames | `screenshot/` | `copy_image_file_to_clipboard`, `save_as_file_dialog`, `show_save_result`, `manual_save_filename_format`, `auto_save_filename_format` |
| Export · Screen recording filename | `screen_recording/` | `video_filename_format` |
| Export · Screenshot triggers | `export_tasks/` | `screenshot_copy`, `screenshot_save`, `screenshot_pin`, `screenshot_print`, `screenshot_airdrop`, `screenshot_upload` |
| Export · Pinned content triggers | `export_tasks/` | `pinned_copy`, `pinned_save`, `pinned_print`, `pinned_airdrop`, `pinned_upload` |

For **Remember last used tool** and **Sync tool colors**, sync only the toggles. Each device keeps
its last used tool and current shared color locally. The three user-configured color palettes can
sync. Apply the tray middle-click action only on platforms that offer it. Filename templates define
the naming format. Each device chooses its own save directories.

### Keyboard and mouse bindings and fullscreen rules (146 keys)

| Setting or action | Key prefix | Key name |
| --- | --- | --- |
| Global shortcuts · Screenshots | `global_shortcuts/` | `screenshot`, `screenshot_delay`, `screenshot_fixed`, `screenshot_ocr`, `screenshot_ocr_copy`, `screenshot_translation`, `screenshot_scrolling`, `screenshot_copy`, `screenshot_save`, `screenshot_quick_save`, `screenshot_full_screen`, `screenshot_focused_window`, `open_capture_history` |
| Global shortcuts · Pin to screen | `global_shortcuts/` | `pin_clipboard_content`, `pin_selected_files`, `restore_last_closed_windows`, `hide_show_all_windows`, `switch_window_group`, `open_pin_to_screen_management` |
| Global shortcuts · Screen recording | `global_shortcuts/` | `screen_record`, `screen_record_copy`, `open_screen_recording_folder` |
| Global shortcuts · Global canvas and image processing | `global_shortcuts/` | `global_canvas`, `stitch_selected_images`, `split_selected_images`, `edit_selected_images`, `compress_selected_images` |
| Global shortcuts · Settings and system actions | `global_shortcuts/` | `open_settings`, `translate_selected_text`, `toggle_window_under_cursor_always_on_top`, `toggle_global_hotkeys`, `toggle_global_mouse`, `toggle_disable_on_focused_fullscreen_window` |
| Automatic disabling in fullscreen windows | `global_shortcuts/` | `disable_on_focused_fullscreen_window`, `fullscreen_window_filter`, `fullscreen_disable_scope` |
| Global mouse · Screenshots and screen recording | `global_mouse/` | `screenshot_copy`, `screenshot_fixed`, `screenshot_ocr`, `screenshot_ocr_copy`, `screenshot_translation`, `screenshot_save`, `screenshot_quick_save`, `screen_recording` |
| Screenshot shortcuts · Selection and cursor | `screenshot_shortcuts/` | `move_tool`, `move_cursor_up`, `move_cursor_down`, `move_cursor_left`, `move_cursor_right`, `move_entire_selection`, `keep_selection_width_and_height_consistent`, `selection_aspect_ratio_snap`, `switch_selection_between_window_and_window_sub_element`, `next_selection_type`, `previous_selection_type` |
| Screenshot shortcuts · History and UI | `screenshot_shortcuts/` | `previous_screenshot_history`, `next_screenshot_history`, `select_previously_selected_area`, `select_next_previously_selected_area`, `duplicate_latest_region`, `toggle_cursor_visibility`, `recapture`, `toggle_coordinate_mode`, `toggle_guides`, `copy_color` |
| Screenshot shortcuts · Image recognition | `screenshot_shortcuts/` | `table_recognition`, `qr_code_recognition`, `text_recognition`, `text_translation` |
| Screenshot shortcuts · Actions | `screenshot_shortcuts/` | `video_recording`, `scrolling_screenshot`, `quick_save`, `print`, `save_as_file`, `pin_to_screen`, `pin_window_to_screen`, `cancel_screenshot`, `copy_to_clipboard`, `undo`, `redo`, `upload_to_cloud` |
| Annotation shortcuts · Drawing tools | `drawing_shortcuts/` | `select`, `shape`, `speech_bubble`, `arrow`, `distance`, `angle`, `bracket`, `line`, `magnifier`, `spotlight`, `brush`, `highlight` |
| Annotation shortcuts · Other tools and actions | `drawing_shortcuts/` | `text`, `serial_number`, `filter`, `eraser`, `reset_canvas`, `watermark`, `cycle_annotation_tool_options` |
| Pinned content shortcuts · Content and batch actions | `pin_to_screen_shortcuts/` | `copy_to_clipboard`, `copy_original_content`, `print`, `batch_select_all`, `batch_image_stitching`, `batch_copy_files_to_clipboard`, `batch_save_as_file`, `save_as_file`, `upload_to_cloud` |
| Pinned content shortcuts · Recognition and playback | `pin_to_screen_shortcuts/` | `show_text_recognition_results`, `drawing_mode`, `toggle_playback`, `next_frame`, `previous_frame`, `decrease_playback_speed`, `increase_playback_speed`, `reset_playback_speed` |
| Pinned content shortcuts · Windows | `pin_to_screen_shortcuts/` | `resize_window`, `hide_to_top`, `toggle_click_through`, `toggle_lock`, `always_on_top`, `show_border`, `show_shadow`, `thumbnail_mode`, `close_window`, `destroy_window` |
| Pinned content shortcuts · Transforms and cursor | `pin_to_screen_shortcuts/` | `increase_opacity`, `decrease_opacity`, `increase_scale`, `decrease_scale`, `rotate_clockwise`, `rotate_counterclockwise`, `flip_horizontal`, `flip_vertical`, `reset_transform`, `move_cursor_up`, `move_cursor_down`, `move_cursor_left`, `move_cursor_right` |
| Screen recording shortcuts · Recording areas and actions | `screen_recording_shortcuts/` | `previous_recording_area`, `next_recording_area`, `export`, `toggle_recording`, `copy_to_clipboard`, `end_recording` |
