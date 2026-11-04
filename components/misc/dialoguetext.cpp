#include "dialoguetext.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <string>

namespace Misc::DialogueText
{
    namespace
    {
        bool space(char ch)
        {
            return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
        }

        bool digit(char ch)
        {
            return ch >= '0' && ch <= '9';
        }

        bool word(char ch)
        {
            return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || digit(ch)
                || static_cast<unsigned char>(ch) >= 0x80 || ch == '_';
        }

        std::string_view trim(std::string_view text)
        {
            while (!text.empty() && space(text.front()))
                text.remove_prefix(1);
            while (!text.empty() && space(text.back()))
                text.remove_suffix(1);
            return text;
        }

        std::string lower(std::string_view text)
        {
            std::string result;
            result.reserve(text.size());
            for (char ch : text)
            {
                if (space(ch))
                {
                    if (!result.empty() && result.back() != ' ')
                        result.push_back(' ');
                }
                else
                    result.push_back(ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch);
            }
            if (!result.empty() && result.back() == ' ')
                result.pop_back();
            return result;
        }

        bool startsAny(std::string_view text, std::initializer_list<std::string_view> prefixes)
        {
            return std::any_of(prefixes.begin(), prefixes.end(),
                [text](std::string_view prefix) { return text.starts_with(prefix); });
        }

        bool hasWord(std::string_view text, std::string_view needle)
        {
            for (std::size_t at = text.find(needle); at != std::string_view::npos; at = text.find(needle, at + 1))
            {
                const std::size_t end = at + needle.size();
                if ((at == 0 || !word(text[at - 1])) && (end == text.size() || !word(text[end])))
                    return true;
            }
            return false;
        }

        bool hasAnyWord(std::string_view text, std::initializer_list<std::string_view> words)
        {
            return std::any_of(words.begin(), words.end(),
                [text](std::string_view candidate) { return hasWord(text, candidate); });
        }

        constexpr std::string_view sStatNames[]{ "strength", "intelligence", "willpower", "agility",
            "speed", "endurance", "personality", "luck", "health", "magicka", "fatigue", "stamina", "reputation",
            "disposition", "level", "acrobatics", "alchemy", "alteration", "armorer", "armourer", "athletics",
            "axe", "block", "blunt weapon", "conjuration", "destruction", "enchant", "hand-to-hand", "hand to hand",
            "heavy armor", "heavy armour", "illusion", "light armor", "light armour", "long blade", "marksman",
            "medium armor", "medium armour", "mercantile", "mysticism", "restoration", "security", "short blade",
            "sneak", "spear", "speechcraft", "unarmored", "unarmoured" };

        bool numericChange(std::string_view text)
        {
            bool hasNumber = false;
            for (std::size_t i = 0; i < text.size();)
            {
                if (digit(text[i]))
                {
                    hasNumber = true;
                    ++i;
                }
                else if (space(text[i]) || std::string_view("+-.%,/()").find(text[i]) != std::string_view::npos)
                    ++i;
                else
                {
                    const std::size_t start = i;
                    while (i < text.size() && word(text[i]))
                        ++i;
                    if (i == start)
                        return false;
                    const auto token = text.substr(start, i - start);
                    if (token != "by" && token != "to" && token != "from" && token != "at" && token != "of"
                        && token != "in" && token != "point" && token != "points" && token != "level"
                        && token != "levels" && token != "permanently" && token != "temporarily")
                        return false;
                }
            }
            return hasNumber;
        }

