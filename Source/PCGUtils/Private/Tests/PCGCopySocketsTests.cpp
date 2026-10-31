// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Components/InstancedStaticMeshComponent.h"
#include "Data/PCGPointArrayData.h"
#include "Elements/PCGCopySockets.h"
#include "Elements/PCGGetISMData.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataDomain.h"
#include "PCGContext.h"
#include "PCGPin.h"

namespace PCGCopySocketsTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	struct FExecute : FPCGCopySocketsElement
	{
		using FPCGCopySocketsElement::ExecuteInternal;
	};

	UStaticMeshSocket* AddSocket(UStaticMesh* Mesh, FName Name, const FString& Tag, const FVector& Location, const FRotator& Rotation)
	{
		UStaticMeshSocket* Socket = NewObject<UStaticMeshSocket>(Mesh);
		Socket->SocketName = Name;
		Socket->Tag = Tag;
		Socket->RelativeLocation = Location;
		Socket->RelativeRotation = Rotation;
		Mesh->Sockets.Add(Socket);
		return Socket;
	}

	UStaticMesh* MakeMesh()
	{
		UStaticMesh* Mesh = NewObject<UStaticMesh>(GetTransientPackage(), NAME_None, RF_Transient);
		AddSocket(Mesh, TEXT("Top"), TEXT("Anchor"), FVector(0, 0, 100), FRotator::ZeroRotator);
		AddSocket(Mesh, TEXT("Side"), TEXT(""), FVector(50, 0, 0), FRotator(0, 90, 0));
		AddSocket(Mesh, TEXT("Front"), TEXT("Anchor"), FVector(0, 50, 0), FRotator::ZeroRotator);
		return Mesh;
	}

	/** Two points of one mesh, with a point attribute, an @Data attribute and per-point mesh paths. */
	UPCGPointArrayData* MakePoints(UStaticMesh* Mesh, bool bMeshOnData)
	{
		UPCGPointArrayData* Points = NewObject<UPCGPointArrayData>();
		Points->SetNumPoints(2);
		Points->AllocateProperties(EPCGPointNativeProperties::All);
		FPCGMetadataDomain* Elements = Points->Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements);
		FPCGMetadataDomain* Data = Points->Metadata->GetMetadataDomain(PCGMetadataDomainID::Data);
		FPCGMetadataAttribute<int32>* Id = Elements->CreateAttribute<int32>(TEXT("Id"), -1, false);
		Data->CreateAttribute<int32>(TEXT("DataValue"), 7, false);
		if (bMeshOnData)
		{
			Data->CreateAttribute<FSoftObjectPath>(TEXT("Mesh"), FSoftObjectPath(Mesh), false);
		}
		else
		{
			Elements->CreateAttribute<FSoftObjectPath>(TEXT("Mesh"), FSoftObjectPath(Mesh), false);
		}

		auto Transforms = Points->GetTransformValueRange();
		auto Keys = Points->GetMetadataEntryValueRange();
		auto Density = Points->GetDensityValueRange();
		for (int32 I = 0; I < 2; ++I)
		{
			Transforms[I] = FTransform(FRotator(0, 90 * I, 0), FVector(1000 * I, 0, 0), FVector(2.0));
			Keys[I] = Elements->AddEntry();
			Id->SetValue(Keys[I], 10 + I);
			Density[I] = 0.5f + 0.25f * I;
		}
		return Points;
	}

	TArray<FPCGTaggedData> Run(UPCGCopySocketsSettings* Settings, UPCGBasePointData* Points)
	{
		FPCGCopySocketsContext Context;
		Context.InputData.TaggedData.Emplace_GetRef().Data = Settings;
		FPCGTaggedData& Input = Context.InputData.TaggedData.Emplace_GetRef();
		Input.Data = Points;
		Input.Pin = PCGPinConstants::DefaultInputLabel;
		Input.Tags.Add(TEXT("KeptTag"));
		FExecute().ExecuteInternal(&Context);
		return Context.OutputData.TaggedData;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGCopySocketsNamesTest, "PCGUtils.Points.CopySockets.SocketNames", PCGCopySocketsTests::Flags)
