#include "apps/openmw/mwgui/bookpage.hpp"
#include "apps/openmw/mwgui/journalbooks.hpp"

#include <components/settings/values.hpp>

#include <MyGUI_FontManager.h>
#include <MyGUI_ResourceManager.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <iterator>
#include <map>
#include <string>
#include <utility>

namespace MWGui
{
    namespace
    {
        // Deterministic font metrics exercise the real typesetter without a renderer,
        // texture atlas, game data or a platform-specific installed font.
        class MetricsFont final : public MyGUI::IFont
        {
        public:
            explicit MetricsFont(std::string_view name, int height = 16)
                : mHeight(height)
                , mLetter(0, height / 2.f, static_cast<float>(height), height / 2.f)
                , mSpace(' ', height / 4.f, static_cast<float>(height), height / 4.f)
            {
                setResourceName(name);
            }

            const MyGUI::GlyphInfo* getGlyphInfo(MyGUI::Char codePoint) const override
            {
                return codePoint == ' ' ? &mSpace : &mLetter;
            }

            MyGUI::ITexture* getTextureFont() const override { return nullptr; }
            int getDefaultHeight() const override { return mHeight; }

            void setRightOverhang(float pixels) { mLetter.width = mLetter.advance + pixels; }

        private:
            int mHeight;
            MyGUI::GlyphInfo mLetter;
            MyGUI::GlyphInfo mSpace;
        };

        class JournalTypographyTest : public testing::Test
        {
        protected:
            MyGUI::ResourceManager mResources;
            MyGUI::FontManager mFonts;
            MetricsFont mFont{ "JournalTypographyTest" };
            MetricsFont mBookFont{ "Journalbook JournalTypographyTest" };
            MetricsFont mHighResolutionFont{ "JournalTypographyTestHighResolution", 32 };
            const int mOldFontSize = Settings::gui().mFontSize.get();
            const int mOldJournalFontSize = Settings::gui().mJournalFontSize.get();
            const int mOldJournalAlignment = Settings::gui().mJournalTextAlignment.get();
            const Settings::CategorySettingVector mOldChangedSettings = Settings::Manager::mChangedSettings;

            void SetUp() override
            {
                mResources.addResource(&mFont);
                mResources.addResource(&mBookFont);
                mResources.addResource(&mHighResolutionFont);
                mFonts.setDefaultFont("JournalTypographyTest");
                Settings::gui().mFontSize.set(16);
            }

            void TearDown() override
            {
                Settings::gui().mFontSize.set(mOldFontSize);
                Settings::gui().mJournalFontSize.set(mOldJournalFontSize);
                Settings::gui().mJournalTextAlignment.set(mOldJournalAlignment);
                Settings::Manager::mChangedSettings = mOldChangedSettings;
                // These resources are fixture members, rather than heap-owned resources.
                mResources.removeResource(&mHighResolutionFont);
                mResources.removeResource(&mBookFont);
                mResources.removeResource(&mFont);
            }

            static BookTypesetter::Style* style(const std::shared_ptr<BookTypesetter>& typesetter)
            {
                return typesetter->createStyle("JournalTypographyTest", MyGUI::Colour::Black, false);
            }

            static std::map<int, std::pair<int, int>> lineExtents(const TypesetBook& book)
            {
                std::map<int, std::pair<int, int>> result;
                for (const auto& run : book.getTextRuns())
                {
                    const auto [it, inserted] = result.emplace(run.mTop, std::make_pair(run.mLeft, run.mRight));
                    if (!inserted)
                    {
                        it->second.first = std::min(it->second.first, run.mLeft);
                        it->second.second = std::max(it->second.second, run.mRight);
                    }
                }
                return result;
            }

            static std::shared_ptr<TypesetBook> typeset(std::string_view text, BookTypesetter::Alignment alignment,
                int width = 60, int height = 320, int fontSize = 16)
            {
                auto typesetter = BookTypesetter::create(width, height, fontSize);
                typesetter->setSectionAlignment(alignment);
                typesetter->write(style(typesetter), text);
                return typesetter->complete();
            }
        };

        TEST_F(JournalTypographyTest, DefaultDocumentKeepsGlobalFontSize)
        {
            const auto book = typeset("abc", BookTypesetter::AlignLeft, 100, 100, 0);
            EXPECT_EQ(book->getSize(), std::make_pair(24u, 16u));
        }

