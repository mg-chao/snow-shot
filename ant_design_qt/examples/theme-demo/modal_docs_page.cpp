#include "modal_docs_page.h"

#include "demo_theme_utils.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QRandomGenerator>
#include <QTimer>
#include <QVBoxLayout>

using adqt::widgets::AdButton;
using adqt::widgets::AdModal;
using adqt::widgets::AdModalService;

namespace {}  // namespace

ModalDocsPage::ModalDocsPage(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(16, 16, 16, 24);
  root->setSpacing(16);

  auto* title = new QLabel("Modal");
  QFont titleFont = title->font();
  titleFont.setPointSize(titleFont.pointSize() + 8);
  titleFont.setBold(true);
  title->setFont(titleFont);
  root->addWidget(title);

  auto* subtitle = new QLabel(
      "A floating layer for focused tasks and confirmations. This page ports the official antd "
      "Modal demos.");
  subtitle->setWordWrap(true);
  root->addWidget(subtitle);

  addSection(root, "Basic", "Demo: basic.tsx", buildBasicDemo());
  addSection(root, "Controlled open state", "Repository demo: controlled-open.cpp",
             buildControlledDemo());
  addSection(root, "Asynchronously close", "Demo: async.tsx", buildAsyncDemo());
  addSection(root, "Customized Footer", "Demo: footer.tsx", buildFooterDemo());
  addSection(root, "Customized Footer render function", "Demo: footer-render.tsx",
             buildFooterRenderDemo());
  addSection(root, "Internationalization", "Demo: locale.tsx", buildLocaleDemo());
  addSection(root, "Loading", "Demo: loading.tsx", buildLoadingDemo());
  addSection(root, "Mask", "Demo: mask.tsx", buildMaskDemo());
  addSection(root, "Window mode", "Repository demo: window-mode.cpp", buildWindowModeDemo());
  addSection(root, "Position", "Demo: position.tsx", buildPositionDemo());
  addSection(root, "Width", "Demo: width.tsx", buildWidthDemo());
  addSection(root, "Customize footer buttons props", "Demo: button-props.tsx",
             buildButtonPropsDemo());
  addSection(root, "Static Method", "Demo: static-info.tsx", buildStaticInfoDemo());
  addSection(root, "Static confirmation", "Demo: confirm.tsx", buildStaticConfirmDemo());
  addSection(root, "Manual to update destroy", "Demo: manual.tsx", buildManualDemo());
  addSection(root, "Destroy all static modals", "Demo: confirm-router.tsx", buildDestroyAllDemo());
  addSection(root, "Custom semantic dom styling", "Demo: style-class.tsx", buildStyleClassDemo());
  addSection(root, "Component Token / Wireframe", "Demo: component-token.tsx + wireframe.tsx",
             buildComponentTokenDemo());

  root->addStretch();
}

const QVector<QWidget*>& ModalDocsPage::sectionAnchors() const { return anchors_; }

const QStringList& ModalDocsPage::sectionTitles() const { return titles_; }

void ModalDocsPage::addSection(QVBoxLayout* root, const QString& title, const QString& description,
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

QWidget* ModalDocsPage::buildBasicDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(10);

  auto* openButton = new AdButton("Open Modal");
  openButton->setButtonStyle(AdButton::ButtonStyle::Solid);
  openButton->setAccentRole(AdButton::AccentRole::Primary);
  auto* status = new QLabel("Status: waiting");

  auto* modal = new AdModal(box);
  modal->setWindowTitle("Basic Modal");
  modal->setText("Some contents...\nSome contents...\nSome contents...");

  connect(openButton, &QAbstractButton::clicked, modal, [modal]() { modal->open(); });
  connect(modal, &AdModal::accepted, status, [status]() { status->setText("Status: OK clicked"); });
  connect(modal, &AdModal::closed, status, [status](AdModal::CloseReason reason) {
    if (reason != AdModal::CloseReason::OkAction) {
      status->setText("Status: Cancel/Close clicked");
    }
  });

  row->addWidget(openButton);
  row->addWidget(status);
  row->addStretch();
  return box;
}

