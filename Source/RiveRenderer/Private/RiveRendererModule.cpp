// Copyright Epic Games, Inc. All Rights Reserved.

#include "RiveRendererModule.h"
#include "Framework/Application/SlateApplication.h"
#include "RiveRenderer.h"

#if PLATFORM_WINDOWS
#include "Platform/RiveRendererD3D11.h"
#elif PLATFORM_APPLE
#include "Platform/RiveRendererMetal.h"
#endif // PLATFORM_WINDOWS

#define LOCTEXT_NAMESPACE "RiveRendererModule"

void UE::Rive::Renderer::Private::FRiveRendererModule::StartupModule()
{
    // Create Platform Specific Renderer
    RiveRenderer = nullptr;
    
    switch (RHIGetInterfaceType())
    {
#if PLATFORM_WINDOWS

        case ERHIInterfaceType::D3D11:
        {
            RiveRenderer = MakeShared<FRiveRendererD3D11>();
            
            break;
        }
        
        case ERHIInterfaceType::D3D12:
        {
            break;
        }
        
#endif // PLATFORM_WINDOWS
    
#if PLATFORM_APPLE
         
        case ERHIInterfaceType::Metal:
        {
            RiveRenderer = MakeShared<FRiveRendererMetal>();
            
            break;
        }

#endif // PLATFORM_APPLE
            
        case ERHIInterfaceType::Vulkan:
        {
            break;
        }
    default:
        break;
    }


    FCoreDelegates::OnFEngineLoopInitComplete.AddLambda([this]()
    {
        if (RiveRenderer.IsValid())
        {
            RiveRenderer->Initialize();
        }
    });
}

void UE::Rive::Renderer::Private::FRiveRendererModule::ShutdownModule()
{
}

UE::Rive::Renderer::IRiveRenderer* UE::Rive::Renderer::Private::FRiveRendererModule::GetRenderer()
{
    return RiveRenderer.Get();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(UE::Rive::Renderer::Private::FRiveRendererModule, RiveRenderer)
