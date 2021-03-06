/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

 Copyright (C) 2007 Lubos Lunak <l.lunak@kde.org>
 Copyright (C) 2013 Martin Gräßlin <mgraesslin@kde.org>

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

"use strict";

var loginEffect = {
    duration: animationTime(1000),
    isFadeToBlack: false,
    loadConfig: function () {
        loginEffect.isFadeToBlack = effect.readConfig("FadeToBlack", false);
    },
    isLoginSplash: function (window) {
        return window.windowClass === "ksplashqml ksplashqml";
    },
    fadeOut: function (window) {
        animate({
            window: window,
            duration: loginEffect.duration,
            type: Effect.Opacity,
            from: 1.0,
            to: 0.0
        });
    },
    fadeToBlack: function (window) {
        animate({
            window: window,
            duration: loginEffect.duration / 2,
            animations: [{
                type: Effect.Brightness,
                from: 1.0,
                to: 0.0
            }, {
                type: Effect.Opacity,
                from: 1.0,
                to: 0.0,
                delay: loginEffect.duration / 2
            }, {
                // TODO: is there a better way to keep brightness constant?
                type: Effect.Brightness,
                from: 0.0,
                to: 0.0,
                delay: loginEffect.duration / 2
            }]
        });
    },
    closed: function (window) {
        if (!loginEffect.isLoginSplash(window)) {
            return;
        }
        if (loginEffect.isFadeToBlack === true) {
            loginEffect.fadeToBlack(window);
        } else {
            loginEffect.fadeOut(window);
        }
    },
    init: function () {
        effect.configChanged.connect(loginEffect.loadConfig);
        effects.windowClosed.connect(loginEffect.closed);
        loginEffect.loadConfig();
    }
};
loginEffect.init();
