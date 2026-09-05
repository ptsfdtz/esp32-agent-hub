use crate::config::AGENTS;
use anyhow::{Result, bail};
use serde_json::{Value, json};
use std::{
    fs,
    path::Path,
    time::{SystemTime, UNIX_EPOCH},
};
use sysinfo::{Pid, System};

pub fn unix_time() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs() as i64
}

fn bounded_text(value: Option<&Value>, limit: usize) -> String {
    let text = value
        .and_then(Value::as_str)
        .unwrap_or("")
        .replace('\0', "");
    let mut end = text.len().min(limit);
    while !text.is_char_boundary(end) {
        end -= 1;
    }
    text[..end].to_owned()
}

pub fn write_state(directory: &Path, agent: &str, state: &Value) -> Result<()> {
    if !AGENTS.contains(&agent) {
        bail!("unknown agent");
    }
    fs::create_dir_all(directory)?;
    let final_path = directory.join(format!("{agent}.json"));
    let temporary = directory.join(format!(".{agent}-{}.tmp", std::process::id()));
    fs::write(&temporary, serde_json::to_vec(state)?)?;
    if final_path.exists() {
        fs::remove_file(&final_path)?;
    }
    fs::rename(temporary, final_path)?;
    Ok(())
}

pub fn process_matches(pid: u32, started: u64) -> bool {
    let system = System::new_all();
    system
        .process(Pid::from_u32(pid))
        .is_some_and(|p| p.start_time().abs_diff(started) <= 1)
}

pub fn read_raw(directory: &Path, agent: &str) -> Result<Value> {
    let path = directory.join(format!("{agent}.json"));
    if fs::metadata(&path)?.len() > 4096 {
        bail!("state exceeds 4096 bytes");
    }
    Ok(serde_json::from_slice(&fs::read(path)?)?)
}

pub fn read_state(directory: &Path, agent: &str, now: i64) -> (Value, Option<Value>) {
    let offline = || json!({"online":false,"working":false,"model":"","task":"","ts":now});
    let Ok(value) = read_raw(directory, agent) else {
        return (offline(), None);
    };
    let Some(object) = value.as_object() else {
        return (offline(), None);
    };
    let Some(stamp) = object.get("ts").and_then(Value::as_i64) else {
        return (offline(), None);
    };
    if !(-5..15).contains(&(now - stamp)) {
        return (offline(), None);
    }
    let (Some(online), Some(working)) = (
        object.get("online").and_then(Value::as_bool),
        object.get("working").and_then(Value::as_bool),
    ) else {
        return (offline(), None);
    };
    if let Some(pid) = object.get("pid").and_then(Value::as_u64) {
        let Some(started) = object.get("process_started").and_then(Value::as_u64) else {
            return (offline(), None);
        };
        if pid > u32::MAX as u64 || !process_matches(pid as u32, started) {
            return (offline(), None);
        }
    }
    let mut status = json!({"online":online,"working":online && working,"model":bounded_text(object.get("model"),31),
        "task":bounded_text(object.get("task"),79),"ts":now});
    if let Some(completed) = object
        .get("completed_at")
        .and_then(Value::as_u64)
        .filter(|v| *v <= (now + 5) as u64)
    {
        status["completed_at"] = json!(completed);
    }
    let usage = object.get("usage").and_then(|u| {
        let five = u.get("five_hour")?.as_f64()?; let week = u.get("weekly")?.as_f64()?;
        let five_reset = u.get("five_hour_reset")?.as_u64()?; let week_reset = u.get("weekly_reset")?.as_u64()?;
        if !(0.0..=100.0).contains(&five) || !(0.0..=100.0).contains(&week) || five_reset > u32::MAX as u64 || week_reset > u32::MAX as u64 { return None; }
        let elapsed = now.saturating_sub(stamp) as u64;
        Some(json!({"five_hour":five,"weekly":week,"five_hour_reset":five_reset.saturating_sub(elapsed),"weekly_reset":week_reset.saturating_sub(elapsed)}))
    });
    (status, usage)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn utf8_is_bounded() {
        let value = json!("真实任务".repeat(50));
        assert!(bounded_text(Some(&value), 79).len() <= 79);
    }
    #[test]
    fn missing_is_offline() {
        let dir = tempfile::tempdir().unwrap();
        assert!(
            !read_state(dir.path(), "codex", 100).0["online"]
                .as_bool()
                .unwrap()
        );
    }
    #[test]
    fn expiry_and_usage_countdown() {
        let dir = tempfile::tempdir().unwrap();
        write_state(dir.path(),"codex",&json!({"online":true,"working":true,"ts":100,"usage":{"five_hour":21,"weekly":40,"five_hour_reset":20,"weekly_reset":200}})).unwrap();
        let (s, u) = read_state(dir.path(), "codex", 105);
        assert!(s["working"].as_bool().unwrap());
        assert_eq!(u.unwrap()["five_hour_reset"], 15);
        assert!(
            !read_state(dir.path(), "codex", 120).0["online"]
                .as_bool()
                .unwrap()
        );
    }
}
