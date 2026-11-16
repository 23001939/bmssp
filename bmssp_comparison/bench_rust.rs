//! Benchmark runner for logtwothirds (Rust BMSSP + Dijkstra).
//! Reads graph from text file (same format as bench_cpp.cpp):
//!   first line: n m
//!   next m lines: u v w
//! Usage: bench_rust <mode> <input.txt> <output_dist.txt>
//! Modes: dijkstra, bmssp, bmssp-fast
//! Outputs JSON timing to stdout, distances to output file.

use _logtwothirds::block_queue::SplitMix64;
use _logtwothirds::bmssp::{build_csr, sssp_bmssp, Csr};
use _logtwothirds::dijkstra;
use _logtwothirds::variants::fast;
use std::io::{BufRead, BufReader, Write, BufWriter};
use std::time::Instant;

fn read_graph(path: &str) -> Csr {
    let file = std::fs::File::open(path).expect("open input graph");
    let reader = BufReader::new(file);
    let mut lines = reader.lines();
    let header = lines.next().unwrap().unwrap();
    let mut hparts = header.split_whitespace();
    let n: usize = hparts.next().unwrap().parse().unwrap();
    let m: usize = hparts.next().unwrap().parse().unwrap();
    let mut edges = Vec::with_capacity(m);
    for line in lines {
        let line = line.unwrap();
        if line.is_empty() { continue; }
        let mut parts = line.split_whitespace();
        let u: u32 = parts.next().unwrap().parse().unwrap();
        let v: u32 = parts.next().unwrap().parse().unwrap();
        let w: f64 = parts.next().unwrap().parse().unwrap();
        edges.push((u, v, w));
    }
    assert_eq!(edges.len(), m, "edge count mismatch");
    build_csr(n, &edges)
}

fn median(mut xs: Vec<f64>) -> f64 {
    xs.sort_by(|a, b| a.total_cmp(b));
    xs[xs.len() / 2]
}

fn write_distances(path: &str, dist: &[f64]) {
    let file = std::fs::File::create(path).expect("create output file");
    let mut writer = BufWriter::new(file);
    for d in dist {
        writeln!(writer, "{:.17e}", d).unwrap();
    }
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() != 4 {
        eprintln!("Usage: bench_rust <mode> <input.txt> <output_dist.txt>");
        eprintln!("Modes: dijkstra, bmssp, bmssp-fast");
        std::process::exit(2);
    }
    let mode = &args[1];
    let g = read_graph(&args[2]);
    let n = g.n;

    // CSR for Dijkstra (i64/i32 types)
    let indptr: Vec<i64> = g.indptr.iter().map(|&p| p as i64).collect();
    let indices: Vec<i32> = g.indices.iter().map(|&v| v as i32).collect();

    let runs = 6; // 1 warmup + 5 measured
    let mut times = Vec::new();
    let mut final_dist: Vec<f64> = Vec::new();

    for r in 0..runs {
        let t0 = Instant::now();
        match mode.as_str() {
            "dijkstra" => {
                let mut dist = vec![0.0f64; n];
                let mut pred = vec![0i32; n];
                let mut heap = dijkstra::Heap::new();
                dijkstra::dijkstra(&indptr, &indices, &g.weights, 0, &mut dist, &mut pred, &mut heap)
                    .expect("dijkstra failed");
                final_dist = dist;
            }
            "bmssp" => {
                let result = sssp_bmssp(&g, 0, 0, None).expect("bmssp failed");
                final_dist = result.dist;
            }
            "bmssp-fast" => {
                let result = fast::sssp(&g, 0, 0, None).expect("bmssp-fast failed");
                final_dist = result.dist;
            }
            _ => {
                eprintln!("Unknown mode: {mode}");
                std::process::exit(3);
            }
        }
        let dt = t0.elapsed().as_secs_f64() * 1000.0; // ms
        if r > 0 {
            times.push(dt);
        }
    }

    write_distances(&args[3], &final_dist);

    // Output JSON
    let med = median(times.clone());
    print!("{{\"samples_ms\":[");
    for (i, t) in times.iter().enumerate() {
        if i > 0 { print!(","); }
        print!("{t}");
    }
    print!("],\"median_ms\":{med}");
    println!("}}");
}
