#ifndef OPENMW_MWGUI_DIALOGUEFORMATTING_H
#define OPENMW_MWGUI_DIALOGUEFORMATTING_H

#include "bookpage.hpp"

#include <components/misc/dialoguetext.hpp>

#include <array>
#include <span>
#include <string_view>

namespace MWGui
{
    struct TextColours;

    struct DialogueTextLink
    {
        size_t mBegin;
        size_t mEnd;
        TypesetBook::InteractiveId mId;
    };

    struct DialoguePalette
    {
        // Indexed by Misc::DialogueText::Category. Link colours remain independent
        // of prose classification, including hover, press and controller focus.
        std::array<MyGUI::Colour, 5> mText;
        std::array<MyGUI::Colour, 3> mLink;
    };

    DialoguePalette getDialoguePalette(const TextColours& colours, bool message = false);
    DialoguePalette getJournalDialoguePalette(const TextColours& colours);

    // Text is the already expanded display text. Both kinds of span refer to its
    // UTF-8 byte offsets. Links are ordered, non-overlapping, and always take
    // precedence over semantic colours; no text, topic IDs or whitespace changes.
    void writeDialogueText(BookTypesetter& typesetter, std::string_view text,
        std::span<const Misc::DialogueText::Span> spans, std::span<const DialogueTextLink> links,
        const DialoguePalette& palette, bool useBookFont = false);
}

#endif
