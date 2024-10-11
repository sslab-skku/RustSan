use std::env;

fn main() {
    let llvm_dir = env::var("LLVM_DIR")
    .expect("Please set the LLVM_DIR environment variable");

    println!("cargo:rustc-link-search=native={}/lib/clang/13.0.0/lib/linux/", llvm_dir);
    println!("cargo:rustc-link-lib=static=clang_rt.asan");
}

