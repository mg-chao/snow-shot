#include "snow_canvas_text_editor_overlay.h"

#include "snow_canvas_text_layout.h"

#include <QBrush>
#include <QColor>
#include <QFontMetricsF>
#include <QPainter>
#include <QPen>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QVector>

namespace snow_canvas_text_editor_overlay {
namespace {

constexpr double kRadiansToDegrees = 180.0 / 3.14159265358979323846;
constexpr double kTextCursorWidth = 1.2;
namespace text_layout = snow_canvas_text_layout;

QColor toQColor(const SnowColorRgba8& color) {
    return QColor(color.r, color.g, color.b, color.a);
}

} // namespace

void renderSelection(QPainter& painter, const SnowSceneDisplayItem& item, const QString& text,
                     int selectionStart, int selectionEnd, const QFont& baseFont,
                     const QPointF& centerView, double zoom) {
    if (selectionStart == selectionEnd) {
        return;
    }

    text_layout::DocumentLayout layout =
        text_layout::createDocumentLayout(item, baseFont, zoom, text, true);
    const QVector<QRectF> documentRects =
        text_layout::rangeRectsInDocument(layout.textDocument(), selectionStart, selectionEnd);
    if (documentRects.isEmpty()) {
        return;
    }

    const QColor highlight(0x33, 0x99, 0xff, 0x78);

    painter.save();
    painter.translate(centerView);
    painter.rotate(item.rotation * kRadiansToDegrees);
    painter.setPen(Qt::NoPen);
    painter.setBrush(highlight);
    for (const QRectF& documentRect : documentRects) {
        painter.drawRect(text_layout::documentRectToLocalItemRect(documentRect, layout));
    }
    painter.restore();
}

void renderSelectedText(QPainter& painter, const SnowSceneDisplayItem& item, const QString& text,
                        int selectionStart, int selectionEnd, const QFont& baseFont,
                        const QPointF& centerView, double zoom) {
    if (selectionStart == selectionEnd) {
        return;
    }

    QColor selectedTextColor(Qt::white);
    selectedTextColor.setAlpha(item.text_color.a);
    if (selectedTextColor.alpha() == 0) {
        return;
    }

    text_layout::DocumentLayout layout =
        text_layout::createDocumentLayout(item, baseFont, zoom, text, true);
    QTextDocument& document = layout.textDocument();
    const QVector<QRectF> documentRects =
        text_layout::rangeRectsInDocument(document, selectionStart, selectionEnd);
    if (documentRects.isEmpty()) {
        return;
    }

    QTextCursor documentCursor(&document);
    documentCursor.select(QTextCursor::Document);
    QTextCharFormat hiddenFormat;
    hiddenFormat.setForeground(QBrush(Qt::transparent));
    documentCursor.mergeCharFormat(hiddenFormat);

    const int documentEnd = qMax(0, document.characterCount() - 1);
    const int boundedSelectionStart = qBound(0, qMin(selectionStart, selectionEnd), documentEnd);
    const int boundedSelectionEnd =
        qBound(boundedSelectionStart, qMax(selectionStart, selectionEnd), documentEnd);
    QTextCursor selectedCursor(&document);
    selectedCursor.setPosition(boundedSelectionStart);
    selectedCursor.setPosition(boundedSelectionEnd, QTextCursor::KeepAnchor);
    QTextCharFormat selectedFormat;
    selectedFormat.setForeground(QBrush(selectedTextColor));
    selectedCursor.mergeCharFormat(selectedFormat);

    painter.save();
    painter.translate(centerView);
    painter.rotate(item.rotation * kRadiansToDegrees);
    painter.translate(-layout.itemWidth / 2.0, -layout.itemHeight / 2.0 + layout.topOffset);
    painter.scale(layout.resolution.scale, layout.resolution.scale);
    painter.setOpacity(qBound(0.0, item.opacity, 1.0));
    for (const QRectF& documentRect : documentRects) {
        painter.save();
        painter.setClipRect(documentRect);
        document.drawContents(&painter, text_layout::documentContentsRect(layout));
        painter.restore();
    }
    painter.restore();
}

void renderCaret(QPainter& painter, const SnowSceneDisplayItem& item, const QString& text,
                 int cursorPosition, const QFont& baseFont, const QPointF& centerView,
                 double zoom) {
    if (item.font_size <= 0.0) {
        return;
    }

    text_layout::DocumentLayout layout =
        text_layout::createDocumentLayout(item, baseFont, zoom, text, true);
    if (layout.textDocument().documentLayout() == nullptr) {
        return;
    }

    QRectF cursorRect = text_layout::cursorRectInDocument(layout.textDocument(), cursorPosition);
    if (cursorRect.isEmpty()) {
        cursorRect.setSize(QSizeF(kTextCursorWidth / layout.resolution.scale,
                                  qMax(1.0, QFontMetricsF(layout.resolution.font).height())));
    }

    QColor caretColor = toQColor(item.text_color);
    if (caretColor.alpha() == 0) {
        caretColor = QColor(0x1e, 0x1e, 0x1e);
    }
    QPen pen(caretColor);
    pen.setWidthF(qMax(1.0, kTextCursorWidth * layout.safeZoom) / layout.resolution.scale);
    pen.setCapStyle(Qt::FlatCap);

    painter.save();
    painter.translate(centerView);
    painter.rotate(item.rotation * kRadiansToDegrees);
    painter.translate(-layout.itemWidth / 2.0, -layout.itemHeight / 2.0 + layout.topOffset);
    painter.scale(layout.resolution.scale, layout.resolution.scale);
    painter.setPen(pen);
    painter.drawLine(QPointF(cursorRect.left(), cursorRect.top()),
                     QPointF(cursorRect.left(), cursorRect.bottom()));
    painter.restore();
}

void renderPreeditUnderline(QPainter& painter, const SnowSceneDisplayItem& item,
                            const QString& text, int preeditStart, int preeditLength,
                            const QFont& baseFont, const QPointF& centerView, double zoom) {
    if (preeditLength <= 0 || item.font_size <= 0.0) {
        return;
    }

    text_layout::DocumentLayout layout =
        text_layout::createDocumentLayout(item, baseFont, zoom, text, true);
    const QVector<QRectF> documentRects = text_layout::rangeRectsInDocument(
        layout.textDocument(), preeditStart, preeditStart + preeditLength);
    if (documentRects.isEmpty()) {
        return;
    }

    QColor underlineColor = toQColor(item.text_color);
    if (underlineColor.alpha() == 0) {
        underlineColor = QColor(0x1e, 0x1e, 0x1e);
    }
    QPen pen(underlineColor);
    pen.setWidthF(qMax(1.0, layout.safeZoom) / layout.resolution.scale);
    pen.setStyle(Qt::DashLine);

    painter.save();
    painter.translate(centerView);
    painter.rotate(item.rotation * kRadiansToDegrees);
    painter.translate(-layout.itemWidth / 2.0, -layout.itemHeight / 2.0 + layout.topOffset);
    painter.scale(layout.resolution.scale, layout.resolution.scale);
    painter.setPen(pen);
    for (const QRectF& documentRect : documentRects) {
        const qreal y = documentRect.bottom() - 1.0;
        painter.drawLine(QLineF(documentRect.left(), y, documentRect.right(), y));
    }
    painter.restore();
}

} // namespace snow_canvas_text_editor_overlay
