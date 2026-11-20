#include "snow_shot/presentation/screenshotocrtextlayout.h"

#include <QGuiApplication>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
int failures = 0;
void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        ++failures;
    }
}

QPolygonF quad(const QRectF& rect) {
    return {rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()};
}

QImage paint(ScreenshotOcrTextLayout& layout, const QTransform& transform) {
    QImage image(1000, 220, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setTransform(transform);
    layout.paint(&painter);
    return image;
}

QVector<QRect> paintedRows(const QImage& image) {
    QVector<QRect> rows;
    for (int y = 0; y < image.height(); ++y) {
        QRect scan;
        for (int x = 0; x < image.width(); ++x)
            if (image.pixelColor(x, y).alpha() > 48)
                scan = scan.united(QRect(x, y, 1, 1));
        if (scan.isEmpty())
            continue;
        if (!rows.isEmpty() && y == rows.last().bottom() + 1)
            rows.back() = rows.back().united(scan);
        else
            rows.push_back(scan);
    }
    return rows;
}

void smartMergeReflowsMoreRowsAtTheAverageInkHeight() {
    ScreenshotOcrLine line;
    line.smartMergeTranslation = true;
    line.text = QStringLiteral("HHHHHHHH HHHHHHHH HHHHHHHH HHHHHHHH HHHHHHHH");
    const QRectF box(20, 20, 162, 158);
    line.quad = quad(box);
    line.sourceLineQuads = {quad(QRectF(83, 20, 97, 12)), quad(QRectF(20, 55, 84, 14)),
                            quad(QRectF(109, 55, 73, 14)), quad(QRectF(20, 90, 130, 20)),
                            quad(QRectF(20, 144, 115, 34))};
    const auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    ScreenshotOcrTextLayout layout;
    QTransform transform;
    qreal size = 0;
    require(
        configureScreenshotOcrTextLayout(layout, line, {}, font, Qt::black, {}, &transform, &size),
        "Smart Merge paragraph must fit");
    const auto preview = paint(layout, transform);
    const auto rows = paintedRows(preview);
    require(rows.size() == 5, "four source rows must allow five full-width translated rows");
    if (rows.size() != 5)
        return;
    for (qsizetype i = 0; i < rows.size(); ++i) {
        require(std::abs(rows[i].height() - 20) <= 1 && rows[i].width() > 100 &&
                    std::abs(rows[i].left() - box.left()) <= 1,
                "every row, including the former indent, uses the full width at the ink cap");
        if (i > 0)
            require(std::abs(rows[i].top() - rows[i - 1].top() - (box.height() - 20) / 4) < 2 &&
                        rows[i].top() - rows[i - 1].bottom() > size * 0.2,
                    "extra rows tighten the source advance just enough to fit at the target size");
    }
    require(std::abs((rows.first().top() + rows.last().bottom() + 1) / 2.0 - box.center().y()) < 1,
            "the paragraph is vertically centered");
    const auto fragments = layout.rangeFragments({0, static_cast<int>(line.text.size())});
    require(fragments.size() == rows.size(), "selection exposes each reflowed row");
    if (!fragments.isEmpty()) {
        const auto& last = fragments.last();
        require(layout.cursorPositionAt(last.rect.center()) >= last.range.start,
                "clicking visible ink in the last row finds its text");
    }
    line.translationStreaming = true;
    require(configureScreenshotOcrTextLayout(layout, line, {}, font, Qt::black, {}, &transform) &&
                paint(layout, transform) == preview,
            "equal Smart Merge preview and final text paint identically");
    QTransform output;
    output.translate(210, 10).scale(1.5, 1.5).rotate(24);
    QTransform rotated;
    require(configureScreenshotOcrTextLayout(layout, line, output, font, Qt::black, {}, &rotated) &&
                rotated == transform * output,
            "zoom and rotation must compose with the fitted paragraph");
}

void smartMergeKeepsAlignmentAndBlankRows() {
    const auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    const QRectF box(20, 20, 220, 165);
    ScreenshotOcrLine line;
    line.smartMergeTranslation = true;
    line.quad = quad(box);
    ScreenshotOcrTextLayout layout;
    QTransform transform;
    for (const auto alignment : {Qt::AlignLeft, Qt::AlignHCenter, Qt::AlignRight}) {
        line.sourceLineQuads = {quad(QRectF(20, 20, 220, 18))};
        for (const auto& [width, top] : {std::pair(100, 70), std::pair(140, 167)}) {
            const qreal spare = box.width() - width;
            const qreal offset = alignment == Qt::AlignLeft      ? 0
                                 : alignment == Qt::AlignHCenter ? spare / 2
                                                                 : spare;
            line.sourceLineQuads.push_back(quad(QRectF(box.left() + offset, top, width, 18)));
        }
        line.text = QStringLiteral("HHHH\r\n\t\t\nHH");
        const auto configure = [&] {
            return configureScreenshotOcrTextLayout(layout, line, {}, font, Qt::black, {},
                                                    &transform);
        };
        require(configure(), "explicit and blank rows must configure");
        const auto rows = paintedRows(paint(layout, transform));
        require(rows.size() == 2, "blank rows take space without painting glyphs");
        if (rows.size() != 2)
            continue;
        for (const auto& row : rows) {
            const qreal error = alignment == Qt::AlignLeft ? row.left() - box.left()
                                : alignment == Qt::AlignRight
                                    ? row.right() + 1 - box.right()
                                    : (row.left() + row.right() + 1) / 2.0 - box.center().x();
            require(std::abs(error) < 2, "each visible row preserves the source alignment");
        }
        const auto fragments = layout.rangeFragments({0, static_cast<int>(line.text.size())});
        require(fragments.size() == 2 && fragments.last().range.start == 9,
                "CRLF and the blank row preserve logical selection offsets");
        line.text = QStringLiteral("HHHH\nHH");
        require(configure(), "consecutive visible rows must configure");
        const auto withoutBlank = paintedRows(paint(layout, transform));
        require(withoutBlank.size() == 2 &&
                    rows.last().top() - rows.first().top() >
                        withoutBlank.last().top() - withoutBlank.first().top() + 10,
                "a blank row reserves a full font line height");
        line.text.append(u'\u2029');
        require(configure(), "trailing empty row must configure");
        const auto trailingBlank = paintedRows(paint(layout, transform));
        require(trailingBlank.size() == 2 && trailingBlank == withoutBlank &&
                    transform.mapRect(layout.boundingRect()).bottom() >
                        trailingBlank.last().bottom() + 10,
                "a trailing blank row reserves space after the anchored visible rows");
    }
}

void smartMergeMatchesPaintedSourceTypeAcrossScripts() {
    auto font = QGuiApplication::font();
    font.setPixelSize(22);
    const QString original = QStringLiteral("Image compression and layout Agyp");
    const qreal width = QFontMetricsF(font).horizontalAdvance(original);
    QVector<ScreenshotOcrLine> source;
    ScreenshotOcrLine paragraph;
    paragraph.quad = quad(QRectF(20, 18, width, 116));
    paragraph.paragraph = true;
    ScreenshotOcrTextLayout layout;
    QTransform transform;
    qreal sourceSize = 0;
    QVector<QRect> reference;
    for (const int top : {18, 53, 102}) {
        ScreenshotOcrLine row;
        row.quad = quad(QRectF(20, top, width, 32));
        row.text = original;
        require(configureScreenshotOcrTextLayout(layout, row, {}, font, Qt::black, {}, &transform,
                                                 &sourceSize),
                "render the OCR reference row");
        reference += paintedRows(paint(layout, transform));
        paragraph.sourceLineQuads.push_back(row.quad);
        source.push_back(row);
    }
    require(reference.size() == 3 && reference.front().height() < 27,
            "width-limited source ink must be visibly smaller than its detector boxes");
    if (reference.size() != 3)
        return;

    QVector<ScreenshotOcrLine> overlay{paragraph};
    prepareScreenshotOcrTranslationLayout(overlay, source, {{0, 1, 2}}, font, true);
    auto& line = overlay.front();
    const QString mixed = QStringLiteral("图像压缩 8K");
    line.text = QStringList{mixed, mixed, mixed}.join(u'\n');
    qreal size = 0;
    const auto render = [&] {
        require(configureScreenshotOcrTextLayout(layout, line, {}, font, Qt::black, {}, &transform,
                                                 &size),
                "render the source-matched translation");
        return paintedRows(paint(layout, transform));
    };
    const auto rows = render();
    require(rows.size() == reference.size() && size <= sourceSize + 0.01,
            "fitting translated rows retain the OCR font cap");
    if (rows.size() != reference.size())
        return;
    for (int i = 0; i < rows.size(); ++i) {
        require(std::abs(rows[i].center().y() - reference[i].center().y()) <= 1 &&
                    std::abs(rows[i].height() - reference[i].height()) <= 2,
                "mixed-script ink matches the painted source height and irregular row positions");
    }
    const qreal fittingSize = size;
    line.text = mixed;
    const auto shortRow = render();
    require(shortRow.size() == 1 && std::abs(shortRow.first().top() - rows.first().top()) <= 1 &&
                std::abs(size - fittingSize) < 0.01,
            "a shorter translation starts on the same row and leaves empty space below");
    line.text = (mixed + u'\n').repeated(7) + mixed;
    const auto overflow = render();
    require(overflow.size() == 8 && size < fittingSize && overflow.front().top() >= 17 &&
                overflow.back().bottom() <= 134,
            "overflowing translations keep every line inside the box at a smaller size");
}

void smartMergeFitsInkAtDifferentHeights() {
    ScreenshotOcrLine line;
    line.smartMergeTranslation = true;
    line.quad = quad(QRectF(20, 20, 140, 90));
    line.sourceLineQuads = {quad(QRectF(20, 20, 140, 36)), quad(QRectF(20, 74, 140, 36))};
    line.text = QStringLiteral("HHHH\n----\nHHHH\n----\nHHHH");
    const auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    ScreenshotOcrTextLayout layout;
    QTransform transform;
    qreal size = 0;
    require(
        configureScreenshotOcrTextLayout(layout, line, {}, font, Qt::black, {}, &transform, &size),
        "a tall translation must shrink to fit while keeping all of its rows");
    const auto rows = paintedRows(paint(layout, transform));
    require(rows.size() == 5, "font fitting preserves each explicitly broken row");
    if (rows.size() != 5)
        return;
    require(rows.first().height() < 36 && rows[1].height() < rows.first().height() / 2 &&
                std::abs(rows.first().top() - 20) <= 1 &&
                std::abs(rows.last().bottom() + 1 - 110) <= 1,
            "the paragraph uses the largest size that fits its actual mixed ink heights");
    for (qsizetype i = 1; i < rows.size(); ++i)
        require(std::abs(rows[i].top() - rows[i - 1].bottom() - 1 - size * 0.2) < 2,
                "small glyphs and tall glyphs have the same visible line gap");
}

void smartMergeBalancesVerticalColumnsAndWhitespace() {
    ScreenshotOcrLine line;
    line.smartMergeTranslation = true;
    line.direction = ScreenshotOcrTextDirection::Vertical;
    line.text = QStringLiteral("HHHHH \r\n\n\t HHHHH");
    line.quad = quad(QRectF(20, 20, 160, 138));
    line.sourceLineQuads = {quad(QRectF(20, 20, 13, 100)), quad(QRectF(70, 20, 15, 108)),
                            quad(QRectF(148, 20, 32, 138))};
    const auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    ScreenshotOcrTextLayout layout;
    QTransform transform;
    const auto configure = [&] {
        return configureScreenshotOcrTextLayout(layout, line, {}, font, Qt::black, {}, &transform);
    };
    require(configure(), "vertical Smart Merge lays out the whole paragraph");
    const auto columns = layout.rangeFragments({0, static_cast<int>(line.text.size())});
    require(columns.size() == 3, "vertical Smart Merge uses the minimum three columns");
    if (columns.size() != 3)
        return;
    const auto rectFor = [](const ScreenshotTextAnnotationFragment& cell) {
        return cell.localToSource.mapRect(cell.rect);
    };
    const auto first = rectFor(columns.first());
    const auto second = rectFor(columns[1]);
    const auto third = rectFor(columns.last());
    require(first.left() > second.left() && second.left() > third.left() &&
                first.top() == second.top() && third.top() == first.top() &&
                first.height() == second.height() &&
                std::abs(3 * second.height() - 4 * third.height()) < 0.001,
            "eleven cells are balanced 4, 4, 3 from the right edge with equal top alignment");
    const QRectF paintedBox = transform.mapRect(first.united(second).united(third));
    require(std::abs(transform.mapRect(first).center().x() - 164) < 0.001 &&
                std::abs(transform.mapRect(second).center().x() - 77.5) < 0.001 &&
                std::abs(transform.mapRect(third).left() - 20) < 0.001 &&
                std::abs(paintedBox.top() - 20) < 0.001 &&
                std::abs(transform.mapRect(first).width() - 20) < 0.001 &&
                paintedBox.bottom() <= 158.001,
            "columns retain source centers, clamping the leftmost ink inside the paragraph");
    require(columns[1].range.start == 4 && columns[1].range.end == 13 &&
                layout.rangeFragments({5, 6}).size() == 1 &&
                layout.cursorPositionAt(second.topLeft() + QPointF(second.width() / 2, 0)) == 4,
            "vertical cells preserve offsets and identify the clicked column");
    const auto withWhitespace = paint(layout, transform);
    line.text = QStringLiteral("HHHHH HHHHH");
    require(configure() && paint(layout, transform) == withWhitespace,
            "vertical whitespace collapses to one ordinary blank cell");
    line.text = QStringLiteral("HH");
    require(configure(), "a short vertical translation must configure");
    const auto shortColumn = layout.rangeFragments({0, 2});
    require(shortColumn.size() == 1 &&
                std::abs(transform.mapRect(rectFor(shortColumn.first())).center().x() - 164) <
                    0.001,
            "a short vertical translation begins in the rightmost source column");
}

void smartMergeUsesPaintedVerticalSourceWidth() {
    ScreenshotOcrLine original;
    original.direction = ScreenshotOcrTextDirection::Vertical;
    original.text = QStringLiteral("HHHHHHHH");
    original.quad = quad(QRectF(20, 16, 44, 155));
    const auto font = QGuiApplication::font();
    ScreenshotOcrTextLayout layout;
    QTransform transform;
    qreal size = 0;
    const auto inkWidth = [&](const ScreenshotOcrLine& line) {
        require(configureScreenshotOcrTextLayout(layout, line, {}, font, Qt::black, {}, &transform,
                                                 &size),
                "render a vertical source or translated column");
        QRect ink;
        for (const auto& row : paintedRows(paint(layout, transform)))
            ink = ink.united(row);
        return ink.width();
    };
    const int sourceWidth = inkWidth(original);
    const qreal sourceSize = size;
    QVector<ScreenshotOcrLine> overlay{original};
    prepareScreenshotOcrTranslationLayout(overlay, {original}, {{0}}, font, true);
    overlay[0].text = QStringLiteral("图");
    const int translatedWidth = inkWidth(overlay[0]);
    require(sourceWidth > 0 && sourceWidth < 30 && translatedWidth > 0 &&
                translatedWidth <= sourceWidth + 2 && size <= sourceSize + 0.01,
            "a short upright translation respects the painted width of rotated source glyphs");
}

void sourceTextFillsWideQuadsAtFullHeight() {
    ScreenshotOcrLine line;
    line.text = QStringLiteral("Readable text: Agyp");
    line.quad = quad(QRectF(20, 30, 800, 30));
    QFont font = QGuiApplication::font();
    font.setLetterSpacing(QFont::AbsoluteSpacing, 12);
    font.setWordSpacing(20);
    ScreenshotOcrTextLayout layout;
    QTransform transform;
    qreal size = 0;
    require(
        configureScreenshotOcrTextLayout(layout, line, {}, font, Qt::black, {}, &transform, &size),
        "source text must configure");
    const QRectF fitted = transform.mapRect(layout.boundingRect());
    require(std::abs(fitted.height() - 30) < 0.001 && std::abs(fitted.left() - 20) < 0.001,
            "source text must use the full estimated ink height and align to the left edge");
    require(std::abs(fitted.right() - 820) < 1,
            "source text must reach the end of a wide detection quad");
    const qreal spacingEm = layout.sourceTextMetrics().spacingEm;
    line.quad = quad(QRectF(20, 30, 900, 30));
    qreal widerSize = 0;
    require(configureScreenshotOcrTextLayout(layout, line, {}, font, Qt::black, {}, &transform,
                                             &widerSize),
            "wider source text must configure");
    const QRectF wider = transform.mapRect(layout.boundingRect());
    require(std::abs(size - widerSize) < 0.001 && std::abs(wider.left() - fitted.left()) < 0.001 &&
                std::abs(wider.right() - 920) < 1 &&
                layout.sourceTextMetrics().spacingEm > spacingEm,
            "increasing the target width must add spacing while preserving the size and anchor");
    line.quad = quad(QRectF(20, 30, 60, 30));
    qreal narrowerSize = 0;
    require(configureScreenshotOcrTextLayout(layout, line, {}, font, Qt::black, {}, &transform,
                                             &narrowerSize),
            "narrow source text must configure");
    require(narrowerSize < size && transform.mapRect(layout.boundingRect()).width() <= 60.001,
            "source text must shrink uniformly only when its natural width overflows");
    const QPointF start = transform.map(QPointF());
    const QPointF x = transform.map(QPointF(1, 0)) - start;
    const QPointF y = transform.map(QPointF(0, 1)) - start;
    require(std::abs(std::hypot(x.x(), x.y()) - std::hypot(y.x(), y.y())) < 0.001,
            "width-constrained glyphs must retain their aspect ratio");

    line.quad = quad(QRectF(20, 30, 1.0e12, 30));
    require(configureScreenshotOcrTextLayout(layout, line, {}, font, Qt::black, {}, &transform) &&
                layout.sourceTextMetrics().spacingEm == 0,
            "a width beyond Qt's text coordinate range must retain natural spacing");
}

void sourceTextFillsModeratelyWiderQuads() {
    const QFont font = QGuiApplication::font();
    const auto check = [&](const QString& text, qreal extraWidthEm) {
        ScreenshotOcrLine line;
        line.text = text;
        ScreenshotOcrTextLayout layout;
        QTransform transform;
        line.quad = quad(QRectF(20, 30, 800, 30));
        qreal fullSize = 0;
        require(configureScreenshotOcrTextLayout(layout, line, {}, font, Qt::black, {}, &transform,
                                                 &fullSize),
                "source text must configure at full height");
        line.quad = quad(QRectF(20, 30, 30, 30));
        qreal constrainedSize = 0;
        require(configureScreenshotOcrTextLayout(layout, line, {}, font, Qt::black, {}, &transform,
                                                 &constrainedSize) &&
                    constrainedSize > 0 && constrainedSize < fullSize,
                "a narrow quad must uniformly scale the text with its natural spacing");
        if (constrainedSize <= 0)
            return;
        const qreal naturalWidth = 30 * fullSize / constrainedSize;
        const qreal targetWidth = naturalWidth + extraWidthEm * fullSize;
        line.quad = quad(QRectF(20, 30, targetWidth, 30));
        qreal fittedSize = 0;
        require(configureScreenshotOcrTextLayout(layout, line, {}, font, Qt::black, {}, &transform,
                                                 &fittedSize),
                "source text must configure in a moderately wider quad");
        const QRectF fitted = transform.mapRect(layout.boundingRect());
        require(std::abs(fitted.left() - 20) < 0.001 && std::abs(fittedSize - fullSize) < 0.001,
                "added spacing must preserve the left anchor and font size");
        require(fitted.width() <= targetWidth + 0.001 &&
                    fitted.width() >= targetWidth - 0.03 * fullSize,
                "added spacing must approach the requested width without overflowing");
    };
    check(QStringLiteral("Agyp Agyp"), 0.125);
    check(QStringLiteral("\u6bcf\u884c14\u7ef4\u51e0\u4f55\u4fe1\u606f"), 0.75);
}

void sourceRowsKeepTheirBoxesAndSelection() {
    ScreenshotOcrLine line;
    line.text = QStringLiteral("Agyp Agyp Agyp Agyp Agyp Agyp Agyp Agyp");
    line.paragraph = true;
    line.quad = quad(QRectF(20, 20, 824, 80));
    line.sourceLineQuads = {quad(QRectF(20, 20, 800, 30)), quad(QRectF(44, 70, 800, 30))};
    ScreenshotOcrTextLayout layout;
    QTransform transform;
    const QFont font = QGuiApplication::font();
    require(configureScreenshotOcrTextLayout(layout, line, {}, font, Qt::black, {}, &transform) &&
                layout.usesSourceRows(),
            "short source rows must retain their original slots with added spacing");
    const auto fragments = layout.rangeFragments({0, static_cast<int>(line.text.size())});
    require(fragments.size() == 2, "selected source text must retain both visual rows");
    const auto baseline = paint(layout, transform);
    for (const auto& quad : line.sourceLineQuads) {
        const QRect bounds = quad.boundingRect().toRect();
        const auto hasInk = [&](int left, int right) {
            for (int x = left; x < right; ++x) {
                for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
                    if (qAlpha(baseline.pixel(x, y)) > 32)
                        return true;
                }
            }
            return false;
        };
        require(hasInk(bounds.left(), bounds.left() + 3) &&
                    hasInk(bounds.right() - 2, bounds.right() + 1),
                "each paragraph row must paint near both ends of its original slot");
    }
    layout.setSelection({0, static_cast<int>(line.text.size())});
    layout.setSelection({});
    require(paint(layout, transform) == baseline, "selection must not alter source glyph layout");
}

