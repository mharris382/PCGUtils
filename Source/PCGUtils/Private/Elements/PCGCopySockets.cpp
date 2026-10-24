// Copyright Max Harris

#include "Elements/PCGCopySockets.h"

#include "Async/ParallelFor.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGPointArrayData.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Helpers/PCGHelpers.h"
#include "Metadata/Accessors/IPCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"
#include "PCGPin.h"
#include "PCGUtilsAttributeTargets.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGCopySockets)

#define LOCTEXT_NAMESPACE "PCGCopySockets"

namespace PCGUtilsCopySockets
{
	TArray<FSelectedSocket> SelectSockets(const UStaticMesh& Mesh, EPCGUtilsSocketSelectionMode Mode,
		TConstArrayView<FName> SocketNames, const FString& SocketTag, TArray<FName>* OutMissingNames)
	{
		auto ToSelected = [](const UStaticMeshSocket& Socket)
		{
			return FSelectedSocket{Socket.SocketName, Socket.Tag,
				FTransform(Socket.RelativeRotation, Socket.RelativeLocation, Socket.RelativeScale)};
		};

		TArray<FSelectedSocket> Result;
		if (Mode == EPCGUtilsSocketSelectionMode::SocketNames)
		{
			// Listed order, so the user decides the order of the copies.
			for (const FName Name : SocketNames)
			{
				const UStaticMeshSocket* Socket = Mesh.FindSocket(Name);
				if (Socket)
				{
					Result.Add(ToSelected(*Socket));
				}
				else if (OutMissingNames)
				{
					OutMissingNames->AddUnique(Name);
				}
			}
			return Result;
		}

		for (const UStaticMeshSocket* Socket : Mesh.Sockets)
		{
			if (Socket && (Mode == EPCGUtilsSocketSelectionMode::AllSockets || Socket->Tag == SocketTag))
			{
				Result.Add(ToSelected(*Socket));
			}
		}
		return Result;
	}
}

namespace PCGCopySocketsElementHelpers
{
	/** Reads the mesh path of every point (or the one @Data path, broadcast). Logs and returns false when unreadable. */
	bool ReadMeshPaths(FPCGContext* Context, const UPCGCopySocketsSettings& Settings, const UPCGBasePointData& Points,
		int32 InputIndex, TArray<FSoftObjectPath>& OutPaths)
	{
		const FPCGAttributePropertyInputSelector Selector = Settings.MeshAttribute.CopyAndFixLast(&Points);
		const TUniquePtr<const IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateConstAccessor(&Points, Selector);
		const TUniquePtr<const IPCGAttributeAccessorKeys> Keys = PCGAttributeAccessorHelpers::CreateConstKeys(&Points, Selector);
		if (!Accessor || !Keys)
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("NoMeshAttribute",
				"Copy Sockets: Mesh Attribute '{0}' does not exist on input {1}. Its points were skipped."),
				Settings.MeshAttribute.GetDisplayText(), FText::AsNumber(InputIndex)), Context);
			return false;
		}

		OutPaths.SetNum(Keys->GetNum());
		if (!Accessor->GetRange<FSoftObjectPath>(OutPaths, 0, *Keys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible))
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("MeshAttributeType",
				"Copy Sockets: Mesh Attribute '{0}' on input {1} cannot be read as a Soft Object Path or string. Its points were skipped."),
				Settings.MeshAttribute.GetDisplayText(), FText::AsNumber(InputIndex)), Context);
			return false;
		}
		return true;
	}

	/** Resolves a per-point (or @Data) output name, which must land on the points: the values differ per point. */
	bool ResolvePointTarget(FPCGContext* Context, UPCGData& Output, FName Name, const TCHAR* SettingLabel,
		PCGUtilsAttributeTargets::FResolvedTarget& OutTarget)
	{
		if (Name.IsNone())
		{
			PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("NameNone",
				"Copy Sockets: {0} is None, so that attribute was not written. Name it or turn the output off."),
				FText::FromString(SettingLabel)), Context);
			return false;
		}
		FText Error;
		if (!PCGUtilsAttributeTargets::Resolve(Output, Name, SettingLabel, OutTarget, Error))
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("NameBad", "Copy Sockets: {0}"), Error), Context);
			return false;
		}
		if (OutTarget.IsDataDomain())
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("NotPointDomain",
				"Copy Sockets: {0} is '{1}', but the value differs per point; expected a point attribute name without a domain prefix."),
				FText::FromString(SettingLabel), FText::FromName(Name)), Context);
			return false;
		}
		return true;
	}
}

UPCGCopySocketsSettings::UPCGCopySocketsSettings()
{
	MeshAttribute.SetAttributeName(TEXT("Mesh"));
}

