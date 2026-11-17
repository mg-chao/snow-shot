#include "snow_shot/presentation/screenshotdisplaysession.h"
#include "snow_shot/presentation/screenshotocrpresentation.h"
#include "snow_shot/presentation/screenshotocrtexteditingsession.h"
#include "snow_shot/presentation/screenshotocrtexttransform.h"
#include "snow_shot/presentation/screenshottabledocument.h"
#include "snow_shot/presentation/screenshotsourceimagecomposer.h"

#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QCryptographicHash>
#include <QImage>
#include <QMimeData>
#include <QPointF>
#include <QTextCursor>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QThread>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <ole2.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

ScreenshotOcrLine line(const QString& text, qreal left, qreal top) {
    ScreenshotOcrLine output;
    output.text = text;
    output.quad = QPolygonF({
        QPointF(left, top),
        QPointF(left + static_cast<qreal>(text.size()) * 10.0, top),
        QPointF(left + static_cast<qreal>(text.size()) * 10.0, top + 10.0),
        QPointF(left, top + 10.0),
    });
    return output;
}

void textSelectionFollowsMouseCharacterRange() {
    ScreenshotOcrPresentation presentation;
    presentation.lines.push_back(line(QStringLiteral("AB"), 0.0, 0.0));
    presentation.lines.push_back(line(QStringLiteral("CD"), 0.0, 20.0));

    presentation.beginTextSelection(QPointF(4.0, 5.0));
    require(!presentation.hasTextSelection(), "mouse press should establish only a caret anchor");
    presentation.finishTextSelection();
    require(!presentation.hasTextSelection(),
            "a click without a drag should not select the whole OCR line");

    presentation.beginTextSelection(ScreenshotOcrTextPosition{0, 1});
    presentation.updateTextSelection(ScreenshotOcrTextPosition{0, 2});
    presentation.finishTextSelection();
    require(presentation.selectedText() == QStringLiteral("B"),
            "a drag within one line should select only the covered characters");
    const ScreenshotOcrTextRange firstLineRange = presentation.textSelectionForLine(0);
    require(firstLineRange.start == 1 && firstLineRange.length == 1,
            "the rendered selection range should retain exact character offsets");

    presentation.beginTextSelection(ScreenshotOcrTextPosition{0, 1});
    presentation.updateTextSelection(ScreenshotOcrTextPosition{1, 1});
    presentation.finishTextSelection();
    require(presentation.selectedText() == QStringLiteral("B\nC"),
            "a forward cross-line drag should preserve partial endpoint lines");

    presentation.beginTextSelection(ScreenshotOcrTextPosition{1, 1});
    presentation.updateTextSelection(ScreenshotOcrTextPosition{0, 1});
    presentation.finishTextSelection();
    require(presentation.selectedText() == QStringLiteral("B\nC"),
            "a reverse drag should preserve the same character range");
    require(presentation.lineSelected(0) && presentation.lineSelected(1),
            "partial endpoint lines should both render selection highlights");

    presentation.selectAll();
    require(presentation.selectedText() == QStringLiteral("AB\nCD"),
            "select all should preserve visual line breaks");
    presentation.clearTextSelection();
    require(!presentation.hasTextSelection(), "clearing should remove the text selection");
}

void lineSelectionPreservesSupplementaryCharacters() {
    ScreenshotOcrPresentation presentation;
    ScreenshotOcrLine output;
    output.text = QStringLiteral("A") + QString::fromUcs4(U"\U0001F642") + QStringLiteral("B");
    output.quad = QPolygonF({
        QPointF(0.0, 0.0),
        QPointF(30.0, 0.0),
        QPointF(30.0, 10.0),
        QPointF(0.0, 10.0),
    });
    presentation.lines.push_back(output);

    presentation.beginTextSelection(QPointF(9.0, 5.0));
    presentation.updateTextSelection(QPointF(21.0, 5.0));
    presentation.finishTextSelection();
    require(presentation.selectedText() == QString::fromUcs4(U"\U0001F642"),
            "mouse hit testing should select a supplementary character intact");
}

void verticalLineHitTestingFollowsTopToBottomGraphemes() {
    ScreenshotOcrPresentation presentation;
    ScreenshotOcrLine output;
    output.text = QStringLiteral("A") + QString::fromUcs4(U"\U0001F642") + QStringLiteral("B");
    output.direction = ScreenshotOcrTextDirection::Vertical;
    output.quad = QPolygonF({
        QPointF(10.0, 10.0),
        QPointF(30.0, 12.0),
        QPointF(26.0, 92.0),
        QPointF(6.0, 90.0),
    });
    presentation.lines.push_back(output);

    const ScreenshotOcrTextPosition start = presentation.textPositionAt(QPointF(19.8, 12.0));
    const ScreenshotOcrTextPosition afterFirst = presentation.textPositionAt(QPointF(18.5, 37.0));
    const ScreenshotOcrTextPosition afterEmoji = presentation.textPositionAt(QPointF(17.0, 65.0));
    const ScreenshotOcrTextPosition end = presentation.textPositionAt(QPointF(16.2, 90.0));
    require(start.lineIndex == 0 && start.characterIndex == 0,
            "vertical hit testing should start at the top of the OCR quad");
    require(afterFirst.characterIndex == 1,
            "vertical hit testing should advance along the quad's top-to-bottom axis");
    require(afterEmoji.characterIndex == 3,
            "vertical hit testing should preserve supplementary grapheme boundaries");
    require(end.characterIndex == output.text.size(),
            "vertical hit testing should end at the bottom of the OCR quad");

    presentation.beginTextSelection(afterFirst);
    presentation.updateTextSelection(afterEmoji);
    presentation.finishTextSelection();
    require(presentation.selectedText() == QString::fromUcs4(U"\U0001F642"),
            "vertical selection should copy the original grapheme without layout substitutions");
}

void preparedGeometryAndSelectionRevisionsAvoidRepeatedWork() {
    ScreenshotOcrPresentation presentation;
    presentation.lines.push_back(line(QStringLiteral("AB"), -80.0, -20.0));
    presentation.lines.push_back(line(QStringLiteral("CD"), 20.0, 20.0));
    presentation.prepareForRendering();

    require(presentation.lineAt(QPointF(-75.0, -15.0)) == 0 &&
                presentation.lineAt(QPointF(25.0, 25.0)) == 1 &&
                presentation.lineAt(QPointF(0.0, 0.0)) == -1,
            "prepared OCR geometry should preserve exact point-to-line hit testing");
    const ScreenshotOcrTextPosition secondLineMiddle =
        presentation.textPositionAt(QPointF(30.0, 25.0));
    require(secondLineMiddle.lineIndex == 1 && secondLineMiddle.characterIndex == 1,
            "prepared OCR geometry should preserve character hit testing");

    presentation.beginTextSelection(ScreenshotOcrTextPosition{1, 0});
    const quint64 anchorRevision = presentation.selectionRevision();
    require(presentation.textSelectionActive(),
            "beginning an OCR selection should expose its active drag state");
    presentation.updateTextSelection(ScreenshotOcrTextPosition{1, 0});
    require(presentation.selectionRevision() == anchorRevision,
            "repeating an OCR character position should not invalidate selection rendering");
    presentation.updateTextSelection(secondLineMiddle);
    require(presentation.selectionRevision() == anchorRevision + 1,
            "changing the OCR character range should invalidate selection rendering once");
    presentation.updateTextSelection(secondLineMiddle);
    require(presentation.selectionRevision() == anchorRevision + 1,
            "an unchanged OCR selection range should remain a rendering no-op");
    presentation.finishTextSelection();
    require(!presentation.textSelectionActive(),
            "finishing an OCR selection should end its active drag state");
}

CapturedDisplayModel display(const QRect& canvasRect, const QColor& color, bool active = true) {
    CapturedDisplayModel output;
    output.canvasRect = canvasRect;
    output.physicalRect = canvasRect;
    output.logicalRect = canvasRect;
    output.image = QImage(canvasRect.size(), QImage::Format_RGBA8888);
    output.image.fill(color);
    output.active = active;
    return output;
}

