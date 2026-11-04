#include <gtest/gtest.h>

#include <components/misc/dialoguetext.hpp>

#include <algorithm>
#include <string>
#include <string_view>

namespace
{
    using Misc::DialogueText::Category;
    using Misc::DialogueText::Context;
    using Misc::DialogueText::Span;
    using Misc::DialogueText::classify;
    using Misc::DialogueText::matchesFormat;

    void expectCoverage(std::string_view text, const std::vector<Span>& spans)
    {
        std::size_t end = 0;
        std::string reconstructed;
        for (const Span& span : spans)
        {
            ASSERT_EQ(span.mBegin, end);
            ASSERT_GT(span.mEnd, span.mBegin);
            ASSERT_LE(span.mEnd, text.size());
            for (std::size_t boundary : { span.mBegin, span.mEnd })
                if (boundary < text.size())
                {
                    EXPECT_NE(static_cast<unsigned char>(text[boundary]) & 0xc0, 0x80);
                }
            reconstructed += text.substr(span.mBegin, span.mEnd - span.mBegin);
            end = span.mEnd;
        }
        EXPECT_EQ(end, text.size());
        EXPECT_EQ(reconstructed, text);
        for (std::size_t i = 1; i < spans.size(); ++i)
            EXPECT_NE(spans[i - 1].mCategory, spans[i].mCategory);
    }

    void expectOnly(std::string_view text, Category category, Context context = Context::Dialogue)
    {
        SCOPED_TRACE(text);
        const auto spans = classify(text, context);
        expectCoverage(text, spans);
        ASSERT_EQ(spans.size(), 1u);
        EXPECT_EQ(spans.front().mCategory, category);
    }

    void expectPart(std::string_view text, std::string_view part, Category category,
        Context context = Context::Dialogue, bool detectNarration = true)
    {
        SCOPED_TRACE(part);
        const auto spans = classify(text, context, detectNarration);
        expectCoverage(text, spans);
        const std::size_t begin = text.find(part);
        ASSERT_NE(begin, std::string_view::npos);
        for (const Span& span : spans)
            if (span.mEnd > begin && span.mBegin < begin + part.size())
            {
                EXPECT_EQ(span.mCategory, category);
            }
    }

    TEST(DialogueText, ScreenshotSeparatesSpokenTextInventoryAndJournal)
    {
        constexpr std::string_view text =
            "Perhaps not. But still, the fact remains that they have committed atrocities. "
            "Take this scroll--I have a feeling you may need it to reach Alliastus.\n"
            "Scroll of Ekash's Lock Splitter has been added to your inventory.\n"
            "Your journal has been updated.";
        expectPart(text, "Take this scroll", Category::Default);
        expectPart(text, "Scroll of Ekash's Lock Splitter has been added to your inventory.", Category::Inventory);
        expectPart(text, "Your journal has been updated.", Category::Journal);
    }

    TEST(DialogueText, InventoryNoticesRecogniseCountsAndRemovals)
    {
        for (std::string_view text : { "A note has been added to your inventory.",
                 "2 potions have been added to your inventory.", "A note has been removed from your inventory.",
                 "3 notes have been removed from your inventory.", "Item received: Letter to Caius.",
                 "\"A Stranger's Note\" has been added to your inventory." })
            expectOnly(text, Category::Inventory);
    }

    TEST(DialogueText, ConversationAboutInventoryDoesNotBecomeANotice)
    {
        for (std::string_view text : { "If a note has been added to your inventory, read it.",
                 "You should check whether a note has been added to your inventory.",
                 "I said a note has been added to your inventory.", "I will give you a note.",
                 "Did a note get added to your inventory?", "Item added:" })
            expectOnly(text, Category::Default);
    }

    TEST(DialogueText, RecognisesJournalUpdateNotices)
    {
        for (std::string_view text : { "Your journal has been updated.", "Journal updated!",
                 "Your quest journal has been updated." })
            expectOnly(text, Category::Journal);
        expectOnly("Your journal has been updated?", Category::Default);
        expectOnly("I read your journal.", Category::Default);
    }

    TEST(DialogueText, StatChangesRecogniseGainsLossesAndNumericalResults)
    {
        for (std::string_view text : { "Your Strength has increased by 5.", "Strength +5", "Fatigue -10",
                 "Your Alchemy increased to 40.", "Your reputation has increased.",
                 "Your Health has been restored by 25 points.", "Your Magicka is now 105.",
                 "Your base Strength increased from 40 to 45.", "Your skill in Long Blade has increased by 2.",
                 "Your Mercantile skill has increased to 20.", "Your Strength attribute has increased by 1.",
                 "Sneak decreased to 35.", "You gain 5 Strength.", "You have gained 2 points of Intelligence.",
                 "You lost 5 points of Reputation.", "Your Personality has been reduced by 2.5." })
            expectOnly(text, Category::Stat);
    }

