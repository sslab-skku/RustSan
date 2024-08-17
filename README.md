# RustSan: Retrofitting AddressSanitizer for Efficient Sanitization of Rust

## How to use
0. Build all our dependencies
1. Build your own-program with RustSan flag and extract the IR(e.g., `RUSTFLAGS="--pta-filter --emit llvm-ir" cargo build`)
2. Input the whole-program IR in our analyzer
3. Build the output LLVM IR of analyzer with ASan, and test it.

### Or...
Just Use our cargo-rustsan (e.g., `RUSTSAN_DIR=$PWD cargo rustsan build`)!
It runs our analyzer on LTO-mode, so you don't need any manual stuff.
However, LTO itself is very heavy work, the compile time may be much bloater than manual stuff.
It's your own choice, and we recommend the manual stuff.


## Bibtex
```
@inproceedings {rustsan,
author = {Kyuwon Cho, Jongyoon Kim, Kha Dinh Duy, Hajeong Lim, and Hojoon Lee},
title = {RustSan: Retrofitting AddressSanitizer for Efficient Sanitization of Rust},
booktitle = {33rd USENIX Security Symposium (USENIX Security 24)},
year = {2024},
isbn = {978-1-939133-44-1},
address = {Philadelphia, PA},
pages = {3729--3746},
url = {https://www.usenix.org/conference/usenixsecurity24/presentation/cho-kyuwon},
publisher = {USENIX Association},
month = aug
}
```
