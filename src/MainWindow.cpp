#include "MainWindow.h"

#include "VideoBackgroundWidget.h"

#include <QAbstractItemView>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QCursor>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QEasingCurve>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPainter>
#include <QApplication>
#include <QStyledItemDelegate>
#include <QProcess>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRandomGenerator>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSize>
#include <QSizePolicy>
#include <QSlider>
#include <QStackedLayout>
#include <QStandardPaths>
#include <QStyle>
#include <QVariant>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrent/QtConcurrentRun>
#include <memory>

extern "C" {
#include <mpv/client.h>
}
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
constexpr int kCompactControlsThresholdPx = 960;

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

double probeDurationWithMpv(const QString &path)
{
    if (path.isEmpty()) {
        return -1.0;
    }

    QFileInfo info(path);
    if (!info.exists()) {
        return -1.0;
    }

    struct MpvHandleDeleter
    {
        void operator()(mpv_handle *handle) const
        {
            if (handle) {
                mpv_terminate_destroy(handle);
            }
        }
    };

    std::unique_ptr<mpv_handle, MpvHandleDeleter> handle(mpv_create());
    if (!handle) {
        return -1.0;
    }

    mpv_set_option_string(handle.get(), "terminal", "no");
    mpv_set_option_string(handle.get(), "msg-level", "all=no");
    mpv_set_option_string(handle.get(), "vid", "no");
    mpv_set_option_string(handle.get(), "audio", "no");
    mpv_set_option_string(handle.get(), "vo", "null");
    mpv_set_option_string(handle.get(), "ao", "null");
    mpv_set_option_string(handle.get(), "cache", "no");
    mpv_set_option_string(handle.get(), "keep-open", "no");
    mpv_set_option_string(handle.get(), "pause", "yes");

    if (mpv_initialize(handle.get()) < 0) {
        return -1.0;
    }

    QByteArray encoded = QFile::encodeName(path);
    const char *loadCmd[] = {"loadfile", encoded.constData(), nullptr};
    if (mpv_command(handle.get(), loadCmd) < 0) {
        return -1.0;
    }

    double duration = -1.0;

    while (true) {
        mpv_event *event = mpv_wait_event(handle.get(), 5.0);
        if (!event) {
            break;
        }

        if (event->event_id == MPV_EVENT_FILE_LOADED) {
            double value = 0.0;
            if (mpv_get_property(handle.get(), "duration", MPV_FORMAT_DOUBLE, &value) >= 0 && std::isfinite(value) && value > 0.0) {
                duration = value;
            }
        }

        if (event->event_id == MPV_EVENT_END_FILE || event->event_id == MPV_EVENT_FILE_LOADED || event->event_id == MPV_EVENT_SHUTDOWN) {
            break;
        }
    }

    return duration;
}

class PlaylistItemDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        if (!painter) {
            return;
        }

        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        QString durationText = index.data(Qt::UserRole + 1).toString();
        const QWidget *widget = option.widget;
        QStyle *style = widget ? widget->style() : QApplication::style();

        opt.text.clear();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);

        painter->save();
        painter->setFont(opt.font);

        QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, widget);
        const QPalette::ColorRole textRole = (opt.state & QStyle::State_Selected) ? QPalette::HighlightedText : QPalette::Text;
        const QColor textColor = opt.palette.color(textRole);
        painter->setPen(textColor);

        QFontMetrics metrics(opt.font);
        const int spacing = 14;

        if (!durationText.isEmpty()) {
            const int durationWidth = metrics.horizontalAdvance(durationText);
            QRect durationRect = textRect;
            durationRect.setLeft(durationRect.right() - durationWidth);
            painter->drawText(durationRect, Qt::AlignRight | Qt::AlignVCenter, durationText);
            textRect.setRight(durationRect.left() - spacing);
        }

        if (textRect.width() < 0) {
            textRect.setWidth(0);
        }

        const QString title = index.data(Qt::DisplayRole).toString();
        const QString elided = metrics.elidedText(title, Qt::ElideRight, textRect.width());
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, elided);

        painter->restore();
    }
};
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_settings("evoid", "Drift Player")
{
    resize(980, 640);
    setMinimumSize(880, 480);
    setupUi();

    m_playlistWatcher = new QFileSystemWatcher(this);
    connect(m_playlistWatcher, &QFileSystemWatcher::fileChanged, this, &MainWindow::handleWatchedFileChanged);

    m_durationFutureWatcher = new QFutureWatcher<double>(this);
    connect(m_durationFutureWatcher, &QFutureWatcher<double>::finished, this, [this]() {
        const QString path = m_durationProbeCurrent;
        const double seconds = m_durationFutureWatcher->result();

        if (!path.isEmpty()) {
            if (seconds > 0.5) {
                finalizeDurationFor(path, seconds);
            } else {
                spdlog::warn("Duration probe returned {} for '{}'", seconds, path.toStdString());
            }
        }

        m_durationProbeCurrent.clear();
        QMetaObject::invokeMethod(this, &MainWindow::processDurationQueue, Qt::QueuedConnection);
    });

    loadSettings();
    updatePlayPauseButton(false);
}