void verticalSourceTextDoesNotStretchItsSpacing() {
    ScreenshotOcrLine line;
    line.text = QStringLiteral("\u7530\u7530");
    line.direction = ScreenshotOcrTextDirection::Vertical;
    line.quad = quad(QRectF(20, 20, 24, 120));
    ScreenshotOcrTextLayout layout;
    QTransform transform;
    const QFont font = QGuiApplication::font();
    qreal size = 0;
    require(
        configureScreenshotOcrTextLayout(layout, line, {}, font, Qt::black, {}, &transform, &size),
        "vertical source text must configure");
    const auto original = paint(layout, transform);
    const auto originalSize = size;
    line.quad = quad(QRectF(20, 20, 24, 150));
    require(configureScreenshotOcrTextLayout(layout, line, {}, font, Qt::black, {}, &transform,
                                             &size) &&
                std::abs(size - originalSize) < 0.001 && paint(layout, transform) == original,
            "unused column height must not stretch vertical glyph spacing or change font size");
}

void translationRetainsSourceSizeAndBaseline() {
    ScreenshotOcrLine source;
    source.text = QStringLiteral("Readable text with descenders Agyp");
    source.quad = quad(QRectF(20, 30, 800, 30));
    QVector<ScreenshotOcrLine> translation{source};
    translation[0].text = QStringLiteral("Agyp");
    const QFont font = QGuiApplication::font();
    prepareScreenshotOcrTranslationLayout(translation, {source}, {{0}}, font);
    ScreenshotOcrTextLayout layout;
    QTransform transform;
    qreal size = 0;
    require(configureScreenshotOcrTextLayout(layout, translation[0], {}, font, Qt::black, {},
                                             &transform, &size),
            "short translation must configure");
    const auto shortSize = size;
    translation[0].text = source.text;
    require(configureScreenshotOcrTextLayout(layout, translation[0], {}, font, Qt::black, {},
                                             &transform, &size) &&
                std::abs(size - shortSize) < 0.001,
            "short and fitting translations must preserve the source font cap");
    require(layout.sourceTextMetrics().spacingEm == 0,
            "translation must retain natural spacing after source fitting changes");
}

