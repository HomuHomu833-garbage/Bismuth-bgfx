#pragma once

#include <common.hpp>

#include "ObjectUtils.hpp"

/*
    Placeholder node at the z-order of a vanilla sprite batch node. When cocos2d
    reaches it while drawing, Bismuth draws (or composites) that layer.
*/
class ObjectBatchNode : public cocos2d::CCNode {
public:
    inline ObjectBatchNode(const LayerKey& id)
        : layerId(id) {}

    void draw() override;

    inline const LayerKey& getLayerKey() const { return layerId; }

public:
    static inline geode::Ref<ObjectBatchNode> create(const LayerKey& id) {
        auto ret = new ObjectBatchNode(id);
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

private:
    LayerKey layerId;
};
