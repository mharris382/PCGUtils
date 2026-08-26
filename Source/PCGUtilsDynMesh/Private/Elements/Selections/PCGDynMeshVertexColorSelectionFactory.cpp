// Copyright Max Harris

#include "Elements/Selections/PCGDynMeshVertexColorSelectionFactory.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "PCGContext.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshVertexColorSelectionFactory"

namespace
{
	float GetChannel(const FLinearColor& Color, EPCGUtilsDynMeshVertexColorChannel Channel)
	{
		switch (Channel)
		{
		case EPCGUtilsDynMeshVertexColorChannel::Green: return Color.G;
		case EPCGUtilsDynMeshVertexColorChannel::Blue: return Color.B;
		case EPCGUtilsDynMeshVertexColorChannel::Alpha: return Color.A;
		case EPCGUtilsDynMeshVertexColorChannel::Red:
		default: return Color.R;
		}
	}

	const TCHAR* GetChannelName(EPCGUtilsDynMeshVertexColorChannel Channel)
	{
		switch (Channel)
		{
		case EPCGUtilsDynMeshVertexColorChannel::Green: return TEXT("G");
		case EPCGUtilsDynMeshVertexColorChannel::Blue: return TEXT("B");
		case EPCGUtilsDynMeshVertexColorChannel::Alpha: return TEXT("A");
		case EPCGUtilsDynMeshVertexColorChannel::Red:
		default: return TEXT("R");
		}
	}

	class FVertexColorSelectionOperation final : public FPCGUtilsDynMeshSelectionOperation
	{
	public:
		explicit FVertexColorSelectionOperation(const UPCGDynMeshVertexColorSelectionFactoryData* InFactory)
			: Factory(InFactory)
		{
		}

		virtual bool Initialize(const FPCGUtilsDynMeshSelectionEvaluationContext& InSelectionContext) override
		{
			if (!FPCGUtilsDynMeshSelectionOperation::Initialize(InSelectionContext) || !Factory)
			{
				return false;
			}

			const UE::Geometry::FDynamicMesh3& Mesh = InSelectionContext.Mesh;
			ColorOverlay = Mesh.HasAttributes() ? Mesh.Attributes()->PrimaryColors() : nullptr;
			VertexColors.Init(FLinearColor::Transparent, Mesh.MaxVertexID());
			VertexHasColor.Init(false, Mesh.MaxVertexID());

			if (ColorOverlay)
			{
				TArray<int32> ColorCounts;
				ColorCounts.Init(0, Mesh.MaxVertexID());
				for (const int32 TriangleID : Mesh.TriangleIndicesItr())
				{
					if (!ColorOverlay->IsSetTriangle(TriangleID))
					{
						continue;
					}

					const UE::Geometry::FIndex3i Triangle = Mesh.GetTriangle(TriangleID);
					FVector4f A, B, C;
					ColorOverlay->GetTriElements(TriangleID, A, B, C);
					AccumulateVertexColor(Triangle.A, A, ColorCounts);
					AccumulateVertexColor(Triangle.B, B, ColorCounts);
					AccumulateVertexColor(Triangle.C, C, ColorCounts);
				}

				for (int32 VertexID = 0; VertexID < ColorCounts.Num(); ++VertexID)
				{
					if (ColorCounts[VertexID] > 0)
					{
						VertexColors[VertexID] *= 1.0f / static_cast<float>(ColorCounts[VertexID]);
						VertexHasColor[VertexID] = true;
					}
				}
			}
			else if (Mesh.HasVertexColors())
			{
				for (const int32 VertexID : Mesh.VertexIndicesItr())
				{
					const FVector3f Color = Mesh.GetVertexColor(VertexID);
					VertexColors[VertexID] = FLinearColor(Color.X, Color.Y, Color.Z, 1.0f);
					VertexHasColor[VertexID] = true;
				}
			}
			else
			{
				PCGLog::LogErrorOnGraph(
					LOCTEXT("MissingVertexColors", "Select by Vertex Color requires a mesh with vertex colors."),
					Context);
				return false;
			}

			Tolerance = FMath::Max(0.0, Factory->Tolerance);
			ColorDistanceThreshold = FMath::Max(0.0, Factory->ColorDistanceThreshold);
			return true;
		}

