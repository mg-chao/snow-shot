#include <QAccessible>
#include <QDir>
#include <QHBoxLayout>
#include <QImage>
#include <QPainter>
#include <QRadioButton>
#include <QSet>
#include <QSignalSpy>
#include <QVBoxLayout>
#include <QtTest>

#include "antd_icons.h"
#include "theme/theme.h"
#include "widgets/segmented.h"

using adqt::widgets::AdSegmented;

namespace {

AdSegmented::Option makeOption(const QString& value, const QString& label = QString(),
                               bool enabled = true) {
  AdSegmented::Option option;
  option.value = value;
  option.label = label.isNull() ? value : label;
  option.enabled = enabled;
  return option;
}

QList<QRadioButton*> optionButtons(AdSegmented* segmented) {
  QList<QRadioButton*> result = segmented->findChildren<QRadioButton*>(
      QStringLiteral("ad-segmented-item"), Qt::FindDirectChildrenOnly);
  std::sort(result.begin(), result.end(), [](const QRadioButton* lhs, const QRadioButton* rhs) {
    return lhs->property("optionIndex").toInt() < rhs->property("optionIndex").toInt();
  });
  return result;
}

}  // namespace

class SegmentedTest final : public QObject {
  Q_OBJECT

 private slots:
  void firstEnabledOptionBecomesSelected();
  void valuesAndSignalsFollowQtProperties();
  void dynamicOptionsKeepSelectionConsistent();
  void optionUpdatesHaveDeterministicSignals();
  void disabledOptionsAreSkippedByKeyboard();
  void focusFollowsSelectionChanges();
  void mouseActivationAndGlobalDisable();
  void propertiesAndOptionDataRoundTrip();
  void geometryFollowsOrientationDistributionAndRtl();
  void sizeHintsAndCustomizationHooks();
  void optionButtonsExposeRadioAccessibility();
  void rendersRepresentativeStates();
};

void SegmentedTest::firstEnabledOptionBecomesSelected() {
  AdSegmented segmented;
  QSignalSpy indexChanged(&segmented, &AdSegmented::currentIndexChanged);
  QSignalSpy valueChanged(&segmented, &AdSegmented::currentValueChanged);

  AdSegmented::Option disabled =
      makeOption(QStringLiteral("daily"), QStringLiteral("Daily"), false);
  QCOMPARE(segmented.addOption(disabled), 0);
  QCOMPARE(segmented.currentIndex(), -1);
  QVERIFY(!segmented.currentValue().isValid());
  QCOMPARE(indexChanged.count(), 0);
  QCOMPARE(valueChanged.count(), 0);

  QCOMPARE(segmented.addOption(QStringLiteral("Weekly")), 1);
  QCOMPARE(segmented.currentIndex(), 1);
  QCOMPARE(segmented.currentValue(), QVariant(QStringLiteral("Weekly")));
  QCOMPARE(indexChanged.count(), 1);
  QCOMPARE(valueChanged.count(), 1);
  QCOMPARE(segmented.addOption(QStringLiteral("Weekly")), -1);
  QCOMPARE(segmented.count(), 2);
  QCOMPARE(segmented.optionLabel(1), QStringLiteral("Weekly"));
  QCOMPARE(segmented.option(1).value, QVariant(QStringLiteral("Weekly")));
}