        TEST_F(JournalTypographyTest, ExplicitSizeChangesOnlyThatDocument)
        {
            const auto smaller = typeset("abc", BookTypesetter::AlignLeft, 100, 100, 12);
            const auto ordinary = typeset("abc", BookTypesetter::AlignLeft, 100, 100, 0);
            EXPECT_EQ(smaller->getSize(), std::make_pair(18u, 12u));
            EXPECT_EQ(ordinary->getSize(), std::make_pair(24u, 16u));
            EXPECT_EQ(Settings::gui().mFontSize.get(), 16);
        }

        TEST_F(JournalTypographyTest, DocumentSnapshotsItsFontSize)
        {
            auto typesetter = BookTypesetter::create(100, 100);
            Settings::gui().mFontSize.set(18);
            typesetter->write(style(typesetter), "abc");
            EXPECT_EQ(typesetter->complete()->getSize(), std::make_pair(24u, 16u));
        }

        TEST_F(JournalTypographyTest, JournalBooksUseJournalSettingWhileOtherDocumentsKeepGlobalSize)
        {
            Settings::gui().mJournalFontSize.set(12);
            Settings::gui().mJournalTextAlignment.set(3);
            JournalBooks journal(nullptr, ToUTF8::WINDOWS_1252);
            const auto book = journal.createEmptyJournalBook();
            const auto runs = book->getTextRuns();
            ASSERT_FALSE(runs.empty());
            for (const auto& run : runs)
                EXPECT_EQ(run.mBottom - run.mTop, 12);

            // The date/title line is kept left aligned; wrapped prose fills the page.
            EXPECT_EQ(runs.front().mLeft, 0);
            const auto lines = lineExtents(*book);
            EXPECT_TRUE(std::any_of(std::next(lines.begin()), lines.end(), [](const auto& line) {
                return line.second == std::make_pair(0, 238);
            }));
            const auto ordinary = typeset("abc", BookTypesetter::AlignLeft, 100, 100, 0);
            EXPECT_EQ(ordinary->getSize(), std::make_pair(24u, 16u));
        }

        TEST_F(JournalTypographyTest, JournalRightInsetContainsSmallGlyphOverhangWithoutChangingOtherDocuments)
        {
            Settings::gui().mJournalFontSize.set(16);
            mBookFont.setRightOverhang(2.f);
            for (int alignment : { 2, 3 }) // Right and Justified journal settings.
            {
                Settings::gui().mJournalTextAlignment.set(alignment);
                JournalBooks journal(nullptr, ToUTF8::WINDOWS_1252);
                const auto book = journal.createEmptyJournalBook();
                const auto runs = book->getTextRuns();
                ASSERT_FALSE(runs.empty());
                EXPECT_EQ(runs.front().mLeft, 0); // The heading's left origin is unchanged.
                bool reachesReadingEdge = false;
                for (const auto& run : runs)
                {
                    if (run.mText.find_last_not_of(' ') == std::string_view::npos)
                        continue;
                    // At size 16 this font has zero bearing and ink two pixels
                    // wider than its advance. Check containment without a GPU.
                    EXPECT_LE(run.mRight + 2, 240);
                    reachesReadingEdge = reachesReadingEdge || run.mRight == 238;
                }
                EXPECT_TRUE(reachesReadingEdge);
                if (alignment == 3)
                    EXPECT_EQ(lineExtents(*book).at(16).first, 0);
            }

            auto ordinary = BookTypesetter::create(240, 320, 16);
            ordinary->setSectionAlignment(BookTypesetter::AlignRight);
            ordinary->write(ordinary->createStyle({}, MyGUI::Colour::Black), "abc");
            EXPECT_EQ(lineExtents(*ordinary->complete()).at(0), std::make_pair(216, 240));
        }

        TEST_F(JournalTypographyTest, FontAtlasResolutionDoesNotChangeDocumentMetrics)
        {
            auto typesetter = BookTypesetter::create(100, 100, 12);
            auto* textStyle = typesetter->createStyle("JournalTypographyTestHighResolution", MyGUI::Colour::Black, false);
            typesetter->write(textStyle, "abc");
            EXPECT_EQ(typesetter->complete()->getSize(), std::make_pair(18u, 12u));
        }

        TEST_F(JournalTypographyTest, SmallerTextReflowsAndReducesPageCount)
        {
            const auto normal = typeset("aaa bbb ccc ddd eee fff", BookTypesetter::AlignLeft, 60, 32, 16);
            const auto small = typeset("aaa bbb ccc ddd eee fff", BookTypesetter::AlignLeft, 60, 32, 12);
            EXPECT_EQ(lineExtents(*normal).size(), 3u);
            EXPECT_EQ(lineExtents(*small).size(), 2u);
            EXPECT_EQ(normal->pageCount(), 2u);
            EXPECT_EQ(small->pageCount(), 1u);
        }

        TEST_F(JournalTypographyTest, SeparateEntriesStillSharePagesUntilFull)
        {
            auto typesetter = BookTypesetter::create(60, 32, 16);
            auto* textStyle = style(typesetter);
            typesetter->write(textStyle, "aaa bbb");
            typesetter->sectionBreak();
            typesetter->write(textStyle, "ccc ddd");
            typesetter->sectionBreak();
            typesetter->write(textStyle, "eee fff");
            const auto book = typesetter->complete();
            EXPECT_EQ(lineExtents(*book).size(), 3u);
            EXPECT_EQ(book->pageCount(), 2u);
        }

        TEST_F(JournalTypographyTest, LeftCentreAndRightUseTheRequestedMargins)
        {
            for (const auto [alignment, left] : {
                     std::make_pair(BookTypesetter::AlignLeft, 0),
                     std::make_pair(BookTypesetter::AlignCenter, 18),
                     std::make_pair(BookTypesetter::AlignRight, 36),
                 })
            {
                const auto book = typeset("aaa", alignment);
                const auto lines = lineExtents(*book);
                ASSERT_EQ(lines.size(), 1u);
                EXPECT_EQ(lines.begin()->second, std::make_pair(left, left + 24));
            }
        }

        TEST_F(JournalTypographyTest, JustificationFillsWrappedLineAndLeavesLastLineNatural)
        {
            const auto book = typeset("aaa bbb ccc", BookTypesetter::AlignJustify);
            const auto lines = lineExtents(*book);
            ASSERT_EQ(lines.size(), 2u);
            EXPECT_EQ(lines.at(0), std::make_pair(0, 60));
            EXPECT_EQ(lines.at(16), std::make_pair(0, 24));
        }

        TEST_F(JournalTypographyTest, PageBoundaryDoesNotEndAJustifiedParagraph)
        {
            const auto book = typeset("aaa bbb ccc ddd eee", BookTypesetter::AlignJustify, 60, 32);
            const auto lines = lineExtents(*book);
            ASSERT_EQ(lines.size(), 3u);
            EXPECT_EQ(lines.at(0), std::make_pair(0, 60));
            EXPECT_EQ(lines.at(16), std::make_pair(0, 60));
            EXPECT_EQ(lines.at(32), std::make_pair(0, 24));
            EXPECT_EQ(book->pageCount(), 2u);
        }

        TEST_F(JournalTypographyTest, ExplicitNewlineEndsParagraphWithoutStretching)
        {
            const auto book = typeset("aaa bbb\nccc ddd", BookTypesetter::AlignJustify);
            const auto lines = lineExtents(*book);
            ASSERT_EQ(lines.size(), 2u);
            EXPECT_EQ(lines.at(0), std::make_pair(0, 52));
            EXPECT_EQ(lines.at(16), std::make_pair(0, 52));
        }

        TEST_F(JournalTypographyTest, CrlfAndLfHaveIdenticalAlignmentAndPageCount)
        {
            for (const auto alignment : { BookTypesetter::AlignLeft, BookTypesetter::AlignCenter,
                     BookTypesetter::AlignRight, BookTypesetter::AlignJustify })
            {
                const auto lf = typeset("aaa bbb\nccc ddd", alignment, 60, 32);
                const auto crlf = typeset("aaa bbb\r\nccc ddd", alignment, 60, 32);
                EXPECT_EQ(lineExtents(*crlf), lineExtents(*lf));
                EXPECT_EQ(crlf->getSize(), lf->getSize());
                EXPECT_EQ(crlf->pageCount(), lf->pageCount());
            }
        }

