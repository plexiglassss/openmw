#include "../../apps/openmw/mwgui/messageboxposition.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    int checks = 0;

    void require(bool result, const char* explanation)
    {
        ++checks;
        if (!result)
            throw std::runtime_error(explanation);
    }

    void legacyPositions()
    {
        // Different widths and heights catch incorrect group-centering and
        // reversing the bottom stack. These are actual legacy layout values.
        for (const auto& view : { std::array<int, 2>{ 640, 480 }, { 1920, 1080 }, { 3840, 2160 } })
        {
            int preceding = 0;
            const std::array<int, 3> heights{ 28, 52, 36 };
            const std::array<int, 3> widths{ 220, 310, 180 };
            for (int i = 0; i < 3; ++i)
            {
                const auto p = MWGui::getMessageBoxPosition(
                    view[0], view[1], widths[i], heights[i], 124, preceding, 7, 0, 0);
                require(p.mLeft == (view[0] - widths[i]) / 2, "Default horizontal placement changed");
                require(p.mTop == view[1] - heights[i] - preceding - 48, "Default vertical placement changed");
                preceding += heights[i] + 4;
            }
        }
    }

    void anchorsAndDirections()
    {
        constexpr std::array<int, 3> xs{ 8, 800, 1592 };
        constexpr std::array<int, 3> ys{ 8, 520, 992 };
        for (int anchor = 0; anchor < 9; ++anchor)
        {
            const auto p = MWGui::getMessageBoxPosition(1920, 1080, 320, 40, 40, 0, anchor, 0, 0);
            require(p.mLeft == xs[anchor % 3] && p.mTop == ys[anchor / 3], "Wrong anchor placement");
        }
        const auto raised = MWGui::getMessageBoxPosition(1920, 1080, 320, 40, 40, 0, 7, 0, -80);
        require(raised.mTop == 912, "Negative vertical offset must raise messages above hotkeys");
        const auto moved = MWGui::getMessageBoxPosition(1920, 1080, 320, 40, 40, 0, 4, 120, 75);
        require(moved.mLeft == 920 && moved.mTop == 595, "Offset directions must be right and down");
        const auto resized = MWGui::getMessageBoxPosition(960, 540, 320, 40, 40, 0, 4, 120, 75);
        require(resized.mLeft == 440 && resized.mTop == 325, "Placement must follow the current logical viewport");
    }

    void stackSpacingAndBounds()
    {
        for (const auto& view : { std::array<int, 2>{ 640, 480 }, { 320, 200 }, { 1920, 1080 } })
            for (int anchor = 0; anchor < 9; ++anchor)
                for (int offset : { -1000, -80, 0, 80, 1000 })
                {
                    const auto first = MWGui::getMessageBoxPosition(view[0], view[1], 240, 28, 124, 0, anchor, offset, offset);
                    const auto second = MWGui::getMessageBoxPosition(view[0], view[1], 280, 52, 124, 32, anchor, offset, offset);
                    const auto third = MWGui::getMessageBoxPosition(view[0], view[1], 200, 36, 124, 88, anchor, offset, offset);
                    require(first.mLeft >= 0 && first.mLeft + 240 <= view[0], "First message left screen horizontally");
                    require(second.mLeft >= 0 && second.mLeft + 280 <= view[0], "Second message left screen horizontally");
                    require(third.mLeft >= 0 && third.mLeft + 200 <= view[0], "Third message left screen horizontally");
                    require(first.mTop >= 0 && first.mTop + 28 <= view[1], "First message left screen vertically");
                    require(second.mTop >= 0 && second.mTop + 52 <= view[1], "Second message left screen vertically");
                    require(third.mTop >= 0 && third.mTop + 36 <= view[1], "Third message left screen vertically");
                    if (anchor / 3 == 2)
                    {
                        require(second.mTop + 52 + 4 == first.mTop, "Bottom stack lost its first gap");
                        require(third.mTop + 36 + 4 == second.mTop, "Bottom stack lost its second gap");
                    }
                    else
                    {
                        require(first.mTop + 28 + 4 == second.mTop, "Top/centre stack lost its first gap");
                        require(second.mTop + 52 + 4 == third.mTop, "Top/centre stack lost its second gap");
                    }
                }

        const auto centre = MWGui::getMessageBoxPosition(1920, 1080, 240, 28, 124, 0, 4, 0, 0);
        require(centre.mTop == 478, "Centre must centre the entire stack rather than its first message");
        // Expiry/new messages change the stack's height. The bottom edge stays
        // anchored, while a centre stack re-centres as a group.
        const auto remaining = MWGui::getMessageBoxPosition(1920, 1080, 240, 28, 28, 0, 7, 0, 0);
        require(remaining.mTop == 1004, "Single remaining bottom message drifted after expiry");
    }

    void invalidAndOversizedInputs()
    {
        for (int anchor : { -1, 9, std::numeric_limits<int>::min(), std::numeric_limits<int>::max() })
        {
            const auto p = MWGui::getMessageBoxPosition(1920, 1080, 320, 40, 40, 0, anchor, 0, 0);
            require(p.mLeft == 800 && p.mTop == 992, "Unknown anchors must fall back to the existing position");
        }
        for (int anchor = 0; anchor < 9; ++anchor)
        {
            const auto empty = MWGui::getMessageBoxPosition(0, 0, 320, 40, 40, 0, anchor, 0, 0);
            require(empty.mLeft == 0 && empty.mTop == 0, "Zero-sized viewport gave an invalid origin");
            const auto huge = MWGui::getMessageBoxPosition(100, 40, 500, 300, 600, 304, anchor, -1000, 1000);
            require(huge.mLeft == 0 && huge.mTop == 0, "Oversized message origin must remain visible");
            const auto negative = MWGui::getMessageBoxPosition(-1, -10, -1, -1, -1, -1, anchor, 0, 0);
            require(negative.mLeft == 0 && negative.mTop == 0, "Negative dimensions must not produce negative positions");
        }
        const auto extreme = MWGui::getMessageBoxPosition(std::numeric_limits<int>::max(),
            std::numeric_limits<int>::max(), 300, 40, std::numeric_limits<int>::max(),
            std::numeric_limits<int>::max(), 8, std::numeric_limits<int>::max(), std::numeric_limits<int>::min());
        require(extreme.mLeft >= 0 && extreme.mTop >= 0, "Extreme integer inputs overflowed");
    }
}

int main()
{
    try
    {
        legacyPositions();
        anchorsAndDirections();
        stackSpacingAndBounds();
        invalidAndOversizedInputs();
        std::cout << "PASS: " << checks << " message-position geometry checks\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
