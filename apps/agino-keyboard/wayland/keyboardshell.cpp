// SPDX-License-Identifier: GPL-3.0-or-later
// Minimal keyboard-only shell integration, built for Qt 6.8 private ABI.
#include "qwayland-wlr-layer-shell-unstable-v1.h"
#include "waylandproperties.h"
#include <QtWaylandClient/private/qwaylandshellintegration_p.h>
#include <QtWaylandClient/private/qwaylandshellintegrationplugin_p.h>
#include <QtWaylandClient/private/qwaylandshellsurface_p.h>
#include <QtWaylandClient/private/qwaylandscreen_p.h>
#include <QtWaylandClient/private/qwaylandsurface_p.h>
#include <QtWaylandClient/private/qwaylandwindow_p.h>
#include <QDynamicPropertyChangeEvent>
#include <QPointer>
#include <QTimer>
#include <QWindow>
#include <QtGui/private/qhighdpiscaling_p.h>
#include <qpa/qwindowsysteminterface.h>
#include <any>

using namespace QtWaylandClient;

class KeyboardSurface final : public QWaylandShellSurface,
                              public QtWayland::zwlr_layer_surface_v1 {
public:
    KeyboardSurface(QWaylandWindow *window, QtWayland::zwlr_layer_shell_v1 *shell)
        : QWaylandShellSurface(window)
    {
        auto screen = dynamic_cast<QWaylandScreen *>(window->window()->screen()->handle());
        // Deepin KWin maps the layer namespace to NET window type. A dock
        // avoids normal-window placement moving the panel above its own strut.
        init(shell->get_layer_surface(window->waylandSurface()->object(),
                                     screen ? screen->output() : nullptr,
                                     QtWayland::zwlr_layer_shell_v1::layer_top,
                                     QStringLiteral("dock")));
        set_keyboard_interactivity(keyboard_interactivity_none);
        desiredSize_ = window->windowContentGeometry().size().expandedTo(QSize(1, 1));
        set_size(desiredSize_.width(), desiredSize_.height());
        applyPlacement();
        window->window()->installEventFilter(this);
        // The first commit must have no buffer. Qt waits for isExposed() before
        // painting; only the acknowledged initial configure enables exposure.
        wl_surface_commit(wlSurface());
    }

    ~KeyboardSurface() override { destroy(); }
    bool isExposed() const override { return configured_ && !closed_; }
    bool wantsDecorations() const override { return false; }
    std::any surfaceRole() const override { return object(); }

    void setWindowGeometry(const QRect &geometry) override
    {
        if (applyingConfigure_ || closed_)
            return;
        const auto size = geometry.size().expandedTo(QSize(1, 1));
        if (size == desiredSize_) {
            applyPosition();
            return;
        }
        desiredSize_ = size;
        applyPosition();
        set_size(size.width(), size.height());
        // Size and exclusive zone are double-buffered. A Fn/settings resize
        // must reach the compositor even when no paint has been scheduled yet.
        wl_surface_commit(wlSurface());
    }

    void applyConfigure() override
    {
        if (!pendingSize_.isValid() || closed_)
            return;
        applyingConfigure_ = true;
        resizeFromApplyConfigure(pendingSize_);
        window()->window()->setProperty(KeyboardWayland::NegotiatedSize,QHighDpi::fromNativePixels(pendingSize_,window()->window()));
        applyPosition();
        applyingConfigure_ = false;
        pendingSize_ = QSize();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == window()->window() && event->type() == QEvent::DynamicPropertyChange) {
            auto change = static_cast<QDynamicPropertyChangeEvent *>(event);
            if (!closed_ && (change->propertyName() == KeyboardWayland::ExclusiveZone ||
                            change->propertyName() == KeyboardWayland::Floating ||
                            change->propertyName() == KeyboardWayland::Position)) {
                applyPlacement();
                wl_surface_commit(wlSurface());
            }
        }
        return false;
    }

    void zwlr_layer_surface_v1_configure(uint32_t serial, uint32_t width, uint32_t height) override
    {
        if (closed_)
            return;
        // Zero means client chooses this dimension, not a zero-sized panel.
        pendingSize_ = QSize(width ? int(width) : desiredSize_.width(),
                             height ? int(height) : desiredSize_.height());
        ack_configure(serial);
        if (!configured_) {
            configured_ = true;
            applyConfigure();
            window()->sendRecursiveExposeEvent();
        } else {
            applyConfigureWhenPossible();
        }
    }

    void zwlr_layer_surface_v1_closed() override
    {
        closed_ = true;
        // Hide/recreate outside the protocol callback: hiding deletes this
        // shell surface. Bind recovery to this role so a remap cancels the old
        // callback. Resolve the output after Qt handles registry removal.
        QPointer<QWindow> panel = window()->window();
        QTimer::singleShot(0, this, [panel] {
            if (!panel || !panel->isVisible())
                return;
            panel->hide();
            if (panel->screen())
                panel->show();
        });
    }

