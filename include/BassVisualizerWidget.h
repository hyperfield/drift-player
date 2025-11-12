#pragma once

#include <QWidget>
#include <QTimer>

class BassVisualizerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit BassVisualizerWidget(QWidget *parent = nullptr);

    void setSensitivity(double value); // range 0.0 - 1.0
    void setActive(bool active);

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void handleTick();

private:
    QTimer m_timer;
    double m_phase = 0.0;
    double m_sensitivity = 0.6;
    double m_currentLevel = 0.0;
    double m_targetLevel = 0.0;
    bool m_active = false;
};

