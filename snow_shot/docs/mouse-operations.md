Please implement the feature described below. Before starting, carefully review the requirements and the existing implementation, then design a reliable architecture for the feature. There is ample development time, so take the time to refine the implementation and ensure high standards for architecture, performance, and code quality.

# Snow Shot mouse operation settings

## 1. Settings page

Add **Mouse operations** to the settings navigation directly below **Application shortcuts**, at the same level. Users can configure mouse gestures for screenshot sessions and pinned windows on this page.

- Use the two-column layout from **Application shortcuts**. Show **Screenshots** first and **Pin to screen** second. Each entry displays the action name and its current binding, or **Unset** if the action is unbound.
- Selecting a configurable action opens a dialog modeled on the **Global mouse** configuration dialog. It has two fields, **Keyboard modifiers** and **Mouse operation**.
- Each category has its own reset to factory defaults. Remove **Double-click action** and **Middle mouse button action** from both **Screenshots → Interaction** and **Pin to screen → Interaction**. The **Mouse operations** page now owns configuration and reset for all four settings.
- Users continue to configure the wheel zoom anchor, regular zoom step, and regular opacity step on the **Pin to screen** settings page.

## 2. Bindings and validation

### 2.1 Inputs for each action

Each configurable action has one binding consisting of zero or more keyboard modifiers and one mouse operation. The type of action determines which mouse operations are available.

| Field or operation type | Available choices | Selection rule |
| --- | --- | --- |
| Keyboard modifiers | Windows, Ctrl, Alt, and Shift on Windows. Command, Control, Option, and Shift on macOS. | Select any number, including zero. Order does not matter. |
| Click | Single-click or double-click with the left button, right button, middle button, side button 1 (back), or side button 2 (forward). Also offers **None**. | Select one. |
| Wheel | Mouse wheel or **None**. | Select one. A single binding covers both scroll directions. |
| Drag | Drag with the left button, right button, middle button, side button 1 (back), or side button 2 (forward). Also offers **None**. | Select one. |

Selecting **None** unbinds the action. Keyboard modifiers remain editable and are saved for the next time a mouse operation is selected. Any number of actions can be unbound, including actions with the same saved modifiers. The defaults below use Windows key names. On macOS, use native key names. In the default bindings, Ctrl maps to Command, Alt to Option, and Shift to Shift. Control is a separate choice on macOS.

Cloud settings store bindings separately for each platform. The backend implementation for settings sync is in `E:\SnowShotApi` on Windows.

### 2.2 Conflicts and reserved gestures

1. **Match modifiers exactly.** An unmodified gesture matches only when no modifier is held. For example, Ctrl+Shift+wheel does not trigger an action bound to Ctrl+wheel.
2. **Check conflicts within each category.** Each binding can belong to only one action in a category. **Screenshots** and **Pin to screen** may reuse the same binding. This page does not check or display warnings for overlaps with **Global mouse**.
3. **Reserve the unmodified left single-click.** Neither category accepts this gesture as a configurable binding. Both accept modified left single-clicks and unmodified left double-clicks.
4. **Reserve unmodified right clicks during screenshots.** An unmodified right single-click is fixed to **Go back one step**. An unmodified right double-click is also unavailable for assignment. Users can configure modified right clicks during screenshots and unmodified right clicks in pinned windows.
5. **Make single-click and double-click mutually exclusive for the same button and modifiers.** This applies to all five buttons within a category. Assigning either gesture prevents assigning the other with the same modifiers. Different modifier sets can have separate bindings.
6. **Allow clicks and drags to share a button and modifiers.** Releasing the button before reaching the drag threshold counts as a click. Once a drag starts, releasing the button ends the drag. Each gesture executes one action.

The dialog prevents saving a reserved or conflicting binding and explains why. For a conflict, it names the action that already uses the gesture and keeps that binding active.

## 3. Screenshots

The settings page lists seven configurable click actions.

| Action | Factory default | Behavior |
| --- | --- | --- |
| Copy to clipboard | Left-button double-click | Copy the current screenshot selection. |
| Save as file | None | Open the save dialog and save the current selection to the location the user chooses. |
| Quick save | None | Save the current selection using the quick save settings. |
| Pin to screen | Middle-button single-click | Create a pinned window from the current selection. |
| Text recognition | None | Recognize text in the current selection. |
| Copy after text recognition | None | Recognize text in the current selection and copy the result. |
| Cancel screenshot | None | Exit the screenshot session immediately. |

**Smart selection.** Actions such as copy, save, pin, and text recognition can use the smart selection preview under the pointer without a separate confirmation. This behavior follows the action when its binding changes. For example, if **Pin to screen** is bound to a single-click of side button 1, clicking that button over the preview pins the region directly. **Cancel screenshot** can exit the session without a confirmed selection.

