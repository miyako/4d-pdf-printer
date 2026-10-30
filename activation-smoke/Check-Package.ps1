Set-StrictMode -Version Latest

function Assert-SmokeManifest([xml]$Manifest, [string]$Architecture) {
    $identity = $Manifest.Package.Identity
    if ($identity.Name -cne 'Miyako.PsaActivationSmoke' -or
        $identity.Publisher -cne 'CN=DesktopPdfPrototypeDev' -or $identity.Version -ne '0.1.1.0' -or
        $identity.ProcessorArchitecture -cne $Architecture.ToLowerInvariant()) { throw 'Smoke identity/version/publisher/architecture mismatch.' }
    $ns = [Xml.XmlNamespaceManager]::new($Manifest.NameTable)
    $ns.AddNamespace('p', 'http://schemas.microsoft.com/appx/manifest/foundation/windows10')
    $ns.AddNamespace('psa', 'http://schemas.microsoft.com/appx/manifest/printsupport/windows10/2')
    $ns.AddNamespace('d', 'http://schemas.microsoft.com/appx/manifest/desktop/windows10')
    $ns.AddNamespace('r', 'http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities')
    $printers = @($Manifest.SelectNodes('//psa:PrintSupportVirtualPrinter', $ns))
    if ($printers.Count -ne 1 -or $printers[0].PreferredInputFormat -ne 'application/oxps' -or
        $printers[0].HasAttribute('OutputFileTypes') -or
        $printers[0].PrinterUri -cne 'psa-activation-smoke:synthetic' -or
        $printers[0].DisplayName -cne 'ms-resource:QueueDisplayName') { throw 'Expected one fixed OXPS smoke queue, no OutputFileTypes/Save As.' }
    $hosts = @($Manifest.SelectNodes('//d:Extension[@Category="windows.fullTrustProcess"]', $ns))
    if ($hosts.Count -ne 1 -or $hosts[0].Executable -cne 'SmokeHost.exe' -or
        $hosts[0].ParentNode.ParentNode.Id -cne 'Printer') { throw 'Fixed host extension must belong to Printer application.' }
    if ($Manifest.SelectNodes('//r:Capability[@Name="runFullTrust"]', $ns).Count -ne 1) { throw 'Missing runFullTrust capability.' }
    $task = $Manifest.SelectSingleNode('//p:ActivatableClass', $ns)
    if ($task.ActivatableClassId -cne 'PsaActivationSmoke.Task' -or
        $task.ParentNode.Path -cne 'SmokeTask.dll') { throw 'Task factory registration mismatch.' }
    $apps = @($Manifest.SelectNodes('/p:Package/p:Applications/p:Application', $ns))
    if ($apps.Count -ne 2 -or $apps[0].Executable -cne 'SmokeApp.exe' -or
        $apps[1].Executable -cne 'SmokeObserver.exe') { throw 'Smoke application executable mismatch.' }
}

function Assert-SmokePri([xml]$Dump) {
    if ($Dump.PriInfo.ResourceMap.name -cne 'Miyako.PsaActivationSmoke') { throw 'Smoke PRI identity mismatch.' }
    $values = @($Dump.SelectNodes('/PriInfo/ResourceMap/ResourceMapSubtree[@name="Resources"]/NamedResource[@name="QueueDisplayName"]/Candidate[@type="String"]/Value'))
    if ($values.Count -ne 1 -or $values[0].InnerText -cne '4D PDF Activation Smoke (No PDF)') {
        throw 'Packaged PRI must contain the exact isolated smoke queue name.'
    }
}
