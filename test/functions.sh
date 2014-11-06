function error() {
  local msg="$*"
  echo
  echo "FAIL: $msg"
  echo
  exit 1
}

function success() {
  echo -en "\\033[1;32m"; echo "$1"; echo -en "\\033[0;39m"
}

function ALLPASS() {
  success "All Tests Passed"
}

function OK() {
  success OK
}

function check_col_val() {
  local table="$1"
  local rowid="$2"
  local column="$3"
  local expected="$4"
  actual="$(echo "select $column from "$table" where rowid = "$rowid";" | $CMD)"
  [[ "$expected" == "$actual" ]] && echo -n "." || error "Value mismatch in '$col'. Expected '$expected', found '$actual'"
}