        bool statNotice(std::string_view text)
        {
            std::string_view subject = text;
            if (subject.starts_with("your "))
                subject.remove_prefix(5);
            if (subject.starts_with("base "))
                subject.remove_prefix(5);
            else if (subject.starts_with("maximum "))
                subject.remove_prefix(8);
            if (subject.starts_with("skill in "))
                subject.remove_prefix(9);

            for (std::string_view name : sStatNames)
            {
                if (!subject.starts_with(name))
                    continue;
                std::string_view change = trim(subject.substr(name.size()));
                if (change.empty() || (subject.size() > name.size() && word(subject[name.size()])))
                    continue;
                for (std::string_view label : { "skill ", "attribute ", "stat " })
                    if (change.starts_with(label))
                        change = trim(change.substr(label.size()));
                if (change.empty())
                    continue;
                if ((change.front() == '+' || change.front() == '-') && numericChange(change))
                    return true;
                for (std::string_view verb : { "has been increased", "has been decreased", "has been raised",
                         "has been reduced", "has been restored", "has increased", "has decreased", "has improved",
                         "was increased", "was decreased", "was restored", "increased", "decreased", "improved",
                         "raised", "reduced", "restored", "drained", "fortified", "damaged" })
                {
                    if (change == verb)
                        return true;
                    if (change.starts_with(verb) && change.size() > verb.size() && space(change[verb.size()])
                        && numericChange(trim(change.substr(verb.size()))))
                        return true;
                }
                if (change.starts_with("is now ") && numericChange(change.substr(7)))
                    return true;
            }

            for (std::string_view prefix : { "you gain ", "you gained ", "you have gained ", "you lose ",
                     "you lost ", "you have lost " })
            {
                if (!text.starts_with(prefix))
                    continue;
                const auto rest = text.substr(prefix.size());
                for (std::string_view name : sStatNames)
                {
                    if (rest.size() > name.size() && rest.ends_with(name)
                        && space(rest[rest.size() - name.size() - 1])
                        && numericChange(rest.substr(0, rest.size() - name.size())))
                        return true;
                }
            }
            return false;
        }

        bool inventoryNotice(std::string_view text)
        {
            if (startsAny(text, { "item added:", "items added:", "item removed:", "items removed:",
                    "item received:", "items received:", "received item:" }))
                return text.find_first_not_of(' ', text.find(':') + 1) != std::string_view::npos;

            for (std::string_view suffix : { " has been added to your inventory", " have been added to your inventory",
                     " was added to your inventory", " were added to your inventory", " added to your inventory",
                     " has been removed from your inventory", " have been removed from your inventory",
                     " was removed from your inventory", " were removed from your inventory",
                     " removed from your inventory" })
            {
                if (!text.ends_with(suffix) || text.size() == suffix.size())
                    continue;
                const auto item = trim(text.substr(0, text.size() - suffix.size()));
                if (!item.empty()
                    && !startsAny(item, { "i ", "we ", "you ", "if ", "when ", "once ", "after ", "before ",
                        "perhaps ", "maybe ", "did ", "has ", "have ", "will ", "would ", "could ", "should " })
                    && !hasAnyWord(item, { "said", "says", "say", "whether", "if", "that" }))
                    return true;
            }
            return false;
        }

        bool actorSubject(std::string_view original, std::string_view subject, Context context)
        {
            subject = trim(subject);
            for (std::string_view auxiliary : { " is", " was", " are", " were", " starts", " started", " begins",
                     " began", " keeps", " kept", " suddenly", " quietly", " loudly", " softly" })
                if (subject.ends_with(auxiliary))
                    subject.remove_suffix(auxiliary.size());
            subject = trim(subject);
            if (subject.empty() || subject == "the" || subject == "a" || subject == "an" || subject.size() > 80
                || std::count(subject.begin(), subject.end(), ' ') > 6
                || subject.find_first_of(".,!?;:\"[]()*/\n") != std::string_view::npos
                || hasAnyWord(subject, { "if", "when", "whether", "whenever", "will", "would", "can", "could",
                    "may", "might", "must", "should", "said", "says", "say", "tell", "told", "think", "believe",
                    "hear", "heard", "about", "because", "that", "who", "which", "what", "why", "how", "have",
                    "has", "had", "is", "are", "was", "were", "be", "been" }))
                return false;
            if (subject == "i")
                return context == Context::Journal;
            if (startsAny(subject, { "i ", "my ", "your ", "his ", "her ", "our ", "their " }))
                return false;
            if (subject == "he" || subject == "she" || subject == "they" || subject == "it" || subject == "you")
                return true;
            if (startsAny(subject, { "the ", "a ", "an " }))
                return true;
            original = trim(original);
            return !original.empty()
                && ((original.front() >= 'A' && original.front() <= 'Z')
                    || static_cast<unsigned char>(original.front()) >= 0x80);
        }

