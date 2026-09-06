#include "message_docs_page.h"

#include "antd_icons.h"
#include "widgets/detail/timing_hub.h"
#include "widgets/widgets.h"

#include <QAbstractButton>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QVBoxLayout>

#include <memory>

using adqt::widgets::AdButton;
using adqt::widgets::AdMessage;
using adqt::widgets::AdMessageHandle;
using adqt::widgets::AdMessageService;
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

AdMessage* messagesFor(QWidget* context) { return AdMessageService::instance(context); }

}  // namespace

MessageDocsPage::MessageDocsPage(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(16, 16, 16, 24);
  root->setSpacing(16);

  auto* title = new QLabel(QStringLiteral("Message"));
  QFont titleFont = title->font();
  titleFont.setPointSize(titleFont.pointSize() + 8);
  titleFont.setBold(true);
  title->setFont(titleFont);
  root->addWidget(title);

  auto* subtitle = new QLabel(QStringLiteral(
      "Display lightweight global feedback at the top center of the current window."));
  subtitle->setWordWrap(true);
  root->addWidget(subtitle);

  addSection(root, QStringLiteral("Basic"), QStringLiteral("A standard informational message."),
             buildBasicDemo());
  addSection(root, QStringLiteral("Other types"),
             QStringLiteral("Success, info, warning, error, and loading feedback."),
             buildTypesDemo());
  addSection(root, QStringLiteral("Customize duration"),
             QStringLiteral("Override the default three-second display duration."),
             buildDurationDemo());
  addSection(root, QStringLiteral("Loading indicator"),
             QStringLiteral("A persistent loading message dismissed asynchronously."),
             buildLoadingDemo());
  addSection(root, QStringLiteral("Update by key"),
             QStringLiteral("Reuse a key to update content and status in place."),
             buildUpdateDemo());
  addSection(root, QStringLiteral("Manual close"),
             QStringLiteral("The returned handle closes one message directly."), buildManualDemo());
  addSection(
      root, QStringLiteral("Maximum count"),
      QStringLiteral("Drop the oldest active message when the configured limit is exceeded."),
      buildMaximumCountDemo());
  addSection(root, QStringLiteral("Custom icon and content"),
             QStringLiteral("Icon references and hosted QWidget content are both supported."),
             buildCustomContentDemo());
  addSection(root, QStringLiteral("Semantic styling"),
             QStringLiteral("Root, icon, and content slots can be styled per request."),
             buildSemanticStyleDemo());
  addSection(root, QStringLiteral("Callbacks"),
             QStringLiteral(
                 "Click and close callbacks integrate message feedback with application state."),
             buildCallbackDemo());

  root->addStretch();
}

const QVector<QWidget*>& MessageDocsPage::sectionAnchors() const { return anchors_; }

const QStringList& MessageDocsPage::sectionTitles() const { return titles_; }

