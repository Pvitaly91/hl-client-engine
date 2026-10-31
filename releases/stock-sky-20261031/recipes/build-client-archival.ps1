#requires -Version 7.2
[CmdletBinding()]
param([ValidateSet('Fixture','Client')][string]$Kind='Fixture')

# Private compile only. No test/game launch, CMake regeneration or delivery.
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')
$taskSource=Join-Path $taskRoot 'source'
$Variant='StockSkyCorrection'
$repo=[IO.Path]::GetFullPath((Join-Path $taskRoot '../../..')).TrimEnd('\')
$b6=Join-Path $repo 'manual-artifacts/task-records/world-local-lights-b6-20261026'
$source=Join-Path $b6 'b6-isolated/source'
$libraries=Join-Path $b6 'b6-isolated/Release-libs'
$baseline=Join-Path $repo 'manual-artifacts/task-records/bsp-pvs-world-20261025/baseline'
$acceptedBuild=Join-Path $repo 'manual-artifacts/task-records/runtime-text-policy-b6-20261028/build-client-20261027T232140333Z-45bcc8'
$sdlRoot=Join-Path $repo 'build/_deps/sdl3-src'
$sdlInclude=Join-Path $sdlRoot 'include'
$gladInclude=Join-Path $repo 'third_party/glad/include'
$toolsetRoot='C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.34.31933'
$sdkRoot='C:\Program Files (x86)\Windows Kits\10'
$msbuild='C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe'
$expectedClient='24a55d8e041d0cce0a651d09d064c73611c8b7b9f648b2560e7347de616e5ace'
$expectedLauncher='fd0cb49ca76e5130a5b9eab291f8f1ab5226c744d4afb5fbab0879d8965007a1'
$expectedCandidateBase='36c5168f209f963e20f24cadc342c4369dfd569971e642a4a803743bcb7880eb'
$comparison=[StringComparison]::OrdinalIgnoreCase
function Require([bool]$Condition,[string]$Message){if(-not $Condition){throw $Message}}
function Under([string]$Path,[string]$Root){return [IO.Path]::GetFullPath($Path).StartsWith([IO.Path]::GetFullPath($Root).TrimEnd('\')+'\',$comparison)}
function No-Reparse([string]$Path){
    $cursor=[IO.Path]::GetFullPath($Path)
    Require ($cursor.Length -lt 260) "Legacy path limit exceeded: $cursor"
    while($cursor){
        if(Test-Path -LiteralPath $cursor){Require (-not ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) "Reparse path rejected: $cursor"}
        $parent=[IO.Path]::GetDirectoryName($cursor.TrimEnd('\'));if($parent -eq $cursor){break};$cursor=$parent
    }
}
function Identity([string]$Path){
    No-Reparse $Path;$f=Get-Item -LiteralPath $Path -Force
    Require (-not $f.PSIsContainer) "File required: $Path"
    return [ordered]@{path=$f.FullName;bytes=[long]$f.Length;sha256=(Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
}
function Assert-Identity($Id){$now=Identity $Id.path;Require ($now.sha256 -ceq $Id.sha256 -and $now.bytes -eq $Id.bytes) "Input changed: $($Id.path)"}
function Write-Json([string]$Path,$Value){Require (Under $Path $taskRoot) "Output escaped task: $Path";No-Reparse $Path;$Value|ConvertTo-Json -Depth 30|Set-Content -LiteralPath $Path -Encoding utf8NoBOM}
function Active-Identities{
    $rows=@(foreach($name in @('hlclient.exe','HLClientLauncher.exe','HLClientSystemDiagnostics.exe','SDL3.dll','msvcp140.dll','msvcp140_2.dll','msvcp140_atomic_wait.dll','vcruntime140.dll','HLCLIENT_DELIVERY_INFO.json')){Identity (Join-Path $repo "build/bin/Release/$name")})
    Require ($rows[0].sha256 -ceq $expectedClient -and $rows[1].sha256 -ceq $expectedLauncher) 'Accepted active client/launcher changed.'
    return $rows
}
function Child-Text($Xml,$Parent,[string]$Name,[string]$Text){
    $node=$Parent.SelectSingleNode("*[local-name()='$Name']")
    if(-not $node){$node=$Xml.CreateElement($Name,$Xml.DocumentElement.NamespaceURI);$null=$Parent.AppendChild($node)}
    $node.InnerText=$Text
}
function Prepare-Project([string]$Template,[string]$Directory,[string]$Target,[string[]]$Units,[string[]]$Includes,[bool]$StaticLibrary){
    [xml]$xml=Get-Content -LiteralPath $Template -Raw
    $uri=$xml.DocumentElement.NamespaceURI;$ns=[Xml.XmlNamespaceManager]::new($xml.NameTable);$ns.AddNamespace('m',$uri)
    foreach($n in @($xml.SelectNodes('//m:ProjectConfiguration',$ns))){if($n.Include -cne 'Release|Win32'){$null=$n.ParentNode.RemoveChild($n)}}
    foreach($n in @($xml.SelectNodes('//*[@Condition]',$ns))){if($n.Condition -match '(Debug|MinSizeRel|RelWithDebInfo)\|Win32'){$null=$n.ParentNode.RemoveChild($n)}}
    $removed=@()
    foreach($query in @('//m:ProjectReference','//m:CustomBuild','//m:Target','//m:PreBuildEvent','//m:PreLinkEvent','//m:PostBuildEvent','//m:CustomBuildStep','//m:Import[@Label="LocalAppDataPlatform"]')){
        foreach($n in @($xml.SelectNodes($query,$ns))){$removed+=$n.OuterXml;$null=$n.ParentNode.RemoveChild($n)}
    }
    $isolation=$xml.CreateElement('PropertyGroup',$uri)
    foreach($pair in @(@('ImportDirectoryBuildProps','false'),@('ImportDirectoryBuildTargets','false'),@('VCToolsVersion','14.34.31933'))){Child-Text $xml $isolation $pair[0] $pair[1]}
    $null=$xml.Project.PrependChild($isolation)
    $xml.SelectSingleNode('//m:ProjectGuid',$ns).InnerText='{'+[guid]::NewGuid().ToString().ToUpperInvariant()+'}'
    $nameNode=$xml.SelectSingleNode('//m:ProjectName',$ns);if($nameNode){$nameNode.InnerText=$Target}
    foreach($n in @($xml.SelectNodes('//m:OutDir',$ns))){$n.InnerText=(Join-Path $Directory 'bin')+'\'}
    foreach($n in @($xml.SelectNodes('//m:IntDir',$ns))){$n.InnerText=(Join-Path $Directory 'obj')+'\'}
    foreach($n in @($xml.SelectNodes('//m:TargetName',$ns))){$n.InnerText=$Target}
    foreach($n in @($xml.SelectNodes('//m:LocalDebuggerWorkingDirectory',$ns))){$n.InnerText=$Directory}
    foreach($n in @($xml.SelectNodes('//m:AdditionalIncludeDirectories',$ns))){$n.InnerText=$Includes -join ';'}
    $definitions=@($xml.SelectNodes('//m:ItemDefinitionGroup',$ns));Require ($definitions.Count -eq 1) 'Exactly one Release definition required.';$g=$definitions[0]
    # External paths are rewritten too; do not inherit dirty checkout includes.
    $options=([string]$g.ClCompile.AdditionalOptions -replace '%\(AdditionalOptions\)','') -replace '/external:I\s+("[^"]+"|\S+)',''
    Child-Text $xml $g.ClCompile 'AdditionalOptions' ($options.Trim()+' /showIncludes')
    Child-Text $xml $g.ClCompile 'PreprocessorDefinitions' (([string]$g.ClCompile.PreprocessorDefinitions)-replace '%\(PreprocessorDefinitions\);?','')
    if($StaticLibrary){
        Child-Text $xml $g.Lib 'AdditionalOptions' (([string]$g.Lib.AdditionalOptions)-replace '%\(AdditionalOptions\)','')
        Child-Text $xml $g.Lib 'OutputFile' (Join-Path $Directory "bin/$Target.lib")
    }else{
        Child-Text $xml $g.Link 'AdditionalLibraryDirectories' ''
        Child-Text $xml $g.Link 'AdditionalOptions' ((([string]$g.Link.AdditionalOptions)-replace '%\(AdditionalOptions\)','').Trim()+' /VERBOSE:LIB')
        Child-Text $xml $g.Link 'OutputFile' (Join-Path $Directory "bin/$Target.exe")
        Child-Text $xml $g.Link 'ImportLibrary' (Join-Path $Directory "$Target.lib")
        Child-Text $xml $g.Link 'ProgramDataBaseFile' (Join-Path $Directory "$Target.pdb")
        Child-Text $xml $g.Link 'GenerateMapFile' 'true'
        Child-Text $xml $g.Link 'MapFileName' (Join-Path $Directory "$Target.map")
        if($Kind -ceq 'Fixture'){Child-Text $xml $g.Link 'SubSystem' 'Console';Child-Text $xml $g.Link 'DelayLoadDLLs' ''}
    }
    foreach($n in @($xml.SelectNodes('/m:Project/m:ItemGroup/m:ClCompile|/m:Project/m:ItemGroup/m:ClInclude',$ns))){$null=$n.ParentNode.RemoveChild($n)}
    $group=$xml.CreateElement('ItemGroup',$uri)
    foreach($unit in $Units){No-Reparse $unit;$n=$xml.CreateElement('ClCompile',$uri);$n.SetAttribute('Include',$unit);$null=$group.AppendChild($n)}
    $null=$xml.Project.AppendChild($group)
    return [ordered]@{xml=$xml;group=$g;removed=$removed}
}
function Invoke-OwnedBuild([string]$Project,[string]$Directory){
    $arguments=@($Project,'/p:Configuration=Release','/p:Platform=Win32','/p:VCToolsVersion=14.34.31933','/p:BuildProjectReferences=false','/p:ImportDirectoryBuildProps=false','/p:ImportDirectoryBuildTargets=false','/m:1','/nr:false','/v:minimal',('/bl:'+(Join-Path $Directory 'build.binlog')))
    $record=[ordered]@{argv=@($msbuild)+$arguments;started_utc=[DateTime]::UtcNow.ToString('o');status='prepared';exit_code=$null}
    $out=Join-Path $Directory 'build.stdout.log';$err=Join-Path $Directory 'build.stderr.log';$p=$null;$os=$null;$es=$null
    try{
        $si=[Diagnostics.ProcessStartInfo]::new($msbuild);$si.WorkingDirectory=$Directory;$si.UseShellExecute=$false;$si.CreateNoWindow=$true;$si.WindowStyle='Hidden';$si.RedirectStandardOutput=$true;$si.RedirectStandardError=$true
        foreach($arg in $arguments){$si.ArgumentList.Add($arg)}
        foreach($name in @('CL','_CL_','LINK','_LINK_')){$null=$si.Environment.Remove($name)};$si.Environment['VSLANG']='1033'
        $os=[IO.File]::Open($out,'CreateNew','Write','Read');$es=[IO.File]::Open($err,'CreateNew','Write','Read');$p=[Diagnostics.Process]::new();$p.StartInfo=$si
        Require ($p.Start()) 'Could not start owned MSBuild.';$record.process_id=$p.Id;$record.status='building';Write-Json (Join-Path $Directory 'process.json') $record
        $ot=$p.StandardOutput.BaseStream.CopyToAsync($os);$et=$p.StandardError.BaseStream.CopyToAsync($es);$p.WaitForExit();[void]$ot.GetAwaiter().GetResult();[void]$et.GetAwaiter().GetResult();$record.exit_code=$p.ExitCode
        $os.Dispose();$os=$null;$es.Dispose();$es=$null;$record.stdout=Identity $out;$record.stderr=Identity $err
        Require ($record.exit_code -eq 0) "MSBuild failed; preserved logs at $Directory"
        $record.status='built'
    }catch{$record.status='failed_preserved';$record.error=$_.Exception.Message;throw}
    finally{if($os){$os.Dispose()};if($es){$es.Dispose()};if($p){$p.Dispose()};$record.completed_utc=[DateTime]::UtcNow.ToString('o');Write-Json (Join-Path $Directory 'process.json') $record}
    return $record
}
function Audit-Includes([string]$Log,[string]$Output){
    $paths=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($line in [IO.File]::ReadLines($Log)){if($line -match '^\s*Note: including file:\s*(.+?)\s*$'){$null=$paths.Add([IO.Path]::GetFullPath($Matches[1]))}}
    Require ($paths.Count -gt 0) 'Missing actual /showIncludes evidence.'
    $rows=@(foreach($path in @($paths|Sort-Object)){
        $id=Identity $path
        if(Under $path $inputRoot){$id.role='snapshotted_task_input';Require ($snapshotHashes.ContainsKey($path) -and $id.sha256 -ceq $snapshotHashes[$path]) "Unmanifested task include: $path"}
        elseif(Under $path $source){$id.role='frozen_B6_header';$relative=[IO.Path]::GetRelativePath($source,$path).Replace('\','/');Require ($sourceHashes.ContainsKey($relative) -and $id.sha256 -ceq $sourceHashes[$relative]) "Changed/unaccepted frozen include: $relative"}
        elseif(Under $path $sdlInclude){$id.role='pinned_SDL_interface'}
        elseif(Under $path $gladInclude){$id.role='pinned_GLAD_interface';Require ($gladHashes.ContainsKey($path) -and $id.sha256 -ceq $gladHashes[$path]) "Unpinned GLAD include: $path"}
        elseif((Under $path $toolsetRoot)-or(Under $path $sdkRoot)){$id.role='MSVC_Windows_SDK'}
        else{throw "Uncontrolled include: $path"};$id
    })
    Write-Json $Output $rows;return ,$rows
}

Require ($repo -ieq 'D:\DEV\CPP\HL-Client-engine' -and [IO.Path]::GetFileName($taskRoot) -ceq 'stock-sky-correction-001-20261031') 'Unexpected task location.'
$lineageIdentity=Identity (Join-Path $taskRoot 'source-lineage.json')
$lineage=Get-Content -LiteralPath $lineageIdentity.path -Raw|ConvertFrom-Json
Require ($lineage.schema -ceq 'hlc.stock-sky-correction.source-lineage.v1' -and $lineage.base_client_sha256 -ceq $expectedCandidateBase -and $lineage.active_writes -eq $false) 'Wrong isolated candidate lineage.'
$candidateBaseRecord=Identity $lineage.client_build
$candidateBase=Get-Content -LiteralPath $candidateBaseRecord.path -Raw|ConvertFrom-Json
Require ($candidateBase.status -ceq 'built_private_not_executed_or_accepted' -and $candidateBase.kind -ceq 'Client' -and $candidateBase.executable.sha256 -ceq $expectedCandidateBase) 'Baseline is not the exact 36c5168f private client.'
Assert-Identity $candidateBase.executable
$activeBefore=@(Active-Identities)
$provenance=Identity (Join-Path $b6 'b6-source-provenance.json')
$proof=Get-Content -LiteralPath $provenance.path -Raw|ConvertFrom-Json
Require ($proof.status -ceq 'verified_isolated_B6_source_and_libraries_no_build' -and $proof.isolated_copy.source_root -ieq $source -and $proof.isolated_copy.library_root -ieq $libraries) 'Accepted B6 source authority differs.'
$sourceManifest=Identity (Join-Path $b6 'b6-isolated/source-manifest.json')
Require ($sourceManifest.sha256 -ceq '3c663803834e3ded81665fd00a726a13549db404174dfcd6c9d1db79b27d87e8') 'Frozen source inventory changed.'
$sourceHashes=@{};foreach($entry in @(Get-Content -LiteralPath $sourceManifest.path -Raw|ConvertFrom-Json)){$sourceHashes[$entry.path]=$entry.sha256}
$libraryManifest=Identity (Join-Path $b6 'b6-isolated/library-manifest.json')
Require ($libraryManifest.sha256 -ceq '29b6b6df7400f33d9a47226034de0f416a01fbbfbfd9ff757d2f07d11b6b8e09') 'Frozen archive inventory changed.'
$libraryHashes=@{};foreach($entry in @(Get-Content -LiteralPath $libraryManifest.path -Raw|ConvertFrom-Json)){$libraryHashes[$entry.path]=$entry.sha256};Require ($libraryHashes.Count -eq 132) 'Unexpected frozen inventory count.'
$clientTemplate=Identity (Join-Path $baseline 'build-metadata/hlclient.vcxproj')
$rendererTemplate=Identity (Join-Path $baseline 'build-metadata/hlclient_renderer_opengl.vcxproj')
Require ($clientTemplate.sha256 -ceq '6caccf9774f8feb288644f1a16789698ff2f40d87dcc1f00a297c1ecc2b1fe75' -and $rendererTemplate.sha256 -ceq 'b0fb94161e7cd872f70c396631ae9eee27f400d5e97d6334e91901f92fa4c384') 'Frozen template changed.'
$acceptedMain=Identity (Join-Path $acceptedBuild 'inputs/main.cpp')
Require ($acceptedMain.sha256 -ceq '8f5fd9fee33997f6f256ce1cf979b3838557265fe9dd9e16635426badd55a7a1') 'Accepted default-off main changed.'
$acceptedBuildRecord=Identity (Join-Path $acceptedBuild 'build-record.json')
$accepted=Get-Content -LiteralPath $acceptedBuildRecord.path -Raw|ConvertFrom-Json
Require ($accepted.production_default_off -eq $true -and $accepted.executable.sha256 -ceq $expectedClient -and $accepted.exit_code -eq 0) 'Accepted client build binding differs.'
$sdlHead=(Get-Content -LiteralPath (Join-Path $sdlRoot '.git/HEAD') -Raw).Trim();Require ($sdlHead -ceq '147a8ee32dbf9ac02f3794964490687b6bbda1bc') 'Pinned SDL interface changed.'
$gladHashes=@{};$gladHashes[(Join-Path $gladInclude 'glad/gl.h')]='9cf344dd73f7f5edab54b9a4ac191a4b2a9ad73f09aee52803ee5711c532c6a3';$gladHashes[(Join-Path $gladInclude 'KHR/khrplatform.h')]='5ee5d5c2c6b2abd028cec8dc1a24e995bb47b075654a6fc55f3a1293de36943f'
foreach($path in $gladHashes.Keys){Require ((Identity $path).sha256 -ceq $gladHashes[$path]) 'Pinned GLAD interface changed.'}
$rendererOriginal=Identity (Join-Path $taskSource 'src/renderer/opengl/opengl_renderer.cpp')
$entityOriginal=Identity (Join-Path $source 'src/renderer/opengl/opengl_entity_renderer.cpp')
Require ($entityOriginal.sha256 -ceq $sourceHashes['src/renderer/opengl/opengl_entity_renderer.cpp']) 'Frozen entity renderer changed.'
$tools=@(foreach($path in @($msbuild,(Join-Path $toolsetRoot 'bin/Hostx64/x86/cl.exe'),(Join-Path $toolsetRoot 'bin/Hostx64/x86/link.exe'),(Join-Path $toolsetRoot 'bin/Hostx64/x86/lib.exe'))){$id=Identity $path;$id.file_version=(Get-Item -LiteralPath $path).VersionInfo.FileVersion;$id})
$build=Join-Path $taskRoot ('b-'+$Kind.ToLowerInvariant()+'-'+$Variant.ToLowerInvariant()+'-'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'-'+[guid]::NewGuid().ToString('N').Substring(0,6))
$inputRoot=Join-Path $build 'in';No-Reparse $build
foreach($part in @('','in','in/tests','in/src/renderer/opengl','bin','obj','renderer','renderer/bin','renderer/obj')){$null=New-Item -ItemType Directory -Path (Join-Path $build $part) -ErrorAction Stop}
Copy-Item -LiteralPath $PSCommandPath -Destination (Join-Path $build 'build-workflow.ps1') -ErrorAction Stop
$snapshots=[Collections.Generic.List[object]]::new();$snapshotHashes=@{}
function Snapshot([string]$Original,[string]$Relative){
    $before=Identity $Original;$destination=Join-Path $inputRoot $Relative;Require (Under $destination $inputRoot) 'Snapshot escaped input root.';No-Reparse $destination
    Copy-Item -LiteralPath $Original -Destination $destination -ErrorAction Stop;$copy=Identity $destination
    Require ($copy.sha256 -ceq $before.sha256) 'Input changed during snapshot.';$snapshots.Add([ordered]@{source=$before;copy=$copy});$snapshotHashes[$copy.path]=$copy.sha256;return $copy.path
}
$rendererCpp=Snapshot $rendererOriginal.path 'src/renderer/opengl/opengl_renderer.cpp'
$entityCpp=Snapshot $entityOriginal.path 'src/renderer/opengl/opengl_entity_renderer.cpp'
$taskGlHeaders=@(Get-ChildItem -LiteralPath (Join-Path $taskSource 'src/renderer/opengl') -File|Where-Object Extension -in @('.hpp','.h'))
Require (@($taskGlHeaders|Where-Object Name -ceq 'opengl_scene_target.hpp').Count -eq 1) 'The owned-target contract header must be a task-local input.'
foreach($header in $taskGlHeaders){$null=Snapshot $header.FullName ('src/renderer/opengl/'+$header.Name)}
$sceneTargetHeader=Join-Path $inputRoot 'src/renderer/opengl/opengl_scene_target.hpp'
$api=Snapshot (Join-Path $taskSource 'sky_candidate_api.hpp') 'sky_candidate_api.hpp'
foreach($file in @(Get-ChildItem -LiteralPath $taskSource -File|Where-Object { $_.Extension -in @('.hpp','.h') -and $_.Name -cne 'sky_candidate_api.hpp' })){$null=Snapshot $file.FullName $file.Name}
$units=@()
if($Kind -ceq 'Client'){
    $units+=Snapshot (Join-Path $taskSource 'main.cpp') 'main.cpp'
    $units+=Snapshot (Join-Path $taskSource 'sky_diagnostic_entry.cpp') 'sky_diagnostic_entry.cpp'
    foreach($file in @(Get-ChildItem -LiteralPath (Join-Path $taskSource 'tests') -File|Where-Object Extension -in @('.hpp','.h','.cpp'))){$null=Snapshot $file.FullName ('tests/'+$file.Name)}
}else{
    $units+=Snapshot (Join-Path $taskSource 'tests/sky_candidate_fixture.cpp') 'tests/sky_candidate_fixture.cpp'
    foreach($file in @(Get-ChildItem -LiteralPath (Join-Path $taskSource 'tests') -File|Where-Object Extension -in @('.hpp','.h'))){$null=Snapshot $file.FullName ('tests/'+$file.Name)}
}
Write-Json (Join-Path $build 'input-manifest.json') $snapshots.ToArray()
$record=[ordered]@{schema='hlc.background-sky.private-build.v1';status='prepared';variant=$Variant;kind=$Kind;build_directory=$build;created_utc=[DateTime]::UtcNow.ToString('o');
    source_authority='exact_36c5168f_compiled_inputs_plus_task_local_static_world_sky_submission_delta_with_inherited_frozen_B6_link_closure';accepted_client_sha256=$expectedClient;accepted_launcher_sha256=$expectedLauncher;
    candidate_baseline_sha256=$expectedCandidateBase;candidate_baseline_record=$candidateBaseRecord;source_lineage=$lineageIdentity;algorithm='world_sky_non_occluding';default_policy='original';supported_startup_policies=@('original','depth','no_background','no_sky_color','world_sky_non_occluding');reserved_not_reinterpreted_policy_values=@(4,5);
    accepted_client_build=$acceptedBuildRecord;accepted_main=$acceptedMain;source_provenance=$provenance;source_manifest=$sourceManifest;library_manifest=$libraryManifest;
    renderer_template=$rendererTemplate;client_template=$clientTemplate;tools=$tools;configuration='Release';platform='Win32';toolset='14.34.31933';sdl_commit=$sdlHead;
    renderer_translation_units=@($rendererCpp,$entityCpp);executable_translation_units=$units;other_archives_rebuilt=0;active_before=$activeBefore;active_after=$null;delivery_writes=$false;game_or_tests_launched=$false}
function Save-Record {Write-Json (Join-Path $build 'build-record.json') $record}
Save-Record
try{
    # Task-local renderer headers precede frozen B6 headers for BOTH renderer
    # translation units. A quoted include cannot silently select the old target.
    $commonIncludes=@($inputRoot,(Join-Path $inputRoot 'src/renderer/opengl'),(Join-Path $inputRoot 'tests'),(Join-Path $source 'include'),(Join-Path $source 'src/renderer/opengl'),(Join-Path $source 'tests'),$sdlInclude,$gladInclude)
    $libRoot=Join-Path $build 'renderer'
    $lib=Prepare-Project $rendererTemplate.path $libRoot 'hlclient_renderer_opengl' @($rendererCpp,$entityCpp) $commonIncludes $true
    $libProject=Join-Path $libRoot 'r.vcxproj';$lib.xml.Save($libProject)
    $record.renderer_project=Identity $libProject;$record.renderer_removed_build_nodes=$lib.removed;$record.status='building_renderer';Save-Record
    $record.renderer_process=Invoke-OwnedBuild $libProject $libRoot
    $libRows=Audit-Includes $record.renderer_process.stdout.path (Join-Path $libRoot 'actual-include-manifest.json')
    Require (@($libRows|Where-Object path -ieq $api).Count -eq 1) 'Candidate diagnostic API absent from actual renderer include trace.'
    Require (@($libRows|Where-Object path -ieq $sceneTargetHeader).Count -eq 1) 'Task-local owned-target header absent from actual renderer include trace.'
    $archive=Identity (Join-Path $libRoot 'bin/hlclient_renderer_opengl.lib');$record.renderer_archive=$archive
    $includes=$commonIncludes;if($Kind -ceq 'Fixture'){$includes+=@((Join-Path $inputRoot 'tests'),(Join-Path $source 'tests'))}
    $target=if($Kind -ceq 'Client'){'hlclient'}else{'sky_candidate_fixture'}
    $app=Prepare-Project $clientTemplate.path $build $target $units $includes $false
    $ordered=[Collections.Generic.List[object]]::new();$unique=@{};$dependencies=[Collections.Generic.List[string]]::new();$system=[Collections.Generic.List[string]]::new();$overrides=0
    foreach($dependency in ([string]$app.group.Link.AdditionalDependencies -split ';')){
        if($dependency -match '^lib\\Release\\([^\\]+\.lib)$'){
            $name=$Matches[1];Require ($libraryHashes.ContainsKey($name)) "Unknown client archive: $name"
            if($name -ceq 'hlclient_renderer_opengl.lib'){$id=$archive;++$overrides}
            elseif($unique.ContainsKey($name)){$id=$unique[$name]}
            else{$id=Identity (Join-Path $libraries $name);Require ($id.sha256 -ceq $libraryHashes[$name]) "Frozen linked archive changed: $name"}
            $unique[$name]=$id;$ordered.Add($id);$dependencies.Add($id.path)
        }elseif(-not [string]::IsNullOrWhiteSpace($dependency)){
            Require ($dependency -match '^[A-Za-z0-9_]+\.lib$') "Uncontrolled dependency: $dependency";$dependencies.Add($dependency);$system.Add($dependency)
        }
    }
    Require ($overrides -eq 1 -and $ordered.Count -eq 136 -and $unique.Count -eq 117) 'Exactly one renderer override in unchanged ordered client closure required.'
    Child-Text $app.xml $app.group.Link 'AdditionalDependencies' ($dependencies -join ';')
    $project=Join-Path $build 'main.vcxproj';$app.xml.Save($project)
    Write-Json (Join-Path $build 'link-input-manifest.json') ([ordered]@{ordered_archive_count=136;unique_archive_count=117;rebuilt_archive_count=1;unchanged_frozen_archive_count=116;renderer_override=$archive;ordered_archive_inputs=$ordered.ToArray();unique_archive_inputs=@($unique.Values|Sort-Object path);system_library_names=$system.ToArray()})
    $record.project=Identity $project;$record.removed_build_nodes=$app.removed;$record.include_directories=$includes;$record.status='building_executable';Save-Record
    $record.process=Invoke-OwnedBuild $project $build
    $appRows=Audit-Includes $record.process.stdout.path (Join-Path $build 'actual-include-manifest.json')
    $evidence=[Collections.Generic.List[object]]::new();$searches=@{};$lineNumber=0
    foreach($line in [IO.File]::ReadLines($record.process.stdout.path)){
        ++$lineNumber
        if($line -match '^\s*(Searching .+\.lib:|Loaded .+\.lib\(.+\))\s*$'){$evidence.Add([ordered]@{source_line=$lineNumber;text=$line.Trim()})}
        if($line -match '^\s*Searching (.+\.lib):\s*$'){
            $path=[IO.Path]::GetFullPath($Matches[1]);if($searches.ContainsKey($path)){continue};$id=Identity $path
            if($path -ieq $archive.path){$id.role='task_renderer_override';Require ($id.sha256 -ceq $archive.sha256) 'Renderer archive changed at link.'}
            elseif(Under $path $libraries){$id.role='frozen_B6_archive';$name=[IO.Path]::GetFileName($path);Require ($unique.ContainsKey($name) -and $id.sha256 -ceq $unique[$name].sha256 -and $name -cne 'hlclient_renderer_opengl.lib') 'Unexpected frozen archive at link.'}
            elseif((Under $path $toolsetRoot)-or(Under $path $sdkRoot)){$id.role='MSVC_Windows_SDK'}else{throw "Uncontrolled linked archive: $path"};$searches[$path]=$id
        }
    }
    Require ($evidence.Count -gt 0 -and $searches.ContainsKey($archive.path)) 'Missing actual renderer-override linker evidence.'
    $map=Identity (Join-Path $build "$target.map")
    Write-Json (Join-Path $build 'actual-link-manifest.json') ([ordered]@{source_log=$record.process.stdout;records=$evidence.ToArray();actual_library_searches=@($searches.Values|Sort-Object path);map=$map;interpretation='Search alone is not member use; preserve VERBOSE:LIB Loaded records and map.'})
    foreach($row in $snapshots){Assert-Identity $row.source;Assert-Identity $row.copy}
    foreach($id in @($unique.Values)+@($acceptedMain,$acceptedBuildRecord,$sourceManifest,$libraryManifest,$clientTemplate,$rendererTemplate,$provenance,$lineageIdentity,$candidateBaseRecord)){Assert-Identity $id}
    $binaryManifest=@(Get-Content -LiteralPath (Join-Path $baseline 'binary-manifest.json') -Raw|ConvertFrom-Json)
    $runtime=@(foreach($name in @('SDL3.dll','msvcp140.dll','msvcp140_2.dll','msvcp140_atomic_wait.dll','vcruntime140.dll')){
        $original=Identity (Join-Path $baseline "Release-binaries/$name");$entry=@($binaryManifest|Where-Object path -ceq $name)
        Require ($entry.Count -eq 1 -and $original.sha256 -ceq $entry[0].sha256) "Frozen companion changed: $name"
        $destination=Join-Path $build "bin/$name";Copy-Item -LiteralPath $original.path -Destination $destination -ErrorAction Stop;$copy=Identity $destination
        Require ($copy.sha256 -ceq $original.sha256) 'Companion copy differs.';[ordered]@{source=$original;copy=$copy}
    })
    Write-Json (Join-Path $build 'runtime-manifest.json') $runtime
    $record.executable=Identity (Join-Path $build "bin/$target.exe");$record.map=$map;$record.actual_include_count=$appRows.Count;$record.exit_code=0
    $record.active_after=@(Active-Identities);foreach($id in $activeBefore){Assert-Identity $id}
    $record.status='built_private_not_executed_or_accepted'
}catch{$record.status='failed_build_preserved';$record.error=$_.Exception.Message;throw}
finally{$record.completed_utc=[DateTime]::UtcNow.ToString('o');Save-Record;Write-Output "BUILD=$build"}




