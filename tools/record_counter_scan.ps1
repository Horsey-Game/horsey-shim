$log = 'D:\SteamLibrary\steamapps\common\Horsey Game\SHIM.log'
$latest = Select-String -Path $log -Pattern 'record (\d+) named' | Select-Object -Last 1
$baseId = [int]$latest.Matches[0].Groups[1].Value
Write-Output ('fresh first scan for ' + $baseId)
& 'D:\dev\SHIM\tools\memscan.ps1' -Mode first -Value $baseId
$deadline = (Get-Date).AddSeconds(240)
$newId = $null
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 10
    $now = Select-String -Path $log -Pattern 'record (\d+) named' | Select-Object -Last 1
    $id = [int]$now.Matches[0].Groups[1].Value
    if ($id -gt $baseId) { $newId = $id; break }
}
if ($null -eq $newId) {
    Write-Output 'no new record in 4 minutes'
} else {
    Write-Output ('new record id: ' + $newId)
    & 'D:\dev\SHIM\tools\memscan.ps1' -Mode next -Value $newId
}
