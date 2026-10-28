# MK09 の Pascal コンパイラ

**版: 1.13** (2026-10-28)

MK09 の上で動く Pascal コンパイラ。C コンパイラ ([IR.md](IR.md)) と同じ**中間表現 (IR)** に出し、`cg` と `as` とライブラリ (`/LIB/CLIB.S`) を共用する。

```
 foo.pas ──► pas1 ──► foo.ir ──► cg ──► foo.s ──► as ──► foo.mx
```

* `pc [-S] [-k] [-v] [-o out] file.pas` がドライバ (`cc` と同じ作り: 段は PM_EXEC でつながる)。
* `pas1` は 1 パスで、木を作らずに式を読みながら IR を出す (IR がスタックマシンなので、Pascal の式がそのまま後置になる)。名前は大文字小文字を区別しない (15 文字まで)。

## 1. 言語 (1.12)

標準 Pascal の中核:

* **宣言**: `const` (整数、文字、文字列)、`type`、`var`、`procedure` / `function` (入れ子、`forward`、`var` 引数)、`label` (読み飛ばす)。
* **型**: `integer` (16 ビット、`maxint` = 32767)、`longint` (32 ビット)、`real` (IEEE 単精度。`single` `double` `extended` も同じ)、`char`、`boolean`、`byte`、列挙、部分範囲、`array` (多次元)、`record`、`^` (ポインタ。あとで宣言する型でもよい)。
* **文**: 代入、手続き呼び出し、`begin end`、`if`、`while`、`repeat`、`for` (`to` `downto`)、`case` (範囲 `a..b` と `else`)、`with`。
* **式**: `+ - * div mod and or not xor shl shr`、関係演算子。`and` `or` は**短絡評価** (Turbo Pascal と同じ)。`$FF` と `#13` と `'a''b'` が使える。
* **組み込み**: `write writeln read readln` (`:幅` と、実数は `:幅:桁`)、`trunc round sqrt sin cos arctan ln exp pi int frac` (`trunc` と `round` は `longint` を返す)、`new dispose halt inc dec abs sqr odd ord chr succ pred eof eoln`。
* コメントは `{ }` `(* *)` `//`。

## 2. 作りの要点

* **呼び出し**: 引数は**左から**積む (C は右から)。j 番目 (全部で n 個) は `paddr n-1-j`。`var` 引数と、配列・レコードの値引数はアドレスを渡す (値引数は呼ばれた側が枠に写す)。
  返り値は 16 ビットの値 (`ret`)。
* **入れ子の手続き**: 外側の手続きの枠のアドレスを**静的リンク**として最初の引数にし、呼ばれた側が自分の枠の先頭 (オフセット 0) に保つ。外側の変数は
  リンクをたどって (`laddr 0; load.w ...`) 読む。外側の手続きの**引数**が内側から使われるときは、外側がそれを枠に写す (先頭で)。
* **大域変数**は `.bss v名前`。手続きは `P<n>`、主プログラムは `main`。文字列は `S<n>`。
* **ランタイム**は `lib/cc/clib.c` の `__pw*` `__pr*` `__peof` `__peoln` `__pstr` (引数の順は積む順)。

## 3. real と longint (1.13)

C の `float` と `long` と同じ作り ([DESIGN.md](DESIGN.md) 17.3、17.4): **4 バイトのメモリの対象**で、式の値は常にそのアドレス (変数か枠の一時変数)、演算は
ライブラリ関数 (`__fadd(d, b, a)` ...)。`integer` と混ぜると広い方に直す (`integer` → `longint` → `real`)。`/` は常に `real`。`longint` に `real` は入らない (`trunc` `round`)。
`longint` を `integer` に入れると下位 16 ビット。**実数の定数は、実行時に `atof` で変換する** (コンパイラを小さくするため: `pas1` は TPA にぎりぎり入る)。
`write(x)` は `' 1.234568E+01'` の形、`write(x:w:d)` は固定小数点。`sin` などはライブラリ (`lib/cc/clib.c` の `__psin` ...) の級数で、6 桁ほどの精度。

## 4. 限界

`set`、`file`、`goto`、手続き型、可変レコード、`string` はまだない。範囲検査もない。
