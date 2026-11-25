#include "snow_shot/presentation/screenshotocrlayout.h"
#include "../src/presentation/ocr/screenshotocrlinegeometry.h"

#include <QCoreApplication>
#include <QStringList>
#include <QLineF>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}
ScreenshotOcrLine box(const QString& text, qreal x, qreal y, qreal width, qreal height = 20) {
    return {text,
            0.95,
            {QPointF(x, y), QPointF(x + width, y), QPointF(x + width, y + height),
             QPointF(x, y + height)}};
}
void expect(const QVector<ScreenshotOcrLine>& input, const QStringList& expected,
            const char* name) {
    const auto result = snow_shot::presentation::mergeOcrLayout(input);
    QStringList actual;
    for (const auto& line : result)
        actual.push_back(line.text);
    if (actual != expected) {
        std::cerr << name << ": " << actual.join(QStringLiteral(" | ")).toStdString() << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void smallLabelGeometry() {
    using snow_shot::ocr::prepareDetectedLineGeometry;
    // Source-pixel quads returned for seven desktop labels, including one wrapped filename.
    const QSize size(1278, 636);
    const QRectF pixels(QPointF(), QSizeF{size});
    const QVector<ScreenshotOcrLine> raw{
        {QStringLiteral("layout-imag"), 0.9, {{81, 174}, {196, 176}, {196, 203}, {81, 201}}},
        box(QStringLiteral("es"), 128, 206, 23, 17),
        box(QStringLiteral("secret.txt"), 320, 326, 85, 21),
        box(QStringLiteral("deepseek..."), 535, 324, 98, 23),
        {QStringLiteral("glm.txt"), 0.9, {{886, 324}, {957, 322}, {958, 349}, {887, 351}}},
        box(QStringLiteral("test-ocr-i..."), 90, 471, 96, 23),
        {QStringLiteral("gpt.txt"), 0.9, {{1000, 473}, {1066, 468}, {1068, 493}, {1002, 498}}}};
    auto lines = raw;
    prepareDetectedLineGeometry(lines, size, pixels);
    for (qsizetype i = 0; i < raw.size(); ++i) {
        const auto& q = lines[i].quad;
        const auto& original = raw[i].quad;
        require(q[0].y() == q[1].y() && q[2].y() == q[3].y() && q[0].x() == q[3].x() &&
                    q[1].x() == q[2].x(),
                "desktop labels should be horizontal");
        require(
            q.boundingRect().center() == original.boundingRect().center() &&
                qFuzzyCompare(q.boundingRect().width(),
                              QLineF(original[0], original[1]).length()) &&
                qFuzzyCompare(q.boundingRect().height(), QLineF(original[0], original[3]).length()),
            "leveling should preserve the center and detector side lengths");
        require(lines[i].text == raw[i].text && lines[i].confidence == raw[i].confidence,
                "correcting geometry should preserve recognition and line indices");
    }
    const auto merged =
        snow_shot::presentation::mergeOcrParagraphs(lines, {{0, 1}, {2}, {3}, {4}, {5}, {6}});
    require(merged && merged->size() == 6 &&
                merged->front().sourceLineQuads ==
                    QVector<QPolygonF>{lines[0].quad, lines[1].quad} &&
                merged->front().quad[0].y() == merged->front().quad[1].y(),
            "paragraph construction should use the same leveled rows as the overlay");

    auto scaled = raw;
    const QRectF canvas(-300, 45, size.width() / 2.0, size.height() / 3.0);
    prepareDetectedLineGeometry(scaled, size, canvas);
    for (qsizetype i = 0; i < lines.size(); ++i)
        for (int j = 0; j < 4; ++j) {
            const auto p = lines[i].quad[j];
            const auto expected = canvas.topLeft() + QPointF(p.x() / 2, p.y() / 3);
            require(QLineF(scaled[i].quad[j], expected).length() < 1e-8,
                    "source pixel decisions must precede anisotropic canvas mapping");
        }
}

void genuineTiltsAndIsolatedRows() {
    const QSize size(1000, 800);
    const QRectF pixels(QPointF(), QSizeF{size});
    const auto tilt = [](qreal x, qreal y, qreal width, qreal height, qreal degrees) {
        auto line = box(QStringLiteral("row"), 0, 0, width, height);
        QTransform t;
        t.translate(x, y);
        t.rotate(degrees);
        line.quad = t.map(line.quad);
        return line;
    };
    QVector<ScreenshotOcrLine> lines{tilt(40, 40, 80, 24, 3), tilt(42, 68, 90, 24, 3),
                                     tilt(42, 98, 46, 20, 2), tilt(700, 450, 80, 24, -3)};
    for (int i = 0; i < 6; ++i)
        lines.push_back(tilt(700, 482 + i * 25, 80, 24, 0));
    const auto raw = lines;
    snow_shot::ocr::prepareDetectedLineGeometry(lines, size, pixels);
    for (int i = 0; i < 3; ++i)
        require(lines[i].quad == raw[i].quad,
                "a locally tilted paragraph must survive a flat page");
    require(lines[3].quad[0].y() == lines[3].quad[1].y(),
            "a flat neighbor should support correcting a noisy label in the same region");
    auto reversed = raw;
    std::reverse(reversed.begin(), reversed.end());
    snow_shot::ocr::prepareDetectedLineGeometry(reversed, size, pixels);
    for (qsizetype i = 0; i < lines.size(); ++i)
        require(lines[i].quad == reversed[lines.size() - 1 - i].quad,
                "input order must not bias the votes");

    for (const auto& line :
         {tilt(200, 200, 55, 20, 4), tilt(200, 200, 600, 24, 2), tilt(200, 200, 26, 12, 6),
          tilt(200, 200, 14, 46, 4), tilt(200, 200, 19, 19, 2), tilt(0, 0, 50, 20, 2)}) {
        QVector<ScreenshotOcrLine> single{line};
        snow_shot::ocr::prepareDetectedLineGeometry(single, size, pixels);
        require(single.front().quad == line.quad, "isolated uncertain, long, steep, vertical, "
                                                  "square and clipped boxes must keep their shape");
    }
    QVector<ScreenshotOcrLine> isolated{tilt(200, 200, 46, 18, 2)};
    snow_shot::ocr::prepareDetectedLineGeometry(isolated, size, pixels);
    require(isolated.front().quad[0].y() == isolated.front().quad[1].y(),
            "an isolated sub-two-pixel tilt should be leveled");

    const auto suffix = tilt(100, 98, 24, 18, -4);
    QVector<ScreenshotOcrLine> narrow{suffix};
    snow_shot::ocr::prepareDetectedLineGeometry(narrow, size, pixels);
    require(narrow.front().quad == suffix.quad, "a narrow fragment alone has uncertain direction");
    narrow.push_back(tilt(88, 70, 54, 18, 0));
    snow_shot::ocr::prepareDetectedLineGeometry(narrow, size, pixels);
    require(narrow.front().quad[0].y() == narrow.front().quad[1].y(),
            "a wrapped fragment can use a longer horizontal neighbor");

    // Flat neighbors cannot override the displacement limit or rectify perspective.
    for (const auto& protectedLine :
         {tilt(180, 200, 600, 22, 2), tilt(180, 200, 250, 90, 2),
          ScreenshotOcrLine{QStringLiteral("perspective"),
                            0.9,
                            {{300, 200}, {370, 204}, {366, 223}, {300, 221}}}}) {
        const auto center = protectedLine.quad.boundingRect().center();
        QVector<ScreenshotOcrLine> mixed{protectedLine,
                                         tilt(center.x(), center.y() + 20, 70, 20, 0)};
        snow_shot::ocr::prepareDetectedLineGeometry(mixed, size, pixels);
        require(mixed.front().quad == protectedLine.quad,
                "flat support must preserve large displacement and perspective");
    }
}

void verticalLineGeometry() {
    const QSize size(900, 800);
    const QRectF pixels(QPointF(), QSizeF{size});
    const ScreenshotOcrLine rightTop{
        QStringLiteral("right top"), 0.9, {{180, 60}, {205, 58}, {210, 124}, {185, 126}}};
    const auto leftTop = box(QStringLiteral("left top"), 140, 60, 20, 66);
    QVector<ScreenshotOcrLine> columns{box(QStringLiteral("left bottom"), 140, 132, 20, 66),
                                       rightTop, leftTop,
                                       box(QStringLiteral("right bottom"), 185, 132, 20, 66)};
    snow_shot::ocr::prepareDetectedLineGeometry(columns, size, pixels);
    const auto& aligned = columns[1].quad;
    require(aligned[0].x() == aligned[3].x() && aligned[1].x() == aligned[2].x() &&
                aligned.boundingRect().center() == rightTop.quad.boundingRect().center() &&
                qFuzzyCompare(aligned.boundingRect().width(),
                              QLineF(rightTop.quad[0], rightTop.quad[1]).length()) &&
                qFuzzyCompare(aligned.boundingRect().height(),
                              QLineF(rightTop.quad[0], rightTop.quad[3]).length()) &&
                columns[1].direction == ScreenshotOcrTextDirection::Vertical,
            "leveling a vertical column keeps its center, thickness, length and direction");
    const auto paragraph = snow_shot::presentation::mergeOcrParagraphs(columns, {{0, 1, 2, 3}});
    require(paragraph && paragraph->size() == 1 &&
                paragraph->front().sourceLineIndices == QVector<int>{1, 3, 2, 0} &&
                paragraph->front().sourceLineQuads.front() == aligned,
            "corrected vertical columns read downwards from right to left");

    const ScreenshotOcrLine minor{
        QStringLiteral("short"), 0.9, {{180, 60}, {198, 61}, {196, 108}, {178, 107}}};
    QVector<ScreenshotOcrLine> alone{minor};
    snow_shot::ocr::prepareDetectedLineGeometry(alone, size, pixels);
    require(alone.front().quad[0].x() == alone.front().quad[3].x(),
            "an isolated two-pixel drift can be corrected on either side of vertical");

    // Broad horizontal support cannot straighten a column. Three distant, upright columns can.
    QVector<ScreenshotOcrLine> mixed{rightTop};
    for (int i = 0; i < 5; ++i)
        mixed.push_back(box(QStringLiteral("horizontal"), 160, 15 + 22 * i, 95, 18));
    auto horizontalNeighbors = mixed;
    snow_shot::ocr::prepareDetectedLineGeometry(horizontalNeighbors, size, pixels);
    require(horizontalNeighbors.front().quad == rightTop.quad,
            "horizontal rows cannot vote for a vertical column");
    for (int i = 0; i < 3; ++i)
        mixed.push_back(box(QStringLiteral("upright"), 550 + 55 * i, 400, 20, 100));
    auto tiltedBlock = mixed;
    auto neighbor = rightTop;
    neighbor.quad.translate(-30, 0);
    tiltedBlock.push_back(neighbor);
    snow_shot::ocr::prepareDetectedLineGeometry(tiltedBlock, size, pixels);
    require(tiltedBlock.front().quad == rightTop.quad && tiltedBlock.back().quad == neighbor.quad,
            "a genuinely tilted vertical block takes precedence over page and horizontal evidence");
    snow_shot::ocr::prepareDetectedLineGeometry(mixed, size, pixels);
    require(mixed.front().quad == aligned,
            "sparse upright columns can corroborate vertical jitter");

    QVector<ScreenshotOcrLine> crossed{
        {QStringLiteral("noisy row"), 0.9, {{65, 102}, {131, 97}, {133, 122}, {67, 127}}},
        box(QStringLiteral("straight row"), 65, 135, 70, 20),
        {QStringLiteral("long column"), 0.9, {{100, 20}, {113, 240}, {101, 241}, {88, 21}}}};
    const auto longColumn = crossed.back().quad;
    snow_shot::ocr::prepareDetectedLineGeometry(crossed, size, pixels);
    require(crossed[0].quad[0].y() == crossed[0].quad[1].y() && crossed.back().quad == longColumn,
            "a column whose corners interleave in x cannot vote as a horizontal row");
}
} // namespace
int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    smallLabelGeometry();
    genuineTiltsAndIsolatedRows();
    verticalLineGeometry();
    using snow_shot::presentation::mergeOcrLayout;
    {
        using snow_shot::presentation::mergeOcrParagraphs;
        const QVector<ScreenshotOcrLine> lines{box(QStringLiteral("second"), 0, 24, 100),
                                               box(QStringLiteral("first"), 0, 0, 100),
                                               box(QStringLiteral("distant"), 0, 1000, 100)};
        const auto separate = mergeOcrParagraphs(lines, {{0}, {2}, {1}});
        require(separate && separate->size() == 3 &&
                    separate->at(0).text == QStringLiteral("first"),
                "spatial order ignores model enumeration and cannot merge model singletons");
        const auto combined = mergeOcrParagraphs(lines, {{2, 0, 1}});
        const auto permuted = mergeOcrParagraphs(lines, {{1, 2, 0}});
        require(combined && permuted && combined->size() == 1 &&
                    combined->front().text == QStringLiteral("first second distant") &&
                    combined->front().text == permuted->front().text &&
                    combined->front().sourceLineIndices == QVector<int>{1, 0, 2},
                "model membership overrides gaps while host geometry determines reading order");
        require(!mergeOcrParagraphs(lines, {{0, 1}}) &&
                    !mergeOcrParagraphs(lines, {{0, 1}, {1, 2}}) &&
                    !mergeOcrParagraphs(lines, {{0, 1, 3}}),
                "incomplete, duplicate and out-of-range membership must fail without guessing");
        ScreenshotOcrPresentation source, translated;
        source.lines = lines;
        translated.lines = *combined;
        require(snow_shot::presentation::ocrTranslationSourceIndices(translated, source) ==
                    std::optional{QVector<QVector<int>>{{1, 0, 2}}},
                "translation provenance restores without heuristic regrouping");
    }
    {
        using snow_shot::presentation::mergeOcrParagraphs;
        QVector<ScreenshotOcrLine> lines{box(QStringLiteral("left bottom"), 63, 53, 12, 44),
                                         box(QStringLiteral("right bottom"), 79, 53, 14, 44),
                                         box(QStringLiteral("left top"), 62, 1, 12, 44),
                                         box(QStringLiteral("right top"), 80, 8, 12, 40)};
        for (auto& line : lines)
            line.direction = ScreenshotOcrTextDirection::Vertical;
        const auto paragraph = mergeOcrParagraphs(lines, {{0, 2, 3, 1}});
        require(
            paragraph && paragraph->size() == 1 &&
                paragraph->front().text ==
                    QStringLiteral("right top right bottom left top left bottom") &&
                paragraph->front().sourceLineIndices == QVector<int>{3, 1, 2, 0} &&
                paragraph->front().sourceLineQuads ==
                    QVector<QPolygonF>{lines[3].quad, lines[1].quad, lines[2].quad, lines[0].quad},
            "vertical paragraphs read down each column before moving to the left");

        lines.push_back(box(QStringLiteral("header"), 0, -40, 150));
        lines.push_back(box(QStringLiteral("footer"), 0, 110, 150));
        QVector<QVector<int>> order;
        const auto separate = mergeOcrParagraphs(lines, {{2, 0}, {5}, {1, 3}, {4}}, &order);
        require(separate && order == QVector<QVector<int>>{{4}, {3, 1}, {2, 0}, {5}},
                "separate vertical paragraphs read right to left between horizontal blocks");
    }
    // Fixtures adapted from STranslate OcrLayoutAnalyzerTests (MIT).
    const QVector<ScreenshotOcrLine> paragraph{
        box(QStringLiteral("This is the first line"), 0, 0, 180),
        box(QStringLiteral("continued on the next line"), 0, 24, 210)};
    expect(paragraph, {QStringLiteral("This is the first line continued on the next line")},
           "paragraph");
    const auto merged = mergeOcrLayout(paragraph);
    auto coloredParagraph = paragraph;
    coloredParagraph[0].sourceTextColor = Qt::blue;
    coloredParagraph[1].sourceTextColor = Qt::red;
    const auto coloredMerged = mergeOcrLayout(coloredParagraph);
    require(coloredMerged.size() == 1 &&
                coloredMerged[0].sourceLineTextColors == QVector<QColor>{Qt::blue, Qt::red} &&
                coloredMerged[0].sourceTextColor == QColor(Qt::red) &&
                coloredParagraph[0].sourceLineTextColors.isEmpty(),
            "paragraph merging must preserve source-row colors and choose one representative for "
            "reflow");
    auto shifted = paragraph;
    const QPointF origin(-500, -300);
    for (auto& line : shifted)
        line.quad.translate(origin);
    const auto shiftedMerged = mergeOcrLayout(shifted, origin);
    require(shiftedMerged.size() == 1 && shiftedMerged[0].text == merged[0].text &&
                shiftedMerged[0].quad == merged[0].quad.translated(origin),
            "canvas origin must not change image layout decisions");
    require(merged.size() == 1 && merged[0].paragraph &&
                merged[0].quad.boundingRect() == QRectF(0, 0, 210, 44) &&
                merged[0].sourceLineQuads ==
                    QVector<QPolygonF>{paragraph[0].quad, paragraph[1].quad} &&
                paragraph[0].text == QStringLiteral("This is the first line") &&
                paragraph[0].sourceLineQuads.isEmpty(),
            "merge must retain source geometry without modifying original OCR");
    expect({box(QStringLiteral("Left first paragraph"), 0, 0, 170),
            box(QStringLiteral("continues here"), 0, 24, 135),
            box(QStringLiteral("Right column starts"), 300, 12, 170),
            box(QStringLiteral("continues separately"), 300, 36, 180),
            box(QStringLiteral("Left second paragraph"), 0, 70, 185),
            box(QStringLiteral("continues too"), 0, 94, 120)},
           {QStringLiteral("Left first paragraph continues here"),
            QStringLiteral("Left second paragraph continues too"),
            QStringLiteral("Right column starts continues separately")},
           "column reading order");
    expect({box(QStringLiteral("File"), 0, 0, 32), box(QStringLiteral("Edit"), 66, 0, 32),
            box(QStringLiteral("View"), 132, 0, 36)},
           {QStringLiteral("File"), QStringLiteral("Edit"), QStringLiteral("View")}, "menu items");
    expect({box(QStringLiteral("First name"), 0, 0, 100),
            box(QStringLiteral("Order status"), 150, 0, 110),
            box(QStringLiteral("Alice Smith"), 0, 28, 105),
            box(QStringLiteral("Active now"), 150, 28, 95),
            box(QStringLiteral("Bob Stone"), 0, 56, 90),
            box(QStringLiteral("Paused now"), 150, 56, 96)},
           {QStringLiteral("First name"), QStringLiteral("Alice Smith"),
            QStringLiteral("Bob Stone"), QStringLiteral("Order status"),
            QStringLiteral("Active now"), QStringLiteral("Paused now")},
           "table cells");
    expect({box(QStringLiteral("This is trans-"), 0, 0, 160),
            box(QStringLiteral("lation continued"), 0, 24, 180)},
           {QStringLiteral("This is translation continued")}, "hyphenated continuation");
    expect({box(QStringLiteral("Hello"), 0, 0, 50), box(QStringLiteral("world"), 56, 0, 50)},
           {QStringLiteral("Hello world")}, "Latin word spacing");
    expect({box(QStringLiteral("\u4f60\u597d"), 0, 0, 40),
            box(QStringLiteral("\u4e16\u754c"), 44, 0, 40)},
           {QStringLiteral("\u4f60\u597d\u4e16\u754c")}, "CJK word spacing");
    expect(
        {box(QStringLiteral("1. First item"), 0, 0, 150),
         box(QStringLiteral("continued description"), 20, 24, 200),
         box(QStringLiteral("2. Second item"), 0, 48, 150)},
        {QStringLiteral("1. First item continued description"), QStringLiteral("2. Second item")},
        "list continuation");
    expect({box(QStringLiteral("1. First\ncontinuation\n2. Second\n3. Third"), 0, 0, 200, 80)},
           {QStringLiteral("1. First continuation"), QStringLiteral("2. Second"),
            QStringLiteral("3. Third")},
           "embedded list");
    const QString ordinary = QStringLiteral("Ordinary multiline\nOCR block");
    expect({box(ordinary, 0, 0, 200, 40)}, {ordinary}, "ordinary multiline block");
    const QString embeddedBreak = QStringLiteral("First row\u2028second row");
    const auto embeddedBlock = mergeOcrLayout(
        {box(QStringLiteral("Prefix"), 0, 0, 48, 40), box(embeddedBreak, 52, 0, 160, 40)});
    require(embeddedBlock.size() == 1 && embeddedBlock[0].paragraph &&
                embeddedBlock[0].text == QStringLiteral("Prefix ") + embeddedBreak,
            "same-row merging retains an embedded Unicode line break in a later OCR box");
    const QString numbers = QStringLiteral("1. First\n3. Third");
    expect({box(numbers, 0, 0, 200, 40)}, {numbers}, "nonsequential list");
    expect({box(QStringLiteral("A large heading"), 0, 0, 240, 36),
            box(QStringLiteral("Body starts below"), 0, 40, 200)},
           {QStringLiteral("A large heading"), QStringLiteral("Body starts below")}, "heading");
    expect({box(QStringLiteral("The first paragraph ends."), 0, 0, 240),
            box(QStringLiteral("Another paragraph starts here"), 0, 38, 250)},
           {QStringLiteral("The first paragraph ends."),
            QStringLiteral("Another paragraph starts here")},
           "paragraph gap");
    auto vertical = box(QStringLiteral("vertical text"), 300, 0, 20, 160);
    vertical.direction = ScreenshotOcrTextDirection::Vertical;
    auto rotated = box(QStringLiteral("rotated text"), 400, 0, 100);
    rotated.quad = QTransform().rotate(20).map(rotated.quad);
    ScreenshotOcrLine missing;
    missing.text = QStringLiteral("no coordinates");
    auto invalid = box(QStringLiteral("invalid coordinates"), 0, 0, 10);
    invalid.quad[0].setX(std::numeric_limits<qreal>::quiet_NaN());
    auto mixed = paragraph;
    mixed += {vertical, rotated, missing, invalid};
    const auto preserved = mergeOcrLayout(mixed);
    require(preserved.size() == 5 && preserved[1].quad == vertical.quad &&
                preserved[2].quad == rotated.quad && preserved[3].text == missing.text &&
                preserved[4].text == invalid.text,
            "unsupported geometry must never lose OCR text");
    require(mergeOcrLayout({}).isEmpty(), "empty input");
    QVector<ScreenshotOcrLine> touchingTable;
    expect({box(QStringLiteral("Column one starts here"), 0, 0, 205),
            box(QStringLiteral("Column two starts here"), 300, 0, 205),
            box(QStringLiteral("Column three starts here"), 600, 0, 225),
            box(QStringLiteral("and carries the thought"), 0, 24, 210),
            box(QStringLiteral("with the next sentence"), 300, 24, 210),
            box(QStringLiteral("through another line"), 600, 24, 185),
            box(QStringLiteral("before ending normally"), 0, 48, 210),
            box(QStringLiteral("inside the same column"), 300, 48, 215),
            box(QStringLiteral("without table spacing"), 600, 48, 195)},
           {QStringLiteral("Column one starts here and carries the thought before ending normally"),
            QStringLiteral("Column two starts here with the next sentence inside the same column"),
            QStringLiteral("Column three starts here through another line without table spacing")},
           "multi-column body must not become a table");
    expect({box(QStringLiteral("first paragraph starts with an indent"), 24, 0, 420, 30),
            box(QStringLiteral("continues on the full body width"), 0, 36, 500, 30),
            box(QStringLiteral("ends on a short last line"), 0, 72, 240, 30),
            box(QStringLiteral("next paragraph starts indented"), 24, 108, 430, 30),
            box(QStringLiteral("continues with body text"), 0, 144, 490, 30)},
           {QStringLiteral("first paragraph starts with an indent continues on the full body width "
                           "ends on a short last line"),
            QStringLiteral("next paragraph starts indented continues with body text")},
           "first-line indent");
    expect({box(QStringLiteral("*"), 0, 0, 20, 24),
            box(QStringLiteral("Advanced Paste"), 44, 0, 154, 24),
            box(QStringLiteral("*"), 300, 0, 20, 24),
            box(QStringLiteral("Always on Top"), 344, 0, 148, 24),
            box(QStringLiteral("*"), 0, 40, 20, 24),
            box(QStringLiteral("Color Picker"), 44, 40, 128, 24),
            box(QStringLiteral("*"), 300, 40, 20, 24),
            box(QStringLiteral("Command Palette"), 344, 40, 184, 24),
            box(QStringLiteral("*"), 0, 80, 20, 24),
            box(QStringLiteral("File Explorer Add-ons"), 44, 80, 212, 24),
            box(QStringLiteral("*"), 300, 80, 20, 24),
            box(QStringLiteral("File Locksmith"), 344, 80, 144, 24)},
           {QStringLiteral("*Advanced Paste"), QStringLiteral("*Color Picker"),
            QStringLiteral("*File Explorer Add-ons"), QStringLiteral("*Always on Top"),
            QStringLiteral("*Command Palette"), QStringLiteral("*File Locksmith")},
           "table leading adornments stay with text");
    for (const int y : {0, 20, 40, 68}) {
        for (const int x : {0, 250, 500}) {
            touchingTable.push_back(box(QStringLiteral("Several words in this cell"), x, y, 200));
        }
    }
    require(mergeOcrLayout(touchingTable).size() == 12,
            "STranslate medians ignore zero gaps when recognizing touching table rows");
    QVector<QVector<int>> indices;
    const auto withProvenance = mergeOcrLayout(mixed, {}, &indices);
    require(withProvenance.size() == preserved.size() &&
                indices == QVector<QVector<int>>{{0, 1}, {2}, {3}, {4}, {5}},
            "merged and preserved units retain their original OCR line indices");
    const QVector<ScreenshotOcrLine> splitSource{
        box(QStringLiteral("1. First\ncontinuation\n2. Second\n3. Third"), 0, 0, 200, 80)};
    const auto split = mergeOcrLayout(splitSource, {}, &indices);
    require(split.size() == 3 && indices == QVector<QVector<int>>{{0}, {0}, {0}},
            "every split list item inherits its original block's font baseline provenance");
    require(split[0].paragraph && split[0].quad.boundingRect() == QRectF(0, 0, 200, 40) &&
                !split[1].paragraph && !split[2].paragraph,
            "a flattened list continuation retains its multiline area for translation");
    ScreenshotOcrPresentation source;
    source.selection = QRect(0, 0, 200, 80);
    source.lines = splitSource;
    ScreenshotOcrPresentation translation = source;
    translation.lines = split;
    for (auto& line : translation.lines)
        line.text = QStringLiteral("translated");
    QVector<int> paragraphsToRestore;
    bool smartMerge = false;
    require(snow_shot::presentation::ocrTranslationSourceIndices(
                translation, source, &paragraphsToRestore, &smartMerge) == indices &&
                paragraphsToRestore.isEmpty() && smartMerge,
            "restoration reconstructs captured Smart Merge provenance without translated text");
    translation.lines[0].paragraph = false;
    require(snow_shot::presentation::ocrTranslationSourceIndices(translation, source,
                                                                 &paragraphsToRestore) == indices &&
                paragraphsToRestore == QVector<int>{0},
            "legacy pins restore paragraph layout only for matching list continuation geometry");
    translation.lines[0].quad.translate(1, 0);
    require(!snow_shot::presentation::ocrTranslationSourceIndices(translation, source,
                                                                  &paragraphsToRestore) &&
                paragraphsToRestore.isEmpty(),
            "legacy migration still requires the original captured quad");
    translation.lines[0].quad = split[0].quad;
    translation.lines[1].paragraph = true;
    require(!snow_shot::presentation::ocrTranslationSourceIndices(translation, source,
                                                                  &paragraphsToRestore) &&
                paragraphsToRestore.isEmpty(),
            "unrelated paragraph flag changes do not qualify a capture for migration");
    translation.lines = source.lines;
    translation.lines[0].text = QStringLiteral("short");
    require(snow_shot::presentation::ocrTranslationSourceIndices(
                translation, source, nullptr, &smartMerge) == QVector<QVector<int>>{{0}} &&
                !smartMerge,
            "restoration also matches the original unmerged layout");
    const auto singleton = snow_shot::presentation::mergeOcrParagraphs(source.lines, {{0}});
    require(singleton.has_value(), "a Smart Merge group may contain a single source box");
    translation.lines = *singleton;
    require(snow_shot::presentation::ocrTranslationSourceIndices(
                translation, source, nullptr, &smartMerge) == QVector<QVector<int>>{{0}} &&
                smartMerge,
            "saved single-box Smart Merge retains its explicit provenance");
    translation.lines = source.lines;
    translation.lines[0].quad.translate(10, 0);
    require(!snow_shot::presentation::ocrTranslationSourceIndices(translation, source),
            "unrelated geometry must not inherit another source's font cap");
    static_cast<void>(mergeOcrLayout(
        {box(QStringLiteral("Hello"), 0, 0, 50), box(QStringLiteral("world"), 56, 0, 50)}, {},
        &indices));
    require(indices == QVector<QVector<int>>{{0, 1}},
            "same-row fragments contribute their separate original baselines");
    return EXIT_SUCCESS;
}
