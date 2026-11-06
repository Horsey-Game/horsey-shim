$log = 'D:\SteamLibrary\steamapps\common\Horsey Game\SHIM.log'
$last = Select-String -Path $log -Pattern 'record (\d+) named' | Select-Object -Last 1
$id = [int]$last.Matches[0].Groups[1].Value
Write-Output ('latest record id in log: ' + $id)
& 'D:\dev\SHIM\tools\memscan.ps1' -Mode first -Value $id