void MessageDocsPage::addSection(QVBoxLayout* root, const QString& title,
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

QWidget* MessageDocsPage::buildBasicDemo() {
  auto* button = makeButton(QStringLiteral("Display normal message"));
  button->setButtonStyle(AdButton::ButtonStyle::Solid);
  button->setAccentRole(AdButton::AccentRole::Primary);
  connect(button, &QAbstractButton::clicked, this, [this]() {
    if (AdMessage* messages = messagesFor(this)) {
      messages->info(QStringLiteral("Hello, Ant Design!"));
    }
  });
  return makeButtonRow({button});
}

QWidget* MessageDocsPage::buildTypesDemo() {
  auto* success = makeButton(QStringLiteral("Success"));
  auto* info = makeButton(QStringLiteral("Info"));
  auto* warning = makeButton(QStringLiteral("Warning"));
  auto* error = makeButton(QStringLiteral("Error"));
  auto* loading = makeButton(QStringLiteral("Loading"));

  connect(success, &QAbstractButton::clicked, this, [this]() {
    if (AdMessage* messages = messagesFor(this)) {
      messages->success(QStringLiteral("This is a success message"));
    }
  });
  connect(info, &QAbstractButton::clicked, this, [this]() {
    if (AdMessage* messages = messagesFor(this)) {
      messages->info(QStringLiteral("This is an info message"));
    }
  });
  connect(warning, &QAbstractButton::clicked, this, [this]() {
    if (AdMessage* messages = messagesFor(this)) {
      messages->warning(QStringLiteral("This is a warning message"));
    }
  });
  connect(error, &QAbstractButton::clicked, this, [this]() {
    if (AdMessage* messages = messagesFor(this)) {
      messages->error(QStringLiteral("This is an error message"));
    }
  });
  connect(loading, &QAbstractButton::clicked, this, [this]() {
    if (AdMessage* messages = messagesFor(this)) {
      messages->loading(QStringLiteral("Action in progress..."));
    }
  });
  return makeButtonRow({success, info, warning, error, loading});
}

QWidget* MessageDocsPage::buildDurationDemo() {
  auto* button = makeButton(QStringLiteral("Customized display duration"));
  connect(button, &QAbstractButton::clicked, this, [this]() {
    AdMessage::Request request;
    request.type = AdMessage::Type::Success;
    request.content = QStringLiteral("This prompt will disappear in ten seconds");
    request.durationMs = 10000;
    AdMessageService::open(request, this);
  });
  return makeButtonRow({button});
}

QWidget* MessageDocsPage::buildLoadingDemo() {
  auto* button = makeButton(QStringLiteral("Display a loading indicator"));
  connect(button, &QAbstractButton::clicked, this, [this, button]() {
    AdMessage::Request request;
    request.type = AdMessage::Type::Loading;
    request.content = QStringLiteral("Action in progress...");
    request.key = QStringLiteral("message-docs-loading");
    request.durationMs = 0;
    AdMessageService::open(request, this);

    QPointer<QWidget> context(this);
    adqt::widgets::detail::scheduleTimingTask(
        button, QStringLiteral("MessageDocs.LoadingDismiss"), 2500, [context]() {
          if (context) {
            AdMessageService::destroy(QStringLiteral("message-docs-loading"), context);
          }
        });
  });
  return makeButtonRow({button});
}

QWidget* MessageDocsPage::buildUpdateDemo() {
  auto* button = makeButton(QStringLiteral("Open the message box"));
  button->setButtonStyle(AdButton::ButtonStyle::Solid);
  button->setAccentRole(AdButton::AccentRole::Primary);
  connect(button, &QAbstractButton::clicked, this, [this, button]() {
    AdMessage::Request loading;
    loading.type = AdMessage::Type::Loading;
    loading.content = QStringLiteral("Loading...");
    loading.key = QStringLiteral("message-docs-updatable");
    loading.durationMs = 0;
    AdMessageService::open(loading, this);

    QPointer<QWidget> context(this);
    adqt::widgets::detail::scheduleTimingTask(
        button, QStringLiteral("MessageDocs.Update"), 1000, [context]() {
          if (!context) {
            return;
          }
          AdMessage::Request loaded;
          loaded.type = AdMessage::Type::Success;
          loaded.content = QStringLiteral("Loaded!");
          loaded.key = QStringLiteral("message-docs-updatable");
          loaded.durationMs = 2000;
          AdMessageService::open(loaded, context);
        });
  });
  return makeButtonRow({button});
}

QWidget* MessageDocsPage::buildManualDemo() {
  auto* open = makeButton(QStringLiteral("Open persistent message"));
  auto* close = makeButton(QStringLiteral("Close message"));
  auto active = std::make_shared<QPointer<AdMessageHandle>>();

  connect(open, &QAbstractButton::clicked, this, [this, active]() {
    if (AdMessage* messages = messagesFor(this)) {
      *active = messages->loading(QStringLiteral("Action in progress..."), 0);
    }
  });
  connect(close, &QAbstractButton::clicked, this, [active]() {
    if (*active) {
      (*active)->close();
    }
  });
  return makeButtonRow({open, close});
}

QWidget* MessageDocsPage::buildMaximumCountDemo() {
  auto* open = makeButton(QStringLiteral("Open five messages"));
  auto* clear = makeButton(QStringLiteral("Clear"));
  connect(open, &QAbstractButton::clicked, this, [this]() {
    AdMessage* messages = messagesFor(this);
    if (!messages) {
      return;
    }
    messages->setMaximumCount(3);
    for (int index = 1; index <= 5; ++index) {
      AdMessage::Request request;
      request.type = AdMessage::Type::Info;
      request.content = QStringLiteral("Message %1").arg(index);
      request.durationMs = 0;
      messages->open(request);
    }
  });
  connect(clear, &QAbstractButton::clicked, this, [this]() {
    if (AdMessage* messages = messagesFor(this)) {
      messages->destroyAll();
      messages->setMaximumCount(0);
    }
  });
  return makeButtonRow({open, clear});
}

QWidget* MessageDocsPage::buildCustomContentDemo() {
  auto* button = makeButton(QStringLiteral("Custom message"));
  connect(button, &QAbstractButton::clicked, this, [this]() {
    auto* content = new QLabel(QStringLiteral("A QWidget with a custom icon"));
    QFont font = content->font();
    font.setBold(true);
    content->setFont(font);
    content->setAccessibleName(content->text());

    AdMessage::Request request;
    request.contentWidget = content;
    request.icon = outlined_icons::Smile();
    request.semanticStyles.content.textColor = QColor(QStringLiteral("#d4380d"));
    AdMessageService::open(request, this);
  });
  return makeButtonRow({button});
}

QWidget* MessageDocsPage::buildSemanticStyleDemo() {
  auto* button = makeButton(QStringLiteral("Customized style"));
  connect(button, &QAbstractButton::clicked, this, [this]() {
    AdMessage::Request request;
    request.type = AdMessage::Type::Success;
    request.content = QStringLiteral("This message uses semantic slot styles");
    request.componentTokens.contentBg = QColor(QStringLiteral("#fffbe6"));
    request.semanticStyles.root.borderColor = QColor(QStringLiteral("#ffe58f"));
    request.semanticStyles.content.textColor = QColor(QStringLiteral("#613400"));
    request.semanticStyleResolver = [](const AdMessage::StyleContext& context) {
      AdMessage::SemanticStyles styles;
      if (context.hovered) {
        styles.root.backgroundColor = QColor(QStringLiteral("#fff1b8"));
      }
      return styles;
    };
    AdMessageService::open(request, this);
  });
  return makeButtonRow({button});
}

QWidget* MessageDocsPage::buildCallbackDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* button = makeButton(QStringLiteral("Open clickable message"));
  auto* status = new QLabel(QStringLiteral("Status: waiting"));
  connect(button, &QAbstractButton::clicked, this, [this, status]() {
    QPointer<QLabel> guardedStatus(status);
    AdMessage::Request request;
    request.type = AdMessage::Type::Info;
    request.content = QStringLiteral("Click this message");
    request.onClick = [guardedStatus]() {
      if (guardedStatus) {
        guardedStatus->setText(QStringLiteral("Status: clicked"));
      }
    };
    request.onClose = [guardedStatus]() {
      if (guardedStatus) {
        guardedStatus->setText(QStringLiteral("Status: closed"));
      }
    };
    AdMessageService::open(request, this);
  });

  layout->addWidget(button, 0, Qt::AlignLeft);
  layout->addWidget(status);
  return box;
}