        TEST_F(JournalTypographyTest, InvisibleCarriageReturnCannotWrapAnExactlyFittingLine)
        {
            // The font deliberately supplies nonzero fallback metrics for CR,
            // as a real font can when its configured glyph ranges omit U+000D.
            const auto lf = typeset("aaa bbb\nccc ddd", BookTypesetter::AlignJustify, 52, 32);
            const auto crlf = typeset("aaa bbb\r\nccc ddd", BookTypesetter::AlignJustify, 52, 32);
            EXPECT_EQ(lineExtents(*crlf), lineExtents(*lf));
            EXPECT_EQ(crlf->getSize(), std::make_pair(52u, 32u));
            EXPECT_EQ(crlf->pageCount(), 1u);
        }

        TEST_F(JournalTypographyTest, ConsecutiveCrlfBreaksKeepTheExistingLfParagraphSpacing)
        {
            for (const auto alignment : { BookTypesetter::AlignLeft, BookTypesetter::AlignCenter,
                     BookTypesetter::AlignRight, BookTypesetter::AlignJustify })
            {
                const auto lf = typeset("aaa bbb\n\nccc ddd", alignment, 60, 32);
                const auto crlf = typeset("aaa bbb\r\n\r\nccc ddd", alignment, 60, 32);
                EXPECT_EQ(lineExtents(*crlf), lineExtents(*lf));
                EXPECT_EQ(crlf->getSize(), lf->getSize());
                EXPECT_EQ(crlf->pageCount(), 1u);
            }
        }

        TEST_F(JournalTypographyTest, TrailingSpacesBeforeCrlfDoNotJustifyTheLastParagraphLine)
        {
            const auto lf = typeset("aaa bbb    \nccc ddd", BookTypesetter::AlignJustify, 60, 32);
            const auto crlf = typeset("aaa bbb    \r\nccc ddd", BookTypesetter::AlignJustify, 60, 32);
            const auto lines = lineExtents(*crlf);
            EXPECT_EQ(lines, lineExtents(*lf));
            ASSERT_EQ(lines.size(), 2u);
            EXPECT_EQ(lines.at(0), std::make_pair(0, 52));
            EXPECT_EQ(lines.at(16), std::make_pair(0, 52));
            EXPECT_EQ(crlf->getSize(), lf->getSize());
            EXPECT_EQ(crlf->pageCount(), 1u);
        }

        TEST_F(JournalTypographyTest, SpaceOnlyCrlfLinesKeepTheExistingLfSpacingAndPagination)
        {
            for (const auto alignment : { BookTypesetter::AlignLeft, BookTypesetter::AlignCenter,
                     BookTypesetter::AlignRight, BookTypesetter::AlignJustify })
            {
                const auto lf = typeset("aaa bbb\n    \nccc ddd", alignment, 60, 32);
                const auto crlf = typeset("aaa bbb\r\n    \r\nccc ddd", alignment, 60, 32);
                EXPECT_EQ(lineExtents(*crlf), lineExtents(*lf));
                EXPECT_EQ(crlf->getSize(), lf->getSize());
                EXPECT_EQ(crlf->pageCount(), lf->pageCount());
                EXPECT_EQ(crlf->pageCount(), 2u);
            }
        }

        TEST_F(JournalTypographyTest, ExplicitLineBreakAlsoEndsParagraphWithoutStretching)
        {
            auto typesetter = BookTypesetter::create(60, 320, 16);
            typesetter->setSectionAlignment(BookTypesetter::AlignJustify);
            auto* textStyle = style(typesetter);
            typesetter->write(textStyle, "aaa bbb");
            typesetter->lineBreak();
            typesetter->write(textStyle, "ccc ddd");
            const auto lines = lineExtents(*typesetter->complete());
            EXPECT_EQ(lines.at(0), std::make_pair(0, 52));
            EXPECT_EQ(lines.at(16), std::make_pair(0, 52));
        }

        TEST_F(JournalTypographyTest, BodyAlignmentDoesNotMoveEarlierDateHeading)
        {
            auto typesetter = BookTypesetter::create(60, 320, 16);
            auto* textStyle = style(typesetter);
            typesetter->write(textStyle, "aaa");
            typesetter->lineBreak();
            typesetter->setLineAlignment(BookTypesetter::AlignRight);
            typesetter->write(textStyle, "bbb");
            const auto lines = lineExtents(*typesetter->complete());
            EXPECT_EQ(lines.at(0), std::make_pair(0, 24));
            EXPECT_EQ(lines.at(16), std::make_pair(36, 60));
        }

