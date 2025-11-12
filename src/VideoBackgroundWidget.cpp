#include "VideoBackgroundWidget.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QPainter>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QSizePolicy>
#include <QSize>
#include <QThread>
#include <QMetaType>
#include <QtDebug>
#include <QRandomGenerator>
#include <QVariant>
#include <QVariantMap>
#include <QVariantList>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <atomic>

namespace
{
constexpr float kMaxBlurRadius = 24.0f;
constexpr float kQuadVertices[] = {
    -1.0f, -1.0f, 0.0f, 0.0f,
     1.0f, -1.0f, 1.0f, 0.0f,
    -1.0f,  1.0f, 0.0f, 1.0f,
     1.0f,  1.0f, 1.0f, 1.0f
};

const char *kBlurVertexShader = R"(#version 120
attribute vec2 aPosition;
attribute vec2 aTexCoord;
varying vec2 vTexCoord;
void main() {
    gl_Position = vec4(aPosition, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

const char *kBlurFragmentHorizontal = R"(#version 120
uniform sampler2D uTexture;
uniform float uTexelSize;
varying vec2 vTexCoord;
void main() {
    const float weights[7] = float[7](
        0.196482,
        0.176213,
        0.120598,
        0.064759,
        0.027995,
        0.009300,
        0.002653
    );

    vec4 color = texture2D(uTexture, vTexCoord) * weights[0];
    vec2 offset = vec2(uTexelSize, 0.0);
    for (int i = 1; i < 7; ++i) {
        float w = weights[i];
        float o = float(i);
        color += texture2D(uTexture, vTexCoord + offset * o) * w;
        color += texture2D(uTexture, vTexCoord - offset * o) * w;
    }
    gl_FragColor = color;
}
)";

const char *kBlurFragmentVertical = R"(#version 120
uniform sampler2D uTexture;
uniform float uTexelSize;
varying vec2 vTexCoord;
void main() {
    const float weights[7] = float[7](
        0.196482,
        0.176213,
        0.120598,
        0.064759,
        0.027995,
        0.009300,
        0.002653
    );

    vec4 color = texture2D(uTexture, vTexCoord) * weights[0];
    vec2 offset = vec2(0.0, uTexelSize);
    for (int i = 1; i < 7; ++i) {
        float w = weights[i];
        float o = float(i);
        color += texture2D(uTexture, vTexCoord + offset * o) * w;
        color += texture2D(uTexture, vTexCoord - offset * o) * w;
    }
    gl_FragColor = color;
}
)";

QVariant mpvNodeToVariant(const mpv_node *node)
{
    if (!node) {
        return QVariant();
    }

    switch (node->format) {
    case MPV_FORMAT_STRING:
        return QString::fromUtf8(node->u.string ? node->u.string : "");
    case MPV_FORMAT_INT64:
        return static_cast<qlonglong>(node->u.int64);
    case MPV_FORMAT_DOUBLE:
        return node->u.double_;
    case MPV_FORMAT_FLAG:
        return static_cast<bool>(node->u.flag);
    case MPV_FORMAT_NODE_ARRAY: {
        QVariantList list;
        if (node->u.list) {
            for (int i = 0; i < node->u.list->num; ++i) {
                list.append(mpvNodeToVariant(&node->u.list->values[i]));
            }
        }
        return list;
    }
    case MPV_FORMAT_NODE_MAP: {
        QVariantMap map;
        if (node->u.list) {
            for (int i = 0; i < node->u.list->num; ++i) {
                const char *key = node->u.list->keys ? node->u.list->keys[i] : nullptr;
                if (!key) {
                    continue;
                }
                map.insert(QString::fromUtf8(key), mpvNodeToVariant(&node->u.list->values[i]));
            }
        }
        return map;
    }
    default:
        return QVariant();
    }
}

} // namespace

Q_DECLARE_METATYPE(MpvEventPayload);

class MpvEventWorker : public QObject
{
    Q_OBJECT

public:
    explicit MpvEventWorker(mpv_handle *handle, QObject *parent = nullptr)
        : QObject(parent)
        , m_handle(handle)
    {
    }

public slots:
    void process();
    void stop();

signals:
    void eventReady(const MpvEventPayload &payload);

private:
    mpv_handle *m_handle = nullptr;
    std::atomic_bool m_running{true};
};

