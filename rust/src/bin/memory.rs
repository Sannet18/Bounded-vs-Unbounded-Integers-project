// Same CSV columns as the other memory tools:
//     impl,bits,bytes,payload_bytes,allocator_bytes
//
// A BigUint is a small handle (a Vec<u64>: pointer, capacity, length = 24
// bytes) pointing at a heap buffer of 64-bit digits. Rust gives no direct way
// to ask "how big is this number", so this installs a counting global
// allocator: it wraps the system allocator and keeps a running total of the
// bytes currently allocated. Building the number between two reads of that
// total shows exactly what the heap buffer cost:
//     bytes         = size_of::<BigUint>() + (heap bytes the number holds)
//     payload_bytes = 8 * number of u64 digits in use
// The counter records bytes REQUESTED, not any rounding the system allocator
// adds, so allocator_bytes is left blank.
//
// To run the release build: cargo build --release
//  ./target/release/memory ../data/memory/memory_rust.csv

use num_bigint::BigUint;
use std::alloc::{GlobalAlloc, Layout, System};
use std::env;
use std::fs::{self, File};
use std::io::Write;
use std::path::Path;
use std::sync::atomic::{AtomicUsize, Ordering};

struct Counting;
static LIVE: AtomicUsize = AtomicUsize::new(0);

unsafe impl GlobalAlloc for Counting {
    unsafe fn alloc(&self, l: Layout) -> *mut u8 {
        let p = System.alloc(l);
        if !p.is_null() {
            LIVE.fetch_add(l.size(), Ordering::SeqCst);
        }
        p
    }
    unsafe fn dealloc(&self, p: *mut u8, l: Layout) {
        System.dealloc(p, l);
        LIVE.fetch_sub(l.size(), Ordering::SeqCst);
    }
    unsafe fn realloc(&self, p: *mut u8, l: Layout, new_size: usize) -> *mut u8 {
        let q = System.realloc(p, l, new_size);
        if !q.is_null() {
            LIVE.fetch_sub(l.size(), Ordering::SeqCst);
            LIVE.fetch_add(new_size, Ordering::SeqCst);
        }
        q
    }
}

#[global_allocator]
static ALLOCATOR: Counting = Counting;

fn bit_list() -> Vec<usize> {
    let mut v: Vec<usize> = (1..=320).collect();
    v.extend_from_slice(&[
        384, 448, 512, 640, 768, 1024, 1536, 2048, 3072, 4096, 6144, 8192, 12288, 16384,
        32768, 65536, 131072, 262144, 524288, 1048576,
    ]);
    v
}

/// Big-endian bytes of 2^bits - 1.
fn all_ones_bytes(bits: usize) -> Vec<u8> {
    let nbytes = (bits + 7) / 8;
    let mut b = vec![0xFFu8; nbytes];
    let rem = bits % 8;
    if rem != 0 {
        b[0] = (1u16 << rem) as u8 - 1;
    }
    b
}

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() != 2 {
        eprintln!("usage: {} <out.csv>", args[0]);
        std::process::exit(1);
    }
    let outpath = &args[1];

    // Collect every row first: building the output text allocates, and must
    // not happen inside a measurement window.
    let mut rows: Vec<String> = Vec::new();

    for bits in bit_list() {
        if bits <= 64 {
            rows.push(format!("rust_u64,{},8,8,", bits));
        }
        if bits <= 128 {
            rows.push(format!("rust_u128,{},16,16,", bits));
        }

        let bytes = all_ones_bytes(bits);
        let before = LIVE.load(Ordering::SeqCst);
        let x = BigUint::from_bytes_be(&bytes);
        let after = LIVE.load(Ordering::SeqCst);
        let heap = after - before;

        assert_eq!(x.bits() as usize, bits, "bit length");
        let handle = std::mem::size_of::<BigUint>();
        let payload = 8 * x.to_u64_digits().len();
        rows.push(format!("rust,{},{},{},", bits, handle + heap, payload));
        drop(x);
    }

    if let Some(parent) = Path::new(outpath).parent() {
        if !parent.as_os_str().is_empty() {
            fs::create_dir_all(parent).expect("create output directory");
        }
    }
    let mut f = File::create(outpath).expect("create output file");
    writeln!(
        f,
        "# Rust num-bigint 0.4, size_of::<BigUint>() = {} bytes, size_of::<u64>() = {}",
        std::mem::size_of::<BigUint>(),
        std::mem::size_of::<u64>()
    )
    .unwrap();
    writeln!(f, "impl,bits,bytes,payload_bytes,allocator_bytes").unwrap();
    for r in rows {
        writeln!(f, "{}", r).unwrap();
    }
    eprintln!("wrote {}", outpath);
}