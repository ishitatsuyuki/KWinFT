/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2014 Martin Gräßlin <mgraesslin@kde.org>

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
#include "decorationrenderer.h"
#include "decoratedclient.h"
#include "decorations/decorations_logging.h"
#include "screens.h"
#include "toplevel.h"

#include <KDecoration2/Decoration>
#include <KDecoration2/DecoratedClient>

#include <QDebug>
#include <QPainter>

namespace KWin
{
namespace Decoration
{

Renderer::Renderer(DecoratedClientImpl *client)
    : QObject(client)
    , m_client(client)
    , m_imageSizesDirty(true)
{
    auto markImageSizesDirty = [this]{
        m_imageSizesDirty = true;
    };
    connect(client->client(), &Toplevel::screenScaleChanged, this, markImageSizesDirty);
    connect(client->decoration(), &KDecoration2::Decoration::bordersChanged, this, markImageSizesDirty);
    connect(client->decoratedClient(), &KDecoration2::DecoratedClient::widthChanged, this, markImageSizesDirty);
    connect(client->decoratedClient(), &KDecoration2::DecoratedClient::heightChanged, this, markImageSizesDirty);
}

Renderer::~Renderer() = default;

void Renderer::schedule(const QRect &rect)
{
    m_scheduled = m_scheduled.united(rect);
    emit renderScheduled(rect);
}

QRegion Renderer::getScheduled()
{
    QRegion region = m_scheduled;
    m_scheduled = QRegion();
    return region;
}

QImage Renderer::renderToImage(const QRect &geo)
{
    Q_ASSERT(m_client);
    auto dpr = client()->client()->screenScale();

    // Guess the pixel format of the X pixmap into which the QImage will be copied.
    QImage::Format format;
    const int depth = client()->client()->depth();
    switch (depth) {
    case 30:
        format = QImage::Format_A2RGB30_Premultiplied;
        break;
    case 24:
    case 32:
        format = QImage::Format_ARGB32_Premultiplied;
        break;
    default:
        qCCritical(KWIN_DECORATIONS) << "Unsupported client depth" << depth;
        format = QImage::Format_ARGB32_Premultiplied;
        break;
    };

    QImage image(geo.width() * dpr, geo.height() * dpr, format);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setWindow(QRect(geo.topLeft(), geo.size() * dpr));
    p.setClipRect(geo);
    renderToPainter(&p, geo);
    return image;
}

void Renderer::renderToPainter(QPainter *painter, const QRect &rect)
{
    client()->decoration()->paint(painter, rect);
}

void Renderer::reparent(Toplevel* window)
{
    setParent(window);
    m_client = nullptr;
}

}
}
