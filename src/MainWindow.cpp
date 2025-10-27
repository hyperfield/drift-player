#include "MainWindow.h"

#include "VideoBackgroundWidget.h"

#include <QAbstractItemView>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QCursor>
#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPainter>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRandomGenerator>
#include <QEasingCurve>
#include <QElapsedTimer>
#include <QSlider>
#include <QSize>
#include <QSizePolicy>
#include <QStackedLayout>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QTimer>
#include <QSignalBlocker>
#include <QWidget>
#include <cmath>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace
{
constexpr int kDefaultVolume = 60;
constexpr int kDefaultBlur = 70;
constexpr qreal kInteractiveActiveOpacity = 1.0;
constexpr qreal kInteractiveIdleOpacity = 0.55;
constexpr int kInteractiveFadeDelayMs = 2000;

QString displayNameForFile(const QString &filePath)
{
    QFileInfo info(filePath);
    QString base = info.completeBaseName();
    if (base.isEmpty()) {
        base = info.fileName();
    }
    base.replace(QChar('_'), QChar(' '));
    base.replace(QChar('-'), QChar(' '));
    return base.simplified();
}

QString formatTime(double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0.0) {
        seconds = 0.0;
    }

    int totalSeconds = static_cast<int>(seconds + 0.5);
    int hours = totalSeconds / 3600;
    int minutes = (totalSeconds % 3600) / 60;
    int secs = totalSeconds % 60;

    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours, 2, 10, QChar('0'))
            .arg(minutes, 2, 10, QChar('0'))
            .arg(secs, 2, 10, QChar('0'));
    }

    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0'));
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_settings("evoid", "Drift Player")
{
    resize(980, 640);
    setMinimumSize(720, 460);
    setupUi();
    loadSettings();
    updatePlayPauseButton(false);
}

MainWindow::~MainWindow()
{
    saveSettings();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();
    event->accept();
}

void MainWindow::handleAddMedia()
{
    const bool wasPlaying = m_videoWidget && m_videoWidget->hasMedia() && !m_videoWidget->isPaused();
    if (wasPlaying) {
        spdlog::info("Pausing playback for Add Media dialog");
        m_videoWidget->pause();
    }

    const QString filter = tr("Media files (*.mp4 *.mkv *.mov *.mp3 *.flac *.wav);;All files (*.*)");
    spdlog::info("Opening modal Add Media dialog");
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Add Media"), QString(), filter);

    if (wasPlaying) {
        spdlog::info("Resuming playback after Add Media dialog");
        m_videoWidget->play();
    }

    if (!files.isEmpty()) {
        spdlog::info("Add Media selection received: {} entries", files.size());
        processSelectedFiles(files);
    } else {
        spdlog::info("Add Media dialog dismissed without selection");
    }
}

void MainWindow::handlePlayPause()
{
    if (!m_videoWidget->hasMedia() && !m_tracks.isEmpty()) {
        playTrack(0);
        return;
    }

    if (m_videoWidget->isPaused()) {
        m_videoWidget->play();
    } else {
        m_videoWidget->pause();
    }
}

void MainWindow::handlePlayNext()
{
    const int nextIndex = resolveNextIndex();
    if (nextIndex != -1) {
        playTrack(nextIndex);
    }
}

void MainWindow::handlePlayPrevious()
{
    const int prevIndex = resolvePreviousIndex();
    if (prevIndex != -1) {
        playTrack(prevIndex);
    }
}

void MainWindow::handlePlaylistActivated(QListWidgetItem *item)
{
    if (!item) {
        return;
    }

    const int row = m_playlist->row(item);
    playTrack(row);
}

void MainWindow::handleShuffleToggled()
{
    m_shuffleEnabled = !m_shuffleEnabled;
    m_shuffleButton->setChecked(m_shuffleEnabled);
}

void MainWindow::handleRepeatMode()
{
    switch (m_repeatMode) {
    case RepeatMode::None:
        m_repeatMode = RepeatMode::All;
        break;
    case RepeatMode::All:
        m_repeatMode = RepeatMode::One;
        break;
    case RepeatMode::One:
        m_repeatMode = RepeatMode::None;
        break;
    }

    updateRepeatButton();
}

void MainWindow::handlePlaybackStateChanged(bool playing)
{
    updatePlayPauseButton(playing);
    updateTransportAvailability();
}

void MainWindow::handlePlaybackFinished()
{
    const int nextIndex = resolveNextIndex();
    if (nextIndex != -1) {
        playTrack(nextIndex);
    } else {
        updatePlayPauseButton(false);
        updateNowPlaying(QString());
        updateTransportAvailability();
    }
}

void MainWindow::handleVolumeChanged(int value)
{
    m_videoWidget->setVolume(value);
}

