#include "Elements/Conversion/PCGDynMeshSample.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "Data/PCGPointArrayData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicSubmesh3.h"
#include "GameFramework/Actor.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "GeometryScript/MeshRepairFunctions.h"
#include "GeometryScript/MeshVoxelFunctions.h"
#include "Helpers/PCGHelpers.h"
#include "Materials/MaterialInterface.h"
#include "MeshTarget/PCGUtilsMeshTargetFunctions.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Sampling/MeshSurfacePointSampling.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshSample"

using namespace UE::Geometry;

namespace
{
	const FName SampleInputPin = TEXT("Mesh");

	// One generated point's geometry + attributes, always in the source DynMesh's local space. World-space
	// conversion (bTransformToWorldSpace) is applied afterwards as a single, separate pass in ExecuteInternal,
	// so the sampling stage below never needs to know about it.
	struct FSampledPoint
	{
		FVector Position = FVector::ZeroVector;
		FVector Normal = FVector::UpVector;
		FVector TangentX = FVector::ForwardVector;
		FLinearColor Color = FLinearColor::White;
		// Already remapped to the original source DynMesh's triangle id when sampling a Selection, via
		// FDynamicSubmesh3::MapTriangleToBaseMesh (see the RemapTriangleId lambda in ExecuteInternal).
		int32 SourceTriangleId = INDEX_NONE;
		TOptional<FVector2D> UV;
		TOptional<int32> MaterialId;
	};

	void SetFrame(FSampledPoint& Point, const FVector& Position, const FVector& Normal)
	{
		Point.Position = Position;
		FVector SafeNormal = Normal;
		if (!SafeNormal.Normalize())
		{
			SafeNormal = FVector::UpVector;
		}
		Point.Normal = SafeNormal;
		Point.TangentX = FRotationMatrix::MakeFromZ(SafeNormal).GetUnitAxis(EAxis::X);
	}

	FTransform ToLocalTransform(const FSampledPoint& Point)
	{
		return FTransform(FRotationMatrix::MakeFromXZ(Point.TangentX, Point.Normal).ToQuat(), Point.Position);
	}

