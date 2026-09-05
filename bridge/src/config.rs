use anyhow::{Context, Result, bail};
use serde::{Deserialize, Serialize};
use std::{
    collections::HashMap,
    fs,
    path::{Path, PathBuf},
};

pub const AGENTS: [&str; 3] = ["codex", "claude", "opencode"];
pub const ACTIONS: [&str; 8] = [
    "confirm", "cancel", "stop", "pause", "resume", "approve", "reject", "retry",
];

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(default)]
pub struct Config {
    pub host: String,
    pub port: u16,
    pub client_id: String,
    pub username_env: String,
    pub password_env: String,
    pub devices: Vec<String>,
    pub state_dir: PathBuf,
    pub interval: u64,
    pub codex_usage_command: Option<Vec<String>>,
    pub handlers: HashMap<String, HashMap<String, Vec<String>>>,
    pub tls_ca: Option<PathBuf>,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            host: String::new(),
            port: 1883,
            client_id: "agentdeck-pc-main".into(),
            username_env: "AGENTDECK_MQTT_USER".into(),
            password_env: "AGENTDECK_MQTT_PASSWORD".into(),
            devices: vec![],
            state_dir: ".state".into(),
            interval: 3,
            codex_usage_command: Some(vec!["codex".into(), "app-server".into()]),
            handlers: HashMap::new(),
            tls_ca: None,
        }
    }
}

fn identifier(value: &str, max: usize) -> bool {
    !value.is_empty()
        && value.len() <= max
        && value
            .bytes()
            .all(|c| c.is_ascii_alphanumeric() || c == b'_' || c == b'-')
}

impl Config {
    pub fn load(path: &Path) -> Result<Self> {
        let absolute = path
            .canonicalize()
            .with_context(|| format!("cannot open {}", path.display()))?;
        let mut cfg: Self = serde_json::from_slice(&fs::read(&absolute)?)?;
        cfg.validate()?;
        if cfg.state_dir.is_relative() {
            cfg.state_dir = absolute.parent().unwrap().join(&cfg.state_dir);
        }
        if let Some(ca) = &cfg.tls_ca {
            if ca.is_relative() {
                cfg.tls_ca = Some(absolute.parent().unwrap().join(ca));
            }
        }
        Ok(cfg)
    }

    pub fn validate(&self) -> Result<()> {
        if self.host.is_empty() {
            bail!("host is required");
        }
        if self.devices.is_empty() || self.devices.iter().any(|d| !identifier(d, 32)) {
            bail!("devices must list authorized device IDs");
        }
        if !(1..=5).contains(&self.interval) {
            bail!("interval must be 1..5 seconds");
        }
        for (agent, actions) in &self.handlers {
            if !AGENTS.contains(&agent.as_str()) {
                bail!("invalid handler agent: {agent}");
            }
            for (action, argv) in actions {
                if !ACTIONS.contains(&action.as_str())
                    || argv.is_empty()
                    || argv.iter().any(|v| v.is_empty())
                {
                    bail!("handlers must be fixed, nonempty argv lists");
                }
            }
        }
        Ok(())
    }
}

pub fn valid_command_id(value: &str) -> bool {
    identifier(value, 64)
}