void longSingleLineTranslationWrapsAtEveryZoom() {
    ScreenshotOcrLine source;
    source.text = QStringLiteral("OCR source");
    source.quad = quad(QRectF(20, 30, 150, 32));
    const QFont font = QGuiApplication::font();
    QVector<ScreenshotOcrLine> translation{source};
    prepareScreenshotOcrTranslationLayout(translation, {source}, {{0}}, font);
    auto& line = translation.front();
    line.text = QStringLiteral("Long translation ").repeated(6).trimmed();
    const auto length = static_cast<int>(line.text.size());
    ScreenshotOcrTextLayout layout;
    for (const bool streaming : {false, true}) {
        line.translationStreaming = streaming;
        for (const qreal zoom : {0.5, 1.0, 2.0}) {
            QTransform transform;
            qreal size = 0;
            const auto output = QTransform::fromScale(zoom, zoom);
            require(configureScreenshotOcrTextLayout(layout, line, output, font, Qt::black, {},
                                                     &transform, &size),
                    "long translation must configure at every zoom");
            const auto rows = layout.rangeFragments({0, length});
            require(rows.size() >= 2 && rows.first().baseline < rows.last().baseline &&
                        size > 6 * zoom,
                    "a translation requiring tiny single-line text must wrap at a larger size");
            if (!rows.isEmpty()) {
                const auto last = rows.last();
                const auto start = QPointF(last.rect.left(), last.rect.center().y());
                require(layout.cursorPositionAt(last.localToSource.map(start)) >= last.range.start,
                        "clicking a wrapped row must locate its text");
            }
        }
    }
}

