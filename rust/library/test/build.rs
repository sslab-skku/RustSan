fn main() {
    println!("cargo:rustc-link-search=native=/home/kyuwoncho18/sslab/RustSan/llvm-project/build/lib/clang/13.0.0/lib/x86_64-unknown-linux-gnu/");
    println!("cargo:rustc-link-lib=static=clang_rt.asan");
}