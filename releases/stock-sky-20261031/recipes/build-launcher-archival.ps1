#requires -Version 7.2
[CmdletBinding()]
param([ValidateSet('Launcher','Tests','All')][string]$Kind='All')
# Private accepted-closure build. Never invokes CMake/project dependencies or delivery.
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')
$repo=[IO.Path]::GetFullPath((Join-Path $taskRoot '../../..')).TrimEnd('\')
$launcherSource=Join-Path $taskRoot 'launcher-source'
$exactTask=Join-Path $repo 'manual-artifacts/task-records/sky-disable-diagnostic-20261029'
$exactLauncher=Join-Path $exactTask 'launcher-all-20261029T012737252Z-34923c/build-record.json'
$nativeBase=Join-Path $repo 'manual-artifacts/task-records/sky-native-checkbox-20261028/build-all-20261028T190903573Z-582853'
$b6=Join-Path $repo 'manual-artifacts/task-records/world-local-lights-b6-20261026/b6-isolated'
$source=Join-Path $b6 'source';$libraries=Join-Path $b6 'Release-libs'
$baseline=Join-Path $repo 'manual-artifacts/task-records/bsp-pvs-world-20261025/baseline'
$accepted=Join-Path $repo 'manual-artifacts/task-records/original-hl-launch-fix-20261028/build-launcher-20261028T135400695Z-56220c'
$sdlInclude=Join-Path $repo 'build/_deps/sdl3-src/include'
$toolsetRoot='C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.34.31933'
$sdkRoot='C:\Program Files (x86)\Windows Kits\10'
$msbuild='C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe'
$utf8=[Text.UTF8Encoding]::new($false)
function Require([bool]$Value,[string]$Message){if(!$Value){throw $Message}}
function Under([string]$Path,[string]$Root){[IO.Path]::GetFullPath($Path).StartsWith([IO.Path]::GetFullPath($Root).TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)}
function Id([string]$Path){
    $f=Get-Item -LiteralPath $Path -Force
    Require (!$f.PSIsContainer) ('Expected ordinary file: '+$Path)
    for($c=$f;$null -ne $c;){
        Require (!( $c.Attributes -band [IO.FileAttributes]::ReparsePoint)) ('Reparse path rejected: '+$c.FullName)
        $c=if($c -is [IO.DirectoryInfo]){$c.Parent}else{$c.Directory}
    }
    [ordered]@{path=$f.FullName;bytes=[long]$f.Length;sha256=(Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
}
function Assert-Id($Expected){$a=Id $Expected.path;Require ($a.sha256 -ceq $Expected.sha256 -and $a.bytes -eq $Expected.bytes) ('Input changed: '+$Expected.path)}
function Json([string]$Path,$Value){Require (Under $Path $taskRoot) 'Output escaped task';[IO.File]::WriteAllText($Path,($Value|ConvertTo-Json -Depth 24)+"`n",$utf8)}
function Active {
    $items=@(foreach($leaf in @('hlclient.exe','HLClientLauncher.exe','HLClientSystemDiagnostics.exe','SDL3.dll','HLCLIENT_DELIVERY_INFO.json')){Id (Join-Path $repo ('build/bin/Release/'+$leaf))})
    Require ($items[0].sha256 -ceq '24a55d8e041d0cce0a651d09d064c73611c8b7b9f648b2560e7347de616e5ace') 'Active client changed'
    Require ($items[1].sha256 -ceq 'fd0cb49ca76e5130a5b9eab291f8f1ab5226c744d4afb5fbab0879d8965007a1') 'Active launcher changed'
    return $items
}
Require ($repo -ieq 'D:\DEV\CPP\HL-Client-engine' -and (Split-Path -Leaf $taskRoot) -ceq 'stock-sky-correction-001-20261031') 'Unexpected task root'
$lineage=Id (Join-Path $taskRoot 'source-lineage.json')
$lineageValue=Get-Content -LiteralPath $lineage.path -Raw|ConvertFrom-Json
Require ($lineageValue.schema -ceq 'hlc.stock-sky-correction.source-lineage.v1') 'Wrong prepared source lineage'
$exactLauncherIdentity=Id $exactLauncher
$exactLauncherRecord=Get-Content -LiteralPath $exactLauncher -Raw|ConvertFrom-Json
Require ($exactLauncherRecord.executable.sha256 -ceq '916b5e110d0f79c5376c72142ae4b42dc8e2fbb5893febb9e366411c8d767118' -and $exactLauncherRecord.exit_code -eq 0) 'Wrong exact36-era frozen launcher baseline'
Assert-Id $exactLauncherRecord.executable
$activeBefore=@(Active)
$acceptedRecord=Get-Content -LiteralPath (Join-Path $accepted 'build-record.json') -Raw|ConvertFrom-Json
Require ($acceptedRecord.executable.sha256 -ceq $activeBefore[1].sha256 -and $acceptedRecord.exit_code -eq 0) 'Accepted launcher provenance differs'
Assert-Id $acceptedRecord.executable;Assert-Id $acceptedRecord.project;Assert-Id $acceptedRecord.template;Assert-Id $acceptedRecord.library_manifest;Assert-Id $acceptedRecord.source_manifest
$sourceHashes=@{};foreach($e in @(Get-Content -LiteralPath $acceptedRecord.source_manifest.path -Raw|ConvertFrom-Json)){$sourceHashes[$e.path]=$e.sha256}
$links=Get-Content -LiteralPath (Join-Path $accepted 'link-input-manifest.json') -Raw|ConvertFrom-Json
Require ($links.ordered_archive_count -eq 13 -and $links.unique_archive_count -eq 12) 'Accepted library closure changed'
foreach($e in $links.unique_archive_inputs){Assert-Id $e}
$acceptedProcess=$links.process_library.archive;Assert-Id $acceptedProcess
Require ($acceptedProcess.sha256 -ceq 'c7f2ff7f24e0ccff6a5e60afb7b9eaaa7f9e9024ecced01e178330fbbabd1852') 'Accepted native process archive differs'
$acceptedInputs=Get-Content -LiteralPath (Join-Path $accepted 'input-manifest.json') -Raw|ConvertFrom-Json
$nativeBaseRecord=Id (Join-Path $nativeBase 'build-record.json')
$nativeBaseValue=Get-Content -LiteralPath $nativeBaseRecord.path -Raw|ConvertFrom-Json
Require ($nativeBaseValue.executable.sha256 -ceq '727cb99d5bb910f3825eb411fd87fdb981b15fd5ab9e133917e8f10f7c6490a9') 'Wrong native checkbox launcher baseline'
Assert-Id $nativeBaseValue.executable
$nativeInputs=Get-Content -LiteralPath (Join-Path $nativeBase 'input-manifest.json') -Raw|ConvertFrom-Json
foreach($leaf in @('startup_journal.hpp','smooth_scroll.hpp','smooth_scroll_check.hpp','gpu_preference.hpp','original_half_life.hpp')){
    $relative='apps/hlclient_launcher/'+$leaf
    $expected=@($nativeInputs|Where-Object relative -ceq $relative)
    Require ($expected.Count -eq 1) 'Missing frozen launcher header'
    Assert-Id $expected[0].snapshot
    $originalText=(Get-Content -LiteralPath $expected[0].snapshot.path -Raw).Replace("`r`n","`n").TrimEnd()
    $localText=(Get-Content -LiteralPath (Join-Path $launcherSource $relative) -Raw).Replace("`r`n","`n").TrimEnd()
    Require ($originalText -ceq $localText) ('Unrelated frozen launcher header changed: '+$leaf)
}
$inputs=@('apps/hlclient_launcher/main.cpp')
$inputs+=@(Get-ChildItem -LiteralPath (Join-Path $launcherSource 'apps/hlclient_launcher') -File -Filter '*.hpp'|ForEach-Object{'apps/hlclient_launcher/'+$_.Name})
$inputs+=@('src/platform/windows/external_launcher.cpp','include/hlclient/platform/windows/external_launcher.hpp','include/hlclient/platform/windows/executable_launch_path.hpp')
if($Kind -ne 'Launcher'){$inputs+=@('apps/hlclient_external_process_fixture/main.cpp','tests/test_launcher_gpu_path_standalone.cpp')}
$build=Join-Path $taskRoot ('launcher-'+$Kind.ToLowerInvariant()+'-'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'-'+[guid]::NewGuid().ToString('N').Substring(0,6))
foreach($d in @('','inputs','bin')){[void](New-Item -ItemType Directory -Path (Join-Path $build $d))}
[IO.File]::Copy($PSCommandPath,(Join-Path $build 'build-workflow.ps1'),$false)
$inputRows=@(foreach($relative in $inputs){
    $original=Id (Join-Path $launcherSource $relative);$target=Join-Path $build ('inputs/'+$relative)
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target));[IO.File]::Copy($original.path,$target,$false)
    $copy=Id $target;Require ($copy.sha256 -ceq $original.sha256) 'Input changed during snapshot'
    [ordered]@{relative=$relative;original=$original;snapshot=$copy}
})
Json (Join-Path $build 'input-manifest.json') $inputRows
$record=[ordered]@{schema='hlc.stock-sky-correction.native-launcher-build.v1';status='prepared';kind=$Kind;build_directory=$build;created_utc=[DateTime]::UtcNow.ToString('o');configuration='Release';platform='Win32';toolset='14.34.31933';accepted_launcher_build=(Id (Join-Path $accepted 'build-record.json'));accepted_launcher_sha256=$activeBefore[1].sha256;unchanged_client_sha256=$activeBefore[0].sha256;accepted_process_archive=$acceptedProcess;native_baseline=$nativeBaseRecord;exact36_launcher_build=$exactLauncherIdentity;prepared_source_lineage=$lineage;source_authority='frozen exact36-era916b5e11 launcher inputs plus one test-only startup mode/UI delta; accepted13archive closure';default_policy='original';visible_policies=@('original','world_sky_non_occluding');historical_policy_values=[ordered]@{original=0;depth=1;no_background=2;no_sky_color=3};reserved_unaccepted_values=@(4,5);new_policy_value=6;legacy_depth_visible=$false;runtime_text_policy='off';active_before=$activeBefore;active_after=$null;builds=@();game_or_tests_launched=$false;delivery_writes=$false;client_or_renderer_rebuilt=$false;exit_code=$null}
$record.tools=@(foreach($path in @($msbuild,(Join-Path $toolsetRoot 'bin/Hostx64/x86/cl.exe'),(Join-Path $toolsetRoot 'bin/Hostx64/x86/link.exe'))){Id $path})
Json (Join-Path $build 'build-record.json') $record
$uri='http://schemas.microsoft.com/developer/msbuild/2003'
function Set-Node($Document,$Parent,[string]$Name,[string]$Value){
    $node=$Parent.SelectSingleNode('*[local-name()="'+$Name+'"]')
    if(!$node){$node=$Document.CreateElement($Name,$uri);[void]$Parent.AppendChild($node)}
    $node.InnerText=$Value
}
function Compile([string]$Target,[string]$Template,[string]$Relative,[bool]$Static,[bool]$Console,[string]$ProcessArchive=''){
    $root=Join-Path $build $Target;[void](New-Item -ItemType Directory -Path $root);[void](New-Item -ItemType Directory -Path (Join-Path $root 'obj'))
    $templateId=Id $Template
    [xml]$xml=Get-Content -LiteralPath $Template -Raw
    $ns=[Xml.XmlNamespaceManager]::new($xml.NameTable);$ns.AddNamespace('m',$uri)
    foreach($n in @($xml.SelectNodes('//m:ProjectConfiguration',$ns))){if($n.Include -cne 'Release|Win32'){[void]$n.ParentNode.RemoveChild($n)}}
    foreach($n in @($xml.SelectNodes('//*[@Condition]',$ns))){if($n.Condition -match '(Debug|MinSizeRel|RelWithDebInfo)\|Win32'){[void]$n.ParentNode.RemoveChild($n)}}
    foreach($query in @('//m:ProjectReference','//m:CustomBuild','//m:Target','//m:PreBuildEvent','//m:PreLinkEvent','//m:PostBuildEvent','//m:CustomBuildStep','//m:Import[@Label="LocalAppDataPlatform"]','//m:ImportGroup[@Label="PropertySheets"]')){foreach($n in @($xml.SelectNodes($query,$ns))){[void]$n.ParentNode.RemoveChild($n)}}
    $isolation=$xml.CreateElement('PropertyGroup',$uri)
    foreach($pair in @(@('ImportDirectoryBuildProps','false'),@('ImportDirectoryBuildTargets','false'),@('VCToolsVersion','14.34.31933'))){Set-Node $xml $isolation $pair[0] $pair[1]}
    [void]$xml.Project.PrependChild($isolation)
    $xml.SelectSingleNode('//m:ProjectGuid',$ns).InnerText='{'+[guid]::NewGuid().ToString().ToUpperInvariant()+'}'
    $xml.SelectSingleNode('//m:ProjectName',$ns).InnerText=$Target
    foreach($n in @($xml.SelectNodes('//m:OutDir',$ns))){$n.InnerText=(Join-Path $build 'bin')+'\'}
    foreach($n in @($xml.SelectNodes('//m:IntDir',$ns))){$n.InnerText=(Join-Path $root 'obj')+'\'}
    foreach($n in @($xml.SelectNodes('//m:TargetName',$ns))){$n.InnerText=$Target}
    foreach($n in @($xml.SelectNodes('//m:LocalDebuggerWorkingDirectory',$ns))){$n.InnerText=$root}
    $defs=@($xml.SelectNodes('//m:ItemDefinitionGroup',$ns));Require ($defs.Count -eq 1) 'One Release definition required';$g=$defs[0]
    $includes=@((Join-Path $build 'inputs/include'),(Join-Path $build 'inputs/apps/hlclient_launcher'),(Join-Path $source 'include'),$sdlInclude)
    foreach($n in @($xml.SelectNodes('//m:AdditionalIncludeDirectories',$ns))){$n.InnerText=$includes -join ';'}
    Set-Node $xml $g.ClCompile 'AdditionalOptions' ('/Zc:__cplusplus /FS /utf-8 /showIncludes /external:I "'+$sdlInclude+'"')
    Set-Node $xml $g.ClCompile 'DebugInformationFormat' ''
    $g.ClCompile.PreprocessorDefinitions=($g.ClCompile.PreprocessorDefinitions -replace '%\(PreprocessorDefinitions\);?','')
    $ordered=@();$system=@()
    if($Static){Set-Node $xml $g.Lib 'AdditionalOptions' '/machine:X86'}
    else{
        $deps=@()
        foreach($dep in ($g.Link.AdditionalDependencies -split ';')){
            if([IO.Path]::IsPathRooted($dep)){
                $known=@($links.unique_archive_inputs|Where-Object path -ieq $dep);Require ($known.Count -eq 1) ('Uncontrolled link archive '+$dep)
                $path=if($dep -ieq $acceptedProcess.path){$ProcessArchive}else{$dep}
                Require (![string]::IsNullOrEmpty($path)) 'Replacement process archive missing'
                $id=Id $path;$ordered+=@($id);$deps+=@($path)
            }elseif($dep -match '^lib\\Release\\') {throw 'Unrebased link input found'}
            elseif(![string]::IsNullOrWhiteSpace($dep)){Require ($dep -match '^[A-Za-z0-9_]+\.lib$') ('Uncontrolled system library '+$dep);$system+=@($dep);$deps+=@($dep)}
        }
        $g.Link.AdditionalDependencies=$deps -join ';'
        if($Target -ne 'hlclient_external_process_fixture'){Require ($ordered.Count -eq 13) 'Launcher/test ordered archive closure changed'}
        Set-Node $xml $g.Link 'AdditionalLibraryDirectories' ''
        Set-Node $xml $g.Link 'AdditionalOptions' '/machine:X86 /VERBOSE:LIB'
        Set-Node $xml $g.Link 'ImportLibrary' (Join-Path $root ($Target+'.lib'))
        Set-Node $xml $g.Link 'ProgramDataBaseFile' (Join-Path $root ($Target+'.pdb'))
        Set-Node $xml $g.Link 'GenerateMapFile' 'true';Set-Node $xml $g.Link 'MapFileName' (Join-Path $root ($Target+'.map'))
        if($Console){Set-Node $xml $g.Link 'SubSystem' 'Console';Set-Node $xml $g.Link 'DelayLoadDLLs' ''}
    }
    foreach($n in @($xml.SelectNodes('/m:Project/m:ItemGroup/m:ClCompile|/m:Project/m:ItemGroup/m:ClInclude',$ns))){[void]$n.ParentNode.RemoveChild($n)}
    $unit=Join-Path $build ('inputs/'+$Relative);$group=$xml.CreateElement('ItemGroup',$uri);$n=$xml.CreateElement('ClCompile',$uri);$n.SetAttribute('Include',$unit);[void]$group.AppendChild($n);[void]$xml.Project.AppendChild($group)
    Require (@($xml.SelectNodes('/m:Project/m:ItemGroup/m:ClCompile',$ns)).Count -eq 1) 'Exactly one TU per target required'
    $project=Join-Path $root ($Target+'.vcxproj');$xml.Save($project)
    $args=@($project,'/p:Configuration=Release','/p:Platform=Win32','/p:VCToolsVersion=14.34.31933','/p:BuildProjectReferences=false','/p:ImportDirectoryBuildProps=false','/p:ImportDirectoryBuildTargets=false','/m:1','/nr:false','/v:minimal',('/bl:'+(Join-Path $root 'build.binlog')))
    $part=[ordered]@{target=$Target;template=$templateId;project=(Id $project);translation_unit=(Id $unit);ordered_archives=$ordered;system_libraries=$system;argv=@($msbuild)+$args;exit_code=$null;status='prepared'}
    $si=[Diagnostics.ProcessStartInfo]::new($msbuild);$si.UseShellExecute=$false;$si.CreateNoWindow=$true;$si.WindowStyle='Hidden';$si.WorkingDirectory=$root;$si.RedirectStandardOutput=$true;$si.RedirectStandardError=$true
    foreach($arg in $args){$si.ArgumentList.Add($arg)};foreach($key in @('CL','_CL_','LINK','_LINK_')){[void]$si.Environment.Remove($key)};$si.Environment['VSLANG']='1033'
    $so=$null;$se=$null;$p=$null
    try{
        $so=[IO.File]::Open((Join-Path $root 'build.stdout.log'),'CreateNew','Write','Read');$se=[IO.File]::Open((Join-Path $root 'build.stderr.log'),'CreateNew','Write','Read')
        $p=[Diagnostics.Process]::Start($si);$part.process_id=$p.Id;$part.status='building';Json (Join-Path $root 'build-record.json') $part
        $ot=$p.StandardOutput.BaseStream.CopyToAsync($so);$et=$p.StandardError.BaseStream.CopyToAsync($se);$p.WaitForExit();[void]$ot.GetAwaiter().GetResult();[void]$et.GetAwaiter().GetResult();$part.exit_code=$p.ExitCode
        $so.Dispose();$so=$null;$se.Dispose();$se=$null
        $part.stdout=Id (Join-Path $root 'build.stdout.log');$part.stderr=Id (Join-Path $root 'build.stderr.log')
        Require ($part.exit_code -eq 0) ('Compile failed; retained '+$root)
        $headers=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase);$searched=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach($line in [IO.File]::ReadLines($part.stdout.path)){
            if($line -match '^\s*Note: including file:\s*(.+?)\s*$'){[void]$headers.Add([IO.Path]::GetFullPath($Matches[1]))}
            if($line -match '^\s*Searching (.+\.lib):\s*$'){[void]$searched.Add([IO.Path]::GetFullPath($Matches[1]))}
        }
        Require ($headers.Count -gt 0) 'Missing actual include output'
        $headerRows=@(foreach($path in @($headers|Sort-Object)){
            $role=if(Under $path (Join-Path $build 'inputs')){'task_snapshot'}elseif(Under $path (Join-Path $source 'include')){'frozen_B6'}elseif(Under $path $sdlInclude){'pinned_SDL'}elseif((Under $path $toolsetRoot)-or(Under $path $sdkRoot)){'system_toolchain'}else{throw ('Uncontrolled include: '+$path)}
            $id=Id $path;$id.role=$role
            if($role -ceq 'frozen_B6'){$rel=[IO.Path]::GetRelativePath($source,$path).Replace('\','/');Require ($sourceHashes.ContainsKey($rel)-and $sourceHashes[$rel] -ceq $id.sha256) ('Frozen include changed '+$rel)}
            $id
        })
        Json (Join-Path $root 'actual-include-manifest.json') $headerRows
        $searchRows=@(foreach($path in @($searched|Sort-Object)){
            $id=Id $path;$known=@($ordered|Where-Object path -ieq $path)
            Require (($known.Count -gt 0 -and $known[0].sha256 -ceq $id.sha256)-or(Under $path $toolsetRoot)-or(Under $path $sdkRoot)) ('Uncontrolled linker search: '+$path)
            $id
        })
        Json (Join-Path $root 'actual-link-manifest.json') $searchRows
        $part.actual_include_manifest=Id (Join-Path $root 'actual-include-manifest.json');$part.actual_link_manifest=Id (Join-Path $root 'actual-link-manifest.json')
        $part.output=Id (Join-Path $build ('bin/'+$Target+$(if($Static){'.lib'}else{'.exe'})))
        if(!$Static){$part.map=Id (Join-Path $root ($Target+'.map'))}
        $part.status='built_not_executed'
    }catch{$part.status='failed_preserved';$part.error=$_.Exception.Message;throw}
    finally{if($so){$so.Dispose()};if($se){$se.Dispose()};if($p){$p.Dispose()};$part.completed_utc=[DateTime]::UtcNow.ToString('o');Json (Join-Path $root 'build-record.json') $part;$script:record.builds+=@($part);Json (Join-Path $build 'build-record.json') $script:record}
    return $part.output
}
try{
    $processTemplate=Join-Path $baseline 'build-metadata/hlclient_external_launcher.vcxproj'
    Require ((Id $processTemplate).sha256 -ceq '3db55c5dc312ca93758516ae36ddfaf9740b6f6063bfcdf717e78841e377767c') 'Process template changed'
    $process=Compile 'hlclient_external_launcher' $processTemplate 'src/platform/windows/external_launcher.cpp' $true $false
    $record.process_library=$process
    if($Kind -ne 'Tests'){$record.executable=Compile 'HLClientLauncher' $acceptedRecord.project.path 'apps/hlclient_launcher/main.cpp' $false $false $process.path}
    if($Kind -ne 'Launcher'){
        $fixtureTemplate=Join-Path $baseline 'build-metadata/hlclient_external_process_fixture.vcxproj'
        Require ((Id $fixtureTemplate).sha256 -ceq '761818eaf00be5568ef1b2df2ff8fa77dc339930130cd25aea65ac74458ef776') 'Fixture template changed'
        $record.fixture=Compile 'hlclient_external_process_fixture' $fixtureTemplate 'apps/hlclient_external_process_fixture/main.cpp' $false $true
        $record.tests=Compile 'test_launcher_gpu_path_standalone' $acceptedRecord.project.path 'tests/test_launcher_gpu_path_standalone.cpp' $false $true $process.path
    }
    $runtime=@(foreach($name in @('SDL3.dll','msvcp140.dll','msvcp140_2.dll','msvcp140_atomic_wait.dll','vcruntime140.dll')){
        $file=Id (Join-Path $baseline ('Release-binaries/'+$name));$destination=Join-Path $build ('bin/'+$name);[IO.File]::Copy($file.path,$destination,$false);$copy=Id $destination;Require ($file.sha256 -ceq $copy.sha256) 'Runtime copy differs';$copy
    });Json (Join-Path $build 'runtime-manifest.json') $runtime
    foreach($e in $inputRows){Assert-Id $e.original;Assert-Id $e.snapshot};foreach($e in $links.unique_archive_inputs){Assert-Id $e}
    $record.active_after=@(Active);foreach($e in $activeBefore){Assert-Id $e}
    $record.exit_code=0;$record.status='built_private_not_executed_or_delivered'
}catch{$record.status='failed_build_preserved';$record.error=$_.Exception.Message;throw}
finally{$record.completed_utc=[DateTime]::UtcNow.ToString('o');Json (Join-Path $build 'build-record.json') $record;Write-Output ('BUILD='+$build)}
