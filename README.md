# OnlineSubsystemTheia
An Online Subsystem for Unreal Engine that allows for Online Sessions to be created.
Built from Online Subsystem Null and takes advantage of VOIP features made available in Subsystem Null.

Must add the following to a projects Config/DefaultEngine.ini file and if building for IOS Engine/Config/Android/IOSEngine.ini 
and for Android Engine/Config/AndroidEngine.ini:

[OnlineSubsystem]
DefaultPlatformService=Theia
bHasVoiceEnabled=true


Must add the following to a projects Config/Game.ini file:

[/Script/Engine.GameSession]
bRequiresPushToTalk=false/true

OnlineSubsystemTheia Should be placed in the Engine/Plugins/Online folder