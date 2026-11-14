# MK09 for the FM-11 (BOARD=fm11)

MK09 (MC6809 マイクロカーネル + TTY/DISK/FS/PM/MM サーバ) を富士通 FM-11 (6809 モード) へ移植したものです。
元: <https://github.com/kaya-z/sim09/tree/master/mk09> (v1.35 に取り込み済み)。sbc09 ボード向けのビルドは変更なし (`BOARD` 省略時は従来どおり、`build/` が同一バイト列になることを確認済み)。

## Level 1 と Level 2

| | Level 1 | Level 2 (既定) |
|---|---|---|
| メモリ | 64KB フラット、MMU なし | 128KB、MMU あり |
| プログラム領域 | 145 ページ (37KB) を全員で共有 | **プログラムごとに自分のアドレス空間**: `$2000-$EFFF` の 52KB |
| 動かないもの | cc / cc1 / cg / pc / pas1 (160・149・154 ページ必要) は**イメージに入れない** (`tools/fitapps.py` が自動判定) | すべて動く (cc1 の 160 ページも) |
| BASIC / jwp | BASIC は数学関数なし・プログラム 1200 バイト、jwp は 7300 バイト | フル版 |
| バージョン表示 | `MK09 1.37 (for FM-11)`、バナー `Level 1: 64KB flat, no MMU` | `Level 2: 128KB, MMU` |

どちらもこのツリーに残してあり、`LEVEL=1` / `LEVEL=2` で作り分けます (`BOARD=fm11` の既定は 2)。ビルド先は `build-fm11-l1`、`build-fm11-l2` (2HD 版は `-2hd` が付く)。
FM-11 専用のコマンド (`format` `mount` `umount` `mem`) は `-v` でバージョンを出し、`(for FM-11)` が付きます。

### Level 2 の仕組み (MMU: `$FD80-$FD93`)

* **マップ**: MMR は 16 組 (タスクごとに `$FD90` で切り替え)、各組が論理ページ 16 枚 (4KB) を物理ページ (最大 1MB、実装は 128KB) へ写します。タスク番号 = マップの組の番号です。
  アイドルとサーバ (タスク 0-5) は**恒等写像**で、Level 1 と同じ 64KB を見ます。プログラム (タスク 6-10) はページ 0・1 (カーネル) が共通で、ページ 2-14 は MM が渡す私有の物理ページです。
* **カーネル** (`arch/m6809/kernel.s`、`MMU` 条件アセンブル): トラップの入口でタスクのフレームを TCB に退避し (`getframe`)、再開時にタスクのマップに切り替えてスタックへ書き戻します (`dispatch`)。
  他タスクのメモリは直接触れないので、受信者へのメッセージは TCB に置いて再開時に渡し (`T_RMSG` / `T_PEND`)、`wake` は TCB のフレームを書き換えます。
  サーバとクライアントの間のデータ (`mk_copyfrom` / `mk_copyto` / `mk_tload`) は `$7C00` の 1KB ウィンドウ (`$FD93` bit6、`$FD91/$FD92`) で相手の物理メモリを 256 バイトずつ読み書きします (`xcopy`)。
* **メモリマップ** (`memmap_fm11_l2.mk`): カーネルのコードとデータ (TCB、スタック、ウィンドウのバッファ、コンソールのキーボードのリング) をページ 0・1 に置き、サーバのコードの位置は変えません。
  プログラムに渡す物理ページは、システムが使わない 24 ページ (5-12、16-28)。ページ 29-31 は `format` のトラックバッファ (ディスクサーバの論理 `$9000`)。
* **MM** (`servers/mm2.c`): 4KB ページの空き管理。PM に物理ページの番号の一覧を返します。**PM** はそれをカーネルの `mk_tcreate` に渡します (`tcreate_t.map`)。
* プログラムは `$2000` にリンクします (`APPLINK`)。MX の再配置はそのまま使えます。
* DMA は物理アドレスなので、ディスクサーバのバッファ (恒等写像) はそのまま使えます。`format` のトラックイメージはディスクサーバが自分のバッファで作ります (Level 1 は `format` が作って渡す)。

## ビルドと実行

