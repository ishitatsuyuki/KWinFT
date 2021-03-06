/**************************************************************************
 * KWin - the KDE window manager                                          *
 * This file is part of the KDE project.                                  *
 *                                                                        *
 * Copyright (C) 2013 Antonis Tsiapaliokas <kok3rs@gmail.com>             *
 * Copyright (C) 2013 Martin Gräßlin <mgraesslin@kde.org>                 *
 *                                                                        *
 * This program is free software; you can redistribute it and/or modify   *
 * it under the terms of the GNU General Public License as published by   *
 * the Free Software Foundation; either version 2 of the License, or      *
 * (at your option) any later version.                                    *
 *                                                                        *
 * This program is distributed in the hope that it will be useful,        *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 * GNU General Public License for more details.                           *
 *                                                                        *
 * You should have received a copy of the GNU General Public License      *
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.  *
 **************************************************************************/


#include "ui_compositing.h"
#include <kwin_compositing_interface.h>

#include <QAction>
#include <QApplication>
#include <QLayout>

#include <kcmodule.h>
#include <kservice.h>

#include <algorithm>
#include <functional>

#include "kwincompositing_setting.h"
#include "kwincompositingdata.h"

static bool isRunningPlasma()
{
    return qgetenv("XDG_CURRENT_DESKTOP") == "KDE";
}

class KWinCompositingKCM : public KCModule
{
    Q_OBJECT
public:
    enum CompositingTypeIndex {
        OPENGL31_INDEX = 0,
        OPENGL20_INDEX,
        XRENDER_INDEX
    };

    explicit KWinCompositingKCM(QWidget *parent = nullptr, const QVariantList &args = QVariantList());

public Q_SLOTS:
    void load() override;
    void save() override;
    void defaults() override;

private Q_SLOTS:
    void onBackendChanged();
    void reenableGl();

private:
    void init();
    void updateUnmanagedItemStatus();
    bool compositingRequired() const;

    Ui_CompositingForm m_form;

    OrgKdeKwinCompositingInterface *m_compositingInterface;
    KWinCompositingSetting *m_settings;
};

static const QVector<qreal> s_animationMultipliers = {8, 4, 2, 1, 0.5, 0.25, 0.125, 0};

bool KWinCompositingKCM::compositingRequired() const
{
    return m_compositingInterface->platformRequiresCompositing();
}

KWinCompositingKCM::KWinCompositingKCM(QWidget *parent, const QVariantList &args)
    : KCModule(parent, args)
    , m_compositingInterface(new OrgKdeKwinCompositingInterface(QStringLiteral("org.kde.KWin"), QStringLiteral("/Compositor"), QDBusConnection::sessionBus(), this))
    , m_settings(new KWinCompositingSetting(this))
{
    m_form.setupUi(this);
    addConfig(m_settings, this);

    m_form.glCrashedWarning->setIcon(QIcon::fromTheme(QStringLiteral("dialog-warning")));
    QAction *reenableGlAction = new QAction(i18n("Re-enable OpenGL detection"), this);
    connect(reenableGlAction, &QAction::triggered, this, &KWinCompositingKCM::reenableGl);
    connect(reenableGlAction, &QAction::triggered, m_form.glCrashedWarning, &KMessageWidget::animatedHide);
    m_form.glCrashedWarning->addAction(reenableGlAction);
    m_form.windowThumbnailWarning->setIcon(QIcon::fromTheme(QStringLiteral("dialog-warning")));

    m_form.kcfg_Enabled->setVisible(!compositingRequired());
    m_form.kcfg_WindowsBlockCompositing->setVisible(!compositingRequired());

    connect(this, &KWinCompositingKCM::defaultsIndicatorsVisibleChanged, this, &KWinCompositingKCM::updateUnmanagedItemStatus);

    init();
}

void KWinCompositingKCM::reenableGl()
{
    m_settings->setOpenGLIsUnsafe(false);
    m_settings->save();
}

void KWinCompositingKCM::init()
{
    auto currentIndexChangedSignal = static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged);

    // animation speed
    m_form.animationDurationFactor->setMaximum(s_animationMultipliers.size() - 1);
    connect(m_form.animationDurationFactor, &QSlider::valueChanged, this, [this]() {
        m_settings->setAnimationDurationFactor(s_animationMultipliers[m_form.animationDurationFactor->value()]);
        updateUnmanagedItemStatus();
    });

    if (isRunningPlasma()) {
        m_form.animationSpeedLabel->hide();
        m_form.animationSpeedControls->hide();
    }

    // windowThumbnail
    connect(m_form.kcfg_HiddenPreviews, currentIndexChangedSignal, this,
        [this](int index) {
            if (index == 2) {
                m_form.windowThumbnailWarning->animatedShow();
            } else {
                m_form.windowThumbnailWarning->animatedHide();
            }
        }
    );

    // compositing type
    m_form.backend->addItem(i18n("OpenGL 3.1"), CompositingTypeIndex::OPENGL31_INDEX);
    m_form.backend->addItem(i18n("OpenGL 2.0"), CompositingTypeIndex::OPENGL20_INDEX);
    m_form.backend->addItem(i18n("XRender"), CompositingTypeIndex::XRENDER_INDEX);

    connect(m_form.backend, currentIndexChangedSignal, this, &KWinCompositingKCM::onBackendChanged);

    if (m_settings->openGLIsUnsafe()) {
        m_form.glCrashedWarning->animatedShow();
    }
}