QWidget* ModalDocsPage::buildControlledDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* controls = new QHBoxLayout();
  controls->setContentsMargins(0, 0, 0, 0);
  controls->setSpacing(8);

  auto* openButton = new AdButton("Open controlled modal");
  openButton->setButtonStyle(AdButton::ButtonStyle::Solid);
  openButton->setAccentRole(AdButton::AccentRole::Primary);
  auto* blockClose = new QCheckBox("Block close requests");
  auto* status = new QLabel("Status: waiting");

  auto* modal = new AdModal(box);
  modal->setWindowTitle("Controlled Modal");
  modal->setText("User actions emit closeRequested. The owner decides when open becomes false.");
  modal->setClosePolicy(AdModal::ClosePolicy::Manual);

  const auto reasonText = [](AdModal::CloseReason reason) {
    switch (reason) {
      case AdModal::CloseReason::OkAction:
        return QStringLiteral("OK");
      case AdModal::CloseReason::CancelAction:
        return QStringLiteral("Cancel button");
      case AdModal::CloseReason::CloseButton:
        return QStringLiteral("Close button");
      case AdModal::CloseReason::Mask:
        return QStringLiteral("Mask");
      case AdModal::CloseReason::Keyboard:
        return QStringLiteral("Keyboard");
      case AdModal::CloseReason::ScopeHidden:
        return QStringLiteral("Scope hidden");
      case AdModal::CloseReason::Programmatic:
        return QStringLiteral("Programmatic");
    }
    return QStringLiteral("Unknown");
  };

  connect(openButton, &QAbstractButton::clicked, modal, [modal, status]() {
    modal->open();
    status->setText("Status: controlled modal opened");
  });
  connect(
      modal, &AdModal::closeRequested, modal,
      [modal, blockClose, status, reasonText](AdModal::CloseReason reason) {
        status->setText(QStringLiteral("Status: close requested by %1").arg(reasonText(reason)));
        if (!blockClose->isChecked()) {
          const AdModal::DialogCode result = reason == AdModal::CloseReason::OkAction
                                                 ? AdModal::DialogCode::Accepted
                                                 : AdModal::DialogCode::Rejected;
          modal->done(result);
        }
      });
  connect(modal, &AdModal::finished, status,
          [status](AdModal::DialogCode) { status->setText("Status: controlled modal closed"); });

  controls->addWidget(openButton);
  controls->addWidget(blockClose);
  controls->addStretch();

  layout->addLayout(controls);
  layout->addWidget(status);
  layout->addWidget(
      makeHintLabel("When the checkbox is checked, the modal stays open after closeRequested."));
  return box;
}

QWidget* ModalDocsPage::buildAsyncDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* openButton = new AdButton("Open Modal with async logic");
  openButton->setButtonStyle(AdButton::ButtonStyle::Solid);
  openButton->setAccentRole(AdButton::AccentRole::Primary);
  auto* status = new QLabel("Status: waiting");

  auto* modal = new AdModal(box);
  modal->setWindowTitle("Title");
  modal->setText("Content of the modal");
  modal->setClosePolicy(AdModal::ClosePolicy::Manual);

  connect(openButton, &QAbstractButton::clicked, modal, [modal, status]() {
    modal->setText("Content of the modal");
    status->setText("Status: opened");
    modal->open();
  });
  connect(modal, &AdModal::closeRequested, modal, [modal, status](AdModal::CloseReason reason) {
    if (reason != AdModal::CloseReason::OkAction) {
      status->setText("Status: canceled");
      modal->reject();
      return;
    }
    modal->setText("The modal will be closed after two seconds");
    modal->setAcceptButtonBusy(true);
    status->setText("Status: confirming...");
    QTimer::singleShot(2000, modal, [modal, status]() {
      modal->setAcceptButtonBusy(false);
      modal->accept();
      status->setText("Status: closed");
    });
  });

  layout->addWidget(openButton, 0, Qt::AlignLeft);
  layout->addWidget(status);
  return box;
}

QWidget* ModalDocsPage::buildFooterDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* openButton = new AdButton("Open Modal with customized footer");
  openButton->setButtonStyle(AdButton::ButtonStyle::Solid);
  openButton->setAccentRole(AdButton::AccentRole::Primary);

  auto* status = new QLabel("Status: waiting");
  auto* modal = new AdModal(box);
  modal->setWindowTitle("Title");
  modal->setText("Some contents...\nSome contents...\nSome contents...");

  auto* footerHost = new QWidget();
  auto* footerRow = new QHBoxLayout(footerHost);
  footerRow->setContentsMargins(0, 0, 0, 0);
  footerRow->setSpacing(8);
  auto* returnButton = new AdButton("Return");
  auto* submitButton = new AdButton("Submit");
  submitButton->setButtonStyle(AdButton::ButtonStyle::Solid);
  submitButton->setAccentRole(AdButton::AccentRole::Primary);
  auto* searchButton = new AdButton("Search");
  searchButton->setButtonStyle(AdButton::ButtonStyle::Solid);
  searchButton->setAccentRole(AdButton::AccentRole::Primary);
  footerRow->addStretch();
  footerRow->addWidget(returnButton);
  footerRow->addWidget(submitButton);
  footerRow->addWidget(searchButton);
  modal->setFooterWidget(footerHost);

  connect(openButton, &QAbstractButton::clicked, modal, [modal, status]() {
    status->setText("Status: opened");
    modal->open();
  });
  connect(returnButton, &QAbstractButton::clicked, modal, [modal, status]() {
    modal->reject();
    status->setText("Status: return");
  });
  connect(submitButton, &QAbstractButton::clicked, modal, [modal, status, submitButton]() {
    submitButton->setBusy(true);
    status->setText("Status: submit loading");
    QTimer::singleShot(1200, submitButton, [modal, status, submitButton]() {
      submitButton->setBusy(false);
      modal->accept();
      status->setText("Status: submit complete");
    });
  });
  connect(searchButton, &QAbstractButton::clicked, status,
          [status]() { status->setText("Status: custom action from footer"); });

  layout->addWidget(openButton, 0, Qt::AlignLeft);
  layout->addWidget(status);
  return box;
}

