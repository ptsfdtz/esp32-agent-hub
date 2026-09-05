use agentdeck_bridge::{
    config::{AGENTS, Config},
    service,
    state::{process_matches, read_raw, read_state, unix_time, write_state},
};
use anyhow::{Context, Result, bail};
use clap::{Parser, Subcommand};
use serde_json::{Value, json};
use std::{
    collections::HashMap,
    io::{self, BufRead, Read},
    path::PathBuf,
    process::Command,
    sync::{
        Arc,
        atomic::{AtomicBool, Ordering},
    },
    thread,
    time::Duration,
};
use sysinfo::{Pid, Signal, System};

#[derive(Parser)]
#[command(
    name = "agentdeck-bridge",
    version,
    about = "Agent Deck PC bridge and task runner"
)]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}
#[derive(Subcommand)]
enum Commands {
    Service {
        #[arg(long, default_value = "bridge/config.json")]
        config: PathBuf,
    },
    Configure {
        #[arg(long)]
        host: String,
        #[arg(long, default_value_t = 1883)]
        port: u16,
        #[arg(long, default_value = "agentdeck-01")]
        device: String,
        #[arg(long, default_value = "bridge/config.json")]
        output: PathBuf,
    },
    Provision {
        #[arg(long)]
        ssid: Option<String>,
        #[arg(long)]
        wifi_password: Option<String>,
        #[arg(long)]
        mqtt_host: Option<String>,
        #[arg(long, default_value_t = 1883)]
        mqtt_port: u16,
        #[arg(long)]
        mqtt_user: Option<String>,
        #[arg(long)]
        mqtt_password: Option<String>,
        #[arg(long, default_value = "agentdeck-01")]
        device: String,
    },
    Run {
        #[arg(long, default_value = "bridge/.state")]
        state_dir: PathBuf,
        #[arg(long)]
        agent: String,
        #[arg(long)]
        task: String,
        #[arg(long, default_value = "")]
        model: String,
        #[arg(last = true, required = true)]
        command: Vec<String>,
    },
    Feed {
        #[arg(long, default_value = "bridge/.state")]
        state_dir: PathBuf,
        #[arg(long)]
        agent: String,
    },
    Control {
        #[arg(long, default_value = "bridge/.state")]
        state_dir: PathBuf,
    },
}
fn valid_agent(agent: &str) -> Result<()> {
    if AGENTS.contains(&agent) {
        Ok(())
    } else {
        bail!("unknown agent: {agent}")
    }
}

fn configure(host: String, port: u16, device: String, output: PathBuf) -> Result<()> {
    let mut cfg = Config::default();
    cfg.host = host;
    cfg.port = port;
    cfg.devices = vec![device];
    cfg.state_dir = PathBuf::from(".state");
    let output_absolute = if output.is_absolute() {
        output.clone()
    } else {
        std::env::current_dir()?.join(&output)
    };
    let handler = vec![
        std::env::current_exe()?.to_string_lossy().into_owned(),
        "control".into(),
        "--state-dir".into(),
        output_absolute
            .parent()
            .unwrap_or_else(|| std::path::Path::new("."))
            .join(".state")
            .to_string_lossy()
            .into_owned(),
    ];
    for agent in AGENTS {
        cfg.handlers.insert(
            agent.into(),
            HashMap::from([
                ("stop".into(), handler.clone()),
                ("cancel".into(), handler.clone()),
            ]),
        );
    }
    cfg.validate()?;
    if output.exists() {
        bail!("{} already exists", output.display())
    }
    if let Some(parent) = output.parent() {
        std::fs::create_dir_all(parent)?;
    }
    std::fs::write(&output, serde_json::to_vec_pretty(&cfg)?)?;
    println!(
        "Created {}. Start: agentdeck-bridge service --config {}",
        output.display(),
        output.display()
    );
    Ok(())
}

