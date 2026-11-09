#include "traveltooltipdata.hpp"

#include <algorithm>
#include <charconv>
#include <istream>
#include <stdexcept>

namespace MWGui::TravelTooltips
{
    namespace
    {
        constexpr std::size_t maxBytes = 4 * 1024 * 1024;
        constexpr std::size_t maxLineBytes = 65536;
        constexpr std::size_t maxRecords = 10000;

        char lower(char ch)
        {
            return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch;
        }

        bool startsWith(std::string_view value, std::string_view prefix)
        {
            return value.size() >= prefix.size()
                && std::equal(prefix.begin(), prefix.end(), value.begin(),
                    [](char a, char b) { return lower(a) == lower(b); });
        }

        bool contains(std::string_view value, std::string_view part)
        {
            return std::search(value.begin(), value.end(), part.begin(), part.end(),
                       [](char a, char b) { return lower(a) == lower(b); })
                != value.end();
        }

        std::vector<std::string> fields(std::string_view line)
        {
            std::vector<std::string> result(1);
            bool escaped = false;
            for (char ch : line)
            {
                if (escaped)
                {
                    switch (ch)
                    {
                        case '\\': result.back() += '\\'; break;
                        case 't': result.back() += '\t'; break;
                        case 'r': result.back() += '\r'; break;
                        case 'n': result.back() += '\n'; break;
                        default: throw std::runtime_error("invalid escape");
                    }
                    escaped = false;
                }
                else if (ch == '\\')
                    escaped = true;
                else if (ch == '\t')
                    result.emplace_back();
                else
                    result.back() += ch;
            }
            if (escaped)
                throw std::runtime_error("unfinished escape");
            return result;
        }

        int number(std::string_view value, int min, int max)
        {
            int result;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
            if (error != std::errc{} || end != value.data() + value.size() || result < min || result > max)
                throw std::runtime_error("invalid number");
            return result;
        }

        void validateMatch(std::string_view value)
        {
            if (value.empty() || value.size() > 1024 || value.find_first_of("\r\n\t") != std::string_view::npos)
                throw std::runtime_error("invalid destination match");
        }

        std::string texturePath(std::string value)
        {
            if (value.size() > 2048)
                throw std::runtime_error("texture path is too long");
            for (char& ch : value)
            {
                if (static_cast<unsigned char>(ch) < 32 || ch == ':')
                    throw std::runtime_error("invalid texture path");
                ch = ch == '\\' ? '/' : lower(ch);
            }
            if (!value.starts_with("textures/") || value.back() == '/')
                throw std::runtime_error("texture path must be relative to textures/");
            std::size_t begin = 0;
            while (begin < value.size())
            {
                const auto end = value.find('/', begin);
                const auto component = std::string_view(value).substr(begin, end - begin);
                if (component.empty() || component == "." || component == "..")
                    throw std::runtime_error("invalid texture path component");
                if (end == std::string::npos)
                    break;
                begin = end + 1;
            }
            return value;
        }

        const Image* imageInPack(const Data& data, std::string_view destination, int pack,
            std::uint64_t seed, const ImageAvailable& available)
        {
            std::size_t longest = 0;
            std::vector<const Image*> candidates;
            for (const Image& image : data.mImages)
            {
                if (image.mPack != pack || image.mPrefix.size() < longest
                    || !startsWith(destination, image.mPrefix) || !available(image.mPath))
                    continue;
                if (image.mPrefix.size() > longest)
                {
                    candidates.clear();
                    longest = image.mPrefix.size();
                }
                candidates.push_back(&image);
            }
            if (candidates.empty())
                return nullptr;
            // Include the destination in the seed so towns do not all choose the same variant.
            for (char ch : destination)
                seed = (seed ^ static_cast<unsigned char>(lower(ch))) * 1099511628211ull;
            return candidates[seed % candidates.size()];
        }
    }

