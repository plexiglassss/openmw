#ifndef MWGUI_JOURNALTYPOGRAPHY_HPP
#define MWGUI_JOURNALTYPOGRAPHY_HPP

#include <MyGUI_Types.h>

#include <components/misc/utf8stream.hpp>

#include <algorithm>
#include <array>
#include <cstddef>

namespace MWGui
{
    inline int getJournalIndexColumnCount(int characterCount, int fontSize)
    {
        // Keep the familiar two columns while they fit the 260-pixel index.
        return ((characterCount + 1) / 2) * fontSize <= 260 ? 2 : 3;
    }

    inline int getJournalIndexRowCount(int characterCount, int columns)
    {
        return (characterCount + columns - 1) / columns;
    }

    inline std::array<MyGUI::IntCoord, 3> layoutJournalIndexColumns(
        const std::array<MyGUI::IntCoord, 3>& original, int columns)
    {
        if (columns != 3)
            return original;
        auto result = original;
        const int width = std::max(1, (original[2].right() - original[0].left) / 3);
        for (std::size_t i = 0; i < result.size(); ++i)
        {
            result[i].left = original[0].left + static_cast<int>(i) * width;
            result[i].width = width;
        }
        return result;
    }

    inline Utf8Stream::UnicodeChar getJournalIndexCharacter(std::size_t selected, bool cyrillic)
    {
        if (!cyrillic)
            return static_cast<Utf8Stream::UnicodeChar>('A' + selected);
        // Russian topic names cannot start with the hard or soft sign.
        return static_cast<Utf8Stream::UnicodeChar>(0x0410 + selected + (selected >= 26) + (selected >= 27));
    }

    inline std::size_t moveJournalIndexSelection(
        std::size_t selected, int offset, std::size_t characterCount, std::size_t rows)
    {
        if (characterCount == 0 || rows == 0)
            return 0;
        selected = std::min(selected, characterCount - 1);
        if (offset == 0)
            return selected;
        const std::size_t column = selected / rows;
        const std::size_t row = selected % rows;
        if (offset == -1 || offset == 1)
        {
            const std::size_t count = std::min(rows, characterCount - column * rows);
            const std::size_t next = offset < 0 ? (row + count - 1) % count : (row + 1) % count;
            return column * rows + next;
        }
        const std::size_t columns = (characterCount + rows - 1) / rows;
        const std::size_t nextColumn = offset < 0 ? (column + columns - 1) % columns : (column + 1) % columns;
        const std::size_t count = std::min(rows, characterCount - nextColumn * rows);
        return nextColumn * rows + std::min(row, count - 1);
    }

    inline MyGUI::IntCoord scaleJournalButton(const MyGUI::IntCoord& original, int percent)
    {
        percent = std::clamp(percent, 50, 100);
        const int width = std::max(1, (original.width * percent + 50) / 100);
        const int height = std::max(1, (original.height * percent + 50) / 100);
        return { original.left + (original.width - width) / 2, original.top + (original.height - height) / 2,
            width, height };
    }

    inline MyGUI::IntCoord resizeJournalPageNumber(
        const MyGUI::IntCoord& original, const MyGUI::IntSize& textSize)
    {
        // Expand around the layout's horizontal center so multi-digit numbers
        // do not grow only toward the next journal button.
        const int width = std::max(original.width, textSize.width + 2);
        return { original.left - (width - original.width) / 2, original.top, width,
            std::max(original.height, textSize.height) };
    }
}

#endif