        TEST_F(JournalTypographyTest, StyledLinkMovesWithItsJustifiedWordsAndHitArea)
        {
            auto typesetter = BookTypesetter::create(60, 320, 16);
            typesetter->setSectionAlignment(BookTypesetter::AlignJustify);
            auto* body = style(typesetter);
            auto* link = typesetter->createHotStyle(body, MyGUI::Colour::Blue, MyGUI::Colour::Red,
                MyGUI::Colour::Green, 42);
            typesetter->write(body, "aaa ");
            typesetter->write(link, "bbb");
            typesetter->write(body, " ccc");
            const auto book = typesetter->complete();
            EXPECT_EQ(book->getLinkAt(35, 8), 0);
            EXPECT_EQ(book->getLinkAt(36, 8), 42);
            EXPECT_EQ(book->getLinkAt(59, 8), 42);
            EXPECT_EQ(book->getLinkAt(60, 8), 0);
            EXPECT_EQ(book->getLinkAt(36, 16), 0);
            for (const auto& run : book->getTextRuns())
            {
                if (run.mInteractiveId == 42)
                {
                    EXPECT_EQ(run.mText, "bbb");
                    EXPECT_EQ(run.mLeft, 36);
                    EXPECT_EQ(run.mRight, 60);
                }
            }
        }

        TEST_F(JournalTypographyTest, MultiwordLinkRemainsClickableAcrossExpandedSpace)
        {
            auto typesetter = BookTypesetter::create(60, 320, 16);
            typesetter->setSectionAlignment(BookTypesetter::AlignJustify);
            auto* body = style(typesetter);
            auto* link = typesetter->createHotStyle(body, MyGUI::Colour::Blue, MyGUI::Colour::Red,
                MyGUI::Colour::Green, 42);
            typesetter->write(link, "aaa bbb");
            typesetter->write(body, " ccc");
            const auto book = typesetter->complete();
            EXPECT_EQ(book->getLinkAt(1, 8), 42);
            EXPECT_EQ(book->getLinkAt(30, 8), 42);
            EXPECT_EQ(book->getLinkAt(59, 8), 42);
        }

        TEST_F(JournalTypographyTest, NonbreakingSpaceKeepsItsWordTogetherAndUnstretched)
        {
            const auto book = typeset("aa\xC2\xA0" "bb cc dddd", BookTypesetter::AlignJustify, 84);
            const auto runs = book->getTextRuns();
            auto joinedWord = std::find_if(runs.begin(), runs.end(), [](const auto& run) {
                return run.mText == "aa\xC2\xA0" "bb";
            });
            ASSERT_NE(joinedWord, runs.end());
            EXPECT_EQ(joinedWord->mRight - joinedWord->mLeft, 40);
            EXPECT_EQ(joinedWord->mTop, 0);
            EXPECT_EQ(lineExtents(*book).at(0), std::make_pair(0, 84));
        }

        TEST_F(JournalTypographyTest, Utf8CharactersAreMeasuredAsGlyphsRatherThanBytes)
        {
            const auto book = typeset("\xC3\xA9\xC3\xA9 \xD0\x90\xD0\x91 cc", BookTypesetter::AlignJustify, 44);
            const auto lines = lineExtents(*book);
            ASSERT_EQ(lines.size(), 2u);
            EXPECT_EQ(lines.at(0), std::make_pair(0, 44));
            EXPECT_EQ(lines.at(16), std::make_pair(0, 16));
        }

        TEST_F(JournalTypographyTest, LongUnbrokenWordIsNeverCompressedOrStretched)
        {
            const auto book = typeset("abcdefghij a", BookTypesetter::AlignJustify, 40);
            const auto lines = lineExtents(*book);
            ASSERT_EQ(lines.size(), 2u);
            EXPECT_EQ(lines.at(0), std::make_pair(0, 80));
            EXPECT_EQ(lines.at(16), std::make_pair(0, 8));
        }

        TEST_F(JournalTypographyTest, JustificationDistributesRemainderAcrossAllWordGaps)
        {
            const auto book = typeset("aa bb cc dddd", BookTypesetter::AlignJustify, 69);
            const auto runs = book->getTextRuns();
            for (const auto& run : runs)
            {
                if (run.mTop == 0 && (run.mText == "aa" || run.mText == "bb" || run.mText == "cc"))
                    EXPECT_EQ(run.mRight - run.mLeft, 16);
            }
            EXPECT_EQ(lineExtents(*book).at(0), std::make_pair(0, 69));
            EXPECT_EQ(lineExtents(*book).at(16), std::make_pair(0, 32));
        }
    }
}
