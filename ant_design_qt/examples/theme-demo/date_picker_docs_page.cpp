#include "date_picker_docs_page.h"

#include "antd_icons.h"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QRectF>
#include <QVBoxLayout>

#include <utility>

using adqt::widgets::AdDatePicker;
using adqt::widgets::AdDatePickerPanel;
using adqt::widgets::AdDateRangePicker;
using adqt::widgets::AdPopupLayerMode;

namespace outlined_icons = adqt::icons::antd::outlined;

namespace {

QFrame* makeDemoPanel() {
  auto* panel = new QFrame();
  panel->setFrameShape(QFrame::StyledPanel);
  auto* layout = new QVBoxLayout(panel);
  layout->setContentsMargins(14, 14, 14, 14);
  layout->setSpacing(10);
  return panel;
}

QLabel* makeValueLabel(const QString& text = QString()) {
  auto* label = new QLabel(text);
  label->setMinimumWidth(180);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  return label;
}

QString dateText(const QDate& value) {
  return value.isValid() ? value.toString(QStringLiteral("yyyy-MM-dd")) : QStringLiteral("(empty)");
}

QString rangeText(const QDate& start, const QDate& end) {
  return QStringLiteral("%1 -> %2").arg(dateText(start), dateText(end));
}

QString pickerModeText(AdDatePickerPanel::PickerMode mode) {
  switch (mode) {
    case AdDatePickerPanel::PickerMode::Week:
      return QStringLiteral("week");
    case AdDatePickerPanel::PickerMode::Month:
      return QStringLiteral("month");
    case AdDatePickerPanel::PickerMode::Quarter:
      return QStringLiteral("quarter");
    case AdDatePickerPanel::PickerMode::Year:
      return QStringLiteral("year");
    case AdDatePickerPanel::PickerMode::Decade:
      return QStringLiteral("decade");
    case AdDatePickerPanel::PickerMode::Time:
      return QStringLiteral("time");
    case AdDatePickerPanel::PickerMode::Date:
    default:
      return QStringLiteral("date");
  }
}

QString datesText(const QVector<QDate>& values) {
  if (values.isEmpty()) {
    return QStringLiteral("(empty)");
  }
  QStringList parts;
  parts.reserve(values.size());
  for (const QDate& value : values) {
    parts.append(dateText(value));
  }
  return parts.join(QStringLiteral(", "));
}

QString dateTimeText(const QDateTime& value) {
  return value.isValid() ? value.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                         : QStringLiteral("(empty)");
}

QString rangeDateTimeText(const QDateTime& start, const QDateTime& end) {
  return QStringLiteral("%1 -> %2").arg(dateTimeText(start), dateTimeText(end));
}

QString timeText(const QTime& value, const QString& format = QStringLiteral("HH:mm:ss")) {
  return value.isValid() ? value.toString(format) : QStringLiteral("(empty)");
}

QString timeRangeText(const QTime& start, const QTime& end,
                      const QString& format = QStringLiteral("HH:mm:ss")) {
  return QStringLiteral("%1 -> %2").arg(timeText(start, format), timeText(end, format));
}

void addLabeledRow(QGridLayout* grid, int row, const QString& label, QWidget* widget) {
  auto* text = new QLabel(label);
  text->setMinimumWidth(96);
  grid->addWidget(text, row, 0, Qt::AlignTop);
  grid->addWidget(widget, row, 1);
}

AdDatePicker* makePicker(const QDate& date = QDate()) {
  auto* picker = new AdDatePicker();
  picker->setDate(date);
  picker->setAllowClear(true);
  return picker;
}

AdDatePickerPanel::CellRenderCallback firstDayCellRender() {
  return [](QPainter& painter, const AdDatePickerPanel::CellRenderInfo& info) {
    if (info.type != AdDatePickerPanel::PickerMode::Date || !info.inView || !info.date.isValid() ||
        info.date.day() != 1) {
      return;
    }

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(22, 119, 255), info.selected ? 2 : 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QRectF(info.contentRect).adjusted(4.5, 4.5, -4.5, -4.5));
  };
}

AdDatePickerPanel::CellRenderCallback timeCellRender() {
  return [](QPainter& painter, const AdDatePickerPanel::CellRenderInfo& info) {
    if (info.type != AdDatePickerPanel::PickerMode::Time) {
      return;
    }

    if (info.subType == AdDatePickerPanel::CellSubType::Second && info.value % 15 == 0) {
      painter.setRenderHint(QPainter::Antialiasing, true);
      painter.setPen(QPen(QColor(22, 119, 255), 2));
      const QRect line = info.contentRect.adjusted(8, info.contentRect.height() - 5, -8, -3);
      painter.drawLine(line.left(), line.center().y(), line.right(), line.center().y());
    }
    if (info.subType == AdDatePickerPanel::CellSubType::Meridiem && info.value == 1) {
      painter.setRenderHint(QPainter::Antialiasing, true);
      painter.setPen(QPen(QColor(114, 46, 209), 1));
      painter.setBrush(Qt::NoBrush);
      painter.drawRoundedRect(QRectF(info.contentRect).adjusted(6.5, 4.5, -6.5, -4.5), 4, 4);
    }
  };
}

}  // namespace

DatePickerDocsPage::DatePickerDocsPage(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(16, 16, 16, 24);
  root->setSpacing(16);

  auto* title = new QLabel("DatePicker");
  QFont titleFont = title->font();
  titleFont.setPointSize(titleFont.pointSize() + 8);
  titleFont.setBold(true);
  title->setFont(titleFont);
  root->addWidget(title);

  auto* subtitle = new QLabel(
      "Date, week, month, quarter, year, and range selection with Ant Design styled inputs and "
      "panels.");
  subtitle->setWordWrap(true);
  root->addWidget(subtitle);

  addSection(root, "Basic", "Default date input and popup selection.", buildBasicDemo());
  addSection(root, "Picker modes", "Date, week, month, quarter, year, and decade modes.",
             buildModeDemo());
  addSection(root, "Show week", "API: showWeek", buildShowWeekDemo());
  addSection(root, "Locale", "API: locale", buildLocaleDemo());
  addSection(root, "Buddhist era", "Locale-aware Buddhist era display and parsing.",
             buildBuddhistEraDemo());
  addSection(root, "Panel view", "APIs: defaultPickerValue, pickerValue", buildPanelViewDemo());
  addSection(root, "Range picker", "APIs: order, calendarChanged", buildRangeDemo());
  addSection(root, "Range separator", "RangePicker API: separator", buildRangeSeparatorDemo());
  addSection(root, "Customized range picker", "Coordinated single pickers for start/end input.",
             buildStartEndDemo());
  addSection(root, "Min and max date", "Bounded date selection.", buildMinMaxDemo());
  addSection(root, "Select range dates", "Context-aware disabled-date predicates.",
             buildRangeConstraintDemo());
  addSection(root, "Allow empty range end", "APIs: allowEmptyStart, allowEmptyEnd",
             buildAllowEmptyDemo());
  addSection(root, "Date format", "APIs: displayFormat, displayFormats, displayTextCallback",
             buildFormatDemo());
  addSection(root, "Mask format", "API: maskFormat", buildMaskFormatDemo());
  addSection(root, "Input behavior",
             "APIs: id, previewValue, inputReadOnly, preserveInvalidOnBlur, focus, blur",
             buildInputBehaviorDemo());
  addSection(root, "Custom cell render", "API: cellRenderCallback", buildCellRenderDemo());
  addSection(root, "Multiple", "Multiple date selection.", buildMultipleDemo());
  addSection(root, "Need confirm", "API: needConfirm", buildNeedConfirmDemo());
  addSection(root, "Show time", "Date selection with time columns.", buildShowTimeDemo());
  addSection(root, "Time picker", "Time-only single and range selection.", buildTimePickerDemo());
  addSection(root, "Prefix and suffix", "APIs: prefixText, suffixText, suffixIconRef",
             buildPrefixSuffixDemo());
  addSection(root, "Header navigation icons",
             "APIs: prevIcon, nextIcon, superPrevIcon, superNextIcon", buildNavigationIconDemo());
  addSection(root, "Semantic styles",
             "Typed slot styling for root, input, prefix, suffix, and popup panel surfaces.",
             buildSemanticStyleDemo());
  addSection(root, "Component tokens", "Typed DatePicker component token overrides.",
             buildComponentTokenDemo());
  addSection(root, "Sizes", "Small, middle, and large controls.", buildSizeDemo());
  addSection(root, "Variants", "Outlined, filled, borderless, and underlined controls.",
             buildVariantDemo());
  addSection(root, "Status and disabled date", "APIs: status, disabledDatePredicate",
             buildStatusDisabledDemo());
  addSection(root, "Presets and extra footer", "APIs: presets, extraFooterWidget",
             buildPresetFooterDemo());
  addSection(root, "Placement", "Popup placement modes.", buildPlacementDemo());
  addSection(root, "Popup layer", "Qt extension: switch between in-window and Qt tool layers.",
             buildPopupLayerModeDemo());
  addSection(root, "Popup content wrapper", "API: popupContentWrapperFactory",
             buildPopupContentWrapperDemo());
  addSection(root, "Panel components", "API: panelComponentFactory", buildPanelComponentDemo());
  addSection(root, "External panel", "Standalone AdDatePickerPanel usage.", buildPanelDemo());

  root->addStretch();
}

const QVector<QWidget*>& DatePickerDocsPage::sectionAnchors() const { return anchors_; }

const QStringList& DatePickerDocsPage::sectionTitles() const { return titles_; }

void DatePickerDocsPage::addSection(QVBoxLayout* root, const QString& title,
                                    const QString& description, QWidget* content) {
  auto* panel = makeDemoPanel();
  auto* layout = qobject_cast<QVBoxLayout*>(panel->layout());

  auto* titleLabel = new QLabel(title);
  QFont titleFont = titleLabel->font();
  titleFont.setBold(true);
  titleFont.setPointSize(titleFont.pointSize() + 1);
  titleLabel->setFont(titleFont);

  auto* descLabel = new QLabel(description);
  descLabel->setWordWrap(true);

  layout->addWidget(titleLabel);
  layout->addWidget(descLabel);
  layout->addWidget(content);

  root->addWidget(panel);
  anchors_.append(panel);
  titles_.append(title);
}

QWidget* DatePickerDocsPage::buildBasicDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(12);

  auto* picker = makePicker(QDate::currentDate());
  auto* value = makeValueLabel(dateText(picker->date()));
  connect(picker, &AdDatePicker::dateChanged, value,
          [value](const QDate& date) { value->setText(dateText(date)); });

  row->addWidget(picker);
  row->addWidget(value);
  row->addStretch();
  return box;
}

