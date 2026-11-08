#include "settingswindow.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include <unicode/locid.h>

#include <MyGUI_ComboBox.h>
#include <MyGUI_Gui.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_ScrollBar.h>
#include <MyGUI_ScrollView.h>
#include <MyGUI_TabControl.h>
#include <MyGUI_TabItem.h>
#include <MyGUI_TextBox.h>
#include <MyGUI_UString.h>
#include <MyGUI_Window.h>

#include <SDL_video.h>

#include <components/debug/debuglog.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/l10n/manager.hpp>
#include <components/lua_ui/scriptsettings.hpp>
#include <components/misc/constants.hpp>
#include <components/misc/display.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/sceneutil/lightmanager.hpp>
#include <components/settings/values.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/recursivedirectoryiterator.hpp>
#include <components/widgets/sharedstatebutton.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/inputmanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwlua/luamanagerimp.hpp"

#include "confirmationdialog.hpp"
#include "textcolours.hpp"
#include "weightedsearch.hpp"

namespace
{
    std::string_view textureFilteringToStr(const std::string& mipFilter, const std::string& magFilter)
    {
        if (mipFilter == "none")
            return "#{OMWEngine:TextureFilteringDisabled}";

        if (magFilter == "linear")
        {
            if (mipFilter == "linear")
                return "#{OMWEngine:TextureFilteringTrilinear}";
            if (mipFilter == "nearest")
                return "#{OMWEngine:TextureFilteringBilinear}";
        }
        else if (magFilter == "nearest")
            return "#{OMWEngine:TextureFilteringNearest}";

        Log(Debug::Warning) << "Warning: Invalid texture filtering options: " << mipFilter << ", " << magFilter;
        return "#{OMWEngine:TextureFilteringOther}";
    }

    bool sortResolutions(std::pair<int, int> left, std::pair<int, int> right)
    {
        if (left.first == right.first)
            return left.second > right.second;
        return left.first > right.first;
    }

    const std::string_view checkButtonType = "CheckButton";
    const std::string_view sliderType = "Slider";

    constexpr std::array<std::string_view, 4> dialogueColourToggleSettings{
        "dialogue colouring", "dialogue use system colour", "dialogue classify narration", "journal colour narration"
    };
    constexpr std::array<std::string_view, 7> dialogueColourSettings{ "color dialogue normal",
        "color dialogue inventory", "color dialogue stat", "color dialogue narration", "color dialogue journal",
        "color journal narration", "color dialogue topic" };
    constexpr std::array<std::string_view, 7> dialogueColourSamples{ "#{OMWEngine:DialogueColourSampleNormal}",
        "#{OMWEngine:DialogueColourSampleInventory}", "#{OMWEngine:DialogueColourSampleStat}",
        "#{OMWEngine:DialogueColourSampleNarration}", "#{OMWEngine:DialogueColourSampleJournal}",
        "#{OMWEngine:DialogueColourSampleJournalNarration}", "#{OMWEngine:DialogueColourSampleTopic}" };

    size_t colourChannelPosition(float value)
    {
        if (!std::isfinite(value))
            return 0;
        return static_cast<size_t>(std::lround(std::clamp(value, 0.f, 1.f) * 255.f));
    }

    std::string_view getSettingType(MyGUI::Widget* widget)
    {
        return widget->getUserString("SettingType");
    }

    std::string_view getSettingName(MyGUI::Widget* widget)
    {
        return widget->getUserString("SettingName");
    }

    std::string_view getSettingCategory(MyGUI::Widget* widget)
    {
        return widget->getUserString("SettingCategory");
    }

    std::string_view getSettingValueType(MyGUI::Widget* widget)
    {
        return widget->getUserString("SettingValueType");
    }

    void getSettingMinMax(MyGUI::Widget* widget, float& min, float& max)
    {
        const char* settingMin = "SettingMin";
        const char* settingMax = "SettingMax";
        min = 0.f;
        max = 1.f;
        if (!widget->getUserString(settingMin).empty())
            min = MyGUI::utility::parseFloat(widget->getUserString(settingMin));
        if (!widget->getUserString(settingMax).empty())
            max = MyGUI::utility::parseFloat(widget->getUserString(settingMax));
    }

    void updateMaxLightsComboBox(MyGUI::ComboBox* box)
    {
        constexpr int min = 8;
        constexpr int max = 64;
        constexpr int increment = 8;
        const int maxLights = Settings::shaders().mMaxLights;
        // show increments of 8 in dropdown
        if (maxLights >= min && maxLights <= max && !(maxLights % increment))
            box->setIndexSelected((maxLights / increment) - 1);
        else
            box->setIndexSelected(MyGUI::ITEM_NONE);
    }

    void updateAnisotropyComboBox(MyGUI::ComboBox* box)
    {
        const int anisotropy = Settings::general().mAnisotropy;
        if (anisotropy <= 1)
            box->setIndexSelected(0);
        else if (anisotropy <= 2)
            box->setIndexSelected(1);
        else if (anisotropy <= 4)
            box->setIndexSelected(2);
        else if (anisotropy <= 8)
            box->setIndexSelected(3);
        else
            box->setIndexSelected(4);
    }

    void updateShadowMapResolutionComboBox(MyGUI::ComboBox* box)
    {
        const int resolution = Settings::shadows().mShadowMapResolution;
        if (resolution == 512)
            box->setIndexSelected(0);
        else if (resolution == 1024)
            box->setIndexSelected(1);
        else if (resolution == 2048)
            box->setIndexSelected(2);
        else if (resolution == 4096)
            box->setIndexSelected(3);
        else
        {
            box->addItem(std::to_string(resolution), resolution);
            box->setIndexSelected(4);
        }
    }

    void updateSliderLabel(MyGUI::ScrollBar* scroller, MyGUI::TextBox* textBox,
        const std::vector<icu::UnicodeString>& argNames, const std::vector<icu::Formattable>& args)
    {
        if (textBox != nullptr)
        {
            auto l10n = MWBase::Environment::get().getL10nManager()->getContext("OMWEngine");
            std::string labelCaption
                = l10n->formatMessage(scroller->getUserString("SettingLabelCaption"), argNames, args);
            textBox->setCaption(labelCaption);
        }
    }
}

namespace MWGui
{
    void SettingsWindow::configureWidgets(MyGUI::Widget* widget, bool init)
    {
        MyGUI::EnumeratorWidgetPtr widgets = widget->getEnumerator();
        while (widgets.next())
        {
            MyGUI::Widget* current = widgets.current();

            std::string_view type = getSettingType(current);
            if (type == checkButtonType)
            {
                std::string_view initialValue
                    = Settings::get<bool>(getSettingCategory(current), getSettingName(current)) ? "#{Interface:On}"
                                                                                                : "#{Interface:Off}";
                current->castType<MyGUI::Button>()->setCaptionWithReplacing(initialValue);
                if (init)
                    current->eventMouseButtonClick += MyGUI::newDelegate(this, &SettingsWindow::onButtonToggled);
            }
            if (type == sliderType)
            {
                MyGUI::ScrollBar* scroll = current->castType<MyGUI::ScrollBar>();
                std::string_view valueType = getSettingValueType(current);
                std::vector<icu::UnicodeString> argNames;
                std::vector<icu::Formattable> args;
                if (valueType == "Float" || valueType == "Integer" || valueType == "Cell")
                {
                    // TODO: ScrollBar isn't meant for this. should probably use a dedicated FloatSlider widget
                    float min, max;
                    getSettingMinMax(scroll, min, max);
                    float value;

                    if (valueType == "Cell")
                    {
                        value = Settings::get<float>(getSettingCategory(current), getSettingName(current));
                        argNames.emplace_back("cells");
                        args.emplace_back(value / Constants::CellSizeInUnits);
                    }
                    else if (valueType == "Float")
                    {
                        value = Settings::get<float>(getSettingCategory(current), getSettingName(current));
                        argNames.emplace_back("value");
                        args.emplace_back(value);
                    }
                    else
                    {
                        const int intValue = Settings::get<int>(getSettingCategory(current), getSettingName(current));
                        argNames.emplace_back("value");
                        args.emplace_back(intValue);
                        value = static_cast<float>(intValue);
                    }

                    value = std::clamp(value, min, max);
                    value = (value - min) / (max - min);

                    scroll->setScrollPosition(static_cast<size_t>(value * (scroll->getScrollRange() - 1)));
                }
                else
                {
                    const int value = Settings::get<int>(getSettingCategory(current), getSettingName(current));
                    argNames.emplace_back("value");
                    args.emplace_back(value);
                    scroll->setScrollPosition(value);
                }
                if (init)
                    scroll->eventScrollChangePosition
                        += MyGUI::newDelegate(this, &SettingsWindow::onSliderChangePosition);
                if (scroll->getVisible())
                    updateSliderLabel(scroll, getSliderLabel(scroll), argNames, args);
            }

            configureWidgets(current, init);
        }
    }