		virtual bool TestElement(int32 ElementID) const override
		{
			FLinearColor Color;
			if (!GetElementColor(ElementID, Color))
			{
				return false;
			}

			if (Factory->SelectionMode == EPCGUtilsDynMeshVertexColorSelectionMode::Channel)
			{
				return PCGUtilsDynMeshSelectionComparison::Compare(
					GetChannel(Color, Factory->Channel), Factory->ChannelValue,
					Tolerance, Factory->Comparison);
			}

			const FLinearColor Delta = Color - Factory->ReferenceColor;
			const double Distance = FMath::Sqrt(
				FMath::Square(Delta.R) + FMath::Square(Delta.G) +
				FMath::Square(Delta.B) + FMath::Square(Delta.A));
			return Distance <= ColorDistanceThreshold;
		}

	private:
		void AccumulateVertexColor(int32 VertexID, const FVector4f& Color, TArray<int32>& ColorCounts)
		{
			VertexColors[VertexID] += FLinearColor(Color.X, Color.Y, Color.Z, Color.W);
			++ColorCounts[VertexID];
		}

		bool GetVertexColor(int32 VertexID, FLinearColor& OutColor) const
		{
			if (!VertexColors.IsValidIndex(VertexID) || !VertexHasColor[VertexID])
			{
				return false;
			}
			OutColor = VertexColors[VertexID];
			return true;
		}

		bool GetElementColor(int32 ElementID, FLinearColor& OutColor) const
		{
			const UE::Geometry::FDynamicMesh3& Mesh = SelectionContext->Mesh;
			if (SelectionContext->Domain.ElementType == UE::Geometry::EGeometryElementType::Vertex)
			{
				return Mesh.IsVertex(ElementID) && GetVertexColor(ElementID, OutColor);
			}

			if (SelectionContext->Domain.ElementType == UE::Geometry::EGeometryElementType::Edge)
			{
				if (!Mesh.IsEdge(ElementID))
				{
					return false;
				}
				const UE::Geometry::FIndex2i Edge = Mesh.GetEdgeV(ElementID);
				FLinearColor A, B;
				if (!GetVertexColor(Edge.A, A) || !GetVertexColor(Edge.B, B))
				{
					return false;
				}
				OutColor = (A + B) * 0.5f;
				return true;
			}

			if (SelectionContext->Domain.ElementType == UE::Geometry::EGeometryElementType::Face &&
				Mesh.IsTriangle(ElementID))
			{
				if (ColorOverlay && ColorOverlay->IsSetTriangle(ElementID))
				{
					FVector4f A, B, C;
					ColorOverlay->GetTriElements(ElementID, A, B, C);
					OutColor = (FLinearColor(A.X, A.Y, A.Z, A.W) +
						FLinearColor(B.X, B.Y, B.Z, B.W) +
						FLinearColor(C.X, C.Y, C.Z, C.W)) / 3.0f;
					return true;
				}

				const UE::Geometry::FIndex3i Triangle = Mesh.GetTriangle(ElementID);
				FLinearColor A, B, C;
				if (!GetVertexColor(Triangle.A, A) || !GetVertexColor(Triangle.B, B) ||
					!GetVertexColor(Triangle.C, C))
				{
					return false;
				}
				OutColor = (A + B + C) / 3.0f;
				return true;
			}

			return false;
		}

		TObjectPtr<const UPCGDynMeshVertexColorSelectionFactoryData> Factory;
		const UE::Geometry::FDynamicMeshColorOverlay* ColorOverlay = nullptr;
		TArray<FLinearColor> VertexColors;
		TBitArray<> VertexHasColor;
		double Tolerance = 0.0;
		double ColorDistanceThreshold = 0.0;
	};
}

