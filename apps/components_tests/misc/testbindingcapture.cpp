#include <gtest/gtest.h>
#include <components/misc/bindingcapture.hpp>
TEST(BindingCapture, ConsumesOnePhysicalEdgeThroughItsRelease)
{
    Misc::BindingCapture c;
    EXPECT_FALSE(c.event(0, 4, true));
    c.begin();
    EXPECT_TRUE(c.event(0, 4, true));
    EXPECT_TRUE(c.event(0, 4, true));
    EXPECT_TRUE(c.event(1, 1, true));
    EXPECT_TRUE(c.event(1, 1, false));
    EXPECT_TRUE(c.active());
    EXPECT_FALSE(c.take());
    EXPECT_TRUE(c.event(0, 4, false));
    EXPECT_FALSE(c.active());
    EXPECT_EQ(c.take(), std::make_pair(0, 4));
    EXPECT_FALSE(c.take());
}
TEST(BindingCapture, ReleaseWithoutCapturedPressDoesNotCompleteAndCancelDiscardsIt)
{
    Misc::BindingCapture c;
    c.begin();
    EXPECT_TRUE(c.event(0, 225, false)); // pre-held Shift: caller also clears native held state
    EXPECT_TRUE(c.active());
    EXPECT_FALSE(c.take());
    EXPECT_TRUE(c.event(2, 0, true));
    c.cancel();
    EXPECT_FALSE(c.active());
    EXPECT_FALSE(c.take());
    EXPECT_FALSE(c.event(2, 0, false));
}