    void SettingsWindow::onFrame(float duration)
    {
        if (mScriptView->getVisible())
        {
            const auto scriptsSize = mScriptAdapter->getSize();
            if (mScriptView->getCanvasSize() != scriptsSize)
                mScriptView->setCanvasSize(scriptsSize);
        }
    }

    MyGUI::TextBox* SettingsWindow::getSliderLabel(MyGUI::ScrollBar* scroller) const
    {
        auto labelWidgetName = scroller->getUserString("SettingLabelWidget");
        if (!labelWidgetName.empty())
        {
            MyGUI::TextBox* textBox;
            getWidget(textBox, labelWidgetName);
            return textBox;
        }
        return nullptr;
    }

    SettingsWindow::SettingsWindow(Files::ConfigurationManager& cfgMgr)
        : WindowBase("openmw_settings_window.layout")
        , mKeyboardMode(true)
        , mCurrentPage(static_cast<size_t>(-1))
        , mCfgMgr(cfgMgr)
    {
        const bool terrain = Settings::terrain().mDistantTerrain;
        const std::string_view widgetName = terrain ? "RenderingDistanceSlider" : "LargeRenderingDistanceSlider";
        MyGUI::Widget* unusedSlider;
        getWidget(unusedSlider, widgetName);
        unusedSlider->setVisible(false);

        configureWidgets(mMainWidget, true);

        setTitle("#{OMWEngine:SettingsWindow}");

        getWidget(mSettingsTab, "SettingsTab");
        getWidget(mOkButton, "OkButton");
        getWidget(mResolutionList, "ResolutionList");
        getWidget(mWindowModeList, "WindowModeList");
        getWidget(mVSyncModeList, "VSyncModeList");
        getWidget(mWindowBorderButton, "WindowBorderButton");
        getWidget(mTextureFilteringButton, "TextureFilteringButton");
        getWidget(mAnisotropy, "Anisotropy");
        getWidget(mControlsBox, "ControlsBox");
        getWidget(mResetControlsButton, "ResetControlsButton");
        getWidget(mKeyboardSwitch, "KeyboardButton");
        getWidget(mControllerSwitch, "ControllerButton");
        getWidget(mWaterRefractionButton, "WaterRefractionButton");
        getWidget(mWaterTextureSize, "WaterTextureSize");
        getWidget(mWaterReflectionDetail, "WaterReflectionDetail");
        getWidget(mWaterRainRippleDetail, "WaterRainRippleDetail");
        getWidget(mPrimaryLanguage, "PrimaryLanguage");
        getWidget(mSecondaryLanguage, "SecondaryLanguage");
        getWidget(mGmstOverridesL10n, "GmstOverridesL10nButton");
        getWidget(mWindowModeHint, "WindowModeHint");
        getWidget(mClusteredLightingButton, "ClusteredLightingButton");
        getWidget(mLightsResetButton, "LightsResetButton");
        getWidget(mMaxLights, "MaxLights");
        getWidget(mScriptFilter, "ScriptFilter");
        getWidget(mScriptList, "ScriptList");
        getWidget(mScriptBox, "ScriptBox");
        getWidget(mScriptView, "ScriptView");
        getWidget(mScriptAdapter, "ScriptAdapter");
        getWidget(mScriptDisabled, "ScriptDisabled");
        getWidget(mClassicFalloffWidget, "ClassicFalloffWidget");
        getWidget(mMinimumBrightnessText, "MinimumBrightnessText");
        getWidget(mMinimumBrightnessScroll, "MinimumBrightnessScroll");
        getWidget(mActorShadowsButton, "ActorShadowsButton");
        getWidget(mPlayerShadowsButton, "PlayerShadowsButton");
        getWidget(mTerrainShadowsButton, "TerrainShadowsButton");
        getWidget(mObjectShadowsButton, "ObjectShadowsButton");
        getWidget(mShadowMapResolution, "ShadowMapResolution");

#ifndef WIN32
        // hide gamma controls since it currently does not work under Linux
        MyGUI::ScrollBar* gammaSlider;
        getWidget(gammaSlider, "GammaSlider");
        gammaSlider->setVisible(false);
        MyGUI::TextBox* textBox;
        getWidget(textBox, "GammaText");
        textBox->setVisible(false);
        getWidget(textBox, "GammaTextDark");
        textBox->setVisible(false);
        getWidget(textBox, "GammaTextLight");
        textBox->setVisible(false);
#endif

        mMainWidget->castType<MyGUI::Window>()->eventWindowChangeCoord
            += MyGUI::newDelegate(this, &SettingsWindow::onWindowResize);

        mSettingsTab->eventTabChangeSelect += MyGUI::newDelegate(this, &SettingsWindow::onTabChanged);
        mOkButton->eventMouseButtonClick += MyGUI::newDelegate(this, &SettingsWindow::onOkButtonClicked);
        mTextureFilteringButton->eventComboChangePosition
            += MyGUI::newDelegate(this, &SettingsWindow::onTextureFilteringChanged);
        mAnisotropy->eventComboChangePosition += MyGUI::newDelegate(this, &SettingsWindow::onAnisotropyChanged);
        mResolutionList->eventListChangePosition += MyGUI::newDelegate(this, &SettingsWindow::onResolutionSelected);

        mWaterTextureSize->eventComboChangePosition
            += MyGUI::newDelegate(this, &SettingsWindow::onWaterTextureSizeChanged);
        mWaterReflectionDetail->eventComboChangePosition
            += MyGUI::newDelegate(this, &SettingsWindow::onWaterReflectionDetailChanged);
        mWaterRainRippleDetail->eventComboChangePosition
            += MyGUI::newDelegate(this, &SettingsWindow::onWaterRainRippleDetailChanged);

        mLightsResetButton->eventMouseButtonClick
            += MyGUI::newDelegate(this, &SettingsWindow::onLightsResetButtonClicked);
        mMaxLights->eventComboChangePosition += MyGUI::newDelegate(this, &SettingsWindow::onMaxLightsChanged);

        mActorShadowsButton->eventMouseButtonClick += MyGUI::newDelegate(this, &SettingsWindow::onShadowsButtonClicked);
        mPlayerShadowsButton->eventMouseButtonClick
            += MyGUI::newDelegate(this, &SettingsWindow::onShadowsButtonClicked);
        mTerrainShadowsButton->eventMouseButtonClick
            += MyGUI::newDelegate(this, &SettingsWindow::onShadowsButtonClicked);
        mObjectShadowsButton->eventMouseButtonClick
            += MyGUI::newDelegate(this, &SettingsWindow::onShadowsButtonClicked);
        mShadowMapResolution->eventComboChangePosition
            += MyGUI::newDelegate(this, &SettingsWindow::onShadowMapResolutionChanged);

        mWindowModeList->eventComboChangePosition += MyGUI::newDelegate(this, &SettingsWindow::onWindowModeChanged);
        mVSyncModeList->eventComboChangePosition += MyGUI::newDelegate(this, &SettingsWindow::onVSyncModeChanged);

        mKeyboardSwitch->eventMouseButtonClick += MyGUI::newDelegate(this, &SettingsWindow::onKeyboardSwitchClicked);
        mControllerSwitch->eventMouseButtonClick
            += MyGUI::newDelegate(this, &SettingsWindow::onControllerSwitchClicked);

        mPrimaryLanguage->eventComboChangePosition
            += MyGUI::newDelegate(this, &SettingsWindow::onPrimaryLanguageChanged);
        mSecondaryLanguage->eventComboChangePosition
            += MyGUI::newDelegate(this, &SettingsWindow::onSecondaryLanguageChanged);
        mGmstOverridesL10n->eventMouseButtonClick
            += MyGUI::newDelegate(this, &SettingsWindow::onGmstOverridesL10nChanged);

        initialiseJournalControls();
        mJournalFontSize->addItem(
            MyGUI::LanguageManager::getInstance().replaceTags("#{OMWEngine:JournalFontSizeAutomatic}"), 0);
        mJournalPageNumberFontSize->addItem(
            MyGUI::LanguageManager::getInstance().replaceTags("#{OMWEngine:JournalPageNumberFontSizeAutomatic}"), 0);
        for (int size = 10; size <= 24; ++size)
        {
            mJournalFontSize->addItem(MyGUI::utility::toString(size), size);
            mJournalPageNumberFontSize->addItem(MyGUI::utility::toString(size), size);
        }
        for (int scale = 50; scale <= 100; ++scale)
            mJournalButtonScale->addItem(MyGUI::utility::toString(scale) + "%", scale);
        mJournalFontSize->eventComboChangePosition
            += MyGUI::newDelegate(this, &SettingsWindow::onJournalFontSizeChanged);
        mJournalTextAlignment->eventComboChangePosition
            += MyGUI::newDelegate(this, &SettingsWindow::onJournalTextAlignmentChanged);
        mJournalPageNumberFontSize->eventComboChangePosition
            += MyGUI::newDelegate(this, &SettingsWindow::onJournalPageNumberFontSizeChanged);
        mJournalButtonScale->eventComboChangePosition
            += MyGUI::newDelegate(this, &SettingsWindow::onJournalButtonScaleChanged);
        updateJournalSettings();
        initialiseDialogueColourControls();
        initialiseMessagePositionControls();

        computeMinimumWindowSize();

        center();

        mResetControlsButton->eventMouseButtonClick
            += MyGUI::newDelegate(this, &SettingsWindow::onResetDefaultBindings);

        // fill resolution list
        const int screen = Settings::video().mScreen;
        int numDisplayModes = SDL_GetNumDisplayModes(screen);
        std::vector<std::pair<int, int>> resolutions;
        for (int i = 0; i < numDisplayModes; i++)
        {
            SDL_DisplayMode mode;
            SDL_GetDisplayMode(screen, i, &mode);
            resolutions.emplace_back(mode.w, mode.h);
        }
        std::sort(resolutions.begin(), resolutions.end(), sortResolutions);
        for (std::pair<int, int>& resolution : resolutions)
        {
            std::string str = Misc::getResolutionText(resolution.first, resolution.second);

            if (mResolutionList->findItemIndexWith(str) == MyGUI::ITEM_NONE)
                mResolutionList->addItem(str, resolution);
        }
        highlightCurrentResolution();

        mTextureFilteringButton->setCaptionWithReplacing(
            textureFilteringToStr(Settings::general().mTextureMipmap, Settings::general().mTextureMinFilter));
        updateAnisotropyComboBox(mAnisotropy);

        int waterTextureSize = Settings::water().mRttSize;
        if (waterTextureSize >= 512)
            mWaterTextureSize->setIndexSelected(0);
        if (waterTextureSize >= 1024)
            mWaterTextureSize->setIndexSelected(1);
        if (waterTextureSize >= 2048)
            mWaterTextureSize->setIndexSelected(2);

        const int waterReflectionDetail = Settings::water().mReflectionDetail;
        mWaterReflectionDetail->setIndexSelected(waterReflectionDetail);

        const int waterRainRippleDetail = Settings::water().mRainRippleDetail;
        mWaterRainRippleDetail->setIndexSelected(waterRainRippleDetail);

        updateMaxLightsComboBox(mMaxLights);

        updateShadowMapResolutionComboBox(mShadowMapResolution);

        const Settings::WindowMode windowMode = Settings::video().mWindowMode;
        mWindowBorderButton->setEnabled(
            windowMode != Settings::WindowMode::Fullscreen && windowMode != Settings::WindowMode::WindowedFullscreen);

        mWindowModeHint->setVisible(windowMode == Settings::WindowMode::WindowedFullscreen);

        mKeyboardSwitch->setStateSelected(true);
        mControllerSwitch->setStateSelected(false);

        mScriptFilter->eventEditTextChange += MyGUI::newDelegate(this, &SettingsWindow::onScriptFilterChange);
        mScriptList->eventListMouseItemActivate += MyGUI::newDelegate(this, &SettingsWindow::onScriptListSelection);

        std::vector<std::string> availableLanguages;
        const VFS::Manager* vfs = MWBase::Environment::get().getResourceSystem()->getVFS();
        constexpr VFS::Path::NormalizedView l10n("l10n/");
        for (const auto& path : vfs->getRecursiveDirectoryIterator(l10n))
        {
            if (path.extension() == "yaml")
            {
                std::string_view localeName(path.stem());
                if (localeName == "gmst")
                    continue; // fake locale to get gmst strings from content files
                if (std::find(availableLanguages.begin(), availableLanguages.end(), localeName)
                    == availableLanguages.end())
                    availableLanguages.emplace_back(localeName);
            }
        }

        std::sort(availableLanguages.begin(), availableLanguages.end());

        std::vector<std::string> currentLocales = Settings::general().mPreferredLocales;
        if (currentLocales.empty())
            currentLocales.push_back("en");

        icu::Locale primaryLocale(currentLocales[0].c_str());

        mPrimaryLanguage->removeAllItems();
        mPrimaryLanguage->setIndexSelected(MyGUI::ITEM_NONE);

        mSecondaryLanguage->removeAllItems();
        mSecondaryLanguage->addItem(
            MyGUI::LanguageManager::getInstance().replaceTags("#{Interface:None}"), std::string());
        mSecondaryLanguage->setIndexSelected(0);

        size_t i = 0;
        for (const auto& language : availableLanguages)
        {
            icu::Locale locale(language.c_str());

            icu::UnicodeString str(language.c_str());
            locale.getDisplayName(primaryLocale, str);
            std::string localeString;
            str.toUTF8String(localeString);

            mPrimaryLanguage->addItem(localeString, language);
            mSecondaryLanguage->addItem(localeString, language);

            if (language == currentLocales[0])
                mPrimaryLanguage->setIndexSelected(i);
            if (currentLocales.size() > 1 && language == currentLocales[1])
                mSecondaryLanguage->setIndexSelected(i + 1);

            i++;
        }

        mControllerButtons.mA = "#{Interface:Select}";
        mControllerButtons.mB = "#{Interface:OK}";
        mControllerButtons.mLStick = "#{Interface:Mouse}";
    }

