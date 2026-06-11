#! /bin/sh
# tapview - a TAP (Test Anything Protocol) viewer in pure POSIX shell
#
# SPDX-FileCopyrightText: Eric S. Raymond <esr@thyrsus.com>
# SPDX-License-Identifier: MIT-0
#
# This code is intended to be embedded in your project. The author
# grants permission for it to be distributed under the prevailing
# license of your project if you choose, provided that license is
# OSD-compliant; otherwise the following SPDX tag incorporates the
# MIT No Attribution license by reference.
#
# A newer version may be available at https://gitlab.com/esr/tapview
# Check your last commit dqte for this file against the commit list
# there to see if it might be a good idea to update.
#
OK="."
FAIL="F"
SKIP="s"
TODO_NOT_OK="x"
TODO_OK="u"

LF='
'

ship_char() {
    # shellcheck disable=SC2039
    printf '%s' "$1"	# https://www.etalabs.net/sh_tricks.html
}

ship_line() {
    report="${report}${1}$LF"
}

ship_error() {
    # Terminate dot display and bail out with error
    if [ "${testcount}" -gt 0 ]
    then
	echo ""
    fi
    report="${report}${1}$LF"
    echo "${report}"
    exit 1
}

testcount=0
failcount=0
skipcount=0
todocount=0
status=0
report=""
IFS=""
ln=0
state=plaintext

# shellcheck disable=SC2086
context_get () { printenv "ctx_${1}${depth}"; }
context_set () { export "ctx_${1}${depth}=${2}"; }

context_push () {
    context_set plan ""
    context_set count 0
    context_set test_before_plan no
    context_set test_after_plan no
    context_set expect ""
    context_set bail no
    context_set strict no
}

context_pop () {
    if [ "$(context_get count)" -gt 0 ] && [ -z "$(context_get plan)" ]
    then
	ship_line "Missing a plan at line ${ln}."
	status=1
    elif [ "$(context_get test_before_plan)" = "yes" ] && [ "$(context_get test_after_plan)" = "yes" ] 
    then
	ship_line "A plan line may only be placed before or after all tests."
	status=1
    elif [ "$(context_get plan)" != "" ] && [ "$(context_get expect)" -gt "$(context_get count)" ]
    then
	ship_line "Expected $(context_get expect) tests but only ${testcount} ran."
	status=1
    fi
}

depth=0
context_push