	template<typename OverlayType, typename ValueType>
	ValueType Sample_GetFirstVertexOverlayElement(const FDynamicMesh3& Mesh, const OverlayType* Overlay, int32 VertexID, const ValueType& DefaultValue)
	{
		if (!Overlay)
		{
			return DefaultValue;
		}
		ValueType Result = DefaultValue;
		bool bFound = false;
		Mesh.EnumerateVertexTriangles(VertexID, [&](int32 TriangleID)
		{
			if (bFound || !Overlay->IsSetTriangle(TriangleID))
			{
				return;
			}
			const FIndex3i TriVerts = Mesh.GetTriangle(TriangleID);
			const FIndex3i TriElems = Overlay->GetTriangle(TriangleID);
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				if (TriVerts[Corner] == VertexID && Overlay->IsElement(TriElems[Corner]))
				{
					Result = Overlay->GetElement(TriElems[Corner]);
					bFound = true;
					break;
				}
			}
		});
		return Result;
	}

	FLinearColor InterpolateTriangleColor(const FDynamicMesh3& Mesh, int32 TriangleID, const FVector& Barycentric, const FLinearColor& DefaultColor)
	{
		if (Mesh.HasAttributes())
		{
			if (const FDynamicMeshColorOverlay* Colors = Mesh.Attributes()->PrimaryColors())
			{
				if (Mesh.IsTriangle(TriangleID) && Colors->IsSetTriangle(TriangleID))
				{
					FVector4f A, B, C;
					Colors->GetTriElements(TriangleID, A, B, C);
					const FVector4f Interpolated = (float)Barycentric.X * A + (float)Barycentric.Y * B + (float)Barycentric.Z * C;
					return FLinearColor(Interpolated.X, Interpolated.Y, Interpolated.Z, Interpolated.W);
				}
			}
		}
		return DefaultColor;
	}

	TOptional<FVector2D> InterpolateTriangleUV(const FDynamicMesh3& Mesh, int32 UVChannel, int32 TriangleID, const FVector& Barycentric)
	{
		if (!Mesh.HasAttributes() || UVChannel < 0 || UVChannel >= Mesh.Attributes()->NumUVLayers())
		{
			return {};
		}
		const FDynamicMeshUVOverlay* UVs = Mesh.Attributes()->GetUVLayer(UVChannel);
		if (!UVs || !Mesh.IsTriangle(TriangleID) || !UVs->IsSetTriangle(TriangleID))
		{
			return {};
		}
		FVector2f A, B, C;
		UVs->GetTriElements(TriangleID, A, B, C);
		const FVector2f Interpolated = (float)Barycentric.X * A + (float)Barycentric.Y * B + (float)Barycentric.Z * C;
		return FVector2D(Interpolated);
	}

	TOptional<int32> GetTriangleMaterialId(const FDynamicMesh3& Mesh, int32 TriangleID)
	{
		if (Mesh.HasAttributes() && Mesh.Attributes()->HasMaterialID() && Mesh.IsTriangle(TriangleID))
		{
			return Mesh.Attributes()->GetMaterialID()->GetValue(TriangleID);
		}
		return {};
	}

	float ExtractDensityFromColor(EPCGColorChannel Channel, const FLinearColor& Color)
	{
		switch (Channel)
		{
		case EPCGColorChannel::Red: return Color.R;
		case EPCGColorChannel::Green: return Color.G;
		case EPCGColorChannel::Blue: return Color.B;
		case EPCGColorChannel::Alpha: return Color.A;
		default: return 1.0f;
		}
	}

	// One Point Per Vertex: matches the vanilla Mesh Sampler's restriction that UVs/Triangle Id/Material are not
	// computed for this mode, since a mesh vertex is shared by multiple triangles and has no single triangle id.
	void SampleOnePointPerVertex(const FDynamicMesh3& Mesh, const UPCGDynMeshSampleSettings* Settings, TArray<FSampledPoint>& OutPoints)
	{
		const FDynamicMeshNormalOverlay* Normals = Mesh.HasAttributes() ? Mesh.Attributes()->PrimaryNormals() : nullptr;
		const FDynamicMeshColorOverlay* Colors = Mesh.HasAttributes() ? Mesh.Attributes()->PrimaryColors() : nullptr;
		const FLinearColor DefaultColor = Settings->bHasDefaultColorOverride ? Settings->DefaultColorOverride : FLinearColor::White;

		OutPoints.Reserve(OutPoints.Num() + Mesh.VertexCount());
		for (const int32 VertexID : Mesh.VertexIndicesItr())
		{
			FSampledPoint& Point = OutPoints.Emplace_GetRef();

			FVector3f Normal = Normals
				? Sample_GetFirstVertexOverlayElement<FDynamicMeshNormalOverlay, FVector3f>(Mesh, Normals, VertexID, FVector3f::UnitZ())
				: (Mesh.HasVertexNormals() ? Mesh.GetVertexNormal(VertexID) : FVector3f::UnitZ());
			SetFrame(Point, Mesh.GetVertex(VertexID), FVector(Normal));

			const FVector4f DefaultColor4f(DefaultColor.R, DefaultColor.G, DefaultColor.B, DefaultColor.A);
			FVector4f VertexColor = DefaultColor4f;
			if (Mesh.HasVertexColors())
			{
				const FVector3f VC = Mesh.GetVertexColor(VertexID);
				VertexColor = FVector4f(VC.X, VC.Y, VC.Z, DefaultColor4f.W);
			}
			VertexColor = Colors ? Sample_GetFirstVertexOverlayElement<FDynamicMeshColorOverlay, FVector4f>(Mesh, Colors, VertexID, DefaultColor4f) : VertexColor;
			Point.Color = FLinearColor(VertexColor.X, VertexColor.Y, VertexColor.Z, VertexColor.W);
		}
	}

	void SampleOnePointPerTriangle(const FDynamicMesh3& Mesh, const UPCGDynMeshSampleSettings* Settings,
		const TFunction<int32(int32)>& RemapTriangleId, TArray<FSampledPoint>& OutPoints)
	{
		const FVector Barycentric(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0);
		const FLinearColor DefaultColor = Settings->bHasDefaultColorOverride ? Settings->DefaultColorOverride : FLinearColor::White;

		OutPoints.Reserve(OutPoints.Num() + Mesh.TriangleCount());
		for (const int32 TriangleID : Mesh.TriangleIndicesItr())
		{
			FVector A, B, C;
			Mesh.GetTriVertices(TriangleID, A, B, C);

			FSampledPoint& Point = OutPoints.Emplace_GetRef();
			SetFrame(Point, (A + B + C) / 3.0, Mesh.GetTriNormal(TriangleID));
			Point.Color = InterpolateTriangleColor(Mesh, TriangleID, Barycentric, DefaultColor);
			Point.SourceTriangleId = RemapTriangleId(TriangleID);
			if (Settings->bExtractUVAsAttribute)
			{
				Point.UV = InterpolateTriangleUV(Mesh, Settings->UVChannel, TriangleID, Barycentric);
			}
			if (Settings->bOutputMaterialInfo)
			{
				Point.MaterialId = GetTriangleMaterialId(Mesh, TriangleID);
			}
		}
	}

	// Reuses UE::Geometry::FMeshSurfacePointSampling directly - the same core Poisson-disk algorithm the vanilla
	// Mesh Sampler calls into - rather than reimplementing surface Poisson sampling.
	void SamplePoisson(const FDynamicMesh3& Mesh, const UPCGDynMeshSampleSettings* Settings, int32 ContextSeed,
		const TFunction<int32(int32)>& RemapTriangleId, TArray<FSampledPoint>& OutPoints)
	{
		FMeshSurfacePointSampling PointSampling;
		PointSampling.SampleRadius = Settings->SamplingOptions.SamplingRadius;
		PointSampling.MaxSamples = Settings->SamplingOptions.MaxNumSamples > 0
			? (uint32)Settings->SamplingOptions.MaxNumSamples : TNumericLimits<uint32>::Max();
		PointSampling.RandomSeed = PCGHelpers::ComputeSeed(ContextSeed, Settings->SamplingOptions.RandomSeed);
		PointSampling.SubSampleDensity = Settings->SamplingOptions.SubSampleDensity;
		PointSampling.SamplingMethodVersion = Settings->SamplingOptions.SamplingMethodVersion;

		if (Settings->NonUniformSamplingOptions.MaxSamplingRadius > PointSampling.SampleRadius)
		{
			PointSampling.MaxSampleRadius = Settings->NonUniformSamplingOptions.MaxSamplingRadius;
			PointSampling.SizeDistribution = FMeshSurfacePointSampling::ESizeDistribution(static_cast<int>(Settings->NonUniformSamplingOptions.SizeDistribution));
			PointSampling.SizeDistributionPower = FMath::Clamp(Settings->NonUniformSamplingOptions.SizeDistributionPower, 1.0, 10.0);
		}

		PointSampling.bComputeBarycentrics = true;
		PointSampling.ComputePoissonSampling(Mesh);

		const FLinearColor DefaultColor = Settings->bHasDefaultColorOverride ? Settings->DefaultColorOverride : FLinearColor::White;

		OutPoints.Reserve(OutPoints.Num() + PointSampling.Samples.Num());
		for (int32 SampleIndex = 0; SampleIndex < PointSampling.Samples.Num(); ++SampleIndex)
		{
			const FFrame3d& Frame = PointSampling.Samples[SampleIndex];
			const int32 TriangleID = PointSampling.TriangleIDs[SampleIndex];
			const FVector Barycentric = PointSampling.BarycentricCoords[SampleIndex];

			FSampledPoint& Point = OutPoints.Emplace_GetRef();
			Point.Position = Frame.Origin;
			Point.Normal = Frame.Z();
			Point.TangentX = Frame.X();
			Point.Color = InterpolateTriangleColor(Mesh, TriangleID, Barycentric, DefaultColor);
			Point.SourceTriangleId = RemapTriangleId(TriangleID);
			if (Settings->bExtractUVAsAttribute)
			{
				Point.UV = InterpolateTriangleUV(Mesh, Settings->UVChannel, TriangleID, Barycentric);
			}
			if (Settings->bOutputMaterialInfo)
			{
				Point.MaterialId = GetTriangleMaterialId(Mesh, TriangleID);
			}
		}
	}

	// Applies bVoxelize as a mutating preparation pass. Requires a real UDynamicMesh (GeometryScript's voxel
	// functions are not FDynamicMesh3-const-friendly), so a temporary copy is unavoidable here - unlike the
	// zero-copy full-mesh path in ExecuteInternal, this is the "required by the underlying API" case called out
	// by the PCGUtilsDynMesh mesh-copy convention. Voxelization regenerates topology, so any Selection
	// triangle-id remapping is intentionally dropped past this point (matches vanilla Mesh Sampler behavior).
	void ApplyVoxelize(const UPCGDynMeshSampleSettings* Settings, FDynamicMesh3& Mesh, FPCGContext* Context)
	{
		UDynamicMesh* Scratch = FPCGContext::NewObject_AnyThread<UDynamicMesh>(Context);
		Scratch->SetMesh(MoveTemp(Mesh));

		FGeometryScriptSolidifyOptions SolidifyOptions{};
		SolidifyOptions.GridParameters.GridCellSize = Settings->VoxelSize;
		SolidifyOptions.GridParameters.SizeMethod = EGeometryScriptGridSizingMethod::GridCellSize;
		UGeometryScriptLibrary_MeshVoxelFunctions::ApplyMeshSolidify(Scratch, SolidifyOptions);

		if (Settings->bRemoveHiddenTriangles)
		{
			FGeometryScriptRemoveHiddenTrianglesOptions RemoveOptions{};
			UGeometryScriptLibrary_MeshRepairFunctions::RemoveHiddenTriangles(Scratch, RemoveOptions);
		}

		Mesh = MoveTemp(Scratch->GetMeshRef());
	}

	int32 IdentityRemap(int32 TriangleID)
	{
		return TriangleID;
	}
}

