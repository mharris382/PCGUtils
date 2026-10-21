#include "Geometry/ClosedPathDecomposition.h"
#include "Algo/Reverse.h"

DEFINE_LOG_CATEGORY_STATIC(LogClosedPathDecomposition, Log, All);

namespace ClosedPathDecomposition
{
static double Cross2(const FVector2D& A, const FVector2D& B) { return A.X*B.Y-A.Y*B.X; }
static double Orient(const FVector2D& A, const FVector2D& B, const FVector2D& C) { return Cross2(B-A,C-A); }
static double Area(const TArray<FVector2D>& Q)
{
	double Sum=0;
	for(int32 I=0;I<Q.Num();++I) Sum+=Cross2(Q[I],Q[(I+1)%Q.Num()]);
	return FMath::Abs(Sum)*.5;
}
static bool Crosses(const FVector2D& A,const FVector2D& B,const FVector2D& C,const FVector2D& D,double E=1e-9)
{
	const double X=Orient(A,B,C),Y=Orient(A,B,D),Z=Orient(C,D,A),W=Orient(C,D,B);
	return ((X>E&&Y<-E)||(X<-E&&Y>E))&&((Z>E&&W<-E)||(Z<-E&&W>E));
}
static bool Simple(const TArray<FVector2D>& P)
{
	for(int32 I=0;I<P.Num();++I) for(int32 J=I+1;J<P.Num();++J)
	{
		if(J==I+1||(I==0&&J==P.Num()-1)) continue;
		const FVector2D A=P[I],B=P[(I+1)%P.Num()],C=P[J],D=P[(J+1)%P.Num()];
		if(Crosses(A,B,C,D)) return false;
		auto On=[](const FVector2D& U,const FVector2D& V,const FVector2D& W)
		{ return FMath::Abs(Orient(U,V,W))<=1e-9&&FVector2D::DotProduct(W-U,W-V)<=1e-9; };
		if(On(A,B,C)||On(A,B,D)||On(C,D,A)||On(C,D,B)) return false;
	}
	return true;
}

// Jacobi eigensolver for the symmetric covariance matrix. No global projection
// is used: only a proposed patch is projected to its least-squares plane.
static FVector PlaneNormal(const TArray<FVector>& Q,const FVector& Center)
{
	double A[3][3]={}, V[3][3]={{1,0,0},{0,1,0},{0,0,1}};
	for(const FVector& P:Q)
	{
		const FVector D=P-Center;
		for(int I=0;I<3;++I) for(int J=0;J<3;++J) A[I][J]+=D[I]*D[J];
	}
	for(int Iter=0;Iter<50;++Iter)
	{
		int P=0,R=1;
		for(int I=0;I<3;++I) for(int J=I+1;J<3;++J)
			if(FMath::Abs(A[I][J])>FMath::Abs(A[P][R])) {P=I;R=J;}
		if(FMath::Abs(A[P][R])<1e-15) break;
		const double Angle=.5*FMath::Atan2(2*A[P][R],A[R][R]-A[P][P]);
		const double C=FMath::Cos(Angle),S=FMath::Sin(Angle);
		const double AP=A[P][P],AR=A[R][R],APR=A[P][R];
		A[P][P]=C*C*AP-2*S*C*APR+S*S*AR;
		A[R][R]=S*S*AP+2*S*C*APR+C*C*AR;
		A[P][R]=A[R][P]=0;
		for(int K=0;K<3;++K)
		{
			if(K!=P&&K!=R)
			{
				const double KP=A[K][P],KR=A[K][R];
				A[K][P]=A[P][K]=C*KP-S*KR;
				A[K][R]=A[R][K]=S*KP+C*KR;
			}
			const double VP=V[K][P],VR=V[K][R];
			V[K][P]=C*VP-S*VR; V[K][R]=S*VP+C*VR;
		}
	}
	int K=0; for(int I=1;I<3;++I) if(A[I][I]<A[K][K]) K=I;
	return FVector(V[0][K],V[1][K],V[2][K]).GetSafeNormal();
}
static bool Shape(const TArray<FVector>& Points,const FLoop& Loop,double& Concavity,double& Warp)
{
	TArray<FVector> Q; FVector Center=FVector::ZeroVector;
	for(int32 I:Loop) {Q.Add(Points[I]);Center+=Points[I];}
	Center/=Q.Num();
	const FVector N=PlaneNormal(Q,Center);
	const FVector Axis=FMath::Abs(N.X)<.8?FVector::XAxisVector:FVector::YAxisVector;
	const FVector U=FVector::CrossProduct(N,Axis).GetSafeNormal(), V=FVector::CrossProduct(N,U);
	TArray<FVector2D> UV;
	double Deviation=0,Perimeter=0;
	for(int I=0;I<Q.Num();++I)
	{
		const FVector D=Q[I]-Center;
		UV.Emplace(FVector::DotProduct(D,U),FVector::DotProduct(D,V));
		Deviation=FMath::Max(Deviation,FMath::Abs(FVector::DotProduct(D,N)));
		Perimeter+=FVector::Distance(Q[I],Q[(I+1)%Q.Num()]);
	}
	if(!Simple(UV)) return false;
	const double PolygonArea=Area(UV);
	TArray<FVector2D> Sorted=UV,Hull;
	Sorted.Sort([](const FVector2D& A,const FVector2D& B){return A.X==B.X?A.Y<B.Y:A.X<B.X;});
	for(int Side=0;Side<2;++Side)
	{
		TArray<FVector2D> Half;
		for(const FVector2D& P:Sorted)
		{
			while(Half.Num()>1&&Orient(Half[Half.Num()-2],Half.Last(),P)<=0) Half.Pop(EAllowShrinking::No);
			Half.Add(P);
		}
		Half.Pop(EAllowShrinking::No); Hull.Append(Half); Algo::Reverse(Sorted);
	}
	const double HullArea=Area(Hull), Width=2*PolygonArea/Perimeter;
	if(PolygonArea<1e-10||HullArea<1e-10||Width<1e-10) return false;
	Concavity=FMath::Max(0.,1-PolygonArea/HullArea); Warp=Deviation/Width;
	return true;
}
// Normalized by median boundary edge length, so the classification is scale independent.
// This is a numerical equal-height tolerance, not permission to flatten or move points.
static constexpr double LevelHeightTolerance = 1e-6;
static bool IsLevel(const TArray<FVector>& Points, const FLoop& Loop)
{
	double MinZ = TNumericLimits<double>::Max(), MaxZ = TNumericLimits<double>::Lowest();
	for (int32 I : Loop) { MinZ = FMath::Min(MinZ, Points[I].Z); MaxZ = FMath::Max(MaxZ, Points[I].Z); }
	return MaxZ - MinZ <= LevelHeightTolerance;
}
static bool Join(const FLoop& A,const FLoop& B,FLoop& Out,FIntPoint& Shared)
{
	TArray<FIntPoint> Edges,Exterior;
	for(const FLoop* L:{&A,&B}) for(int I=0;I<L->Num();++I) Edges.Emplace((*L)[I],(*L)[(I+1)%L->Num()]);
	for(FIntPoint E:Edges) {if(!Edges.Contains(FIntPoint(E.Y,E.X))) Exterior.Add(E);else Shared=E;}
	if(Exterior.Num()!=Edges.Num()-2) return false;
	TMap<int32,int32> Next; int32 First=MAX_int32;
	for(FIntPoint E:Exterior) {if(Next.Contains(E.X)) return false;Next.Add(E.X,E.Y);First=FMath::Min(First,E.X);}
	Out={First};
	while(Next.Contains(Out.Last())&&Next[Out.Last()]!=First)
	{
		Out.Add(Next[Out.Last()]);
		if(Out.Num()>Exterior.Num()) return false;
	}
	return Out.Num()==Exterior.Num()&&Next.Contains(Out.Last());
}
static bool InsideTriangle(const FVector2D& P,const TArray<FVector2D>& T,bool Strict)
{
	const double A=Orient(T[0],T[1],P),B=Orient(T[1],T[2],P),C=Orient(T[2],T[0],P);
	const double Min=FMath::Min3(A,B,C),Max=FMath::Max3(A,B,C);
	return Strict?(Min>1e-8||Max<-1e-8):(Min>=-1e-8||Max<=1e-8);
}
static bool Intersects(const TArray<FVector>& P,const FLoop& AI,const FLoop& BI)
{
	TArray<FVector> A,B; TArray<int32> Shared;
	FBox BoxA(ForceInit),BoxB(ForceInit);
	for(int I:AI) {A.Add(P[I]);BoxA+=P[I];if(BI.Contains(I)) Shared.Add(I);}
	for(int I:BI) {B.Add(P[I]);BoxB+=P[I];}
	if(!BoxA.ExpandBy(1e-8).Intersect(BoxB)) return false;
	auto Permitted=[&](const FVector& Hit)
	{
		for(int I:Shared) if(FVector::Distance(Hit,P[I])<1e-8) return true;
		if(Shared.Num()==2)
		{
			const FVector Start=P[Shared[0]],D=P[Shared[1]]-Start;
			const double T=FVector::DotProduct(Hit-Start,D)/D.SizeSquared();
			return T>=-1e-8&&T<=1+1e-8&&FVector::Distance(Hit,Start+T*D)<1e-8;
		}
		return false;
	};
	const FVector N=FVector::CrossProduct(A[1]-A[0],A[2]-A[0]).GetSafeNormal();
	bool Coplanar=true; for(const FVector& Q:B) Coplanar&=FMath::Abs(FVector::DotProduct(Q-A[0],N))<1e-8;
	if(Coplanar)
	{
		int Drop=0;for(int I=1;I<3;++I) if(FMath::Abs(N[I])>FMath::Abs(N[Drop])) Drop=I;
		auto Project=[&](FVector Q){return FVector2D(Q[(Drop+1)%3],Q[(Drop+2)%3]);};
		TArray<FVector2D> AA,BB;for(FVector Q:A) AA.Add(Project(Q));for(FVector Q:B) BB.Add(Project(Q));
		for(int I=0;I<3;++I)
		{
			if(InsideTriangle(AA[I],BB,true)||InsideTriangle(BB[I],AA,true)) return true;
			for(int J=0;J<3;++J) if(Crosses(AA[I],AA[(I+1)%3],BB[J],BB[(J+1)%3],1e-8)) return true;
		}
		for(int Side=0;Side<2;++Side)
		{
			const auto& Poly=Side?B:A; const auto& Other=Side?AA:BB;
			for(int I=0;I<3;++I) for(FVector Q:{Poly[I],(Poly[I]+Poly[(I+1)%3])*.5})
				if(InsideTriangle(Project(Q),Other,false)&&!Permitted(Q)) return true;
		}
	}
	else for(int Side=0;Side<2;++Side)
	{
		const auto& Segs=Side?B:A; const auto& Tri=Side?A:B;
		const FVector U=Tri[1]-Tri[0],V=Tri[2]-Tri[0];
		for(int I=0;I<3;++I)
		{
			const FVector Start=Segs[I],D=Segs[(I+1)%3]-Start,H=FVector::CrossProduct(D,V);
			const double Det=FVector::DotProduct(U,H);
			if(FMath::Abs(Det)<1e-8) continue;
			const FVector S=Start-Tri[0],Q=FVector::CrossProduct(S,U);
			const double Beta=FVector::DotProduct(S,H)/Det,Gamma=FVector::DotProduct(D,Q)/Det,T=FVector::DotProduct(V,Q)/Det;
			if(Beta>=-1e-8&&Gamma>=-1e-8&&Beta+Gamma<=1+1e-8&&T>=-1e-8&&T<=1+1e-8&&!Permitted(Start+T*D)) return true;
		}
	}
	return false;
}
bool Solve(const TArray<FVector>& Points,const FOptions& O,TArray<FLoop>& Out,FString& Error)
{
	Out.Reset();Error.Reset();
	auto Fail=[&](FString E){Error=MoveTemp(E);Out.Reset();return false;};
	const TPair<const TCHAR*,double> Scalars[]={{TEXT("Area Weight"),O.AreaWeight},{TEXT("Length Weight"),O.LengthWeight},
		{TEXT("Max Concavity"),O.Concavity},{TEXT("Max Quad Warp"),O.QuadWarp},{TEXT("Max Polygon Warp"),O.PolygonWarp}};
	for(auto S:Scalars) if(!FMath::IsFinite(S.Value)||S.Value<0) return Fail(FString::Printf(TEXT("%s=%g; expected finite and >= 0."),S.Key,S.Value));
	if(O.AreaWeight==0&&O.LengthWeight==0) return Fail(TEXT("Area Weight and Length Weight are both 0; at least one must be positive."));
	if(O.Concavity>=1) return Fail(FString::Printf(TEXT("Max Concavity=%g; expected < 1."),O.Concavity));
	if(O.MaxPoints<3||O.MaxPoints>2048) return Fail(FString::Printf(TEXT("Max Input Points=%d; expected 3..2048."),O.MaxPoints));
	const int N=Points.Num();
	if(N<3||N>O.MaxPoints) return Fail(FString::Printf(TEXT("Control point count=%d; expected 3..%d, without repeating the closing vertex."),N,O.MaxPoints));
	FVector Center=FVector::ZeroVector;TArray<double> Lengths;
	for(int I=0;I<N;++I)
	{
		for(int Axis=0;Axis<3;++Axis) if(!FMath::IsFinite(Points[I][Axis])) return Fail(FString::Printf(TEXT("Point %d coordinate %d=%g; expected finite."),I,Axis,Points[I][Axis]));
		Center+=Points[I];Lengths.Add(FVector::Distance(Points[I],Points[(I+1)%N]));
	}
	Center/=N;Lengths.Sort();const double Scale=(Lengths[(N-1)/2]+Lengths[N/2])*.5;
	if(!FMath::IsFinite(Scale)||Scale<=0) return Fail(FString::Printf(TEXT("Median boundary edge length=%g; expected finite and > 0."),Scale));
	TArray<FVector> P;for(FVector Q:Points) P.Add((Q-Center)/Scale);
	for(int I=0;I<N;++I) for(int J=I+1;J<N;++J) if(FVector::Distance(P[I],P[J])<1e-8)
		return Fail(FString::Printf(TEXT("Points %d and %d are coincident; separation=%g, required >= %g world units."),I,J,FVector::Distance(Points[I],Points[J]),Scale*1e-8));
	const double TimeStart=FPlatformTime::Seconds();
	TArray<double> DP;DP.Init(TNumericLimits<double>::Max(),N*N);
	TArray<int> Choice;Choice.Init(INDEX_NONE,N*N);
	for(int I=0;I<N-1;++I) DP[I*N+I+1]=0;
	for(int Span=2;Span<N;++Span) for(int I=0;I<N-Span;++I)
	{
		const int J=I+Span;
		for(int K=I+1;K<J;++K)
		{
			const double A=FVector::CrossProduct(P[K]-P[I],P[J]-P[I]).Size()*.5;
			if(A<1e-10) continue;
			if(DP[I*N+K]==TNumericLimits<double>::Max()||DP[K*N+J]==TNumericLimits<double>::Max()) continue;
			double TriangleCost=O.AreaWeight*A+O.LengthWeight*(FVector::Distance(P[I],P[K])+FVector::Distance(P[K],P[J])+FVector::Distance(P[J],P[I]));
			if (O.bBiasFlatCuts && FMath::Max3(P[I].Z,P[K].Z,P[J].Z)-FMath::Min3(P[I].Z,P[K].Z,P[J].Z)<=LevelHeightTolerance)
				TriangleCost *= 0.5;
			// Preserve the original floating-point evaluation order when the option is off.
			const double C=O.bBiasFlatCuts ? DP[I*N+K]+DP[K*N+J]+TriangleCost :
				DP[I*N+K]+DP[K*N+J]+O.AreaWeight*A+O.LengthWeight*(FVector::Distance(P[I],P[K])+FVector::Distance(P[K],P[J])+FVector::Distance(P[J],P[I]));
			if(C<DP[I*N+J]) {DP[I*N+J]=C;Choice[I*N+J]=K;}
		}
	}
	const double TimeDP=FPlatformTime::Seconds();
	if(Choice[N-1]==INDEX_NONE) return Fail(TEXT("No nondegenerate boundary-vertex triangulation found; normalized triangle area must be >= 1e-10."));
	TFunction<void(int,int)> Recover=[&](int I,int J){if(J-I<2)return;const int K=Choice[I*N+J];Out.Add({I,K,J});Recover(I,K);Recover(K,J);};
	Recover(0,N-1);
	const double TimeRecover=FPlatformTime::Seconds();
	for(int I=0;I<Out.Num();++I) for(int J=I+1;J<Out.Num();++J) if(Intersects(P,Out[I],Out[J]))
	{
		// A preference must not turn a valid original solution into a crossing surface.
		// Retry the unchanged algorithm if the preferred seed fails the intersection screen.
		if (O.bBiasFlatCuts)
		{
			FOptions Fallback = O; Fallback.bBiasFlatCuts = false;
			return Solve(Points, Fallback, Out, Error);
		}
		return Fail(FString::Printf(TEXT("Seed triangles %d (%d,%d,%d) and %d (%d,%d,%d) intersect outside shared topology; expected an embedded disk."),I,Out[I][0],Out[I][1],Out[I][2],J,Out[J][0],Out[J][1],Out[J][2]));
	}
	const double TimeScreen=FPlatformTime::Seconds();
	int32 MergeRounds=0;
	const int32 SeedTriangles=Out.Num();
	while(true)
	{
		++MergeRounds;
		int BestI=INDEX_NONE,BestJ=INDEX_NONE;double BestQ=0,BestLength=0;FLoop Best;
		for(int I=0;I<Out.Num();++I) for(int J=I+1;J<Out.Num();++J)
		{
			FLoop Loop;FIntPoint Edge;double C,W;
			if(!Join(Out[I],Out[J],Loop,Edge)||!Shape(P,Loop,C,W)) continue;
			// Do not erase the newly selected flat-to-slope seam during greedy merging.
			if (O.bBiasFlatCuts && (IsLevel(P,Out[I]) || IsLevel(P,Out[J])) && !IsLevel(P,Loop)) continue;
			if(C>O.Concavity||W>(Loop.Num()<=4?O.QuadWarp:O.PolygonWarp)) continue;
			const double Quality=FMath::RoundToDouble((C/FMath::Max(O.Concavity,1e-12)+W/FMath::Max(O.QuadWarp,1e-12))*1e10)/1e10;
			const double Length=FMath::RoundToDouble(FVector::Distance(P[Edge.X],P[Edge.Y])*1e10)/1e10;
			bool Lex=false;
			if(BestI!=INDEX_NONE&&Quality==BestQ&&Length==BestLength)
			{
				for(int K=0;K<FMath::Min(Loop.Num(),Best.Num());++K) if(Loop[K]!=Best[K]) {Lex=Loop[K]<Best[K];break;}
			}
			if(BestI==INDEX_NONE||Quality<BestQ||(Quality==BestQ&&(Length>BestLength||(Length==BestLength&&Lex))))
			{BestI=I;BestJ=J;BestQ=Quality;BestLength=Length;Best=MoveTemp(Loop);}
		}
		if(BestI==INDEX_NONE) break;
		Out[BestI]=MoveTemp(Best);Out.RemoveAt(BestJ);
	}
	const double TimeMerge=FPlatformTime::Seconds();
	TMap<FIntPoint,int32> Counts;
	for(int32 PatchIndex=0;PatchIndex<Out.Num();++PatchIndex)
	{
		const FLoop& L=Out[PatchIndex];
		double C,W;if(!Shape(P,L,C,W)) return Fail(FString::Printf(TEXT("Output patch %d failed the simple nondegenerate local-projection constraint."),PatchIndex));
		for(int I=0;I<L.Num();++I) ++Counts.FindOrAdd(FIntPoint(L[I],L[(I+1)%L.Num()]));
	}
	for(int I=0;I<N;++I) if(Counts.FindRef(FIntPoint(I,(I+1)%N))!=1||Counts.FindRef(FIntPoint((I+1)%N,I))!=0)
		return Fail(FString::Printf(TEXT("Boundary edge %d->%d is not represented exactly once forward."),I,(I+1)%N));
	for(auto E:Counts) if((E.Key.X+1)%N!=E.Key.Y)
		if(E.Value!=1||Counts.FindRef(FIntPoint(E.Key.Y,E.Key.X))!=1)
			return Fail(FString::Printf(TEXT("Interior edge %d->%d lacks exactly one reverse occurrence."),E.Key.X,E.Key.Y));
	const double TimeEnd=FPlatformTime::Seconds();
	UE_LOG(LogClosedPathDecomposition,Log,TEXT("Solve N=%d seedTris=%d mergeRounds=%d patches=%d | DP %.1f ms, recover %.1f ms, intersect screen %.1f ms, merge %.1f ms, validate %.1f ms, total %.1f ms"),
		N,SeedTriangles,MergeRounds,Out.Num(),(TimeDP-TimeStart)*1e3,(TimeRecover-TimeDP)*1e3,(TimeScreen-TimeRecover)*1e3,(TimeMerge-TimeScreen)*1e3,(TimeEnd-TimeMerge)*1e3,(TimeEnd-TimeStart)*1e3);
	return true;
}
}
