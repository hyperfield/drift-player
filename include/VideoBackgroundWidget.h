#pragma once

#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QScopedPointer>
#include <QByteArray>
#include <QTimer>
#include <memory>

extern "C" {
#include <mpv/client.h>
#include <mpv/render_gl.h>
}

class QThread;
class QOpenGLBuffer;
class QOpenGLFramebufferObject;
class QOpenGLShaderProgram;
class QOpenGLVertexArrayObject;
class MpvEventWorker;

struct MpvEventPayload
{
    mpv_event_id id = MPV_EVENT_NONE;
    QByteArray propertyName;
    mpv_format format = MPV_FORMAT_NONE;
    int endFileReason = -1;
};

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
    void setBlurAmount(float amount);
    float blurAmount() const;
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
    void blurModeChanged(bool shaderBlurActive);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

private slots:
    void handleUpdate();
    void handleMpvEventPayload(const MpvEventPayload &payload);

private:
    static void onMpvUpdate(void *ctx);
    static void *getMpvProcAddress(void *ctx, const char *name);

    void initializeMpv();
    void releaseMpv();
    void stopMpvEventThread();
    void handleMpvEvent(const MpvEventPayload &payload);
    void scheduleUpdate();
    void initializeBlurResources();
    void releaseBlurResources();
    void ensureFramebuffers();
    void renderBlurPass();
    void ensureVertexState();
    void selectBlurStrategy();
    void updateHardwareLogging();
    bool queryHardwareDecoding(bool *known = nullptr) const;
    bool ensureMpvShaderFile(float radius);
    void applyMpvShader();
    void removeMpvShader();
    void pollPlaybackPosition();

    mpv_handle *m_mpv = nullptr;
    mpv_render_context *m_mpvRender = nullptr;
    QThread *m_mpvEventThread = nullptr;
    class MpvEventWorker *m_mpvEventWorker = nullptr;
    QString m_currentPath;
    bool m_hasMedia = false;
    bool m_isPaused = true;
    bool m_renderInitialized = false;
    double m_duration = 0.0;
    double m_position = 0.0;
    std::unique_ptr<QOpenGLFramebufferObject> m_sourceFbo;
    std::unique_ptr<QOpenGLFramebufferObject> m_blurFbo;
    std::unique_ptr<QOpenGLShaderProgram> m_blurProgramHorizontal;
    std::unique_ptr<QOpenGLShaderProgram> m_blurProgramVertical;
    std::unique_ptr<QOpenGLBuffer> m_fullscreenVbo;
    std::unique_ptr<QOpenGLVertexArrayObject> m_fullscreenVao;
    bool m_blurResourcesReady = false;
    float m_blurAmount = 0.7f;
    bool m_useShaderBlur = true;
    bool m_hwdecKnown = false;
    bool m_lastHwdecState = false;
    QString m_mpvShaderPath;
    bool m_mpvShaderActive = false;
    QTimer m_positionTimer;
    bool m_ignoreStopEndFile = false;
};
