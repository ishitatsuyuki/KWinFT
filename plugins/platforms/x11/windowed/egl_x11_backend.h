/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2015 Martin Gräßlin <mgraesslin@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#ifndef KWIN_EGL_X11_BACKEND_H
#define KWIN_EGL_X11_BACKEND_H
#include "abstract_egl_backend.h"

#include <xcb/xcb.h>

namespace KWin
{

class X11WindowedBackend;

/**
 * @brief OpenGL Backend using Egl windowing system over an X overlay window.
 */
class EglX11Backend : public AbstractEglBackend
{
public:
    explicit EglX11Backend(X11WindowedBackend *backend);
    ~EglX11Backend() override;

    void init() override;

    void screenGeometryChanged(const QSize &size) override;

    QRegion prepareRenderingFrame() override;
    void endRenderingFrame(const QRegion &damage, const QRegion &damagedRegion) override;
    bool usesOverlayWindow() const override;
    bool hasSwapEvent() const override;
    QRegion prepareRenderingForScreen(AbstractOutput* output) override;
    void endRenderingFrameForScreen(AbstractOutput* output, const QRegion &damage,
                                    const QRegion &damagedRegion) override;

protected:
    void present() override;
    void cleanupSurfaces() override;
    bool createSurfaces();

private:
    struct Output {
        EGLSurface surface;
        AbstractOutput* output;
    };

    bool initRenderingContext();
    bool initBufferConfigs();

    void setupViewport(AbstractOutput* output);
    bool makeContextCurrent(const EGLSurface &surface);
    void presentSurface(EGLSurface surface, const QRegion &damage, const QRect &screenGeometry);

    EGLSurface createSurface(xcb_window_t window);
    Output& get_output(AbstractOutput* output);

    bool m_havePlatformBase = false;
    int m_surfaceHasSubPost = 0;

    std::vector<Output> m_surfaces;
    X11WindowedBackend *m_backend;
};

}

#endif
