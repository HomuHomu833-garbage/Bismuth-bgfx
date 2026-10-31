#pragma once

#include <Geode/binding/GameObject.hpp>
#include <common.hpp>
#include <Geode/Geode.hpp>
#include <set>
#include <vector>

#include "BatchData.hpp"
#include "math/ConvexList.hpp"
#include "ObjectUtils.hpp"
#include "WorkGroupScheduler.hpp"
#include "../utils/GPUTimer.hpp"

using namespace geode;

class Renderer;

struct SpriteVertexTransforms {
    glm::vec2 positionBottomLeft;
    glm::vec2 positionRight;
    glm::vec2 positionUp;
    glm::vec2 texCoordBottomLeft;
    glm::vec2 texCoordRight;
    glm::vec2 texCoordUp;
};

/*
    Turns the level's game objects into sprite meshes (written once), and keeps
    track of the sprites sorted per layer for the current color blending states.
*/
class ObjectBatch {
public:
    inline ObjectBatch(Renderer& renderer)
        : renderer(renderer) {}

    SpriteVertexTransforms getSpriteVertexTransform(
        cocos2d::CCSprite* sprite,
        const cocos2d::CCAffineTransform& transform,
        SpriteSheet spriteSheet
    );

    void prepareSpriteMeshWrite(
        GameObject* parentObject,
        cocos2d::CCSprite* sprite,
        SpriteType type,
        const cocos2d::CCAffineTransform& transform
    );

    void writeSpriteVertex(glm::vec2 pos);

    void writeSpriteMeshFromConvexList(const ConvexList& list);

    void addSprite(
        GameObject* parentObject,
        cocos2d::CCSprite* sprite,
        SpriteType type,
        const cocos2d::CCAffineTransform& transform
    );

    void writeGameObject(GameObject* object);

    /// Moves the sprite meshes and input sprites into `out` (after the object infos).
    void finishWriting(bismuth::StaticBatchData& out);

    /// Picks the sorted batch for the current blending states (generating it on a
    /// worker thread if needed). Returns the batch to render this frame.
    SortedObjectBatch* predraw();

    inline SortedObjectBatch* getSortedBatch() const { return sortedBatch; }

    inline bool shouldDraw(const LayerKey& layer) const {
        if (!sortedBatch) return false;
        auto it = sortedBatch->layerIndexPerKey.find(layer);
        return it != sortedBatch->layerIndexPerKey.end() && it->second.spriteCount > 0;
    }

    inline usize getVertexBufferSize() { return vertexCount * sizeof(bismuth::PackedVertex); }
    inline void setSpriteSheetFilter(SpriteSheet sheet) { spriteSheetFilter = sheet; }
    inline const std::set<LayerKey>& getUsedLayerIds() { return usedLayerKeys; }
    inline usize getSpriteCount() { return workGroupScheduler.getSpriteCount(); }
    inline WorkGroupScheduler& getScheduler() { return workGroupScheduler; }

private:
    Renderer& renderer;

    SpriteSheet spriteSheetFilter = (SpriteSheet)-1;

    GPUTimer cullingTimer { "Sprite culling" };

    WorkGroupScheduler workGroupScheduler;
    SortedObjectBatch* sortedBatch = nullptr;

    usize spriteCount = 0;

    std::vector<bismuth::PackedVertex> verticies;
    u32 vertexCount = 0;

    // (objectIndex, spriteType, firstTriangle, triangleCount)
    std::vector<bismuth::UVec4> inputSprites;

    std::set<LayerKey> usedLayerKeys;

    SpriteVertexTransforms currentSpriteVertexTransforms;
    glm::vec2 currentSpriteObjectStartPosition;
    u32 currentSpriteVertexIndex;
    u32 currentSpriteSRBIndex;
    u32 currentSpriteIndex;
};
