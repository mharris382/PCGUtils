#include "Elements/Deform/PCGSplineDeform.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGSplineData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/PCGUtilsSplineHelpers.h"
#include "GeometryScript/MeshNormalsFunctions.h"
#include "MeshTarget/PCGUtilsMeshTargetFunctions.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGSplineDeformElement"

namespace
{
	const FName SplineDeformMeshPin = TEXT("Mesh");
	const FName SplineDeformSplinePin = TEXT("Spline");

	/** Resolves ForwardAxis into (Longitudinal, TransverseA, TransverseB) axis indices (0=X, 1=Y, 2=Z). */
	void ResolveAxisIndices(ESplineMeshAxis::Type ForwardAxis, int32& OutLongitudinal, int32& OutTransverseA, int32& OutTransverseB)
	{
		switch (ForwardAxis)
		{
		case ESplineMeshAxis::Y:
			OutLongitudinal = 1; OutTransverseA = 0; OutTransverseB = 2;
			break;
		case ESplineMeshAxis::Z:
			OutLongitudinal = 2; OutTransverseA = 0; OutTransverseB = 1;
			break;
		case ESplineMeshAxis::X:
		default:
			OutLongitudinal = 0; OutTransverseA = 1; OutTransverseB = 2;
			break;
		}
	}

	/** Where, in spline-distance units, the source mesh's leading edge (T=0) should map to. */
	double ResolveAnchorBaseDistance(EPCGUtilsSplineDeformAnchor Anchor, double RangeStart, double RangeEnd, double SourceExtent)
	{
		switch (Anchor)
		{
		case EPCGUtilsSplineDeformAnchor::Start:
			return RangeStart;
		case EPCGUtilsSplineDeformAnchor::End:
			return RangeEnd - SourceExtent;
		case EPCGUtilsSplineDeformAnchor::Center:
		default:
			return (RangeStart + RangeEnd) * 0.5 - SourceExtent * 0.5;
		}
	}

	/**
	 * Resolves the distance to actually evaluate the spline frame at, per OutOfRangeMode, plus a signed
	 * extrapolation offset (nonzero only for ExtendAlongTangent on an out-of-range distance) the caller should
	 * add along the evaluated endpoint's tangent.
	 */
	double ResolveEvalDistance(double RawDistance, double RangeStart, double RangeEnd, double SplineLength,
		EPCGUtilsSplineDeformOutOfRangeMode OutOfRangeMode, double& OutExtrapolationOffset)
	{
		OutExtrapolationOffset = 0.0;

		if (RawDistance >= RangeStart && RawDistance <= RangeEnd)
		{
			return RawDistance;
		}

		if (OutOfRangeMode == EPCGUtilsSplineDeformOutOfRangeMode::Wrap)
		{
			double Wrapped = FMath::Fmod(RawDistance, SplineLength);
			if (Wrapped < 0.0) { Wrapped += SplineLength; }
			return Wrapped;
		}

		if (OutOfRangeMode == EPCGUtilsSplineDeformOutOfRangeMode::ExtendAlongTangent)
		{
			if (RawDistance < RangeStart)
			{
				OutExtrapolationOffset = RawDistance - RangeStart;
				return RangeStart;
			}
			OutExtrapolationOffset = RawDistance - RangeEnd;
			return RangeEnd;
		}

		// Clamp
		return FMath::Clamp(RawDistance, RangeStart, RangeEnd);
	}

