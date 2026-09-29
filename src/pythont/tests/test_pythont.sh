#!/usr/bin/env bash
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/pythont.c"
BIN="$ROOT/test-state/pythont-suite"
TMP="$ROOT/test-state/suite"
mkdir -p "$TMP"

pass=0
fail=0

ok() { printf '[PASS] %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '[FAIL] %s\n' "$1"; fail=$((fail + 1)); }

run_eq() {
    name="$1"; expected="$2"; shift 2
    out="$("$@" 2>"$TMP/stderr"; printf '\001%s' "$?")"
    rc="${out##*$'\001'}"
    out="${out%$'\001'*}"
    out="${out%$'\n'}"
    if [ "$rc" -eq 0 ] && [ "$out" = "$expected" ]; then ok "$name"; else bad "$name (rc=$rc, out=$(printf '%q' "$out"))"; fi
}

run_contains() {
    name="$1"; needle="$2"; shift 2
    out="$("$@" 2>"$TMP/stderr"; printf '\001%s' "$?")"
    rc="${out##*$'\001'}"
    out="${out%$'\001'*}"
    out="${out%$'\n'}"
    if [ "$rc" -eq 0 ] && [[ "$out" == *"$needle"* ]]; then ok "$name"; else bad "$name (rc=$rc, missing=$(printf '%q' "$needle"))"; fi
}

printf '== build ==\n'
if cc -std=c11 -Wall -Wextra -Wpedantic -O2 "$SRC" -o "$BIN" 2>"$TMP/compile.err"; then
    ok 'compile'
else
    bad 'compile'
    cat "$TMP/compile.err"
    exit 1
fi

printf '\n== cli ==\n'
run_contains 'help' 'pythont 1.0-release' "$BIN" --help
run_eq 'execute expression' '3' "$BIN" -e 'print(1 + 2)'
run_eq 'multiple -e statements' $'0\n2' "$BIN" -e 'x = 0; print(x); x = x + 2; print(x)'
run_eq 'string containing semicolon' 'a;b' "$BIN" -e 'print("a;b")'

printf '\n== language ==\n'
cat > "$TMP/if.py" <<'PY'
x = 2
if x == 1:
    print(10)
elif x == 2:
    print(20)
else:
    print(30)
PY
run_eq 'if/elif/else' '20' "$BIN" "$TMP/if.py"

cat > "$TMP/while.py" <<'PY'
x = 0
while x < 3:
    print(x)
    x = x + 1
PY
run_eq 'while' $'0\n1\n2' "$BIN" "$TMP/while.py"

cat > "$TMP/def.py" <<'PY'
def add(a, b):
    return a + b
print(add(2, 3))
PY
run_eq 'function/return' '5' "$BIN" "$TMP/def.py"

run_eq 'string' 'hello' "$BIN" -e 's = "hello"; print(s)'
run_eq 'list' '[1, 2, 3]' "$BIN" -e 'a = [1,2,3]; print(a)'
run_eq 'dict' "{'x': 10}" "$BIN" -e 'd = {"x": 10}; print(d)'
run_eq 'native list operations' $'[1, 9, 3, 7]\n4\n9' "$BIN" -e 'a = [1,2,3]; a[1] = 9; a.append(7); print(a); print(len(a)); print(a[1])'
run_eq 'native dict operations' "{'x': 10}"$'\n10\n1' "$BIN" -e 'd = {"x": 10}; print(d); print(d["x"]); print(len(d))'
run_eq 'native float list' $'[1.5, 2.5]\n3.5\n[3.5, 2.5, 4.5]' "$BIN" -e 'a = [1.5,2.5]; print(a); a[0] = 3.5; print(a[0]); a.append(4.5); print(a)'
run_eq 'native string list' $'[\'x\', \'y\']\nz\n[\'x\', \'z\', \'w\']' "$BIN" -e 'a = ["x","y"]; print(a); a[1] = "z"; print(a[1]); a.append("w"); print(a)'
run_eq 'native bool list' $'[True, False]\nTrue' "$BIN" -e 'a = [True,False]; print(a); a[1] = True; print(a[1])'
expected_float_dict="{'x': 1.5, 'y': 2.5}"$'\n3.5'
run_eq 'native float dict' "$expected_float_dict" "$BIN" -e 'd = {"x":1.5,"y":2.5}; print(d); d["x"] = 3.5; print(d["x"])'
run_eq 'native negative list index' '3' "$BIN" -e 'a = [1,2,3]; print(a[-1])'
run_eq 'native list in control flow' '[1, 2]' "$BIN" -e $'a = [1,2]\nif a[0] == 1:\n    print(a)'
if "$BIN" -e 'a=[1]; a[0]="x"' >"$TMP/out" 2>"$TMP/stderr"; then
    bad 'incompatible container mutation rejects'
else
    if grep -q 'incompatible element type' "$TMP/stderr"; then ok 'incompatible container mutation rejects'; else bad 'incompatible container mutation rejects (wrong diagnostic)'; fi
fi
if "$BIN" -e 'a=[1]; print(a[2])' >"$TMP/out" 2>"$TMP/stderr"; then
    bad 'list bounds check'
else
    if grep -q 'list index out of range' "$TMP/stderr"; then ok 'list bounds check'; else bad 'list bounds check (wrong diagnostic)'; fi
fi

printf '\n== frontend ==\n'
run_contains 'tokens' 'INDENT' "$BIN" --tokens "$TMP/if.py"
run_contains 'ast typed integer' '<int>' "$BIN" --ast -e 'x = 1 + 2'
run_contains 'ast typed float' '<float>' "$BIN" --ast -e 'x = 3.5'
run_contains 'ast typed string' '<str>' "$BIN" --ast -e 'x = "hello"'
run_contains 'ast typed list element' '<int>' "$BIN" --ast -e 'x = [1,2]'
run_contains 'native list emit' 'py_ast_list_t x' "$BIN" --emit-c -e 'x = [1,2]'
run_contains 'native dict emit' 'py_ast_dict_t d' "$BIN" --emit-c -e 'd = {"x": 1}'
run_contains 'native tagged value emit' 'py_ast_value_t' "$BIN" --emit-c -e 'a = [1.5,2.5]'
run_contains 'emit C' 'int main' "$BIN" --emit-c "$TMP/def.py"

printf '\n== legacy ==\n'
run_eq 'forced legacy' '42' "$BIN" --legacy -e 'print(42)'

printf '\n== container safety ==\n'
if "$BIN" -e 'a = [1,2]; a[0] = "x"; print(a)' >"$TMP/out" 2>"$TMP/stderr"; then
    bad 'reject incompatible list mutation'
else
    if grep -q 'incompatible element type' "$TMP/stderr"; then ok 'reject incompatible list mutation'; else bad 'reject incompatible list mutation (wrong diagnostic)'; fi
fi
if "$BIN" -e 'd = {"x": 1}; print(d["missing"])' >"$TMP/out" 2>"$TMP/stderr"; then
    bad 'reject missing dict key'
else
    if grep -q 'dict key not found' "$TMP/stderr"; then ok 'reject missing dict key'; else bad 'reject missing dict key (wrong diagnostic)'; fi
fi

printf '\n== malformed input ==\n'
if "$BIN" --ast -e 'if :' >"$TMP/out" 2>"$TMP/stderr"; then
    bad 'malformed AST rejects'
else
    if grep -q 'AST parse error' "$TMP/stderr"; then ok 'malformed AST rejects'; else bad 'malformed AST rejects (wrong diagnostic)'; fi
fi

printf '\nTOTAL: %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
