#ifndef OPENMW_COMPONENTS_LOADINGLISTENER_GAMEPLAYHANDOFF_HPP
#define OPENMW_COMPONENTS_LOADINGLISTENER_GAMEPLAYHANDOFF_HPP

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <string_view>
#include <utility>

namespace Loading
{
    // Main-thread only. A visual handoff, not a new pause/GUI mode: scripts must
    // still run and the scene must be rendered before the cover is released.
    // This is a bounded settling check, NOT proof that an arbitrary mod cannot
    // schedule more work later. A timeout is reported distinctly, never as ready.
    class GameplayHandoff
    {
    public:
        using Clock = std::chrono::steady_clock;
        enum class Result { None, Settled, Limit, Error };
        static constexpr unsigned MinSamples = 4;
        static constexpr unsigned RequiredShortSamples = 3;
        static constexpr unsigned MaxSamples = 120;
        static constexpr double ShortSampleMs = 50.0;
        static constexpr double MaxHoldMs = 30000.0;

        bool active() const { return mActive; }
        std::uint64_t generation() const { return mGeneration; }
        unsigned samples() const { return mSamples; }
        double worstMs() const { return mWorstMs; }
        double elapsedMs() const
        {
            return std::chrono::duration<double, std::milli>(Clock::now() - mStarted).count();
        }
        Result result() const { return mResult; }

        void request()
        {
            // Reloading while a previous handoff is active must not inherit its
            // consecutive-frame counter or release an old generation's cover.
            cancel();
            ++mGeneration;
            mActive = true;
            mStarted = Clock::now();
            mSamples = mShortSamples = 0;
            mWorstMs = 0;
            mResult = Result::None;
        }

        void attachCover(const void* owner, std::function<void()> release)
        {
            mOwner = owner;
            mReleaseCover = std::move(release);
        }
        void detachCover(const void* owner)
        {
            if (mOwner != owner)
                return;
            mOwner = nullptr;
            mReleaseCover = {};
            mActive = false;
            mResult = Result::None;
        }
        void cancel()
        {
            mActive = false;
            mResult = Result::None;
            releaseCover();
        }

        void observe(std::uint64_t frameGeneration, double frameMs, bool completed,
            double heldMs)
        {
            // A load can happen inside Engine::frame. Do not count that partial
            // frame as a complete initialization cycle for the new game.
            if (!mActive || frameGeneration != mGeneration || mResult != Result::None)
                return;
            ++mSamples;
            if (!completed || !std::isfinite(frameMs) || frameMs < 0)
            {
                mResult = Result::Error;
                return;
            }
            if (frameMs > mWorstMs)
                mWorstMs = frameMs;
            mShortSamples = frameMs <= ShortSampleMs ? mShortSamples + 1 : 0;
            if (mSamples >= MinSamples && mShortSamples >= RequiredShortSamples)
                mResult = Result::Settled;
            else if (mSamples >= MaxSamples || heldMs >= MaxHoldMs)
                mResult = Result::Limit;
        }

        // Called at the beginning of the NEXT frame, after finishGc and before
        // input processing. Rendering and LuaFinish for the measured frame have
        // both completed. Never expose the game midway through a blocking frame.
        Result releaseIfPending()
        {
            if (!mActive || mResult == Result::None)
                return Result::None;
            const Result result = mResult;
            mActive = false;
            releaseCover();
            return result;
        }

    private:
        void releaseCover()
        {
            auto release = std::move(mReleaseCover);
            mReleaseCover = {};
            mOwner = nullptr;
            if (release)
                release();
        }
        bool mActive = false;
        std::uint64_t mGeneration = 0;
        unsigned mSamples = 0;
        unsigned mShortSamples = 0;
        double mWorstMs = 0;
        Result mResult = Result::None;
        Clock::time_point mStarted = Clock::now();
        const void* mOwner = nullptr;
        std::function<void()> mReleaseCover;
    };

    inline GameplayHandoff& gameplayHandoff()
    {
        static GameplayHandoff instance;
        return instance;
    }

    inline bool gameplayHandoffEnabled()
    {
        static const bool enabled = [] {
            const char* value = std::getenv("OPENMW_POSTLOAD_HANDOFF");
            return value == nullptr || std::string_view(value) != "0";
        }();
        return enabled;
    }
}
#endif
