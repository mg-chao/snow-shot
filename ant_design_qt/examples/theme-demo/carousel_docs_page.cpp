#include "carousel_docs_page.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

#include <algorithm>

#include "widgets/carousel.h"

using adqt::widgets::AdCarousel;

namespace {

class CarouselDemoSlide final : public QWidget {
 public:
  CarouselDemoSlide(const QColor& color, int number, QWidget* parent = nullptr)
      : QWidget(parent), color_(color), number_(number) {
    setAccessibleName(QStringLiteral("Slide %1").arg(number));
    setMinimumHeight(180);
  }

  QSize sizeHint() const override { return QSize(560, 210); }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), color_);

    QFont numberFont = font();
    numberFont.setPixelSize(38);
    numberFont.setBold(true);
    painter.setFont(numberFont);
    painter.setPen(Qt::white);
    painter.drawText(rect(), Qt::AlignCenter, QString::number(number_));
  }

 private:
  QColor color_;
  int number_ = 1;
};

AdCarousel* makeCarousel(QWidget* parent = nullptr) {
  auto* carousel = new AdCarousel(parent);
  const QList<QColor> colors = {
      QColor(QStringLiteral("#1677ff")), QColor(QStringLiteral("#13c2c2")),
      QColor(QStringLiteral("#722ed1")), QColor(QStringLiteral("#d4380d"))};
  for (int index = 0; index < colors.size(); ++index) {
    carousel->addSlide(new CarouselDemoSlide(colors.at(index), index + 1));
  }
  carousel->setMinimumHeight(210);
  return carousel;
}

}  // namespace

CarouselDocsPage::CarouselDocsPage(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(16, 16, 16, 24);
  root->setSpacing(16);

  auto* title = new QLabel(QStringLiteral("Carousel"));
  QFont titleFont = title->font();
  titleFont.setPointSize(titleFont.pointSize() + 8);
  titleFont.setBold(true);
  title->setFont(titleFont);
  root->addWidget(title);

  auto* subtitle = new QLabel(QStringLiteral(
      "A carousel rotates peer content through a compact, focused viewport. Slides remain ordinary "
      "Qt widgets with explicit ownership, native focus, keyboard input, and theme-aware "
      "controls."));
  subtitle->setWordWrap(true);
  root->addWidget(subtitle);

  addSection(root, QStringLiteral("Basic"),
             QStringLiteral("The default mirrors Ant Design: bottom indicators, infinite wrapping, "
                            "a 500 ms scroll transition, and hidden arrows."),
             buildBasicDemo());
  addSection(
      root, QStringLiteral("Indicator placement"),
      QStringLiteral("Top and bottom keep horizontal motion. Start and end use vertical motion "
                     "and follow Qt layout direction."),
      buildPlacementDemo());
  addSection(
      root, QStringLiteral("Autoplay progress"),
      QStringLiteral("Progress indicators use the autoplay interval and pause while the pointer "
                     "is over the carousel."),
      buildAutoplayDemo());
  addSection(
      root, QStringLiteral("Fade"),
      QStringLiteral("Fade transitions preserve each slide widget and render through a clipped "
                     "transition layer."),
      buildFadeDemo());
  addSection(
      root, QStringLiteral("Arrows and finite content"),
      QStringLiteral("Finite carousels disable unavailable arrows, exposing native focus and "
                     "accessibility state at each boundary."),
      buildArrowsDemo());
  addSection(root, QStringLiteral("Component tokens"),
             QStringLiteral("Per-instance color and metric tokens overlay the resolved application "
                            "theme without replacing semantic defaults."),
             buildTokenDemo());
  root->addStretch();
}

const QVector<QWidget*>& CarouselDocsPage::sectionAnchors() const { return anchors_; }
const QStringList& CarouselDocsPage::sectionTitles() const { return titles_; }

void CarouselDocsPage::addSection(QVBoxLayout* root, const QString& title,
                                  const QString& description, QWidget* content) {
  auto* panel = new QFrame;
  panel->setFrameShape(QFrame::StyledPanel);
  auto* layout = new QVBoxLayout(panel);
  layout->setContentsMargins(14, 14, 14, 14);
  layout->setSpacing(10);
  auto* heading = new QLabel(title);
  QFont font = heading->font();
  font.setBold(true);
  font.setPointSize(font.pointSize() + 1);
  heading->setFont(font);
  auto* copy = new QLabel(description);
  copy->setWordWrap(true);
  layout->addWidget(heading);
  layout->addWidget(copy);
  layout->addWidget(content);
  root->addWidget(panel);
  anchors_.append(panel);
  titles_.append(title);
}

QWidget* CarouselDocsPage::buildBasicDemo() { return makeCarousel(); }

QWidget* CarouselDocsPage::buildPlacementDemo() {
  auto* box = new QWidget;
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);
  auto* controls = new QHBoxLayout;
  controls->addWidget(new QLabel(QStringLiteral("Indicator placement:")));
  auto* placement = new QComboBox;
  placement->addItems({QStringLiteral("bottom"), QStringLiteral("top"), QStringLiteral("start"),
                       QStringLiteral("end")});
  controls->addWidget(placement);
  controls->addStretch();
  auto* carousel = makeCarousel();
  carousel->setArrowsVisible(true);
  connect(placement, &QComboBox::currentIndexChanged, carousel, [carousel](int index) {
    static const AdCarousel::DotPlacement placements[] = {
        AdCarousel::DotPlacement::Bottom, AdCarousel::DotPlacement::Top,
        AdCarousel::DotPlacement::Start, AdCarousel::DotPlacement::End};
    carousel->setDotPlacement(placements[std::clamp(index, 0, 3)]);
  });
  layout->addLayout(controls);
  layout->addWidget(carousel);
  return box;
}

QWidget* CarouselDocsPage::buildAutoplayDemo() {
  auto* carousel = makeCarousel();
  carousel->setAutoplay(true);
  carousel->setAutoplayInterval(5000);
  carousel->setAutoplayProgressVisible(true);
  return carousel;
}

QWidget* CarouselDocsPage::buildFadeDemo() {
  auto* carousel = makeCarousel();
  carousel->setEffect(AdCarousel::Effect::Fade);
  carousel->setAutoplay(true);
  return carousel;
}

QWidget* CarouselDocsPage::buildArrowsDemo() {
  auto* carousel = makeCarousel();
  carousel->setArrowsVisible(true);
  carousel->setInfinite(false);
  return carousel;
}

QWidget* CarouselDocsPage::buildTokenDemo() {
  auto* carousel = makeCarousel();
  carousel->setArrowsVisible(true);
  AdCarousel::ComponentTokens tokens;
  tokens.colors.arrowColor = QColor(QStringLiteral("#fff1b8"));
  tokens.colors.dotColor = QColor(QStringLiteral("#fff1b8"));
  tokens.colors.focusOutline = QColor(QStringLiteral("#faad14"));
  tokens.metrics.arrowSize = 20;
  tokens.metrics.dotWidth = 20;
  tokens.metrics.dotActiveWidth = 32;
  tokens.metrics.dotHeight = 4;
  tokens.metrics.dotGap = 5;
  carousel->setComponentTokens(tokens);
  return carousel;
}
