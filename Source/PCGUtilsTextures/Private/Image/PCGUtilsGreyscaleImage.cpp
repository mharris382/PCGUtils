// Copyright Max Harris

#include "Image/PCGUtilsGreyscaleImage.h"

#include "Async/ParallelFor.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "ImageCore.h"
#include "PCGUtilsTexturesShaders.h"
#include "RenderingThread.h"
#include "TextureResource.h"

#define LOCTEXT_NAMESPACE "PCGUtilsGreyscaleImage"

void FPCGUtilsGreyscaleImage::Init(int32 InWidth, int32 InHeight, float Fill)
{
	Width = FMath::Max(0, InWidth);
	Height = FMath::Max(0, InHeight);
	Values.Init(Fill, Width * Height);
}

namespace PCGUtilsImageSampling
{
	namespace
	{
		float ReduceToChannel(const FLinearColor& Color, EPCGUtilsImageChannel Channel)
		{
			switch (Channel)
			{
			case EPCGUtilsImageChannel::Red: return Color.R;
			case EPCGUtilsImageChannel::Green: return Color.G;
			case EPCGUtilsImageChannel::Blue: return Color.B;
			case EPCGUtilsImageChannel::Alpha: return Color.A;
			case EPCGUtilsImageChannel::MaxRGB: return FMath::Max3(Color.R, Color.G, Color.B);
			case EPCGUtilsImageChannel::Luminance:
			default: return 0.2126f * Color.R + 0.7152f * Color.G + 0.0722f * Color.B;
			}
		}

		FVector4f ChannelWeightsFor(EPCGUtilsImageChannel Channel)
		{
			switch (Channel)
			{
			case EPCGUtilsImageChannel::Red: return FVector4f(1, 0, 0, 0);
			case EPCGUtilsImageChannel::Green: return FVector4f(0, 1, 0, 0);
			case EPCGUtilsImageChannel::Blue: return FVector4f(0, 0, 1, 0);
			case EPCGUtilsImageChannel::Alpha: return FVector4f(0, 0, 0, 1);
			default: return FVector4f(0.2126f, 0.7152f, 0.0722f, 0.0f);
			}
		}

		/** Levels then invert: the order the GPU pass applies them in. */
		float ApplyLevels(float Value, const FPCGUtilsImageSampleOptions& Options)
		{
			const float Range = FMath::Max(Options.WhitePoint - Options.BlackPoint, 1e-6f);
			Value = FMath::Clamp((Value - Options.BlackPoint) / Range, 0.0f, 1.0f);
			return Options.bInvert ? 1.0f - Value : Value;
		}

		/** Box-filtered resample: each output pixel averages the source texels its footprint covers (bilinear when enlarging). */
		void ResampleChannel(TConstArrayView<float> Source, int32 SourceWidth, int32 SourceHeight, FPCGUtilsGreyscaleImage& Out)
		{
			const double ScaleX = static_cast<double>(SourceWidth) / Out.Width;
			const double ScaleY = static_cast<double>(SourceHeight) / Out.Height;
			ParallelFor(Out.Height, [&](int32 Y)
			{
				for (int32 X = 0; X < Out.Width; ++X)
				{
					if (ScaleX <= 1.0 && ScaleY <= 1.0)
					{
						const double SX = FMath::Clamp((X + 0.5) * ScaleX - 0.5, 0.0, SourceWidth - 1.0);
						const double SY = FMath::Clamp((Y + 0.5) * ScaleY - 0.5, 0.0, SourceHeight - 1.0);
						const int32 X0 = FMath::FloorToInt32(SX), Y0 = FMath::FloorToInt32(SY);
						const int32 X1 = FMath::Min(X0 + 1, SourceWidth - 1), Y1 = FMath::Min(Y0 + 1, SourceHeight - 1);
						const float TX = static_cast<float>(SX - X0), TY = static_cast<float>(SY - Y0);
						const float Top = FMath::Lerp(Source[Y0 * SourceWidth + X0], Source[Y0 * SourceWidth + X1], TX);
						const float Bottom = FMath::Lerp(Source[Y1 * SourceWidth + X0], Source[Y1 * SourceWidth + X1], TX);
						Out.At(X, Y) = FMath::Lerp(Top, Bottom, TY);
						continue;
					}
					const int32 SX0 = FMath::Clamp(FMath::FloorToInt32(X * ScaleX), 0, SourceWidth - 1);
					const int32 SY0 = FMath::Clamp(FMath::FloorToInt32(Y * ScaleY), 0, SourceHeight - 1);
					const int32 SX1 = FMath::Clamp(FMath::CeilToInt32((X + 1) * ScaleX), SX0 + 1, SourceWidth);
					const int32 SY1 = FMath::Clamp(FMath::CeilToInt32((Y + 1) * ScaleY), SY0 + 1, SourceHeight);
					double Sum = 0.0;
					for (int32 SY = SY0; SY < SY1; ++SY)
					{
						for (int32 SX = SX0; SX < SX1; ++SX)
						{
							Sum += Source[SY * SourceWidth + SX];
						}
					}
					Out.At(X, Y) = static_cast<float>(Sum / ((SX1 - SX0) * (SY1 - SY0)));
				}
			});
		}
	}

