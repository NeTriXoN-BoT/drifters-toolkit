// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Drifter's Toolkit contributors.
// This program is free software under GPL version 3 or any later version.
// It comes WITHOUT ANY WARRANTY. See COPYING and NOTICE.txt.
//! A separate, generic save-container filter. No toolkit UI or internal ABI.
//! stdin: one frozen save <=80 MiB. stdout: its GVAS bytes <=64 MiB.
//! No paths, arguments, network or game-writing operations are supported.
use oozextract::Extractor;
use std::io::{Read, Write};

const MAX_INPUT: usize = 80 * 1024 * 1024;
const MAX_EXPANDED: usize = 64 * 1024 * 1024;
const CHUNK: usize = 131072;
#[derive(Debug, PartialEq)]
struct Invalid;
type Result<T> = std::result::Result<T, Invalid>;
fn le32(bytes: &[u8], at: usize) -> Result<usize> {
    let part = bytes.get(at..at.checked_add(4).ok_or(Invalid)?).ok_or(Invalid)?;
    Ok(u32::from_le_bytes(part.try_into().map_err(|_| Invalid)?) as usize)
}
fn le64(bytes: &[u8], at: usize) -> Result<usize> {
    let part = bytes.get(at..at.checked_add(8).ok_or(Invalid)?).ok_or(Invalid)?;
    u64::from_le_bytes(part.try_into().map_err(|_| Invalid)?).try_into().map_err(|_| Invalid)
}
fn unpack(input: &[u8]) -> Result<Vec<u8>> {
    if input.len() > MAX_INPUT { return Err(Invalid); }
    if input.get(..4) == Some(b"GVAS") {
        return if input.len() <= MAX_EXPANDED { Ok(input.to_vec()) } else { Err(Invalid) };
    }
    if input.len() < 56 || input.get(..4) != Some(b"pmOc") ||
       le64(input, 8)? != 0x9e2a83c1 || le64(input, 16)? != CHUNK { return Err(Invalid); }
    let packed_total = le64(input, 24)?;
    let plain_total = le64(input, 32)?;
    if plain_total < 4 || plain_total > MAX_EXPANDED || plain_total != le32(input, 4)? ||
       packed_total == 0 || packed_total > MAX_INPUT { return Err(Invalid); }
    let count = plain_total.div_ceil(CHUNK);
    let mut cursor = 40usize.checked_add(count.checked_mul(16).ok_or(Invalid)?).ok_or(Invalid)?;
    if cursor.checked_add(packed_total).ok_or(Invalid)? != input.len() { return Err(Invalid); }
    let mut output = Vec::with_capacity(plain_total);
    let mut packed_sum = 0usize;
    for index in 0..count {
        let packed = le64(input, 40 + index * 16)?;
        let plain = le64(input, 48 + index * 16)?;
        let expected = std::cmp::min(CHUNK, plain_total - output.len());
        if packed < 2 || packed > MAX_INPUT || plain != expected { return Err(Invalid); }
        packed_sum = packed_sum.checked_add(packed).ok_or(Invalid)?;
        if packed_sum > packed_total { return Err(Invalid); }
        let end = cursor.checked_add(packed).ok_or(Invalid)?;
        let compressed = input.get(cursor..end).ok_or(Invalid)?;
        // Observed Kraken framing, including its independently verified CC06
        // literal form. Equal compressed/plain lengths are not guessed as raw.
        if compressed[0] & 15 != 12 || compressed[1] & 127 != 6 { return Err(Invalid); }
        let mut chunk = vec![0u8; plain];
        let written = Extractor::new().read_from_slice(compressed, &mut chunk).map_err(|_| Invalid)?;
        if written != plain { return Err(Invalid); }
        output.extend_from_slice(&chunk); cursor = end;
    }
    if packed_sum != packed_total || output.len() != plain_total || cursor != input.len() || output.get(..4) != Some(b"GVAS") { return Err(Invalid); }
    Ok(output)
}

