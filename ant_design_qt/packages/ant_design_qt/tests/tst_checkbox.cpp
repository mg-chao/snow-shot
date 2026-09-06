#include <QAccessible>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QImage>
#include <QPainter>
#include <QSignalSpy>
#include <QWidget>
#include <QtTest>

#include "widgets/checkbox.h"
#include "widgets/checkbox_group.h"

using adqt::widgets::AdCheckbox;
using adqt::widgets::AdCheckboxGroup;

namespace {

QImage renderIndicator(Qt::CheckState state, bool enabled = true) {
  AdCheckbox checkbox;
  checkbox.setCheckState(state);
  checkbox.setEnabled(enabled);
  checkbox.ensurePolished();
  checkbox.resize(32, 32);

  QImage image(checkbox.size(), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::white);
  QPainter painter(&image);
  checkbox.render(&painter);
  painter.end();
  return image;
}

int differingPixelCount(const QImage& lhs, const QImage& rhs) {
  if (lhs.size() != rhs.size() || lhs.format() != rhs.format()) {
    return -1;
  }
  int count = 0;
  for (int y = 0; y < lhs.height(); ++y) {
    const QRgb* lhsLine = reinterpret_cast<const QRgb*>(lhs.constScanLine(y));
    const QRgb* rhsLine = reinterpret_cast<const QRgb*>(rhs.constScanLine(y));
    for (int x = 0; x < lhs.width(); ++x) {
      if (lhsLine[x] != rhsLine[x]) {
        ++count;
      }
    }
  }
  return count;
}

}  // namespace

class CheckboxTests final : public QObject {
  Q_OBJECT

 private slots:
  void standaloneDefaultsAndToggle();
  void indeterminateUsesNativePartialState();
  void nativePartialStateIsReportedAsIndeterminate();
  void groupKeepsRegistrationOrder();
  void groupEmitsOneChangePerUpdate();
  void groupEnabledStateBlocksInteraction();
  void groupTracksRemovalAndDestruction();
  void standardButtonGroupApiStaysTyped();
  void groupExcludesPartialStateFromValues();
  void optionValueCanChangeAfterRegistration();
  void removedValuesAreFiltered();
  void checkboxTokensOverrideGroupTokens();
  void childTokensRefreshManagedLayout();
  void propertySettersAreIdempotent();
  void explicitCursorIsPreserved();
  void nativeAccessibilityReportsMixedState();
  void visualStateImagesAreDistinct();
  void visualStatesRender();
};

void CheckboxTests::standaloneDefaultsAndToggle() {
  AdCheckbox checkbox(QStringLiteral("Remember me"));
  QVERIFY(!checkbox.isChecked());
  QVERIFY(!checkbox.isIndeterminate());
  QCOMPARE(checkbox.focusPolicy(), Qt::StrongFocus);
  QVERIFY(checkbox.sizeHint().width() > checkbox.sizeHint().height());

  QSignalSpy toggled(&checkbox, &QAbstractButton::toggled);
  checkbox.click();
  QVERIFY(checkbox.isChecked());
  QCOMPARE(toggled.count(), 1);
}

void CheckboxTests::indeterminateUsesNativePartialState() {
  AdCheckbox checkbox;
  QSignalSpy changed(&checkbox, &AdCheckbox::indeterminateChanged);

  checkbox.setIndeterminate(true);
  QVERIFY(checkbox.isIndeterminate());
  QCOMPARE(checkbox.checkState(), Qt::PartiallyChecked);
  QCOMPARE(changed.count(), 1);

  checkbox.setChecked(true);
  QVERIFY(!checkbox.isIndeterminate());
  QVERIFY(checkbox.isChecked());

  checkbox.setIndeterminate(true);
  checkbox.setIndeterminate(false);
  QVERIFY(!checkbox.isIndeterminate());
  QCOMPARE(checkbox.checkState(), Qt::Unchecked);
  QCOMPARE(changed.count(), 4);
}

