#include "notification_docs_page.h"

#include "antd_icons.h"
#include "widgets/detail/timing_hub.h"
#include "widgets/widgets.h"

#include <QAbstractButton>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QVBoxLayout>

using adqt::widgets::AdButton;
using adqt::widgets::AdNotification;
using adqt::widgets::AdNotificationService;
namespace outlined_icons = adqt::icons::antd::outlined;

namespace {

AdButton* makeButton(const QString& text, QWidget* parent = nullptr) {
  auto* button = new AdButton(text, parent);
  button->setSizeClass(AdButton::SizeClass::Small);
  return button;
}

QWidget* makeButtonRow(const QList<AdButton*>& buttons) {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8);
  for (AdButton* button : buttons) {
    row->addWidget(button);
  }
  row->addStretch();
  return box;
}

AdNotification::Request notificationRequest(const QString& title, const QString& description) {
  AdNotification::Request request;
  request.title = title;
  request.description = description;
  return request;
}

AdNotification* notificationsFor(QWidget* context) {
  return AdNotificationService::instance(context);
}

}  // namespace

NotificationDocsPage::NotificationDocsPage(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(16, 16, 16, 24);
  root->setSpacing(16);

  auto* title = new QLabel(QStringLiteral("Notification"));
  QFont titleFont = title->font();
  titleFont.setPointSize(titleFont.pointSize() + 8);
  titleFont.setBold(true);
  title->setFont(titleFont);
  root->addWidget(title);

  auto* subtitle = new QLabel(
      QStringLiteral("Display a notification message globally, at any of six window positions."));
  subtitle->setWordWrap(true);
  root->addWidget(subtitle);

  addSection(root, QStringLiteral("Basic"),
             QStringLiteral("A standard notification with a title and description."),
             buildBasicDemo());
  addSection(root, QStringLiteral("Other types"),
             QStringLiteral("Success, info, warning, and error states."), buildTypesDemo());
  addSection(root, QStringLiteral("Placement"),
             QStringLiteral("Open a notification at any supported placement."),
             buildPlacementDemo());
  addSection(root, QStringLiteral("Duration"),
             QStringLiteral("Override the default 4.5-second lifetime."), buildDurationDemo());
  addSection(root, QStringLiteral("Progress"),
             QStringLiteral("Show the remaining lifetime along the lower edge."),
             buildProgressDemo());
  addSection(root, QStringLiteral("Stack"),
             QStringLiteral("Collapse busy notification groups until hovered."), buildStackDemo());
  addSection(root, QStringLiteral("Update by key"),
             QStringLiteral("Update an existing notification in place."), buildUpdateDemo());
  addSection(root, QStringLiteral("Actions"),
             QStringLiteral("Host QWidget actions inside the notification."), buildActionsDemo());
  addSection(root, QStringLiteral("Custom style"),
             QStringLiteral("Override component tokens and semantic slots."),
             buildCustomStyleDemo());
  root->addStretch();
}

const QVector<QWidget*>& NotificationDocsPage::sectionAnchors() const { return anchors_; }

const QStringList& NotificationDocsPage::sectionTitles() const { return titles_; }

void NotificationDocsPage::addSection(QVBoxLayout* root, const QString& title,
                                      const QString& description, QWidget* content) {
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
  auto* descriptionLabel = new QLabel(description);
  descriptionLabel->setWordWrap(true);
  layout->addWidget(titleLabel);
  layout->addWidget(descriptionLabel);
  layout->addWidget(content);
  root->addWidget(panel);
  anchors_.append(panel);
  titles_.append(title);
}

QWidget* NotificationDocsPage::buildBasicDemo() {
  auto* button = makeButton(QStringLiteral("Open notification"));
  button->setButtonStyle(AdButton::ButtonStyle::Solid);
  button->setAccentRole(AdButton::AccentRole::Primary);
  connect(button, &QAbstractButton::clicked, this, [this]() {
    AdNotificationService::open(
        notificationRequest(QStringLiteral("Notification Title"),
                            QStringLiteral("This is the content of the notification.")),
        this);
  });
  return makeButtonRow({button});
}

QWidget* NotificationDocsPage::buildTypesDemo() {
  auto* success = makeButton(QStringLiteral("Success"));
  auto* info = makeButton(QStringLiteral("Info"));
  auto* warning = makeButton(QStringLiteral("Warning"));
  auto* error = makeButton(QStringLiteral("Error"));
  auto bind = [this](AdButton* button, AdNotification::Type type, const QString& title) {
    connect(button, &QAbstractButton::clicked, this, [this, type, title]() {
      AdNotification::Request request = notificationRequest(
          title, QStringLiteral("This notification uses the matching status icon."));
      request.type = type;
      AdNotificationService::open(request, this);
    });
  };
  bind(success, AdNotification::Type::Success, QStringLiteral("Success"));
  bind(info, AdNotification::Type::Info, QStringLiteral("Information"));
  bind(warning, AdNotification::Type::Warning, QStringLiteral("Warning"));
  bind(error, AdNotification::Type::Error, QStringLiteral("Error"));
  return makeButtonRow({success, info, warning, error});
}