// The native caller additionally enforces memory/CPU/descriptor limits and a
// wall deadline. Deny file access, networking and foreign process control here.
#[cfg(all(target_os = "linux", target_arch = "x86_64"))]
fn restrict_process() -> Result<()> {
    #[repr(C)] struct Filter { code: u16, jt: u8, jf: u8, k: u32 }
    #[repr(C)] struct Program { len: u16, filters: *const Filter }
    unsafe extern "C" { fn prctl(option: i32, a2: usize, a3: usize, a4: usize, a5: usize) -> i32; }
    let mut filters = vec![
        Filter { code: 0x20, jt: 0, jf: 0, k: 4 },
        Filter { code: 0x15, jt: 1, jf: 0, k: 0xc000003e },
        Filter { code: 0x06, jt: 0, jf: 0, k: 0x80000000 },
        Filter { code: 0x20, jt: 0, jf: 0, k: 0 },
    ];
    for syscall in [0u32, 1, 3, 5, 8, 9, 10, 11, 12, 13, 14, 15, 24, 25, 28, 35,
                    39, 60, 186, 202, 218, 228, 231, 273, 318, 334] {
        filters.push(Filter { code: 0x15, jt: 0, jf: 1, k: syscall });
        filters.push(Filter { code: 0x06, jt: 0, jf: 0, k: 0x7fff0000 });
    }
    filters.push(Filter { code: 0x06, jt: 0, jf: 0, k: 0x00050001 });
    let program = Program { len: filters.len() as u16, filters: filters.as_ptr() };
    if unsafe { prctl(38, 1, 0, 0, 0) } != 0 ||
       unsafe { prctl(22, 2, (&program as *const Program) as usize, 0, 0) } != 0 { return Err(Invalid); }
    Ok(())
}
#[cfg(not(all(target_os = "linux", target_arch = "x86_64")))]
fn restrict_process() -> Result<()> { if cfg!(target_os = "linux") { Err(Invalid) } else { Ok(()) } }
fn run() -> Result<()> {
    if std::env::args_os().len() != 1 { return Err(Invalid); }
    let mut input = Vec::new();
    std::io::stdin().take((MAX_INPUT + 1) as u64).read_to_end(&mut input).map_err(|_| Invalid)?;
    let output = unpack(&input)?;
    std::io::stdout().write_all(&output).map_err(|_| Invalid)
}
fn main() {
    if restrict_process().is_err() { std::process::exit(2); }
    std::panic::set_hook(Box::new(|_| {}));
    if !matches!(std::panic::catch_unwind(run), Ok(Ok(()))) { std::process::exit(2); }
}
#[cfg(test)]
mod tests {
    use super::*;
    fn archive(plain: &[u8]) -> Vec<u8> {
        let packed = plain.len() + 2 * plain.len().div_ceil(CHUNK);
        let mut bytes = b"pmOc".to_vec(); bytes.extend_from_slice(&(plain.len() as u32).to_le_bytes());
        for value in [0x9e2a83c1u64, CHUNK as u64, packed as u64, plain.len() as u64] { bytes.extend_from_slice(&value.to_le_bytes()); }
        for chunk in plain.chunks(CHUNK) { bytes.extend_from_slice(&((chunk.len()+2) as u64).to_le_bytes()); bytes.extend_from_slice(&(chunk.len() as u64).to_le_bytes()); }
        for chunk in plain.chunks(CHUNK) { bytes.extend_from_slice(&[0xcc, 6]); bytes.extend_from_slice(chunk); }
        bytes
    }
    #[test] fn plain_gvas_is_exact_and_bounded() { assert_eq!(unpack(b"GVAS opaque tail").unwrap(), b"GVAS opaque tail"); assert_eq!(unpack(b"not a save"),Err(Invalid)); }
    #[test] fn literal_chunks_round_trip() { for size in [4, CHUNK-1, CHUNK, CHUNK+1, CHUNK*2+17] { let mut plain=vec![42;size]; plain[..4].copy_from_slice(b"GVAS"); assert_eq!(unpack(&archive(&plain)).unwrap(),plain); } }
    #[test] fn archive_metadata_and_totals_fail_closed() { let good=archive(b"GVAS supported data"); for at in [4,8,16,24,32,40,48] { let mut bad=good.clone(); bad[at]^=1; assert_eq!(unpack(&bad),Err(Invalid)); } assert_eq!(unpack(&good[..good.len()-1]),Err(Invalid)); let mut extra=good;extra.push(0);assert_eq!(unpack(&extra),Err(Invalid)); }
    #[test] fn opaque_chunk_framing_is_not_guessed() { let mut bad=archive(b"GVAS supported data");bad[56]=0;assert_eq!(unpack(&bad),Err(Invalid)); let mut wrong=archive(b"NOPE invalid signature");assert_eq!(unpack(&wrong),Err(Invalid));wrong[4..8].copy_from_slice(&((MAX_EXPANDED+1) as u32).to_le_bytes());wrong[32..40].copy_from_slice(&((MAX_EXPANDED+1) as u64).to_le_bytes());assert_eq!(unpack(&wrong),Err(Invalid)); }
    #[cfg(all(target_os = "linux", target_arch = "x86_64"))]
    #[test] fn linux_guard_denies_paths_network_and_foreign_signals() {
        if std::env::var_os("DRIFTER_GUARD_CHILD").is_some() {
            let parent:isize=std::env::var("DRIFTER_GUARD_PARENT").unwrap().parse().unwrap(); restrict_process().unwrap();
            assert_eq!(std::fs::File::open("/etc/passwd").unwrap_err().raw_os_error(),Some(1));
            assert_eq!(std::net::TcpStream::connect("127.0.0.1:9").unwrap_err().raw_os_error(),Some(1));
            unsafe extern "C" { fn syscall(number:isize,...) -> isize; }
            for (number,a,b,c) in [(62isize,parent,0isize,0isize),(200,parent,0,0),(234,parent,parent,0)] { assert_eq!(unsafe{syscall(number,a,b,c)},-1);assert_eq!(std::io::Error::last_os_error().raw_os_error(),Some(1)); }
            return;
        }
        assert!(std::process::Command::new(std::env::current_exe().unwrap()).arg("--exact").arg("tests::linux_guard_denies_paths_network_and_foreign_signals").arg("--test-threads=1").env("DRIFTER_GUARD_CHILD","1").env("DRIFTER_GUARD_PARENT",std::process::id().to_string()).status().unwrap().success());
    }
}
