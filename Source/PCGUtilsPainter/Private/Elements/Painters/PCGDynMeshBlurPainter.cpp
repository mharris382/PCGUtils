// Copyright Max Harris

#include "Elements/Painters/PCGDynMeshBlurPainter.h"
#include "Helpers/PCGUtilsParallelInputs.h"
#include "Data/PCGDynamicMeshData.h"

#include "Async/ParallelFor.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/Painters/PCGUtilsPainterShorthandHelpers.h"
#include "PCGContext.h"
#include "Selections/MeshConnectedComponents.h"
#include "Serialization/ArchiveCrc32.h"
#include "Spatial/PointHashGrid3.h"
#include "Target/PCGUtilsPainterSampling.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshBlurPainter"

namespace
{
	double FalloffWeight(EPCGUtilsPainterBlurFalloff Falloff, double DistanceSq, double RadiusSq)
	{
		const double TSq = RadiusSq > 0.0 ? DistanceSq / RadiusSq : 0.0;
		switch (Falloff)
		{
		case EPCGUtilsPainterBlurFalloff::Uniform:
			return 1.0;
		case EPCGUtilsPainterBlurFalloff::Linear:
			return FMath::Max(0.0, 1.0 - FMath::Sqrt(TSq));
		case EPCGUtilsPainterBlurFalloff::Gaussian:
		default:
			// Radius = 3 sigma: exp(-d^2 / (2 sigma^2)) with sigma = R / 3.
			return FMath::Exp(-4.5 * TSq);
		}
	}

	class FBlurPainterOperation final : public FPCGUtilsPainterDecoratorOperation
	{
	public:
		explicit FBlurPainterOperation(const UPCGDynMeshBlurPainterFactoryData* InFactory)
			: FPCGUtilsPainterDecoratorOperation(InFactory), Factory(InFactory)
		{
		}

