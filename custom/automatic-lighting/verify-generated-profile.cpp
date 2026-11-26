// Exercise a real generator output with the production OpenMW reader.
#include <apps/openmw/mwrender/locationrenderingprofiles.hpp>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace
{
    void require(bool condition, std::string_view message)
    {
        if (!condition)
            throw std::runtime_error(std::string(message));
    }
}

int main(int argc, char** argv)
{
    try
    {
        require(argc == 2, "Usage: verify-generated-profile location-rendering.yaml");
        MWRender::LocationRenderingProfiles profiles;
        profiles.load(std::filesystem::path(argv[1]), {});
        require(profiles.profiles().size() == 1, "Expected one generated automatic profile");
        const auto* profile = profiles.find("A newly installed, previously unknown interior", false, true);
        require(profile != nullptr, "The generated condition did not match negative lighting");
        require(profile->mId == "automatic-negative-light-interiors", "Unexpected generated rule identity");
        require(profile->mContainsNegativeLight && profile->mInteriors.empty(), "Expected a generic condition without a cell list");
        require(profile->mRequiredContent.empty(), "The generic rule must not depend on particular mod names");
        require(profile->mShaders.mClampLighting && !profile->mShaders.mForcePerPixelLighting
                && !profile->mShaders.mAutoUseObjectNormalMaps && !profile->mShaders.mAutoUseObjectSpecularMaps,
            "The generated shader values do not match the agreed default policy");
        require(profiles.find("A newly installed, previously unknown interior", false, false) == nullptr,
            "An ordinary interior incorrectly selected the automatic profile");
        require(profiles.find("An exterior", true, true) == nullptr,
            "The interior rule incorrectly selected an exterior");
        std::cout << "PASS: generated shared YAML parsed by production OpenMW; generic condition, shader policy, "
                     "unmatched fallback and exterior boundary verified\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