void MainWindow::handleBlurChanged(int value)
{
    if (m_videoWidget) {
        m_videoWidget->setBlurAmount(static_cast<float>(value) / 100.0f);
    }
}

void MainWindow::handleProgressSliderPressed()
{
    m_progressSliderPressed = true;
    m_progressFadeTimer.stop();
    setWidgetOpacity(m_progressOpacity, m_progressFadeAnimation, kInteractiveActiveOpacity, 120);
}

void MainWindow::handleProgressSliderReleased()
{
    if (!m_progressSlider || !m_videoWidget->hasMedia()) {
        m_progressSliderPressed = false;
        m_progressFadeTimer.start(kInteractiveFadeDelayMs);
        return;
    }

    if (m_lastDuration <= 0.0) {
        m_progressSliderPressed = false;
        m_progressFadeTimer.start(kInteractiveFadeDelayMs);
        return;
    }

    const int maxValue = m_progressSlider->maximum();
    if (maxValue <= 0) {
        m_progressSliderPressed = false;
        m_progressFadeTimer.start(kInteractiveFadeDelayMs);
        return;
    }

    const double ratio = static_cast<double>(m_progressSlider->value()) / static_cast<double>(maxValue);
    const double targetPosition = ratio * m_lastDuration;
    m_videoWidget->seek(targetPosition);
    if (m_timeLabel) {
        m_timeLabel->setText(QStringLiteral("%1 / %2")
                                 .arg(formatTime(targetPosition),
                                     formatTime(m_lastDuration)));
    }
    m_progressSliderPressed = false;
    m_progressFadeTimer.start(kInteractiveFadeDelayMs);
}

void MainWindow::handleProgressSliderMoved(int value)
{
    if (!m_progressSliderPressed || m_lastDuration <= 0.0 || !m_timeLabel) {
        return;
    }

    const int maxValue = m_progressSlider->maximum();
    if (maxValue <= 0) {
        return;
    }

    const double ratio = static_cast<double>(value) / static_cast<double>(maxValue);
    const double previewPosition = ratio * m_lastDuration;
    m_timeLabel->setText(QStringLiteral("%1 / %2")
                             .arg(formatTime(previewPosition),
                                  formatTime(m_lastDuration)));
    m_progressFadeTimer.stop();
    setWidgetOpacity(m_progressOpacity, m_progressFadeAnimation, kInteractiveActiveOpacity, 120);
}

