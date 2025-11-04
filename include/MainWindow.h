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
class QAction;
class QListWidgetItem;
class QGraphicsOpacityEffect;
class QPropertyAnimation;
class QWidget;
class QHBoxLayout;
class QGridLayout;
class QResizeEvent;
class QFileSystemWatcher;
class QSlider;
class QLabel;
class BassVisualizerWidget;
class QEvent;

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
    void handleOpenUrl();
    void handleLoadPlaylist();
    void handleSavePlaylist();
    void handleUndo();
    void handleRedo();
    void handleRemoveSelectedTrack();
    void handleToggleCompactControls(bool checked);
    void handleToggleFullscreen(bool checked);
    void handleShowAboutDrift();
    void handleShowAboutQt();
    void handlePlaylistActivated(QListWidgetItem *item);
    void handleShuffleToggled();
    void handleRepeatMode();
    void handlePlaybackStateChanged(bool playing);
    void handlePlaybackFinished();
    void handleVolumeChanged(int value);
    void handleBlurChanged(int value);
    void handleBassThresholdChanged(int value);
    void handleProgressSliderPressed();
    void handleProgressSliderReleased();
    void handleProgressSliderMoved(int value);
    void handlePositionChanged(double position, double duration);
    void handleWatchedFileChanged(const QString &path);
    void handlePlaylistContextMenu(const QPoint &pos);
    void handleDeleteTrackFromPlaylist();
    void handleDeleteTrackFromDisk();

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
    void refreshPlaylistStyles();
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
    void requestRemoveTrack(int index);
    void updatePlaylistRowState(int index, QListWidgetItem *item);
    void updateBassEffectState();
    void refreshNextLabel();
    void setNextLabelText(int nextIndex);
    void invalidateNextIndexCache();
    void restorePlaybackState();
    [[nodiscard]] int indexForNormalizedPath(const QString &normalizedPath) const;
    void clearPendingRestore();
    void applyRestoredProgressToUi(double position, double durationSeconds);
    void updateMenuAvailability();

protected:
    void changeEvent(QEvent *event) override;

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
    QSlider *m_bassSlider = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_blurLabel = nullptr;
    QLabel *m_volumeLabel = nullptr;
    QLabel *m_bassLabel = nullptr;
    QSlider *m_progressSlider = nullptr;
    QLabel *m_timeLabel = nullptr;
    QLabel *m_nextLabel = nullptr;
    QAction *m_loadPlaylistAction = nullptr;
    QAction *m_savePlaylistAction = nullptr;
    QAction *m_openUrlAction = nullptr;
    QAction *m_addMediaAction = nullptr;
    QAction *m_quitAction = nullptr;
    QAction *m_aboutDriftAction = nullptr;
    QAction *m_aboutQtAction = nullptr;
    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;
    QAction *m_removeTrackAction = nullptr;
    QAction *m_viewCompactAction = nullptr;
    QAction *m_viewFullscreenAction = nullptr;
    QAction *m_playAction = nullptr;
    QAction *m_playNextAction = nullptr;
    QAction *m_playPreviousAction = nullptr;
    QWidget *m_controlsContainer = nullptr;
    QWidget *m_progressContainer = nullptr;
    QHBoxLayout *m_actionsLayout = nullptr;
    QGridLayout *m_slidersLayout = nullptr;
    QFileSystemWatcher *m_playlistWatcher = nullptr;
    QFutureWatcher<double> *m_durationFutureWatcher = nullptr;
    QQueue<QString> m_durationProbeQueue;
    QString m_durationProbeCurrent;
    BassVisualizerWidget *m_bassVisualizer = nullptr;
    bool m_debugBassVisualizer = false;

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
    bool m_compactOverrideEnabled = false;
    bool m_compactOverrideValue = false;
    bool m_isRestoringPlaylist = false;
    QSet<QString> m_watchedPaths;
    int m_contextMenuIndex = -1;
    mutable bool m_nextIndexCacheValid = false;
    mutable int m_nextIndexCache = -1;
    bool m_restorePlaybackPending = false;
    bool m_restorePositionApplied = false;
    double m_restoreSeekTarget = 0.0;
    bool m_restoreShouldPlay = false;
    QString m_restoreNormalizedPath;
};
