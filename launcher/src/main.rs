#![cfg_attr(windows, windows_subsystem = "windows")]
use std::{fs, io, path::PathBuf, process::Command};
include!(concat!(env!("OUT_DIR"), "/payload.rs"));
fn run() -> io::Result<()> {
    let root = PathBuf::from(
        std::env::var_os("LOCALAPPDATA").ok_or_else(|| io::Error::other("LOCALAPPDATA missing"))?,
    )
    .join("AgentDeck");
    fs::create_dir_all(&root)?;
    // Hold a per-user file lock for the entire supervisor lifetime.
    #[cfg(windows)]
    use std::os::windows::fs::OpenOptionsExt;
    let _lock = match fs::OpenOptions::new()
        .write(true)
        .create(true)
        .truncate(false)
        .share_mode(0)
        .open(root.join("launcher.lock"))
    {
        Ok(file) => file,
        Err(e) if e.raw_os_error() == Some(32) => return Ok(()),
        Err(e) => return Err(e),
    };
    for (name, bytes) in FILES {
        let path = root.join(name);
        if fs::read(&path).ok().as_deref() != Some(*bytes) {
            fs::create_dir_all(path.parent().unwrap())?;
            fs::write(path, bytes)?;
        }
    }
    let current = std::env::current_exe()?;
    let installed = root.join("AgentDeck.exe");
    if current != installed {
        fs::copy(&current, &installed)?;
        drop(_lock);
        let mut installed_cmd = Command::new(&installed);
        installed_cmd.arg(current.parent().unwrap());
        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
            installed_cmd.creation_flags(0x08000000);
        }
        installed_cmd.spawn()?;
        return Ok(());
    }
    let mut cmd = Command::new("powershell.exe");
    cmd.args([
        "-NoProfile",
        "-NonInteractive",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
    ])
    .arg(root.join("tools/desktop-start.ps1"))
    .arg("-SourceDirectory")
    .arg(
        std::env::args_os()
            .nth(1)
            .map(PathBuf::from)
            .unwrap_or_else(|| current.parent().unwrap().to_path_buf()),
    );
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        cmd.creation_flags(0x08000000);
    }
    loop {
        let status = cmd.status()?;
        if status.success() {
            break;
        }
        std::thread::sleep(std::time::Duration::from_secs(5));
    }
    Ok(())
}
fn main() {
    if let Err(e) = run() {
        if let Some(base) = std::env::var_os("LOCALAPPDATA") {
            let _ = fs::write(
                PathBuf::from(base).join("AgentDeck/launcher-error.log"),
                e.to_string(),
            );
        }
    }
}
