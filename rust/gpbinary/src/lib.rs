// SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

//! The Guitar Pro formats that are hand-rolled binary, behind a C ABI.
//!
//! Guitar Pro 7 and 8 are a ZIP holding an XML document, and C++ reads those
//! with KArchive and Qt: two libraries that have been fed malformed input by
//! more people than this project will ever have users. GP3 to GP5 and GP6 are
//! different. They are binary formats with no specification, described only by
//! the programs that worked them out, and reading one means walking a byte
//! stream taking lengths and offsets on trust from a file somebody downloaded.
//!
//! That is the shape of problem where a language with bounds checks is worth a
//! foreign function boundary, and where `cargo fuzz` finds in an afternoon what
//! review does not find in a week. Hence this crate, which the architecture
//! committed to long before anything was written in it.
//!
//! # What is here, and what is not
//!
//! **The boundary, and format detection.** Nothing that decodes a bar of music,
//! and that is deliberate rather than unfinished: there is not a single `.gpx`,
//! `.gp3`, `.gp4` or `.gp5` file to check a decoder against. A binary parser
//! tested only against fixtures written by the same person who wrote the parser
//! is a parser that agrees with itself, which is worth nothing and reads as
//! though it were worth something.
//!
//! Detection is different, and can be honest today: it looks at the first bytes
//! of a file and says which format they announce, which is checkable against
//! the seven real transcriptions this project does have -- every one of them
//! must come out as GP7/8 and not as anything else.
//!
//! # The rules at this boundary
//!
//! Every function here takes a pointer and a length and returns a plain
//! integer. Nothing allocates on behalf of the caller, nothing takes ownership,
//! nothing can panic across the boundary -- the release profile aborts rather
//! than unwinding, because unwinding into C++ is undefined behaviour and an
//! abort is at least a stack trace.

use core::slice;

/// Which format a file announces itself to be.
///
/// The numbers are part of the ABI and may be added to but never reordered.
#[repr(i32)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Format {
    /// Nothing recognised. Not an error: most files are not Guitar Pro files.
    Unknown = 0,
    /// Guitar Pro 3, 4 or 5: a version string, then hand-rolled binary.
    Gp3 = 1,
    Gp4 = 2,
    Gp5 = 3,
    /// Guitar Pro 6: a container of its own, compressed or not.
    Gpx = 4,
    /// Guitar Pro 7 or 8: a ZIP. C++ already reads these; this only says so.
    Gp7 = 5,
}

/// The version of this ABI, so a mismatched pair of halves says so loudly.
///
/// Bumped whenever anything above changes meaning. C++ checks it once at
/// startup rather than discovering the disagreement inside a parser.
pub const ABI_VERSION: i32 = 1;

/// The first bytes of a GP3-to-GP5 file: a Pascal string naming the version.
///
/// One length byte, then thirty of text, of which the first characters are
/// always this. Every implementation that reads these formats agrees on it,
/// and it is plain ASCII rather than anything that needs working out.
const VERSION_PREFIX: &[u8] = b"FICHIER GUITAR PRO v";

fn format_of(data: &[u8]) -> Format {
    // A ZIP, which for this program means Guitar Pro 7 or 8. Only the local
    // file header signature is checked: an empty archive starts differently
    // and is not a score either way.
    if data.starts_with(b"PK\x03\x04") {
        return Format::Gp7;
    }

    // Guitar Pro 6, compressed or stored. BCFS is the container and BCFZ is
    // the same container with a bit-level compressor over it.
    if data.starts_with(b"BCFZ") || data.starts_with(b"BCFS") {
        return Format::Gpx;
    }

    // GP3 to GP5 open with a Pascal string: one length byte, then the text.
    // The length is checked against the text rather than trusted, because a
    // length that disagrees with what follows it is the first thing a corrupt
    // or hostile file gets wrong.
    if data.len() < 1 + VERSION_PREFIX.len() {
        return Format::Unknown;
    }
    let length = usize::from(data[0]);
    if length < VERSION_PREFIX.len() || length + 1 > data.len() {
        return Format::Unknown;
    }
    if &data[1..1 + VERSION_PREFIX.len()] != VERSION_PREFIX {
        return Format::Unknown;
    }

    // The digit after the prefix is the major version. Anything else is a
    // file that says it is Guitar Pro and does not say which, which is not a
    // file this can claim to read.
    match data.get(1 + VERSION_PREFIX.len()) {
        Some(b'3') => Format::Gp3,
        Some(b'4') => Format::Gp4,
        Some(b'5') => Format::Gp5,
        _ => Format::Unknown,
    }
}

