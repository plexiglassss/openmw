#ifndef OPENMW_STARTUPSCREEN_HPP
#define OPENMW_STARTUPSCREEN_HPP

#include <memory>

namespace OMW
{
    // Platform drawing details must not leak into engine translation units.
    class StartupScreen
    {
    public:
        explicit StartupScreen(bool enabled = true);
        StartupScreen(const StartupScreen&) = delete;
        StartupScreen& operator=(const StartupScreen&) = delete;
        ~StartupScreen();
        void setStage(const wchar_t* stage);
        void finish();

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}
#endif