bool FPCGCopySocketsNamesTest::RunTest(const FString&)
{
	using namespace PCGCopySocketsTests;
	UStaticMesh* Mesh = MakeMesh();
	UPCGCopySocketsSettings* Settings = NewObject<UPCGCopySocketsSettings>();
	Settings->SocketNames = {TEXT("Side"), TEXT("Top")};
	Settings->bOutputSocketTag = true;

	const TArray<FPCGTaggedData> Outputs = Run(Settings, MakePoints(Mesh, false));
	if (!TestEqual(TEXT("One output per input"), Outputs.Num(), 1)) return false;
	const UPCGBasePointData* Out = Cast<UPCGBasePointData>(Outputs[0].Data);
	if (!TestNotNull(TEXT("Output is point data"), Out)) return false;
	TestTrue(TEXT("Tags kept"), Outputs[0].Tags.Contains(TEXT("KeptTag")));
	if (!TestEqual(TEXT("Two sockets per point"), Out->GetNumPoints(), 4)) return false;

	const UPCGMetadata* Metadata = Out->ConstMetadata();
	const FPCGMetadataAttribute<int32>* Id = Metadata->GetConstTypedAttribute<int32>(TEXT("Id"));
	const FPCGMetadataAttribute<FName>* SocketName = Metadata->GetConstTypedAttribute<FName>(TEXT("SocketName"));
	const FPCGMetadataAttribute<FString>* SocketTag = Metadata->GetConstTypedAttribute<FString>(TEXT("SocketTag"));
	const FPCGMetadataDomain* DataDomain = Metadata->GetConstMetadataDomain(PCGMetadataDomainID::Data);
	TestNotNull(TEXT("@Data attribute kept"), DataDomain ? DataDomain->GetConstAttribute(TEXT("DataValue")) : nullptr);
	if (!TestNotNull(TEXT("Point attribute kept"), Id) || !TestNotNull(TEXT("SocketName written"), SocketName)
		|| !TestNotNull(TEXT("SocketTag written"), SocketTag))
	{
		return false;
	}

	auto Transforms = Out->GetConstTransformValueRange();
	auto Keys = Out->GetConstMetadataEntryValueRange();
	auto Density = Out->GetConstDensityValueRange();
	const FName ExpectedNames[] = {TEXT("Side"), TEXT("Top"), TEXT("Side"), TEXT("Top")};
	for (int32 I = 0; I < 4; ++I)
	{
		const int32 Source = I / 2;
		const FTransform Instance(FRotator(0, 90 * Source, 0), FVector(1000 * Source, 0, 0), FVector(2.0));
		const FTransform Local = I % 2 == 0 ? FTransform(FRotator(0, 90, 0), FVector(50, 0, 0)) : FTransform(FVector(0, 0, 100));
		const FTransform Expected = Local * Instance;
		TestTrue(FString::Printf(TEXT("Point %d placed at its socket"), I), Transforms[I].GetLocation().Equals(Expected.GetLocation(), 1e-3));
		TestTrue(FString::Printf(TEXT("Point %d rotated by its socket"), I), Transforms[I].GetRotation().Equals(Expected.GetRotation(), 1e-4));
		TestEqual(FString::Printf(TEXT("Point %d keeps its source's attribute"), I), Id->GetValueFromItemKey(Keys[I]), 10 + Source);
		TestEqual(FString::Printf(TEXT("Point %d keeps its source's density"), I), Density[I], 0.5f + 0.25f * Source);
		TestEqual(FString::Printf(TEXT("Point %d socket name"), I), SocketName->GetValueFromItemKey(Keys[I]), ExpectedNames[I]);
		TestEqual(FString::Printf(TEXT("Point %d socket tag"), I), SocketTag->GetValueFromItemKey(Keys[I]), FString(I % 2 == 0 ? TEXT("") : TEXT("Anchor")));
	}
	// A third point in world space: socket Top of the second instance is 200 up (scale 2).
	TestTrue(TEXT("Instance scale applies to the socket offset"), Transforms[3].GetLocation().Equals(FVector(1000, 0, 200), 1e-3));

	TestEqual(TEXT("Title shows multiple names"), Settings->GetAdditionalTitleInformation(), FString(TEXT("Multiple Sockets")));
	Settings->SocketNames = {TEXT("Top")};
	TestEqual(TEXT("Title shows the one name"), Settings->GetAdditionalTitleInformation(), FString(TEXT("Top")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGCopySocketsTagAndDataTest, "PCGUtils.Points.CopySockets.TagAndDataMesh", PCGCopySocketsTests::Flags)
bool FPCGCopySocketsTagAndDataTest::RunTest(const FString&)
{
	using namespace PCGCopySocketsTests;
	UStaticMesh* Mesh = MakeMesh();
	UPCGCopySocketsSettings* Settings = NewObject<UPCGCopySocketsSettings>();
	Settings->MeshAttribute.Update(TEXT("@Data.Mesh"));
	Settings->SocketSelection = EPCGUtilsSocketSelectionMode::SocketTag;
	Settings->SocketTag = TEXT("Anchor");
	Settings->bOutputSocketName = false;

	TArray<FPCGTaggedData> Outputs = Run(Settings, MakePoints(Mesh, true));
	const UPCGBasePointData* Out = Outputs.Num() == 1 ? Cast<UPCGBasePointData>(Outputs[0].Data) : nullptr;
	if (!TestNotNull(TEXT("Output from an @Data mesh"), Out)) return false;
	TestEqual(TEXT("Two tagged sockets per point"), Out->GetNumPoints(), 4);
	TestNull(TEXT("SocketName not written when off"), Out->ConstMetadata()->GetConstAttribute(TEXT("SocketName")));
	TestEqual(TEXT("Title shows the tag"), Settings->GetAdditionalTitleInformation(), FString(TEXT("SocketTag: Anchor")));

	Settings->SocketSelection = EPCGUtilsSocketSelectionMode::AllSockets;
	Outputs = Run(Settings, MakePoints(Mesh, true));
	Out = Outputs.Num() == 1 ? Cast<UPCGBasePointData>(Outputs[0].Data) : nullptr;
	TestEqual(TEXT("All sockets"), Out ? Out->GetNumPoints() : -1, 6);
	TestEqual(TEXT("Title shows all"), Settings->GetAdditionalTitleInformation(), FString(TEXT("AllSockets")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGGetISMDataPointsTest, "PCGUtils.Points.GetISMData.InstancePoints", PCGCopySocketsTests::Flags)
bool FPCGGetISMDataPointsTest::RunTest(const FString&)
{
	UStaticMesh* Mesh = PCGCopySocketsTests::MakeMesh();
	UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	Component->SetStaticMesh(Mesh);
	Component->AddInstance(FTransform(FVector(10, 0, 0)));
	Component->AddInstance(FTransform(FRotator(0, 45, 0), FVector(0, 20, 0)));
	Component->AddInstance(FTransform(FVector(0, 0, 30)));

	UPCGGetISMDataSettings* Settings = NewObject<UPCGGetISMDataSettings>();
	Settings->bOutputMaterial = true;
	FPCGContext Context;
	const UPCGBasePointData* Points = FPCGGetISMDataElement::MakeInstancePoints(&Context, *Settings, *Component);
	if (!TestNotNull(TEXT("Points made"), Points)) return false;
	TestEqual(TEXT("One point per instance"), Points->GetNumPoints(), 3);
	TestTrue(TEXT("Instance transform read"), Points->GetConstTransformValueRange()[1].GetLocation().Equals(FVector(0, 20, 0), 1e-3));

	const UPCGMetadata* Metadata = Points->ConstMetadata();
	const FPCGMetadataDomain* Elements = Metadata->GetConstMetadataDomain(PCGMetadataDomainID::Elements);
	const FPCGMetadataDomain* Data = Metadata->GetConstMetadataDomain(PCGMetadataDomainID::Data);
	const FPCGMetadataAttribute<FSoftObjectPath>* MeshAttribute = Elements->GetConstTypedAttribute<FSoftObjectPath>(TEXT("Mesh"));
	if (TestNotNull(TEXT("Mesh written per point by default"), MeshAttribute))
	{
		TestEqual(TEXT("Every point reads the mesh"), MeshAttribute->GetValueFromItemKey(PCGInvalidEntryKey), FSoftObjectPath(Mesh));
	}
	TestNull(TEXT("Mesh not on @Data by default"), Data->GetConstAttribute(TEXT("Mesh")));
	TestNotNull(TEXT("Material on @Data by default"), Data->GetConstAttribute(TEXT("Material")));
	TestNull(TEXT("Material not per point by default"), Elements->GetConstAttribute(TEXT("Material")));

	Settings->MeshAttributeName = TEXT("@Data.SourceMesh");
	Points = FPCGGetISMDataElement::MakeInstancePoints(&Context, *Settings, *Component);
	TestNotNull(TEXT("Renamed mesh lands on @Data"),
		Points->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Data)->GetConstAttribute(TEXT("SourceMesh")));
	return true;
}

#endif
