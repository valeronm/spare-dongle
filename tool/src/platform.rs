#[cfg(unix)]
use std::time::{Duration, Instant};

use anyhow::Result;
#[cfg(unix)]
use anyhow::{Context, bail};

#[cfg(unix)]
const ACCESS_RETRY: Duration = Duration::from_millis(100);
#[cfg(unix)]
const ACCESS_WAIT: Duration = Duration::from_secs(2);

/// macOS lists each USB port twice; the `tty.` side blocks on open until carrier detect.
pub fn usable_port(name: &str) -> bool {
    !name.starts_with("/dev/tty.")
}

/// Rust starts with SIGPIPE ignored, which turns a closed output pipe into a println! panic.
#[cfg(unix)]
pub fn restore_sigpipe() {
    unsafe {
        libc::signal(libc::SIGPIPE, libc::SIG_DFL);
    }
}

#[cfg(not(unix))]
pub fn restore_sigpipe() {}

/// Waits until the device node at `path` can be opened for reading and writing, since udev grants
/// access a moment after the node appears. Other failures are left for the open to report.
#[cfg(unix)]
pub fn wait_for_access(path: &str) -> Result<()> {
    wait_for_access_within(path, ACCESS_WAIT)
}

#[cfg(not(unix))]
pub fn wait_for_access(_path: &str) -> Result<()> {
    Ok(())
}

#[cfg(unix)]
fn wait_for_access_within(path: &str, wait: Duration) -> Result<()> {
    let c_path = std::ffi::CString::new(path).with_context(|| format!("device path {path:?}"))?;
    let deadline = Instant::now() + wait;
    // Checked with access(2) because opening a serial port can restart the board.
    while unsafe { libc::access(c_path.as_ptr(), libc::R_OK | libc::W_OK) } != 0
        && std::io::Error::last_os_error().kind() == std::io::ErrorKind::PermissionDenied
    {
        if Instant::now() >= deadline {
            bail!(
                "no access to {path}: install 70-{tool}.rules from ~/.local/share/{tool} \
                 (tools/udev in a source checkout) into /etc/udev/rules.d, then run \
                 sudo udevadm control --reload and \
                 sudo udevadm trigger --subsystem-match=hidraw --subsystem-match=tty",
                tool = crate::TOOL
            );
        }
        std::thread::sleep(ACCESS_RETRY);
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn mac_dial_in_ports_are_skipped() {
        assert!(usable_port("/dev/ttyACM0"));
        assert!(usable_port("/dev/cu.usbmodem1101"));
        assert!(!usable_port("/dev/tty.usbmodem1101"));
    }

    #[cfg(unix)]
    #[test]
    fn a_node_without_access_names_the_udev_rule() {
        use std::os::unix::fs::PermissionsExt;
        let path =
            std::env::temp_dir().join(format!("spare-dongle-no-access-{}", std::process::id()));
        std::fs::write(&path, b"").unwrap();
        std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o000)).unwrap();
        let path = path.to_str().unwrap();
        let err = wait_for_access_within(path, Duration::ZERO);
        std::fs::remove_file(path).unwrap();
        let err = err.unwrap_err().to_string();
        assert!(err.contains(path), "{err}");
        assert!(err.contains("70-spare-dongle.rules"), "{err}");
        assert!(err.contains("udevadm trigger"), "{err}");
    }
}