fn control(directory: PathBuf) -> Result<i32> {
    let mut input = Vec::new();
    io::stdin().take(1025).read_to_end(&mut input)?;
    let envelope: Value = serde_json::from_slice(&input)?;
    let agent = envelope["agent"].as_str().context("missing agent")?;
    valid_agent(agent)?;
    if !matches!(envelope["action"].as_str(), Some("stop" | "cancel"))
        || envelope["expires_at"].as_i64().unwrap_or(0) < unix_time()
    {
        return Ok(2);
    }
    let (status, _) = read_state(&directory, agent, unix_time());
    if !status["online"].as_bool().unwrap_or(false) {
        return Ok(3);
    }
    let raw = read_raw(&directory, agent)?;
    if raw["source"] != "agentdeck-runner" {
        return Ok(3);
    }
    let pid = raw["pid"].as_u64().context("missing pid")? as u32;
    let started = raw["process_started"]
        .as_u64()
        .context("missing process identity")?;
    if !process_matches(pid, started) {
        return Ok(3);
    }
    let mut system = System::new_all();
    let root = Pid::from_u32(pid);
    let mut targets = vec![root];
    loop {
        let found: Vec<_> = system
            .processes()
            .iter()
            .filter(|(pid, p)| {
                p.parent().is_some_and(|parent| targets.contains(&parent)) && !targets.contains(pid)
            })
            .map(|(pid, _)| *pid)
            .collect();
        if found.is_empty() {
            break;
        }
        targets.extend(found);
    }
    for target in targets.iter().rev() {
        if let Some(p) = system.process(*target) {
            p.kill_with(Signal::Term);
        }
    }
    thread::sleep(Duration::from_secs(1));
    system.refresh_all();
    let mut terminated = system.process(root).is_none();
    for target in targets.iter().rev() {
        if let Some(p) = system.process(*target) {
            terminated |= p.kill_with(Signal::Kill).unwrap_or(false);
        }
    }
    Ok(if terminated { 0 } else { 1 })
}

fn run_agent(
    directory: PathBuf,
    agent: String,
    task: String,
    model: String,
    command: Vec<String>,
) -> Result<i32> {
    valid_agent(&agent)?;
    if read_state(&directory, &agent, unix_time()).0["online"].as_bool() == Some(true) {
        bail!("this agent slot already has a live producer")
    }
    let mut child = Command::new(&command[0]).args(&command[1..]).spawn()?;
    let pid = child.id();
    let system = System::new_all();
    let started = system
        .process(Pid::from_u32(pid))
        .context("cannot identify child")?
        .start_time();
    let base = json!({"online":true,"working":true,"model":model,"task":task,"pid":pid,"process_started":started,"source":"agentdeck-runner"});
    while child.try_wait()?.is_none() {
        let mut state = base.clone();
        state["ts"] = json!(unix_time());
        write_state(&directory, &agent, &state)?;
        thread::sleep(Duration::from_secs(1));
    }
    let status = child.wait()?;
    write_state(
        &directory,
        &agent,
        &json!({"online":false,"working":false,"ts":unix_time(),"task":task,"model":model,"completed_at":if status.success(){unix_time()}else{0}}),
    )?;
    Ok(status.code().unwrap_or(1))
}

fn feed(directory: PathBuf, agent: String) -> Result<()> {
    valid_agent(&agent)?;
    for line in io::stdin().lock().lines() {
        let line = line?;
        if line.len() > 4096 {
            bail!("event exceeds 4096 bytes")
        }
        let mut event: Value = serde_json::from_str(&line)?;
        if event["online"].as_bool().is_none() || event["working"].as_bool().is_none() {
            bail!("each event requires actual online/working booleans")
        }
        event["ts"] = json!(unix_time());
        write_state(&directory, &agent, &event)?;
    }
    Ok(())
}

fn main() -> Result<()> {
    let cli = Cli::parse();
    let code = match cli.command {
        Commands::Service { config } => {
            let stop = Arc::new(AtomicBool::new(false));
            let signal_stop = stop.clone();
            ctrlc::set_handler(move || signal_stop.store(true, Ordering::Relaxed))?;
            service::run(Config::load(&config)?, stop)?;
            0
        }
        Commands::Configure {
            host,
            port,
            device,
            output,
        } => {
            configure(host, port, device, output)?;
            0
        }
        Commands::Provision {
            ssid,
            wifi_password,
            mqtt_host,
            mqtt_port,
            mqtt_user,
            mqtt_password,
            device,
        } => {
            let runtime = tokio::runtime::Runtime::new()?;
            runtime.block_on(agentdeck_bridge::provision::run(
                agentdeck_bridge::provision::Options {
                    ssid,
                    wifi_password,
                    mqtt_host,
                    mqtt_port,
                    mqtt_user,
                    mqtt_password,
                    device_id: device,
                },
            ))?;
            0
        }
        Commands::Run {
            state_dir,
            agent,
            task,
            model,
            command,
        } => run_agent(state_dir, agent, task, model, command)?,
        Commands::Feed { state_dir, agent } => {
            feed(state_dir, agent)?;
            0
        }
        Commands::Control { state_dir } => control(state_dir)?,
    };
    std::process::exit(code)
}
