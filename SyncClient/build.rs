extern crate embed_resource;

use std::fs;
use std::path::Path;
use std::process::Command;

fn main() {
    let _ = embed_resource::compile("assets/app.rc", std::iter::empty::<&std::ffi::OsStr>());
    compile_shaders();
}

struct Shader<'a> {
    src: &'a str,
    target: &'a str,
    entry: &'a str,
    output: &'a str,
}

fn compile_shaders() {
    // Ensure assets directory exists
    let assets_dir = Path::new("assets");
    fs::create_dir_all(assets_dir).expect("Could not create assets directory");

    // List of shaders to compile
    let shaders = vec![
        Shader {
            src: "src/shaders/fullscreen_vs.hlsl",
            target: "vs_5_0",
            entry: "main",
            output: "assets/fullscreen_vs.cso",
        },
        Shader {
            src: "src/shaders/bilinear_ps.hlsl",
            target: "ps_5_0",
            entry: "main",
            output: "assets/bilinear_ps.cso",
        }
    ];

    // Compile each shader
    for shader in shaders {
        println!("cargo:rerun-if-changed={}", shader.src);

        let status = Command::new("fxc")
            .args(&[
                "/T", shader.target,
                "/E", shader.entry,
                "/Fo", shader.output,
                shader.src,
            ])
            .status()
            .expect("failed to run fxc");

        assert!(status.success(), "fxc failed to compile {}", shader.src);
    }
}
