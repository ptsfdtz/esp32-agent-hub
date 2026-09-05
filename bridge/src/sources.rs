use crate::state::unix_time;
use serde_json::{Value, json};
use std::{
    io::{BufRead, BufReader, Write},
    process::{Command, Stdio},
    sync::{
        Arc, Mutex,
        atomic::{AtomicBool, Ordering},
    },
    thread,
    time::{Duration, Instant},
};
use sysinfo::{Networks, System};

pub struct PcMetrics {
    system: System,
    networks: Networks,
    previous: u64,
    at: Instant,
    gpu: Option<f64>,
    gpu_at: Instant,
}
impl PcMetrics {
    pub fn new() -> Self {
        let mut system = System::new_all();
        system.refresh_cpu_usage();
        let networks = Networks::new_with_refreshed_list();
        let previous = networks.values().map(|n| n.total_received()).sum();
        Self {
            system,
            networks,
            previous,
            at: Instant::now(),
            gpu: None,
            gpu_at: Instant::now() - Duration::from_secs(10),
        }
    }
    pub fn sample(&mut self) -> Value {
        let now = Instant::now();
        self.system.refresh_cpu_usage();
        self.system.refresh_memory();
        self.networks.refresh(true);
        let received: u64 = self.networks.values().map(|n| n.total_received()).sum();
        let down = ((received.saturating_sub(self.previous) as f64) * 8.0
            / now.duration_since(self.at).as_secs_f64().max(0.001)
            / 1000.0)
            .round()
            .min(u32::MAX as f64) as u64;
        self.previous = received;
        self.at = now;
        if now.duration_since(self.gpu_at) >= Duration::from_secs(10) {
            self.gpu_at = now;
            self.gpu = query_gpu();
        }
        let ram = if self.system.total_memory() == 0 {
            0.0
        } else {
            self.system.used_memory() as f64 * 100.0 / self.system.total_memory() as f64
        };
        json!({"online":true,"cpu":self.system.global_cpu_usage(),"ram":ram,"gpu":self.gpu,"down_kbps":down,"ts":unix_time()})
    }
}
fn query_gpu() -> Option<f64> {
    let output = Command::new("nvidia-smi")
        .args([
            "--query-gpu=utilization.gpu",
            "--format=csv,noheader,nounits",
        ])
        .creation_flags_no_window()
        .output()
        .ok()?;
    if !output.status.success() {
        return None;
    }
    String::from_utf8(output.stdout)
        .ok()?
        .lines()
        .filter_map(|v| v.trim().parse::<f64>().ok())
        .filter(|v| (0.0..=100.0).contains(v))
        .reduce(f64::max)
}

trait CommandExt {
    fn creation_flags_no_window(&mut self) -> &mut Self;
}
impl CommandExt for Command {
    fn creation_flags_no_window(&mut self) -> &mut Self {
        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
            self.creation_flags(0x08000000);
        }
        self
    }
}

pub fn parse_rate_limits(result: &Value, now: i64) -> Option<Value> {
    let limits = result
        .get("rateLimitsByLimitId")
        .and_then(|v| v.get("codex"))
        .or_else(|| result.get("rateLimits"))?;
    let windows = [limits.get("primary"), limits.get("secondary")];
    let find = |duration: u64| {
        windows
            .iter()
            .flatten()
            .find(|w| w.get("windowDurationMins").and_then(Value::as_u64) == Some(duration))
            .copied()
    };
    let short = find(300)?;
    let week = find(10080)?;
    let fp = short.get("usedPercent")?.as_f64()?;
    let wp = week.get("usedPercent")?.as_f64()?;
    if !(0.0..=100.0).contains(&fp) || !(0.0..=100.0).contains(&wp) {
        return None;
    }
    Some(json!({"five_hour":fp,"weekly":wp,
        "five_hour_reset":short.get("resetsAt")?.as_i64()?.saturating_sub(now),
        "weekly_reset":week.get("resetsAt")?.as_i64()?.saturating_sub(now)}))
}