        constexpr std::string_view sActions[]{ "coughs", "coughed", "coughing", "sighs", "sighed", "sighing",
            "laughs", "laughed", "laughing", "chuckles", "chuckled", "chuckling", "smiles", "smiled", "smiling",
            "frowns", "frowned", "frowning", "nods", "nodded", "nodding", "shrugs", "shrugged", "shrugging",
            "shivers", "shivered", "shivering", "trembles", "trembled", "trembling", "sneezes", "sneezed", "sneezing",
            "yawns", "yawned", "yawning", "gasps", "gasped", "gasping", "groans", "groaned", "groaning", "scowls",
            "scowled", "scowling", "grins", "grinned", "grinning", "winces", "winced", "wincing", "blinks", "blinked",
            "blinking", "pauses", "paused", "pausing", "sniffs", "sniffed", "sniffing", "sobs", "sobbed", "sobbing",
            "weeps", "wept", "weeping", "bows", "bowed", "bowing", "creaks", "creaked", "creaking", "rumbles",
            "rumbled", "rumbling", "echoes", "echoed", "echoing", "rustles", "rustled", "rustling" };

        bool stageDirection(std::string_view original, std::string_view text, Context context)
        {
            const std::size_t end = text.find(' ');
            const auto first = text.substr(0, end);
            // These broader physical verbs are accepted only inside explicit stage-direction delimiters.
            for (std::string_view action : { "draws", "draw", "drawing", "takes", "take", "taking", "places",
                     "place", "placing", "hands", "hand", "handing", "gives", "give", "giving", "reaches", "reach",
                     "reaching", "rubs", "rub", "rubbing", "closes", "close", "closing", "opens", "open", "opening",
                     "raises", "raise", "raising", "lowers", "lower", "lowering", "shakes", "shake", "shaking",
                     "clears", "clear", "clearing", "looks", "look", "looking", "turns", "turn", "turning", "steps",
                     "step", "stepping", "leans", "lean", "leaning", "sits", "sit", "sitting", "stands", "stand",
                     "standing", "walks", "walk", "walking", "moves", "move", "moving", "smirks", "smirk",
                     "smirking", "picks", "pick", "picking", "points", "point", "pointing", "gestures", "gesture",
                     "gesturing", "breathes", "breathe", "breathing", "inhales", "inhale", "inhaling", "exhales",
                     "exhale", "exhaling", "whispers", "whisper", "whispering", "mutters", "mutter", "muttering",
                     "stares", "stare", "staring", "glares", "glare", "glaring" })
            {
                if (first == action && end != std::string_view::npos
                    && !hasAnyWord(text.substr(end + 1), { "is", "are", "means", "mean", "because", "if", "when",
                        "that", "but", "will", "would", "could" }))
                    return true;
                const std::size_t at = text.find(action);
                if (at > 0 && at != std::string_view::npos && space(text[at - 1])
                    && (at + action.size() == text.size() || !word(text[at + action.size()]))
                    && actorSubject(original, text.substr(0, at), context))
                    return true;
            }
            const bool action = std::find(std::begin(sActions), std::end(sActions), first) != std::end(sActions)
                || first == "cough" || first == "sigh" || first == "laugh" || first == "chuckle" || first == "gasp"
                || first == "groan" || first == "sob" || first == "sniff" || first == "sneeze" || first == "yawn";
            if (!action)
                return false;
            if (end == std::string_view::npos)
                return true;
            const auto rest = text.substr(end + 1);
            return startsAny(rest, { "at ", "to ", "with ", "in ", "under ", "for " })
                || (!hasAnyWord(rest, { "is", "are", "was", "were", "means", "mean", "because", "if", "when",
                         "that", "but", "and", "or", "will", "can", "would", "could" })
                    && (rest.ends_with("ly") || rest == first || rest == "a little"));
        }

