//
//  AnimDebugDraw.cpp
//
//  Copyright 2015 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "animdebugdraw_vert.h"
#include "animdebugdraw_frag.h"
#include <gpu/Batch.h>
#include "AbstractViewStateInterface.h"
#include "RenderUtilsLogging.h"
#include "GLMHelpers.h"
#include "DebugDraw.h"

#include "AnimDebugDraw.h"

struct Vertex {
    glm::vec3 pos;
    uint32_t rgba;
};

class AnimDebugDrawData {
public:
    typedef render::Payload<AnimDebugDrawData> Payload;
    typedef Payload::DataPointer Pointer;

    AnimDebugDrawData() {

        _vertexFormat = std::make_shared<gpu::Stream::Format>();
        _vertexBuffer = std::make_shared<gpu::Buffer>();
        _indexBuffer = std::make_shared<gpu::Buffer>();

        _vertexFormat->setAttribute(gpu::Stream::POSITION, 0, gpu::Element::VEC3F_XYZ, 0);
        _vertexFormat->setAttribute(gpu::Stream::COLOR, 0, gpu::Element::COLOR_RGBA_32, offsetof(Vertex, rgba));
    }

    void render(RenderArgs* args) {
        auto& batch = *args->_batch;
        batch.setPipeline(_pipeline);
        auto transform = Transform{};
        batch.setModelTransform(transform);

        batch.setInputFormat(_vertexFormat);
        batch.setInputBuffer(0, _vertexBuffer, 0, sizeof(Vertex));
        batch.setIndexBuffer(gpu::UINT16, _indexBuffer, 0);

        auto numIndices = _indexBuffer->getSize() / sizeof(uint16_t);
        batch.drawIndexed(gpu::LINES, (int)numIndices);
    }

    gpu::PipelinePointer _pipeline;
    render::ItemID _item;
    gpu::Stream::FormatPointer _vertexFormat;
    gpu::BufferPointer _vertexBuffer;
    gpu::BufferPointer _indexBuffer;

    render::Item::Bound _bound { glm::vec3(-16384.0f), 32768.0f };
    bool _isVisible { true };
};

typedef render::Payload<AnimDebugDrawData> AnimDebugDrawPayload;

namespace render {
    template <> const ItemKey payloadGetKey(const AnimDebugDrawData::Pointer& data) { return (data->_isVisible ? ItemKey::Builder::opaqueShape() : ItemKey::Builder::opaqueShape().withInvisible()); }
    template <> const Item::Bound payloadGetBound(const AnimDebugDrawData::Pointer& data) { return data->_bound; }
    template <> void payloadRender(const AnimDebugDrawData::Pointer& data, RenderArgs* args) {
        data->render(args);
    }
}

AnimDebugDraw& AnimDebugDraw::getInstance() {
    static AnimDebugDraw instance;
    return instance;
}

static uint32_t toRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return ((uint32_t)r | (uint32_t)g << 8 | (uint32_t)b << 16 | (uint32_t)a << 24);
}

static uint32_t toRGBA(const glm::vec4& v) {
    return toRGBA(static_cast<uint8_t>(v.r * 255.0f),
                  static_cast<uint8_t>(v.g * 255.0f),
                  static_cast<uint8_t>(v.b * 255.0f),
                  static_cast<uint8_t>(v.a * 255.0f));
}

gpu::PipelinePointer AnimDebugDraw::_pipeline;