QWidget* DatePickerDocsPage::buildModeDemo() {
  auto* box = new QWidget();
  auto* grid = new QGridLayout(box);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(12);
  grid->setVerticalSpacing(10);

  struct Row {
    QString label;
    AdDatePickerPanel::PickerMode mode;
    QDate date;
  };

  const QVector<Row> rows = {
      {QStringLiteral("Date"), AdDatePickerPanel::PickerMode::Date, QDate::currentDate()},
      {QStringLiteral("Week"), AdDatePickerPanel::PickerMode::Week, QDate::currentDate()},
      {QStringLiteral("Month"), AdDatePickerPanel::PickerMode::Month, QDate::currentDate()},
      {QStringLiteral("Quarter"), AdDatePickerPanel::PickerMode::Quarter, QDate::currentDate()},
      {QStringLiteral("Year"), AdDatePickerPanel::PickerMode::Year, QDate::currentDate()},
  };

  for (int i = 0; i < rows.size(); ++i) {
    auto* picker = makePicker(rows.at(i).date);
    picker->setPickerMode(rows.at(i).mode);
    addLabeledRow(grid, i, rows.at(i).label, picker);
  }

  grid->setColumnStretch(2, 1);
  return box;
}

QWidget* DatePickerDocsPage::buildShowWeekDemo() {
  auto* box = new QWidget();
  auto* grid = new QGridLayout(box);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(12);
  grid->setVerticalSpacing(10);

  auto* date = makePicker(QDate::currentDate());
  date->setShowWeek(true);

  auto* week = makePicker(QDate::currentDate());
  week->setPickerMode(AdDatePickerPanel::PickerMode::Week);

  addLabeledRow(grid, 0, "Date + week", date);
  addLabeledRow(grid, 1, "Week picker", week);
  return box;
}

QWidget* DatePickerDocsPage::buildLocaleDemo() {
  auto* box = new QWidget();
  auto* grid = new QGridLayout(box);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(12);
  grid->setVerticalSpacing(10);

  auto* german = makePicker(QDate(2026, 7, 16));
  german->setLocale(QLocale(QLocale::German, QLocale::Germany));
  german->setDisplayFormat(QStringLiteral("dd. MMMM yyyy"));
  auto* germanValue = makeValueLabel(german->lineEdit()->text());
  connect(german, &AdDatePicker::dateChanged, germanValue, [german, germanValue](const QDate&) {
    germanValue->setText(german->lineEdit()->text());
  });

  auto* week = makePicker(QDate(2026, 7, 8));
  week->setPickerMode(AdDatePickerPanel::PickerMode::Week);
  week->setLocale(QLocale(QLocale::English, QLocale::UnitedStates));
  auto* weekValue =
      makeValueLabel(QStringLiteral("Stored week start: %1").arg(dateText(week->date())));
  connect(week, &AdDatePicker::dateChanged, weekValue, [weekValue](const QDate& date) {
    weekValue->setText(QStringLiteral("Stored week start: %1").arg(dateText(date)));
  });

  auto* range = new AdDateRangePicker();
  range->setAllowClear(true);
  range->setPickerMode(AdDatePickerPanel::PickerMode::Week);
  range->setLocale(QLocale(QLocale::English, QLocale::UnitedStates));
  range->setRange(QDate(2026, 7, 8), QDate(2026, 7, 20));
  auto* rangeValue = makeValueLabel(rangeText(range->startDate(), range->endDate()));
  connect(range, &AdDateRangePicker::rangeChanged, rangeValue,
          [rangeValue](const QDate& start, const QDate& end) {
            rangeValue->setText(rangeText(start, end));
          });

  auto* germanRow = new QWidget();
  auto* germanLayout = new QHBoxLayout(germanRow);
  germanLayout->setContentsMargins(0, 0, 0, 0);
  germanLayout->setSpacing(12);
  germanLayout->addWidget(german);
  germanLayout->addWidget(germanValue);
  germanLayout->addStretch();

  auto* weekRow = new QWidget();
  auto* weekLayout = new QHBoxLayout(weekRow);
  weekLayout->setContentsMargins(0, 0, 0, 0);
  weekLayout->setSpacing(12);
  weekLayout->addWidget(week);
  weekLayout->addWidget(weekValue);
  weekLayout->addStretch();

  auto* rangeRow = new QWidget();
  auto* rangeLayout = new QHBoxLayout(rangeRow);
  rangeLayout->setContentsMargins(0, 0, 0, 0);
  rangeLayout->setSpacing(12);
  rangeLayout->addWidget(range);
  rangeLayout->addWidget(rangeValue);
  rangeLayout->addStretch();

  addLabeledRow(grid, 0, "German date", germanRow);
  addLabeledRow(grid, 1, "US week", weekRow);
  addLabeledRow(grid, 2, "US week range", rangeRow);
  return box;
}

QWidget* DatePickerDocsPage::buildBuddhistEraDemo() {
  auto* box = new QWidget();
  auto* grid = new QGridLayout(box);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(12);
  grid->setVerticalSpacing(10);

  auto* date = makePicker(QDate(2024, 1, 1));
  date->setDisplayFormat(QStringLiteral("BBBB-MM-DD"));
  auto* dateValue = makeValueLabel(dateText(date->date()));
  connect(date, &AdDatePicker::dateChanged, dateValue,
          [dateValue](const QDate& value) { dateValue->setText(dateText(value)); });

  auto* dateRow = new QWidget();
  auto* dateLayout = new QHBoxLayout(dateRow);
  dateLayout->setContentsMargins(0, 0, 0, 0);
  dateLayout->setSpacing(12);
  dateLayout->addWidget(date);
  dateLayout->addWidget(dateValue);
  dateLayout->addStretch();

  auto* dateTime = new AdDatePicker();
  dateTime->setAllowClear(true);
  dateTime->setShowTime(true);
  dateTime->setDisplayFormat(QStringLiteral("BBBB-MM-DD HH:mm:ss"));
  dateTime->setDateTime(QDateTime(QDate(2024, 1, 1), QTime(9, 30, 0)));
  auto* dateTimeValue = makeValueLabel(dateTimeText(dateTime->dateTime()));
  connect(dateTime, &AdDatePicker::dateTimeChanged, dateTimeValue,
          [dateTimeValue](const QDateTime& value) { dateTimeValue->setText(dateTimeText(value)); });

  auto* dateTimeRow = new QWidget();
  auto* dateTimeLayout = new QHBoxLayout(dateTimeRow);
  dateTimeLayout->setContentsMargins(0, 0, 0, 0);
  dateTimeLayout->setSpacing(12);
  dateTimeLayout->addWidget(dateTime);
  dateTimeLayout->addWidget(dateTimeValue);
  dateTimeLayout->addStretch();

  auto* range = new AdDateRangePicker();
  range->setAllowClear(true);
  range->setDisplayFormat(QStringLiteral("BBBB-MM-DD"));
  range->setRange(QDate(2024, 1, 1), QDate(2024, 1, 10));
  auto* rangeValue = makeValueLabel(rangeText(range->startDate(), range->endDate()));
  connect(range, &AdDateRangePicker::rangeChanged, rangeValue,
          [rangeValue](const QDate& start, const QDate& end) {
            rangeValue->setText(rangeText(start, end));
          });

  auto* rangeRow = new QWidget();
  auto* rangeLayout = new QHBoxLayout(rangeRow);
  rangeLayout->setContentsMargins(0, 0, 0, 0);
  rangeLayout->setSpacing(12);
  rangeLayout->addWidget(range);
  rangeLayout->addWidget(rangeValue);
  rangeLayout->addStretch();

  addLabeledRow(grid, 0, "Date", dateRow);
  addLabeledRow(grid, 1, "Date + time", dateTimeRow);
  addLabeledRow(grid, 2, "Range", rangeRow);
  return box;
}

QWidget* DatePickerDocsPage::buildPanelViewDemo() {
  auto* box = new QWidget();
  auto* grid = new QGridLayout(box);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(12);
  grid->setVerticalSpacing(10);

  auto* defaultView = new AdDatePicker();
  defaultView->setAllowClear(true);
  defaultView->setDefaultPickerValue(QDate(2026, 12, 1));

  auto* defaultOpen = new AdDatePicker();
  defaultOpen->setAllowClear(true);
  defaultOpen->setDefaultOpen(true);
  defaultOpen->setDefaultPickerValue(QDate(2026, 11, 1));

  auto* panelMode = makePicker(QDate::currentDate());
  panelMode->setPanelMode(AdDatePickerPanel::PickerMode::Year);
  auto* panelModeLabel = makeValueLabel(QStringLiteral("Mode: year"));
  connect(panelMode, &AdDatePicker::panelChanged, panelModeLabel,
          [panelModeLabel](const QDate&, AdDatePicker::PickerMode mode) {
            panelModeLabel->setText(QStringLiteral("Mode: %1").arg(pickerModeText(mode)));
          });
  auto* panelModeRow = new QWidget();
  auto* panelModeLayout = new QHBoxLayout(panelModeRow);
  panelModeLayout->setContentsMargins(0, 0, 0, 0);
  panelModeLayout->setSpacing(12);
  panelModeLayout->addWidget(panelMode);
  panelModeLayout->addWidget(panelModeLabel);
  panelModeLayout->addStretch();

  auto* timePanelMode = makePicker(QDate::currentDate());
  timePanelMode->setShowTime(true);
  timePanelMode->setPanelMode(AdDatePickerPanel::PickerMode::Time);
  auto* timePanelModeLabel = makeValueLabel(QStringLiteral("Mode: time"));
  connect(timePanelMode, &AdDatePicker::panelChanged, timePanelModeLabel,
          [timePanelModeLabel](const QDate&, AdDatePicker::PickerMode mode) {
            timePanelModeLabel->setText(QStringLiteral("Mode: %1").arg(pickerModeText(mode)));
          });
  auto* timePanelModeRow = new QWidget();
  auto* timePanelModeLayout = new QHBoxLayout(timePanelModeRow);
  timePanelModeLayout->setContentsMargins(0, 0, 0, 0);
  timePanelModeLayout->setSpacing(12);
  timePanelModeLayout->addWidget(timePanelMode);
  timePanelModeLayout->addWidget(timePanelModeLabel);
  timePanelModeLayout->addStretch();

  auto* decadePanelMode = makePicker(QDate::currentDate());
  decadePanelMode->setPanelMode(AdDatePickerPanel::PickerMode::Decade);
  auto* decadePanelModeLabel = makeValueLabel(QStringLiteral("Mode: decade"));
  connect(decadePanelMode, &AdDatePicker::panelChanged, decadePanelModeLabel,
          [decadePanelModeLabel](const QDate&, AdDatePicker::PickerMode mode) {
            decadePanelModeLabel->setText(QStringLiteral("Mode: %1").arg(pickerModeText(mode)));
          });
  auto* decadePanelModeRow = new QWidget();
  auto* decadePanelModeLayout = new QHBoxLayout(decadePanelModeRow);
  decadePanelModeLayout->setContentsMargins(0, 0, 0, 0);
  decadePanelModeLayout->setSpacing(12);
  decadePanelModeLayout->addWidget(decadePanelMode);
  decadePanelModeLayout->addWidget(decadePanelModeLabel);
  decadePanelModeLayout->addStretch();

  auto* controlledView = makePicker(QDate::currentDate());
  controlledView->setPickerValue(QDate(2027, 5, 1));
  auto* controlledViewLabel = makeValueLabel(QStringLiteral("Panel: 2027-05-01"));
  connect(controlledView, &AdDatePicker::panelChanged, controlledViewLabel,
          [controlledViewLabel](const QDate& viewDate, AdDatePicker::PickerMode) {
            controlledViewLabel->setText(QStringLiteral("Panel: %1").arg(dateText(viewDate)));
          });
  auto* controlledViewRow = new QWidget();
  auto* controlledViewLayout = new QHBoxLayout(controlledViewRow);
  controlledViewLayout->setContentsMargins(0, 0, 0, 0);
  controlledViewLayout->setSpacing(12);
  controlledViewLayout->addWidget(controlledView);
  controlledViewLayout->addWidget(controlledViewLabel);
  controlledViewLayout->addStretch();

  auto* rangeView = new AdDateRangePicker();
  rangeView->setAllowClear(true);
  rangeView->setRange(QDate::currentDate(), QDate::currentDate().addDays(7));
  rangeView->setPickerValue(QDate(2027, 8, 1));
  auto* rangeViewLabel = makeValueLabel(QStringLiteral("Panels: 2027-08-01 -> 2027-09-01"));
  connect(rangeView, &AdDateRangePicker::panelChanged, rangeViewLabel,
          [rangeViewLabel](const QDate& primaryViewDate, const QDate& secondaryViewDate,
                           AdDateRangePicker::PickerMode) {
            rangeViewLabel->setText(
                QStringLiteral("Panels: %1").arg(rangeText(primaryViewDate, secondaryViewDate)));
          });
  auto* rangeViewRow = new QWidget();
  auto* rangeViewLayout = new QHBoxLayout(rangeViewRow);
  rangeViewLayout->setContentsMargins(0, 0, 0, 0);
  rangeViewLayout->setSpacing(12);
  rangeViewLayout->addWidget(rangeView);
  rangeViewLayout->addWidget(rangeViewLabel);
  rangeViewLayout->addStretch();

  addLabeledRow(grid, 0, "Default view", defaultView);
  addLabeledRow(grid, 1, "Default open", defaultOpen);
  addLabeledRow(grid, 2, "Panel mode", panelModeRow);
  addLabeledRow(grid, 3, "Time panel", timePanelModeRow);
  addLabeledRow(grid, 4, "Decade panel", decadePanelModeRow);
  addLabeledRow(grid, 5, "Picker view", controlledViewRow);
  addLabeledRow(grid, 6, "Range view", rangeViewRow);
  return box;
}