    Data readData(std::istream& stream)
    {
        Data data;
        bool version = false;
        std::size_t bytes = 0;
        std::size_t lineNumber = 0;
        std::size_t records = 0;
        std::string line;
        while (true)
        {
            line.clear();
            char ch;
            while (stream.get(ch))
            {
                if (++bytes > maxBytes || line.size() >= maxLineBytes)
                    throw std::runtime_error("Travel tooltip manifest exceeds size limit");
                if (ch == '\0')
                    throw std::runtime_error("Travel tooltip manifest contains a NUL byte");
                if (ch == '\n')
                    break;
                line += ch;
            }
            if (line.empty() && !stream)
                break;
            ++lineNumber;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty() || line.front() == '#')
                continue;
            if (++records > maxRecords)
                throw std::runtime_error("Travel tooltip manifest has too many records");
            try
            {
                auto row = fields(line);
                if (!version)
                {
                    if (row.size() != 2 || row[0] != "version" || row[1] != "1")
                        throw std::runtime_error("expected version 1 header");
                    version = true;
                }
                else if ((row[0] == "description" || row[0] == "gondolier") && row.size() == 3)
                {
                    validateMatch(row[1]);
                    auto& descriptions = row[0] == "description" ? data.mDescriptions : data.mGondoliers;
                    descriptions.push_back({ std::move(row[1]), std::move(row[2]) });
                }
                else if (row[0] == "image" && row.size() == 6)
                {
                    validateMatch(row[2]);
                    data.mImages.push_back({ number(row[1], 0, 2), std::move(row[2]), texturePath(std::move(row[3])),
                        number(row[4], 1, 16384), number(row[5], 1, 16384) });
                }
                else if (row[0] == "overview" && row.size() == 5)
                {
                    const int index = number(row[1], 0, 2);
                    if (std::any_of(data.mOverviews.begin(), data.mOverviews.end(),
                            [index](const Image& value) { return value.mPack == index; }))
                        throw std::runtime_error("duplicate overview index");
                    data.mOverviews.push_back({ index, {}, texturePath(std::move(row[2])),
                        number(row[3], 1, 16384), number(row[4], 1, 16384) });
                }
                else
                    throw std::runtime_error("unknown record or wrong field count");
            }
            catch (const std::exception& error)
            {
                throw std::runtime_error("Travel tooltip manifest line " + std::to_string(lineNumber) + ": " + error.what());
            }
        }
        if (stream.bad())
            throw std::runtime_error("Cannot read travel tooltip manifest");
        if (!version)
            throw std::runtime_error("Travel tooltip manifest is empty");
        return data;
    }

    const Image* destinationImage(const Data& data, std::string_view destination, int pack, bool fallback,
        std::uint64_t seed, const ImageAvailable& available)
    {
        if (pack < 0 || pack > 2)
            return nullptr;
        if (const Image* image = imageInPack(data, destination, pack, seed, available))
            return image;
        return fallback && pack != 0 ? imageInPack(data, destination, 0, seed, available) : nullptr;
    }

    const Image* overviewImage(const Data& data, int index, const ImageAvailable& available)
    {
        for (const Image& image : data.mOverviews)
            if (image.mPack == index && available(image.mPath))
                return &image;
        return nullptr;
    }

    std::string_view destinationDescription(const Data& data, std::string_view destination, bool gondolier)
    {
        const Description* result = nullptr;
        for (const Description& value : gondolier ? data.mGondoliers : data.mDescriptions)
        {
            if ((!result || value.mMatch.size() > result->mMatch.size())
                && (gondolier ? contains(destination, value.mMatch) : startsWith(destination, value.mMatch)))
                result = &value;
        }
        return result ? std::string_view(result->mText) : std::string_view{};
    }

    bool hasVvardenfellOverview(std::string_view providerId)
    {
        // The supplied overview maps cover Vvardenfell, not mainland or province routes.
        return !startsWith(providerId, "tr_") && !startsWith(providerId, "pc_") && !startsWith(providerId, "sky_");
    }
}