void incrementalTextUpdatesPreserveGeometryAndGraphemeSelection() {
    ScreenshotOcrPresentation presentation;
    presentation.lines = {line(QStringLiteral("original"), 0, 0),
                          line(QStringLiteral("unchanged"), 0, 20)};
    presentation.prepareForRendering();
    const QPolygonF quad = presentation.lines[0].quad;
    presentation.beginTextSelection(ScreenshotOcrTextPosition{1, 0});
    presentation.updateTextSelection(ScreenshotOcrTextPosition{1, 4});
    presentation.finishTextSelection();
    const QString unicode = QString::fromUcs4(U"A\U0001f642B");
    presentation.setLineText(0, unicode);
    require(presentation.selectedText() == QStringLiteral("unch") &&
                presentation.lines[0].quad == quad,
            "updating one box should preserve other selections and box geometry");
    require(presentation.textPositionAt(QPointF(55, 5)).characterIndex == 3,
            "text replacement must refresh cached grapheme hit testing");
    presentation.beginTextSelection(ScreenshotOcrTextPosition{0, 2});
    presentation.updateTextSelection(ScreenshotOcrTextPosition{0, 4});
    presentation.setLineText(0, QString::fromUcs4(U"A\U0001f642"));
    require(presentation.selectionAnchor().characterIndex == 1 &&
                presentation.selectionFocus().characterIndex == 3 &&
                presentation.selectedText() == QString::fromUcs4(U"\U0001f642"),
            "streamed replacement must clamp selection endpoints to complete graphemes");
    presentation.setLineText(0, QString());
    require(!presentation.hasTextSelection() && presentation.lineAt(QPointF(5, 5)) == 0,
            "empty replacement should clamp selection without changing spatial indexing");
}

void lineBoxSelectionPreviewsCurrentHitsAndCommitsFixedToggles() {
    ScreenshotOcrPresentation presentation;
    presentation.lines = {line(QStringLiteral("Alpha"), 0, 0), line(QStringLiteral("Beta"), 0, 20),
                          line(QStringLiteral("Gamma"), 0, 40)};
    presentation.beginTextSelection(ScreenshotOcrTextPosition{0, 1});
    presentation.updateTextSelection(ScreenshotOcrTextPosition{1, 4});
    presentation.finishTextSelection();
    const QString original = presentation.selectedText();
    presentation.beginLineBoxSelection(QPointF(-1, -1));
    require(presentation.lineBoxSelectionActive() && presentation.textSelectionActive(),
            "box selection exposes active ownership before any lines are hit");
    presentation.updateLineBoxSelection(QRectF(-1, -1, 52, 32));
    require(
        presentation.lineSelectionPreview(0) == ScreenshotOcrLineSelectionPreview::Select &&
            presentation.lineSelectionPreview(1) == ScreenshotOcrLineSelectionPreview::Deselect &&
            presentation.lineSelectionPreview(2) == ScreenshotOcrLineSelectionPreview::None &&
            presentation.selectedText() == original,
        "partial lines preview selecting and full lines preview deselecting without committing");
    const quint64 revision = presentation.selectionRevision();
    presentation.updateLineBoxSelection(QRectF(-1, -1, 52, 32));
    require(presentation.selectionRevision() == revision,
            "repeating a box rectangle does not invalidate selection rendering");
    presentation.updateLineBoxSelection(QRectF(-1, -1, 52, 12));
    require(presentation.lineSelectionPreview(1) == ScreenshotOcrLineSelectionPreview::None &&
                presentation.selectedText() == original,
            "a shrinking rectangle removes preview hits while preserving committed selection");
    presentation.cancelLineBoxSelection();
    require(!presentation.textSelectionActive() && presentation.lineBoxSelectionRect().isNull() &&
                presentation.selectedText() == original,
            "cancel removes the rectangle and preserves the previous character selection");
    presentation.beginLineBoxSelection(QPointF(-1, -1));
    presentation.updateLineBoxSelection(QRectF(-1, -1, 52, 32));
    presentation.finishLineBoxSelection();
    require(presentation.selectedText() == QStringLiteral("Alpha") &&
                !presentation.textSelectionActive() && !presentation.lineSelected(1),
            "release expands partial selection and removes full selection in the same operation");
    presentation.beginLineBoxSelection(QPointF(-1, 39));
    presentation.updateLineBoxSelection(QRectF(-1, 39, 52, 12));
    presentation.finishLineBoxSelection();
    require(presentation.selectedText() == QStringLiteral("Alpha\nGamma"),
            "sparse selection copies in reading order without inserting gaps for omitted lines");
    presentation.selectAll();
    require(presentation.selectedText() == QStringLiteral("Alpha\nBeta\nGamma"),
            "select all replaces sparse selection with all presentation lines");
    presentation.clearTextSelection();
    require(!presentation.hasTextSelection() && !presentation.textSelectionActive(),
            "clear removes committed sparse selection and gesture state");
}

void lineBoxSelectionRequiresPositivePolygonOverlap() {
    ScreenshotOcrPresentation presentation;
    auto diamond = line(QStringLiteral("Diamond"), 0, 0);
    diamond.quad = QPolygonF{QPointF(0, 10), QPointF(10, 0), QPointF(20, 10), QPointF(10, 20)};
    auto vertical = line(QStringLiteral("Vertical"), 40, 0);
    vertical.direction = ScreenshotOcrTextDirection::Vertical;
    vertical.quad = QPolygonF{QPointF(40, 0), QPointF(50, 0), QPointF(50, 80), QPointF(40, 80)};
    presentation.lines = {diamond, vertical};
    presentation.beginLineBoxSelection(QPointF(-5, -5));
    presentation.updateLineBoxSelection(QRectF(0, 0, 2, 2));
    require(presentation.lineSelectionPreview(0) == ScreenshotOcrLineSelectionPreview::None,
            "overlapping polygon bounds alone must not select a rotated OCR quad");
    presentation.updateLineBoxSelection(QRectF(-5, 5, 5, 10));
    require(presentation.lineSelectionPreview(0) == ScreenshotOcrLineSelectionPreview::None,
            "a rectangle touching only the quad boundary has no selection overlap");
    presentation.updateLineBoxSelection(QRectF(0, 9, 0.01, 2));
    require(presentation.lineSelectionPreview(0) == ScreenshotOcrLineSelectionPreview::Select,
            "any positive polygon overlap selects a whole line, including subpixel overlap");
    presentation.updateLineBoxSelection(QRectF(51, 81, -12, -82));
    require(presentation.lineBoxSelectionRect() == QRectF(39, -1, 12, 82) &&
                presentation.lineSelectionPreview(0) == ScreenshotOcrLineSelectionPreview::None &&
                presentation.lineSelectionPreview(1) == ScreenshotOcrLineSelectionPreview::Select,
            "reverse rectangles normalize and select complete vertical lines");
    presentation.finishLineBoxSelection();
    require(presentation.selectedText() == QStringLiteral("Vertical"),
            "only lines intersecting the final rectangle are committed");
}

void lineBoxSelectionContainmentCachesPreserveExactGeometry() {
    ScreenshotOcrPresentation presentation;
    auto reversed = line(QStringLiteral("Reversed"), 40, 0);
    std::reverse(reversed.quad.begin(), reversed.quad.end());
    auto diamond = line(QStringLiteral("Diamond"), 0, 0);
    diamond.quad = QPolygonF{QPointF(0, 10), QPointF(10, 0), QPointF(20, 10), QPointF(10, 20)};
    auto triangle = line(QStringLiteral("Triangle"), 80, 0);
    triangle.quad = QPolygonF{QPointF(80, 0), QPointF(100, 0), QPointF(90, 10), QPointF(90, 10)};
    auto collinear = line(QStringLiteral("Collinear"), 120, 0);
    collinear.quad =
        QPolygonF{QPointF(120, 0), QPointF(130, 10), QPointF(140, 20), QPointF(150, 30)};
    presentation.lines = {diamond, reversed, triangle, collinear};
    presentation.beginLineBoxSelection(QPointF(-1, -1));
    presentation.updateLineBoxSelection(QRectF(-1, -1, 181, 32));
    require(presentation.lineSelectionPreview(0) == ScreenshotOcrLineSelectionPreview::Select &&
                presentation.lineSelectionPreview(1) == ScreenshotOcrLineSelectionPreview::Select &&
                presentation.lineSelectionPreview(2) == ScreenshotOcrLineSelectionPreview::Select &&
                presentation.lineSelectionPreview(3) == ScreenshotOcrLineSelectionPreview::None,
            "whole-geometry containment keeps rotated and reversed shapes but excludes zero area");
    const quint64 previous = presentation.selectionRevision();
    presentation.updateLineBoxSelection(QRectF(-2, -2, 183, 34));
    const auto* changed = presentation.selectionChangesSince(previous);
    require(changed != nullptr && changed->isEmpty(),
            "equivalent full-containment queries retain every preview without dirty line work");
    presentation.updateLineBoxSelection(QRectF(80, 8, 1, 1));
    require(presentation.lineSelectionPreview(2) == ScreenshotOcrLineSelectionPreview::None,
            "repeated quad corners must not turn a triangular shape into a bounds-only hit");
    presentation.updateLineBoxSelection(QRectF(125, 4, 1, 1));
    require(presentation.lineSelectionPreview(3) == ScreenshotOcrLineSelectionPreview::None,
            "partial queries cannot select a zero-area quad");
    presentation.cancelLineBoxSelection();
}

