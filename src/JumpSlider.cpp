#include "JumpSlider.h"

#include <QMouseEvent>
#include <QStyle>
#include <QStyleOptionSlider>

#include <algorithm>

void JumpSlider::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        setSliderDown(true);
        Q_EMIT sliderPressed();
        jumpToPosition(event->pos());
        Q_EMIT sliderMoved(value());
        event->accept();
        return;
    }
    QSlider::mousePressEvent(event);
}

void JumpSlider::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && isSliderDown()) {
        setSliderDown(false);
        Q_EMIT sliderReleased();
        event->accept();
        return;
    }
    QSlider::mouseReleaseEvent(event);
}

void JumpSlider::mouseMoveEvent(QMouseEvent *event)
{
    if ((event->buttons() & Qt::LeftButton) && isSliderDown()) {
        jumpToPosition(event->pos());
        Q_EMIT sliderMoved(value());
        event->accept();
        return;
    }
    QSlider::mouseMoveEvent(event);
}

void JumpSlider::jumpToPosition(const QPoint &pos)
{
    QStyleOptionSlider option;
    initStyleOption(&option);

    const int sliderLength = style()->pixelMetric(QStyle::PM_SliderLength, &option, this);
    const int sliderSpanRaw = (orientation() == Qt::Horizontal ? width() : height()) - sliderLength;
    const int sliderSpan = std::max(1, sliderSpanRaw);

    int coordinate = (orientation() == Qt::Horizontal ? pos.x() : pos.y()) - sliderLength / 2;
    coordinate = std::clamp(coordinate, 0, sliderSpan);

    const bool upsideDown = option.upsideDown;
    const int newValue = QStyle::sliderValueFromPosition(minimum(), maximum(), coordinate, sliderSpan, upsideDown);
    setValue(newValue);
}
