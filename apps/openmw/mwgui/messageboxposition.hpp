#ifndef OPENMW_MWGUI_MESSAGEBOXPOSITION_H
#define OPENMW_MWGUI_MESSAGEBOXPOSITION_H

#include <algorithm>
#include <cstdint>

namespace MWGui
{
    inline constexpr int MessageBoxGap = 4;

    struct MessageBoxPosition
    {
        int mLeft;
        int mTop;
    };

    // Anchors are row-major: top left (0) through bottom right (8).
    // The default (7) keeps the original bottom-centre position and padding.
    // All dimensions are MyGUI logical pixels, so offsets follow UI scaling.
    inline MessageBoxPosition getMessageBoxPosition(int viewWidth, int viewHeight, int boxWidth, int boxHeight,
        int stackHeight, int precedingHeight, int anchor, int horizontalOffset, int verticalOffset)
    {
        using Coordinate = std::int64_t;
        const Coordinate width = std::max(0, viewWidth);
        const Coordinate height = std::max(0, viewHeight);
        const Coordinate messageWidth = std::max(0, boxWidth);
        const Coordinate messageHeight = std::max(0, boxHeight);
        const Coordinate totalHeight = std::max<Coordinate>(messageHeight, stackHeight);
        const Coordinate preceding = std::clamp<Coordinate>(precedingHeight, 0, totalHeight - messageHeight);
        if (anchor < 0 || anchor > 8)
            anchor = 7;
        const int column = anchor % 3;
        const int row = anchor / 3;

        Coordinate left;
        if (column == 0)
            left = 8;
        else if (column == 1)
            left = (width - messageWidth) / 2;
        else
            left = width - messageWidth - 8;
        left += std::clamp(horizontalOffset, -1000, 1000);

        Coordinate top;
        if (row == 0)
            top = 8;
        else if (row == 1)
            top = (height - totalHeight) / 2;
        else
            top = height - totalHeight - 48;
        top += std::clamp(verticalOffset, -1000, 1000);

        // Clamp the whole stack first. Clamping each ordinary message on its
        // own would pile messages on top of one another near a screen edge.
        top = std::clamp<Coordinate>(top, 0, std::max<Coordinate>(0, height - totalHeight));
        if (row == 2)
            top += totalHeight - messageHeight - preceding;
        else
            top += preceding;

        // Oversized messages cannot fit completely, but their origin remains
        // visible even after reducing the window size or increasing UI scale.
        left = std::clamp<Coordinate>(left, 0, std::max<Coordinate>(0, width - messageWidth));
        top = std::clamp<Coordinate>(top, 0, std::max<Coordinate>(0, height - messageHeight));
        return { static_cast<int>(left), static_cast<int>(top) };
    }
}

#endif