void MpvEventWorker::process()
{
    while (m_running.load(std::memory_order_acquire)) {
        mpv_event *event = mpv_wait_event(m_handle, -1);
        if (!event) {
            continue;
        }

        MpvEventPayload payload;
        payload.id = event->event_id;

        if (event->event_id == MPV_EVENT_PROPERTY_CHANGE) {
            auto *prop = static_cast<mpv_event_property *>(event->data);
            if (prop && prop->name) {
                payload.propertyName = QByteArray(prop->name);
                payload.format = prop->format;
            }
        } else if (event->event_id == MPV_EVENT_END_FILE) {
            auto *end = static_cast<mpv_event_end_file *>(event->data);
            if (end) {
                payload.endFileReason = static_cast<int>(end->reason);
            }
        }

        spdlog::debug("mpv worker event id={} prop='{}' fmt={}", payload.id, payload.propertyName.constData(), payload.format);

        emit eventReady(payload);

        if (!m_running.load(std::memory_order_acquire) || event->event_id == MPV_EVENT_SHUTDOWN) {
            break;
        }
    }
}

void MpvEventWorker::stop()
{
    m_running.store(false, std::memory_order_release);
    if (m_handle) {
        mpv_wakeup(m_handle);
    }
}

VideoBackgroundWidget::VideoBackgroundWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(false);
    setMinimumSize(0, 0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    qRegisterMetaType<MpvEventPayload>("MpvEventPayload");

    m_positionTimer.setInterval(80);
    m_positionTimer.setSingleShot(false);
    connect(&m_positionTimer, &QTimer::timeout, this, &VideoBackgroundWidget::pollPlaybackPosition);

    m_bassTimer.setInterval(33);
    m_bassTimer.setSingleShot(false);
    connect(&m_bassTimer, &QTimer::timeout, this, [this]() {
        if (!isVisible()) {
            return;
        }

        m_bassPhase += 0.24;
        if (m_bassPhase > 2.0 * M_PI) {
            m_bassPhase -= 2.0 * M_PI;
        }

        const double base = (std::sin(m_bassPhase) + 1.0) * 0.5;
        const double modulation = (std::sin(m_bassPhase * 0.5 + 1.7) + 1.0) * 0.5;
        double amplitude = (0.65 * base + 0.35 * modulation);
        amplitude = std::clamp(amplitude, 0.0, 1.0);
        if (!m_bassEnabled) {
            amplitude = 0.0;
        }

        if (amplitude > m_bassThreshold) {
            const double intensity = (amplitude - m_bassThreshold);
            m_bassFlash = std::min(1.0, m_bassFlash + intensity * 0.5 + 0.05);

            m_bassStripes.clear();
            const int stripes = 2 + static_cast<int>(intensity * 6.0);
            for (int i = 0; i < stripes; ++i) {
                const double topRatio = QRandomGenerator::global()->generateDouble();
                const double heightRatio = std::clamp(intensity * (0.08 + QRandomGenerator::global()->generateDouble() * 0.17), 0.02, 0.4);
                m_bassStripes.append(QRectF(0.0, topRatio, 1.0, heightRatio));
            }
        } else {
            m_bassFlash *= 0.90;
            if (m_bassFlash < 0.015) {
                m_bassFlash = 0.0;
                m_bassStripes.clear();
                scheduleUpdate();
            }
        }

        if (m_bassFlash > 0.01) {
            scheduleUpdate();
        }
    });
    m_bassTimer.start();

    initializeMpv();
}

VideoBackgroundWidget::~VideoBackgroundWidget()
{
    m_positionTimer.stop();
    m_bassTimer.stop();
    makeCurrent();
    releaseBlurResources();
    removeMpvShader();
    releaseMpv();
    doneCurrent();
}

