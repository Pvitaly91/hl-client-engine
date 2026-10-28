#requires -Version 7
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$task=Join-Path $repo 'manual-artifacts/task-records/studio-pose-frame-build-50b5ffe1-20261024'
$profileRoot=Join-Path $task 'profile';$baseline=Join-Path $task 'baseline/frozen'
$builder='C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/amd64/MSBuild.exe'
$map=@{
 'hlclient_studio_pose'=@{'src/goldsrc/studio/goldsrc_studio_pose.cpp'='profile_goldsrc_studio_pose.cpp'}
 'hlclient_entity_scene_render'=@{'src/entity_render/entity_scene_render.cpp'='profile_entity_scene_render.cpp'}
 'hlclient_application_runtime_replay'=@{'src/app/runtime_replay_local_assets.cpp'='profile_runtime_replay_local_assets.cpp'}
}
foreach($name in @('hlclient_studio_pose','hlclient_entity_scene_render','hlclient_application_runtime_replay')) {
 $dir=Join-Path $profileRoot $name;New-Item -ItemType Directory -Path $dir -Force|Out-Null
 [xml]$xml=Get-Content -LiteralPath (Join-Path $repo "build/$name.vcxproj") -Raw
 $ns=[Xml.XmlNamespaceManager]::new($xml.NameTable);$ns.AddNamespace('m','http://schemas.microsoft.com/developer/msbuild/2003')
 foreach($q in @('//m:ProjectReference','//m:CustomBuild','//m:Target','//m:PreBuildEvent','//m:PreLinkEvent','//m:PostBuildEvent')) {
  foreach($node in @($xml.SelectNodes($q,$ns))){$null=$node.ParentNode.RemoveChild($node)}
 }
 $xml.SelectSingleNode('//m:ProjectGuid',$ns).InnerText='{'+[guid]::NewGuid().ToString().ToUpperInvariant()+'}'
 foreach($n in @($xml.SelectNodes('//m:OutDir',$ns))){$n.InnerText=$profileRoot+'/libs/'}
 foreach($n in @($xml.SelectNodes('//m:IntDir',$ns))){$n.InnerText=$dir+'/obj/'}
 foreach($n in @($xml.SelectNodes('//m:ClCompile[@Include]',$ns))) {
  $relative=[IO.Path]::GetRelativePath($repo,$n.Include).Replace('\','/')
  $n.Include=if($map[$name].ContainsKey($relative)){Join-Path $profileRoot $map[$name][$relative]}else{Join-Path $baseline $relative}
 }
 foreach($n in @($xml.SelectNodes('//m:ClCompile[not(@Include)]',$ns))) {
  $n.AdditionalIncludeDirectories="$profileRoot;$baseline/include;%(AdditionalIncludeDirectories)"
  $n.DebugInformationFormat='ProgramDatabase'
 }
 $project=Join-Path $dir ($name+'.vcxproj');$xml.Save($project)
 & $builder $project /p:Configuration=Release /p:Platform=Win32 /p:VCToolsVersion=14.34.31933 /p:BuildProjectReferences=false /m:1 /nr:false /v:minimal *> (Join-Path $dir 'build.log')
 if($LASTEXITCODE -ne 0){throw "Profile library build failed: $name"}
}
& (Join-Path $task 'new-isolated-fixture-project.ps1') -Variant B0 -TargetName studio_profile -SourceFiles @("$profileRoot/profile_harness.cpp","$profileRoot/profile_allocation.cpp") -Defines @('HLCLIENT_HISTORY_COST_FIXTURE=1')|Out-Null
$fixture=Join-Path $task 'isolated-fixtures/B0/studio_profile'
$metadata=Get-Content -LiteralPath (Join-Path $fixture 'fixture-project.json') -Raw|ConvertFrom-Json
[xml]$xml=Get-Content -LiteralPath $metadata.generated_project -Raw
$ns=[Xml.XmlNamespaceManager]::new($xml.NameTable);$ns.AddNamespace('m','http://schemas.microsoft.com/developer/msbuild/2003')
$group=$xml.SelectSingleNode("//m:ItemDefinitionGroup[contains(@Condition,'Release|Win32')]",$ns)
$text=$group.Link.AdditionalDependencies
foreach($name in $map.Keys){$text=$text.Replace((Join-Path $task "baseline/Release-libs/$name.lib"),(Join-Path $profileRoot "libs/$name.lib"))}
$group.Link.AdditionalDependencies=$text+';dbghelp.lib'
$group.Link.GenerateDebugInformation='true'
$group.ClCompile.DebugInformationFormat='ProgramDatabase'
$xml.Save($metadata.generated_project)
& $builder $metadata.generated_project /p:Configuration=Release /p:Platform=Win32 /p:VCToolsVersion=14.34.31933 /p:BuildProjectReferences=false /m:1 /nr:false /v:minimal *> (Join-Path $fixture 'build.log')
if($LASTEXITCODE -ne 0){throw 'Profile driver build failed'}
Copy-Item -LiteralPath (Join-Path $task 'baseline/Release-binaries/SDL3.dll') -Destination (Join-Path $fixture 'bin/SDL3.dll')
Get-FileHash -LiteralPath $metadata.expected_executable
