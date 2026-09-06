#include <QLabel>
#include <QPointer>
#include <QSignalSpy>
#include <QtTest>

#include "widgets/descriptions.h"

using adqt::widgets::AdDescriptions;

namespace {

QLabel* labelWithText(AdDescriptions* descriptions, const QString& text) {
  const QList<QLabel*> labels = descriptions->findChildren<QLabel*>();
  for (QLabel* label : labels) {
    if (label->text() == text) return label;
  }
  return nullptr;
}

AdDescriptions::Item item(const QString& key, const QString& label, const QString& content,
                          int span = 1) {
  AdDescriptions::Item result;
  result.key = key;
  result.label = label;
  result.content = content;
  result.span = span;
  return result;
}

class ParentChangeLabel final : public QLabel {
 public:
  using QLabel::QLabel;

  int parentChangeCount = 0;

 protected:
  bool event(QEvent* event) override {
    if (event->type() == QEvent::ParentChange) ++parentChangeCount;
    return QLabel::event(event);
  }
};

}  // namespace

class DescriptionsTest final : public QObject {
  Q_OBJECT

 private slots:
  void defaultsAndPropertiesRoundTrip();
  void observablePropertiesTrackCollectionAndLayout();
  void responsiveColumnsFollowAntBreakpoints();
  void fixedColumnDisablesResponsiveMap();
  void spanAndFillRemainingPackRows();
  void itemSpanCanRespondToWidth();
  void itemMutationUsesStableKeys();
  void customWidgetsFollowOwnershipContract();
  void externallyDestroyedWidgetsRefreshTheirSlots();
  void semanticRootStyleDoesNotReenterRebuild();
  void resizeOnlyRebuildsAtResponsiveBoundaries();
  void wrappedContentParticipatesInHeightForWidth();
  void titleExtraAndAccessibilityAreExposed();
  void componentTokensAndSemanticStylesRender();
  void zeroWidthBorderDoesNotPaint();
  void verticalBorderedAndRtlRender();
};

void DescriptionsTest::defaultsAndPropertiesRoundTrip() {
  AdDescriptions descriptions;
  QCOMPARE(descriptions.count(), 0);
  QVERIFY(!descriptions.bordered());
  QCOMPARE(descriptions.descriptionSize(), AdDescriptions::Size::Default);
  QCOMPARE(descriptions.layoutMode(), AdDescriptions::LayoutMode::Horizontal);
  QCOMPARE(descriptions.column(), 3);
  QVERIFY(descriptions.colon());
  const QMap<int, int> defaultColumns{{0, 1}, {576, 2}, {768, 3}};
  QCOMPARE(descriptions.responsiveColumns(), defaultColumns);

  QSignalSpy borderedChanged(&descriptions, &AdDescriptions::borderedChanged);
  QSignalSpy sizeChanged(&descriptions, &AdDescriptions::sizeChanged);
  QSignalSpy layoutChanged(&descriptions, &AdDescriptions::layoutModeChanged);
  QSignalSpy colonChanged(&descriptions, &AdDescriptions::colonChanged);
  descriptions.setBordered(true);
  descriptions.setSize(AdDescriptions::Size::Small);
  descriptions.setLayoutMode(AdDescriptions::LayoutMode::Vertical);
  descriptions.setColon(false);
  QCOMPARE(borderedChanged.count(), 1);
  QCOMPARE(sizeChanged.count(), 1);
  QCOMPARE(layoutChanged.count(), 1);
  QCOMPARE(colonChanged.count(), 1);
}

