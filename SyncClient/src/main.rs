#![cfg_attr(windows, windows_subsystem = "windows")]
extern crate eframe;
extern crate egui;
extern crate image;
extern crate tracing;
extern crate tracing_subscriber;
extern crate windows;
extern crate palette;
extern crate windows_capture;

use core::controller::Controller;
use core::led_strip::LedStrip;
use eframe::NativeOptions;
use egui::ViewportBuilder;
use network::udp_client::UdpClient;
use tracing::info;
use ui::app::AppUI;

mod core;
mod effects;
mod network;
mod ui;

fn main() -> Result<(), eframe::Error> {
    tracing_subscriber::fmt::init();
    info!("Starting Application");

    let native_options = NativeOptions {
        viewport: ViewportBuilder::default()
            .with_inner_size([350.0, 450.0]),
        ..Default::default()
    };

    // Configuration
    let num_leds = 288;
    let target_ip = "192.168.0.244";
    let target_port = 5005;

    // Create LED strip and UDP client
    let strip = LedStrip::new(num_leds);
    let client = UdpClient::new(target_ip, target_port).expect("Unable to start a UDP client");
    let controller = Controller::new(strip, client);

    eframe::run_native(
        "LED Controller",
        native_options,
        Box::new(|_cc| Ok(Box::new(AppUI::new(controller)))),
    )
}