QWidget* ModalDocsPage::buildFooterRenderDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(10);

  auto* openModalButton = new AdButton("Open Modal");
  openModalButton->setButtonStyle(AdButton::ButtonStyle::Solid);
  openModalButton->setAccentRole(AdButton::AccentRole::Primary);
  auto* openConfirmButton = new AdButton("Open Modal Confirm");
  openConfirmButton->setButtonStyle(AdButton::ButtonStyle::Solid);
  openConfirmButton->setAccentRole(AdButton::AccentRole::Primary);

  auto* modal = new AdModal(box);
  modal->setWindowTitle("Title");
  modal->setText("Some contents...\nSome contents...\nSome contents...");

  auto* footerHost = new QWidget();
  auto* footerRow = new QHBoxLayout(footerHost);
  footerRow->setContentsMargins(0, 0, 0, 0);
  footerRow->setSpacing(8);
  auto* customButton = new AdButton("Custom Button");
  auto* cancelButton = new AdButton("Cancel");
  auto* okButton = new AdButton("OK");
  okButton->setButtonStyle(AdButton::ButtonStyle::Solid);
  okButton->setAccentRole(AdButton::AccentRole::Primary);
  footerRow->addStretch();
  footerRow->addWidget(customButton);
  footerRow->addWidget(cancelButton);
  footerRow->addWidget(okButton);
  modal->setFooterWidget(footerHost);

  connect(openModalButton, &QAbstractButton::clicked, modal, [modal]() { modal->open(); });
  connect(customButton, &QAbstractButton::clicked, modal,
          [modal]() { modal->setText("Custom footer button clicked."); });
  connect(cancelButton, &QAbstractButton::clicked, modal, [modal]() { modal->reject(); });
  connect(okButton, &QAbstractButton::clicked, modal, [modal]() { modal->accept(); });

  connect(openConfirmButton, &QAbstractButton::clicked, this, [this]() {
    AdModalService::Request config;
    config.title = QStringLiteral("Confirm");
    config.text = QStringLiteral("Bla bla ...");
    config.standardButtons = AdModal::StandardButton::Ok | AdModal::StandardButton::Cancel;
    config.onAccept = [](AdModal* modal) {
      if (modal) {
        modal->accept();
      }
    };
    config.onReject = [](AdModal* modal) {
      if (modal) {
        modal->reject();
      }
    };
    AdModal* staticModal = AdModalService::showConfirm(config, window());
    if (!staticModal) {
      return;
    }

    auto* footer = new QWidget();
    auto* row = new QHBoxLayout(footer);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);
    auto* custom = new AdButton("Custom Button");
    auto* cancel = new AdButton("Cancel");
    auto* ok = new AdButton("OK");
    ok->setButtonStyle(AdButton::ButtonStyle::Solid);
    ok->setAccentRole(AdButton::AccentRole::Primary);
    row->addStretch();
    row->addWidget(custom);
    row->addWidget(cancel);
    row->addWidget(ok);
    staticModal->setFooterWidget(footer);

    connect(custom, &QAbstractButton::clicked, staticModal,
            [staticModal]() { staticModal->setText("Custom static footer clicked."); });
    connect(cancel, &QAbstractButton::clicked, staticModal,
            [staticModal]() { staticModal->reject(); });
    connect(ok, &QAbstractButton::clicked, staticModal, [staticModal]() { staticModal->accept(); });
  });

  row->addWidget(openModalButton);
  row->addWidget(openConfirmButton);
  row->addStretch();
  return box;
}