void CheckboxTests::nativePartialStateIsReportedAsIndeterminate() {
  AdCheckbox checkbox;
  QSignalSpy changed(&checkbox, &AdCheckbox::indeterminateChanged);

  checkbox.setCheckState(Qt::PartiallyChecked);
  QVERIFY(checkbox.isIndeterminate());
  QCOMPARE(changed.count(), 1);
  QCOMPARE(changed.takeFirst().at(0).toBool(), true);

  checkbox.setCheckState(Qt::Unchecked);
  QVERIFY(!checkbox.isIndeterminate());
  QCOMPARE(changed.count(), 1);
  QCOMPARE(changed.takeFirst().at(0).toBool(), false);
}

void CheckboxTests::groupKeepsRegistrationOrder() {
  AdCheckbox first(QStringLiteral("Apple"));
  AdCheckbox second(QStringLiteral("Pear"));
  AdCheckbox third(QStringLiteral("Orange"));
  AdCheckboxGroup group;
  group.addCheckbox(&first, QStringLiteral("apple"));
  group.addCheckbox(&second, 2);
  group.addCheckbox(&third, true);

  group.setValues({true, QStringLiteral("apple"), 2});
  QCOMPARE(group.values(), QVariantList({QStringLiteral("apple"), 2, true}));
  QVERIFY(first.isChecked());
  QVERIFY(second.isChecked());
  QVERIFY(third.isChecked());
}

void CheckboxTests::groupEmitsOneChangePerUpdate() {
  AdCheckbox first(QStringLiteral("A"));
  AdCheckbox second(QStringLiteral("B"));
  AdCheckboxGroup group;
  group.addCheckbox(&first, QStringLiteral("a"));
  group.addCheckbox(&second, QStringLiteral("b"));
  QSignalSpy changed(&group, &AdCheckboxGroup::valuesChanged);

  group.setValues({QStringLiteral("a"), QStringLiteral("b")});
  QCOMPARE(changed.count(), 1);
  QCOMPARE(changed.takeFirst().at(0).toList(),
           QVariantList({QStringLiteral("a"), QStringLiteral("b")}));

  first.click();
  QCOMPARE(changed.count(), 1);
  QCOMPARE(group.values(), QVariantList({QStringLiteral("b")}));

  group.setValues(group.values());
  QCOMPARE(changed.count(), 1);
}

void CheckboxTests::groupEnabledStateBlocksInteraction() {
  QWidget window;
  QHBoxLayout layout(&window);
  auto* checkbox = new AdCheckbox(QStringLiteral("Blocked"), &window);
  layout.addWidget(checkbox);
  AdCheckboxGroup group(&window);
  group.addCheckbox(checkbox, QStringLiteral("blocked"));
  QVERIFY(group.isEnabled());
  group.setEnabled(false);
  QVERIFY(!checkbox->isEnabled());

  window.show();
  QTest::qWait(10);
  QTest::mouseClick(checkbox, Qt::LeftButton);
  QVERIFY(!checkbox->isChecked());

  group.setEnabled(true);
  QVERIFY(checkbox->isEnabled());
  QTest::mouseClick(checkbox, Qt::LeftButton);
  QVERIFY(checkbox->isChecked());

  checkbox->setEnabled(false);
  group.setEnabled(false);
  group.setEnabled(true);
  QVERIFY(!checkbox->isEnabled());
}

void CheckboxTests::groupTracksRemovalAndDestruction() {
  AdCheckboxGroup group;
  auto* checkbox = new AdCheckbox(QStringLiteral("Transient"));
  group.addCheckbox(checkbox, 7);
  checkbox->setChecked(true);
  QSignalSpy changed(&group, &AdCheckboxGroup::valuesChanged);

  delete checkbox;
  QCOMPARE(group.checkboxes().size(), 0);
  QCOMPARE(group.values(), QVariantList{});
  QCOMPARE(changed.count(), 1);
  QCOMPARE(changed.takeFirst().at(0).toList(), QVariantList{});

  AdCheckbox reusable(QStringLiteral("Reusable"));
  group.addCheckbox(&reusable, 9);
  group.removeCheckbox(&reusable);
  group.addCheckbox(&reusable, 9);
  changed.clear();
  reusable.setChecked(true);
  QCOMPARE(changed.count(), 1);
}