    TEST(DialogueText, OrdinaryDiscussionOfStatsKeepsDialogueColour)
    {
        for (std::string_view text : { "My strength has increased by 5.", "Your strength is impressive.",
                 "Your Strength will increase by 5 if you complete my task.",
                 "Your strength has increased since last we met.",
                 "Your strength increased by 5 if you did that.", "I can improve your Alchemy.",
                 "You have gained my trust.", "You lost the note.", "Your reputation precedes you." })
            expectOnly(text, Category::Default);
    }

    TEST(DialogueText, StatSentencesDoNotColourTheFollowingConversation)
    {
        constexpr std::string_view text = "Your Strength increased by 5. I can teach you more another day.";
        expectPart(text, "Your Strength increased by 5.", Category::Stat);
        expectPart(text, "I can teach you more another day.", Category::Default);
    }

    TEST(DialogueText, RecognisesSoundsAndVisibleCharacterActions)
    {
        for (std::string_view text : { "You hear a sound behind you.", "You can hear footsteps approaching.",
                 "Ariela coughs.", "The guard is coughing loudly.", "He clears his throat.",
                 "Dralcea Arethi quietly coughs.", "She takes a step back.", "A loud noise echoes from the chamber.",
                 "You notice the guard coughing.", "You feel a cold breeze against your skin." })
            expectOnly(text, Category::Narration);
    }

    TEST(DialogueText, ConversationAndHypotheticalActionsStayNormal)
    {
        for (std::string_view text : { "I hear you sell swords.", "I hear a sound behind the door.",
                 "You hear that the guard coughs at night.", "You hear rumours about the missing bell.",
                 "If Ariela coughs, offer her a potion.", "He says he coughs at night.",
                 "I think the guard coughs too much.", "My strength is returning.",
                 "The guard will cough to signal you.", "The coughs are getting worse.", "Do you hear a sound?" })
            expectOnly(text, Category::Default);
    }

    TEST(DialogueText, StageDirectionsOnlyColourTheirOwnDelimitedText)
    {
        constexpr std::string_view text = "Good day. [Ariela coughs.] I need a favour. (as I said) *cough* *really*.";
        expectPart(text, "Good day.", Category::Default);
        expectPart(text, "[Ariela coughs.]", Category::Narration);
        expectPart(text, "I need a favour.", Category::Default);
        expectPart(text, "(as I said)", Category::Default);
        expectPart(text, "*cough*", Category::Narration);
        expectPart(text, "*really*", Category::Default);
        expectOnly("(coughing is a common symptom)", Category::Default);
        expectOnly("(He clears his throat.)", Category::Narration);
        expectOnly("*laughs at you*", Category::Narration);
        expectOnly("[She places the scroll in your hands.]", Category::Narration);
        expectOnly("*draws a deep breath*", Category::Narration);
        expectOnly("[Your Strength has increased by 5.]", Category::Stat);
    }

    TEST(DialogueText, QuotedSpeechIsNotInferredAsNarrationOrANotice)
    {
        for (std::string_view text : { "\"You hear a sound.\"", "'Your Strength has increased by 5.'",
                 "\"[Ariela coughs.]\"", "\xe2\x80\x9cYou hear a noise.\xe2\x80\x9d",
                 "\xe2\x80\x98Your journal has been updated.\xe2\x80\x99" })
            expectOnly(text, Category::Default);

        constexpr std::string_view text = "Ariela coughs, \"Excuse me. Your Strength has increased by 5.\"";
        expectPart(text, "Ariela coughs, ", Category::Narration);
        expectPart(text, "\"Excuse me. Your Strength has increased by 5.\"", Category::Default);
    }

    TEST(DialogueText, JournalFirstPersonSensesDifferFromSpokenDialogue)
    {
        for (std::string_view text : { "I heard a scraping sound behind the door.", "I felt a cold breeze.",
                 "I noticed the guard coughing.", "I witnessed Ariela coughing." })
        {
            expectOnly(text, Category::Narration, Context::Journal);
            expectOnly(text, Category::Default, Context::Dialogue);
        }
        for (std::string_view text : { "I heard that the guard's bell was stolen.",
                 "I spoke with Caius in Balmora. He asked me to find a book.",
                 "I learned that the tomb is north of town.", "I heard about the strange sounds from Caius." })
            expectOnly(text, Category::Default, Context::Journal);
    }

    TEST(DialogueText, NarrationSwitchLeavesActualNoticesEnabled)
    {
        constexpr std::string_view text = "Ariela coughs. [She clears her throat.]\n"
                                         "Your Strength increased by 5.\n"
                                         "A note has been added to your inventory.\n"
                                         "Your journal has been updated.";
        expectPart(text, "Ariela coughs.", Category::Default, Context::Dialogue, false);
        expectPart(text, "[She clears her throat.]", Category::Default, Context::Dialogue, false);
        expectPart(text, "Your Strength increased by 5.", Category::Stat, Context::Dialogue, false);
        expectPart(text, "A note has been added to your inventory.", Category::Inventory, Context::Dialogue, false);
        expectPart(text, "Your journal has been updated.", Category::Journal, Context::Dialogue, false);
    }

