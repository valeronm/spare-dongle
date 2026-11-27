use std::os::fd::RawFd;
use std::time::Duration;

use diagclient::device::{Lost, Transport};
use diagclient::protocol::REPORT_LEN;

#[repr(C)]
struct BulkTransfer {
    ep: libc::c_uint,
    len: libc::c_uint,
    timeout_ms: libc::c_uint,
    data: *mut libc::c_void,
}

/// `_IOWR('U', 2, struct usbdevfs_bulktransfer)` from linux/usbdevice_fs.h.
const USBDEVFS_BULK: libc::c_ulong = (3 << 30)
    | ((std::mem::size_of::<BulkTransfer>() as libc::c_ulong) << 16)
    | ((b'U' as libc::c_ulong) << 8)
    | 2;

/// Borrows `fd`, which the Android `UsbDeviceConnection` owns and closes.
pub struct Usbfs {
    fd: RawFd,
    in_ep: u8,
    out_ep: u8,
}

impl Usbfs {
    pub fn new(fd: RawFd, in_ep: u8, out_ep: u8) -> Self {
        Self { fd, in_ep, out_ep }
    }

    fn transfer(
        &self,
        ep: u8,
        buf: &mut [u8; REPORT_LEN],
        timeout: Duration,
    ) -> std::io::Result<Option<usize>> {
        let mut t = BulkTransfer {
            ep: ep.into(),
            len: REPORT_LEN as libc::c_uint,
            timeout_ms: timeout.as_millis().clamp(1, u32::MAX.into()) as libc::c_uint,
            data: buf.as_mut_ptr().cast(),
        };
        let n = unsafe { libc::ioctl(self.fd, USBDEVFS_BULK as _, &mut t) };
        if n >= 0 {
            return Ok(Some(n as usize));
        }
        let e = std::io::Error::last_os_error();
        if e.raw_os_error() == Some(libc::ETIMEDOUT) {
            Ok(None)
        } else {
            Err(e)
        }
    }
}

impl Transport for Usbfs {
    fn write(&mut self, report: &[u8; REPORT_LEN]) -> Result<(), Lost> {
        let mut buf = *report;
        match self.transfer(self.out_ep, &mut buf, Duration::from_secs(1)) {
            Ok(Some(_)) => Ok(()),
            Ok(None) => Err(Lost {
                doing: "writing to the dongle",
                cause: "timed out".into(),
            }),
            Err(e) => Err(Lost {
                doing: "writing to the dongle",
                cause: Box::new(e),
            }),
        }
    }

    fn read(&mut self, buf: &mut [u8; REPORT_LEN], timeout: Duration) -> Result<bool, Lost> {
        match self.transfer(self.in_ep, buf, timeout) {
            Ok(n) => Ok(n == Some(REPORT_LEN)),
            Err(e) => Err(Lost {
                doing: "reading from the dongle",
                cause: Box::new(e),
            }),
        }
    }
}
