#[allow(dead_code)]
pub fn type_of<T>(_: &T) -> String {
    format!("{}", std::any::type_name::<T>())
}

#[allow(dead_code)]
pub unsafe fn to_mut<T>(t: &T) -> &mut T {
    #[allow(mutable_transmutes)]
    std::mem::transmute::<&T, &mut T>(t)
}

#[macro_export]
macro_rules! print_red {
    () => { };
    ($($arg:tt)*) => {{
        print!("\x1b[1;31m");
        print!($($arg)*);
        print!("\x1b[0m\n");
    }};
}

#[macro_export]
macro_rules! print_grn {
    () => { };
    ($($arg:tt)*) => {{
        print!("\x1b[1;32m");
        print!($($arg)*);
        print!("\x1b[0m\n");
    }};
}

#[macro_export]
macro_rules! print_yel {
    () => { };
    ($($arg:tt)*) => {{
        print!("\x1b[1;33m");
        print!($($arg)*);
        print!("\x1b[0m\n");
    }};
}

#[macro_export]
macro_rules! print_blu {
    () => { };
    ($($arg:tt)*) => {{
        print!("\x1b[1;34m");
        print!($($arg)*);
        print!("\x1b[0m\n");
    }};
}

pub use print_red;
pub use print_grn;
pub use print_yel;
pub use print_blu;