void lineBoxSelectionRejectsCrossingQuadsAtEveryScale() {
    ScreenshotOcrPresentation presentation;
    auto bowtie = line(QStringLiteral("Bowtie"), 0, 0);
    bowtie.quad = QPolygonF{QPointF(0, 0), QPointF(10, 10), QPointF(0, 10), QPointF(10, 0)};
    auto unequalBowtie = line(QStringLiteral("Unequal bowtie"), 20, 0);
    unequalBowtie.quad =
        QPolygonF{QPointF(20, 0), QPointF(32, 10), QPointF(20, 10), QPointF(30, 0)};
    auto concave = line(QStringLiteral("Concave"), 40, 0);
    concave.quad = QPolygonF{QPointF(40, 0), QPointF(50, 0), QPointF(43, 3), QPointF(40, 10)};
    auto reversed = line(QStringLiteral("Reversed"), 80, 0);
    std::reverse(reversed.quad.begin(), reversed.quad.end());
    auto triangle = line(QStringLiteral("Triangle"), 140, 0);
    triangle.quad = QPolygonF{QPointF(140, 0), QPointF(160, 0), QPointF(150, 10), QPointF(150, 10)};
    presentation.lines = {bowtie, unequalBowtie, concave, reversed, triangle};
    presentation.beginLineBoxSelection(QPointF(-1, -1));
    require(presentation.lineAt(QPointF(1, 0.25)) == -1 &&
                presentation.lineAt(QPointF(21, 0.25)) == -1 &&
                presentation.lineAt(QPointF(41, 1)) == 2,
            "prepared point queries exclude crossing geometry while retaining concave interiors");
    presentation.updateLineBoxSelection(QRectF(0, 0, 1, 1));
    require(presentation.lineSelectionPreview(0) == ScreenshotOcrLineSelectionPreview::None,
            "a small rectangle cannot select one lobe of an invalid crossing quad");
    presentation.updateLineBoxSelection(QRectF(20, 0, 1, 1));
    require(presentation.lineSelectionPreview(1) == ScreenshotOcrLineSelectionPreview::None,
            "crossing quads remain invalid when their signed polygon area is nonzero");
    presentation.updateLineBoxSelection(QRectF(-1, -1, 162, 12));
    require(presentation.lineSelectionPreview(0) == ScreenshotOcrLineSelectionPreview::None &&
                presentation.lineSelectionPreview(1) == ScreenshotOcrLineSelectionPreview::None &&
                presentation.lineSelectionPreview(2) == ScreenshotOcrLineSelectionPreview::Select &&
                presentation.lineSelectionPreview(3) == ScreenshotOcrLineSelectionPreview::Select &&
                presentation.lineSelectionPreview(4) == ScreenshotOcrLineSelectionPreview::Select,
            "full containment rejects crossing edges while retaining concave shapes, reversed "
            "winding, and repeated-corner triangles");
    presentation.updateLineBoxSelection(QRectF(0, 0, 1, 1));
    presentation.finishLineBoxSelection();
    require(!presentation.hasTextSelection(),
            "expanding and shrinking around an invalid bowtie never commits a selection");
    presentation.beginLineBoxSelection(QPointF(-1, -1));
    presentation.updateLineBoxSelection(QRectF(-1, -1, 162, 12));
    presentation.finishLineBoxSelection();
    require(presentation.selectedText() == QStringLiteral("Concave\nReversed\nTriangle"),
            "crossing-geometry validation preserves positive-area simple polygons");
}

void closedQuadsPrepareAsFourCornerGeometry() {
    const QPolygonF closedQuad(QRectF(0, 0, 50, 10));
    const QVector<QPolygonF> closedRows{QPolygonF(QRectF(0, 0, 50, 4)),
                                        QPolygonF(QRectF(0, 6, 50, 4))};
    ScreenshotOcrPresentation presentation;
    auto closed = line(QStringLiteral("Closed paragraph"), 0, 0);
    closed.quad = closedQuad;
    closed.sourceLineQuads = closedRows;
    closed.paragraph = true;
    presentation.lines = {closed};
    require(presentation.lineAt(QPointF(25, 5)) == 0,
            "unprepared point queries accept Qt's closed rectangle polygon");
    presentation.prepareForRendering();
    require(presentation.lines[0].quad.size() == 4 &&
                presentation.lines[0].quad.boundingRect() == closedQuad.boundingRect() &&
                presentation.lines[0].sourceLineQuads[0].size() == 4 &&
                presentation.lines[0].sourceLineQuads[1].size() == 4 && closedQuad.size() == 5 &&
                closedRows[0].size() == 5 && closedRows[1].size() == 5 && closed.quad.size() == 5 &&
                closed.sourceLineQuads[0].size() == 5 && presentation.lineAt(QPointF(25, 5)) == 0,
            "preparation canonicalizes exact closures without mutating shared source geometry or "
            "changing point hits");
    ScreenshotOcrPresentation snapshot;
    snapshot.lines = presentation.lines;
    snapshot.prepareForRendering();
    require(snapshot.lines[0].quad.size() == 4 &&
                snapshot.lines[0].sourceLineQuads[0].size() == 4 &&
                snapshot.lines[0].sourceLineQuads[1].size() == 4 &&
                snapshot.lineAt(QPointF(25, 5)) == 0,
            "snapshots retain canonical main and source-row quads");
    presentation.beginLineBoxSelection(QPointF(25, 5));
    presentation.updateLineBoxSelection(QRectF(25, 5, 1, 1));
    require(presentation.lineSelectionPreview(0) == ScreenshotOcrLineSelectionPreview::Select,
            "a closed paragraph accepts positive overlap between its source rows");
    presentation.updateLineBoxSelection(QRectF(50, 0, 1, 10));
    require(presentation.lineSelectionPreview(0) == ScreenshotOcrLineSelectionPreview::None,
            "canonical closed quads still reject edge-only overlap");
    presentation.updateLineBoxSelection(QRectF(-1, -1, 52, 12));
    presentation.finishLineBoxSelection();
    require(presentation.selectedText() == QStringLiteral("Closed paragraph"),
            "whole containment commits all text from a canonical closed quad");
}

void lineBoxSelectionRevisionsDescribeOnlyChangedLines() {
    ScreenshotOcrPresentation presentation;
    presentation.lines = {line(QStringLiteral("Alpha"), 0, 0), line(QStringLiteral("Beta"), 0, 20)};
    presentation.beginLineBoxSelection(QPointF(-1, -1));
    const quint64 previous = presentation.selectionRevision();
    presentation.updateLineBoxSelection(QRectF(-1, -1, 52, 12));
    const auto* changed = presentation.selectionChangesSince(previous);
    require(changed != nullptr && *changed == QVector<int>{0} &&
                presentation.lineSelectionRevision(0) == presentation.selectionRevision() &&
                presentation.lineSelectionRevision(1) == 0,
            "a new preview hit invalidates only the affected OCR line");
    const quint64 firstHit = presentation.selectionRevision();
    presentation.updateLineBoxSelection(QRectF(-2, -2, 54, 14));
    changed = presentation.selectionChangesSince(firstHit);
    require(changed != nullptr && changed->isEmpty() &&
                presentation.lineSelectionRevision(0) == firstHit,
            "moving the rectangle without changing hits does not invalidate text items");
    require(presentation.selectionChangesSince(previous) == nullptr,
            "a reader skipping visual revisions must fall back to per-line revision checks");
    const quint64 beforeRelease = presentation.selectionRevision();
    presentation.finishLineBoxSelection();
    changed = presentation.selectionChangesSince(beforeRelease);
    require(changed != nullptr && *changed == QVector<int>{0} &&
                presentation.lineSelectionRevision(0) == presentation.selectionRevision(),
            "release invalidates the line whose preview becomes committed selection");
    presentation.setLineText(1, QStringLiteral("Updated"));
    require(presentation.lineSelectionRevision(1) == presentation.selectionRevision(),
            "streamed text replacement invalidates its line without rebuilding geometry");
}

