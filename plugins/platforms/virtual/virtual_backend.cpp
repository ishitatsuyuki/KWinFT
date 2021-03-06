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
#include "virtual_backend.h"
#include "virtual_output.h"

#include "main.h"
#include "scene_qpainter_virtual_backend.h"
#include "screens.h"
#include "wayland_server.h"
#include "egl_gbm_backend.h"
// Qt
#include <QTemporaryDir>
// Wrapland
#include <Wrapland/Server/seat.h>
// system
#include <fcntl.h>
#include <unistd.h>
#include <config-kwin.h>

namespace KWin
{

VirtualBackend::VirtualBackend(QObject *parent)
    : Platform(parent)
{
    if (qEnvironmentVariableIsSet("KWIN_WAYLAND_VIRTUAL_SCREENSHOTS")) {
        m_screenshotDir.reset(new QTemporaryDir);
        if (!m_screenshotDir->isValid()) {
            m_screenshotDir.reset();
        }
        if (!m_screenshotDir.isNull()) {
            qDebug() << "Screenshots saved to: " << m_screenshotDir->path();
        }
    }
    setSupportsPointerWarping(true);
    setSupportsGammaControl(true);
}

VirtualBackend::~VirtualBackend()
{
}

void VirtualBackend::init()
{
    /*
     * Some tests currently expect one output present at start,
     * others set them explicitly.
     *
     * TODO: rewrite all tests to explicitly set the outputs.
     */
    if (!m_outputs.size()) {
        VirtualOutput *dummyOutput = new VirtualOutput(this);
        dummyOutput->init(0, QPoint(0, 0), initialWindowSize(), initialWindowSize());
        m_outputs << dummyOutput ;
        m_enabledOutputs << dummyOutput ;
    }

    setSoftWareCursor(true);
    waylandServer()->seat()->setHasPointer(true);
    waylandServer()->seat()->setHasKeyboard(true);
    waylandServer()->seat()->setHasTouch(true);

    Screens::self()->updateAll();
    kwinApp()->continueStartupWithCompositor();
}

QString VirtualBackend::screenshotDirPath() const
{
    if (m_screenshotDir.isNull()) {
        return QString();
    }
    return m_screenshotDir->path();
}

QPainterBackend *VirtualBackend::createQPainterBackend()
{
    return new VirtualQPainterBackend(this);
}

OpenGLBackend *VirtualBackend::createOpenGLBackend()
{
    return new EglGbmBackend(this);
}

Outputs VirtualBackend::outputs() const
{
    return m_outputs;
}

Outputs VirtualBackend::enabledOutputs() const
{
    return m_enabledOutputs;
}

void VirtualBackend::setVirtualOutputs(int count, QVector<QRect> geometries, QVector<int> scales)
{
    Q_ASSERT(geometries.size() == 0 || geometries.size() == count);
    Q_ASSERT(scales.size() == 0 || scales.size() == count);

    for (auto output : m_outputs) {
        Q_EMIT output_removed(output);
    }
    qDeleteAll(m_outputs.begin(), m_outputs.end());
    m_outputs.resize(count);
    m_enabledOutputs.resize(count);

    int sumWidth = 0;
    for (int i = 0; i < count; i++) {
        VirtualOutput *vo = new VirtualOutput(this);
        double scale = 1.;
        if (scales.size()) {
            scale = scales.at(i);
        }
        if (geometries.size()) {
            const QRect geo = geometries.at(i);
            vo->init(i + 1, geo.topLeft(), geo.size() * scale, geo.size());
        } else {
            const auto size = initialWindowSize();
            vo->init(i + 1, QPoint(sumWidth, 0), size * scale, size);
            sumWidth += size.width();
        }
        m_outputs[i] = m_enabledOutputs[i] = vo;
        Q_EMIT output_added(vo);
    }

    Screens::self()->updateAll();
}

}