QWidget* NotificationDocsPage::buildPlacementDemo() {
  auto* box = new QWidget();
  auto* grid = new QGridLayout(box);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setSpacing(8);
  const QList<QPair<QString, AdNotification::Placement>> placements = {
      {QStringLiteral("Top Left"), AdNotification::Placement::TopLeft},
      {QStringLiteral("Top"), AdNotification::Placement::Top},
      {QStringLiteral("Top Right"), AdNotification::Placement::TopRight},
      {QStringLiteral("Bottom Left"), AdNotification::Placement::BottomLeft},
      {QStringLiteral("Bottom"), AdNotification::Placement::Bottom},
      {QStringLiteral("Bottom Right"), AdNotification::Placement::BottomRight}};
  for (int i = 0; i < placements.size(); ++i) {
    auto* button = makeButton(placements.at(i).first);
    const AdNotification::Placement placement = placements.at(i).second;
    connect(button, &QAbstractButton::clicked, this, [this, placement]() {
      AdNotification::Request request =
          notificationRequest(QStringLiteral("Placement"),
                              QStringLiteral("The notification follows the selected edge."));
      request.placement = placement;
      AdNotificationService::open(request, this);
    });
    grid->addWidget(button, i / 3, i % 3);
  }
  grid->setColumnStretch(3, 1);
  return box;
}

QWidget* NotificationDocsPage::buildDurationDemo() {
  auto* button = makeButton(QStringLiteral("Open for 10 seconds"));
  connect(button, &QAbstractButton::clicked, this, [this]() {
    AdNotification::Request request =
        notificationRequest(QStringLiteral("Custom duration"),
                            QStringLiteral("This notification closes after ten seconds."));
    request.durationMs = 10000;
    AdNotificationService::open(request, this);
  });
  return makeButtonRow({button});
}

QWidget* NotificationDocsPage::buildProgressDemo() {
  auto* button = makeButton(QStringLiteral("Show progress"));
  connect(button, &QAbstractButton::clicked, this, [this]() {
    AdNotification::Request request = notificationRequest(
        QStringLiteral("Progress"), QStringLiteral("Hover to pause the countdown."));
    request.durationMs = 7000;
    request.showProgress = true;
    request.pauseOnHover = true;
    AdNotificationService::open(request, this);
  });
  return makeButtonRow({button});
}

QWidget* NotificationDocsPage::buildStackDemo() {
  auto* open = makeButton(QStringLiteral("Open five notifications"));
  auto* clear = makeButton(QStringLiteral("Clear"));
  connect(open, &QAbstractButton::clicked, this, [this]() {
    AdNotification* notifications = notificationsFor(this);
    if (!notifications) return;
    AdNotification::Config config = notifications->config();
    config.stackEnabled = true;
    config.stackThreshold = 3;
    notifications->setConfig(config);
    for (int i = 1; i <= 5; ++i) {
      AdNotification::Request request =
          notificationRequest(QStringLiteral("Notification %1").arg(i),
                              QStringLiteral("Hover over the stack to expand every item."));
      request.durationMs = 0;
      notifications->open(request);
    }
  });
  connect(clear, &QAbstractButton::clicked, this, [this]() {
    if (AdNotification* notifications = notificationsFor(this)) notifications->destroyAll();
  });
  return makeButtonRow({open, clear});
}

QWidget* NotificationDocsPage::buildUpdateDemo() {
  auto* button = makeButton(QStringLiteral("Run keyed update"));
  connect(button, &QAbstractButton::clicked, this, [this, button]() {
    AdNotification::Request first = notificationRequest(
        QStringLiteral("Working"), QStringLiteral("The same notification will be updated."));
    first.type = AdNotification::Type::Info;
    first.key = QStringLiteral("notification-docs-update");
    first.durationMs = 0;
    AdNotificationService::open(first, this);
    QPointer<QWidget> context(this);
    adqt::widgets::detail::scheduleTimingTask(
        button, QStringLiteral("NotificationDocs.Update"), 1200, [context]() {
          if (!context) return;
          AdNotification::Request done = notificationRequest(
              QStringLiteral("Complete"), QStringLiteral("The notification was updated in place."));
          done.type = AdNotification::Type::Success;
          done.key = QStringLiteral("notification-docs-update");
          done.durationMs = 2500;
          AdNotificationService::open(done, context);
        });
  });
  return makeButtonRow({button});
}

QWidget* NotificationDocsPage::buildActionsDemo() {
  auto* button = makeButton(QStringLiteral("Open with action"));
  connect(button, &QAbstractButton::clicked, this, [this]() {
    const QString key = QStringLiteral("notification-docs-action");
    auto* undo = makeButton(QStringLiteral("Undo"));
    QPointer<QWidget> context(this);
    connect(undo, &QAbstractButton::clicked, undo, [context, key]() {
      if (context) AdNotificationService::destroy(key, context);
    });
    AdNotification::Request request = notificationRequest(
        QStringLiteral("File deleted"), QStringLiteral("The item was moved to the recycle bin."));
    request.key = key;
    request.actionsWidget = undo;
    request.durationMs = 0;
    AdNotificationService::open(request, this);
  });
  return makeButtonRow({button});
}

QWidget* NotificationDocsPage::buildCustomStyleDemo() {
  auto* button = makeButton(QStringLiteral("Open styled notification"));
  connect(button, &QAbstractButton::clicked, this, [this]() {
    AdNotification::Request request =
        notificationRequest(QStringLiteral("Custom style"),
                            QStringLiteral("Component tokens and semantic slots are merged."));
    request.icon = outlined_icons::Smile();
    request.componentTokens.backgroundColor = QColor(QStringLiteral("#fffbe6"));
    request.semanticStyles.root.borderColor = QColor(QStringLiteral("#ffe58f"));
    request.semanticStyles.title.textColor = QColor(QStringLiteral("#613400"));
    request.semanticStyles.description.textColor = QColor(QStringLiteral("#874d00"));
    AdNotificationService::open(request, this);
  });
  return makeButtonRow({button});
}