void lineBoxSelectionPreservesStreamingSentinelsAndPressDirections() {
    ScreenshotOcrPresentation presentation;
    presentation.lines = {line(QStringLiteral("Alpha"), 0, 0), line(QStringLiteral("Beta"), 0, 20)};
    presentation.beginLineBoxSelection(QPointF(-1, -1));
    presentation.updateLineBoxSelection(QRectF(-1, -1, 52, 12));
    presentation.finishLineBoxSelection();
    presentation.setLineText(0, QStringLiteral("Longer translated content"));
    require(presentation.selectedText() == presentation.lines[0].text &&
                presentation.textSelectionForLine(0).length == presentation.lines[0].text.size(),
            "a fully selected box expands to the complete streamed replacement");
    presentation.setLineText(0, QString{});
    require(
        presentation.hasTextSelection() && presentation.selectedText().isEmpty(),
        "an empty intermediate stream retains full-line selection rather than copying all text");
    presentation.setLineText(0, QStringLiteral("Restored"));
    require(presentation.selectedText() == QStringLiteral("Restored"),
            "full-line selection survives an empty stream and selects later content");
    presentation.beginLineBoxSelection(QPointF(-1, -1));
    presentation.updateLineBoxSelection(QRectF(-1, -1, 52, 12));
    presentation.setLineText(0, QStringLiteral("Replacement during drag"));
    require(presentation.lineSelectionPreview(0) == ScreenshotOcrLineSelectionPreview::Deselect,
            "a fully selected line keeps its press-time deselection direction while streaming");
    presentation.finishLineBoxSelection();
    require(!presentation.hasTextSelection(), "release removes the streamed full-line selection");
    presentation.beginTextSelection(ScreenshotOcrTextPosition{1, 0});
    presentation.updateTextSelection(ScreenshotOcrTextPosition{1, 2});
    presentation.finishTextSelection();
    presentation.beginLineBoxSelection(QPointF(-1, 19));
    presentation.updateLineBoxSelection(QRectF(-1, 19, 42, 12));
    presentation.setLineText(1, QStringLiteral("Be"));
    require(presentation.lineSelectionPreview(1) == ScreenshotOcrLineSelectionPreview::Select,
            "a partial range becoming complete while streaming still keeps its select direction");
    presentation.finishLineBoxSelection();
    presentation.setLineText(1, QStringLiteral("Beta grows"));
    require(presentation.selectedText() == QStringLiteral("Beta grows"),
            "release promotes a partial line to a streaming full-line sentinel");
}

void sparsePartialSelectionClampsCombiningGraphemesIndependently() {
    ScreenshotOcrPresentation presentation;
    presentation.lines = {line(QStringLiteral("ABCD"), 0, 0), line(QStringLiteral("Gamma"), 0, 20)};
    presentation.beginTextSelection(ScreenshotOcrTextPosition{0, 2});
    presentation.updateTextSelection(ScreenshotOcrTextPosition{0, 3});
    presentation.finishTextSelection();
    presentation.beginLineBoxSelection(QPointF(-1, 19));
    presentation.updateLineBoxSelection(QRectF(-1, 19, 52, 12));
    presentation.finishLineBoxSelection();
    require(presentation.selectedText() == QStringLiteral("C\nGamma"),
            "box selection preserves an unrelated partial character range");
    presentation.setLineText(0, QStringLiteral("Ae\u0301B"));
    const ScreenshotOcrTextRange range = presentation.textSelectionForLine(0);
    require(range.start == 1 && range.length == 2 &&
                presentation.selectedText() == QStringLiteral("e\u0301\nGamma"),
            "sparse partial endpoints clamp to a complete combining grapheme during streaming");
    presentation.setLineText(0, QString{});
    require(presentation.hasTextSelection() && presentation.textSelectionForLine(0).empty() &&
                presentation.selectedText() == QStringLiteral("Gamma"),
            "empty replacement removes only the partial range and keeps other full-line selection");
    presentation.setLineText(1, QStringLiteral("Gamma grows"));
    require(
        presentation.selectedText() == QStringLiteral("Gamma grows"),
        "a full-line sentinel continues tracking text after a neighboring partial range vanishes");
}

void lineBoxSelectionPreservesLinearNewlineAndParagraphContracts() {
    ScreenshotOcrPresentation presentation;
    presentation.lines = {line(QStringLiteral("AB"), 0, 0), line(QString{}, 0, 20),
                          line(QStringLiteral("CD"), 0, 40)};
    presentation.beginTextSelection(ScreenshotOcrTextPosition{0, 2});
    presentation.updateTextSelection(ScreenshotOcrTextPosition{2, 0});
    presentation.finishTextSelection();
    require(presentation.hasTextSelection() &&
                presentation.selectedText() == QStringLiteral("\n\n"),
            "ordinary character selection preserves empty endpoints and selected line separators");
    presentation.beginLineBoxSelection(QPointF(100, 100));
    presentation.finishLineBoxSelection();
    require(presentation.selectedText() == QStringLiteral("\n\n"),
            "a blank click without box overlap preserves the linear newline selection");
    presentation.beginLineBoxSelection(QPointF(100, 100));
    presentation.updateLineBoxSelection(QRectF(100, 100, 10, 10));
    presentation.cancelLineBoxSelection();
    require(presentation.selectedText() == QStringLiteral("\n\n"),
            "canceling an empty box leaves the original linear clipboard contract intact");
    auto paragraph = line(QStringLiteral("Wrapped translated paragraph"), 0, 80);
    paragraph.quad =
        QPolygonF{QPointF(0, 80), QPointF(100, 80), QPointF(100, 130), QPointF(0, 130)};
    paragraph.paragraph = true;
    paragraph.sourceLineQuads = {line(QStringLiteral("Row"), 0, 80).quad,
                                 line(QStringLiteral("Row"), 0, 120).quad};
    presentation.lines.push_back(paragraph);
    presentation.beginLineBoxSelection(QPointF(50, 101));
    presentation.updateLineBoxSelection(QRectF(50, 101, 1, 1));
    presentation.finishLineBoxSelection();
    require(presentation.selectedText() == paragraph.text && presentation.lineSelected(3),
            "overlap between source rows selects the complete wrapped paragraph block");
}

void lineBoxSelectionHandlesBoundedIndexesAndInvalidGeometry() {
    ScreenshotOcrPresentation presentation;
    auto huge = line(QStringLiteral("Large"), -1000000, -1000000);
    huge.quad = QPolygonF{QPointF(-1000000, -1000000), QPointF(1000000, -1000000),
                          QPointF(1000000, 1000000), QPointF(-1000000, 1000000)};
    auto invalid = line(QStringLiteral("Invalid"), 0, 0);
    invalid.quad[0].setX(std::numeric_limits<qreal>::infinity());
    presentation.lines = {huge, line(QStringLiteral("Negative"), -100, -100), invalid};
    presentation.beginLineBoxSelection(QPointF(-101, -101));
    presentation.updateLineBoxSelection(QRectF(-101, -101, 2, 2));
    require(presentation.lineSelectionPreview(0) == ScreenshotOcrLineSelectionPreview::Select &&
                presentation.lineSelectionPreview(1) == ScreenshotOcrLineSelectionPreview::Select &&
                presentation.lineSelectionPreview(2) == ScreenshotOcrLineSelectionPreview::None,
            "oversized line fallback and negative spatial cells retain exact overlap results");
    presentation.updateLineBoxSelection(QRectF(-1e12, -1e12, 2e12, 2e12));
    require(
        presentation.lineSelectionPreview(0) == ScreenshotOcrLineSelectionPreview::Select &&
            presentation.lineSelectionPreview(1) == ScreenshotOcrLineSelectionPreview::Select &&
            presentation.lineSelectionPreview(2) == ScreenshotOcrLineSelectionPreview::None,
        "very large rectangles use bounded full-line fallback without visiting billions of cells");
    presentation.updateLineBoxSelection(QRectF(0, -1e12, 2e12, 2e12));
    require(presentation.lineSelectionPreview(0) == ScreenshotOcrLineSelectionPreview::Select &&
                presentation.lineSelectionPreview(1) == ScreenshotOcrLineSelectionPreview::None &&
                presentation.lineSelectionPreview(2) == ScreenshotOcrLineSelectionPreview::None,
            "a huge partial query uses bounded line fallback and excludes out-of-rectangle lines");
    presentation.updateLineBoxSelection(QRectF(0, 0, std::numeric_limits<qreal>::infinity(), 10));
    require(presentation.lineSelectionPreview(0) == ScreenshotOcrLineSelectionPreview::None &&
                presentation.lineSelectionPreview(1) == ScreenshotOcrLineSelectionPreview::None,
            "non-finite rectangles remove preview hits safely");
    presentation.finishLineBoxSelection();
    require(!presentation.hasTextSelection() &&
                presentation.lineAt(QPointF(std::numeric_limits<qreal>::infinity(), 0)) == -1,
            "invalid input must not create committed selection or unsafe spatial coordinates");
}

