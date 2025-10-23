#include "VideoBackgroundWidget.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QOpenGLContext>
#include <QPainter>
#include <QSurfaceFormat>
#include <QSizePolicy>
#include <QSize>
#include <QtDebug>

#include <cmath>
#include <cstring>

namespace
{
constexpr int kEventPollIntervalMs = 16;

void appendBlurFilter(mpv_handle *handle)
{
    if (!handle) {
        return;
    }

    static const char *removeExisting[] = {"vf", "remove", "@backgroundblur", nullptr};
    static const char *addFilter[] = {"vf", "add", "@backgroundblur:lavfi=[boxblur=12:8]", nullptr};

    mpv_command(handle, removeExisting);
    mpv_command(handle, addFilter);
}
} // namespace

VideoBackgroundWidget::VideoBackgroundWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(false);
    setMinimumSize(0, 0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    initializeMpv();

    if (m_mpv) {
        connect(&m_eventTimer, &QTimer::timeout, this, &VideoBackgroundWidget::processMpvEvents);
        m_eventTimer.start(kEventPollIntervalMs);
    }
}

VideoBackgroundWidget::~VideoBackgroundWidget()
{
    m_eventTimer.stop();
    releaseMpv();
}

bool VideoBackgroundWidget::loadFile(const QString &filePath)
{
    if (!m_mpv) {
        return false;
    }

    QByteArray encoded = QFile::encodeName(filePath);
    const char *loadCmd[] = {"loadfile", encoded.constData(), "replace", nullptr};
    if (mpv_command(m_mpv, loadCmd) < 0) {
        qWarning() << "mpv failed to load file" << filePath;
        return false;
    }

    appendBlurFilter(m_mpv);

    m_currentPath = filePath;
    m_hasMedia = true;
    m_isPaused = false;
    m_position = 0.0;
    m_duration = 0.0;

    emit mediaLoaded(filePath);
    emit playbackStateChanged(true);
    emit positionChanged(m_position, m_duration);

    return true;
}

void VideoBackgroundWidget::play()
{
    setPaused(false);
}

void VideoBackgroundWidget::pause()
{
    setPaused(true);
}

void VideoBackgroundWidget::setPaused(bool paused)
{
    if (!m_mpv || paused == m_isPaused) {
        return;
    }

    int flag = paused ? 1 : 0;
    if (mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &flag) < 0) {
        qWarning() << "Failed to set pause state";
        return;
    }

    m_isPaused = paused;
    emit playbackStateChanged(!m_isPaused);
}

void VideoBackgroundWidget::setVolume(int volume)
{
    if (!m_mpv) {
        return;
    }

    double vol = static_cast<double>(volume);
    if (mpv_set_property(m_mpv, "volume", MPV_FORMAT_DOUBLE, &vol) < 0) {
        qWarning() << "Failed to set volume";
    }
}

bool VideoBackgroundWidget::hasMedia() const
{
    return m_hasMedia;
}

bool VideoBackgroundWidget::isPaused() const
{
    return m_isPaused;
}

double VideoBackgroundWidget::duration() const
{
    return m_duration;
}

double VideoBackgroundWidget::position() const
{
    return m_position;
}

void VideoBackgroundWidget::initializeGL()
{
    initializeOpenGLFunctions();

    if (!m_mpv || m_renderInitialized) {
        return;
    }

    mpv_opengl_init_params initParams{};
    initParams.get_proc_address = &VideoBackgroundWidget::getMpvProcAddress;
    initParams.get_proc_address_ctx = this;

    const char *apiType = MPV_RENDER_API_TYPE_OPENGL;

    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(apiType)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &initParams},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };

    if (mpv_render_context_create(&m_mpvRender, m_mpv, params) < 0) {
        qWarning() << "Failed to create mpv render context";
        m_mpvRender = nullptr;
        return;
    }

    mpv_render_context_set_update_callback(m_mpvRender, &VideoBackgroundWidget::onMpvUpdate, this);
    m_renderInitialized = true;
}

void VideoBackgroundWidget::seek(double seconds)
{
    if (!m_mpv || !m_hasMedia) {
        return;
    }

    if (seconds < 0.0) {
        seconds = 0.0;
    }

    if (mpv_set_property(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &seconds) < 0) {
        qWarning() << "Failed to seek" << seconds;
    }
}

void VideoBackgroundWidget::paintGL()
{
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (!m_mpvRender) {
        return;
    }

    const qreal dpr = devicePixelRatioF();
    const QSize fbSize = QSize(width(), height()) * dpr;

    glViewport(0, 0, fbSize.width(), fbSize.height());

    mpv_opengl_fbo fbo{};
    fbo.fbo = static_cast<int>(defaultFramebufferObject());
    fbo.w = fbSize.width();
    fbo.h = fbSize.height();
    fbo.internal_format = 0;

    int flipY = 1;

    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flipY},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };

    if (mpv_render_context_render(m_mpvRender, params) < 0) {
        qWarning() << "mpv failed to render frame";
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(0, 0, 0, 110));
}