void SegmentedTest::valuesAndSignalsFollowQtProperties() {
  AdSegmented segmented;
  segmented.addOption(makeOption(QStringLiteral("map"), QStringLiteral("Map")));
  segmented.addOption(makeOption(QStringLiteral("transit"), QStringLiteral("Transit")));
  segmented.addOption(makeOption(QStringLiteral("satellite"), QStringLiteral("Satellite")));

  QSignalSpy indexChanged(&segmented, &AdSegmented::currentIndexChanged);
  QSignalSpy valueChanged(&segmented, &AdSegmented::currentValueChanged);
  QSignalSpy changed(&segmented, &AdSegmented::currentChanged);

  segmented.setCurrentValue(QStringLiteral("satellite"));
  QCOMPARE(segmented.currentIndex(), 2);
  QCOMPARE(indexChanged.count(), 1);
  QCOMPARE(valueChanged.count(), 1);
  QCOMPARE(changed.count(), 1);

  segmented.setCurrentIndex(1);
  QCOMPARE(segmented.currentValue(), QVariant(QStringLiteral("transit")));
  QCOMPARE(indexChanged.count(), 2);
  QCOMPARE(valueChanged.count(), 2);

  segmented.setCurrentIndex(99);
  QCOMPARE(segmented.currentIndex(), 1);
  segmented.setCurrentIndex(-1);
  QCOMPARE(segmented.currentIndex(), -1);
  QVERIFY(!segmented.currentValue().isValid());
  for (QRadioButton* button : optionButtons(&segmented)) {
    QVERIFY(!button->isChecked());
  }

  segmented.setCurrentValue(QStringLiteral("missing"));
  QCOMPARE(segmented.currentIndex(), -1);
  QCOMPARE(indexChanged.count(), 3);
  QCOMPARE(valueChanged.count(), 3);
}

void SegmentedTest::dynamicOptionsKeepSelectionConsistent() {
  AdSegmented segmented;
  segmented.addOption(QStringLiteral("Daily"));
  segmented.addOption(QStringLiteral("Weekly"));
  segmented.addOption(QStringLiteral("Monthly"));
  segmented.setCurrentValue(QStringLiteral("Weekly"));

  segmented.removeOption(1);
  QCOMPARE(segmented.currentIndex(), 1);
  QCOMPARE(segmented.currentValue(), QVariant(QStringLiteral("Monthly")));

  segmented.insertOption(1, makeOption(QStringLiteral("Weekly")));
  QCOMPARE(segmented.currentIndex(), 2);
  QCOMPARE(segmented.currentValue(), QVariant(QStringLiteral("Monthly")));

  segmented.setCurrentValue(QStringLiteral("Quarterly"));
  QCOMPARE(segmented.currentIndex(), 2);
  segmented.addOption(QStringLiteral("Quarterly"));
  QCOMPARE(segmented.currentIndex(), 2);
  QCOMPARE(segmented.currentValue(), QVariant(QStringLiteral("Monthly")));

  segmented.setOptions({makeOption(QStringLiteral("weekly"), QStringLiteral("Weekly"), false),
                        makeOption(QStringLiteral("quarterly"), QStringLiteral("Quarterly"))});
  QCOMPARE(segmented.currentIndex(), 1);
  QCOMPARE(segmented.currentValue(), QVariant(QStringLiteral("quarterly")));

  segmented.clear();
  QCOMPARE(segmented.count(), 0);
  QCOMPARE(segmented.currentIndex(), -1);
  QVERIFY(!segmented.currentValue().isValid());
}

void SegmentedTest::optionUpdatesHaveDeterministicSignals() {
  AdSegmented segmented;
  const QList<AdSegmented::Option> options = {
      makeOption(QStringLiteral("daily"), QStringLiteral("Daily")),
      makeOption(QStringLiteral("weekly"), QStringLiteral("Weekly")),
      makeOption(QStringLiteral("monthly"), QStringLiteral("Monthly"))};
  segmented.setOptions(options);
  segmented.setCurrentIndex(1);

  QSignalSpy optionsChanged(&segmented, &AdSegmented::optionsChanged);
  QSignalSpy indexChanged(&segmented, &AdSegmented::currentIndexChanged);
  QSignalSpy valueChanged(&segmented, &AdSegmented::currentValueChanged);
  QSignalSpy changed(&segmented, &AdSegmented::currentChanged);

  segmented.setOptions(options);
  QCOMPARE(optionsChanged.count(), 0);
  QCOMPARE(indexChanged.count(), 0);
  QCOMPARE(valueChanged.count(), 0);

  segmented.setOptionEnabled(1, false);
  QCOMPARE(segmented.currentIndex(), 2);
  QCOMPARE(segmented.currentValue(), QVariant(QStringLiteral("monthly")));
  QCOMPARE(optionsChanged.count(), 1);
  QCOMPARE(indexChanged.count(), 1);
  QCOMPARE(valueChanged.count(), 1);
  QCOMPARE(changed.count(), 1);

  segmented.setOptionEnabled(2, false);
  QCOMPARE(segmented.currentIndex(), 0);
  QCOMPARE(segmented.currentValue(), QVariant(QStringLiteral("daily")));
  QCOMPARE(optionsChanged.count(), 2);
  QCOMPARE(indexChanged.count(), 2);
  QCOMPARE(valueChanged.count(), 2);

  segmented.setOptionEnabled(0, false);
  QCOMPARE(segmented.currentIndex(), -1);
  QVERIFY(!segmented.currentValue().isValid());
  segmented.setCurrentIndex(1);
  QCOMPARE(segmented.currentIndex(), -1);

  segmented.setOptionEnabled(1, true);
  QCOMPARE(segmented.currentIndex(), 1);
  QCOMPARE(segmented.currentValue(), QVariant(QStringLiteral("weekly")));
}

