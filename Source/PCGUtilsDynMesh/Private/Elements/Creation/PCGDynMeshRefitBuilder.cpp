// Copyright Max Harris
#include "Elements/Creation/PCGDynMeshRefitBuilder.h"
#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshTransforms.h"
#include "Factories/PCGUtilsDynMeshFactories.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

namespace PCGUtilsBuilderRefit
{
bool Error(FPCGContext *Context, const FString &Message)
{
	PCGLog::LogErrorOnGraph(FText::FromString(Message), Context);
	return false;
}
bool Measure(const FPCGUtilsDynMeshBuildResult &Result, const FTransform &Frame, FBox &Bounds, FPCGContext *Context,
             int32 SeedIndex, const TCHAR *Role)
{
	const UDynamicMesh *Object = Result.MeshData ? Result.MeshData->GetDynamicMesh() : nullptr;
	const UE::Geometry::FDynamicMesh3 *Mesh = Object ? Object->GetMeshPtr() : nullptr;
	if (!Mesh)
		return Error(Context, FString::Printf(TEXT("%s Builder has no mesh at seed %d."), Role, SeedIndex));
	if (Mesh->VertexCount() == 0)
		return Error(Context,
		             FString::Printf(TEXT("%s Builder has 0 vertices at seed %d; bounds require at least one vertex."),
		                             Role, SeedIndex));
	Bounds = FBox(ForceInit);
	for (int32 ID : Mesh->VertexIndicesItr())
	{
		const FVector Position = Frame.InverseTransformPosition(FVector(Mesh->GetVertex(ID)));
		if (Position.ContainsNaN())
			return Error(Context,
			             FString::Printf(TEXT("%s Builder vertex %d at seed %d is non-finite."), Role, ID, SeedIndex));
		Bounds += Position;
	}
	return true;
}
class FRefitOperation final : public FPCGUtilsDynMeshBuilderOperation
{
  public:
	explicit FRefitOperation(const UPCGDynMeshRefitBuilderData *InData) : Data(InData)
	{
	}
	virtual bool Prepare() override
	{
		if (!Data->Source)
			return Error(Context, TEXT("Builder input is missing; exactly one source recipe is required."));
		Source = Data->Source->CreateOperation(Context);
		if (Data->Target)
			Target = Data->Target->CreateOperation(Context);
		return Source.IsValid() && (!Data->Target || Target.IsValid());
	}
	virtual bool Build(const FPCGUtilsDynMeshBuildContext &Input, FPCGUtilsDynMeshBuildResult &Output) const override
	{
		FPCGContext *Ctx = Input.Context ? Input.Context : Context;
		FString OrientationError;
		if (!Data->Fitting.Orientation.Validate(OrientationError))
			return Error(Ctx, FString::Printf(TEXT("Seed %d: %s"), Input.SeedIndex, *OrientationError));
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (!FMath::IsFinite(Data->Fitting.PaddingMin[Axis]))
				return Error(Ctx, FString::Printf(TEXT("PaddingMin axis %d is %g at seed %d; expected a finite value."),
				                                  Axis, Data->Fitting.PaddingMin[Axis], Input.SeedIndex));
			if (!FMath::IsFinite(Data->Fitting.PaddingMax[Axis]))
				return Error(Ctx, FString::Printf(TEXT("PaddingMax axis %d is %g at seed %d; expected a finite value."),
				                                  Axis, Data->Fitting.PaddingMax[Axis], Input.SeedIndex));
		}
		FTransform TargetFrame = Input.GetFittingTransform();
		FBox TargetBounds = Input.GetFittingBounds();
		if (Target)
		{
			FPCGUtilsDynMeshBuildResult Reference;
			if (!Target->Build(Input, Reference))
				return false;
			if (Data->bUseTargetFrame && Reference.bHasBuilderFrame)
				TargetFrame = Reference.BuilderFrame;
			TargetFrame.SetScale3D(FVector::OneVector);
			if (!Measure(Reference, TargetFrame, TargetBounds, Ctx, Input.SeedIndex, TEXT("Target")))
				return false;
		}
		if (!TargetBounds.IsValid)
			return Error(Ctx, FString::Printf(TEXT("Fitting target bounds are invalid at seed %d."), Input.SeedIndex));
		if (TargetFrame.ContainsNaN())
			return Error(Ctx,
			             FString::Printf(TEXT("Fitting target transform is non-finite at seed %d."), Input.SeedIndex));
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (!FMath::IsFinite(TargetBounds.Min[Axis]) || !FMath::IsFinite(TargetBounds.Max[Axis]))
				return Error(
				    Ctx,
				    FString::Printf(TEXT("Fitting target bounds axis %d at seed %d has non-finite endpoints [%g, %g]."),
				                    Axis, Input.SeedIndex, TargetBounds.Min[Axis], TargetBounds.Max[Axis]));
			if (TargetBounds.Min[Axis] > TargetBounds.Max[Axis])
				return Error(Ctx,
				             FString::Printf(TEXT("Fitting target bounds axis %d at seed %d has Min %g > Max %g."),
				                             Axis, Input.SeedIndex, TargetBounds.Min[Axis], TargetBounds.Max[Axis]));
		}
		if (Data->bRetarget)
		{
			PCGUtilsFitting::ApplyPadding(TargetBounds, Data->Fitting.PaddingMin, Data->Fitting.PaddingMax);
			Data->Fitting.Orientation.RemapTarget(TargetFrame, TargetBounds);
			FPCGUtilsDynMeshBuildContext Scoped = Input;
			Scoped.bHasFittingTarget = true;
			Scoped.FittingTransform = TargetFrame;
			Scoped.FittingBounds = TargetBounds;
			return Source->Build(Scoped, Output);
		}
		if (!Source->Build(Input, Output))
			return false;
		FTransform SourceFrame = Output.bHasBuilderFrame ? Output.BuilderFrame : Input.GetFittingTransform();
		SourceFrame.SetScale3D(FVector::OneVector);
		FBox SourceBounds;
		if (!Measure(Output, SourceFrame, SourceBounds, Ctx, Input.SeedIndex, TEXT("Source")))
			return false;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const double Size = SourceBounds.GetSize()[Axis];
			if (Size <= UE_SMALL_NUMBER && Data->Fitting.ScaleToFit.ScaleToFitMode != EPCGUtilsFitMode::None)
				return Error(Ctx, FString::Printf(TEXT("Source bounds size axis %d is %g at seed %d; scaled refitting "
				                                       "requires a size greater than %g."),
				                                  Axis, Size, Input.SeedIndex, double(UE_SMALL_NUMBER)));
		}
		FTransform Placement;
		// Source geometry already contains its seed scale. Bake reference scale into the bounds,
		// so None preserves the realized dimensions instead of applying the seed scale twice.
		FPCGUtilsFittingDetails PlacementFitting = Data->Fitting;
		PCGUtilsFitting::ApplyPadding(TargetBounds, PlacementFitting.PaddingMin, PlacementFitting.PaddingMax);
		PlacementFitting.PaddingMin = PlacementFitting.PaddingMax = FVector::ZeroVector;
		TargetBounds =
		    TargetBounds.TransformBy(FTransform(FQuat::Identity, FVector::ZeroVector, TargetFrame.GetScale3D()));
		TargetFrame.SetScale3D(FVector::OneVector);
		PlacementFitting.ComputeLocalTransform(TargetFrame, TargetBounds, SourceBounds, Placement);
		if (Placement.ContainsNaN())
			return Error(Ctx, FString::Printf(
			                      TEXT("Refit produced a non-finite placement at seed %d; source bounds size is %s."),
			                      Input.SeedIndex, *SourceBounds.GetSize().ToString()));
		// Two passes avoid collapsing rotated nonuniform scale into an inexact SRT composition.
		// MeshTransforms preserves vertex IDs, transforms normal overlays, and corrects reflected winding.
		Output.MeshData->GetMutableDynamicMesh()->EditMesh(
		    [&](UE::Geometry::FDynamicMesh3 &Mesh)
		    {
			    MeshTransforms::ApplyTransform(Mesh, UE::Geometry::FTransformSRT3d(SourceFrame.Inverse()), true);
			    MeshTransforms::ApplyTransform(Mesh, UE::Geometry::FTransformSRT3d(Placement), true);
		    });
		Output.SetBuilderFrame(Placement);
		return true;
	}

  private:
	TObjectPtr<const UPCGDynMeshRefitBuilderData> Data;
	TSharedPtr<FPCGUtilsDynMeshBuilderOperation> Source, Target;
};
} // namespace PCGUtilsBuilderRefit

