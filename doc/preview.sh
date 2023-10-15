#!/usr/bin/env bash
#
# Previewer script for Lfm. Compatible to Ranger's scope.sh,
# see https://github.com/ranger/ranger/blob/master/ranger/data/scope.sh

set -o noclobber -o noglob -o pipefail
IFS=$'\n'

file_path=$1
pv_width=$2
pv_height=$3
image_cache_path=$4
pv_image_enabled=$5

mimetype=$(file --dereference --brief --mime-type -- "${file_path}")

case $mimetype in
text/*)
	head -"$PV_HEIGHT" "$file_path"
	exit 2
	;;

image/*)
	exiftool "$file_path" && exit 3
	exit 1
	;;

video/* | audio/*)
	mediainfo "$file_path" && exit 3
	exiftool "$file_path" && exit 3
	exit 1
	;;
esac

# Fallback
echo '----- File Type Classification -----' && file --dereference --brief -- "$file_path" | fold -w "$pv_width" -s && exit 3
exit 1