AnimDebugDraw::AnimDebugDraw() :
    _itemID(0) {

    auto state = std::make_shared<gpu::State>();
    state->setCullMode(gpu::State::CULL_BACK);
    state->setDepthTest(true, true, gpu::LESS_EQUAL);
    state->setBlendFunction(false, gpu::State::SRC_ALPHA, gpu::State::BLEND_OP_ADD,
                            gpu::State::INV_SRC_ALPHA, gpu::State::FACTOR_ALPHA,
                            gpu::State::BLEND_OP_ADD, gpu::State::ONE);
    auto vertShader = gpu::Shader::createVertex(std::string(animdebugdraw_vert));
    auto fragShader = gpu::Shader::createPixel(std::string(animdebugdraw_frag));
    auto program = gpu::Shader::createProgram(vertShader, fragShader);
    _pipeline = gpu::Pipeline::create(program, state);

    _animDebugDrawData = std::make_shared<AnimDebugDrawData>();
    _animDebugDrawPayload = std::make_shared<AnimDebugDrawPayload>(_animDebugDrawData);

    _animDebugDrawData->_pipeline = _pipeline;

    render::ScenePointer scene = AbstractViewStateInterface::instance()->getMain3DScene();
    if (scene) {
        _itemID = scene->allocateID();
        render::PendingChanges pendingChanges;
        pendingChanges.resetItem(_itemID, _animDebugDrawPayload);
        scene->enqueuePendingChanges(pendingChanges);
    }

    // HACK: add red, green and blue axis at (1,1,1)
    _animDebugDrawData->_vertexBuffer->resize(sizeof(Vertex) * 6);
    Vertex* data = (Vertex*)_animDebugDrawData->_vertexBuffer->editData();

    data[0].pos = glm::vec3(1.0, 1.0f, 1.0f);
    data[0].rgba = toRGBA(255, 0, 0, 255);
    data[1].pos = glm::vec3(2.0, 1.0f, 1.0f);
    data[1].rgba = toRGBA(255, 0, 0, 255);

    data[2].pos = glm::vec3(1.0, 1.0f, 1.0f);
    data[2].rgba = toRGBA(0, 255, 0, 255);
    data[3].pos = glm::vec3(1.0, 2.0f, 1.0f);
    data[3].rgba = toRGBA(0, 255, 0, 255);

    data[4].pos = glm::vec3(1.0, 1.0f, 1.0f);
    data[4].rgba = toRGBA(0, 0, 255, 255);
    data[5].pos = glm::vec3(1.0, 1.0f, 2.0f);
    data[5].rgba = toRGBA(0, 0, 255, 255);

    _animDebugDrawData->_indexBuffer->resize(sizeof(uint16_t) * 6);
    uint16_t* indices = (uint16_t*)_animDebugDrawData->_indexBuffer->editData();
    for (int i = 0; i < 6; i++) {
        indices[i] = i;
    }

}

AnimDebugDraw::~AnimDebugDraw() {
}

void AnimDebugDraw::shutdown() {
    // remove renderItem from main 3d scene.
    render::ScenePointer scene = AbstractViewStateInterface::instance()->getMain3DScene();
    if (scene && _itemID) {
        render::PendingChanges pendingChanges;
        pendingChanges.removeItem(_itemID);
        scene->enqueuePendingChanges(pendingChanges);
    }
}

void AnimDebugDraw::addAbsolutePoses(const std::string& key, AnimSkeleton::ConstPointer skeleton, const AnimPoseVec& poses, const AnimPose& rootPose, const glm::vec4& color) {
    _absolutePoses[key] = PosesInfo(skeleton, poses, rootPose, color);
}

void AnimDebugDraw::removeAbsolutePoses(const std::string& key) {
    _absolutePoses.erase(key);
}

static const uint32_t red = toRGBA(255, 0, 0, 255);
static const uint32_t green = toRGBA(0, 255, 0, 255);
static const uint32_t blue = toRGBA(0, 0, 255, 255);

const int NUM_CIRCLE_SLICES = 24;

