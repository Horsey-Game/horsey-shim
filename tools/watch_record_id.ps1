$log = 'D:\SteamLibrary\steamapps\common\Horsey Game\SHIM.log'
$start = Select-String -Path $log -Pattern 'record (\d+) named' | Select-Object -Last 1
$startId = [int]$start.Matches[0].Groups[1].Value
Write-Output ('watching for a new record after ' + $startId)
$deadline = (Get-Date).AddSeconds(180)
$newId = $null
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 10
    $now = Select-String -Path $log -Pattern 'record (\d+) named' | Select-Object -Last 1
    $id = [int]$now.Matches[0].Groups[1].Value
    if ($id -ne $startId) { $newId = $id; break }
}
if ($null -eq $newId) {
    Write-Output 'no new record in 3 minutes'
} else {
    Write-Output ('new record id: ' + $newId)
    & 'D:\dev\SHIM\tools\memscan.ps1' -Mode next -Value $newId
}
