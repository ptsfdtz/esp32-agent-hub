use anyhow::{Context, Result, bail};
use btleplug::{
    api::{Central, CharPropFlags, Manager as _, Peripheral as _, ScanFilter, WriteType},
    platform::Manager,
};
use serde_json::json;
use std::{
    io::{self, Write},
    time::Duration,
};
use uuid::Uuid;

const CONFIG_UUID: Uuid = Uuid::from_u128(0x7ce40002_5f9c_4e77_a14b_4cf9ec720001);
const STATUS_UUID: Uuid = Uuid::from_u128(0x7ce40003_5f9c_4e77_a14b_4cf9ec720001);

#[derive(Default)]
pub struct Options {
    pub ssid: Option<String>,
    pub wifi_password: Option<String>,
    pub mqtt_host: Option<String>,
    pub mqtt_port: u16,
    pub mqtt_user: Option<String>,
    pub mqtt_password: Option<String>,
    pub device_id: String,
}

fn prompt(label: &str, default: Option<&str>) -> Result<String> {
    print!(
        "{label}{}: ",
        default.map(|v| format!(" [{v}]")).unwrap_or_default()
    );
    io::stdout().flush()?;
    let mut value = String::new();
    io::stdin().read_line(&mut value)?;
    let value = value.trim();
    Ok(if value.is_empty() {
        default.unwrap_or("").to_owned()
    } else {
        value.to_owned()
    })
}

fn validate(value: &str, max: usize, label: &str, required: bool) -> Result<()> {
    if (required && value.is_empty()) || value.len() > max || !value.is_char_boundary(value.len()) {
        bail!("invalid {label}");
    }
    Ok(())
}

pub async fn run(mut options: Options) -> Result<()> {
    let default_host = local_ip_address::local_ip()
        .ok()
        .filter(|ip| ip.is_ipv4())
        .map(|ip| ip.to_string());
    let ssid = match options.ssid.take() {
        Some(v) => v,
        None => prompt("Wi-Fi name", None)?,
    };
    let wifi_password = match options.wifi_password.take() {
        Some(v) => v,
        None => rpassword::prompt_password("Wi-Fi password (blank for open network): ")?,
    };
    let mqtt_host = match options.mqtt_host.take() {
        Some(v) => v,
        None => prompt("MQTT host", default_host.as_deref())?,
    };
    let mqtt_user = match options.mqtt_user.take() {
        Some(v) => v,
        None => prompt("MQTT user (optional)", None)?,
    };
    let mqtt_password = match options.mqtt_password.take() {
        Some(v) => v,
        None => {
            if mqtt_user.is_empty() {
                String::new()
            } else {
                rpassword::prompt_password("MQTT password: ")?
            }
        }
    };
    validate(&ssid, 32, "Wi-Fi name", true)?;
    validate(&wifi_password, 64, "Wi-Fi password", false)?;
    validate(&mqtt_host, 63, "MQTT host", true)?;
    validate(&mqtt_user, 64, "MQTT user", false)?;
    validate(&mqtt_password, 64, "MQTT password", false)?;
    validate(&options.device_id, 32, "device ID", true)?;
    if options.mqtt_port == 0 {
        bail!("invalid MQTT port");
    }

    println!(
        "Scanning for Agent Deck. Power it on while holding BACK if it was configured before."
    );
    let manager = Manager::new()
        .await
        .context("cannot open Bluetooth manager")?;
    let adapters = manager
        .adapters()
        .await
        .context("cannot list Bluetooth adapters")?;
    let adapter = adapters
        .into_iter()
        .next()
        .context("no Bluetooth adapter found")?;
    adapter
        .start_scan(ScanFilter::default())
        .await
        .context("cannot start Bluetooth scan")?;
    tokio::time::sleep(Duration::from_secs(8)).await;
    let mut device = None;
    for peripheral in adapter.peripherals().await? {
        if peripheral
            .properties()
            .await?
            .and_then(|p| p.local_name)
            .is_some_and(|n| n.starts_with("AgentDeck Setup"))
        {
            device = Some(peripheral);
            break;
        }
    }
    let device = device.context("Agent Deck not found; enter setup mode and retry")?;
    println!("Agent Deck found. Connecting...");
    device
        .connect()
        .await
        .context("Bluetooth connection failed")?;
    device
        .discover_services()
        .await
        .context("cannot discover Agent Deck services")?;
    let config = device
        .characteristics()
        .into_iter()
        .find(|c| c.uuid == CONFIG_UUID && c.properties.contains(CharPropFlags::WRITE))
        .context("configuration characteristic missing")?;
    let status = device
        .characteristics()
        .into_iter()
        .find(|c| c.uuid == STATUS_UUID)
        .context("status characteristic missing")?;
    let mut payload = serde_json::to_vec(
        &json!({"ssid":ssid,"wifi_password":wifi_password,"mqtt_host":mqtt_host,"mqtt_port":options.mqtt_port,"mqtt_user":mqtt_user,"mqtt_password":mqtt_password,"device_id":options.device_id}),
    )?;
    payload.push(b'\n');
    for chunk in payload.chunks(18) {
        device
            .write(&config, chunk, WriteType::WithResponse)
            .await
            .context("configuration write failed")?;
    }
    tokio::time::sleep(Duration::from_millis(300)).await;
    let result = String::from_utf8_lossy(
        &device
            .read(&status)
            .await
            .context("cannot read device result")?,
    )
    .into_owned();
    if result != "saved" {
        bail!("device rejected configuration: {result}");
    }
    println!("Configuration saved. Agent Deck is restarting.");
    Ok(())
}