bool VideoBackgroundWidget::loadFile(const QString &filePath)
{
    if (!m_mpv) {
        spdlog::error("Cannot load file {}; mpv handle invalid", filePath);
        return false;
    }

    spdlog::info("Loading media {}", filePath);

    // ensure mpv sees pause command after the new file is ready
    int pausedFlag = 1;
    mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &pausedFlag);
    m_isPaused = true;

    m_ignoreStopEndFile = true;
    QByteArray encoded = QFile::encodeName(filePath);
    const char *loadCmd[] = {"loadfile", encoded.constData(), "replace", nullptr};
    if (mpv_command(m_mpv, loadCmd) < 0) {
        spdlog::error("mpv failed to load file {}", filePath);
        m_ignoreStopEndFile = false;
        return false;
    }
    spdlog::info("mpv loadfile issued for {}", filePath);

    m_currentPath = filePath;
    m_hasMedia = true;
    m_isPaused = true;
    m_position = 0.0;
    m_duration = 0.0;

    emit mediaLoaded(filePath);
    emit playbackStateChanged(false);
    emit positionChanged(m_position, m_duration);

    return true;
}

void VideoBackgroundWidget::play()
{
    spdlog::debug("VideoBackgroundWidget::play() current paused={}", m_isPaused);
    setPaused(false);
}

void VideoBackgroundWidget::pause()
{
    spdlog::debug("VideoBackgroundWidget::pause() current paused={}", m_isPaused);
    setPaused(true);
}

void VideoBackgroundWidget::setPaused(bool paused)
{
    if (!m_mpv || paused == m_isPaused) {
        return;
    }

    int flag = paused ? 1 : 0;
    const int rc = mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &flag);
    if (rc < 0) {
        spdlog::warn("Failed to set pause={} (rc={})", paused, rc);
        return;
    }

    m_isPaused = paused;
    spdlog::debug("Pause state updated paused={}", m_isPaused);
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

void VideoBackgroundWidget::setBlurAmount(float amount)
{
    amount = std::clamp(amount, 0.0f, 1.0f);
    if (std::fabs(amount - m_blurAmount) < 0.001f) {
        return;
    }

    m_blurAmount = amount;
    spdlog::info("Setting blur amount to {}", m_blurAmount);
    if (m_useShaderBlur) {
        scheduleUpdate();
    } else {
        applyMpvShader();
    }
}

float VideoBackgroundWidget::blurAmount() const
{
    return m_blurAmount;
}

void VideoBackgroundWidget::setBassThreshold(double threshold)
{
    m_bassThreshold = std::clamp(threshold, 0.0, 1.0);
}

void VideoBackgroundWidget::setBassEnabled(bool enabled)
{
    m_bassEnabled = enabled;
    if (!m_bassEnabled) {
        m_bassFlash = 0.0;
        m_bassStripes.clear();
        scheduleUpdate();
    }
}

void VideoBackgroundWidget::setAutoStartOnLoad(bool autoStart)
{
    m_autoStartOnLoad = autoStart;
}

void VideoBackgroundWidget::requestFrame()
{
    spdlog::debug("VideoBackgroundWidget::requestFrame() scheduling update");
    scheduleUpdate();
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
    initializeBlurResources();
    ensureFramebuffers();

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
        spdlog::warn("Seek ignored seconds={} m_mpv={} hasMedia={}", seconds, static_cast<bool>(m_mpv), m_hasMedia);
        return;
    }

    if (seconds < 0.0) {
        seconds = 0.0;
    }

    const int rc = mpv_set_property(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &seconds);
    if (rc < 0) {
        spdlog::warn("Failed to seek to {} (rc={})", seconds, rc);
    } else {
        spdlog::debug("Seek requested to {} (rc={})", seconds, rc);
    }
}

