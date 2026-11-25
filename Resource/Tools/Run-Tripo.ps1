param(
    [Parameter(Mandatory = $true)]
    [string]$Part
)

$tripoSecret = Import-Clixml -LiteralPath "$env:LOCALAPPDATA\GameSoftware-TripoKey.xml" -ErrorAction Stop
$env:TRIPO_API_KEY = [System.Net.NetworkCredential]::new('', $tripoSecret).Password
$env:TRIPO_REGION = 'ov'
$env:TRIPO_ASTRA_SESSION = '여기에-Astra-세션-ID'

try {
    python "$PSScriptRoot\tripo_asset_pipeline.py" run $Part
    exit $LASTEXITCODE
} finally {
    Remove-Item Env:TRIPO_API_KEY
}