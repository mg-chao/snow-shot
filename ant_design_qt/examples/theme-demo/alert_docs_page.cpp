#include "alert_docs_page.h"

#include "demo_theme_utils.h"

#include <QElapsedTimer>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

using adqt::widgets::AdAlert;
using adqt::widgets::AdButton;

namespace {

template <typename Fn>
AdAlert* configureAlert(AdAlert* alert, Fn&& fn) {
  if (!alert) {
    return nullptr;
  }
  fn(alert);
  return alert;
}

class LoopingTextLabel final : public QWidget {
 public:
  explicit LoopingTextLabel(const QString& text, QWidget* parent = nullptr)
      : QWidget(parent), source_(text) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setAttribute(Qt::WA_Hover, true);
    setAccessibleName(text);
    timer_.setInterval(16);
    connect(&timer_, &QTimer::timeout, this, [this]() { tick(); });
    refreshMetrics();
    elapsed_.start();
    timer_.start();
  }

  QSize sizeHint() const override {
    const QFontMetrics metrics(font());
    return QSize(std::max(metrics.horizontalAdvance(source_), 1), metrics.height() + 2);
  }

  QSize minimumSizeHint() const override {
    const QFontMetrics metrics(font());
    return QSize(0, metrics.height() + 2);
  }

 private:
  bool event(QEvent* event) override {
    if (event) {
      if (event->type() == QEvent::Enter) {
        paused_ = true;
      } else if (event->type() == QEvent::Leave) {
        paused_ = false;
        elapsed_.restart();
      } else if (event->type() == QEvent::FontChange) {
        refreshMetrics();
      }
    }
    return QWidget::event(event);
  }

  void resizeEvent(QResizeEvent* event) override {
    QWidget::resizeEvent(event);
    refreshMetrics();
  }

  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event)
    if (source_.isEmpty()) {
      return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setPen(palette().color(QPalette::WindowText));

    const QFontMetrics metrics(font());
    const int baseline = (height() - metrics.height()) / 2 + metrics.ascent();
    const qreal cycle = static_cast<qreal>(textWidth_ + gapPx_);
    if (cycle <= 0.0) {
      painter.drawText(0, baseline, source_);
      return;
    }

    qreal x = -offsetPx_;
    while (x < width()) {
      painter.drawText(QPointF(x, baseline), source_);
      x += cycle;
    }
  }

  void refreshMetrics() {
    const QFontMetrics metrics(font());
    textWidth_ = std::max(metrics.horizontalAdvance(source_), 1);
    gapPx_ = std::max(metrics.horizontalAdvance(QStringLiteral("    ")), 24);
    const qreal cycle = static_cast<qreal>(textWidth_ + gapPx_);
    if (cycle > 0.0) {
      offsetPx_ = std::fmod(offsetPx_, cycle);
      if (offsetPx_ < 0.0) {
        offsetPx_ += cycle;
      }
    } else {
      offsetPx_ = 0.0;
    }
    updateGeometry();
    update();
  }

  void tick() {
    if (source_.isEmpty() || paused_) {
      return;
    }

    const qint64 elapsedMs = elapsed_.restart();
    const qreal frameMs = elapsedMs > 0 ? static_cast<qreal>(elapsedMs) : 16.0;
    offsetPx_ += (speedPxPerSecond_ * frameMs) / 1000.0;

    const qreal cycle = static_cast<qreal>(textWidth_ + gapPx_);
    if (cycle > 0.0) {
      while (offsetPx_ >= cycle) {
        offsetPx_ -= cycle;
      }
    }
    update();
  }

  QString source_;
  QTimer timer_;
  QElapsedTimer elapsed_;
  qreal offsetPx_ = 0.0;
  int textWidth_ = 1;
  int gapPx_ = 24;
  qreal speedPxPerSecond_ = 50.0;
  bool paused_ = false;
};

QLabel* makeBadge(const QString& text, QWidget* parent = nullptr) {
  auto* badge = new QLabel(text, parent);
  badge->setAlignment(Qt::AlignCenter);
  badge->setFrameShape(QFrame::StyledPanel);
  badge->setMinimumWidth(40);
  badge->setContentsMargins(8, 4, 8, 4);
  badge->setAccessibleName(text);
  return badge;
}

}  // namespace