void VideoBackgroundWidget::paintGL()
{
    if (!m_mpvRender) {
        spdlog::warn("paintGL: render context missing");
        return;
    }

    const qreal dpr = devicePixelRatioF();
    const QSize fbSize = QSize(width(), height()) * dpr;
    ensureFramebuffers();
    const bool useBlur = m_useShaderBlur && m_blurResourcesReady && m_sourceFbo && m_blurFbo && m_blurAmount > 0.01f;
    const int targetWidth = useBlur ? m_sourceFbo->width() : fbSize.width();
    const int targetHeight = useBlur ? m_sourceFbo->height() : fbSize.height();

    if (useBlur) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_sourceFbo->handle());
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    }

    glViewport(0, 0, targetWidth, targetHeight);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    mpv_opengl_fbo fbo{};
    fbo.fbo = static_cast<int>(useBlur ? m_sourceFbo->handle() : defaultFramebufferObject());
    fbo.w = targetWidth;
    fbo.h = targetHeight;
    fbo.internal_format = 0;

    int flipY = 1;

    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flipY},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };

    const int renderRc = mpv_render_context_render(m_mpvRender, params);
    if (renderRc < 0) {
        spdlog::warn("mpv failed to render frame rc={}", renderRc);
        return;
    }
    spdlog::debug("paintGL: rendered frame {}x{} blur={}", targetWidth, targetHeight, useBlur);

    if (useBlur) {
        renderBlurPass();
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
        glViewport(0, 0, fbSize.width(), fbSize.height());
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(0, 0, 0, 110));

    if (m_bassFlash > 0.01) {
        const double intensity = std::clamp(m_bassFlash, 0.0, 1.0);
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setOpacity(intensity * 0.45);
        painter.fillRect(rect(), QColor(140, 80, 255, 120));
        painter.setOpacity(intensity * 0.35);
        painter.setPen(Qt::NoPen);
        painter.setCompositionMode(QPainter::CompositionMode_Screen);
        const double totalHeight = static_cast<double>(height());
        const double totalWidth = static_cast<double>(width());
        for (const QRectF &stripe : m_bassStripes) {
            double top = stripe.y() * totalHeight;
            double h = stripe.height() * totalHeight;
            top = std::clamp(top, 0.0, totalHeight);
            h = std::max(4.0, std::min(h, totalHeight - top));
            QRectF drawRect(0.0, top, totalWidth, h);
            painter.fillRect(drawRect, QColor(255, 255, 255, 140));
        }
        painter.restore();
    }
}

void VideoBackgroundWidget::resizeGL(int w, int h)
{
    Q_UNUSED(w)
    Q_UNUSED(h)
    ensureFramebuffers();
    scheduleUpdate();
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
        spdlog::error("Failed to create mpv handle");
        return;
    }

    mpv_set_option_string(m_mpv, "terminal", "no");
    mpv_set_option_string(m_mpv, "keep-open", "yes");
    mpv_set_option_string(m_mpv, "vo", "libmpv");
    mpv_set_option_string(m_mpv, "video-sync", "display-resample");
    mpv_set_option_string(m_mpv, "osc", "no");
    mpv_set_option_string(m_mpv, "force-window", "no");
    mpv_set_option_string(m_mpv, "hwdec", "auto-safe");

    if (mpv_initialize(m_mpv) < 0) {
        spdlog::error("Failed to initialize mpv");
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
        return;
    }

    stopMpvEventThread();

    m_mpvEventThread = new QThread;
    m_mpvEventThread->setObjectName(QStringLiteral("MpvEventThread"));
    m_mpvEventWorker = new MpvEventWorker(m_mpv);
    m_mpvEventWorker->moveToThread(m_mpvEventThread);

    connect(m_mpvEventThread, &QThread::started, m_mpvEventWorker, &MpvEventWorker::process);
    connect(m_mpvEventWorker, &MpvEventWorker::eventReady, this, &VideoBackgroundWidget::handleMpvEventPayload, Qt::QueuedConnection);

    m_mpvEventThread->start();

    mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "eof-reached", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "hwdec-current", MPV_FORMAT_STRING);
    mpv_observe_property(m_mpv, 0, "metadata", MPV_FORMAT_NODE);
    mpv_observe_property(m_mpv, 0, "media-title", MPV_FORMAT_STRING);
    updateHardwareLogging();
}