TArray<FPCGPinProperties> UPCGCopySocketsSettings::InputPinProperties() const
{
	FPCGPinProperties Pin(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point);
#if WITH_EDITOR
	Pin.Tooltip = LOCTEXT("InTip", "Points to replace. Each is treated as an instance of the mesh in Mesh Attribute.");
#endif
	return {Pin};
}

TArray<FPCGPinProperties> UPCGCopySocketsSettings::OutputPinProperties() const
{
	FPCGPinProperties Pin(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point);
#if WITH_EDITOR
	Pin.Tooltip = LOCTEXT("OutTip", "One data set per input: one point per selected socket per input point, with that point's attributes.");
#endif
	return {Pin};
}

#if WITH_EDITOR
FText UPCGCopySocketsSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Utils | Copy Sockets");
}

FText UPCGCopySocketsSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip",
		"Treats each point as an instance of its static mesh and replaces it with one point per selected socket, placed "
		"where that socket sits on the instance. Output points keep their original point's attributes and properties; "
		"@Data and tags carry over. Points whose mesh is missing or has no selected sockets produce no points.");
}
#endif

FString UPCGCopySocketsSettings::GetAdditionalTitleInformation() const
{
	switch (SocketSelection)
	{
	case EPCGUtilsSocketSelectionMode::AllSockets:
		return TEXT("AllSockets");
	case EPCGUtilsSocketSelectionMode::SocketTag:
		return FString::Printf(TEXT("SocketTag: %s"), *SocketTag);
	case EPCGUtilsSocketSelectionMode::SocketNames:
	default:
		return SocketNames.Num() == 1 ? SocketNames[0].ToString() : FString(TEXT("Multiple Sockets"));
	}
}

FPCGElementPtr UPCGCopySocketsSettings::CreateElement() const
{
	return MakeShared<FPCGCopySocketsElement>();
}

bool FPCGCopySocketsElement::PrepareDataInternal(FPCGContext* InContext) const
{
	FPCGCopySocketsContext* Context = static_cast<FPCGCopySocketsContext*>(InContext);
	const UPCGCopySocketsSettings* Settings = Context->GetInputSettings<UPCGCopySocketsSettings>();
	check(Settings);
	if (Context->WasLoadRequested())
	{
		return true;
	}

	TSet<FSoftObjectPath> Unique;
	for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel))
	{
		const UPCGBasePointData* Points = Cast<const UPCGBasePointData>(Input.Data);
		if (!Points)
		{
			continue;
		}
		// Quiet read: ExecuteInternal reports unreadable attributes against the input that has them.
		const FPCGAttributePropertyInputSelector Selector = Settings->MeshAttribute.CopyAndFixLast(Points);
		const TUniquePtr<const IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateConstAccessor(Points, Selector, /*bQuiet=*/true);
		const TUniquePtr<const IPCGAttributeAccessorKeys> Keys = PCGAttributeAccessorHelpers::CreateConstKeys(Points, Selector);
		if (!Accessor || !Keys)
		{
			continue;
		}
		TArray<FSoftObjectPath> Paths;
		Paths.SetNum(Keys->GetNum());
		if (Accessor->GetRange<FSoftObjectPath>(Paths, 0, *Keys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible))
		{
			Unique.Append(Paths);
		}
	}
	Unique.Remove(FSoftObjectPath());
	return Context->RequestResourceLoad(Context, Unique.Array(), !Settings->bSynchronousLoad);
}