void narrowTranslationKeepsLineBreaksAtEveryZoom() {
    ScreenshotOcrLine source;
    source.text = QStringLiteral("Short label");
    source.quad = quad(QRectF(20, 30, 73, 9));
    const QFont font = QGuiApplication::font();
    QVector<ScreenshotOcrLine> translation{source};
    prepareScreenshotOcrTranslationLayout(translation, {source}, {{0}}, font);
    auto& line = translation.front();
    line.text = QStringLiteral("The same text flows after a transform");
    const auto length = static_cast<int>(line.text.size());
    ScreenshotOcrTextLayout layout;
    QVector<QPair<int, int>> originalRanges;
    qreal originalSize = 0;
    for (const qreal zoom : {1.0, 0.5, 1.25, 2.0}) {
        QTransform transform;
        qreal size = 0;
        require(configureScreenshotOcrTextLayout(layout, line, QTransform::fromScale(zoom, zoom),
                                                 font, Qt::black, {}, &transform, &size),
                "a narrow translation must configure at every zoom");
        QVector<QPair<int, int>> ranges;
        for (const auto& fragment : layout.rangeFragments({0, length}))
            ranges.push_back({fragment.range.start, fragment.range.end});
        if (zoom == 1.0) {
            originalRanges = ranges;
            originalSize = size;
        }
        require(!originalRanges.isEmpty() && ranges == originalRanges &&
                    std::abs(size / zoom - originalSize) < 0.001,
                "zoom must preserve the break positions and source-space font size");
    }
}

