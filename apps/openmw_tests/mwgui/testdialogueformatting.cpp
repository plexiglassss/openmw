#include "apps/openmw/mwgui/dialogueformatting.hpp"
#include "apps/openmw/mwgui/textcolours.hpp"

#include <components/settings/values.hpp>

#include <MyGUI_FontManager.h>
#include <MyGUI_ResourceManager.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <tuple>

namespace MWGui
{
    namespace
    {
        using Misc::DialogueText::Category;
        using Misc::DialogueText::Context;

        class ColourTestFont final : public MyGUI::IFont
        {
        public:
            ColourTestFont(std::string_view name, float advance)
                : mLetter(0, advance, 16.f, advance)
                , mSpace(' ', 4.f, 16.f, 4.f)
            {
                setResourceName(name);
            }

            const MyGUI::GlyphInfo* getGlyphInfo(MyGUI::Char c) const override
            {
                return c == ' ' ? &mSpace : &mLetter;
            }
            MyGUI::ITexture* getTextureFont() const override { return nullptr; }
            int getDefaultHeight() const override { return 16; }

        private:
            MyGUI::GlyphInfo mLetter;
            MyGUI::GlyphInfo mSpace;
        };

        class DialogueFormattingTest : public testing::Test
        {
        protected:
            MyGUI::ResourceManager mResources;
            MyGUI::FontManager mFonts;
            ColourTestFont mFont{ "DialogueColoursTest", 8.f };
            ColourTestFont mBookFont{ "Journalbook DialogueColoursTest", 10.f };
            const int mOldFontSize = Settings::gui().mFontSize;
            const bool mOldEnabled = Settings::gui().mDialogueColouring;
            const bool mOldTheme = Settings::gui().mDialogueUseSystemColour;
            const bool mOldJournal = Settings::gui().mJournalColourNarration;
            const MyGUI::Colour mOldNormal = Settings::gui().mColorDialogueNormal;
            const Settings::CategorySettingVector mOldChanges = Settings::Manager::mChangedSettings;

            void SetUp() override
            {
                mResources.addResource(&mFont);
                mResources.addResource(&mBookFont);
                mFonts.setDefaultFont("DialogueColoursTest");
                Settings::gui().mFontSize.set(16);
                Settings::gui().mDialogueColouring.set(true);
                Settings::gui().mDialogueUseSystemColour.set(true);
                Settings::gui().mJournalColourNarration.set(true);
            }

            void TearDown() override
            {
                Settings::gui().mFontSize.set(mOldFontSize);
                Settings::gui().mDialogueColouring.set(mOldEnabled);
                Settings::gui().mDialogueUseSystemColour.set(mOldTheme);
                Settings::gui().mJournalColourNarration.set(mOldJournal);
                Settings::gui().mColorDialogueNormal.set(mOldNormal);
                Settings::Manager::mChangedSettings = mOldChanges;
                mResources.removeResource(&mBookFont);
                mResources.removeResource(&mFont);
            }

            static TextColours theme()
            {
                TextColours colours{};
                colours.normal = MyGUI::Colour(.82f, .68f, .42f);
                colours.notify = MyGUI::Colour(.88f, .72f, .48f);
                colours.link = MyGUI::Colour(.2f, .4f, .8f);
                colours.linkOver = MyGUI::Colour(.3f, .5f, .9f);
                colours.linkPressed = MyGUI::Colour(.1f, .3f, .7f);
                colours.journalLink = MyGUI::Colour(.1f, .15f, .5f);
                colours.journalLinkOver = MyGUI::Colour(.2f, .25f, .6f);
                colours.journalLinkPressed = MyGUI::Colour(.05f, .1f, .4f);
                return colours;
            }

            static std::shared_ptr<TypesetBook> render(std::string_view text,
                const std::vector<Misc::DialogueText::Span>& spans, const std::vector<DialogueTextLink>& links,
                const DialoguePalette& palette, bool bookFont = false, int width = 1000,
                BookTypesetter::Alignment alignment = BookTypesetter::AlignLeft)
            {
                auto typesetter = BookTypesetter::create(width, 320, 16);
                typesetter->setLineAlignment(alignment);
                writeDialogueText(*typesetter, text, spans, links, palette, bookFont);
                return typesetter->complete();
            }