void SegmentedTest::disabledOptionsAreSkippedByKeyboard() {
  AdSegmented segmented;
  segmented.addOption(makeOption(QStringLiteral("daily"), QStringLiteral("Daily")));
  segmented.addOption(makeOption(QStringLiteral("weekly"), QStringLiteral("Weekly"), false));
  segmented.addOption(makeOption(QStringLiteral("monthly"), QStringLiteral("Monthly")));
  segmented.show();
  QVERIFY(QTest::qWaitForWindowExposed(&segmented));

  const QList<QRadioButton*> buttons = optionButtons(&segmented);
  QCOMPARE(buttons.size(), 3);
  buttons.at(0)->setFocus(Qt::TabFocusReason);
  QSignalSpy activated(&segmented, &AdSegmented::activated);
  QTest::keyClick(buttons.at(0), Qt::Key_Right);
  QCOMPARE(segmented.currentIndex(), 2);
  QCOMPARE(activated.count(), 1);

  QTest::keyClick(buttons.at(2), Qt::Key_Right);
  QCOMPARE(segmented.currentIndex(), 0);
  QTest::keyClick(buttons.at(0), Qt::Key_End);
  QCOMPARE(segmented.currentIndex(), 2);
  QTest::keyClick(buttons.at(2), Qt::Key_Home);
  QCOMPARE(segmented.currentIndex(), 0);
}

void SegmentedTest::focusFollowsSelectionChanges() {
  AdSegmented segmented;
  segmented.addOption(makeOption(QStringLiteral("daily"), QStringLiteral("Daily")));
  segmented.addOption(makeOption(QStringLiteral("weekly"), QStringLiteral("Weekly")));
  segmented.addOption(makeOption(QStringLiteral("monthly"), QStringLiteral("Monthly")));
  segmented.setCurrentIndex(1);
  segmented.show();
  QVERIFY(QTest::qWaitForWindowExposed(&segmented));

  QList<QRadioButton*> buttons = optionButtons(&segmented);
  buttons.at(1)->setFocus(Qt::TabFocusReason);
  QVERIFY(buttons.at(1)->hasFocus());

  segmented.setOptionEnabled(1, false);
  buttons = optionButtons(&segmented);
  QCOMPARE(segmented.currentIndex(), 2);
  QVERIFY(buttons.at(2)->hasFocus());

  segmented.removeOption(2);
  QCoreApplication::processEvents();
  buttons = optionButtons(&segmented);
  QCOMPARE(segmented.currentIndex(), 0);
  QVERIFY(buttons.at(0)->hasFocus());

  segmented.insertOption(0, makeOption(QStringLiteral("hourly"), QStringLiteral("Hourly")));
  QCoreApplication::processEvents();
  buttons = optionButtons(&segmented);
  QCOMPARE(segmented.currentIndex(), 1);
  QCOMPARE(segmented.currentValue(), QVariant(QStringLiteral("daily")));
  QVERIFY(buttons.at(1)->hasFocus());

  AdSegmented single;
  single.addOption(QStringLiteral("Only"));
  single.show();
  QVERIFY(QTest::qWaitForWindowExposed(&single));
  QSignalSpy activated(&single, &AdSegmented::activated);
  QRadioButton* onlyButton = optionButtons(&single).first();
  onlyButton->setFocus(Qt::TabFocusReason);
  QTest::keyClick(onlyButton, Qt::Key_Right);
  QCOMPARE(activated.count(), 0);
}

