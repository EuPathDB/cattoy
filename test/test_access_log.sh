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

TABLE=access_log
TESTLOG=test_access_log
source "$TESTDIR/functions.sh"


echo
echo '########################################################'
echo '#           access_log tests                            #'
echo '########################################################'
echo 

CMD="sqlite3 -init init-access-test"

expected="$(wc -l < "$TESTLOG")"
actual="$(echo "select * from $TABLE;" | $CMD | wc -l)"

echo -n "Checking all $expected rows returned: "
[[ $expected -eq $actual ]] && OK || error "Expected $expected, found $actual"

#sed -n -e 1p "$TESTLOG"

####################################
# ROW 1
# Testing:
#   - epoch calculation when Daylight Saving Time is in effect
#   - referer
####################################
declare -A columns
rowid=1
m='Oct'
d='11'
y='2014'
t='00:26:42'
z='-0400'
time_str="$d/$m/$y:$t $z"
time_epoch="$(date --date "$m $d $t $y" +%s)"
remote_host='192.168.210.200'
remote_host_int="$(echo "$remote_host" | tr . '\n' | awk '{s = s*256 + $1} END{print s}')"
columns=(
  [remote_host]="$remote_host"
  [remote_user]="-"
  [time]="$time_str"
  [request]="GET /cgi-bin/dataPlotter.pl?type=Microarray::TwoChannel&project_id=FooDB&dataset=linfJPCM5_microarrayExpression_GSE13983_Papadoupou_Amastigote_RSRC&template=1&fmt=png&id=LinJ.33.2740&vp=_LEGEND,exprn_val HTTP/1.0"
  [status]="200"
  [bytes]="11790"
  [referer]="http://foodb.org/foodb/showRecord.do?name=GeneRecordClasses.GeneRecordClass&project_id=FooDB&source_id=LinJ.33.2740"
  [user_agent]="Mozilla/5.0 (Windows NT 5.1; rv:33.0) Gecko/20100101 Firefox/33.0"
  [response_time]="498425"
  [time_day]="11"
  [time_month]="10"
  [time_year]="2014"
  [time_hour]="0"
  [time_min]="26"
  [time_sec]="42"
  [time_epoch]="$time_epoch"
  [method]="GET"
  [url]="/cgi-bin/dataPlotter.pl?type=Microarray::TwoChannel&project_id=FooDB&dataset=linfJPCM5_microarrayExpression_GSE13983_Papadoupou_Amastigote_RSRC&template=1&fmt=png&id=LinJ.33.2740&vp=_LEGEND,exprn_val"
)
echo -n "Checking values of columns in row $rowid: "
for col in "${!columns[@]}"; do check_col_val  "$TABLE" "$rowid" "$col" "${columns[$col]}"; done
OK

####################################
# ROW 2
# Testing:
#   - no referer
#   - epoc calc of EST time
#   - 302 status
####################################
declare -A columns
rowid=2
m='Nov'
d='06'
y='2014'
t='10:26:29'
z='-0500'
time_str="$d/$m/$y:$t $z"
time_epoch="$(date --date "$m $d $t $y" +%s)"
remote_host='192.168.210.200'
remote_host_int="$(echo "$remote_host" | tr . '\n' | awk '{s = s*256 + $1} END{print s}')"
columns=(
  [remote_host]="$remote_host"
  [remote_user]="-"
  [time]="$time_str"
  [request]="GET / HTTP/1.1"
  [status]="302"
  [bytes]="300"
  [referer]="-"
  [user_agent]="Mozilla/5.0 (Macintosh; Intel Mac OS X 10_10) AppleWebKit/600.1.25 (KHTML, like Gecko) Version/8.0 Safari/600.1.25"
  [response_time]="144"
  [time_day]="6"
  [time_month]="11"
  [time_year]="2014"
  [time_hour]="10"
  [time_min]="26"
  [time_sec]="29"
  [time_epoch]="$time_epoch"
  [method]="GET"
  [url]="/"
)
echo -n "Checking values of columns in row $rowid: "
for col in "${!columns[@]}"; do check_col_val  "$TABLE"   "$rowid" "$col" "${columns[$col]}"; done
OK

