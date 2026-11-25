# Recognition image export

## Behavior contract

The contract applies to Screenshot, Pinned, and Image Editing. Save as File, Quick Save, Print,
Upload to Cloud, and AirDrop share the same image source within each window.

- R1: The Recognition output section exposes Export recognition result as image. Its
  `text_recognition/save_recognition_result_as_image` key defaults to true, applies to open
  windows at the next export, and has a reset independent of OCR appearance and model settings.
- R2: Image output uses recognition pixels when the switch is on and text recognition or
  original-image translation is displayed on the image. The standard composite includes drawing
  annotations and is used for Show Original Image, open text editors, and other recognition modes.
- R3: Save captures the displayed result before modal event processing or asynchronous work.
  Partial translation is captured immediately. Pending or empty OCR saves the current image.
  The system Save dialog uses the same snapshot for the image and its capture history.
  Later edits, mode changes, and setting changes do not mutate an already prepared artifact.
- R4: The image source uses the same pixel dimensions across destinations. Screenshot standard
  composites use their configured HiDPI scale. Recognition snapshots omit selection, carets, toolbars,
  and drawing annotations.
  Follow-up pins use the image at native resolution.
  Clipboard, automatic saves after copying, capture history eligibility, and pin persistence retain
  their existing behavior.
- R5: Rendering and encoding stay on export workers. Snapshots contain Qt value types with
  copy-on-write ownership and no widget/session references. Preview and encoding share the artifact.
  Cancellation discards partial output; ordinary save errors remain visible without substituting
  the original image for a failed recognition render.
- R6: Available tables and documents save through their file exporters. Pending documents save as
  the standard image, and missing tables show the save error. Print, Upload, and AirDrop use the
  standard image in these modes.

## Implementation boundaries

`ScreenshotOcrTextLayout` owns shaping and painting, with explicit font/color inputs. The live
QGraphicsItem and recognition exporter use the same layout and quad fitting. Selection painting
is an explicit input and is empty for export. The snapshot retains the background patch actually
shown, including its canvas coordinates; saving never starts recognition or background filtering.

Merged horizontal paragraphs share source-row fitting between the canvas and export. Valid OCR
rows retain their indentation, gaps, and shorter final row with bounded spacing and uniform glyph
scaling. Overlapping rows or translations that cannot fit readably use ordinary paragraph layout.
The canvas keeps selection and grapheme-safe hit testing; export renders without selection.

The recognition session checks the shared setting and captures the attached view. Screenshot
supplies the capture selection, Pinned supplies its transformed base and crop geometry, and Image
Editing supplies the document base. The storage key stays stable so stored preferences continue to
apply.

## Focused verification

| Rules | Coverage |
| --- | --- |
| R1 | settings catalog order/default/binding; translation settings backend persistence and isolated reset |
| R2–R3 | recognition session visibility; offscreen recognition window snapshot excludes editor/QR pages and owns copied values |
| R2–R4 | offscreen pinned recognition save integration: enabled/disabled, OCR/translation/editor, Quick Save, both dialogs, dialog-time changes, zoom, rotated paragraph source regions |
| R3–R5 | recognition image tests: native pixels, unchanged source buffers, displayed background crop, effects, mixed Unicode, paragraph/vertical/perspective text, independent worker equality, cancellation and invalid input |
| R4–R5 | `mergedParagraphUsesSourceRows`: row geometry, Unicode hit testing, selection, zoom, cache invalidation and fallback; `sourceRowsSurviveImageExportOnWorkers`: row occupancy, gaps, shorter final row, worker equality and overlap fallback; existing export artifact, Quick Save, and save-dialog regressions |
| R2–R6 | Screenshot and Image Editing image output comparisons; pinned Print and AirDrop; document Quick Save during recognition; pinned missing-table and workbook saves |
| R2–R4 | Batch and individual pinned saves agree with both recognition output settings. System Save freezes image pixels and history while the dialog runs. Follow-up pins from Print, AirDrop, and Upload retain native resolution for selections and scrolling captures. |

The standalone renderer tests and the performance target disable unused default Qt plugins and
load Windows test fonts explicitly for deterministic offscreen shaping. Test providers use cached
results; no OCR model or translation network request is needed.

## Performance measurement

Build `snow-shot-recognition-image-performance-benchmark` with `windows-msvc-performance`
(Release), then run it with `-platform offscreen`. It reports 1080p and 4K fixtures with 20 and
500 lines, snapshot median time, render median/p95, and source-image bytes. Each fixture discards
two warmups and measures twenty runs. It asserts that snapshot copies retain the source image and
line-container backing buffers. Rasterization is measured separately from snapshot capture;
encoding and disk timing belong to the existing export pipeline.

Run only the named related tests; do not run the full repository suite. Benchmark measurements
and final verification results belong in the delivery report.
