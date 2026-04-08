#include "Cafe/HW/Latte/Renderer/Metal/CachedFBOMtl.h"
#include "Cafe/HW/Latte/Renderer/Metal/LatteTextureViewMtl.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalRenderer.h"
#include "Cafe/HW/Latte/Renderer/Metal/LatteToMtl.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if TARGET_OS_SIMULATOR
static uint32 GetPixelFormatBytes(MTL::PixelFormat fmt)
{
	switch (fmt) {
	case MTL::PixelFormatRGBA32Float: case MTL::PixelFormatRGBA32Uint: case MTL::PixelFormatRGBA32Sint: return 16;
	case MTL::PixelFormatRGBA16Float: case MTL::PixelFormatRGBA16Unorm: case MTL::PixelFormatRGBA16Snorm:
	case MTL::PixelFormatRGBA16Uint: case MTL::PixelFormatRGBA16Sint:
	case MTL::PixelFormatRG32Float: case MTL::PixelFormatRG32Uint: case MTL::PixelFormatRG32Sint:
	case MTL::PixelFormatRG11B10Float: case MTL::PixelFormatRGB9E5Float: // stored as RGBA16F in tile memory
		return 8;
	case MTL::PixelFormatRGBA8Unorm: case MTL::PixelFormatRGBA8Unorm_sRGB: case MTL::PixelFormatBGRA8Unorm:
	case MTL::PixelFormatBGRA8Unorm_sRGB: case MTL::PixelFormatRGBA8Snorm:
	case MTL::PixelFormatRGBA8Uint: case MTL::PixelFormatRGBA8Sint:
	case MTL::PixelFormatRGB10A2Unorm: case MTL::PixelFormatRGB10A2Uint:
	case MTL::PixelFormatBGR10A2Unorm:
	case MTL::PixelFormatR32Float: case MTL::PixelFormatR32Uint: case MTL::PixelFormatR32Sint:
	case MTL::PixelFormatRG16Float: case MTL::PixelFormatRG16Unorm: case MTL::PixelFormatRG16Snorm:
	case MTL::PixelFormatRG16Uint: case MTL::PixelFormatRG16Sint:
	case MTL::PixelFormatDepth32Float: return 4;
	case MTL::PixelFormatRG8Unorm: case MTL::PixelFormatRG8Snorm:
	case MTL::PixelFormatR16Float: case MTL::PixelFormatR16Unorm: case MTL::PixelFormatR16Snorm:
	case MTL::PixelFormatR16Uint: case MTL::PixelFormatR16Sint: return 2;
	case MTL::PixelFormatR8Unorm: case MTL::PixelFormatR8Snorm:
	case MTL::PixelFormatR8Uint: case MTL::PixelFormatR8Sint: case MTL::PixelFormatA8Unorm: return 1;
	case MTL::PixelFormatDepth32Float_Stencil8: return 8; // 4 depth + 4 stencil (padded)
	case MTL::PixelFormatInvalid: return 0;
	default: return 4;
	}
}
#endif

CachedFBOMtl::CachedFBOMtl(class MetalRenderer* metalRenderer, uint64 key) : LatteCachedFBO(key)
{
	m_renderPassDescriptor = MTL::RenderPassDescriptor::alloc()->init();

#if TARGET_OS_SIMULATOR
	// Simulator GPU (Apple3-class) has 32-byte render target storage limit.
	// Pre-calculate depth/stencil cost so we can cap color attachments.
	uint32 depthBytes = 0;
	if (depthBuffer.texture)
	{
		auto depthTextureView = static_cast<LatteTextureViewMtl*>(depthBuffer.texture);
		depthBytes = GetPixelFormatBytes(depthTextureView->GetRGBAView()->pixelFormat());
	}
	uint32 rtBudget = 32 - depthBytes;
	uint32 colorBytesUsed = 0;
#endif

	bool hasAttachment = false;
	for (int i = 0; i < 8; ++i)
	{
		const auto& buffer = colorBuffer[i];
		auto textureView = (LatteTextureViewMtl*)buffer.texture;
		if (!textureView)
		{
			continue;
		}

#if TARGET_OS_SIMULATOR
		uint32 fmtBytes = GetPixelFormatBytes(textureView->GetRGBAView()->pixelFormat());
		if (colorBytesUsed + fmtBytes > rtBudget)
			continue;
		colorBytesUsed += fmtBytes;
#endif

		auto colorAttachment = m_renderPassDescriptor->colorAttachments()->object(i);
		colorAttachment->setTexture(textureView->GetRGBAView());
		colorAttachment->setLoadAction(MTL::LoadActionLoad);
		colorAttachment->setStoreAction(MTL::StoreActionStore);

		hasAttachment = true;
	}

	// setup depth attachment
	if (depthBuffer.texture)
	{
		auto textureView = static_cast<LatteTextureViewMtl*>(depthBuffer.texture);
		auto depthAttachment = m_renderPassDescriptor->depthAttachment();
		depthAttachment->setTexture(textureView->GetRGBAView());
		depthAttachment->setLoadAction(MTL::LoadActionLoad);
		depthAttachment->setStoreAction(MTL::StoreActionStore);

		// setup stencil attachment
		if (depthBuffer.hasStencil && GetMtlPixelFormatInfo(depthBuffer.texture->format, true).hasStencil)
		{
		    auto stencilAttachment = m_renderPassDescriptor->stencilAttachment();
            stencilAttachment->setTexture(textureView->GetRGBAView());
            stencilAttachment->setLoadAction(MTL::LoadActionLoad);
            stencilAttachment->setStoreAction(MTL::StoreActionStore);
		}

		hasAttachment = true;
	}

	// HACK: setup a dummy color attachment to prevent Metal from discarding draws for stremout draws in Super Smash Bros. for Wii U (works fine on MoltenVK without this hack though)
	if (!hasAttachment)
	{
        auto colorAttachment = m_renderPassDescriptor->colorAttachments()->object(0);
    	colorAttachment->setTexture(metalRenderer->GetNullTexture2D());
    	colorAttachment->setLoadAction(MTL::LoadActionDontCare);
    	colorAttachment->setStoreAction(MTL::StoreActionDontCare);
	}

	// Visibility buffer
	m_renderPassDescriptor->setVisibilityResultBuffer(metalRenderer->GetOcclusionQueryResultBuffer());
}

CachedFBOMtl::~CachedFBOMtl()
{
	m_renderPassDescriptor->release();
}
