param(
    [string]$Mode = 'test',
    [int]$Value = 0,
    [string]$StateFile = 'D:\dev\SHIM\memscan_state.txt'
)

Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public static class MemScan
{
    [StructLayout(LayoutKind.Sequential)]
    public struct MBI
    {
        public IntPtr BaseAddress;
        public IntPtr AllocationBase;
        public uint AllocationProtect;
        public IntPtr RegionSize;
        public uint State;
        public uint Protect;
        public uint Type;
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] buf, IntPtr size, out IntPtr read);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr VirtualQueryEx(IntPtr h, IntPtr addr, out MBI mbi, IntPtr len);

    [DllImport("kernel32.dll")]
    public static extern bool CloseHandle(IntPtr h);

    const uint READABLE = 0x02 | 0x04 | 0x08 | 0x20 | 0x40 | 0x80;
    const uint PAGE_GUARD = 0x100;
    const uint MEM_COMMIT = 0x1000;
    const uint MEM_PRIVATE = 0x20000;

    public static List<long> ScanInt(IntPtr h, int value, bool allMemory)
    {
        var hits = new List<long>();
        long addr = 0;
        MBI mbi;
        int mbiSize = Marshal.SizeOf(typeof(MBI));
        var buf = new byte[8 * 1024 * 1024];
        while (VirtualQueryEx(h, new IntPtr(addr), out mbi, new IntPtr(mbiSize)) != IntPtr.Zero)
        {
            long baseAddr = mbi.BaseAddress.ToInt64();
            long size = mbi.RegionSize.ToInt64();
            bool wanted = mbi.State == MEM_COMMIT
                && (mbi.Protect & READABLE) != 0
                && (mbi.Protect & PAGE_GUARD) == 0
                && (allMemory || mbi.Type == MEM_PRIVATE);
            if (wanted)
            {
                long pos = 0;
                while (pos < size)
                {
                    int n = (int)Math.Min((long)buf.Length, size - pos);
                    IntPtr read;
                    if (ReadProcessMemory(h, new IntPtr(baseAddr + pos), buf, new IntPtr(n), out read))
                    {
                        int got = read.ToInt32();
                        for (int i = 0; i + 4 <= got; i += 4)
                        {
                            if (BitConverter.ToInt32(buf, i) == value)
                                hits.Add(baseAddr + pos + i);
                        }
                    }
                    pos += n;
                }
            }
            long next = baseAddr + size;
            if (next <= addr) break;
            addr = next;
        }
        return hits;
    }

    public static int ReadInt(IntPtr h, long addr, out bool ok)
    {
        var buf = new byte[4];
        IntPtr read;
        ok = ReadProcessMemory(h, new IntPtr(addr), buf, new IntPtr(4), out read) && read.ToInt32() == 4;
        return ok ? BitConverter.ToInt32(buf, 0) : 0;
    }

    public static int RegionCount(IntPtr h, out long totalBytes)
    {
        long addr = 0;
        MBI mbi;
        int count = 0;
        totalBytes = 0;
        int mbiSize = Marshal.SizeOf(typeof(MBI));
        while (VirtualQueryEx(h, new IntPtr(addr), out mbi, new IntPtr(mbiSize)) != IntPtr.Zero)
        {
            long baseAddr = mbi.BaseAddress.ToInt64();
            long size = mbi.RegionSize.ToInt64();
            if (mbi.State == MEM_COMMIT && (mbi.Protect & READABLE) != 0 && (mbi.Protect & PAGE_GUARD) == 0 && mbi.Type == MEM_PRIVATE)
            {
                count++;
                totalBytes += size;
            }
            long next = baseAddr + size;
            if (next <= addr) break;
            addr = next;
        }
        return count;
    }
}
"@

$proc = Get-Process -Name 'Horsey' -ErrorAction Stop | Select-Object -First 1
$h = [MemScan]::OpenProcess(0x0410, $false, [uint32]$proc.Id)
if ($h -eq [IntPtr]::Zero) { throw 'OpenProcess failed' }

try {
    switch ($Mode) {
        'test' {
            $ok = $false
            $mz = [MemScan]::ReadInt($h, 0x140000000, [ref]$ok)
            Write-Output ("image base first dword: 0x{0:X8} (readable: {1})" -f $mz, $ok)
            $total = [long]0
            $regions = [MemScan]::RegionCount($h, [ref]$total)
            Write-Output ("private readable regions: {0}, total {1:N0} MB" -f $regions, ($total / 1MB))
        }
        'first' {
            $hits = [MemScan]::ScanInt($h, $Value, $false)
            $hits | ForEach-Object { '{0:X}' -f $_ } | Set-Content -Path $StateFile
            Write-Output ("first scan for {0}: {1} hits saved to {2}" -f $Value, $hits.Count, $StateFile)
        }
        'next' {
            $lines = Get-Content -Path $StateFile
            $keep = New-Object System.Collections.Generic.List[string]
            foreach ($line in $lines) {
                $addr = [Convert]::ToInt64($line, 16)
                $ok = $false
                $v = [MemScan]::ReadInt($h, $addr, [ref]$ok)
                if ($ok -and $v -eq $Value) { $keep.Add($line) }
            }
            $keep | Set-Content -Path $StateFile
            Write-Output ("next filter for {0}: {1} candidates left" -f $Value, $keep.Count)
            $keep | Select-Object -First 20 | ForEach-Object { Write-Output ("  0x{0}" -f $_) }
        }
    }
}
finally {
    [MemScan]::CloseHandle($h) | Out-Null
}
