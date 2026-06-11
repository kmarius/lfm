#!/bin/bash

set -u

testdir=$(realpath "${0%/*}")

tempdir=$(mktemp -d)
trap 'rm -rf "$tempdir"' EXIT

TAP_RESULT=$tempdir/tap.log
export TAP_RESULT

LFM=$(realpath "${LFM:-"${0%/*}"/../../build/lfm}")

cd "$testdir"

ret=0

if [ $# -eq 0 ]; then
  set -- "$testdir"/test_*.lua
fi

echo "using $LFM"
for file; do
  echo "
:: running tests from $file"

  "$LFM" -u /dev/null -c "
  lfm.o.preview = false
  package.path = package.path .. ';./?.lua'
  coroutine.wrap(function(file)
    loadfile(file)()
    lfm.quit(require('tap').ret)
  end)('${file}')
"
  "$testdir"/tapview <"$TAP_RESULT" || ret=1
done

exit $ret
