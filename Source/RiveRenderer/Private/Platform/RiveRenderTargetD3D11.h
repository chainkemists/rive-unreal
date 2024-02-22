// Copyright Rive, Inc. All rights reserved.

#pragma once

#include "RiveRenderTarget.h"

#if PLATFORM_WINDOWS

#if WITH_RIVE
#include "PreRiveHeaders.h"
THIRD_PARTY_INCLUDES_START
#include "rive/refcnt.hpp"
THIRD_PARTY_INCLUDES_END

namespace rive::pls
{
	class PLSRenderTargetD3D;
}

#endif // WITH_RIVE

namespace UE::Rive::Renderer::Private
{
	class FRiveRendererD3D11;

	class FRiveRenderTargetD3D11 final : public FRiveRenderTarget
	{
	public:
		/**
		 * Structor(s)
		 */

		FRiveRenderTargetD3D11(const TSharedRef<FRiveRendererD3D11>& InRiveRenderer, const FName& InRiveName, UTexture2DDynamic* InRenderTarget);
		virtual ~FRiveRenderTargetD3D11() override;
		//~ BEGIN : IRiveRenderTarget Interface

	public:
		virtual void CacheTextureTarget_RenderThread(FRHICommandListImmediate& RHICmdList, const FTexture2DRHIRef& InRHIResource) override;

#if WITH_RIVE
		
		//~ END : IRiveRenderTarget Interface

		//~ BEGIN : FRiveRenderTarget Interface

	protected:
		// It Might need to be on rendering thread, render QUEUE is required
		virtual rive::rcp<rive::pls::PLSRenderTarget> GetRenderTarget() const override;
		virtual void EndFrame() const override;

		//~ END : FRiveRenderTarget Interface

		/**
		 * Attribute(s)
		 */

	private:
		void ResetBlendState() const;

	private:
		TSharedRef<FRiveRendererD3D11> RiveRendererD3D11;

		rive::rcp<rive::pls::PLSRenderTargetD3D> CachedPLSRenderTargetD3D;

#endif // WITH_RIVE
	};
}

#endif // PLATFORM_WINDOWS