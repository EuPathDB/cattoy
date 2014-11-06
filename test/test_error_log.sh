#!/bin/sh
set -e

TESTDIR=$( readlink -f -- "$( dirname -- "$0" )" )

SRCDIR="$TESTDIR/.."
LIBDIR="$TESTDIR/.."
export LD_LIBRARY_PATH="$LIBDIR"

# echo Compiling
# cd "$SRCDIR"
# make clean
# make

cd "$TESTDIR"

source "$TESTDIR/functions.sh"

TABLE=error_log
TESTLOG=test_error_log
source "$TESTDIR/functions.sh"


echo
echo '########################################################'
echo '#           error_log tests                            #'
echo '########################################################'
echo 

CMD="sqlite3 -init init-error-test"

expected="$(wc -l < "$TESTLOG")"
actual="$(echo "select * from $TABLE;" | $CMD | wc -l)"

echo -n "Checking all $expected rows returned: "
[[ $expected -eq $actual ]] && OK || error "Expected $expected, found $actual"

#sed -n -e 1p "$TESTLOG"

####################################
# ROW 1
# Testing:
#   - epoch calculation when Daylight Saving Time is in effect
####################################
declare -A columns
rowid=1
time_str='Sat Oct 11 00:26:42 2014'
time_epoch="$(date --date "$time_str" +%s)"
remote_host='192.168.210.200'
remote_host_int="$(echo "$remote_host" | tr . '\n' | awk '{s = s*256 + $1} END{print s}')"
columns=(
  [time]="$time_str"
  [log_level]='error'
  [message]='which: no inkscape in (/sbin:/usr/sbin:/bin:/usr/bin)'
  [remote_host]="$remote_host"
  [remote_host_int]="$remote_host_int"
  [time_month]='10'
  [time_day]='11'
  [time_year]='2014'
  [time_hour]='0'
  [time_min]='26'
  [time_sec]='42'
  [time_epoch]="$time_epoch"
)
echo -n "Checking values of columns in row $rowid: "
for col in "${!columns[@]}"; do check_col_val  "$TABLE"   "$rowid" "$col" "${columns[$col]}"; done
OK

####################################
# ROW 2
# Testing:
#   - epoch calculation when Daylight Saving Time is not in effect
#   - parentheses in message
#   - no remote_host field
####################################
declare -A columns
rowid=2
time_str='Tue Nov 04 13:14:32 2014' # test EST
time_epoch="$(date --date "$time_str" +%s)"
remote_host=''
remote_host_int=''
columns=(
  [time]="$time_str"
  [log_level]='debug'
  [message]='proxy: worker already initialized'
#  [message]='proxy_util.c(1852): proxy: worker already initialized'
  [remote_host]="$remote_host"
  [remote_host_int]="$remote_host_int"
  [time_day]='4'
  [time_month]='11'
  [time_year]='2014'
  [time_hour]='13'
  [time_min]='14'
  [time_sec]='32'
  [time_epoch]="$time_epoch"
)
echo -n "Checking values of columns in row $rowid: "
for col in "${!columns[@]}"; do check_col_val  "$TABLE"  "$rowid" "$col" "${columns[$col]}"; done
OK

####################################
# ROW 3
# Testing:
#   - [] in message
####################################
declare -A columns
rowid=3
time_str='Wed Nov 05 05:27:07 2014' # test EST
time_epoch="$(date --date "$time_str" +%s)"
remote_host='10.15.20.200'
remote_host_int="$(echo "$remote_host" | tr . '\n' | awk '{s = s*256 + $1} END{print s}')"
columns=(
  [time]="$time_str"
  [log_level]='error'
  [message]='[Tue Nov  4 00:27:07 2014] gbrowse: DBD::Oracle::db ping failed: ORA-03135: connection lost contact (DBD ERROR: OCISessionServerRelease) at /usr/share/perl5/vendor_perl/CGI/Session/Driver/DBI.pm line 136 during global destruction., referer: http://integrate.foodb.org/cgi-bin/gbrowse/foodb/'
  [remote_host]="$remote_host"
  [remote_host_int]="$remote_host_int"
  [time_day]='5'
  [time_month]='11'
  [time_year]='2014'
  [time_hour]='5'
  [time_min]='27'
  [time_sec]='7'
  [time_epoch]="$time_epoch"
)
echo -n "Checking values of columns in row $rowid: "
for col in "${!columns[@]}"; do check_col_val  "$TABLE"  "$rowid" "$col" "${columns[$col]}"; done
OK

ALLPASS
echo

exit;
