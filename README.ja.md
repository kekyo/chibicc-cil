# chibicc-cil: A Small C Compiler on the CIL/.NET

[English language](README.md)

これは、Rui Ueyama氏の [chibicc: A Small C Compiler](https://github.com/rui314/chibicc) を.NET環境に移植するプロジェクトのCコンパイラリポジトリです。

## 詳細

chibiccはC言語処理系ですが、これを.NETに移植する際には「Cランタイムライブラリ」が必要です。しかし、.NETには標準的な実装がありません。したがって、chibicc自身でCランタイムライブラリをビルドする必要があります。このプロセスを簡単に実現するために、 [メタビルドスクリプト](https://github.com/kekyo/chibicc-cil-build) を用意しています。実際に動作させるには、まずはこのスクリプトを参照する事をおすすめします。

このプロジェクトは、chibiccの「ステップバイステップでC言語処理系を実装する」を踏襲して、できるだけコミット単位を一致させながら、CIL (.NET bytecode)を出力するように移植するプロジェクトです。一部、.NETのみの都合で実装したコミットについては、「.NET Related」というコミットメッセージで区別出来るようにしています。

初回コミットから中盤のステージ2コンパイラをビルドする手前までは、 [YouTubeのチャンネルで移植作業を公開しています](https://www.youtube.com/playlist?list=PLL43LzwbRhvRL2PkpewoRv0AFVobTtZGt) 。ただ、生収録のため、解説に向いた動画ではありません。chibiccの移植に取り組みたい方には参考になるかも知れません。

## ご注意

現状で、あと10コミット程でchibicc移植達成という状態になっていますが、移植者はここで移植を断念しています。
理由については、以下の場で登壇して解説しています:

* [NGK2025S (名古屋合同懇親会2025)](https://ngk.connpass.com/event/334796/) (LT登壇なので、解説と言えるほどの内容はありません)
* [第9回 Center CLR 勉強会](https://centerclr.connpass.com/event/341192/)

また、 [YouTubeのchibicc移植リスト](https://www.youtube.com/playlist?list=PLL43LzwbRhvRL2PkpewoRv0AFVobTtZGt) でも公開するかも知れません（未定です）。

より詳しくは、各サブモジュール先のリポジトリを参照してください。
大きな残件は、メタビルドリポジトリのディスカッションに書き残してあります。

このプロジェクトに興味を持ってくれてありがとう！ 私はしばらくこのプロジェクトを更新する予定はありません。issueやPRを投げてもらっても構いませんが、修正依頼については反応が無いかも知れません。

もちろん、フォークして弄るのは自由です。少なくともプロジェクトは全体的にMIT配下にあります。

----

## オリジナルのREADME

[英語のREADME](README.md) を参照してください。