void VideoBackgroundWidget::releaseMpv()
{
    stopMpvEventThread();

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

void VideoBackgroundWidget::stopMpvEventThread()
{
    if (!m_mpvEventThread) {
        return;
    }

    if (m_mpvEventWorker) {
        m_mpvEventWorker->stop();
    }

    m_mpvEventThread->quit();
    m_mpvEventThread->wait();

    m_mpvEventThread->deleteLater();
    m_mpvEventThread = nullptr;
}

void VideoBackgroundWidget::handleMpvEventPayload(const MpvEventPayload &payload)
{
    if (!m_mpv) {
        return;
    }

    handleMpvEvent(payload);
}

void VideoBackgroundWidget::handleMpvEvent(const MpvEventPayload &payload)
{
    switch (payload.id) {
    case MPV_EVENT_FILE_LOADED: {
        spdlog::debug("MPV_EVENT_FILE_LOADED path='{}' autoStart={}", m_currentPath.toStdString(), m_autoStartOnLoad);
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
        spdlog::debug("File loaded duration={} position={} paused={}", m_duration, m_position, m_isPaused);

        emit positionChanged(m_position, m_duration);
        updateHardwareLogging();
        if (!m_isPaused) {
            m_positionTimer.start();
        }
        if (m_autoStartOnLoad) {
            int pausedFlag = 0;
            if (mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &pausedFlag) >= 0) {
                if (m_isPaused) {
                    m_isPaused = false;
                    emit playbackStateChanged(true);
                }
                if (!m_positionTimer.isActive()) {
                    m_positionTimer.start();
                }
            }
        } else {
            int pausedFlag = 1;
            mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &pausedFlag);
            if (!m_isPaused) {
                m_isPaused = true;
                emit playbackStateChanged(false);
            }
            m_positionTimer.stop();
        }
        scheduleUpdate();
        m_autoStartOnLoad = true;
        m_ignoreStopEndFile = false;
        break;
    }
    case MPV_EVENT_END_FILE: {
        const auto reason = static_cast<mpv_end_file_reason>(payload.endFileReason);
        spdlog::info("MPV end file reason={} ignoreStop={}", static_cast<int>(reason), m_ignoreStopEndFile);
        if (reason == MPV_END_FILE_REASON_STOP) {
            if (m_ignoreStopEndFile) {
                m_ignoreStopEndFile = false;
                break;
            }
        } else {
            m_ignoreStopEndFile = false;
        }

        m_hasMedia = false;
        m_isPaused = true;
        m_position = 0.0;
        emit playbackFinished();
        emit playbackStateChanged(false);
        emit positionChanged(m_position, m_duration);
        m_positionTimer.stop();
        break;
    }
    case MPV_EVENT_PROPERTY_CHANGE: {
        if (payload.propertyName.isEmpty()) {
            break;
        }

        const QByteArray &name = payload.propertyName;
        if (name == "pause" && payload.format == MPV_FORMAT_FLAG) {
            int pausedFlag = 0;
            if (mpv_get_property(m_mpv, name.constData(), MPV_FORMAT_FLAG, &pausedFlag) >= 0) {
                bool paused = pausedFlag != 0;
                if (paused != m_isPaused) {
                    spdlog::debug("pause property changed -> {}", paused);
                    m_isPaused = paused;
                    emit playbackStateChanged(!m_isPaused);
                    if (m_isPaused) {
                        m_positionTimer.stop();
                    } else {
                        m_positionTimer.start();
                    }
                }
            }
        } else if (name == "metadata") {
            mpv_node node{};
            if (mpv_get_property(m_mpv, name.constData(), MPV_FORMAT_NODE, &node) >= 0) {
                QVariant variant = mpvNodeToVariant(&node);
                mpv_free_node_contents(&node);
                if (variant.canConvert<QVariantMap>()) {
                    emit metadataChanged(variant.toMap());
                }
            }
        } else if (name == "media-title" && payload.format == MPV_FORMAT_STRING) {
            char *value = nullptr;
            if (mpv_get_property(m_mpv, name.constData(), MPV_FORMAT_STRING, &value) >= 0) {
                QString title = QString::fromUtf8(value ? value : "");
                mpv_free(value);
                emit mediaTitleChanged(title);
            }
        } else if (name == "eof-reached" && payload.format == MPV_FORMAT_FLAG) {
            int eofFlag = 0;
            if (mpv_get_property(m_mpv, name.constData(), MPV_FORMAT_FLAG, &eofFlag) >= 0) {
                bool reached = eofFlag != 0;
                spdlog::info("mpv eof-reached={} ignoreStop={}", reached, m_ignoreStopEndFile);
                if (reached) {
                    m_ignoreStopEndFile = false;
                    if (m_hasMedia) {
                        m_hasMedia = false;
                        m_isPaused = true;
                        m_positionTimer.stop();
                        emit playbackFinished();
                        emit playbackStateChanged(false);
                    }
                }
            }
        } else if (name == "duration" && payload.format == MPV_FORMAT_DOUBLE) {
            double duration = 0.0;
            if (mpv_get_property(m_mpv, name.constData(), MPV_FORMAT_DOUBLE, &duration) >= 0 && std::isfinite(duration)) {
                if (duration < 0.0) {
                    duration = 0.0;
                }
            } else {
                duration = 0.0;
            }
            spdlog::debug("duration property change -> {}", duration);

            if (std::fabs(m_duration - duration) > 0.01) {
                m_duration = duration;
                emit positionChanged(m_position, m_duration);
            }
        } else if (name == "time-pos" && payload.format == MPV_FORMAT_DOUBLE) {
            double position = 0.0;
            if (mpv_get_property(m_mpv, name.constData(), MPV_FORMAT_DOUBLE, &position) >= 0 && std::isfinite(position)) {
                if (position < 0.0) {
                    position = 0.0;
                }
            } else {
                position = 0.0;
            }

            m_position = position;
            spdlog::debug("time-pos property change -> {}", m_position);
            emit positionChanged(m_position, m_duration);
        } else if (name == "hwdec-current" && payload.format == MPV_FORMAT_STRING) {
            char *value = nullptr;
            if (mpv_get_property(m_mpv, name.constData(), MPV_FORMAT_STRING, &value) >= 0) {
                spdlog::info("mpv hwdec-current property changed to '{}'", value ? value : "");
                mpv_free(value);
                updateHardwareLogging();
            }
        }
        break;
    }
    default:
        break;
    }
}