/// Which format the bytes announce.
///
/// # Safety
///
/// `data` must point at `len` readable bytes, or be null with `len` zero.
/// Nothing is retained after the call returns.
#[no_mangle]
pub unsafe extern "C" fn gpbinary_format(data: *const u8, len: usize) -> i32 {
    if data.is_null() || len == 0 {
        return Format::Unknown as i32;
    }
    format_of(slice::from_raw_parts(data, len)) as i32
}

/// The ABI version this library was built with.
#[no_mangle]
pub extern "C" fn gpbinary_abi_version() -> i32 {
    ABI_VERSION
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A GP3-to-GP5 header: the Pascal string, padded as a real file pads it.
    fn header(version: &str) -> Vec<u8> {
        let text = format!("FICHIER GUITAR PRO v{version}");
        let mut out = vec![text.len() as u8];
        out.extend_from_slice(text.as_bytes());
        out.resize(31, 0);
        out
    }

    #[test]
    fn reads_the_version_out_of_the_pascal_string() {
        assert_eq!(format_of(&header("3.00")), Format::Gp3);
        assert_eq!(format_of(&header("4.06")), Format::Gp4);
        assert_eq!(format_of(&header("5.10")), Format::Gp5);
    }

    #[test]
    fn knows_the_two_container_formats_apart() {
        assert_eq!(format_of(b"BCFZ\x00\x00\x00\x00"), Format::Gpx);
        assert_eq!(format_of(b"BCFS\x00\x00\x00\x00"), Format::Gpx);
        assert_eq!(format_of(b"PK\x03\x04rest of a zip"), Format::Gp7);
    }

    /// A length byte that disagrees with the text after it is a file lying
    /// about itself, and the answer is "unknown" rather than a guess.
    #[test]
    fn refuses_a_length_that_does_not_match_its_text() {
        let mut lying = header("5.10");
        lying[0] = 250; // longer than the file
        assert_eq!(format_of(&lying), Format::Unknown);

        let mut short = header("5.10");
        short[0] = 3; // shorter than the prefix it claims to hold
        assert_eq!(format_of(&short), Format::Unknown);
    }

    /// Every prefix of a real header, so that a truncated file cannot read
    /// past its end. This is the whole reason the crate is in this language.
    #[test]
    fn never_reads_past_the_end_of_a_truncated_file() {
        let whole = header("5.10");
        for length in 0..whole.len() {
            let _ = format_of(&whole[..length]);
        }
        for length in 0..4 {
            let _ = format_of(&b"BCFZ"[..length]);
            let _ = format_of(&b"PK\x03\x04"[..length]);
        }
    }

    #[test]
    fn says_nothing_about_files_that_are_not_guitar_pro() {
        assert_eq!(format_of(b""), Format::Unknown);
        assert_eq!(format_of(b"this is not a container"), Format::Unknown);
        assert_eq!(format_of(&header("6.00")), Format::Unknown);
    }

    #[test]
    fn the_null_pointer_is_a_question_rather_than_a_crash() {
        assert_eq!(unsafe { gpbinary_format(core::ptr::null(), 0) }, 0);
        assert_eq!(unsafe { gpbinary_format(core::ptr::null(), 99) }, 0);
    }
}