QWidget* ModalDocsPage::buildLocaleDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(10);

  auto* modalButton = new AdButton("Modal");
  modalButton->setButtonStyle(AdButton::ButtonStyle::Solid);
  modalButton->setAccentRole(AdButton::AccentRole::Primary);
  auto* confirmButton = new AdButton("Confirm");

  auto* modal = new AdModal(box);
  modal->setWindowTitle("Modal");
  modal->setText("Bla bla ...");
  modal->setAcceptText(QStringLiteral("确认"));
  modal->setRejectText(QStringLiteral("取消"));

  connect(modalButton, &QAbstractButton::clicked, modal, [modal]() { modal->open(); });
  connect(confirmButton, &QAbstractButton::clicked, this, [this]() {
    AdModalService::Request config;
    config.title = QStringLiteral("Confirm");
    config.text = QStringLiteral("Bla bla ...");
    config.acceptText = QStringLiteral("确认");
    config.rejectText = QStringLiteral("取消");
    config.standardButtons = AdModal::StandardButton::Ok | AdModal::StandardButton::Cancel;
    AdModalService::showConfirm(config, window());
  });

  row->addWidget(modalButton);
  row->addWidget(confirmButton);
  row->addStretch();
  return box;
}

QWidget* ModalDocsPage::buildLoadingDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* openButton = new AdButton("Open Modal");
  openButton->setButtonStyle(AdButton::ButtonStyle::Solid);
  openButton->setAccentRole(AdButton::AccentRole::Primary);

  auto* modal = new AdModal(box);
  modal->setWindowTitle("Loading Modal");
  modal->setText("Some contents...\nSome contents...\nSome contents...");
  modal->setStandardButtons(AdModal::StandardButton::Ok);

  auto* footerHost = new QWidget();
  auto* footerRow = new QHBoxLayout(footerHost);
  footerRow->setContentsMargins(0, 0, 0, 0);
  auto* reloadButton = new AdButton("Reload");
  reloadButton->setButtonStyle(AdButton::ButtonStyle::Solid);
  reloadButton->setAccentRole(AdButton::AccentRole::Primary);
  footerRow->addStretch();
  footerRow->addWidget(reloadButton);
  modal->setFooterWidget(footerHost);

  connect(openButton, &QAbstractButton::clicked, modal, [modal]() {
    modal->open();
    modal->setContentLoading(true);
    QTimer::singleShot(2000, modal, [modal]() { modal->setContentLoading(false); });
  });
  connect(reloadButton, &QAbstractButton::clicked, modal, [modal]() {
    modal->setContentLoading(true);
    QTimer::singleShot(2000, modal, [modal]() { modal->setContentLoading(false); });
  });
  layout->addWidget(openButton, 0, Qt::AlignLeft);
  layout->addWidget(
      makeHintLabel("Loading=true replaces content with a skeleton-like placeholder text."));
  return box;
}

QWidget* ModalDocsPage::buildMaskDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8);

  auto* dimmed = new AdButton("Dimmed mask");
  auto* noMask = new AdButton("No mask");
  auto* notClosable = new AdButton("Mask not closable");

  const auto openConfirm = [this](bool maskEnabled, bool closable) {
    AdModalService::Request config;
    config.title = QStringLiteral("Title");
    config.text = QStringLiteral("Some contents...");
    config.maskVisible = maskEnabled;
    config.closeOnMaskClick = closable;
    config.standardButtons = AdModal::StandardButton::Ok | AdModal::StandardButton::Cancel;
    AdModalService::showConfirm(config, window());
  };

  connect(dimmed, &QAbstractButton::clicked, this, [openConfirm]() { openConfirm(true, true); });
  connect(noMask, &QAbstractButton::clicked, this, [openConfirm]() { openConfirm(false, false); });
  connect(notClosable, &QAbstractButton::clicked, this,
          [openConfirm]() { openConfirm(true, false); });

  row->addWidget(dimmed);
  row->addWidget(noMask);
  row->addWidget(notClosable);
  row->addStretch();
  return box;
}

