#include "MainWindow.h"

#include "VideoBackgroundWidget.h"
#include "BassVisualizerWidget.h"

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
#include <QItemSelectionModel>
#include <QItemSelection>
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
#include <QMenu>
#include <QMenuBar>
#include <QAction>
#include <QMessageBox>
#include <QGridLayout>
#include <QVariant>
#include <QGraphicsDropShadowEffect>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QColor>
#include <QKeySequence>
#include <QInputDialog>
#include <QLineEdit>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariant>
#include <QVariantList>
#include <QtConcurrent/QtConcurrentRun>
#include <memory>
#include <string>

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
constexpr int kDefaultBassThreshold = 55;
constexpr bool kDefaultDebugBass = false;
constexpr qreal kInteractiveActiveOpacity = 1.0;
constexpr qreal kInteractiveIdleOpacity = 0.55;
constexpr int kInteractiveFadeDelayMs = 2000;
constexpr int kCompactControlsThresholdPx = 960;

QString displayNameForFile(const QString &filePath)
{
    const QUrl url = QUrl::fromUserInput(filePath);
    if (url.isValid() && !url.isRelative() && !url.isLocalFile() && !url.scheme().isEmpty()) {
        QString base = url.fileName();
        if (base.isEmpty()) {
            base = url.path();
            if (base.endsWith('/')) {
                base.chop(1);
            }
            const int lastSlash = base.lastIndexOf('/');
            if (lastSlash != -1) {
                base = base.mid(lastSlash + 1);
            }
        }
        if (base.isEmpty()) {
            base = url.host();
        }
        if (base.isEmpty()) {
            base = url.toString(QUrl::RemoveUserInfo);
        }
        base.replace(QChar('_'), QChar(' '));
        base.replace(QChar('-'), QChar(' '));
        return base.simplified();
    }

    QFileInfo info(filePath);
    QString base = info.completeBaseName();
    if (base.isEmpty()) {
        base = info.fileName();
    }
    base.replace(QChar('_'), QChar(' '));
    base.replace(QChar('-'), QChar(' '));
    return base.simplified();
}

bool isRemoteLocation(const QString &path)
{
    const QUrl url = QUrl::fromUserInput(path);
    return url.isValid() && !url.isRelative() && !url.isLocalFile() && !url.scheme().isEmpty();
}

QString platformLabelForUrl(const QUrl &url)
{
    if (!url.isValid()) {
        return QStringLiteral("Online");
    }

    QString host = url.host().toLower();
    if (host.isEmpty()) {
        return QStringLiteral("Online");
    }

    auto matches = [&](const char *needle) {
        return host.contains(QLatin1String(needle));
    };

    if (matches("youtube") || matches("youtu.be")) {
        return QStringLiteral("YouTube");
    }
    if (matches("vimeo")) {
        return QStringLiteral("Vimeo");
    }
    if (matches("soundcloud")) {
        return QStringLiteral("SoundCloud");
    }
    if (matches("twitch")) {
        return QStringLiteral("Twitch");
    }
    if (matches("bandcamp")) {
        return QStringLiteral("Bandcamp");
    }
    if (matches("mixcloud")) {
        return QStringLiteral("Mixcloud");
    }

    QString trimmed = host;
    if (trimmed.startsWith(QStringLiteral("www."))) {
        trimmed.remove(0, 4);
    }
    const QStringList segments = trimmed.split('.');
    QString base;
    if (!segments.isEmpty()) {
        base = segments.first();
        if (base == QLatin1String("www") && segments.size() > 1) {
            base = segments.at(1);
        }
        if (base == QLatin1String("m") && segments.size() > 1) {
            base = segments.at(1);
        }
    }
    if (base.isEmpty()) {
        base = trimmed;
    }

    QString label = base.replace('-', ' ');
    if (!label.isEmpty()) {
        label[0] = label[0].toUpper();
        for (int i = 1; i < label.size(); ++i) {
            if (label[i - 1] == ' ') {
                label[i] = label[i].toUpper();
            }
        }
    }
    if (label.isEmpty()) {
        label = QStringLiteral("Online");
    }
    return label;
}

QString friendlyPlatformName(const QString &raw)
{
    if (raw.isEmpty()) {
        return QString();
    }

    const QString lower = raw.toLower();
    if (lower == QStringLiteral("youtube")) {
        return QStringLiteral("YouTube");
    }
    if (lower == QStringLiteral("soundcloud")) {
        return QStringLiteral("SoundCloud");
    }
    if (lower == QStringLiteral("twitch")) {
        return QStringLiteral("Twitch");
    }
    if (lower == QStringLiteral("vimeo")) {
        return QStringLiteral("Vimeo");
    }

    QString text = raw;
    text.replace('_', ' ');
    text.replace('-', ' ');
    text = text.trimmed().toLower();
    QString result;
    bool capitalize = true;
    for (QChar ch : text) {
        if (capitalize) {
            result.append(ch.toUpper());
        } else {
            result.append(ch);
        }
        capitalize = ch.isSpace();
    }
    return result;
}

struct MetadataResult
{
    bool success = false;
    bool networkError = false;
    QString title;
    QString platform;
    QString errorMessage;
};

