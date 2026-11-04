#ifndef OPENMW_COMPONENTS_MISC_DIALOGUETEXT_H
#define OPENMW_COMPONENTS_MISC_DIALOGUETEXT_H

#include <cstddef>
#include <string_view>
#include <vector>

namespace Misc::DialogueText
{
    enum class Category
    {
        Default,
        Inventory,
        Stat,
        Narration,
        Journal,
    };

    enum class Context
    {
        Dialogue,
        Journal,
    };

    struct Span
    {
        std::size_t mBegin;
        std::size_t mEnd;
        Category mCategory;

        bool operator==(const Span&) const = default;
    };

    /// Recognise conservative English notices and stage directions in displayed plain text.
    /// The returned spans cover the original bytes exactly once; the text is never rewritten.
    /// Resolve dialogue hypertext first and keep interactive links in their own existing styles.
    /// Engine-origin categories and localised message templates should take precedence over these heuristics.
    std::vector<Span> classify(std::string_view text, Context context, bool detectNarration = true);

    /// Match a complete localised printf template, including %% and string/integer/float fields.
    /// There is no regex evaluation or recursive backtracking. Empty/placeholder-only, unsupported,
    /// or excessively large templates are rejected rather than matching arbitrary messages.
    bool matchesFormat(std::string_view text, std::string_view format);
}

#endif