QWidget* DatePickerDocsPage::buildRangeDemo() {
  auto* box = new QWidget();
  auto* grid = new QGridLayout(box);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(12);
  grid->setVerticalSpacing(10);

  auto* range = new AdDateRangePicker();
  range->setAllowClear(true);
  range->setRange(QDate::currentDate(), QDate::currentDate().addDays(7));
  auto* value = makeValueLabel(rangeText(range->startDate(), range->endDate()));
  connect(range, &AdDateRangePicker::rangeChanged, value,
          [value](const QDate& start, const QDate& end) { value->setText(rangeText(start, end)); });
  auto* pendingValue = makeValueLabel(QStringLiteral("Pending: (none)"));
  connect(
      range, &AdDateRangePicker::calendarChanged, pendingValue,
      [pendingValue](const QDate& start, const QDate& end, AdDateRangePicker::RangePart rangePart) {
        const QString part = rangePart == AdDateRangePicker::RangePart::Start
                                 ? QStringLiteral("start")
                                 : QStringLiteral("end");
        pendingValue->setText(QStringLiteral("Pending %1: %2").arg(part, rangeText(start, end)));
      });

  auto* rangeRow = new QWidget();
  auto* rangeLayout = new QHBoxLayout(rangeRow);
  rangeLayout->setContentsMargins(0, 0, 0, 0);
  rangeLayout->setSpacing(12);
  rangeLayout->addWidget(range);
  rangeLayout->addWidget(value);
  rangeLayout->addWidget(pendingValue);
  rangeLayout->addStretch();

  auto* reverseRange = new AdDateRangePicker();
  reverseRange->setAllowClear(true);
  reverseRange->setOrder(false);
  reverseRange->setRange(QDate(2026, 7, 16), QDate(2026, 7, 6));
  auto* reverseValue =
      makeValueLabel(rangeText(reverseRange->startDate(), reverseRange->endDate()));
  connect(reverseRange, &AdDateRangePicker::rangeChanged, reverseValue,
          [reverseValue](const QDate& start, const QDate& end) {
            reverseValue->setText(rangeText(start, end));
          });

  auto* reverseRow = new QWidget();
  auto* reverseLayout = new QHBoxLayout(reverseRow);
  reverseLayout->setContentsMargins(0, 0, 0, 0);
  reverseLayout->setSpacing(12);
  reverseLayout->addWidget(reverseRange);
  reverseLayout->addWidget(reverseValue);
  reverseLayout->addStretch();

  addLabeledRow(grid, 0, "Default", rangeRow);
  addLabeledRow(grid, 1, "Preserve order", reverseRow);
  return box;
}

QWidget* DatePickerDocsPage::buildRangeSeparatorDemo() {
  auto* box = new QWidget();
  auto* grid = new QGridLayout(box);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(12);
  grid->setVerticalSpacing(10);

  auto* textSeparator = new AdDateRangePicker();
  textSeparator->setAllowClear(true);
  textSeparator->setRange(QDate::currentDate(), QDate::currentDate().addDays(7));
  textSeparator->setSeparator(QStringLiteral(" until "));

  auto* compactSeparator = new AdDateRangePicker();
  compactSeparator->setAllowClear(true);
  compactSeparator->setRange(QDate::currentDate(), QDate::currentDate().addDays(14));
  compactSeparator->setSeparator(QStringLiteral(" | "));

  addLabeledRow(grid, 0, "Text", textSeparator);
  addLabeledRow(grid, 1, "Compact", compactSeparator);
  return box;
}

QWidget* DatePickerDocsPage::buildStartEndDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(12);

  auto* start = new AdDatePicker();
  start->setAllowClear(true);
  start->setShowTime(true);
  start->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
  start->setPlaceholder(QStringLiteral("Start"));

  auto* end = new AdDatePicker();
  end->setAllowClear(true);
  end->setShowTime(true);
  end->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
  end->setPlaceholder(QStringLiteral("End"));

  start->setDisabledDatePredicate([end](const QDate& date) {
    return date.isValid() && end->date().isValid() && date > end->date();
  });
  end->setDisabledDatePredicate([start](const QDate& date) {
    return date.isValid() && start->date().isValid() && date <= start->date();
  });
  connect(start, &AdDatePicker::popupVisibleChanged, end, [end](bool open) {
    if (!open) {
      end->showPopup();
    }
  });

  auto* value = makeValueLabel(rangeDateTimeText(start->dateTime(), end->dateTime()));
  const auto syncValue = [start, end, value]() {
    value->setText(rangeDateTimeText(start->dateTime(), end->dateTime()));
  };
  connect(start, &AdDatePicker::dateTimeChanged, value, syncValue);
  connect(end, &AdDatePicker::dateTimeChanged, value, syncValue);

  row->addWidget(start);
  row->addWidget(end);
  row->addWidget(value);
  row->addStretch();
  return box;
}

QWidget* DatePickerDocsPage::buildMinMaxDemo() {
  auto* box = new QWidget();
  auto* grid = new QGridLayout(box);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(12);
  grid->setVerticalSpacing(10);

  const QDate minDate(2019, 8, 1);
  const QDate maxDate(2020, 10, 31);

  auto* single = makePicker(QDate(2019, 9, 3));
  single->setMinDate(minDate);
  single->setMaxDate(maxDate);
  auto* singleValue = makeValueLabel(dateText(single->date()));
  connect(single, &AdDatePicker::dateChanged, singleValue,
          [singleValue](const QDate& date) { singleValue->setText(dateText(date)); });

  auto* singleRow = new QWidget();
  auto* singleLayout = new QHBoxLayout(singleRow);
  singleLayout->setContentsMargins(0, 0, 0, 0);
  singleLayout->setSpacing(12);
  singleLayout->addWidget(single);
  singleLayout->addWidget(singleValue);
  singleLayout->addStretch();

  auto* range = new AdDateRangePicker();
  range->setAllowClear(true);
  range->setMinDate(minDate);
  range->setMaxDate(maxDate);
  range->setRange(QDate(2019, 9, 3), QDate(2019, 11, 22));
  auto* rangeValue = makeValueLabel(rangeText(range->startDate(), range->endDate()));
  connect(range, &AdDateRangePicker::rangeChanged, rangeValue,
          [rangeValue](const QDate& start, const QDate& end) {
            rangeValue->setText(rangeText(start, end));
          });

  auto* rangeRow = new QWidget();
  auto* rangeLayout = new QHBoxLayout(rangeRow);
  rangeLayout->setContentsMargins(0, 0, 0, 0);
  rangeLayout->setSpacing(12);
  rangeLayout->addWidget(range);
  rangeLayout->addWidget(rangeValue);
  rangeLayout->addStretch();

  addLabeledRow(grid, 0, "Single", singleRow);
  addLabeledRow(grid, 1, "Range", rangeRow);
  return box;
}