QWidget* ModalDocsPage::buildWindowModeDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* row = new QHBoxLayout();
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8);

  auto* openButton = new AdButton("Open window modal");
  openButton->setButtonStyle(AdButton::ButtonStyle::Solid);
  openButton->setAccentRole(AdButton::AccentRole::Primary);
  auto* confirmButton = new AdButton("Open window confirm");
  auto* status = new QLabel("Status: waiting");

  auto* modal = new AdModal(box);
  modal->setMode(AdModal::Mode::Window);
  modal->setWindowTitle("Window Mode Modal");
  modal->setText(
      "The modal panel is hosted by its own top-level window without a background mask.");
  modal->setCentered(true);
  modal->setPreferredWidth(460);
  modal->setCloseOnMaskClick(false);

  connect(openButton, &QAbstractButton::clicked, modal, [modal, status]() {
    status->setText("Status: window modal opened");
    modal->open();
  });
  connect(modal, &AdModal::finished, status,
          [status](AdModal::DialogCode) { status->setText("Status: window modal closed"); });

  connect(confirmButton, &QAbstractButton::clicked, this, [this, status]() {
    AdModalService::Request config;
    config.mode = AdModal::Mode::Window;
    config.centered = true;
    config.title = QStringLiteral("Window Mode Confirm");
    config.text = QStringLiteral("Static modal helpers can also open without a mask.");
    config.standardButtons = AdModal::StandardButton::Ok | AdModal::StandardButton::Cancel;
    config.onAccept = [status](AdModal* modal) {
      status->setText("Status: window confirm accepted");
      if (modal) {
        modal->accept();
      }
    };
    config.onReject = [status](AdModal* modal) {
      status->setText("Status: window confirm rejected");
      if (modal) {
        modal->reject();
      }
    };
    AdModalService::showConfirm(config, window());
  });

  row->addWidget(openButton);
  row->addWidget(confirmButton);
  row->addStretch();

  layout->addLayout(row);
  layout->addWidget(status);
  return box;
}

QWidget* ModalDocsPage::buildPositionDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  auto* topButton = new AdButton("Display a modal dialog at 20px to Top");
  topButton->setButtonStyle(AdButton::ButtonStyle::Solid);
  topButton->setAccentRole(AdButton::AccentRole::Primary);
  auto* centeredButton = new AdButton("Vertically centered modal dialog");
  centeredButton->setButtonStyle(AdButton::ButtonStyle::Solid);
  centeredButton->setAccentRole(AdButton::AccentRole::Primary);

  auto* topModal = new AdModal(box);
  topModal->setWindowTitle("20px to Top");
  topModal->setText("some contents...\nsome contents...\nsome contents...");
  topModal->setTopOffset(20);
  topModal->setCentered(false);

  auto* centeredModal = new AdModal(box);
  centeredModal->setWindowTitle("Vertically centered modal dialog");
  centeredModal->setText("some contents...\nsome contents...\nsome contents...");
  centeredModal->setCentered(true);

  connect(topButton, &QAbstractButton::clicked, topModal, [topModal]() { topModal->open(); });
  connect(centeredButton, &QAbstractButton::clicked, centeredModal,
          [centeredModal]() { centeredModal->open(); });

  layout->addWidget(topButton, 0, Qt::AlignLeft);
  layout->addWidget(centeredButton, 0, Qt::AlignLeft);
  return box;
}

QWidget* ModalDocsPage::buildWidthDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* fixedButton = new AdButton("Open Modal of 1000px width");
  fixedButton->setButtonStyle(AdButton::ButtonStyle::Solid);
  fixedButton->setAccentRole(AdButton::AccentRole::Primary);
  auto* responsiveButton = new AdButton("Open Modal of responsive width");
  responsiveButton->setButtonStyle(AdButton::ButtonStyle::Solid);
  responsiveButton->setAccentRole(AdButton::AccentRole::Primary);

  auto* fixedModal = new AdModal(box);
  fixedModal->setWindowTitle("Modal 1000px width");
  fixedModal->setText("some contents...\nsome contents...\nsome contents...");
  fixedModal->setCentered(true);
  fixedModal->setPreferredWidth(1000);

  auto* responsiveModal = new AdModal(box);
  responsiveModal->setWindowTitle("Modal responsive width");
  responsiveModal->setText("some contents...\nsome contents...\nsome contents...");
  responsiveModal->setCentered(true);

  connect(fixedButton, &QAbstractButton::clicked, fixedModal,
          [fixedModal]() { fixedModal->open(); });
  connect(responsiveButton, &QAbstractButton::clicked, responsiveModal, [responsiveModal, this]() {
    const int viewportWidth = window() ? window()->width() : 1280;
    int width = static_cast<int>(viewportWidth * 0.9);
    if (viewportWidth >= 1600) {
      width = static_cast<int>(viewportWidth * 0.4);
    } else if (viewportWidth >= 1200) {
      width = static_cast<int>(viewportWidth * 0.5);
    } else if (viewportWidth >= 992) {
      width = static_cast<int>(viewportWidth * 0.6);
    } else if (viewportWidth >= 768) {
      width = static_cast<int>(viewportWidth * 0.7);
    } else if (viewportWidth >= 576) {
      width = static_cast<int>(viewportWidth * 0.8);
    }
    responsiveModal->setPreferredWidth(width);
    responsiveModal->open();
  });

  layout->addWidget(fixedButton, 0, Qt::AlignLeft);
  layout->addWidget(responsiveButton, 0, Qt::AlignLeft);
  return box;
}