void DescriptionsTest::observablePropertiesTrackCollectionAndLayout() {
  AdDescriptions descriptions;
  QVERIFY(descriptions.metaObject()->indexOfProperty("count") >= 0);
  QVERIFY(descriptions.metaObject()->indexOfProperty("effectiveColumn") >= 0);
  QSignalSpy countChanged(&descriptions, &AdDescriptions::countChanged);
  QSignalSpy effectiveChanged(&descriptions, &AdDescriptions::effectiveColumnChanged);

  descriptions.addItem(QStringLiteral("Name"), QStringLiteral("Ada"));
  QCOMPARE(descriptions.property("count").toInt(), 1);
  QCOMPARE(countChanged.count(), 1);
  descriptions.removeItem(0);
  QCOMPARE(descriptions.property("count").toInt(), 0);
  QCOMPARE(countChanged.count(), 2);

  descriptions.resize(640, 120);
  descriptions.show();
  QVERIFY(QTest::qWaitForWindowExposed(&descriptions));
  descriptions.resize(480, 120);
  QCOMPARE(descriptions.property("effectiveColumn").toInt(), 1);
  descriptions.resize(800, 120);
  QCOMPARE(descriptions.property("effectiveColumn").toInt(), 3);
  QCOMPARE(effectiveChanged.count(), 2);
}

void DescriptionsTest::responsiveColumnsFollowAntBreakpoints() {
  AdDescriptions descriptions;
  descriptions.resize(900, 200);
  QCOMPARE(descriptions.effectiveColumn(), 3);
  descriptions.resize(700, 200);
  QCOMPARE(descriptions.effectiveColumn(), 2);
  descriptions.resize(480, 200);
  QCOMPARE(descriptions.effectiveColumn(), 1);

  descriptions.setResponsiveColumns({{0, 1}, {400, 2}, {800, 4}});
  QCOMPARE(descriptions.effectiveColumn(), 2);
  descriptions.resize(820, 200);
  QCOMPARE(descriptions.effectiveColumn(), 4);
}

void DescriptionsTest::fixedColumnDisablesResponsiveMap() {
  AdDescriptions descriptions;
  QSignalSpy columnChanged(&descriptions, &AdDescriptions::columnChanged);
  QSignalSpy responsiveChanged(&descriptions, &AdDescriptions::responsiveColumnsChanged);
  descriptions.resize(320, 160);
  QCOMPARE(descriptions.effectiveColumn(), 1);
  descriptions.setColumn(3);
  QCOMPARE(columnChanged.count(), 0);
  QCOMPARE(responsiveChanged.count(), 1);
  descriptions.setColumn(4);
  QCOMPARE(descriptions.column(), 4);
  QCOMPARE(descriptions.effectiveColumn(), 4);
  QVERIFY(descriptions.responsiveColumns().isEmpty());
  QCOMPARE(columnChanged.count(), 1);
  QCOMPARE(responsiveChanged.count(), 1);
  descriptions.resize(220, 160);
  QCOMPARE(descriptions.effectiveColumn(), 4);
}

void DescriptionsTest::spanAndFillRemainingPackRows() {
  AdDescriptions descriptions;
  descriptions.setColumn(3);
  descriptions.setBordered(true);
  descriptions.addItem(item(QStringLiteral("a"), QStringLiteral("A"), QStringLiteral("one"), 2));
  descriptions.addItem(item(QStringLiteral("b"), QStringLiteral("B"), QStringLiteral("two"), 2));
  AdDescriptions::Item filled =
      item(QStringLiteral("c"), QStringLiteral("C"), QStringLiteral("three"));
  filled.fillRemaining = true;
  descriptions.addItem(filled);
  descriptions.addItem(item(QStringLiteral("d"), QStringLiteral("D"), QStringLiteral("four")));
  descriptions.resize(900, 260);
  descriptions.show();
  QVERIFY(QTest::qWaitForWindowExposed(&descriptions));

  QLabel* one = labelWithText(&descriptions, QStringLiteral("one"));
  QLabel* two = labelWithText(&descriptions, QStringLiteral("two"));
  QLabel* three = labelWithText(&descriptions, QStringLiteral("three"));
  QLabel* four = labelWithText(&descriptions, QStringLiteral("four"));
  QVERIFY(one);
  QVERIFY(two);
  QVERIFY(three);
  QVERIFY(four);
  QCOMPARE(one->mapTo(&descriptions, QPoint()).y(), two->mapTo(&descriptions, QPoint()).y());
  QVERIFY(two->mapTo(&descriptions, QPoint()).y() < three->mapTo(&descriptions, QPoint()).y());
  QVERIFY(three->mapTo(&descriptions, QPoint()).y() < four->mapTo(&descriptions, QPoint()).y());
}

