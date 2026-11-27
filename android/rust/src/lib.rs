mod descriptor;
mod usbfs;
mod view;

use descriptor::{is_diagnostic, report_descriptor_length};
use diagclient::device::Device;
use jni::JNIEnv;
use jni::objects::{JByteArray, JClass, JString};
use jni::sys::{JNI_FALSE, jboolean, jint, jlong, jstring};
use std::panic::{AssertUnwindSafe, catch_unwind};

type Dongle = Device<usbfs::Usbfs>;

#[unsafe(no_mangle)]
pub extern "system" fn Java_io_github_valeronm_sparedongle_Native_open(
    _: JNIEnv,
    _: JClass,
    fd: jint,
    in_ep: jint,
    out_ep: jint,
) -> jlong {
    let transport = usbfs::Usbfs::new(fd, in_ep as u8, out_ep as u8);
    Box::into_raw(Box::new(Dongle::open(transport))) as jlong
}

#[unsafe(no_mangle)]
pub extern "system" fn Java_io_github_valeronm_sparedongle_Native_readingJson(
    env: JNIEnv,
    _: JClass,
    handle: jlong,
) -> jstring {
    let dev = unsafe { &mut *(handle as *mut Dongle) };
    string_or_throw(env, || Ok(serde_json::to_string(&view::read(dev)?)?))
}

#[unsafe(no_mangle)]
pub extern "system" fn Java_io_github_valeronm_sparedongle_Native_reportText(
    env: JNIEnv,
    _: JClass,
    handle: jlong,
) -> jstring {
    let dev = unsafe { &mut *(handle as *mut Dongle) };
    string_or_throw(env, || view::report_text(dev))
}

#[unsafe(no_mangle)]
pub extern "system" fn Java_io_github_valeronm_sparedongle_Native_switchProtocol(
    env: JNIEnv,
    _: JClass,
    handle: jlong,
    code: JString,
) -> jboolean {
    let dev = unsafe { &mut *(handle as *mut Dongle) };
    or_throw(env, JNI_FALSE, "the protocol switch failed", |env| {
        let code: String = env.get_string(&code)?.into();
        let want = view::protocol_named(&code)
            .ok_or_else(|| anyhow::anyhow!("unknown protocol \"{code}\""))?;
        let (_, rebooting) = dev.protocol(Some(want))?;
        Ok(jboolean::from(rebooting))
    })
}

#[unsafe(no_mangle)]
pub extern "system" fn Java_io_github_valeronm_sparedongle_Native_isDiagnostic(
    env: JNIEnv,
    _: JClass,
    descriptor: JByteArray,
) -> jboolean {
    let checked = catch_unwind(AssertUnwindSafe(|| {
        env.convert_byte_array(&descriptor)
            .is_ok_and(|bytes| is_diagnostic(&bytes))
    }));
    checked.map_or(JNI_FALSE, jboolean::from)
}

/// Returns 0 when the length is unknown.
#[unsafe(no_mangle)]
pub extern "system" fn Java_io_github_valeronm_sparedongle_Native_reportDescriptorLength(
    env: JNIEnv,
    _: JClass,
    raw: JByteArray,
    interface_number: jint,
) -> jint {
    let length = catch_unwind(AssertUnwindSafe(|| {
        let bytes = env.convert_byte_array(&raw).ok()?;
        report_descriptor_length(&bytes, u8::try_from(interface_number).ok()?)
    }));
    length.ok().flatten().map_or(0, jint::from)
}

#[unsafe(no_mangle)]
pub extern "system" fn Java_io_github_valeronm_sparedongle_Native_close(
    _: JNIEnv,
    _: JClass,
    handle: jlong,
) {
    drop(unsafe { Box::from_raw(handle as *mut Dongle) });
}

fn string_or_throw(env: JNIEnv, read: impl FnOnce() -> anyhow::Result<String>) -> jstring {
    or_throw(
        env,
        std::ptr::null_mut(),
        "the status reader failed",
        |env| Ok(env.new_string(read()?)?.into_raw()),
    )
}

fn or_throw<T>(
    mut env: JNIEnv,
    failed: T,
    panicked: &str,
    run: impl FnOnce(&mut JNIEnv) -> anyhow::Result<T>,
) -> T {
    let result = catch_unwind(AssertUnwindSafe(|| run(&mut env)))
        .unwrap_or_else(|_| Err(anyhow::anyhow!("{panicked}")));
    result.unwrap_or_else(|e| {
        if matches!(env.exception_check(), Ok(false)) {
            let _ = env.throw_new("java/io/IOException", format!("{e:#}"));
        }
        failed
    })
}