void VideoBackgroundWidget::pollPlaybackPosition()
{
    if (!m_mpv || !m_hasMedia) {
        return;
    }

    double duration = m_duration;
    double position = m_position;

    if (mpv_get_property(m_mpv, "duration", MPV_FORMAT_DOUBLE, &duration) < 0 || !std::isfinite(duration) || duration < 0.0) {
        duration = 0.0;
    }

    if (mpv_get_property(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &position) < 0 || !std::isfinite(position) || position < 0.0) {
        position = 0.0;
    }

    bool durationChanged = std::fabs(m_duration - duration) > 0.01;
    bool positionDelta = std::fabs(m_position - position) > 0.005;

    if (durationChanged || positionDelta) {
        m_duration = duration;
        m_position = position;
        emit positionChanged(m_position, m_duration);
    }
}

void VideoBackgroundWidget::scheduleUpdate()
{
    QMetaObject::invokeMethod(this, &VideoBackgroundWidget::handleUpdate, Qt::QueuedConnection);
}

void VideoBackgroundWidget::initializeBlurResources()
{
    if (m_blurResourcesReady) {
        return;
    }

    m_fullscreenVbo = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer);
    if (!m_fullscreenVbo->create()) {
        qWarning() << "Failed to create blur VBO";
        m_fullscreenVbo.reset();
        return;
    }
    m_fullscreenVbo->bind();
    m_fullscreenVbo->allocate(kQuadVertices, sizeof(kQuadVertices));

    m_fullscreenVao = std::make_unique<QOpenGLVertexArrayObject>();
    if (!m_fullscreenVao->create()) {
        qWarning() << "Failed to create blur VAO";
        m_fullscreenVbo->release();
        m_fullscreenVbo->destroy();
        m_fullscreenVbo.reset();
        return;
    }

    {
        QOpenGLVertexArrayObject::Binder binder(m_fullscreenVao.get());
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void *>(2 * sizeof(float)));
    }
    m_fullscreenVbo->release();

    m_blurProgramHorizontal = std::make_unique<QOpenGLShaderProgram>();
    m_blurProgramHorizontal->addShaderFromSourceCode(QOpenGLShader::Vertex, kBlurVertexShader);
    m_blurProgramHorizontal->addShaderFromSourceCode(QOpenGLShader::Fragment, kBlurFragmentHorizontal);
    m_blurProgramHorizontal->bindAttributeLocation("aPosition", 0);
    m_blurProgramHorizontal->bindAttributeLocation("aTexCoord", 1);
    if (!m_blurProgramHorizontal->link()) {
        qWarning() << "Failed to link horizontal blur shader:" << m_blurProgramHorizontal->log();
        releaseBlurResources();
        return;
    }

    m_blurProgramVertical = std::make_unique<QOpenGLShaderProgram>();
    m_blurProgramVertical->addShaderFromSourceCode(QOpenGLShader::Vertex, kBlurVertexShader);
    m_blurProgramVertical->addShaderFromSourceCode(QOpenGLShader::Fragment, kBlurFragmentVertical);
    m_blurProgramVertical->bindAttributeLocation("aPosition", 0);
    m_blurProgramVertical->bindAttributeLocation("aTexCoord", 1);
    if (!m_blurProgramVertical->link()) {
        qWarning() << "Failed to link vertical blur shader:" << m_blurProgramVertical->log();
        releaseBlurResources();
        return;
    }

    m_blurResourcesReady = true;
}

