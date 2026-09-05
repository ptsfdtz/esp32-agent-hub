use crate::{
    config::{ACTIONS, AGENTS, Config, valid_command_id},
    sources::{PcMetrics, UsageCache},
    state::{read_state, unix_time},
};
use anyhow::Result;
use rumqttc::{
    Client, ConnectionError, Event, Incoming, LastWill, MqttOptions, Outgoing, Publish, QoS,
    Transport,
};
use rusqlite::{Connection, params};
use serde_json::{Value, json};
use std::{
    io::Write,
    process::{Command, Stdio},
    sync::{
        Arc,
        atomic::{AtomicBool, Ordering},
        mpsc,
    },
    thread,
    time::{Duration, Instant},
};

#[derive(Debug)]
struct IncomingCommand {
    agent: String,
    data: Value,
}

fn accepted(publish: &Publish, cfg: &Config) -> Option<IncomingCommand> {
    if publish.retain || publish.payload.len() > 1024 {
        return None;
    }
    let parts: Vec<_> = publish.topic.split('/').collect();
    if parts.len() != 3
        || parts[0] != "agent"
        || !AGENTS.contains(&parts[1])
        || parts[2] != "command"
    {
        return None;
    }
    let data: Value = serde_json::from_slice(&publish.payload).ok()?;
    let object = data.as_object()?;
    let device = object.get("device_id")?.as_str()?;
    if !cfg.devices.iter().any(|d| d == device) {
        return None;
    }
    if !valid_command_id(object.get("id")?.as_str()?) {
        return None;
    }
    Some(IncomingCommand {
        agent: parts[1].into(),
        data,
    })
}
fn ack(data: &Value, status: &str, reason: &str) -> (String, Value) {
    (
        format!("agentdeck/{}/ack", data["device_id"].as_str().unwrap()),
        json!({"id":data["id"],"status":status,"reason":reason,"ts":unix_time()}),
    )
}

fn handle(db: &Connection, cfg: &Config, item: IncomingCommand) -> (String, Value) {
    let data = item.data;
    let now = unix_time();
    let Some(action) = data["action"].as_str() else {
        return ack(&data, "rejected", "expired_or_invalid");
    };
    let valid = ACTIONS.contains(&action)
        && data["ts"]
            .as_i64()
            .is_some_and(|v| (now - 15..=now + 5).contains(&v))
        && data["expires_at"]
            .as_i64()
            .is_some_and(|v| (now..=now + 30).contains(&v));
    if !valid {
        return ack(&data, "rejected", "expired_or_invalid");
    }
    let device = data["device_id"].as_str().unwrap();
    let id = data["id"].as_str().unwrap();
    let existing = db
        .query_row(
            "SELECT agent,action,status FROM commands WHERE device=?1 AND id=?2",
            params![device, id],
            |r| {
                Ok((
                    r.get::<_, String>(0)?,
                    r.get::<_, String>(1)?,
                    r.get::<_, String>(2)?,
                ))
            },
        )
        .ok();
    if let Some((old_agent, old_action, status)) = existing {
        return ack(
            &data,
            if old_agent == item.agent && old_action == action && status != "executing" {
                &status
            } else {
                "rejected"
            },
            "duplicate_or_unknown",
        );
    }
    let (state, _) = read_state(&cfg.state_dir, &item.agent, now);
    let online = state["online"].as_bool().unwrap_or(false);
    let argv = cfg.handlers.get(&item.agent).and_then(|m| m.get(action));
    if !online || argv.is_none() {
        return ack(
            &data,
            "rejected",
            if online {
                "unsupported_action"
            } else {
                "agent_offline"
            },
        );
    }
    db.execute(
        "INSERT INTO commands VALUES(?1,?2,?3,?4,'executing',?5)",
        params![device, id, item.agent, action, now],
    )
    .ok();
    let mut envelope = data.clone();
    envelope["agent"] = json!(item.agent);
    let bytes = serde_json::to_vec(&envelope).unwrap();
    let argv = argv.unwrap();
    let result = Command::new(&argv[0])
        .args(&argv[1..])
        .stdin(Stdio::piped())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
        .and_then(|mut child| {
            child.stdin.take().unwrap().write_all(&bytes)?;
            let deadline = Instant::now() + Duration::from_secs(5);
            loop {
                if let Some(status) = child.try_wait()? {
                    break Ok(status.success());
                }
                if Instant::now() >= deadline {
                    let _ = child.kill();
                    let _ = child.wait();
                    break Ok(false);
                }
                thread::sleep(Duration::from_millis(25));
            }
        })
        .unwrap_or(false);
    let status = if result { "completed" } else { "failed" };
    db.execute(
        "UPDATE commands SET status=?1 WHERE device=?2 AND id=?3",
        params![status, device, id],
    )
    .ok();
    db.execute("DELETE FROM commands WHERE ts < ?1", params![now - 86400])
        .ok();
    ack(&data, status, "")
}