QWidget* ModalDocsPage::buildButtonPropsDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* openButton = new AdButton("Open Modal with customized button props");
  openButton->setButtonStyle(AdButton::ButtonStyle::Solid);
  openButton->setAccentRole(AdButton::AccentRole::Primary);

  auto* modal = new AdModal(box);
  modal->setWindowTitle("Basic Modal");
  modal->setText("Some contents...\nSome contents...\nSome contents...");
  modal->close();

  connect(openButton, &QAbstractButton::clicked, modal, [modal]() {
    modal->open();
    if (modal->acceptButton()) {
      modal->acceptButton()->setEnabled(false);
    }
    if (modal->rejectButton()) {
      modal->rejectButton()->setEnabled(false);
    }
  });
  connect(modal, &AdModal::openChanged, modal, [modal](bool open) {
    if (!open) {
      if (modal->acceptButton()) {
        modal->acceptButton()->setEnabled(true);
      }
      if (modal->rejectButton()) {
        modal->rejectButton()->setEnabled(true);
      }
    }
  });

  layout->addWidget(openButton, 0, Qt::AlignLeft);
  layout->addWidget(makeHintLabel("OK / Cancel are disabled while this demo modal is open."));
  return box;
}

QWidget* ModalDocsPage::buildStaticInfoDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8);

  auto* info = new AdButton("Info");
  auto* success = new AdButton("Success");
  auto* error = new AdButton("Error");
  auto* warning = new AdButton("Warning");

  connect(info, &QAbstractButton::clicked, this, [this]() {
    AdModalService::Request config;
    config.title = QStringLiteral("This is a notification message");
    config.text = QStringLiteral("some messages...some messages...");
    AdModalService::showInfo(config, window());
  });
  connect(success, &QAbstractButton::clicked, this, [this]() {
    AdModalService::Request config;
    config.text = QStringLiteral("some messages...some messages...");
    AdModalService::showSuccess(config, window());
  });
  connect(error, &QAbstractButton::clicked, this, [this]() {
    AdModalService::Request config;
    config.title = QStringLiteral("This is an error message");
    config.text = QStringLiteral("some messages...some messages...");
    AdModalService::showError(config, window());
  });
  connect(warning, &QAbstractButton::clicked, this, [this]() {
    AdModalService::Request config;
    config.title = QStringLiteral("This is a warning message");
    config.text = QStringLiteral("some messages...some messages...");
    AdModalService::showWarning(config, window());
  });

  row->addWidget(info);
  row->addWidget(success);
  row->addWidget(error);
  row->addWidget(warning);
  row->addStretch();
  return box;
}

QWidget* ModalDocsPage::buildStaticConfirmDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8);

  auto* confirmButton = new AdButton("Confirm");
  auto* promiseButton = new AdButton("With promise");
  auto* deleteButton = new AdButton("Delete");
  deleteButton->setButtonStyle(AdButton::ButtonStyle::Dashed);
  deleteButton->setAccentRole(AdButton::AccentRole::Neutral);
  auto* propsButton = new AdButton("With extra props");
  propsButton->setButtonStyle(AdButton::ButtonStyle::Dashed);
  propsButton->setAccentRole(AdButton::AccentRole::Neutral);

  connect(confirmButton, &QAbstractButton::clicked, this, [this]() {
    AdModalService::Request config;
    config.title = QStringLiteral("Do you want to delete these items?");
    config.text = QStringLiteral("Some descriptions");
    config.standardButtons = AdModal::StandardButton::Ok | AdModal::StandardButton::Cancel;
    AdModalService::showConfirm(config, window());
  });

  connect(promiseButton, &QAbstractButton::clicked, this, [this]() {
    AdModalService::Request config;
    config.title = QStringLiteral("Do you want to delete these items?");
    config.text =
        QStringLiteral("When clicked the OK button, this dialog will be closed after 1 second");
    config.standardButtons = AdModal::StandardButton::Ok | AdModal::StandardButton::Cancel;
    config.onAccept = [](AdModal* modal) {
      if (!modal) {
        return;
      }
      modal->setAcceptButtonBusy(true);
      const bool shouldResolve = QRandomGenerator::global()->bounded(100) >= 50;
      QTimer::singleShot(1000, modal, [modal, shouldResolve]() {
        modal->setAcceptButtonBusy(false);
        if (shouldResolve) {
          modal->accept();
        } else {
          modal->setText("Oops errors! Click OK again.");
        }
      });
    };
    AdModalService::showConfirm(config, window());
  });

  connect(deleteButton, &QAbstractButton::clicked, this, [this]() {
    AdModalService::Request config;
    config.title = QStringLiteral("Are you sure delete this task?");
    config.text = QStringLiteral("Some descriptions");
    config.acceptText = QStringLiteral("Yes");
    config.rejectText = QStringLiteral("No");
    config.acceptAccentRole = AdButton::AccentRole::Primary;
    config.acceptButtonStyle = AdButton::ButtonStyle::Solid;
    config.standardButtons = AdModal::StandardButton::Ok | AdModal::StandardButton::Cancel;
    AdModalService::showConfirm(config, window());
  });

  connect(propsButton, &QAbstractButton::clicked, this, [this]() {
    AdModalService::Request config;
    config.title = QStringLiteral("Are you sure delete this task?");
    config.text = QStringLiteral("Some descriptions");
    config.acceptText = QStringLiteral("Yes");
    config.rejectText = QStringLiteral("No");
    config.standardButtons = AdModal::StandardButton::Ok | AdModal::StandardButton::Cancel;
    AdModal* modal = AdModalService::showConfirm(config, window());
    if (modal && modal->acceptButton()) {
      modal->acceptButton()->setEnabled(false);
    }
  });

  row->addWidget(confirmButton);
  row->addWidget(promiseButton);
  row->addWidget(deleteButton);
  row->addWidget(propsButton);
  row->addStretch();
  return box;
}