void VideoBackgroundWidget::releaseBlurResources()
{
    m_sourceFbo.reset();
    m_blurFbo.reset();
    if (m_fullscreenVbo) {
        if (m_fullscreenVbo->isCreated()) {
            m_fullscreenVbo->destroy();
        }
        m_fullscreenVbo.reset();
    }
    if (m_fullscreenVao) {
        if (m_fullscreenVao->isCreated()) {
            m_fullscreenVao->destroy();
        }
        m_fullscreenVao.reset();
    }
    m_blurProgramHorizontal.reset();
    m_blurProgramVertical.reset();
    m_blurResourcesReady = false;
}

void VideoBackgroundWidget::ensureFramebuffers()
{
    if (!m_blurResourcesReady || !m_useShaderBlur) {
        return;
    }

    QSize pixelSize = QSize(width(), height()) * devicePixelRatioF();
    if (pixelSize.isEmpty()) {
        m_sourceFbo.reset();
        m_blurFbo.reset();
        return;
    }

    if (m_sourceFbo && m_sourceFbo->size() == pixelSize) {
        return;
    }

    QOpenGLFramebufferObjectFormat format;
    format.setAttachment(QOpenGLFramebufferObject::NoAttachment);
    format.setTextureTarget(GL_TEXTURE_2D);
    format.setInternalTextureFormat(GL_RGBA8);

    m_sourceFbo = std::make_unique<QOpenGLFramebufferObject>(pixelSize, format);
    m_blurFbo = std::make_unique<QOpenGLFramebufferObject>(pixelSize, format);
}

void VideoBackgroundWidget::renderBlurPass()
{
    if (!m_sourceFbo || !m_blurFbo || !m_blurProgramHorizontal || !m_blurProgramVertical || !m_fullscreenVao) {
        return;
    }

    glDisable(GL_DEPTH_TEST);

    QOpenGLVertexArrayObject::Binder binder(m_fullscreenVao.get());

    glBindFramebuffer(GL_FRAMEBUFFER, m_blurFbo->handle());
    glViewport(0, 0, m_blurFbo->width(), m_blurFbo->height());
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    m_blurProgramHorizontal->bind();
    m_blurProgramHorizontal->setUniformValue("uTexture", 0);
    const float blurScale = std::max(m_blurAmount * kMaxBlurRadius, 0.001f);
    m_blurProgramHorizontal->setUniformValue("uTexelSize", blurScale / static_cast<float>(m_sourceFbo->width()));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_sourceFbo->texture());
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    m_blurProgramHorizontal->release();

    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    const qreal dpr = devicePixelRatioF();
    glViewport(0, 0, static_cast<GLint>(width() * dpr), static_cast<GLint>(height() * dpr));
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    m_blurProgramVertical->bind();
    m_blurProgramVertical->setUniformValue("uTexture", 0);
    m_blurProgramVertical->setUniformValue("uTexelSize", blurScale / static_cast<float>(m_blurFbo->height()));
    glBindTexture(GL_TEXTURE_2D, m_blurFbo->texture());
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    m_blurProgramVertical->release();

    glBindTexture(GL_TEXTURE_2D, 0);
}

void VideoBackgroundWidget::updateHardwareLogging()
{
    bool hwAccel = queryHardwareDecoding(&m_hwdecKnown);
    if (!m_hwdecKnown) {
        return;
    }

    if (!m_hwdecKnown || hwAccel != m_lastHwdecState) {
        spdlog::info("Hardware decoding detected: {}.", hwAccel ? "yes" : "no");
        m_lastHwdecState = hwAccel;
    }

    selectBlurStrategy();
}

bool VideoBackgroundWidget::queryHardwareDecoding(bool *known) const
{
    if (!m_mpv) {
        if (known) {
            *known = false;
        }
        return false;
    }

    char *value = mpv_get_property_string(m_mpv, "hwdec-current");
    if (!value) {
        if (known) {
            *known = false;
        }
        return false;
    }

    bool hasHw = value[0] != '\0' && std::strcmp(value, "no") != 0;
    spdlog::info("mpv hwdec-current='{}'", value);
    mpv_free(value);
    if (known) {
        *known = true;
    }
    return hasHw;
}

