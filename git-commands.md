# Quick commands to pull all branches

# 1. Fetch all remote references
git fetch --all

# 2. List all remote branches
git branch -r

# 3. For each remote branch you want locally, create a tracking branch:
# git checkout -b <branch-name> origin/<branch-name>

# 4. Pull all branches using this one-liner:
# git branch -r | grep -v '\->' | while read remote; do git branch --track "${remote#origin/}" "$remote"; done

# 5. Update all local branches:
# git branch | while read branch; do git checkout "$branch" && git pull origin "$branch"; done

# For PowerShell specifically:
# git branch -r | Where-Object { $_ -notmatch "HEAD" } | ForEach-Object { 
#     $branch = $_.Trim() -replace "origin/", ""
#     git checkout -b $branch origin/$branch 2>$null
# }

# Then pull all:
# git branch --format="%(refname:short)" | ForEach-Object {
#     git checkout $_
#     git pull origin $_
# }
