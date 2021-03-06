/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2019 NVIDIA Inc.

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
#ifndef KWIN_EGL_STREAM_BACKEND_H
#define KWIN_EGL_STREAM_BACKEND_H
#include "abstract_egl_backend.h"
#include <Wrapland/Server/surface.h>
#include <Wrapland/Server/egl_stream_controller.h>
#include <wayland-server-core.h>

namespace KWin
{

class DrmBackend;
class DrmOutput;
class DrmBuffer;
class XdgShellClient;

/**
 * @brief OpenGL Backend using Egl with an EGLDevice.
 */
class EglStreamBackend : public AbstractEglBackend
{
    Q_OBJECT
public:
    EglStreamBackend(DrmBackend *b);
    ~EglStreamBackend() override;
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
    struct StreamTexture 
    {
        EGLStreamKHR stream;
        GLuint texture;
    };
    StreamTexture *lookupStreamTexture(Wrapland::Server::Surface *surface);
    void attachStreamConsumer(Wrapland::Server::Surface *surface,
                              void *eglStream,
                              wl_array *attribs);
    struct Output 
    {
        DrmOutput *output = nullptr;
        DrmBuffer *buffer = nullptr;
        EGLSurface eglSurface = EGL_NO_SURFACE;
        EGLStreamKHR eglStream = EGL_NO_STREAM_KHR;
    };

    Output& get_output(AbstractOutput* output);
    bool resetOutput(Output &output, DrmOutput *drmOutput);
    bool makeContextCurrent(const Output &output);
    void presentOnOutput(Output &output);
    void cleanupOutput(const Output &output);
    void createOutput(DrmOutput *output);

    DrmBackend *m_backend;
    QVector<Output> m_outputs;
    Wrapland::Server::EglStreamController *m_eglStreamControllerInterface;
    QHash<Wrapland::Server::Surface *, StreamTexture> m_streamTextures;

    friend class EglStreamTexture;
};

/**
 * @brief External texture bound to an EGLStreamKHR.
 */
class EglStreamTexture : public EglTexture
{
public:
    ~EglStreamTexture() override;
    bool loadTexture(WindowPixmap *pixmap) override;
    void updateTexture(WindowPixmap *pixmap) override;

private:
    EglStreamTexture(SceneOpenGLTexture *texture, EglStreamBackend *backend);
    bool acquireStreamFrame(EGLStreamKHR stream);
    void createFbo();
    void copyExternalTexture(GLuint tex);
    bool attachBuffer(Wrapland::Server::Buffer *buffer);
    EglStreamBackend *m_backend;
    GLuint m_fbo, m_rbo;
    GLenum m_format;
    friend class EglStreamBackend;
};

} // namespace

#endif