void DescriptionsTest::itemSpanCanRespondToWidth() {
  AdDescriptions descriptions;
  descriptions.setColumn(4);
  descriptions.setBordered(true);
  AdDescriptions::Item first =
      item(QStringLiteral("a"), QStringLiteral("A"), QStringLiteral("one"));
  first.responsiveSpans = {{0, 1}, {800, 3}};
  descriptions.addItem(first);
  descriptions.addItem(item(QStringLiteral("b"), QStringLiteral("B"), QStringLiteral("two")));
  descriptions.addItem(item(QStringLiteral("c"), QStringLiteral("C"), QStringLiteral("three")));
  descriptions.addItem(item(QStringLiteral("d"), QStringLiteral("D"), QStringLiteral("four")));
  descriptions.resize(900, 220);
  descriptions.show();
  QVERIFY(QTest::qWaitForWindowExposed(&descriptions));
  QLabel* one = labelWithText(&descriptions, QStringLiteral("one"));
  QLabel* three = labelWithText(&descriptions, QStringLiteral("three"));
  QVERIFY(one);
  QVERIFY(three);
  QVERIFY(one->mapTo(&descriptions, QPoint()).y() < three->mapTo(&descriptions, QPoint()).y());

  descriptions.resize(700, 220);
  QCoreApplication::processEvents();
  one = labelWithText(&descriptions, QStringLiteral("one"));
  three = labelWithText(&descriptions, QStringLiteral("three"));
  QVERIFY(one);
  QVERIFY(three);
  QCOMPARE(one->mapTo(&descriptions, QPoint()).y(), three->mapTo(&descriptions, QPoint()).y());
}

void DescriptionsTest::itemMutationUsesStableKeys() {
  AdDescriptions descriptions;
  QSignalSpy added(&descriptions, &AdDescriptions::itemAdded);
  QSignalSpy changed(&descriptions, &AdDescriptions::itemChanged);
  QSignalSpy removed(&descriptions, &AdDescriptions::itemRemoved);
  QCOMPARE(descriptions.addItem(
               item(QStringLiteral("profile"), QStringLiteral("User"), QStringLiteral("Ada"))),
           0);
  QCOMPARE(descriptions.addItem(item(QStringLiteral("profile"), QStringLiteral("Duplicate"),
                                     QStringLiteral("Ignored"))),
           -1);
  QCOMPARE(descriptions.indexOf(QStringLiteral("profile")), 0);
  descriptions.setItemContent(0, QStringLiteral("Grace"));
  descriptions.setItemSpan(0, 0);
  QCOMPARE(descriptions.itemAt(0).content, QStringLiteral("Grace"));
  QCOMPARE(descriptions.itemAt(0).span, 1);
  descriptions.removeItem(QStringLiteral("profile"));
  QCOMPARE(descriptions.count(), 0);
  QCOMPARE(added.count(), 1);
  QCOMPARE(changed.count(), 1);
  QCOMPARE(removed.count(), 1);
}