            static std::string visibleText(const TypesetBook& book)
            {
                std::string result;
                for (const auto& run : book.getTextRuns())
                    result.append(run.mText);
                return result;
            }
        };

        TEST_F(DialogueFormattingTest, OrdinaryDialogueKeepsActualThemeColour)
        {
            const auto colours = theme();
            const auto book = render("Take this scroll. You may need it.", {}, {}, getDialoguePalette(colours));
            ASSERT_FALSE(book->getTextRuns().empty());
            for (const auto& run : book->getTextRuns())
                EXPECT_EQ(run.mColour, colours.normal);
        }

        TEST_F(DialogueFormattingTest, ExplicitInventoryCategoryWorksWithLocalizedText)
        {
            const std::string text = "Un parchemin a été ajouté à votre inventaire.";
            const auto book = render(text, { { 0, text.size(), Category::Inventory } }, {},
                getDialoguePalette(theme(), true));
            ASSERT_FALSE(book->getTextRuns().empty());
            for (const auto& run : book->getTextRuns())
                EXPECT_EQ(run.mColour, Settings::gui().mColorDialogueInventory.get());
        }

        TEST_F(DialogueFormattingTest, DisabledFeatureRestoresBothOriginalTextColours)
        {
            Settings::gui().mDialogueColouring.set(false);
            Settings::gui().mDialogueUseSystemColour.set(false);
            Settings::gui().mColorDialogueNormal.set(MyGUI::Colour::Red);
            const auto colours = theme();
            for (const auto& colour : getDialoguePalette(colours).mText)
                EXPECT_EQ(colour, colours.normal);
            for (const auto& colour : getDialoguePalette(colours, true).mText)
                EXPECT_EQ(colour, colours.notify);
            for (const auto& colour : getJournalDialoguePalette(colours).mText)
                EXPECT_EQ(colour, MyGUI::Colour::Black);
        }

        TEST_F(DialogueFormattingTest, CustomNormalColourDoesNotReplaceTopicColours)
        {
            Settings::gui().mDialogueUseSystemColour.set(false);
            Settings::gui().mColorDialogueNormal.set(MyGUI::Colour::Green);
            const std::string text = "Visit Balmora.";
            const auto colours = theme();
            const auto book = render(text, {}, { { 6, 13, 71 } }, getDialoguePalette(colours));
            bool foundLink = false;
            for (const auto& run : book->getTextRuns())
            {
                EXPECT_EQ(run.mColour, run.mInteractiveId ? colours.link : MyGUI::Colour::Green);
                if (run.mInteractiveId)
                {
                    foundLink = true;
                    EXPECT_EQ(run.mInteractiveId, 71);
                    EXPECT_EQ(book->getLinkAt((run.mLeft + run.mRight) / 2, (run.mTop + run.mBottom) / 2), 71);
                }
            }
            EXPECT_TRUE(foundLink);
        }

        TEST_F(DialogueFormattingTest, TopicInsideStageDirectionRemainsClickable)
        {
            const std::string text = "Hello. [Ariela coughs.] Ask about Balmora.";
            const auto spans = Misc::DialogueText::classify(text, Context::Dialogue);
            const size_t name = text.find("Ariela");
            const size_t town = text.find("Balmora");
            const std::vector<DialogueTextLink> links{ { name, name + 6, 18 }, { town, town + 7, 42 } };
            const auto palette = getDialoguePalette(theme());
            const auto book = render(text, spans, links, palette);
            const auto original = render(text, {}, links, palette);
            EXPECT_EQ(visibleText(*book), visibleText(*original));
            EXPECT_EQ(book->getSize(), original->getSize());
            bool foundNarration = false;
            bool foundLink = false;
            for (const auto& run : book->getTextRuns())
            {
                if (run.mInteractiveId)
                {
                    foundLink = true;
                    EXPECT_EQ(run.mColour, theme().link);
                    EXPECT_EQ(book->getLinkAt((run.mLeft + run.mRight) / 2, (run.mTop + run.mBottom) / 2),
                        run.mInteractiveId);
                }
                else if (run.mText.find("coughs") != std::string_view::npos)
                {
                    foundNarration = true;
                    EXPECT_EQ(run.mColour, Settings::gui().mColorDialogueNarration.get());
                }
            }
            EXPECT_TRUE(foundLink);
            EXPECT_TRUE(foundNarration);
        }