**Active tools take priority.** Drawing, text editing, color picking, and region construction tools get the first opportunity to handle gestures that belong to the current interaction. For example, a double-click on a region being edited performs the active tool's double-click action. The screenshot action runs only if the tool leaves the gesture unhandled.

**Go back one step.** An unmodified right single-click uses a read-only binding in the same matcher as the configurable gestures. It first cancels an operation in progress, such as picking a color or constructing a region. At a stage that allows reselection, it returns to region selection. During smart selection preview, it exits the screenshot session.

## 4. Pin to screen

### 4.1 Wheel actions

| Action | Factory default | Change per wheel notch |
| --- | --- | --- |
| Zoom | Mouse wheel | Use the regular zoom step, which defaults to 10 percentage points. |
| Fine zoom | Alt+Mouse wheel | Use a zoom step of 1 percentage point. |
| Adjust opacity | Ctrl+Mouse wheel | Use the regular opacity step, which defaults to 5 percentage points. |
| Fine opacity adjustment | None | Use an opacity step of 1 percentage point. |

Each fine adjustment follows the corresponding regular action's scroll direction, limits, zoom anchor where applicable, handling of device scroll deltas, and availability rules. For example, one fine zoom step increases the scale from 150% to 151%, and one fine opacity step reduces opacity from 85% to 84%. Fine zoom applies to the entire selection wherever group zoom is available. Both opacity actions affect only the window under the pointer. Two-finger trackpad pinch gestures use the native zoom behavior.

### 4.2 Click actions

| Action | Factory default | Behavior |
| --- | --- | --- |
| Copy to clipboard | None | Perform a regular copy. Depending on the window state, copy the current view, selected text, or recognition content. |
| Copy to clipboard and close | None | Perform a regular copy. Once that copy succeeds, send one normal close request to the current window. |
| Copy text to clipboard | Shift+Right-button single-click | Read the available text from the current window or the selected windows. |
| Save as file | None | Open the save dialog for the current pinned window. |
| Reset zoom | Middle-button single-click | Reset the current window's zoom to 100%. |
| Thumbnail mode | Left-button double-click | Toggle thumbnail mode for the current window. |
| Hide at top | None | Toggle the current window's hide-at-top state. |
| Show text recognition results | None | Toggle the text recognition results view for the current window. |
| Close | None | Send a normal close request to the current window. |
| Destroy | None | Request permanent destruction of the current window, honoring the confirmation setting. |
| Menu | Right-button single-click | Open the single-window or multi-selection menu according to which window was clicked. |
| Annotation mode | None | Toggle annotation mode on a window that supports it. |
| Multi-select | Ctrl+Left-button single-click | Toggle the clicked window's selection state. |

**Copying text.** Clicking a member of a multi-selection targets the whole selection. Clicking an unselected window targets that window alone. Both use the rules for **Copy text to clipboard** in the multi-selection menu. Use plain text from text windows, completed translations from translation windows, and existing valid recognition results from other eligible windows. Skip windows without available text and join the remaining entries with line breaks in the existing multi-selection copy order. The action reads only content already available. If none of the targets has text, preserve the clipboard contents.

**Copying and closing.** Send the close request only after this copy has successfully written its content to the clipboard. Keep the window open if the copy fails, is canceled, or is superseded by a later copy. If **Exit annotation mode before closing** is enabled and the window is in annotation mode, this request exits annotation mode and leaves the window open. Show the standard close confirmation when required. **Copy to clipboard**, **Copy to clipboard and close**, **Close**, and **Destroy** affect only the clicked window. Batch close and destroy actions are available through the multi-selection menu.

**Handle at the top of the screen.** When a window is hidden at the top of the screen, its handle shares the window's menu binding and applicable click bindings. For example, if the menu is bound to a single-click of side button 2, clicking that button on the handle also opens the menu.

### 4.3 Drag actions

| Action | Factory default | Behavior |
| --- | --- | --- |
| Select crop region | Right-button drag | Drag a rectangle over content that supports cropping. Apply a valid crop region on release. |
| Pan within crop | Shift+Left-button drag | Pan the visible content within an existing crop to change which part of the source image is shown. |
| Move or resize window | Left-button drag | Drag a movable content area to move the window, or drag an edge or corner to resize it. When a crop is active, its border follows the existing crop-resize behavior. |
| Force window move | Alt+Left-button drag | Use the pinned window's existing Alt+left-button drag behavior, including moving the window by dragging over selectable text. Existing rules for borders, locked windows, and editing apply. |
| Drag image | Ctrl+Left-button drag | Drag the image out of a window that supports drag export. |

