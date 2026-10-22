// Copyright Max Harris

#include "Geometry/PCGUtilsDynMeshProperties.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/DynamicMeshOverlay.h"
#include "Materials/MaterialInterface.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataDomain.h"
#include "PCGContext.h"
#include "Selections/MeshConnectedComponents.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsDynMeshProperties"

namespace PCGUtilsDynMeshPropertyWriting
{
	struct FNamedOutput
	{
		bool bEnabled;
		FName AttributeName;
		const TCHAR* PropertyLabel;
	};

	TArray<FNamedOutput, TInlineAllocator<16>> NamedOutputs(const FPCGUtilsDynMeshFreeProperties& P)
	{
		return {
			{P.bOutputTriangleCount, P.TriangleCountAttributeName, TEXT("Triangle Count Attribute Name")},
			{P.bOutputVertexCount, P.VertexCountAttributeName, TEXT("Vertex Count Attribute Name")},
			{P.bOutputEdgeCount, P.EdgeCountAttributeName, TEXT("Edge Count Attribute Name")},
			{P.bOutputEulerCharacteristic, P.EulerCharacteristicAttributeName, TEXT("Euler Characteristic Attribute Name")},
			{P.bOutputIsCompact, P.IsCompactAttributeName, TEXT("Is Compact Attribute Name")},
			{P.bOutputMaterialSlotCount, P.MaterialSlotCountAttributeName, TEXT("Material Slot Count Attribute Name")},
			{P.bOutputMaterials, P.MaterialAttributeName, TEXT("Material Attribute Name")},
			{P.bOutputUVChannelCount, P.UVChannelCountAttributeName, TEXT("UV Channel Count Attribute Name")},
			{P.bOutputHasNormals, P.HasNormalsAttributeName, TEXT("Has Normals Attribute Name")},
			{P.bOutputHasVertexColors, P.HasVertexColorsAttributeName, TEXT("Has Vertex Colors Attribute Name")},
			{P.bOutputHasPolyGroups, P.HasPolyGroupsAttributeName, TEXT("Has PolyGroups Attribute Name")},
			{P.bOutputPolyGroupLayerCount, P.PolyGroupLayerCountAttributeName, TEXT("PolyGroup Layer Count Attribute Name")},
		};
	}

	TArray<FNamedOutput, TInlineAllocator<16>> NamedOutputs(const FPCGUtilsDynMeshComputedProperties& P)
	{
		return {
			{P.bOutputBoundsMin, P.BoundsMinAttributeName, TEXT("Bounds Min Attribute Name")},
			{P.bOutputBoundsMax, P.BoundsMaxAttributeName, TEXT("Bounds Max Attribute Name")},
			{P.bOutputSurfaceArea, P.SurfaceAreaAttributeName, TEXT("Surface Area Attribute Name")},
			{P.bOutputVolume, P.VolumeAttributeName, TEXT("Volume Attribute Name")},
			{P.bOutputIsClosed, P.IsClosedAttributeName, TEXT("Is Closed Attribute Name")},
			{P.bOutputBoundaryEdgeCount, P.BoundaryEdgeCountAttributeName, TEXT("Boundary Edge Count Attribute Name")},
			{P.bOutputConnectedComponentCount, P.ConnectedComponentCountAttributeName, TEXT("Connected Component Count Attribute Name")},
			{P.bOutputPolyGroupCount, P.PolyGroupCountAttributeName, TEXT("PolyGroup Count Attribute Name")},
			{P.bOutputDegenerateTriangleCount, P.DegenerateTriangleCountAttributeName, TEXT("Degenerate Triangle Count Attribute Name")},
			{P.bOutputMinTriangleAngle, P.MinTriangleAngleAttributeName, TEXT("Min Triangle Angle Attribute Name")},
			{P.bOutputInvertedNormalTriangleCount, P.InvertedNormalTriangleCountAttributeName, TEXT("Inverted Normal Triangle Count Attribute Name")},
			{P.bOutputNormalSeamEdgeCount, P.NormalSeamEdgeCountAttributeName, TEXT("Normal Seam Edge Count Attribute Name")},
			{P.bOutputBowtieVertexCount, P.BowtieVertexCountAttributeName, TEXT("Bowtie Vertex Count Attribute Name")},
		};
	}

