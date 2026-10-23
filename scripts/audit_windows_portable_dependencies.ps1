param(
    [Parameter(Mandatory = $true)][string]$ReleaseDirectory,
    [Parameter(Mandatory = $true)][string]$BuildDirectory,
    [string]$OutputPath,
    [string]$ExpectedClientSha256
)

# Packaging-time inspection only. It never loads the inspected modules, builds,
# installs runtimes, searches PATH for DLLs, or launches Steam/the game.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$release = (Resolve-Path -LiteralPath $ReleaseDirectory).Path
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

if (-not ('HLCPortableAudit.PeImage' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
namespace HLCPortableAudit {
public class Import { public string dll; public string kind; public string[] symbols; }
public class DebugIdentity { public uint type, size; public string format, guid, pdb_path; public uint age; }
public class ImageResult {
    public string architecture, machine, optional_header, operating_system_version,
        subsystem_version, linker_version, subsystem;
    public Import[] imports;
    public string[] manifests;
    public DebugIdentity[] debug_identities;
    public bool delay_directory_present;
}
public sealed class PeImage {
    byte[] b; uint imageBase, headerSize; int opt, directory, sectionCount; bool pe64;
    struct Section { public uint va, size, raw, rawSize; }
    Section[] sections;
    PeImage(string path) {
        b = File.ReadAllBytes(path);
        if (U16(0) != 0x5A4D) throw new InvalidDataException("Not MZ");
        int pe = checked((int)U32(0x3c));
        if (U32(pe) != 0x4550) throw new InvalidDataException("Not PE");
        opt = pe + 24; sectionCount = U16(pe + 6);
        ushort magic = U16(opt); pe64 = magic == 0x20b;
        if (!pe64 && magic != 0x10b) throw new InvalidDataException("Unsupported PE optional header");
        imageBase = pe64 ? 0 : U32(opt + 28); headerSize = U32(opt + 60);
        directory = opt + (pe64 ? 112 : 96);
        uint dirs = U32(opt + (pe64 ? 108 : 92));
        if (dirs < 14 || sectionCount > 96) throw new InvalidDataException("Incomplete PE directories");
        sections = new Section[sectionCount];
        int start = opt + U16(pe + 20);
        for (int i = 0; i < sectionCount; ++i) {
            int p = start + i * 40;
            sections[i] = new Section { va=U32(p+12), size=U32(p+8), raw=U32(p+20), rawSize=U32(p+16) };
        }
    }
    void Need(int p, int n) { if (p < 0 || n < 0 || p > b.Length - n) throw new InvalidDataException("PE extent out of bounds"); }
    ushort U16(int p) { Need(p,2); return BitConverter.ToUInt16(b,p); }
    uint U32(int p) { Need(p,4); return BitConverter.ToUInt32(b,p); }
    ulong U64(int p) { Need(p,8); return BitConverter.ToUInt64(b,p); }
    int Rva(uint r) {
        if (r < headerSize) { Need(checked((int)r),1); return (int)r; }
        foreach (Section s in sections) {
            if (r >= s.va && (ulong)r < (ulong)s.va + Math.Max(s.size,s.rawSize)) {
                uint delta = r - s.va;
                if (delta >= s.rawSize) throw new InvalidDataException("PE RVA points at virtual zero fill");
                int p = checked((int)(s.raw + delta)); Need(p,1); return p;
            }
        }
        throw new InvalidDataException("Unmapped PE RVA");
    }
    string Text(uint r) {
        int p=Rva(r), end=p;
        while(end < b.Length && b[end] != 0 && end-p < 16384) ++end;
        if (end >= b.Length || end-p >= 16384) throw new InvalidDataException("Unterminated PE string");
        return Encoding.ASCII.GetString(b,p,end-p);
    }
    string[] Thunks(uint r) {
        var names = new List<string>();
        for (int i=0; i<65536; ++i) {
            int p=Rva(checked(r+(uint)(i*(pe64?8:4))));
            ulong t=pe64?U64(p):U32(p);
            if(t==0) return names.ToArray();
            ulong flag=pe64?0x8000000000000000UL:0x80000000UL;
            names.Add((t&flag)!=0 ? "ordinal:"+(t&0xffff).ToString() : Text(checked((uint)t+2)));
        }
        throw new InvalidDataException("Too many PE import thunks");
    }
    uint DelayRva(uint value, uint attributes) { return value == 0 ? 0 : ((attributes & 1)!=0 ? value : checked(value-imageBase)); }
    List<Import> Imports() {
        var output=new List<Import>();
        uint r=U32(directory+8), size=U32(directory+12);
        if(r!=0) {
            bool terminated=false;
            for(uint offset=0; offset+20 <= size; offset+=20) {
                int p=Rva(checked(r+offset)); uint lookup=U32(p), name=U32(p+12), iat=U32(p+16);
                if(lookup==0 && name==0 && iat==0) {terminated=true;break;}
                output.Add(new Import {dll=Text(name),kind="normal",symbols=Thunks(lookup!=0?lookup:iat)});
            }
            if(!terminated) throw new InvalidDataException("Unterminated PE import directory");
        }
        r=U32(directory+13*8);size=U32(directory+13*8+4);
        if(r!=0) {
            bool terminated=false;
            for(uint offset=0;offset+32<=size;offset+=32) {
                int p=Rva(checked(r+offset));uint a=U32(p),name=U32(p+4),iat=U32(p+12),lookup=U32(p+16);
                if(a==0 && name==0 && iat==0 && lookup==0) {terminated=true;break;}
                if(pe64 && (a&1)==0) throw new InvalidDataException("Unsupported VA-mode PE64 delay imports");
                output.Add(new Import {dll=Text(DelayRva(name,a)),kind="delay",symbols=Thunks(DelayRva(lookup!=0?lookup:iat,a))});
            }
            if(!terminated) throw new InvalidDataException("Unterminated PE delay directory");
        }
        return output;
    }
    void Resources(uint root, uint relative, int depth, bool manifest, List<string> output) {
        if(depth>3) throw new InvalidDataException("Resource nesting too deep");
        int p=Rva(checked(root+relative)), count=U16(p+12)+U16(p+14);
        if(count>65535) throw new InvalidDataException("Resource count");
        for(int i=0;i<count;++i) {
            uint id=U32(p+16+i*8), child=U32(p+20+i*8);
            bool selected = depth==0 ? id==24 : manifest;
            if(!selected) continue;
            if((child&0x80000000)!=0) Resources(root,child&0x7fffffff,depth+1,selected,output);
            else {
                int data=Rva(checked(root+child)); uint r=U32(data), n=U32(data+4);
                if(n>1024*1024) throw new InvalidDataException("Manifest unexpectedly large");
                int start=Rva(r); Need(start,(int)n);
                output.Add(Encoding.UTF8.GetString(b,start,(int)n).TrimEnd('\0'));
            }
        }
    }
    DebugIdentity[] DebugEntries() {
        uint r=U32(directory+6*8), size=U32(directory+6*8+4);
        var output=new List<DebugIdentity>();
        if(r==0)return output.ToArray();
        if(size%28!=0 || size>28*4096)throw new InvalidDataException("Bad PE debug directory");
        for(uint offset=0;offset<size;offset+=28) {
            int p=Rva(checked(r+offset));uint type=U32(p+12),n=U32(p+16),raw=U32(p+24);
            var d=new DebugIdentity {type=type,size=n,format="other"};
            if(type==2 && n>=24) {
                int start=checked((int)raw);Need(start,checked((int)n));
                if(U32(start)==0x53445352) {
                    d.format="RSDS";var guid=new byte[16];Array.Copy(b,start+4,guid,0,16);
                    d.guid=new Guid(guid).ToString();d.age=U32(start+20);
                    int end=start+24;while(end<start+n && b[end]!=0)++end;
                    d.pdb_path=Encoding.UTF8.GetString(b,start+24,end-start-24);
                }
            }
            output.Add(d);
        }
        return output.ToArray();
    }
    public static ImageResult Read(string path) {
        var x=new PeImage(path); int pe=checked((int)x.U32(0x3c));ushort machine=x.U16(pe+4);
        var manifests=new List<string>();uint resource=x.U32(x.directory+2*8);
        if(resource!=0)x.Resources(resource,0,0,false,manifests);
        return new ImageResult {
            architecture=machine==0x14c?"x86":machine==0x8664?"x64":"unsupported",
            machine="0x"+machine.ToString("X4"), optional_header=x.pe64?"PE32+":"PE32",
            operating_system_version=x.U16(x.opt+40)+"."+x.U16(x.opt+42),
            subsystem_version=x.U16(x.opt+48)+"."+x.U16(x.opt+50),
            linker_version=x.b[x.opt+2]+"."+x.b[x.opt+3], subsystem=x.U16(x.opt+68).ToString(),
            imports=x.Imports().ToArray(),manifests=manifests.ToArray(),
            debug_identities=x.DebugEntries(),
            delay_directory_present=x.U32(x.directory+13*8)!=0
        };
    }
}
}
'@
}

$compilerFiles = @(Get-ChildItem -LiteralPath (Join-Path $build 'CMakeFiles') -Filter CMakeCXXCompiler.cmake -Recurse -File)
if ($compilerFiles.Count -ne 1) { throw 'Expected one configured CMake C++ compiler description.' }
$compilerText = Get-Content -LiteralPath $compilerFiles[0].FullName -Raw
if ($compilerText -notmatch 'set\(CMAKE_CXX_COMPILER "(?<compiler>(?<vs>[^"\r\n]+?)/VC/Tools/MSVC/(?<version>\d+\.\d+\.\d+)/[^"\r\n]+)"\)') { throw 'Cannot identify official configured MSVC compiler.' }
$compiler = $Matches.compiler.Replace('/', '\')
$compilerToolset = $Matches.version
$vs = $Matches.vs.Replace('/', '\')
$toolsetParts = $compilerToolset.Split('.')
$toolsetPrefix = $toolsetParts[0] + '.' + $toolsetParts[1]
$redistRoot = Join-Path $vs 'VC\Redist\MSVC'
$redist = @(Get-ChildItem -LiteralPath $redistRoot -Directory | Where-Object { $_.Name -match ('^' + [regex]::Escape($toolsetPrefix) + '\.\d+$') } | Sort-Object { [version]$_.Name } -Descending | ForEach-Object { Join-Path $_.FullName 'x86\Microsoft.VC143.CRT' } | Where-Object { Test-Path -LiteralPath $_ -PathType Container })
if ($redist.Count -eq 0) { throw "Official same-toolset x86 redistributables missing for $compilerToolset" }
$redistDirectory = $redist[0]
$roots = @('hlclient.exe', 'HLClientLauncher.exe', 'HLClientSystemDiagnostics.exe', 'SDL3.dll')
# A closed OS list, not arbitrary *.dll. CRT API sets are Windows 10's UCRT.
$windowsDlls = @('advapi32.dll','bcrypt.dll','comctl32.dll','comdlg32.dll','crypt32.dll','fwpuclnt.dll','gdi32.dll','imm32.dll','kernel32.dll','ntdll.dll','ole32.dll','oleaut32.dll','pdh.dll','powrprof.dll','rpcrt4.dll','secur32.dll','setupapi.dll','shell32.dll','shlwapi.dll','user32.dll','userenv.dll','version.dll','winmm.dll','wintrust.dll','ws2_32.dll','ucrtbase.dll')
$crtAllowed = @('msvcp140.dll','msvcp140_1.dll','msvcp140_2.dll','msvcp140_atomic_wait.dll','msvcp140_codecvt_ids.dll','vcruntime140.dll','concrt140.dll')
$queue = [Collections.Generic.Queue[object]]::new()
foreach ($name in $roots) { $queue.Enqueue(@{ name=$name; path=(Join-Path $release $name); origin='explicit_release_directory' }) }
$seen = @{}; $files = [Collections.Generic.List[object]]::new(); $edges = [Collections.Generic.List[object]]::new()
while ($queue.Count -gt 0) {
    $entry = $queue.Dequeue(); $key = $entry.name.ToLowerInvariant()
    if ($seen.ContainsKey($key)) { continue }
    if (-not (Test-Path -LiteralPath $entry.path -PathType Leaf)) { throw "Missing mandatory package dependency: $($entry.path)" }
    $image = [HLCPortableAudit.PeImage]::Read($entry.path)
    if ($image.architecture -ne 'x86') { throw "Wrong architecture ($($image.architecture)) for $($entry.path)" }
    $item = Get-Item -LiteralPath $entry.path; $hash = (Get-FileHash -LiteralPath $entry.path -Algorithm SHA256).Hash
    if ($key -eq 'hlclient.exe' -and $ExpectedClientSha256 -and $hash -ine $ExpectedClientSha256) { throw 'Accepted client SHA256 mismatch.' }
    if ($entry.origin -eq 'official_msvc_redist' -and -not $item.VersionInfo.FileVersion.StartsWith($toolsetPrefix + '.')) { throw "VC runtime version differs from toolset: $($entry.path)" }
    $signatureEvidence=$null
    if ($entry.origin -eq 'official_msvc_redist') {
        $signature=Get-AuthenticodeSignature -LiteralPath $entry.path
        if ($signature.Status -ne 'Valid' -or -not $signature.SignerCertificate -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation(?:,|$)') { throw "Official VC runtime Authenticode verification failed: $($entry.path)" }
        $signatureEvidence=@{ status=[string]$signature.Status; signer=$signature.SignerCertificate.Subject; certificate_thumbprint=$signature.SignerCertificate.Thumbprint }
    }
    if ($key.EndsWith('.exe') -and -not (($image.manifests -join "`n") -match 'requestedExecutionLevel level=[''"'']asInvoker[''"'']')) { throw "Expected asInvoker manifest for $($entry.path)" }
    $files.Add([pscustomobject][ordered]@{ name=$entry.name; source=$entry.path; origin=$entry.origin; sha256=$hash; size=$item.Length; version=$item.VersionInfo.FileVersion; architecture=$image.architecture; authenticode=$signatureEvidence; pe=$image })
    $seen[$key] = $true
    foreach ($import in $image.imports) {
        $dll = $import.dll.ToLowerInvariant()
        if ($dll -match '(?:msvcp\d+.*d|vcruntime\d+.*d|ucrtbased|msvcr\d+d)\.dll$') { throw "Debug runtime import rejected: $dll" }
        if ($dll -eq 'sdl3.dll') { $classification='packaged_native'; $path=Join-Path $release 'SDL3.dll'; $origin='explicit_release_directory' }
        elseif ($crtAllowed -contains $dll) { $classification='packaged_official_vc_crt'; $path=Join-Path $redistDirectory $dll; $origin='official_msvc_redist' }
        elseif ($windowsDlls -contains $dll -or $dll -match '^api-ms-win-crt-[a-z0-9-]+\.dll$') { $classification='windows10_system_not_copied'; $path=$null; $origin=$null }
        else { throw "Unclassified mandatory dependency $($entry.name) -> $($import.dll). Packaging must stop for review." }
        $edges.Add([pscustomobject]@{ from=$entry.name; to=$import.dll; import_kind=$import.kind; classification=$classification })
        if ($path) { $queue.Enqueue(@{name=$dll;path=$path;origin=$origin}) }
    }
}

$sourceFiles = @('apps\hlclient_launcher\main.cpp','apps\hlclient_launcher\startup_journal.hpp','apps\hlclient_system_diagnostics\main.cpp','apps\hlclient_system_diagnostics\wpr_probe.cpp','src\app\steam_authentication_provider.cpp','src\platform\sdl_window.cpp','src\renderer\opengl\opengl_renderer.cpp','third_party\glad\src\gl.c')
$sourceEvidence = @($sourceFiles | ForEach-Object { $p=Join-Path $repository $_; [pscustomobject]@{ path=$_; sha256=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash } })
$launcherImage = @($files | Where-Object name -eq 'HLClientLauncher.exe')[0]
$launcherHardDpiImports = @($launcherImage.pe.imports | ForEach-Object { $_.symbols } | Where-Object { $_ -in @('SetProcessDpiAwarenessContext','GetDpiForSystem') })
$launcherSource = Get-Content -LiteralPath (Join-Path $repository 'apps\hlclient_launcher\main.cpp') -Raw
$launcherDpiResolution = if ($launcherHardDpiImports.Count -gt 0) { 'hard_import' } elseif ($launcherSource -match 'GetProcAddress' -and $launcherSource -match 'SetProcessDpiAwarenessContext' -and $launcherSource -match 'GetDpiForSystem') { 'runtime_feature_detection_in_current_source; absent_from_audited_binary_import_tables' } else { 'no_modern_dpi_import_observed; source_contract_not_identified' }
$operatingSystemRequirement = if ($launcherHardDpiImports.Count -gt 0) { 'Windows 10 x64 with WOW64, version 1703 (build 15063) or later by launcher API contract. Packaged modules remain x86. Windows 10 32-bit is not promised for the complete Steam-dependent workflow.' } else { 'Windows 10 x64 with WOW64. Modern launcher DPI entrypoints are not mandatory imports; current launcher source resolves them at runtime with a fallback. Packaged modules remain x86. Windows 10 32-bit is not promised for the complete Steam-dependent workflow.' }
$operatingSystemReason = if ($launcherHardDpiImports.Count -gt 0) { 'Launcher hard-imports modern DPI entrypoints; SetProcessDpiAwarenessContext has a function-specific Microsoft minimum of 1703. PE header 6.0 is not the effective API minimum.' } else { 'Modern DPI entrypoints are absent from the audited launcher import tables. This removes the earlier loader dependency on SetProcessDpiAwarenessContext/GetDpiForSystem. No target Windows 10 execution is claimed.' }
$flags = @()
foreach ($relativeDirectory in @('hlclient.dir\Release','hlclient_launcher.dir\Release','hlclient_system_diagnostics.dir\Release','hlclient_renderer_opengl.dir\Release','_deps\sdl3-build\SDL3-shared.dir\Release')) {
    $logDirectory=Join-Path $build $relativeDirectory
    if (-not (Test-Path -LiteralPath $logDirectory -PathType Container)) { continue }
    foreach ($log in @(Get-ChildItem -LiteralPath $logDirectory -Filter CL.command.1.tlog -Recurse -File)) {
        $p=$log.FullName; $relative=$p.Substring($build.Length+1)
        $text=Get-Content -LiteralPath $p -Raw
        $archFlags=@([regex]::Matches($text,'(?i)/arch:[^\s]+') | ForEach-Object Value | Sort-Object -Unique)
        $flags += [pscustomobject]@{ path=$relative; sha256=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash; explicit_arch_flags=$archFlags; release_md_observed=([bool]($text -match '(?i)/MD\s')); scope='current retained build command log; not full machine-code disassembly' }
    }
}
$result = [pscustomobject][ordered]@{
    schema='hlclient.windows-portable-dependency-audit.v1'; status='passed'; audited_utc=[DateTime]::UtcNow.ToString('o')
    release_directory=$release; architecture='x86'; compiler_path=$compiler; compiler_toolset=$compilerToolset; redist_directory=$redistDirectory
    files=@($files); required_app_local_runtime_files=@($files | Where-Object origin -eq 'official_msvc_redist'); dependency_edges=@($edges)
    audit_scope=@{ normal_imports='all packaged roots and recursive app-local CRT dependencies'; delay_imports='PE directory parsed, all discovered imports classified'; operating_system_closure='OS DLLs/API sets are supplied by Windows, not copied or recursively bundled'; executable_modules_loaded=$false; source_audit='reviewed separately; source hashes pinned below'; unknown_dependency_policy='throw'; debug_runtime_policy='throw'; missing_dependency_policy='throw' }
    requirements=@{
        os=$operatingSystemRequirement
        os_reason=$operatingSystemReason
        launcher_dpi_entrypoint_resolution=$launcherDpiResolution
        launcher_mandatory_modern_dpi_imports=$launcherHardDpiImports
        windows10_runtime='not_run'; clean_machine_runtime='not_run'; external_steam_requirements='Valve ended Steam support for 32-bit Windows on 2026-01-01. Existing legacy clients may function temporarily, but this package does not promise a supported complete workflow on Windows x86. Legitimate installed Steam/Half-Life and official runtime remain required.'
        cpu='x86 with SSE2 is the MSVC default baseline; retained selected Release command logs contain no global AVX/AVX2 flag. Optional SDL/runtime SIMD paths may dispatch by CPU capability. No exhaustive binary instruction reachability audit or old-CPU execution was performed.'
        opengl='Hardware driver providing OpenGL 3.3 Core; SDL requests major=3 minor=3 core and renderer validates it. No bundled software renderer/opengl32.'
        vc_runtime='Required x86 VC143 Release files included app-local from configured same-major.minor official REDIST; Windows 10 supplies UCRT. No installer required for these files on intact supported Windows 10.'
        elevation='All three EXE embedded manifests are inspected below. Optional diagnostics capabilities may be partial; no install or elevation performed.'
    }
    runtime_loading_audit=@(
        @{component='client';source='src/app/steam_authentication_provider.cpp';target='user-selected verified official steam_api.dll';classification='external prerequisite, never bundled';detail='LoadLibraryExW with DLL_LOAD_DIR and SYSTEM32 search after provenance/architecture validation; function exports checked; stock auth never stubbed'},
        @{component='client/OpenGL';source='src/platform/sdl_window.cpp, third_party/glad/src/gl.c';target='Windows opengl32.dll and vendor driver';classification='OS/driver prerequisite, never bundled';detail='Normal path gets GL procedures via SDL; GLAD includes optional built-in opengl32 fallback loader; no external shader files required'},
        @{component='SDL3';source='build/_deps/sdl3-src/src/video/windows, core/windows, audio/wasapi';target='OS feature libraries and entrypoints';classification='system optional feature-detected';detail='XInput fallback, WASAPI avrt, hid, combase, DPI/dark-mode functions, optional Vulkan/D3D/camera backends; game selects OpenGL, not Vulkan/D3D renderer. SDL_DYNAMIC_API environmental override is upstream capability; package supplies no override DLL'},
        @{component='launcher';source='apps/hlclient_launcher/main.cpp, apps/hlclient_launcher/startup_journal.hpp';target='hlclient.exe and HLClientSystemDiagnostics.exe beside launcher';classification='packaged executables';detail='Resolved from GetModuleFileNameW, not current directory or PATH. Always-on bounded startup journal and allowlisted startup ZIP export use native file/ZIP code; no PowerShell or external archiver is required'},
        @{component='diagnostics helper';source='apps/hlclient_system_diagnostics/wpr_probe.cpp';target='Windows-system WPR if present';classification='optional read-only probe';detail='WPR/ADK and hardware counters not required to launch game; no third-party sensor DLL or tool installed'}
    )
    source_evidence=$sourceEvidence; selected_release_command_logs=$flags
    official_references=@(
        'https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setprocessdpiawarenesscontext',
        'https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution',
        'https://learn.microsoft.com/en-us/cpp/windows/choosing-a-deployment-method?view=msvc-170',
        'https://learn.microsoft.com/en-us/cpp/build/reference/arch-x86?view=msvc-170',
        'https://help.steampowered.com/en/faqs/view/49A1-B944-48B8-FF00'
    )
}
if ($OutputPath) {
    $destination=[IO.Path]::GetFullPath($OutputPath)
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination)) | Out-Null
    [IO.File]::WriteAllText($destination,($result | ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
}
$result