void KWinCompositingKCM::onBackendChanged()
{
    updateUnmanagedItemStatus();
}

void KWinCompositingKCM::updateUnmanagedItemStatus()
{
    int backend = KWinCompositingSetting::EnumBackend::OpenGL;
    bool glCore = true;
    const int currentType = m_form.backend->currentData().toInt();
    switch (currentType) {
    case CompositingTypeIndex::OPENGL31_INDEX:
        // default already set
        break;
    case CompositingTypeIndex::OPENGL20_INDEX:
        glCore = false;
        break;
    case CompositingTypeIndex::XRENDER_INDEX:
        backend = KWinCompositingSetting::EnumBackend::XRender;
        glCore = false;
        break;
    }
    const auto animationDuration = s_animationMultipliers[m_form.animationDurationFactor->value()];

    const bool inPlasma = isRunningPlasma();

    bool changed = glCore != m_settings->glCore();
    changed |= backend != m_settings->backend();
    if (!inPlasma) {
      changed |= (animationDuration != m_settings->animationDurationFactor());
    }
    unmanagedWidgetChangeState(changed);

    bool defaulted = glCore == m_settings->defaultGlCoreValue();
    defaulted &= backend == m_settings->defaultBackendValue();
    if (!inPlasma) {
        defaulted &= animationDuration == m_settings->defaultAnimationDurationFactorValue();
    }

    m_form.backend->setProperty("_kde_highlight_neutral", defaultsIndicatorsVisible() && (backend != m_settings->defaultBackendValue() || glCore != m_settings->defaultGlCoreValue()));
    m_form.backend->update();

    unmanagedWidgetDefaultState(defaulted);
}

void KWinCompositingKCM::load()
{
    KCModule::load();

    // unmanaged items
    m_settings->findItem("AnimationDurationFactor")->readConfig(m_settings->config());
    const double multiplier = m_settings->animationDurationFactor();
    auto const it = std::lower_bound(s_animationMultipliers.begin(), s_animationMultipliers.end(), multiplier, std::greater<qreal>());
    const int index = static_cast<int>(std::distance(s_animationMultipliers.begin(), it));
    m_form.animationDurationFactor->setValue(index);
    m_form.animationDurationFactor->setDisabled(m_settings->isAnimationDurationFactorImmutable());

    m_settings->findItem("Backend")->readConfig(m_settings->config());
    m_settings->findItem("glCore")->readConfig(m_settings->config());

    if (m_settings->backend() == KWinCompositingSetting::EnumBackend::OpenGL) {
        if (m_settings->glCore()) {
            m_form.backend->setCurrentIndex(CompositingTypeIndex::OPENGL31_INDEX);
        } else {
            m_form.backend->setCurrentIndex(CompositingTypeIndex::OPENGL20_INDEX);
        }
    } else {
        m_form.backend->setCurrentIndex(CompositingTypeIndex::XRENDER_INDEX);
    }
    m_form.backend->setDisabled(m_settings->isBackendImmutable());

    onBackendChanged();
}

void KWinCompositingKCM::defaults()
{
    KCModule::defaults();

    // unmanaged widgets
    m_form.backend->setCurrentIndex(CompositingTypeIndex::OPENGL20_INDEX);
    // corresponds to 1.0 seconds in s_animationMultipliers
    m_form.animationDurationFactor->setValue(3);
}

void KWinCompositingKCM::save()
{
    int backend = KWinCompositingSetting::EnumBackend::OpenGL;
    bool glCore = true;
    const int currentType = m_form.backend->currentData().toInt();
    switch (currentType) {
    case CompositingTypeIndex::OPENGL31_INDEX:
        // default already set
        break;
    case CompositingTypeIndex::OPENGL20_INDEX:
        backend = KWinCompositingSetting::EnumBackend::OpenGL;
        glCore = false;
        break;
    case CompositingTypeIndex::XRENDER_INDEX:
        backend = KWinCompositingSetting::EnumBackend::XRender;
        glCore = false;
        break;
    }
    m_settings->setBackend(backend);
    m_settings->setGlCore(glCore);

    const auto animationDuration = s_animationMultipliers[m_form.animationDurationFactor->value()];
    m_settings->setAnimationDurationFactor(animationDuration);
    m_settings->save();

    KCModule::save();

    // Send signal to all kwin instances
    QDBusMessage message = QDBusMessage::createSignal(QStringLiteral("/Compositor"),
                                                      QStringLiteral("org.kde.kwin.Compositing"),
                                                      QStringLiteral("reinit"));
    QDBusConnection::sessionBus().send(message);
}

K_PLUGIN_FACTORY(KWinCompositingConfigFactory,
                 registerPlugin<KWinCompositingKCM>();
                 registerPlugin<KWinCompositingData>();
                )

#include "main.moc"
