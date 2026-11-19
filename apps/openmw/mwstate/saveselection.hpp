#ifndef OPENMW_GAME_MWSTATE_SAVESELECTION_H
#define OPENMW_GAME_MWSTATE_SAVESELECTION_H

#include <list>

#include "character.hpp"

namespace MWState
{
    struct SaveSelection
    {
        const Character* mCharacter = nullptr;
        const Slot* mSlot = nullptr;
    };

    /// Find the newest recognized save across all characters, including quicksaves and autosaves.
    /// Neither character order nor slot order is assumed to stay sorted after saves are changed.
    /// The returned pointers must not be retained across changes to the character/slot lists.
    inline SaveSelection selectLatestSave(
        std::list<Character>::const_iterator begin, std::list<Character>::const_iterator end)
    {
        SaveSelection result;
        for (auto character = begin; character != end; ++character)
        {
            for (const Slot& slot : *character)
            {
                // Equal timestamps are possible after a save-directory copy or on coarse filesystems.
                // Use the complete path to make the selection independent of directory iteration order.
                if (!result.mSlot || slot.mTimeStamp > result.mSlot->mTimeStamp
                    || (slot.mTimeStamp == result.mSlot->mTimeStamp && slot.mPath < result.mSlot->mPath))
                    result = { &*character, &slot };
            }
        }
        return result;
    }
}

#endif