		virtual bool Prepare(const FPCGUtilsDynMeshPainterEvaluationContext& InPainterContext) override
		{
			using namespace UE::Geometry;

			if (!FPCGUtilsPainterDecoratorOperation::Prepare(InPainterContext))
			{
				return false;
			}

			double Radius = 0.0;
			FText Error;
			if (!Factory->Radius.TryReadDataValue(InPainterContext.MeshData, Radius, Error,
				LOCTEXT("RadiusLabel", "Painter | Blur: Radius"), InPainterContext.DataSetIndex))
			{
				PCGLog::LogErrorOnGraph(Error, Context);
				return false;
			}
			if (!FMath::IsFinite(Radius))
			{
				PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("NonFiniteRadius",
					"Painter | Blur: Radius (input {0}) is not a finite number."),
					FText::AsNumber(InPainterContext.DataSetIndex)), Context);
				return false;
			}
			// A constant Radius is clamped by the UI, but an override pin can still deliver a negative value.
			if (Radius < 0.0)
			{
				PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("NegativeRadius",
					"Painter | Blur: Radius (input {0}) is {1}; it must be 0 or greater."),
					FText::AsNumber(InPainterContext.DataSetIndex), FText::AsNumber(Radius)), Context);
				return false;
			}

			const FDynamicMesh3& Mesh = *InPainterContext.Mesh;
			TArray<int32> VertexIDs;
			VertexIDs.Reserve(Mesh.VertexCount());
			for (const int32 VertexID : Mesh.VertexIndicesItr())
			{
				VertexIDs.Add(VertexID);
			}

			// The connected Painter's field, sampled once per vertex exactly as the paint traversal samples it.
			const FPCGUtilsPainterSampleBuilder SampleBuilder(Mesh, InPainterContext.LocalToWorld);
			const bool bWorld = Factory->Space == EPCGUtilsDynMeshPainterCoordinateSpace::World;
			TArray<FVector3d> Positions;
			Positions.SetNumZeroed(Mesh.MaxVertexID());
			TArray<FPCGUtilsDynMeshPainterValue> SourceValues;
			SourceValues.SetNum(Mesh.MaxVertexID());
			ParallelFor(VertexIDs.Num(), [&](int32 Index)
			{
				const int32 VertexID = VertexIDs[Index];
				const FPCGUtilsDynMeshPainterSample Sample = SampleBuilder.MakeSample(VertexID);
				Positions[VertexID] = FVector3d(bWorld ? Sample.WorldPosition : Sample.LocalPosition);
				SourceValues[VertexID] = SourceOperation->Evaluate(Sample);
			});

			if (Radius <= 0.0 || VertexIDs.Num() < 2)
			{
				Blurred = MoveTemp(SourceValues);
				return true;
			}

			TArray<int32> IslandOfVertex;
			if (Factory->bKeepIslandsSeparate)
			{
				IslandOfVertex.Init(INDEX_NONE, Mesh.MaxVertexID());
				FMeshConnectedComponents Components(&Mesh);
				Components.FindConnectedVertices();
				for (int32 ComponentIndex = 0; ComponentIndex < Components.Components.Num(); ++ComponentIndex)
				{
					for (const int32 VertexID : Components.Components[ComponentIndex].Indices)
					{
						if (IslandOfVertex.IsValidIndex(VertexID))
						{
							IslandOfVertex[VertexID] = ComponentIndex;
						}
					}
				}
			}

			TPointHashGrid3d<int32> Grid(Radius, INDEX_NONE);
			Grid.Reserve(VertexIDs.Num());
			for (const int32 VertexID : VertexIDs)
			{
				Grid.InsertPointUnsafe(VertexID, Positions[VertexID]);
			}

			const bool bScalar = SourceOperation->GetOutputType() == EPCGUtilsDynMeshPainterValueType::Scalar;
			const double RadiusSq = Radius * Radius;
			const EPCGUtilsPainterBlurFalloff Falloff = Factory->Falloff;

			Blurred.SetNum(Mesh.MaxVertexID());
			ParallelFor(VertexIDs.Num(), [&](int32 Index)
			{
				const int32 VertexID = VertexIDs[Index];
				const FVector3d& Center = Positions[VertexID];
				// An isolated loose vertex (INDEX_NONE island) only averages with other loose vertices' INDEX_NONE,
				// which is harmless: they are themselves unconnected single-vertex islands.
				const int32 Island = IslandOfVertex.IsEmpty() ? INDEX_NONE : IslandOfVertex[VertexID];

				double ScalarSum = 0.0;
				double ScalarWeight = 0.0;
				double ChannelSum[4] = {0.0, 0.0, 0.0, 0.0};
				double ChannelWeight[4] = {0.0, 0.0, 0.0, 0.0};

				Grid.EnumeratePointsInBall(Center, Radius,
					[&Positions, &Center](const int32& Other) { return FVector3d::DistSquared(Positions[Other], Center); },
					[&](int32 Other, double DistanceSq)
					{
						const double Weight = FalloffWeight(Falloff, DistanceSq, RadiusSq);
						if (Weight <= 0.0)
						{
							return true;
						}
						const FPCGUtilsDynMeshPainterValue& Value = SourceValues[Other];
						if (bScalar)
						{
							if (FMath::IsFinite(Value.Scalar))
							{
								ScalarSum += Weight * Value.Scalar;
								ScalarWeight += Weight;
							}
							return true;
						}
						for (int32 Channel = 0; Channel < 4; ++Channel)
						{
							// A scalar from a color Painter broadcasts to every channel, as it would when painted.
							const bool bDefined = Value.Type == EPCGUtilsDynMeshPainterValueType::Scalar
								|| EnumHasAnyFlags(Value.ColorChannels, static_cast<EPCGUtilsDynMeshPainterColorChannel>(1 << Channel));
							const float ChannelValue = Value.Type == EPCGUtilsDynMeshPainterValueType::Scalar
								? Value.Scalar : Value.Color[Channel];
							if (bDefined && FMath::IsFinite(ChannelValue))
							{
								ChannelSum[Channel] += Weight * ChannelValue;
								ChannelWeight[Channel] += Weight;
							}
						}
						return true;
					},
					[&IslandOfVertex, Island](const int32& Other)
					{
						return !IslandOfVertex.IsEmpty() && IslandOfVertex[Other] != Island;
					});

				if (bScalar)
				{
					// The vertex itself always contributes with weight 1, so the sum is only empty for a non-finite
					// source value; keep that value rather than inventing one.
					Blurred[VertexID] = ScalarWeight > 0.0
						? FPCGUtilsDynMeshPainterValue::MakeScalar(static_cast<float>(ScalarSum / ScalarWeight))
						: SourceValues[VertexID];
					return;
				}

				FVector4f Color = FVector4f::Zero();
				EPCGUtilsDynMeshPainterColorChannel Channels = EPCGUtilsDynMeshPainterColorChannel::None;
				for (int32 Channel = 0; Channel < 4; ++Channel)
				{
					if (ChannelWeight[Channel] > 0.0)
					{
						Color[Channel] = static_cast<float>(ChannelSum[Channel] / ChannelWeight[Channel]);
						Channels |= static_cast<EPCGUtilsDynMeshPainterColorChannel>(1 << Channel);
					}
				}
				Blurred[VertexID] = FPCGUtilsDynMeshPainterValue::MakeColor(Color, Channels);
			});
			return true;
		}

		virtual FPCGUtilsDynMeshPainterValue Evaluate(const FPCGUtilsDynMeshPainterSample& Sample) const override
		{
			// Every canonical vertex was blurred in Prepare; anything else (no vertex ID) gets the unblurred field.
			return Blurred.IsValidIndex(Sample.VertexID) && InPainterContextHasVertex(Sample.VertexID)
				? Blurred[Sample.VertexID]
				: SourceOperation->Evaluate(Sample);
		}

	private:
		bool InPainterContextHasVertex(int32 VertexID) const
		{
			return PainterContext && PainterContext->Mesh && PainterContext->Mesh->IsVertex(VertexID);
		}

		TObjectPtr<const UPCGDynMeshBlurPainterFactoryData> Factory;
		TArray<FPCGUtilsDynMeshPainterValue> Blurred;
	};
}