static void addBone(const AnimPose& rootPose, const AnimPose& pose, float radius, Vertex*& v) {

    const float XYZ_AXIS_LENGTH = radius * 4.0f;

    AnimPose finalPose = rootPose * pose;
    glm::vec3 base = rootPose * pose.trans;

    glm::vec3 xRing[NUM_CIRCLE_SLICES + 1];  // one extra for last index.
    glm::vec3 yRing[NUM_CIRCLE_SLICES + 1];
    glm::vec3 zRing[NUM_CIRCLE_SLICES + 1];
    const float dTheta = (2.0f * (float)M_PI) / NUM_CIRCLE_SLICES;
    for (int i = 0; i < NUM_CIRCLE_SLICES + 1; i++) {
        float rCosTheta = radius * cosf(dTheta * i);
        float rSinTheta = radius * sinf(dTheta * i);
        xRing[i] = finalPose * glm::vec3(0.0f, rCosTheta, rSinTheta);
        yRing[i] = finalPose * glm::vec3(rCosTheta, 0.0f, rSinTheta);
        zRing[i] = finalPose * glm::vec3(rCosTheta, rSinTheta, 0.0f);
    }

    // x-axis
    v->pos = base;
    v->rgba = red;
    v++;
    v->pos = finalPose * glm::vec3(XYZ_AXIS_LENGTH, 0.0f, 0.0f);
    v->rgba = red;
    v++;

    // x-ring
    for (int i = 0; i < NUM_CIRCLE_SLICES; i++) {
        v->pos = xRing[i];
        v->rgba = red;
        v++;
        v->pos = xRing[i + 1];
        v->rgba = red;
        v++;
    }

    // y-axis
    v->pos = base;
    v->rgba = green;
    v++;
    v->pos = finalPose * glm::vec3(0.0f, XYZ_AXIS_LENGTH, 0.0f);
    v->rgba = green;
    v++;

    // y-ring
    for (int i = 0; i < NUM_CIRCLE_SLICES; i++) {
        v->pos = yRing[i];
        v->rgba = green;
        v++;
        v->pos = yRing[i + 1];
        v->rgba = green;
        v++;
    }

    // z-axis
    v->pos = base;
    v->rgba = blue;
    v++;
    v->pos = finalPose * glm::vec3(0.0f, 0.0f, XYZ_AXIS_LENGTH);
    v->rgba = blue;
    v++;

    // z-ring
    for (int i = 0; i < NUM_CIRCLE_SLICES; i++) {
        v->pos = zRing[i];
        v->rgba = blue;
        v++;
        v->pos = zRing[i + 1];
        v->rgba = blue;
        v++;
    }
}

static void addLink(const AnimPose& rootPose, const AnimPose& pose, const AnimPose& parentPose,
                    float radius, const glm::vec4& colorVec, Vertex*& v) {

    uint32_t color = toRGBA(colorVec);

    AnimPose pose0 = rootPose * parentPose;
    AnimPose pose1 = rootPose * pose;

    glm::vec3 boneAxisWorld = glm::normalize(pose1.trans - pose0.trans);
    glm::vec3 boneAxis0 = glm::normalize(pose0.inverse().xformVector(boneAxisWorld));
    glm::vec3 boneAxis1 = glm::normalize(pose1.inverse().xformVector(boneAxisWorld));

    glm::vec3 boneBase = pose0 * (boneAxis0 * radius);
    glm::vec3 boneTip = pose1 * (boneAxis1 * -radius);

    const int NUM_BASE_CORNERS = 4;

    // make sure there's room between the two bones to draw a nice bone link.
    if (glm::dot(boneTip - pose0.trans, boneAxisWorld) > glm::dot(boneBase - pose0.trans, boneAxisWorld)) {

        // there is room, so lets draw a nice bone

        glm::vec3 uAxis, vAxis, wAxis;
        generateBasisVectors(boneAxis0, glm::vec3(1, 0, 0), uAxis, vAxis, wAxis);

        glm::vec3 boneBaseCorners[NUM_BASE_CORNERS];
        boneBaseCorners[0] = pose0 * ((uAxis * radius) + (vAxis * radius) + (wAxis * radius));
        boneBaseCorners[1] = pose0 * ((uAxis * radius) - (vAxis * radius) + (wAxis * radius));
        boneBaseCorners[2] = pose0 * ((uAxis * radius) - (vAxis * radius) - (wAxis * radius));
        boneBaseCorners[3] = pose0 * ((uAxis * radius) + (vAxis * radius) - (wAxis * radius));

        for (int i = 0; i < NUM_BASE_CORNERS; i++) {
            v->pos = boneBaseCorners[i];
            v->rgba = color;
            v++;
            v->pos = boneBaseCorners[(i + 1) % NUM_BASE_CORNERS];
            v->rgba = color;
            v++;
        }

        for (int i = 0; i < NUM_BASE_CORNERS; i++) {
            v->pos = boneBaseCorners[i];
            v->rgba = color;
            v++;
            v->pos = boneTip;
            v->rgba = color;
            v++;
        }
    } else {
        // There's no room between the bones to draw the link.
        // just draw a line between the two bone centers.
        // We add the same line multiple times, so the vertex count is correct.
        for (int i = 0; i < NUM_BASE_CORNERS * 2; i++) {
            v->pos = pose0.trans;
            v->rgba = color;
            v++;
            v->pos = pose1.trans;
            v->rgba = color;
            v++;
        }
    }
}