AlertDocsPage::AlertDocsPage(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(16, 16, 16, 24);
  root->setSpacing(16);

  auto* title = new QLabel("Alert");
  QFont titleFont = title->font();
  titleFont.setPointSize(titleFont.pointSize() + 8);
  titleFont.setBold(true);
  title->setFont(titleFont);
  root->addWidget(title);

  auto* subtitle = new QLabel(
      "Display warning messages that require attention. This page demonstrates the Qt-native "
      "AdAlert API. text/informativeText are plain text; custom leading, body and action content "
      "use hosted widgets; close() now follows QWidget close semantics.");
  subtitle->setWordWrap(true);
  root->addWidget(subtitle);

  addSection(root, "Basic", "A single success alert using plain text content.", buildBasicDemo());
  addSection(root, "More types", "Severity-driven visual styles.", buildStyleDemo());
  addSection(root, "Closable", "Close button path emits closeRequested() and closed().",
             buildClosableDemo());
  addSection(root, "Informative Text", "Two-line alerts with title and supporting content.",
             buildDescriptionDemo());
  addSection(root, "Icon", "Inline alerts can opt into a visible severity icon.", buildIconDemo());
  addSection(root, "Banner", "Banner mode stretches edge-to-edge and defaults to a visible icon.",
             buildBannerDemo());
  addSection(root, "Loop Banner", "Custom text widget content in banner mode.",
             buildLoopBannerDemo());
  addSection(root, "Leading Widget", "Custom leading content replaces token-based custom icons.",
             buildLeadingWidgetDemo());
  addSection(root, "Qt Close Lifecycle", "Programmatic close() and reopen behavior.",
             buildLifecycleDemo());
  addSection(root, "Custom action", "Hosted action widgets compose with close and content.",
             buildActionDemo());
  addSection(root, "Palette Override", "Explicit QPalette and QFont customize the component.",
             buildPaletteDemo());

  root->addStretch();
}

const QVector<QWidget*>& AlertDocsPage::sectionAnchors() const { return anchors_; }

const QStringList& AlertDocsPage::sectionTitles() const { return titles_; }

void AlertDocsPage::addSection(QVBoxLayout* root, const QString& title, const QString& description,
                               QWidget* content) {
  auto* panel = new QFrame();
  panel->setFrameShape(QFrame::StyledPanel);
  auto* layout = new QVBoxLayout(panel);
  layout->setContentsMargins(14, 14, 14, 14);
  layout->setSpacing(10);

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

AdAlert* AlertDocsPage::makeAlert(const QString& text, std::optional<AdAlert::Severity> severity,
                                  std::optional<AdAlert::IconMode> iconMode, bool closable,
                                  const QString& informativeText, QWidget* parent) {
  auto* alert = new AdAlert(parent);
  if (severity.has_value()) {
    alert->setSeverity(severity.value());
  }
  alert->setText(text);
  if (iconMode.has_value()) {
    alert->setIconMode(iconMode.value());
  }
  alert->setClosable(closable);
  alert->setInformativeText(informativeText);
  return alert;
}

QWidget* AlertDocsPage::buildBasicDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);
  layout->addWidget(makeAlert("Success Text", AdAlert::Severity::Success));
  return box;
}

QWidget* AlertDocsPage::buildStyleDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);
  layout->addWidget(makeAlert("Success Text", AdAlert::Severity::Success));
  layout->addWidget(makeAlert("Info Text", AdAlert::Severity::Info));
  layout->addWidget(makeAlert("Warning Text", AdAlert::Severity::Warning));
  layout->addWidget(makeAlert("Error Text", AdAlert::Severity::Error));
  return box;
}

QWidget* AlertDocsPage::buildClosableDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* status = new QLabel("Close status: waiting.");
  const QList<QPair<QString, AdAlert::Severity>> items = {
      {"Warning Title", AdAlert::Severity::Warning},
      {"Success Title", AdAlert::Severity::Success},
      {"Info Title", AdAlert::Severity::Info},
      {"Error Title", AdAlert::Severity::Error},
  };
  for (const auto& item : items) {
    AdAlert* alert = makeAlert(item.first, item.second, AdAlert::IconMode::Hidden, true);
    connect(alert, &AdAlert::closeRequested, status,
            [status, title = item.first](AdAlert::CloseReason) {
              status->setText(QStringLiteral("Close status: requested \"%1\"").arg(title));
            });
    connect(alert, &AdAlert::closed, status, [status, title = item.first](AdAlert::CloseReason) {
      status->setText(QStringLiteral("Close status: closed \"%1\"").arg(title));
    });
    layout->addWidget(alert);
  }
  layout->addWidget(status);
  return box;
}

QWidget* AlertDocsPage::buildDescriptionDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  layout->addWidget(makeAlert("Success Text", AdAlert::Severity::Success, AdAlert::IconMode::Hidden,
                              false,
                              "Success Description Success Description Success Description"));
  layout->addWidget(
      makeAlert("Info Text", AdAlert::Severity::Info, AdAlert::IconMode::Hidden, false,
                "Info Description Info Description Info Description Info Description"));
  layout->addWidget(
      makeAlert("Warning Text", AdAlert::Severity::Warning, AdAlert::IconMode::Hidden, false,
                "Warning Description Warning Description Warning Description Warning Description"));
  layout->addWidget(
      makeAlert("Error Text", AdAlert::Severity::Error, AdAlert::IconMode::Hidden, false,
                "Error Description Error Description Error Description Error Description"));
  return box;
}