    void SettingsWindow::onTabChanged(MyGUI::TabControl* /*sender*/, size_t /*index*/)
    {
        layoutDialogueColourControls();
        layoutMessagePositionControls();
        resetScrollbars();
    }

    void SettingsWindow::onOkButtonClicked(MyGUI::Widget* /*sender*/)
    {
        MWBase::Environment::get().getWindowManager()->toggleSettingsWindow();
    }

    void SettingsWindow::onResolutionSelected(MyGUI::ListBox* /*sender*/, size_t index)
    {
        if (index == MyGUI::ITEM_NONE)
            return;

        ConfirmationDialog* dialog = MWBase::Environment::get().getWindowManager()->getConfirmationDialog();
        dialog->askForConfirmation("#{OMWEngine:ConfirmResolution}");
        dialog->eventOkClicked.clear();
        dialog->eventOkClicked += MyGUI::newDelegate(this, &SettingsWindow::onResolutionAccept);
        dialog->eventCancelClicked.clear();
        dialog->eventCancelClicked += MyGUI::newDelegate(this, &SettingsWindow::onResolutionCancel);
    }

    void SettingsWindow::onResolutionAccept()
    {
        auto resolution = mResolutionList->getItemDataAt<std::pair<int, int>>(mResolutionList->getIndexSelected());
        if (resolution)
        {
            Settings::video().mResolutionX.set(resolution->first);
            Settings::video().mResolutionY.set(resolution->second);

            apply();
        }
    }