**Panning within a crop.** Panning is available when a valid crop exists and the window allows crop interaction. During the drag, the window's screen position, visible area size, and zoom level stay fixed. The image follows the pointer. Dragging right reveals more of the source image to the left, and dragging left reveals more to the right. Panning stops at each image edge to avoid exposing empty space. Once the user releases the button, copying, saving, and reopening the pinned window all use the new view.

**Multi-selection and export.** Starting **Move or resize window** on a movable area of a selected window moves the entire selection. **Drag image** exports only the image from the window where the drag started, even if that window belongs to a multi-selection. With the defaults, Ctrl+left single-click toggles selection, Ctrl+left-button drag exports the image, and an unmodified left-button drag moves the selected group. Each behavior follows its action when bindings change.

**Cropping and opening the menu.** With the defaults, a right single-click opens the menu and a right-button drag selects a crop region. Once a drag starts, a rectangle that is too small cancels the crop. If the menu is rebound, the original right-click gesture can trigger only the action currently assigned to it.

## 5. Migration on upgrade

Migrate **Screenshots** and **Pin to screen** independently. For each category that has no settings in the new format, read the effective legacy settings. Each old setting identifies the action for a gesture. Store that gesture as the binding for the selected action.

| Legacy setting | Binding to store for the selected action |
| --- | --- |
| Screenshots → Interaction → Double-click action | Unmodified left double-click in **Screenshots** |
| Screenshots → Interaction → Middle mouse button action | Unmodified middle single-click in **Screenshots** |
| Pin to screen → Interaction → Double-click action | Unmodified left double-click in **Pin to screen** |
| Pin to screen → Interaction → Middle mouse button action | Unmodified middle single-click in **Pin to screen** |

- If the two legacy gestures trigger different actions, preserve both assignments. They replace the corresponding factory assignments in that category so that each gesture belongs to only one action.
- If both legacy gestures trigger the same action in a category, keep the left double-click and release the middle single-click. The action has one binding after migration.
- Preserve an explicit **None** as an unbound gesture. For example, if the legacy screenshot double-click was **None** and the middle single-click was **Save as file**, the double-click stays unbound and the middle single-click saves the selection. **Copy to clipboard** and **Pin to screen** are both unbound in this case.
- Use factory defaults for new installations and for legacy settings that were never changed. For existing users, initialize the other mouse actions with the defaults in this document. Once the new settings have been saved, subsequent launches read them so that later user edits take precedence over legacy values.
- Resetting a category on the new page restores that category's factory bindings. The user's pre-upgrade assignments are used only for the initial migration.

## 6. Acceptance scenarios

| Scenario | Steps and expected result |
| --- | --- |
| Conflicting assignment | With the pinned window menu bound to a right single-click, assign an unmodified right double-click to **Close**. The dialog identifies **Menu** as the conflicting action and prevents saving. Changing the binding to Ctrl+right double-click allows saving if that combination is available. |
| Clearing and reserving gestures | Set several actions to **None** at the same time. The **Screenshots** section omits **Go back one step**. Attempting to assign an unmodified right single-click or double-click to any screenshot action prevents saving. |
| Rebinding a smart selection action | Bind **Pin to screen** in **Screenshots** to a single-click of side button 1. Click that button over a smart selection preview to pin it directly. The former middle-button binding is released. |
| Targets in a multi-selection | Select windows A and B. Invoking **Copy text to clipboard** on A collects the available text from A and B. Invoking it on unselected window C collects text only from C. Starting **Drag image** on A exports only A's image. |
| Closing after a successful copy | Enable **Exit annotation mode before closing** for a window in annotation mode. Invoke **Copy to clipboard and close**. A successful copy exits annotation mode and leaves the window open. A failed copy preserves the window's original state. |
| Crop panning | Select a crop substantially smaller than the source image and drag the cropped image to the right. The window frame stays fixed as more of the left side of the source comes into view. Copying, saving, and reopening the window all show the new view. |
| Wheel precision | Bind **Fine zoom** to Alt+wheel. Scroll up one notch at 150% to reach 151%. Bind **Fine opacity adjustment** and decrease opacity by one notch at 85% to reach 84%. |
| Platform defaults and the top handle | On macOS, Command+left single-click toggles selection and Option+wheel performs fine zoom by default. Bind **Menu** to a single-click of side button 2 and verify that both the window and its handle at the top of the screen respond. |
| Reset after migration | Before upgrading, set both screenshot double-click and middle single-click to **Quick save**. After upgrading, only the left double-click triggers quick save. Reset **Screenshots** to restore left double-click for copy and middle single-click for pin. The user's pinned window bindings are preserved. |
