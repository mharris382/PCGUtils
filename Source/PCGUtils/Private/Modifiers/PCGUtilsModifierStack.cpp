// Copyright Max Harris

#include "Modifiers/PCGUtilsModifierStack.h"

#include "Components/SceneComponent.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGPointArrayData.h"
#include "Data/PCGSplineData.h"
#include "GameFramework/Actor.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataDomain.h"
#include "Data/PCGPathPoint.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsModifierStack"

//////////////////////////////////////////////////////////////////////////
// Context and base modifier

void FPCGUtilsModifierContext::Warn(const UPCGUtilsComponentModifier* Modifier, const FText& Message, TOptional<FVector> WorldLocation) const
{
	if (!Warnings)
	{
		return;
	}
	FPCGUtilsModifierWarning& Warning = Warnings->AddDefaulted_GetRef();
	Warning.Modifier = Modifier;
	Warning.Message = Message;
	Warning.WorldLocation = WorldLocation;
}

FText UPCGUtilsComponentModifier::GetModifierDisplayName() const
{
	return GetClass()->GetDisplayNameText();
}

void UPCGUtilsComponentModifier::ModifyPoints(const FPCGUtilsModifierContext& Context, const UPCGBasePointData* In, TArray<UPCGBasePointData*>& Out) const
{
	Out.Add(const_cast<UPCGBasePointData*>(In));
}

void UPCGUtilsComponentModifier::ModifyPath(const FPCGUtilsModifierContext& Context, const UPCGSplineData* In, TArray<UPCGSplineData*>& Out) const
{
	Out.Add(const_cast<UPCGSplineData*>(In));
}

void UPCGUtilsComponentModifier::ModifyVolume(const FPCGUtilsModifierContext& Context, const UPCGSpatialData* In, TArray<UPCGSpatialData*>& Out) const
{
	Out.Add(const_cast<UPCGSpatialData*>(In));
}

//////////////////////////////////////////////////////////////////////////
// Stack

namespace PCGUtilsModifierStack
{
	FText TargetName(EPCGUtilsModifierTarget Target)
	{
		return StaticEnum<EPCGUtilsModifierTarget>()->GetDisplayNameTextByValue(static_cast<int64>(Target));
	}

	template <typename DataType, typename ModifyFn>
	TArray<DataType*> Run(const TArray<TObjectPtr<UPCGUtilsComponentModifier>>& Modifiers, const FPCGUtilsModifierContext& Context,
		DataType* In, EPCGUtilsModifierTarget Target, ModifyFn&& Modify)
	{
		TArray<DataType*> Current;
		if (!In)
		{
			return Current;
		}
		Current.Add(In);

		for (int32 Index = 0; Index < Modifiers.Num(); ++Index)
		{
			const UPCGUtilsComponentModifier* Modifier = Modifiers[Index];
			if (!Modifier || !Modifier->bEnabled)
			{
				continue;
			}
			if (!Modifier->SupportsTarget(Target))
			{
				Context.Warn(Modifier, FText::Format(
					LOCTEXT("UnsupportedTarget", "Modifier {0} ({1}) does not modify {2} data; it was skipped."),
					FText::AsNumber(Index), Modifier->GetModifierDisplayName(), TargetName(Target)));
				continue;
			}

			TArray<DataType*> Next;
			for (DataType* Data : Current)
			{
				if (Data)
				{
					Modify(*Modifier, Data, Next);
				}
			}
			Current = MoveTemp(Next);
		}
		return Current;
	}
}

bool FPCGUtilsModifierStack::HasActiveModifiers(EPCGUtilsModifierTarget Target) const
{
	return Modifiers.ContainsByPredicate([Target](const UPCGUtilsComponentModifier* Modifier)
	{
		return Modifier && Modifier->bEnabled && Modifier->SupportsTarget(Target);
	});
}

TArray<UPCGBasePointData*> FPCGUtilsModifierStack::ApplyToPoints(const FPCGUtilsModifierContext& Context, UPCGBasePointData* In) const
{
	return PCGUtilsModifierStack::Run<UPCGBasePointData>(Modifiers, Context, In, EPCGUtilsModifierTarget::Marker,
		[&Context](const UPCGUtilsComponentModifier& Modifier, UPCGBasePointData* Data, TArray<UPCGBasePointData*>& Out)
		{
			Modifier.ModifyPoints(Context, Data, Out);
		});
}

TArray<UPCGSplineData*> FPCGUtilsModifierStack::ApplyToPath(const FPCGUtilsModifierContext& Context, UPCGSplineData* In) const
{
	return PCGUtilsModifierStack::Run<UPCGSplineData>(Modifiers, Context, In, EPCGUtilsModifierTarget::Path,
		[&Context](const UPCGUtilsComponentModifier& Modifier, UPCGSplineData* Data, TArray<UPCGSplineData*>& Out)
		{
			Modifier.ModifyPath(Context, Data, Out);
		});
}