	/**
	 * Deforms every vertex of Handle's (FullMeshCopy) target mesh in place. The mapping frame - source min/max
	 * along Forward Axis, and the transverse bounds center - is always derived from the *complete* source mesh
	 * (Handle.GetSourceMeshData()), never from the working/selected subset, so narrowing a Mesh Selection never
	 * changes where the mesh maps onto the spline.
	 * @return false (with a warning already logged) if the source mesh is invalid or has degenerate extent along Forward Axis - the mesh is left unchanged in that case.
	 */
	bool DeformMesh(FPCGUtilsMeshTargetHandle& Handle, const UPCGSplineDeformSettings* Settings,
		const FPCGSplineStruct& Spline, const FTransform& ActorTransform, double SplineLength,
		double EffectiveRangeStart, double EffectiveRangeEnd, EPCGUtilsSplineDeformOutOfRangeMode EffectiveOutOfRangeMode,
		FPCGContext* Context)
	{
		using namespace UE::Geometry;

		const UPCGDynamicMeshData* SourceData = Handle.GetSourceMeshData();
		const UDynamicMesh* SourceObject = SourceData ? SourceData->GetDynamicMesh() : nullptr;
		const FDynamicMesh3* SourceMesh = SourceObject ? SourceObject->GetMeshPtr() : nullptr;
		if (!SourceMesh)
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("InvalidSourceMesh", "Spline Deform skipped an input with no valid source mesh."), Context);
			return false;
		}

		int32 LongitudinalAxis, TransverseAAxis, TransverseBAxis;
		ResolveAxisIndices(Settings->ForwardAxis, LongitudinalAxis, TransverseAAxis, TransverseBAxis);

		const FAxisAlignedBox3d SourceBounds = SourceMesh->GetBounds();
		const double SourceMin = SourceBounds.Min[LongitudinalAxis];
		const double SourceMax = SourceBounds.Max[LongitudinalAxis];
		const double SourceExtent = SourceMax - SourceMin;
		if (SourceExtent < UE_DOUBLE_KINDA_SMALL_NUMBER)
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("DegenerateSourceExtent",
				"Spline Deform skipped an input whose full source mesh has effectively zero extent along the chosen Forward Axis; the mesh was left unchanged."), Context);
			return false;
		}

		const double TransverseACenter = (SourceBounds.Min[TransverseAAxis] + SourceBounds.Max[TransverseAAxis]) * 0.5;
		const double TransverseBCenter = (SourceBounds.Min[TransverseBAxis] + SourceBounds.Max[TransverseBAxis]) * 0.5;

		const bool bUseScale = Settings->bUseSplineScale;
		const bool bFitToSpline = Settings->MappingMode == EPCGUtilsSplineDeformMappingMode::FitToSpline;
		const double AnchorBase = bFitToSpline
			? 0.0
			: ResolveAnchorBaseDistance(Settings->Anchor, EffectiveRangeStart, EffectiveRangeEnd, SourceExtent);

		Handle.GetTargetMesh()->EditMesh([&](FDynamicMesh3& M)
		{
			for (const int32 VertexID : M.VertexIndicesItr())
			{
				// Decompose the vertex's *original* position (the mesh starts as an exact copy of the source)
				// into a longitudinal coordinate and two transverse offsets from the source bounds centerline.
				const FVector3d OriginalPos = M.GetVertex(VertexID);
				const double Longitudinal = OriginalPos[LongitudinalAxis];
				const double TransverseA = OriginalPos[TransverseAAxis] - TransverseACenter;
				const double TransverseB = OriginalPos[TransverseBAxis] - TransverseBCenter;

				// Distance from the "leading" edge of the source mesh in the traversal direction - this is what
				// ties FitToSpline's Alpha and PreserveLength's distance calc into one shared quantity, and what
				// bReverseDirection flips (leading edge becomes SourceMax instead of SourceMin).
				const double LeadingDistance = Settings->bReverseDirection ? (SourceMax - Longitudinal) : (Longitudinal - SourceMin);

				double RawSplineDistance;
				if (bFitToSpline)
				{
					const double Alpha = LeadingDistance / SourceExtent;
					RawSplineDistance = FMath::Lerp(EffectiveRangeStart, EffectiveRangeEnd, Alpha);
				}
				else
				{
					RawSplineDistance = AnchorBase + LeadingDistance + (double)Settings->DistanceOffset;
				}

				double ExtrapolationOffset = 0.0;
				const double EvalDistance = ResolveEvalDistance(
					RawSplineDistance, EffectiveRangeStart, EffectiveRangeEnd, SplineLength, EffectiveOutOfRangeMode, ExtrapolationOffset);

				const float InputKey = Spline.GetInputKeyAtDistanceAlongSpline(EvalDistance);
				FTransform WorldFrame = Spline.GetTransformAtSplineInputKey(InputKey, ESplineCoordinateSpace::World, bUseScale);

				if (FMath::Abs(ExtrapolationOffset) > UE_DOUBLE_KINDA_SMALL_NUMBER)
				{
					const FVector Tangent = Spline.GetTangentAtSplineInputKey(InputKey, ESplineCoordinateSpace::World).GetSafeNormal();
					WorldFrame.AddToTranslation(Tangent * ExtrapolationOffset);
				}

				const FTransform LocalFrame = WorldFrame.GetRelativeTransform(ActorTransform);
				const FQuat Rotation = LocalFrame.GetRotation();
				const FVector Right = Rotation.RotateVector(FVector::RightVector);
				const FVector Up = Rotation.RotateVector(FVector::UpVector);

				double ScaledTransverseA = TransverseA;
				double ScaledTransverseB = TransverseB;
				if (bUseScale)
				{
					// Only the transverse (Y/Z) scale is ever applied - longitudinal (X) scale must never affect
					// distance along the spline, which is handled entirely via distance/range math above.
					const FVector Scale = LocalFrame.GetScale3D();
					ScaledTransverseA *= Scale.Y;
					ScaledTransverseB *= Scale.Z;
				}

				const FVector NewPos = LocalFrame.GetLocation() + Right * ScaledTransverseA + Up * ScaledTransverseB;
				M.SetVertex(VertexID, FVector3d(NewPos));
			}
		});

		return true;
	}
}