bool VideoBackgroundWidget::ensureMpvShaderFile(float radius)
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) {
        spdlog::warn("Cannot determine writable location for mpv shader");
        return false;
    }

    QDir dir(base);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        spdlog::warn("Failed to create directory {}", base);
        return false;
    }

    m_mpvShaderPath = dir.absoluteFilePath(QStringLiteral("blur_hook.glsl"));
    QFile file(m_mpvShaderPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        spdlog::warn("Failed to open shader file {}", m_mpvShaderPath);
        return false;
    }

    const float clamped = std::clamp(radius, 0.5f, kMaxBlurRadius);
    const QString shader = QStringLiteral(R"GLSL(
!HOOK MAIN
!BIND HOOKED
!DESC Horizontal blur
#define BLUR_RADIUS %1
vec4 hook() {
    vec2 texel = vec2(BLUR_RADIUS / HOOKED_size.x, 0.0);
    vec2 tc = HOOKED_texcoord;
    vec4 color = texture(HOOKED_tex, tc) * 0.29411765;
    color += texture(HOOKED_tex, tc + texel) * 0.23529412;
    color += texture(HOOKED_tex, tc - texel) * 0.23529412;
    color += texture(HOOKED_tex, tc + 2.0 * texel) * 0.11764706;
    color += texture(HOOKED_tex, tc - 2.0 * texel) * 0.11764706;
    return color;
}

!HOOK MAIN
!BIND PREV
!DESC Vertical blur
#define BLUR_RADIUS %1
vec4 hook() {
    vec2 texel = vec2(0.0, BLUR_RADIUS / PREV_size.y);
    vec2 tc = PREV_texcoord;
    vec4 color = texture(PREV_tex, tc) * 0.29411765;
    color += texture(PREV_tex, tc + texel) * 0.23529412;
    color += texture(PREV_tex, tc - texel) * 0.23529412;
    color += texture(PREV_tex, tc + 2.0 * texel) * 0.11764706;
    color += texture(PREV_tex, tc - 2.0 * texel) * 0.11764706;
    return color;
}
)GLSL").arg(clamped, 0, 'f', 3);

    file.write(shader.toUtf8());
    file.close();
    return true;
}

void VideoBackgroundWidget::applyMpvShader()
{
    if (!m_mpv) {
        return;
    }

    if (m_blurAmount <= 0.01f) {
        removeMpvShader();
        return;
    }

    if (!ensureMpvShaderFile(m_blurAmount * kMaxBlurRadius)) {
        return;
    }

    QByteArray encoded = QFile::encodeName(m_mpvShaderPath);
    if (mpv_set_property_string(m_mpv, "glsl-shaders", encoded.constData()) < 0) {
        spdlog::warn("Failed to apply mpv shader {}", m_mpvShaderPath);
        return;
    }

    if (!m_mpvShaderActive) {
        spdlog::info("Applied mpv GLSL blur shader");
    }
    m_mpvShaderActive = true;
}

void VideoBackgroundWidget::removeMpvShader()
{
    if (!m_mpv || !m_mpvShaderActive) {
        return;
    }

    if (mpv_set_property_string(m_mpv, "glsl-shaders", "") == 0) {
        spdlog::info("Cleared mpv GLSL shader");
    }
    m_mpvShaderActive = false;
}
void VideoBackgroundWidget::selectBlurStrategy()
{
    bool preferShader = true; // reliable path for now

    if (m_useShaderBlur != preferShader) {
        m_useShaderBlur = preferShader;

        QMetaObject::invokeMethod(this, [this]() {
            if (!context()) {
                return;
            }
            QOpenGLContext *current = QOpenGLContext::currentContext();
            if (current != context()) {
                makeCurrent();
                ensureFramebuffers();
                doneCurrent();
            } else {
                ensureFramebuffers();
            }
            emit blurModeChanged(m_useShaderBlur);
            scheduleUpdate();
        }, Qt::QueuedConnection);
    }

    if (m_useShaderBlur) {
        removeMpvShader();
    } else {
        applyMpvShader();
    }
}

#include "VideoBackgroundWidget.moc"
