#ifndef OPENMW_COMPONENTS_DIAGNOSTICS_EXTERIORARRIVAL_H
#define OPENMW_COMPONENTS_DIAGNOSTICS_EXTERIORARRIVAL_H

#include <chrono>
#include <osg/Vec3f>
#include <components/debug/debuglog.hpp>

namespace Diagnostics::ExteriorArrival
{
    using Clock = std::chrono::steady_clock;
    // Door actions, character control and physics observation run on the scene owner.
    inline Clock::time_point started{};
    inline bool landed = false;
    inline bool requested = false;
    inline osg::Vec3f origin;
    inline bool active() { return started != Clock::time_point{}; }
    inline double elapsed() { return std::chrono::duration<double, std::milli>(Clock::now() - started).count(); }
    inline void clear() { started = {}; landed = requested = false; }
    inline void begin()
    {
        clear();
        started = Clock::now();
    }
    inline void ready(const osg::Vec3f& pos)
    {
        if (!active())
            return;
        landed = true;
        origin = pos;
        Log(Debug::Info) << "Exterior arrival gameplay ready: sinceActivationMs=" << elapsed();
    }
    inline void movement(const osg::Vec3f& position, float x, float y)
    {
        if (landed && !requested && (x != 0.f || y != 0.f))
        {
            requested = true;
            origin = position;
            Log(Debug::Info) << "Exterior arrival movement request: sinceActivationMs=" << elapsed();
        }
    }
    inline void observe(const osg::Vec3f& pos)
    {
        if (active() && elapsed() > 30000.0)
        {
            clear();
            return;
        }
        const float dx = pos.x() - origin.x(), dy = pos.y() - origin.y();
        if (requested && dx * dx + dy * dy > 0.0001f)
        {
            Log(Debug::Info) << "Exterior arrival first player motion: sinceActivationMs=" << elapsed();
            clear();
        }
    }
}
#endif