void committedSelectionTransferExcludesActivePreview() {
    ScreenshotOcrPresentation source;
    source.lines = {line(QStringLiteral("Alpha"), 0, 0),
                    line(QString::fromUcs4(U"A\U0001f642B"), 0, 20),
                    line(QStringLiteral("Gamma"), 0, 40)};
    source.beginTextSelection(ScreenshotOcrTextPosition{0, 1});
    source.updateTextSelection(ScreenshotOcrTextPosition{1, 3});
    source.finishTextSelection();
    source.beginLineBoxSelection(QPointF(-1, 39));
    source.updateLineBoxSelection(QRectF(-1, 39, 52, 12));
    source.finishLineBoxSelection();
    const QString selected = source.selectedText();
    source.beginLineBoxSelection(QPointF(-1, -1));
    source.updateLineBoxSelection(QRectF(-1, -1, 52, 12));
    ScreenshotOcrPresentation destination;
    destination.lines = source.lines;
    destination.copyCommittedSelectionFrom(source);
    require(
        destination.selectedText() == selected && !destination.textSelectionActive() &&
            destination.lineSelectionPreview(0) == ScreenshotOcrLineSelectionPreview::None,
        "presentation replacement preserves committed sparse ranges without transferring preview");
    destination.setLineText(1, QString::fromUcs4(U"A\U0001f642"));
    require(destination.textSelectionForLine(1).start == 0 &&
                destination.textSelectionForLine(1).length == 3,
            "transferred partial selections preserve complete supplementary graphemes");
    source.cancelLineBoxSelection();
    source.beginTextSelection(ScreenshotOcrTextPosition{0, 5});
    source.updateTextSelection(ScreenshotOcrTextPosition{1, 0});
    source.finishTextSelection();
    destination.copyCommittedSelectionFrom(source);
    require(destination.selectedText() == QStringLiteral("\n"),
            "selection transfer preserves the ordinary linear newline-only selection contract");
}

void sourceComposerUsesOnlyCapturedDisplayPixels() {
    ScreenshotDisplaySession displays;
    displays.appendDisplay(display(QRect(0, 0, 4, 2), Qt::red));
    displays.appendDisplay(display(QRect(4, 0, 4, 2), Qt::blue));
    displays.appendDisplay(display(QRect(0, 2, 4, 2), Qt::green, false));

    const QImage composed = composeScreenshotSourceSelection(displays, QRect(2, 0, 4, 2));
    require(composed.size() == QSize(4, 2), "composer should crop to the selection");
    require(composed.pixelColor(0, 0) == Qt::red, "left crop should come from display one");
    require(composed.pixelColor(1, 1) == Qt::red, "display one pixels should be retained");
    require(composed.pixelColor(2, 0) == Qt::blue, "right crop should come from display two");
    require(composed.pixelColor(3, 1) == Qt::blue, "display two pixels should be retained");

    const QImage inactive = composeScreenshotSourceSelection(displays, QRect(0, 2, 4, 2));
    require(inactive.isNull(), "a selection without active display pixels has no image source");
    const QImage mixed = composeScreenshotSourceSelection(displays, QRect(2, 0, 4, 4));
    require(mixed.size() == QSize(4, 4) && mixed.pixelColor(0, 2).alpha() == 0,
            "inactive displays must not contribute screenshot pixels");
}

void textTransformsUseOriginalPunctuationAndLineRules() {
    ScreenshotOcrPresentation presentation;
    presentation.lines.push_back(line(QStringLiteral("A,B!"), 0.0, 0.0));
    presentation.lines.push_back(line(QStringLiteral("C?"), 0.0, 20.0));
    const QString original = snow_shot::presentation::originalOcrText(presentation);
    require(original == QStringLiteral("A,B!\nC?"), "original OCR text should join lines");
    require(snow_shot::presentation::removeOcrLineBreaks(original) == QStringLiteral("A,B!C?"),
            "line-break removal should remove only line feeds");
    const QString full = snow_shot::presentation::convertOcrPunctuation(original, true);
    const QString expectedFull = QStringLiteral("A") + QChar(0xFF0C) + QStringLiteral("B") +
                                 QChar(0xFF01) + QChar('\n') + QStringLiteral("C") + QChar(0xFF1F);
    require(full == expectedFull, "full-width conversion should affect punctuation only");
    require(snow_shot::presentation::convertOcrPunctuation(full, false) == original,
            "half-width conversion should reverse the paired punctuation map");
}

void textEditingHistoryPreservesNativeEditsAndAtomicTransforms() {
    ScreenshotOcrTextEditingSession session(QStringLiteral("recognized text"));
    QTextDocument* const document = session.document();
    require(document != nullptr && session.text() == QStringLiteral("recognized text") &&
                !session.canUndo() && !session.canRedo(),
            "a new OCR editing session should start at a clean recognized baseline");

    QTextCursor cursor(document);
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(QStringLiteral(" edited"));
    require(session.document() == document &&
                session.text() == QStringLiteral("recognized text edited") && session.canUndo(),
            "native editor changes should update the persistent session document and history");

    session.undo();
    require(session.text() == QStringLiteral("recognized text") && session.canRedo(),
            "undo should reverse native editor changes and expose redo");
    session.redo();
    require(session.text() == QStringLiteral("recognized text edited"),
            "redo should restore native editor changes");

    require(session.replaceText(QStringLiteral("transformed text")) &&
                session.text() == QStringLiteral("transformed text"),
            "a toolbar transformation should replace the complete draft");
    session.undo();
    require(session.text() == QStringLiteral("recognized text edited"),
            "a complete-text transformation should be reversible in one undo step");
    session.redo();
    require(session.text() == QStringLiteral("transformed text"),
            "redo should restore an atomic complete-text transformation");
}

void textEditingHistoryInvalidatesRedoBranchesAndSurvivesReentry() {
    ScreenshotOcrTextEditingSession session(QStringLiteral("original"));
    QTextDocument* const firstDocument = session.document();
    require(session.replaceText(QStringLiteral("first")),
            "the first programmatic edit should be recorded");
    require(session.replaceText(QStringLiteral("second")),
            "the second programmatic edit should be recorded separately");
    session.undo();
    require(session.text() == QStringLiteral("first") && session.canRedo(),
            "undo should expose the replaced edit as a redo branch");

    require(session.replaceText(QStringLiteral("branched")) && !session.canRedo(),
            "a new edit after undo should discard the stale redo branch");
    require(session.document() == firstDocument,
            "showing the editor again should reuse the document that owns the history");
    require(!session.replaceText(QStringLiteral("branched")),
            "feeding the document's current text back into the session should not add history");

    require(session.reset() && session.text() == QStringLiteral("original"),
            "reset should restore the recognition baseline");
    session.undo();
    require(session.text() == QStringLiteral("branched"),
            "reset should be a single reversible history operation");
    session.undo();
    require(session.text() == QStringLiteral("first"),
            "history from before editor re-entry should remain available");
}