void AnimDebugDraw::update() {

    render::ScenePointer scene = AbstractViewStateInterface::instance()->getMain3DScene();
    if (!scene) {
        return;
    }

    render::PendingChanges pendingChanges;
    pendingChanges.updateItem<AnimDebugDrawData>(_itemID, [&](AnimDebugDrawData& data) {

        const size_t VERTICES_PER_BONE = (6 + (NUM_CIRCLE_SLICES * 2) * 3);
        const size_t VERTICES_PER_LINK = 8 * 2;

        const float BONE_RADIUS = 0.01f; // 1 cm
        const float POSE_RADIUS = 0.1f; // 10 cm

        // figure out how many verts we will need.
        int numVerts = 0;

        for (auto& iter : _absolutePoses) {
            AnimSkeleton::ConstPointer& skeleton = std::get<0>(iter.second);
            numVerts += skeleton->getNumJoints() * VERTICES_PER_BONE;
            for (auto i = 0; i < skeleton->getNumJoints(); i++) {
                auto parentIndex = skeleton->getParentIndex(i);
                if (parentIndex >= 0) {
                    numVerts += VERTICES_PER_LINK;
                }
            }
        }

        // count marker verts from shared DebugDraw singleton
        auto markerMap = DebugDraw::getInstance().getMarkerMap();
        numVerts += (int)markerMap.size() * VERTICES_PER_BONE;
        auto myAvatarMarkerMap = DebugDraw::getInstance().getMyAvatarMarkerMap();
        numVerts += (int)myAvatarMarkerMap.size() * VERTICES_PER_BONE;

        // allocate verts!
        data._vertexBuffer->resize(sizeof(Vertex) * numVerts);
        Vertex* verts = (Vertex*)data._vertexBuffer->editData();
        Vertex* v = verts;

        // draw absolute poses
        for (auto& iter : _absolutePoses) {
            AnimSkeleton::ConstPointer& skeleton = std::get<0>(iter.second);
            AnimPoseVec& absPoses = std::get<1>(iter.second);
            AnimPose rootPose = std::get<2>(iter.second);
            glm::vec4 color = std::get<3>(iter.second);

            for (int i = 0; i < skeleton->getNumJoints(); i++) {
                const float radius = BONE_RADIUS / (absPoses[i].scale.x * rootPose.scale.x);

                // draw bone
                addBone(rootPose, absPoses[i], radius, v);

                // draw link to parent
                auto parentIndex = skeleton->getParentIndex(i);
                if (parentIndex >= 0) {
                    assert(parentIndex < skeleton->getNumJoints());
                    addLink(rootPose, absPoses[i], absPoses[parentIndex], radius, color, v);
                }
            }
        }

        // draw markers from shared DebugDraw singleton
        for (auto& iter : markerMap) {
            glm::quat rot = std::get<0>(iter.second);
            glm::vec3 pos = std::get<1>(iter.second);
            glm::vec4 color = std::get<2>(iter.second);  // TODO: currently ignored.
            Q_UNUSED(color);
            const float radius = POSE_RADIUS;
            addBone(AnimPose::identity, AnimPose(glm::vec3(1), rot, pos), radius, v);
        }

        AnimPose myAvatarPose(glm::vec3(1), DebugDraw::getInstance().getMyAvatarRot(), DebugDraw::getInstance().getMyAvatarPos());
        for (auto& iter : myAvatarMarkerMap) {
            glm::quat rot = std::get<0>(iter.second);
            glm::vec3 pos = std::get<1>(iter.second);
            glm::vec4 color = std::get<2>(iter.second);  // TODO: currently ignored.
            Q_UNUSED(color);
            const float radius = POSE_RADIUS;
            addBone(myAvatarPose, AnimPose(glm::vec3(1), rot, pos), radius, v);
        }

        assert(numVerts == (v - verts));

        // The RenderItem culling is not working correctly
        // Workaround this issue by using the default constructed
        // item._bound which is a 16 km cube.
        /*
        render::Item::Bound theBound;
        for (int i = 0; i < numVerts; i++) {
            theBound += verts[i].pos;
        }
        data._bound = theBound;
        */

        data._isVisible = (numVerts > 0);


        data._indexBuffer->resize(sizeof(uint16_t) * numVerts);
        uint16_t* indices = (uint16_t*)data._indexBuffer->editData();
        for (int i = 0; i < numVerts; i++) {
            indices[i] = i;
        }
    });
    scene->enqueuePendingChanges(pendingChanges);
}