MainWindow::~MainWindow()
{
    cleanupAddDialogProcess();
    saveSettings();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();
    event->accept();
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    updateControlsLayoutMode(event->size().width() < kCompactControlsThresholdPx);
}

void MainWindow::handleAddMedia()
{
    if (m_addDialogOpen || m_addDialogProcess) {
        spdlog::info("Add Media dialog already active; ignoring duplicate request");
        return;
    }

    const QString helperPath = resolveDialogHelperPath();
    if (helperPath.isEmpty()) {
        spdlog::error("Add Media helper executable not found");
        return;
    }

    const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (tempDir.isEmpty()) {
        spdlog::error("Unable to determine writable temporary directory");
        return;
    }

    QDir dir(tempDir);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        spdlog::error("Unable to create temporary directory {}", tempDir.toStdString());
        return;
    }

    m_addDialogTempFile = dir.filePath(QStringLiteral("drift_player_add_%1.txt")
                                           .arg(QDateTime::currentMSecsSinceEpoch()));
    QFile::remove(m_addDialogTempFile);

    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, &MainWindow::handleAddDialogHelperFinished);
    connect(process, &QProcess::errorOccurred, this, &MainWindow::handleAddDialogHelperError);

    QStringList arguments;
    arguments << m_addDialogTempFile;

    process->setProgram(helperPath);
    process->setArguments(arguments);

    spdlog::info("Launching Add Media helper {} -> {}", helperPath.toStdString(), m_addDialogTempFile.toStdString());
    m_addDialogOpen = true;
    m_addDialogProcess = process;
    m_addDialogProcess->start();
}

void MainWindow::handleAddDialogFiles(const QStringList &files)
{
    spdlog::info("Add Media selection received: {} entries", files.size());
    processSelectedFiles(files);
}

void MainWindow::handleAddDialogClosed(int result)
{
    spdlog::info("Add Media dialog closed result={}", result);
    m_addDialogOpen = false;
}

void MainWindow::handleAddDialogHelperFinished(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(status);

    if (!m_addDialogProcess || sender() != m_addDialogProcess) {
        return;
    }

    const QString tempPath = m_addDialogTempFile;
    QStringList files;

    if (!tempPath.isEmpty()) {
        QFile file(tempPath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream stream(&file);
            while (!stream.atEnd()) {
                const QString line = stream.readLine().trimmed();
                if (!line.isEmpty()) {
                    files.append(line);
                }
            }
        } else {
            spdlog::warn("Add Media helper unable to open output file '{}'", tempPath.toStdString());
        }
    }

    if (!tempPath.isEmpty()) {
        QFile::remove(tempPath);
    }
    m_addDialogTempFile.clear();

    if (!files.isEmpty()) {
        handleAddDialogFiles(files);
        handleAddDialogClosed(QDialog::Accepted);
    } else {
        handleAddDialogClosed((exitCode == 0) ? QDialog::Rejected : QDialog::Rejected);
    }

    cleanupAddDialogProcess();
}