    TEST(DialogueText, MultibyteNamesAndMixedWhitespaceKeepExactBytes)
    {
        const std::string text = "\xc3\x89lise coughs.\r\n\t"
                                 "\xce\x9a\xce\xb1\xce\xbb\xce\xb7\xce\xbc\xce\xad\xcf\x81\xce\xb1. "
                                 "[She clears her throat.]\nA note has been added to your inventory.\r\n";
        expectPart(text, "\xc3\x89lise coughs.", Category::Narration);
        expectPart(text, "\xce\x9a\xce\xb1\xce\xbb\xce\xb7\xce\xbc\xce\xad\xcf\x81\xce\xb1.", Category::Default);
        expectPart(text, "[She clears her throat.]", Category::Narration);
        expectPart(text, "A note has been added to your inventory.", Category::Inventory);
    }

    TEST(DialogueText, EmptyAndUnbalancedTextArePreserved)
    {
        EXPECT_TRUE(classify({}, Context::Dialogue).empty());
        for (std::string_view text : { " ", "\n\n", "*cough", "[cough", "(as I said", "*", "[", "(",
                 "\"An unfinished quotation", "## @unknown# ", "... ! ? ", "A note\n*really*" })
            expectCoverage(text, classify(text, Context::Dialogue));
    }

    TEST(DialogueText, LocalisedPrintfFormatsMatchWholeNotifications)
    {
        EXPECT_TRUE(matchesFormat("A note has been added to your inventory.", "%s has been added to your inventory."));
        EXPECT_TRUE(matchesFormat("3 notes have been added to your inventory.", "%d %s have been added to your inventory."));
        EXPECT_TRUE(matchesFormat("Alchemy increased to 14.", "%s increased to %d."));
        EXPECT_TRUE(matchesFormat("Notiz wurde deinem Inventar hinzugef\xc3\xbcgt.",
            "%s wurde deinem Inventar hinzugef\xc3\xbcgt."));
        EXPECT_TRUE(matchesFormat("Reputation +5.50%", "Reputation %+.2f%%"));
        EXPECT_TRUE(matchesFormat("Sneak erh\xc3\xa4lt -2 Punkte.", "%2$s erh\xc3\xa4lt %1$d Punkte."));
        EXPECT_TRUE(matchesFormat("Value:  12", "Value: %3ld"));
        EXPECT_TRUE(matchesFormat("Value: -.5", "Value: %f"));
        EXPECT_TRUE(matchesFormat("Value: 1.2e+03", "Value: %g"));
        EXPECT_TRUE(matchesFormat("Value: 1,5", "Value: %f"));
        EXPECT_TRUE(matchesFormat("Your journal has been updated.", "Your journal has been updated."));
        EXPECT_TRUE(matchesFormat("A A B", "%s A %s"));
        EXPECT_TRUE(matchesFormat(" added.", "%s added."));
    }

    TEST(DialogueText, FormatMatcherRejectsPartialOrInvalidMatches)
    {
        EXPECT_FALSE(matchesFormat("I said a note has been added to your inventory. More follows.",
            "%s has been added to your inventory."));
        EXPECT_FALSE(matchesFormat("Strength increased to many.", "%s increased to %d."));
        EXPECT_FALSE(matchesFormat("Value: 1.5", "Value: %d"));
        EXPECT_FALSE(matchesFormat("Value: 1e", "Value: %g"));
        EXPECT_FALSE(matchesFormat("Value: +", "Value: %d"));
        EXPECT_FALSE(matchesFormat("Anything", "%s"));
        EXPECT_FALSE(matchesFormat("Anything", ""));
        EXPECT_FALSE(matchesFormat("Value: 1", "Value: %n"));
        EXPECT_FALSE(matchesFormat("Value: 1", "Value: %*d"));
        EXPECT_FALSE(matchesFormat("Value: 1", "Value: %"));
        EXPECT_FALSE(matchesFormat("A note\nA sword added.", "%s added."));
        EXPECT_FALSE(matchesFormat("Your journal has been updated. More follows.", "Your journal has been updated."));
    }

    TEST(DialogueText, FormatMatcherHasABoundedWorkBudget)
    {
        EXPECT_FALSE(matchesFormat(std::string(65537, 'a'), "%s added."));
        EXPECT_FALSE(matchesFormat("a", std::string(4097, 'a')));
        EXPECT_FALSE(matchesFormat(std::string(16384, 'a'), std::string(300, 'a') + "%s"));
        const std::string repeated = std::string(2000, 'a') + " added.";
        EXPECT_TRUE(matchesFormat(repeated, "%s added."));
    }
}