//////////////////////////////////////////////////////////////////////////
// Owner interface

FTransform IPCGUtilsModifierStackOwner::GetModifierSpaceToWorld() const
{
	const UObject* Self = Cast<UObject>(this);
	if (const USceneComponent* Scene = Cast<USceneComponent>(Self))
	{
		return Scene->GetComponentTransform();
	}
	if (const AActor* Actor = Cast<AActor>(Self))
	{
		return Actor->GetActorTransform();
	}
	return FTransform::Identity;
}

const FPCGUtilsModifierStack* IPCGUtilsModifierStackOwner::FindStack(const UObject* Object)
{
	const IPCGUtilsModifierStackOwner* Owner = Cast<IPCGUtilsModifierStackOwner>(Object);
	return Owner ? Owner->GetModifierStack() : nullptr;
}

FPCGUtilsModifierContext IPCGUtilsModifierStackOwner::MakeContext(const UObject* Object, FPCGContext* PCGContext, TArray<FPCGUtilsModifierWarning>* Warnings)
{
	FPCGUtilsModifierContext Context;
	Context.PCGContext = PCGContext;
	Context.Owner = Object;
	Context.Warnings = Warnings;
	if (const IPCGUtilsModifierStackOwner* Owner = Cast<IPCGUtilsModifierStackOwner>(Object))
	{
		Context.ModifierToWorld = Owner->GetModifierSpaceToWorld();
	}
	return Context;
}

//////////////////////////////////////////////////////////////////////////
// Helpers

FSplinePoint FPCGUtilsModifierHelpers::TransformSplinePoint(const FSplinePoint& Point, const FMatrix& Matrix)
{
	FSplinePoint Result = Point;
	Result.Position = Matrix.TransformPosition(Point.Position);
	Result.ArriveTangent = Matrix.TransformVector(Point.ArriveTangent);
	Result.LeaveTangent = Matrix.TransformVector(Point.LeaveTangent);

	// Rotation: carry the point's frame through the matrix and keep only its rotation. A reflection comes out as a
	// proper rotation plus a negative scale axis; spline points have no use for the sign, so it is dropped.
	const FTransform Frame(FRotationMatrix(Point.Rotation) * Matrix);
	Result.Rotation = Frame.Rotator();
	Result.Scale = Point.Scale * FTransform(Matrix).GetScale3D().GetAbs();
	return Result;
}

FPCGUtilsModifierPath FPCGUtilsModifierHelpers::ReadPath(const FPCGUtilsModifierContext& Context, const UPCGSplineData* In)
{
	FPCGUtilsModifierPath Path;
	if (!In)
	{
		return Path;
	}

	const FMatrix LocalToModifier = In->SplineStruct.GetTransform().ToMatrixWithScale() * Context.ModifierToWorld.ToInverseMatrixWithScale();
	const TArray<FSplinePoint> Points = In->GetSplinePoints();
	Path.Points.Reserve(Points.Num());
	Path.SourceIndices.Reserve(Points.Num());
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		Path.Add(TransformSplinePoint(Points[Index], LocalToModifier), Index);
	}
	Path.bClosed = In->IsClosed();
	return Path;
}

UPCGSplineData* FPCGUtilsModifierHelpers::WritePath(const FPCGUtilsModifierContext& Context, const UPCGSplineData* Source, const FPCGUtilsModifierPath& Path)
{
	UPCGSplineData* Out = Context.NewData<UPCGSplineData>();
	if (Source)
	{
		Out->InitializeFromData(Source);
	}

	TArray<FSplinePoint> Points = Path.Points;
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		Points[Index].InputKey = static_cast<float>(Index);
	}

	// Control points keep metadata correspondence when the source has per-point entries.
	TArray<PCGMetadataEntryKey> EntryKeys;
	const TConstArrayView<PCGMetadataEntryKey> SourceKeys = Source ? Source->GetConstVerticesEntryKeys() : TConstArrayView<PCGMetadataEntryKey>();
	FPCGMetadataDomain* ElementsDomain = Out->MutableMetadata() ? Out->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Elements) : nullptr;
	if (!SourceKeys.IsEmpty() && ElementsDomain)
	{
		EntryKeys.Reserve(Points.Num());
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			const int32 SourceIndex = Path.SourceIndices.IsValidIndex(Index) ? Path.SourceIndices[Index] : INDEX_NONE;
			const PCGMetadataEntryKey Parent = SourceKeys.IsValidIndex(SourceIndex) ? SourceKeys[SourceIndex] : PCGInvalidEntryKey;
			EntryKeys.Add(Parent != PCGInvalidEntryKey ? ElementsDomain->AddEntry(Parent) : PCGInvalidEntryKey);
		}
	}

	Out->Initialize(Points, Path.bClosed, Context.ModifierToWorld, MoveTemp(EntryKeys));
	return Out;
}

