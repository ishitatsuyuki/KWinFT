/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2011 Martin Gräßlin <mgraesslin@kde.org>

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
#include "shadow.h"
// kwin
#include "atoms.h"
#include "composite.h"
#include "effects.h"
#include "toplevel.h"
#include "wayland_server.h"

#include "win/deco.h"
#include "win/scene.h"

#include <KDecoration2/Decoration>
#include <KDecoration2/DecorationShadow>

#include <Wrapland/Server/buffer.h>
#include <Wrapland/Server/shadow.h>
#include <Wrapland/Server/surface.h>

namespace KWin
{

Shadow::Shadow(Toplevel *toplevel)
    : m_topLevel(toplevel)
    , m_cachedSize(toplevel->size())
    , m_decorationShadow(nullptr)
{
    connect(m_topLevel, &Toplevel::frame_geometry_changed, this, &Shadow::geometryChanged);
}

Shadow::~Shadow()
{
}

Shadow *Shadow::createShadow(Toplevel *toplevel)
{
    if (!effects) {
        return nullptr;
    }
    Shadow *shadow = createShadowFromDecoration(toplevel);
    if (!shadow && waylandServer()) {
        shadow = createShadowFromWayland(toplevel);
    }
    if (!shadow && kwinApp()->x11Connection()) {
        shadow = createShadowFromX11(toplevel);
    }
    if (!shadow) {
        return nullptr;
    }
    if (toplevel->effectWindow() && toplevel->effectWindow()->sceneWindow()) {
        toplevel->effectWindow()->sceneWindow()->updateShadow(shadow);
        emit toplevel->shadowChanged();
    }
    return shadow;
}

Shadow *Shadow::createShadowFromX11(Toplevel *toplevel)
{
    auto data = Shadow::readX11ShadowProperty(toplevel->xcb_window());
    if (!data.isEmpty()) {
        Shadow *shadow = Compositor::self()->scene()->createShadow(toplevel);

        if (!shadow->init(data)) {
            delete shadow;
            return nullptr;
        }
        return shadow;
    } else {
        return nullptr;
    }
}

Shadow *Shadow::createShadowFromDecoration(Toplevel *toplevel)
{
    if (!toplevel || !toplevel->control) {
        return nullptr;
    }
    if (!win::decoration(toplevel)) {
        return nullptr;
    }
    Shadow *shadow = Compositor::self()->scene()->createShadow(toplevel);
    if (!shadow->init(win::decoration(toplevel))) {
        delete shadow;
        return nullptr;
    }
    return shadow;
}

Shadow *Shadow::createShadowFromWayland(Toplevel *toplevel)
{
    auto surface = toplevel->surface();
    if (!surface) {
        return nullptr;
    }
    const auto s = surface->shadow();
    if (!s) {
        return nullptr;
    }
    Shadow *shadow = Compositor::self()->scene()->createShadow(toplevel);
    if (!shadow->init(s)) {
        delete shadow;
        return nullptr;
    }
    return shadow;
}

QVector< uint32_t > Shadow::readX11ShadowProperty(xcb_window_t id)
{
    QVector<uint32_t> ret;
    if (id != XCB_WINDOW_NONE) {
        Xcb::Property property(false, id, atoms->kde_net_wm_shadow, XCB_ATOM_CARDINAL, 0, 12);
        uint32_t *shadow = property.value<uint32_t*>();
        if (shadow) {
            ret.reserve(12);
            for (int i=0; i<12; ++i) {
                ret << shadow[i];
            }
        }
    }
    return ret;
}

bool Shadow::init(const QVector< uint32_t > &data)
{
    QVector<Xcb::WindowGeometry> pixmapGeometries(ShadowElementsCount);
    QVector<xcb_get_image_cookie_t> getImageCookies(ShadowElementsCount);
    auto *c = connection();
    for (int i = 0; i < ShadowElementsCount; ++i) {
        pixmapGeometries[i] = Xcb::WindowGeometry(data[i]);
    }
    auto discardReplies = [&getImageCookies](int start) {
        for (int i = start; i < getImageCookies.size(); ++i) {
            xcb_discard_reply(connection(), getImageCookies.at(i).sequence);
        }
    };
    for (int i = 0; i < ShadowElementsCount; ++i) {
        auto &geo = pixmapGeometries[i];
        if (geo.isNull()) {
            discardReplies(0);
            return false;
        }
        getImageCookies[i] = xcb_get_image_unchecked(c, XCB_IMAGE_FORMAT_Z_PIXMAP, data[i],
                                                     0, 0, geo->width, geo->height, ~0);
    }
    for (int i = 0; i < ShadowElementsCount; ++i) {
        auto *reply = xcb_get_image_reply(c, getImageCookies.at(i), nullptr);
        if (!reply) {
            discardReplies(i+1);
            return false;
        }
        auto &geo = pixmapGeometries[i];
        QImage image(xcb_get_image_data(reply), geo->width, geo->height, QImage::Format_ARGB32);
        m_shadowElements[i] = QPixmap::fromImage(image);
        free(reply);
    }
    m_topOffset = data[ShadowElementsCount];
    m_rightOffset = data[ShadowElementsCount+1];
    m_bottomOffset = data[ShadowElementsCount+2];
    m_leftOffset = data[ShadowElementsCount+3];
    updateShadowRegion();
    if (!prepareBackend()) {
        return false;
    }
    buildQuads();
    return true;
}

bool Shadow::init(KDecoration2::Decoration *decoration)
{
    if (m_decorationShadow) {
        // disconnect previous connections
        disconnect(m_decorationShadow.data(), &KDecoration2::DecorationShadow::innerShadowRectChanged, m_topLevel, nullptr);
        disconnect(m_decorationShadow.data(), &KDecoration2::DecorationShadow::shadowChanged,        m_topLevel, nullptr);
        disconnect(m_decorationShadow.data(), &KDecoration2::DecorationShadow::paddingChanged,       m_topLevel, nullptr);
    }
    m_decorationShadow = decoration->shadow();
    if (!m_decorationShadow) {
        return false;
    }
    // setup connections - all just mapped to recreate
    auto update_shadow = [toplevel = m_topLevel]() {
        win::update_shadow(toplevel);
    };
    connect(m_decorationShadow.data(), &KDecoration2::DecorationShadow::innerShadowRectChanged, m_topLevel, update_shadow);
    connect(m_decorationShadow.data(), &KDecoration2::DecorationShadow::shadowChanged,        m_topLevel, update_shadow);
    connect(m_decorationShadow.data(), &KDecoration2::DecorationShadow::paddingChanged,       m_topLevel, update_shadow);

    const QMargins &p = m_decorationShadow->padding();
    m_topOffset    = p.top();
    m_rightOffset  = p.right();
    m_bottomOffset = p.bottom();
    m_leftOffset   = p.left();
    updateShadowRegion();
    if (!prepareBackend()) {
        return false;
    }
    buildQuads();
    return true;
}

bool Shadow::init(const QPointer< Wrapland::Server::Shadow> &shadow)
{
    if (!shadow) {
        return false;
    }

    m_shadowElements[ShadowElementTop] = shadow->top() ? QPixmap::fromImage(shadow->top()->shmImage()->createQImage().copy()) : QPixmap();
    m_shadowElements[ShadowElementTopRight] = shadow->topRight() ? QPixmap::fromImage(shadow->topRight()->shmImage()->createQImage().copy()) : QPixmap();
    m_shadowElements[ShadowElementRight] = shadow->right() ? QPixmap::fromImage(shadow->right()->shmImage()->createQImage().copy()) : QPixmap();
    m_shadowElements[ShadowElementBottomRight] = shadow->bottomRight() ? QPixmap::fromImage(shadow->bottomRight()->shmImage()->createQImage().copy()) : QPixmap();
    m_shadowElements[ShadowElementBottom] = shadow->bottom() ? QPixmap::fromImage(shadow->bottom()->shmImage()->createQImage().copy()) : QPixmap();
    m_shadowElements[ShadowElementBottomLeft] = shadow->bottomLeft() ? QPixmap::fromImage(shadow->bottomLeft()->shmImage()->createQImage().copy()) : QPixmap();
    m_shadowElements[ShadowElementLeft] = shadow->left() ? QPixmap::fromImage(shadow->left()->shmImage()->createQImage().copy()) : QPixmap();
    m_shadowElements[ShadowElementTopLeft] = shadow->topLeft() ? QPixmap::fromImage(shadow->topLeft()->shmImage()->createQImage().copy()) : QPixmap();

    const QMarginsF &p = shadow->offset();
    m_topOffset    = p.top();
    m_rightOffset  = p.right();
    m_bottomOffset = p.bottom();
    m_leftOffset   = p.left();
    updateShadowRegion();
    if (!prepareBackend()) {
        return false;
    }
    buildQuads();
    return true;
}

void Shadow::updateShadowRegion()
{
    auto const size = m_topLevel->size();
    const QRect top(0, - m_topOffset, size.width(), m_topOffset);
    const QRect right(size.width(), - m_topOffset, m_rightOffset, size.height() + m_topOffset + m_bottomOffset);
    const QRect bottom(0, size.height(), size.width(), m_bottomOffset);
    const QRect left(- m_leftOffset, - m_topOffset, m_leftOffset, size.height() + m_topOffset + m_bottomOffset);
    m_shadowRegion = QRegion(top).united(right).united(bottom).united(left);
}

void Shadow::buildQuads()
{
    // prepare window quads
    m_shadowQuads.clear();

    auto const size = m_topLevel->size();
    const QSize top(m_shadowElements[ShadowElementTop].size());
    const QSize topRight(m_shadowElements[ShadowElementTopRight].size());
    const QSize right(m_shadowElements[ShadowElementRight].size());
    const QSize bottomRight(m_shadowElements[ShadowElementBottomRight].size());
    const QSize bottom(m_shadowElements[ShadowElementBottom].size());
    const QSize bottomLeft(m_shadowElements[ShadowElementBottomLeft].size());
    const QSize left(m_shadowElements[ShadowElementLeft].size());
    const QSize topLeft(m_shadowElements[ShadowElementTopLeft].size());
    if ((left.width() - m_leftOffset > size.width()) ||
        (right.width() - m_rightOffset > size.width()) ||
        (top.height() - m_topOffset > size.height()) ||
        (bottom.height() - m_bottomOffset > size.height())) {
        // if our shadow is bigger than the window, we don't render the shadow
        m_shadowRegion = QRegion();
        return;
    }

    const QRect outerRect(QPoint(-m_leftOffset, -m_topOffset),
                          QPoint(size.width() + m_rightOffset, size.height() + m_bottomOffset));

    WindowQuad topLeftQuad(WindowQuadShadowTopLeft);
    topLeftQuad[ 0 ] = WindowVertex(outerRect.x(),                      outerRect.y(), 0.0, 0.0);
    topLeftQuad[ 1 ] = WindowVertex(outerRect.x() + topLeft.width(),    outerRect.y(), 1.0, 0.0);
    topLeftQuad[ 2 ] = WindowVertex(outerRect.x() + topLeft.width(),    outerRect.y() + topLeft.height(), 1.0, 1.0);
    topLeftQuad[ 3 ] = WindowVertex(outerRect.x(),                      outerRect.y() + topLeft.height(), 0.0, 1.0);
    m_shadowQuads.append(topLeftQuad);

    WindowQuad topQuad(WindowQuadShadowTop);
    topQuad[ 0 ] = WindowVertex(outerRect.x() + topLeft.width(),        outerRect.y(), 0.0, 0.0);
    topQuad[ 1 ] = WindowVertex(outerRect.right() - topRight.width(),   outerRect.y(), 1.0, 0.0);
    topQuad[ 2 ] = WindowVertex(outerRect.right() - topRight.width(),   outerRect.y() + top.height(), 1.0, 1.0);
    topQuad[ 3 ] = WindowVertex(outerRect.x() + topLeft.width(),        outerRect.y() + top.height(), 0.0, 1.0);
    m_shadowQuads.append(topQuad);

    WindowQuad topRightQuad(WindowQuadShadowTopRight);
    topRightQuad[ 0 ] = WindowVertex(outerRect.right() - topRight.width(),  outerRect.y(), 0.0, 0.0);
    topRightQuad[ 1 ] = WindowVertex(outerRect.right(),                     outerRect.y(), 1.0, 0.0);
    topRightQuad[ 2 ] = WindowVertex(outerRect.right(),                     outerRect.y() + topRight.height(), 1.0, 1.0);
    topRightQuad[ 3 ] = WindowVertex(outerRect.right() - topRight.width(),  outerRect.y() + topRight.height(), 0.0, 1.0);
    m_shadowQuads.append(topRightQuad);

    WindowQuad rightQuad(WindowQuadShadowRight);
    rightQuad[ 0 ] = WindowVertex(outerRect.right() - right.width(),    outerRect.y() + topRight.height(), 0.0, 0.0);
    rightQuad[ 1 ] = WindowVertex(outerRect.right(),                    outerRect.y() + topRight.height(), 1.0, 0.0);
    rightQuad[ 2 ] = WindowVertex(outerRect.right(),                    outerRect.bottom() - bottomRight.height(), 1.0, 1.0);
    rightQuad[ 3 ] = WindowVertex(outerRect.right() - right.width(),    outerRect.bottom() - bottomRight.height(), 0.0, 1.0);
    m_shadowQuads.append(rightQuad);

    WindowQuad bottomRightQuad(WindowQuadShadowBottomRight);
    bottomRightQuad[ 0 ] = WindowVertex(outerRect.right() - bottomRight.width(),    outerRect.bottom() - bottomRight.height(), 0.0, 0.0);
    bottomRightQuad[ 1 ] = WindowVertex(outerRect.right(),                          outerRect.bottom() - bottomRight.height(), 1.0, 0.0);
    bottomRightQuad[ 2 ] = WindowVertex(outerRect.right(),                          outerRect.bottom(), 1.0, 1.0);
    bottomRightQuad[ 3 ] = WindowVertex(outerRect.right() - bottomRight.width(),    outerRect.bottom(), 0.0, 1.0);
    m_shadowQuads.append(bottomRightQuad);

    WindowQuad bottomQuad(WindowQuadShadowBottom);
    bottomQuad[ 0 ] = WindowVertex(outerRect.x() + bottomLeft.width(),      outerRect.bottom() - bottom.height(), 0.0, 0.0);
    bottomQuad[ 1 ] = WindowVertex(outerRect.right() - bottomRight.width(), outerRect.bottom() - bottom.height(), 1.0, 0.0);
    bottomQuad[ 2 ] = WindowVertex(outerRect.right() - bottomRight.width(), outerRect.bottom(), 1.0, 1.0);
    bottomQuad[ 3 ] = WindowVertex(outerRect.x() + bottomLeft.width(),      outerRect.bottom(), 0.0, 1.0);
    m_shadowQuads.append(bottomQuad);

    WindowQuad bottomLeftQuad(WindowQuadShadowBottomLeft);
    bottomLeftQuad[ 0 ] = WindowVertex(outerRect.x(),                       outerRect.bottom() - bottomLeft.height(), 0.0, 0.0);
    bottomLeftQuad[ 1 ] = WindowVertex(outerRect.x() + bottomLeft.width(),  outerRect.bottom() - bottomLeft.height(), 1.0, 0.0);
    bottomLeftQuad[ 2 ] = WindowVertex(outerRect.x() + bottomLeft.width(),  outerRect.bottom(), 1.0, 1.0);
    bottomLeftQuad[ 3 ] = WindowVertex(outerRect.x(),                       outerRect.bottom(), 0.0, 1.0);
    m_shadowQuads.append(bottomLeftQuad);

    WindowQuad leftQuad(WindowQuadShadowLeft);
    leftQuad[ 0 ] = WindowVertex(outerRect.x(),                 outerRect.y() + topLeft.height(), 0.0, 0.0);
    leftQuad[ 1 ] = WindowVertex(outerRect.x() + left.width(),  outerRect.y() + topLeft.height(), 1.0, 0.0);
    leftQuad[ 2 ] = WindowVertex(outerRect.x() + left.width(),  outerRect.bottom() - bottomLeft.height(), 1.0, 1.0);
    leftQuad[ 3 ] = WindowVertex(outerRect.x(),                 outerRect.bottom() - bottomLeft.height(), 0.0, 1.0);
    m_shadowQuads.append(leftQuad);
}

bool Shadow::updateShadow()
{
    if (!m_topLevel) {
        return false;
    }

    if (m_decorationShadow) {
        if (m_topLevel->control) {
            if (auto deco = win::decoration(m_topLevel)) {
                if (init(deco)) {
                    return true;
                }
            }
        }
        return false;
    }

    if (waylandServer()) {
        if (m_topLevel && m_topLevel->surface()) {
            if (const auto &s = m_topLevel->surface()->shadow()) {
                if (init(s)) {
                    return true;
                }
            }
        }
    }

    auto data = Shadow::readX11ShadowProperty(m_topLevel->xcb_window());
    if (data.isEmpty()) {
        return false;
    }

    init(data);

    return true;
}

void Shadow::setToplevel(Toplevel *topLevel)
{
    // TODO(romangg): This function works because it is only used to change the toplevel to the
    //                remnant. But in general this would not clean up the connection from the ctor.
    m_topLevel = topLevel;
    connect(m_topLevel, &Toplevel::frame_geometry_changed, this, &Shadow::geometryChanged);
}
void Shadow::geometryChanged()
{
    if (m_cachedSize == m_topLevel->size()) {
        return;
    }
    m_cachedSize = m_topLevel->size();
    updateShadowRegion();
    buildQuads();
}

QImage Shadow::decorationShadowImage() const
{
    if (!m_decorationShadow) {
        return QImage();
    }
    return m_decorationShadow->shadow();
}

QSize Shadow::elementSize(Shadow::ShadowElements element) const
{
    if (m_decorationShadow) {
        switch (element) {
        case ShadowElementTop:
            return m_decorationShadow->topGeometry().size();
        case ShadowElementTopRight:
            return m_decorationShadow->topRightGeometry().size();
        case ShadowElementRight:
            return m_decorationShadow->rightGeometry().size();
        case ShadowElementBottomRight:
            return m_decorationShadow->bottomRightGeometry().size();
        case ShadowElementBottom:
            return m_decorationShadow->bottomGeometry().size();
        case ShadowElementBottomLeft:
            return m_decorationShadow->bottomLeftGeometry().size();
        case ShadowElementLeft:
            return m_decorationShadow->leftGeometry().size();
        case ShadowElementTopLeft:
            return m_decorationShadow->topLeftGeometry().size();
        default:
            return QSize();
        }
    } else {
        return m_shadowElements[element].size();
    }
}

QMargins Shadow::margins() const
{
    return QMargins(m_leftOffset, m_topOffset, m_rightOffset, m_topOffset);
}

void Shadow::setShadowElement(const QPixmap &shadow, Shadow::ShadowElements element)
{
    m_shadowElements[element] = shadow;
}

} // namespace