QWidget* ModalDocsPage::buildManualDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8);

  auto* openButton = new AdButton("Open modal to close in 5s");
  auto* status = new QLabel("Status: waiting");

  connect(openButton, &QAbstractButton::clicked, this, [this, status]() {
    int* secondsToGo = new int(5);
    AdModalService::Request config;
    config.title = QStringLiteral("This is a notification message");
    config.text = QStringLiteral("This modal will be destroyed after 5 seconds.");
    config.standardButtons = AdModal::StandardButton::Ok;
    AdModal* modal = AdModalService::showSuccess(config, window());
    if (!modal) {
      delete secondsToGo;
      return;
    }

    status->setText("Status: countdown started");
    auto* timer = new QTimer(modal);
    connect(timer, &QTimer::timeout, modal, [modal, timer, secondsToGo, status]() {
      *secondsToGo -= 1;
      if (*secondsToGo <= 0) {
        timer->stop();
        modal->close();
        status->setText("Status: closed");
        delete secondsToGo;
        return;
      }
      modal->setText(
          QStringLiteral("This modal will be destroyed after %1 seconds.").arg(*secondsToGo));
    });
    timer->start(1000);
  });

  row->addWidget(openButton);
  row->addWidget(status);
  row->addStretch();
  return box;
}

QWidget* ModalDocsPage::buildDestroyAllDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* openButton = new AdButton("Open 3 confirms");
  auto* destroyAllButton = new AdButton("Destroy all");
  auto* row = new QHBoxLayout();
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8);
  row->addWidget(openButton);
  row->addWidget(destroyAllButton);
  row->addStretch();

  connect(openButton, &QAbstractButton::clicked, this, [this]() {
    for (int i = 0; i < 3; ++i) {
      QTimer::singleShot(i * 400, this, [this, i]() {
        AdModalService::Request config;
        config.title = QStringLiteral("Confirmation #%1").arg(i + 1);
        config.text = QStringLiteral("Click destroy-all to close every static modal.");
        config.standardButtons = AdModal::StandardButton::Ok | AdModal::StandardButton::Cancel;
        AdModalService::showConfirm(config, window());
      });
    }
  });
  connect(destroyAllButton, &QAbstractButton::clicked, this, []() { AdModalService::closeAll(); });

  layout->addLayout(row);
  layout->addWidget(makeHintLabel(
      "Equivalent of Modal.destroyAll() on router change. Here it closes all static modals."));
  return box;
}

