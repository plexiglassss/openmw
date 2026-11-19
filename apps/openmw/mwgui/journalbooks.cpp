#include "journalbooks.hpp"

#include "dialogueformatting.hpp"
#include "journaltypography.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"

#include <components/misc/utf8stream.hpp>
#include <components/settings/values.hpp>

#include "textcolours.hpp"

namespace
{
    MWGui::BookTypesetter::Alignment getJournalTextAlignment()
    {
        switch (Settings::gui().mJournalTextAlignment)
        {
            case 1:
                return MWGui::BookTypesetter::AlignCenter;
            case 2:
                return MWGui::BookTypesetter::AlignRight;
            case 3:
                return MWGui::BookTypesetter::AlignJustify;
            default:
                return MWGui::BookTypesetter::AlignLeft;
        }
    }

    struct AddContent
    {
        std::shared_ptr<MWGui::BookTypesetter> mTypesetter;
        MWGui::BookTypesetter::Style* mBodyStyle;

        explicit AddContent(std::shared_ptr<MWGui::BookTypesetter> typesetter, MWGui::BookTypesetter::Style* bodyStyle)
            : mTypesetter(std::move(typesetter))
            , mBodyStyle(bodyStyle)
        {
        }
    };

    struct AddEntry
    {
        std::shared_ptr<MWGui::BookTypesetter> mTypesetter;
        MWGui::BookTypesetter::Style* mBodyStyle;
        Misc::DialogueText::Context mContext;

        AddEntry(std::shared_ptr<MWGui::BookTypesetter> typesetter, MWGui::BookTypesetter::Style* bodyStyle,
            Misc::DialogueText::Context context = Misc::DialogueText::Context::Journal)
            : mTypesetter(std::move(typesetter))
            , mBodyStyle(bodyStyle)
            , mContext(context)
        {
        }

        void operator()(MWGui::JournalViewModel::Entry const& entry)
        {
            const std::string_view text = entry.body();
            std::vector<MWGui::DialogueTextLink> links;
            entry.visitSpans([&links](const MWDialogue::Topic* topic, size_t begin, size_t end) {
                if (topic)
                    links.push_back({ begin, end, reinterpret_cast<MWGui::TypesetBook::InteractiveId>(topic) });
            });
            const auto& settings = Settings::gui();
            const auto spans = settings.mDialogueColouring && settings.mJournalColourNarration
                ? Misc::DialogueText::classify(text, mContext)
                : std::vector<Misc::DialogueText::Span>{};
            const auto& colours = MWBase::Environment::get().getWindowManager()->getTextColours();
            MWGui::writeDialogueText(*mTypesetter, text, spans, links,
                MWGui::getJournalDialoguePalette(colours), true);
        }
    };

    struct AddJournalEntry : AddEntry
    {
        bool mAddHeader;
        MWGui::BookTypesetter::Style* mHeaderStyle;

        explicit AddJournalEntry(std::shared_ptr<MWGui::BookTypesetter> typesetter,
            MWGui::BookTypesetter::Style* bodyStyle, MWGui::BookTypesetter::Style* headerStyle, bool addHeader)
            : AddEntry(std::move(typesetter), bodyStyle)
            , mAddHeader(addHeader)
            , mHeaderStyle(headerStyle)
        {
        }

        void operator()(MWGui::JournalViewModel::JournalEntry const& entry)
        {
            if (mAddHeader)
            {
                mTypesetter->setLineAlignment(MWGui::BookTypesetter::AlignLeft);
                mTypesetter->write(mHeaderStyle, entry.timestamp());
                mTypesetter->lineBreak();
            }

            mTypesetter->setLineAlignment(getJournalTextAlignment());
            AddEntry::operator()(entry);

            mTypesetter->sectionBreak(30);
        }
    };

    struct AddTopicEntry : AddEntry
    {
        const MWGui::TypesetBook::Content* mContentId;
        MWGui::BookTypesetter::Style* mHeaderStyle;

        explicit AddTopicEntry(std::shared_ptr<MWGui::BookTypesetter> typesetter,
            MWGui::BookTypesetter::Style* bodyStyle, MWGui::BookTypesetter::Style* headerStyle,
            const MWGui::TypesetBook::Content* contentId)
            : AddEntry(std::move(typesetter), bodyStyle, Misc::DialogueText::Context::Dialogue)
            , mContentId(contentId)
            , mHeaderStyle(headerStyle)
        {
        }

        void operator()(MWGui::JournalViewModel::TopicEntry const& entry)
        {
            mTypesetter->setLineAlignment(getJournalTextAlignment());
            mTypesetter->write(mBodyStyle, entry.source());
            mTypesetter->write(mBodyStyle, 0, 3); // begin

            AddEntry::operator()(entry);

            mTypesetter->selectContent(mContentId);
            mTypesetter->write(mBodyStyle, 2, 3); // end quote

            mTypesetter->sectionBreak(30);
        }
    };

    struct AddTopicName : AddContent
    {
        AddTopicName(std::shared_ptr<MWGui::BookTypesetter> typesetter, MWGui::BookTypesetter::Style* style)
            : AddContent(std::move(typesetter), style)
        {
        }

        void operator()(std::string_view topicName)
        {
            mTypesetter->write(mBodyStyle, topicName);
            mTypesetter->sectionBreak();
        }
    };

    struct AddQuestName : AddContent
    {
        AddQuestName(std::shared_ptr<MWGui::BookTypesetter> typesetter, MWGui::BookTypesetter::Style* style)
            : AddContent(std::move(typesetter), style)
        {
        }

        void operator()(std::string_view topicName)
        {
            mTypesetter->write(mBodyStyle, topicName);
            mTypesetter->sectionBreak();
        }
    };
}

namespace MWGui
{

