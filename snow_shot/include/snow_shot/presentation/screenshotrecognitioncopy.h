// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Result uses the host's export policy. Preview copies the displayed content or selection.
// SourceText copies the complete original OCR result, including its configured transforms.
// TableSelection copies the selected cells from the table grid in place.
enum class ScreenshotRecognitionCopy { Result, Preview, SourceText, TableSelection };
