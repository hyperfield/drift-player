#pragma once

#include <QMainWindow>
#include <QProcess>
#include <QSet>
#include <QSettings>
#include <QTimer>
#include <QString>
#include <QVector>
#include <QQueue>
#include <QFutureWatcher>

class QListWidget;
class QPushButton;
class QSlider;
class QLabel;
class QToolButton;
class QListWidgetItem;
class QGraphicsOpacityEffect;
class QPropertyAnimation;
class QWidget;
class QHBoxLayout;
class QResizeEvent;
class QFileSystemWatcher;

struct TrackEntry
{
    QString title;
    QString filePath;
    QString normalizedPath;
    double durationSeconds = -1.0;
};

enum class RepeatMode
{
    None,
    All,
    One
};

class VideoBackgroundWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void handleAddMedia();
    void handleAddDialogFiles(const QStringList &files);
    void handleAddDialogClosed(int result);
    void handleAddDialogHelperFinished(int exitCode, QProcess::ExitStatus status);
    void handleAddDialogHelperError(QProcess::ProcessError error);
    void handlePlayPause();
    void handlePlayNext();
    void handlePlayPrevious();
    void handlePlaylistActivated(QListWidgetItem *item);
    void handleShuffleToggled();
    void handleRepeatMode();
    void handlePlaybackStateChanged(bool playing);
    void handlePlaybackFinished();
    void handleVolumeChanged(int value);
    void handleBlurChanged(int value);
    void handleProgressSliderPressed();
    void handleProgressSliderReleased();
    void handleProgressSliderMoved(int value);
    void handlePositionChanged(double position, double duration);
    void handleWatchedFileChanged(const QString &path);

private:
    void setupUi();
    void loadSettings();
    void saveSettings();
    bool addTrack(const QString &filePath);
    void processSelectedFiles(const QStringList &files);
    bool trackExists(const QString &filePath) const;
    void playTrack(int index);
    [[nodiscard]] int resolveNextIndex() const;
    [[nodiscard]] int resolvePreviousIndex() const;
    void updatePlayPauseButton(bool playing);
    void updateRepeatButton();
    void updateNowPlaying(const QString &filePath);
    void updateTransportAvailability();
    void updateControlsLayoutMode(bool compact);
    void updateControlsLayoutMode();
    void setWidgetOpacity(QGraphicsOpacityEffect *effect, QPropertyAnimation *animation, qreal value, int durationMs = 250);
    bool handleFadeEvent(QObject *watched, QEvent *event, QWidget *container, QTimer &timer,
                         QGraphicsOpacityEffect *effect, QPropertyAnimation *animation);
    [[nodiscard]] bool isCursorInside(QWidget *widget) const;
    [[nodiscard]] QString normalizedPathFor(const QString &filePath) const;
    [[nodiscard]] QString resolveDialogHelperPath() const;
    void cleanupAddDialogProcess();
    void startWatchingTrack(const QString &normalizedPath);
    void stopWatchingTrack(const QString &normalizedPath);
    void removeTrackByNormalizedPath(const QString &normalizedPath);
    void removeTrackAt(int index);
    void restorePlaylistState();
    void savePlaylistState();
    void updatePlaylistItem(int index);
    void enqueueDurationProbe(const QString &normalizedPath);
    void processDurationQueue();
    void finalizeDurationFor(const QString &normalizedPath, double seconds);
    void logTracks(const char *tag) const;

    bool eventFilter(QObject *watched, QEvent *event) override;

    VideoBackgroundWidget *m_videoWidget = nullptr;
    QListWidget *m_playlist = nullptr;
    QPushButton *m_addButton = nullptr;
    QToolButton *m_playPauseButton = nullptr;
    QToolButton *m_nextButton = nullptr;
    QToolButton *m_prevButton = nullptr;
    QToolButton *m_shuffleButton = nullptr;
    QToolButton *m_repeatButton = nullptr;
    QSlider *m_volumeSlider = nullptr;
    QSlider *m_blurSlider = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_blurLabel = nullptr;
    QLabel *m_volumeLabel = nullptr;
    QSlider *m_progressSlider = nullptr;
    QLabel *m_timeLabel = nullptr;
    QWidget *m_controlsContainer = nullptr;
    QWidget *m_progressContainer = nullptr;
    QHBoxLayout *m_actionsLayout = nullptr;
    QHBoxLayout *m_slidersLayout = nullptr;
    QFileSystemWatcher *m_playlistWatcher = nullptr;
    QFutureWatcher<double> *m_durationFutureWatcher = nullptr;
    QQueue<QString> m_durationProbeQueue;
    QString m_durationProbeCurrent;

    QVector<TrackEntry> m_tracks;
    int m_currentIndex = -1;
    bool m_shuffleEnabled = false;
    RepeatMode m_repeatMode = RepeatMode::None;
    QSettings m_settings;
    QGraphicsOpacityEffect *m_playlistOpacity = nullptr;
    QPropertyAnimation *m_playlistFadeAnimation = nullptr;
    QTimer m_playlistFadeTimer;
    QGraphicsOpacityEffect *m_controlsOpacity = nullptr;
    QPropertyAnimation *m_controlsFadeAnimation = nullptr;
    QTimer m_controlsFadeTimer;
    QGraphicsOpacityEffect *m_progressOpacity = nullptr;
    QPropertyAnimation *m_progressFadeAnimation = nullptr;
    QTimer m_progressFadeTimer;
    bool m_progressSliderPressed = false;
    double m_lastDuration = 0.0;
    QSet<QString> m_knownPaths;
    bool m_addDialogOpen = false;
    QProcess *m_addDialogProcess = nullptr;
    QString m_addDialogTempFile;
    bool m_compactControls = false;
    bool m_isRestoringPlaylist = false;
    QSet<QString> m_watchedPaths;
};