void CheckboxTests::standardButtonGroupApiStaysTyped() {
  AdCheckbox checkbox(QStringLiteral("Typed"));
  AdCheckboxGroup group;
  group.addButton(&checkbox, 42);

  QCOMPARE(group.checkboxes(), QList<AdCheckbox*>{&checkbox});
  QCOMPARE(group.id(&checkbox), 42);
  checkbox.setChecked(true);
  QCOMPARE(group.values(), QVariantList{QVariant(QStringLiteral("Typed"))});

  group.removeButton(&checkbox);
  QVERIFY(group.checkboxes().isEmpty());
  QVERIFY(!group.buttons().contains(&checkbox));

  QCheckBox plain;
  group.addButton(&plain);
  QVERIFY(!group.buttons().contains(&plain));
}

void CheckboxTests::groupExcludesPartialStateFromValues() {
  AdCheckbox checkbox(QStringLiteral("Partial"));
  AdCheckboxGroup group;
  group.addCheckbox(&checkbox, QStringLiteral("partial"));
  checkbox.setCheckState(Qt::PartiallyChecked);
  QCOMPARE(group.values(), QVariantList{});
}

void CheckboxTests::optionValueCanChangeAfterRegistration() {
  AdCheckbox checkbox(QStringLiteral("Option"));
  AdCheckboxGroup group;
  group.addCheckbox(&checkbox, 1);
  checkbox.setChecked(true);
  QSignalSpy changed(&group, &AdCheckboxGroup::valuesChanged);

  checkbox.setValue(QStringLiteral("updated"));
  QCOMPARE(group.value(&checkbox), QVariant(QStringLiteral("updated")));
  QCOMPARE(group.values(), QVariantList({QStringLiteral("updated")}));
  QCOMPARE(changed.count(), 1);
}

void CheckboxTests::removedValuesAreFiltered() {
  AdCheckbox first(QStringLiteral("First"));
  AdCheckbox second(QStringLiteral("Second"));
  AdCheckboxGroup group;
  group.addCheckbox(&first, 1);
  group.addCheckbox(&second, 2);

  group.setValues({1, 2});
  QSignalSpy changed(&group, &AdCheckboxGroup::valuesChanged);
  group.removeCheckbox(&first);
  QCOMPARE(group.values(), QVariantList{QVariant(2)});
  QCOMPARE(changed.count(), 1);
}

void CheckboxTests::checkboxTokensOverrideGroupTokens() {
  AdCheckbox checkbox(QStringLiteral("Sized"));
  AdCheckboxGroup group;
  group.addCheckbox(&checkbox, 1);
  const int baseline = checkbox.sizeHint().height();

  AdCheckbox::ComponentTokens groupTokens;
  groupTokens.metrics.checkboxSize = baseline + 8;
  group.setComponentTokens(groupTokens);
  QCOMPARE(checkbox.sizeHint().height(), baseline + 8);

  AdCheckbox::ComponentTokens localTokens;
  localTokens.metrics.checkboxSize = baseline + 12;
  checkbox.setComponentTokens(localTokens);
  QCOMPARE(checkbox.sizeHint().height(), baseline + 12);
}

void CheckboxTests::childTokensRefreshManagedLayout() {
  QWidget host;
  QHBoxLayout layout(&host);
  AdCheckbox checkbox(QStringLiteral("Spaced"), &host);
  layout.addWidget(&checkbox);
  AdCheckboxGroup group(&host);
  group.setManagedLayout(&layout);
  group.addCheckbox(&checkbox, 1);

  AdCheckbox::ComponentTokens tokens;
  tokens.metrics.wrapperMarginInlineEnd = 37;
  checkbox.setComponentTokens(tokens);
  QCOMPARE(layout.spacing(), 37);
}

void CheckboxTests::propertySettersAreIdempotent() {
  AdCheckbox checkbox;
  AdCheckboxGroup group;
  QSignalSpy checkboxTokensChanged(&checkbox, &AdCheckbox::componentTokensChanged);
  QSignalSpy groupTokensChanged(&group, &AdCheckboxGroup::componentTokensChanged);
  QSignalSpy enabledChanged(&group, &AdCheckboxGroup::enabledChanged);

  checkbox.setComponentTokens({});
  checkbox.resetComponentTokenResolver();
  group.setComponentTokens({});
  group.resetComponentTokenResolver();
  group.setEnabled(true);

  QCOMPARE(checkboxTokensChanged.count(), 0);
  QCOMPARE(groupTokensChanged.count(), 0);
  QCOMPARE(enabledChanged.count(), 0);
}

