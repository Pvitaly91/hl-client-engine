# Windows PowerShell 5.1-compatible startup logging. No Steam/network actions.
$script:PortableLogDirectory = $null

function Protect-PortableLogLine {
    param([string]$Message)
    # Drop the whole line instead of retaining a partial credential value.
    if ($Message -match '(?i)ticket|token|password|passwd|credential|rcon|cookie|authorization|steam[_ -]?id|auth[_ -]?(data|blob|payload|key)|session[_ -]?key|userinfo|\[(game|server|chat)\]') {
        return '[sensitive or chat line omitted]'
    }
    if ($Message.Length -gt 8192) { return '[overlong line omitted]' }
    return $Message.Replace([string][char]0, '')
}

function Write-PortableLog {
    [CmdletBinding()]
    param([string]$Message)
    if (-not $script:PortableLogDirectory) { return }
    $path = Join-Path $script:PortableLogDirectory 'launcher.log'
    try {
        if ((Get-Item -LiteralPath $path -ErrorAction SilentlyContinue).Length -gt 262144) { return }
        $line = '{0} {1}{2}' -f [DateTime]::UtcNow.ToString('o'),
            (Protect-PortableLogLine $Message), [Environment]::NewLine
        [System.IO.File]::AppendAllText($path, $line, [System.Text.UTF8Encoding]::new($false))
    } catch {
        Write-Warning 'Could not write the startup log. Check free disk space and folder permissions.'
    }
}