	template <typename PropertiesType>
	bool ValidateNamedOutputs(const PropertiesType& Properties, FPCGContext* Context)
	{
		bool bValid = true;
		for (const FNamedOutput& Output : NamedOutputs(Properties))
		{
			if (!Output.bEnabled)
			{
				continue;
			}
			if (Output.AttributeName.IsNone())
			{
				PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("NoneName",
					"{0} is None while its output is enabled; that property was not written."),
					FText::FromString(Output.PropertyLabel)), Context);
				continue;
			}
			if (!FPCGMetadataAttributeBase::IsValidName(Output.AttributeName))
			{
				PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("InvalidName",
					"{0} = '{1}' is not a valid PCG attribute name (FPCGMetadataAttributeBase::IsValidName rejected it)."),
					FText::FromString(Output.PropertyLabel), FText::FromName(Output.AttributeName)), Context);
				bValid = false;
			}
		}
		return bValid;
	}

	bool ShouldWrite(bool bEnabled, FName AttributeName)
	{
		return bEnabled && !AttributeName.IsNone() && FPCGMetadataAttributeBase::IsValidName(AttributeName);
	}

	template <typename T>
	void WriteDataAttribute(UPCGDynamicMeshData& Data, FName AttributeName, const T& Value)
	{
		UPCGMetadata* Metadata = Data.MutableMetadata();
		FPCGMetadataDomain* Domain = Metadata ? Metadata->GetMetadataDomain(PCGMetadataDomainID::Data) : nullptr;
		if (!Domain)
		{
			return;
		}
		// Replace, whatever type an attribute of this name had before (including one inherited from upstream).
		Domain->DeleteAttribute(AttributeName);
		Domain->CreateAttribute<T>(AttributeName, Value, /*bAllowsInterpolation=*/false, /*bOverrideParent=*/true);
	}

	const UE::Geometry::FDynamicMesh3* GetMesh(const UPCGDynamicMeshData& Data)
	{
		const UDynamicMesh* Object = Data.GetDynamicMesh();
		return Object ? Object->GetMeshPtr() : nullptr;
	}
}

bool FPCGUtilsDynMeshFreeProperties::AnyEnabled() const
{
	for (const PCGUtilsDynMeshPropertyWriting::FNamedOutput& Output : PCGUtilsDynMeshPropertyWriting::NamedOutputs(*this))
	{
		if (Output.bEnabled) { return true; }
	}
	return false;
}

bool FPCGUtilsDynMeshComputedProperties::AnyEnabled() const
{
	for (const PCGUtilsDynMeshPropertyWriting::FNamedOutput& Output : PCGUtilsDynMeshPropertyWriting::NamedOutputs(*this))
	{
		if (Output.bEnabled) { return true; }
	}
	return false;
}

bool PCGUtilsDynMeshProperties::Validate(const FPCGUtilsDynMeshFreeProperties& Properties, FPCGContext* Context)
{
	bool bValid = PCGUtilsDynMeshPropertyWriting::ValidateNamedOutputs(Properties, Context);
	if (Properties.bOutputMaterials && Properties.MaxMaterialCount < 1)
	{
		PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("MaxMaterialCount",
			"Max Material Count = {0}; expected at least 1."), FText::AsNumber(Properties.MaxMaterialCount)), Context);
		bValid = false;
	}
	return bValid;
}

bool PCGUtilsDynMeshProperties::Validate(const FPCGUtilsDynMeshComputedProperties& Properties, FPCGContext* Context)
{
	return PCGUtilsDynMeshPropertyWriting::ValidateNamedOutputs(Properties, Context);
}

