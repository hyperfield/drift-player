#pragma once

#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QTimer>

extern "C" {
#include <mpv/client.h>
#include <mpv/render_gl.h>
}

class VideoBackgroundWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit VideoBackgroundWidget(QWidget *parent = nullptr);
    ~VideoBackgroundWidget() override;

    bool loadFile(const QString &filePath);
    void play();
    void pause();
    void setPaused(bool paused);
    void setVolume(int volume);
    void seek(double seconds);

    [[nodiscard]] bool hasMedia() const;
    [[nodiscard]] bool isPaused() const;
    [[nodiscard]] double duration() const;
    [[nodiscard]] double position() const;

signals:
    void mediaLoaded(const QString &path);
    void playbackStateChanged(bool playing);
    void playbackFinished();
    void positionChanged(double position, double duration);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

private slots:
    void processMpvEvents();
    void handleUpdate();

private:
    static void onMpvUpdate(void *ctx);
    static void *getMpvProcAddress(void *ctx, const char *name);

    void initializeMpv();
    void releaseMpv();
    void handleMpvEvent(mpv_event *event);
    void scheduleUpdate();

    mpv_handle *m_mpv = nullptr;
    mpv_render_context *m_mpvRender = nullptr;
    QTimer m_eventTimer;
    QString m_currentPath;
    bool m_hasMedia = false;
    bool m_isPaused = true;
    bool m_renderInitialized = false;
    double m_duration = 0.0;
    double m_position = 0.0;
};