	FIntPoint ResolveOutputSize(FIntPoint SourceSize, int32 MaxResolution)
	{
		if (SourceSize.X <= 0 || SourceSize.Y <= 0)
		{
			return FIntPoint::ZeroValue;
		}
		const int32 Longest = FMath::Max(SourceSize.X, SourceSize.Y);
		if (MaxResolution <= 0 || Longest <= MaxResolution)
		{
			return SourceSize;
		}
		const double Scale = static_cast<double>(MaxResolution) / Longest;
		return FIntPoint(FMath::Max(1, FMath::RoundToInt32(SourceSize.X * Scale)), FMath::Max(1, FMath::RoundToInt32(SourceSize.Y * Scale)));
	}

	void Blur(FPCGUtilsGreyscaleImage& Image, float Radius)
	{
		if (Radius <= 0.0f || !Image.IsValid())
		{
			return;
		}
		const float Sigma = FMath::Max(Radius * 0.5f, 0.5f);
		const int32 KernelRadius = FMath::Clamp(FMath::CeilToInt32(Sigma * 3.0f), 1, 64);
		TArray<float> Kernel;
		Kernel.SetNumUninitialized(KernelRadius * 2 + 1);
		float Sum = 0.0f;
		for (int32 Offset = -KernelRadius; Offset <= KernelRadius; ++Offset)
		{
			Sum += Kernel[Offset + KernelRadius] = FMath::Exp(-static_cast<float>(Offset * Offset) / (2.0f * Sigma * Sigma));
		}
		for (float& Weight : Kernel)
		{
			Weight /= Sum;
		}

		const int32 W = Image.Width, H = Image.Height;
		TArray<float> Scratch;
		Scratch.SetNumUninitialized(W * H);
		ParallelFor(H, [&](int32 Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				float Value = 0.0f;
				for (int32 Offset = -KernelRadius; Offset <= KernelRadius; ++Offset)
				{
					Value += Kernel[Offset + KernelRadius] * Image.Values[Y * W + FMath::Clamp(X + Offset, 0, W - 1)];
				}
				Scratch[Y * W + X] = Value;
			}
		});
		ParallelFor(H, [&](int32 Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				float Value = 0.0f;
				for (int32 Offset = -KernelRadius; Offset <= KernelRadius; ++Offset)
				{
					Value += Kernel[Offset + KernelRadius] * Scratch[FMath::Clamp(Y + Offset, 0, H - 1) * W + X];
				}
				Image.Values[Y * W + X] = Value;
			}
		});
	}

	void ProcessLinearColors(
		TConstArrayView<FLinearColor> Colors, int32 Width, int32 Height,
		const FPCGUtilsImageSampleOptions& Options, FPCGUtilsGreyscaleImage& OutImage)
	{
		TArray<float> Channel;
		Channel.SetNumUninitialized(Width * Height);
		ParallelFor(Height, [&](int32 Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				Channel[Y * Width + X] = ReduceToChannel(Colors[Y * Width + X], Options.Channel);
			}
		});

		const FIntPoint OutSize = ResolveOutputSize(FIntPoint(Width, Height), Options.MaxResolution);
		OutImage.Init(OutSize.X, OutSize.Y);
		if (OutSize == FIntPoint(Width, Height))
		{
			OutImage.Values = MoveTemp(Channel);
		}
		else
		{
			ResampleChannel(Channel, Width, Height, OutImage);
		}
		for (float& Value : OutImage.Values)
		{
			Value = ApplyLevels(Value, Options);
		}
		Blur(OutImage, Options.BlurRadius);
	}

	namespace
	{
#if WITH_EDITORONLY_DATA
		bool SampleSourceData(UTexture* Texture, const FPCGUtilsImageSampleOptions& Options, FPCGUtilsGreyscaleImage& OutImage, FText& OutError)
		{
			FImage Image;
			if (!Texture->Source.IsValid() || !Texture->Source.GetMipImage(Image, 0))
			{
				OutError = FText::Format(LOCTEXT("NoSourceData", "texture '{0}' has no readable source data (mip 0)"),
					FText::FromString(Texture->GetName()));
				return false;
			}
			if (ERawImageFormat::GetFormatNeedsGammaSpace(Image.Format))
			{
				// Authored reads the stored values as they are; Linear decodes an sRGB texture.
				Image.GammaSpace = Options.ValueSpace == EPCGUtilsImageValueSpace::Linear && Texture->SRGB
					? EGammaSpace::sRGB : EGammaSpace::Linear;
			}
			FImage Float;
			Image.CopyTo(Float, ERawImageFormat::RGBA32F, EGammaSpace::Linear);
			ProcessLinearColors(Float.AsRGBA32F(), static_cast<int32>(Float.SizeX), static_cast<int32>(Float.SizeY), Options, OutImage);
			return true;
		}
#endif

		bool SampleGPU(UTexture* Texture, const FPCGUtilsImageSampleOptions& Options, FPCGUtilsGreyscaleImage& OutImage, FText& OutError)
		{
			if (!PCGUtilsTexturesShaders::IsGPUSamplingAvailable())
			{
				OutError = LOCTEXT("NoGPU", "the GPU path needs a running renderer, and this process has none (-nullrhi)");
				return false;
			}
			if (UTexture2D* Texture2D = Cast<UTexture2D>(Texture))
			{
				// Streaming may have only low mips resident; the vectorizer wants the full image.
				Texture2D->SetForceMipLevelsToBeResident(30.0f);
				Texture2D->WaitForStreaming();
			}
			FTextureResource* Resource = Texture->GetResource();
			if (!Resource)
			{
				OutError = FText::Format(LOCTEXT("NoResource", "texture '{0}' has no GPU resource yet"), FText::FromString(Texture->GetName()));
				return false;
			}
			const FIntPoint SourceSize(FMath::RoundToInt32(Texture->GetSurfaceWidth()), FMath::RoundToInt32(Texture->GetSurfaceHeight()));
			const FIntPoint OutSize = ResolveOutputSize(SourceSize, Options.MaxResolution);
			if (OutSize.X <= 0 || OutSize.Y <= 0)
			{
				OutError = FText::Format(LOCTEXT("EmptyTexture", "texture '{0}' is {1}x{2}; it has no pixels"),
					FText::FromString(Texture->GetName()), FText::AsNumber(SourceSize.X), FText::AsNumber(SourceSize.Y));
				return false;
			}

			FPCGUtilsGreyscaleGPUParams Params;
			Params.OutputSize = OutSize;
			Params.ChannelWeights = ChannelWeightsFor(Options.Channel);
			Params.bMaxOfRGB = Options.Channel == EPCGUtilsImageChannel::MaxRGB;
			// The sampler decodes sRGB to linear; Authored re-encodes it so both paths read the painted values.
			Params.bEncodeSRGB = Options.ValueSpace == EPCGUtilsImageValueSpace::Authored && Texture->SRGB;
			Params.BlackPoint = Options.BlackPoint;
			Params.WhitePoint = Options.WhitePoint;
			Params.bInvert = Options.bInvert;
			Params.BlurRadius = Options.BlurRadius;

			bool bRead = false;
			TArray<float> Values;
			ENQUEUE_RENDER_COMMAND(PCGUtilsReadGreyscale)([Resource, Params, &Values, &bRead](FRHICommandListImmediate& RHICmdList)
			{
				bRead = PCGUtilsTexturesShaders::ReadGreyscale_RenderThread(RHICmdList, Resource->GetTexture2DRHI(), Params, Values);
			});
			FlushRenderingCommands();

			if (!bRead || Values.Num() != OutSize.X * OutSize.Y)
			{
				OutError = FText::Format(LOCTEXT("GPUReadFailed", "texture '{0}' could not be read back from the GPU (it must be a 2D texture)"),
					FText::FromString(Texture->GetName()));
				return false;
			}
			OutImage.Width = OutSize.X;
			OutImage.Height = OutSize.Y;
			OutImage.Values = MoveTemp(Values);
			return true;
		}
	}

	bool SampleTexture(UTexture* Texture, const FPCGUtilsImageSampleOptions& Options, FPCGUtilsGreyscaleImage& OutImage, FText& OutError)
	{
		check(IsInGameThread());
		OutImage = FPCGUtilsGreyscaleImage();
		if (!Texture)
		{
			OutError = LOCTEXT("NoTexture", "no texture is set");
			return false;
		}
		if (Options.WhitePoint <= Options.BlackPoint)
		{
			OutError = FText::Format(LOCTEXT("BadLevels", "White Point ({0}) must be greater than Black Point ({1})"),
				FText::AsNumber(Options.WhitePoint), FText::AsNumber(Options.BlackPoint));
			return false;
		}

		switch (Options.ReadSource)
		{
		case EPCGUtilsImageReadSource::GPU:
			return SampleGPU(Texture, Options, OutImage, OutError);
		case EPCGUtilsImageReadSource::SourceData:
#if WITH_EDITORONLY_DATA
			return SampleSourceData(Texture, Options, OutImage, OutError);
#else
			OutError = LOCTEXT("NoSourceInGame", "Source Data is editor-only; use GPU or Auto in a cooked build");
			return false;
#endif
		case EPCGUtilsImageReadSource::Auto:
		default:
#if WITH_EDITORONLY_DATA
			if (Texture->Source.IsValid())
			{
				return SampleSourceData(Texture, Options, OutImage, OutError);
			}
#endif
			return SampleGPU(Texture, Options, OutImage, OutError);
		}
	}
}

#undef LOCTEXT_NAMESPACE