```sh
tools/install_cmoc.sh $HOME/cmoc           # CMOC + lwtools (一度だけ。リポジトリの ../tools にある)
make BOARD=fm11 CMOC_BIN=$HOME/cmoc/bin                              # Level 2、HDD 版 (フル)   -> build-fm11-l2/mk09.img
make BOARD=fm11 LEVEL=1 CMOC_BIN=$HOME/cmoc/bin                      # Level 1、HDD 版          -> build-fm11-l1/mk09.img
make BOARD=fm11 BOOTDEV=2hd CMOC_BIN=$HOME/cmoc/bin                  # Level 2、2HD 版 (縮小)   -> build-fm11-l2-2hd/mk09_2hd.img
make BOARD=fm11 LEVEL=1 BOOTDEV=2hd CMOC_BIN=$HOME/cmoc/bin          # Level 1、2HD 版          -> build-fm11-l1-2hd/mk09_2hd.img
sh fm11/test_fm11.sh [path/to/fm11] ["1 2"]                          # エミュレータ (Fm11gtk) でのヘッドレス試験 (Level 1・2)
```

エミュレータ (`emulator/Fm11gtk`) での起動:

```sh
./fm11 -h0 mk09_fm11_l2_hdd.img --dipsw 0x7F      # HDD ブート (DIPSW: HDD のみ ON)
./fm11 -f0 mk09_fm11_l2_2hd.img --dipsw 0xBF      # 2HD ブート (8インチ/2HD ドライブ 0)
```

HDD イメージは 20MB (M2235B: 315 シリンダ x 8 ヘッド x 32 レコード x 256B) に合わせてあります (`dist/` のものは 20,643,840 バイトに伸ばしてある)。
2HD イメージは 1,021,696 バイト (シミュレータの FDD_2HD_256 形式: トラック 0 サイド 0 が 128B x 26、残りが 256B x 26)。

## メモリマップ (Level 1: `memmap_fm11.mk`。Level 2 は上の節と `memmap_fm11_l2.mk`)

| 範囲 | 内容 |
|---|---|
| `$0100-$010F` | 割り込みベクタのジャンプテーブル (カーネルが作る。`$FFF2-$FFFD` は RAM なのでカーネルがそこへ書く) |
| `$0110-$02FF` | DISK / MM サーバのデータ (`$0200-$02FF` は IPL のセクタバッファ) |
| `$0400-$05FF` | IPL (FM-11 のブート ROM が読み込む場所。起動後は未使用) |
| `$0600-` | システムコード (カーネル、TTY、DISK、FS) |
| `$4E00-$DFBF` | プログラム領域 (Level 1: 37,312 バイト = 145 ページ。sbc09 は 164 ページ) |
| `$DFC0-$FB9E` | サーバのデータ、PM/MM のコード |
| `$FC00-$FFFF` | ハードウェア: `$FC80-$FCFF` はサブ CPU との共有メモリ、`$FD00-$FDFF` は I/O |

MMU は使わず (`$FD93` bit7 = 0)、ブート ROM の `$5000-$5FFF` オーバレイは IPL が `$FD93` bit0 = 1 で切ります。

## 使うハードウェア

* **コンソール出力** = 表示サブシステム。`$FD05` (bit7 = HALT/BUSY) でサブ CPU を止め、`$FC80` の共有メモリに `PUT STRING` (コマンド `$03`) を書いて解放します (`bsp/fm11/console.c`、TTY サーバが 1 回の待ちごとにまとめて送る)。
* **キー入力** = `$FD00/$FD01` (16 ビットのキーコード、`$FD01` が ASCII)。キー割り込み (`$FD02` bit7、`$FD03` bit6) をカーネルが `rxdrain` で読みます。BREAK キーは `^C` になります。
* **ティック** = MC6840 PTM のタイマ 2 (`$FD38-$FD3F`)、連続モード、内部クロック (E)、40000 カウント = 50Hz。割り込みマスク `$FD02` bit4。
* **HDD** = MDC (`$FDC0-$FDC3`) をポーリング、データは DMA チャネル 2 (`bsp/fm11/mdc.c`)。CHR アドレス (32 レコード x 8 ヘッド)、256 バイト/セクタ。OS-9 ドライバと同じ手順で、割り込みは使わない。
* **2HD** = MB8877 (`$FD30-$FD37`) をポーリング、DMA チャネル 1 (`bsp/fm11/fdc.c`)。ブート ROM (`$5516...`) と同じ手順 (seek `$1A`、read `$80`、write `$A0`、`$FD37` bit6 を待つ)。
  トラック 0 サイド 0 (128B) を飛ばし、LSN 0 = トラック 0 サイド 1 セクタ 1 として 256B セクタを順に並べる (3978 セクタ = 994KB)。