        TEST_F(DialogueFormattingTest, JournalNarrationKeepsPaperFontAndBaseText)
        {
            const std::string text = "abc def";
            const auto palette = getJournalDialoguePalette(theme());
            const auto book = render(text, { { 0, 3, Category::Narration }, { 3, 7, Category::Default } }, {},
                palette, true);
            const auto original = render(text, {}, {}, palette, true);
            EXPECT_EQ(book->getSize(), original->getSize());
            EXPECT_EQ(visibleText(*book), visibleText(*original));
            EXPECT_EQ(book->getSize().first, 64u); // six 10px journal glyphs plus a 4px space
            bool foundNarration = false;
            bool foundPlain = false;
            for (const auto& run : book->getTextRuns())
            {
                if (run.mText.find("abc") != std::string_view::npos)
                {
                    foundNarration = true;
                    EXPECT_EQ(run.mColour, Settings::gui().mColorJournalNarration.get());
                }
                if (run.mText.find("def") != std::string_view::npos)
                {
                    foundPlain = true;
                    EXPECT_EQ(run.mColour, MyGUI::Colour::Black);
                }
            }
            EXPECT_TRUE(foundNarration);
            EXPECT_TRUE(foundPlain);
        }

        TEST_F(DialogueFormattingTest, JournalColourOptOutDoesNotDisableDialogueNarration)
        {
            Settings::gui().mJournalColourNarration.set(false);
            const auto index = static_cast<size_t>(Category::Narration);
            EXPECT_EQ(getJournalDialoguePalette(theme()).mText[index], MyGUI::Colour::Black);
            EXPECT_EQ(getDialoguePalette(theme()).mText[index], Settings::gui().mColorDialogueNarration.get());
        }

        TEST_F(DialogueFormattingTest, ColourBoundariesDoNotChangeJustifiedWrappingOrLinkHits)
        {
            const std::string text = "Take this scroll. [Ariela coughs softly.] Ask about Balmora and return tomorrow.";
            const auto spans = Misc::DialogueText::classify(text, Context::Dialogue);
            const size_t topic = text.find("Balmora");
            const std::vector<DialogueTextLink> links{ { topic, topic + 7, 99 } };
            const auto palette = getJournalDialoguePalette(theme());
            const auto coloured = render(text, spans, links, palette, true, 238, BookTypesetter::AlignJustify);
            const auto original = render(text, {}, links, palette, true, 238, BookTypesetter::AlignJustify);
            EXPECT_EQ(coloured->getSize(), original->getSize());
            EXPECT_EQ(coloured->pageCount(), original->pageCount());
            EXPECT_EQ(visibleText(*coloured), visibleText(*original));
            std::vector<std::tuple<int, int, int, int>> originalHits;
            std::vector<std::tuple<int, int, int, int>> colouredHits;
            for (const auto& run : original->getTextRuns())
                if (run.mInteractiveId)
                    originalHits.emplace_back(run.mLeft, run.mTop, run.mRight, run.mBottom);
            for (const auto& run : coloured->getTextRuns())
            {
                EXPECT_LE(run.mRight, 238);
                if (run.mInteractiveId)
                    colouredHits.emplace_back(run.mLeft, run.mTop, run.mRight, run.mBottom);
            }
            EXPECT_FALSE(colouredHits.empty());
            EXPECT_EQ(colouredHits, originalHits);
        }
    }
}