void MainWindow::handleAddDialogHelperError(QProcess::ProcessError error)
{
    if (!m_addDialogProcess || sender() != m_addDialogProcess) {
        return;
    }

    spdlog::error("Add Media helper process error {}", static_cast<int>(error));
    handleAddDialogClosed(QDialog::Rejected);
    cleanupAddDialogProcess();
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
    spdlog::info("Playback finished. Current index={} next index={}", m_currentIndex, nextIndex);
    if (nextIndex != -1) {
        QMetaObject::invokeMethod(this, [this, nextIndex]() {
            spdlog::info("Advancing to next index {}", nextIndex);
            playTrack(nextIndex);
        }, Qt::QueuedConnection);
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

    if (m_currentIndex >= 0 && m_currentIndex < m_tracks.size() && duration > 0.0) {
        TrackEntry &entry = m_tracks[m_currentIndex];
        if (std::fabs(entry.durationSeconds - duration) > 0.5) {
            entry.durationSeconds = duration;
            updatePlaylistItem(m_currentIndex);
            if (!m_isRestoringPlaylist) {
                savePlaylistState();
            }
        }
    }
}

void MainWindow::handleWatchedFileChanged(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }

    QFileInfo info(path);
    if (info.exists()) {
        // ensure the watcher remains active after certain file operations
        startWatchingTrack(path);
        return;
    }

    spdlog::info("Detected missing media file '{}'; removing from playlist", path);
    removeTrackByNormalizedPath(path);
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
    m_playlist->setItemDelegate(new PlaylistItemDelegate(m_playlist));
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
        "    border-radius: 999px;\n"
        "    padding: 0;\n"
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
    m_addButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    const int addButtonTextWidth = m_addButton->fontMetrics().horizontalAdvance(m_addButton->text());
    m_addButton->setMinimumWidth(addButtonTextWidth + 36);

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
    m_prevButton->setFixedSize(44, 44);
    m_nextButton->setFixedSize(44, 44);
    m_playPauseButton->setFixedSize(52, 52);

    m_prevButton->setStyleSheet(iconButtonStyle);
    m_nextButton->setStyleSheet(iconButtonStyle);
    m_playPauseButton->setStyleSheet(iconButtonStyle);

    m_addButton->setStyleSheet(textButtonStyle);
    m_shuffleButton->setStyleSheet(textButtonStyle);
    m_repeatButton->setStyleSheet(textButtonStyle);
    m_shuffleButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    m_repeatButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    m_shuffleButton->setMinimumWidth(96);
    m_repeatButton->setMinimumWidth(124);

    m_blurSlider = new QSlider(Qt::Horizontal, m_controlsContainer);
    m_blurSlider->setRange(0, 100);
    m_blurSlider->setValue(kDefaultBlur);
    m_blurSlider->setCursor(Qt::PointingHandCursor);
    m_blurSlider->setToolTip(tr("Background blur"));
    m_blurSlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_blurSlider->setMinimumWidth(110);
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
    m_volumeSlider->setToolTip(tr("Volume"));
    m_volumeSlider->setCursor(Qt::PointingHandCursor);
    m_volumeSlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_volumeSlider->setMinimumWidth(120);
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
    buttonsLayout->setSpacing(16);

    m_actionsLayout = new QHBoxLayout;
    m_actionsLayout->setContentsMargins(0, 0, 0, 0);
    m_actionsLayout->setSpacing(18);
    m_actionsLayout->addWidget(m_addButton);
    m_actionsLayout->addSpacing(12);
    m_actionsLayout->addWidget(m_prevButton);
    m_actionsLayout->addWidget(m_playPauseButton);
    m_actionsLayout->addWidget(m_nextButton);
    m_actionsLayout->addWidget(m_shuffleButton);
    m_actionsLayout->addWidget(m_repeatButton);

    m_blurLabel = new QLabel(tr("Blur"), m_controlsContainer);
    m_blurLabel->setStyleSheet("color: rgba(255, 255, 255, 210);");

    m_volumeLabel = new QLabel(tr("Volume"), m_controlsContainer);
    m_volumeLabel->setStyleSheet("color: rgba(255, 255, 255, 210);");

    m_slidersLayout = new QHBoxLayout;
    m_slidersLayout->setContentsMargins(0, 0, 0, 0);
    m_slidersLayout->setSpacing(12);
    m_slidersLayout->addWidget(m_blurLabel);
    m_slidersLayout->addWidget(m_blurSlider);
    m_slidersLayout->addSpacing(8);
    m_slidersLayout->addWidget(m_volumeLabel);
    m_slidersLayout->addWidget(m_volumeSlider);
    m_slidersLayout->setAlignment(m_blurLabel, Qt::AlignVCenter);
    m_slidersLayout->setAlignment(m_volumeLabel, Qt::AlignVCenter);
    m_slidersLayout->setStretch(1, 1);
    m_slidersLayout->setStretch(3, 1);

    buttonsLayout->addLayout(m_actionsLayout);
    buttonsLayout->addSpacing(18);
    buttonsLayout->addStretch(1);
    buttonsLayout->addLayout(m_slidersLayout, 1);
    controlsLayout->addLayout(buttonsLayout);

    updateControlsLayoutMode();

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

    restorePlaylistState();
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
    savePlaylistState();
    m_settings.sync();
}