void DescriptionsTest::customWidgetsFollowOwnershipContract() {
  AdDescriptions descriptions;
  auto* label = new QLabel(QStringLiteral("Custom label"));
  auto* content = new QLabel(QStringLiteral("Custom content"));
  QPointer<QWidget> labelGuard(label);
  QPointer<QWidget> contentGuard(content);
  AdDescriptions::Item custom = item(QStringLiteral("custom"), QString(), QString());
  custom.labelWidget = label;
  custom.contentWidget = content;
  QCOMPARE(descriptions.addItem(custom), 0);
  QCOMPARE(label->parentWidget()->window(), &descriptions);
  QCOMPARE(content->parentWidget()->window(), &descriptions);
  QCOMPARE(descriptions.addItem(custom), -1);

  AdDescriptions::Item taken = descriptions.takeItem(0);
  QCOMPARE(taken.labelWidget.data(), label);
  QCOMPARE(taken.contentWidget.data(), content);
  QVERIFY(label->parentWidget() == nullptr);
  QVERIFY(content->parentWidget() == nullptr);
  delete label;
  delete content;
  QVERIFY(labelGuard.isNull());
  QVERIFY(contentGuard.isNull());

  auto* owned = new QLabel(QStringLiteral("Owned"));
  QPointer<QWidget> ownedGuard(owned);
  custom = item(QStringLiteral("owned"), QStringLiteral("Label"), QString());
  custom.contentWidget = owned;
  descriptions.addItem(custom);
  descriptions.removeItem(0);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QVERIFY(ownedGuard.isNull());
}

void DescriptionsTest::externallyDestroyedWidgetsRefreshTheirSlots() {
  AdDescriptions descriptions;
  descriptions.setTitle(QStringLiteral("Fallback title"));
  auto* title = new QLabel(QStringLiteral("Custom title"));
  title->setObjectName(QStringLiteral("callerTitle"));
  descriptions.setTitleWidget(title);

  AdDescriptions::Item custom =
      item(QStringLiteral("custom"), QStringLiteral("Label"), QStringLiteral("Fallback content"));
  auto* content = new QLabel(QStringLiteral("Custom content"));
  custom.contentWidget = content;
  descriptions.addItem(custom);
  QCOMPARE(title->objectName(), QStringLiteral("callerTitle"));

  QSignalSpy titleChanged(&descriptions, &AdDescriptions::titleWidgetChanged);
  QSignalSpy itemChanged(&descriptions, &AdDescriptions::itemChanged);
  delete title;
  delete content;

  QTRY_COMPARE(titleChanged.count(), 1);
  QTRY_COMPARE(itemChanged.count(), 1);
  QVERIFY(!descriptions.titleWidget());
  QVERIFY(!descriptions.itemAt(0).contentWidget);
  QVERIFY(labelWithText(&descriptions, QStringLiteral("Fallback title")));
  QVERIFY(labelWithText(&descriptions, QStringLiteral("Fallback content")));
}

void DescriptionsTest::semanticRootStyleDoesNotReenterRebuild() {
  AdDescriptions descriptions;
  descriptions.setTitle(QStringLiteral("User Info"));
  descriptions.addItem(QStringLiteral("Name"), QStringLiteral("Ada"));

  AdDescriptions::SemanticStyles semantics;
  semantics.root.backgroundColor = QColor(QStringLiteral("#f6f7f9"));
  QFont rootFont = descriptions.font();
  rootFont.setPixelSize(17);
  semantics.root.font = rootFont;
  descriptions.setSemanticStyles(semantics);

  QCOMPARE(descriptions.findChildren<QWidget*>(QStringLiteral("adDescriptionsView")).size(), 1);
  QCOMPARE(descriptions.findChildren<QWidget*>(QStringLiteral("adDescriptionsHeader")).size(), 1);
  QLabel* title = labelWithText(&descriptions, QStringLiteral("User Info"));
  QLabel* label = labelWithText(&descriptions, QStringLiteral("Name"));
  QVERIFY(title);
  QVERIFY(label);
  QCOMPARE(title->font().pixelSize(), 17);
  QCOMPARE(label->font().pixelSize(), 17);
}

void DescriptionsTest::resizeOnlyRebuildsAtResponsiveBoundaries() {
  AdDescriptions descriptions;
  descriptions.setColumn(3);
  AdDescriptions::Item custom =
      item(QStringLiteral("responsive"), QStringLiteral("Label"), QStringLiteral("Content"));
  custom.responsiveSpans = {{0, 1}, {800, 2}};
  auto* content = new ParentChangeLabel(QStringLiteral("Custom content"));
  custom.contentWidget = content;
  descriptions.addItem(custom);
  descriptions.resize(700, 160);
  descriptions.show();
  QVERIFY(QTest::qWaitForWindowExposed(&descriptions));

  content->parentChangeCount = 0;
  descriptions.resize(720, 160);
  QCoreApplication::processEvents();
  QCOMPARE(content->parentChangeCount, 0);

  descriptions.resize(820, 160);
  QCoreApplication::processEvents();
  QVERIFY(content->parentChangeCount > 0);
}

