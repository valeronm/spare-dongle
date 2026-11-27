mod descriptor;
mod rows;
mod usbfs;

use descriptor::{is_diagnostic, report_descriptor_length};
use diagclient::device::Device;
use jni::JNIEnv;
use jni::objects::{JByteArray, JClass, JObject};
use jni::sys::{JNI_FALSE, jboolean, jint, jlong, jobjectArray};
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
pub extern "system" fn Java_io_github_valeronm_sparedongle_Native_statusRows(
    mut env: JNIEnv,
    _: JClass,
    handle: jlong,
) -> jobjectArray {
    let dev = unsafe { &mut *(handle as *mut Dongle) };
    let read = catch_unwind(AssertUnwindSafe(|| {
        dev.status()
            .and_then(|s| Ok(rows::status_rows(&s, &dev.shown(&s)?)))
    }));
    let rows = read.unwrap_or_else(|_| Err(anyhow::anyhow!("the status reader failed")));
    match rows.and_then(|rows| to_java(&mut env, &rows)) {
        Ok(array) => array,
        Err(e) => {
            if matches!(env.exception_check(), Ok(false)) {
                let _ = env.throw_new("java/io/IOException", format!("{e:#}"));
            }
            std::ptr::null_mut()
        }
    }
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

fn to_java(env: &mut JNIEnv, rows: &[(&str, String)]) -> anyhow::Result<jobjectArray> {
    let array =
        env.new_object_array((rows.len() * 2) as i32, "java/lang/String", JObject::null())?;
    for (i, text) in rows.iter().flat_map(|(l, v)| [*l, v.as_str()]).enumerate() {
        let s = env.new_string(text)?;
        env.set_object_array_element(&array, i as i32, s)?;
    }
    Ok(array.into_raw())
}
