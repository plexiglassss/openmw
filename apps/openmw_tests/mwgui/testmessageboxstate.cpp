#include "apps/openmw/mwgui/messageboxstate.hpp"
#include "apps/openmw/mwscript/interpretercontext.hpp"

#include <gtest/gtest.h>

#include <limits>

namespace MWGui
{
    namespace
    {
        TEST(MessageBoxState, EmptyAndUnchangedSnapshotsDoNotAllocateCaptions)
        {
            MessageBoxState state;
            EXPECT_EQ(state.get(), nullptr);
            state.begin("What blessing do you ask for?", { "Cure Disease", "Veloth's Indwelling" }, "shrine_veloth");
            ASSERT_NE(state.get(), nullptr);
            const auto id = state.get()->mId;
            EXPECT_EQ(state.get(id), nullptr);
            EXPECT_EQ(state.get()->mMessage, "What blessing do you ask for?");
            EXPECT_EQ(state.get()->mButtons.at(1), "Veloth's Indwelling");
            EXPECT_EQ(state.get()->mSourceScriptId, "shrine_veloth");
        }

        TEST(MessageBoxState, IdenticalPromptsStillHaveDistinctGenerations)
        {
            MessageBoxState state;
            state.begin("Choose", { "Yes", "No" }, "first");
            const auto first = state.get()->mId;
            state.begin("Choose", { "Yes", "No" }, "second");
            ASSERT_NE(state.get(first), nullptr);
            EXPECT_GT(state.get()->mId, first);
            EXPECT_EQ(state.get()->mSourceScriptId, "second");
            EXPECT_FALSE(state.accepts(first, { { 0, "spell" } }));
            EXPECT_FALSE(state.accepts(first, {})); // Stale clear must not affect replacement.
        }

        TEST(MessageBoxState, AnsweredOrClearedBoxRejectsQueuedAnnotations)
        {
            MessageBoxState state;
            state.begin("Choose", { "Yes" }, "script");
            const auto first = state.get()->mId;
            state.clear(); // The production manager does this before answer listeners.
            EXPECT_EQ(state.get(), nullptr);
            EXPECT_FALSE(state.accepts(first, { { 0, "spell" } }));
            EXPECT_FALSE(state.accepts(first, {}));
            state.clear(); // Repeated cleanup is harmless.
            state.begin("Choose", { "Yes" }, "");
            EXPECT_GT(state.get()->mId, first); // Clear/load never resets the counter.
            EXPECT_TRUE(state.get()->mSourceScriptId.empty());
            EXPECT_FALSE(state.accepts(first, { { 0, "spell" } }));
        }

        TEST(MessageBoxState, SparseAnnotationsAndClearsRequireMatchingBoxAndValidButtons)
        {
            MessageBoxState state;
            state.begin("Choose", { "First", "Second", "Cancel" }, "script");
            const auto id = state.get()->mId;
            EXPECT_TRUE(state.accepts(id, { { 1, "spell" } }));
            EXPECT_TRUE(state.accepts(id, { { 0, "spell1" }, { 1, "spell2" } }));
            EXPECT_TRUE(state.accepts(id, {}));
            EXPECT_FALSE(state.accepts(id, { { 3, "spell" } }));
            EXPECT_FALSE(state.accepts(id, { { 0, "" } }));
            EXPECT_FALSE(state.accepts(id + 1, { { 0, "spell" } }));
            EXPECT_EQ(state.get()->mButtons.back(), "Cancel");
        }

        TEST(MessageBoxState, GenerationValidationRejectsFractionalAndNonfiniteIds)
        {
            EXPECT_EQ(checkedMessageBoxId(1), 1u);
            EXPECT_EQ(checkedMessageBoxId(static_cast<double>(MaxMessageBoxId)), MaxMessageBoxId);
            for (const double id : { 0., -1., 1.5, static_cast<double>(MaxMessageBoxId) + 1.,
                     std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
                     std::numeric_limits<double>::quiet_NaN() })
                EXPECT_THROW(checkedMessageBoxId(id), std::invalid_argument);
        }

        TEST(MessageBoxState, LuaIndicesAreOneBasedAndMustFitTheObservedMenu)
        {
            EXPECT_EQ(checkedMessageBoxButtonIndex(1, 3), 0u);
            EXPECT_EQ(checkedMessageBoxButtonIndex(3, 3), 2u);
            for (const double index : { 0., -1., 1.5, 4., std::numeric_limits<double>::infinity(),
                     std::numeric_limits<double>::quiet_NaN() })
                EXPECT_THROW(checkedMessageBoxButtonIndex(index, 3), std::invalid_argument);
            EXPECT_THROW(checkedMessageBoxButtonIndex(1, 0), std::invalid_argument);
        }

        TEST(MessageBoxSource, UnnamedContextsDoNotGuessAnAttachedScript)
        {
            MWScript::InterpreterContext context(nullptr, MWWorld::Ptr());
            EXPECT_TRUE(context.getMessageBoxSourceScriptId().empty());
            const MWScript::InterpreterContext::ScriptSourceScope noEngineContext(nullptr, ESM::RefId());
            EXPECT_TRUE(context.getMessageBoxSourceScriptId().empty());
        }

        TEST(MessageBoxSource, NamedScriptScopeRestoresOuterSourceAndClearsOnExit)
        {
            MWScript::InterpreterContext context(nullptr, MWWorld::Ptr());
            const auto outer = ESM::RefId::stringRefId("shrine_outer");
            const auto inner = ESM::RefId::stringRefId("shrine_inner");
            {
                const MWScript::InterpreterContext::ScriptSourceScope first(&context, outer);
                EXPECT_EQ(context.getMessageBoxSourceScriptId(), outer);
                {
                    const MWScript::InterpreterContext::ScriptSourceScope second(&context, inner);
                    EXPECT_EQ(context.getMessageBoxSourceScriptId(), inner);
                }
                EXPECT_EQ(context.getMessageBoxSourceScriptId(), outer);
            }
            EXPECT_TRUE(context.getMessageBoxSourceScriptId().empty());
        }

        TEST(MessageBoxSource, FailingScriptsCannotLeakSourceIntoLaterMenus)
        {
            MWScript::InterpreterContext context(nullptr, MWWorld::Ptr());
            try
            {
                const MWScript::InterpreterContext::ScriptSourceScope source(
                    &context, ESM::RefId::stringRefId("failing_shrine"));
                throw std::runtime_error("Script failure");
            }
            catch (const std::runtime_error&)
            {
            }
            EXPECT_TRUE(context.getMessageBoxSourceScriptId().empty());
        }
    }
}