bool FPCGCopySocketsElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGCopySocketsElement::ExecuteInternal);
	using namespace PCGCopySocketsElementHelpers;
	const UPCGCopySocketsSettings* Settings = Context->GetInputSettings<UPCGCopySocketsSettings>();
	check(Settings);

	if (Settings->SocketSelection == EPCGUtilsSocketSelectionMode::SocketNames && Settings->SocketNames.IsEmpty())
	{
		PCGLog::LogWarningOnGraph(LOCTEXT("NoNames", "Copy Sockets: Socket Selection is Socket Names but Socket Names is empty, so no sockets are copied."), Context);
	}

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	for (int32 InputIndex = 0; InputIndex < Inputs.Num(); ++InputIndex)
	{
		const FPCGTaggedData& Input = Inputs[InputIndex];
		const UPCGBasePointData* InPoints = Cast<const UPCGBasePointData>(Input.Data);
		if (!InPoints)
		{
			PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("NotPoints",
				"Copy Sockets: input {0} is {1}, expected point data. It was skipped."),
				FText::AsNumber(InputIndex), FText::FromString(Input.Data ? Input.Data->GetClass()->GetName() : TEXT("null"))), Context);
			continue;
		}

		const int32 NumIn = InPoints->GetNumPoints();
		TArray<FSoftObjectPath> Paths;
		if (NumIn > 0 && !ReadMeshPaths(Context, *Settings, *InPoints, InputIndex, Paths))
		{
			continue;
		}
		// One path read from @Data applies to every point.
		const bool bBroadcast = Paths.Num() == 1 && NumIn != 1;

		// Resolve each distinct mesh once, and pick its sockets once.
		TMap<FSoftObjectPath, int32> MeshIndexByPath;
		TArray<TArray<PCGUtilsCopySockets::FSelectedSocket>> SocketsByMesh;
		TArray<int32> MeshIndexByKey;
		MeshIndexByKey.SetNumUninitialized(Paths.Num());
		for (int32 Key = 0; Key < Paths.Num(); ++Key)
		{
			const FSoftObjectPath& Path = Paths[Key];
			if (const int32* Found = MeshIndexByPath.Find(Path))
			{
				MeshIndexByKey[Key] = *Found;
				continue;
			}

			const int32 MeshIndex = SocketsByMesh.AddDefaulted();
			MeshIndexByPath.Add(Path, MeshIndex);
			MeshIndexByKey[Key] = MeshIndex;

			const FString Where = bBroadcast ? FString(TEXT("@Data")) : FString::Printf(TEXT("point %d"), Key);
			if (Path.IsNull())
			{
				PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("EmptyPath",
					"Copy Sockets: input {0}, {1}: Mesh Attribute '{2}' is empty. Points without a mesh produce no points."),
					FText::AsNumber(InputIndex), FText::FromString(Where), Settings->MeshAttribute.GetDisplayText()), Context);
				continue;
			}
			UObject* Object = Path.ResolveObject();
			const UStaticMesh* Mesh = Cast<UStaticMesh>(Object);
			if (!Mesh)
			{
				PCGLog::LogErrorOnGraph(FText::Format(Object
					? LOCTEXT("NotMesh", "Copy Sockets: input {0}, {1}: '{2}' is a {3}, expected a Static Mesh. Points using it produce no points.")
					: LOCTEXT("NotLoaded", "Copy Sockets: input {0}, {1}: '{2}' could not be loaded. Points using it produce no points."),
					FText::AsNumber(InputIndex), FText::FromString(Where), FText::FromString(Path.ToString()),
					FText::FromString(Object ? Object->GetClass()->GetName() : FString())), Context);
				continue;
			}

			TArray<FName> Missing;
			SocketsByMesh[MeshIndex] = PCGUtilsCopySockets::SelectSockets(*Mesh, Settings->SocketSelection,
				Settings->SocketNames, Settings->SocketTag, &Missing);
			if (!Missing.IsEmpty())
			{
				PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("MissingNames",
					"Copy Sockets: input {0}: mesh '{1}' has no socket named {2}. Those names were skipped for its points."),
					FText::AsNumber(InputIndex), FText::FromString(Mesh->GetName()),
					FText::FromString(FString::JoinBy(Missing, TEXT(", "), [](FName N) { return FString::Printf(TEXT("'%s'"), *N.ToString()); }))), Context);
			}
			else if (SocketsByMesh[MeshIndex].IsEmpty() && Settings->SocketSelection != EPCGUtilsSocketSelectionMode::SocketNames)
			{
				PCGLog::LogWarningOnGraph(FText::Format(Settings->SocketSelection == EPCGUtilsSocketSelectionMode::AllSockets
					? LOCTEXT("NoSockets", "Copy Sockets: input {0}: mesh '{1}' has no sockets. Its points produce no points.")
					: LOCTEXT("NoTagged", "Copy Sockets: input {0}: mesh '{1}' has no socket tagged '{2}'. Its points produce no points."),
					FText::AsNumber(InputIndex), FText::FromString(Mesh->GetName()), FText::FromString(Settings->SocketTag)), Context);
			}
		}

		// Where each input point's copies start in the output.
		TArray<int32> Offsets;
		Offsets.SetNumUninitialized(NumIn + 1);
		Offsets[0] = 0;
		for (int32 Index = 0; Index < NumIn; ++Index)
		{
			Offsets[Index + 1] = Offsets[Index] + SocketsByMesh[MeshIndexByKey[bBroadcast ? 0 : Index]].Num();
		}
		const int32 NumOut = Offsets[NumIn];

		UPCGBasePointData* OutPoints = FPCGContext::NewObject_AnyThread<UPCGPointArrayData>(Context);
		OutPoints->InitializeFromDataWithParams(FPCGInitializeFromDataParams(InPoints));
		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		Output.Pin = PCGPinConstants::DefaultOutputLabel;
		Output.Data = OutPoints;
		if (NumOut == 0)
		{
			continue;
		}

		OutPoints->SetNumPoints(NumOut, /*bInitializeValues=*/false);
		OutPoints->AllocateProperties(InPoints->GetAllocatedProperties() | EPCGPointNativeProperties::Transform
			| EPCGPointNativeProperties::Seed | EPCGPointNativeProperties::BoundsMin | EPCGPointNativeProperties::BoundsMax
			| EPCGPointNativeProperties::MetadataEntry);

		// Copy every property (including the metadata entry) from the source point, then place the copies.
		TArray<int32> ReadIndices, WriteIndices;
		ReadIndices.SetNumUninitialized(NumOut);
		WriteIndices.SetNumUninitialized(NumOut);
		ParallelFor(NumIn, [&](int32 Index)
		{
			for (int32 Out = Offsets[Index]; Out < Offsets[Index + 1]; ++Out)
			{
				ReadIndices[Out] = Index;
				WriteIndices[Out] = Out;
			}
		});
		InPoints->CopyPropertiesTo(OutPoints, ReadIndices, WriteIndices, EPCGPointNativeProperties::All);

		const FBox SocketBounds(FVector(-1.0), FVector(1.0));
		auto Transforms = OutPoints->GetTransformValueRange();
		auto Seeds = OutPoints->GetSeedValueRange();
		auto Mins = OutPoints->GetBoundsMinValueRange();
		auto Maxs = OutPoints->GetBoundsMaxValueRange();
		ParallelFor(NumIn, [&](int32 Index)
		{
			const int32 Start = Offsets[Index];
			const int32 Count = Offsets[Index + 1] - Start;
			if (Count == 0)
			{
				return;
			}
			const TArray<PCGUtilsCopySockets::FSelectedSocket>& Sockets = SocketsByMesh[MeshIndexByKey[bBroadcast ? 0 : Index]];
			// Read before overwriting: the copied transform is the instance's.
			const FTransform Instance = Transforms[Start];
			const int32 SourceSeed = Seeds[Start];
			for (int32 Socket = 0; Socket < Count; ++Socket)
			{
				const int32 Out = Start + Socket;
				Transforms[Out] = Sockets[Socket].LocalTransform * Instance;
				Seeds[Out] = PCGHelpers::ComputeSeed(SourceSeed, Socket);
				// A socket is a location, not the mesh: the copies get unit bounds rather than the instance's box.
				Mins[Out] = SocketBounds.Min;
				Maxs[Out] = SocketBounds.Max;
			}
		});

		PCGUtilsAttributeTargets::FResolvedTarget NameTarget, TagTarget;
		const bool bWriteName = Settings->bOutputSocketName
			&& ResolvePointTarget(Context, *OutPoints, Settings->SocketNameAttributeName, TEXT("Socket Name Attribute Name"), NameTarget);
		const bool bWriteTag = Settings->bOutputSocketTag
			&& ResolvePointTarget(Context, *OutPoints, Settings->SocketTagAttributeName, TEXT("Socket Tag Attribute Name"), TagTarget);
		if (!bWriteName && !bWriteTag)
		{
			continue;
		}

		// The copies share their source point's entry; give each its own (parented to it) before writing per-point values.
		auto Entries = OutPoints->GetMetadataEntryValueRange();
		TArray<int64*> EntryPointers;
		EntryPointers.SetNumUninitialized(NumOut);
		for (int32 Out = 0; Out < NumOut; ++Out)
		{
			EntryPointers[Out] = &Entries[Out];
		}
		OutPoints->MutableMetadata()->AddEntriesInPlace(EntryPointers);

		TArray<FName> Names;
		TArray<FString> Tags;
		if (bWriteName) Names.SetNumUninitialized(NumOut);
		if (bWriteTag) Tags.SetNum(NumOut);
		ParallelFor(NumIn, [&](int32 Index)
		{
			const int32 Start = Offsets[Index];
			const TArray<PCGUtilsCopySockets::FSelectedSocket>& Sockets = SocketsByMesh[MeshIndexByKey[bBroadcast ? 0 : Index]];
			for (int32 Socket = 0; Socket < Offsets[Index + 1] - Start; ++Socket)
			{
				if (bWriteName) Names[Start + Socket] = Sockets[Socket].Name;
				if (bWriteTag) Tags[Start + Socket] = Sockets[Socket].Tag;
			}
		});

		if (bWriteName)
		{
			NameTarget.Domain->DeleteAttribute(NameTarget.AttributeName);
			FPCGMetadataAttribute<FName>* Attribute = NameTarget.Domain->CreateAttribute<FName>(NameTarget.AttributeName, NAME_None, false, true);
			Attribute->SetValues(Entries, Names);
		}
		if (bWriteTag)
		{
			TagTarget.Domain->DeleteAttribute(TagTarget.AttributeName);
			FPCGMetadataAttribute<FString>* Attribute = TagTarget.Domain->CreateAttribute<FString>(TagTarget.AttributeName, FString(), false, true);
			Attribute->SetValues(Entries, Tags);
		}
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
