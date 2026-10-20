#include <components/diagnostics/actorfailure.hpp>
#include <cassert>
#include <string>
int main()
{
    for (int i = 0; i < 12; ++i)
    {
        const auto result = Diagnostics::actorFailureDetails("getCreatureStats", "object 123 (Static, example)");
        assert(result.find("operation=getCreatureStats; object 123 (Static, example)") != std::string::npos);
#ifdef _WIN32
        assert((result.find("; stack=") != std::string::npos) == (i < 8));
#endif
    }
}
