#include "mainmenu.hpp"

#include <algorithm>

#include <MyGUI_Button.h>
#include <MyGUI_FactoryManager.h>
#include <MyGUI_Gui.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_RenderManager.h>
#include <MyGUI_TextBox.h>

#include <components/misc/frameratelimiter.hpp>
#include <components/settings/values.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>
#include <components/widgets/imagebutton.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/statemanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/globals.hpp"

#include "../mwstate/saveselection.hpp"

#include "backgroundimage.hpp"
#include "confirmationdialog.hpp"
#include "mainmenutextbutton.hpp"
#include "savegamedialog.hpp"
#include "settingswindow.hpp"
#include "videowidget.hpp"

namespace MWGui
{
    void MenuVideo::run()
    {
        Misc::FrameRateLimiter frameRateLimiter
            = Misc::makeFrameRateLimiter(MWBase::Environment::get().getFrameRateLimit());
        const MWBase::WindowManager& windowManager = *MWBase::Environment::get().getWindowManager();
        bool paused = false;
        while (mRunning)
        {
            if (windowManager.isWindowVisible())
            {
                if (paused)
                {
                    mVideo->resume();
                    paused = false;
                }
                // If finished playing, start again
                if (!mVideo->update())
                    mVideo->playVideo("video\\menu_background.bik");
            }
            else if (!paused)
            {
                paused = true;
                mVideo->pause();
            }
            frameRateLimiter.limit();
        }
    }

    MenuVideo::MenuVideo(const VFS::Manager* vfs)
        : mRunning(true)
    {
        // Use black background to correct aspect ratio
        mVideoBackground = MyGUI::Gui::getInstance().createWidgetReal<MyGUI::ImageBox>(
            "ImageBox", 0, 0, 1, 1, MyGUI::Align::Default, "MainMenuBackground");
        mVideoBackground->setImageTexture("black");

        mVideo = mVideoBackground->createWidget<VideoWidget>(
            "ImageBox", 0, 0, 1, 1, MyGUI::Align::Stretch, "MainMenuBackground");
        mVideo->setVFS(vfs);

        mVideo->playVideo("video\\menu_background.bik");
        mThread = std::thread([this] { run(); });
    }

    void MenuVideo::resize(int screenWidth, int screenHeight)
    {
        const bool stretch = Settings::gui().mStretchMenuBackground;
        mVideoBackground->setSize(screenWidth, screenHeight);
        mVideo->autoResize(stretch);
        mVideo->setVisible(true);
    }

    void MenuVideo::commitFrame()
    {
        if (mVideo)
            mVideo->commitFrame();
    }

    MenuVideo::~MenuVideo()
    {
        mRunning = false;
        mThread.join();
        try
        {
            MyGUI::Gui::getInstance().destroyWidget(mVideoBackground);
        }
        catch (const MyGUI::Exception& e)
        {
            Log(Debug::Error) << "Error in the destructor: " << e.what();
        }
    }

    MainMenu::MainMenu(int w, int h, const VFS::Manager* vfs, const std::string& versionDescription)
        : WindowBase("openmw_mainmenu.layout")
        , mWidth(w)
        , mHeight(h)
        , mVFS(vfs)
        , mButtonBox(nullptr)
        , mBackground(nullptr)
    {
        MyGUI::FactoryManager::getInstance().registerFactory<MainMenuTextButton>("Widget");
        getWidget(mVersionText, "VersionText");
        mVersionText->setCaption(versionDescription);

        constexpr VFS::Path::NormalizedView menuBackgroundVideo("video/menu_background.bik");

        mHasAnimatedMenu = mVFS->exists(menuBackgroundVideo);
        mDisableGamepadCursor = Settings::gui().mControllerMenus;

        updateMenu();
    }

    void MainMenu::onResChange(int w, int h)
    {
        mWidth = w;
        mHeight = h;

        updateMenu();
        if (mVideo)
            mVideo->resize(w, h);
    }

