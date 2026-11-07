// This just times BigUint multiplication across the same bit ladder
// as in the C, Python and Java harnesses, writing the 
// same CSV schema: {impl,machine,x,trial,time_ns}

// IMPORTANT: always rin the compiled 'release' binary, never 'debug'!
// Rust's debug will add overflow checks and will skip optimizations, making the results meaningless.
// This will make them dramatically slower than the other languages, and will not be a fair comparison
// (not measuring the same kind of thing the other languages harnesses measure).

// Usage : bignum_becnch <maching_label> <out.csv>

// How to add rust to your machine(if testing on your machine)
// I used the linux version because I'm using WSL UBUNTU
// curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
// source "$HOME/.cargo/env"
// rustc --version

// then to run
// cargo build --release
// ./target/release/bignum_bench sizes amd-8940hx ../data/raw/sizes_rust_amd-8940hx.csv
// ./target/release/bignum_bench threshold amd-8940hx ../data/raw/threshold_rust_amd-8940hx.csv


use num_bigint::BigUint;
use rand::{RngCore, SeedableRng};
use rand::rngs::StdRng;
use std::env;
use std::fs::{self, File};
use std::io::Write;
use std::path::Path;
use std::time::Instant;
 
const SIZES_BITS: [u32; 12] = [
    64, 128, 256, 512, 1024, 2048, 4096, 8192,
    16384, 32768, 65536, 131072,
];
const TRIALS: u32 = 30;
const SEED: u64 = 42;
 
fn reps_for_bits(bits: u32) -> u32 {
    if bits <= 512 { 20000 }
    else if bits <= 4096 { 2000 }
    else if bits <= 32768 { 200 }
    else { 20 }
}
 
fn random_of_bits(bits: u32, rng: &mut StdRng) -> BigUint {
    let bytes_needed = ((bits + 7) / 8) as usize;
    let mut bytes = vec![0u8; bytes_needed];
    rng.fill_bytes(&mut bytes);
 
    let extra_bits = (bytes_needed as u32 * 8) - bits;
    bytes[0] |= 1 << (7 - extra_bits);
 
    BigUint::from_bytes_be(&bytes)
}
 
// Wide geometric sweep (~10% steps) from 8 to ~1024 limbs (64-bit digits
// on 64-bit targets -- confirmed from num-bigint's macros.rs). Spans
// roughly 0.25x to 30x of num-bigint's documented thresholds: 32 limbs
// (long multiplication -> Karatsuba) and 256 limbs (Karatsuba ->
// Toom-3). A narrow window around each threshold is not enough: across
// +/-20% in size the curve changes by only a few percent, below
// run-to-run noise. The analysis fits the growth exponent per zone.
fn threshold_sweep_limbs() -> Vec<u32> {
    let mut limbs = std::collections::BTreeSet::new();
    let mut v = 8.0_f64;
    while v <= 1024.0 {
        limbs.insert(v.round() as u32);
        v *= 1.10;
    }
    limbs.into_iter().collect()
}
 
fn run_sizes(file: &mut File, machine: &str, rng: &mut StdRng) {
    for &bits in SIZES_BITS.iter() {
        let reps = reps_for_bits(bits);
        for trial in 0..TRIALS {
            let a = random_of_bits(bits, rng);
            let b = random_of_bits(bits, rng);
 
            let t0 = Instant::now();
            for _ in 0..reps {
                let r = &a * &b;
                std::hint::black_box(&r);
            }
            let elapsed = t0.elapsed();
 
            let time_ns = elapsed.as_nanos() as f64 / reps as f64;
            writeln!(file, "rust,{},{},{},{:.3}", machine, bits, trial, time_ns).unwrap();
        }
        eprintln!("sizes: done {} bits", bits);
        file.flush().unwrap();
    }
}
 
fn run_threshold(file: &mut File, machine: &str, rng: &mut StdRng) {
    for limbs in threshold_sweep_limbs() {
        let bits = limbs * 64;
        let reps = reps_for_bits(bits);
        for trial in 0..TRIALS {
            let a = random_of_bits(bits, rng);
            let b = random_of_bits(bits, rng);
 
            let t0 = Instant::now();
            for _ in 0..reps {
                let r = &a * &b;
                std::hint::black_box(&r);
            }
            let elapsed = t0.elapsed();
 
            let time_ns = elapsed.as_nanos() as f64 / reps as f64;
            // x = bits, same column meaning as "sizes" mode; divide by
            // 64 in analysis to compare directly against the 32/256
            // limb constants.
            writeln!(file, "rust_threshold,{},{},{},{:.3}", machine, bits, trial, time_ns).unwrap();
        }
        eprintln!("threshold: done {} limbs ({} bits)", limbs, bits);
        file.flush().unwrap();
    }
}
 
fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() != 4 {
        eprintln!("usage: {} <sizes|threshold> <machine_label> <out.csv>", args[0]);
        std::process::exit(1);
    }
    let mode = &args[1];
    let machine = &args[2];
    let outpath = &args[3];
 
    eprintln!("starting: mode={}, machine={}, output={}", mode, machine, outpath);
 
    if let Some(parent) = Path::new(outpath).parent() {
        if !parent.as_os_str().is_empty() && !parent.exists() {
            eprintln!("creating missing output directory: {}", parent.display());
            fs::create_dir_all(parent).expect("failed to create output directory");
        }
    }
 
    let mut file = File::create(outpath).expect("failed to create output file");
    writeln!(file, "impl,machine,x,trial,time_ns").unwrap();
 
    let mut rng = StdRng::seed_from_u64(SEED);
 
    // Warmup
    for _ in 0..50 {
        let a = random_of_bits(2048, &mut rng);
        let b = random_of_bits(2048, &mut rng);
        let _ = &a * &b;
    }
 
    match mode.as_str() {
        "sizes" => run_sizes(&mut file, machine, &mut rng),
        "threshold" => run_threshold(&mut file, machine, &mut rng),
        _ => {
            eprintln!("unknown mode '{}' (use 'sizes' or 'threshold')", mode);
            std::process::exit(1);
        }
    }
}