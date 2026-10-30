Set-StrictMode -Version Latest

function Get-RendererBuildPlan {
    param(
        [Parameter(Mandatory)][ValidateSet('x64', 'arm64')][string]$Architecture,
        [ValidateSet('2022', '2026')][string]$VisualStudioVersion = '2022',
        [Parameter(Mandatory)][version]$CMakeVersion
    )
    if ($VisualStudioVersion -eq '2026' -and $CMakeVersion -lt [version]'4.2.0') {
        throw 'VS2026 requires CMake 4.2+'
    }
    [pscustomobject]@{
        Architecture = $Architecture.ToLowerInvariant()
        VisualStudioVersion = $VisualStudioVersion
        CMakeGenerator = $(if ($VisualStudioVersion -eq '2026') { 'Visual Studio 18 2026' } else { 'Visual Studio 17 2022' })
        VisualStudioRange = $(if ($VisualStudioVersion -eq '2026') { '[18.0,19.0)' } else { '[17.0,18.0)' })
    }
}
