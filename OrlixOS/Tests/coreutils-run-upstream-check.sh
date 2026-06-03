#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -uo pipefail

cd /coreutils-build || exit 125

export HOME=/tmp
export LANG=C
export LC_ALL=C
export LOGNAME=nobody
export PATH=/coreutils-build/src:/bin:/usr/bin
export PWD=/coreutils-build
export SHELL=/bin/bash
export CONFIG_SHELL=/bin/bash
export srcdir=/coreutils
export top_srcdir=/coreutils
export abs_srcdir=/coreutils
export abs_top_srcdir=/coreutils
export abs_top_builddir=/coreutils-build
export CONFIG_HEADER=/coreutils-build/lib/config.h
export PERL=/usr/bin/perl
export AWK=awk
export COREUTILS_GROUPS='0 2'
export EGREP='grep -E'
export EXEEXT=
export MAKE=make
export NON_ROOT_GID=65534
export NON_ROOT_USERNAME=nobody
export RUN_EXPENSIVE_TESTS=yes
export RUN_VERY_EXPENSIVE_TESTS=yes
export TMPDIR=/tmp
export USER=nobody
export VERBOSE=

if [ ! -r /coreutils-build/coreutils-test-env.sh ]; then
  echo 'ORLIX-COREUTILS-TEST-END failures=1 skips=0 total=0'
  exit 1
fi
. /coreutils-build/coreutils-test-env.sh

chmod 0777 /coreutils-build
mkdir -p test-logs
chmod 0777 test-logs

if [ ! -s /coreutils-test-list.txt ]; then
  echo 'ORLIX-COREUTILS-TEST-END failures=1 skips=0 total=0'
  exit 1
fi

total=0
failures=0
skips=0

echo 'TAP version 13'
plan="$(grep -cv '^[[:space:]]*\(#\|$\)' /coreutils-test-list.txt)"
echo "1..$plan"

while read -r mode test_name; do
  case "$mode $test_name" in
    ''|'#'*) continue ;;
  esac
  total=$((total + 1))
  safe_name="${test_name//\//_}"
  log_file="test-logs/${safe_name}.log"
  trs_file="test-logs/${safe_name}.trs"

  runner=(/coreutils/build-aux/test-driver
    --test-name "$test_name"
    --log-file "$log_file"
    --trs-file "$trs_file"
    --color-tests no
    --enable-hard-errors yes
    --expect-failure no
    --)

  if [[ "$test_name" == *.pl ]]; then
    command=(/usr/bin/perl -w -I/coreutils/tests -MCuSkip -MCoreutils "/coreutils/$test_name")
  else
    command=(/bin/bash "/coreutils/$test_name")
  fi

  echo "ORLIX-COREUTILS-TEST-RUNNING $total $test_name"
  /init --run-as "$mode" "${runner[@]}" "${command[@]}" 9>&2
  result='FAIL'
  if [ -s "$trs_file" ]; then
    result="$(sed -n 's/^:test-result: //p' "$trs_file" | tail -n 1)"
  fi

  case "$result" in
    PASS)
      echo "ok $total - $test_name"
      ;;
    SKIP)
      skips=$((skips + 1))
      echo "ok $total - $test_name # SKIP"
      ;;
    *)
      failures=$((failures + 1))
      echo "not ok $total - $test_name"
      [ -s "$log_file" ] && sed 's/^/# /' "$log_file"
      ;;
  esac
done < /coreutils-test-list.txt

echo "ORLIX-COREUTILS-TEST-END failures=$failures skips=$skips total=$total"

if [ "$failures" -eq 0 ] && [ "$skips" -eq 0 ]; then
  exit 0
fi
exit 1