QWidget* DatePickerDocsPage::buildRangeConstraintDemo() {
  auto* box = new QWidget();
  auto* grid = new QGridLayout(box);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(12);
  grid->setVerticalSpacing(10);

  auto* sevenDays = new AdDateRangePicker();
  sevenDays->setAllowClear(true);
  sevenDays->setRange(QDate(2026, 7, 6), QDate(2026, 7, 12));
  sevenDays->setDisabledDateContextPredicate(
      [](const QDate& current, const AdDatePickerPanel::DisabledDateContext& info) {
        if (!current.isValid() || !info.from.isValid()) {
          return false;
        }
        const QDate minDate = info.from.addDays(-6);
        const QDate maxDate = info.from.addDays(6);
        switch (info.type) {
          case AdDatePickerPanel::PickerMode::Year:
            return current.year() < minDate.year() || current.year() > maxDate.year();
          case AdDatePickerPanel::PickerMode::Month:
            return current.year() * 12 + current.month() < minDate.year() * 12 + minDate.month() ||
                   current.year() * 12 + current.month() > maxDate.year() * 12 + maxDate.month();
          case AdDatePickerPanel::PickerMode::Date:
          case AdDatePickerPanel::PickerMode::Week:
          case AdDatePickerPanel::PickerMode::Quarter:
          default: {
            const int days = info.from.daysTo(current);
            return days <= -7 || days >= 7;
          }
        }
      });

  auto* sevenValue = makeValueLabel(rangeText(sevenDays->startDate(), sevenDays->endDate()));
  connect(sevenDays, &AdDateRangePicker::rangeChanged, sevenValue,
          [sevenValue](const QDate& start, const QDate& end) {
            sevenValue->setText(rangeText(start, end));
          });

  auto* sevenRow = new QWidget();
  auto* sevenLayout = new QHBoxLayout(sevenRow);
  sevenLayout->setContentsMargins(0, 0, 0, 0);
  sevenLayout->setSpacing(12);
  sevenLayout->addWidget(sevenDays);
  sevenLayout->addWidget(sevenValue);
  sevenLayout->addStretch();

  auto* sixMonths = new AdDateRangePicker();
  sixMonths->setAllowClear(true);
  sixMonths->setPickerMode(AdDatePickerPanel::PickerMode::Month);
  sixMonths->setRange(QDate(2026, 7, 1), QDate(2026, 12, 1));
  sixMonths->setDisabledDateContextPredicate(
      [](const QDate& current, const AdDatePickerPanel::DisabledDateContext& info) {
        if (!current.isValid() || !info.from.isValid()) {
          return false;
        }
        const QDate minDate = info.from.addMonths(-5);
        const QDate maxDate = info.from.addMonths(5);
        if (info.type == AdDatePickerPanel::PickerMode::Year) {
          return current.year() < minDate.year() || current.year() > maxDate.year();
        }
        return current.year() * 12 + current.month() < minDate.year() * 12 + minDate.month() ||
               current.year() * 12 + current.month() > maxDate.year() * 12 + maxDate.month();
      });

  auto* monthValue = makeValueLabel(rangeText(sixMonths->startDate(), sixMonths->endDate()));
  connect(sixMonths, &AdDateRangePicker::rangeChanged, monthValue,
          [monthValue](const QDate& start, const QDate& end) {
            monthValue->setText(rangeText(start, end));
          });

  auto* monthRow = new QWidget();
  auto* monthLayout = new QHBoxLayout(monthRow);
  monthLayout->setContentsMargins(0, 0, 0, 0);
  monthLayout->setSpacing(12);
  monthLayout->addWidget(sixMonths);
  monthLayout->addWidget(monthValue);
  monthLayout->addStretch();

  addLabeledRow(grid, 0, "7 days range", sevenRow);
  addLabeledRow(grid, 1, "6 months range", monthRow);
  return box;
}

QWidget* DatePickerDocsPage::buildAllowEmptyDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(12);

  auto* range = new AdDateRangePicker();
  range->setAllowClear(true);
  range->setAllowEmptyEnd(true);
  range->setRangePlaceholders(QStringLiteral("Start Date"), QStringLiteral("Till Now"));
  range->setRange(QDate::currentDate().addDays(-30), QDate());

  auto* value = makeValueLabel(rangeText(range->startDate(), range->endDate()));
  connect(range, &AdDateRangePicker::rangeChanged, value,
          [value](const QDate& start, const QDate& end) { value->setText(rangeText(start, end)); });

  row->addWidget(range);
  row->addWidget(value);
  row->addStretch();
  return box;
}

QWidget* DatePickerDocsPage::buildFormatDemo() {
  auto* box = new QWidget();
  auto* grid = new QGridLayout(box);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(12);
  grid->setVerticalSpacing(10);

  const QStringList formats = {
      QStringLiteral("yyyy/MM/dd"),
      QStringLiteral("dd.MM.yyyy"),
      QStringLiteral("yyyy-MM-dd"),
  };

  auto* single = makePicker(QDate(2026, 7, 6));
  single->setDisplayFormats(formats);
  auto* singleValue = makeValueLabel(dateText(single->date()));
  connect(single, &AdDatePicker::dateChanged, singleValue,
          [singleValue](const QDate& date) { singleValue->setText(dateText(date)); });

  auto* singleRow = new QWidget();
  auto* singleLayout = new QHBoxLayout(singleRow);
  singleLayout->setContentsMargins(0, 0, 0, 0);
  singleLayout->setSpacing(12);
  singleLayout->addWidget(single);
  singleLayout->addWidget(singleValue);
  singleLayout->addStretch();

  auto* customSingle = makePicker(QDate(2026, 7, 6));
  customSingle->setDisplayTextCallback([](const QDate& date, const QTime&) {
    return QStringLiteral("custom format: %1").arg(date.toString(QStringLiteral("yyyy/MM/dd")));
  });

  auto* customSingleRow = new QWidget();
  auto* customSingleLayout = new QHBoxLayout(customSingleRow);
  customSingleLayout->setContentsMargins(0, 0, 0, 0);
  customSingleLayout->setSpacing(12);
  customSingleLayout->addWidget(customSingle);
  customSingleLayout->addStretch();

  auto* range = new AdDateRangePicker();
  range->setAllowClear(true);
  range->setDisplayFormats(formats);
  range->setRange(QDate(2026, 7, 6), QDate(2026, 7, 16));
  auto* rangeValue = makeValueLabel(rangeText(range->startDate(), range->endDate()));
  connect(range, &AdDateRangePicker::rangeChanged, rangeValue,
          [rangeValue](const QDate& start, const QDate& end) {
            rangeValue->setText(rangeText(start, end));
          });

  auto* rangeRow = new QWidget();
  auto* rangeLayout = new QHBoxLayout(rangeRow);
  rangeLayout->setContentsMargins(0, 0, 0, 0);
  rangeLayout->setSpacing(12);
  rangeLayout->addWidget(range);
  rangeLayout->addWidget(rangeValue);
  rangeLayout->addStretch();

  auto* customRange = new AdDateRangePicker();
  customRange->setAllowClear(true);
  customRange->setRange(QDate(2026, 7, 6), QDate(2026, 7, 16));
  customRange->setDisplayTextCallback([](const QDate& date, const QTime&) {
    return QStringLiteral("custom %1").arg(date.toString(QStringLiteral("MM/dd")));
  });

  auto* customRangeRow = new QWidget();
  auto* customRangeLayout = new QHBoxLayout(customRangeRow);
  customRangeLayout->setContentsMargins(0, 0, 0, 0);
  customRangeLayout->setSpacing(12);
  customRangeLayout->addWidget(customRange);
  customRangeLayout->addStretch();

  addLabeledRow(grid, 0, "Single", singleRow);
  addLabeledRow(grid, 1, "Range", rangeRow);
  addLabeledRow(grid, 2, "Custom display", customSingleRow);
  addLabeledRow(grid, 3, "Custom range", customRangeRow);
  return box;
}

QWidget* DatePickerDocsPage::buildMaskFormatDemo() {
  auto* box = new QWidget();
  auto* grid = new QGridLayout(box);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(12);
  grid->setVerticalSpacing(10);

  auto* dateMask = makePicker();
  dateMask->setDisplayFormat(QStringLiteral("YYYY-MM-DD"));
  dateMask->setMaskFormat(true);
  auto* dateValue = makeValueLabel(dateText(dateMask->date()));
  connect(dateMask, &AdDatePicker::dateChanged, dateValue,
          [dateValue](const QDate& date) { dateValue->setText(dateText(date)); });

  auto* dateRow = new QWidget();
  auto* dateLayout = new QHBoxLayout(dateRow);
  dateLayout->setContentsMargins(0, 0, 0, 0);
  dateLayout->setSpacing(12);
  dateLayout->addWidget(dateMask);
  dateLayout->addWidget(dateValue);
  dateLayout->addStretch();

  auto* dateTimeMask = makePicker();
  dateTimeMask->setDisplayFormat(QStringLiteral("YYYY-MM-DD HH:mm:ss"));
  dateTimeMask->setMaskFormat(true);
  auto* dateTimeValue = makeValueLabel(dateTimeText(dateTimeMask->dateTime()));
  connect(dateTimeMask, &AdDatePicker::dateTimeChanged, dateTimeValue,
          [dateTimeValue](const QDateTime& value) { dateTimeValue->setText(dateTimeText(value)); });

  auto* dateTimeRow = new QWidget();
  auto* dateTimeLayout = new QHBoxLayout(dateTimeRow);
  dateTimeLayout->setContentsMargins(0, 0, 0, 0);
  dateTimeLayout->setSpacing(12);
  dateTimeLayout->addWidget(dateTimeMask);
  dateTimeLayout->addWidget(dateTimeValue);
  dateTimeLayout->addStretch();

  addLabeledRow(grid, 0, "Date", dateRow);
  addLabeledRow(grid, 1, "Date time", dateTimeRow);
  return box;
}

