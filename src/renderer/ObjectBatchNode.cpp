#include "ObjectBatchNode.hpp"
#include "Renderer.hpp"

void ObjectBatchNode::draw() {
    if (auto renderer = Renderer::get())
        renderer->drawLayerNode(this);
}