QWidget* AlertDocsPage::buildIconDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  layout->addWidget(
      makeAlert("Success Tips", AdAlert::Severity::Success, AdAlert::IconMode::Visible));
  layout->addWidget(
      makeAlert("Informational Notes", AdAlert::Severity::Info, AdAlert::IconMode::Visible));
  layout->addWidget(
      makeAlert("Warning", AdAlert::Severity::Warning, AdAlert::IconMode::Visible, true));
  layout->addWidget(makeAlert("Error", AdAlert::Severity::Error, AdAlert::IconMode::Visible));
  layout->addWidget(makeAlert("Success Tips", AdAlert::Severity::Success,
                              AdAlert::IconMode::Visible, false,
                              "Detailed description and advice about successful copywriting."));
  layout->addWidget(makeAlert("Informational Notes", AdAlert::Severity::Info,
                              AdAlert::IconMode::Visible, false,
                              "Additional description and information about copywriting."));
  layout->addWidget(makeAlert("Warning", AdAlert::Severity::Warning, AdAlert::IconMode::Visible,
                              true, "This is a warning notice about copywriting."));
  layout->addWidget(makeAlert("Error", AdAlert::Severity::Error, AdAlert::IconMode::Visible, false,
                              "This is an error message about copywriting."));

  return box;
}

QWidget* AlertDocsPage::buildBannerDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* first = makeAlert("Warning text", AdAlert::Severity::Warning);
  configureAlert(first,
                 [](AdAlert* alert) { alert->setDisplayMode(AdAlert::DisplayMode::Banner); });
  layout->addWidget(first);

  auto* second = makeAlert("Very long warning text warning text text text text text text text",
                           AdAlert::Severity::Warning);
  configureAlert(second, [](AdAlert* alert) {
    alert->setDisplayMode(AdAlert::DisplayMode::Banner);
    alert->setClosable(true);
  });
  layout->addWidget(second);

  auto* third = makeAlert("Warning text without icon", AdAlert::Severity::Warning);
  configureAlert(third, [](AdAlert* alert) {
    alert->setDisplayMode(AdAlert::DisplayMode::Banner);
    alert->setIconMode(AdAlert::IconMode::Hidden);
  });
  layout->addWidget(third);

  auto* fourth = makeAlert("Error text", AdAlert::Severity::Error);
  configureAlert(fourth,
                 [](AdAlert* alert) { alert->setDisplayMode(AdAlert::DisplayMode::Banner); });
  layout->addWidget(fourth);

  return box;
}

QWidget* AlertDocsPage::buildLoopBannerDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  auto* alert = makeAlert(QString());
  configureAlert(alert, [](AdAlert* value) {
    value->setSeverity(AdAlert::Severity::Warning);
    value->setDisplayMode(AdAlert::DisplayMode::Banner);
    value->setTextWidget(
        new LoopingTextLabel("I can be a QWidget, multiple widgets, or just some text.", value));
  });
  layout->addWidget(alert);
  return box;
}

QWidget* AlertDocsPage::buildLeadingWidgetDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* first = makeAlert("Network status", AdAlert::Severity::Info, AdAlert::IconMode::Hidden);
  configureAlert(first, [](AdAlert* alert) { alert->setLeadingWidget(makeBadge("NET", alert)); });
  layout->addWidget(first);

  auto* second =
      makeAlert("Deployment paused", AdAlert::Severity::Warning, AdAlert::IconMode::Hidden, false,
                "Custom leading content can replace the default severity icon.");
  configureAlert(second, [](AdAlert* alert) { alert->setLeadingWidget(makeBadge("OPS", alert)); });
  layout->addWidget(second);

  layout->addWidget(
      makeHintLabel("Use setLeadingWidget() for custom iconography or compact semantic badges "
                    "instead of token-based icon overrides."));
  return box;
}