    void SettingsWindow::onResolutionCancel()
    {
        highlightCurrentResolution();
    }

    void SettingsWindow::highlightCurrentResolution()
    {
        mResolutionList->setIndexSelected(MyGUI::ITEM_NONE);

        const int currentX = Settings::video().mResolutionX;
        const int currentY = Settings::video().mResolutionY;

        for (size_t i = 0; i < mResolutionList->getItemCount(); ++i)
        {
            auto resolution = mResolutionList->getItemDataAt<std::pair<int, int>>(i);
            if (resolution && resolution->first == currentX && resolution->second == currentY)
            {
                mResolutionList->setIndexSelected(i);
                break;
            }
        }
    }

    void SettingsWindow::onWaterTextureSizeChanged(MyGUI::ComboBox* /*sender*/, size_t pos)
    {
        int size = 0;
        if (pos == 0)
            size = 512;
        else if (pos == 1)
            size = 1024;
        else if (pos == 2)
            size = 2048;
        Settings::water().mRttSize.set(size);
        apply();
    }

    void SettingsWindow::onWaterReflectionDetailChanged(MyGUI::ComboBox* /*sender*/, size_t pos)
    {
        Settings::water().mReflectionDetail.set(static_cast<int>(pos));
        apply();
    }

    void SettingsWindow::onWaterRainRippleDetailChanged(MyGUI::ComboBox* /*sender*/, size_t pos)
    {
        Settings::water().mRainRippleDetail.set(static_cast<int>(pos));
        apply();
    }

    void SettingsWindow::onLanguageChanged(size_t langPriority, MyGUI::ComboBox* sender, size_t pos)
    {
        if (pos == MyGUI::ITEM_NONE)
            return;

        sender->setCaptionWithReplacing(sender->getItemNameAt(sender->getIndexSelected()));

        MWBase::Environment::get().getWindowManager()->interactiveMessageBox(
            "#{OMWEngine:ChangeRequiresRestart}", { "#{Interface:OK}" }, true);

        std::vector<std::string> currentLocales = Settings::general().mPreferredLocales;
        if (currentLocales.size() <= langPriority)
            currentLocales.resize(langPriority + 1, "en");

        const auto& languageCode = *sender->getItemDataAt<std::string>(pos);
        if (!languageCode.empty())
            currentLocales[langPriority] = languageCode;
        else
            currentLocales.resize(1);

        Settings::general().mPreferredLocales.set(currentLocales);
    }

    void SettingsWindow::onGmstOverridesL10nChanged(MyGUI::Widget*)
    {
        MWBase::Environment::get().getWindowManager()->interactiveMessageBox(
            "#{OMWEngine:ChangeRequiresRestart}", { "#{Interface:OK}" }, true);
    }

    void SettingsWindow::onVSyncModeChanged(MyGUI::ComboBox* sender, size_t pos)
    {
        if (pos == MyGUI::ITEM_NONE)
            return;

        Settings::video().mVsyncMode.set(static_cast<SDLUtil::VSyncMode>(sender->getIndexSelected()));
        apply();
    }

    void SettingsWindow::onWindowModeChanged(MyGUI::ComboBox* sender, size_t pos)
    {
        if (pos == MyGUI::ITEM_NONE)
            return;

        const Settings::WindowMode windowMode = static_cast<Settings::WindowMode>(sender->getIndexSelected());
        if (windowMode == Settings::WindowMode::WindowedFullscreen)
        {
            mResolutionList->setEnabled(false);
            mWindowModeHint->setVisible(true);
        }
        else
        {
            mResolutionList->setEnabled(true);
            mWindowModeHint->setVisible(false);
        }

        if (windowMode == Settings::WindowMode::Windowed)
            mWindowBorderButton->setEnabled(true);
        else
            mWindowBorderButton->setEnabled(false);

        Settings::video().mWindowMode.set(windowMode);
        apply();
    }

    void SettingsWindow::onMaxLightsChanged(MyGUI::ComboBox* /*sender*/, size_t pos)
    {
        Settings::shaders().mMaxLights.set(8 * static_cast<int>(pos + 1));
        apply();
        configureWidgets(mMainWidget, false);
    }

    void SettingsWindow::onLightsResetButtonClicked(MyGUI::Widget* /*sender*/)
    {
        std::vector<std::string> buttons = { "#{Interface:Yes}", "#{Interface:No}" };
        MWBase::Environment::get().getWindowManager()->interactiveMessageBox(
            "#{OMWEngine:LightingResetToDefaults}", buttons, true);
        int selectedButton = MWBase::Environment::get().getWindowManager()->readPressedButton();
        if (selectedButton == 1 || selectedButton == -1)
            return;

        Settings::shaders().mForcePerPixelLighting.reset();
        Settings::shaders().mClassicFalloff.reset();
        Settings::shaders().mClampLighting.reset();
        Settings::shaders().mMatchSunlightToSun.reset();
        Settings::shaders().mLightRadiusMultiplier.reset();
        Settings::shaders().mMaximumLightDistance.reset();
        Settings::shaders().mLightFadeStart.reset();
        Settings::shaders().mMinimumInteriorBrightness.reset();
        Settings::shaders().mMaxLights.reset();
        Settings::shaders().mClusteredLighting.reset();

        updateMaxLightsComboBox(mMaxLights);

        apply();
        configureWidgets(mMainWidget, false);
    }

    void SettingsWindow::onButtonToggled(MyGUI::Widget* sender)
    {
        const std::string on = MWBase::Environment::get().getL10nManager()->getMessage("Interface", "On");
        const std::string off = MWBase::Environment::get().getL10nManager()->getMessage("Interface", "Off");
        bool newState;
        if (sender->castType<MyGUI::Button>()->getCaption() == on)
        {
            sender->castType<MyGUI::Button>()->setCaption(MyGUI::UString(off));
            newState = false;
        }
        else
        {
            sender->castType<MyGUI::Button>()->setCaption(MyGUI::UString(on));
            newState = true;
        }

        if (getSettingType(sender) == checkButtonType)
        {
            Settings::get<bool>(getSettingCategory(sender), getSettingName(sender)).set(newState);
            apply();
            return;
        }
    }

    void SettingsWindow::onTextureFilteringChanged(MyGUI::ComboBox* /*sender*/, size_t pos)
    {
        auto& generalSettings = Settings::general();
        switch (pos)
        {
            case 0: // Bilinear with mips
                generalSettings.mTextureMipmap.set("nearest");
                generalSettings.mTextureMagFilter.set("linear");
                generalSettings.mTextureMinFilter.set("linear");
                break;
            case 1: // Trilinear with mips
                generalSettings.mTextureMipmap.set("linear");
                generalSettings.mTextureMagFilter.set("linear");
                generalSettings.mTextureMinFilter.set("linear");
                break;
            default:
                Log(Debug::Warning) << "Unexpected texture filtering option pos " << pos;
                break;
        }

        apply();
    }

    void SettingsWindow::onAnisotropyChanged(MyGUI::ComboBox* /*sender*/, size_t pos)
    {
        if (pos == MyGUI::ITEM_NONE)
            return;

        auto& generalSettings = Settings::general();
        switch (pos)
        {
            case 0:
                generalSettings.mAnisotropy.set(0);
                break;
            case 1:
                generalSettings.mAnisotropy.set(2);
                break;
            case 2:
                generalSettings.mAnisotropy.set(4);
                break;
            case 3:
                generalSettings.mAnisotropy.set(8);
                break;
            case 4:
                generalSettings.mAnisotropy.set(16);
                break;
            default:
                Log(Debug::Warning) << "Unexpected anisotropy option pos " << pos;
                break;
        }

        apply();
    }

