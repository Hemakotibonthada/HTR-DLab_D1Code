# PowerShell script to pull all branches from origin
# This script fetches all remote branches and creates local tracking branches

Write-Host "Fetching all remote branches..." -ForegroundColor Green
git fetch --all

Write-Host "Getting list of remote branches..." -ForegroundColor Green
$remoteBranches = git branch -r | Where-Object { $_ -notmatch "HEAD" -and $_ -notmatch "origin/HEAD" }

foreach ($remoteBranch in $remoteBranches) {
    $branchName = $remoteBranch.Trim() -replace "origin/", ""
    
    # Check if local branch already exists
    $localBranchExists = git branch --list $branchName
    
    if (-not $localBranchExists) {
        Write-Host "Creating local tracking branch: $branchName" -ForegroundColor Yellow
        git branch --track $branchName origin/$branchName
    } else {
        Write-Host "Local branch $branchName already exists" -ForegroundColor Cyan
    }
}

Write-Host "Pulling latest changes for all local branches..." -ForegroundColor Green
$localBranches = git branch --format="%(refname:short)"

foreach ($branch in $localBranches) {
    if ($branch -ne "* main" -and $branch -ne "main") {
        Write-Host "Switching to and pulling branch: $branch" -ForegroundColor Yellow
        git checkout $branch
        git pull origin $branch
    }
}

# Switch back to main branch
Write-Host "Switching back to main branch" -ForegroundColor Green
git checkout main
git pull origin main

Write-Host "All branches have been updated!" -ForegroundColor Green
Write-Host "Local branches:" -ForegroundColor Cyan
git branch