bool UPCGDynMeshVertexColorSelectionFactoryData::SupportsDomain(
	const FPCGUtilsDynMeshSelectionDomain& Domain) const
{
	return Domain.TopologyType == UE::Geometry::EGeometryTopologyType::Triangle &&
		(Domain.ElementType == UE::Geometry::EGeometryElementType::Vertex ||
		 Domain.ElementType == UE::Geometry::EGeometryElementType::Edge ||
		 Domain.ElementType == UE::Geometry::EGeometryElementType::Face);
}

TSharedPtr<FPCGUtilsDynMeshSelectionOperation>
UPCGDynMeshVertexColorSelectionFactoryData::CreateOperationInternal() const
{
	return MakeShared<FVertexColorSelectionOperation>(this);
}

void UPCGDynMeshVertexColorSelectionFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (bFullDataCrc)
	{
		uint8 ModeValue = static_cast<uint8>(SelectionMode);
		uint8 ChannelValueEnum = static_cast<uint8>(Channel);
		uint8 ComparisonValue = static_cast<uint8>(Comparison);
		double ChannelValueCopy = ChannelValue;
		double ToleranceCopy = Tolerance;
		FLinearColor ReferenceColorCopy = ReferenceColor;
		double ColorDistanceCopy = ColorDistanceThreshold;
		Ar << ModeValue;
		Ar << ChannelValueEnum;
		Ar << ComparisonValue;
		Ar << ChannelValueCopy;
		Ar << ToleranceCopy;
		Ar << ReferenceColorCopy;
		Ar << ColorDistanceCopy;
	}
}

#if WITH_EDITOR
FText UPCGDynMeshVertexColorSelectionFactoryProviderSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Select by Vertex Color");
}

TArray<FText> UPCGDynMeshVertexColorSelectionFactoryProviderSettings::GetNodeTitleAliases() const
{
	return {
		LOCTEXT("ColorAlias", "Color Selector"),
		LOCTEXT("MeshColorAlias", "Mesh Color Selector")
	};
}

FText UPCGDynMeshVertexColorSelectionFactoryProviderSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Creates a reusable vertex-color predicate. Channel mode compares R, G, B, or A; Linear Color Distance mode selects colors within an RGBA distance threshold.");
}

FString UPCGDynMeshVertexColorSelectionFactoryProviderSettings::GetAdditionalTitleInformation() const
{
	if (SelectionMode == EPCGUtilsDynMeshVertexColorSelectionMode::Channel)
	{
		return FString::Printf(TEXT("%s %s %.3f"), GetChannelName(Channel),
			PCGUtilsDynMeshSelectionComparison::GetOperator(Comparison), ChannelValue);
	}
	return FString::Printf(TEXT("RGBA Distance <= %.3f"), ColorDistanceThreshold);
}
#endif

FName UPCGDynMeshVertexColorSelectionFactoryProviderSettings::GetMainOutputPin() const
{
	return PCGUtilsDynMeshSelectionFactoryConstants::OutputPin;
}

const FPCGDataTypeBaseId& UPCGDynMeshVertexColorSelectionFactoryProviderSettings::GetFactoryTypeId() const
{
	return FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId();
}

UPCGUtilsDynMeshFactoryData* UPCGDynMeshVertexColorSelectionFactoryProviderSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	UPCGDynMeshVertexColorSelectionFactoryData* Factory = InFactory
		? Cast<UPCGDynMeshVertexColorSelectionFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGDynMeshVertexColorSelectionFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->Priority = Priority;
	Factory->SelectionMode = SelectionMode;
	Factory->Channel = Channel;
	Factory->Comparison = Comparison;
	Factory->ChannelValue = ChannelValue;
	Factory->Tolerance = Tolerance;
	Factory->ReferenceColor = ReferenceColor;
	Factory->ColorDistanceThreshold = ColorDistanceThreshold;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE
