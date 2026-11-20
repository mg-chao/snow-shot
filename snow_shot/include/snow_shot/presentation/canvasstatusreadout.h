#ifndef SNOW_SHOT_PRESENTATION_CANVASSTATUSREADOUT_H
#define SNOW_SHOT_PRESENTATION_CANVASSTATUSREADOUT_H

#include <QDeadlineTimer>
#include <QLabel>
#include <QRectF>

class QPainter;
class QTimer;

class CanvasStatusReadout final : public QLabel {
    Q_OBJECT

  public:
    enum class Message {
        Scale,
        Opacity,
        Locked,
        MotionPreview,
        ResultPreview,
        RenderingPreview,
        Thinking
    };
    enum class Corner { BottomLeft, TopLeft };
    enum class DisplayMode { Hidden, Transient, Persistent };

    struct State {
        Message message = Message::Scale;
        qreal value = 0;
        int decimals = 0;
        QRectF anchor;
        Corner corner = Corner::BottomLeft;
        bool visible = false;
        DisplayMode mode = DisplayMode::Hidden;
        QDeadlineTimer deadline{0, Qt::PreciseTimer};
        bool customText = false;
        QString fullText;
    };

    explicit CanvasStatusReadout(QWidget* parent);

    // Scale and opacity values are percentages; decimals controls their display precision.
    void setMessage(Message message, qreal value = 0, int decimals = 0);
    void setScale(qreal factor, bool showOnChange = false);
    [[nodiscard]] Message message() const;
    void setText(const QString& text);
    void layoutIn(const QRectF& anchor, Corner corner = Corner::BottomLeft);
    void showTransient();
    void showPersistent();
    void dismiss();
    [[nodiscard]] QTimer* dismissalTimer() const;
    [[nodiscard]] State captureState() const;
    void restoreState(const State& state);
    void paintOverlay(QPainter& painter);

  signals:
    void presentationChanged();

  protected:
    void changeEvent(QEvent* event) override;

  private:
    [[nodiscard]] QString formattedMessage() const;
    void refreshPresentation();
    void updateLayout();

    QTimer* m_timer = nullptr;
    Message m_message = Message::Scale;
    qreal m_value = 0;
    int m_decimals = 0;
    QString m_fullText;
    QRectF m_anchor;
    Corner m_corner = Corner::BottomLeft;
    DisplayMode m_mode = DisplayMode::Hidden;
    QDeadlineTimer m_deadline{0, Qt::PreciseTimer};
    bool m_customText = false;
    bool m_hasAnchor = false;
    bool m_validLayout = false;
    bool m_initializing = true;
    bool m_refreshing = false;
};

#endif // SNOW_SHOT_PRESENTATION_CANVASSTATUSREADOUT_H