void CheckboxTests::explicitCursorIsPreserved() {
  AdCheckbox checkbox;
  QCOMPARE(checkbox.cursor().shape(), Qt::PointingHandCursor);

  checkbox.setCursor(Qt::CrossCursor);
  checkbox.setEnabled(false);
  checkbox.setEnabled(true);
  QCOMPARE(checkbox.cursor().shape(), Qt::CrossCursor);

  checkbox.unsetCursor();
  QCoreApplication::processEvents();
  QCOMPARE(checkbox.cursor().shape(), Qt::PointingHandCursor);
}

void CheckboxTests::nativeAccessibilityReportsMixedState() {
  AdCheckbox checkbox(QStringLiteral("Select all"));
  checkbox.setIndeterminate(true);
  QAccessibleInterface* interface = QAccessible::queryAccessibleInterface(&checkbox);
  QVERIFY(interface != nullptr);
  QCOMPARE(interface->role(), QAccessible::CheckBox);
  QCOMPARE(interface->text(QAccessible::Name), QStringLiteral("Select all"));
  QVERIFY(interface->state().checkStateMixed);
}

void CheckboxTests::visualStateImagesAreDistinct() {
  const QImage unchecked = renderIndicator(Qt::Unchecked);
  const QImage checked = renderIndicator(Qt::Checked);
  const QImage partial = renderIndicator(Qt::PartiallyChecked);
  const QImage disabled = renderIndicator(Qt::Unchecked, false);

  QVERIFY(differingPixelCount(unchecked, checked) > 30);
  QVERIFY(differingPixelCount(checked, partial) > 15);
  QVERIFY(differingPixelCount(unchecked, partial) > 15);
  QVERIFY(differingPixelCount(unchecked, disabled) > 15);
}

void CheckboxTests::visualStatesRender() {
  QWidget gallery;
  gallery.setAutoFillBackground(true);
  QPalette galleryPalette = gallery.palette();
  galleryPalette.setColor(QPalette::Window, Qt::white);
  gallery.setPalette(galleryPalette);
  auto* layout = new QGridLayout(&gallery);

  auto add = [layout, &gallery](int row, int column, const QString& label, bool checked,
                                bool indeterminate, bool enabled = true) {
    auto* checkbox = new AdCheckbox(label, &gallery);
    checkbox->setChecked(checked);
    checkbox->setIndeterminate(indeterminate);
    checkbox->setEnabled(enabled);
    layout->addWidget(checkbox, row, column);
    return checkbox;
  };

  add(0, 0, QStringLiteral("Unchecked"), false, false);
  add(0, 1, QStringLiteral("Checked"), true, false);
  add(1, 0, QStringLiteral("Indeterminate"), false, true);
  add(1, 1, QStringLiteral("Indeterminate checked input"), true, true);
  add(2, 0, QStringLiteral("Disabled"), false, false, false);
  add(2, 1, QStringLiteral("Disabled checked"), true, false, false);
  AdCheckbox* rtl = add(3, 0, QStringLiteral("Right to left"), true, false);
  rtl->setLayoutDirection(Qt::RightToLeft);

  gallery.setFixedSize(520, 220);
  gallery.show();
  QTest::qWait(20);

  QImage image(gallery.size() * gallery.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
  image.setDevicePixelRatio(gallery.devicePixelRatioF());
  image.fill(Qt::white);
  QPainter painter(&image);
  gallery.render(&painter);
  painter.end();

  int nonWhitePixels = 0;
  for (int y = 0; y < image.height(); ++y) {
    const QRgb* scanline = reinterpret_cast<const QRgb*>(image.constScanLine(y));
    for (int x = 0; x < image.width(); ++x) {
      if (qRed(scanline[x]) < 245 || qGreen(scanline[x]) < 245 || qBlue(scanline[x]) < 245) {
        ++nonWhitePixels;
      }
    }
  }
  QVERIFY(nonWhitePixels > 500);

  const QString outputPath = qEnvironmentVariable("ADQT_CHECKBOX_SCREENSHOT");
  if (!outputPath.isEmpty()) {
    QVERIFY2(image.save(outputPath),
             qPrintable(QStringLiteral("Could not save %1").arg(outputPath)));
  }
}

QTEST_MAIN(CheckboxTests)

#include "tst_checkbox.moc"
