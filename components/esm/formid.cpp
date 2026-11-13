#include "formid.hpp"

#include <charconv>
#include <iterator>

std::string ESM::FormId::toString(std::string_view prefix) const
{
    // The longest generated suffix is '@0x' plus 16 hexadecimal digits.
    char suffix[19];
    char* buf = suffix;
    uint64_t value;
    if (hasContentFile())
    {
        if ((mIndex & 0xff000000) != 0)
            throw std::invalid_argument("Invalid FormId index value: " + std::to_string(mIndex));
        value = mIndex | (static_cast<uint64_t>(mContentFile) << 24);
    }
    else
    {
        *(buf++) = '@';
        value = mIndex | (static_cast<uint64_t>(-static_cast<int64_t>(mContentFile) - 1) << 32);
    }
    *(buf++) = '0';
    *(buf++) = 'x';
    const auto r = std::to_chars(buf, std::end(suffix), value, 16);
    if (r.ec != std::errc())
        throw std::system_error(std::make_error_code(r.ec), "ESM::FormId::toString failed");
    std::string result;
    result.reserve(prefix.size() + static_cast<size_t>(r.ptr - suffix));
    result.append(prefix);
    result.append(suffix, r.ptr);
    return result;
}

uint32_t ESM::FormId::toUint32() const
{
    if (isSet() && !hasContentFile())
        throw std::runtime_error("Generated FormId can not be converted to 32bit format");
    if (mContentFile > 0xfe)
        throw std::runtime_error("FormId with mContentFile > 0xFE can not be converted to 32bit format");
    return (mIndex & 0xffffff) | ((hasContentFile() ? mContentFile : 0xff) << 24);
}