void translationKeepsHardBreaksAndCompactsOverflowingRows() {
    ScreenshotOcrLine line;
    line.text = QStringLiteral("First row\r\nSecond row\nThird row");
    const auto length = static_cast<int>(line.text.size());
    const QFont font = QGuiApplication::font();
    ScreenshotOcrTextLayout layout;
    QTransform transform;
    const auto checkRows = [&] {
        const auto rows = layout.rangeFragments({0, length});
        require(rows.size() == 3 && rows[0].range.start == 0 && rows[1].range.start == 11 &&
                    rows[2].range.start == 22 && rows[0].baseline < rows[1].baseline &&
                    rows[1].baseline < rows[2].baseline && rows[2].range.end == length,
                "explicit CRLF and LF must create rows and preserve selection offsets");
    };
    line.quad = quad(QRectF(20, 20, 300, 70));
    line.fontSizeCapRatio = 16.0 / 70;
    require(configureScreenshotOcrTextLayout(layout, line, {}, font, Qt::black, {}, &transform),
            "a single source box with explicit translation breaks must configure");
    checkRows();

    line.paragraph = true;
    line.quad = quad(QRectF(20, 20, 300, 120));
    line.sourceLineQuads = {quad(QRectF(20, 20, 300, 24)), quad(QRectF(20, 116, 300, 24))};
    line.fontSizeCapRatio = 16.0 / 120;
    qreal size = 0;
    require(
        configureScreenshotOcrTextLayout(layout, line, {}, font, Qt::black, {}, &transform, &size),
        "a merged translation with widely spaced source rows must configure");
    checkRows();
    require(std::abs(size - 16) < 0.01,
            "additional translated rows should tighten spacing before shrinking the font");
}