function Initialize-PortableLog {
    param([string]$PackageRoot)
    $script:PortableLogDirectory = $null
    $roots = @((Join-Path $PackageRoot 'logs'))
    if ($env:LOCALAPPDATA) { $roots += (Join-Path $env:LOCALAPPDATA 'HLClientEngine\portable-logs') }
    foreach ($root in $roots) {
        try {
            $folder = Join-Path $root ('launch-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
            [void][System.IO.Directory]::CreateDirectory($folder)
            [System.IO.File]::WriteAllText((Join-Path $folder 'launcher.log'),
                "HL Client portable startup log v1`r`n", [System.Text.UTF8Encoding]::new($false))
            $script:PortableLogDirectory = $folder
            break
        } catch { }
    }
    if (-not $script:PortableLogDirectory) {
        throw 'Cannot create startup logs in the portable folder or LocalAppData. Extract the package to a writable folder.'
    }
    Write-PortableLog ('OS={0}; OS64={1}; launcher64={2}; PowerShell={3}; CLR={4}' -f
        [Environment]::OSVersion.VersionString, [Environment]::Is64BitOperatingSystem,
        [Environment]::Is64BitProcess, $PSVersionTable.PSVersion, [Environment]::Version)
    Write-PortableLog 'Full session reports/network traces are disabled. No environment dump, account data or raw command line is collected.'
}

function Invoke-PortableClient {
    param([string]$Executable, [string[]]$Arguments, [string]$WorkingDirectory)
    if (-not $script:PortableLogDirectory) { throw 'Startup logging was not initialized.' }
    if (-not ('HLCPortableProcessLog' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

public static class HLCPortableProcessLog {
    private const int MaximumLine = 8192;
    private const int MaximumBytes = 2 * 1024 * 1024;
    private static readonly Regex Sensitive = new Regex(
        @"ticket|token|password|passwd|credential|rcon|cookie|authorization|steam[_ -]?id|auth[_ -]?(data|blob|payload|key)|session[_ -]?key|userinfo|\[(game|server|chat)\]",
        RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);

    // Windows CommandLineToArgv/CRT quoting; no cmd.exe or shell interpolation.
    private static string Quote(string value) {
        var result = new StringBuilder("\"");
        int slashes = 0;
        foreach (char c in value) {
            if (c == '\\') { ++slashes; continue; }
            if (c == '"') {
                result.Append('\\', slashes * 2 + 1).Append(c);
            } else { result.Append('\\', slashes).Append(c); }
            slashes = 0;
        }
        return result.Append('\\', slashes * 2).Append('"').ToString();
    }
    private sealed class Sink : IDisposable {
        private readonly object gate = new object();
        private readonly FileStream stream;
        private readonly Encoding encoding = new UTF8Encoding(false);
        private bool full, closed;
        public string Error;
        public Sink(string path) {
            stream = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.Read);
        }
        public void Write(string channel, string line) {
            lock (gate) {
                if (closed || full || Error != null) return;
                if (Sensitive.IsMatch(line)) line = "[sensitive or chat line omitted]";
                line = line.Replace("\0", "");
                byte[] bytes = encoding.GetBytes(DateTime.UtcNow.ToString("o") + " [" + channel + "] " + line + "\r\n");
                if (stream.Length + bytes.Length > MaximumBytes - 128) {
                    bytes = encoding.GetBytes("[output limit reached: later output drained but not saved]\r\n");
                    full = true;
                }
                try { stream.Write(bytes, 0, bytes.Length); stream.Flush(); }
                catch (Exception e) { Error = "output_log_write_failed: " + e.GetType().Name; }
            }
        }
        public void Dispose() { lock (gate) { closed = true; stream.Dispose(); } }
    }
    private static void Pump(StreamReader reader, Sink sink, string channel) {
        var line = new StringBuilder();
        bool overlong = false;
        try {
            // StreamReader already buffers bytes. Read(char[],...,count) can
            // wait for count chars, hiding a short fatal line until process exit.
            int next;
            while ((next = reader.Read()) >= 0) {
                    char c = (char)next;
                    if (c == '\n' || c == '\r') {
                        if (overlong) sink.Write(channel, "[overlong line omitted]");
                        else if (line.Length != 0) sink.Write(channel, line.ToString());
                        line.Clear(); overlong = false;
                    } else if (!overlong) {
                        if (line.Length == MaximumLine) { line.Clear(); overlong = true; }
                        else line.Append(c);
                    }
            }
            if (overlong) sink.Write(channel, "[overlong line omitted]");
            else if (line.Length != 0) sink.Write(channel, line.ToString());
        } catch (Exception e) { sink.Write("reader", e.GetType().Name); }
    }
    public static int Run(string executable, string[] args, string cwd, string log) {
        var quoted = new string[args.Length];
        for (int i = 0; i < args.Length; ++i) quoted[i] = Quote(args[i]);
        var start = new ProcessStartInfo(executable, String.Join(" ", quoted));
        start.WorkingDirectory = cwd;
        start.UseShellExecute = false;
        start.CreateNoWindow = true;
        start.RedirectStandardOutput = true;
        start.RedirectStandardError = true;
        start.StandardOutputEncoding = Encoding.UTF8;
        start.StandardErrorEncoding = Encoding.UTF8;
        using (var sink = new Sink(log))
        using (var process = new Process()) {
            process.StartInfo = start;
            sink.Write("launcher", "Starting client; stdout and stderr capture enabled.");
            if (!process.Start()) throw new InvalidOperationException("Client process did not start.");
            var stdout = Task.Factory.StartNew(() => Pump(process.StandardOutput, sink, "stdout"), TaskCreationOptions.LongRunning);
            var stderr = Task.Factory.StartNew(() => Pump(process.StandardError, sink, "stderr"), TaskCreationOptions.LongRunning);
            process.WaitForExit();
            // Do not wait forever if a descendant inherited an output pipe.
            if (!Task.WaitAll(new Task[] { stdout, stderr }, 5000))
                sink.Write("launcher", "Output drain timeout; final output may be incomplete.");
            int code = process.ExitCode;
            sink.Write("launcher", "Exit code " + code + " (0x" + unchecked((uint)code).ToString("X8") + ")");
            if (sink.Error != null) Console.Error.WriteLine(sink.Error);
            return code;
        }
    }
}
'@
    }
    $output = Join-Path $script:PortableLogDirectory 'client-output.log'
    try {
        Write-PortableLog 'Starting client process.'
        $code = [HLCPortableProcessLog]::Run($Executable, $Arguments, $WorkingDirectory, $output)
        $unsigned = [BitConverter]::ToUInt32([BitConverter]::GetBytes([int]$code), 0)
        Write-PortableLog ('Client exit code={0}; hex=0x{1:X8}' -f $code, $unsigned)
        return [int]$code
    } catch {
        $cause = $_.Exception.GetBaseException()
        Write-PortableLog ('Client launch failed: {0}; exception={1}; HRESULT={2}' -f
            $cause.Message, $cause.GetType().FullName, $cause.HResult)
        if ($cause -is [System.ComponentModel.Win32Exception]) {
            Write-PortableLog ('Windows native error code={0}' -f $cause.NativeErrorCode)
        }
        throw
    }
}
