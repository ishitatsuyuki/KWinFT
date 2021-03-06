/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright © 2019 Roman Gilg <subdiff@gmail.com>
Copyright © 2018 Fredrik Höglund <fredrik@kde.org>

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
#pragma once

#include "../../../linux_dmabuf.h"

#include "abstract_egl_backend.h"

#include <QVector>

namespace KWin
{
class EglDmabuf;

class EglDmabufBuffer : public DmabufBuffer
{
public:
    using Plane = Wrapland::Server::LinuxDmabufV1::Plane;
    using Flags = Wrapland::Server::LinuxDmabufV1::Flags;

    enum class ImportType {
        Direct,
        Conversion
    };

    EglDmabufBuffer(EGLImage image,
                    const QVector<Plane> &planes,
                    uint32_t format,
                    const QSize &size,
                    Flags flags,
                    EglDmabuf *interfaceImpl);

    EglDmabufBuffer(const QVector<Plane> &planes,
                    uint32_t format,
                    const QSize &size,
                    Flags flags,
                    EglDmabuf *interfaceImpl);

    ~EglDmabufBuffer() override;

    void setInterfaceImplementation(EglDmabuf *interfaceImpl);
    void addImage(EGLImage image);
    void removeImages();

    std::vector<EGLImage> const& images() const;

private:
    std::vector<EGLImage> m_images;
    EglDmabuf *m_interfaceImpl;
    ImportType m_importType;
};

class EglDmabuf : public LinuxDmabuf
{
public:
    using Plane = Wrapland::Server::LinuxDmabufV1::Plane;
    using Flags = Wrapland::Server::LinuxDmabufV1::Flags;

    static EglDmabuf* factory(AbstractEglBackend *backend);

    explicit EglDmabuf(AbstractEglBackend *backend);
    ~EglDmabuf() override;

    Wrapland::Server::LinuxDmabufBufferV1 *importBuffer(const QVector<Plane> &planes,
                                                                uint32_t format,
                                                                const QSize &size,
                                                                Flags flags) override;

private:
    EGLImage createImage(const QVector<Plane> &planes,
                         uint32_t format,
                         const QSize &size);

    Wrapland::Server::LinuxDmabufBufferV1 *yuvImport(const QVector<Plane> &planes,
                                                             uint32_t format,
                                                             const QSize &size,
                                                             Flags flags);
    QVector<uint32_t> queryFormats();
    void setSupportedFormatsAndModifiers();

    AbstractEglBackend *m_backend;

    friend class EglDmabufBuffer;
};

}
