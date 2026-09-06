#include "image_docs_page.h"

#include <QDateTime>
#include <QDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

using adqt::widgets::AdImage;
using adqt::widgets::AdImageItem;
using adqt::widgets::AdImageItems;
using adqt::widgets::AdImageListModel;
using adqt::widgets::AdImageViewer;

namespace {

const QUrl kBasicPng(
    QStringLiteral("https://zos.alipayobjects.com/rmsportal/jkjgkEfvpUPVyRjUImniVslZfWPnJuuZ.png"));
const QUrl kSvgA(
    QStringLiteral("https://gw.alipayobjects.com/zos/rmsportal/KDpgvguMpGfqaHPjicRK.svg"));
const QUrl kSvgB(
    QStringLiteral("https://gw.alipayobjects.com/zos/antfincdn/aPkFc8Sj7n/method-draw-image.svg"));
const QUrl kWebpA(QStringLiteral(
    "https://gw.alipayobjects.com/zos/antfincdn/LlvErxo8H9/photo-1503185912284-5271ff81b9a8.webp"));
const QUrl kWebpB(QStringLiteral(
    "https://gw.alipayobjects.com/zos/antfincdn/cV16ZqzMjW/photo-1473091540282-9b846e7965e3.webp"));
const QUrl kWebpC(
    QStringLiteral("https://gw.alipayobjects.com/zos/antfincdn/x43I27A55%26/"
                   "photo-1438109491414-7198515b166b.webp"));
const QUrl kBlurPng(
    QStringLiteral("https://zos.alipayobjects.com/rmsportal/jkjgkEfvpUPVyRjUImniVslZfWPnJuuZ.png"
                   "?x-oss-process=image/blur,r_50,s_50/quality,q_1/resize,m_mfit,h_200,w_200"));

QWidget* makeRow(QWidget* parent = nullptr) {
  auto* box = new QWidget(parent);
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8);
  return box;
}

QLabel* makeInfoLabel(const QString& text, QWidget* parent = nullptr) {
  auto* label = new QLabel(text, parent);
  label->setWordWrap(true);
  return label;
}

AdImageItem makeItem(const QUrl& source, const QString& altText) {
  AdImageItem item;
  item.source = source;
  item.altText = altText;
  return item;
}

AdImageViewer* createViewer(const AdImageItems& items, QObject* parent = nullptr) {
  auto* viewer = new AdImageViewer(parent);
  auto* model = new AdImageListModel(viewer);
  model->setItems(items);
  viewer->setModel(model);
  return viewer;
}

}  // namespace

ImageDocsPage::ImageDocsPage(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(16, 16, 16, 24);
  root->setSpacing(16);

  auto* title = new QLabel(QStringLiteral("Image"));
  QFont titleFont = title->font();
  titleFont.setPointSize(titleFont.pointSize() + 8);
  titleFont.setBold(true);
  title->setFont(titleFont);
  root->addWidget(title);

  auto* subtitle = new QLabel(QStringLiteral("Qt-native image loading and preview."));
  subtitle->setWordWrap(true);
  root->addWidget(subtitle);

  addSection(root, QStringLiteral("Basic Usage"), QStringLiteral("Simple image loading."),
             buildBasicDemo());
  addSection(root, QStringLiteral("Fault Tolerant"),
             QStringLiteral("Fallback source when the primary source fails."), buildFallbackDemo());
  addSection(root, QStringLiteral("Progressive Loading"),
             QStringLiteral("Placeholder source while the main image is loading."),
             buildPlaceholderDemo());
  addSection(root, QStringLiteral("Preview Group"),
             QStringLiteral("Multiple thumbnails share one preview dialog."),
             buildPreviewGroupDemo());
  addSection(root, QStringLiteral("Preview Items"),
             QStringLiteral("One thumbnail can open a full preview album."),
             buildPreviewItemsDemo());
  addSection(root, QStringLiteral("Preview Text And Mask"),
             QStringLiteral("Preview text and mask styling stay within the supported API."),
             buildPreviewMaskDemo());
  addSection(root, QStringLiteral("Semantic Styling"),
             QStringLiteral("Use semantic slots and component tokens for theming."),
             buildStyleDemo());
  addSection(root, QStringLiteral("Preview Info"),
             QStringLiteral("Read current preview item info from the signal."),
             buildPreviewInfoDemo());
  addSection(root, QStringLiteral("Nested Dialog"),
             QStringLiteral("Preview still works when the image lives in a dialog."),
             buildNestedDemo());

  root->addStretch();
}

const QVector<QWidget*>& ImageDocsPage::sectionAnchors() const { return anchors_; }

const QStringList& ImageDocsPage::sectionTitles() const { return titles_; }

void ImageDocsPage::addSection(QVBoxLayout* root, const QString& title, const QString& description,
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

AdImage* ImageDocsPage::createImage(const QUrl& src, const QSize& preferredSize, const QString& alt,
                                    QWidget* parent) {
  auto* image = new AdImage(parent);
  image->setSource(src);
  image->setAltText(alt);
  image->setPreferredImageSize(preferredSize);
  return image;
}

QWidget* ImageDocsPage::buildBasicDemo() {
  auto* box = makeRow();
  auto* row = qobject_cast<QHBoxLayout*>(box->layout());
  row->addWidget(createImage(kBasicPng, QSize(200, 0), QStringLiteral("basic"), box));
  row->addStretch();
  return box;
}

QWidget* ImageDocsPage::buildFallbackDemo() {
  auto* box = makeRow();
  auto* row = qobject_cast<QHBoxLayout*>(box->layout());

  auto* image = createImage(QUrl(QStringLiteral("https://invalid.ant.design/image.png")),
                            QSize(200, 200), QStringLiteral("fallback"), box);
  image->setFallbackSource(kBasicPng);
  row->addWidget(image);
  row->addStretch();
  return box;
}

QWidget* ImageDocsPage::buildPlaceholderDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8);

  auto* image = createImage(kBasicPng, QSize(200, 0), QStringLiteral("progressive"), box);
  image->setPlaceholderSource(kBlurPng);
  row->addWidget(image);

  auto* reload = new QPushButton(QStringLiteral("Reload"));
  connect(reload, &QAbstractButton::clicked, image, [image]() {
    const qint64 stamp = QDateTime::currentMSecsSinceEpoch();
    QUrl updated = kBasicPng;
    updated.setQuery(QString::number(stamp));
    image->setSource(updated);
  });
  row->addWidget(reload, 0, Qt::AlignBottom);
  row->addStretch();
  return box;
}

QWidget* ImageDocsPage::buildPreviewGroupDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* info = makeInfoLabel(QStringLiteral("Current index: 0"), box);
  auto* rowBox = makeRow(box);
  auto* row = qobject_cast<QHBoxLayout*>(rowBox->layout());

  const AdImageItems items = {
      makeItem(kSvgA, QStringLiteral("svg-a")),
      makeItem(kSvgB, QStringLiteral("svg-b")),
      makeItem(kBasicPng, QStringLiteral("png")),
  };
  auto* viewer = createViewer(items, box);

  auto* first = createImage(kSvgA, QSize(200, 0), QStringLiteral("svg-a"), rowBox);
  auto* second = createImage(kSvgB, QSize(200, 0), QStringLiteral("svg-b"), rowBox);
  auto* third = createImage(kBasicPng, QSize(200, 0), QStringLiteral("png"), rowBox);
  first->setViewer(viewer);
  first->setPreviewRow(0);
  second->setViewer(viewer);
  second->setPreviewRow(1);
  third->setViewer(viewer);
  third->setPreviewRow(2);

  connect(viewer, &AdImageViewer::currentItemChanged, box,
          [info](int currentRow, int totalCount, const AdImageItem& item, const QSize&) {
            info->setText(QStringLiteral("Current index: %1 / %2, source: %3")
                              .arg(currentRow + 1)
                              .arg(totalCount)
                              .arg(item.source.toString()));
          });

  row->addWidget(first);
  row->addWidget(second);
  row->addWidget(third);
  row->addStretch();
  layout->addWidget(rowBox);
  layout->addWidget(info);
  return box;
}

QWidget* ImageDocsPage::buildPreviewItemsDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* image = createImage(kWebpA, QSize(220, 140), QStringLiteral("album cover"), box);
  image->setPreviewText(QStringLiteral("Open album"));
  const AdImageItems items = {
      makeItem(kWebpA, QStringLiteral("album-1")),
      makeItem(kWebpB, QStringLiteral("album-2")),
      makeItem(kWebpC, QStringLiteral("album-3")),
  };
  image->setPreviewItems(items);

  auto* info =
      makeInfoLabel(QStringLiteral("Click the thumbnail to open three preview items."), box);
  connect(image->ensureViewer(), &AdImageViewer::currentItemChanged, box,
          [info](int, int, const AdImageItem& item, const QSize& actualSize) {
            info->setText(QStringLiteral("Current item: %1, actual size: %2x%3")
                              .arg(item.altText)
                              .arg(actualSize.width())
                              .arg(actualSize.height()));
          });

  layout->addWidget(image, 0, Qt::AlignLeft);
  layout->addWidget(info);
  return box;
}