private:
    bool floating() { return window()->window()->property(KeyboardWayland::Floating).toBool(); }
    void applyPosition()
    {
        if (!configured_)
            return;
        const auto screen = window()->window()->screen();
        if (!screen || !screen->handle())
            return;
        auto platform = window();
        const auto output = screen->handle()->geometry();
        QPoint offset;
        if (floating()) {
            offset = QHighDpi::toNativeLocalPosition(QPointF(window()->window()->property(KeyboardWayland::Position).toPoint()), window()->window()).toPoint();
        } else {
            offset = QPoint((output.width() - platform->geometry().width()) / 2,
                            output.height() - platform->geometry().height());
        }
        const QRect geometry(output.topLeft() + offset, platform->geometry().size());
        // Qt 6.8 fixes ordinary Wayland toplevel positions at the output origin.
        // Keep our known layer geometry consistent in both docked and floating
        // modes, so mapToGlobal stays continuous when a title drag begins.
        platform->QPlatformWindow::setGeometry(geometry);
        QWindowSystemInterface::handleGeometryChange(platform->window(), geometry);
    }
    void applyPlacement()
    {
        if (floating()) {
            const auto offset = QHighDpi::toNativeLocalPosition(QPointF(window()->window()->property(KeyboardWayland::Position).toPoint()), window()->window()).toPoint();
            set_anchor(anchor_top | anchor_left);
            set_margin(offset.y(), 0, 0, offset.x());
            // -1 reserves nothing and positions against the full output,
            // without displacement by another panel's exclusive zone.
            set_exclusive_zone(-1);
        } else {
            set_anchor(anchor_bottom);
            set_margin(0, 0, 0, 0);
            set_exclusive_zone(qMax(0, window()->window()->property(KeyboardWayland::ExclusiveZone).toInt()));
        }
        applyPosition();
    }
    QSize desiredSize_;
    QSize pendingSize_;
    bool configured_ = false;
    bool applyingConfigure_ = false;
    bool closed_ = false;
};

class KeyboardIntegration final : public QWaylandShellIntegrationTemplate<KeyboardIntegration>,
                                  public QtWayland::zwlr_layer_shell_v1 {
public:
    KeyboardIntegration() : QWaylandShellIntegrationTemplate(3) {}
    ~KeyboardIntegration() override
    {
        if (!isInitialized())
            return;
        if (QtWayland::zwlr_layer_shell_v1::version() >= 3)
            destroy();
        else
            wl_proxy_destroy(reinterpret_cast<wl_proxy *>(object()));
    }
    QWaylandShellSurface *createShellSurface(QWaylandWindow *window) override
    {
        return isActive() ? new KeyboardSurface(window, this) : nullptr;
    }
    void *nativeResourceForWindow(const QByteArray &name, QWindow *) override
    {
        return name == KeyboardWayland::IntegrationResource && isActive() ? this : nullptr;
    }
};

class KeyboardShellPlugin final : public QWaylandShellIntegrationPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QWaylandShellIntegrationFactoryInterface_iid FILE "keyboard-shell.json")
public:
    QWaylandShellIntegration *create(const QString &key, const QStringList &) override
    {
        return key == QLatin1String("agino-keyboard") ? new KeyboardIntegration : nullptr;
    }
};
#include "keyboardshell.moc"