    void SettingsWindow::initialiseJournalControls()
    {
        const auto findComboBox = [this](std::string_view name) {
            std::string fullName = mPrefix;
            fullName += name;
            MyGUI::Widget* widget = mMainWidget->findWidget(fullName);
            return widget ? widget->castType<MyGUI::ComboBox>(false) : nullptr;
        };
        mJournalFontSize = findComboBox("JournalFontSize");
        mJournalTextAlignment = findComboBox("JournalTextAlignment");
        mJournalPageNumberFontSize = findComboBox("JournalPageNumberFontSize");
        mJournalButtonScale = findComboBox("JournalButtonScale");
        if (mJournalFontSize && mJournalTextAlignment && mJournalPageNumberFontSize && mJournalButtonScale)
            return;

        // Keep any controls an older interface layout already provides. Put only
        // missing controls in a native tab, avoiding duplicate or orphaned fields.

        const auto translated = [](std::string_view text) {
            return MyGUI::LanguageManager::getInstance().replaceTags(MyGUI::UString(text));
        };
        MyGUI::TabItem* tab = mSettingsTab->addItem(translated("#{OMWEngine:JournalTypography}"));
        int row = 12;
        const auto addControl = [this, tab, &row](
                                    MyGUI::ComboBox*& control, std::string_view caption, const std::string& name) {
            if (control)
                return;
            auto* label = tab->createWidget<MyGUI::TextBox>(
                "SandText", { 4, row + 4, 154, 24 }, MyGUI::Align::Default);
            label->setCaptionWithReplacing(caption);
            control = tab->createWidget<MyGUI::ComboBox>("MW_ComboBox", { 164, row, 202, 28 },
                MyGUI::Align::Default, mPrefix + name);
            control->setComboModeDrop(true);
            row += 36;
        };
        const bool alignmentMissing = mJournalTextAlignment == nullptr;
        addControl(mJournalFontSize, "#{OMWEngine:JournalFontSize}", "JournalFontSizeFallback");
        addControl(mJournalTextAlignment, "#{OMWEngine:JournalTextAlignment}", "JournalTextAlignmentFallback");
        addControl(mJournalPageNumberFontSize, "#{OMWEngine:JournalPageNumberFontSize}",
            "JournalPageNumberFontSizeFallback");
        addControl(mJournalButtonScale, "#{OMWEngine:JournalButtonScale}", "JournalButtonScaleFallback");
        // The hint is deliberately wrapped within the same content width as the
        // bundled Interface tab, independent of the replacement's decoration.
        auto* hint = tab->createWidget<MyGUI::EditBox>(
            "SandText", { 4, row + 12, 362, 100 }, MyGUI::Align::Default);
        hint->setEditStatic(true);
        hint->setEditMultiLine(true);
        hint->setEditWordWrap(true);
        hint->setTextAlign(MyGUI::Align::Left | MyGUI::Align::Top);
        hint->setCaptionWithReplacing("#{OMWEngine:JournalTypographyHint}");
        if (alignmentMissing)
            for (std::string_view name : { "#{OMWEngine:JournalAlignLeft}", "#{OMWEngine:JournalAlignCentre}",
                     "#{OMWEngine:JournalAlignRight}", "#{OMWEngine:JournalAlignJustified}" })
                mJournalTextAlignment->addItem(translated(name));
    }

    void SettingsWindow::updateJournalSettings()
    {
        const int size = Settings::gui().mJournalFontSize;
        mJournalFontSize->setIndexSelected(size == 0 ? 0 : static_cast<size_t>(size - 9));
        mJournalTextAlignment->setIndexSelected(Settings::gui().mJournalTextAlignment);
        const int pageSize = Settings::gui().mJournalPageNumberFontSize;
        mJournalPageNumberFontSize->setIndexSelected(pageSize == 0 ? 0 : static_cast<size_t>(pageSize - 9));
        mJournalButtonScale->setIndexSelected(static_cast<size_t>(Settings::gui().mJournalButtonScale.get() - 50));
    }

    void SettingsWindow::onJournalFontSizeChanged(MyGUI::ComboBox* sender, size_t pos)
    {
        if (pos == MyGUI::ITEM_NONE)
            return;
        if (const int* size = sender->getItemDataAt<int>(pos))
        {
            Settings::gui().mJournalFontSize.set(*size);
            apply();
        }
    }

    void SettingsWindow::onJournalTextAlignmentChanged(MyGUI::ComboBox* /*sender*/, size_t pos)
    {
        if (pos == MyGUI::ITEM_NONE || pos > 3)
            return;
        Settings::gui().mJournalTextAlignment.set(static_cast<int>(pos));
        apply();
    }

    void SettingsWindow::onJournalPageNumberFontSizeChanged(MyGUI::ComboBox* sender, size_t pos)
    {
        if (pos == MyGUI::ITEM_NONE)
            return;
        if (const int* size = sender->getItemDataAt<int>(pos))
        {
            Settings::gui().mJournalPageNumberFontSize.set(*size);
            apply();
        }
    }

    void SettingsWindow::onJournalButtonScaleChanged(MyGUI::ComboBox* sender, size_t pos)
    {
        if (pos == MyGUI::ITEM_NONE)
            return;
        if (const int* scale = sender->getItemDataAt<int>(pos))
        {
            Settings::gui().mJournalButtonScale.set(*scale);
            apply();
        }
    }

    void SettingsWindow::initialiseDialogueColourControls()
    {
        // Build this tab natively so a UI mod's older settings layout cannot omit it.
        mDialogueColourTab = mSettingsTab->addItem(
            MyGUI::LanguageManager::getInstance().replaceTags("#{OMWEngine:DialogueColours}"));
        mDialogueColourControls = createDialogueColourControls(mDialogueColourTab, mPrefix);
        for (size_t i = 0; i < dialogueColourToggleSettings.size(); ++i)
        {
            auto* button = mDialogueColourControls.mToggles[i];
            button->setUserString("SettingName", std::string(dialogueColourToggleSettings[i]));
            button->eventMouseButtonClick += MyGUI::newDelegate(this, &SettingsWindow::onDialogueColourToggled);
        }
        mDialogueColourControls.mRole->eventComboChangePosition
            += MyGUI::newDelegate(this, &SettingsWindow::onDialogueColourRoleChanged);
        for (auto* slider : mDialogueColourControls.mSliders)
            slider->eventScrollChangePosition += MyGUI::newDelegate(this, &SettingsWindow::onDialogueColourChanged);
        mDialogueColourControls.mReset->eventMouseButtonClick
            += MyGUI::newDelegate(this, &SettingsWindow::onDialogueColourReset);
        updateDialogueColourSettings();
    }

    void SettingsWindow::layoutDialogueColourControls()
    {
        MWGui::layoutDialogueColourControls(mDialogueColourControls);
    }

    void SettingsWindow::updateDialogueColourSettings()
    {
        if (!mDialogueColourControls.mRole)
            return;
        mUpdatingDialogueColours = true;
        for (size_t i = 0; i < dialogueColourToggleSettings.size(); ++i)
        {
            const bool enabled = Settings::get<bool>("GUI", dialogueColourToggleSettings[i]);
            mDialogueColourControls.mToggles[i]->setCaptionWithReplacing(enabled ? "#{Interface:On}" : "#{Interface:Off}");
        }
        const size_t role = mDialogueColourControls.mRole->getIndexSelected();
        if (role < dialogueColourSettings.size())
        {
            const bool useTheme = role == 0 && Settings::gui().mDialogueUseSystemColour;
            const MyGUI::Colour colour = useTheme
                ? MWBase::Environment::get().getWindowManager()->getTextColours().normal
                : Settings::get<MyGUI::Colour>("GUI", dialogueColourSettings[role]).get();
            const std::array<float, 3> channels{ colour.red, colour.green, colour.blue };
            for (size_t i = 0; i < channels.size(); ++i)
            {
                const size_t position = colourChannelPosition(channels[i]);
                mDialogueColourControls.mSliders[i]->setScrollPosition(position);
                mDialogueColourControls.mSliders[i]->setEnabled(!useTheme);
                mDialogueColourControls.mValues[i]->setCaption(std::to_string(position));
            }
            mDialogueColourControls.mReset->setEnabled(!useTheme);
            // Journal text is previewed against paper, not the dark dialogue box.
            mDialogueColourControls.mPreviewBackground->setColour(
                role == 5 ? MyGUI::Colour(0.87f, 0.83f, 0.69f) : MyGUI::Colour(0.06f, 0.06f, 0.06f));
            mDialogueColourControls.mPreview->setCaptionWithReplacing(dialogueColourSamples[role]);
            mDialogueColourControls.mPreview->setTextColour(colour);
        }
        mUpdatingDialogueColours = false;
        layoutDialogueColourControls();
    }