void DescriptionsTest::wrappedContentParticipatesInHeightForWidth() {
  AdDescriptions descriptions;
  descriptions.setColumn(1);
  descriptions.setBordered(true);
  descriptions.addItem(
      QStringLiteral("Summary"),
      QStringLiteral("A deliberately long description that wraps onto several lines when the "
                     "available width becomes narrow."));

  QCOMPARE(descriptions.sizePolicy().verticalPolicy(), QSizePolicy::Minimum);
  QVERIFY(descriptions.hasHeightForWidth());
  const int wideHeight = descriptions.heightForWidth(640);
  const int narrowHeight = descriptions.heightForWidth(240);
  QVERIFY(wideHeight > 0);
  QVERIFY(narrowHeight > wideHeight);
}

void DescriptionsTest::titleExtraAndAccessibilityAreExposed() {
  AdDescriptions descriptions;
  descriptions.setTitle(QStringLiteral("User Info"));
  descriptions.setExtra(QStringLiteral("Edit"));
  descriptions.addItem(QStringLiteral("Name"), QStringLiteral("Ada Lovelace"));
  QCOMPARE(descriptions.accessibleName(), QStringLiteral("User Info"));
  QVERIFY(descriptions.accessibleDescription().contains(QStringLiteral("Name: Ada Lovelace")));
  QLabel* title = labelWithText(&descriptions, QStringLiteral("User Info"));
  QLabel* extra = labelWithText(&descriptions, QStringLiteral("Edit"));
  QVERIFY(title);
  QVERIFY(extra);
  QCOMPARE(title->objectName(), QStringLiteral("adDescriptionsTitle"));
  QCOMPARE(extra->objectName(), QStringLiteral("adDescriptionsExtra"));

  descriptions.setAccessibleName(QStringLiteral("Account summary"));
  descriptions.setAccessibleDescription(QStringLiteral("Application-provided summary"));
  descriptions.setTitle(QStringLiteral("Changed title"));
  descriptions.setItemContent(0, QStringLiteral("Grace Hopper"));
  QCOMPARE(descriptions.accessibleName(), QStringLiteral("Account summary"));
  QCOMPARE(descriptions.accessibleDescription(), QStringLiteral("Application-provided summary"));

  descriptions.setAccessibleName(QString());
  descriptions.setAccessibleDescription(QString());
  descriptions.setItemContent(0, QStringLiteral("Katherine Johnson"));
  QCOMPARE(descriptions.accessibleName(), QStringLiteral("Changed title"));
  QVERIFY(descriptions.accessibleDescription().contains(QStringLiteral("Name: Katherine Johnson")));
}

