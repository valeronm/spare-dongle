// `concat!` takes only literals, so the tool name in the protocol's messages cannot be a `const`.
macro_rules! tool_name {
    () => {
        "spare-dongle"
    };
}

pub const TOOL: &str = tool_name!();

pub mod device;
pub mod protocol;
pub mod words;
