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
#ifndef KWIN_SCENE_QPAINTER_VIRTUAL_BACKEND_H
#define KWIN_SCENE_QPAINTER_VIRTUAL_BACKEND_H

#include <platformsupport/scenes/qpainter/backend.h>

#include <QImage>
#include <QObject>

#include <vector>

namespace KWin
{

class VirtualBackend;

class VirtualQPainterBackend : public QObject, public QPainterBackend
{
    Q_OBJECT
public:
    VirtualQPainterBackend(VirtualBackend *backend);
    ~VirtualQPainterBackend() override;

    QImage *buffer() override;
    QImage *bufferForScreen(AbstractOutput* output) override;
    bool needsFullRepaint() const override;
    void prepareRenderingFrame() override;
    void present(AbstractOutput* output, const QRegion &damage) override;

private:
    struct Output {
        AbstractOutput* output;
        QImage image;
    };
    Output& get_output(AbstractOutput* output);
    void createOutputs();

    std::vector<Output> m_backBuffers;
    VirtualBackend *m_backend;
    int m_frameCounter = 0;
};

}

#endif
