// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;

public class ArcTrace : ModuleRules
{
	public ArcTrace(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// 모듈 루트의 ArcTrace.h(로그 카테고리)를 하위 폴더에서 include 하기 위함
		PrivateIncludePaths.Add(ModuleDirectory);
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });

		// 지형(WGS84 지구) · 궤적/영역 메시 · 폭발 이펙트
		PublicDependencyModuleNames.AddRange(new string[] { "CesiumRuntime", "ProceduralMeshComponent", "Niagara" });

		// 기록 meta.json 파싱
		PrivateDependencyModuleNames.AddRange(new string[] { "Json" });

		// 재생 바 UI (위젯 트리를 C++에서 구성)
		PrivateDependencyModuleNames.AddRange(new string[] { "UMG", "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