pub fn run(cfg: Config, stop: Arc<AtomicBool>) -> Result<()> {
    std::fs::create_dir_all(&cfg.state_dir)?;
    let mut options = MqttOptions::new(&cfg.client_id, &cfg.host, cfg.port);
    options.set_keep_alive(Duration::from_secs(10));
    options.set_last_will(LastWill::new(
        "pc/status",
        serde_json::to_vec(&json!({"online":false,"ts":unix_time()}))?,
        QoS::AtLeastOnce,
        true,
    ));
    if let Ok(user) = std::env::var(&cfg.username_env) {
        options.set_credentials(user, std::env::var(&cfg.password_env).unwrap_or_default());
    }
    if let Some(ca) = &cfg.tls_ca {
        options.set_transport(Transport::tls(std::fs::read(ca)?, None, None));
    }
    let (client, mut connection) = Client::new(options, 32);
    let (command_tx, command_rx) = mpsc::sync_channel(16);
    let (out_tx, out_rx) = mpsc::sync_channel::<(String, Value)>(64);
    let connected = Arc::new(AtomicBool::new(false));
    let event_stop = stop.clone();
    let event_connected = connected.clone();
    let event_cfg = cfg.clone();
    let event_client = client.clone();
    let event = thread::spawn(move || {
        while !event_stop.load(Ordering::Relaxed) {
            match connection.recv_timeout(Duration::from_millis(200)) {
                Ok(Ok(Event::Incoming(Incoming::ConnAck(_)))) => {
                    for agent in AGENTS {
                        let _ = event_client
                            .subscribe(format!("agent/{agent}/command"), QoS::AtLeastOnce);
                    }
                    event_connected.store(true, Ordering::Relaxed);
                    println!("MQTT connected");
                }
                Ok(Ok(Event::Incoming(Incoming::Publish(p)))) => {
                    if let Some(c) = accepted(&p, &event_cfg) {
                        let _ = command_tx.try_send(c);
                    }
                }
                Ok(Ok(Event::Outgoing(Outgoing::Disconnect)))
                | Ok(Err(ConnectionError::RequestsDone)) => break,
                Ok(Err(e)) => {
                    event_connected.store(false, Ordering::Relaxed);
                    eprintln!("MQTT: {e}");
                    thread::sleep(Duration::from_secs(1));
                }
                Err(_) | Ok(Ok(_)) => {}
            }
        }
    });
    let worker_cfg = cfg.clone();
    let worker_stop = stop.clone();
    let worker = thread::spawn(move || {
        let db = Connection::open(worker_cfg.state_dir.join("commands.sqlite3")).unwrap();
        db.execute("CREATE TABLE IF NOT EXISTS commands(device TEXT,id TEXT,agent TEXT,action TEXT,status TEXT,ts INTEGER,PRIMARY KEY(device,id))",[]).unwrap();
        while !worker_stop.load(Ordering::Relaxed) {
            if let Ok(item) = command_rx.recv_timeout(Duration::from_millis(200)) {
                let _ = out_tx.try_send(handle(&db, &worker_cfg, item));
            }
        }
    });
    let usage = UsageCache::start(cfg.codex_usage_command.clone(), stop.clone());
    let mut metrics = PcMetrics::new();
    let mut next = Instant::now();
    while !stop.load(Ordering::Relaxed) {
        if connected.load(Ordering::Relaxed) && Instant::now() >= next {
            next = Instant::now() + Duration::from_secs(cfg.interval);
            let body = metrics.sample();
            let _ = client.publish(
                "pc/status",
                QoS::AtMostOnce,
                false,
                serde_json::to_vec(&body)?,
            );
            for agent in AGENTS {
                let (mut status, state_usage) = read_state(&cfg.state_dir, agent, unix_time());
                if agent == "codex"
                    && !status["online"].as_bool().unwrap_or(false)
                    && usage.reachable()
                {
                    status["online"] = json!(true);
                    status["working"] = json!(false);
                }
                let agent_usage = if agent == "codex" {
                    usage.sample().or(state_usage)
                } else {
                    state_usage
                };
                let usage_body =
                    agent_usage.unwrap_or_else(|| json!({"available":false,"ts":unix_time()}));
                let _ = client.publish(
                    format!("agent/{agent}/status"),
                    QoS::AtMostOnce,
                    false,
                    serde_json::to_vec(&status)?,
                );
                let _ = client.publish(
                    format!("agent/{agent}/usage"),
                    QoS::AtMostOnce,
                    false,
                    serde_json::to_vec(&usage_body)?,
                );
            }
        }
        while let Ok((topic, body)) = out_rx.try_recv() {
            if unix_time() - body["ts"].as_i64().unwrap_or(0) <= 15 {
                let _ = client.publish(topic, QoS::AtMostOnce, false, serde_json::to_vec(&body)?);
            }
        }
        thread::sleep(Duration::from_millis(50));
    }
    for agent in AGENTS {
        let _ = client.publish(
            format!("agent/{agent}/status"),
            QoS::AtMostOnce,
            false,
            serde_json::to_vec(&json!({"online":false,"working":false,"ts":unix_time()}))?,
        );
    }
    let _ = client.publish(
        "pc/status",
        QoS::AtLeastOnce,
        true,
        serde_json::to_vec(&json!({"online":false,"ts":unix_time()}))?,
    );
    let _ = client.disconnect();
    let _ = worker.join();
    let _ = event.join();
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn retained_and_unauthorized_rejected() {
        let mut cfg = Config::default();
        cfg.host = "x".into();
        cfg.devices = vec!["deck".into()];
        let p = Publish::new(
            "agent/codex/command",
            QoS::AtLeastOnce,
            json!({"device_id":"deck","id":"x"}).to_string(),
        );
        assert!(accepted(&p, &cfg).is_some());
        let mut retained = p.clone();
        retained.retain = true;
        assert!(accepted(&retained, &cfg).is_none());
    }
}
