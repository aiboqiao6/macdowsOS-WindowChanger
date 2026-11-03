$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'txgui-tree-check.ps1')
$checks=[Collections.Generic.List[object]]::new()
function Check($Name,$Passed) { $checks.Add([pscustomobject]@{name=$Name;passed=[bool]$Passed}) }
function Leaf([long]$Identity) { [pscustomobject]@{identity=$Identity;childCount=0;childrenHresult=0;children=@()} }
function Document($Tree,[int]$Count,[bool]$Complete=$true) { [pscustomobject]@{tree=$Tree;nodes=$Count;treeComplete=$Complete} }
$root=[pscustomobject]@{identity=1;childCount=2;childrenHresult=0;children=@((Leaf 2),(Leaf 3))}
Check 'complete original counts and unique identities pass' (Get-TxGuiTreeValidation (Document $root 3)).passed
Check 'native incomplete declaration fails' (-not (Get-TxGuiTreeValidation (Document $root 3 $false)).passed)
Check 'wrong total native count fails' (-not (Get-TxGuiTreeValidation (Document $root 4)).passed)
$root.childCount=3
Check 'silently truncated child list fails' (-not (Get-TxGuiTreeValidation (Document $root 3)).passed)
$root.childCount=2
$root.children=@((Leaf 2),(Leaf 2))
Check 'duplicate identities fail' (-not (Get-TxGuiTreeValidation (Document $root 3)).passed)
$root.children=@((Leaf 2),$null)
Check 'null child fails' (-not (Get-TxGuiTreeValidation (Document $root 2)).passed)
$root.children=@((Leaf 2),(Leaf 3))
$root.childrenHresult=0x80004005L
Check 'failed native child count fails' (-not (Get-TxGuiTreeValidation (Document $root 3)).passed)
Check 'missing root fails' (-not (Get-TxGuiTreeValidation (Document $null 0)).passed)
$historical=Join-Path $PSScriptRoot '../out/live-v26/drawing-complete-Release/read-before/tree.json'
if (Test-Path -LiteralPath $historical) {
    Check 'historical v26 capped tree is rejected' (-not (Get-TxGuiTreeValidation (Get-Content -Raw $historical | ConvertFrom-Json)).passed)
}
$failed=@($checks | Where-Object { -not $_.passed })
$checks | ConvertTo-Json
"Tree verifier checks=$($checks.Count); failures=$($failed.Count)"
if ($failed.Count) { exit 1 }
exit 0
