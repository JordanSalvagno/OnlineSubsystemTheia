using UnrealBuildTool;
using System.IO;
 
public class AdvancedSessions : ModuleRules
{
    public AdvancedSessions(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        Definitions.Add("WITH_ADVANCED_SESSIONS=1");

        PrivateIncludePaths.AddRange(new string[] { "AdvancedSessions/Private" });
        PublicIncludePaths.AddRange(new string[] { "AdvancedSessions/Public" });
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "OnlineSubsystem", "CoreUObject", "OnlineSubsystemUtils", "Networking", "Sockets" });
        PrivateDependencyModuleNames.AddRange(new string[] { "OnlineSubsystem", "Sockets", "Networking", "OnlineSubsystemUtils" });
    }
}