MetadataResult fetchMetadataForRemote(const QString &urlString)
{
    MetadataResult result;
    if (urlString.isEmpty()) {
        return result;
    }

    QString program = QStandardPaths::findExecutable(QStringLiteral("yt-dlp"));
    if (program.isEmpty()) {
        program = QStandardPaths::findExecutable(QStringLiteral("youtube-dl"));
    }
    if (program.isEmpty()) {
        result.errorMessage = QStringLiteral("yt-dlp not found on PATH");
        return result;
    }

    QProcess process;
    process.setProgram(program);
    QStringList arguments{
        QStringLiteral("--no-warnings"),
        QStringLiteral("--dump-single-json"),
        QStringLiteral("--skip-download"),
        QStringLiteral("--no-playlist"),
        urlString
    };
    process.setArguments(arguments);
    process.start();

    if (!process.waitForFinished(15000)) {
        process.kill();
        process.waitForFinished(2000);
        result.networkError = true;
        result.errorMessage = QStringLiteral("Metadata request timed out");
        return result;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString errorText = QString::fromUtf8(process.readAllStandardError());
        result.errorMessage = errorText;
        const QString lowered = errorText.toLower();
        if (lowered.contains(QStringLiteral("unable")) || lowered.contains(QStringLiteral("network"))
            || lowered.contains(QStringLiteral("timed out")) || lowered.contains(QStringLiteral("connection"))
            || lowered.contains(QStringLiteral("resolve"))) {
            result.networkError = true;
        }
        return result;
    }

    const QByteArray output = process.readAllStandardOutput();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(output, &parseError);
    if (document.isNull() || !document.isObject()) {
        result.errorMessage = parseError.errorString();
        return result;
    }

    const QJsonObject object = document.object();
    QString title = object.value(QStringLiteral("fulltitle")).toString();
    if (title.isEmpty()) {
        title = object.value(QStringLiteral("title")).toString();
    }
    QString extractor = object.value(QStringLiteral("extractor_key")).toString();
    if (extractor.isEmpty()) {
        extractor = object.value(QStringLiteral("extractor")).toString();
    }

    result.title = title.trimmed();
    if (!extractor.isEmpty()) {
        result.platform = friendlyPlatformName(extractor);
    }

    if (!result.title.isEmpty() || !result.platform.isEmpty()) {
        result.success = true;
    }

    return result;
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
        spdlog::debug("probeDurationWithMpv: empty path");
        return -1.0;
    }

    QFileInfo info(path);
    if (!info.exists()) {
        spdlog::debug("probeDurationWithMpv: '{}' does not exist", path);
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
        spdlog::warn("probeDurationWithMpv: mpv_create failed");
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
        spdlog::warn("probeDurationWithMpv: mpv loadfile failed for '{}'", path);
        return -1.0;
    }

    int pauseFlag = 0;
    mpv_set_property(handle.get(), "pause", MPV_FORMAT_FLAG, &pauseFlag);

    auto fetchDuration = [&]() -> double {
        double value = 0.0;
        if (mpv_get_property(handle.get(), "duration", MPV_FORMAT_DOUBLE, &value) >= 0 && std::isfinite(value) && value > 0.0) {
            return value;
        }
        return -1.0;
    };

    double duration = fetchDuration();

    // Poll mpv events briefly to give it time to populate metadata.
    const double timeoutSeconds = 0.5;
    const int maxIterations = 20;
    for (int i = 0; i < maxIterations && duration <= 0.0; ++i) {
        mpv_event *event = mpv_wait_event(handle.get(), timeoutSeconds);
        if (!event) {
            continue;
        }

        spdlog::debug("probeDurationWithMpv: event id={} for '{}'", event->event_id, path);

        if (event->event_id == MPV_EVENT_FILE_LOADED || event->event_id == MPV_EVENT_PROPERTY_CHANGE) {
            duration = fetchDuration();
        }

        if (event->event_id == MPV_EVENT_END_FILE || event->event_id == MPV_EVENT_SHUTDOWN) {
            duration = fetchDuration();
            break;
        }
    }

    if (duration <= 0.0) {
        // fallback: try ffprobe
        QStringList arguments;
        arguments << "-v" << "error"
                  << "-select_streams" << "v:0"
                  << "-show_entries" << "format=duration"
                  << "-of" << "default=noprint_wrappers=1:nokey=1"
                  << path;

        QProcess process;
        process.setProgram("ffprobe");
        process.setArguments(arguments);
        process.start();
        if (process.waitForFinished(5000)) {
            QByteArray output = process.readAllStandardOutput().trimmed();
            if (!output.isEmpty()) {
                bool ok = false;
                double ffprobeDuration = output.toDouble(&ok);
                if (ok && ffprobeDuration > 0.0) {
                    duration = ffprobeDuration;
                    spdlog::debug("probeDurationWithMpv: ffprobe duration for '{}' = {}", path, duration);
                } else {
                    spdlog::warn("probeDurationWithMpv: ffprobe returned non-numeric duration '{}' for '{}'", output.constData(), path);
                }
            } else {
                spdlog::warn("probeDurationWithMpv: ffprobe produced no output for '{}'. stderr='{}'",
                             path, process.readAllStandardError().constData());
            }
        } else {
            spdlog::warn("probeDurationWithMpv: ffprobe timed out or failed for '{}'", path);
        }
    }

    spdlog::debug("probeDurationWithMpv: duration for '{}' = {}", path, duration);
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
    if (m_compactOverrideEnabled) {
        updateControlsLayoutMode(m_compactOverrideValue);
    } else {
        updateControlsLayoutMode(event->size().width() < kCompactControlsThresholdPx);
    }
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (!event) {
        return;
    }
    if (event->type() == QEvent::WindowStateChange) {
        if (m_viewFullscreenAction) {
            const QSignalBlocker blocker(m_viewFullscreenAction);
            m_viewFullscreenAction->setChecked(isFullScreen());
        }
        updateMenuAvailability();
    }
}