void DescriptionsTest::componentTokensAndSemanticStylesRender() {
  AdDescriptions descriptions;
  descriptions.setColumn(1);
  descriptions.setBordered(true);
  descriptions.setTitle(QStringLiteral("Status details"));
  descriptions.addItem(QStringLiteral("Status"), QStringLiteral("Running"));
  AdDescriptions::ComponentTokens tokens;
  tokens.colors.labelBackground = QColor(QStringLiteral("#ff0000"));
  tokens.colors.borderColor = QColor(QStringLiteral("#00ff00"));
  tokens.metrics.borderWidth = 2;
  tokens.metrics.borderedPaddingBlock = 12;
  descriptions.setComponentTokens(tokens);
  AdDescriptions::SemanticStyles semantics;
  semantics.content.textColor = QColor(QStringLiteral("#0000ff"));
  semantics.header.backgroundColor = QColor(QStringLiteral("#00ffff"));
  semantics.title.backgroundColor = QColor(QStringLiteral("#ff00ff"));
  semantics.title.borderColor = QColor(QStringLiteral("#ffff00"));
  descriptions.setSemanticStyles(semantics);
  descriptions.resize(420, 120);
  descriptions.show();
  QVERIFY(QTest::qWaitForWindowExposed(&descriptions));
  const QImage image = descriptions.grab().toImage().convertToFormat(QImage::Format_ARGB32);
  QVERIFY(!image.isNull());
  bool hasRed = false;
  bool hasGreen = false;
  bool hasBlue = false;
  bool hasCyan = false;
  bool hasMagenta = false;
  bool hasYellow = false;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const QColor pixel = image.pixelColor(x, y);
      hasRed = hasRed || (pixel.red() > 220 && pixel.green() < 60 && pixel.blue() < 60);
      hasGreen = hasGreen || (pixel.green() > 220 && pixel.red() < 60 && pixel.blue() < 60);
      hasBlue = hasBlue || (pixel.blue() > 180 && pixel.red() < 100);
      hasCyan = hasCyan || (pixel.green() > 220 && pixel.blue() > 220 && pixel.red() < 60);
      hasMagenta = hasMagenta || (pixel.red() > 220 && pixel.blue() > 220 && pixel.green() < 60);
      hasYellow = hasYellow || (pixel.red() > 220 && pixel.green() > 220 && pixel.blue() < 60);
    }
  }
  QVERIFY(hasRed);
  QVERIFY(hasGreen);
  QVERIFY(hasBlue);
  QVERIFY(hasCyan);
  QVERIFY(hasMagenta);
  QVERIFY(hasYellow);
}

void DescriptionsTest::zeroWidthBorderDoesNotPaint() {
  AdDescriptions descriptions;
  descriptions.setColumn(1);
  descriptions.setBordered(true);
  descriptions.addItem(QStringLiteral("Status"), QStringLiteral("Running"));
  AdDescriptions::ComponentTokens tokens;
  tokens.colors.borderColor = QColor(QStringLiteral("#00ff00"));
  tokens.metrics.borderWidth = 0;
  descriptions.setComponentTokens(tokens);
  descriptions.resize(420, 120);
  descriptions.show();
  QVERIFY(QTest::qWaitForWindowExposed(&descriptions));

  const QImage image = descriptions.grab().toImage().convertToFormat(QImage::Format_ARGB32);
  QVERIFY(!image.isNull());
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const QColor pixel = image.pixelColor(x, y);
      QVERIFY2(!(pixel.green() > 220 && pixel.red() < 60 && pixel.blue() < 60),
               "A zero-width border painted a cosmetic one-pixel line");
    }
  }
}

void DescriptionsTest::verticalBorderedAndRtlRender() {
  AdDescriptions descriptions;
  descriptions.setColumn(2);
  descriptions.setLayoutMode(AdDescriptions::LayoutMode::Vertical);
  descriptions.setBordered(true);
  descriptions.setLayoutDirection(Qt::RightToLeft);
  descriptions.addItem(QStringLiteral("Product"), QStringLiteral("Cloud Database"));
  descriptions.addItem(QStringLiteral("Billing"), QStringLiteral("Prepaid"));
  descriptions.resize(640, 180);
  descriptions.show();
  QVERIFY(QTest::qWaitForWindowExposed(&descriptions));
  QLabel* product = labelWithText(&descriptions, QStringLiteral("Product"));
  QLabel* content = labelWithText(&descriptions, QStringLiteral("Cloud Database"));
  QVERIFY(product);
  QVERIFY(content);
  QVERIFY(product->mapTo(&descriptions, QPoint()).y() <
          content->mapTo(&descriptions, QPoint()).y());
  const QImage image = descriptions.grab().toImage();
  QVERIFY(!image.isNull());
  QCOMPARE(qRound(image.width() / image.devicePixelRatio()), descriptions.width());
}

QTEST_MAIN(DescriptionsTest)
#include "tst_descriptions.moc"