QWidget* ModalDocsPage::buildStyleClassDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8);

  auto* objectButton = new AdButton("Open Style Modal");
  auto* resolverButton = new AdButton("Open Function Modal");
  objectButton->setButtonStyle(AdButton::ButtonStyle::Solid);
  objectButton->setAccentRole(AdButton::AccentRole::Primary);
  resolverButton->setButtonStyle(AdButton::ButtonStyle::Solid);
  resolverButton->setAccentRole(AdButton::AccentRole::Primary);

  connect(objectButton, &QAbstractButton::clicked, this, [this]() {
    auto* modal = new AdModal(this);
    connect(modal, &AdModal::finished, modal, &QObject::deleteLater);
    modal->setWindowTitle("Custom Style Modal");
    modal->setText("Following the Ant Design specification, this demo customizes semantic slots.");
    const adqt::theme::ThemeMapToken map = demo::resolveTheme(window());
    AdModal::SemanticStyles styles;
    styles.mask.backgroundColor = QColor(24, 24, 27, 230);
    styles.container.backgroundColor = demo::themeColorOr(map.colorBgElevated, QColor("#f7f8fa"));
    styles.container.borderColor = demo::themeColorOr(map.colorBorder, QColor("#d9d9d9"));
    styles.title.textColor = demo::themeColorOr(map.colorText, QColor("#171717"));
    styles.body.textColor = demo::themeColorOr(map.colorText, QColor("#171717"));
    styles.footer.backgroundColor = demo::themeColorOr(map.colorFillAlter, QColor("#fafafa"));
    modal->setSemanticStyles(styles);
    modal->open();
  });

  connect(resolverButton, &QAbstractButton::clicked, this, [this]() {
    auto* modal = new AdModal(this);
    connect(modal, &AdModal::finished, modal, &QObject::deleteLater);
    modal->setWindowTitle("Custom Function Modal");
    modal->setText("Semantic style resolver changes color theme based on open state.");
    modal->setSemanticStyleResolver([](const AdModal::StyleContext& ctx) {
      AdModal::SemanticStyles styles;
      if (ctx.open) {
        styles.container.backgroundColor = QColor("#fffbe6");
        styles.container.borderColor = QColor("#ffe58f");
        styles.title.textColor = QColor("#ad6800");
        styles.body.textColor = QColor("#ad6800");
      } else {
        styles.container.backgroundColor = QColor(53, 71, 125, 204);
        styles.title.textColor = QColor("#ffffff");
        styles.body.textColor = QColor("#ffffff");
      }
      return styles;
    });
    modal->open();
  });

  row->addWidget(objectButton);
  row->addWidget(resolverButton);
  row->addStretch();
  return box;
}

QWidget* ModalDocsPage::buildComponentTokenDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8);

  auto* tokenButton = new AdButton("Open token modal");
  auto* wireframeButton = new AdButton("Open wireframe modal");
  tokenButton->setButtonStyle(AdButton::ButtonStyle::Solid);
  tokenButton->setAccentRole(AdButton::AccentRole::Primary);
  wireframeButton->setButtonStyle(AdButton::ButtonStyle::Outline);
  wireframeButton->setAccentRole(AdButton::AccentRole::Neutral);

  connect(tokenButton, &QAbstractButton::clicked, this, [this]() {
    auto* modal = new AdModal(this);
    connect(modal, &AdModal::finished, modal, &QObject::deleteLater);
    modal->setWindowTitle("Component Token Modal");
    modal->setText("Footer/header/content colors and metrics are overridden via component tokens.");
    AdModal::ComponentTokens tokens;
    tokens.headerBg = QColor("#f9f0ff");
    tokens.bodyBg = QColor("#e6fffb");
    tokens.footerBg = QColor("#f6ffed");
    tokens.titleColor = QColor("#1d39c4");
    tokens.borderRadius = 12;
    tokens.width = 560;
    tokens.headerPaddingHorizontal = 20;
    tokens.bodyPaddingHorizontal = 20;
    tokens.footerPaddingHorizontal = 20;
    modal->setComponentTokens(tokens);
    modal->open();
  });

  connect(wireframeButton, &QAbstractButton::clicked, this, [this]() {
    auto* modal = new AdModal(this);
    connect(modal, &AdModal::finished, modal, &QObject::deleteLater);
    modal->setWindowTitle("Wireframe-like Modal");
    modal->setText("A wireframe style look using semantic slots and token overrides.");
    const adqt::theme::ThemeMapToken map = demo::resolveTheme(window());
    AdModal::ComponentTokens tokens;
    tokens.contentBg = demo::themeColorOr(map.colorBgElevated, QColor("#ffffff"));
    tokens.headerBg = demo::themeColorOr(map.colorBgElevated, QColor("#ffffff"));
    tokens.footerBg = demo::themeColorOr(map.colorBgElevated, QColor("#ffffff"));
    tokens.borderColor = demo::themeColorOr(map.colorBorder, QColor("#d9d9d9"));
    tokens.borderRadius = 8;
    modal->setComponentTokens(tokens);

    AdModal::SemanticStyles styles;
    styles.title.textColor = demo::themeColorOr(map.colorText, QColor("#262626"));
    styles.body.textColor = demo::themeColorOr(map.colorTextSecondary, QColor("#595959"));
    styles.mask.backgroundColor = QColor(0, 0, 0, 70);
    modal->setSemanticStyles(styles);
    modal->open();
  });

  row->addWidget(tokenButton);
  row->addWidget(wireframeButton);
  row->addStretch();
  return box;
}
