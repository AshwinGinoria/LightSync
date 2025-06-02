extern crate tracing;
extern crate tracing_subscriber;

use tracing::info;

use crate::core::controller::Controller;
use crate::core::led_strip::LedStrip;
use crate::effects::solid_effect::SolidEffect;
use crate::effects::effect::Effect;
use crate::core::parameter::Parameter;
use crate::network::udp_client::UdpClient;
use std::thread;
use std::time::{Duration, Instant};

mod core;
mod effects;
mod network;

fn main() -> std::io::Result<()> {
    tracing_subscriber::fmt::init();
    info!("This will be logged to stdout");

    // Configuration
    let num_leds = 288;
    let target_ip = "192.168.0.244";
    let target_port = 5005;

    // Create LED strip and UDP client
    let strip = LedStrip::new(num_leds);
    let client = UdpClient::new(target_ip, target_port)?;

    // Create the effect (you can later load this from config or UI)
    let mut effect = SolidEffect::new();

    // Timing setup
    let target_frame_time = Duration::from_millis(1000); // ~30 FPS
    let mut last_frame = Instant::now();

    effect.set_parameter("color", Parameter::Color([100, 0, 0]));
    effect.set_parameter("brightness", Parameter::Float(0.2));

    let mut controller = Controller::new(strip, client);
    controller.set_effect(Box::new(effect));

    println!("Running LED controller loop...");
    loop {
        let now = Instant::now();
        let dt = (now - last_frame).as_secs_f32();
        last_frame = now;

        // Tick the effect
        controller.tick(dt);

        // Frame delay
        let elapsed = now.elapsed();
        if elapsed < target_frame_time {
            thread::sleep(target_frame_time - elapsed);
        }
    }
    
    controller.stop_effect();
}