TSharedPtr<FPCGUtilsDynMeshPainterOperation> UPCGDynMeshBlurPainterFactoryData::CreateOperationInternal() const
{
	return MakeShared<FBlurPainterOperation>(this);
}

void UPCGDynMeshBlurPainterFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (!bFullDataCrc)
	{
		return;
	}
	PCGUtilsPainterShorthands::AddToCrc(Ar, Radius, Radius.Constant);
	uint8 FalloffValue = static_cast<uint8>(Falloff);
	uint8 SpaceValue = static_cast<uint8>(Space);
	bool bIslands = bKeepIslandsSeparate;
	Ar << FalloffValue;
	Ar << SpaceValue;
	Ar << bIslands;
}

UPCGDynMeshBlurPainterProviderSettings::UPCGDynMeshBlurPainterProviderSettings()
	: Radius(50.0, TEXT("BlurRadius"))
{
}

#if WITH_EDITOR
FText UPCGDynMeshBlurPainterProviderSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Painter | Blur");
}

FText UPCGDynMeshBlurPainterProviderSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Blurs the connected Painter: each vertex takes the weighted average of the Painter's value over every "
		"vertex within Radius (straight-line distance). Radius is a constant or is read from each target mesh's "
		"@Data domain. Keep Islands Separate stops separate pieces bleeding into each other.");
}

FString UPCGDynMeshBlurPainterProviderSettings::GetAdditionalTitleInformation() const
{
	return Radius.IsAttribute() ? Radius.Attribute.ToString() : FString::SanitizeFloat(Radius.Constant);
}

TArray<FPCGSettingsOverridableParam> UPCGDynMeshBlurPainterProviderSettings::GatherOverridableParams() const
{
	TArray<FPCGSettingsOverridableParam> Params = Super::GatherOverridableParams();
	PCGUtilsInputShorthands::ApplyOverrideLabels(Params);
	return Params;
}
#endif

UPCGUtilsDynMeshFactoryData* UPCGDynMeshBlurPainterProviderSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	UPCGDynMeshBlurPainterFactoryData* Factory = InFactory
		? Cast<UPCGDynMeshBlurPainterFactoryData>(InFactory)
		: PCGUtilsAsyncObjects::NewObject<UPCGDynMeshBlurPainterFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->Radius = Radius;
	Factory->Falloff = Falloff;
	Factory->Space = Space;
	Factory->bKeepIslandsSeparate = bKeepIslandsSeparate;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE
