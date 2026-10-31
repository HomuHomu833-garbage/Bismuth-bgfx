#include "ObjectBatch.hpp"
#include "BProfiler.hpp"
#include "Renderer.hpp"
#include "SpriteMeshDictionary.hpp"
#include "common.hpp"
#include "glm/fwd.hpp"
#include "math/ConvexList.hpp"
#include "math/ConvexPolygon.hpp"
#include <culling/SpriteDrawMap.hpp>
#include <string>
#include "../profiler.hpp"

using namespace geode::prelude;

#define QUAD_BL 0
#define QUAD_BR 1
#define QUAD_TL 2
#define QUAD_TR 3

SpriteVertexTransforms ObjectBatch::getSpriteVertexTransform(
    cocos2d::CCSprite* sprite,
    const cocos2d::CCAffineTransform& transform,
    SpriteSheet spriteSheet
) {
    CCRect crop = sprite->getTextureRect();
    
    CCTexture2D* texture = renderer.getSpriteSheetTexture(spriteSheet);
    if (texture == nullptr) return {};

    glm::vec2 posBottomLeft  = ccPointToGLM(CCPointApplyAffineTransform(sprite->getOffsetPosition(), transform));
    glm::vec2 posRightVector = glm::vec2(transform.a, transform.b) * crop.size.width;
    glm::vec2 posUpVector    = glm::vec2(transform.c, transform.d) * crop.size.height;

    glm::vec2 texBottomLeft;
    glm::vec2 texRightVector;
    glm::vec2 texUpVector;

    if (!sprite->isTextureRectRotated()) {
        texBottomLeft  = { crop.origin.x, crop.origin.y + crop.size.height };
        texRightVector = { crop.size.width, 0 };
        texUpVector    = { 0, -crop.size.height };
    } else {
        texBottomLeft  = { crop.origin.x, crop.origin.y };
        texRightVector = { 0, crop.size.width };
        texUpVector    = { crop.size.height, 0 };
    }
    
    if (sprite->isFlipX()) {
        posBottomLeft += posRightVector;
        posRightVector = -posRightVector;
    }
    if (sprite->isFlipY()) {
        posBottomLeft += posUpVector;
        posUpVector = -posUpVector;
    }

    float contentScaleFactor = CCDirector::get()->getContentScaleFactor();
    glm::vec2 texCoordFactor = glm::vec2(
        contentScaleFactor / texture->getPixelsWide(),
        contentScaleFactor / texture->getPixelsHigh()
    );

    texBottomLeft  *= texCoordFactor;
    texRightVector *= texCoordFactor;
    texUpVector    *= texCoordFactor;

    return {
        posBottomLeft, posRightVector, posUpVector,
        texBottomLeft, texRightVector, texUpVector
    };
}

void ObjectBatch::prepareSpriteMeshWrite(
    GameObject* object,
    cocos2d::CCSprite* sprite,
    SpriteType type,
    const cocos2d::CCAffineTransform& transform
) {
    SpriteSheet spriteSheet = ObjectUtils::getSpritesheetOfObject(object, type);
    if (spriteSheetFilter != (SpriteSheet)-1 && spriteSheet != spriteSheetFilter)
        return;

    u32 colorChannel = ObjectUtils::getSpriteColorChannel(type, object, sprite);
    if (type == SpriteType::DETAIL)
        colorChannel |= A_COLOR_CHANNEL_IS_SPRITE_DETAIL;

    currentSpriteVertexTransforms    = getSpriteVertexTransform(sprite, transform, spriteSheet);
    currentSpriteObjectStartPosition = ccPointToGLM(object->m_startPosition);

    currentSpriteSRBIndex    = renderer.getObjectSRBIndex(object);
    currentSpriteVertexIndex = verticies.size();
}

void ObjectBatch::writeSpriteVertex(glm::vec2 pos) {
    auto& transforms = currentSpriteVertexTransforms;

    glm::vec2 positionOffset = transforms.positionRight * pos.x +
                               transforms.positionUp    * pos.y +
                               transforms.positionBottomLeft - currentSpriteObjectStartPosition;

    glm::vec2 texCoord = transforms.texCoordRight * pos.x +
                         transforms.texCoordUp    * pos.y +
                         transforms.texCoordBottomLeft;

    verticies.push_back({
        positionOffset.x,
        positionOffset.y,
        bismuth::packTexCoord(texCoord.x, texCoord.y),
        currentSpriteIndex
    });
}

/*
void ObjectBatch::writeSpriteIndex(u32 index) {
    indicies.push_back(currentSpriteVertexIndex + index);
}
*/

void ObjectBatch::writeSpriteMeshFromConvexList(const ConvexList& list) {
    u32 vertexIndex = 0;
    list.triangulate([&](const glm::vec2& p1, const glm::vec2& p2, const glm::vec2& p3) {
        writeSpriteVertex(p1);
        writeSpriteVertex(p2);
        writeSpriteVertex(p3);
        // writeSpriteIndex(vertexIndex + 0);
        // writeSpriteIndex(vertexIndex + 1);
        // writeSpriteIndex(vertexIndex + 2);
        vertexIndex += 3;
    });
}

static SpriteDrawMap drawMap;

void ObjectBatch::addSprite(
    GameObject* object,
    cocos2d::CCSprite* sprite,
    SpriteType type,
    const cocos2d::CCAffineTransform& transform
) {
    if (object->m_hasNoGlow && type == SpriteType::GLOW)
        return;

    spriteCount++;

    prepareSpriteMeshWrite(object, sprite, type, transform);

    usize trianglesStart = verticies.size() / 3;

    ConvexList* spriteMesh = SpriteMeshDictionary::getSpriteMeshForSprite(sprite);

    drawMap.addSprite(object, sprite, type, {});

    currentSpriteIndex = inputSprites.size();

    if (spriteMesh) {
        writeSpriteMeshFromConvexList(*spriteMesh);
    } else {
        writeSpriteVertex({ 0, 0 });
        writeSpriteVertex({ 1, 0 });
        writeSpriteVertex({ 1, 1 });
        writeSpriteVertex({ 1, 1 });
        writeSpriteVertex({ 0, 1 });
        writeSpriteVertex({ 0, 0 });

        // writeSpriteIndex(QUAD_BL);
        // writeSpriteIndex(QUAD_TL);
        // writeSpriteIndex(QUAD_TR);
        // writeSpriteIndex(QUAD_BL);
        // writeSpriteIndex(QUAD_TR);
        // writeSpriteIndex(QUAD_BR);
    }

    usize trianglesCount = verticies.size() / 3 - trianglesStart;

    inputSprites.push_back(bismuth::UVec4(
        (u32)currentSpriteSRBIndex,
        (u32)type,
        (u32)trianglesStart,
        (u32)trianglesCount
    ));

    /*
    preCulledSprites.push_back({
        .groupCombinationIndex = renderer.getGroupManager().getGroupCombinationIndexForObject(object),
        .startPosition = ccPointToGLM(object->getStartPos())
    });
    */

    ZLayer zLayer = object->getObjectZLayer();
    SpriteSheet spriteSheet = ObjectUtils::getSpritesheetOfObject(object, type);

    usedLayerKeys.insert({zLayer, spriteSheet, false});
    usedLayerKeys.insert({zLayer, spriteSheet, true});

    workGroupScheduler.addSprite(object, sprite, type, currentSpriteIndex, trianglesCount);
}

void ObjectBatch::writeGameObject(GameObject* object) {
    float originalScaleX = object->getScaleX();
    float originalScaleY = object->getScaleY();

    if (object->m_usesAudioScale) {
        object->setScaleX(object->m_scaleX);
        object->setScaleY(object->m_scaleY);
    }

    ObjectUtils::unpackObjectIntoSprites(object, [&](const UnpackedSprite& sprite) {
        addSprite(sprite.parentObject, sprite.sprite, sprite.type, sprite.transform);
    });

    object->setScaleX(originalScaleX);
    object->setScaleY(originalScaleY);
}

void ObjectBatch::finishWriting(bismuth::StaticBatchData& out) {
    vertexCount = verticies.size();

    out.inputSpriteOffset = (u32)out.staticBuffer.size();
    out.inputSpriteCount = (u32)inputSprites.size();
    out.staticBuffer.insert(out.staticBuffer.end(), inputSprites.begin(), inputSprites.end());
    out.triangleCount = vertexCount / 3;
    out.vertices = std::move(verticies);

    verticies = {};
    inputSprites = {};
}

SortedObjectBatch* ObjectBatch::predraw() {
    PROFILER_PUSH("ObjectBatch::predraw");

    const auto& blendStates = renderer.getColorChannelBlendStates();

    // This uses a different thread to avoid a lag spike
    auto newSortedBatch = workGroupScheduler.fetchSortedBatchForBlendStates(blendStates);
    if (newSortedBatch)
        sortedBatch = newSortedBatch;
    else if (!sortedBatch) {
        workGroupScheduler.waitForJobs();
        sortedBatch = workGroupScheduler.fetchSortedBatchForBlendStates(blendStates);
        assert(sortedBatch);
    }

    PROFILER_POP();
    return sortedBatch;
}
