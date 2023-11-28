#include "graphicspage.hpp"

#include "sdlinit.hpp"

#include <components/misc/display.hpp>
#include <components/settings/values.hpp>

#include <QMessageBox>
#include <QScreen>

#ifdef MAC_OS_X_VERSION_MIN_REQUIRED
#undef MAC_OS_X_VERSION_MIN_REQUIRED
// We need to do this because of Qt: https://bugreports.qt-project.org/browse/QTBUG-22154
#define MAC_OS_X_VERSION_MIN_REQUIRED __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__
#endif // MAC_OS_X_VERSION_MIN_REQUIRED

#include <SDL_video.h>

#include <array>

Launcher::GraphicsPage::GraphicsPage(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("GraphicsPage");
    setupUi(this);

    // Set the maximum res we can set in windowed mode
    QRect res = getMaximumResolution();
    customWidthSpinBox->setMaximum(res.width());
    customHeightSpinBox->setMaximum(res.height());

    connect(windowModeComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
        &GraphicsPage::slotFullScreenChanged);
    connect(standardRadioButton, &QRadioButton::toggled, this, &GraphicsPage::slotStandardToggled);
    connect(screenComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this, &GraphicsPage::screenChanged);
    connect(framerateLimitCheckBox, &QCheckBox::toggled, this, &GraphicsPage::slotFramerateLimitToggled);
    connect(shadowDistanceCheckBox, &QCheckBox::toggled, this, &GraphicsPage::slotShadowDistLimitToggled);
}

bool Launcher::GraphicsPage::setupSDL()
{
    bool sdlConnectSuccessful = initSDL();
    if (!sdlConnectSuccessful)
    {
        return false;
    }

    int displays = SDL_GetNumVideoDisplays();

    if (displays < 0)
    {
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("Error receiving number of screens"));
        msgBox.setIcon(QMessageBox::Critical);
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setText(
            tr("<br><b>SDL_GetNumVideoDisplays failed:</b><br><br>") + QString::fromUtf8(SDL_GetError()) + "<br>");
        msgBox.exec();
        return false;
    }

    screenComboBox->clear();
    mResolutionsPerScreen.clear();
    for (int i = 0; i < displays; i++)
    {
        mResolutionsPerScreen.append(getAvailableResolutions(i));
        screenComboBox->addItem(QString(tr("Screen ")) + QString::number(i + 1));
    }
    screenChanged(0);

    // Disconnect from SDL processes
    quitSDL();

    return true;
}

bool Launcher::GraphicsPage::loadSettings()
{
    if (!setupSDL())
        return false;

    // Visuals

    const int vsync = Settings::video().mVsyncMode;

    vSyncComboBox->setCurrentIndex(vsync);

    const Settings::WindowMode windowMode = Settings::video().mWindowMode;

    windowModeComboBox->setCurrentIndex(static_cast<int>(windowMode));
    handleWindowModeChange(windowMode);

    if (Settings::video().mWindowBorder)
        windowBorderCheckBox->setCheckState(Qt::Checked);

    // aaValue is the actual value (0, 1, 2, 4, 8, 16)
    const int aaValue = Settings::video().mAntialiasing;
    // aaIndex is the index into the allowed values in the pull down.
    const int aaIndex = antiAliasingComboBox->findText(QString::number(aaValue));
    if (aaIndex != -1)
        antiAliasingComboBox->setCurrentIndex(aaIndex);

    const int width = Settings::video().mResolutionX;
    const int height = Settings::video().mResolutionY;
    QString resolution = QString::number(width) + QString(" x ") + QString::number(height);
    screenComboBox->setCurrentIndex(Settings::video().mScreen);

    int resIndex = resolutionComboBox->findText(resolution, Qt::MatchStartsWith);

    if (resIndex != -1)
    {
        standardRadioButton->toggle();
        resolutionComboBox->setCurrentIndex(resIndex);
    }
    else
    {
        customRadioButton->toggle();
        customWidthSpinBox->setValue(width);
        customHeightSpinBox->setValue(height);
    }

    const float fpsLimit = Settings::video().mFramerateLimit;
    if (fpsLimit != 0)
    {
        framerateLimitCheckBox->setCheckState(Qt::Checked);
        framerateLimitSpinBox->setValue(fpsLimit);
    }

    // Lighting
    int lightingMethod = 1;
    switch (Settings::shaders().mLightingMethod)
    {
        case SceneUtil::LightingMethod::FFP:
            lightingMethod = 0;
            break;
        case SceneUtil::LightingMethod::PerObjectUniform:
            lightingMethod = 1;
            break;
        case SceneUtil::LightingMethod::SingleUBO:
            lightingMethod = 2;
            break;
    }
    lightingMethodComboBox->setCurrentIndex(lightingMethod);

    // Shadows
    if (Settings::shadows().mActorShadows)
        actorShadowsCheckBox->setCheckState(Qt::Checked);
    if (Settings::shadows().mPlayerShadows)
        playerShadowsCheckBox->setCheckState(Qt::Checked);
    if (Settings::shadows().mTerrainShadows)
        terrainShadowsCheckBox->setCheckState(Qt::Checked);
    if (Settings::shadows().mObjectShadows)
        objectShadowsCheckBox->setCheckState(Qt::Checked);
    if (Settings::shadows().mEnableIndoorShadows)
        indoorShadowsCheckBox->setCheckState(Qt::Checked);

    shadowComputeSceneBoundsComboBox->setCurrentIndex(
        shadowComputeSceneBoundsComboBox->findText(QString(tr(Settings::shadows().mComputeSceneBounds.get().c_str()))));

    const int shadowDistLimit = Settings::shadows().mMaximumShadowMapDistance;
    if (shadowDistLimit > 0)
    {
        shadowDistanceCheckBox->setCheckState(Qt::Checked);
        shadowDistanceSpinBox->setValue(shadowDistLimit);
    }

    const float shadowFadeStart = Settings::shadows().mShadowFadeStart;
    if (shadowFadeStart != 0)
        fadeStartSpinBox->setValue(shadowFadeStart);

    const int shadowRes = Settings::shadows().mShadowMapResolution;
    int shadowResIndex = shadowResolutionComboBox->findText(QString::number(shadowRes));
    if (shadowResIndex != -1)
        shadowResolutionComboBox->setCurrentIndex(shadowResIndex);

    return true;
}

