#pragma once

#include <QSlider>

class JumpSlider : public QSlider
{
public:
    using QSlider::QSlider;

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    void jumpToPosition(const QPoint &pos);
};
