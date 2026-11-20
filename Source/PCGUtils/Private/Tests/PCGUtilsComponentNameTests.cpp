// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Components/PCGMarkerComponent.h"
#include "Components/PCGSplineComponent.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGPointData.h"
#include "Data/PCGSplineData.h"
#include "Data/PCGUtilsComponentData.h"
#include "Elements/PCGGetMarkerData.h"
#include "Elements/PCGGetPathData.h"
#include "Elements/PCGGetPointData.h"
#include "GameFramework/Actor.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Modifiers/PCGUtilsArrayModifier.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "ShapePath/ShapePathComponent.h"

namespace PCGUtilsComponentNameTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	template <typename ElementType>
	struct TReader : ElementType { using ElementType::ProcessActor; };

	template <typename ElementType, typename SettingsType>
	TArray<FPCGTaggedData> Read(SettingsType* Settings, AActor* Actor)
	{
		FPCGDataFromActorContext Context;
		Context.ComponentSelector = Settings->ComponentSelector;
		TReader<ElementType>().ProcessActor(&Context, Settings, Actor);
		return Context.OutputData.TaggedData;
	}

	template <typename ComponentType>
	ComponentType* Add(AActor* Actor, FName Tag)
	{
		ComponentType* Component = NewObject<ComponentType>(Actor);
		Actor->AddOwnedComponent(Component);
		Component->ComponentTags.Add(Tag);
		return Component;
	}

	const FPCGMetadataAttribute<FName>* NameAttribute(const UPCGData* Data, FName Name, FPCGMetadataDomainID Domain)
	{
		return Data && Data->ConstMetadata()
			? Data->ConstMetadata()->GetConstTypedAttribute<FName>(FPCGAttributeIdentifier(Name, Domain)) : nullptr;
	}

	template <typename SettingsType, typename ElementType>
	bool CheckMarkerReader(FAutomationTestBase& Test)
	{
		AActor* Actor = NewObject<AActor>();
		Actor->Tags.Add(TEXT("NamesActor"));
		for (FName Tag : {FName(TEXT("First")), FName(TEXT("Second"))})
		{
			UPCGMarkerComponent* Marker = Add<UPCGMarkerComponent>(Actor, Tag);
			Marker->PointData.MarkerName = Tag;
			Marker->PointData.GroupID = Tag == TEXT("First") ? 11 : 22;
			UPCGUtilsArrayModifier* Array = NewObject<UPCGUtilsArrayModifier>(Marker);
			Array->Count = 2;
			Marker->ModifierStack.Modifiers.Add(Array);
		}
		SettingsType* Settings = NewObject<SettingsType>();
		Test.TestFalse(TEXT("Marker-name extraction defaults off"), Settings->PointSettings.bExtractMarkerName);
		for (const FPCGTaggedData& Output : Read<ElementType>(Settings, Actor))
		{
			Test.TestNull(TEXT("Disabled marker extraction creates no Name attribute"),
				NameAttribute(Output.Data, TEXT("MarkerName"), PCGMetadataDomainID::Elements));
		}

		Settings->PointSettings.bExtractMarkerName = true;
		Settings->PointSettings.MarkerNameAttributeName = TEXT("DemoMarkerRole");
		for (EPCGUtilsPointMetadataDomain Domain : {EPCGUtilsPointMetadataDomain::Elements, EPCGUtilsPointMetadataDomain::Data})
		{
			Settings->PointSettings.AttributeDomain = Domain;
			const FPCGMetadataDomainID MetadataDomain = Domain == EPCGUtilsPointMetadataDomain::Elements
				? PCGMetadataDomainID::Elements : PCGMetadataDomainID::Data;
			const TArray<FPCGTaggedData> Outputs = Read<ElementType>(Settings, Actor);
			if (!Test.TestEqual(TEXT("Both named marker components produce data"), Outputs.Num(), 2)) return false;
			for (const FPCGTaggedData& Output : Outputs)
			{
				const UPCGBasePointData* Points = Cast<UPCGBasePointData>(Output.Data);
				if (!Test.TestNotNull(TEXT("Marker output is point data"), Points)) return false;
				Test.TestEqual(TEXT("Array copies preserve the marker dataset"), Points->GetNumPoints(), 2);
				const FName Expected = Output.Tags.Contains(TEXT("First")) ? FName(TEXT("First")) : FName(TEXT("Second"));
				const FPCGMetadataAttribute<FName>* Name = NameAttribute(Points, TEXT("DemoMarkerRole"), MetadataDomain);
				if (!Test.TestNotNull(TEXT("Custom attribute has Name type in the selected domain"), Name)) return false;
				const FPCGMetadataAttribute<int32>* Group = Points->ConstMetadata()->GetConstTypedAttribute<int32>(
					FPCGAttributeIdentifier(TEXT("GroupID"), MetadataDomain));
				if (!Test.TestNotNull(TEXT("Existing group extraction is preserved"), Group)) return false;
				for (int32 Index = 0; Index < Points->GetNumPoints(); ++Index)
				{
					const PCGMetadataEntryKey Key = Domain == EPCGUtilsPointMetadataDomain::Elements
						? Points->GetMetadataEntry(Index) : PCGInvalidEntryKey;
					Test.TestEqual(TEXT("Each copy keeps its own component's name"), Name->GetValueFromItemKey(Key), Expected);
					Test.TestEqual(TEXT("Each copy keeps its group ID"), Group->GetValueFromItemKey(Key), Expected == TEXT("First") ? 11 : 22);
				}
				Test.TestTrue(TEXT("Actor tags survive"), Output.Tags.Contains(TEXT("NamesActor")));
				Test.TestNull(TEXT("Renaming output does not also write the conventional name"), NameAttribute(Points, TEXT("MarkerName"), MetadataDomain));
				Test.TestNull(TEXT("Name is not written to the other domain"), NameAttribute(Points, TEXT("DemoMarkerRole"),
					Domain == EPCGUtilsPointMetadataDomain::Elements ? PCGMetadataDomainID::Data : PCGMetadataDomainID::Elements));
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsMarkerNameReadersTest, "PCGUtils.ComponentData.Names.MarkerReaders", PCGUtilsComponentNameTests::Flags)
bool FPCGUtilsMarkerNameReadersTest::RunTest(const FString&)
{
	using namespace PCGUtilsComponentNameTests;
	return CheckMarkerReader<UPCGGetMarkerDataSettings, FPCGGetMarkerDataElement>(*this)
		&& CheckMarkerReader<UPCGGetPointDataSettings, FPCGGetPointDataElement>(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsPathNameReadersTest, "PCGUtils.ComponentData.Names.PathReaders", PCGUtilsComponentNameTests::Flags)
bool FPCGUtilsPathNameReadersTest::RunTest(const FString&)
{
	using namespace PCGUtilsComponentNameTests;
	AActor* Actor = NewObject<AActor>();
	Actor->Tags.Add(TEXT("NamesActor"));
	UPCGSplineComponent* Spline = Add<UPCGSplineComponent>(Actor, TEXT("SplinePath"));
	Spline->PathData.PathName = TEXT("SplineDemo");
	Spline->PathData.GroupID = 11;
	Spline->ClearSplinePoints(false);
	Spline->AddSplinePoint(FVector::ZeroVector, ESplineCoordinateSpace::Local, false);
	Spline->AddSplinePoint(FVector(100, 0, 0), ESplineCoordinateSpace::Local, false);
	Spline->UpdateSpline();
	UPCGUtilsArrayModifier* Array = NewObject<UPCGUtilsArrayModifier>(Spline);
	Array->Count = 2;
	Spline->ModifierStack.Modifiers.Add(Array);
	UShapePathComponent* Shape = Add<UShapePathComponent>(Actor, TEXT("ShapePath"));
	Shape->PathData.PathName = TEXT("ShapeDemo");
	Shape->PathData.GroupID = 22;
	UPCGGetPathDataSettings* Settings = NewObject<UPCGGetPathDataSettings>();
	TestFalse(TEXT("Path-name extraction defaults off"), Settings->PathSettings.bExtractPathName);
	for (EPCGPathDataOutputMode Mode : {EPCGPathDataOutputMode::PointData, EPCGPathDataOutputMode::SplineData})
	{
		Settings->OutputMode = Mode;
		Settings->PathSettings.bExtractPathName = false;
		for (const FPCGTaggedData& Output : Read<FPCGGetPathElement>(Settings, Actor))
		{
			TestNull(TEXT("Disabled path extraction creates no name"), NameAttribute(Output.Data, TEXT("PathName"), PCGMetadataDomainID::Data));
		}
		Settings->PathSettings.bExtractPathName = true;
		Settings->PathSettings.PathNameAttributeName = TEXT("DemoPathRole");
		const TArray<FPCGTaggedData> Outputs = Read<FPCGGetPathElement>(Settings, Actor);
		if (!TestEqual(TEXT("Two modified spline paths plus one shape path"), Outputs.Num(), 3)) return false;
		for (const FPCGTaggedData& Output : Outputs)
		{
			const bool bSpline = Output.Tags.Contains(TEXT("SplinePath"));
			const FName Expected = bSpline ? FName(TEXT("SplineDemo")) : FName(TEXT("ShapeDemo"));
			const FPCGMetadataAttribute<FName>* Name = NameAttribute(Output.Data, TEXT("DemoPathRole"), PCGMetadataDomainID::Data);
			if (!TestNotNull(TEXT("Path Name is an @Data Name attribute in both output modes"), Name)) return false;
			TestEqual(TEXT("Every path retains its source name"), Name->GetValueFromItemKey(PCGInvalidEntryKey), Expected);
			const FPCGMetadataAttribute<int32>* Group = Output.Data->ConstMetadata()->GetConstTypedAttribute<int32>(
				FPCGAttributeIdentifier(TEXT("PathGroup"), PCGMetadataDomainID::Data));
			if (!TestNotNull(TEXT("Existing path group extraction is preserved"), Group)) return false;
			TestEqual(TEXT("Each path keeps its source group"), Group->GetValueFromItemKey(PCGInvalidEntryKey), bSpline ? 11 : 22);
			TestTrue(TEXT("Actor tags survive"), Output.Tags.Contains(TEXT("NamesActor")));
			TestNull(TEXT("Custom path name replaces the default attribute name"), NameAttribute(Output.Data, TEXT("PathName"), PCGMetadataDomainID::Data));
			TestTrue(TEXT("Requested output representation is honored"), Mode == EPCGPathDataOutputMode::PointData
				? Cast<UPCGBasePointData>(Output.Data) != nullptr : Cast<UPCGSplineData>(Output.Data) != nullptr);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsComponentNameSerializationTest, "PCGUtils.ComponentData.Names.Serialization", PCGUtilsComponentNameTests::Flags)
bool FPCGUtilsComponentNameSerializationTest::RunTest(const FString&)
{
	FPathComponentData Path;
	FPointComponentData Marker;
	TestTrue(TEXT("Existing paths receive an unset name by default"), Path.PathName.IsNone());
	TestTrue(TEXT("Existing markers receive an unset name by default"), Marker.MarkerName.IsNone());
	Path.PathName = TEXT("NamedPath");
	Path.GroupID = 73;
	Marker.MarkerName = TEXT("NamedMarker");
	Marker.GroupID = 91;
	FGetPathElementSettingsConfiguration PathSettings;
	PathSettings.bExtractPathName = true;
	PathSettings.PathNameAttributeName = TEXT("PathRole");
	FGetPointElementSettingsConfiguration PointSettings;
	PointSettings.bExtractMarkerName = true;
	PointSettings.MarkerNameAttributeName = TEXT("MarkerRole");
	TArray<uint8> Bytes;
	{
		FMemoryWriter Writer(Bytes);
		FObjectAndNameAsStringProxyArchive Archive(Writer, false);
		FPathComponentData::StaticStruct()->SerializeItem(Archive, &Path, nullptr);
		FPointComponentData::StaticStruct()->SerializeItem(Archive, &Marker, nullptr);
		FGetPathElementSettingsConfiguration::StaticStruct()->SerializeItem(Archive, &PathSettings, nullptr);
		FGetPointElementSettingsConfiguration::StaticStruct()->SerializeItem(Archive, &PointSettings, nullptr);
		TestFalse(TEXT("Authoring structs serialize successfully"), Archive.IsError());
	}
	FPathComponentData LoadedPath;
	FPointComponentData LoadedMarker;
	FGetPathElementSettingsConfiguration LoadedPathSettings;
	FGetPointElementSettingsConfiguration LoadedPointSettings;
	{
		FMemoryReader Reader(Bytes);
		FObjectAndNameAsStringProxyArchive Archive(Reader, false);
		FPathComponentData::StaticStruct()->SerializeItem(Archive, &LoadedPath, nullptr);
		FPointComponentData::StaticStruct()->SerializeItem(Archive, &LoadedMarker, nullptr);
		FGetPathElementSettingsConfiguration::StaticStruct()->SerializeItem(Archive, &LoadedPathSettings, nullptr);
		FGetPointElementSettingsConfiguration::StaticStruct()->SerializeItem(Archive, &LoadedPointSettings, nullptr);
		TestFalse(TEXT("Authoring structs deserialize successfully"), Archive.IsError());
	}
	TestEqual(TEXT("Authored path name survives serialization"), LoadedPath.PathName, Path.PathName);
	TestEqual(TEXT("Authored marker name survives serialization"), LoadedMarker.MarkerName, Marker.MarkerName);
	TestEqual(TEXT("Existing path group survives serialization"), LoadedPath.GroupID, Path.GroupID);
	TestEqual(TEXT("Existing marker group survives serialization"), LoadedMarker.GroupID, Marker.GroupID);
	TestTrue(TEXT("Path extractor flag survives serialization"), LoadedPathSettings.bExtractPathName);
	TestTrue(TEXT("Marker extractor flag survives serialization"), LoadedPointSettings.bExtractMarkerName);
	TestEqual(TEXT("Custom path output name survives serialization"), LoadedPathSettings.PathNameAttributeName, PathSettings.PathNameAttributeName);
	TestEqual(TEXT("Custom marker output name survives serialization"), LoadedPointSettings.MarkerNameAttributeName, PointSettings.MarkerNameAttributeName);

	// An authored None is a valid value, distinct from disabling extraction or providing no attribute name.
	UPCGPointData* EmptyNameData = NewObject<UPCGPointData>();
	FPathComponentData UnnamedPath;
	UPCGUtilPathDataLibrary::GetPathDataFromSettings(EmptyNameData->MutableMetadata(), &PathSettings, &UnnamedPath);
	const auto* UnsetName = PCGUtilsComponentNameTests::NameAttribute(EmptyNameData, TEXT("PathRole"), PCGMetadataDomainID::Data);
	if (!TestNotNull(TEXT("An enabled extractor writes an unset authored name"), UnsetName)) return false;
	TestTrue(TEXT("Unset authored name is emitted as None"), UnsetName->GetValueFromItemKey(PCGInvalidEntryKey).IsNone());
	return true;
}

#endif
