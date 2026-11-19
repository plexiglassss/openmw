#include "locals.hpp"

#include <algorithm>
#include <iterator>
#include <numeric>
#include <utility>
#include <ostream>
#include <stdexcept>

#include <components/misc/strings/lower.hpp>

namespace Compiler
{
    namespace
    {
        constexpr std::size_t indexedVariableThreshold = 32;
    }

    const std::vector<int>& Locals::getLookupIndex(char type) const
    {
        switch (type)
        {
            case 's': return mShortIndex;
            case 'l': return mLongIndex;
            case 'f': return mFloatIndex;
        }
        throw std::logic_error("Unknown variable type");
    }

    std::vector<int>& Locals::getLookupIndex(char type)
    {
        return const_cast<std::vector<int>&>(std::as_const(*this).getLookupIndex(type));
    }

    const std::vector<std::string>& Locals::get(char type) const
    {
        switch (type)
        {
            case 's':
                return mShorts;
            case 'l':
                return mLongs;
            case 'f':
                return mFloats;
        }

        throw std::logic_error("Unknown variable type");
    }

    int Locals::searchIndex(char type, std::string_view name) const
    {
        const std::vector<std::string>& collection = get(type);

        if (collection.size() >= indexedVariableThreshold)
        {
            const auto& index = getLookupIndex(type);
            const auto found = std::lower_bound(index.begin(), index.end(), name,
                [&](int i, std::string_view value) { return collection[i] < value; });
            return found != index.end() && collection[*found] == name ? *found : -1;
        }

        auto iter = std::find(collection.begin(), collection.end(), name);

        if (iter == collection.end())
            return -1;

        return static_cast<int>(iter - collection.begin());
    }

    bool Locals::search(char type, std::string_view name) const
    {
        return searchIndex(type, name) != -1;
    }

    std::vector<std::string>& Locals::get(char type)
    {
        switch (type)
        {
            case 's':
                return mShorts;
            case 'l':
                return mLongs;
            case 'f':
                return mFloats;
        }

        throw std::logic_error("Unknown variable type");
    }

    char Locals::getType(std::string_view name) const
    {
        if (search('s', name))
            return 's';

        if (search('l', name))
            return 'l';

        if (search('f', name))
            return 'f';

        return ' ';
    }

    int Locals::getIndex(std::string_view name) const
    {
        int index = searchIndex('s', name);

        if (index != -1)
            return index;

        index = searchIndex('l', name);

        if (index != -1)
            return index;

        return searchIndex('f', name);
    }

    void Locals::write(std::ostream& localFile) const
    {
        localFile << get('s').size() << ' ' << get('l').size() << ' ' << get('f').size() << std::endl;

        std::copy(get('s').begin(), get('s').end(), std::ostream_iterator<std::string>(localFile, " "));
        std::copy(get('l').begin(), get('l').end(), std::ostream_iterator<std::string>(localFile, " "));
        std::copy(get('f').begin(), get('f').end(), std::ostream_iterator<std::string>(localFile, " "));
    }

    void Locals::declare(char type, std::string_view name)
    {
        auto& collection = get(type);
        auto& index = getLookupIndex(type);
        // Reserve first so an allocation failure cannot leave an incomplete index.
        if (collection.size() + 1 >= indexedVariableThreshold && index.capacity() < collection.size() + 1)
            index.reserve(std::max(collection.size() + 1, index.capacity() * 2));
        collection.push_back(Misc::StringUtils::lowerCase(name));
        if (collection.size() == indexedVariableThreshold)
        {
            index.resize(collection.size());
            std::iota(index.begin(), index.end(), 0);
            std::sort(index.begin(), index.end(), [&](int a, int b) {
                return collection[a] != collection[b] ? collection[a] < collection[b] : a < b;
            });
        }
        else if (collection.size() > indexedVariableThreshold)
        {
            // Insert after equal names: searchIndex must retain first-declaration semantics.
            const auto position = std::upper_bound(index.begin(), index.end(), std::string_view(collection.back()),
                [&](std::string_view value, int i) { return value < collection[i]; });
            index.insert(position, static_cast<int>(collection.size() - 1));
        }
    }

    void Locals::clear()
    {
        get('s').clear();
        get('l').clear();
        get('f').clear();
        mShortIndex.clear();
        mLongIndex.clear();
        mFloatIndex.clear();
    }
}
