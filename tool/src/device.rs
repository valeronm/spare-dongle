use std::time::Duration;

use anyhow::{Context, Result, bail};

use crate::protocol::{REPORT_LEN, Status};

#[cfg(test)]
pub(crate) use diagclient::device::testing as tests;
pub use diagclient::device::*;

pub const USAGE_PAGE: u16 = 0xff00;

pub struct HidTransport(hidapi::HidDevice);

impl Transport for HidTransport {
    fn write(&mut self, report: &[u8; REPORT_LEN]) -> Result<(), Lost> {
        let mut out = [0u8; REPORT_LEN + 1];
        out[1..].copy_from_slice(report);
        self.0.write(&out).map_err(|cause| Lost {
            doing: "writing to the dongle",
            cause: Box::new(cause),
        })?;
        Ok(())
    }

    fn read(&mut self, buf: &mut [u8; REPORT_LEN], timeout: Duration) -> Result<bool, Lost> {
        let ms = i32::try_from(timeout.as_millis()).unwrap_or(i32::MAX);
        let n = self.0.read_timeout(buf, ms).map_err(|cause| Lost {
            doing: "reading from the dongle",
            cause: Box::new(cause),
        })?;
        Ok(n == REPORT_LEN)
    }
}

pub fn dongles(api: &hidapi::HidApi) -> impl Iterator<Item = &hidapi::DeviceInfo> {
    api.device_list()
        .filter(|d| d.vendor_id() == VID && d.product_id() == PID && d.usage_page() == USAGE_PAGE)
}

pub fn open(serial: Option<&str>) -> Result<(Device<HidTransport>, Status)> {
    open_unchecked(serial)?.checked()
}

/// Skips the protocol check, for the commands every protocol version keeps.
pub fn open_unchecked(serial: Option<&str>) -> Result<Device<HidTransport>> {
    let api = hidapi::HidApi::new().context("starting hidapi")?;
    let found: Vec<_> = dongles(&api)
        .filter(|d| serial.is_none_or(|s| d.serial_number() == Some(s)))
        .collect();
    let info = match found.as_slice() {
        [] => bail!("no {} with a diagnostic interface found", crate::PRODUCT),
        [one] => *one,
        many => {
            let serials: Vec<_> = many.iter().filter_map(|d| d.serial_number()).collect();
            bail!(
                "{} dongles found; pick one with --serial: {}",
                many.len(),
                serials.join(", ")
            );
        }
    };
    if let Ok(path) = info.path().to_str() {
        crate::platform::wait_for_access(path)?;
    }
    let device = info.open_device(&api).context("opening the dongle")?;
    Ok(Device::open(HidTransport(device)))
}