bool MainWindow::addTrack(const QString &filePath)
{
    if (filePath.isEmpty()) {
        spdlog::warn("addTrack called with empty path");
        return false;
    }

    const QString normalized = normalizedPathFor(filePath);
    if (normalized.isEmpty() || trackExists(normalized)) {
        spdlog::info("Skipping track '{}' (normalized='{}') - already in playlist", filePath, normalized);
        return false;
    }

    QFileInfo info(normalized);
    if (!info.exists()) {
        spdlog::warn("Skipping '{}' because file is missing", normalized);
        return false;
    }

    TrackEntry entry{displayNameForFile(filePath), filePath, normalized};
    m_tracks.append(entry);
    const int index = m_tracks.size() - 1;

    auto *item = new QListWidgetItem(entry.title);
    item->setSizeHint(QSize(0, 42));
    m_playlist->addItem(item);
    m_knownPaths.insert(normalized);
    spdlog::info("Track registered title='{}' normalized='{}'", entry.title, normalized);

    updatePlaylistItem(index);

    startWatchingTrack(normalized);

    if (!m_isRestoringPlaylist) {
        savePlaylistState();
    }

    updateTransportAvailability();
    return true;
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
        if (addTrack(file)) {
            spdlog::info("Queued '{}' in {} ms", file, timer.elapsed());
            const QString normalized = normalizedPathFor(file);
            enqueueDurationProbe(normalized);
        }
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

void MainWindow::startWatchingTrack(const QString &normalizedPath)
{
    if (!m_playlistWatcher || normalizedPath.isEmpty() || m_watchedPaths.contains(normalizedPath)) {
        return;
    }

    if (!QFileInfo::exists(normalizedPath)) {
        return;
    }

    if (m_playlistWatcher->addPath(normalizedPath)) {
        m_watchedPaths.insert(normalizedPath);
    }
}

void MainWindow::stopWatchingTrack(const QString &normalizedPath)
{
    if (!m_playlistWatcher || normalizedPath.isEmpty() || !m_watchedPaths.contains(normalizedPath)) {
        return;
    }

    m_playlistWatcher->removePath(normalizedPath);
    m_watchedPaths.remove(normalizedPath);
}

void MainWindow::removeTrackByNormalizedPath(const QString &normalizedPath)
{
    for (int i = 0; i < m_tracks.size(); ++i) {
        if (m_tracks.at(i).normalizedPath == normalizedPath) {
            removeTrackAt(i);
            break;
        }
    }
}

void MainWindow::removeTrackAt(int index)
{
    if (index < 0 || index >= m_tracks.size()) {
        return;
    }

    const TrackEntry entry = m_tracks.at(index);
    spdlog::info("Removing track '{}' (normalized='{}')", entry.filePath, entry.normalizedPath);

    stopWatchingTrack(entry.normalizedPath);
    m_knownPaths.remove(entry.normalizedPath);
    m_tracks.removeAt(index);

    m_durationProbeQueue.removeAll(entry.normalizedPath);
    if (!m_durationProbeCurrent.isEmpty() && m_durationProbeCurrent == entry.normalizedPath) {
        m_durationProbeCurrent.clear();
    }

    QMetaObject::invokeMethod(this, &MainWindow::processDurationQueue, Qt::QueuedConnection);

    if (m_playlist) {
        if (QListWidgetItem *item = m_playlist->takeItem(index)) {
            delete item;
        }
    }

    if (m_currentIndex == index) {
        m_currentIndex = -1;
        updateNowPlaying(QString());
        if (m_videoWidget) {
            m_videoWidget->pause();
        }
        updatePlayPauseButton(false);
        if (m_playlist) {
            m_playlist->setCurrentRow(-1);
        }
    } else if (m_currentIndex > index) {
        --m_currentIndex;
        if (m_playlist && m_currentIndex >= 0 && m_currentIndex < m_tracks.size()) {
            m_playlist->setCurrentRow(m_currentIndex);
        }
    }

    savePlaylistState();
    updateTransportAvailability();
}

void MainWindow::restorePlaylistState()
{
    const QStringList stored = m_settings.value("playlist/paths").toStringList();
    if (stored.isEmpty()) {
        return;
    }

    const QVariantList storedDurations = m_settings.value("playlist/durations").toList();

    m_isRestoringPlaylist = true;
    for (int i = 0; i < stored.size(); ++i) {
        const QString &path = stored.at(i);
        if (path.isEmpty()) {
            continue;
        }

        if (!QFileInfo::exists(path)) {
            spdlog::info("Skipping missing playlist entry '{}' during restore", path);
            continue;
        }

        if (addTrack(path)) {
            if (i < storedDurations.size()) {
                double storedDuration = storedDurations.at(i).toDouble();
                if (storedDuration > 0.5 && !m_tracks.isEmpty()) {
                    m_tracks.last().durationSeconds = storedDuration;
                    updatePlaylistItem(m_tracks.size() - 1);
                } else {
                    enqueueDurationProbe(path);
                }
            } else {
                enqueueDurationProbe(path);
            }
        }
    }
    m_isRestoringPlaylist = false;

    savePlaylistState();
}

void MainWindow::savePlaylistState()
{
    QStringList paths;
    paths.reserve(m_tracks.size());
    for (const TrackEntry &entry : m_tracks) {
        paths.append(entry.normalizedPath);
    }

    m_settings.setValue("playlist/paths", paths);

    QVariantList durations;
    durations.reserve(m_tracks.size());
    for (const TrackEntry &entry : m_tracks) {
        durations.append(entry.durationSeconds);
    }
    m_settings.setValue("playlist/durations", durations);
}

void MainWindow::enqueueDurationProbe(const QString &normalizedPath)
{
    if (!m_durationFutureWatcher || normalizedPath.isEmpty()) {
        return;
    }

    auto trackIt = std::find_if(m_tracks.cbegin(), m_tracks.cend(), [&normalizedPath](const TrackEntry &entry) {
        return entry.normalizedPath == normalizedPath;
    });

    if (trackIt == m_tracks.cend()) {
        return;
    }

    if (trackIt->durationSeconds > 0.5) {
        return;
    }

    if (m_durationProbeCurrent == normalizedPath || m_durationProbeQueue.contains(normalizedPath)) {
        return;
    }

    m_durationProbeQueue.enqueue(normalizedPath);
    if (m_durationProbeCurrent.isEmpty()) {
        QMetaObject::invokeMethod(this, &MainWindow::processDurationQueue, Qt::QueuedConnection);
    }
}

void MainWindow::processDurationQueue()
{
    if (!m_durationFutureWatcher || m_durationFutureWatcher->isRunning() || !m_durationProbeCurrent.isEmpty()) {
        return;
    }

    while (!m_durationProbeQueue.isEmpty()) {
        const QString path = m_durationProbeQueue.dequeue();

        auto trackIt = std::find_if(m_tracks.cbegin(), m_tracks.cend(), [&path](const TrackEntry &entry) {
            return entry.normalizedPath == path;
        });

        if (trackIt == m_tracks.cend()) {
            continue;
        }

        if (trackIt->durationSeconds > 0.5) {
            continue;
        }

        if (!QFileInfo::exists(path)) {
            continue;
        }

        m_durationProbeCurrent = path;
        m_durationFutureWatcher->setFuture(QtConcurrent::run(&probeDurationWithMpv, path));
        break;
    }
}

void MainWindow::finalizeDurationFor(const QString &normalizedPath, double seconds)
{
    if (normalizedPath.isEmpty() || seconds <= 0.5) {
        return;
    }

    bool updated = false;
    for (int i = 0; i < m_tracks.size(); ++i) {
        if (m_tracks.at(i).normalizedPath == normalizedPath) {
            if (std::fabs(m_tracks[i].durationSeconds - seconds) > 0.5) {
                m_tracks[i].durationSeconds = seconds;
                updatePlaylistItem(i);
                updated = true;
            }
        }
    }

    if (updated && !m_isRestoringPlaylist) {
        savePlaylistState();
    }
}

void MainWindow::updatePlaylistItem(int index)
{
    if (!m_playlist || index < 0 || index >= m_tracks.size()) {
        return;
    }

    QListWidgetItem *item = m_playlist->item(index);
    if (!item) {
        return;
    }

    const TrackEntry &entry = m_tracks.at(index);
    item->setText(entry.title);
    item->setData(Qt::UserRole, entry.filePath);
    item->setToolTip(QStringLiteral("%1\n%2").arg(entry.title, entry.filePath));

    QString durationText;
    if (entry.durationSeconds > 0.5) {
        durationText = formatTime(entry.durationSeconds);
    }
    item->setData(Qt::UserRole + 1, durationText);
}

void MainWindow::updatePlayPauseButton(bool playing)
{
    m_playPauseButton->setIcon(style()->standardIcon(playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
}

void MainWindow::updateRepeatButton()
{
    if (!m_repeatButton) {
        return;
    }

    QString buttonText;
    QString tooltipText;

    switch (m_repeatMode) {
    case RepeatMode::None:
        tooltipText = tr("Repeat: Off");
        buttonText = m_compactControls ? tr("Rpt Off") : tooltipText;
        break;
    case RepeatMode::All:
        tooltipText = tr("Repeat: All");
        buttonText = m_compactControls ? tr("Rpt All") : tooltipText;
        break;
    case RepeatMode::One:
        tooltipText = tr("Repeat: One");
        buttonText = m_compactControls ? tr("Rpt One") : tooltipText;
        break;
    }

    m_repeatButton->setText(buttonText);
    m_repeatButton->setToolTip(tooltipText);
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

void MainWindow::updateControlsLayoutMode()
{
    updateControlsLayoutMode(width() < kCompactControlsThresholdPx);
}

void MainWindow::updateControlsLayoutMode(bool compact)
{
    m_compactControls = compact;

    if (m_actionsLayout) {
        m_actionsLayout->setSpacing(compact ? 10 : 18);
    }

    if (m_shuffleButton) {
        m_shuffleButton->setText(compact ? tr("Shfl") : tr("Shuffle"));
        m_shuffleButton->setMinimumWidth(compact ? 74 : 96);
        m_shuffleButton->setToolTip(tr("Shuffle playback"));
    }

    if (m_repeatButton) {
        m_repeatButton->setMinimumWidth(compact ? 108 : 124);
    }

    if (m_slidersLayout) {
        m_slidersLayout->setSpacing(compact ? 8 : 12);
        m_slidersLayout->setContentsMargins(compact ? 12 : 0, 0, 0, 0);
    }

    if (m_blurLabel) {
        m_blurLabel->setVisible(!compact);
    }

    if (m_blurSlider) {
        m_blurSlider->setMinimumWidth(compact ? 90 : 110);
    }

    if (m_volumeLabel) {
        m_volumeLabel->setText(compact ? tr("Vol") : tr("Volume"));
    }

    if (m_volumeSlider) {
        m_volumeSlider->setMinimumWidth(compact ? 110 : 120);
    }

    updateRepeatButton();
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

QString MainWindow::resolveDialogHelperPath() const
{
    QString helper = QCoreApplication::applicationDirPath();
    if (helper.isEmpty()) {
        return QString();
    }

    helper += QStringLiteral("/drift_dialog_helper");
#ifdef Q_OS_WIN
    helper += QStringLiteral(".exe");
#endif

    QFileInfo info(helper);
    if (!info.exists() || !info.isExecutable()) {
        spdlog::error("Add Media helper not executable at '{}'", helper.toStdString());
        return QString();
    }

    return helper;
}

void MainWindow::cleanupAddDialogProcess()
{
    if (m_addDialogProcess) {
        if (m_addDialogProcess->state() != QProcess::NotRunning) {
            m_addDialogProcess->kill();
            m_addDialogProcess->waitForFinished(2000);
        }
        m_addDialogProcess->deleteLater();
        m_addDialogProcess = nullptr;
    }

    if (!m_addDialogTempFile.isEmpty()) {
        QFile::remove(m_addDialogTempFile);
        m_addDialogTempFile.clear();
    }

    m_addDialogOpen = false;
}
    QElapsedTimer timer;