        bool narration(std::string_view original, std::string_view text, Context context)
        {
            const bool hears = startsAny(text, { "you hear ", "you can hear " })
                || (context == Context::Journal && startsAny(text, { "i hear ", "i heard ", "i can hear ", "i could hear " }));
            if (hears && !hasAnyWord(text, { "rumour", "rumor", "rumours", "rumors" })
                && hasAnyWord(text, { "sound", "sounds", "noise", "noises", "footsteps", "cough", "coughing", "scream",
                    "screams", "screaming", "whisper", "whispers", "whispering", "rustle", "rustling", "voice", "voices",
                    "click", "clicking", "thud", "bang", "ringing", "bell", "bells", "laughter", "laughing", "cry",
                    "cries", "crying", "groan", "groaning", "roar", "roaring", "growl", "growling", "splash", "splashing",
                    "drip", "dripping", "music", "singing", "chanting", "breathing", "muttering", "thunder", "shout",
                    "shouting", "clatter", "rattle", "rattling", "scrape", "scraping", "howl", "howling", "chime", "chimes",
                    "rumble", "rumbling", "sobbing", "humming", "creak", "creaking" })
                && !startsAny(text, { "you hear that ", "you hear about ", "you hear of ", "you hear what ",
                    "i heard that ", "i hear that ", "i heard about ", "i heard of " }))
                return true;

            const bool feels = startsAny(text, { "you feel ", "you smell " })
                || (context == Context::Journal && startsAny(text, { "i feel ", "i felt ", "i smell ", "i smelled " }));
            if (feels && hasAnyWord(text, { "cold", "warmth", "heat", "chill", "shiver", "tremor", "pain", "sting",
                             "burning", "tingling", "numbness", "breeze", "touch", "presence", "dizzy", "smoke", "sulphur",
                             "sulfur", "blood", "rot", "decay" }))
                return true;

            for (std::string_view action : sActions)
            {
                for (std::size_t at = text.find(action); at != std::string_view::npos; at = text.find(action, at + 1))
                {
                    const std::size_t end = at + action.size();
                    if (at > 0 && space(text[at - 1]) && (end == text.size() || !word(text[end]))
                        && actorSubject(original, text.substr(0, at), context))
                        return true;
                }
            }
            for (std::string_view action : { "clears his throat", "clears her throat", "clears their throat",
                     "cleared his throat", "cleared her throat", "cleared their throat", "shakes his head",
                     "shakes her head", "shakes their head", "shook his head", "shook her head", "looks at you",
                     "looks away", "looks around", "looks down", "looks up", "turns to you", "turns away",
                     "turns around", "takes a step back", "steps back", "steps forward", "steps aside", "leans closer",
                     "leans forward", "leans back", "raises an eyebrow" })
            {
                const std::size_t at = text.find(action);
                if (at > 0 && at != std::string_view::npos && space(text[at - 1])
                    && actorSubject(original, text.substr(0, at), context))
                    return true;
            }

            const bool observes = startsAny(text, { "you see ", "you notice ", "you watch " })
                || (context == Context::Journal
                    && startsAny(text, { "i see ", "i saw ", "i notice ", "i noticed ", "i watched ", "i witnessed " }));
            if (observes)
                for (std::string_view action : sActions)
                    if (hasWord(text, action))
                        return true;
            return false;
        }

        Category recognise(std::string_view original, Context context, bool detectNarration, bool stage = false)
        {
            original = trim(original);
            if (original.empty() || original.back() == '?')
                return Category::Default;
            while (!original.empty() && std::string_view(".!;:").find(original.back()) != std::string_view::npos)
                original.remove_suffix(1);
            const std::string text = lower(original);
            if (text == "your journal has been updated" || text == "journal has been updated"
                || text == "your quest journal has been updated" || text == "journal updated" || text == "quest updated")
                return Category::Journal;
            if (inventoryNotice(text))
                return Category::Inventory;
            if (statNotice(text))
                return Category::Stat;
            if (detectNarration
                && (narration(original, text, context) || (stage && stageDirection(original, text, context))))
                return Category::Narration;
            return Category::Default;
        }