void Launcher::GraphicsPage::saveSettings()
{
    // Visuals

    Settings::video().mVsyncMode.set(static_cast<SDLUtil::VSyncMode>(vSyncComboBox->currentIndex()));
    Settings::video().mWindowMode.set(static_cast<Settings::WindowMode>(windowModeComboBox->currentIndex()));
    Settings::video().mWindowBorder.set(windowBorderCheckBox->checkState() == Qt::Checked);
    Settings::video().mAntialiasing.set(antiAliasingComboBox->currentText().toInt());

    int cWidth = 0;
    int cHeight = 0;
    if (standardRadioButton->isChecked())
    {
        QRegularExpression resolutionRe("^(\\d+) x (\\d+)");
        QRegularExpressionMatch match = resolutionRe.match(resolutionComboBox->currentText().simplified());
        if (match.hasMatch())
        {
            cWidth = match.captured(1).toInt();
            cHeight = match.captured(2).toInt();
        }
    }
    else
    {
        cWidth = customWidthSpinBox->value();
        cHeight = customHeightSpinBox->value();
    }

    Settings::video().mResolutionX.set(cWidth);
    Settings::video().mResolutionY.set(cHeight);
    Settings::video().mScreen.set(screenComboBox->currentIndex());

    if (framerateLimitCheckBox->checkState() != Qt::Unchecked)
    {
        Settings::video().mFramerateLimit.set(framerateLimitSpinBox->value());
    }
    else if (Settings::video().mFramerateLimit != 0)
    {
        Settings::video().mFramerateLimit.set(0);
    }

    // Lighting
    static constexpr std::array<SceneUtil::LightingMethod, 3> lightingMethodMap = {
        SceneUtil::LightingMethod::FFP,
        SceneUtil::LightingMethod::PerObjectUniform,
        SceneUtil::LightingMethod::SingleUBO,
    };
    Settings::shaders().mLightingMethod.set(lightingMethodMap[lightingMethodComboBox->currentIndex()]);

    // Shadows
    const int cShadowDist = shadowDistanceCheckBox->checkState() != Qt::Unchecked ? shadowDistanceSpinBox->value() : 0;
    Settings::shadows().mMaximumShadowMapDistance.set(cShadowDist);
    const float cFadeStart = fadeStartSpinBox->value();
    if (cShadowDist > 0)
        Settings::shadows().mShadowFadeStart.set(cFadeStart);

    const bool cActorShadows = actorShadowsCheckBox->checkState() != Qt::Unchecked;
    const bool cObjectShadows = objectShadowsCheckBox->checkState() != Qt::Unchecked;
    const bool cTerrainShadows = terrainShadowsCheckBox->checkState() != Qt::Unchecked;
    const bool cPlayerShadows = playerShadowsCheckBox->checkState() != Qt::Unchecked;
    if (cActorShadows || cObjectShadows || cTerrainShadows || cPlayerShadows)
    {
        Settings::shadows().mEnableShadows.set(true);
        Settings::shadows().mActorShadows.set(cActorShadows);
        Settings::shadows().mPlayerShadows.set(cPlayerShadows);
        Settings::shadows().mObjectShadows.set(cObjectShadows);
        Settings::shadows().mTerrainShadows.set(cTerrainShadows);
    }
    else
    {
        Settings::shadows().mEnableShadows.set(false);
        Settings::shadows().mActorShadows.set(false);
        Settings::shadows().mPlayerShadows.set(false);
        Settings::shadows().mObjectShadows.set(false);
        Settings::shadows().mTerrainShadows.set(false);
    }

    Settings::shadows().mEnableIndoorShadows.set(indoorShadowsCheckBox->checkState() != Qt::Unchecked);
    Settings::shadows().mShadowMapResolution.set(shadowResolutionComboBox->currentText().toInt());
    Settings::shadows().mComputeSceneBounds.set(shadowComputeSceneBoundsComboBox->currentText().toStdString());
}

