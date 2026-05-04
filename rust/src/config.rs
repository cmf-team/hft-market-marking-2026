use serde::Deserialize;
use std::path::PathBuf;
use thiserror::Error;

#[derive(Debug, Clone, Deserialize)]
pub struct DataCfg {
    pub lob_csv: PathBuf,
    pub trades_csv: PathBuf,
}

#[derive(Debug, Clone, Deserialize)]
pub struct EngineCfg {
    pub tick_ms: u64,
    pub allow_partial_fills: bool,
    #[serde(default)]
    pub start_us: Option<u64>,
    #[serde(default)]
    pub end_us: Option<u64>,
}

#[derive(Debug, Clone, Deserialize)]
pub struct AsCfg {
    pub sigma: f64,
    pub k: f64,
    pub a: f64,
    pub gamma: f64,
    pub quote_qty: u64,
    pub max_abs_inventory: i64,
}

#[derive(Debug, Clone, Deserialize)]
pub struct SymCfg {
    pub half_spread: String,
    pub quote_qty: u64,
    pub max_abs_inventory: i64,
}

#[derive(Debug, Clone, Deserialize)]
pub struct OutCfg {
    pub dir: PathBuf,
    pub equity_curve: bool,
    pub fills: bool,
}

#[derive(Debug, Clone, Deserialize)]
pub struct StrategyCfg {
    /// "symmetric" | "avellaneda_stoikov"
    pub kind: String,
    #[serde(default)]
    pub symmetric: Option<SymCfg>,
    #[serde(default)]
    pub avellaneda_stoikov: Option<AsCfg>,
}

#[derive(Debug, Clone, Deserialize)]
pub struct Config {
    pub data: DataCfg,
    pub engine: EngineCfg,
    pub strategy: StrategyCfg,
    pub output: OutCfg,
}

#[derive(Debug, Error)]
pub enum ConfigError {
    #[error("io: {0}")]
    Io(#[from] std::io::Error),
    #[error("toml: {0}")]
    Toml(#[from] toml::de::Error),
    #[error("validation: {0}")]
    Validation(String),
}

impl Config {
    pub fn from_path(p: &std::path::Path) -> Result<Self, ConfigError> {
        let s = std::fs::read_to_string(p)?;
        let c: Config = toml::from_str(&s)?;
        c.validate()?;
        Ok(c)
    }

    pub fn validate(&self) -> Result<(), ConfigError> {
        if self.engine.tick_ms == 0 {
            return Err(ConfigError::Validation("engine.tick_ms must be > 0".into()));
        }
        match self.strategy.kind.as_str() {
            "symmetric" => {
                let s = self.strategy.symmetric.as_ref().ok_or_else(|| {
                    ConfigError::Validation("missing [strategy.symmetric]".into())
                })?;
                if s.quote_qty == 0 {
                    return Err(ConfigError::Validation("symmetric.quote_qty=0".into()));
                }
                if s.max_abs_inventory <= 0 {
                    return Err(ConfigError::Validation(
                        "symmetric.max_abs_inventory<=0".into(),
                    ));
                }
                let _: rust_decimal::Decimal = s.half_spread.parse().map_err(|_| {
                    ConfigError::Validation(format!("invalid half_spread: {}", s.half_spread))
                })?;
            }
            "avellaneda_stoikov" => {
                let a = self.strategy.avellaneda_stoikov.as_ref().ok_or_else(|| {
                    ConfigError::Validation("missing [strategy.avellaneda_stoikov]".into())
                })?;
                if a.sigma <= 0.0 {
                    return Err(ConfigError::Validation("AS.sigma must be > 0".into()));
                }
                if a.k <= 0.0 {
                    return Err(ConfigError::Validation("AS.k must be > 0".into()));
                }
                if a.gamma <= 0.0 {
                    return Err(ConfigError::Validation("AS.gamma must be > 0".into()));
                }
                if a.quote_qty == 0 {
                    return Err(ConfigError::Validation("AS.quote_qty=0".into()));
                }
                if a.max_abs_inventory <= 0 {
                    return Err(ConfigError::Validation("AS.max_abs_inventory<=0".into()));
                }
            }
            other => {
                return Err(ConfigError::Validation(format!(
                    "unknown strategy kind: {}",
                    other
                )))
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;

    fn write_tmp(s: &str) -> tempfile::NamedTempFile {
        let mut f = tempfile::NamedTempFile::new().unwrap();
        f.write_all(s.as_bytes()).unwrap();
        f
    }

    const VALID_AS: &str = r#"
[data]
lob_csv = "lob.csv"
trades_csv = "trades.csv"

[engine]
tick_ms = 100
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
dir = "out"
equity_curve = true
fills = true
"#;

    #[test]
    fn valid_as_loads() {
        let f = write_tmp(VALID_AS);
        let c = Config::from_path(f.path()).unwrap();
        assert_eq!(c.engine.tick_ms, 100);
    }

    #[test]
    fn rejects_zero_tick() {
        let f = write_tmp(&VALID_AS.replace("tick_ms = 100", "tick_ms = 0"));
        assert!(Config::from_path(f.path()).is_err());
    }

    #[test]
    fn rejects_negative_gamma() {
        let f = write_tmp(&VALID_AS.replace("gamma = 0.1", "gamma = -1.0"));
        assert!(Config::from_path(f.path()).is_err());
    }

    #[test]
    fn rejects_unknown_kind() {
        let f = write_tmp(&VALID_AS.replace(r#"kind = "avellaneda_stoikov""#, r#"kind = "wat""#));
        assert!(Config::from_path(f.path()).is_err());
    }
}
