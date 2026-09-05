use std::{env, fs, path::PathBuf};
fn main() {
    println!("cargo:rerun-if-env-changed=AGENTDECK_PAYLOAD");
    let root = PathBuf::from(env::var("AGENTDECK_PAYLOAD").expect("Use tools/build-exe.ps1"));
    let mut entries = Vec::new();
    fn visit(root: &std::path::Path, dir: &std::path::Path, entries: &mut Vec<String>) {
        for entry in fs::read_dir(dir).unwrap() {
            let path = entry.unwrap().path();
            if path.is_dir() {
                visit(root, &path, entries);
            } else {
                println!("cargo:rerun-if-changed={}", path.display());
                entries.push(format!(
                    "({:?}, include_bytes!({:?})),",
                    path.strip_prefix(root).unwrap().to_str().unwrap(),
                    path.to_str().unwrap()
                ));
            }
        }
    }
    visit(&root, &root, &mut entries);
    entries.sort();
    fs::write(
        PathBuf::from(env::var("OUT_DIR").unwrap()).join("payload.rs"),
        format!("const FILES: &[(&str, &[u8])] = &[{}];", entries.join("\n")),
    )
    .unwrap();
}