QWidget* DatePickerDocsPage::buildInputBehaviorDemo() {
  auto* box = new QWidget();
  auto* grid = new QGridLayout(box);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(12);
  grid->setVerticalSpacing(10);

  auto* readOnly = makePicker(QDate::currentDate());
  readOnly->setInputReadOnly(true);

  auto* preserveSingle = makePicker(QDate::currentDate());
  preserveSingle->setPreserveInvalidOnBlur(true);
  preserveSingle->setPlaceholder(QStringLiteral("Invalid text is preserved"));

  auto* preserveRange = new AdDateRangePicker();
  preserveRange->setAllowClear(true);
  preserveRange->setRange(QDate::currentDate(), QDate::currentDate().addDays(7));
  preserveRange->setPreserveInvalidOnBlur(true);
  preserveRange->setPlaceholder(QStringLiteral("Start -> end"));

  auto* previewHover = makePicker(QDate(2026, 7, 6));
  previewHover->setDefaultPickerValue(QDate(2026, 7, 1));

  auto* previewDisabled = makePicker(QDate(2026, 7, 6));
  previewDisabled->setDefaultPickerValue(QDate(2026, 7, 1));
  previewDisabled->setPreviewValue(AdDatePicker::PreviewValue::Disabled);

  auto* previewRange = new AdDateRangePicker();
  previewRange->setAllowClear(true);
  previewRange->setRange(QDate(2026, 7, 6), QDate());
  previewRange->setDefaultPickerValue(QDate(2026, 7, 1));

  auto* previewRow = new QWidget();
  auto* previewLayout = new QHBoxLayout(previewRow);
  previewLayout->setContentsMargins(0, 0, 0, 0);
  previewLayout->setSpacing(12);
  previewLayout->addWidget(previewHover);
  previewLayout->addWidget(previewDisabled);
  previewLayout->addWidget(previewRange);
  previewLayout->addStretch();

  auto* idSingle = makePicker(QDate(2026, 7, 6));
  idSingle->setId(QStringLiteral("booking-date"));

  auto* idRange = new AdDateRangePicker();
  idRange->setAllowClear(true);
  idRange->setId(QStringLiteral("booking-range"));
  idRange->setRangeIds(QStringLiteral("booking-start"), QStringLiteral("booking-end"));
  idRange->setRange(QDate(2026, 7, 6), QDate(2026, 7, 16));

  auto* idValue = makeValueLabel(
      QStringLiteral("Input: %1; endpoints: %2, %3")
          .arg(idRange->lineEdit()->objectName(), idRange->startId(), idRange->endId()));
  auto* idRow = new QWidget();
  auto* idLayout = new QHBoxLayout(idRow);
  idLayout->setContentsMargins(0, 0, 0, 0);
  idLayout->setSpacing(12);
  idLayout->addWidget(idSingle);
  idLayout->addWidget(idRange);
  idLayout->addWidget(idValue);
  idLayout->addStretch();

  auto* focusRange = new AdDateRangePicker();
  focusRange->setAllowClear(true);
  focusRange->setRange(QDate(2026, 7, 6), QDate(2026, 7, 16));
  auto* focusStart = new QPushButton(QStringLiteral("Focus start"));
  auto* focusEnd = new QPushButton(QStringLiteral("Focus end"));
  auto* focusValue = makeValueLabel(QStringLiteral("Focus: (none)"));
  connect(focusStart, &QPushButton::clicked, focusRange,
          [focusRange]() { focusRange->focus(AdDateRangePicker::RangePart::Start); });
  connect(focusEnd, &QPushButton::clicked, focusRange,
          [focusRange]() { focusRange->focus(AdDateRangePicker::RangePart::End); });
  connect(focusRange, &AdDateRangePicker::focused, focusValue,
          [focusValue](AdDateRangePicker::RangePart range) {
            focusValue->setText(range == AdDateRangePicker::RangePart::Start
                                    ? QStringLiteral("Focus: start")
                                    : QStringLiteral("Focus: end"));
          });
  connect(focusRange, &AdDateRangePicker::blurred, focusValue,
          [focusValue](AdDateRangePicker::RangePart range) {
            focusValue->setText(range == AdDateRangePicker::RangePart::Start
                                    ? QStringLiteral("Blur: start")
                                    : QStringLiteral("Blur: end"));
          });
  auto* focusRow = new QWidget();
  auto* focusLayout = new QHBoxLayout(focusRow);
  focusLayout->setContentsMargins(0, 0, 0, 0);
  focusLayout->setSpacing(12);
  focusLayout->addWidget(focusRange);
  focusLayout->addWidget(focusStart);
  focusLayout->addWidget(focusEnd);
  focusLayout->addWidget(focusValue);
  focusLayout->addStretch();

  addLabeledRow(grid, 0, "Read-only input", readOnly);
  addLabeledRow(grid, 1, "Preserve invalid", preserveSingle);
  addLabeledRow(grid, 2, "Range preserve", preserveRange);
  addLabeledRow(grid, 3, "Hover preview", previewRow);
  addLabeledRow(grid, 4, "Input ids", idRow);
  addLabeledRow(grid, 5, "Focus and blur", focusRow);
  return box;
}

QWidget* DatePickerDocsPage::buildCellRenderDemo() {
  auto* box = new QWidget();
  auto* grid = new QGridLayout(box);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(12);
  grid->setVerticalSpacing(10);

  const auto callback = firstDayCellRender();

  auto* single = makePicker(QDate::currentDate());
  single->setCellRenderCallback(callback);

  auto* range = new AdDateRangePicker();
  range->setAllowClear(true);
  range->setRange(QDate::currentDate(), QDate::currentDate().addDays(8));
  range->setCellRenderCallback(callback);

  auto* time = makePicker(QDate(2026, 7, 6));
  time->setShowTime(true);
  time->setUse12Hours(true);
  time->setTimeFormat(QStringLiteral("h:mm:ss AP"));
  time->setDisplayFormat(QStringLiteral("yyyy-MM-dd h:mm:ss AP"));
  time->setDateTime(QDateTime(QDate(2026, 7, 6), QTime(22, 30, 15)));
  time->setCellRenderCallback(timeCellRender());

  addLabeledRow(grid, 0, "DatePicker", single);
  addLabeledRow(grid, 1, "RangePicker", range);
  addLabeledRow(grid, 2, "Time cells", time);
  return box;
}

QWidget* DatePickerDocsPage::buildMultipleDemo() {
  auto* box = new QWidget();
  auto* grid = new QGridLayout(box);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(12);
  grid->setVerticalSpacing(10);

  const QVector<QDate> defaultDates = {
      QDate(2026, 7, 1),
      QDate(2026, 7, 3),
      QDate(2026, 7, 5),
      QDate(2026, 7, 8),
  };

  struct Row {
    QString label;
    AdDatePicker::Size size;
  };
  const QVector<Row> rows = {
      {QStringLiteral("Small"), AdDatePicker::Size::Small},
      {QStringLiteral("Middle"), AdDatePicker::Size::Middle},
      {QStringLiteral("Large"), AdDatePicker::Size::Large},
  };

  auto* value = makeValueLabel(datesText(defaultDates));
  for (int i = 0; i < rows.size(); ++i) {
    auto* picker = new AdDatePicker();
    picker->setMultiple(true);
    picker->setAllowClear(true);
    picker->setResponsiveMaxTagCount(true);
    picker->setSize(rows.at(i).size);
    picker->setSelectedDates(defaultDates);
    connect(picker, &AdDatePicker::selectedDatesChanged, value,
            [value](const QVector<QDate>& dates) { value->setText(datesText(dates)); });
    addLabeledRow(grid, i, rows.at(i).label, picker);
  }
  addLabeledRow(grid, rows.size(), "Selected", value);
  return box;
}

QWidget* DatePickerDocsPage::buildNeedConfirmDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(12);

  auto* picker = makePicker(QDate(2026, 7, 6));
  picker->setNeedConfirm(true);
  auto* value = makeValueLabel(dateText(picker->date()));
  connect(picker, &AdDatePicker::accepted, value,
          [value](const QDate& date) { value->setText(dateText(date)); });

  row->addWidget(picker);
  row->addWidget(value);
  row->addStretch();
  return box;
}

QWidget* DatePickerDocsPage::buildShowTimeDemo() {
  auto* box = new QWidget();
  auto* grid = new QGridLayout(box);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(12);
  grid->setVerticalSpacing(10);

  auto* single = makePicker();
  single->setShowTime(true);
  single->setDateTime(QDateTime(QDate(2026, 7, 6), QTime(9, 30, 0)));
  auto* singleValue = makeValueLabel(dateTimeText(single->dateTime()));
  connect(single, &AdDatePicker::dateTimeChanged, singleValue,
          [singleValue](const QDateTime& value) { singleValue->setText(dateTimeText(value)); });

  auto* singleRow = new QWidget();
  auto* singleLayout = new QHBoxLayout(singleRow);
  singleLayout->setContentsMargins(0, 0, 0, 0);
  singleLayout->setSpacing(12);
  singleLayout->addWidget(single);
  singleLayout->addWidget(singleValue);
  singleLayout->addStretch();

  auto* withoutNow = makePicker();
  withoutNow->setShowTime(true);
  withoutNow->setShowNow(false);
  withoutNow->setDateTime(QDateTime(QDate(2026, 7, 6), QTime(9, 30, 0)));
  auto* withoutNowValue = makeValueLabel(dateTimeText(withoutNow->dateTime()));
  connect(
      withoutNow, &AdDatePicker::dateTimeChanged, withoutNowValue,
      [withoutNowValue](const QDateTime& value) { withoutNowValue->setText(dateTimeText(value)); });

  auto* withoutNowRow = new QWidget();
  auto* withoutNowLayout = new QHBoxLayout(withoutNowRow);
  withoutNowLayout->setContentsMargins(0, 0, 0, 0);
  withoutNowLayout->setSpacing(12);
  withoutNowLayout->addWidget(withoutNow);
  withoutNowLayout->addWidget(withoutNowValue);
  withoutNowLayout->addStretch();

  auto* defaultOpenTime = makePicker();
  defaultOpenTime->setShowTime(true);
  defaultOpenTime->setTimeFormat(QStringLiteral("HH:mm"));
  defaultOpenTime->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
  defaultOpenTime->setDefaultOpenTime(QTime(6, 30));

  auto* defaultOpenTimeRow = new QWidget();
  auto* defaultOpenTimeLayout = new QHBoxLayout(defaultOpenTimeRow);
  defaultOpenTimeLayout->setContentsMargins(0, 0, 0, 0);
  defaultOpenTimeLayout->setSpacing(12);
  defaultOpenTimeLayout->addWidget(defaultOpenTime);
  defaultOpenTimeLayout->addStretch();

  auto* steppedTime = makePicker();
  steppedTime->setShowTime(true);
  steppedTime->setTimeFormat(QStringLiteral("HH:mm:ss"));
  steppedTime->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
  steppedTime->setTimeSteps(2, 15, 30);
  steppedTime->setDateTime(QDateTime(QDate(2026, 7, 6), QTime(10, 30, 30)));
  auto* steppedTimeValue = makeValueLabel(dateTimeText(steppedTime->dateTime()));
  connect(steppedTime, &AdDatePicker::dateTimeChanged, steppedTimeValue,
          [steppedTimeValue](const QDateTime& value) {
            steppedTimeValue->setText(dateTimeText(value));
          });

  auto* steppedTimeRow = new QWidget();
  auto* steppedTimeLayout = new QHBoxLayout(steppedTimeRow);
  steppedTimeLayout->setContentsMargins(0, 0, 0, 0);
  steppedTimeLayout->setSpacing(12);
  steppedTimeLayout->addWidget(steppedTime);
  steppedTimeLayout->addWidget(steppedTimeValue);
  steppedTimeLayout->addStretch();

  auto* visibleColumns = makePicker();
  visibleColumns->setShowTime(true);
  visibleColumns->setTimeFormat(QStringLiteral("HH:mm:ss"));
  visibleColumns->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
  visibleColumns->setVisibleTimeColumns(true, true, false);
  visibleColumns->setDateTime(QDateTime(QDate(2026, 7, 6), QTime(10, 30, 45)));
  auto* visibleColumnsValue = makeValueLabel(dateTimeText(visibleColumns->dateTime()));
  connect(visibleColumns, &AdDatePicker::dateTimeChanged, visibleColumnsValue,
          [visibleColumnsValue](const QDateTime& value) {
            visibleColumnsValue->setText(dateTimeText(value));
          });

  auto* visibleColumnsRow = new QWidget();
  auto* visibleColumnsLayout = new QHBoxLayout(visibleColumnsRow);
  visibleColumnsLayout->setContentsMargins(0, 0, 0, 0);
  visibleColumnsLayout->setSpacing(12);
  visibleColumnsLayout->addWidget(visibleColumns);
  visibleColumnsLayout->addWidget(visibleColumnsValue);
  visibleColumnsLayout->addStretch();

  auto* antHourFormat = makePicker();
  antHourFormat->setShowTime(true);
  antHourFormat->setDisplayFormat(QStringLiteral("YYYY-MM-DD kk:mm"));
  antHourFormat->setDateTime(QDateTime(QDate(2026, 7, 6), QTime(9, 30, 45)));

  auto* antMeridiemFormat = makePicker();
  antMeridiemFormat->setShowTime(true);
  antMeridiemFormat->setDisplayFormat(QStringLiteral("YYYY-MM-DD h:mm A"));
  antMeridiemFormat->setDateTime(QDateTime(QDate(2026, 7, 6), QTime(22, 30, 45)));

  auto* antBracketLiteralFormat = makePicker();
  antBracketLiteralFormat->setShowTime(true);
  antBracketLiteralFormat->setDisplayFormat(QStringLiteral("YYYY-MM-DD [at] HH:mm"));
  antBracketLiteralFormat->setDateTime(QDateTime(QDate(2026, 7, 6), QTime(9, 30, 45)));

  auto* antFormatRow = new QWidget();
  auto* antFormatLayout = new QHBoxLayout(antFormatRow);
  antFormatLayout->setContentsMargins(0, 0, 0, 0);
  antFormatLayout->setSpacing(12);
  antFormatLayout->addWidget(antHourFormat);
  antFormatLayout->addWidget(antMeridiemFormat);
  antFormatLayout->addWidget(antBracketLiteralFormat);
  antFormatLayout->addStretch();

  auto* twelveHour = makePicker();
  twelveHour->setShowTime(true);
  twelveHour->setUse12Hours(true);
  twelveHour->setDateTime(QDateTime(QDate(2026, 7, 6), QTime(22, 30, 0)));
  auto* twelveHourValue = makeValueLabel(dateTimeText(twelveHour->dateTime()));
  connect(
      twelveHour, &AdDatePicker::dateTimeChanged, twelveHourValue,
      [twelveHourValue](const QDateTime& value) { twelveHourValue->setText(dateTimeText(value)); });

  auto* twelveHourRow = new QWidget();
  auto* twelveHourLayout = new QHBoxLayout(twelveHourRow);
  twelveHourLayout->setContentsMargins(0, 0, 0, 0);
  twelveHourLayout->setSpacing(12);
  twelveHourLayout->addWidget(twelveHour);
  twelveHourLayout->addWidget(twelveHourValue);
  twelveHourLayout->addStretch();

  auto* scrollTime = makePicker();
  scrollTime->setShowTime(true);
  scrollTime->setChangeOnScroll(true);
  scrollTime->setDateTime(QDateTime(QDate(2026, 7, 6), QTime(9, 30, 0)));
  auto* scrollTimeValue = makeValueLabel(dateTimeText(scrollTime->dateTime()));
  connect(
      scrollTime, &AdDatePicker::dateTimeChanged, scrollTimeValue,
      [scrollTimeValue](const QDateTime& value) { scrollTimeValue->setText(dateTimeText(value)); });

  auto* scrollTimeRow = new QWidget();
  auto* scrollTimeLayout = new QHBoxLayout(scrollTimeRow);
  scrollTimeLayout->setContentsMargins(0, 0, 0, 0);
  scrollTimeLayout->setSpacing(12);
  scrollTimeLayout->addWidget(scrollTime);
  scrollTimeLayout->addWidget(scrollTimeValue);
  scrollTimeLayout->addStretch();

  auto* range = new AdDateRangePicker();
  range->setAllowClear(true);
  range->setShowTime(true);
  range->setTimeFormat(QStringLiteral("HH:mm"));
  range->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
  range->setDefaultOpenTimeRange(QTime(0, 0), QTime(11, 59));
  range->setDateTimeRange(QDateTime(QDate(2026, 7, 6), QTime(8, 15)),
                          QDateTime(QDate(2026, 7, 16), QTime(18, 45)));
  auto* rangeValue =
      makeValueLabel(rangeDateTimeText(range->startDateTime(), range->endDateTime()));
  connect(range, &AdDateRangePicker::dateTimeRangeChanged, rangeValue,
          [rangeValue](const QDateTime& start, const QDateTime& end) {
            rangeValue->setText(rangeDateTimeText(start, end));
          });

  auto* rangeRow = new QWidget();
  auto* rangeLayout = new QHBoxLayout(rangeRow);
  rangeLayout->setContentsMargins(0, 0, 0, 0);
  rangeLayout->setSpacing(12);
  rangeLayout->addWidget(range);
  rangeLayout->addWidget(rangeValue);
  rangeLayout->addStretch();

  addLabeledRow(grid, 0, "Single", singleRow);
  addLabeledRow(grid, 1, "No now shortcut", withoutNowRow);
  addLabeledRow(grid, 2, "Default open time", defaultOpenTimeRow);
  addLabeledRow(grid, 3, "Stepped time", steppedTimeRow);
  addLabeledRow(grid, 4, "Visible columns", visibleColumnsRow);
  addLabeledRow(grid, 5, "Ant format tokens", antFormatRow);
  addLabeledRow(grid, 6, "12-hour time", twelveHourRow);
  addLabeledRow(grid, 7, "Scroll changes time", scrollTimeRow);
  addLabeledRow(grid, 8, "Range", rangeRow);
  return box;
}

