#include "dialogueformatting.hpp"

#include "textcolours.hpp"

#include <components/settings/values.hpp>

#include <algorithm>

namespace MWGui
{
    DialoguePalette getDialoguePalette(const TextColours& colours, bool message)
    {
        const auto& settings = Settings::gui();
        const bool enabled = settings.mDialogueColouring;
        const MyGUI::Colour normal = message ? colours.notify
            : enabled && !settings.mDialogueUseSystemColour ? settings.mColorDialogueNormal.get() : colours.normal;
        DialoguePalette palette;
        palette.mText.fill(normal);
        palette.mLink = { colours.link, colours.linkOver, colours.linkPressed };
        if (enabled)
        {
            using Misc::DialogueText::Category;
            palette.mText[static_cast<size_t>(Category::Inventory)] = settings.mColorDialogueInventory;
            palette.mText[static_cast<size_t>(Category::Stat)] = settings.mColorDialogueStat;
            palette.mText[static_cast<size_t>(Category::Narration)] = settings.mColorDialogueNarration;
            palette.mText[static_cast<size_t>(Category::Journal)] = settings.mColorDialogueJournal;
        }
        return palette;
    }

    DialoguePalette getJournalDialoguePalette(const TextColours& colours)
    {
        DialoguePalette palette;
        palette.mText.fill(MyGUI::Colour::Black);
        palette.mLink = { colours.journalLink, colours.journalLinkOver, colours.journalLinkPressed };
        const auto& settings = Settings::gui();
        if (settings.mDialogueColouring && settings.mJournalColourNarration)
            palette.mText[static_cast<size_t>(Misc::DialogueText::Category::Narration)]
                = settings.mColorJournalNarration;
        return palette;
    }

    void writeDialogueText(BookTypesetter& typesetter, std::string_view text,
        std::span<const Misc::DialogueText::Span> spans, std::span<const DialogueTextLink> links,
        const DialoguePalette& palette, bool useBookFont)
    {
        using Misc::DialogueText::Category;
        typesetter.addContent(text);
        std::array<BookTypesetter::Style*, 5> styles{};
        const auto styleFor = [&](Category category) {
            const size_t index = static_cast<size_t>(category);
            if (!styles[index])
                styles[index] = typesetter.createStyle({}, palette.mText[index], useBookFont);
            return styles[index];
        };

        size_t spanIndex = 0;
        const auto writePlain = [&](size_t begin, size_t end) {
            while (begin < end)
            {
                while (spanIndex < spans.size() && spans[spanIndex].mEnd <= begin)
                    ++spanIndex;
                if (spanIndex == spans.size() || spans[spanIndex].mBegin >= end)
                {
                    typesetter.write(styleFor(Category::Default), begin, end);
                    break;
                }
                const auto& span = spans[spanIndex];
                if (begin < span.mBegin)
                {
                    typesetter.write(styleFor(Category::Default), begin, span.mBegin);
                    begin = span.mBegin;
                }
                const size_t stop = std::min(end, span.mEnd);
                typesetter.write(styleFor(span.mCategory), begin, stop);
                begin = stop;
            }
        };

        size_t begin = 0;
        for (const auto& link : links)
        {
            if (link.mBegin < begin || link.mEnd < link.mBegin || link.mEnd > text.size())
                continue;
            writePlain(begin, link.mBegin);
            auto* style = typesetter.createHotStyle(styleFor(Category::Default), palette.mLink[0], palette.mLink[1],
                palette.mLink[2], link.mId);
            typesetter.write(style, link.mBegin, link.mEnd);
            begin = link.mEnd;
        }
        writePlain(begin, text.size());
    }
}