void VideoBackgroundWidget::resizeGL(int w, int h)
{
    Q_UNUSED(w)
    Q_UNUSED(h)
    scheduleUpdate();
}

void VideoBackgroundWidget::processMpvEvents()
{
    if (!m_mpv) {
        return;
    }

    while (true) {
        mpv_event *event = mpv_wait_event(m_mpv, 0);
        if (!event || event->event_id == MPV_EVENT_NONE) {
            break;
        }

        handleMpvEvent(event);
    }
}

void VideoBackgroundWidget::handleUpdate()
{
    update();
}

void VideoBackgroundWidget::onMpvUpdate(void *ctx)
{
    auto *self = static_cast<VideoBackgroundWidget *>(ctx);
    if (!self) {
        return;
    }

    self->scheduleUpdate();
}

void *VideoBackgroundWidget::getMpvProcAddress(void *ctx, const char *name)
{
    auto *self = static_cast<VideoBackgroundWidget *>(ctx);
    if (!self) {
        return nullptr;
    }

    QOpenGLContext *context = self->context();
    if (!context) {
        return nullptr;
    }

    QFunctionPointer proc = context->getProcAddress(QByteArray(name));
    return reinterpret_cast<void *>(proc);
}

void VideoBackgroundWidget::initializeMpv()
{
    m_mpv = mpv_create();
    if (!m_mpv) {
        qWarning() << "Failed to create mpv handle";
        return;
    }

    mpv_set_option_string(m_mpv, "terminal", "no");
    mpv_set_option_string(m_mpv, "keep-open", "yes");
    mpv_set_option_string(m_mpv, "vo", "libmpv");
    mpv_set_option_string(m_mpv, "video-sync", "display-resample");
    mpv_set_option_string(m_mpv, "osc", "no");
    mpv_set_option_string(m_mpv, "force-window", "no");

    if (mpv_initialize(m_mpv) < 0) {
        qWarning() << "Failed to initialize mpv";
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
        return;
    }

    mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
}

void VideoBackgroundWidget::releaseMpv()
{
    if (m_mpvRender) {
        makeCurrent();
        mpv_render_context_set_update_callback(m_mpvRender, nullptr, nullptr);
        mpv_render_context_free(m_mpvRender);
        m_mpvRender = nullptr;
        doneCurrent();
    }

    if (m_mpv) {
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
    }
}

void VideoBackgroundWidget::handleMpvEvent(mpv_event *event)
{
    switch (event->event_id) {
    case MPV_EVENT_FILE_LOADED:
        appendBlurFilter(m_mpv);
        if (m_mpv) {
            double duration = 0.0;
            if (mpv_get_property(m_mpv, "duration", MPV_FORMAT_DOUBLE, &duration) >= 0 && std::isfinite(duration)) {
                if (duration < 0.0) {
                    duration = 0.0;
                }
                m_duration = duration;
            } else {
                m_duration = 0.0;
            }

            double position = 0.0;
            if (mpv_get_property(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &position) >= 0 && std::isfinite(position)) {
                if (position < 0.0) {
                    position = 0.0;
                }
                m_position = position;
            } else {
                m_position = 0.0;
            }

            emit positionChanged(m_position, m_duration);
        }
        break;
    case MPV_EVENT_END_FILE:
        m_hasMedia = false;
        m_isPaused = true;
        m_position = 0.0;
        emit playbackFinished();
        emit playbackStateChanged(false);
        emit positionChanged(m_position, m_duration);
        break;
    case MPV_EVENT_PROPERTY_CHANGE: {
        auto *prop = static_cast<mpv_event_property *>(event->data);
        if (!prop || !prop->name) {
            break;
        }

        if (strcmp(prop->name, "pause") == 0 && prop->format == MPV_FORMAT_FLAG) {
            bool paused = prop->data && (*static_cast<int *>(prop->data) != 0);
            if (paused != m_isPaused) {
                m_isPaused = paused;
                emit playbackStateChanged(!m_isPaused);
            }
        } else if (strcmp(prop->name, "duration") == 0 && prop->format == MPV_FORMAT_DOUBLE) {
            double duration = prop->data ? *static_cast<double *>(prop->data) : 0.0;
            if (!std::isfinite(duration) || duration < 0.0) {
                duration = 0.0;
            }
            if (std::fabs(m_duration - duration) > 0.01) {
                m_duration = duration;
                emit positionChanged(m_position, m_duration);
            }
        } else if (strcmp(prop->name, "time-pos") == 0 && prop->format == MPV_FORMAT_DOUBLE) {
            double position = prop->data ? *static_cast<double *>(prop->data) : 0.0;
            if (!std::isfinite(position) || position < 0.0) {
                position = 0.0;
            }
            m_position = position;
            emit positionChanged(m_position, m_duration);
        }
        break;
    }
    default:
        break;
    }
}

void VideoBackgroundWidget::scheduleUpdate()
{
    QMetaObject::invokeMethod(this, &VideoBackgroundWidget::handleUpdate, Qt::QueuedConnection);
}
