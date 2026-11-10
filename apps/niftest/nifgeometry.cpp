// Temporary audit utility: parse NIFs with OpenMW's own reader and export static geometry to OBJ.

#include <components/files/constrainedfilestream.hpp>
#include <components/files/conversion.hpp>
#include <components/nif/data.hpp>
#include <components/nif/niffile.hpp>
#include <components/nif/node.hpp>

#include <osg/Matrixf>
#include <osg/Vec3f>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    std::string safeName(const std::string& value, std::size_t fallback)
    {
        std::string out;
        out.reserve(value.size());
        for (unsigned char c : value)
            out.push_back(std::isalnum(c) || c == '_' || c == '-' ? static_cast<char>(c) : '_');
        if (out.empty())
            out = "geometry_" + std::to_string(fallback);
        return out;
    }

    class ObjWriter
    {
    public:
        explicit ObjWriter(const std::filesystem::path& path)
            : mOut(path)
        {
            if (!mOut)
                throw std::runtime_error("Failed to open output: " + Files::pathToUnicodeString(path));
            mOut.setf(std::ios::fmtflags(0), std::ios::floatfield);
            mOut.precision(9);
        }

        void visit(const Nif::NiAVObject& object, const osg::Matrixf& parent)
        {
            const osg::Matrixf world = object.mTransform.toMatrix() * parent;

            if (const auto* geometry = dynamic_cast<const Nif::NiGeometry*>(&object))
                writeGeometry(*geometry, world);

            if (const auto* node = dynamic_cast<const Nif::NiNode*>(&object))
            {
                for (const auto& child : node->mChildren)
                {
                    if (!child.empty())
                        visit(*child.getPtr(), world);
                }
            }
        }

        std::size_t geometryCount() const { return mGeometryCount; }
        std::size_t vertexCount() const { return mVertexCount; }
        std::size_t triangleCount() const { return mTriangleCount; }

    private:
        void writeGeometry(const Nif::NiGeometry& geometry, const osg::Matrixf& world)
        {
            if (geometry.mData.empty())
                return;
            const Nif::NiGeometryData* data = geometry.mData.getPtr();
            if (data == nullptr || data->mVertices.empty())
                return;

            ++mGeometryCount;
            const std::size_t base = mNextVertex;
            mOut << "\no " << safeName(geometry.mName, mGeometryCount) << "\n";
            mOut << "# original_name " << geometry.mName << "\n";
            mOut << "# flags " << geometry.mFlags << "\n";

            for (const osg::Vec3f& vertex : data->mVertices)
            {
                const osg::Vec3f transformed = vertex * world;
                mOut << "v " << transformed.x() << ' ' << transformed.y() << ' ' << transformed.z() << "\n";
            }
            mNextVertex += data->mVertices.size();
            mVertexCount += data->mVertices.size();

            if (const auto* triangles = dynamic_cast<const Nif::NiTriShapeData*>(data))
            {
                for (std::size_t i = 0; i + 2 < triangles->mTriangles.size(); i += 3)
                {
                    const unsigned short a = triangles->mTriangles[i];
                    const unsigned short b = triangles->mTriangles[i + 1];
                    const unsigned short c = triangles->mTriangles[i + 2];
                    if (a == b || b == c || a == c)
                        continue;
                    mOut << "f " << (base + a) << ' ' << (base + b) << ' ' << (base + c) << "\n";
                    ++mTriangleCount;
                }
            }
            else if (const auto* strips = dynamic_cast<const Nif::NiTriStripsData*>(data))
            {
                for (const auto& strip : strips->mStrips)
                {
                    for (std::size_t i = 2; i < strip.size(); ++i)
                    {
                        unsigned short a = strip[i - 2];
                        unsigned short b = strip[i - 1];
                        unsigned short c = strip[i];
                        if (i % 2 != 0)
                            std::swap(a, b);
                        if (a == b || b == c || a == c)
                            continue;
                        mOut << "f " << (base + a) << ' ' << (base + b) << ' ' << (base + c) << "\n";
                        ++mTriangleCount;
                    }
                }
            }
        }

        std::ofstream mOut;
        std::size_t mNextVertex{ 1 };
        std::size_t mGeometryCount{ 0 };
        std::size_t mVertexCount{ 0 };
        std::size_t mTriangleCount{ 0 };
    };
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "Usage: nifgeometry <input.nif> <output.obj>\n";
        return 2;
    }

    try
    {
        const std::filesystem::path input = Files::pathFromUnicodeString(argv[1]);
        const std::filesystem::path output = Files::pathFromUnicodeString(argv[2]);

        Nif::NIFFile file(VFS::Path::Normalized(Files::pathToUnicodeString(input)));
        Nif::Reader reader(file, nullptr);
        reader.parse(Files::openConstrainedFileStream(input));

        ObjWriter writer(output);
        const osg::Matrixf identity = osg::Matrixf::identity();
        for (Nif::Record* root : file.mRoots)
        {
            if (const auto* av = dynamic_cast<const Nif::NiAVObject*>(root))
                writer.visit(*av, identity);
        }

        std::cout << Files::pathToUnicodeString(input) << '\t' << writer.geometryCount() << '\t'
                  << writer.vertexCount() << '\t' << writer.triangleCount() << '\n';
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