* **ブート**: ブート ROM は HDD ならシリンダ 0 ヘッド 0 のレコード 0-1 (512B)、2HD ならトラック 0 サイド 0 のセクタ 1-4 (128B x 4) を `$0400` に読んで実行します。
  `boot/ipl_fm11_hdd.s` (MBR の先頭 446 バイト。`tools/mkdisk.py --ipl`) と `boot/ipl_fm11_2hd.s` がそこで動き、セクタ 2 以降のブートイメージ (sbc09 と同じレコード形式) を読んでカーネルへ飛びます。
* **reboot** = メモリを消して、ブート ROM のブートローダ入口 `$5003` へ (BREAK キー後と同じ。リセットベクタだとメモリテストが走る)。

## 変更したファイル

`arch/m6809/kernel.s` (`FM11` 条件アセンブル)、`servers/tty.c`、`servers/disk.c`、`servers/fs_core.c` (FM-11 のみ 4085 クラスタ未満の FAT16 を許可)、`apps/reboot.c`、
`Makefile` (`BOARD=fm11`、`BOOTDEV=2hd`)、`tools/genmap.py` (`TOP`)、`tools/mkdisk.py` (`--ipl --part-start --total-blocks --small`)、
新規: `bsp/fm11/`、`boot/ipl_fm11_*.s`、`memmap_fm11.mk`、`tools/mk2hd.py`、`fm11/`。

## format コマンド (フロッピーディスク)

`format [-y] ドライブ [ラベル]` : 2HD フロッピーを初期化します。ドライブ 0 はシステムディスクなので対象外 (HDD 版: 0 = HDD、1-4 = フロッピーのユニット 0-3。2HD 版: 0-3 = ユニット 0-3、1-3 を初期化可能)。
1. 全トラック (77 x 2 面) を物理フォーマット (MFM、256B x 26 セクタ。トラックイメージを `format` が作り、ディスクサーバの `DISK_FORMAT` が FDC の Write Track + DMA で書く)
2. MBR、FAT16 のブートセクタ、FAT 2 面、空のルートディレクトリを書く (1KB クラスタ x 973 = 973KB)
3. 全ブロックを読み戻して面を検査し、ブートセクタと FAT を確認する

起動用ディスクにはなりません (トラック 0 面 0 も 256B で作るので、ブート ROM が読む 128B x 26 の IPL トラックがない)。
**初期化したディスクは `mount` で使います** (下記)。
エミュレータでは空ファイルを `-f0` に渡すと (D88 として) 初期化でき、`fm11/test_fm11.sh` がこれを試験します。実機のライトトラックのタイミングやギャップは未確認です。

## mount / umount (フロッピーの第 2 ドライブ)

FS サーバは複数のボリュームを持てるようになりました (`servers/fs_core.c` の `fs_select`: ボリューム情報を入れ替え、そのときキャッシュを空にする。キャッシュは書き込み即時なので dirty ブロックはない)。

* `mount` : ドライブの一覧 (0 = システムディスク、1-4 = フロッピー) と、マウント中のボリュームの大きさ
* `mount N` : ドライブ N (1-4) のディスクをマウント。以後 `N:/DIR/FILE` でそのディスクのファイルを指せる (`ls 1:/`、`cp ETC/RC.SH 1:/R.TXT`、`cat 1:/D/F.TXT`、`mkdir`、`mv`、`rm`、`df 1` など。`N:` のない名前はドライブ 0)
* `umount N` : 解除 (そのドライブのファイルを開いたままだと `E_BUSY`)。ディスクの交換は umount のあとで

制限: カレントディレクトリはドライブ 0 のものだけ (`cd 1:/X` は不可、`1:` の名前は常にルートからの絶対パス)。`mv` / `cp` の一部として別ドライブへ移すのは `cp` で (`mv` は同じドライブの中だけ)。
プログラムの起動は `/bin` (ドライブ 0) から。FM-11 の FS 要求 `FS_MOUNT` (`include/proto.h`) と `FS_STATFS` の drive 引数を追加。

## C のサンプル (`/C`、Level 2 のイメージ)

`cc` が動くイメージ (Level 2、sbc09 ではなく FM-11 の HDD 版・2HD 版) には、ディレクトリ `/C` に C のサンプルが入っています (Level 1 は `cc` が動かないので入れていません)。
`cc /C/HELLO.C -o /HELLO.MX` で作って `/HELLO.MX` で走らせます。