QWidget* AlertDocsPage::buildLifecycleDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* alert =
      makeAlert("Alert Message Text", AdAlert::Severity::Success, AdAlert::IconMode::Hidden, true);
  auto* status = new QLabel("Status: open");
  auto* buttonsRow = new QHBoxLayout();
  buttonsRow->setContentsMargins(0, 0, 0, 0);
  buttonsRow->setSpacing(8);

  auto* closeProgrammatically = new AdButton("Call close()");
  closeProgrammatically->setSizeClass(AdButton::SizeClass::Small);

  auto* reopen = new AdButton("Reopen");
  reopen->setSizeClass(AdButton::SizeClass::Small);
  reopen->setEnabled(false);

  buttonsRow->addWidget(closeProgrammatically);
  buttonsRow->addWidget(reopen);
  buttonsRow->addStretch();

  connect(alert, &AdAlert::closeRequested, this, [status](AdAlert::CloseReason reason) {
    const QString reasonText = reason == AdAlert::CloseReason::CloseButton
                                   ? QStringLiteral("close button")
                                   : QStringLiteral("programmatic close()");
    status->setText(QStringLiteral("Status: close requested via %1").arg(reasonText));
  });
  connect(alert, &AdAlert::closed, this, [status, reopen](AdAlert::CloseReason reason) {
    const QString reasonText = reason == AdAlert::CloseReason::CloseButton
                                   ? QStringLiteral("close button")
                                   : QStringLiteral("programmatic close()");
    status->setText(QStringLiteral("Status: closed via %1").arg(reasonText));
    reopen->setEnabled(true);
  });
  connect(closeProgrammatically, &QAbstractButton::clicked, alert, [alert]() { alert->close(); });
  connect(reopen, &QAbstractButton::clicked, alert, [alert, status, reopen]() {
    alert->show();
    status->setText(QStringLiteral("Status: open"));
    reopen->setEnabled(false);
  });

  layout->addWidget(alert);
  layout->addWidget(status);
  layout->addLayout(buttonsRow);
  layout->addWidget(makeHintLabel(
      "The close button and direct close() now use the same QWidget close lifecycle."));
  return box;
}

QWidget* AlertDocsPage::buildActionDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* first =
      makeAlert("Success Tips", AdAlert::Severity::Success, AdAlert::IconMode::Visible, true);
  auto* undo = new AdButton("UNDO");
  undo->setSizeClass(AdButton::SizeClass::Small);
  undo->setButtonStyle(AdButton::ButtonStyle::Text);
  undo->setAccentRole(AdButton::AccentRole::Neutral);
  configureAlert(first, [undo](AdAlert* alert) { alert->setActionsWidget(undo); });
  layout->addWidget(first);

  auto* second =
      makeAlert("Error Text", AdAlert::Severity::Error, AdAlert::IconMode::Visible, false,
                "Error Description Error Description Error Description Error Description");
  auto* detail = new AdButton("Detail");
  detail->setSizeClass(AdButton::SizeClass::Small);
  detail->setAccentRole(AdButton::AccentRole::Danger);
  configureAlert(second, [detail](AdAlert* alert) { alert->setActionsWidget(detail); });
  layout->addWidget(second);

  auto* third = makeAlert("Info Text", AdAlert::Severity::Info, AdAlert::IconMode::Hidden, true,
                          "Info Description Info Description Info Description Info Description");
  auto* actions = new QWidget();
  auto* actionsLayout = new QVBoxLayout(actions);
  actionsLayout->setContentsMargins(0, 0, 0, 0);
  actionsLayout->setSpacing(4);

  auto* accept = new AdButton("Accept");
  accept->setSizeClass(AdButton::SizeClass::Small);
  accept->setButtonStyle(AdButton::ButtonStyle::Solid);
  accept->setAccentRole(AdButton::AccentRole::Primary);

  auto* decline = new AdButton("Decline");
  decline->setSizeClass(AdButton::SizeClass::Small);
  decline->setAccentRole(AdButton::AccentRole::Danger);
  decline->setButtonStyle(AdButton::ButtonStyle::GhostOutline);

  actionsLayout->addWidget(accept);
  actionsLayout->addWidget(decline);
  configureAlert(third, [actions](AdAlert* alert) { alert->setActionsWidget(actions); });
  layout->addWidget(third);

  return box;
}

QWidget* AlertDocsPage::buildPaletteDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* alert =
      makeAlert("Palette Override", AdAlert::Severity::Info, AdAlert::IconMode::Visible, true,
                "Explicit QPalette/QFont overrides now form the public styling path.");

  QPalette palette = alert->palette();
  palette.setColor(QPalette::Window, QColor(245, 250, 255));
  palette.setColor(QPalette::Mid, QColor("#9dc7f7"));
  palette.setColor(QPalette::WindowText, QColor("#0f3d73"));
  palette.setColor(QPalette::Text, QColor("#205493"));
  palette.setColor(QPalette::ButtonText, QColor("#0f3d73"));
  palette.setColor(QPalette::AlternateBase, QColor(220, 236, 255));
  palette.setColor(QPalette::Button, QColor(204, 226, 255));
  palette.setColor(QPalette::Highlight, QColor("#1677ff"));
  alert->setPalette(palette);

  QFont font = alert->font();
  font.setPointSize(font.pointSize() + 1);
  alert->setFont(font);

  layout->addWidget(alert);
  layout->addWidget(
      makeHintLabel("Use theme defaults for consistency; apply palette/font overrides when a host "
                    "view needs semantic emphasis."));
  return box;
}
