use diagclient::device::USAGE_PAGE;

fn first_usage_page(report_descriptor: &[u8]) -> Option<u16> {
    const LONG_ITEM: u8 = 0xfe;
    const GLOBAL: u8 = 1;
    const USAGE_PAGE_TAG: u8 = 0;
    let mut rest = report_descriptor;
    while let &[prefix, ref after @ ..] = rest {
        let len = match prefix {
            LONG_ITEM => 2 + usize::from(*after.first()?),
            _ => [0, 1, 2, 4][usize::from(prefix & 3)],
        };
        let data = after.get(..len)?;
        if (prefix >> 2) & 3 == GLOBAL && prefix >> 4 == USAGE_PAGE_TAG {
            let value = data.iter().rev().fold(0u32, |v, b| v << 8 | u32::from(*b));
            return u16::try_from(value).ok();
        }
        rest = &after[len..];
    }
    None
}

/// Whether a HID report descriptor is the diagnostic interface's, by the first usage page it sets.
pub fn is_diagnostic(report_descriptor: &[u8]) -> bool {
    first_usage_page(report_descriptor) == Some(USAGE_PAGE)
}

/// `raw` is a device descriptor followed by its configuration descriptors, as `/dev/bus/usb`
/// serves them; `None` when the interface has no HID descriptor or `raw` is malformed.
pub fn report_descriptor_length(raw: &[u8], interface: u8) -> Option<u16> {
    const INTERFACE: u8 = 4;
    const HID: u8 = 0x21;
    const REPORT: u8 = 0x22;
    let mut current = None;
    let mut rest = raw;
    while let &[len, kind, ..] = rest {
        let desc = rest.get(..usize::from(len)).filter(|_| len >= 2)?;
        match kind {
            INTERFACE => current = desc.get(2..4).map(|n| (n[0], n[1])),
            HID if current == Some((interface, 0)) => {
                let count = usize::from(*desc.get(5)?);
                return desc
                    .get(6..)?
                    .as_chunks::<3>()
                    .0
                    .iter()
                    .take(count)
                    .find(|&&[kind, ..]| kind == REPORT)
                    .map(|&[_, lo, hi]| u16::from_le_bytes([lo, hi]));
            }
            _ => {}
        }
        rest = &rest[desc.len()..];
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    const DIAG_DESC: [u8; 27] = [
        0x06, 0x00, 0xff, 0x09, 0x01, 0xa1, 0x01, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95,
        0x40, 0x09, 0x02, 0x81, 0x02, 0x95, 0x40, 0x09, 0x03, 0x91, 0x02, 0xc0,
    ];

    #[test]
    fn the_diagnostic_descriptor_is_diagnostic() {
        assert!(is_diagnostic(&DIAG_DESC));
    }

    #[test]
    fn the_keyboard_descriptor_is_not_diagnostic() {
        let keyboard = [
            0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7, 0x15, 0x00,
            0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x75, 0x08, 0x95, 0x01, 0x81, 0x03,
            0x75, 0x01, 0x95, 0x05, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02, 0x75, 0x03,
            0x95, 0x01, 0x91, 0x03, 0x75, 0x08, 0x95, 0x06, 0x15, 0x00, 0x26, 0xff, 0x00, 0x05,
            0x07, 0x19, 0x00, 0x29, 0xff, 0x81, 0x00, 0xc0,
        ];
        assert!(!is_diagnostic(&keyboard));
    }

    #[test]
    fn the_extra_keys_descriptor_is_not_diagnostic() {
        let extra = [
            0x05, 0x0c, 0x09, 0x01, 0xa1, 0x01, 0x85, 0x01, 0x15, 0x01, 0x26, 0xa0, 0x02, 0x19,
            0x01, 0x2a, 0xa0, 0x02, 0x75, 0x10, 0x95, 0x01, 0x81, 0x00, 0xc0, 0x05, 0x01, 0x09,
            0x80, 0xa1, 0x01, 0x85, 0x02, 0x16, 0x81, 0x00, 0x26, 0x83, 0x00, 0x19, 0x81, 0x29,
            0x83, 0x75, 0x10, 0x95, 0x01, 0x81, 0x00, 0xc0, 0x05, 0x01, 0x09, 0x06, 0xa1, 0x01,
            0x85, 0x03, 0x05, 0x07, 0x19, 0x00, 0x29, 0xdf, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01,
            0x95, 0xe0, 0x81, 0x02, 0xc0,
        ];
        assert!(!is_diagnostic(&extra));
    }

    #[test]
    fn a_long_item_before_the_usage_page_is_skipped() {
        let mut desc = vec![0xfe, 0x02, 0x00, 0x06, 0x00];
        desc.extend_from_slice(&DIAG_DESC);
        assert!(is_diagnostic(&desc));
    }

    const KEYBOARD_DESC_LEN: u16 = 64;
    const EXTRA_DESC_LEN: u16 = 75;
    const MOUSE_DESC_LEN: u16 = 92;

    fn interface(number: u8, class: u8, endpoints: u8) -> Vec<u8> {
        vec![9, 4, number, 0, endpoints, class, 0, 0, 0]
    }

    fn hid(report_len: u16) -> Vec<u8> {
        let [lo, hi] = report_len.to_le_bytes();
        vec![9, 0x21, 0x11, 0x01, 0, 1, 0x22, lo, hi]
    }

    fn endpoint(address: u8, attributes: u8, size: u8) -> Vec<u8> {
        vec![7, 5, address, attributes, size, 0, 1]
    }

    /// The dongle's device and configuration descriptors: CDC ACM on interfaces 0 and 1, then the
    /// keyboard, media, mouse and diagnostic HID interfaces.
    fn dongle_raw_descriptors() -> Vec<u8> {
        let mut config = vec![8, 0x0b, 0, 2, 2, 2, 1, 0];
        config.extend(interface(0, 2, 1));
        config.extend([5, 0x24, 0x00, 0x10, 0x01]);
        config.extend([5, 0x24, 0x01, 0x00, 0x01]);
        config.extend([4, 0x24, 0x02, 0x02]);
        config.extend([5, 0x24, 0x06, 0x00, 0x01]);
        config.extend(endpoint(0x81, 3, 16));
        config.extend(interface(1, 0x0a, 2));
        config.extend(endpoint(0x82, 2, 64));
        config.extend(endpoint(0x01, 2, 64));
        for (number, len, ep) in [
            (2, KEYBOARD_DESC_LEN, 3),
            (3, EXTRA_DESC_LEN, 4),
            (4, MOUSE_DESC_LEN, 5),
        ] {
            config.extend(interface(number, 3, 1));
            config.extend(hid(len));
            config.extend(endpoint(0x80 | ep, 3, 64));
        }
        config.extend(interface(5, 3, 2));
        config.extend(hid(DIAG_DESC.len() as u16));
        config.extend(endpoint(0x86, 3, 64));
        config.extend(endpoint(0x02, 3, 64));
        let total = (9 + config.len() as u16).to_le_bytes();
        let mut raw = vec![
            18, 1, 0x00, 0x02, 0xef, 0x02, 0x01, 64, 0xe3, 0x2f, 0x44, 0x4e, 0, 1, 1, 2, 3, 1,
        ];
        raw.extend([9, 2, total[0], total[1], 6, 1, 4, 0xa0, 50]);
        raw.extend(config);
        raw
    }

    #[test]
    fn the_diagnostic_interfaces_report_descriptor_length_is_its_own() {
        let raw = dongle_raw_descriptors();
        assert_eq!(
            report_descriptor_length(&raw, 5),
            Some(DIAG_DESC.len() as u16)
        );
    }

    #[test]
    fn an_alternate_setting_ahead_of_setting_0_is_skipped() {
        let raw = dongle_raw_descriptors();
        let diag = raw.len() - 9 - 9 - 7 - 7;
        let mut alternate = vec![9, 4, 5, 1, 2, 3, 0, 0, 0];
        alternate.extend(hid(KEYBOARD_DESC_LEN));
        let raw = [&raw[..diag], &alternate, &raw[diag..]].concat();
        assert_eq!(
            report_descriptor_length(&raw, 5),
            Some(DIAG_DESC.len() as u16)
        );
    }

    #[test]
    fn the_keyboards_report_descriptor_length_is_its_own() {
        let raw = dongle_raw_descriptors();
        assert_eq!(report_descriptor_length(&raw, 2), Some(KEYBOARD_DESC_LEN));
    }

    #[test]
    fn an_interface_without_a_hid_descriptor_has_no_report_descriptor_length() {
        let raw = dongle_raw_descriptors();
        assert_eq!(report_descriptor_length(&raw, 0), None);
        assert_eq!(report_descriptor_length(&raw, 1), None);
    }

    #[test]
    fn an_unknown_interface_has_no_report_descriptor_length() {
        assert_eq!(report_descriptor_length(&dongle_raw_descriptors(), 6), None);
    }

    #[test]
    fn truncated_or_zero_length_descriptors_have_no_report_descriptor_length() {
        let raw = dongle_raw_descriptors();
        let diag_hid_end = raw.len() - 14;
        assert_eq!(report_descriptor_length(&raw[..diag_hid_end - 1], 5), None);
        assert_eq!(report_descriptor_length(&[], 5), None);
        let mut zero = raw.clone();
        zero[18] = 0;
        assert_eq!(report_descriptor_length(&zero, 5), None);
    }

    #[test]
    fn an_empty_or_truncated_descriptor_is_not_diagnostic() {
        assert!(!is_diagnostic(&[]));
        assert!(!is_diagnostic(&DIAG_DESC[..2]));
        assert!(!is_diagnostic(&[0xfe]));
        assert!(!is_diagnostic(&[0xfe, 0xff, 0x00]));
    }
}
