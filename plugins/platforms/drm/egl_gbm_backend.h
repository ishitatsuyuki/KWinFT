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
#ifndef KWIN_EGL_GBM_BACKEND_H
#define KWIN_EGL_GBM_BACKEND_H
#include "abstract_egl_backend.h"

#include <memory>

struct gbm_surface;

namespace KWin
{
class DrmBackend;
class DrmBuffer;
class DrmOutput;
class GbmSurface;

/**
 * @brief OpenGL Backend using Egl on a GBM surface.
 */
class EglGbmBackend : public AbstractEglBackend
{
    Q_OBJECT
public:
    EglGbmBackend(DrmBackend *drmBackend);
    ~EglGbmBackend() override;
    void screenGeometryChanged(const QSize &size) override;
    SceneOpenGLTexturePrivate *createBackendTexture(SceneOpenGLTexture *texture) override;
    QRegion prepareRenderingFrame() override;
    void endRenderingFrame(const QRegion &renderedRegion, const QRegion &damagedRegion) override;
    void endRenderingFrameForScreen(AbstractOutput* output, const QRegion &damage, const QRegion &damagedRegion) override;
    bool usesOverlayWindow() const override;
    QRegion prepareRenderingForScreen(AbstractOutput* output) override;
    void init() override;

protected:
    void present() override;
    void cleanupSurfaces() override;

private:
    bool initializeEgl();
    bool initBufferConfigs();
    bool initRenderingContext();
    struct Output {
        DrmOutput *output = nullptr;
        DrmBuffer *buffer = nullptr;
        std::shared_ptr<GbmSurface> gbmSurface;
        EGLSurface eglSurface = EGL_NO_SURFACE;
        int bufferAge = 0;
        /**
         * @brief The damage history for the past 10 frames.
         */
        QList<QRegion> damageHistory;

        struct {
            GLuint framebuffer = 0;
            GLuint texture = 0;
            std::shared_ptr<GLVertexBuffer> vbo;
        } render;
    };

    Output& get_output(AbstractOutput* output);
    void createOutput(DrmOutput *drmOutput);
    bool resetOutput(Output &output, DrmOutput *drmOutput);
    std::shared_ptr<GbmSurface> createGbmSurface(const QSize &size) const;
    EGLSurface createEglSurface(std::shared_ptr<GbmSurface> gbmSurface) const;

    bool makeContextCurrent(const Output &output) const;
    void setViewport(const Output &output) const;

    bool resetFramebuffer(Output &output);
    void initRenderTarget(Output &output);

    void prepareRenderFramebuffer(const Output &output) const;
    void renderFramebufferToSurface(Output &output);

    void presentOnOutput(Output &output);

    void removeOutput(DrmOutput *drmOutput);
    void cleanupOutput(Output &output);
    void cleanupFramebuffer(Output &output);

    DrmBackend *m_backend;
    QVector<Output> m_outputs;
    friend class EglGbmTexture;
};

/**
 * @brief Texture using an EGLImageKHR.
 */
class EglGbmTexture : public EglTexture
{
public:
    ~EglGbmTexture() override;

private:
    friend class EglGbmBackend;
    EglGbmTexture(SceneOpenGLTexture *texture, EglGbmBackend *backend);
};

} // namespace

#endif
