#include "JumpSlider.h"

#include <QMouseEvent>
#include <QStyle>
#include <QStyleOptionSlider>

#include <algorithm>

void JumpSlider::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        setSliderDown(true);
        jumpToPosition(event->pos());
        event->accept();
        return;
    }
    QSlider::mousePressEvent(event);
}

void JumpSlider::mouseMoveEvent(QMouseEvent *event)
{
    if ((event->buttons() & Qt::LeftButton) && isSliderDown()) {
        jumpToPosition(event->pos());
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