void MainWindow::handlePositionChanged(double position, double duration)
{
    m_lastDuration = duration;

    if (m_progressSlider) {
        if (duration <= 0.0) {
            const QSignalBlocker blocker(m_progressSlider);
            m_progressSlider->setValue(0);
            m_progressSlider->setEnabled(false);
        } else {
            if (!m_progressSlider->isEnabled()) {
                m_progressSlider->setEnabled(true);
            }

            if (!m_progressSliderPressed) {
                const int maxValue = m_progressSlider->maximum();
                int sliderValue = 0;
                if (maxValue > 0) {
                    sliderValue = static_cast<int>((position / duration) * maxValue + 0.5);
                    if (sliderValue < 0) {
                        sliderValue = 0;
                    } else if (sliderValue > maxValue) {
                        sliderValue = maxValue;
                    }
                }

                const QSignalBlocker blocker(m_progressSlider);
                m_progressSlider->setValue(sliderValue);
            }
        }
    }

    if (m_timeLabel) {
        m_timeLabel->setText(QStringLiteral("%1 / %2")
                                 .arg(formatTime(position),
                                      formatTime(duration)));
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    handleFadeEvent(watched, event, m_playlist, m_playlistFadeTimer, m_playlistOpacity, m_playlistFadeAnimation);
    handleFadeEvent(watched, event, m_controlsContainer, m_controlsFadeTimer, m_controlsOpacity, m_controlsFadeAnimation);
    handleFadeEvent(watched, event, m_progressContainer, m_progressFadeTimer, m_progressOpacity, m_progressFadeAnimation);

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::setupUi()
{
    m_videoWidget = new VideoBackgroundWidget(this);

    auto *overlay = new QWidget(this);
    overlay->setAttribute(Qt::WA_TranslucentBackground);
    overlay->setAutoFillBackground(false);
    overlay->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_playlist = new QListWidget(overlay);
    m_playlist->setSelectionMode(QAbstractItemView::SingleSelection);
    m_playlist->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_playlist->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_playlist->setFrameShape(QFrame::NoFrame);
    m_playlist->setWordWrap(true);
    m_playlist->setSpacing(6);
    m_playlist->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    m_playlist->setMinimumWidth(0);
    QFont playlistFont = m_playlist->font();
    playlistFont.setPointSize(12);
    playlistFont.setLetterSpacing(QFont::PercentageSpacing, 102);
    m_playlist->setFont(playlistFont);
    m_playlist->setStyleSheet(R"(
        QListWidget {
            background-color: rgba(12, 12, 20, 160);
            color: rgba(245, 245, 250, 220);
            border-radius: 18px;
            padding: 16px;
        }
        QListWidget::item {
            margin: 4px 0;
            padding: 10px 14px;
            border-radius: 12px;
        }
        QListWidget::item:selected {
            background: rgba(255, 255, 255, 60);
            color: rgba(255, 255, 255, 250);
        }
        QListWidget::item:hover {
            background: rgba(255, 255, 255, 36);
            color: rgba(255, 255, 255, 255);
        }
        QListWidget QScrollBar:vertical {
            background: transparent;
            width: 10px;
            margin: 6px 0;
        }
        QListWidget QScrollBar::handle:vertical {
            background: rgba(255, 255, 255, 60);
            border-radius: 6px;
        }
        QListWidget QScrollBar::handle:vertical:hover {
            background: rgba(255, 255, 255, 90);
        }
        QListWidget QScrollBar::add-line:vertical,
        QListWidget QScrollBar::sub-line:vertical {
            background: none;
        }
    )");
    m_playlistOpacity = new QGraphicsOpacityEffect(m_playlist);
    m_playlistOpacity->setOpacity(kInteractiveIdleOpacity);
    m_playlist->setGraphicsEffect(m_playlistOpacity);

    m_playlistFadeAnimation = new QPropertyAnimation(m_playlistOpacity, "opacity", this);
    m_playlistFadeAnimation->setDuration(250);
    m_playlistFadeAnimation->setEasingCurve(QEasingCurve::InOutQuad);

    m_playlistFadeTimer.setParent(this);
    m_playlistFadeTimer.setInterval(kInteractiveFadeDelayMs);
    m_playlistFadeTimer.setSingleShot(true);
    connect(&m_playlistFadeTimer, &QTimer::timeout, this, [this]() {
        setWidgetOpacity(m_playlistOpacity, m_playlistFadeAnimation, kInteractiveIdleOpacity, 500);
    });

    m_playlist->setMouseTracking(true);
    if (QWidget *viewport = m_playlist->viewport()) {
        viewport->setMouseTracking(true);
    }

    auto registerInteractive = [this](QWidget *widget) {
        if (!widget) {
            return;
        }
        widget->setMouseTracking(true);
        widget->setAttribute(Qt::WA_Hover, true);
        widget->installEventFilter(this);
        const auto children = widget->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
        for (QWidget *child : children) {
            child->setMouseTracking(true);
            child->setAttribute(Qt::WA_Hover, true);
            child->installEventFilter(this);
        }
    };

    registerInteractive(m_playlist);

    connect(m_playlist, &QListWidget::itemDoubleClicked, this, &MainWindow::handlePlaylistActivated);

    m_progressContainer = new QWidget(overlay);
    m_progressContainer->setAttribute(Qt::WA_TranslucentBackground);
    m_progressContainer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    m_progressSlider = new QSlider(Qt::Horizontal, m_progressContainer);
    m_progressSlider->setRange(0, 1000);
    m_progressSlider->setCursor(Qt::PointingHandCursor);
    m_progressSlider->setEnabled(false);
    m_progressSlider->setSingleStep(1);
    m_progressSlider->setPageStep(10);
    m_progressSlider->setToolTip(tr("Scrub position"));
    m_progressSlider->setStyleSheet(R"(
        QSlider {
            height: 24px;
        }
        QSlider::groove:horizontal {
            background: rgba(255, 255, 255, 40);
            border-radius: 4px;
            height: 6px;
        }
        QSlider::sub-page:horizontal {
            background: rgba(120, 180, 255, 200);
            border-radius: 4px;
        }
        QSlider::add-page:horizontal {
            background: rgba(255, 255, 255, 22);
            border-radius: 4px;
        }
        QSlider::handle:horizontal {
            background: rgba(255, 255, 255, 240);
            width: 18px;
            margin: -7px 0;
            border-radius: 9px;
        }
        QSlider::handle:horizontal:hover {
            background: rgba(255, 255, 255, 255);
        }
    )");

    m_timeLabel = new QLabel(QStringLiteral("00:00 / 00:00"), m_progressContainer);
    m_timeLabel->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
    m_timeLabel->setMinimumWidth(120);
    m_timeLabel->setStyleSheet("color: rgba(255, 255, 255, 205); font-size: 12px; letter-spacing: 0.3px;");

    auto *progressLayout = new QHBoxLayout(m_progressContainer);
    progressLayout->setContentsMargins(0, 0, 0, 0);
    progressLayout->setSpacing(14);
    progressLayout->addWidget(m_progressSlider, 1);
    progressLayout->addWidget(m_timeLabel, 0, Qt::AlignVCenter | Qt::AlignRight);

    m_progressOpacity = new QGraphicsOpacityEffect(m_progressContainer);
    m_progressOpacity->setOpacity(kInteractiveIdleOpacity);
    m_progressContainer->setGraphicsEffect(m_progressOpacity);

    m_progressFadeAnimation = new QPropertyAnimation(m_progressOpacity, "opacity", this);
    m_progressFadeAnimation->setDuration(250);
    m_progressFadeAnimation->setEasingCurve(QEasingCurve::InOutQuad);

    m_progressFadeTimer.setParent(this);
    m_progressFadeTimer.setInterval(kInteractiveFadeDelayMs);
    m_progressFadeTimer.setSingleShot(true);
    connect(&m_progressFadeTimer, &QTimer::timeout, this, [this]() {
        setWidgetOpacity(m_progressOpacity, m_progressFadeAnimation, kInteractiveIdleOpacity, 500);
    });

    const QString iconButtonStyle = QStringLiteral(
        "QToolButton {\n"
        "    background-color: rgba(255, 255, 255, 32);\n"
        "    border: none;\n"
        "    border-radius: 20px;\n"
        "    padding: 8px;\n"
        "    color: rgba(255, 255, 255, 230);\n"
        "}\n"
        "QToolButton:hover {\n"
        "    background-color: rgba(255, 255, 255, 56);\n"
        "}\n"
        "QToolButton:pressed {\n"
        "    background-color: rgba(255, 255, 255, 76);\n"
        "}\n");

    const QString textButtonStyle = QStringLiteral(
        "QToolButton, QPushButton {\n"
        "    background-color: rgba(255, 255, 255, 34);\n"
        "    border: none;\n"
        "    border-radius: 18px;\n"
        "    padding: 8px 18px;\n"
        "    color: rgba(255, 255, 255, 230);\n"
        "    font-weight: 500;\n"
    "}\n"
        "QToolButton:hover, QPushButton:hover {\n"
        "    background-color: rgba(255, 255, 255, 58);\n"
        "}\n"
        "QToolButton:pressed, QPushButton:pressed {\n"
        "    background-color: rgba(255, 255, 255, 80);\n"
        "}\n"
        "QToolButton:checked {\n"
        "    background-color: rgba(120, 180, 255, 120);\n"
        "    color: rgba(255, 255, 255, 255);\n"
        "}\n");

    const QSize iconButtonSize(28, 28);

    m_controlsContainer = new QWidget(overlay);
    m_controlsContainer->setAttribute(Qt::WA_TranslucentBackground);
    m_controlsContainer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    m_addButton = new QPushButton(tr("Add Media"), m_controlsContainer);
    m_addButton->setCursor(Qt::PointingHandCursor);

    m_playPauseButton = new QToolButton(m_controlsContainer);
    m_playPauseButton->setCheckable(false);
    m_playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_playPauseButton->setToolTip(tr("Play / Pause"));
    m_playPauseButton->setCursor(Qt::PointingHandCursor);

    m_prevButton = new QToolButton(m_controlsContainer);
    m_prevButton->setIcon(style()->standardIcon(QStyle::SP_MediaSkipBackward));
    m_prevButton->setToolTip(tr("Previous"));
    m_prevButton->setCursor(Qt::PointingHandCursor);

    m_nextButton = new QToolButton(m_controlsContainer);
    m_nextButton->setIcon(style()->standardIcon(QStyle::SP_MediaSkipForward));
    m_nextButton->setToolTip(tr("Next"));
    m_nextButton->setCursor(Qt::PointingHandCursor);

    m_shuffleButton = new QToolButton(m_controlsContainer);
    m_shuffleButton->setText(tr("Shuffle"));
    m_shuffleButton->setCheckable(true);
    m_shuffleButton->setCursor(Qt::PointingHandCursor);

    m_repeatButton = new QToolButton(m_controlsContainer);
    m_repeatButton->setCheckable(false);
    m_repeatButton->setCursor(Qt::PointingHandCursor);
    updateRepeatButton();

    m_prevButton->setIconSize(iconButtonSize);
    m_nextButton->setIconSize(iconButtonSize);
    m_playPauseButton->setIconSize(QSize(32, 32));

    m_prevButton->setStyleSheet(iconButtonStyle);
    m_nextButton->setStyleSheet(iconButtonStyle);
    m_playPauseButton->setStyleSheet(iconButtonStyle);

    m_addButton->setStyleSheet(textButtonStyle);
    m_shuffleButton->setStyleSheet(textButtonStyle);
    m_repeatButton->setStyleSheet(textButtonStyle);

    m_blurSlider = new QSlider(Qt::Horizontal, m_controlsContainer);
    m_blurSlider->setRange(0, 100);
    m_blurSlider->setValue(kDefaultBlur);
    m_blurSlider->setFixedWidth(160);
    m_blurSlider->setCursor(Qt::PointingHandCursor);
    m_blurSlider->setToolTip(tr("Background blur"));
    m_blurSlider->setStyleSheet(R"(
        QSlider {
            height: 18px;
        }
        QSlider::groove:horizontal {
            background: rgba(255, 255, 255, 28);
            border-radius: 3px;
            height: 4px;
        }
        QSlider::sub-page:horizontal {
            background: rgba(160, 200, 255, 140);
            border-radius: 3px;
        }
        QSlider::add-page:horizontal {
            background: rgba(255, 255, 255, 18);
            border-radius: 3px;
        }
        QSlider::handle:horizontal {
            background: rgba(255, 255, 255, 230);
            width: 16px;
            margin: -6px 0;
            border-radius: 8px;
        }
        QSlider::handle:horizontal:hover {
            background: rgba(255, 255, 255, 255);
        }
    )");

    m_volumeSlider = new QSlider(Qt::Horizontal, m_controlsContainer);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(kDefaultVolume);
    m_volumeSlider->setFixedWidth(180);
    m_volumeSlider->setToolTip(tr("Volume"));
    m_volumeSlider->setCursor(Qt::PointingHandCursor);
    m_volumeSlider->setStyleSheet(R"(
        QSlider {
            height: 18px;
        }
        QSlider::groove:horizontal {
            background: rgba(255, 255, 255, 28);
            border-radius: 3px;
            height: 4px;
        }
        QSlider::sub-page:horizontal {
            background: rgba(255, 255, 255, 130);
            border-radius: 3px;
        }
        QSlider::add-page:horizontal {
            background: rgba(255, 255, 255, 18);
            border-radius: 3px;
        }
        QSlider::handle:horizontal {
            background: rgba(255, 255, 255, 230);
            width: 16px;
            margin: -6px 0;
            border-radius: 8px;
        }
        QSlider::handle:horizontal:hover {
            background: rgba(255, 255, 255, 255);
        }
    )");

    m_titleLabel = new QLabel(tr("Add tracks to begin"), m_controlsContainer);
    m_titleLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    m_titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_titleLabel->setMinimumHeight(36);
    m_titleLabel->setWordWrap(true);
    m_titleLabel->setStyleSheet(R"(
        color: rgba(255, 255, 255, 230);
        background-color: rgba(10, 10, 18, 170);
        padding: 8px 18px;
        border-radius: 18px;
        font-size: 14px;
        font-weight: 500;
        letter-spacing: 0.4px;
    )");

    auto *controlsLayout = new QVBoxLayout(m_controlsContainer);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->setSpacing(12);
    controlsLayout->addWidget(m_titleLabel);

    auto *buttonsLayout = new QHBoxLayout;
    buttonsLayout->setContentsMargins(0, 0, 0, 0);
    buttonsLayout->setSpacing(14);
    buttonsLayout->addWidget(m_addButton);
    buttonsLayout->addSpacing(12);
    buttonsLayout->addWidget(m_prevButton);
    buttonsLayout->addWidget(m_playPauseButton);
    buttonsLayout->addWidget(m_nextButton);
    buttonsLayout->addWidget(m_shuffleButton);
    buttonsLayout->addWidget(m_repeatButton);
    buttonsLayout->addSpacing(20);
    auto *blurLabel = new QLabel(tr("Blur"), m_controlsContainer);
    blurLabel->setStyleSheet("color: rgba(255, 255, 255, 210);");
    buttonsLayout->addWidget(blurLabel);
    buttonsLayout->addWidget(m_blurSlider);
    buttonsLayout->addSpacing(12);
    auto *volumeLabel = new QLabel(tr("Volume"), m_controlsContainer);
    volumeLabel->setStyleSheet("color: rgba(255, 255, 255, 210);");
    buttonsLayout->addWidget(volumeLabel);
    buttonsLayout->addWidget(m_volumeSlider);
    controlsLayout->addLayout(buttonsLayout);

    m_controlsOpacity = new QGraphicsOpacityEffect(m_controlsContainer);
    m_controlsOpacity->setOpacity(kInteractiveIdleOpacity);
    m_controlsContainer->setGraphicsEffect(m_controlsOpacity);

    m_controlsFadeAnimation = new QPropertyAnimation(m_controlsOpacity, "opacity", this);
    m_controlsFadeAnimation->setDuration(250);
    m_controlsFadeAnimation->setEasingCurve(QEasingCurve::InOutQuad);

    m_controlsFadeTimer.setParent(this);
    m_controlsFadeTimer.setInterval(kInteractiveFadeDelayMs);
    m_controlsFadeTimer.setSingleShot(true);
    connect(&m_controlsFadeTimer, &QTimer::timeout, this, [this]() {
        setWidgetOpacity(m_controlsOpacity, m_controlsFadeAnimation, kInteractiveIdleOpacity, 500);
    });

    registerInteractive(m_controlsContainer);
    registerInteractive(m_progressContainer);

    auto *overlayLayout = new QVBoxLayout(overlay);
    overlayLayout->setContentsMargins(32, 28, 32, 28);
    overlayLayout->setSpacing(20);
    overlayLayout->addSpacing(12);
    overlayLayout->addWidget(m_playlist, 1);
    overlayLayout->addSpacing(10);
    overlayLayout->addWidget(m_progressContainer);
    overlayLayout->addSpacing(16);
    overlayLayout->addWidget(m_controlsContainer);

    auto *central = new QWidget(this);
    setCentralWidget(central);

    auto *stackedLayout = new QStackedLayout(central);
    stackedLayout->addWidget(m_videoWidget);
    stackedLayout->addWidget(overlay);
    stackedLayout->setStackingMode(QStackedLayout::StackAll);
    stackedLayout->setCurrentWidget(overlay);
    overlay->raise();

    updateNowPlaying(QString());
    updateTransportAvailability();

    connect(m_addButton, &QPushButton::clicked, this, &MainWindow::handleAddMedia);
    connect(m_playPauseButton, &QToolButton::clicked, this, &MainWindow::handlePlayPause);
    connect(m_nextButton, &QToolButton::clicked, this, &MainWindow::handlePlayNext);
    connect(m_prevButton, &QToolButton::clicked, this, &MainWindow::handlePlayPrevious);
    connect(m_shuffleButton, &QToolButton::clicked, this, &MainWindow::handleShuffleToggled);
    connect(m_repeatButton, &QToolButton::clicked, this, &MainWindow::handleRepeatMode);
    connect(m_volumeSlider, &QSlider::valueChanged, this, &MainWindow::handleVolumeChanged);
    connect(m_blurSlider, &QSlider::valueChanged, this, &MainWindow::handleBlurChanged);
    connect(m_progressSlider, &QSlider::sliderPressed, this, &MainWindow::handleProgressSliderPressed);
    connect(m_progressSlider, &QSlider::sliderReleased, this, &MainWindow::handleProgressSliderReleased);
    connect(m_progressSlider, &QSlider::sliderMoved, this, &MainWindow::handleProgressSliderMoved);

    connect(m_videoWidget, &VideoBackgroundWidget::playbackStateChanged, this, &MainWindow::handlePlaybackStateChanged);
    connect(m_videoWidget, &VideoBackgroundWidget::playbackFinished, this, &MainWindow::handlePlaybackFinished);
    connect(m_videoWidget, &VideoBackgroundWidget::positionChanged, this, &MainWindow::handlePositionChanged);
    connect(m_videoWidget, &VideoBackgroundWidget::blurModeChanged, this, [this](bool shaderActive) {
        if (m_blurSlider) {
            m_blurSlider->setEnabled(shaderActive);
            m_blurSlider->setToolTip(shaderActive
                                          ? tr("Background blur")
                                          : tr("Blur handled by decoder"));
        }
    });

    m_playlistFadeTimer.start(kInteractiveFadeDelayMs);
    m_controlsFadeTimer.start(kInteractiveFadeDelayMs);
    m_progressFadeTimer.start(kInteractiveFadeDelayMs);
}

void MainWindow::loadSettings()
{
    const int volume = m_settings.value("audio/volume", kDefaultVolume).toInt();
    m_volumeSlider->setValue(volume);
    m_videoWidget->setVolume(volume);

    const bool shuffle = m_settings.value("playback/shuffle", false).toBool();
    m_shuffleEnabled = shuffle;
    m_shuffleButton->setChecked(m_shuffleEnabled);

    const int repeat = m_settings.value("playback/repeat", static_cast<int>(RepeatMode::None)).toInt();
    m_repeatMode = static_cast<RepeatMode>(repeat);
    updateRepeatButton();

    const int blur = m_settings.value("visual/blur", kDefaultBlur).toInt();
    if (m_blurSlider) {
        m_blurSlider->setValue(blur);
    }
    if (m_videoWidget) {
        m_videoWidget->setBlurAmount(static_cast<float>(blur) / 100.0f);
    }
    updateTransportAvailability();
}

void MainWindow::saveSettings()
{
    m_settings.setValue("audio/volume", m_volumeSlider->value());
    m_settings.setValue("playback/shuffle", m_shuffleEnabled);
    m_settings.setValue("playback/repeat", static_cast<int>(m_repeatMode));
    if (m_blurSlider) {
        m_settings.setValue("visual/blur", m_blurSlider->value());
    }
    m_settings.sync();
}

void MainWindow::addTrack(const QString &filePath)
{
    if (filePath.isEmpty()) {
        spdlog::warn("addTrack called with empty path");
        return;
    }

    const QString normalized = normalizedPathFor(filePath);
    if (normalized.isEmpty() || trackExists(normalized)) {
        spdlog::info("Skipping track '{}' (normalized='{}') - already in playlist", filePath, normalized);
        return;
    }

    TrackEntry entry{displayNameForFile(filePath), filePath, normalized};
    m_tracks.append(entry);
    auto *item = new QListWidgetItem(entry.title);
    item->setData(Qt::UserRole, entry.filePath);
    item->setToolTip(entry.title);
    item->setSizeHint(QSize(0, 42));
    m_playlist->addItem(item);
    m_knownPaths.insert(normalized);
    spdlog::info("Track registered title='{}' normalized='{}'", entry.title, normalized);
    updateTransportAvailability();
}

void MainWindow::processSelectedFiles(const QStringList &files)
{
    QVector<QString> newTracks;
    newTracks.reserve(files.size());
    for (const QString &file : files) {
        const QString normalized = normalizedPathFor(file);
        if (normalized.isEmpty() || m_knownPaths.contains(normalized)) {
            spdlog::info("Skipping duplicate {} (normalized={})", file, normalized);
            continue;
        }
        spdlog::debug("Candidate track '{}' normalized '{}'", file, normalized);
        newTracks.append(file);
    }

    if (newTracks.isEmpty()) {
        spdlog::info("No new tracks to add");
        return;
    }

    spdlog::info("Adding {} tracks", newTracks.size());
    QElapsedTimer timer;
    for (const QString &file : newTracks) {
        timer.start();
        addTrack(file);
        spdlog::info("Queued '{}' in {} ms", file, timer.elapsed());
    }

    if (m_currentIndex == -1 && !m_tracks.isEmpty()) {
        playTrack(0);
    }
}

void MainWindow::playTrack(int index)
{
    if (index < 0 || index >= m_tracks.size()) {
        spdlog::warn("playTrack out of bounds index={}", index);
        return;
    }

    const TrackEntry &entry = m_tracks.at(index);
    if (!m_videoWidget->loadFile(entry.filePath)) {
        spdlog::error("Failed to load track '{}'", entry.filePath);
        return;
    }

    m_currentIndex = index;
    m_playlist->setCurrentRow(index);
    updateNowPlaying(entry.filePath);
    updateTransportAvailability();
}

int MainWindow::resolveNextIndex() const
{
    if (m_tracks.isEmpty()) {
        return -1;
    }

    if (m_repeatMode == RepeatMode::One && m_currentIndex >= 0) {
        return m_currentIndex;
    }

    if (m_shuffleEnabled && m_tracks.size() > 1) {
        int next = m_currentIndex;
        while (next == m_currentIndex) {
            next = static_cast<int>(QRandomGenerator::global()->bounded(m_tracks.size()));
        }
        return next;
    }

    int next = m_currentIndex + 1;
    if (next >= m_tracks.size()) {
        if (m_repeatMode == RepeatMode::All) {
            next = 0;
        } else {
            next = -1;
        }
    }
    return next;
}

int MainWindow::resolvePreviousIndex() const
{
    if (m_tracks.isEmpty()) {
        return -1;
    }

    if (m_repeatMode == RepeatMode::One && m_currentIndex >= 0) {
        return m_currentIndex;
    }

    if (m_shuffleEnabled && m_tracks.size() > 1) {
        int prev = m_currentIndex;
        while (prev == m_currentIndex) {
            prev = static_cast<int>(QRandomGenerator::global()->bounded(m_tracks.size()));
        }
        return prev;
    }

    int prev = m_currentIndex - 1;
    if (prev < 0) {
        if (m_repeatMode == RepeatMode::All) {
            prev = m_tracks.isEmpty() ? -1 : m_tracks.size() - 1;
        } else {
            prev = -1;
        }
    }
    return prev;
}

bool MainWindow::trackExists(const QString &filePath) const
{
    return m_knownPaths.contains(filePath);
}

void MainWindow::updatePlayPauseButton(bool playing)
{
    m_playPauseButton->setIcon(style()->standardIcon(playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
}

void MainWindow::updateRepeatButton()
{
    switch (m_repeatMode) {
    case RepeatMode::None:
        m_repeatButton->setText(tr("Repeat: Off"));
        break;
    case RepeatMode::All:
        m_repeatButton->setText(tr("Repeat: All"));
        break;
    case RepeatMode::One:
        m_repeatButton->setText(tr("Repeat: One"));
        break;
    }
}

void MainWindow::updateTransportAvailability()
{
    const bool hasTracks = !m_tracks.isEmpty();
    const bool hasMedia = m_videoWidget && m_videoWidget->hasMedia();
    const bool canNavigate = hasTracks && m_tracks.size() > 1;

    if (m_playPauseButton) {
        m_playPauseButton->setEnabled(hasTracks);
    }
    if (m_prevButton) {
        m_prevButton->setEnabled(canNavigate);
    }
    if (m_nextButton) {
        m_nextButton->setEnabled(canNavigate);
    }
    if (m_shuffleButton) {
        m_shuffleButton->setEnabled(hasTracks);
    }
    if (m_repeatButton) {
        m_repeatButton->setEnabled(hasTracks);
    }

    if (!hasMedia) {
        updatePlayPauseButton(false);
    }
}

void MainWindow::setWidgetOpacity(QGraphicsOpacityEffect *effect, QPropertyAnimation *animation, qreal value, int durationMs)
{
    if (!effect) {
        return;
    }

    value = std::clamp(value, 0.0, 1.0);

    if (!animation || durationMs <= 0) {
        if (animation) {
            animation->stop();
        }
        effect->setOpacity(value);
        return;
    }

    if (std::fabs(effect->opacity() - value) < 0.01) {
        return;
    }

    animation->stop();
    animation->setDuration(durationMs);
    animation->setStartValue(effect->opacity());
    animation->setEndValue(value);
    animation->start();
}

void MainWindow::updateNowPlaying(const QString &filePath)
{
    if (!m_titleLabel) {
        return;
    }

    if (filePath.isEmpty()) {
        m_titleLabel->setText(tr("Select a track to play"));
        m_titleLabel->setToolTip(QString());
        m_lastDuration = 0.0;
        m_progressSliderPressed = false;
        if (m_progressSlider) {
            const QSignalBlocker blocker(m_progressSlider);
            m_progressSlider->setValue(0);
            m_progressSlider->setEnabled(false);
        }
        if (m_timeLabel) {
            m_timeLabel->setText(QStringLiteral("00:00 / 00:00"));
        }
        updateTransportAvailability();
        return;
    }

    const QString title = displayNameForFile(filePath);
    m_titleLabel->setText(tr("Now Playing: %1").arg(title));
    m_titleLabel->setToolTip(title);
    m_lastDuration = 0.0;
    m_progressSliderPressed = false;
    if (m_progressSlider) {
        const QSignalBlocker blocker(m_progressSlider);
        m_progressSlider->setValue(0);
        m_progressSlider->setEnabled(false);
    }
    if (m_timeLabel) {
        m_timeLabel->setText(QStringLiteral("00:00 / 00:00"));
    }
    updateTransportAvailability();
}

bool MainWindow::handleFadeEvent(QObject *watched,
                                 QEvent *event,
                                 QWidget *container,
                                 QTimer &timer,
                                 QGraphicsOpacityEffect *effect,
                                 QPropertyAnimation *animation)
{
    if (!container || !effect || !animation) {
        return false;
    }

    QWidget *widget = qobject_cast<QWidget *>(watched);
    if (!widget) {
        return false;
    }

    if (widget != container && !container->isAncestorOf(widget)) {
        return false;
    }

    switch (event->type()) {
    case QEvent::Enter:
    case QEvent::HoverEnter:
    case QEvent::FocusIn:
    case QEvent::HoverMove:
    case QEvent::MouseMove:
    case QEvent::MouseButtonPress:
        timer.stop();
        setWidgetOpacity(effect, animation, kInteractiveActiveOpacity, 180);
        break;
    case QEvent::Leave:
    case QEvent::HoverLeave:
    case QEvent::FocusOut:
    case QEvent::MouseButtonRelease:
        if (isCursorInside(container)) {
            timer.stop();
            break;
        }
        timer.start(kInteractiveFadeDelayMs);
        break;
    default:
        break;
    }

    return false;
}

bool MainWindow::isCursorInside(QWidget *widget) const
{
    if (!widget) {
        return false;
    }

    const QPoint localPos = widget->mapFromGlobal(QCursor::pos());
    return widget->rect().contains(localPos);
}

QString MainWindow::normalizedPathFor(const QString &filePath) const
{
    QFileInfo info(filePath);
    const QString absolute = info.absoluteFilePath();
    return QDir::cleanPath(absolute);
}
    QElapsedTimer timer;