UPCGBasePointData* FPCGUtilsModifierHelpers::NewPointsFrom(const FPCGUtilsModifierContext& Context, const UPCGBasePointData* Source, const TArray<int32>& SourceIndices)
{
	UPCGPointArrayData* Out = Context.NewData<UPCGPointArrayData>();
	if (!Source)
	{
		return Out;
	}
	Out->InitializeFromDataWithParams(FPCGInitializeFromDataParams(Source));
	Out->SetNumPoints(SourceIndices.Num(), /*bInitializeValues=*/false);
	Out->AllocateProperties(Source->GetAllocatedProperties());

	TArray<int32> WriteIndices;
	WriteIndices.Reserve(SourceIndices.Num());
	for (int32 Index = 0; Index < SourceIndices.Num(); ++Index)
	{
		WriteIndices.Add(Index);
	}
	Source->CopyPropertiesTo(Out, SourceIndices, WriteIndices, EPCGPointNativeProperties::All);

	// Fresh entries, each parented to its source point's, so per-copy attributes can differ.
	if (UPCGMetadata* Metadata = Out->MutableMetadata())
	{
		TPCGValueRange<int64> Entries = Out->GetMetadataEntryValueRange();
		TArray<int64*> EntryPointers;
		EntryPointers.Reserve(Entries.Num());
		for (int64& Entry : Entries)
		{
			EntryPointers.Add(&Entry);
		}
		Metadata->AddEntriesInPlace(EntryPointers);
	}
	return Out;
}

void FPCGUtilsModifierHelpers::WritePointInt(UPCGBasePointData* Data, FName Name, const TArray<int32>& Values)
{
	if (!Data || Name.IsNone())
	{
		return;
	}
	UPCGMetadata* Metadata = Data->MutableMetadata();
	if (!Metadata)
	{
		return;
	}
	FPCGMetadataAttribute<int32>* Attribute = Metadata->FindOrCreateAttribute<int32>(Name, 0, /*bAllowsInterpolation=*/false, /*bOverrideParent=*/true, /*bOverwriteIfTypeMismatch=*/true);
	if (!Attribute)
	{
		return;
	}
	const TConstPCGValueRange<int64> Entries = Data->GetConstMetadataEntryValueRange();
	for (int32 Index = 0; Index < Entries.Num() && Index < Values.Num(); ++Index)
	{
		Attribute->SetValue(Entries[Index], Values[Index]);
	}
}

void FPCGUtilsModifierHelpers::WriteDataInt(UPCGData* Data, FName Name, int32 Value)
{
	if (!Data || Name.IsNone())
	{
		return;
	}
	UPCGMetadata* Metadata = Data->MutableMetadata();
	FPCGMetadataDomain* Domain = Metadata ? Metadata->GetMetadataDomain(PCGMetadataDomainID::Data) : nullptr;
	if (!Domain)
	{
		return;
	}
	// Replace rather than find: an inherited attribute of the same name belongs to the source dataset.
	Domain->DeleteAttribute(Name);
	if (FPCGMetadataAttribute<int32>* Attribute = Domain->CreateAttribute<int32>(Name, Value, /*bAllowsInterpolation=*/false, /*bOverrideParent=*/true))
	{
		Attribute->SetValue(PCGInvalidEntryKey, Value);
	}
}

UPCGSplineData* FPCGUtilsModifierHelpers::MakeSpline(FPCGContext* PCGContext, const TArray<FPCGPathPoint>& Points, bool bIsLocalSpace,
	const FTransform& LocalToWorld, bool bIsLinearPath, bool bIsClosed)
{
	UPCGSplineData* Spline = FPCGContext::NewObject_AnyThread<UPCGSplineData>(PCGContext);
	TArray<FSplinePoint> SplinePoints;
	SplinePoints.Reserve(Points.Num());
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		const FPCGPathPoint& Point = Points[Index];
		const bool bHasTangents = !Point.ArriveTangent.IsNearlyZero() || !Point.LeaveTangent.IsNearlyZero();
		const ESplinePointType::Type Type = bIsLinearPath
			? ESplinePointType::Linear
			: (bHasTangents ? ESplinePointType::CurveCustomTangent : ESplinePointType::Curve);
		SplinePoints.Emplace(static_cast<float>(Index), Point.Transform.GetLocation(), Point.ArriveTangent, Point.LeaveTangent,
			Point.Transform.Rotator(), Point.Transform.GetScale3D(), Type);
	}
	Spline->Initialize(SplinePoints, bIsClosed, bIsLocalSpace ? LocalToWorld : FTransform::Identity);
	return Spline;
}

