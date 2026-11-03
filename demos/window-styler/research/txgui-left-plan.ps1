param(
    [Parameter(Mandatory=$true)][string]$TreePath,
    [string]$OutputPath
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'txgui-tree-check.ps1')
$document=Get-Content -Raw -LiteralPath $TreePath | ConvertFrom-Json
$validation=Get-TxGuiTreeValidation $document
if (-not $validation.passed) { throw "Refuse layout planning from an incomplete tree: $($validation.errors -join '; ')" }
$nodes=@{}
$parents=@{}
function Index($Node,[long]$Parent) {
    $nodes[[long]$Node.identity]=$Node
    $parents[[long]$Node.identity]=$Parent
    foreach ($child in $Node.children) { Index $child ([long]$Node.identity) }
}
Index $document.tree 0
function One($Candidates,[string]$Description) {
    $values=@($Candidates)
    if ($values.Count -ne 1) { throw "Expected one unambiguous $Description, found $($values.Count)." }
    return $values[0]
}
$hits=@($document.captionHitSamples | Select-Object -First 7 | Where-Object frameworkType -eq 'IGFButton' |
    Group-Object identity | ForEach-Object { $_.Group[0] })
if ($hits.Count -ne 3 -or (($hits.tooltipIntent | Sort-Object) -join ',') -cne 'close-intent,maximize-or-restore-intent,minimize-intent') {
    throw 'Three original standard caption intents must be independently identified.'
}
$captionParent=One ($hits | ForEach-Object { $parents[[long]$_.identity] } | Select-Object -Unique) 'original caption parent'
$caption=$nodes[[long]$captionParent]
$title=$nodes[$parents[[long]$caption.identity]]
$body=$nodes[$parents[[long]$title.identity]]
if ($caption.frameworkType -cne 'IGFStackPanel' -or $title.frameworkType -cne 'IGFFrame' -or
    -not $title.visible -or $title.hidden -or $title.rect[1] -ne 0 -or $title.rect[0] -le 0) {
    throw 'Original semantic caption group does not have a supported top-frame geometry.'
}
$height=$title.rect[3]-$title.rect[1]
$sidebar=One ($body.children | Where-Object {
    $_.visible -and -not $_.hidden -and $_.rect[0] -eq 0 -and $_.rect[1] -eq 0 -and
    $_.rect[2] -eq $title.rect[0] -and $_.rect[3] -gt $height
}) 'left sibling sharing the existing top-frame boundary'
$header=One ($sidebar.children | Where-Object {
    $_.frameworkType -ceq 'IGFStackPanel' -and $_.visible -and -not $_.hidden -and
    $_.rect[1] -ge 0 -and $_.rect[3] -le $height -and $_.rect[2] -gt $_.rect[0] -and $_.childCount -gt 0
}) 'visible original left header container'
$content=One ($title.children | Where-Object {
    $_.identity -ne $caption.identity -and $_.visible -and -not $_.hidden -and $_.rect[0] -eq 0 -and
    $_.rect[2] -gt 0 -and $_.rect[3]-$_.rect[1] -eq $height
}) 'original top-row content container'
if (-not $header.layoutItemsComplete -or $header.layoutOrientation -ne 0 -or
    (($header.layoutOrder | Sort-Object) -join ',') -cne (($header.children.identity | Sort-Object) -join ',')) {
    throw 'Original left header typed layout items and vertical orientation are not fully verified.'
}
$reserve=84
$span=[int]$sidebar.rect[2]+$reserve
$headerWidth=[int]$header.rect[2]-[int]$header.rect[0]
$required=$reserve+[int]$header.margin[0]+$headerWidth+[int]$header.margin[2]
$newContentRight=$span+[int]$content.rect[2]-[int]$content.rect[0]
$captionLeft=[int]$title.rect[0]+[int]$caption.rect[0]
$candidateFits=$required -le $span -and $newContentRight -le $captionLeft
$headerIndex=[array]::IndexOf(@($sidebar.children.identity),$header.identity)
$result=[pscustomobject]@{
    proposalOnly=$true
    inputTreeComplete=$true
    originalNodes=$validation.nodes
    originalHeader=$header.identity
    originalHeaderParent=$sidebar.identity
    originalHeaderIndex=$headerIndex
    originalFollowingSiblings=@($sidebar.children | Select-Object -Skip ($headerIndex+1) | ForEach-Object identity)
    existingTitleFrame=$title.identity
    existingTitleFrameParent=$body.identity
    originalTitleBounds=$title.rect
    existingCaptionGroup=$caption.identity
    unrelatedOriginalCaptionChildren=@($caption.children | Where-Object { $_.identity -notin $hits.identity } | ForEach-Object identity)
    reserveDip=$reserve
    proposedLeftSpanDip=$span
    proposedHeaderRightDip=$required
    proposedContentRightDip=$newContentRight
    existingCaptionLeftDip=$captionLeft
    candidateFits=$candidateFits
    liveReady=$false
    requiredBeforeLive=@(
        'Apply verified independent Element/Frame order restoration to real header migration without changing unrelated parents.'
        'Validate expansion and clipping of the existing title frame through original layout properties.'
        'Verify original input hit paths and populated bindings after moving the actual caption buttons.'
    )
    windowLayoutModified=$false
}
$json=$result | ConvertTo-Json -Depth 6
if ($OutputPath) { $json | Out-File -LiteralPath $OutputPath -Encoding utf8 }
$json