    void MainMenu::setVisible(bool visible)
    {
        if (visible)
            updateMenu();

        bool isMainMenu = MWBase::Environment::get().getWindowManager()->containsMode(MWGui::GM_MainMenu)
            && MWBase::Environment::get().getStateManager()->getState() == MWBase::StateManager::State_NoGame;

        showBackground(isMainMenu);

        if (visible)
        {
            if (isMainMenu)
            {
                if (mContinueButton->getVisible())
                    MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mContinueButton);
                else if (mButtons["loadgame"]->getVisible())
                    MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mButtons["loadgame"]);
                else
                    MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mButtons["newgame"]);
            }
            else
                MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mButtons["return"]);
        }

        Layout::setVisible(visible);
    }

    void MainMenu::onNewGameConfirmed()
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(MWGui::GM_MainMenu);
        MWBase::Environment::get().getStateManager()->newGame();
    }

    void MainMenu::onExitConfirmed()
    {
        MWBase::Environment::get().getStateManager()->requestQuit();
    }

    void MainMenu::onButtonClicked(MyGUI::Widget* sender)
    {
        MWBase::WindowManager* winMgr = MWBase::Environment::get().getWindowManager();

        const std::string& name = *sender->getUserData<std::string>();
        winMgr->playSound(ESM::RefId::stringRefId("Menu Click"));
        if (name == "return")
            winMgr->removeGuiMode(GM_MainMenu);
        else if (name == "continue")
        {
            MWBase::StateManager* stateManager = MWBase::Environment::get().getStateManager();
            // Continue belongs to the title screen. A stale event must never replace a running game.
            if (stateManager->getState() != MWBase::StateManager::State_NoGame)
                return;

            // Re-select here: saves may have been deleted in the Load Game dialog since the menu opened.
            const MWState::SaveSelection save
                = MWState::selectLatestSave(stateManager->characterBegin(), stateManager->characterEnd());
            if (save.mSlot)
                stateManager->requestLoad(save.mCharacter, save.mSlot->mPath);
            else
                updateMenu();
        }
        else if (name == "credits")
            winMgr->playVideo("mw_credits.bik", true);
        else if (name == "exitgame")
        {
            if (MWBase::Environment::get().getStateManager()->getState() == MWBase::StateManager::State_NoGame)
                onExitConfirmed();
            else
            {
                ConfirmationDialog* dialog = winMgr->getConfirmationDialog();
                dialog->askForConfirmation("#{OMWEngine:QuitGameConfirmation}");
                dialog->eventOkClicked.clear();
                dialog->eventOkClicked += MyGUI::newDelegate(this, &MainMenu::onExitConfirmed);
                dialog->eventCancelClicked.clear();
            }
        }
        else if (name == "newgame")
        {
            if (MWBase::Environment::get().getStateManager()->getState() == MWBase::StateManager::State_NoGame)
                onNewGameConfirmed();
            else
            {
                ConfirmationDialog* dialog = winMgr->getConfirmationDialog();
                dialog->askForConfirmation("#{OMWEngine:NewGameConfirmation}");
                dialog->eventOkClicked.clear();
                dialog->eventOkClicked += MyGUI::newDelegate(this, &MainMenu::onNewGameConfirmed);
                dialog->eventCancelClicked.clear();
            }
        }
        else if (name == "loadgame" || name == "savegame")
        {
            if (!mSaveGameDialog)
                mSaveGameDialog = std::make_unique<SaveGameDialog>();
            mSaveGameDialog->setLoadOrSave(name == "loadgame");
            mSaveGameDialog->setVisible(true);
        }

        if (winMgr->isSettingsWindowVisible() || name == "options")
        {
            winMgr->toggleSettingsWindow();
        }
    }

    bool MainMenu::onControllerButtonEvent(const SDL_ControllerButtonEvent& arg)
    {
        if (arg.button == SDL_CONTROLLER_BUTTON_A)
        {
            MWBase::Environment::get().getWindowManager()->injectKeyPress(MyGUI::KeyCode::Space, 0, false);
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_B || arg.button == SDL_CONTROLLER_BUTTON_START)
        {
            if (mButtons["return"]->getVisible())
                onButtonClicked(mButtons["return"]);
            else
                MWBase::Environment::get().getWindowManager()->injectKeyPress(MyGUI::KeyCode::Escape, 0, false);
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_DPAD_UP)
        {
            MyGUI::InputManager::getInstance().injectKeyPress(MyGUI::KeyCode::LeftShift);
            MWBase::Environment::get().getWindowManager()->injectKeyPress(MyGUI::KeyCode::Tab, 0, false);
            MyGUI::InputManager::getInstance().injectKeyRelease(MyGUI::KeyCode::LeftShift);
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN)
        {
            MWBase::Environment::get().getWindowManager()->injectKeyPress(MyGUI::KeyCode::Tab, 0, false);
        }
        return true;
    }

    void MainMenu::showBackground(bool show)
    {
        if (mVideo && !show)
        {
            mVideo.reset();
        }
        if (mBackground && !show)
        {
            MyGUI::Gui::getInstance().destroyWidget(mBackground);
            mBackground = nullptr;
        }

        if (!show)
            return;

        const bool stretch = Settings::gui().mStretchMenuBackground;

        if (mHasAnimatedMenu)
        {
            if (!mVideo)
                mVideo.emplace(mVFS);

            const auto& viewSize = MyGUI::RenderManager::getInstance().getViewSize();
            int screenWidth = viewSize.width;
            int screenHeight = viewSize.height;
            mVideo->resize(screenWidth, screenHeight);
        }
        else
        {
            if (!mBackground)
            {
                mBackground = MyGUI::Gui::getInstance().createWidgetReal<BackgroundImage>(
                    "ImageBox", 0, 0, 1, 1, MyGUI::Align::Stretch, "MainMenuBackground");
                mBackground->setBackgroundImage("textures\\menu_morrowind.dds", true, stretch);
            }
            mBackground->setVisible(true);
        }
    }

    void MainMenu::onFrame(float)
    {
        if (mVideo)
            mVideo->commitFrame();

        // The Load Game dialog can delete the final save while the title menu stays open.
        // This is only a presence check, not a per-frame filesystem scan or full save search.
        if (isVisible()
            && MWBase::Environment::get().getStateManager()->getState() == MWBase::StateManager::State_NoGame)
        {
            MWBase::StateManager* stateManager = MWBase::Environment::get().getStateManager();
            const bool hasSaves = std::any_of(stateManager->characterBegin(), stateManager->characterEnd(),
                [](const MWState::Character& character) { return character.begin() != character.end(); });
            if (mContinueButton->getVisible() != hasSaves)
                updateMenu();

            MyGUI::InputManager& input = MyGUI::InputManager::getInstance();
            MWBase::WindowManager* windowManager = MWBase::Environment::get().getWindowManager();
            if (!input.isModalAny() && !windowManager->isSettingsWindowVisible())
            {
                // Closing Load Game after deleting the final save cannot restore its now-hidden button.
                // Wait until the modal closes, and leave any valid focus selected by the player intact.
                MyGUI::Widget* focus = input.getKeyFocusWidget();
                if (!focus || !focus->getInheritedVisible() || !focus->getInheritedEnabled())
                    windowManager->setKeyFocusWidget(
                        hasSaves ? static_cast<MyGUI::Widget*>(mContinueButton) : mButtons["newgame"]);
            }
        }
    }

    bool MainMenu::exit()
    {
        if (MWBase::Environment::get().getWindowManager()->isSettingsWindowVisible())
        {
            MWBase::Environment::get().getWindowManager()->toggleSettingsWindow();
            return false;
        }

        return MWBase::Environment::get().getStateManager()->getState() == MWBase::StateManager::State_Running;
    }

    void MainMenu::updateMenu()
    {
        setCoord(0, 0, mWidth, mHeight);

        if (!mButtonBox)
            mButtonBox
                = mMainWidget->createWidget<MyGUI::Widget>({}, MyGUI::IntCoord(0, 0, 0, 0), MyGUI::Align::Default);

        // Create first so keyboard/controller traversal follows the visible top-to-bottom order.
        // Keep the configured game font with dedicated gold title-menu interaction colours.
        if (!mContinueButton)
        {
            mContinueButton = mButtonBox->createWidget<MainMenuTextButton>(
                "MW_MainMenuTextButton", MyGUI::IntCoord(0, 0, 0, 48), MyGUI::Align::Default);
            mContinueButton->setCaptionWithReplacing("#{Interface:Continue}");
            mContinueButton->setFontHeight(32);
            mContinueButton->setTextShadow(true);
            mContinueButton->setTextShadowColour(MyGUI::Colour(0, 0, 0, 1));
            mContinueButton->eventMouseButtonClick += MyGUI::newDelegate(this, &MainMenu::onButtonClicked);
            mContinueButton->setUserData(std::string("continue"));
        }

        int curH = 0;

        MWBase::StateManager* stateManager = MWBase::Environment::get().getStateManager();
        MWBase::StateManager::State state = stateManager->getState();
        const bool canContinue = state == MWBase::StateManager::State_NoGame
            && MWState::selectLatestSave(stateManager->characterBegin(), stateManager->characterEnd()).mSlot;
        mContinueButton->setVisible(canContinue);

        mVersionText->setVisible(state == MWBase::StateManager::State_NoGame);

        std::vector<std::string> buttons;

        if (state == MWBase::StateManager::State_Running)
            buttons.emplace_back("return");

        buttons.emplace_back("newgame");

        if (state == MWBase::StateManager::State_Running
            && MWBase::Environment::get().getWorld()->getGlobalInt(MWWorld::Globals::sCharGenState) == -1
            && MWBase::Environment::get().getWindowManager()->isSavingAllowed())
            buttons.emplace_back("savegame");

        if (MWBase::Environment::get().getStateManager()->characterBegin()
            != MWBase::Environment::get().getStateManager()->characterEnd())
            buttons.emplace_back("loadgame");

        buttons.emplace_back("options");

        if (state == MWBase::StateManager::State_NoGame)
            buttons.emplace_back("credits");

        buttons.emplace_back("exitgame");

        // Create new buttons if needed
        for (std::string_view id : { "return", "newgame", "savegame", "loadgame", "options", "credits", "exitgame" })
        {
            if (mButtons.find(id) == mButtons.end())
            {
                Gui::ImageButton* button = mButtonBox->createWidget<Gui::ImageButton>(
                    "ImageBox", MyGUI::IntCoord(0, curH, 0, 0), MyGUI::Align::Default);
                const std::string& buttonId = mButtons.emplace(id, button).first->first;
                button->setProperty("ImageHighlighted", "textures\\menu_" + buttonId + "_over.dds");
                button->setProperty("ImageNormal", "textures\\menu_" + buttonId + ".dds");
                button->setProperty("ImagePushed", "textures\\menu_" + buttonId + "_pressed.dds");
                button->eventMouseButtonClick += MyGUI::newDelegate(this, &MainMenu::onButtonClicked);
                button->setUserData(buttonId);
            }
        }

        // Start by hiding all buttons
        int maxwidth = 0;
        for (const auto& buttonPair : mButtons)
        {
            buttonPair.second->setVisible(false);
            MyGUI::IntSize requested = buttonPair.second->getRequestedSize();
            if (requested.width > maxwidth)
                maxwidth = requested.width;
        }

        if (canContinue)
        {
            // Match the 64-pixel image rows after their existing 16-pixel padding trim.
            constexpr int rowHeight = 48;
            const int width = mContinueButton->getTextSize().width + 32;
            maxwidth = std::max(maxwidth, width);
            mContinueButton->setCoord((maxwidth - width) / 2, curH, width, rowHeight);
            curH += rowHeight;
        }

        // Now show and position the ones we want
        for (const std::string& buttonId : buttons)
        {
            auto it = mButtons.find(buttonId);
            assert(it != mButtons.end());
            Gui::ImageButton* button = it->second;
            button->setVisible(true);

            // By default, assume that all menu buttons textures should have 64 height.
            // If they have a different resolution, scale them.
            MyGUI::IntSize requested = button->getRequestedSize();
            float scale = requested.height / 64.f;

            button->setImageCoord(MyGUI::IntCoord(0, 0, requested.width, requested.height));
            // Trim off some of the excessive padding
            // TODO: perhaps do this within ImageButton?
            int height = requested.height;
            button->setImageTile(MyGUI::IntSize(requested.width, static_cast<int>(requested.height - 16 * scale)));
            button->setCoord(static_cast<int>((maxwidth - requested.width / scale) / 2), curH,
                static_cast<int>(requested.width / scale), static_cast<int>(height / scale - 16));
            curH += static_cast<int>(height / scale - 16);
        }

        if (state == MWBase::StateManager::State_NoGame)
        {
            // Align with the background image
            int bottomPadding = 24;
            mButtonBox->setCoord(mWidth / 2 - maxwidth / 2, mHeight - curH - bottomPadding, maxwidth, curH);
        }
        else
            mButtonBox->setCoord(mWidth / 2 - maxwidth / 2, mHeight / 2 - curH / 2, maxwidth, curH);
    }
}