    void SettingsWindow::onDialogueColourToggled(MyGUI::Widget* sender)
    {
        auto& setting = Settings::get<bool>("GUI", getSettingName(sender));
        setting.set(!setting.get());
        updateDialogueColourSettings();
        apply();
    }

    void SettingsWindow::onDialogueColourRoleChanged(MyGUI::ComboBox* /*sender*/, size_t pos)
    {
        if (!mUpdatingDialogueColours && pos < dialogueColourSettings.size())
            updateDialogueColourSettings();
    }

    void SettingsWindow::onDialogueColourChanged(MyGUI::ScrollBar* sender, size_t pos)
    {
        if (mUpdatingDialogueColours)
            return;
        const size_t role = mDialogueColourControls.mRole->getIndexSelected();
        if (role >= dialogueColourSettings.size() || (role == 0 && Settings::gui().mDialogueUseSystemColour))
            return;
        const auto channel
            = std::find(mDialogueColourControls.mSliders.begin(), mDialogueColourControls.mSliders.end(), sender);
        if (channel == mDialogueColourControls.mSliders.end())
            return;
        auto& setting = Settings::get<MyGUI::Colour>("GUI", dialogueColourSettings[role]);
        MyGUI::Colour colour = setting.get();
        const float value = static_cast<float>(std::min(pos, size_t(255))) / 255.f;
        switch (channel - mDialogueColourControls.mSliders.begin())
        {
            case 0:
                colour.red = value;
                break;
            case 1:
                colour.green = value;
                break;
            case 2:
                colour.blue = value;
                break;
        }
        // Change only the edited channel; merely opening this tab never rounds
        // colours loaded from settings.cfg to the slider's 8-bit display values.
        setting.set(colour);
        updateDialogueColourSettings();
        apply();
    }

    void SettingsWindow::onDialogueColourReset(MyGUI::Widget* /*sender*/)
    {
        const size_t role = mDialogueColourControls.mRole->getIndexSelected();
        if (role >= dialogueColourSettings.size() || (role == 0 && Settings::gui().mDialogueUseSystemColour))
            return;
        Settings::get<MyGUI::Colour>("GUI", dialogueColourSettings[role]).reset();
        updateDialogueColourSettings();
        apply();
    }

    void SettingsWindow::initialiseMessagePositionControls()
    {
        // A native tab stays available when a mod replaces the settings layout.
        mMessagePositionTab = mSettingsTab->addItem(
            MyGUI::LanguageManager::getInstance().replaceTags("#{OMWEngine:MessagePositionTab}"));
        mMessagePositionControls = createMessagePositionControls(mMessagePositionTab, mPrefix);
        mMessagePositionControls.mPosition->eventComboChangePosition
            += MyGUI::newDelegate(this, &SettingsWindow::onMessagePositionChanged);
        for (auto* slider : mMessagePositionControls.mSliders)
            slider->eventScrollChangePosition
                += MyGUI::newDelegate(this, &SettingsWindow::onMessagePositionOffsetChanged);
        mMessagePositionControls.mReset->eventMouseButtonClick
            += MyGUI::newDelegate(this, &SettingsWindow::onMessagePositionReset);
        mMessagePositionControls.mTest->eventMouseButtonClick
            += MyGUI::newDelegate(this, &SettingsWindow::onMessagePositionTest);
        updateMessagePositionSettings();
    }

    void SettingsWindow::layoutMessagePositionControls()
    {
        MWGui::layoutMessagePositionControls(mMessagePositionControls);
    }

    void SettingsWindow::updateMessagePositionSettings()
    {
        if (!mMessagePositionControls.mPosition)
            return;
        mUpdatingMessagePosition = true;
        const auto& settings = Settings::gui();
        mMessagePositionControls.mPosition->setIndexSelected(static_cast<size_t>(settings.mMessageBoxPosition.get()));
        const std::array<int, 2> offsets{
            settings.mMessageBoxHorizontalOffset.get(), settings.mMessageBoxVerticalOffset.get()
        };
        for (size_t i = 0; i < offsets.size(); ++i)
        {
            mMessagePositionControls.mSliders[i]->setScrollPosition(static_cast<size_t>(offsets[i] + 1000));
            mMessagePositionControls.mValues[i]->setCaption(std::to_string(offsets[i]));
        }
        mUpdatingMessagePosition = false;
        layoutMessagePositionControls();
    }

    void SettingsWindow::onMessagePositionChanged(MyGUI::ComboBox* /*sender*/, size_t pos)
    {
        if (mUpdatingMessagePosition || pos >= 9)
            return;
        Settings::gui().mMessageBoxPosition.set(static_cast<int>(pos));
        updateMessagePositionSettings();
        apply();
    }

    void SettingsWindow::onMessagePositionOffsetChanged(MyGUI::ScrollBar* sender, size_t pos)
    {
        if (mUpdatingMessagePosition)
            return;
        const auto slider = std::find(
            mMessagePositionControls.mSliders.begin(), mMessagePositionControls.mSliders.end(), sender);
        if (slider == mMessagePositionControls.mSliders.end())
            return;
        const int value = static_cast<int>(std::min(pos, size_t(2000))) - 1000;
        auto& settings = Settings::gui();
        if (slider == mMessagePositionControls.mSliders.begin())
            settings.mMessageBoxHorizontalOffset.set(value);
        else
            settings.mMessageBoxVerticalOffset.set(value);
        updateMessagePositionSettings();
        apply();
    }

    void SettingsWindow::onMessagePositionReset(MyGUI::Widget* /*sender*/)
    {
        auto& settings = Settings::gui();
        settings.mMessageBoxPosition.reset();
        settings.mMessageBoxHorizontalOffset.reset();
        settings.mMessageBoxVerticalOffset.reset();
        updateMessagePositionSettings();
        apply();
    }

    void SettingsWindow::onMessagePositionTest(MyGUI::Widget* /*sender*/)
    {
        MWBase::Environment::get().getWindowManager()->messageBox(
            "#{OMWEngine:MessagePositionTest}", MWGui::ShowInDialogueMode_Never);
    }

    void SettingsWindow::onShadowsButtonClicked(MyGUI::Widget* /*sender*/)
    {
        auto& shadowSettings = Settings::shadows();
        shadowSettings.mEnableShadows.set(shadowSettings.mActorShadows || shadowSettings.mPlayerShadows
            || shadowSettings.mTerrainShadows || shadowSettings.mObjectShadows);
        apply();
    }

    void SettingsWindow::onShadowMapResolutionChanged(MyGUI::ComboBox* sender, size_t pos)
    {
        if (pos == MyGUI::ITEM_NONE)
            return;

        auto& shadowSettings = Settings::shadows();
        switch (pos)
        {
            case 0:
                shadowSettings.mShadowMapResolution.set(512);
                break;
            case 1:
                shadowSettings.mShadowMapResolution.set(1024);
                break;
            case 2:
                shadowSettings.mShadowMapResolution.set(2048);
                break;
            case 3:
                shadowSettings.mShadowMapResolution.set(4096);
                break;
            case 4:
            {
                int* resolution = sender->getItemDataAt<int>(4);
                if (resolution)
                    shadowSettings.mShadowMapResolution.set(*resolution);
                break;
            }
            default:
                Log(Debug::Warning) << "Unexpected shadow map resolution pos " << pos;
                break;
        }

        apply();
    }

    void SettingsWindow::onResChange(int /*width*/, int /*height*/)
    {
        center();
        layoutDialogueColourControls();
        layoutMessagePositionControls();
        highlightCurrentResolution();
    }

