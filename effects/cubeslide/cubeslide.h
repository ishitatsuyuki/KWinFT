/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

 Copyright (C) 2009 Martin Gräßlin <mgraesslin@kde.org>

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

#ifndef KWIN_CUBESLIDE_H
#define KWIN_CUBESLIDE_H

#include <kwineffects.h>
#include <kwinglutils.h>
#include <QQueue>
#include <QSet>
#include <QTimeLine>

namespace KWin
{
class CubeSlideEffect
    : public Effect
{
    Q_OBJECT
    Q_PROPERTY(int rotationDuration READ configuredRotationDuration)
    Q_PROPERTY(bool dontSlidePanels READ isDontSlidePanels)
    Q_PROPERTY(bool dontSlideStickyWindows READ isDontSlideStickyWindows)
    Q_PROPERTY(bool usePagerLayout READ isUsePagerLayout)
    Q_PROPERTY(bool useWindowMoving READ isUseWindowMoving)
public:
    CubeSlideEffect();
    ~CubeSlideEffect() override;
    void reconfigure(ReconfigureFlags) override;
    void prePaintScreen(ScreenPrePaintData& data, std::chrono::milliseconds presentTime) override;
    void paintScreen(int mask, const QRegion &region, ScreenPaintData& data) override;
    void postPaintScreen() override;
    void prePaintWindow(EffectWindow* w, WindowPrePaintData& data, std::chrono::milliseconds presentTime) override;
    void paintWindow(EffectWindow* w, int mask, QRegion region, WindowPaintData& data) override;
    bool isActive() const override;

    int requestedEffectChainPosition() const override {
        return 50;
    }

    static bool supported();

    // for properties
    int configuredRotationDuration() const {
        return rotationDuration;
    }
    bool isDontSlidePanels() const {
        return dontSlidePanels;
    }
    bool isDontSlideStickyWindows() const {
        return dontSlideStickyWindows;
    }
    bool isUsePagerLayout() const {
        return usePagerLayout;
    }
    bool isUseWindowMoving() const {
        return useWindowMoving;
    }
private Q_SLOTS:
    void slotWindowAdded(EffectWindow* w);
    void slotWindowDeleted(EffectWindow* w);

    void slotDesktopChanged(int old, int current, EffectWindow* w);
    void slotWindowStepUserMovedResized(KWin::EffectWindow *w);
    void slotWindowFinishUserMovedResized(KWin::EffectWindow *w);
    void slotNumberDesktopsChanged();

private:
    enum RotationDirection {
        Left,
        Right,
        Upwards,
        Downwards
    };
    void paintSlideCube(int mask, QRegion region, ScreenPaintData& data);
    void windowMovingChanged(float progress, RotationDirection direction);

    bool shouldAnimate(const EffectWindow* w) const;
    void startAnimation();

    bool cube_painting;
    int front_desktop;
    int painting_desktop;
    int other_desktop;
    bool firstDesktop;
    bool stickyPainting;
    QSet<EffectWindow*> staticWindows;
    QTimeLine timeLine;
    std::chrono::milliseconds lastPresentTime;
    QQueue<RotationDirection> slideRotations;
    bool dontSlidePanels;
    bool dontSlideStickyWindows;
    bool usePagerLayout;
    int rotationDuration;
    bool useWindowMoving;
    bool windowMoving;
    bool desktopChangedWhileMoving;
    double progressRestriction;
};
}

#endif
