/*
    SPDX-FileCopyrightText: 2020 Aleix Pol Gonzalez <aleixpol@kde.org>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#include "screencasting.h"
#include "logging.h"
#include "qwayland-zkde-screencast-unstable-v1.h"
#include <KWayland/Client/output.h>
#include <KWayland/Client/plasmawindowmanagement.h>
#include <KWayland/Client/registry.h>
#include <QGuiApplication>
#include <QRect>
#include <QtWaylandClient/QWaylandClientExtensionTemplate>
#include <qpa/qplatformnativeinterface.h>
#include <qscreen.h>
#include <qtwaylandclientversion.h>

using namespace KWayland::Client;

class ScreencastingStreamPrivate : public QtWayland::zkde_screencast_stream_unstable_v1
{
public:
    ScreencastingStreamPrivate(ScreencastingStream *q)
        : q(q)
    {
    }
    ~ScreencastingStreamPrivate()
    {
        if (isInitialized()) {
            close();
        }
        q->deleteLater();
    }

    void zkde_screencast_stream_unstable_v1_created(uint32_t node) override
    {
        m_nodeId = node;
        Q_EMIT q->created(node);
    }

    void zkde_screencast_stream_unstable_v1_closed() override
    {
        Q_EMIT q->closed();
    }

    void zkde_screencast_stream_unstable_v1_failed(const QString &error) override
    {
        Q_EMIT q->failed(error);
    }

    std::optional<uint> m_nodeId;
    QPointer<ScreencastingStream> q;
};

ScreencastingStream::ScreencastingStream(QObject *parent)
    : QObject(parent)
    , d(new ScreencastingStreamPrivate(this))
{
}

ScreencastingStream::~ScreencastingStream() = default;

quint32 ScreencastingStream::nodeId() const
{
    Q_ASSERT(d->m_nodeId.has_value());
    return *d->m_nodeId;
}

class ScreencastingPrivate : public QWaylandClientExtensionTemplate<ScreencastingPrivate>, public QtWayland::zkde_screencast_unstable_v1
{
public:
    ScreencastingPrivate(Screencasting *q)
        : QWaylandClientExtensionTemplate<ScreencastingPrivate>(ZKDE_SCREENCAST_UNSTABLE_V1_STREAM_REGION_SINCE_VERSION)
        , q(q)
    {
#if QTWAYLANDCLIENT_VERSION >= QT_VERSION_CHECK(6, 2, 0)
        initialize();
#else
        // QWaylandClientExtensionTemplate invokes this with a QueuedConnection but we want it called immediately
        QMetaObject::invokeMethod(this, "addRegistryListener", Qt::DirectConnection);
#endif

        if (!isInitialized()) {
            qWarning() << "Remember requesting the interface on your desktop file: X-KDE-Wayland-Interfaces=zkde_screencast_unstable_v1";
        }
        Q_ASSERT(isInitialized());
    }

    ~ScreencastingPrivate()
    {
        destroy();
    }

    Screencasting *const q;
};

Screencasting::Screencasting(QObject *parent)
    : QObject(parent)
    , d(new ScreencastingPrivate(this))
{
}

Screencasting::~Screencasting() = default;

ScreencastingStream *Screencasting::createOutputStream(const QString &outputName, Screencasting::CursorMode mode)
{
    wl_output *output = nullptr;
    for (auto screen : qGuiApp->screens()) {
        if (screen->name() == outputName) {
            output = (wl_output *)QGuiApplication::platformNativeInterface()->nativeResourceForScreen("output", screen);
        }
    }

    if (!output) {
        return nullptr;
    }

    auto stream = new ScreencastingStream(this);
    stream->setObjectName(outputName);
    stream->d->init(d->stream_output(output, mode));
    return stream;
}

ScreencastingStream *Screencasting::createOutputStream(Output *output, CursorMode mode)
{
    auto stream = new ScreencastingStream(this);
    stream->setObjectName(output->model());
    stream->d->init(d->stream_output(*output, mode));
    return stream;
}

ScreencastingStream *Screencasting::createRegionStream(const QRect &geometry, qreal scaling, CursorMode mode)
{
    Q_ASSERT(d->QWaylandClientExtension::version() >= ZKDE_SCREENCAST_UNSTABLE_V1_STREAM_REGION_SINCE_VERSION);
    auto stream = new ScreencastingStream(this);
    stream->setObjectName(QStringLiteral("region-%1,%2 (%3x%4)").arg(geometry.x()).arg(geometry.y()).arg(geometry.width()).arg(geometry.height()));
    stream->d->init(d->stream_region(geometry.x(), geometry.y(), geometry.width(), geometry.height(), wl_fixed_from_double(scaling), mode));
    return stream;
}

ScreencastingStream *Screencasting::createWindowStream(PlasmaWindow *window, CursorMode mode)
{
    auto stream = createWindowStream(QString::fromUtf8(window->uuid()), mode);
    stream->setObjectName(window->appId());
    return stream;
}

ScreencastingStream *Screencasting::createWindowStream(const QString &uuid, CursorMode mode)
{
    auto stream = new ScreencastingStream(this);
    stream->d->init(d->stream_window(uuid, mode));
    return stream;
}

ScreencastingStream *Screencasting::createVirtualMonitorStream(const QString &name, const QSize &size, qreal scale, CursorMode mode)
{
    auto stream = new ScreencastingStream(this);
    stream->d->init(d->stream_virtual_output(name, size.width(), size.height(), wl_fixed_from_double(scale), mode));
    return stream;
}

void Screencasting::destroy()
{
    d.reset(nullptr);
}
