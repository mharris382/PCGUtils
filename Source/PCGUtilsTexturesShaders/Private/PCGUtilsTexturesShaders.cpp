// Copyright Max Harris

#include "PCGUtilsTexturesShaders.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "RHIGPUReadback.h"
#include "RHIStaticStates.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderCore.h"
#include "ShaderParameterStruct.h"

IMPLEMENT_MODULE(FPCGUtilsTexturesShadersModule, PCGUtilsTexturesShaders)

void FPCGUtilsTexturesShadersModule::StartupModule()
{
	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("PCGUtils")))
	{
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/PCGUtils"), FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders")));
	}
}

class FPCGUtilsGreyscaleSampleCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FPCGUtilsGreyscaleSampleCS);
	SHADER_USE_PARAMETER_STRUCT(FPCGUtilsGreyscaleSampleCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_TEXTURE(Texture2D, SourceTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SourceSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputTexture)
		SHADER_PARAMETER(FUintVector2, OutputSize)
		SHADER_PARAMETER(FVector4f, ChannelWeights)
		SHADER_PARAMETER(uint32, bMaxOfRGB)
		SHADER_PARAMETER(uint32, bEncodeSRGB)
		SHADER_PARAMETER(float, BlackPoint)
		SHADER_PARAMETER(float, WhitePoint)
		SHADER_PARAMETER(uint32, bInvert)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FPCGUtilsGreyscaleSampleCS, "/Plugin/PCGUtils/Private/PCGUtilsTextures/PCGUtilsImageToGreyscale.usf", "SampleCS", SF_Compute);

class FPCGUtilsGreyscaleBlurCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FPCGUtilsGreyscaleBlurCS);
	SHADER_USE_PARAMETER_STRUCT(FPCGUtilsGreyscaleBlurCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BlurInput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, BlurOutput)
		SHADER_PARAMETER(FUintVector2, BlurSize)
		SHADER_PARAMETER(FIntPoint, BlurAxis)
		SHADER_PARAMETER(int32, BlurRadiusPixels)
		SHADER_PARAMETER(float, BlurSigma)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FPCGUtilsGreyscaleBlurCS, "/Plugin/PCGUtils/Private/PCGUtilsTextures/PCGUtilsImageToGreyscale.usf", "BlurCS", SF_Compute);

namespace PCGUtilsTexturesShaders
{
	bool IsGPUSamplingAvailable()
	{
		return FApp::CanEverRender() && !GUsingNullRHI;
	}

	bool ReadGreyscale_RenderThread(
		FRHICommandListImmediate& RHICmdList,
		FRHITexture* Source,
		const FPCGUtilsGreyscaleGPUParams& Params,
		TArray<float>& OutValues)
	{
		check(IsInRenderingThread());
		OutValues.Reset();
		if (!Source || Params.OutputSize.X <= 0 || Params.OutputSize.Y <= 0)
		{
			return false;
		}

		const FIntPoint Size = Params.OutputSize;
		const FUintVector2 USize(static_cast<uint32>(Size.X), static_cast<uint32>(Size.Y));
		const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(Size, FIntPoint(8, 8));
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		FRHIGPUTextureReadback Readback(TEXT("PCGUtilsGreyscaleReadback"));

		{
			FRDGBuilder GraphBuilder(RHICmdList);
			const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
				Size, PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV);
			FRDGTextureRef Greyscale = GraphBuilder.CreateTexture(Desc, TEXT("PCGUtilsGreyscale"));

			FPCGUtilsGreyscaleSampleCS::FParameters* SampleParams = GraphBuilder.AllocParameters<FPCGUtilsGreyscaleSampleCS::FParameters>();
			SampleParams->SourceTexture = Source;
			SampleParams->SourceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			SampleParams->OutputTexture = GraphBuilder.CreateUAV(Greyscale);
			SampleParams->OutputSize = USize;
			SampleParams->ChannelWeights = Params.ChannelWeights;
			SampleParams->bMaxOfRGB = Params.bMaxOfRGB ? 1u : 0u;
			SampleParams->bEncodeSRGB = Params.bEncodeSRGB ? 1u : 0u;
			SampleParams->BlackPoint = Params.BlackPoint;
			SampleParams->WhitePoint = Params.WhitePoint;
			SampleParams->bInvert = Params.bInvert ? 1u : 0u;
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("PCGUtilsGreyscaleSample"),
				TShaderMapRef<FPCGUtilsGreyscaleSampleCS>(ShaderMap), SampleParams, GroupCount);

			FRDGTextureRef Result = Greyscale;
			if (Params.BlurRadius > 0.0f)
			{
				// Sigma = Radius / 2, kernel out to 3 sigma: the same kernel PCGUtilsTextures uses on the CPU.
				const float Sigma = FMath::Max(Params.BlurRadius * 0.5f, 0.5f);
				const int32 KernelRadius = FMath::Clamp(FMath::CeilToInt32(Sigma * 3.0f), 1, 64);
				FRDGTextureRef Scratch = GraphBuilder.CreateTexture(Desc, TEXT("PCGUtilsGreyscaleBlur"));

				auto AddBlurPass = [&](FRDGTextureRef Input, FRDGTextureRef Output, FIntPoint Axis)
				{
					FPCGUtilsGreyscaleBlurCS::FParameters* BlurParams = GraphBuilder.AllocParameters<FPCGUtilsGreyscaleBlurCS::FParameters>();
					BlurParams->BlurInput = Input;
					BlurParams->BlurOutput = GraphBuilder.CreateUAV(Output);
					BlurParams->BlurSize = USize;
					BlurParams->BlurAxis = Axis;
					BlurParams->BlurRadiusPixels = KernelRadius;
					BlurParams->BlurSigma = Sigma;
					FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("PCGUtilsGreyscaleBlur"),
						TShaderMapRef<FPCGUtilsGreyscaleBlurCS>(ShaderMap), BlurParams, GroupCount);
				};
				AddBlurPass(Greyscale, Scratch, FIntPoint(1, 0));
				AddBlurPass(Scratch, Greyscale, FIntPoint(0, 1));
			}

			AddEnqueueCopyPass(GraphBuilder, &Readback, Result);
			GraphBuilder.Execute();
		}

		// Editor-time rebuilds are rare and the caller wants the result now, so wait for the GPU here.
		RHICmdList.BlockUntilGPUIdle();

		int32 RowPitchInPixels = 0;
		const float* Data = static_cast<const float*>(Readback.Lock(RowPitchInPixels));
		if (!Data)
		{
			return false;
		}
		OutValues.SetNumUninitialized(Size.X * Size.Y);
		for (int32 Y = 0; Y < Size.Y; ++Y)
		{
			FMemory::Memcpy(&OutValues[Y * Size.X], Data + static_cast<int64>(Y) * RowPitchInPixels, Size.X * sizeof(float));
		}
		Readback.Unlock();
		return true;
	}
}
