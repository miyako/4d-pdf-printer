$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$path = [IO.Path]::GetFullPath([IO.Path]::Combine($PSScriptRoot, '../Test-HostedRegistration.ps1'))
$tokens = $null
$errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$errors)
if ($errors) { throw ($errors | Out-String) }
$definition = $ast.Find({
    param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq 'Get-OwnedPackageRegistrations'
}, $true)
if (-not $definition) { throw 'Cleanup ownership function not found.' }
. ([scriptblock]::Create($definition.Extent.Text))
function Get-ExactPackages {
    param($Manifest, [switch]$AllUsers)
    if ($AllUsers) { return $script:allPackages }
    return $script:currentPackages
}
$shared = [pscustomobject]@{
    Manifest = 'fixture'
    ExistingFullNames = @('shared-framework')
    ExistingCurrentUserFullNames = @()
}
$currentPackages = @([pscustomobject]@{ PackageFullName = 'shared-framework' })
$allPackages = @([pscustomobject]@{ PackageFullName = 'shared-framework' })
$plan = @(Get-OwnedPackageRegistrations $shared)
if ($plan.Count -ne 1 -or $plan[0].AllUsers -or $plan[0].PackageFullName -ne 'shared-framework') {
    throw 'Shared framework cleanup must remove only the newly created current-user registration.'
}
$shared.ExistingCurrentUserFullNames = @('shared-framework')
if (@(Get-OwnedPackageRegistrations $shared).Count -ne 0) {
    throw 'Pre-existing current-user registration must never be removed.'
}
$shared.ExistingCurrentUserFullNames = @()
$currentPackages = @()
if (@(Get-OwnedPackageRegistrations $shared).Count -ne 0) {
    throw 'Other-user-only framework must never be removed.'
}
$new = [pscustomobject]@{
    Manifest = 'fixture'
    ExistingFullNames = @()
    ExistingCurrentUserFullNames = @()
}
$allPackages = @([pscustomobject]@{ PackageFullName = 'job-created-package' })
$plan = @(Get-OwnedPackageRegistrations $new)
if ($plan.Count -ne 1 -or -not $plan[0].AllUsers -or $plan[0].PackageFullName -ne 'job-created-package') {
    throw 'Only an exact globally new job-created package may have all-user cleanup.'
}
Write-Host 'Shared/current-user/new-package cleanup ownership checks passed.'