        struct Range
        {
            std::size_t mBegin;
            std::size_t mEnd;
        };

        std::vector<Range> quotations(std::string_view text)
        {
            std::vector<Range> result;
            std::string_view close;
            std::size_t start = 0;
            for (std::size_t i = 0; i < text.size(); ++i)
            {
                if (text[i] == '\\' && i + 1 < text.size())
                {
                    ++i;
                    continue;
                }
                if (!close.empty())
                {
                    if (text.substr(i).starts_with(close)
                        && !(close == "'" && i > 0 && i + 1 < text.size() && word(text[i - 1]) && word(text[i + 1])))
                    {
                        result.push_back({ start, i + close.size() });
                        i += close.size() - 1;
                        close = {};
                    }
                    continue;
                }
                std::size_t openSize = 1;
                if (text[i] == '"')
                    close = "\"";
                else if (text[i] == '\'' && (i == 0 || !word(text[i - 1])) && i + 1 < text.size()
                    && !space(text[i + 1]))
                    close = "'";
                else if (text.substr(i).starts_with("\xe2\x80\x9c"))
                {
                    close = "\xe2\x80\x9d";
                    openSize = 3;
                }
                else if (text.substr(i).starts_with("\xe2\x80\x98"))
                {
                    close = "\xe2\x80\x99";
                    openSize = 3;
                }
                if (!close.empty())
                {
                    start = i;
                    i += openSize - 1;
                }
            }
            return result;
        }

        bool sentenceEnd(std::string_view text, std::size_t pos)
        {
            if (text[pos] == '\n' || text[pos] == '\r' || text[pos] == ';')
                return true;
            if (text[pos] != '.' && text[pos] != '!' && text[pos] != '?')
                return false;
            if (text[pos] == '.' && pos > 0 && pos + 1 < text.size() && digit(text[pos - 1]) && digit(text[pos + 1]))
                return false;
            if (pos + 1 < text.size() && !space(text[pos + 1]) && text[pos + 1] != ')' && text[pos + 1] != ']')
                return false;
            if (text[pos] == '.')
            {
                std::size_t start = pos;
                while (start > 0 && word(text[start - 1]))
                    --start;
                const auto abbreviation = lower(text.substr(start, pos - start));
                if (pos + 1 < text.size()
                    && ((abbreviation.size() == 1 && abbreviation.front() >= 'a' && abbreviation.front() <= 'z')
                        || abbreviation == "mr" || abbreviation == "mrs" || abbreviation == "ms"
                        || abbreviation == "dr" || abbreviation == "st"))
                    return false;
            }
            return true;
        }

        enum class Field
        {
            Literal,
            String,
            Integer,
            Float,
            Character,
        };

        struct FormatToken
        {
            Field mField;
            std::string mLiteral;
        };

        bool parseFormat(std::string_view format, std::vector<FormatToken>& tokens)
        {
            std::string literal;
            bool anchored = false;
            const auto flush = [&] {
                if (!literal.empty())
                {
                    anchored = anchored || std::any_of(literal.begin(), literal.end(), [](char ch) { return !space(ch); });
                    tokens.push_back({ Field::Literal, std::move(literal) });
                    literal.clear();
                }
            };
            for (std::size_t i = 0; i < format.size();)
            {
                if (format[i] != '%')
                {
                    literal.push_back(format[i++]);
                    continue;
                }
                ++i;
                if (i == format.size())
                    return false;
                if (format[i] == '%')
                {
                    literal.push_back('%');
                    ++i;
                    continue;
                }
                flush();
                const std::size_t position = i;
                while (i < format.size() && digit(format[i]))
                    ++i;
                if (i < format.size() && i > position && format[i] == '$')
                    ++i;
                else
                    i = position;
                while (i < format.size() && std::string_view("-+ #0").find(format[i]) != std::string_view::npos)
                    ++i;
                while (i < format.size() && digit(format[i]))
                    ++i;
                if (i < format.size() && format[i] == '.')
                {
                    ++i;
                    while (i < format.size() && digit(format[i]))
                        ++i;
                }
                if (i < format.size() && std::string_view("hljztL").find(format[i]) != std::string_view::npos)
                {
                    const char length = format[i++];
                    if (i < format.size() && (length == 'h' || length == 'l') && format[i] == length)
                        ++i;
                }
                if (i == format.size())
                    return false;
                Field field;
                switch (format[i++])
                {
                    case 's': field = Field::String; break;
                    case 'd': case 'i': case 'u': field = Field::Integer; break;
                    case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': field = Field::Float; break;
                    case 'c': field = Field::Character; break;
                    default: return false;
                }
                tokens.push_back({ field, {} });
                if (tokens.size() > 64)
                    return false;
            }
            flush();
            return anchored;
        }