QStringList Launcher::GraphicsPage::getAvailableResolutions(int screen)
{
    QStringList result;
    SDL_DisplayMode mode;
    int modeIndex, modes = SDL_GetNumDisplayModes(screen);

    if (modes < 0)
    {
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("Error receiving resolutions"));
        msgBox.setIcon(QMessageBox::Critical);
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setText(
            tr("<br><b>SDL_GetNumDisplayModes failed:</b><br><br>") + QString::fromUtf8(SDL_GetError()) + "<br>");
        msgBox.exec();
        return result;
    }

    for (modeIndex = 0; modeIndex < modes; modeIndex++)
    {
        if (SDL_GetDisplayMode(screen, modeIndex, &mode) < 0)
        {
            QMessageBox msgBox;
            msgBox.setWindowTitle(tr("Error receiving resolutions"));
            msgBox.setIcon(QMessageBox::Critical);
            msgBox.setStandardButtons(QMessageBox::Ok);
            msgBox.setText(
                tr("<br><b>SDL_GetDisplayMode failed:</b><br><br>") + QString::fromUtf8(SDL_GetError()) + "<br>");
            msgBox.exec();
            return result;
        }

        auto str = Misc::getResolutionText(mode.w, mode.h);
        result.append(QString(str.c_str()));
    }

    result.removeDuplicates();
    return result;
}

QRect Launcher::GraphicsPage::getMaximumResolution()
{
    QRect max;

    for (QScreen* screen : QGuiApplication::screens())
    {
        QRect res = screen->geometry();
        if (res.width() > max.width())
            max.setWidth(res.width());
        if (res.height() > max.height())
            max.setHeight(res.height());
    }
    return max;
}

void Launcher::GraphicsPage::screenChanged(int screen)
{
    if (screen >= 0)
    {
        resolutionComboBox->clear();
        resolutionComboBox->addItems(mResolutionsPerScreen[screen]);
    }
}

void Launcher::GraphicsPage::slotFullScreenChanged(int mode)
{
    handleWindowModeChange(static_cast<Settings::WindowMode>(mode));
}

void Launcher::GraphicsPage::handleWindowModeChange(Settings::WindowMode mode)
{
    if (mode == Settings::WindowMode::Fullscreen || mode == Settings::WindowMode::WindowedFullscreen)
    {
        standardRadioButton->toggle();
        customRadioButton->setEnabled(false);
        customWidthSpinBox->setEnabled(false);
        customHeightSpinBox->setEnabled(false);
        windowBorderCheckBox->setEnabled(false);
    }
    else
    {
        customRadioButton->setEnabled(true);
        customWidthSpinBox->setEnabled(true);
        customHeightSpinBox->setEnabled(true);
        windowBorderCheckBox->setEnabled(true);
    }
}

void Launcher::GraphicsPage::slotStandardToggled(bool checked)
{
    if (checked)
    {
        resolutionComboBox->setEnabled(true);
        customWidthSpinBox->setEnabled(false);
        customHeightSpinBox->setEnabled(false);
    }
    else
    {
        resolutionComboBox->setEnabled(false);
        customWidthSpinBox->setEnabled(true);
        customHeightSpinBox->setEnabled(true);
    }
}

void Launcher::GraphicsPage::slotFramerateLimitToggled(bool checked)
{
    framerateLimitSpinBox->setEnabled(checked);
}

void Launcher::GraphicsPage::slotShadowDistLimitToggled(bool checked)
{
    shadowDistanceSpinBox->setEnabled(checked);
    fadeStartSpinBox->setEnabled(checked);
}
