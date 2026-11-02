#include "apps/openmw/mwgui/bookpage.hpp"
#include "apps/openmw/mwgui/journalbooks.hpp"
#include "apps/openmw/mwgui/journaltypography.hpp"
#include "apps/openmw/mwgui/textcolours.hpp"

#include <components/settings/values.hpp>

#include <MyGUI_FontManager.h>
#include <MyGUI_ResourceManager.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <map>
#include <set>
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
            const int mOldPageNumberFontSize = Settings::gui().mJournalPageNumberFontSize.get();
            const int mOldButtonScale = Settings::gui().mJournalButtonScale.get();
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
                Settings::gui().mJournalPageNumberFontSize.set(mOldPageNumberFontSize);
                Settings::gui().mJournalButtonScale.set(mOldButtonScale);
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

            static void expectCompleteIndex(const TypesetBook& book, int fontSize, int columns,
                const std::set<TypesetBook::InteractiveId>& expectedLetters)
            {
                std::set<TypesetBook::InteractiveId> actualLetters;
                std::map<TypesetBook::InteractiveId, std::set<int>> letterLines;
                for (const auto& run : book.getTextRuns())
                {
                    EXPECT_EQ(run.mBottom - run.mTop, fontSize);
                    EXPECT_GE(run.mLeft, 0);
                    EXPECT_LE(run.mRight, columns == 3 ? 67 : 92);
                    EXPECT_NE(run.mInteractiveId, 0);
                    actualLetters.insert(run.mInteractiveId);
                    letterLines[run.mInteractiveId].insert(run.mTop);
                    if (run.mRight > run.mLeft)
                        EXPECT_EQ(book.getLinkAt((run.mLeft + run.mRight) / 2, (run.mTop + run.mBottom) / 2),
                            run.mInteractiveId);
                    EXPECT_EQ(book.getLinkAt(-1, (run.mTop + run.mBottom) / 2), 0);
                }
                EXPECT_EQ(actualLetters, expectedLetters);
                EXPECT_EQ(book.pageCount(), static_cast<std::size_t>(columns));

                const int rows = (static_cast<int>(expectedLetters.size()) + columns - 1) / columns;
                int index = 0;
                for (const auto letter : expectedLetters)
                {
                    ASSERT_EQ(letterLines[letter].size(), 1u);
                    const int top = *letterLines[letter].begin();
                    // Each displayed column is a page in the typeset document.
                    // Every letter must occupy one visible row, including the last.
                    EXPECT_EQ(top, index * fontSize);
                    const int columnTop = (index / rows) * rows * fontSize;
                    EXPECT_LE(top - columnTop + fontSize, 260);
                    ++index;
                }
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

        TEST_F(JournalTypographyTest, LatinIndexUsesJournalSizeAndKeepsEveryLetterAccessible)
        {
            std::set<TypesetBook::InteractiveId> letters;
            for (char letter = 'A'; letter <= 'Z'; ++letter)
                letters.insert(letter);
            Settings::gui().mFontSize.set(18);
            const TextColours colours;
            for (int size = 0; size <= 24; ++size)
            {
                if (size > 0 && size < 10)
                    continue;
                SCOPED_TRACE(size);
                const int columns = size <= 20 ? 2 : 3;
                Settings::gui().mJournalFontSize.set(size);
                JournalBooks journal(nullptr, ToUTF8::WINDOWS_1252);
                const auto book = journal.createTopicIndexBook(colours);
                EXPECT_EQ(journal.mIndexPagesCount, columns);
                expectCompleteIndex(*book, size == 0 ? 18 : size, columns, letters);
            }
        }

        TEST_F(JournalTypographyTest, CyrillicIndexUsesJournalSizeAndKeepsEveryEligibleLetterAccessible)
        {
            std::set<TypesetBook::InteractiveId> letters;
            for (int letter = 0x0410; letter <= 0x042f; ++letter)
                if (letter != 0x042a && letter != 0x042c) // Hard and soft signs cannot start a topic.
                    letters.insert(letter);
            Settings::gui().mFontSize.set(18);
            const TextColours colours;
            for (int size = 0; size <= 24; ++size)
            {
                if (size > 0 && size < 10)
                    continue;
                SCOPED_TRACE(size);
                const int columns = size != 0 && size < 18 ? 2 : 3;
                Settings::gui().mJournalFontSize.set(size);
                JournalBooks journal(nullptr, ToUTF8::WINDOWS_1251);
                const auto book = journal.createTopicIndexBook(colours);
                EXPECT_EQ(journal.mIndexPagesCount, columns);
                expectCompleteIndex(*book, size == 0 ? 18 : size, columns, letters);
            }
        }

        TEST_F(JournalTypographyTest, IndexSizeAndAlignmentStayIndependentOfPageNumbersAndButtons)
        {
            Settings::gui().mJournalFontSize.set(12);
            Settings::gui().mJournalTextAlignment.set(0);
            Settings::gui().mJournalPageNumberFontSize.set(0);
            Settings::gui().mJournalButtonScale.set(100);
            const TextColours colours;
            JournalBooks journal(nullptr, ToUTF8::WINDOWS_1252);
            const auto original = journal.createTopicIndexBook(colours);

            Settings::gui().mJournalTextAlignment.set(3);
            Settings::gui().mJournalPageNumberFontSize.set(24);
            Settings::gui().mJournalButtonScale.set(50);
            const auto changedControls = journal.createTopicIndexBook(colours);
            EXPECT_EQ(lineExtents(*changedControls), lineExtents(*original));
            EXPECT_EQ(changedControls->pageCount(), original->pageCount());
            EXPECT_EQ(Settings::gui().mFontSize.get(), 16);
            const auto ordinary = typeset("abc", BookTypesetter::AlignLeft, 100, 100, 0);
            EXPECT_EQ(ordinary->getSize(), std::make_pair(24u, 16u));
        }

        TEST_F(JournalTypographyTest, IndexNavigationCoversWholeColumnsAndClampsTheShortLastColumn)
        {
            for (const auto [count, rows] : { std::make_pair(26u, 13u), std::make_pair(26u, 9u),
                     std::make_pair(30u, 15u), std::make_pair(30u, 10u) })
            {
                SCOPED_TRACE(::testing::Message() << "count=" << count << ", rows=" << rows);
                for (std::size_t start = 0; start < count; start += rows)
                {
                    std::set<std::size_t> visited;
                    std::size_t selected = start;
                    const std::size_t columnSize = std::min<std::size_t>(rows, count - start);
                    for (std::size_t step = 0; step < columnSize; ++step)
                    {
                        EXPECT_GE(selected, start);
                        EXPECT_LT(selected, start + columnSize);
                        visited.insert(selected);
                        const auto next = moveJournalIndexSelection(selected, 1, count, rows);
                        EXPECT_EQ(moveJournalIndexSelection(next, -1, count, rows), selected);
                        selected = next;
                    }
                    EXPECT_EQ(visited.size(), columnSize);
                    EXPECT_EQ(selected, start);
                }
                for (std::size_t selected = 0; selected < count; ++selected)
                {
                    EXPECT_LT(moveJournalIndexSelection(selected, static_cast<int>(rows), count, rows), count);
                    EXPECT_LT(moveJournalIndexSelection(selected, -static_cast<int>(rows), count, rows), count);
                }
            }
            EXPECT_EQ(moveJournalIndexSelection(17, 9, 26, 9), 25u); // R -> Z: final column has no ninth row.
            EXPECT_EQ(moveJournalIndexSelection(25, 1, 26, 9), 18u); // Z -> S within the last column.
            EXPECT_EQ(moveJournalIndexSelection(18, -1, 26, 9), 25u);
            EXPECT_EQ(moveJournalIndexSelection(25, 9, 26, 9), 7u); // Z -> H on the first column.
            EXPECT_EQ(moveJournalIndexSelection(0, -9, 26, 9), 18u); // A -> S on the last column.
            for (std::size_t index = 0; index < 26; ++index)
                EXPECT_EQ(getJournalIndexCharacter(index, false), 'A' + index);
            EXPECT_EQ(getJournalIndexCharacter(25, true), 0x0429u);
            EXPECT_EQ(getJournalIndexCharacter(26, true), 0x042bu);
            EXPECT_EQ(getJournalIndexCharacter(27, true), 0x042du);
            EXPECT_EQ(getJournalIndexCharacter(29, true), 0x042fu);
        }

        TEST_F(JournalTypographyTest, ButtonScalingRetainsTheOriginalCentreAndCanRestoreItsSize)
        {
            // Include the cropped English Next button's odd 57-pixel width.
            for (const auto original : { MyGUI::IntCoord(40, 350, 64, 32), MyGUI::IntCoord(300, 350, 57, 32),
                     MyGUI::IntCoord(83, 15, 72, 32) })
            {
                for (int percent : { 50, 73, 100 })
                {
                    const auto scaled = scaleJournalButton(original, percent);
                    EXPECT_GE(scaled.left, original.left);
                    EXPECT_GE(scaled.top, original.top);
                    EXPECT_LE(scaled.right(), original.right());
                    EXPECT_LE(scaled.bottom(), original.bottom());
                    EXPECT_NEAR(2 * scaled.left + scaled.width, 2 * original.left + original.width, 1);
                    EXPECT_NEAR(2 * scaled.top + scaled.height, 2 * original.top + original.height, 1);
                    EXPECT_NEAR(scaled.width, original.width * percent / 100.0, 0.5);
                    EXPECT_NEAR(scaled.height, original.height * percent / 100.0, 0.5);
                }
                EXPECT_EQ(scaleJournalButton(original, 100), original);
            }
        }

        TEST_F(JournalTypographyTest, ThreeColumnIndexUsesSeparateHitAreasWithinTheOriginalSpan)
        {
            Settings::gui().mJournalFontSize.set(24);
            const TextColours colours;
            for (const auto original : {
                     std::array{ MyGUI::IntCoord(20, 10, 92, 260), MyGUI::IntCoord(75, 10, 92, 260),
                         MyGUI::IntCoord(130, 10, 92, 260) },
                     std::array{ MyGUI::IntCoord(8, 14, 100, 230), MyGUI::IntCoord(96, 14, 100, 230),
                         MyGUI::IntCoord(184, 14, 100, 230) },
                 })
            {
                EXPECT_EQ(layoutJournalIndexColumns(original, 2), original);
                const auto columns = layoutJournalIndexColumns(original, 3);
                EXPECT_EQ(columns.front().left, original.front().left);
                EXPECT_LE(columns.back().right(), original.back().right());
                EXPECT_LT(original.back().right() - columns.back().right(), static_cast<int>(columns.size()));
                for (std::size_t i = 0; i < columns.size(); ++i)
                {
                    EXPECT_GT(columns[i].width, 0);
                    EXPECT_EQ(columns[i].top, original[i].top);
                    EXPECT_EQ(columns[i].height, original[i].height);
                    if (i + 1 < columns.size())
                        EXPECT_EQ(columns[i].right(), columns[i + 1].left);
                }
                JournalBooks journal(nullptr, ToUTF8::WINDOWS_1252);
                const auto book = journal.createTopicIndexBook(colours, columns.front().width);
                EXPECT_EQ(book->pageCount(), 3u);
                for (const auto& line : lineExtents(*book))
                {
                    const auto& extent = line.second;
                    EXPECT_GE(extent.first, 0);
                    EXPECT_LE(extent.second, columns.front().width);
                    EXPECT_NEAR(extent.first + extent.second, columns.front().width, 1);
                }
            }
        }

        TEST_F(JournalTypographyTest, PageNumberBoundsFitLargeMultiDigitTextAndRestoreTheLayoutAnchor)
        {
            const MyGUI::IntCoord original(150, 350, 32, 16);
            const auto large = resizeJournalPageNumber(original, MyGUI::IntSize(60, 24));
            EXPECT_EQ(large.left, 135);
            EXPECT_EQ(large.top, original.top);
            EXPECT_GE(large.width, 62); // Four large digits plus right-side ink clearance.
            EXPECT_GE(large.height, 24);
            EXPECT_LT(large.right(), 205); // Native Previous button begins at 205.
            const auto rightPage = resizeJournalPageNumber(MyGUI::IntCoord(410, 350, 32, 16), MyGUI::IntSize(60, 24));
            EXPECT_LT(rightPage.right(), 460); // Native Close button begins at 460.
            const auto restored = resizeJournalPageNumber(original, MyGUI::IntSize(8, 16));
            EXPECT_EQ(restored, original);
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
