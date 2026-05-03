# Script to simplify sync process with upstream master, becuase of diffrent folder structure:
# OpenJK: codemp/rd-vulkan
# JK2MV: src/rd-vulkan
# NOTE: I used ChatGPT Free generating this script, since I did not feel like spending much time on this ..
#       ! This is not used during code compilation !
		
OLD="codemp/rd-vulkan"
NEW="src/rd-vulkan"

read -p "How many commits to apply? " N

for commit in $(git log upstream-ejk/master --reverse --pretty=format:%H -n $N); do
    echo "Applying $commit"

    git reset --hard HEAD
    git clean -fd

    skip_commit=0

	if git format-patch -1 "$commit" --stdout \
		| sed "s|$OLD|$NEW|g" \
		| git apply -3 --index; then

		:
	else
		echo "Warning: Apply failed for $commit"

		while true; do
			echo ""
			echo "Options:"
			echo "  [f] fix conflicts manually"
			echo "  [s] skip this commit"
			read -p "Choose (f/s): " choice

			case "$choice" in
				f|F)
					echo "Fix conflicts in editor."
					read -p "NOTE: Press ENTER when staged..."

					while git diff --name-only --diff-filter=U | grep -q .; do
						echo "NOTE: Still unresolved conflicts."
						read -p "Press ENTER to re-check..."
					done
					break
					;;

				s|S)
					echo "NOTE: Skipping commit $commit"
					git reset --hard HEAD
					git clean -fd
					skip_commit=1
					break
					;;

				*)
					echo "Invalid option."
					;;
			esac
		done
	fi

    # ---------------------------------------
    # skip logic
    # ---------------------------------------
    if [ "$skip_commit" -eq 1 ]; then
        continue
    fi

    # ---------------------------------------
    # commit with metadata preserved
    # ---------------------------------------
    msg=$(git log -1 --pretty=%B "$commit")

    author_name=$(git log -1 --pretty=%an "$commit")
    author_email=$(git log -1 --pretty=%ae "$commit")
    author_date=$(git log -1 --pretty=%aI "$commit")

    GIT_AUTHOR_NAME="$author_name" \
    GIT_AUTHOR_EMAIL="$author_email" \
    GIT_AUTHOR_DATE="$author_date" \
    git commit -m "$msg"

done