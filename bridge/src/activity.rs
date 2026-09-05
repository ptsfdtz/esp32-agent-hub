//! Read-only Codex rollout observer. Extracts lifecycle/model metadata, never publishes prompt text.
use serde_json::{Value, json};
use std::{
    collections::HashMap,
    fs,
    io::{BufRead, BufReader, Seek, SeekFrom},
    path::{Path, PathBuf},
    time::SystemTime,
};

#[derive(Default, Clone)]
struct Turn {
    working: bool,
    model: String,
    updated_at: i64,
}
impl Turn {
    fn event(&mut self, value: &Value) {
        if let Some(stamp) = value["timestamp"]
            .as_str()
            .and_then(|s| chrono::DateTime::parse_from_rfc3339(s).ok())
        {
            self.updated_at = self.updated_at.max(stamp.timestamp());
        }
        let payload = &value["payload"];
        match value["type"].as_str() {
            Some("turn_context") => {
                if let Some(model) = payload["model"].as_str() {
                    self.model = model.chars().take(31).collect();
                }
            }
            Some("event_msg") => match payload["type"].as_str() {
                Some("task_started") => self.working = true,
                Some("task_complete" | "task_aborted" | "turn_aborted") => self.working = false,
                _ => {}
            },
            _ => {}
        }
    }
}
#[derive(Default)]
pub struct CodexActivity {
    files: HashMap<PathBuf, (SystemTime, u64, Turn)>,
}
impl CodexActivity {
    pub fn sample(&mut self, now: i64) -> Option<Value> {
        let root = std::env::var_os("CODEX_HOME")
            .map(PathBuf::from)
            .or_else(|| {
                std::env::var_os("USERPROFILE")
                    .or_else(|| std::env::var_os("HOME"))
                    .map(|v| PathBuf::from(v).join(".codex"))
            })?
            .join("sessions");
        self.sample_root(&root, now)
    }
    fn sample_root(&mut self, root: &Path, now: i64) -> Option<Value> {
        let mut paths = Vec::new();
        fn visit(dir: &Path, out: &mut Vec<(SystemTime, PathBuf)>) {
            let Ok(entries) = fs::read_dir(dir) else {
                return;
            };
            for entry in entries.flatten() {
                let Ok(kind) = entry.file_type() else {
                    continue;
                };
                if kind.is_dir() {
                    visit(&entry.path(), out);
                } else if entry.path().extension().is_some_and(|v| v == "jsonl") {
                    if let Ok(modified) = entry.metadata().and_then(|m| m.modified()) {
                        out.push((modified, entry.path()));
                    }
                }
            }
        }
        visit(&root, &mut paths);
        paths.sort_by(|a, b| b.0.cmp(&a.0));
        paths.truncate(16);
        self.files
            .retain(|p, _| paths.iter().any(|(_, path)| path == p));
        let mut latest = None;
        for (modified, path) in paths {
            let file_len = fs::metadata(&path).ok()?.len();
            if self
                .files
                .get(&path)
                .is_none_or(|(m, offset, _)| *m != modified || *offset != file_len)
            {
                let Ok(mut file) = fs::File::open(&path) else {
                    continue;
                };
                let len = file.metadata().ok()?.len();
                let previous = self
                    .files
                    .get(&path)
                    .filter(|(_, offset, _)| *offset <= len);
                let mut offset = previous.map(|(_, offset, _)| *offset).unwrap_or(0);
                let mut turn = previous
                    .map(|(_, _, turn)| turn.clone())
                    .unwrap_or_default();
                file.seek(SeekFrom::Start(offset)).ok()?;
                let mut reader = BufReader::new(file);
                let mut line = Vec::new();
                loop {
                    line.clear();
                    let size = reader.read_until(b'\n', &mut line).ok()?;
                    if size == 0 || line.last() != Some(&b'\n') {
                        break;
                    }
                    offset += size as u64;
                    if let Ok(value) = serde_json::from_slice::<Value>(&line) {
                        turn.event(&value);
                    }
                }
                self.files.insert(path.clone(), (modified, offset, turn));
            }
            let turn = &self.files.get(&path)?.2;
            if now.saturating_sub(turn.updated_at) > 300 {
                continue;
            }
            let status = json!({"online":true,"working":turn.working,"model":turn.model,"task":if turn.working {"IDE task"} else {""},"ts":now});
            if turn.working {
                return Some(status);
            }
            if latest.is_none() {
                latest = Some(status);
            }
        }
        latest
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn restores_long_session_and_follows_appended_completion() {
        use std::io::Write;
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("long.jsonl");
        let mut file = fs::File::create(&path).unwrap();
        writeln!(
            file,
            "{}",
            json!({"type":"event_msg","payload":{"type":"task_started"}})
        )
        .unwrap();
        file.write_all("{\"type\":\"ignored\"}\n".repeat(250_000).as_bytes())
            .unwrap();
        file.flush().unwrap();
        let original_modified = file.metadata().unwrap().modified().unwrap();
        let mut observer = CodexActivity::default();
        assert!(
            observer.sample_root(dir.path(), 0).unwrap()["working"]
                .as_bool()
                .unwrap()
        );
        writeln!(
            file,
            "{}",
            json!({"type":"event_msg","payload":{"type":"task_complete"}})
        )
        .unwrap();
        file.flush().unwrap();
        // Windows may retain mtime while the writer keeps its handle open.
        file.set_modified(original_modified).unwrap();
        assert!(
            !observer.sample_root(dir.path(), 1).unwrap()["working"]
                .as_bool()
                .unwrap()
        );
    }
    #[test]
    fn follows_task_lifecycle_not_messages_or_open_processes() {
        let mut turn = Turn::default();
        turn.event(&json!({"type":"event_msg","payload":{"type":"user_message"}}));
        assert!(!turn.working);
        turn.event(&json!({"type":"event_msg","payload":{"type":"task_started"}}));
        assert!(turn.working);
        turn.event(&json!({"type":"event_msg","payload":{"type":"agent_message"}}));
        assert!(turn.working);
        turn.event(&json!({"type":"event_msg","payload":{"type":"task_complete"}}));
        assert!(!turn.working);
        turn.event(&json!({"type":"event_msg","payload":{"type":"task_started"}}));
        turn.event(&json!({"type":"event_msg","payload":{"type":"turn_aborted"}}));
        assert!(!turn.working);
    }
    #[test]
    fn freshness_comes_from_event_timestamp() {
        let mut turn = Turn::default();
        turn.event(&json!({"timestamp":"2026-09-05T11:42:48.897Z","type":"event_msg","payload":{"type":"task_started"}}));
        assert!(turn.working);
        assert_eq!(turn.updated_at, 1788608568);
    }
}