void textEditingHistoryIgnoresEditorLayoutFormatting() {
    ScreenshotOcrTextEditingSession session(QStringLiteral("recognized"));
    QTextCursor cursor(session.document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(QStringLiteral(" edit"));
    require(session.text() == QStringLiteral("recognized edit") && session.canUndo() &&
                !session.canRedo(),
            "the first visible editor modification should create one text-history entry");

    QTextBlockFormat layoutFormat;
    layoutFormat.setLineHeight(4.0, QTextBlockFormat::LineDistanceHeight);
    cursor.select(QTextCursor::Document);
    cursor.mergeBlockFormat(layoutFormat);
    require(session.canUndo() && !session.canRedo(),
            "editor-only block formatting must not change text-history availability");

    session.undo();
    require(session.text() == QStringLiteral("recognized") && !session.canUndo() &&
                session.canRedo(),
            "one undo should revert the first text edit and disable Undo");
    session.redo();
    require(session.text() == QStringLiteral("recognized edit") && session.canUndo() &&
                !session.canRedo(),
            "redo should restore the text edit and reverse command availability");
}

void textEditingTransformsAccumulateAndManualEditsClearTheirState() {
    ScreenshotOcrTextEditingSession session(QStringLiteral("A,\nB!"));

    require(session.setFormatting(QStringLiteral("remove")) &&
                session.text() == QStringLiteral("A,B!") &&
                session.formatting() == QStringLiteral("remove") && session.punctuation().isEmpty(),
            "line-break formatting should remain selected after it is applied");

    const QString fullWidth =
        QStringLiteral("A") + QChar(0xFF0C) + QStringLiteral("B") + QChar(0xFF01);
    require(session.setPunctuation(QStringLiteral("full")) && session.text() == fullWidth &&
                session.formatting() == QStringLiteral("remove") &&
                session.punctuation() == QStringLiteral("full"),
            "formatting and punctuation should accumulate independently");

    const QString fullWidthWithLineBreak =
        QStringLiteral("A") + QChar(0xFF0C) + QStringLiteral("\nB") + QChar(0xFF01);
    require(session.setFormatting(QStringLiteral("keep")) &&
                session.text() == fullWidthWithLineBreak &&
                session.formatting() == QStringLiteral("keep") &&
                session.punctuation() == QStringLiteral("full"),
            "changing line-break formatting should preserve the punctuation effect");

    require(session.setPunctuation(QString{}) && session.text() == QStringLiteral("A,\nB!") &&
                session.formatting() == QStringLiteral("keep") && session.punctuation().isEmpty(),
            "clearing punctuation should preserve the active formatting selection");
    static_cast<void>(session.setPunctuation(QStringLiteral("full")));

    ScreenshotOcrTextEditingSession reverseOrder(QStringLiteral("A,\nB!"));
    static_cast<void>(reverseOrder.setPunctuation(QStringLiteral("full")));
    require(reverseOrder.setFormatting(QStringLiteral("remove")) &&
                reverseOrder.text() == fullWidth &&
                reverseOrder.formatting() == QStringLiteral("remove") &&
                reverseOrder.punctuation() == QStringLiteral("full"),
            "punctuation and formatting should also accumulate in reverse selection order");
    require(reverseOrder.setFormatting(QString{}) &&
                reverseOrder.text() == fullWidthWithLineBreak &&
                reverseOrder.formatting().isEmpty() &&
                reverseOrder.punctuation() == QStringLiteral("full"),
            "clearing formatting should preserve the active punctuation selection");

    QTextCursor cursor(session.document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(QStringLiteral("?"));
    require(session.text() == fullWidthWithLineBreak + QStringLiteral("?") &&
                session.formatting().isEmpty() && session.punctuation().isEmpty(),
            "a native editor change should clear displayed transform selections");

    static_cast<void>(session.setFormatting(QStringLiteral("remove")));
    static_cast<void>(session.setPunctuation(QStringLiteral("half")));
    require(!session.formatting().isEmpty() && !session.punctuation().isEmpty(),
            "transform state should be populated before reset");
    session.reset();
    require(session.text() == QStringLiteral("A,\nB!") && session.formatting().isEmpty() &&
                session.punctuation().isEmpty(),
            "reset should restore the original text and clear transform selections");
}

void translationHistoryEstablishesSuccessfulAndPartialBaselines() {
    ScreenshotOcrTextEditingSession session(QString{});
    session.replaceTextWithoutHistory(QStringLiteral("streamed translation"));
    require(session.text() == QStringLiteral("streamed translation") && !session.canUndo() &&
                !session.canRedo(),
            "streaming updates should not create one undo entry per chunk");

    session.establishBaseline(QStringLiteral("streamed translation"));
    require(session.originalText() == QStringLiteral("streamed translation") &&
                !session.canUndo() && !session.canRedo(),
            "a completed translation should become the independent reset baseline");
    require(session.replaceText(QStringLiteral("edited translation")) && session.canUndo(),
            "a completed translation should become editable with its own history");
    require(session.reset() && session.text() == QStringLiteral("streamed translation"),
            "translation Reset should restore the exact successful result");

    session.replaceTextWithoutHistory(QStringLiteral("partial retry"));
    session.establishHistory(QStringLiteral("partial retry"));
    require(session.text() == QStringLiteral("partial retry") && !session.canUndo() &&
                session.originalText() == QStringLiteral("streamed translation"),
            "a failed partial stream should be editable without replacing the last successful "
            "baseline");
    require(session.replaceText(QStringLiteral("partial retry fixed")) && session.canUndo(),
            "a partial translation should gain independent edit history after streaming stops");
}

void streamingTextUpdatesPreserveUnchangedBlocksAndCursorAnchors() {
    const QString prefix =
        QStringLiteral("Stable first paragraph\n") + QString(8192, QLatin1Char('x'));
    ScreenshotOcrTextEditingSession session(prefix);
    QTextCursor anchor(session.document());
    anchor.setPosition(7);
    const QTextBlock first = session.document()->begin();
    const int firstRevision = first.revision();
    for (int chunk = 1; chunk <= 20; ++chunk)
        session.replaceTextWithoutHistory(prefix + QString(chunk, QLatin1Char('y')));
    require(session.text() == prefix + QString(20, QLatin1Char('y')) && !session.canUndo(),
            "streaming appends preserve the complete content without creating history");
    require(anchor.position() == 7 && first.revision() == firstRevision,
            "streaming appends preserve cursor anchors and untouched paragraph layout revisions");
    session.replaceTextWithoutHistory(prefix + QStringLiteral(" corrected"));
    require(anchor.position() == 7 && session.text() == prefix + QStringLiteral(" corrected"),
            "streaming corrections replace only the changed suffix");
    session.establishBaseline(session.text());
    QTextCursor cursor(session.document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(QStringLiteral(" edited"));
    session.undo();
    require(session.text() == prefix + QStringLiteral(" corrected"),
            "native edits remain independently undoable after incremental streaming");
}

void replacementTextMatchesQtNewlineAndUnicodeSemantics() {
    for (const bool preserve : {false, true}) {
        ScreenshotOcrTextEditingSession session(QStringLiteral("before"), preserve);
        const QStringList values{
            QStringLiteral("first\r\nsecond\rthird\nlast"), QStringLiteral("first\nsecond\n"),
            QStringLiteral("left\u00a0right\u2028line\u2029paragraph \U0001f600"),
            QStringLiteral("left\u00a0right\u2028line\u2029paragraph \U0001f601"), QString{}};
        for (const auto& text : values) {
            QTextDocument reference;
            reference.setPlainText(text);
            session.replaceTextWithoutHistory(text);
            require(session.document()->toRawText() == reference.toRawText() &&
                        session.text() == (preserve ? text : reference.toPlainText()),
                    "incremental replacements preserve Qt paragraphs and exact-source mode");
        }
    }
}

void tableDocumentPreservesSpansAndExportsCoveredCoordinates() {
    const ScreenshotTableDocument document = ScreenshotTableDocument::fromHtml(QStringLiteral(
        "<table><tr><th rowspan=\"2\">Group</th><th colspan=\"2\">Values</th></tr>"
        "<tr><th>A</th><th>B</th></tr><tr><td>Item</td><td>1</td><td>2</td></tr></table>"));
    require(document.rowCount() == 3 && document.columnCount() == 3,
            "table parser should preserve the recognized row and column dimensions");
    require(document.firstRowIsHeader() && document.cellText(1, 0) == QStringLiteral("Group"),
            "covered coordinates should resolve to their anchor cell");
    require(document.spanRangeAt(1, 0) == ScreenshotTableRange{0, 0, 1, 0} &&
                document.spanRangeAt(0, 2) == ScreenshotTableRange{0, 1, 0, 2},
            "rowspan and colspan geometry should be represented explicitly");
    require(document.toPlainText() == QStringLiteral("Group\tValues\t\n\tA\tB\nItem\t1\t2"),
            "TSV export should leave covered coordinates empty");

    const QString html = document.toHtml();
    require(html.contains(QStringLiteral("rowspan=\"2\"")) &&
                html.contains(QStringLiteral("colspan=\"2\"")),
            "HTML export should preserve multi-row and multi-column spans");
    const ScreenshotTableDocument roundTrip = ScreenshotTableDocument::fromHtml(html);
    require(roundTrip == document, "span-aware table HTML should round-trip without loss");
}

void tableDocumentExportsExcelTextCells() {
    const QString value = QStringLiteral("251231312312321321321312312321");
    const ScreenshotTableDocument document = ScreenshotTableDocument::fromHtml(
        QStringLiteral("<table><tr><th colspan=\"2\">") + value +
        QStringLiteral("</th></tr><tr><td>00123456789012345678</td>"
                       "<td>-12345678901234567890</td></tr>"
                       "<tr><td>1234567890123456.7890</td><td>42</td></tr>"
                       "<tr><td>&lt;value&gt; &amp; text<br/>next</td><td></td></tr></table>"));
    const QString original = document.toPlainText();
    const auto mime = document.toClipboardMimeData();
    require(mime != nullptr &&
                mime->text() ==
                    QLatin1Char('\'') + value +
                        QStringLiteral("\t\n'00123456789012345678\t'-12345678901234567890\n"
                                       "'1234567890123456.7890\t42\n<value> & text\nnext\t"),
            "large numeric clipboard values should carry literal text prefixes, including TSV");
    require(ScreenshotTableDocument::fromHtml(mime->html()).toPlainText() == mime->text(),
            "HTML and plain-text consumers should receive the same protected values");
    require(ScreenshotTableDocument::fromClipboardMimeData(*mime) == document &&
                document.toPlainText() == original,
            "internal copy/paste and the recognized document should preserve original values");

    const ScreenshotTableRange selection{1, 0, 2, 1};
    const auto selected = document.toClipboardMimeData(selection);
    require(selected != nullptr && selected->text().startsWith(QStringLiteral("'001234")) &&
                ScreenshotTableDocument::fromClipboardMimeData(*selected).toPlainText() ==
                    document.toPlainText(selection),
            "selected ranges should protect exported numbers and preserve internal values");

    const ScreenshotTableDocument boundaries = ScreenshotTableDocument::fromPlainText(
        QStringLiteral("99999999999\t100000000000\t+123456789012\t0.1234567890123456\n"
                       "'123456789012\titem123456789012\t=123456789012\t30"));
    const auto boundaryMime = boundaries.toClipboardMimeData();
    require(boundaryMime->text() ==
                QStringLiteral("99999999999\t'100000000000\t'+123456789012\t'0.1234567890123456\n"
                               "'123456789012\titem123456789012\t=123456789012\t30"),
            "only numeric cells with at least twelve digits should gain a prefix");
    require(ScreenshotTableDocument::fromClipboardMimeData(*boundaryMime) == boundaries,
            "existing literal apostrophes must not be stripped or duplicated internally");
    QMimeData external;
    external.setText(QStringLiteral("A\t30"));
    require(ScreenshotTableDocument::fromClipboardMimeData(external).toPlainText() ==
                external.text(),
            "ordinary external TSV paste should still work");
    external.setHtml(QStringLiteral("<table><tr><td>HTML</td></tr></table>"));
    require(ScreenshotTableDocument::fromClipboardMimeData(external).cellText(0, 0) ==
                QStringLiteral("HTML"),
            "external HTML should retain priority over plain text");
    require(!ScreenshotTableDocument().toClipboardMimeData() &&
                !document.toClipboardMimeData(ScreenshotTableRange{}),
            "empty documents and invalid selections should have no clipboard payload");
}

void tableClipboardHtmlHasDocumentContext() {
    const ScreenshotTableDocument document = ScreenshotTableDocument::fromHtml(QStringLiteral(
        "<table><tr><th colspan=\"2\">\u5e74\u4efd</th></tr>"
        "<tr><td>2024</td><td>00123456789012345678</td></tr>"
        "<tr><td>&lt;value&gt; &amp; text<br/>next</td><td>\U0001f642</td></tr></table>"));
    for (const ScreenshotTableRange range :
         {ScreenshotTableRange{0, 0, 2, 1}, ScreenshotTableRange{1, 0, 2, 1},
          ScreenshotTableRange{0, 1, 0, 1}}) {
        const auto mime = document.toClipboardMimeData(range);
        require(mime != nullptr, "a valid table range should produce clipboard data");
        const QString html = mime->html();
        const qsizetype htmlStart = html.indexOf(QStringLiteral("<html>"));
        const qsizetype bodyStart = html.indexOf(QStringLiteral("<body>"));
        const qsizetype fragmentStart = html.indexOf(QStringLiteral("<!--StartFragment-->"));
        const qsizetype tableStart = html.indexOf(QStringLiteral("<table"));
        const qsizetype tableEnd = html.indexOf(QStringLiteral("</table>"));
        const qsizetype fragmentEnd = html.indexOf(QStringLiteral("<!--EndFragment-->"));
        const qsizetype bodyEnd = html.indexOf(QStringLiteral("</body>"));
        const qsizetype htmlEnd = html.indexOf(QStringLiteral("</html>"));
        require(htmlStart >= 0 && bodyStart > htmlStart && fragmentStart > bodyStart &&
                    tableStart > fragmentStart && tableEnd > tableStart && fragmentEnd > tableEnd &&
                    bodyEnd > fragmentEnd && htmlEnd > bodyEnd,
                "spreadsheet clipboard HTML needs a complete document with the table fragment "
                "inside its body so clipboard headers cannot become worksheet rows");
        require(html.contains(QStringLiteral("charset=\"utf-8\"")),
                "the clipboard HTML document should declare its Unicode encoding");
        require(!html.contains(QStringLiteral("StartHTML:")) &&
                    !mime->text().contains(QStringLiteral("StartHTML:")),
                "native clipboard transport headers must not be part of HTML or TSV content");
        require(ScreenshotTableDocument::fromHtml(html).toPlainText() == mime->text(),
                "document context must preserve the table's Unicode text and protected numbers");
        require(ScreenshotTableDocument::fromClipboardMimeData(*mime).toHtml() ==
                    document.toHtml(range),
                "document context must preserve selected spans and original internal values");
    }
}

#if defined(Q_OS_WIN)
bool publishNativeClipboard(const QMimeData& source) {
    // Windows clipboard history can briefly lock the shared clipboard between writes.
    for (int attempt = 0; attempt < 40; ++attempt) {
        auto copy = std::make_unique<QMimeData>();
        for (const auto& format : source.formats())
            copy->setData(format, source.data(format));
        if (source.hasImage())
            copy->setImageData(source.imageData());
        if (source.hasUrls())
            copy->setUrls(source.urls());
        QApplication::clipboard()->setMimeData(copy.release());
        QApplication::processEvents();
        if (QApplication::clipboard()->ownsClipboard())
            return true;
        QThread::msleep(25);
    }
    return false;
}

QByteArray nativeTableClipboardHtml(std::unique_ptr<QMimeData> mime) {
    auto previous = std::make_unique<QMimeData>();
    if (const auto* source = QApplication::clipboard()->mimeData()) {
        for (const auto& format : source->formats())
            previous->setData(format, source->data(format));
        if (source->hasImage())
            previous->setImageData(source->imageData());
        if (source->hasUrls())
            previous->setUrls(source->urls());
    }
    const bool published = publishNativeClipboard(*mime);

    QByteArray bytes;
    HRESULT clipboardResult = E_FAIL;
    for (int attempt = 0; published && attempt < 12; ++attempt) {
        IDataObject* current = nullptr;
        clipboardResult = OleGetClipboard(&current);
        if (SUCCEEDED(clipboardResult) && current != nullptr) {
            FORMATETC format{static_cast<CLIPFORMAT>(RegisterClipboardFormatW(L"HTML Format")),
                             nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
            STGMEDIUM medium{};
            clipboardResult = current->GetData(&format, &medium);
            if (SUCCEEDED(clipboardResult)) {
                if (medium.tymed == TYMED_HGLOBAL) {
                    if (const auto* data = static_cast<const char*>(GlobalLock(medium.hGlobal))) {
                        bytes =
                            QByteArray(data, static_cast<qsizetype>(GlobalSize(medium.hGlobal)));
                        GlobalUnlock(medium.hGlobal);
                    }
                }
                ReleaseStgMedium(&medium);
            }
            current->Release();
        }
        if (clipboardResult != CLIPBRD_E_CANT_OPEN)
            break;
        QApplication::processEvents();
        QThread::msleep(25);
    }

    require(publishNativeClipboard(*previous), "restore the clipboard after native tests");
    HRESULT flushed = E_FAIL;
    for (int attempt = 0; attempt < 40; ++attempt) {
        flushed = OleFlushClipboard();
        if (flushed != CLIPBRD_E_CANT_OPEN)
            break;
        QApplication::processEvents();
        QThread::msleep(25);
    }
    if (FAILED(flushed))
        std::cerr << "clipboard flush HRESULT: " << std::hex << flushed << '\n';
    require(SUCCEEDED(flushed), "flush the restored clipboard after native tests");
    require(SUCCEEDED(clipboardResult), "read native table HTML from the Windows clipboard");
    return bytes;
}

void tableClipboardNativeHtmlOffsetsEncloseOnlyTheTable() {
    const ScreenshotTableDocument document = ScreenshotTableDocument::fromHtml(
        QStringLiteral("<table><tr><th colspan=\"2\">\u5e74\u4efd</th></tr>"
                       "<tr><td>2024</td><td>00123456789012345678</td></tr>"
                       "<tr><td>\U0001f642 &amp; text<br/>next</td><td>42</td></tr></table>"));
    for (const ScreenshotTableRange range :
         {ScreenshotTableRange{0, 0, 2, 1}, ScreenshotTableRange{1, 0, 2, 1},
          ScreenshotTableRange{0, 1, 0, 1}}) {
        const auto mime = document.toClipboardMimeData(range);
        const QByteArray expected = mime->html().toUtf8();
        const QString expectedText = mime->text();
        const QByteArray bytes = nativeTableClipboardHtml(document.toClipboardMimeData(range));
        const auto offset = [&bytes](const QByteArray& field) {
            const qsizetype start = bytes.indexOf(field);
            require(start >= 0, "native clipboard HTML must have all byte-offset fields");
            bool valid = false;
            const qlonglong value = bytes.mid(start + field.size(), 10).toLongLong(&valid);
            require(valid && value >= 0 && value <= bytes.size(),
                    "native clipboard HTML offsets must be valid byte positions");
            return static_cast<qsizetype>(value);
        };
        const qsizetype htmlStart = offset(QByteArrayLiteral("StartHTML:"));
        const qsizetype htmlEnd = offset(QByteArrayLiteral("EndHTML:"));
        const qsizetype fragmentStart = offset(QByteArrayLiteral("StartFragment:"));
        const qsizetype fragmentEnd = offset(QByteArrayLiteral("EndFragment:"));
        require(htmlStart < fragmentStart && fragmentStart < fragmentEnd && fragmentEnd < htmlEnd,
                "native HTML must enclose the table fragment in document context");
        const QByteArray html = bytes.mid(htmlStart, htmlEnd - htmlStart);
        require(html == expected && html.startsWith("<html>") && html.endsWith("</html>"),
                "native StartHTML and EndHTML must delimit a complete HTML document");
        const QByteArray fragment = bytes.mid(fragmentStart, fragmentEnd - fragmentStart);
        require(fragment.startsWith("<table") && fragment.endsWith("</table>") &&
                    !fragment.contains("Version:1.0") &&
                    ScreenshotTableDocument::fromHtml(QString::fromUtf8(fragment)).toPlainText() ==
                        expectedText,
                "native fragment offsets must delimit only table cells and preserve UTF-8 bytes");
        require(bytes.indexOf("Version:1.0") == bytes.lastIndexOf("Version:1.0"),
                "Qt must add the native transport header exactly once");
    }
}
#endif
void tableDocumentMergeSplitClearAndValidationPolicies() {
    ScreenshotTableDocument document =
        ScreenshotTableDocument::fromPlainText(QStringLiteral("A\tB\nC\tD"));
    require(document.canMerge(ScreenshotTableRange{0, 0, 1, 1}) &&
                document.merge(ScreenshotTableRange{0, 0, 1, 1}),
            "a rectangular multi-cell range should merge");
    const ScreenshotTableCell* merged = document.anchorCellAt(0, 0);
    require(merged != nullptr && merged->rowSpan == 2 && merged->columnSpan == 2 &&
                merged->text == QStringLiteral("A\nB\nC\nD"),
            "merge should join non-empty values in row-major order");
    require(!document.isAnchor(1, 1) && document.toPlainText().endsWith(QStringLiteral("\t")),
            "covered merged coordinates should remain empty in TSV output");
    require(document.canSplit(ScreenshotTableRange{1, 1, 1, 1}) &&
                document.split(ScreenshotTableRange{1, 1, 1, 1}),
            "selecting any covered coordinate should split its merged anchor");
    require(document.cellText(0, 0) == QStringLiteral("A\nB\nC\nD") &&
                document.cellText(0, 1).isEmpty() && document.cellText(1, 0).isEmpty() &&
                document.cellText(1, 1).isEmpty(),
            "split should retain merged text only in the top-left cell");
    require(document.setCellText(1, 1, QStringLiteral("<edited> & value")) &&
                document.toHtml().contains(QStringLiteral("&lt;edited&gt; &amp; value")),
            "cell edits should be safely escaped in HTML");
    require(document.clear(ScreenshotTableRange{0, 0, 1, 1}) &&
                document.toPlainText() == QStringLiteral("\t\n\t"),
            "clear should preserve fixed dimensions while emptying cell contents");
    require(!document.setCellText(-1, 0, QStringLiteral("invalid")) &&
                !document.merge(ScreenshotTableRange{0, 0, 0, 0}) &&
                !document.split(ScreenshotTableRange{0, 0, 0, 0}) &&
                !document.clear(ScreenshotTableRange{}),
            "invalid and inapplicable edits should be rejected without mutation");
}
void cachedTextAndTableCheckpointsFollowContentMutations() {
    const auto hash = [](const QString& text) {
        return QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256);
    };
    ScreenshotOcrTextEditingSession text(QStringLiteral("first\r\nsecond"), true);
    require(text.contentCheckpoint() == hash(text.text()),
            "initial source checkpoint matches text");
    text.replaceTextWithoutHistory(QStringLiteral("first\nsecond"));
    require(text.contentCheckpoint() == hash(text.text()),
            "source newline changes invalidate a checkpoint even with identical document pixels");
    text.establishHistory(text.text());
    for (int i = 0; i < 30; ++i) {
        QTextCursor cursor(text.document());
        cursor.movePosition(QTextCursor::End);
        cursor.insertText(QStringLiteral("x"));
        require(text.contentCheckpoint() == hash(text.text()),
                "native edits invalidate checkpoints");
    }
    for (int i = 0; i < 30; ++i)
        text.undo();
    require(text.text() == QStringLiteral("first\nsecond") && !text.canUndo(),
            "delta history returns to its exact baseline");
    for (int i = 0; i < 30; ++i)
        text.redo();
    require(text.contentCheckpoint() == hash(text.text()),
            "redo invalidates the content checkpoint");

    auto table = ScreenshotTableDocument::fromPlainText(QStringLiteral("A\tB\nC\tD"));
    const auto original = table;
    const auto saved = table.contentCheckpoint();
    require(saved == hash(table.toHtml()), "table checkpoint preserves canonical HTML identity");
    require(table.setCellText(0, 0, QStringLiteral("Changed")), "change checkpoint fixture cell");
    require(table.contentCheckpoint() != saved && original.contentCheckpoint() == saved,
            "copying and editing a table does not invalidate another document's identity");
    require(table.merge({0, 0, 1, 1}) && table.contentCheckpoint() == hash(table.toHtml()),
            "merge invalidates cached table identity");
    require(table.split({0, 0, 1, 1}) && table.contentCheckpoint() == hash(table.toHtml()),
            "split invalidates cached table identity");
    require(table.clear({0, 0, 1, 1}) && table.contentCheckpoint() == hash(table.toHtml()),
            "clear invalidates cached table identity");
    table = original;
    require(table.contentCheckpoint() == saved,
            "table history replacement restores its checkpoint");
}
} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
#if defined(Q_OS_WIN)
    if (application.arguments().contains(QStringLiteral("--native-table-clipboard"))) {
        tableClipboardNativeHtmlOffsetsEncloseOnlyTheTable();
        return 0;
    }
#endif
    textSelectionFollowsMouseCharacterRange();
    lineSelectionPreservesSupplementaryCharacters();
    verticalLineHitTestingFollowsTopToBottomGraphemes();
    preparedGeometryAndSelectionRevisionsAvoidRepeatedWork();
    incrementalTextUpdatesPreserveGeometryAndGraphemeSelection();
    lineBoxSelectionPreviewsCurrentHitsAndCommitsFixedToggles();
    lineBoxSelectionRequiresPositivePolygonOverlap();
    lineBoxSelectionContainmentCachesPreserveExactGeometry();
    lineBoxSelectionRejectsCrossingQuadsAtEveryScale();
    closedQuadsPrepareAsFourCornerGeometry();
    lineBoxSelectionRevisionsDescribeOnlyChangedLines();
    lineBoxSelectionPreservesStreamingSentinelsAndPressDirections();
    sparsePartialSelectionClampsCombiningGraphemesIndependently();
    lineBoxSelectionPreservesLinearNewlineAndParagraphContracts();
    lineBoxSelectionHandlesBoundedIndexesAndInvalidGeometry();
    committedSelectionTransferExcludesActivePreview();
    sourceComposerUsesOnlyCapturedDisplayPixels();
    textTransformsUseOriginalPunctuationAndLineRules();
    textEditingHistoryPreservesNativeEditsAndAtomicTransforms();
    textEditingTransformsAccumulateAndManualEditsClearTheirState();
    textEditingHistoryInvalidatesRedoBranchesAndSurvivesReentry();
    textEditingHistoryIgnoresEditorLayoutFormatting();
    translationHistoryEstablishesSuccessfulAndPartialBaselines();
    streamingTextUpdatesPreserveUnchangedBlocksAndCursorAnchors();
    replacementTextMatchesQtNewlineAndUnicodeSemantics();
    tableDocumentPreservesSpansAndExportsCoveredCoordinates();
    tableDocumentExportsExcelTextCells();
    tableClipboardHtmlHasDocumentContext();
    tableDocumentMergeSplitClearAndValidationPolicies();
    cachedTextAndTableCheckpointsFollowContentMutations();
    return 0;
}