TArray<FPCGPathPoint> FPCGUtilsModifierHelpers::SplineToPathPoints(const UPCGSplineData* Spline)
{
	TArray<FPCGPathPoint> Result;
	if (!Spline)
	{
		return Result;
	}
	const FTransform& SplineToWorld = Spline->SplineStruct.GetTransform();
	const TArray<FSplinePoint> Points = Spline->GetSplinePoints();
	Result.Reserve(Points.Num());
	for (const FSplinePoint& Point : Points)
	{
		FPCGPathPoint& PathPoint = Result.AddDefaulted_GetRef();
		PathPoint.Transform = FTransform(Point.Rotation, Point.Position, Point.Scale) * SplineToWorld;
		PathPoint.ArriveTangent = SplineToWorld.TransformVector(Point.ArriveTangent);
		PathPoint.LeaveTangent = SplineToWorld.TransformVector(Point.LeaveTangent);
	}
	return Result;
}

bool FPCGUtilsModifierHelpers::IsLinearSpline(const UPCGSplineData* Spline)
{
	if (!Spline)
	{
		return true;
	}
	for (const FSplinePoint& Point : Spline->GetSplinePoints())
	{
		if (Point.Type != ESplinePointType::Linear)
		{
			return false;
		}
	}
	return true;
}

void FPCGUtilsModifierHelpers::CopyDataDomain(const UPCGData* From, UPCGData* To)
{
	const UPCGMetadata* FromMetadata = From ? From->ConstMetadata() : nullptr;
	UPCGMetadata* ToMetadata = To ? To->MutableMetadata() : nullptr;
	const FPCGMetadataDomain* FromDomain = FromMetadata ? FromMetadata->GetConstMetadataDomain(PCGMetadataDomainID::Data) : nullptr;
	FPCGMetadataDomain* ToDomain = ToMetadata ? ToMetadata->GetMetadataDomain(PCGMetadataDomainID::Data) : nullptr;
	if (!FromDomain || !ToDomain)
	{
		return;
	}
	TArray<FName> Names;
	TArray<EPCGMetadataTypes> Types;
	FromDomain->GetAttributes(Names, Types);
	for (const FName Name : Names)
	{
		ToDomain->DeleteAttribute(Name);
		ToDomain->CopyAttribute(FromDomain, Name, Name);
	}
}

void FPCGUtilsModifierHelpers::LogWarningsOnGraph(FPCGContext* PCGContext, const UObject* Owner, const TArray<FPCGUtilsModifierWarning>& Warnings)
{
	if (!PCGContext)
	{
		return;
	}
	const UActorComponent* Component = Cast<UActorComponent>(Owner);
	const FString OwnerName = Component && Component->GetOwner()
		? FString::Printf(TEXT("%s.%s"), *Component->GetOwner()->GetActorNameOrLabel(), *Component->GetName())
		: GetNameSafe(Owner);
	for (const FPCGUtilsModifierWarning& Warning : Warnings)
	{
		PCGLog::LogWarningOnGraph(FText::Format(INVTEXT("{0}: {1}"), FText::FromString(OwnerName), Warning.Message), PCGContext);
	}
}

bool FPCGUtilsModifierHelpers::HasStack(const UObject* Owner)
{
	const FPCGUtilsModifierStack* Stack = IPCGUtilsModifierStackOwner::FindStack(Owner);
	return Stack && !Stack->Modifiers.IsEmpty();
}

TArray<UPCGBasePointData*> FPCGUtilsModifierHelpers::ApplyOwnerStackToPoints(FPCGContext* PCGContext, const UObject* Owner, UPCGBasePointData* In)
{
	if (!HasStack(Owner))
	{
		return {In};
	}
	TArray<FPCGUtilsModifierWarning> Warnings;
	TArray<UPCGBasePointData*> Result = IPCGUtilsModifierStackOwner::FindStack(Owner)->ApplyToPoints(
		IPCGUtilsModifierStackOwner::MakeContext(Owner, PCGContext, &Warnings), In);
	LogWarningsOnGraph(PCGContext, Owner, Warnings);
	return Result;
}

TArray<UPCGSplineData*> FPCGUtilsModifierHelpers::ApplyOwnerStackToPath(FPCGContext* PCGContext, const UObject* Owner, UPCGSplineData* In)
{
	if (!HasStack(Owner))
	{
		return {In};
	}
	TArray<FPCGUtilsModifierWarning> Warnings;
	TArray<UPCGSplineData*> Result = IPCGUtilsModifierStackOwner::FindStack(Owner)->ApplyToPath(
		IPCGUtilsModifierStackOwner::MakeContext(Owner, PCGContext, &Warnings), In);
	LogWarningsOnGraph(PCGContext, Owner, Warnings);
	return Result;
}

#undef LOCTEXT_NAMESPACE
