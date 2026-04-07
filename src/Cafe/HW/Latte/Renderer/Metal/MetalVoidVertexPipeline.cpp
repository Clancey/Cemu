#include "Cafe/HW/Latte/Renderer/Metal/MetalVoidVertexPipeline.h"

MetalVoidVertexPipeline::MetalVoidVertexPipeline(class MetalRenderer* mtlRenderer, MTL::Library* library, const std::string& vertexFunctionName)
{
    // Render pipeline state
    NS_STACK_SCOPED MTL::Function* vertexFunction = library->newFunction(ToNSString(vertexFunctionName));

    NS_STACK_SCOPED MTL::RenderPipelineDescriptor* renderPipelineDescriptor = MTL::RenderPipelineDescriptor::alloc()->init();
    renderPipelineDescriptor->setVertexFunction(vertexFunction);
    renderPipelineDescriptor->setRasterizationEnabled(false);
    // Some Metal implementations require at least one color attachment even when
    // rasterization is disabled (notably the visionOS simulator).
    renderPipelineDescriptor->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatBGRA8Unorm);

    NS::Error* error = nullptr;
    m_renderPipelineState = mtlRenderer->GetDevice()->newRenderPipelineState(renderPipelineDescriptor, &error);
    if (error)
    {
        cemuLog_log(LogType::Force, "error creating hybrid render pipeline state: {}", error->localizedDescription()->utf8String());
    }
}

MetalVoidVertexPipeline::~MetalVoidVertexPipeline()
{
    m_renderPipelineState->release();
}