    void SettingsWindow::onSliderChangePosition(MyGUI::ScrollBar* scroller, size_t pos)
    {
        if (getSettingType(scroller) == "Slider")
        {
            std::vector<icu::UnicodeString> argNames;
            std::vector<icu::Formattable> args;
            std::string_view valueType = getSettingValueType(scroller);
            if (valueType == "Float" || valueType == "Integer" || valueType == "Cell")
            {
                float value = pos / float(scroller->getScrollRange() - 1);

                float min, max;
                getSettingMinMax(scroller, min, max);
                value = min + (max - min) * value;

                if (valueType == "Cell")
                {
                    Settings::get<float>(getSettingCategory(scroller), getSettingName(scroller)).set(value);
                    argNames.emplace_back("cells");
                    args.emplace_back(value / Constants::CellSizeInUnits);
                }
                else if (valueType == "Float")
                {
                    Settings::get<float>(getSettingCategory(scroller), getSettingName(scroller)).set(value);
                    argNames.emplace_back("value");
                    args.emplace_back(value);
                }
                else
                {
                    int intValue = static_cast<int>(value);
                    Settings::get<int>(getSettingCategory(scroller), getSettingName(scroller)).set(intValue);
                    argNames.emplace_back("value");
                    args.emplace_back(intValue);
                }
            }
            else
            {
                int intValue = static_cast<int>(pos);
                Settings::get<int>(getSettingCategory(scroller), getSettingName(scroller)).set(intValue);
                argNames.emplace_back("value");
                args.emplace_back(intValue);
            }
            updateSliderLabel(scroller, getSliderLabel(scroller), argNames, args);

            apply();
        }
    }

    void SettingsWindow::apply()
    {
        const Settings::CategorySettingVector changed = Settings::Manager::getPendingChanges();
        MWBase::Environment::get().getWorld()->processChangedSettings(changed);
        MWBase::Environment::get().getSoundManager()->processChangedSettings(changed);
        MWBase::Environment::get().getWindowManager()->processChangedSettings(changed);
        MWBase::Environment::get().getInputManager()->processChangedSettings(changed);
        MWBase::Environment::get().getMechanicsManager()->processChangedSettings(changed);
        Settings::Manager::resetPendingChanges();

        updateLightSettings();
    }

    void SettingsWindow::onKeyboardSwitchClicked(MyGUI::Widget* /*sender*/)
    {
        if (mKeyboardMode)
            return;
        mKeyboardMode = true;
        mKeyboardSwitch->setStateSelected(true);
        mControllerSwitch->setStateSelected(false);
        updateControlsBox();
        resetScrollbars();
    }

    void SettingsWindow::onControllerSwitchClicked(MyGUI::Widget* /*sender*/)
    {
        if (!mKeyboardMode)
            return;
        mKeyboardMode = false;
        mKeyboardSwitch->setStateSelected(false);
        mControllerSwitch->setStateSelected(true);
        updateControlsBox();
        resetScrollbars();
    }

    void SettingsWindow::updateControlsBox()
    {
        while (mControlsBox->getChildCount())
            MyGUI::Gui::getInstance().destroyWidget(mControlsBox->getChildAt(0));

        MWBase::Environment::get().getWindowManager()->removeStaticMessageBox();
        const auto inputManager = MWBase::Environment::get().getInputManager();
        const auto& actions
            = mKeyboardMode ? inputManager->getActionKeySorting() : inputManager->getActionControllerSorting();

        for (const int& action : actions)
        {
            std::string desc{ inputManager->getActionDescription(action) };
            if (desc.empty())
                continue;

            std::string binding;
            if (mKeyboardMode)
                binding = inputManager->getActionKeyBindingName(action);
            else
                binding = inputManager->getActionControllerBindingName(action);

            Gui::SharedStateButton* leftText = mControlsBox->createWidget<Gui::SharedStateButton>(
                "SandTextButton", MyGUI::IntCoord(), MyGUI::Align::Default);
            leftText->setCaptionWithReplacing(desc);

            Gui::SharedStateButton* rightText = mControlsBox->createWidget<Gui::SharedStateButton>(
                "SandTextButton", MyGUI::IntCoord(), MyGUI::Align::Default);
            rightText->setCaptionWithReplacing(binding);
            rightText->setTextAlign(MyGUI::Align::Right);
            rightText->setUserData(action); // save the action id for callbacks
            rightText->eventMouseButtonClick += MyGUI::newDelegate(this, &SettingsWindow::onRebindAction);
            rightText->eventMouseWheel += MyGUI::newDelegate(this, &SettingsWindow::onInputTabMouseWheel);

            Gui::ButtonGroup group;
            group.push_back(leftText);
            group.push_back(rightText);
            Gui::SharedStateButton::createButtonGroup(group);
        }

        layoutControlsBox();
    }

    void SettingsWindow::updateLightSettings()
    {
        mClusteredLightingButton->setEnabled(
            MWBase::Environment::get().getResourceSystem()->getSceneManager()->isClusteredLightingSupported());

        const bool isClustered = Settings::shaders().mClusteredLighting;
        const bool isClassic = !isClustered && Settings::shaders().mClassicFalloff;

        mClassicFalloffWidget->setVisible(!isClustered);
        mMinimumBrightnessText->setVisible(!isClassic);
        mMinimumBrightnessScroll->setVisible(!isClassic);
    }

    void SettingsWindow::updateWindowModeSettings()
    {
        const Settings::WindowMode windowMode = Settings::video().mWindowMode;
        const std::size_t windowModeIndex = static_cast<std::size_t>(windowMode);

        mWindowModeList->setIndexSelected(windowModeIndex);

        if (windowMode != Settings::WindowMode::Windowed && windowModeIndex != MyGUI::ITEM_NONE)
        {
            // check if this resolution is supported in fullscreen
            if (mResolutionList->getIndexSelected() != MyGUI::ITEM_NONE)
            {
                auto resolution
                    = mResolutionList->getItemDataAt<std::pair<int, int>>(mResolutionList->getIndexSelected());
                if (resolution)
                {
                    Settings::video().mResolutionX.set(resolution->first);
                    Settings::video().mResolutionY.set(resolution->second);
                }
            }

            bool supported = false;
            int fallbackX = 0, fallbackY = 0;
            for (size_t i = 0; i < mResolutionList->getItemCount(); ++i)
            {
                auto resolution = mResolutionList->getItemDataAt<std::pair<int, int>>(i);
                if (!resolution)
                    continue;

                if (i == 0)
                {
                    fallbackX = resolution->first;
                    fallbackY = resolution->second;
                }

                if (resolution->first == Settings::video().mResolutionX
                    && resolution->second == Settings::video().mResolutionY)
                    supported = true;
            }

            if (!supported && mResolutionList->getItemCount())
            {
                if (fallbackX != 0 && fallbackY != 0)
                {
                    Settings::video().mResolutionX.set(fallbackX);
                    Settings::video().mResolutionY.set(fallbackY);
                }
            }

            mWindowBorderButton->setEnabled(false);
        }

        if (windowMode == Settings::WindowMode::WindowedFullscreen)
            mResolutionList->setEnabled(false);
    }

    void SettingsWindow::updateVSyncModeSettings()
    {
        mVSyncModeList->setIndexSelected(static_cast<size_t>(Settings::video().mVsyncMode));
    }

    void SettingsWindow::layoutControlsBox()
    {
        const int h = Settings::gui().mFontSize + 2;
        const int w = mControlsBox->getWidth() - 28;
        const int noWidgetsInRow = 2;
        const int totalH = static_cast<int>(mControlsBox->getChildCount() / noWidgetsInRow) * h;

        for (size_t i = 0; i < mControlsBox->getChildCount(); i++)
        {
            MyGUI::Widget* widget = mControlsBox->getChildAt(i);
            widget->setCoord(0, static_cast<int>(i / noWidgetsInRow * h), w, h);
        }

        // Canvas size must be expressed with VScroll disabled, otherwise MyGUI would expand the scroll area when the
        // scrollbar is hidden
        mControlsBox->setVisibleVScroll(false);
        mControlsBox->setCanvasSize(mControlsBox->getWidth(), std::max(totalH, mControlsBox->getHeight()));
        mControlsBox->setVisibleVScroll(true);
    }