void PCGUtilsDynMeshProperties::Write(UPCGDynamicMeshData& Data, const FPCGUtilsDynMeshFreeProperties& P)
{
	using namespace PCGUtilsDynMeshPropertyWriting;
	const UE::Geometry::FDynamicMesh3* Mesh = GetMesh(Data);
	if (!Mesh)
	{
		return;
	}
	const UE::Geometry::FDynamicMeshAttributeSet* Attributes = Mesh->HasAttributes() ? Mesh->Attributes() : nullptr;

	if (ShouldWrite(P.bOutputTriangleCount, P.TriangleCountAttributeName))
	{
		WriteDataAttribute<int32>(Data, P.TriangleCountAttributeName, Mesh->TriangleCount());
	}
	if (ShouldWrite(P.bOutputVertexCount, P.VertexCountAttributeName))
	{
		WriteDataAttribute<int32>(Data, P.VertexCountAttributeName, Mesh->VertexCount());
	}
	if (ShouldWrite(P.bOutputEdgeCount, P.EdgeCountAttributeName))
	{
		WriteDataAttribute<int32>(Data, P.EdgeCountAttributeName, Mesh->EdgeCount());
	}
	if (ShouldWrite(P.bOutputEulerCharacteristic, P.EulerCharacteristicAttributeName))
	{
		WriteDataAttribute<int32>(Data, P.EulerCharacteristicAttributeName,
			Mesh->VertexCount() - Mesh->EdgeCount() + Mesh->TriangleCount());
	}
	if (ShouldWrite(P.bOutputIsCompact, P.IsCompactAttributeName))
	{
		WriteDataAttribute<bool>(Data, P.IsCompactAttributeName, Mesh->IsCompact());
	}
	if (ShouldWrite(P.bOutputMaterialSlotCount, P.MaterialSlotCountAttributeName))
	{
		WriteDataAttribute<int32>(Data, P.MaterialSlotCountAttributeName, Data.GetMaterials().Num());
	}
	if (ShouldWrite(P.bOutputMaterials, P.MaterialAttributeName) && P.MaxMaterialCount >= 1)
	{
		const TArray<TObjectPtr<UMaterialInterface>>& Materials = Data.GetMaterials();
		for (int32 Slot = 0; Slot < P.MaxMaterialCount; ++Slot)
		{
			const FName Name = P.MaxMaterialCount == 1
				? P.MaterialAttributeName
				: FName(*FString::Printf(TEXT("%s%d"), *P.MaterialAttributeName.ToString(), Slot));
			const UMaterialInterface* Material = Materials.IsValidIndex(Slot) ? Materials[Slot].Get() : nullptr;
			WriteDataAttribute<FSoftObjectPath>(Data, Name, FSoftObjectPath(Material));
		}
	}
	if (ShouldWrite(P.bOutputUVChannelCount, P.UVChannelCountAttributeName))
	{
		WriteDataAttribute<int32>(Data, P.UVChannelCountAttributeName, Attributes ? Attributes->NumUVLayers() : 0);
	}
	if (ShouldWrite(P.bOutputHasNormals, P.HasNormalsAttributeName))
	{
		WriteDataAttribute<bool>(Data, P.HasNormalsAttributeName, Attributes && Attributes->PrimaryNormals());
	}
	if (ShouldWrite(P.bOutputHasVertexColors, P.HasVertexColorsAttributeName))
	{
		WriteDataAttribute<bool>(Data, P.HasVertexColorsAttributeName, Attributes && Attributes->HasPrimaryColors());
	}
	if (ShouldWrite(P.bOutputHasPolyGroups, P.HasPolyGroupsAttributeName))
	{
		WriteDataAttribute<bool>(Data, P.HasPolyGroupsAttributeName, Mesh->HasTriangleGroups());
	}
	if (ShouldWrite(P.bOutputPolyGroupLayerCount, P.PolyGroupLayerCountAttributeName))
	{
		WriteDataAttribute<int32>(Data, P.PolyGroupLayerCountAttributeName, Attributes ? Attributes->NumPolygroupLayers() : 0);
	}
}