TSharedPtr<FPCGUtilsDynMeshBuilderOperation> UPCGDynMeshRefitBuilderData::CreateOperationInternal() const
{
	return MakeShared<PCGUtilsBuilderRefit::FRefitOperation>(this);
}
void UPCGDynMeshRefitBuilderData::AddToCrc(FArchiveCrc32 &Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (!bFullDataCrc)
		return;
	uint32 SourceCrc = Source ? Source->GetOrComputeCrc(true).GetValue() : 0;
	uint32 TargetCrc = Target ? Target->GetOrComputeCrc(true).GetValue() : 0;
	bool Retarget = bRetarget, UseFrame = bUseTargetFrame;
	Ar << SourceCrc << TargetCrc << Retarget << UseFrame;
	FPCGUtilsFittingDetails Copy = Fitting;
	FPCGUtilsFittingDetails::StaticStruct()->SerializeItem(Ar, &Copy, nullptr);
}
UPCGDynMeshRefitBuilderSettings::UPCGDynMeshRefitBuilderSettings()
{
	Fitting.ScaleToFit.ScaleToFitMode = EPCGUtilsFitMode::None;
}
TArray<FPCGPinProperties> UPCGDynMeshRefitBuilderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(TEXT("Builder"), FPCGUtilsDynMeshBuilderFactoryDataTypeInfo::AsId(), false, false)
	    .SetRequiredPin();
	Pins.Emplace(TEXT("Target"), FPCGUtilsDynMeshBuilderFactoryDataTypeInfo::AsId(), false, false);
	return Pins;
}
UPCGUtilsDynMeshFactoryData *UPCGDynMeshRefitBuilderSettings::CreateFactory(FPCGContext *Context,
                                                                            UPCGUtilsDynMeshFactoryData *Existing) const
{
	TArray<TObjectPtr<const UPCGUtilsDynMeshBuilderFactoryData>> Sources, Targets;
	if (!PCGUtilsDynMeshFactories::GetInputFactories(Context, TEXT("Builder"), Sources,
	                                                 PCGUtilsDynMeshFactories::GetBuilderFactoryTypes()))
		return nullptr;
	if (Sources.Num() != 1)
	{
		PCGUtilsBuilderRefit::Error(
		    Context, FString::Printf(TEXT("Builder input contains %d recipes; exactly 1 is required."), Sources.Num()));
		return nullptr;
	}
	if (!Context->InputData.GetInputsByPin(TEXT("Target")).IsEmpty())
	{
		if (!PCGUtilsDynMeshFactories::GetInputFactories(Context, TEXT("Target"), Targets,
		                                                 PCGUtilsDynMeshFactories::GetBuilderFactoryTypes()))
			return nullptr;
		if (Targets.Num() != 1)
		{
			PCGUtilsBuilderRefit::Error(
			    Context,
			    FString::Printf(TEXT("Target input contains %d recipes; exactly 1 is required when connected."),
			                    Targets.Num()));
			return nullptr;
		}
	}
	UPCGDynMeshRefitBuilderData *Result = FPCGContext::NewObject_AnyThread<UPCGDynMeshRefitBuilderData>(Context);
	Result->Source = Sources[0];
	Result->Target = Targets.IsEmpty() ? nullptr : Targets[0];
	Result->Fitting = Fitting;
	Result->bRetarget = bRetarget;
	Result->bUseTargetFrame = bUseTargetFrame;
	if (bRetarget)
	{
		Result->Fitting.Orientation = TargetOrientation;
		Result->Fitting.PaddingMin = TargetPaddingMin;
		Result->Fitting.PaddingMax = TargetPaddingMax;
	}
	return Super::CreateFactory(Context, Result);
}