####################################
# ROW 3
# Testing:
#   - POST
#   - status 500
#   - out of chronologic order
####################################
declare -A columns
rowid=3
m='Nov'
d='01'
y='2014'
t='10:24:20'
z='-0400'
time_str="$d/$m/$y:$t $z"
time_epoch="$(date --date "$m $d $t $y" +%s)"
remote_host='10.11.228.10'
remote_host_int="$(echo "$remote_host" | tr . '\n' | awk '{s = s*256 + $1} END{print s}')"
columns=(
  [remote_host]="$remote_host"
  [remote_user]="-"
  [time]="$time_str"
  [request]="POST /foodb/processRegister.do HTTP/1.0"
  [status]="500"
  [bytes]="79631"
  [referer]="http://foodb.org/foodb/showRegister.do"
  [user_agent]="Mozilla/5.0 (Windows NT 6.3; WOW64)"
  [response_time]="156228"
  [time_day]="1"
  [time_month]="11"
  [time_year]="2014"
  [time_hour]="10"
  [time_min]="24"
  [time_sec]="20"
  [time_epoch]="$time_epoch"
  [method]="POST"
  [url]="/foodb/processRegister.do"
)
echo -n "Checking values of columns in row $rowid: "
for col in "${!columns[@]}"; do check_col_val  "$TABLE"   "$rowid" "$col" "${columns[$col]}"; done
OK


####################################
# ROW 4
# Testing:
#   - HEAD (no bytes sent, represented as '-' in logs but '0' in vtable)
#   - no referer
####################################
declare -A columns
rowid=4
m='Nov'
d='01'
y='2014'
t='19:36:37'
z='-0400'
time_str="$d/$m/$y:$t $z"
time_epoch="$(date --date "$m $d $t $y" +%s)"
remote_host='10.11.220.128'
remote_host_int="$(echo "$remote_host" | tr . '\n' | awk '{s = s*256 + $1} END{print s}')"
columns=(
  [remote_host]="$remote_host"
  [remote_user]="-"
  [time]="$time_str"
  [request]="HEAD /common/downloads/Current_Release/CfasciculataCfCl/fasta/data//FooDB-8.1_CafsicculatafCCl_AnnottaedrPoteinsf.asta HTTP/1.0"
  [status]="200"
  [bytes]="0"
  [referer]="-"
  [user_agent]="curl/7.19.7 (x86_64-redhat-linux-gnu) libcurl/7.19.7 NSS/3.13.6.0 zlib/1.2.3 libidn/1.18 libssh2/1.4.2"
  [response_time]="702"
  [time_day]="1"
  [time_month]="11"
  [time_year]="2014"
  [time_hour]="19"
  [time_min]="36"
  [time_sec]="37"
  [time_epoch]="$time_epoch"
  [method]="HEAD"
  [url]="/common/downloads/Current_Release/CfasciculataCfCl/fasta/data//FooDB-8.1_CafsicculatafCCl_AnnottaedrPoteinsf.asta"
)
echo -n "Checking values of columns in row $rowid: "
for col in "${!columns[@]}"; do check_col_val  "$TABLE"   "$rowid" "$col" "${columns[$col]}"; done
OK

####################################
# ROW 5
# Testing:
#   - no user-agent
#   - with remote_user
####################################
declare -A columns
rowid=5
m='Nov'
d='06'
y='2014'
t='10:31:29'
z='-0500'
time_str="$d/$m/$y:$t $z"
time_epoch="$(date --date "$m $d $t $y" +%s)"
remote_host='127.1.1.1'
remote_host_int="$(echo "$remote_host" | tr . '\n' | awk '{s = s*256 + $1} END{print s}')"
columns=(
  [remote_host]="$remote_host"
  [remote_user]="goose"
  [time]="$time_str"
  [request]="GET / HTTP/1.1"
  [status]="302"
  [bytes]="300"
  [referer]="-"
  [user_agent]="-"
  [response_time]="171"
  [time_day]="6"
  [time_month]="11"
  [time_year]="2014"
  [time_hour]="10"
  [time_min]="31"
  [time_sec]="29"
  [time_epoch]="$time_epoch"
  [method]="GET"
  [url]="/"
)
echo -n "Checking values of columns in row $rowid: "
for col in "${!columns[@]}"; do check_col_val  "$TABLE"   "$rowid" "$col" "${columns[$col]}"; done
OK

ALLPASS
echo

exit;