#if WITH_EDITOR
FText UPCGSplineDeformSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Spline Deform");
}

FText UPCGSplineDeformSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip",
		"Maps Dynamic Mesh vertex positions onto a PCG spline. The mapping frame is derived automatically from "
		"the full source mesh bounds, so the mesh does not need to be pre-positioned or aligned with the spline. "
		"Changes vertex positions only - topology, UVs, material IDs, PolyGroups, and vertex colors are always "
		"preserved exactly; add subdivisions upstream (eg via Remesh) for a smoother bend.");
}
#endif

TArray<FPCGPinProperties> UPCGSplineDeformSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Add(FPCGUtilsMeshTargetFunctions::MakeMeshInputPinProperties(SplineDeformMeshPin));
	Pins.Emplace_GetRef(SplineDeformSplinePin, EPCGDataType::Spline, true, true).SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGSplineDeformSettings::OutputPinProperties() const
{
	return {FPCGPinProperties(PCGPinConstants::DefaultOutputLabel, EPCGDataType::DynamicMesh, true, true)};
}

FPCGElementPtr UPCGSplineDeformSettings::CreateElement() const
{
	return MakeShared<FPCGSplineDeformElement>();
}

bool FPCGSplineDeformElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGSplineDeformElement::ExecuteInternal);
	check(Context);

	const UPCGSplineDeformSettings* Settings = Context->GetInputSettings<UPCGSplineDeformSettings>();
	check(Settings);

	const UPCGSplineData* SplineData = PCGUtilsSplineHelpers::ResolveSingleSpline(Context, SplineDeformSplinePin);
	if (!SplineData)
	{
		return true;
	}

	const FPCGSplineStruct& Spline = SplineData->SplineStruct;
	const double SplineLength = Spline.GetSplineLength();
	if (SplineLength < UE_DOUBLE_KINDA_SMALL_NUMBER)
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("ZeroLengthSpline", "Spline Deform: the supplied spline has effectively zero length."), Context);
		return true;
	}

	double EffectiveRangeStart = 0.0;
	double EffectiveRangeEnd = SplineLength;
	if (Settings->RangeMode == EPCGUtilsSplineDeformRangeMode::DistanceRange)
	{
		EffectiveRangeStart = FMath::Clamp((double)Settings->StartDistance, 0.0, SplineLength);
		EffectiveRangeEnd = FMath::Clamp((double)Settings->EndDistance, 0.0, SplineLength);
		if (EffectiveRangeEnd <= EffectiveRangeStart)
		{
			PCGLog::LogErrorOnGraph(LOCTEXT("InvalidRange",
				"Spline Deform: Start Distance must be less than End Distance once clamped to the spline length."), Context);
			return true;
		}
	}

	EPCGUtilsSplineDeformOutOfRangeMode EffectiveOutOfRangeMode = Settings->OutOfRangeMode;
	if (EffectiveOutOfRangeMode == EPCGUtilsSplineDeformOutOfRangeMode::Wrap)
	{
		const bool bWrapSupported = Settings->RangeMode == EPCGUtilsSplineDeformRangeMode::EntireSpline && Spline.IsClosedLoop();
		if (!bWrapSupported)
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("WrapUnsupported",
				"Spline Deform: Wrap requires Range Mode = Entire Spline on a closed spline; falling back to Clamp."), Context);
			EffectiveOutOfRangeMode = EPCGUtilsSplineDeformOutOfRangeMode::Clamp;
		}
	}

	const FTransform ActorTransform = PCGUtilsSplineHelpers::ResolveActorTransformForSpline(Context, SplineData, Settings->bConvertSplineToLocalSpace);

	for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(SplineDeformMeshPin))
	{
		FPCGUtilsMeshTargetHandle Handle = FPCGUtilsMeshTargetFunctions::CreateTarget(
			Input.Data, EPCGUtilsMeshTargetPreparation::FullMeshCopy, Context);
		if (!Handle.IsValid())
		{
			continue;
		}

		if (!Handle.IsEmptySelectionNoOp())
		{
			DeformMesh(Handle, Settings, Spline, ActorTransform, SplineLength,
				EffectiveRangeStart, EffectiveRangeEnd, EffectiveOutOfRangeMode, Context);
		}

		FPCGUtilsMeshTargetFunctions::RestoreVertexPositions(Handle, Settings->SelectionBlend);

		if (Settings->bRecomputeNormals)
		{
			if (Handle.IsSelection())
			{
				FPCGUtilsMeshTargetFunctions::RecomputeSelectionAffectedNormals(Handle);
			}
			else
			{
				UGeometryScriptLibrary_MeshNormalsFunctions::SetPerVertexNormals(Handle.GetTargetMesh());
			}
		}

		FPCGUtilsMeshTargetFunctions::EmitOutput(Context, Input, Handle);
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