int coloredPixels(const QImage& image, const QColor& color, const QRect& area) {
    int count = 0;
    const QRect bounds = area.intersected(image.rect());
    for (int y = bounds.top(); y <= bounds.bottom(); ++y)
        for (int x = bounds.left(); x <= bounds.right(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (pixel.alpha() >= 180 && std::abs(pixel.red() - color.red()) <= 3 &&
                std::abs(pixel.green() - color.green()) <= 3 &&
                std::abs(pixel.blue() - color.blue()) <= 3)
                ++count;
        }
    return count;
}

void sourceColorsPaintWithoutChangingSelectionOrShaping() {
    ScreenshotOcrLine line;
    line.text = QStringLiteral("Colored readable text");
    line.quad = quad(QRectF(20, 20, 700, 30));
    line.backgroundFillColor = Qt::white;
    line.sourceTextColor = QColor(20, 40, 190);
    ScreenshotOcrTextLayout layout;
    QTransform transform;
    const QFont font = QGuiApplication::font();
    require(configureScreenshotOcrTextLayout(layout, line, {}, font, Qt::black, {}, &transform),
            "colored source text must configure");
    auto image = paint(layout, transform);
    require(coloredPixels(image, line.sourceTextColor, image.rect()) > 30,
            "estimated ink must override the black/white contrast fallback");
    const auto bounds = layout.boundingRect();
    line.sourceTextColor = QColor(190, 30, 25);
    require(configureScreenshotOcrTextLayout(layout, line, {}, font, Qt::black, {}, &transform),
            "a color-only update must configure");
    image = paint(layout, transform);
    require(coloredPixels(image, line.sourceTextColor, image.rect()) > 30 &&
                layout.boundingRect() == bounds,
            "cached shaping must accept a new color without moving glyphs");
    layout.setSelection({0, static_cast<int>(line.text.size())});
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setTransform(transform);
        layout.paint(&painter, Qt::yellow, Qt::green);
    }
    require(coloredPixels(image, Qt::green, image.rect()) > 30 &&
                coloredPixels(image, line.sourceTextColor, image.rect()) == 0,
            "selection foreground must still override source ink");
    layout.setSelection({});
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setTransform(transform);
        layout.paint(&painter, Qt::yellow, Qt::green,
                     ScreenshotOcrTextLayout::PaintMode::SelectionOnly);
    }
    require(coloredPixels(image, line.sourceTextColor, image.rect()) == 0,
            "selection-only overlays must never reveal estimated text");
    line.sourceTextColor = QColor();
    line.backgroundFillColor = Qt::black;
    require(configureScreenshotOcrTextLayout(layout, line, {}, font, Qt::black, {}, &transform),
            "uncertain source text must configure");
    image = paint(layout, transform);
    require(coloredPixels(image, Qt::white, image.rect()) > 30,
            "uncertain text must retain the existing dark-background fallback");
}

