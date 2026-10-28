param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [ValidateRange(1,36000)][int]$Frames = 180,
    [switch]$Fast
)
$ErrorActionPreference = 'Stop'
$networkRoot = Split-Path $PSScriptRoot -Parent
$networkOutput = Join-Path $networkRoot "Build\Networking\$Configuration"
$networkExecutable = Join-Path $networkOutput 'NetworkDemo.exe'
if (!(Test-Path -LiteralPath $networkExecutable)) { throw '先にTools/NetworkDemo/NetworkDemo.vcxprojをビルドしてください' }

# 空きポートをOSへ割り当ててもらう 既存のアプリやファイアウォールの設定は変更しない
$networkReservations = @([Net.Sockets.UdpClient]::new(0), [Net.Sockets.UdpClient]::new(0))
$networkPorts = @($networkReservations | ForEach-Object { ([Net.IPEndPoint]$_.Client.LocalEndPoint).Port })
$networkReservations | ForEach-Object { $_.Dispose() }
$networkSession = [DateTime]::UtcNow.Ticks
$networkProcesses = @()
try {
    for ($networkPlayer = 0; $networkPlayer -lt 2; $networkPlayer++) {
        $networkArguments = @('--player', $networkPlayer, '--local-port', $networkPorts[$networkPlayer],
            '--peer-port', $networkPorts[1-$networkPlayer], '--session', $networkSession, '--frames', $Frames)
        if ($Fast) { $networkArguments += '--fast' }
        $networkProcesses += Start-Process -FilePath $networkExecutable -ArgumentList $networkArguments `
            -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $networkOutput "peer$networkPlayer.txt") `
            -RedirectStandardError (Join-Path $networkOutput "peer$networkPlayer.err.txt")
    }
    foreach ($networkProcess in $networkProcesses) {
        if (!$networkProcess.WaitForExit([int](($Frames / 60 + 20) * 1000))) { throw '同期サンプルが時間内に終了しませんでした' }
        # リダイレクトされた出力も書き終わるまで待つ
        $networkProcess.WaitForExit()
        if ($networkProcess.ExitCode -ne 0) {
            Get-Content (Join-Path $networkOutput 'peer0.err.txt'), (Join-Path $networkOutput 'peer1.err.txt')
            throw "同期サンプルが失敗しました Exit=$($networkProcess.ExitCode)"
        }
    }
    $networkHashes = @()
    for ($networkPlayer = 0; $networkPlayer -lt 2; $networkPlayer++) {
        $networkText = Get-Content -LiteralPath (Join-Path $networkOutput "peer$networkPlayer.txt") -Raw
        Write-Output $networkText.Trim()
        $networkMatch = [regex]::Match($networkText, 'SYNC OK frames=(\d+) hash=(\d+)')
        if (!$networkMatch.Success -or [int]$networkMatch.Groups[1].Value -ne $Frames) { throw '同期完了の記録がありません' }
        $networkHashes += $networkMatch.Groups[2].Value
    }
    if ($networkHashes[0] -ne $networkHashes[1]) { throw '2プロセスの最終状態が一致しません' }
    Write-Output "同期確認成功: $Frames フレーム 両プロセスの状態が一致しました"
}
finally {
    # 終了対象はこのスクリプトが起動したプロセスだけ
    foreach ($networkProcess in $networkProcesses) {
        if (!$networkProcess.HasExited) { $networkProcess.Kill(); $networkProcess.WaitForExit() }
        $networkProcess.Dispose()
    }
}