    int getJournalFontSize()
    {
        const int size = Settings::gui().mJournalFontSize;
        return size > 0 ? size : static_cast<int>(Settings::gui().mFontSize);
    }

    int getCyrillicIndexPageCount()
    {
        return getJournalIndexColumnCount(30, getJournalFontSize());
    }

    JournalBooks::JournalBooks(std::shared_ptr<JournalViewModel> model, ToUTF8::FromType encoding)
        : mModel(std::move(model))
        , mEncoding(encoding)
        , mIndexPagesCount(0)
    {
    }

    std::shared_ptr<TypesetBook> JournalBooks::createEmptyJournalBook()
    {
        std::shared_ptr<BookTypesetter> typesetter = createTypesetter();

        BookTypesetter::Style* header = typesetter->createStyle({}, journalHeaderColour);
        BookTypesetter::Style* body = typesetter->createStyle({}, MyGUI::Colour::Black);

        typesetter->write(header, "You have no journal entries!");
        typesetter->lineBreak();
        typesetter->setLineAlignment(getJournalTextAlignment());
        typesetter->write(body, "You should have gone though the starting quest and got an initial quest.");

        return typesetter->complete();
    }

    std::shared_ptr<TypesetBook> JournalBooks::createJournalBook()
    {
        std::shared_ptr<BookTypesetter> typesetter = createTypesetter();

        BookTypesetter::Style* header = typesetter->createStyle({}, journalHeaderColour);
        BookTypesetter::Style* body = typesetter->createStyle({}, MyGUI::Colour::Black);

        mModel->visitJournalEntries({}, AddJournalEntry(typesetter, body, header, true));

        return typesetter->complete();
    }

    std::shared_ptr<TypesetBook> JournalBooks::createTopicBook(const MWDialogue::Topic& topic)
    {
        std::shared_ptr<BookTypesetter> typesetter = createTypesetter();

        BookTypesetter::Style* header = typesetter->createStyle({}, journalHeaderColour);
        BookTypesetter::Style* body = typesetter->createStyle({}, MyGUI::Colour::Black);

        mModel->visitTopicName(topic, AddTopicName(typesetter, header));

        const TypesetBook::Content* contentId = typesetter->addContent(", \"");

        mModel->visitTopicEntries(topic, AddTopicEntry(typesetter, body, header, contentId));

        return typesetter->complete();
    }

    std::shared_ptr<TypesetBook> JournalBooks::createQuestBook(std::string_view questName)
    {
        std::shared_ptr<BookTypesetter> typesetter = createTypesetter();

        BookTypesetter::Style* header = typesetter->createStyle({}, journalHeaderColour);
        BookTypesetter::Style* body = typesetter->createStyle({}, MyGUI::Colour::Black);

        AddQuestName addName(typesetter, header);
        addName(questName);

        mModel->visitJournalEntries(questName, AddJournalEntry(typesetter, body, header, false));

        return typesetter->complete();
    }

    std::shared_ptr<TypesetBook> JournalBooks::createTopicIndexBook(int columnWidth)
    {
        return createTopicIndexBook(MWBase::Environment::get().getWindowManager()->getTextColours(), columnWidth);
    }

    std::shared_ptr<TypesetBook> JournalBooks::createTopicIndexBook(const TextColours& textColours, int columnWidth)
    {
        return createJournalIndex(mEncoding == ToUTF8::WINDOWS_1251, textColours, columnWidth)->complete();
    }

    std::shared_ptr<BookTypesetter> JournalBooks::createJournalIndex(
        bool cyrillic, const TextColours& textColours, int columnWidth)
    {
        const int characterCount = cyrillic ? 30 : 26;
        const int fontSize = getJournalFontSize();
        mIndexPagesCount = getJournalIndexColumnCount(characterCount, fontSize);
        const int rows = getJournalIndexRowCount(characterCount, mIndexPagesCount);
        if (columnWidth <= 0)
            columnWidth = mIndexPagesCount == 3 ? 67 : 92;

        // Each typeset page is one index column. A precise column height keeps
        // small fonts from combining two sections on the same page, while the
        // third column keeps every letter visible at larger journal font sizes.
        std::shared_ptr<BookTypesetter> typesetter = BookTypesetter::create(columnWidth, rows * fontSize, fontSize);

        typesetter->setSectionAlignment(BookTypesetter::AlignCenter);

        BookTypesetter::Style* body = typesetter->createStyle({}, MyGUI::Colour::Black);
        std::string buffer;
        for (int i = 0; i < characterCount; ++i)
        {
            const Utf8Stream::UnicodeChar character = getJournalIndexCharacter(i, cyrillic);
            // Three columns use a compact, unbroken label. No unsupported
            // non-breaking-space glyph or smaller font is required to fit.
            buffer = mIndexPagesCount == 3 ? "(" : "( ";
            if (cyrillic)
            {
                buffer += static_cast<char>(0xc0 | (character >> 6));
                buffer += static_cast<char>(0x80 | (character & 0x3f));
            }
            else
                buffer += static_cast<char>(character);
            buffer += mIndexPagesCount == 3 ? ")" : " )";
            BookTypesetter::Style* style = typesetter->createHotStyle(
                body, textColours.journalTopic, textColours.journalTopicOver, textColours.journalTopicPressed, character);

            if (i > 0 && i % rows == 0)
                typesetter->sectionBreak();

            typesetter->write(style, buffer);
            typesetter->lineBreak();
        }

        return typesetter;
    }

    std::shared_ptr<BookTypesetter> JournalBooks::createTypesetter()
    {
        // TODO: determine page size from layout...
        // Leave two pixels inside the 240-pixel widget for glyph ink that extends
        // slightly past its advance, especially at the end of justified lines.
        return BookTypesetter::create(238, 320, getJournalFontSize());
    }

}
