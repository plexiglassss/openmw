#include <apps/openmw/startupscreen.hpp>
#include <chrono>
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <thread>
int main()
{
    { OMW::StartupScreen disabled(false); disabled.finish(); disabled.finish(); }
    { OMW::StartupScreen early; early.finish(); }
    {
        OMW::StartupScreen screen;
        for (int i = 0; i < 20; ++i)
        {
            screen.setStage(i % 2 ? L"Indexing archives" : L"Preparing resources");
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
#ifdef _WIN32
        assert(FindWindowW(L"OpenMWStartupStatus", nullptr) != nullptr);
#endif
        screen.finish();
        screen.finish();
#ifdef _WIN32
        assert(FindWindowW(L"OpenMWStartupStatus", nullptr) == nullptr);
#endif
    }
    try
    {
        OMW::StartupScreen screen;
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        throw std::runtime_error("startup failure");
    }
    catch (const std::runtime_error&) {}
    std::cout << "PASS: disabled, immediate teardown, updates, repeated finish, exception cleanup\n";
}
