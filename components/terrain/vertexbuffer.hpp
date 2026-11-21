#ifndef OPENMW_COMPONENTS_TERRAIN_VERTEXBUFFER_H
#define OPENMW_COMPONENTS_TERRAIN_VERTEXBUFFER_H

#include <osg/Array>
#include <osg/Geometry>

namespace Terrain
{
    // Arrays, bindings and VBO membership are immutable after publication. Each
    // stitching variant owns its indices and ray tree, but can use these arrays.
    struct VertexBuffer
    {
        osg::ref_ptr<osg::Vec3Array> mPositions = new osg::Vec3Array;
        osg::ref_ptr<osg::Vec3Array> mNormals = new osg::Vec3Array;
        osg::ref_ptr<osg::Vec4ubArray> mColours = new osg::Vec4ubArray;

        void seal()
        {
            mPositions->setBinding(osg::Array::BIND_PER_VERTEX);
            mNormals->setBinding(osg::Array::BIND_PER_VERTEX);
            mColours->setBinding(osg::Array::BIND_PER_VERTEX);
            mColours->setNormalize(true);
            osg::ref_ptr<osg::VertexBufferObject> vbo = new osg::VertexBufferObject;
            mPositions->setVertexBufferObject(vbo);
            mNormals->setVertexBufferObject(vbo);
            mColours->setVertexBufferObject(vbo);
        }

        void attach(osg::Geometry& geometry) const
        {
            // Explicit binding arguments would write into already-published arrays.
            geometry.setVertexArray(mPositions);
            geometry.setNormalArray(mNormals);
            geometry.setColorArray(mColours);
        }

        void releaseGLObjects(osg::State* state) const
        {
            mPositions->releaseGLObjects(state);
            mNormals->releaseGLObjects(state);
            mColours->releaseGLObjects(state);
        }
    };
}

#endif
