/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

 Copyright (C) 2016 Martin Gräßlin <mgraesslin@kde.org>

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
#include "colorpicker.h"
#include <kwinglutils.h>
#include <kwinglutils_funcs.h>
#include <QDBusConnection>
#include <KLocalizedString>
#include <QDBusMetaType>

Q_DECLARE_METATYPE(QColor)

QDBusArgument &operator<< (QDBusArgument &argument, const QColor &color)
{
    argument.beginStructure();
    argument << color.rgba();
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, QColor &color)
{
    argument.beginStructure();
    QRgb rgba;
    argument >> rgba;
    argument.endStructure();
    color = QColor::fromRgba(rgba);
    return argument;
}

namespace KWin
{

bool ColorPickerEffect::supported()
{
    return effects->isOpenGLCompositing();
}

ColorPickerEffect::ColorPickerEffect()
    : m_scheduledPosition(QPoint(-1, -1))
{
    qDBusRegisterMetaType<QColor>();
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/ColorPicker"), this, QDBusConnection::ExportScriptableContents);
}

ColorPickerEffect::~ColorPickerEffect() = default;

void ColorPickerEffect::paintScreen(int mask, QRegion region, ScreenPaintData &data)
{
    m_cachedOutputGeometry = data.outputGeometry();
    effects->paintScreen(mask, region, data);

    if (m_infoFrame) {
        m_infoFrame->render(region);
    }
}

void ColorPickerEffect::postPaintScreen()
{
    effects->postPaintScreen();

    if (m_scheduledPosition != QPoint(-1, -1) && (m_cachedOutputGeometry.isEmpty() || m_cachedOutputGeometry.contains(m_scheduledPosition))) {
        uint8_t data[3];
        const QRect geo = GLRenderTarget::virtualScreenGeometry();
        glReadnPixels(m_scheduledPosition.x() - geo.x(), geo.height() - geo.y() - m_scheduledPosition.y(), 1, 1, GL_RGB, GL_UNSIGNED_BYTE, 3, data);
        QDBusConnection::sessionBus().send(m_replyMessage.createReply(QColor(data[0], data[1], data[2])));
        m_picking = false;
        m_scheduledPosition = QPoint(-1, -1);
    }
}

QColor ColorPickerEffect::pick()
{
    if (!calledFromDBus()) {
        return QColor();
    }
    if (m_picking) {
        sendErrorReply(QDBusError::Failed, "Color picking is already in progress");
        return QColor();
    }
    m_picking = true;
    m_replyMessage = message();
    setDelayedReply(true);
    showInfoMessage();
    effects->startInteractivePositionSelection(
        [this] (const QPoint &p) {
            hideInfoMessage();
            if (p == QPoint(-1, -1)) {
                // error condition
                QDBusConnection::sessionBus().send(m_replyMessage.createErrorReply(QStringLiteral("org.kde.kwin.ColorPicker.Error.Cancelled"), "Color picking got cancelled"));
                m_picking = false;
            } else {
                m_scheduledPosition = p;
                effects->addRepaintFull();
            }
        }
    );
    return QColor();
}

void ColorPickerEffect::showInfoMessage()
{
    if (!m_infoFrame.isNull()) {
        return;
    }
    // TODO: turn the info message into a system wide service which performs hiding on mouse over
    m_infoFrame.reset(effects->effectFrame(EffectFrameStyled, false));
    QFont font;
    font.setBold(true);
    m_infoFrame->setFont(font);
    QRect area = effects->clientArea(ScreenArea, effects->activeScreen(), effects->currentDesktop());
    m_infoFrame->setPosition(QPoint(area.x() + area.width() / 2, area.y() + area.height() / 3));
    m_infoFrame->setText(i18n("Select a position for color picking with left click or enter.\nEscape or right click to cancel."));
    effects->addRepaintFull();
}

void ColorPickerEffect::hideInfoMessage()
{
    m_infoFrame.reset();
}

bool ColorPickerEffect::isActive() const
{
    return m_picking && ((m_scheduledPosition != QPoint(-1, -1)) || !m_infoFrame.isNull()) && !effects->isScreenLocked();
}

} // namespace