        std::uint16_t numberStep(std::uint16_t state, char ch, bool floating)
        {
            // A small finite-state matcher for printf's signed, optionally padded decimal output.
            constexpr std::uint16_t lead = 1, sign = 2, integer = 4, dot = 8, fraction = 16, exponent = 32,
                                    exponentSign = 64, exponentDigits = 128, trailing = 256;
            std::uint16_t next = 0;
            if (state & lead)
            {
                if (ch == ' ') next |= lead;
                if (ch == '+' || ch == '-') next |= sign;
                if (digit(ch)) next |= integer;
                if (floating && (ch == '.' || ch == ',')) next |= dot;
            }
            if (state & sign)
            {
                if (digit(ch)) next |= integer;
                if (floating && (ch == '.' || ch == ',')) next |= dot;
            }
            if (state & integer)
            {
                if (digit(ch)) next |= integer;
                if (ch == ' ') next |= trailing;
                if (floating && (ch == '.' || ch == ',')) next |= fraction;
                if (floating && (ch == 'e' || ch == 'E')) next |= exponent;
            }
            if ((state & dot) && digit(ch)) next |= fraction;
            if (state & fraction)
            {
                if (digit(ch)) next |= fraction;
                if (ch == 'e' || ch == 'E') next |= exponent;
                if (ch == ' ') next |= trailing;
            }
            if (state & exponent)
            {
                if (ch == '+' || ch == '-') next |= exponentSign;
                if (digit(ch)) next |= exponentDigits;
            }
            if ((state & exponentSign) && digit(ch)) next |= exponentDigits;
            if (state & exponentDigits)
            {
                if (digit(ch)) next |= exponentDigits;
                if (ch == ' ') next |= trailing;
            }
            if ((state & trailing) && ch == ' ') next |= trailing;
            return next;
        }
    }

    std::vector<Span> classify(std::string_view text, Context context, bool detectNarration)
    {
        if (text.empty())
            return {};
        std::vector<Category> categories(text.size(), Category::Default);
        const auto quotes = quotations(text);
        const auto paint = [&](std::size_t begin, std::size_t end, Category category) {
            std::fill(categories.begin() + begin, categories.begin() + end, category);
        };
        const auto classifySentence = [&](std::size_t begin, std::size_t end) {
            auto content = trim(text.substr(begin, end - begin));
            if (content.empty())
                return;
            const std::size_t first = static_cast<std::size_t>(content.data() - text.data());
            while (!content.empty() && std::string_view(".!;:").find(content.back()) != std::string_view::npos)
                content.remove_suffix(1);
            const std::size_t last = first + content.size();
            const auto firstQuote = std::lower_bound(quotes.begin(), quotes.end(), begin,
                [](const Range& quote, std::size_t position) { return quote.mEnd <= position; });
            if (firstQuote != quotes.end() && firstQuote->mBegin <= first && firstQuote->mEnd >= last)
                return;
            const auto category = recognise(text.substr(begin, end - begin), context, detectNarration);
            paint(begin, end, category);
            if (category == Category::Narration)
                for (auto quote = firstQuote; quote != quotes.end() && quote->mBegin < end; ++quote)
                    paint(std::max(begin, quote->mBegin), std::min(end, quote->mEnd), Category::Default);
        };

        std::size_t begin = 0;
        std::size_t quoteIndex = 0;
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            while (quoteIndex < quotes.size() && quotes[quoteIndex].mEnd <= i)
                ++quoteIndex;
            if (quoteIndex < quotes.size() && quotes[quoteIndex].mBegin <= i)
            {
                i = quotes[quoteIndex].mEnd - 1;
                continue;
            }
            if (sentenceEnd(text, i))
            {
                classifySentence(begin, i + 1);
                begin = i + 1;
            }
        }
        classifySentence(begin, text.size());