void sourceRowsKeepTheirColorsAfterSorting() {
    ScreenshotOcrLine line;
    line.text = QStringLiteral("Colored text Colored text Colored text Colored text");
    line.paragraph = true;
    line.quad = quad(QRectF(20, 20, 800, 80));
    line.sourceLineQuads = {quad(QRectF(20, 70, 800, 30)), quad(QRectF(20, 20, 800, 30))};
    line.sourceTextColor = Qt::blue;
    line.sourceLineTextColors = {Qt::red, Qt::blue};
    ScreenshotOcrTextLayout layout;
    QTransform transform;
    const QFont font = QGuiApplication::font();
    require(configureScreenshotOcrTextLayout(layout, line, {}, font, Qt::black, {}, &transform) &&
                layout.usesSourceRows(),
            "colored source rows must configure");
    auto image = paint(layout, transform);
    require(coloredPixels(image, Qt::blue, QRect(0, 0, 1000, 60)) > 30 &&
                coloredPixels(image, Qt::red, QRect(0, 60, 1000, 120)) > 30,
            "normalizing row order must preserve color/geometry associations");
    line.sourceLineTextColors = {Qt::green, Qt::red};
    require(configureScreenshotOcrTextLayout(layout, line, {}, font, Qt::black, {}, &transform),
            "source-row colors must update through the shaping cache");
    image = paint(layout, transform);
    require(coloredPixels(image, Qt::red, QRect(0, 0, 1000, 60)) > 30 &&
                coloredPixels(image, Qt::green, QRect(0, 60, 1000, 120)) > 30,
            "cached source rows must paint updated colors");
    line.fontSizeCapRatio = 0.8;
    require(configureScreenshotOcrTextLayout(layout, line, {}, font, Qt::black, {}, &transform),
            "translated paragraphs must configure with source color");
    image = paint(layout, transform);
    require(coloredPixels(image, Qt::blue, image.rect()) > 30 &&
                coloredPixels(image, Qt::red, image.rect()) == 0 &&
                coloredPixels(image, Qt::green, image.rect()) == 0,
            "reflowed translations must use the representative color, not stale row mappings");
}
} // namespace

int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    sourceTextFillsWideQuadsAtFullHeight();
    sourceTextFillsModeratelyWiderQuads();
    sourceRowsKeepTheirBoxesAndSelection();
    verticalSourceTextDoesNotStretchItsSpacing();
    translationRetainsSourceSizeAndBaseline();
    longSingleLineTranslationWrapsAtEveryZoom();
    narrowTranslationKeepsLineBreaksAtEveryZoom();
    translationKeepsHardBreaksAndCompactsOverflowingRows();
    smartMergeReflowsMoreRowsAtTheAverageInkHeight();
    smartMergeKeepsAlignmentAndBlankRows();
    smartMergeMatchesPaintedSourceTypeAcrossScripts();
    smartMergeFitsInkAtDifferentHeights();
    smartMergeBalancesVerticalColumnsAndWhitespace();
    smartMergeUsesPaintedVerticalSourceWidth();
    sourceColorsPaintWithoutChangingSelectionOrShaping();
    sourceRowsKeepTheirColorsAfterSorting();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