void SegmentedTest::mouseActivationAndGlobalDisable() {
  AdSegmented segmented;
  segmented.addOption(QStringLiteral("List"));
  segmented.addOption(QStringLiteral("Kanban"));
  segmented.show();
  QVERIFY(QTest::qWaitForWindowExposed(&segmented));
  const QList<QRadioButton*> buttons = optionButtons(&segmented);
  QCOMPARE(buttons.size(), 2);

  QSignalSpy activated(&segmented, &AdSegmented::activated);
  QTest::mouseClick(buttons.at(1), Qt::LeftButton);
  QCOMPARE(segmented.currentIndex(), 1);
  QCOMPARE(activated.count(), 1);

  segmented.setEnabled(false);
  QTest::mouseClick(buttons.at(0), Qt::LeftButton);
  QCOMPARE(segmented.currentIndex(), 1);
  QCOMPARE(activated.count(), 1);
}

void SegmentedTest::propertiesAndOptionDataRoundTrip() {
  adqt::icons::antd::ensureRegistered();
  AdSegmented segmented;
  AdSegmented::Option option = makeOption(QStringLiteral("list"), QStringLiteral("List"));
  option.icon = adqt::icons::antd::outlined::Bars();
  option.tooltip = QStringLiteral("List view");
  option.data = 42;
  segmented.addOption(option);

  segmented.setControlSize(AdSegmented::ControlSize::Large);
  segmented.setOrientation(Qt::Vertical);
  segmented.setDistribution(AdSegmented::Distribution::Fill);
  segmented.setShape(AdSegmented::Shape::Round);
  segmented.setAnimated(false);
  QCOMPARE(segmented.controlSize(), AdSegmented::ControlSize::Large);
  QCOMPARE(segmented.orientation(), Qt::Vertical);
  QCOMPARE(segmented.distribution(), AdSegmented::Distribution::Fill);
  QCOMPARE(segmented.shape(), AdSegmented::Shape::Round);
  QVERIFY(!segmented.animated());
  QCOMPARE(segmented.optionTooltip(0), QStringLiteral("List view"));
  QCOMPARE(segmented.optionData(0), QVariant(42));
  QVERIFY(adqt::icons::isValid(segmented.optionIcon(0)));

  segmented.setOptionLabel(0, QStringLiteral("Rows"));
  segmented.setOptionTooltip(0, QStringLiteral("Row view"));
  segmented.setOptionData(0, QStringLiteral("rows"));
  segmented.setOptionEnabled(0, false);
  QCOMPARE(segmented.optionLabel(0), QStringLiteral("Rows"));
  QCOMPARE(segmented.optionTooltip(0), QStringLiteral("Row view"));
  QCOMPARE(segmented.optionData(0), QVariant(QStringLiteral("rows")));
  QVERIFY(!segmented.isOptionEnabled(0));
}

void SegmentedTest::geometryFollowsOrientationDistributionAndRtl() {
  AdSegmented segmented;
  segmented.addOption(QStringLiteral("Daily"));
  segmented.addOption(QStringLiteral("Weekly"));
  segmented.addOption(QStringLiteral("Monthly"));
  segmented.setDistribution(AdSegmented::Distribution::Fill);
  segmented.resize(480, segmented.sizeHint().height());
  segmented.show();
  QVERIFY(QTest::qWaitForWindowExposed(&segmented));
  QCoreApplication::processEvents();

  const QRect first = segmented.optionRect(0);
  const QRect second = segmented.optionRect(1);
  const QRect third = segmented.optionRect(2);
  QVERIFY(first.width() > 100);
  QVERIFY(std::abs(first.width() - second.width()) <= 1);
  QVERIFY(std::abs(second.width() - third.width()) <= 1);
  QVERIFY(first.left() < second.left());

  segmented.setLayoutDirection(Qt::RightToLeft);
  QCoreApplication::processEvents();
  QVERIFY(segmented.optionRect(0).left() > segmented.optionRect(1).left());

  segmented.setOrientation(Qt::Vertical);
  segmented.resize(220, 240);
  QCoreApplication::processEvents();
  QVERIFY(segmented.optionRect(0).top() < segmented.optionRect(1).top());
  QCOMPARE(segmented.optionAt(segmented.optionRect(2).center()), 2);
}

