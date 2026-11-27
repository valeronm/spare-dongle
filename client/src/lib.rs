// `concat!` takes only literals, so the tool name in the protocol's messages cannot be a `const`.
macro_rules! tool_name {
    () => {
        "spare-dongle"
    };
}

pub const TOOL: &str = tool_name!();

/// Declares a fieldless enum and its `ALL` from one list, so `ALL` holds every variant.
#[macro_export]
macro_rules! with_all {
    (
        $(#[$meta:meta])*
        $vis:vis enum $name:ident { $($(#[$variant_meta:meta])* $variant:ident),+ $(,)? }
    ) => {
        $(#[$meta])*
        $vis enum $name {
            $($(#[$variant_meta])* $variant),+
        }

        impl $name {
            pub const ALL: [Self; [$(stringify!($variant)),+].len()] = [$(Self::$variant),+];
        }
    };
}

pub mod device;
pub mod protocol;
pub mod report;
pub mod words;