        quoteIndex = 0;
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            while (quoteIndex < quotes.size() && quotes[quoteIndex].mEnd <= i)
                ++quoteIndex;
            if (quoteIndex < quotes.size() && quotes[quoteIndex].mBegin <= i)
            {
                i = quotes[quoteIndex].mEnd - 1;
                continue;
            }
            const char open = text[i];
            if (open != '[' && open != '(' && open != '*')
                continue;
            const char close = open == '[' ? ']' : open == '(' ? ')' : '*';
            const std::size_t limit = std::min(text.size(), i + 2048);
            std::size_t end = i + 1;
            int depth = 1;
            for (; end < limit; ++end)
            {
                if (open != '*' && text[end] == open)
                    ++depth;
                else if (text[end] == close && --depth == 0)
                    break;
            }
            if (end == limit)
                continue;
            const auto category = recognise(text.substr(i + 1, end - i - 1), context, detectNarration, true);
            if (category != Category::Default)
            {
                paint(i, end + 1, category);
                if (category == Category::Narration)
                    for (auto quote = quotes.begin() + quoteIndex; quote != quotes.end() && quote->mBegin < end; ++quote)
                        if (quote->mBegin > i && quote->mEnd <= end)
                            paint(quote->mBegin, quote->mEnd, Category::Default);
                i = end;
            }
        }

        std::vector<Span> result;
        begin = 0;
        for (std::size_t i = 1; i <= text.size(); ++i)
        {
            if (i == text.size() || categories[i] != categories[begin])
            {
                result.push_back({ begin, i, categories[begin] });
                begin = i;
            }
        }
        return result;
    }

    bool matchesFormat(std::string_view text, std::string_view format)
    {
        constexpr std::size_t workLimit = 4 * 1024 * 1024;
        if (format.empty() || text.size() > 65536 || format.size() > 4096
            || (!text.empty() && format.size() > workLimit / text.size()))
            return false;
        std::vector<FormatToken> tokens;
        if (!parseFormat(format, tokens))
            return false;
        std::vector<std::uint8_t> reachable(text.size() + 1), next(text.size() + 1);
        reachable[0] = true;
        for (const FormatToken& token : tokens)
        {
            std::fill(next.begin(), next.end(), false);
            if (token.mField == Field::Literal)
            {
                for (std::size_t i = 0; i <= text.size(); ++i)
                    if (reachable[i] && text.substr(i).starts_with(token.mLiteral))
                        next[i + token.mLiteral.size()] = true;
            }
            else if (token.mField == Field::String)
            {
                next[0] = reachable[0];
                for (std::size_t i = 0; i < text.size(); ++i)
                    next[i + 1] = reachable[i + 1] || (next[i] && text[i] != '\n' && text[i] != '\r');
            }
            else if (token.mField == Field::Character)
            {
                for (std::size_t i = 0; i < text.size(); ++i)
                    next[i + 1] = reachable[i];
            }
            else
            {
                std::uint16_t state = 0;
                for (std::size_t i = 0; i < text.size(); ++i)
                {
                    if (reachable[i])
                        state |= 1;
                    state = numberStep(state, text[i], token.mField == Field::Float);
                    next[i + 1] = (state & (4 | 16 | 128 | 256)) != 0;
                }
            }
            reachable.swap(next);
        }
        return reachable.back();
    }
}