void SegmentedTest::sizeHintsAndCustomizationHooks() {
  AdSegmented segmented;
  segmented.addOption(QStringLiteral("Daily"));
  segmented.addOption(QStringLiteral("Weekly"));
  const adqt::theme::ThemeMapToken theme =
      adqt::theme::ThemeManager::instance().resolveTheme(&segmented);
  const QSize medium = segmented.sizeHint();
  QCOMPARE(medium.height(), qRound(theme.controlHeight));
  segmented.setControlSize(AdSegmented::ControlSize::Small);
  const QSize small = segmented.sizeHint();
  QCOMPARE(small.height(), qRound(theme.controlHeightSM));
  segmented.setControlSize(AdSegmented::ControlSize::Large);
  const QSize large = segmented.sizeHint();
  QCOMPARE(large.height(), qRound(theme.controlHeightLG));
  QVERIFY(small.height() < medium.height());
  QVERIFY(large.height() > medium.height());

  AdSegmented::ComponentTokens tokens;
  tokens.metrics.horizontalPadding = 30;
  tokens.metrics.trackPadding = 4;
  tokens.colors.trackBackground = QColor(QStringLiteral("#ffe7ba"));
  QSignalSpy tokenChanges(&segmented, &AdSegmented::componentTokensChanged);
  segmented.setComponentTokens(tokens);
  segmented.setComponentTokens(tokens);
  QCOMPARE(tokenChanges.count(), 1);
  QVERIFY(segmented.sizeHint().width() > large.width());

  segmented.setItemSizeHintCallback([](const AdSegmented::Option&, AdSegmented::ControlSize,
                                       const QFont&) { return QSize(120, 56); });
  QCOMPARE(segmented.sizeHint().height(), 64);
  QVERIFY(segmented.sizeHint().width() >= 248);

  int paintCount = 0;
  QColor paintedForeground;
  int paintedFontPixels = 0;
  AdSegmented::SemanticStyles semanticStyles;
  semanticStyles.root.textColor = QColor(QStringLiteral("#722ed1"));
  QFont semanticFont = segmented.font();
  semanticFont.setPixelSize(19);
  semanticStyles.root.font = semanticFont;
  QSignalSpy semanticChanges(&segmented, &AdSegmented::semanticStylesChanged);
  segmented.setSemanticStyles(semanticStyles);
  segmented.setSemanticStyles(semanticStyles);
  QCOMPARE(semanticChanges.count(), 1);
  segmented.setItemPaintCallback([&paintCount, &paintedForeground, &paintedFontPixels](
                                     QPainter& painter, const AdSegmented::ItemPaintInfo& info) {
    ++paintCount;
    paintedForeground = info.foreground;
    paintedFontPixels = info.font.pixelSize();
    painter.setPen(info.foreground);
    painter.drawText(info.contentRect, Qt::AlignCenter, info.option.label.toUpper());
  });
  segmented.show();
  QVERIFY(QTest::qWaitForWindowExposed(&segmented));
  const QImage image = segmented.grab().toImage();
  QVERIFY(!image.isNull());
  QVERIFY(paintCount >= 2);
  QCOMPARE(paintedForeground, QColor(QStringLiteral("#722ed1")));
  QCOMPARE(paintedFontPixels, 19);
}

void SegmentedTest::optionButtonsExposeRadioAccessibility() {
  AdSegmented segmented;
  segmented.addOption(QStringLiteral("Daily"));
  segmented.addOption(QStringLiteral("Weekly"));
  const QList<QRadioButton*> buttons = optionButtons(&segmented);
  QCOMPARE(buttons.size(), 2);
  QAccessibleInterface* interface = QAccessible::queryAccessibleInterface(buttons.first());
  QVERIFY(interface != nullptr);
  QCOMPARE(interface->role(), QAccessible::RadioButton);
  QCOMPARE(interface->text(QAccessible::Name), QStringLiteral("Daily"));
  QVERIFY(interface->state().checked);
}