    void SettingsWindow::renderScriptSettings()
    {
        mScriptAdapter->detach();

        mScriptList->removeAllItems();
        mScriptView->setCanvasSize({ 0, 0 });

        struct WeightedPage
        {
            size_t mIndex;
            std::string mName;
            size_t mNameWeight;
            size_t mHintWeight;

            constexpr bool operator<(const WeightedPage& rhs) const
            {
                if (mNameWeight != rhs.mNameWeight)
                    return mNameWeight > rhs.mNameWeight;
                if (mHintWeight != rhs.mHintWeight)
                    return mHintWeight > rhs.mHintWeight;
                return mName < rhs.mName;
            }
        };

        const std::vector<std::string> patternArray = generatePatternArray(mScriptFilter->getCaption());
        std::vector<WeightedPage> weightedPages;
        weightedPages.reserve(LuaUi::scriptSettingsPageCount());
        for (size_t i = 0; i < LuaUi::scriptSettingsPageCount(); ++i)
        {
            LuaUi::ScriptSettingsPage page = LuaUi::scriptSettingsPageAt(i);
            size_t nameWeight = weightedSearch(page.mName, patternArray);
            size_t hintWeight = weightedSearch(page.mSearchHints, patternArray);
            if ((nameWeight + hintWeight) > 0)
                weightedPages.push_back({ i, page.mName, nameWeight, hintWeight });
        }
        std::sort(weightedPages.begin(), weightedPages.end());
        for (const WeightedPage& weightedPage : weightedPages)
            mScriptList->addItem(weightedPage.mName, weightedPage.mIndex);

        // Hide script settings when the game world isn't loaded
        bool disabled = LuaUi::scriptSettingsPageCount() == 0;
        mScriptFilter->setVisible(!disabled);
        mScriptList->setVisible(!disabled);
        mScriptBox->setVisible(!disabled);
        mScriptDisabled->setVisible(disabled);

        LuaUi::attachPageAt(mCurrentPage, mScriptAdapter);
    }

    void SettingsWindow::onScriptFilterChange(MyGUI::EditBox*)
    {
        renderScriptSettings();
    }

    void SettingsWindow::onScriptListSelection(MyGUI::ListBox*, size_t index)
    {
        mScriptAdapter->detach();
        mCurrentPage = static_cast<size_t>(-1);
        if (index < mScriptList->getItemCount())
        {
            mCurrentPage = *mScriptList->getItemDataAt<size_t>(index);
            LuaUi::attachPageAt(mCurrentPage, mScriptAdapter);
        }
    }

    void SettingsWindow::onRebindAction(MyGUI::Widget* sender)
    {
        int actionId = *sender->getUserData<int>();

        sender->castType<MyGUI::Button>()->setCaptionWithReplacing("#{Interface:None}");

        MWBase::Environment::get().getWindowManager()->staticMessageBox("#{OMWEngine:RebindAction}");
        MWBase::Environment::get().getWindowManager()->disallowMouse();

        MWBase::Environment::get().getInputManager()->enableDetectingBindingMode(actionId, mKeyboardMode);
    }

    void SettingsWindow::onInputTabMouseWheel(MyGUI::Widget* /*sender*/, int rel)
    {
        if (mControlsBox->getViewOffset().top + rel * 0.3f > 0)
            mControlsBox->setViewOffset(MyGUI::IntPoint(0, 0));
        else
            mControlsBox->setViewOffset(
                MyGUI::IntPoint(0, static_cast<int>(mControlsBox->getViewOffset().top + rel * 0.3f)));
    }

    void SettingsWindow::onResetDefaultBindings(MyGUI::Widget* /*sender*/)
    {
        ConfirmationDialog* dialog = MWBase::Environment::get().getWindowManager()->getConfirmationDialog();
        dialog->askForConfirmation("#{OMWEngine:ConfirmResetBindings}");
        dialog->eventOkClicked.clear();
        dialog->eventOkClicked += MyGUI::newDelegate(this, &SettingsWindow::onResetDefaultBindingsAccept);
        dialog->eventCancelClicked.clear();
    }

    void SettingsWindow::onResetDefaultBindingsAccept()
    {
        if (mKeyboardMode)
            MWBase::Environment::get().getInputManager()->resetToDefaultKeyBindings();
        else
            MWBase::Environment::get().getInputManager()->resetToDefaultControllerBindings();
        updateControlsBox();
    }

    void SettingsWindow::onOpen()
    {
        highlightCurrentResolution();
        updateControlsBox();
        updateLightSettings();
        updateWindowModeSettings();
        updateVSyncModeSettings();
        updateJournalSettings();
        updateDialogueColourSettings();
        updateMessagePositionSettings();
        resetScrollbars();
        renderScriptSettings();
        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mOkButton);
    }

    void SettingsWindow::onClose()
    {
        // Save user settings
        Settings::Manager::saveUser(mCfgMgr.getUserConfigPath() / "settings.cfg");
        MWBase::Environment::get().getLuaManager()->savePermanentStorage(mCfgMgr.getUserConfigPath());
        MWBase::Environment::get().getInputManager()->saveBindings();
    }

    void SettingsWindow::onWindowResize(MyGUI::Window* /*sender*/)
    {
        layoutControlsBox();
        layoutDialogueColourControls();
        layoutMessagePositionControls();
    }

    void SettingsWindow::computeMinimumWindowSize()
    {
        auto* window = mMainWidget->castType<MyGUI::Window>();
        auto minSize = window->getMinSize();

        // Preserve the existing layout's minimum width. Native colour/message tabs
        // and layout tabs marked AllowTabOverflow use the tab bar's overflow arrows.
        int tabBarWidth = 0;
        for (uint32_t i = 0; i < mSettingsTab->getItemCount(); i++)
        {
            const MyGUI::TabItem* tab = mSettingsTab->getItemAt(i);
            if (tab != mDialogueColourTab && tab != mMessagePositionTab
                && tab->getUserString("AllowTabOverflow") != "true")
                tabBarWidth += mSettingsTab->getButtonWidthAt(i);
        }

        // Need to include window margins
        int margins = mMainWidget->getWidth() - mSettingsTab->getWidth();
        int minimumWindowWidth = tabBarWidth + margins;

        if (minimumWindowWidth > minSize.width)
        {
            minSize.width = minimumWindowWidth;
            window->setMinSize(minSize);

            // Make a dummy call to setSize so MyGUI can apply any resize resulting from the change in MinSize
            mMainWidget->setSize(mMainWidget->getSize());
        }
    }

    void SettingsWindow::resetScrollbars()
    {
        mResolutionList->setScrollPosition(0);
        mControlsBox->setViewOffset(MyGUI::IntPoint(0, 0));
        if (mDialogueColourControls.mView)
            mDialogueColourControls.mView->setViewOffset(MyGUI::IntPoint(0, 0));
        if (mMessagePositionControls.mView)
            mMessagePositionControls.mView->setViewOffset(MyGUI::IntPoint(0, 0));
    }

    bool SettingsWindow::onControllerButtonEvent(const SDL_ControllerButtonEvent& arg)
    {
        if (arg.button == SDL_CONTROLLER_BUTTON_B)
        {
            onOkButtonClicked(mOkButton);
            return true;
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER)
        {
            size_t index = mSettingsTab->getIndexSelected();
            index = wrap(index, mSettingsTab->getItemCount(), -1);
            mSettingsTab->setIndexSelected(index);
            mSettingsTab->beginToItemSelected();
            MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("Menu Click"));
            return true;
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)
        {
            size_t index = mSettingsTab->getIndexSelected();
            index = wrap(index, mSettingsTab->getItemCount(), 1);
            mSettingsTab->setIndexSelected(index);
            mSettingsTab->beginToItemSelected();
            MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("Menu Click"));
            return true;
        }

        return false;
    }

}