#[derive(Clone)]
pub struct UsageCache {
    inner: Arc<Mutex<Option<(i64, Value)>>>,
    reachable: Arc<AtomicBool>,
}
impl UsageCache {
    pub fn start(command: Option<Vec<String>>, stop: Arc<AtomicBool>) -> Self {
        let cache = Self {
            inner: Arc::new(Mutex::new(None)),
            reachable: Arc::new(AtomicBool::new(false)),
        };
        if let Some(command) = command {
            let target = cache.inner.clone();
            let reachable = cache.reachable.clone();
            thread::spawn(move || {
                while !stop.load(Ordering::Relaxed) {
                    let result = query_codex(&command).unwrap_or((false, None));
                    reachable.store(result.0, Ordering::Relaxed);
                    *target.lock().unwrap() = result.1.map(|v| (unix_time(), v));
                    for _ in 0..60 {
                        if stop.load(Ordering::Relaxed) {
                            break;
                        }
                        thread::sleep(Duration::from_secs(1));
                    }
                }
            });
        }
        cache
    }
    pub fn reachable(&self) -> bool {
        self.reachable.load(Ordering::Relaxed)
    }
    pub fn sample(&self) -> Option<Value> {
        let guard = self.inner.lock().ok()?;
        let (measured, value) = guard.as_ref()?;
        let elapsed = unix_time() - *measured;
        if elapsed > 120 {
            return None;
        }
        let mut out = value.clone();
        for key in ["five_hour_reset", "weekly_reset"] {
            if let Some(v) = out[key].as_i64() {
                out[key] = json!(v.saturating_sub(elapsed).max(0));
            }
        }
        Some(out)
    }
}
fn query_codex(argv: &[String]) -> anyhow::Result<(bool, Option<Value>)> {
    if argv.is_empty() {
        return Ok((false, None));
    }
    let mut child = Command::new(&argv[0])
        .args(&argv[1..])
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::null())
        .creation_flags_no_window()
        .spawn()?;
    let mut input = child.stdin.take().unwrap();
    let output = child.stdout.take().unwrap();
    writeln!(
        input,
        "{}",
        json!({"id":1,"method":"initialize","params":{"clientInfo":{"name":"agentdeck_bridge","version":"0.2.0"}}})
    )?;
    writeln!(input, "{}", json!({"method":"initialized","params":{}}))?;
    writeln!(
        input,
        "{}",
        json!({"id":2,"method":"account/rateLimits/read"})
    )?;
    input.flush()?;
    let (tx, rx) = std::sync::mpsc::channel();
    thread::spawn(move || {
        for line in BufReader::new(output).lines().map_while(Result::ok) {
            if line.len() <= 65536 {
                if let Ok(v) = serde_json::from_str::<Value>(&line) {
                    let _ = tx.send(v);
                }
            }
        }
    });
    let deadline = Instant::now() + Duration::from_secs(12);
    let mut answer = None;
    let mut initialized = false;
    while Instant::now() < deadline {
        if let Ok(v) = rx.recv_timeout(Duration::from_millis(200)) {
            if v["id"] == 1 && v.get("result").is_some() {
                initialized = true;
            }
            if v["id"] == 2 {
                answer = v
                    .get("result")
                    .and_then(|r| parse_rate_limits(r, unix_time()));
                break;
            }
        }
        if child.try_wait()?.is_some() {
            break;
        }
    }
    let _ = child.kill();
    let _ = child.wait();
    Ok((initialized, answer))
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn official_windows_only() {
        let v = json!({"rateLimits":{"primary":{"windowDurationMins":300,"usedPercent":22,"resetsAt":1100},"secondary":{"windowDurationMins":10080,"usedPercent":45,"resetsAt":2100}}});
        assert_eq!(parse_rate_limits(&v, 1000).unwrap()["five_hour_reset"], 100);
    }
    #[test]
    fn pc_metrics_are_real() {
        let v = PcMetrics::new().sample();
        assert!(v["online"].as_bool().unwrap());
        assert!((0.0..=100.0).contains(&v["cpu"].as_f64().unwrap()));
    }
}