void MainWindow::handleAddMedia()
{
    if (m_addDialogOpen || m_addDialogProcess) {
        spdlog::info("Add Media dialog already active; ignoring duplicate request");
        return;
    }

    m_addDialogOpen = true;
    updateMenuAvailability();

    const QString helperPath = resolveDialogHelperPath();
    if (helperPath.isEmpty()) {
        spdlog::error("Add Media helper executable not found");
        m_addDialogOpen = false;
        updateMenuAvailability();
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
        m_addDialogOpen = false;
        updateMenuAvailability();
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
    updateMenuAvailability();
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
        int targetIndex = m_currentIndex;
        if (targetIndex < 0 || targetIndex >= m_tracks.size()) {
            targetIndex = 0;
        }
        if (m_restorePlaybackPending && !m_restoreShouldPlay && targetIndex >= 0 && targetIndex < m_tracks.size()) {
            const QString &targetPath = m_tracks.at(targetIndex).normalizedPath;
            if (targetPath == m_restoreNormalizedPath) {
                m_restoreShouldPlay = true;
            }
        }
        playTrack(targetIndex);
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

void MainWindow::handleOpenUrl()
{
    QInputDialog dialog(this);
    dialog.setWindowTitle(tr("Open URL"));
    dialog.setLabelText(tr("Enter the media URL:"));
    dialog.setTextEchoMode(QLineEdit::Normal);
    dialog.resize(720, dialog.sizeHint().height());
    dialog.setMinimumWidth(720);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString trimmed = dialog.textValue().trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    QUrl url = QUrl::fromUserInput(trimmed);
    if (!url.isValid() || url.isRelative() || url.scheme().isEmpty()) {
        QMessageBox::warning(this,
                             tr("Invalid URL"),
                             tr("The provided address could not be understood as a media URL."));
        return;
    }

    QString targetSource;
    if (url.isLocalFile()) {
        targetSource = url.toLocalFile();
    } else {
        targetSource = url.toString(QUrl::FullyEncoded);
    }

    if (!addTrack(targetSource)) {
        QMessageBox::information(this,
                                 tr("Already Added"),
                                 tr("That source is already present in the playlist."));
        return;
    }

    const int newIndex = m_tracks.size() - 1;
    if (newIndex >= 0 && m_playlist) {
        if (QListWidgetItem *item = m_playlist->item(newIndex)) {
            m_playlist->scrollToItem(item, QAbstractItemView::PositionAtBottom);
        }
    }

    if (m_currentIndex == -1 && !m_tracks.isEmpty()) {
        m_currentIndex = 0;
        if (m_playlist) {
            m_playlist->setCurrentRow(0);
        }
        updateNowPlaying(m_tracks.at(0).filePath);
    }

    refreshPlaylistStyles();
    updateTransportAvailability();
}

void MainWindow::handleLoadPlaylist()
{
    QMessageBox::information(this,
                             tr("Load Playlist"),
                             tr("Playlist loading is not implemented yet."));
}

void MainWindow::handleSavePlaylist()
{
    QMessageBox::information(this,
                             tr("Save Playlist"),
                             tr("Playlist saving is not implemented yet."));
}

void MainWindow::handleUndo()
{
    QMessageBox::information(this,
                             tr("Undo"),
                             tr("Undo support is not available yet."));
}

void MainWindow::handleRedo()
{
    QMessageBox::information(this,
                             tr("Redo"),
                             tr("Redo support is not available yet."));
}

void MainWindow::handleRemoveSelectedTrack()
{
    if (!m_playlist) {
        return;
    }

    const int row = m_playlist->currentRow();
    if (row < 0 || row >= m_tracks.size()) {
        return;
    }

    requestRemoveTrack(row);
}

void MainWindow::handleToggleCompactControls(bool checked)
{
    if (checked) {
        m_compactOverrideEnabled = true;
        m_compactOverrideValue = true;
        updateControlsLayoutMode(true);
    } else {
        m_compactOverrideEnabled = false;
        updateControlsLayoutMode();
    }
    updateMenuAvailability();
}

void MainWindow::handleToggleFullscreen(bool checked)
{
    if (checked && !isFullScreen()) {
        showFullScreen();
    } else if (!checked && isFullScreen()) {
        showNormal();
    }
    updateMenuAvailability();
}

void MainWindow::handleShowAboutDrift()
{
    QMessageBox::about(this,
                       tr("About Drift Player"),
                       tr("Drift Player\nA cross-platform media player built with Qt and mpv."));
}

void MainWindow::handleShowAboutQt()
{
    QMessageBox::aboutQt(this);
}

void MainWindow::handleMetadataChanged(const QVariantMap &metadata)
{
    if (m_currentIndex < 0 || m_currentIndex >= m_tracks.size() || metadata.isEmpty()) {
        return;
    }

    TrackEntry &entry = m_tracks[m_currentIndex];

    if (entry.isRemote) {
        QString platformCandidate = metadata.value(QStringLiteral("icy-name")).toString().trimmed();
        if (platformCandidate.isEmpty()) {
            platformCandidate = metadata.value(QStringLiteral("server_name")).toString().trimmed();
        }
        if (!platformCandidate.isEmpty()) {
            entry.platform = friendlyPlatformName(platformCandidate);
            updatePlaylistItem(m_currentIndex);
            refreshNowPlayingLabel();
            refreshNextLabel();
            if (!m_isRestoringPlaylist) {
                savePlaylistState();
            }
        }
    }

    QString candidate;
    const QStringList priorityKeys{QStringLiteral("title"),
                                   QStringLiteral("icy-title"),
                                   QStringLiteral("stream-title"),
                                   QStringLiteral("NAME"),
                                   QStringLiteral("Title")};

    for (const QString &key : priorityKeys) {
        const QVariant value = metadata.value(key);
        if (value.isValid()) {
            candidate = value.toString().trimmed();
            if (!candidate.isEmpty()) {
                break;
            }
        }
    }

    if (candidate.isEmpty()) {
        const QString artist = metadata.value(QStringLiteral("artist")).toString().trimmed();
        const QString title = metadata.value(QStringLiteral("title")).toString().trimmed();
        if (!artist.isEmpty() && !title.isEmpty()) {
            candidate = QStringLiteral("%1 - %2").arg(artist, title);
        } else if (!title.isEmpty()) {
            candidate = title;
        }
    }

    if (!candidate.isEmpty()) {
        updateTrackTitle(m_currentIndex, candidate);
    }
}

void MainWindow::handleMediaTitleChanged(const QString &title)
{
    if (title.trimmed().isEmpty()) {
        return;
    }
    updateTrackTitle(m_currentIndex, title);
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
    invalidateNextIndexCache();
    refreshNextLabel();
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
    invalidateNextIndexCache();
    refreshNextLabel();
}

void MainWindow::handlePlaybackStateChanged(bool playing)
{
    if (m_restorePlaybackPending && !m_restoreShouldPlay && !m_restorePositionApplied && playing && m_videoWidget) {
        m_videoWidget->pause();
        playing = false;
    }
    updatePlayPauseButton(playing);
    updateTransportAvailability();
    updateBassEffectState();
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
    updateBassEffectState();
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

void MainWindow::handleBassThresholdChanged(int value)
{
    Q_UNUSED(value)
    updateBassEffectState();
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
    if (m_restorePlaybackPending && !m_restoreNormalizedPath.isEmpty()) {
        const bool currentValid = (m_currentIndex >= 0 && m_currentIndex < m_tracks.size());
        const QString currentNormalized = currentValid ? m_tracks.at(m_currentIndex).normalizedPath : QString();
        const std::string currentNormalizedLog = currentNormalized.toStdString();
        if (!currentValid) {
            // We are still waiting for the playlist selection to catch up; keep the restore state.
            spdlog::debug("Playback restore pending; waiting for playlist selection (index now {})", m_currentIndex);
        } else if (currentNormalized != m_restoreNormalizedPath) {
            spdlog::info("Cancelling playback restore; current track '{}' differs from saved '{}'",
                         currentNormalizedLog,
                         m_restoreNormalizedPath.toStdString());
            clearPendingRestore();
        } else if (!m_restorePositionApplied) {
            const bool readyForSeek = (duration > 0.01) || (m_restoreSeekTarget <= 0.01);
            if (!readyForSeek) {
                spdlog::debug("Playback restore pending for '{}' - waiting for duration (have={}, target={})",
                              currentNormalizedLog,
                              duration,
                              m_restoreSeekTarget);
            } else if (m_videoWidget) {
                double target = m_restoreSeekTarget;
                if (duration > 0.5 && target > duration - 0.25) {
                    target = std::max(0.0, duration - 0.25);
                }
                if (target < 0.0 || !std::isfinite(target)) {
                    target = 0.0;
                }
                m_restoreSeekTarget = target;
                if (target > 0.01) {
                    spdlog::info("Seeking to {}s to restore playback for '{}'", target, currentNormalizedLog);
                    QMetaObject::invokeMethod(m_videoWidget, [widget = m_videoWidget, target]() {
                        widget->seek(target);
                    }, Qt::QueuedConnection);
                } else {
                    spdlog::info("Restore target near start ({}s) for '{}'; skipping seek", target, currentNormalizedLog);
                }

                if (m_restoreShouldPlay) {
                    spdlog::info("Resuming playback for '{}' after restore", currentNormalizedLog);
                    QMetaObject::invokeMethod(m_videoWidget, &VideoBackgroundWidget::play, Qt::QueuedConnection);
                    QTimer::singleShot(50, m_videoWidget, &VideoBackgroundWidget::requestFrame);
                } else {
                    spdlog::info("Keeping '{}' paused after restore", currentNormalizedLog);
                    QMetaObject::invokeMethod(m_videoWidget, &VideoBackgroundWidget::pause, Qt::QueuedConnection);
                    QMetaObject::invokeMethod(m_videoWidget, &VideoBackgroundWidget::requestFrame, Qt::QueuedConnection);
                    QTimer::singleShot(50, m_videoWidget, &VideoBackgroundWidget::requestFrame);
                }
                m_restorePositionApplied = true;
                if (!m_restoreShouldPlay) {
                    // When restoring a paused session we can drop the pending state immediately so the UI responds normally.
                    clearPendingRestore();
                }
            }
        } else {
            const double epsilon = std::max(0.15, duration * 0.01);
            if (std::fabs(position - m_restoreSeekTarget) <= epsilon) {
                spdlog::info("Playback restore complete for '{}' at {}s (target {}s)",
                             currentNormalizedLog,
                             position,
                             m_restoreSeekTarget);
                clearPendingRestore();
            }
        }
    }

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

void MainWindow::handlePlaylistContextMenu(const QPoint &pos)
{
    if (!m_playlist) {
        return;
    }

    QListWidgetItem *item = m_playlist->itemAt(pos);
    if (!item) {
        return;
    }

    m_contextMenuIndex = m_playlist->row(item);

    QMenu menu(this);
    menu.setWindowFlags(menu.windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    menu.setAttribute(Qt::WA_TranslucentBackground);
    menu.setStyleSheet(QStringLiteral(
        "QMenu {\n"
        "    background-color: rgba(18, 20, 28, 215);\n"
        "    border: 1px solid rgba(255, 255, 255, 35);\n"
        "    padding: 10px 8px;\n"
        "    border-radius: 16px;\n"
        "}\n"
        "QMenu::item {\n"
        "    color: rgba(245, 245, 250, 230);\n"
        "    padding: 8px 20px;\n"
        "    border-radius: 12px;\n"
        "}\n"
        "QMenu::item:selected {\n"
        "    background-color: rgba(255, 255, 255, 55);\n"
        "    color: rgba(255, 255, 255, 255);\n"
        "}\n"
        "QMenu::item:disabled {\n"
        "    color: rgba(245, 245, 250, 120);\n"
        "}\n"
        "QMenu::separator {\n"
        "    height: 1px;\n"
        "    background: rgba(255, 255, 255, 35);\n"
        "    margin: 6px 14px;\n"
        "}\n"));
    auto *shadow = new QGraphicsDropShadowEffect(&menu);
    shadow->setBlurRadius(28);
    shadow->setColor(QColor(0, 0, 0, 180));
    shadow->setOffset(0, 8);
    menu.setGraphicsEffect(shadow);

    QAction *deletePlaylistAction = menu.addAction(tr("Delete from Playlist"));
    QAction *deleteDiskAction = menu.addAction(tr("Delete from Disk"));

    QAction *chosen = menu.exec(m_playlist->viewport()->mapToGlobal(pos));
    if (chosen == deletePlaylistAction) {
        handleDeleteTrackFromPlaylist();
    } else if (chosen == deleteDiskAction) {
        handleDeleteTrackFromDisk();
    } else {
        m_contextMenuIndex = -1;
    }
}

void MainWindow::handleDeleteTrackFromPlaylist()
{
    const int index = m_contextMenuIndex;
    m_contextMenuIndex = -1;
    requestRemoveTrack(index);
}

void MainWindow::handleDeleteTrackFromDisk()
{
    const int index = m_contextMenuIndex;
    m_contextMenuIndex = -1;
    if (index < 0 || index >= m_tracks.size()) {
        return;
    }

    const QString path = m_tracks.at(index).normalizedPath;
    const QMessageBox::StandardButton response = QMessageBox::question(
        this,
        tr("Delete from Disk"),
        tr("Are you sure you want to permanently delete this file?\n%1").arg(path),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (response != QMessageBox::Yes) {
        return;
    }

    bool removed = true;
    if (!path.isEmpty() && QFile::exists(path)) {
        QFile file(path);
        if (!file.remove()) {
            removed = false;
            QMessageBox::warning(this,
                                 tr("Unable to Delete"),
                                 tr("Failed to remove file:\n%1").arg(path));
        }
    }

    if (removed) {
        requestRemoveTrack(index);
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
    const QByteArray bassEnv = qgetenv("DRIFT_PLAYER_DEBUG_BASS");
    m_debugBassVisualizer = (bassEnv == "1" || bassEnv == "true" || bassEnv == "on");

    m_videoWidget = new VideoBackgroundWidget(this);

    QMenuBar *mainMenuBar = new QMenuBar(this);
    setMenuBar(mainMenuBar);

    QMenu *fileMenu = mainMenuBar->addMenu(tr("&File"));
    m_loadPlaylistAction = fileMenu->addAction(tr("Load Playlist..."));
    m_loadPlaylistAction->setShortcut(QKeySequence::Open);
    connect(m_loadPlaylistAction, &QAction::triggered, this, &MainWindow::handleLoadPlaylist);

    m_savePlaylistAction = fileMenu->addAction(tr("Save Playlist..."));
    m_savePlaylistAction->setShortcut(QKeySequence::Save);
    connect(m_savePlaylistAction, &QAction::triggered, this, &MainWindow::handleSavePlaylist);

    m_openUrlAction = fileMenu->addAction(tr("Open URL..."));
    m_openUrlAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_U));
    connect(m_openUrlAction, &QAction::triggered, this, &MainWindow::handleOpenUrl);

    m_addMediaAction = fileMenu->addAction(tr("Add Media"));
    m_addMediaAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A));
    connect(m_addMediaAction, &QAction::triggered, this, &MainWindow::handleAddMedia);

    fileMenu->addSeparator();
    m_quitAction = fileMenu->addAction(tr("Quit"));
    m_quitAction->setShortcut(QKeySequence::Quit);
    connect(m_quitAction, &QAction::triggered, qApp, &QApplication::quit);

    QMenu *editMenu = mainMenuBar->addMenu(tr("&Edit"));
    m_undoAction = editMenu->addAction(tr("Undo"));
    m_undoAction->setShortcut(QKeySequence::Undo);
    connect(m_undoAction, &QAction::triggered, this, &MainWindow::handleUndo);

    m_redoAction = editMenu->addAction(tr("Redo"));
    m_redoAction->setShortcut(QKeySequence::Redo);
    connect(m_redoAction, &QAction::triggered, this, &MainWindow::handleRedo);

    editMenu->addSeparator();
    m_removeTrackAction = editMenu->addAction(tr("Remove Selected Track"));
    m_removeTrackAction->setShortcut(QKeySequence::Delete);
    connect(m_removeTrackAction, &QAction::triggered, this, &MainWindow::handleRemoveSelectedTrack);

    QMenu *viewMenu = mainMenuBar->addMenu(tr("&View"));
    m_viewCompactAction = viewMenu->addAction(tr("Compact Controls"));
    m_viewCompactAction->setCheckable(true);
    connect(m_viewCompactAction, &QAction::toggled, this, &MainWindow::handleToggleCompactControls);

    m_viewFullscreenAction = viewMenu->addAction(tr("Fullscreen"));
    m_viewFullscreenAction->setCheckable(true);
    m_viewFullscreenAction->setShortcut(Qt::Key_F11);
    connect(m_viewFullscreenAction, &QAction::toggled, this, &MainWindow::handleToggleFullscreen);

    QMenu *playbackMenu = mainMenuBar->addMenu(tr("&Playback"));
    m_playAction = playbackMenu->addAction(tr("Play / Pause"));
    m_playAction->setShortcut(Qt::Key_Space);
    connect(m_playAction, &QAction::triggered, this, &MainWindow::handlePlayPause);

    m_playPreviousAction = playbackMenu->addAction(tr("Previous"));
    m_playPreviousAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Left));
    connect(m_playPreviousAction, &QAction::triggered, this, &MainWindow::handlePlayPrevious);

    m_playNextAction = playbackMenu->addAction(tr("Next"));
    m_playNextAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Right));
    connect(m_playNextAction, &QAction::triggered, this, &MainWindow::handlePlayNext);

    QMenu *helpMenu = mainMenuBar->addMenu(tr("&Help"));
    m_aboutDriftAction = helpMenu->addAction(tr("About Drift Player"));
    connect(m_aboutDriftAction, &QAction::triggered, this, &MainWindow::handleShowAboutDrift);

    m_aboutQtAction = helpMenu->addAction(tr("About Qt"));
    connect(m_aboutQtAction, &QAction::triggered, this, &MainWindow::handleShowAboutQt);

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
    m_playlist->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_playlist, &QListWidget::customContextMenuRequested, this, &MainWindow::handlePlaylistContextMenu);
    connect(m_playlist, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *, QListWidgetItem *) {
        refreshPlaylistStyles();
        updateMenuAvailability();
    });
    if (auto *selection = m_playlist->selectionModel()) {
        connect(selection, &QItemSelectionModel::selectionChanged, this, [this](const QItemSelection &, const QItemSelection &) {
            refreshPlaylistStyles();
            updateMenuAvailability();
        });
    }
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

    m_bassSlider = new QSlider(Qt::Horizontal, m_controlsContainer);
    m_bassSlider->setRange(0, 100);
    m_bassSlider->setValue(kDefaultBassThreshold);
    m_bassSlider->setToolTip(tr("Bass reaction threshold"));
    m_bassSlider->setCursor(Qt::PointingHandCursor);
    m_bassSlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_bassSlider->setMinimumWidth(140);
    m_bassSlider->setStyleSheet(R"(
        QSlider {
            height: 18px;
        }
        QSlider::groove:horizontal {
            background: rgba(255, 255, 255, 24);
            border-radius: 3px;
            height: 4px;
        }
        QSlider::sub-page:horizontal {
            background: rgba(220, 110, 255, 150);
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

    m_nextLabel = new QLabel(tr("Next: --"), m_controlsContainer);
    m_nextLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    m_nextLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_nextLabel->setMinimumHeight(24);
    m_nextLabel->setWordWrap(true);
    m_nextLabel->setStyleSheet(R"(
        color: rgba(200, 205, 215, 220);
        background-color: rgba(10, 10, 18, 150);
        padding: 4px 18px;
        border-radius: 14px;
        font-size: 12px;
        letter-spacing: 0.2px;
    )");

    auto *controlsLayout = new QVBoxLayout(m_controlsContainer);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->setSpacing(12);
    controlsLayout->addWidget(m_titleLabel);
    controlsLayout->addWidget(m_nextLabel);

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

    m_bassLabel = new QLabel(tr("Bass React"), m_controlsContainer);
    m_bassLabel->setStyleSheet("color: rgba(255, 255, 255, 210);");

    m_slidersLayout = new QGridLayout;
    m_slidersLayout->setContentsMargins(0, 0, 0, 0);
    m_slidersLayout->setHorizontalSpacing(12);
    m_slidersLayout->setVerticalSpacing(6);
    m_slidersLayout->addWidget(m_blurLabel, 0, 0, Qt::AlignRight | Qt::AlignVCenter);
    m_slidersLayout->addWidget(m_blurSlider, 0, 1);
    m_slidersLayout->addWidget(m_volumeLabel, 1, 0, Qt::AlignRight | Qt::AlignVCenter);
    m_slidersLayout->addWidget(m_volumeSlider, 1, 1);
    m_slidersLayout->addWidget(m_bassLabel, 2, 0, Qt::AlignRight | Qt::AlignVCenter);
    m_slidersLayout->addWidget(m_bassSlider, 2, 1);
    m_slidersLayout->setColumnStretch(1, 1);

    buttonsLayout->addLayout(m_actionsLayout);
    buttonsLayout->addSpacing(18);
    buttonsLayout->addStretch(1);
    buttonsLayout->addLayout(m_slidersLayout, 1);
    controlsLayout->addLayout(buttonsLayout);

    m_bassVisualizer = new BassVisualizerWidget(m_controlsContainer);
    m_bassVisualizer->setMinimumHeight(72);
    m_bassVisualizer->setActive(false);
    m_bassVisualizer->setSensitivity(static_cast<double>(kDefaultBassThreshold) / 100.0);
    m_bassVisualizer->setVisible(false);
    controlsLayout->addWidget(m_bassVisualizer);

    updateControlsLayoutMode();

    if (!m_debugBassVisualizer) {
        if (m_bassLabel) {
            m_bassLabel->hide();
        }
        if (m_bassSlider) {
            m_bassSlider->hide();
        }
        if (m_bassVisualizer) {
            m_bassVisualizer->hide();
        }
    }

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

    updateMenuAvailability();
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
    connect(m_bassSlider, &QSlider::valueChanged, this, &MainWindow::handleBassThresholdChanged);
    connect(m_progressSlider, &QSlider::sliderPressed, this, &MainWindow::handleProgressSliderPressed);
    connect(m_progressSlider, &QSlider::sliderReleased, this, &MainWindow::handleProgressSliderReleased);
    connect(m_progressSlider, &QSlider::sliderMoved, this, &MainWindow::handleProgressSliderMoved);

    connect(m_videoWidget, &VideoBackgroundWidget::playbackStateChanged, this, &MainWindow::handlePlaybackStateChanged);
    connect(m_videoWidget, &VideoBackgroundWidget::playbackFinished, this, &MainWindow::handlePlaybackFinished);
    connect(m_videoWidget, &VideoBackgroundWidget::positionChanged, this, &MainWindow::handlePositionChanged);
    connect(m_videoWidget, &VideoBackgroundWidget::metadataChanged, this, &MainWindow::handleMetadataChanged);
    connect(m_videoWidget, &VideoBackgroundWidget::mediaTitleChanged, this, &MainWindow::handleMediaTitleChanged);
    connect(m_videoWidget, &VideoBackgroundWidget::blurModeChanged, this, [this](bool shaderActive) {
        if (m_blurSlider) {
            m_blurSlider->setEnabled(shaderActive);
            m_blurSlider->setToolTip(shaderActive
                                          ? tr("Background blur")
                                          : tr("Blur handled by decoder"));
        }
    });

    updateBassEffectState();

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

    const int bassThreshold = m_settings.value("visual/bassThreshold", kDefaultBassThreshold).toInt();
    if (m_bassSlider) {
        m_bassSlider->setValue(bassThreshold);
    }
    handleBassThresholdChanged(bassThreshold);

    restorePlaylistState();
    restorePlaybackState();
    updateTransportAvailability();
    updateBassEffectState();
    invalidateNextIndexCache();
    refreshNextLabel();
}

void MainWindow::saveSettings()
{
    m_settings.setValue("audio/volume", m_volumeSlider->value());
    m_settings.setValue("playback/shuffle", m_shuffleEnabled);
    m_settings.setValue("playback/repeat", static_cast<int>(m_repeatMode));
    if (m_blurSlider) {
        m_settings.setValue("visual/blur", m_blurSlider->value());
    }
    if (m_bassSlider) {
        m_settings.setValue("visual/bassThreshold", m_bassSlider->value());
    }
    savePlaylistState();
    if (m_currentIndex >= 0 && m_currentIndex < m_tracks.size()) {
        const TrackEntry &entry = m_tracks.at(m_currentIndex);
        m_settings.setValue("playback/currentPath", entry.normalizedPath);

        double position = 0.0;
        bool wasPlaying = false;
        if (m_videoWidget && m_videoWidget->hasMedia()) {
            position = m_videoWidget->position();
            if (!std::isfinite(position) || position < 0.0) {
                position = 0.0;
            }
            wasPlaying = !m_videoWidget->isPaused();
        } else if (!m_restoreNormalizedPath.isEmpty() && entry.normalizedPath == m_restoreNormalizedPath) {
            position = m_restoreSeekTarget;
            wasPlaying = m_restoreShouldPlay;
        }

        spdlog::info("Persisting playback resume path='{}' position={}s playing={}",
                     entry.normalizedPath,
                     position,
                     wasPlaying);
        m_settings.setValue("playback/position", position);
        m_settings.setValue("playback/wasPlaying", wasPlaying);
    } else {
        spdlog::info("Clearing playback resume state (no active track)");
        m_settings.remove("playback/currentPath");
        m_settings.remove("playback/position");
        m_settings.remove("playback/wasPlaying");
    }
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

    const bool remote = isRemoteLocation(filePath);

    if (!remote) {
        QFileInfo info(normalized);
        if (!info.exists()) {
            spdlog::warn("Skipping '{}' because file is missing", normalized);
            return false;
        }
    }

    TrackEntry entry{displayNameForFile(filePath), filePath, normalized};
    entry.isRemote = remote;
    if (remote) {
        entry.platform = platformLabelForUrl(QUrl::fromUserInput(filePath));
    }
    m_tracks.append(entry);
    const int index = m_tracks.size() - 1;

    auto *item = new QListWidgetItem(entry.title);
    item->setSizeHint(QSize(0, 42));
    m_playlist->addItem(item);
    m_knownPaths.insert(normalized);
    spdlog::info("Track registered title='{}' normalized='{}'", entry.title, normalized);

    updatePlaylistItem(index);

    if (!remote) {
        startWatchingTrack(normalized);
    }

    if (!m_isRestoringPlaylist) {
        savePlaylistState();
    }

    updateTransportAvailability();
    if (entry.isRemote && !m_isRestoringPlaylist) {
        startMetadataFetch(index);
    }
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
    bool playlistChanged = false;
    for (const QString &file : newTracks) {
        timer.start();
        if (addTrack(file)) {
            spdlog::info("Queued '{}' in {} ms", file, timer.elapsed());
            const QString normalized = normalizedPathFor(file);
            enqueueDurationProbe(normalized);
            playlistChanged = true;
        }
    }

    if (playlistChanged) {
        invalidateNextIndexCache();
        refreshNextLabel();
    }

    if (m_currentIndex == -1 && !m_tracks.isEmpty()) {
        m_currentIndex = 0;
        invalidateNextIndexCache();
        m_playlist->setCurrentRow(0);
        updateNowPlaying(m_tracks.at(0).filePath);
        updateTransportAvailability();
    }
}

void MainWindow::playTrack(int index)
{
    if (index < 0 || index >= m_tracks.size()) {
        spdlog::warn("playTrack out of bounds index={}", index);
        return;
    }

    if (!m_videoWidget) {
        spdlog::error("Video widget unavailable; cannot play track index={}", index);
        clearPendingRestore();
        return;
    }

    const TrackEntry &entry = m_tracks.at(index);
    const bool restoringTrack = m_restorePlaybackPending && !m_restoreNormalizedPath.isEmpty()
                                && entry.normalizedPath == m_restoreNormalizedPath;

    if (!restoringTrack) {
        clearPendingRestore();
    }

    if (entry.isRemote) {
        startMetadataFetch(index);
    }

    m_videoWidget->setAutoStartOnLoad(restoringTrack ? m_restoreShouldPlay : true);

    if (!m_videoWidget->loadFile(entry.filePath)) {
        spdlog::error("Failed to load track '{}'", entry.filePath);
        if (entry.isRemote) {
            QMessageBox::warning(this,
                                 tr("Network Error"),
                                 tr("Unable to load the remote media source.\n"
                                    "Please check your network connection and try again."));
        }
        if (restoringTrack) {
            clearPendingRestore();
        }
        return;
    }

    m_currentIndex = index;
    invalidateNextIndexCache();
    m_playlist->setCurrentRow(index);
    updateNowPlaying(entry.filePath);
    updateTransportAvailability();
    refreshPlaylistStyles();
}

int MainWindow::resolveNextIndex() const
{
    if (m_nextIndexCacheValid) {
        return m_nextIndexCache;
    }

    int nextIndex = -1;
    const bool haveTracks = !m_tracks.isEmpty();
    const bool currentValid = (m_currentIndex >= 0 && m_currentIndex < m_tracks.size());

    if (!haveTracks) {
        nextIndex = -1;
    } else if (m_repeatMode == RepeatMode::One && currentValid) {
        nextIndex = m_currentIndex;
    } else if (m_shuffleEnabled && m_tracks.size() > 1 && currentValid) {
        int candidate = m_currentIndex;
        while (candidate == m_currentIndex) {
            candidate = static_cast<int>(QRandomGenerator::global()->bounded(m_tracks.size()));
        }
        nextIndex = candidate;
    } else if (currentValid) {
        int candidate = m_currentIndex + 1;
        if (candidate >= m_tracks.size()) {
            if (m_repeatMode == RepeatMode::All) {
                candidate = 0;
            } else {
                candidate = -1;
            }
        }
        nextIndex = candidate;
    } else {
        if (m_shuffleEnabled && m_tracks.size() > 1) {
            nextIndex = static_cast<int>(QRandomGenerator::global()->bounded(m_tracks.size()));
        } else {
            nextIndex = haveTracks ? 0 : -1;
        }
    }

    m_nextIndexCache = nextIndex;
    m_nextIndexCacheValid = true;
    return m_nextIndexCache;
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

    if (!m_restoreNormalizedPath.isEmpty() && m_restoreNormalizedPath == entry.normalizedPath) {
        clearPendingRestore();
    }

    stopWatchingTrack(entry.normalizedPath);
    m_knownPaths.remove(entry.normalizedPath);
    m_metadataPending.remove(entry.normalizedPath);
    m_tracks.removeAt(index);

    m_durationProbeQueue.removeAll(entry.normalizedPath);
    if (!m_durationProbeCurrent.isEmpty() && m_durationProbeCurrent == entry.normalizedPath) {
        m_durationProbeCurrent.clear();
    }

    QMetaObject::invokeMethod(this, &MainWindow::processDurationQueue, Qt::QueuedConnection);

    invalidateNextIndexCache();

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
    refreshPlaylistStyles();
    refreshNextLabel();
}

void MainWindow::restorePlaylistState()
{
    const QStringList stored = m_settings.value("playlist/paths").toStringList();
    if (stored.isEmpty()) {
        return;
    }

    const QVariantList storedDurations = m_settings.value("playlist/durations").toList();
    const QStringList storedTitles = m_settings.value("playlist/titles").toStringList();
    const QStringList storedPlatforms = m_settings.value("playlist/platforms").toStringList();
    const QVariantList storedRemotes = m_settings.value("playlist/remotes").toList();

    m_isRestoringPlaylist = true;
    for (int i = 0; i < stored.size(); ++i) {
        const QString &path = stored.at(i);
        if (path.isEmpty()) {
            continue;
        }

        const bool remote = isRemoteLocation(path);
        if (!remote && !QFileInfo::exists(path)) {
            spdlog::info("Skipping missing playlist entry '{}' during restore", path);
            continue;
        }

        if (addTrack(path)) {
            TrackEntry &entry = m_tracks.last();
            if (i < storedRemotes.size()) {
                entry.isRemote = storedRemotes.at(i).toBool();
            }
            if (entry.isRemote) {
                if (i < storedPlatforms.size() && !storedPlatforms.at(i).isEmpty()) {
                    entry.platform = storedPlatforms.at(i);
                } else {
                    entry.platform = platformLabelForUrl(QUrl::fromUserInput(entry.filePath));
                }
            }
            if (i < storedTitles.size() && !storedTitles.at(i).isEmpty()) {
                entry.title = storedTitles.at(i);
            }
            if (i < storedDurations.size()) {
                double storedDuration = storedDurations.at(i).toDouble();
                if (storedDuration > 0.5 && !m_tracks.isEmpty()) {
                    entry.durationSeconds = storedDuration;
                    updatePlaylistItem(m_tracks.size() - 1);
                } else if (!entry.isRemote) {
                    enqueueDurationProbe(path);
                }
            } else if (!entry.isRemote) {
                enqueueDurationProbe(path);
            }
            updatePlaylistItem(m_tracks.size() - 1);
            if (entry.isRemote) {
                const QString defaultTitle = displayNameForFile(entry.filePath);
                const QString trimmedTitle = entry.title.trimmed();
                if (trimmedTitle.isEmpty() || trimmedTitle.compare(defaultTitle, Qt::CaseInsensitive) == 0) {
                    startMetadataFetch(m_tracks.size() - 1);
                }
            }
        }
    }
    m_isRestoringPlaylist = false;

    savePlaylistState();
    refreshPlaylistStyles();
    invalidateNextIndexCache();
    refreshNextLabel();
}

void MainWindow::restorePlaybackState()
{
    const QString normalized = m_settings.value("playback/currentPath").toString();
    if (normalized.isEmpty()) {
        spdlog::info("No playback resume data available");
        clearPendingRestore();
        return;
    }

    const int index = indexForNormalizedPath(normalized);
    if (index < 0) {
        spdlog::info("Skipping playback restore; saved track '{}' not present", normalized);
        m_settings.remove("playback/currentPath");
        m_settings.remove("playback/position");
        m_settings.remove("playback/wasPlaying");
        clearPendingRestore();
        return;
    }

    double position = m_settings.value("playback/position", 0.0).toDouble();
    if (!std::isfinite(position) || position < 0.0) {
        position = 0.0;
    }

    m_restoreNormalizedPath = normalized;
    m_restoreSeekTarget = position;
    const bool storedWasPlaying = m_settings.value("playback/wasPlaying", false).toBool();
    if (storedWasPlaying) {
        spdlog::info("Saved session was playing, but auto-resume is disabled; restoring paused.");
    }
    m_restoreShouldPlay = false;
    spdlog::info("Restoring playback for '{}' (index {}) position={}s (auto-play disabled)",
                 normalized.toStdString(),
                 index,
                 m_restoreSeekTarget);
    m_restorePositionApplied = false;
    m_restorePlaybackPending = true;

    m_currentIndex = index;
    if (m_playlist) {
        m_playlist->setCurrentRow(index);
    }
    refreshPlaylistStyles();
    updateNowPlaying(m_tracks.at(index).filePath);

    double durationSeconds = m_tracks.at(index).durationSeconds;
    if (durationSeconds <= 0.0) {
        durationSeconds = 0.0;
    }
    applyRestoredProgressToUi(m_restoreSeekTarget, durationSeconds);

    invalidateNextIndexCache();
    refreshNextLabel();
    spdlog::info("Ready to resume '{}'; press Play to continue at {}s",
                 normalized.toStdString(),
                 m_restoreSeekTarget);
}

int MainWindow::indexForNormalizedPath(const QString &normalizedPath) const
{
    if (normalizedPath.isEmpty()) {
        return -1;
    }

    for (int i = 0; i < m_tracks.size(); ++i) {
        if (m_tracks.at(i).normalizedPath == normalizedPath) {
            return i;
        }
    }
    return -1;
}

void MainWindow::clearPendingRestore()
{
    m_restorePlaybackPending = false;
    m_restorePositionApplied = false;
    m_restoreSeekTarget = 0.0;
    m_restoreShouldPlay = false;
    if (!m_restoreNormalizedPath.isEmpty()) {
        spdlog::debug("Clearing playback restore state for '{}'", m_restoreNormalizedPath);
    }
    m_restoreNormalizedPath.clear();
    if (m_videoWidget) {
        m_videoWidget->setAutoStartOnLoad(true);
    }
}

void MainWindow::applyRestoredProgressToUi(double position, double durationSeconds)
{
    if (!std::isfinite(position) || position < 0.0) {
        position = 0.0;
    }
    if (!std::isfinite(durationSeconds) || durationSeconds < 0.0) {
        durationSeconds = 0.0;
    }
    if (durationSeconds > 0.0 && position > durationSeconds) {
        position = durationSeconds;
    }

    m_lastDuration = durationSeconds;
    m_progressSliderPressed = false;

    if (m_progressSlider) {
        const QSignalBlocker blocker(m_progressSlider);
        if (durationSeconds > 0.0) {
            if (!m_progressSlider->isEnabled()) {
                m_progressSlider->setEnabled(true);
            }
            const int maxValue = m_progressSlider->maximum();
            if (maxValue > 0) {
                double ratio = position / durationSeconds;
                if (!std::isfinite(ratio) || ratio < 0.0) {
                    ratio = 0.0;
                } else if (ratio > 1.0) {
                    ratio = 1.0;
                }
                const int sliderValue = static_cast<int>(ratio * maxValue + 0.5);
                m_progressSlider->setValue(std::clamp(sliderValue, 0, maxValue));
            } else {
                m_progressSlider->setValue(0);
            }
        } else {
            m_progressSlider->setValue(0);
            m_progressSlider->setEnabled(false);
        }
    }

    if (m_timeLabel) {
        const QString current = formatTime(position);
        const QString total = formatTime(durationSeconds);
        m_timeLabel->setText(QStringLiteral("%1 / %2").arg(current, total));
    }
}

void MainWindow::updateMenuAvailability()
{
    const bool hasTracks = !m_tracks.isEmpty();
    const bool hasMedia = m_videoWidget && m_videoWidget->hasMedia();
    const bool canNavigate = hasTracks && m_tracks.size() > 1;
    const bool hasSelection = m_playlist && (m_playlist->currentRow() >= 0);

    if (m_loadPlaylistAction) {
        m_loadPlaylistAction->setEnabled(true);
    }
    if (m_savePlaylistAction) {
        m_savePlaylistAction->setEnabled(hasTracks);
    }
    if (m_addMediaAction) {
        const bool enabled = !m_addDialogOpen;
        m_addMediaAction->setEnabled(enabled);
    }
    if (m_openUrlAction) {
        m_openUrlAction->setEnabled(true);
    }
    if (m_quitAction) {
        m_quitAction->setEnabled(true);
    }
    if (m_aboutDriftAction) {
        m_aboutDriftAction->setEnabled(true);
    }
    if (m_aboutQtAction) {
        m_aboutQtAction->setEnabled(true);
    }
    if (m_undoAction) {
        m_undoAction->setEnabled(false);
    }
    if (m_redoAction) {
        m_redoAction->setEnabled(false);
    }
    if (m_removeTrackAction) {
        m_removeTrackAction->setEnabled(hasSelection);
    }
    if (m_viewCompactAction) {
        const QSignalBlocker blocker(m_viewCompactAction);
        m_viewCompactAction->setChecked(m_compactOverrideEnabled && m_compactOverrideValue);
        m_viewCompactAction->setEnabled(true);
    }
    if (m_viewFullscreenAction) {
        const QSignalBlocker blocker(m_viewFullscreenAction);
        m_viewFullscreenAction->setChecked(isFullScreen());
        m_viewFullscreenAction->setEnabled(true);
    }
    if (m_playAction) {
        m_playAction->setEnabled(hasTracks || hasMedia);
    }
    if (m_playNextAction) {
        m_playNextAction->setEnabled(canNavigate);
    }
    if (m_playPreviousAction) {
        m_playPreviousAction->setEnabled(canNavigate);
    }
}

void MainWindow::savePlaylistState()
{
    QStringList paths;
    paths.reserve(m_tracks.size());
    for (const TrackEntry &entry : m_tracks) {
        paths.append(entry.normalizedPath);
    }

    QVariantList durations;
    durations.reserve(m_tracks.size());
    for (const TrackEntry &entry : m_tracks) {
        durations.append(entry.durationSeconds);
    }

    QStringList titles;
    titles.reserve(m_tracks.size());
    for (const TrackEntry &entry : m_tracks) {
        titles.append(entry.title);
    }

    QStringList platforms;
    platforms.reserve(m_tracks.size());
    for (const TrackEntry &entry : m_tracks) {
        platforms.append(entry.platform);
    }

    QVariantList remotes;
    remotes.reserve(m_tracks.size());
    for (const TrackEntry &entry : m_tracks) {
        remotes.append(entry.isRemote);
    }

    m_settings.setValue("playlist/paths", paths);
    m_settings.setValue("playlist/durations", durations);
    m_settings.setValue("playlist/titles", titles);
    m_settings.setValue("playlist/platforms", platforms);
    m_settings.setValue("playlist/remotes", remotes);
    refreshPlaylistStyles();
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

void MainWindow::refreshPlaylistStyles()
{
    if (!m_playlist) {
        return;
    }

    for (int i = 0; i < m_playlist->count(); ++i) {
        if (QListWidgetItem *item = m_playlist->item(i)) {
            updatePlaylistRowState(i, item);
        }
    }
}

void MainWindow::requestRemoveTrack(int index)
{
    if (index < 0 || index >= m_tracks.size()) {
        return;
    }

    removeTrackAt(index);
}

void MainWindow::logTracks(const char *tag) const
{
    spdlog::debug("[{}] tracks={} currentIndex={}", tag, m_tracks.size(), m_currentIndex);
    for (int i = 0; i < m_tracks.size(); ++i) {
        const auto &t = m_tracks.at(i);
        spdlog::debug("  [{}] title='{}' normalized='{}' duration={}", i, t.title, t.normalizedPath, t.durationSeconds);
    }
}

void MainWindow::updatePlaylistRowState(int index, QListWidgetItem *item)
{
    if (!item) {
        return;
    }

    const bool isCurrent = (index == m_currentIndex && m_currentIndex != -1);
    const bool isSelected = item->isSelected();
    QFont font = item->font();
    font.setBold(isCurrent);
    item->setFont(font);

    const QColor normalText(245, 245, 250, 220);
    const QColor playingText(255, 255, 255, 255);
    item->setForeground(isCurrent ? playingText : normalText);

    if (isSelected) {
        item->setBackground(isCurrent ? QColor(255, 255, 255, 80) : QColor(255, 255, 255, 56));
    } else {
        item->setBackground(isCurrent ? QColor(255, 255, 255, 38) : Qt::transparent);
    }
}

void MainWindow::updateBassEffectState()
{
    const double threshold = m_bassSlider ? std::clamp(static_cast<double>(m_bassSlider->value()) / 100.0, 0.0, 1.0) : static_cast<double>(kDefaultBassThreshold) / 100.0;
    const bool sliderActive = m_debugBassVisualizer && m_bassSlider && m_bassSlider->value() > 0;
    const bool playing = m_videoWidget && m_videoWidget->hasMedia() && !m_videoWidget->isPaused();
    const bool active = m_debugBassVisualizer && sliderActive && playing;

    if (m_videoWidget) {
        m_videoWidget->setBassThreshold(threshold);
        m_videoWidget->setBassEnabled(active);
    }

    if (m_bassVisualizer) {
        m_bassVisualizer->setSensitivity(std::max(0.05, threshold));
        m_bassVisualizer->setActive(active);
        m_bassVisualizer->setVisible(m_debugBassVisualizer && !m_compactControls);
    }

    if (m_bassLabel) {
        m_bassLabel->setVisible(m_debugBassVisualizer && !m_compactControls);
    }

    if (m_bassSlider) {
        m_bassSlider->setVisible(m_debugBassVisualizer);
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
    const QString displayText = displayTitleForEntry(entry);
    item->setText(displayText);
    item->setData(Qt::UserRole, entry.filePath);
    item->setToolTip(QStringLiteral("%1\n%2").arg(displayText, entry.filePath));

    QString durationText;
    if (entry.durationSeconds > 0.5) {
        durationText = formatTime(entry.durationSeconds);
    }
    item->setData(Qt::UserRole + 1, durationText);

    updatePlaylistRowState(index, item);
}

QString MainWindow::displayTitleForEntry(const TrackEntry &entry) const
{
    QString base = entry.title.trimmed();
    if (base.isEmpty()) {
        base = displayNameForFile(entry.filePath);
    }

    if (entry.isRemote && !entry.platform.isEmpty()) {
        const QString prefix = entry.platform + QStringLiteral(":");
        if (base.startsWith(prefix, Qt::CaseInsensitive) || base.startsWith(entry.platform, Qt::CaseInsensitive)) {
            return base;
        }
        return QStringLiteral("%1: %2").arg(entry.platform, base);
    }

    return base;
}

QString MainWindow::displayTitleForIndex(int index) const
{
    if (index < 0 || index >= m_tracks.size()) {
        return QString();
    }
    return displayTitleForEntry(m_tracks.at(index));
}

void MainWindow::refreshNowPlayingLabel()
{
    if (!m_titleLabel) {
        return;
    }

    if (m_currentIndex < 0 || m_currentIndex >= m_tracks.size()) {
        m_titleLabel->setText(tr("Select a track to play"));
        m_titleLabel->setToolTip(QString());
        return;
    }

    const QString display = displayTitleForEntry(m_tracks.at(m_currentIndex));
    m_titleLabel->setText(tr("Now Playing: %1").arg(display));
    m_titleLabel->setToolTip(display);
}

void MainWindow::updateTrackTitle(int index, const QString &title)
{
    if (index < 0 || index >= m_tracks.size()) {
        return;
    }

    QString normalized = title.trimmed();
    if (normalized.isEmpty()) {
        return;
    }

    TrackEntry &entry = m_tracks[index];
    if (entry.title.compare(normalized, Qt::CaseInsensitive) == 0) {
        return;
    }

    entry.title = normalized;
    updatePlaylistItem(index);

    if (index == m_currentIndex) {
        refreshNowPlayingLabel();
    }

    if (!m_isRestoringPlaylist) {
        savePlaylistState();
    }
}

void MainWindow::startMetadataFetch(int index)
{
    if (index < 0 || index >= m_tracks.size()) {
        return;
    }

    const TrackEntry &entry = m_tracks.at(index);
    if (!entry.isRemote || entry.normalizedPath.isEmpty()) {
        return;
    }

    if (m_metadataPending.contains(entry.normalizedPath)) {
        return;
    }

    m_metadataPending.insert(entry.normalizedPath);

    auto *watcher = new QFutureWatcher<MetadataResult>(this);
    connect(watcher, &QFutureWatcher<MetadataResult>::finished, this, [this, watcher, path = entry.normalizedPath]() {
        const MetadataResult result = watcher->result();
        watcher->deleteLater();
        m_metadataPending.remove(path);

        const int idx = indexForNormalizedPath(path);
        if (idx == -1) {
            return;
        }

        if (!result.success) {
            if (!result.errorMessage.isEmpty()) {
                spdlog::debug("Metadata fetch failed for '{}': {}", path.toStdString(), result.errorMessage.toStdString());
            }
            return;
        }

        TrackEntry &track = m_tracks[idx];
        if (track.isRemote && !result.platform.isEmpty()) {
            track.platform = result.platform;
        }
        if (!result.title.trimmed().isEmpty()) {
            updateTrackTitle(idx, result.title.trimmed());
        } else {
            updatePlaylistItem(idx);
            if (idx == m_currentIndex) {
                refreshNowPlayingLabel();
            }
            refreshNextLabel();
        }
    });

    watcher->setFuture(QtConcurrent::run([url = entry.filePath]() {
        return fetchMetadataForRemote(url);
    }));
}

void MainWindow::updatePlayPauseButton(bool playing)
{
    if (m_playPauseButton) {
        m_playPauseButton->setIcon(style()->standardIcon(playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    }
    if (m_playAction) {
        m_playAction->setText(playing ? tr("Pause") : tr("Play"));
    }
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

    updateMenuAvailability();
}

void MainWindow::updateControlsLayoutMode()
{
    if (m_compactOverrideEnabled) {
        updateControlsLayoutMode(m_compactOverrideValue);
    } else {
        updateControlsLayoutMode(width() < kCompactControlsThresholdPx);
    }
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
        m_slidersLayout->setHorizontalSpacing(compact ? 8 : 12);
        m_slidersLayout->setVerticalSpacing(compact ? 4 : 6);
        const int margin = compact ? 12 : 0;
        m_slidersLayout->setContentsMargins(margin, compact ? 4 : 0, margin, 0);
        m_slidersLayout->setColumnMinimumWidth(0, compact ? 0 : 70);
        m_slidersLayout->setColumnStretch(1, 1);
    }

    if (m_blurLabel) {
        m_blurLabel->setVisible(!compact);
        if (m_slidersLayout) {
            m_slidersLayout->setRowMinimumHeight(0, compact ? 0 : -1);
        }
    }

    if (m_blurSlider) {
        m_blurSlider->setMinimumWidth(compact ? 90 : 110);
    }

    if (m_volumeLabel) {
        m_volumeLabel->setText(compact ? tr("Vol") : tr("Volume"));
        if (m_slidersLayout) {
            m_slidersLayout->setRowMinimumHeight(1, compact ? 0 : -1);
        }
    }

    if (m_volumeSlider) {
        m_volumeSlider->setMinimumWidth(compact ? 110 : 120);
    }

    if (m_bassLabel) {
        m_bassLabel->setText(compact ? tr("Bass") : tr("Bass React"));
        m_bassLabel->setVisible(m_debugBassVisualizer && !compact);
        if (m_slidersLayout) {
            m_slidersLayout->setRowMinimumHeight(2, (m_debugBassVisualizer && !compact) ? -1 : 0);
        }
    }

    if (m_bassSlider) {
        m_bassSlider->setMinimumWidth(compact ? 110 : 140);
        m_bassSlider->setVisible(m_debugBassVisualizer);
    }

    if (m_bassVisualizer) {
        m_bassVisualizer->setVisible(m_debugBassVisualizer && !compact);
    }

    updateRepeatButton();
    refreshPlaylistStyles();
    if (m_viewCompactAction) {
        const QSignalBlocker blocker(m_viewCompactAction);
        if (m_compactOverrideEnabled) {
            m_viewCompactAction->setChecked(m_compactOverrideValue);
        } else {
            m_viewCompactAction->setChecked(false);
        }
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
        refreshNextLabel();
        updateTransportAvailability();
        return;
    }

    refreshNowPlayingLabel();
    refreshNextLabel();
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

void MainWindow::refreshNextLabel()
{
    if (!m_nextLabel) {
        return;
    }

    if (m_currentIndex < 0 || m_tracks.isEmpty()) {
        setNextLabelText(-1);
        return;
    }

    setNextLabelText(resolveNextIndex());
}

void MainWindow::setNextLabelText(int nextIndex)
{
    if (!m_nextLabel) {
        return;
    }

    if (nextIndex >= 0 && nextIndex < m_tracks.size()) {
        const QString nextTitle = displayTitleForEntry(m_tracks.at(nextIndex));
        m_nextLabel->setText(tr("Next: %1").arg(nextTitle));
        m_nextLabel->setToolTip(nextTitle);
    } else {
        m_nextLabel->setText(tr("Next: --"));
        m_nextLabel->setToolTip(QString());
    }
}

void MainWindow::invalidateNextIndexCache()
{
    m_nextIndexCacheValid = false;
    m_nextIndexCache = -1;
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
    const QUrl url = QUrl::fromUserInput(filePath);
    if (url.isValid() && !url.isRelative() && !url.isLocalFile() && !url.scheme().isEmpty()) {
        return url.toString(QUrl::FullyEncoded);
    }

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
