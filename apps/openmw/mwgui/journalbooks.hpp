#ifndef MWGUI_JOURNALBOOKS_HPP
#define MWGUI_JOURNALBOOKS_HPP

#include "bookpage.hpp"
#include "journalviewmodel.hpp"

#include <components/toutf8/toutf8.hpp>

namespace MWGui
{
    struct TextColours;

    int getJournalFontSize();
    int getCyrillicIndexPageCount();

    const MyGUI::Colour journalHeaderColour = MyGUI::Colour(0.60f, 0.00f, 0.00f);

    struct JournalBooks
    {
        std::shared_ptr<JournalViewModel> mModel;

        JournalBooks(std::shared_ptr<JournalViewModel> model, ToUTF8::FromType encoding);

        std::shared_ptr<TypesetBook> createEmptyJournalBook();
        std::shared_ptr<TypesetBook> createJournalBook();
        std::shared_ptr<TypesetBook> createTopicBook(const MWDialogue::Topic& topic);
        std::shared_ptr<TypesetBook> createQuestBook(std::string_view questName);
        std::shared_ptr<TypesetBook> createTopicIndexBook(int columnWidth = 0);
        std::shared_ptr<TypesetBook> createTopicIndexBook(const TextColours& textColours, int columnWidth = 0);

        ToUTF8::FromType mEncoding;
        int mIndexPagesCount;

    private:
        std::shared_ptr<BookTypesetter> createTypesetter();
        std::shared_ptr<BookTypesetter> createJournalIndex(
            bool cyrillic, const TextColours& textColours, int columnWidth);
    };
}

#endif // MWGUI_JOURNALBOOKS_HPP
