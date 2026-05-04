use std::fs::File;
use std::io::Write;
use std::process::Command;

#[test]
fn sweep_reports_input_errors_without_panicking() {
    let dir = tempfile::tempdir().unwrap();
    let cfg_path = dir.path().join("cfg.toml");
    let out_path = dir.path().join("sweep.csv");
    let toml = format!(
        r#"
[data]
lob_csv = "{lob}"
trades_csv = "{trades}"

[engine]
tick_ms = 1
allow_partial_fills = true

[strategy]
kind = "avellaneda_stoikov"
[strategy.avellaneda_stoikov]
sigma = 0.001
k = 1.5
a = 1.0
gamma = 0.1
quote_qty = 1
max_abs_inventory = 100

[output]
dir = "{out_dir}"
equity_curve = true
fills = true
"#,
        lob = dir.path().join("missing-lob.csv").to_string_lossy(),
        trades = dir.path().join("missing-trades.csv").to_string_lossy(),
        out_dir = dir.path().join("out").to_string_lossy(),
    );
    File::create(&cfg_path)
        .unwrap()
        .write_all(toml.as_bytes())
        .unwrap();

    let output = Command::new(env!("CARGO_BIN_EXE_sweep"))
        .arg("--config")
        .arg(&cfg_path)
        .arg("--gammas")
        .arg("0.1")
        .arg("--out")
        .arg(&out_path)
        .output()
        .unwrap();

    assert!(!output.status.success());
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(!stderr.contains("panicked"), "{stderr}");
}