while read -r line
do
    ln=$((ln + 1))
    # Process bailout
    if expr "$line" : "Bail out!" >/dev/null
    then
	ship_line "$line"
	status=2
	break
    fi
    # Use the current indent to choose a scope level
    indent=$(expr "$line" : '[ 	]*')
    if [ "${indent}" -lt "${depth}" ]
    then
	context_pop
        depth="${indent}"
    elif [ "${indent}" -gt "${depth}" ]
    then
	depth="${indent}"
	context_push
    fi
    # Process a plan line
    if expr "$line" : '[ 	]*1\.\.[0-9][0-9]*' >/dev/null
    then
	if [ "$(context_get plan)" != "" ]
	then
	    ship_error "tapview: cannot have more than one plan line."
	fi
	if expr "$line" : ".* *SKIP" >/dev/null || expr "$line" : ".* *skip" >/dev/null
	then
	    ship_line "$line"
	    echo "${report}"
	    exit 1	# Not specified in the standard whether this should exit 1 or 0
	fi
	context_set plan "${line}"
	context_set expect "$(expr "$line" : '[ 	]*1\.\.\([0-9][0-9]*\)')"
	continue
    elif expr "$line" : '[ 	]*[0-9][0-9]*\.\.[0-9][0-9]*' >/dev/null
    then
	 echo "Ill-formed plan line at ${ln}"
	 exit 1
    fi
    # Check for out-of-order test point numbers with the sequence (TAP 14)
    testpoint=$(expr "$line" : '.*ok  *\([0-9][0-9]*\)')
    if [ "${testpoint}" != "" ] && [ "$(context_get expect)" != "" ] && [ "${testpoint}" -gt "$(context_get expect)" ]
    then
	ship_error "tapview: testpoint number ${testpoint} is out of range for plan $(context_get plan)."
    fi
    # Process an ok line
    if expr "$line" : "[ 	]*ok" >/dev/null
    then
	context_set count $(($(context_get count) + 1)) 
	testcount=$((testcount + 1))
	if [ "$(context_get plan)" = "" ]
	then
	    context_set test_before_plan yes
	else
	    context_set test_after_plan yes
	fi
	if expr "$line" : "[^#]* # *TODO" >/dev/null || expr "$line" : "[^#]* # *todo" >/dev/null
	then
	    ship_char ${TODO_OK}
	    ship_line "$line"
	    todocount=$((todocount + 1))
	    if expr "$line" : "[^#]*#[^ ]" >/dev/null
	    then
		ship_line "Suspicious comment leader at ${ln}"
	    fi
	elif expr "$line" : "[^#]* # *SKIP" >/dev/null || expr "$line" : "[^#]* # *skip" >/dev/null
	then
	    ship_char ${SKIP}
	    ship_line "$line"
	    skipcount=$((skipcount + 1))
	    if expr "$line" : "[^#]*#[^ ]" >/dev/null
	    then
		ship_line "Suspicious comment leader at ${ln}"
	    fi
	else
	    ship_char ${OK}
	fi
	state=plaintext
	continue
    fi
    # Process a not-ok line
    if expr "$line" : "[ 	]*not ok" >/dev/null
    then
	context_set count $(($(context_get count) + 1)) 
	testcount=$((testcount + 1))
	if [ "$(context_get plan)" = "" ]
	then
	    context_set test_before_plan yes
	else
	    context_set test_after_plan yes
	fi
	if expr "$line" : "[^#]* # *SKIP" >/dev/null || expr "$line" : "[^#]* # *skip" >/dev/null
	then
	    ship_char "${SKIP}"
	    state=plaintext
	    skipcount=$((skipcount + 1))
	    if expr "$line" : "[^#]* #[^ ]" >/dev/null
	    then
		ship_line "Suspicious comment leader at lime ${ln}"
	    fi
	    continue
	fi
	if expr "$line" : "[^#]* # *TODO" >/dev/null || expr "$line" : "[^#]* # *todo" >/dev/null
	then
	    ship_char ${TODO_NOT_OK}
	    state=plaintext
	    todocount=$((todocount + 1))
	    if expr "$line" : "[^#]* #[^ ]" >/dev/null
	    then
		ship_line "Suspicious comment leader at line ${ln}"
	    fi
	    continue
	fi
	ship_char "${FAIL}"
	ship_line "$line"
	state=plaintext
	failcount=$((failcount + 1))
	status=1
	if [ "$(context_get bail)" = yes ]
	then
	    ship_line "Bailing out on line ${ln} due to +bail pragma."
	    break
	fi
	continue
    fi
    # Process a TAP 14 pragma
    if expr "$line" : "pragma" >/dev/null
    then
	unset IFS
	# shellcheck disable=SC2086
	set -- $line
	case "$2" in
	    +bail) context_set bail yes;;
	    -bail) context_set bail yes;;
	    +strict) context_set strict yes;;
	    -strict) context_set strict yes;;
	    *) ship_line "Pragma '$line' ignored";;
	esac
	IFS=""
	continue
    fi
    # shellcheck disable=SC2166
    if [ "${state}" = "yaml" ]
    then
	ship_line "$line"
	if expr "$line" : '[ 	]*\.\.\.' >/dev/null
	then
	    state=plaintext
	else
	    continue
	fi
    elif expr "$line" : "[ 	]*---" >/dev/null
    then
	ship_line "$line"
	state=yaml
	continue
    fi
    # Ignore blank lines and comments
    if [ -z "$line" ] || expr "$line" : '[ 	]+$' >/dev/null || expr "$line" : "#" >/dev/null
    then
	continue
    fi
    # Any line that is not a valid plan, test result, pragma,
    # or comment lands here.
    if [ "$(context_get strict)" = yes ]
    then
	ship_line "Bailing out on line ${ln} due to +strict pragma"
	status=1
	break
    fi
done

/bin/echo ""

depth=0
context_pop

report="${report}${testcount} tests, ${failcount} failures"
if [ "$todocount" != 0 ]
then
    report="${report}, ${todocount} TODOs"
fi
if [ "$skipcount" != 0 ]
then
    report="${report}, ${skipcount} SKIPs"
fi

echo "${report}."

exit "${status}"

# end
