function Get-TxGuiTreeValidation($Document) {
    $errors=[Collections.Generic.List[string]]::new()
    $identities=[Collections.Generic.HashSet[long]]::new()
    if ($Document.treeComplete -ne $true) { $errors.Add('native traversal did not declare completeness') }
    if ($null -eq $Document.tree) { $errors.Add('root is missing') }
    function Visit($Node,[int]$Depth) {
        if ($null -eq $Node) { $errors.Add('null original child'); return }
        if ($Depth -gt 48 -or $identities.Count -ge 4096) { $errors.Add('verification budget exceeded'); return }
        if (-not $Node.identity -or -not $identities.Add([long]$Node.identity)) { $errors.Add('missing or repeated original identity'); return }
        if ($null -eq $Node.childCount -or $Node.childCount -lt 0 -or $Node.childCount -gt 1024 -or
            $null -eq $Node.childrenHresult -or [long]$Node.childrenHresult -ge 0x80000000L) {
            $errors.Add('original child-count read failed')
        }
        if (@($Node.children).Count -ne $Node.childCount) { $errors.Add('declared child count differs from read children') }
        foreach ($child in $Node.children) { Visit $child ($Depth+1) }
    }
    if ($null -ne $Document.tree) { Visit $Document.tree 0 }
    if ($identities.Count -ne $Document.nodes) { $errors.Add('native node count differs from verified identities') }
    [pscustomobject]@{passed=$errors.Count -eq 0;nodes=$identities.Count;errors=$errors.ToArray()}
}