void SegmentedTest::rendersRepresentativeStates() {
  adqt::icons::antd::ensureRegistered();
  auto& themeManager = adqt::theme::ThemeManager::instance();
  const adqt::theme::ThemeConfig originalConfig = themeManager.config();
  themeManager.applyTo(*qApp);

  QWidget showcase;
  showcase.resize(760, 420);
  auto* layout = new QVBoxLayout(&showcase);
  layout->setContentsMargins(24, 24, 24, 24);
  layout->setSpacing(20);

  auto* basic = new AdSegmented;
  basic->addOption(QStringLiteral("Daily"));
  basic->addOption(QStringLiteral("Weekly"));
  basic->addOption(QStringLiteral("Monthly"));
  basic->setCurrentIndex(1);
  auto* icons = new AdSegmented;
  AdSegmented::Option list = makeOption(QStringLiteral("list"), QStringLiteral("List"));
  list.icon = adqt::icons::antd::outlined::Bars();
  AdSegmented::Option kanban = makeOption(QStringLiteral("kanban"), QStringLiteral("Kanban"));
  kanban.icon = adqt::icons::antd::outlined::Appstore();
  icons->addOption(list);
  icons->addOption(kanban);
  icons->setShape(AdSegmented::Shape::Round);
  auto* disabled = new AdSegmented;
  disabled->addOption(QStringLiteral("Map"));
  disabled->addOption(QStringLiteral("Transit"));
  disabled->addOption(QStringLiteral("Satellite"));
  disabled->setEnabled(false);
  auto* vertical = new AdSegmented;
  vertical->setOrientation(Qt::Vertical);
  vertical->addOption(QStringLiteral("Overview"));
  vertical->addOption(makeOption(QStringLiteral("details"), QStringLiteral("Details"), false));
  vertical->addOption(QStringLiteral("Activity"));

  layout->addWidget(basic, 0, Qt::AlignLeft);
  layout->addWidget(icons, 0, Qt::AlignLeft);
  layout->addWidget(disabled, 0, Qt::AlignLeft);
  layout->addWidget(vertical, 0, Qt::AlignLeft);
  layout->addStretch();

  const QString snapshotDirectory = qEnvironmentVariable("ADQT_SEGMENTED_SNAPSHOT_DIR");
  auto renderScheme = [&](adqt::theme::ThemeScheme scheme, const QString& fileName) {
    themeManager.setPreset(scheme, adqt::theme::ThemeDensity::Comfortable);
    const adqt::theme::ThemeMapToken colors = themeManager.resolveTheme(&showcase);
    QPalette palette = showcase.palette();
    palette.setColor(QPalette::Window, colors.colorBgContainer);
    palette.setColor(QPalette::WindowText, colors.colorText);
    showcase.setPalette(palette);
    showcase.setAutoFillBackground(true);
    showcase.show();
    QVERIFY(QTest::qWaitForWindowExposed(&showcase));
    QCoreApplication::processEvents();
    const QImage image = showcase.grab().toImage().convertToFormat(QImage::Format_ARGB32);
    QCOMPARE(image.size(), showcase.size());
    QSet<QRgb> colorsFound;
    for (int y = 0; y < image.height(); y += 6) {
      for (int x = 0; x < image.width(); x += 6) {
        colorsFound.insert(image.pixel(x, y));
      }
    }
    QVERIFY2(colorsFound.size() > 12,
             "Segmented rendering should contain track, thumb, text, icon, and disabled colors");
    if (!snapshotDirectory.isEmpty()) {
      QDir().mkpath(snapshotDirectory);
      QVERIFY(image.save(QDir(snapshotDirectory).filePath(fileName)));
    }
  };

  renderScheme(adqt::theme::ThemeScheme::Light, QStringLiteral("segmented-light.png"));
  renderScheme(adqt::theme::ThemeScheme::Dark, QStringLiteral("segmented-dark.png"));
  themeManager.setConfig(originalConfig);
}

QTEST_MAIN(SegmentedTest)

#include "tst_segmented.moc"
