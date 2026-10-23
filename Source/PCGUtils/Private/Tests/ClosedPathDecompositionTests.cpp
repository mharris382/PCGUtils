#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Geometry/ClosedPathDecomposition.h"
#include "Elements/PCGDecomposeClosedPath.h"
#include "PCGContext.h"
#include "Data/PCGPointArrayData.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataDomain.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "GameFramework/Actor.h"

namespace ClosedPathTests
{
#include "ClosedPathExFixture.inl"
#include "ClosedPathFlatBiasFixture.inl"
constexpr EAutomationTestFlags Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
static TSet<FString> LoopSet(const TArray<ClosedPathDecomposition::FLoop>& Loops)
{
	TSet<FString> Result;
	for(auto L:Loops)
	{
		L.Sort(); FString Key;
		for(int I:L) Key+=FString::Printf(TEXT("%d,"),I);
		Result.Add(Key);
	}
	return Result;
}
class FExecute : public FPCGDecomposeClosedPathElement
{
public:
	using FPCGDecomposeClosedPathElement::ExecuteInternal;
};

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClosedPathReferenceTest,"PCGUtils.Paths.Decomposition.ReferenceAndInvariance",ClosedPathTests::Flags)
bool FClosedPathReferenceTest::RunTest(const FString&)
{
	using namespace ClosedPathDecomposition;
	const TArray<FVector> P=ClosedPathTests::ExPoints();
	const TSet<FString> Expected=ClosedPathTests::LoopSet(ClosedPathTests::ExExpected());
	FOptions Options;TArray<FLoop> Out;FString Error;
	for(int Reverse=0;Reverse<2;++Reverse) for(int Shift=0;Shift<P.Num();++Shift)
	{
		TArray<FVector> Input;TArray<int> Map;
		for(int I=0;I<P.Num();++I)
		{
			const int Index=(Shift+(Reverse?P.Num()-1-I:I))%P.Num();
			Map.Add(Index);
			Input.Add(FQuat(FVector(1,2,3).GetSafeNormal(),.83).RotateVector(P[Index])*2.5+FVector(1000,-500,300));
		}
		if(!Solve(Input,Options,Out,Error)) {AddError(Error);return false;}
		for(FLoop& L:Out) for(int& I:L) I=Map[I];
		const TSet<FString> Actual=ClosedPathTests::LoopSet(Out);
		TestEqual(TEXT("Twelve patches"),Out.Num(),12);
		TestEqual(TEXT("No missing expected patches"),Expected.Difference(Actual).Num(),0);
		TestEqual(TEXT("No unexpected patches"),Actual.Difference(Expected).Num(),0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClosedPathGeometryTest,"PCGUtils.Paths.Decomposition.OverhangAndValidation",ClosedPathTests::Flags)
bool FClosedPathGeometryTest::RunTest(const FString&)
{
	using namespace ClosedPathDecomposition;
	TArray<FVector> P={{0,-1,0},{4,-1,0},{6,-1,2},{4,-1,4},{0,-1,4},
		{0,1,4},{4,1,4},{6,1,2},{4,1,0},{0,1,0}};
	TArray<FLoop> Out;FString Error;FOptions O;
	TestTrue(TEXT("Folded overhang succeeds"),Solve(P,O,Out,Error));
	TestEqual(TEXT("Four panels"),Out.Num(),4);
	const TArray<FLoop> Expected={{0,1,8,9},{1,2,7,8},{2,3,6,7},{3,4,5,6}};
	TestEqual(TEXT("Overhang panels match"),ClosedPathTests::LoopSet(Out).Difference(ClosedPathTests::LoopSet(Expected)).Num(),0);
	TestFalse(TEXT("Coincident input rejected"),Solve({FVector::ZeroVector,FVector::XAxisVector,FVector::ZeroVector},O,Out,Error));
	TestTrue(TEXT("Error names points"),Error.Contains(TEXT("Points 0 and 2")));
	TestEqual(TEXT("No output on failure"),Out.Num(),0);
	O.Concavity=1;
	TestFalse(TEXT("Invalid concavity rejected"),Solve(P,O,Out,Error));
	TestTrue(TEXT("Error names property and value"),Error.Contains(TEXT("Max Concavity=1")));
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClosedPathMetadataTest,"PCGUtils.Paths.Decomposition.NodeMetadataAndCuts",ClosedPathTests::Flags)
bool FClosedPathMetadataTest::RunTest(const FString&)
{
 auto* Settings=NewObject<UPCGDecomposeClosedPathSettings>();
 auto* Source=NewObject<UPCGPointArrayData>();
 const auto P=ClosedPathTests::ExPoints();
 Source->SetNumPoints(P.Num());
 auto* Elements=Source->Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements);
 auto* Vertex=Elements->CreateAttribute<int32>(TEXT("TestVertex"),-1,false);
 auto* Data=Source->Metadata->GetMetadataDomain(PCGMetadataDomainID::Data);
 Data->CreateAttribute<bool>(TEXT("IsClosed"),true,false);
 auto* DataAttribute=Data->CreateAttribute<int32>(TEXT("TestData"),-1,false);
 DataAttribute->SetValue(Data->AddEntry(),913);
 auto Transforms=Source->GetTransformValueRange();
 auto Keys=Source->GetMetadataEntryValueRange();
 auto Density=Source->GetDensityValueRange();
 auto Seeds=Source->GetSeedValueRange();
 auto Color=Source->GetColorValueRange();
 auto Min=Source->GetBoundsMinValueRange();
 auto Max=Source->GetBoundsMaxValueRange();
 auto Steepness=Source->GetSteepnessValueRange();
 for(int I=0;I<P.Num();++I)
 {
  Transforms[I]=FTransform(FRotator(0,I,0),P[I],FVector(1+I*.01,2,3));
  Keys[I]=Elements->AddEntry();Vertex->SetValue(Keys[I],I);
  Density[I]=.3f+I*.01f;Seeds[I]=100+I;Color[I]=FVector4(.1,.2,I*.01,1);
  Min[I]=FVector(-I-1);Max[I]=FVector(I+2);Steepness[I]=.2f+I*.01f;
 }
 Source->TargetActor=GetMutableDefault<AActor>();
 // Disabled, default name, custom name, and inheritance explicitly disabled.
 for(int Mode=0;Mode<4;++Mode)
 {
  Settings->bOutputIsCut=Mode!=0;
  Settings->IsCutAttributeName=Mode==2?TEXT("CustomCut"):TEXT("IsCut");
  Settings->bInheritMetadata=Mode!=3;
  FPCGContext Context;
  Context.InputData.TaggedData.Emplace_GetRef().Data=Settings;
  for(int I=0;I<2;++I)
  {
   auto& Input=Context.InputData.TaggedData.Emplace_GetRef();Input.Data=Source;
   Input.Pin=PCGPinConstants::DefaultInputLabel;Input.Tags.Add(TEXT("PreservedTag"));
  }
  ClosedPathTests::FExecute Execute;
  TestTrue(TEXT("Node executes"),Execute.ExecuteInternal(&Context));
  TestEqual(TEXT("Two inputs emit 24 paths"),Context.OutputData.TaggedData.Num(),24);
  int CutCount=0;bool bClosingCut=false;
  TMap<int32,uint8> VertexRoles;
  for(const auto& Tagged:Context.OutputData.TaggedData)
  {
   const auto* Output=Cast<UPCGPointArrayData>(Tagged.Data);
   if(!TestNotNull(TEXT("PointArrayData output"),Output)) return false;
   TestTrue(TEXT("Tag retained"),Tagged.Tags.Contains(TEXT("PreservedTag")));
   TestTrue(TEXT("Target actor retained"),Output->TargetActor==Source->TargetActor);
   const auto* OutData=Output->Metadata->GetConstMetadataDomain(PCGMetadataDomainID::Data);
   const auto* Closure=OutData->GetConstTypedAttribute<bool>(TEXT("IsClosed"));
   if(TestNotNull(TEXT("Closure exists"),Closure)) TestTrue(TEXT("Closed output"),Closure->GetValueFromItemKey(OutData->GetItemCountForChild() > 0 ? PCGFirstEntryKey : PCGInvalidEntryKey));
   const auto* DA=OutData->GetConstTypedAttribute<int32>(TEXT("TestData"));
   const auto* OutElements=Output->Metadata->GetConstMetadataDomain(PCGMetadataDomainID::Elements);
   const auto* Attr=OutElements->GetConstTypedAttribute<int32>(TEXT("TestVertex"));
   const auto* Cut=OutElements->GetConstTypedAttribute<bool>(Settings->IsCutAttributeName);
   if(Mode==0) TestNull(TEXT("Cut attribute disabled by default"),Cut);
   else if(!TestNotNull(TEXT("Requested cut attribute exists"),Cut)) return false;
   if(Mode==3) {TestNull(TEXT("Data inheritance disabled"),DA);TestNull(TEXT("Point inheritance disabled"),Attr);}
   else
   {
    if(!TestNotNull(TEXT("Data retained"),DA)||!TestNotNull(TEXT("Point metadata retained"),Attr))return false;
    TestEqual(TEXT("Data value retained"),DA->GetValueFromItemKey(PCGFirstEntryKey),913);
   }
   const auto OutKeys=Output->GetConstMetadataEntryValueRange();
   const auto OutTransforms=Output->GetConstTransformValueRange();
   TArray<int32> Indices;
   for(int I=0;I<Output->GetNumPoints();++I)
   {
    const int Index=P.IndexOfByPredicate([&](const FVector& Position){return Position.Equals(OutTransforms[I].GetLocation());});
    if(!TestTrue(TEXT("Original vertex"),Index!=INDEX_NONE))return false;
    Indices.Add(Index);
    if(Attr)TestEqual(TEXT("Exact source metadata"),Attr->GetValueFromItemKey(OutKeys[I]),Index);
    TestTrue(TEXT("Transform retained"),OutTransforms[I].Equals(Transforms[Index]));
    TestEqual(TEXT("Density retained"),Output->GetConstDensityValueRange()[I],Density[Index]);
    TestEqual(TEXT("Seed retained"),Output->GetConstSeedValueRange()[I],Seeds[Index]);
    TestTrue(TEXT("Color retained"),Output->GetConstColorValueRange()[I].Equals(Color[Index]));
    TestTrue(TEXT("Bounds min retained"),Output->GetConstBoundsMinValueRange()[I].Equals(Min[Index]));
    TestTrue(TEXT("Bounds max retained"),Output->GetConstBoundsMaxValueRange()[I].Equals(Max[Index]));
    TestEqual(TEXT("Steepness retained"),Output->GetConstSteepnessValueRange()[I],Steepness[Index]);
   }
   if(Cut)for(int I=0;I<Indices.Num();++I)
   {
    const int A=Indices[I],B=Indices[(I+1)%Indices.Num()];
    const bool Expected=(A+1)%P.Num()!=B && (B+1)%P.Num()!=A;
    TestEqual(TEXT("Cut identifies outgoing edge"),Cut->GetValueFromItemKey(OutKeys[I]),Expected);
    CutCount+=Expected; bClosingCut|=Expected && I==Indices.Num()-1;
    VertexRoles.FindOrAdd(A)|=Expected?1:2;
   }
  }
  if(Mode!=0)
  {
   TestEqual(TEXT("11 cuts, both sides, two inputs"),CutCount,44);
   TestTrue(TEXT("Closing cut edge tested"),bClosingCut);
   bool bDifferentRoles=false;for(const auto& Pair:VertexRoles)bDifferentRoles|=Pair.Value==3;
   TestTrue(TEXT("Shared vertex can have different flags across patches"),bDifferentRoles);
  }
 }
 TestNull(TEXT("Source was not mutated"),Elements->GetConstAttribute(TEXT("IsCut")));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClosedPathContractTest,"PCGUtils.Paths.Decomposition.ClosureAndSettings",ClosedPathTests::Flags)
bool FClosedPathContractTest::RunTest(const FString&)
{
 auto* Settings=NewObject<UPCGDecomposeClosedPathSettings>();
 Settings->ClosedAttributeName=TEXT("CustomClosed");
 auto* Source=NewObject<UPCGPointArrayData>();Source->SetNumPoints(4);
 auto Positions=Source->GetTransformValueRange();
 Positions[0]=FTransform(FVector(0,0,0));Positions[1]=FTransform(FVector(10,0,0));
 Positions[2]=FTransform(FVector(10,10,0));Positions[3]=FTransform(FVector(0,10,0));
 auto* Data=Source->Metadata->GetMetadataDomain(PCGMetadataDomainID::Data);
 auto Run=[&](int Expected, bool bExpectClosed=true)
 {
  FPCGContext Context;Context.InputData.TaggedData.Emplace_GetRef().Data=Settings;
  auto& Input=Context.InputData.TaggedData.Emplace_GetRef();Input.Data=Source;Input.Pin=PCGPinConstants::DefaultInputLabel;
  ClosedPathTests::FExecute Execute;Execute.ExecuteInternal(&Context);
  TestEqual(TEXT("Expected output count"),Context.OutputData.TaggedData.Num(),Expected);
  for(const auto& Output:Context.OutputData.TaggedData)
  {
   if(!bExpectClosed)continue;
   const auto* Points=CastChecked<UPCGPointArrayData>(Output.Data);
   const auto* Closed=Points->Metadata->GetConstMetadataDomain(PCGMetadataDomainID::Data)->GetConstTypedAttribute<bool>(TEXT("CustomClosed"));
   if(TestNotNull(TEXT("Custom closure output"),Closed))TestTrue(TEXT("Custom closure true"),Closed->GetValueFromItemKey(Points->Metadata->GetConstMetadataDomain(PCGMetadataDomainID::Data)->GetItemCountForChild() > 0 ? PCGFirstEntryKey : PCGInvalidEntryKey));
  }
 };
 AddExpectedError(TEXT("@Data.CustomClosed is missing"),EAutomationExpectedErrorFlags::Contains,1);Run(1,false);
 Data->CreateAttribute<int32>(TEXT("CustomClosed"),1,false);
 AddExpectedError(TEXT("@Data.CustomClosed has type ID"),EAutomationExpectedErrorFlags::Contains,1);Run(1,false);
 Data->DeleteAttribute(TEXT("CustomClosed"));Data->CreateAttribute<bool>(TEXT("CustomClosed"),false,false);
 AddExpectedError(TEXT("@Data.CustomClosed=false"),EAutomationExpectedErrorFlags::Contains,1);Run(1,false);
 Data->DeleteAttribute(TEXT("CustomClosed"));Data->CreateAttribute<bool>(TEXT("CustomClosed"),true,false);Run(1);
 Settings->MaxConcavity.Input=EPCGUtilsInputValueType::Attribute;
 Settings->MaxConcavity.Attribute.Update(TEXT("@Data.Concavity"));
 AddExpectedMessage(TEXT("was not found"),ELogVerbosity::Warning,EAutomationExpectedMessageFlags::Contains,1);Run(1);
 Data->CreateAttribute<double>(TEXT("Concavity"),0.1,false);Run(1);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClosedPathFlatBiasTest,"PCGUtils.Paths.Decomposition.FlatCutBias",ClosedPathTests::Flags)
bool FClosedPathFlatBiasTest::RunTest(const FString&)
{
 using namespace ClosedPathDecomposition;
 const auto P=ClosedPathTests::FlatBiasPoints();
 auto HasEdge=[](const TArray<FLoop>& Loops,int32 A,int32 B)
 {
  for(const auto& L:Loops)for(int32 I=0;I<L.Num();++I)
   if((L[I]==A && L[(I+1)%L.Num()]==B)||(L[I]==B && L[(I+1)%L.Num()]==A))return true;
  return false;
 };
 FOptions Options;TArray<FLoop> Original,Biased;FString Error;
 TestFalse(TEXT("Bias defaults off"),Options.bBiasFlatCuts);
 if(!TestTrue(TEXT("Original scene solves"),Solve(P,Options,Original,Error))){AddError(Error);return false;}
 TestEqual(TEXT("Original scene patch count retained"),Original.Num(),23);
 TestFalse(TEXT("Original has no requested level cut"),HasEdge(Original,37,45));
 Options.bBiasFlatCuts=true;
 if(!TestTrue(TEXT("Biased scene solves"),Solve(P,Options,Biased,Error))){AddError(Error);return false;}
 TestTrue(TEXT("Bias creates green-to-green seam"),HasEdge(Biased,37,45));
 TestFalse(TEXT("Bias removes green-to-red diagonal"),HasEdge(Biased,34,44));
 // Preserve winding/cyclic start, translation, uniform scale and rotations around world up.
 const auto Expected=ClosedPathTests::LoopSet(Biased);
 for(int Reverse=0;Reverse<2;++Reverse)for(int Shift:{0,19,37})
 {
  TArray<FVector> Input;TArray<int32> Map;
  for(int I=0;I<P.Num();++I)
  {
   const int Index=(Shift+(Reverse?P.Num()-1-I:I))%P.Num();Map.Add(Index);
   Input.Add(FQuat(FVector::UpVector,.83).RotateVector(P[Index])*2.5+FVector(1000,-500,300));
  }
  TArray<FLoop> Out;
  if(!TestTrue(TEXT("Transformed bias solves"),Solve(Input,Options,Out,Error))){AddError(Error);return false;}
  for(auto& L:Out)for(int& I:L)I=Map[I];
  const auto Actual=ClosedPathTests::LoopSet(Out);
  TestEqual(TEXT("No missing biased patches"),Expected.Difference(Actual).Num(),0);
  TestEqual(TEXT("No extra biased patches"),Actual.Difference(Expected).Num(),0);
 }
 // Exercise the reflected flag through the real node, including cut metadata on the new seam.
 auto* Settings=NewObject<UPCGDecomposeClosedPathSettings>();
 TestFalse(TEXT("Node flag defaults off"),Settings->bBiasFlatCuts);
 Settings->bBiasFlatCuts=true;Settings->bOutputIsCut=true;
 auto* Source=NewObject<UPCGPointArrayData>();Source->SetNumPoints(P.Num());
 auto Transforms=Source->GetTransformValueRange();
 for(int I=0;I<P.Num();++I)Transforms[I]=FTransform(P[I]);
 Source->Metadata->GetMetadataDomain(PCGMetadataDomainID::Data)->CreateAttribute<bool>(TEXT("IsClosed"),true,false);
 FPCGContext Context;Context.InputData.TaggedData.Emplace_GetRef().Data=Settings;
 auto& Input=Context.InputData.TaggedData.Emplace_GetRef();Input.Data=Source;Input.Pin=PCGPinConstants::DefaultInputLabel;
 ClosedPathTests::FExecute Execute;Execute.ExecuteInternal(&Context);
 TestEqual(TEXT("Node uses biased result"),Context.OutputData.TaggedData.Num(),Biased.Num());
 int32 SeamSides=0;
 for(const auto& Tagged:Context.OutputData.TaggedData)
 {
  const auto* Output=CastChecked<UPCGPointArrayData>(Tagged.Data);
  const auto T=Output->GetConstTransformValueRange();const auto Keys=Output->GetConstMetadataEntryValueRange();
  const auto* Cut=Output->Metadata->GetConstMetadataDomain(PCGMetadataDomainID::Elements)->GetConstTypedAttribute<bool>(TEXT("IsCut"));
  if(!TestNotNull(TEXT("Cut attribute on biased output"),Cut))return false;
  for(int I=0;I<Output->GetNumPoints();++I)
  {
   const FVector A=T[I].GetLocation(),B=T[(I+1)%Output->GetNumPoints()].GetLocation();
   if((A.Equals(P[37])&&B.Equals(P[45]))||(A.Equals(P[45])&&B.Equals(P[37])))
   {++SeamSides;TestTrue(TEXT("New level seam is flagged"),Cut->GetValueFromItemKey(Keys[I]));}
  }
 }
 TestEqual(TEXT("Both sides of new seam emitted"),SeamSides,2);
 // Completely level input has the same cost ordering with the preference enabled.
 const TArray<FVector> Square={{0,0,0},{10,0,0},{10,10,0},{0,10,0}};
 TestTrue(TEXT("Flat square solves"),Solve(Square,Options,Biased,Error));
 TestEqual(TEXT("No artificial cuts in a flat square"),Biased.Num(),1);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClosedPathConcaveRetryTest,"PCGUtils.Paths.Decomposition.ConcaveBoundaryRecovery",ClosedPathTests::Flags)
bool FClosedPathConcaveRetryTest::RunTest(const FString&)
{
 using namespace ClosedPathDecomposition;
 // Simple, planar, non-crossing polygon rejected by the original unconstrained seed search.
 const TArray<FVector> P={{.5930311912827795,.3057937915799492,0},{2.1689418289762816,1.7214347601430933,0},
 {.8396576305016955,1.0614924988287642,0},{-1.9095836161850472,-1.820408794384081,0},
 {.1686554145531528,-4.901269662756757,0},{.5172791447061381,-2.0810172067936956,0},
 {.9942253254347475,-3.7733752817278696,0},{3.7719234785391227,-2.881653235927635,0},
 {.6370120382440554,-.037821611536838745,0}};
 auto Area=[&](const FLoop& L){double A=0;for(int I=0;I<L.Num();++I){const auto& U=P[L[I]];const auto& V=P[L[(I+1)%L.Num()]];A+=U.X*V.Y-U.Y*V.X;}return FMath::Abs(A)*.5;};
 FLoop Boundary;for(int I=0;I<P.Num();++I)Boundary.Add(I);
 for(int Bias=0;Bias<2;++Bias)for(int Reverse=0;Reverse<2;++Reverse)for(int Shift:{0,3,7})
 {
  FOptions O;O.bBiasFlatCuts=Bias!=0;TArray<FLoop> Out;FString Error;
  TArray<FVector> Input;TArray<int32> Map;
  for(int I=0;I<P.Num();++I){const int Index=(Shift+(Reverse?P.Num()-1-I:I))%P.Num();Map.Add(Index);Input.Add(FQuat(FVector(1,2,3).GetSafeNormal(),.7).RotateVector(P[Index])*3+FVector(100,-20,70));}
  if(!TestTrue(TEXT("Ordinary concave boundary decomposes"),Solve(Input,O,Out,Error))){AddError(Error);return false;}
  TestTrue(TEXT("Recovery creates actual patches, not pass-through"),Out.Num()>1);
  double Total=0;for(auto L:Out){for(int& I:L)I=Map[I];Total+=Area(L);}
  TestTrue(TEXT("Patch area covers boundary without exterior overlap"),FMath::IsNearlyEqual(Total,Area(Boundary),1e-7));
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClosedPathPreservationTest,"PCGUtils.Paths.Decomposition.NeverDropsPointInputs",ClosedPathTests::Flags)
bool FClosedPathPreservationTest::RunTest(const FString&)
{
 const TArray<FVector> Square={{0,0,0},{10,0,0},{10,10,0},{0,10,0}};
 for(int Case=0;Case<10;++Case)
 {
  auto* Settings=NewObject<UPCGDecomposeClosedPathSettings>();Settings->bOutputIsCut=true;
  Settings->IsCutAttributeName=TEXT("TestCut");Settings->bInheritMetadata=false;
  TArray<FVector> P=Square;
  switch(Case)
  {
   case 0:P[2]=P[0];break; // Non-adjacent coincident vertices.
   case 1:P.Add(FVector(P[0]));break; // Explicit repeated closing vertex.
   case 2:P={{0,0,0},{1,0,0},{2,0,0}};break; // Collinear.
   case 3:P={{0,0,0},{10,10,0},{0,10,0},{10,0,0}};break; // Crossing boundary.
   case 4:Settings->MaxInputPoints.Constant=3;break; // Visible search guard.
   case 5:Settings->MaxConcavity.Constant=1;break;
   case 6:Settings->AreaWeight.Constant=0;Settings->LengthWeight.Constant=0;break;
   case 7:Settings->MaxInputPoints.Constant=2;break;
   case 8:P.Reset();break;
   case 9:P={FVector::ZeroVector,FVector::ZeroVector,FVector::ZeroVector};break;
  }
  FPCGContext Context;Context.InputData.TaggedData.Emplace_GetRef().Data=Settings;
  TArray<UPCGPointArrayData*> Sources;
  for(int Dataset=0;Dataset<2;++Dataset)
  {
   const auto& Positions=Dataset==0?P:Square;
   auto* Source=NewObject<UPCGPointArrayData>();Sources.Add(Source);Source->SetNumPoints(Positions.Num());
   Source->TargetActor=GetMutableDefault<AActor>();
   auto* Elements=Source->Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements);
   auto* ID=Elements->CreateAttribute<int32>(TEXT("TestID"),-1,false);
   Elements->CreateAttribute<bool>(TEXT("TestCut"),true,false);
   auto* Data=Source->Metadata->GetMetadataDomain(PCGMetadataDomainID::Data);
   Data->CreateAttribute<bool>(TEXT("IsClosed"),true,false);Data->CreateAttribute<int32>(TEXT("Sentinel"),934,false);
   auto T=Source->GetTransformValueRange();auto Keys=Source->GetMetadataEntryValueRange();auto Density=Source->GetDensityValueRange();
   for(int I=0;I<Positions.Num();++I){T[I]=FTransform(FRotator(0,I,0),Positions[I],FVector(1,2,3));Keys[I]=Elements->AddEntry();ID->SetValue(Keys[I],I);Density[I]=.2f+.01f*I;}
   auto& Tagged=Context.InputData.TaggedData.Emplace_GetRef();Tagged.Data=Source;Tagged.Pin=PCGPinConstants::DefaultInputLabel;Tagged.Tags.Add(Dataset==0?TEXT("ProblemPath"):TEXT("OtherPath"));
  }
  const bool bBothForwarded=Case>=4&&Case<=7;
  AddExpectedMessage(TEXT("forwarded without decomposition"),ELogVerbosity::Warning,EAutomationExpectedMessageFlags::Contains,bBothForwarded?2:1);
  ClosedPathTests::FExecute Execute;Execute.ExecuteInternal(&Context);
  if(!TestEqual(TEXT("Every input survives"),Context.OutputData.TaggedData.Num(),2))return false;
  for(int Dataset=0;Dataset<(bBothForwarded?2:1);++Dataset)
  {
   const auto& Tagged=Context.OutputData.TaggedData[Dataset];const auto* Output=CastChecked<UPCGPointArrayData>(Tagged.Data);const auto* Source=Sources[Dataset];
   TestEqual(TEXT("Point count preserved"),Output->GetNumPoints(),Source->GetNumPoints());
   TestTrue(TEXT("Tags preserved"),Tagged.Tags.Contains(Dataset==0?TEXT("ProblemPath"):TEXT("OtherPath")));
   TestTrue(TEXT("Target actor preserved"),Output->TargetActor==Source->TargetActor);
   const auto* Data=Output->Metadata->GetConstMetadataDomain(PCGMetadataDomainID::Data);
   const auto* Sentinel=Data->GetConstTypedAttribute<int32>(TEXT("Sentinel"));
   if(!TestNotNull(TEXT("Fallback preserves Data metadata even with inheritance disabled"),Sentinel))return false;
   TestEqual(TEXT("Data value preserved"),Sentinel->GetValueFromItemKey(PCGInvalidEntryKey),934);
   const auto* Elements=Output->Metadata->GetConstMetadataDomain(PCGMetadataDomainID::Elements);
   const auto* ID=Elements->GetConstTypedAttribute<int32>(TEXT("TestID"));const auto* Cut=Elements->GetConstTypedAttribute<bool>(TEXT("TestCut"));
   if(!TestNotNull(TEXT("Point metadata preserved"),ID)||!TestNotNull(TEXT("Cut contract exists"),Cut))return false;
   const auto Keys=Output->GetConstMetadataEntryValueRange();
   for(int I=0;I<Output->GetNumPoints();++I)
   {
    TestTrue(TEXT("Transform preserved"),Output->GetConstTransformValueRange()[I].Equals(Source->GetConstTransformValueRange()[I]));
    TestEqual(TEXT("Density preserved"),Output->GetConstDensityValueRange()[I],Source->GetConstDensityValueRange()[I]);
    TestEqual(TEXT("Metadata correspondence preserved"),ID->GetValueFromItemKey(Keys[I]),I);
    TestFalse(TEXT("Pass-through invents no cuts and clears stale flags"),Cut->GetValueFromItemKey(Keys[I]));
   }
   TestTrue(TEXT("Input cut attribute unchanged"),Source->Metadata->GetConstMetadataDomain(PCGMetadataDomainID::Elements)->GetConstTypedAttribute<bool>(TEXT("TestCut"))->GetValueFromItemKey(PCGInvalidEntryKey));
  }
 }
 return true;
}
#endif
