#ifndef OPENMW_MWGUI_MESSAGEBOXSTATE_H
#define OPENMW_MWGUI_MESSAGEBOXSTATE_H

#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace MWGui
{
    using MessageBoxId = std::uint64_t;
    // Exactly representable in both Lua 5.2's numbers and Lua 5.4's integers.
    inline constexpr MessageBoxId MaxMessageBoxId = (MessageBoxId{ 1 } << 53) - 1;

    inline MessageBoxId checkedMessageBoxId(double value)
    {
        if (!std::isfinite(value) || value < 1 || value > static_cast<double>(MaxMessageBoxId)
            || std::floor(value) != value)
            throw std::invalid_argument("Message box ID must be a positive integer returned by getInteractiveMessageBox");
        return static_cast<MessageBoxId>(value);
    }

    inline std::size_t checkedMessageBoxButtonIndex(double value, std::size_t buttonCount)
    {
        if (!std::isfinite(value) || value < 1 || value > static_cast<double>(buttonCount)
            || std::floor(value) != value)
            throw std::invalid_argument("Message box tooltip button index is out of range (indices start at 1)");
        return static_cast<std::size_t>(value) - 1;
    }

    struct InteractiveMessageBoxInfo
    {
        MessageBoxId mId;
        std::string mMessage;
        std::vector<std::string> mButtons;
        std::string mSourceScriptId;
    };

    // Zero-based button indices and serialized ESM::RefIds. No Lua objects or
    // widget pointers survive the queued action boundary.
    using MessageBoxSpellTooltips = std::map<std::size_t, std::string>;

    // The manager retains this generation counter across game loads and clears.
    // A queued action for an old box can therefore never annotate its replacement.
    class MessageBoxState
    {
    public:
        void begin(std::string message, std::vector<std::string> buttons, std::string sourceScriptId)
        {
            if (mGeneration == MaxMessageBoxId)
                throw std::overflow_error("Interactive message box generation exhausted");
            mCurrent = InteractiveMessageBoxInfo{
                ++mGeneration, std::move(message), std::move(buttons), std::move(sourceScriptId) };
        }

        void clear() { mCurrent.reset(); }

        const InteractiveMessageBoxInfo* get(std::optional<MessageBoxId> lastId = {}) const
        {
            if (!mCurrent || (lastId && *lastId == mCurrent->mId))
                return nullptr;
            return &*mCurrent;
        }

        bool accepts(MessageBoxId id, const MessageBoxSpellTooltips& tooltips) const
        {
            if (!mCurrent || mCurrent->mId != id)
                return false;
            for (const auto& [index, spell] : tooltips)
                if (index >= mCurrent->mButtons.size() || spell.empty())
                    return false;
            return true;
        }

    private:
        MessageBoxId mGeneration = 0;
        std::optional<InteractiveMessageBoxInfo> mCurrent;
    };
}

#endif
