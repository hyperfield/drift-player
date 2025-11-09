#include "BassVisualizerWidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QtMath>
#include <algorithm>
#include <random>

namespace
{
constexpr double kUpdateHz = 60.0;
constexpr double kPhaseIncrement = 2.0 * M_PI * 0.8 / kUpdateHz;
constexpr double kDecay = 0.12;
constexpr int kBarCount = 32;
}

BassVisualizerWidget::BassVisualizerWidget(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumHeight(80);

    m_timer.setInterval(static_cast<int>(1000.0 / kUpdateHz));
    connect(&m_timer, &QTimer::timeout, this, &BassVisualizerWidget::handleTick);
    m_timer.start();
}

void BassVisualizerWidget::setSensitivity(double value)
{
    m_sensitivity = std::clamp(value, 0.0, 1.0);
}

void BassVisualizerWidget::setActive(bool active)
{
    m_active = active;
    if (!m_active) {
        m_targetLevel = 0.0;
    }
}

void BassVisualizerWidget::handleTick()
{
    if (!isVisible()) {
        return;
    }

    m_phase += kPhaseIncrement;
    if (m_phase > 2.0 * M_PI) {
        m_phase -= 2.0 * M_PI;
    }

    if (m_active) {
        const double primary = 0.65 * (std::sin(m_phase) + 1.0) * 0.5;
        const double secondary = 0.35 * (std::sin(m_phase * 0.5 + 1.3) + 1.0) * 0.5;
        m_targetLevel = std::clamp((primary + secondary) * m_sensitivity, 0.0, 1.0);
    } else {
        m_targetLevel = 0.0;
    }

    m_currentLevel += (m_targetLevel - m_currentLevel) * kDecay;
    update();
}

void BassVisualizerWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.fillRect(rect(), QColor(10, 12, 18, 120));

    const QRectF bounds = rect().adjusted(18, 14, -18, -14);
    const double barWidth = bounds.width() / static_cast<double>(kBarCount);
    const double maxHeight = bounds.height();

    for (int i = 0; i < kBarCount; ++i) {
        const double offset = static_cast<double>(i) / kBarCount;
        const double envelope = std::sin(offset * M_PI);
        const double wave = std::sin(m_phase * (1.0 + offset * 0.8) + offset * 4.0);
        const double amplitude = std::clamp((wave + 1.0) * 0.5 * envelope * m_currentLevel, 0.0, 1.0);
        const double height = amplitude * maxHeight;

        QRectF barRect(bounds.left() + i * barWidth,
                       bounds.bottom() - height,
                       barWidth * 0.8,
                       height);

        QColor barColor = QColor::fromRgbF(0.45 + 0.5 * amplitude,
                                           0.55,
                                           0.9,
                                           0.35 + 0.45 * amplitude);
        painter.setBrush(barColor);
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(barRect, 3.0, 3.0);
    }

    QLinearGradient grad(bounds.topLeft(), bounds.bottomLeft());
    grad.setColorAt(0.0, QColor(12, 14, 18, 180));
    grad.setColorAt(0.25, Qt::transparent);
    grad.setColorAt(0.75, Qt::transparent);
    grad.setColorAt(1.0, QColor(12, 14, 18, 180));
    painter.fillRect(rect(), grad);
}
