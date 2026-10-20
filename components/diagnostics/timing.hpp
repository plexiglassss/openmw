#ifndef OPENMW_DIAGNOSTICS_TIMING_H
#define OPENMW_DIAGNOSTICS_TIMING_H

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>

// Opt-in elapsed-time measurements; independent of Lua's allocator/instruction hooks.
// Thread-local aggregation avoids a lock for each callback. Output is serialized only
// at frame boundaries. Exclusive time subtracts instrumented children, not arbitrary
// calls made by a mod (including uninstrumented Lua interfaces and native bindings).
namespace Diagnostics
{
    using Clock = std::chrono::steady_clock;
    inline double microseconds(Clock::duration d)
    {
        return std::chrono::duration<double, std::micro>(d).count();
    }
    inline std::string quote(std::string_view s)
    {
        std::string out = "\"";
        for (char c : s)
        {
            if (c == '"') out += '"';
            out += c;
        }
        return out + '"';
    }
    struct Output
    {
        std::mutex mutex;
        std::ofstream stream;
        Clock::time_point epoch = Clock::now();
        std::atomic<unsigned> nextThread{0};
        Output()
        {
#ifdef _WIN32
            const wchar_t* path = _wgetenv(L"OPENMW_TIMING_FILE");
#else
            const char* path = std::getenv("OPENMW_TIMING_FILE");
#endif
            if (path && *path)
            {
                stream.open(std::filesystem::path(path));
                if (stream)
                {
                    stream << "thread,frame,category,source,callback,calls,total_us,self_us,max_us,first_us,last_us\n";
                    stream << std::setprecision(12);
                    stream.flush();
                }
            }
        }
    };
    inline Output& output() { static Output value; return value; }
    inline bool enabled() { static const bool value = output().stream.is_open(); return value; }
    using Key = std::tuple<std::string, std::string, std::string>;
    struct Totals
    {
        unsigned long long calls = 0;
        double total = 0, self = 0, maximum = 0, first = 0, last = 0;
        void add(double elapsed, double exclusive, double begin, double end)
        {
            if (calls++ == 0) first = begin;
            total += elapsed;
            self += exclusive;
            maximum = std::max(maximum, elapsed);
            last = end;
        }
    };
    class Scope;
    struct ThreadState
    {
        unsigned thread = output().nextThread.fetch_add(1);
        long long frame = -1;
        std::string_view callback = "call";
        Scope* active = nullptr;
        std::map<Key, Totals> rows;
        bool failed = false;
        void flush() noexcept
        {
            if (rows.empty()) return;
            try
            {
                const auto begin = Clock::now();
                std::ostringstream text;
                text << std::setprecision(12);
                for (const auto& [key, t] : rows)
                    text << thread << ',' << frame << ',' << quote(std::get<0>(key)) << ','
                         << quote(std::get<1>(key)) << ',' << quote(std::get<2>(key)) << ',' << t.calls
                         << ',' << t.total << ',' << t.self << ',' << t.maximum << ',' << t.first << ',' << t.last << '\n';
                rows.clear();
                auto& sink = output();
                std::lock_guard<std::mutex> lock(sink.mutex);
                sink.stream << text.str();
                sink.stream.flush();
                const auto end = Clock::now();
                const double elapsed = microseconds(end - begin);
                sink.stream << thread << ',' << frame << ",recorder,output,flush,1," << elapsed << ',' << elapsed
                            << ',' << elapsed << ',' << microseconds(begin - sink.epoch) << ','
                            << microseconds(end - sink.epoch) << '\n';
                if (!sink.stream) failed = true;
            }
            catch (...) { rows.clear(); failed = true; }
        }
        ~ThreadState() { flush(); }
    };
    inline ThreadState& state() { static thread_local ThreadState value; return value; }
    inline void setFrame(unsigned frame)
    {
        if (!enabled()) return;
        auto& s = state();
        if (s.frame != frame) { s.flush(); s.frame = frame; }
    }
    class Callback
    {
        std::string_view previous;
        bool recording;
    public:
        explicit Callback(std::string_view name) : recording(enabled())
        {
            if (recording) { previous = state().callback; state().callback = name; }
        }
        ~Callback() { if (recording) state().callback = previous; }
    };
    class Scope
    {
        ThreadState* owner = nullptr;
        Scope* parent = nullptr;
        Totals* totals = nullptr;
        Clock::time_point start;
        double children = 0;
        std::string source;
    public:
        Scope(std::string_view category, std::string_view name, std::string_view callback = {}) noexcept
        {
            if (!enabled()) return;
            auto& s = state();
            if (s.failed) return;
            try
            {
                // Copy the identity: callers may destroy script/container metadata during execution.
                source = name;
                totals = &s.rows[Key{std::string(category), source,
                    std::string(callback.empty() ? s.callback : callback)}];
                owner = &s;
                parent = s.active;
                s.active = this;
                start = Clock::now();
            }
            catch (...) { s.failed = true; }
        }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        ~Scope()
        {
            if (!owner) return;
            const auto end = Clock::now();
            const double elapsed = microseconds(end - start);
            totals->add(elapsed, std::max(0.0, elapsed - children),
                microseconds(start - output().epoch), microseconds(end - output().epoch));
            owner->active = parent;
            if (parent) parent->children += elapsed;
        }
        static std::string currentSource()
        {
            if (!enabled() || !state().active) return {};
            return state().active->source;
        }
    };
}
#endif