`hello` (引数)、`fib` (再帰・ループ・long)、`primes` (ふるい)、`wc` (ファイル: fopen / fgets)、`fcopy` (fread / fwrite)、`strings` (文字列関数・ctype・sprintf)、
`list` (struct・malloc・連結リスト)、`sort` (関数ポインタ・qsort・rand)、`float` (浮動小数点: sqrt・pi・`%f %e %g`)、`README.TXT`。ソースは `disk/c/`、`fm11/test_csamples.sh` が 9 本すべてをシミュレータの中でコンパイルして走らせ、出力を確かめます。

## `mem` (Level 2)

物理メモリ (128KB = 4KB のページ 32 枚) の内訳 (システム / `format` のバッファ / プログラム: 使用中と空き)、物理ページのマップ (`K` カーネル、`S` サーバ、`F` format、`P` プログラム、`.` 空き: MM の `MM_MAP` で空きを取る)、
サーバの論理マップ、プログラムの論理マップを出します。

## 検証したこと / していないこと

エミュレータ `emulator/Fm11gtk` (6809 モード) でのみ確認しました。実機、Windows 版エミュレータ、SD ストレージボード (RTL) では**未確認**です。
`fm11/test_fm11.sh`: HDD ブート、2HD ブート、キー入力、ファイルのコピーと読み戻し (ディスク書き込み)、`reboot` で戻ること、`format` → `mount 1` → `cp` / `mkdir` / `mv` / `ls 1:/` / `df 1` → `umount 1`、が通ります。
`basic` (`PRINT 1+2*3`)、`ls`、`mem`、`ps`、`df`、`fstest` (HDD) も動きました。コンパイラ (`cc`)、Pascal、アセンブラ、`more`、`life` などは実行を試していません。

## 制限

* **日本語**: コンソールは **Shift_JIS** で、FM-11 のビルドは既定の文字コードを `sjis` にしています (`stty sjis`、jwp とそのメッセージ・SKK 辞書も Shift_JIS)。
  サブ CPU はコマンド `$37` (`[3]=$FF`) で漢字モードに入り、Shift_JIS の 2 バイト文字を漢字 ROM の字形としてグラフィック面へ描きます (F-BASIC は `$00` で切るため、デモの漢字はグラフィックに自前で描いている)。
  描いた漢字が見えるには、グラフィック表示 (`$FDA0` の bit2-3) とパレット (`$FD98-$FD9F`) を有効にしておく必要があります (`bsp_con_init`、TTY サーバの起動時)。
  これを忘れると字形は VRAM に書かれても黒で描かれて空白に見えました。jwp のメニュー (日本語) の表示を確認済み。UTF-8 / EUC は未対応。かな漢字変換の入力は未確認です。
* **jwp** (HDD 版): 起動し、画面は ANSI 変換 (下記) で描かれます。テキストバッファは 7300 バイトに縮小 (プログラム領域が 144 ページで、元の 164 ページ必要な構成が入らないため)。
* **ANSI エスケープ**: `bsp/fm11/console.c` が `ESC[r;cH` `ESC[J` `ESC[K` `ESC[nL` `ESC[nM` `ESC[...m` をサブ CPU のオーダ (`ESC = row col`、`ESC *`、`ESC T`、`ESC Y`、`ESC E`、`ESC R`、`ESC G attr`) に変換し、それ以外 (スクロール領域 `r` やカーソル相対移動) は捨てます。
  オーダは `subsys.rom` の逆アセンブルで確認したもの。`E`/`R`/`Y` の動作は ROM の読みだけで、画面での確認は `H` `K` `m` と状態行の表示までです。
* 日付は固定 (2026-01-01)。FM-11 本体側から読める時計は使っていません。
* **Level 1 のプログラム領域**は 144 ページ: **BASIC** は `NOMATH` (SIN COS TAN ATN EXP LOG SQR と非整数のべき乗なし)、プログラム 1200 バイト、配列 120 要素、スタック 1100 バイト、**jwp** のテキストバッファは 7300 バイトに縮めてあります。
  cc / cc1 / cg / pc / pas1 (C と Pascal のコンパイラ) は 160・149・154 ページ必要で入らないので、Level 1 のイメージには入れていません (**Level 2 ではすべて動く**)。
* 2HD 版の FAT16 ボリュームは 476 クラスタ (2KB クラスタ。FAT12 の大きさ) で、MK09 専用です (PC では読めない)。
* DIPSW のブート順は HDD > 2HD > 2D の順に試されます (ブート ROM 側の仕様)。FM-11 の割り込み (FIRQ = サブからのアテンション) は使っていません。
* HDD/FDC のドライバはポーリングです (SD ストレージボードでも割り込みなし、DMA は 256 バイトずつ)。
