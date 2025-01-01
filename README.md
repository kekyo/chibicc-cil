# chibicc-cil: A Small C Compiler on the CIL/.NET

[![Japanese language](../chibicc-cil-toolchain/Images/Japanese.256.png)](README.ja.md)

This is the C compiler repository for the project to port Rui Ueyama's [chibicc: A Small C Compiler](https://github.com/rui314/chibicc) to the .NET environment.

## Details

Although chibicc is a C language processor, porting it to .NET requires a "C runtime library". But there is no standard implementation in .NET.
Therefore, it is necessary to build the C runtime library in chibicc itself.
To simplify this process, I have prepared a [meta-build script](https://github.com/kekyo/chibicc-cil-build).
To actually get it working, I recommend that you refer to this script first.

This project follows chibicc's "Step-by-step implementation of C language compiler" to port CIL (.NET bytecode) output while matching the commit unit as much as possible. Some commits implemented for .NET-only reasons can be distinguished by the commit message ".NET Related".

From the initial commit to just before I build the "Stage 2 compiler", [I am showing the porting process on my YouTube channel (In japanese)](https://www.youtube.com/playlist?list=PLL43LzwbRhvRL2PkpewoRv0AFVobTtZGt). However, because it is not edited, the video is not suitable for commentary; it may be helpful for those who want to tackle the porting of chibicc.

## Note

At present, I am about 10 commits away from achieving chibicc porting, but the porting person has abandoned the porting at this point.

* [NGK2025S (Nagoya Joint Reception 2025)](https://ngk.connpass.com/event/334796/) (In japanese)
* [The 9th Center CLR Study Group](https://centerclr.connpass.com/event/341192/) (In japanese)

It may also be available on [YouTube Center CLR channel, chibicc porting list](https://www.youtube.com/playlist?list=PLL43LzwbRhvRL2PkpewoRv0AFVobTtZGt) (TBD / In japanese).

For more details, please refer to the repository of each submodule destination.
The major remaining issues are left for discussion in the meta-bild repository.

Thanks for your interest in this project! I will not be updating this project for a while; you are welcome to throw out issues and PRs, but there may be no response regarding requests for fixes.

Of course, you are free to fork and play around with it. At least the project is under MIT distribution as a whole.

----

## The following is the content of the original README

Since we have not proceeded to the last commit, this section contains only one line.
See [Ueyama's README for the final chibicc description](https://github.com/rui314/chibicc).

This is the reference implementation of https://www.sigbus.info/compilerbook.