#if WITH_EDITOR
FText UPCGDynMeshSampleSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Sample DynMesh");
}

FText UPCGDynMeshSampleSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip",
		"Samples PCG Points from a Dynamic Mesh, or from only the geometry referenced by a Dynamic Mesh Selection. "
		"Equivalent to the vanilla Mesh Sampler node's sampling stage, operating directly on PCGUtilsDynMesh data.");
}
#endif

TArray<FPCGPinProperties> UPCGDynMeshSampleSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	FPCGPinProperties& MeshPin = Pins.Add_GetRef(FPCGUtilsMeshTargetFunctions::MakeMeshInputPinProperties(SampleInputPin, true, true));
	MeshPin.SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGDynMeshSampleSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point, true, true);
	return Pins;
}

FPCGElementPtr UPCGDynMeshSampleSettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshSampleElement>();
}

bool FPCGDynMeshSampleElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGDynMeshSampleElement::ExecuteInternal);

	check(Context);
	const UPCGDynMeshSampleSettings* Settings = Context->GetInputSettings<UPCGDynMeshSampleSettings>();
	check(Settings);

	for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(SampleInputPin))
	{
		// Resolve the source DynMesh and, for a Selection input, the triangle set to restrict sampling to -
		// reusing the same Selection/DynMesh resolution used throughout PCGUtilsDynMesh (see
		// PCGDynamicMeshSelectionProcessBase) rather than duplicating it here.
		const UPCGDynamicMeshSelectionData* SelectionData = Cast<const UPCGDynamicMeshSelectionData>(Input.Data);
		const UPCGDynamicMeshData* SourceData = SelectionData ? SelectionData->GetSourceMeshData() : Cast<const UPCGDynamicMeshData>(Input.Data);
		if (!SourceData)
		{
			PCGLog::LogErrorOnGraph(LOCTEXT("InvalidInput", "Sample DynMesh skipped an input that is neither a Dynamic Mesh nor a valid Dynamic Mesh Selection."), Context);
			continue;
		}

		const UDynamicMesh* SourceObject = SourceData->GetDynamicMesh();
		const FDynamicMesh3* SourceMesh = SourceObject ? SourceObject->GetMeshPtr() : nullptr;
		if (!SourceMesh)
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("MissingSourceMesh", "Sample DynMesh skipped an input with no valid source mesh."), Context);
			continue;
		}

		// Extracting a submesh remaps triangle ids, so FDynamicSubmesh3's base<->submesh triangle map is kept
		// alive for the duration of sampling and used to translate ids back to the original source mesh (see
		// section on triangle-id preservation). Full-mesh sampling instead reads SourceMesh directly with no
		// copy at all, since nothing here needs to mutate it.
		TUniquePtr<FDynamicSubmesh3> Submesh;
		FDynamicMesh3 VoxelizedMesh;
		const FDynamicMesh3* WorkingMesh = SourceMesh;
		TFunction<int32(int32)> RemapTriangleId = &IdentityRemap;

		if (SelectionData)
		{
			FGeometryScriptMeshSelection GeoSelection;
			GeoSelection.SetSelection(SelectionData->GetSelection());

			TArray<int32> SelectedTriangles;
			GeoSelection.ConvertToMeshIndexArray(*SourceMesh, SelectedTriangles, EGeometryScriptIndexType::Triangle);

			if (SelectedTriangles.IsEmpty())
			{
				UPCGPointArrayData* EmptyOutput = FPCGContext::NewObject_AnyThread<UPCGPointArrayData>(Context);
				EmptyOutput->SetNumPoints(0);
				FPCGTaggedData& EmptyResult = Context->OutputData.TaggedData.Emplace_GetRef(Input);
				EmptyResult.Data = EmptyOutput;
				continue;
			}

			Submesh = MakeUnique<FDynamicSubmesh3>(SourceMesh, SelectedTriangles);
			WorkingMesh = &Submesh->GetSubmesh();
			RemapTriangleId = [SubmeshPtr = Submesh.Get()](int32 SubTriangleID) { return SubmeshPtr->MapTriangleToBaseMesh(SubTriangleID); };
		}

		if (Settings->bVoxelize)
		{
			VoxelizedMesh = *WorkingMesh;
			ApplyVoxelize(Settings, VoxelizedMesh, Context);
			WorkingMesh = &VoxelizedMesh;
			// Voxelization fully regenerates topology - there is no meaningful source triangle to map back to.
			RemapTriangleId = IdentityRemap;
		}

		TArray<FSampledPoint> SampledPoints;
		switch (Settings->SamplingMethod)
		{
		case EPCGMeshSamplingMethod::OnePointPerVertex:
			SampleOnePointPerVertex(*WorkingMesh, Settings, SampledPoints);
			break;
		case EPCGMeshSamplingMethod::OnePointPerTriangle:
			SampleOnePointPerTriangle(*WorkingMesh, Settings, RemapTriangleId, SampledPoints);
			break;
		case EPCGMeshSamplingMethod::PoissonSampling:
			SamplePoisson(*WorkingMesh, Settings, Context->GetSeed(), RemapTriangleId, SampledPoints);
			break;
		default:
			checkNoEntry();
			continue;
		}

		// World-space conversion is a deliberate final pass over already-sampled local-space points, kept
		// separate from the sampling stage above per bTransformToWorldSpace's contract.
		FTransform ToWorld = FTransform::Identity;
		if (Settings->bTransformToWorldSpace)
		{
			if (const AActor* TargetActor = Context->GetTargetActor(SourceData))
			{
				ToWorld = TargetActor->GetActorTransform();
			}
			else
			{
				PCGLog::LogWarningOnGraph(LOCTEXT("MissingTargetActor", "Sample DynMesh could not resolve a target actor; points remain in Dynamic Mesh local space."), Context);
			}
		}

		UPCGPointArrayData* OutputData = FPCGContext::NewObject_AnyThread<UPCGPointArrayData>(Context);
		OutputData->SetNumPoints(SampledPoints.Num(), false);
		auto Transforms = OutputData->GetTransformValueRange();
		auto PointColors = OutputData->GetColorValueRange();
		auto Densities = OutputData->GetDensityValueRange();
		auto BoundsMin = OutputData->GetBoundsMinValueRange();
		auto BoundsMax = OutputData->GetBoundsMaxValueRange();
		auto Steepnesses = OutputData->GetSteepnessValueRange();
		auto Seeds = OutputData->GetSeedValueRange();

		const bool bWantUV = Settings->bExtractUVAsAttribute && Settings->SamplingMethod != EPCGMeshSamplingMethod::OnePointPerVertex;
		const bool bWantTriangleId = Settings->bOutputTriangleIds && Settings->SamplingMethod != EPCGMeshSamplingMethod::OnePointPerVertex;
		const bool bWantMaterialInfo = Settings->bOutputMaterialInfo && Settings->SamplingMethod != EPCGMeshSamplingMethod::OnePointPerVertex;

		UPCGMetadata* Metadata = OutputData->MutableMetadata();
		FPCGMetadataAttribute<FVector2D>* UVAttribute = (bWantUV && Metadata && !Settings->UVAttributeName.IsNone())
			? Metadata->CreateAttribute<FVector2D>(Settings->UVAttributeName, FVector2D::ZeroVector, true, true) : nullptr;
		FPCGMetadataAttribute<int32>* TriangleIdAttribute = (bWantTriangleId && Metadata && !Settings->TriangleIdAttributeName.IsNone())
			? Metadata->CreateAttribute<int32>(Settings->TriangleIdAttributeName, INDEX_NONE, false, true) : nullptr;
		FPCGMetadataAttribute<int32>* MaterialIdAttribute = (bWantMaterialInfo && Metadata && !Settings->MaterialIdAttributeName.IsNone())
			? Metadata->CreateAttribute<int32>(Settings->MaterialIdAttributeName, INDEX_NONE, false, true) : nullptr;
		FPCGMetadataAttribute<FSoftObjectPath>* MaterialAttribute = (bWantMaterialInfo && Metadata && !Settings->MaterialAttributeName.IsNone())
			? Metadata->CreateAttribute<FSoftObjectPath>(Settings->MaterialAttributeName, FSoftObjectPath(), false, true) : nullptr;

		const TArray<TObjectPtr<UMaterialInterface>>& SourceMaterials = SourceData->GetMaterials();
		auto MetadataEntries = OutputData->GetMetadataEntryValueRange();

		for (int32 Index = 0; Index < SampledPoints.Num(); ++Index)
		{
			const FSampledPoint& Point = SampledPoints[Index];
			const FTransform LocalTransform = ToLocalTransform(Point);
			const FTransform FinalTransform = Settings->bTransformToWorldSpace ? LocalTransform * ToWorld : LocalTransform;

			Transforms[Index] = FinalTransform;
			PointColors[Index] = FVector4(Point.Color.R, Point.Color.G, Point.Color.B, Point.Color.A);
			Densities[Index] = Settings->bUseColorChannelAsDensity ? ExtractDensityFromColor(Settings->ColorChannelAsDensity, Point.Color) : 1.0f;
			BoundsMin[Index] = FVector::ZeroVector;
			BoundsMax[Index] = FVector::ZeroVector;
			Steepnesses[Index] = Settings->PointSteepness;
			Seeds[Index] = PCGHelpers::ComputeSeedFromPosition(FinalTransform.GetLocation());

			if (UVAttribute && Point.UV.IsSet())
			{
				Metadata->InitializeOnSet(MetadataEntries[Index]);
				UVAttribute->SetValue(MetadataEntries[Index], Point.UV.GetValue());
			}
			if (TriangleIdAttribute && Point.SourceTriangleId != INDEX_NONE)
			{
				Metadata->InitializeOnSet(MetadataEntries[Index]);
				TriangleIdAttribute->SetValue(MetadataEntries[Index], Point.SourceTriangleId);
			}
			if (MaterialIdAttribute && Point.MaterialId.IsSet())
			{
				Metadata->InitializeOnSet(MetadataEntries[Index]);
				MaterialIdAttribute->SetValue(MetadataEntries[Index], Point.MaterialId.GetValue());
				if (MaterialAttribute && SourceMaterials.IsValidIndex(Point.MaterialId.GetValue()) && SourceMaterials[Point.MaterialId.GetValue()])
				{
					MaterialAttribute->SetValue(MetadataEntries[Index], FSoftObjectPath(SourceMaterials[Point.MaterialId.GetValue()]));
				}
			}
		}

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		Output.Data = OutputData;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
