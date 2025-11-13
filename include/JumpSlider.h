#pragma once

#include <QSlider>

class QMouseEvent;

class JumpSlider : public QSlider
{
public:
    using QSlider::QSlider;

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    void jumpToPosition(const QPoint &pos);
};