void PCGUtilsDynMeshProperties::Write(UPCGDynamicMeshData& Data, const FPCGUtilsDynMeshComputedProperties& P)
{
	using namespace PCGUtilsDynMeshPropertyWriting;
	using namespace UE::Geometry;
	const FDynamicMesh3* Mesh = GetMesh(Data);
	if (!Mesh)
	{
		return;
	}
	const FDynamicMeshNormalOverlay* Normals =
		Mesh->HasAttributes() ? Mesh->Attributes()->PrimaryNormals() : nullptr;

	if (ShouldWrite(P.bOutputBoundsMin, P.BoundsMinAttributeName) || ShouldWrite(P.bOutputBoundsMax, P.BoundsMaxAttributeName))
	{
		const FAxisAlignedBox3d Bounds = Mesh->GetBounds(true);
		const bool bEmpty = Mesh->VertexCount() == 0;
		if (ShouldWrite(P.bOutputBoundsMin, P.BoundsMinAttributeName))
		{
			WriteDataAttribute<FVector>(Data, P.BoundsMinAttributeName, bEmpty ? FVector::ZeroVector : FVector(Bounds.Min));
		}
		if (ShouldWrite(P.bOutputBoundsMax, P.BoundsMaxAttributeName))
		{
			WriteDataAttribute<FVector>(Data, P.BoundsMaxAttributeName, bEmpty ? FVector::ZeroVector : FVector(Bounds.Max));
		}
	}

	// One pass over triangles for everything triangle-shaped.
	const bool bArea = ShouldWrite(P.bOutputSurfaceArea, P.SurfaceAreaAttributeName);
	const bool bVolume = ShouldWrite(P.bOutputVolume, P.VolumeAttributeName);
	const bool bDegenerate = ShouldWrite(P.bOutputDegenerateTriangleCount, P.DegenerateTriangleCountAttributeName);
	const bool bMinAngle = ShouldWrite(P.bOutputMinTriangleAngle, P.MinTriangleAngleAttributeName);
	const bool bInverted = ShouldWrite(P.bOutputInvertedNormalTriangleCount, P.InvertedNormalTriangleCountAttributeName);
	const bool bGroups = ShouldWrite(P.bOutputPolyGroupCount, P.PolyGroupCountAttributeName);
	if (bArea || bVolume || bDegenerate || bMinAngle || bInverted || bGroups)
	{
		double Area = 0.0;
		double SignedVolume = 0.0;
		int32 DegenerateCount = 0;
		int32 InvertedCount = 0;
		double MinAngle = TNumericLimits<double>::Max();
		TSet<int32> Groups;
		const bool bHasGroups = Mesh->HasTriangleGroups();

		for (const int32 TriangleID : Mesh->TriangleIndicesItr())
		{
			FVector3d A, B, C;
			Mesh->GetTriVertices(TriangleID, A, B, C);
			const FVector3d Cross = (B - A).Cross(C - A);
			const double TriangleArea = 0.5 * Cross.Length();
			Area += TriangleArea;
			SignedVolume += A.Dot(B.Cross(C)) / 6.0;

			if (bGroups && bHasGroups)
			{
				Groups.Add(Mesh->GetTriangleGroup(TriangleID));
			}

			const bool bIsDegenerate = TriangleArea <= P.DegenerateAreaThreshold;
			DegenerateCount += bIsDegenerate ? 1 : 0;
			if (bIsDegenerate || TriangleArea <= UE_DOUBLE_SMALL_NUMBER)
			{
				continue;
			}

			if (bMinAngle)
			{
				const FVector3d Corners[3] = {A, B, C};
				for (int32 Corner = 0; Corner < 3; ++Corner)
				{
					const FVector3d U = (Corners[(Corner + 1) % 3] - Corners[Corner]).GetSafeNormal();
					const FVector3d V = (Corners[(Corner + 2) % 3] - Corners[Corner]).GetSafeNormal();
					MinAngle = FMath::Min(MinAngle, FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(U.Dot(V), -1.0, 1.0))));
				}
			}

			if (bInverted && Normals && Normals->IsSetTriangle(TriangleID))
			{
				const FIndex3i Elements = Normals->GetTriangle(TriangleID);
				FVector3f Sum = FVector3f::ZeroVector;
				for (int32 Corner = 0; Corner < 3; ++Corner)
				{
					FVector3f Element;
					Normals->GetElement(Elements[Corner], Element);
					Sum += Element;
				}
				if (FVector3d(Sum).Dot(Cross) < 0.0)
				{
					++InvertedCount;
				}
			}
		}

		if (bArea) { WriteDataAttribute<double>(Data, P.SurfaceAreaAttributeName, Area); }
		if (bVolume) { WriteDataAttribute<double>(Data, P.VolumeAttributeName, SignedVolume); }
		if (bDegenerate) { WriteDataAttribute<int32>(Data, P.DegenerateTriangleCountAttributeName, DegenerateCount); }
		if (bMinAngle)
		{
			WriteDataAttribute<double>(Data, P.MinTriangleAngleAttributeName,
				MinAngle == TNumericLimits<double>::Max() ? 0.0 : MinAngle);
		}
		if (bInverted) { WriteDataAttribute<int32>(Data, P.InvertedNormalTriangleCountAttributeName, InvertedCount); }
		if (bGroups) { WriteDataAttribute<int32>(Data, P.PolyGroupCountAttributeName, Groups.Num()); }
	}

	// One pass over edges.
	const bool bClosed = ShouldWrite(P.bOutputIsClosed, P.IsClosedAttributeName);
	const bool bBoundary = ShouldWrite(P.bOutputBoundaryEdgeCount, P.BoundaryEdgeCountAttributeName);
	const bool bSeams = ShouldWrite(P.bOutputNormalSeamEdgeCount, P.NormalSeamEdgeCountAttributeName);
	if (bClosed || bBoundary || bSeams)
	{
		int32 BoundaryCount = 0;
		int32 SeamCount = 0;
		for (const int32 EdgeID : Mesh->EdgeIndicesItr())
		{
			BoundaryCount += Mesh->IsBoundaryEdge(EdgeID) ? 1 : 0;
			SeamCount += (bSeams && Normals && Normals->IsSeamEdge(EdgeID)) ? 1 : 0;
		}
		if (bClosed) { WriteDataAttribute<bool>(Data, P.IsClosedAttributeName, Mesh->TriangleCount() > 0 && BoundaryCount == 0); }
		if (bBoundary) { WriteDataAttribute<int32>(Data, P.BoundaryEdgeCountAttributeName, BoundaryCount); }
		if (bSeams) { WriteDataAttribute<int32>(Data, P.NormalSeamEdgeCountAttributeName, SeamCount); }
	}

	if (ShouldWrite(P.bOutputConnectedComponentCount, P.ConnectedComponentCountAttributeName))
	{
		FMeshConnectedComponents Components(Mesh);
		Components.FindConnectedTriangles();
		WriteDataAttribute<int32>(Data, P.ConnectedComponentCountAttributeName, Components.Num());
	}

	if (ShouldWrite(P.bOutputBowtieVertexCount, P.BowtieVertexCountAttributeName))
	{
		int32 BowtieCount = 0;
		for (const int32 VertexID : Mesh->VertexIndicesItr())
		{
			BowtieCount += Mesh->IsBowtieVertex(VertexID) ? 1 : 0;
		}
		WriteDataAttribute<int32>(Data, P.BowtieVertexCountAttributeName, BowtieCount);
	}
}

#undef LOCTEXT_NAMESPACE