QWidget* DatePickerDocsPage::buildTimePickerDemo() {
  auto* box = new QWidget();
  auto* grid = new QGridLayout(box);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(12);
  grid->setVerticalSpacing(10);

  auto* basic = makePicker();
  basic->setPickerMode(AdDatePickerPanel::PickerMode::Time);
  basic->setTime(QTime(12, 8, 23));
  auto* basicValue = makeValueLabel(timeText(basic->time()));
  connect(basic, &AdDatePicker::timeChanged, basicValue,
          [basicValue](const QTime& value) { basicValue->setText(timeText(value)); });

  auto* basicRow = new QWidget();
  auto* basicLayout = new QHBoxLayout(basicRow);
  basicLayout->setContentsMargins(0, 0, 0, 0);
  basicLayout->setSpacing(12);
  basicLayout->addWidget(basic);
  basicLayout->addWidget(basicValue);
  basicLayout->addStretch();

  auto* minuteOnly = makePicker();
  minuteOnly->setPickerMode(AdDatePickerPanel::PickerMode::Time);
  minuteOnly->setTimeFormat(QStringLiteral("HH:mm"));
  minuteOnly->setVisibleTimeColumns(true, true, false);
  minuteOnly->setTime(QTime(12, 8));
  auto* minuteOnlyValue = makeValueLabel(timeText(minuteOnly->time(), QStringLiteral("HH:mm")));
  connect(minuteOnly, &AdDatePicker::timeChanged, minuteOnlyValue,
          [minuteOnlyValue](const QTime& value) {
            minuteOnlyValue->setText(timeText(value, QStringLiteral("HH:mm")));
          });

  auto* minuteOnlyRow = new QWidget();
  auto* minuteOnlyLayout = new QHBoxLayout(minuteOnlyRow);
  minuteOnlyLayout->setContentsMargins(0, 0, 0, 0);
  minuteOnlyLayout->setSpacing(12);
  minuteOnlyLayout->addWidget(minuteOnly);
  minuteOnlyLayout->addWidget(minuteOnlyValue);
  minuteOnlyLayout->addStretch();

  auto* twelveHour = makePicker();
  twelveHour->setPickerMode(AdDatePickerPanel::PickerMode::Time);
  twelveHour->setUse12Hours(true);
  twelveHour->setTime(QTime(22, 30, 0));
  auto* twelveHourValue = makeValueLabel(timeText(twelveHour->time()));
  connect(twelveHour, &AdDatePicker::timeChanged, twelveHourValue,
          [twelveHourValue](const QTime& value) { twelveHourValue->setText(timeText(value)); });

  auto* twelveHourRow = new QWidget();
  auto* twelveHourLayout = new QHBoxLayout(twelveHourRow);
  twelveHourLayout->setContentsMargins(0, 0, 0, 0);
  twelveHourLayout->setSpacing(12);
  twelveHourLayout->addWidget(twelveHour);
  twelveHourLayout->addWidget(twelveHourValue);
  twelveHourLayout->addStretch();

  auto* range = new AdDateRangePicker();
  range->setAllowClear(true);
  range->setPickerMode(AdDatePickerPanel::PickerMode::Time);
  range->setTimeFormat(QStringLiteral("HH:mm:ss"));
  range->setTimeRange(QTime(12, 8, 23), QTime(18, 45, 0));
  auto* rangeValue = makeValueLabel(timeRangeText(range->startTime(), range->endTime()));
  connect(range, &AdDateRangePicker::timeRangeChanged, rangeValue,
          [rangeValue](const QTime& start, const QTime& end) {
            rangeValue->setText(timeRangeText(start, end));
          });

  auto* rangeRow = new QWidget();
  auto* rangeLayout = new QHBoxLayout(rangeRow);
  rangeLayout->setContentsMargins(0, 0, 0, 0);
  rangeLayout->setSpacing(12);
  rangeLayout->addWidget(range);
  rangeLayout->addWidget(rangeValue);
  rangeLayout->addStretch();

  auto* suffix = makePicker();
  suffix->setPickerMode(AdDatePickerPanel::PickerMode::Time);
  suffix->setSuffixIconRef(outlined_icons::Smile());
  suffix->setDefaultOpenTime(QTime(0, 0, 0));

  auto* prefix = makePicker();
  prefix->setPickerMode(AdDatePickerPanel::PickerMode::Time);
  prefix->setPrefixIconRef(outlined_icons::Smile());

  auto* hiddenSuffix = makePicker();
  hiddenSuffix->setPickerMode(AdDatePickerPanel::PickerMode::Time);
  hiddenSuffix->setSuffixIconVisible(false);
  hiddenSuffix->setPlaceholder(QStringLiteral("No suffix icon"));

  auto* iconRow = new QWidget();
  auto* iconLayout = new QHBoxLayout(iconRow);
  iconLayout->setContentsMargins(0, 0, 0, 0);
  iconLayout->setSpacing(12);
  iconLayout->addWidget(suffix);
  iconLayout->addWidget(prefix);
  iconLayout->addWidget(hiddenSuffix);
  iconLayout->addStretch();

  addLabeledRow(grid, 0, "Basic", basicRow);
  addLabeledRow(grid, 1, "Hide seconds", minuteOnlyRow);
  addLabeledRow(grid, 2, "12-hour time", twelveHourRow);
  addLabeledRow(grid, 3, "Range", rangeRow);
  addLabeledRow(grid, 4, "Prefix/suffix", iconRow);
  return box;
}

QWidget* DatePickerDocsPage::buildPrefixSuffixDemo() {
  auto* box = new QWidget();
  auto* grid = new QGridLayout(box);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(12);
  grid->setVerticalSpacing(10);

  auto* suffixIcon = makePicker(QDate(2026, 7, 6));
  suffixIcon->setSuffixIconRef(outlined_icons::Smile());

  auto* suffixText = makePicker(QDate(2026, 7, 6));
  suffixText->setSuffixText(QStringLiteral("UTC"));

  auto* prefixIcon = makePicker(QDate(2026, 7, 6));
  prefixIcon->setPickerMode(AdDatePickerPanel::PickerMode::Week);
  prefixIcon->setPrefixIconRef(outlined_icons::Smile());

  auto* prefixText = makePicker(QDate(2026, 7, 6));
  prefixText->setPickerMode(AdDatePickerPanel::PickerMode::Week);
  prefixText->setPrefixText(QStringLiteral("Event Period"));

  auto* clearIcon = makePicker(QDate(2026, 7, 6));
  clearIcon->setClearIconRef(outlined_icons::Close());

  auto* rangeSuffix = new AdDateRangePicker();
  rangeSuffix->setAllowClear(true);
  rangeSuffix->setSuffixIconRef(outlined_icons::Smile());
  rangeSuffix->setRange(QDate(2026, 7, 6), QDate(2026, 7, 16));

  auto* rangePrefix = new AdDateRangePicker();
  rangePrefix->setAllowClear(true);
  rangePrefix->setPickerMode(AdDatePickerPanel::PickerMode::Week);
  rangePrefix->setPrefixText(QStringLiteral("Event Period"));
  rangePrefix->setRange(QDate(2026, 7, 6), QDate(2026, 7, 16));

  auto* rangeClear = new AdDateRangePicker();
  rangeClear->setAllowClear(true);
  rangeClear->setClearIconRef(outlined_icons::Close());
  rangeClear->setRange(QDate(2026, 7, 6), QDate(2026, 7, 16));

  addLabeledRow(grid, 0, "Suffix icon", suffixIcon);
  addLabeledRow(grid, 1, "Suffix text", suffixText);
  addLabeledRow(grid, 2, "Prefix icon", prefixIcon);
  addLabeledRow(grid, 3, "Prefix text", prefixText);
  addLabeledRow(grid, 4, "Clear icon", clearIcon);
  addLabeledRow(grid, 5, "Range suffix", rangeSuffix);
  addLabeledRow(grid, 6, "Range prefix", rangePrefix);
  addLabeledRow(grid, 7, "Range clear", rangeClear);
  return box;
}

QWidget* DatePickerDocsPage::buildNavigationIconDemo() {
  auto* box = new QWidget();
  auto* grid = new QGridLayout(box);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(12);
  grid->setVerticalSpacing(10);

  const auto superPrevIcon = outlined_icons::StepBackward();
  const auto prevIcon = outlined_icons::CaretLeft();
  const auto nextIcon = outlined_icons::CaretRight();
  const auto superNextIcon = outlined_icons::StepForward();

  auto* panel = new AdDatePickerPanel();
  panel->setSelectedDate(QDate(2026, 7, 6));
  panel->setViewDate(QDate(2026, 7, 1));
  panel->setNavigationIconRefs(superPrevIcon, prevIcon, nextIcon, superNextIcon);

  auto* picker = makePicker(QDate(2026, 7, 6));
  picker->setSuperPrevIconRef(superPrevIcon);
  picker->setPrevIconRef(prevIcon);
  picker->setNextIconRef(nextIcon);
  picker->setSuperNextIconRef(superNextIcon);

  auto* range = new AdDateRangePicker();
  range->setAllowClear(true);
  range->setRange(QDate(2026, 7, 6), QDate(2026, 7, 16));
  range->setSuperPrevIconRef(superPrevIcon);
  range->setPrevIconRef(prevIcon);
  range->setNextIconRef(nextIcon);
  range->setSuperNextIconRef(superNextIcon);

  addLabeledRow(grid, 0, "Panel", panel);
  addLabeledRow(grid, 1, "Picker popup", picker);
  addLabeledRow(grid, 2, "Range popup", range);
  return box;
}

QWidget* DatePickerDocsPage::buildSemanticStyleDemo() {
  auto* box = new QWidget();
  auto* grid = new QGridLayout(box);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(12);
  grid->setVerticalSpacing(10);

  const auto styledSlots = []() {
    AdDatePicker::SemanticStyles styles;
    styles.input.backgroundColor = QColor(QStringLiteral("#f6ffed"));
    styles.input.borderColor = QColor(QStringLiteral("#52c41a"));
    styles.input.textColor = QColor(QStringLiteral("#135200"));
    styles.prefix.textColor = QColor(QStringLiteral("#389e0d"));
    styles.suffix.textColor = QColor(QStringLiteral("#389e0d"));
    styles.popup.root.backgroundColor = QColor(QStringLiteral("#ffffff"));
    styles.popup.root.borderColor = QColor(QStringLiteral("#b7eb8f"));
    styles.popup.header.backgroundColor = QColor(QStringLiteral("#f6ffed"));
    styles.popup.header.textColor = QColor(QStringLiteral("#135200"));
    styles.popup.content.backgroundColor = QColor(QStringLiteral("#ffffff"));
    styles.popup.item.borderColor = QColor(QStringLiteral("#d9f7be"));
    styles.popup.footer.backgroundColor = QColor(QStringLiteral("#fcffe6"));
    styles.popup.footer.textColor = QColor(QStringLiteral("#389e0d"));
    return styles;
  };

  auto* picker = makePicker(QDate(2026, 7, 6));
  picker->setPrefixText(QStringLiteral("Ship"));
  picker->setSuffixText(QStringLiteral("UTC"));
  picker->setSemanticStyles(styledSlots());

  auto* range = new AdDateRangePicker();
  range->setAllowClear(true);
  range->setRange(QDate(2026, 7, 6), QDate(2026, 7, 16));
  range->setPrefixIconRef(outlined_icons::Calendar());
  range->setSemanticStyleResolver([](const AdDateRangePicker::StyleContext& context) {
    AdDateRangePicker::SemanticStyles styles;
    const QColor accent = context.popupVisible ? QColor(QStringLiteral("#1677ff"))
                                               : QColor(QStringLiteral("#722ed1"));
    styles.input.borderColor = accent;
    styles.input.textColor = QColor(QStringLiteral("#141414"));
    styles.prefix.textColor = accent;
    styles.suffix.textColor = accent;
    styles.popup.root.borderColor = accent;
    styles.popup.header.textColor = accent;
    styles.popup.content.backgroundColor = QColor(QStringLiteral("#f8fbff"));
    styles.popup.item.borderColor = QColor(QStringLiteral("#e6f4ff"));
    styles.popup.footer.textColor = accent;
    return styles;
  });

  addLabeledRow(grid, 0, "Static slots", picker);
  addLabeledRow(grid, 1, "Resolver", range);
  return box;
}

QWidget* DatePickerDocsPage::buildComponentTokenDemo() {
  auto* box = new QWidget();
  auto* grid = new QGridLayout(box);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(12);
  grid->setVerticalSpacing(10);

  AdDatePicker::ComponentTokens tokens;
  tokens.presetsWidth = 160;
  tokens.presetsMaxWidth = 200;
  tokens.zIndexPopup = 888;
  tokens.timeColumnWidth = 80;
  tokens.timeColumnHeight = 250;
  tokens.timeCellHeight = 30;
  tokens.cellWidth = 64;
  tokens.cellHeight = 40;
  tokens.textHeight = 45;
  tokens.withoutTimeCellHeight = 70;
  tokens.borderRadius = 10;
  tokens.panelBackground = QColor(QStringLiteral("#ffffff"));
  tokens.panelBorderColor = QColor(QStringLiteral("#52c41a"));
  tokens.cellHoverBackground = QColor(QStringLiteral("#f0f0f0"));
  tokens.cellSelectedBackground = QColor(QStringLiteral("#722ed1"));
  tokens.cellRangeBackground = QColor(QStringLiteral("#efdbff"));
  tokens.cellRangeHoverBackground = QColor(QStringLiteral("#e6bbff"));
  tokens.cellRangeBorderColor = QColor(QStringLiteral("#52c41a"));
  tokens.multipleItemHeight = 24;
  tokens.multipleItemHeightSmall = 20;
  tokens.multipleItemHeightLarge = 28;
  tokens.multipleItemBackground = QColor(QStringLiteral("#fff1f0"));
  tokens.multipleItemBorderColor = QColor(QStringLiteral("#ff7875"));
  tokens.multipleItemTextDisabledColor = QColor(QStringLiteral("#a8071a"));
  tokens.multipleItemBorderColorDisabled = QColor(QStringLiteral("#ffa39e"));

  auto* single = makePicker(QDate(2026, 7, 6));
  single->setComponentTokens(tokens);

  auto* range = new AdDateRangePicker();
  range->setAllowClear(true);
  range->setRange(QDate(2026, 7, 6), QDate(2026, 7, 16));
  range->setComponentTokens(tokens);

  auto* month = makePicker(QDate(2026, 7, 1));
  month->setPickerMode(AdDatePicker::PickerMode::Month);
  month->setComponentTokens(tokens);

  auto* time = new AdDatePicker();
  time->setShowTime(true);
  time->setDateTime(QDateTime(QDate(2026, 7, 6), QTime(9, 30, 0)));
  time->setComponentTokens(tokens);

  auto* multiple = new AdDatePicker();
  multiple->setMultiple(true);
  multiple->setSelectedDates({QDate(2026, 7, 1), QDate(2026, 7, 3), QDate(2026, 7, 6)});
  multiple->setComponentTokens(tokens);

  addLabeledRow(grid, 0, "Single", single);
  addLabeledRow(grid, 1, "Range", range);
  addLabeledRow(grid, 2, "Month", month);
  addLabeledRow(grid, 3, "Time", time);
  addLabeledRow(grid, 4, "Multiple tags", multiple);
  return box;
}

QWidget* DatePickerDocsPage::buildSizeDemo() {
  auto* box = new QWidget();
  auto* grid = new QGridLayout(box);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(12);
  grid->setVerticalSpacing(10);

  auto* large = makePicker(QDate::currentDate());
  large->setSize(AdDatePicker::Size::Large);
  auto* middle = makePicker(QDate::currentDate());
  auto* small = makePicker(QDate::currentDate());
  small->setSize(AdDatePicker::Size::Small);

  addLabeledRow(grid, 0, "Large", large);
  addLabeledRow(grid, 1, "Middle", middle);
  addLabeledRow(grid, 2, "Small", small);
  return box;
}

QWidget* DatePickerDocsPage::buildVariantDemo() {
  auto* box = new QWidget();
  auto* grid = new QGridLayout(box);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(12);
  grid->setVerticalSpacing(10);

  struct Row {
    QString label;
    AdDatePicker::Variant variant;
  };
  const QVector<Row> rows = {
      {QStringLiteral("Outlined"), AdDatePicker::Variant::Outlined},
      {QStringLiteral("Filled"), AdDatePicker::Variant::Filled},
      {QStringLiteral("Borderless"), AdDatePicker::Variant::Borderless},
      {QStringLiteral("Underlined"), AdDatePicker::Variant::Underlined},
  };
  for (int i = 0; i < rows.size(); ++i) {
    auto* picker = makePicker(QDate::currentDate());
    picker->setVariant(rows.at(i).variant);
    addLabeledRow(grid, i, rows.at(i).label, picker);
  }
  return box;
}

QWidget* DatePickerDocsPage::buildStatusDisabledDemo() {
  auto* box = new QWidget();
  auto* grid = new QGridLayout(box);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(12);
  grid->setVerticalSpacing(10);

  auto* error = makePicker(QDate::currentDate());
  error->setStatus(AdDatePicker::Status::Error);
  auto* warning = makePicker(QDate::currentDate());
  warning->setStatus(AdDatePicker::Status::Warning);
  auto* disabledDate = makePicker(QDate(2015, 6, 6));
  disabledDate->setDisabled(true);
  auto* disabledMonth = makePicker(QDate(2015, 6, 1));
  disabledMonth->setPickerMode(AdDatePickerPanel::PickerMode::Month);
  disabledMonth->setDisplayFormat(QStringLiteral("yyyy-MM"));
  disabledMonth->setDisabled(true);
  auto* disabledRange = new AdDateRangePicker();
  disabledRange->setRange(QDate(2015, 6, 6), QDate(2015, 6, 6));
  disabledRange->setDisabled(true);
  auto* disabledPast = makePicker(QDate::currentDate());
  disabledPast->setDisabledDatePredicate(
      [](const QDate& date) { return date.isValid() && date < QDate::currentDate(); });
  auto* businessHours = makePicker(QDate::currentDate());
  businessHours->setShowTime(true);
  businessHours->setTimeFormat(QStringLiteral("HH:mm"));
  businessHours->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
  businessHours->setHideDisabledOptions(true);
  businessHours->setDisabledTimePredicate(
      [](const QDate&, const QTime& time, const AdDatePicker::DisabledTimeContext&) {
        return time.isValid() && (time < QTime(9, 0) || time > QTime(17, 0));
      });
  auto* disabledRangeEnd = new AdDateRangePicker();
  disabledRangeEnd->setRange(QDate(2019, 9, 3), QDate(2019, 11, 22));
  disabledRangeEnd->setEndDisabled(true);
  auto* minMax = makePicker(QDate(2019, 9, 3));
  minMax->setMinDate(QDate(2019, 6, 1));
  minMax->setMaxDate(QDate(2020, 6, 30));

  addLabeledRow(grid, 0, "Error", error);
  addLabeledRow(grid, 1, "Warning", warning);
  addLabeledRow(grid, 2, "Disabled date", disabledDate);
  addLabeledRow(grid, 3, "Disabled month", disabledMonth);
  addLabeledRow(grid, 4, "Disabled range", disabledRange);
  addLabeledRow(grid, 5, "Disabled end", disabledRangeEnd);
  addLabeledRow(grid, 6, "Min/max date", minMax);
  addLabeledRow(grid, 7, "No past dates", disabledPast);
  addLabeledRow(grid, 8, "Business hours", businessHours);
  return box;
}

QWidget* DatePickerDocsPage::buildPresetFooterDemo() {
  auto* box = new QWidget();
  auto* grid = new QGridLayout(box);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(12);
  grid->setVerticalSpacing(10);

  auto* single = makePicker(QDate::currentDate());
  AdDatePicker::PresetItem today;
  today.label = QStringLiteral("Today");
  today.value = QDate::currentDate();
  AdDatePicker::PresetItem nextWeek;
  nextWeek.label = QStringLiteral("Next week");
  nextWeek.valueProvider = []() { return QDate::currentDate().addDays(7); };
  single->setPresets({today, nextWeek});
  auto* singleFooter =
      new QLabel(QStringLiteral("Extra footer: presets can be fixed dates or providers."));
  singleFooter->setWordWrap(true);
  single->setExtraFooterWidget(singleFooter);

  auto* range = new AdDateRangePicker();
  range->setAllowClear(true);
  AdDateRangePicker::PresetItem thisWeek;
  thisWeek.label = QStringLiteral("This week");
  thisWeek.rangeStartValue = QDate::currentDate().addDays(-(QDate::currentDate().dayOfWeek() - 1));
  thisWeek.rangeEndValue = thisWeek.rangeStartValue.addDays(6);
  AdDateRangePicker::PresetItem next30;
  next30.label = QStringLiteral("Next 30 days");
  next30.rangeValueProvider = []() {
    const QDate today = QDate::currentDate();
    return std::make_pair(today, today.addDays(29));
  };
  range->setPresets({thisWeek, next30});
  auto* rangeFooter = new QLabel(
      QStringLiteral("Extra footer: range presets can compute both endpoints on click."));
  rangeFooter->setWordWrap(true);
  range->setExtraFooterWidget(rangeFooter);

  addLabeledRow(grid, 0, "Single", single);
  addLabeledRow(grid, 1, "Range", range);
  return box;
}

QWidget* DatePickerDocsPage::buildPlacementDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(12);

  auto* placementBox = new QComboBox();
  placementBox->addItem(QStringLiteral("topLeft"),
                        static_cast<int>(AdDatePicker::Placement::TopLeft));
  placementBox->addItem(QStringLiteral("topRight"),
                        static_cast<int>(AdDatePicker::Placement::TopRight));
  placementBox->addItem(QStringLiteral("bottomLeft"),
                        static_cast<int>(AdDatePicker::Placement::BottomLeft));
  placementBox->addItem(QStringLiteral("bottomRight"),
                        static_cast<int>(AdDatePicker::Placement::BottomRight));

  auto* single = makePicker(QDate(2026, 7, 6));
  auto* range = new AdDateRangePicker();
  range->setAllowClear(true);
  range->setRange(QDate(2026, 7, 6), QDate(2026, 7, 16));

  const auto applyPlacement = [placementBox, single, range]() {
    const auto placement =
        static_cast<AdDatePicker::Placement>(placementBox->currentData().toInt());
    single->setPlacement(placement);
    range->setPlacement(placement);
  };
  applyPlacement();
  connect(placementBox, &QComboBox::currentIndexChanged, single, applyPlacement);

  row->addWidget(placementBox);
  row->addWidget(single);
  row->addWidget(range);
  row->addStretch();
  return box;
}

QWidget* DatePickerDocsPage::buildPopupLayerModeDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(12);

  auto* modeBox = new QComboBox();
  modeBox->addItem(QStringLiteral("InWindow"), static_cast<int>(AdPopupLayerMode::InWindow));
  modeBox->addItem(QStringLiteral("QtTool"), static_cast<int>(AdPopupLayerMode::QtTool));

  auto* picker = makePicker(QDate::currentDate());
  connect(modeBox, &QComboBox::currentIndexChanged, picker, [modeBox, picker]() {
    picker->setPopupLayerMode(static_cast<AdPopupLayerMode>(modeBox->currentData().toInt()));
  });

  row->addWidget(modeBox);
  row->addWidget(picker);
  row->addStretch();
  return box;
}

QWidget* DatePickerDocsPage::buildPopupContentWrapperDemo() {
  auto* box = new QWidget();
  auto* grid = new QGridLayout(box);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(12);
  grid->setVerticalSpacing(10);

  const auto wrapPanel = [](QWidget* origin, QWidget* parent, const QString& title) {
    auto* wrapper = new QWidget(parent);
    auto* layout = new QVBoxLayout(wrapper);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);
    auto* titleLabel = new QLabel(title, wrapper);
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    layout->addWidget(titleLabel);
    layout->addWidget(origin);
    return wrapper;
  };

  auto* single = makePicker(QDate(2026, 7, 6));
  single->setPopupContentWrapperFactory([wrapPanel](QWidget* origin, QWidget* parent) {
    return wrapPanel(origin, parent, QStringLiteral("Wrapped Date Panel"));
  });

  auto* range = new AdDateRangePicker();
  range->setAllowClear(true);
  range->setRange(QDate(2026, 7, 6), QDate(2026, 7, 16));
  range->setPopupContentWrapperFactory([wrapPanel](QWidget* origin, QWidget* parent) {
    return wrapPanel(origin, parent, QStringLiteral("Wrapped Range Panel"));
  });

  addLabeledRow(grid, 0, "Single", single);
  addLabeledRow(grid, 1, "Range", range);
  return box;
}

QWidget* DatePickerDocsPage::buildPanelComponentDemo() {
  auto* box = new QWidget();
  auto* grid = new QGridLayout(box);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(12);
  grid->setVerticalSpacing(10);

  const auto makeComponent = [](const AdDatePicker::PanelComponentContext& context,
                                QWidget* parent) -> QWidget* {
    auto* panel = new QWidget(parent);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    QString roleText = QStringLiteral("Date");
    if (context.role == AdDatePicker::PanelComponentRole::RangeStart) {
      roleText = QStringLiteral("Range start");
    } else if (context.role == AdDatePicker::PanelComponentRole::RangeEnd) {
      roleText = QStringLiteral("Range end");
    }

    auto* title = new QLabel(roleText, panel);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    const QDate base =
        context.selectedDate.isValid()
            ? context.selectedDate
            : (context.rangeStartDate.isValid() ? context.rangeStartDate : QDate(2026, 7, 6));
    auto* value = new QLabel(dateText(base), panel);
    layout->addWidget(value);

    auto* preview = new QPushButton(QStringLiteral("Preview +1d"), panel);
    QObject::connect(preview, &QPushButton::clicked, panel,
                     [previewDate = context.previewDate, base]() {
                       if (previewDate) {
                         previewDate(base.addDays(1));
                       }
                     });
    layout->addWidget(preview);

    auto* select = new QPushButton(QStringLiteral("Select +2d"), panel);
    QObject::connect(select, &QPushButton::clicked, panel,
                     [selectDate = context.selectDate, base]() {
                       if (selectDate) {
                         selectDate(base.addDays(2));
                       }
                     });
    layout->addWidget(select);

    auto* nextMonth = new QPushButton(QStringLiteral("View next month"), panel);
    QObject::connect(nextMonth, &QPushButton::clicked, panel,
                     [setViewDate = context.setViewDate, base]() {
                       if (setViewDate) {
                         setViewDate(base.addMonths(1));
                       }
                     });
    layout->addWidget(nextMonth);

    return panel;
  };

  auto* single = makePicker(QDate(2026, 7, 6));
  single->setPanelComponentFactory(makeComponent);

  auto* range = new AdDateRangePicker();
  range->setAllowClear(true);
  range->setRange(QDate(2026, 7, 6), QDate(2026, 7, 16));
  range->setPanelComponentFactory(makeComponent);

  addLabeledRow(grid, 0, "Single", single);
  addLabeledRow(grid, 1, "Range", range);
  return box;
}

QWidget* DatePickerDocsPage::buildPanelDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(12);

  auto* panel = new AdDatePickerPanel();
  panel->setSelectedDate(QDate::currentDate());
  auto* value = makeValueLabel(dateText(panel->selectedDate()));
  connect(panel, &AdDatePickerPanel::selectedDateChanged, value,
          [value](const QDate& date) { value->setText(dateText(date)); });

  row->addWidget(panel);
  row->addWidget(value);
  row->addStretch();
  return box;
}