QWidget* ImageDocsPage::buildPreviewMaskDemo() {
  auto* box = makeRow();
  auto* row = qobject_cast<QHBoxLayout*>(box->layout());

  auto* defaultMask = createImage(kBasicPng, QSize(140, 0), QStringLiteral("default mask"), box);
  defaultMask->setPreviewText(QStringLiteral("Preview"));

  auto* noMask = createImage(kSvgA, QSize(140, 0), QStringLiteral("no mask"), box);
  noMask->setPreviewText(QStringLiteral("No mask"));
  noMask->ensureViewer()->setMaskVisible(false);

  auto* customMask = createImage(kSvgB, QSize(140, 0), QStringLiteral("custom mask"), box);
  customMask->setPreviewText(QStringLiteral("Read details"));
  AdImage::SemanticStyles imageStyles;
  imageStyles.cover.backgroundColor = QColor(18, 52, 86, 150);
  imageStyles.cover.textColor = QColor(Qt::white);
  customMask->setSemanticStyles(imageStyles);
  AdImageViewer::SemanticStyles viewerStyles;
  viewerStyles.popupMask.backgroundColor = QColor(18, 52, 86, 210);
  customMask->ensureViewer()->setSemanticStyles(viewerStyles);

  row->addWidget(defaultMask);
  row->addWidget(noMask);
  row->addWidget(customMask);
  row->addStretch();
  return box;
}

QWidget* ImageDocsPage::buildStyleDemo() {
  auto* box = makeRow();
  auto* row = qobject_cast<QHBoxLayout*>(box->layout());

  auto* image = createImage(kBasicPng, QSize(180, 0), QStringLiteral("semantic style"), box);
  AdImage::SemanticStyles styles;
  styles.root.borderColor = QColor(QStringLiteral("#0b7285"));
  styles.cover.backgroundColor = QColor(11, 114, 133, 160);
  styles.cover.textColor = QColor(Qt::white);
  image->setSemanticStyles(styles);
  AdImage::ComponentTokens tokens;
  tokens.borderRadius = 16;
  tokens.coverBg = QColor(11, 114, 133, 160);
  tokens.coverColor = QColor(Qt::white);
  image->setComponentTokens(tokens);

  const AdImageItems viewerItems = {
      makeItem(kWebpA, QStringLiteral("tokenized group")),
      makeItem(kWebpB, QStringLiteral("peer")),
  };
  auto* viewer = createViewer(viewerItems, box);
  AdImageViewer::ComponentTokens viewerTokens;
  viewerTokens.previewOperationSize = 22;
  viewer->setComponentTokens(viewerTokens);
  AdImageViewer::SemanticStyles viewerStyles;
  viewerStyles.popupMask.backgroundColor = QColor(15, 23, 42, 214);
  viewerStyles.popupActions.backgroundColor = QColor(255, 255, 255, 24);
  viewer->setSemanticStyles(viewerStyles);

  auto* grouped = createImage(kWebpA, QSize(180, 120), QStringLiteral("tokenized group"), box);
  grouped->setViewer(viewer);
  grouped->setPreviewRow(0);

  auto* peer = createImage(kWebpB, QSize(180, 120), QStringLiteral("peer"), box);
  peer->setViewer(viewer);
  peer->setPreviewRow(1);

  row->addWidget(image);
  row->addWidget(grouped);
  row->addWidget(peer);
  row->addStretch();
  return box;
}

QWidget* ImageDocsPage::buildPreviewInfoDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* image = createImage(kBlurPng, QSize(220, 140), QStringLiteral("preview info"), box);
  image->setPreviewSource(kBasicPng);
  image->setPreviewText(QStringLiteral("Inspect"));

  auto* info = makeInfoLabel(QStringLiteral("Preview item info will appear here."), box);
  connect(image->ensureViewer(), &AdImageViewer::currentItemChanged, box,
          [info](int, int, const AdImageItem& item, const QSize& actualSize) {
            info->setText(QStringLiteral("Source: %1\nAlt: %2\nActual size: %3x%4")
                              .arg(item.source.toString())
                              .arg(item.altText)
                              .arg(actualSize.width())
                              .arg(actualSize.height()));
          });

  layout->addWidget(image, 0, Qt::AlignLeft);
  layout->addWidget(info);
  return box;
}

QWidget* ImageDocsPage::buildNestedDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* button = new QPushButton(QStringLiteral("Open dialog with previewable image"), box);
  layout->addWidget(button, 0, Qt::AlignLeft);
  connect(button, &QAbstractButton::clicked, box, [box]() {
    auto* dialog = new QDialog(box);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->setWindowTitle(QStringLiteral("Nested image preview"));
    auto* root = new QVBoxLayout(dialog);
    root->addWidget(new QLabel(
        QStringLiteral("The preview overlay is attached to this dialog window."), dialog));
    root->addWidget(ImageDocsPage::createImage(kWebpC, QSize(260, 160),
                                               QStringLiteral("nested preview"), dialog),
                    0, Qt::AlignLeft);
    dialog->resize(420, 300);
    dialog->show();
  });

  return box;
}
