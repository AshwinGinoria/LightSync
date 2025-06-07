use crate::core::controller::Controller;
use crate::core::parameter::Parameter;
use crate::effects::effect::Effect;
use eframe::egui::{self, ComboBox, Slider};
use egui::Margin;
use std::collections::HashMap;

pub struct UiState {
    pub selected_effect: String,
}

impl Default for UiState {
    fn default() -> Self {
        Self {
            selected_effect: "Solid".to_string(),
        }
    }
}

pub struct AppUI {
    pub state: UiState,
    pub controller: Controller,
    pub effect_factories: HashMap<String, fn() -> Box<dyn Effect>>,
}

impl AppUI {
    pub fn new(mut controller: Controller) -> Self {
        let mut effect_factories: HashMap<String, fn() -> Box<dyn Effect>> = HashMap::new();
        effect_factories.insert("Replicate".to_string(), || {
            Box::new(crate::effects::replicate::ReplicateEffect::new())
        });
        effect_factories.insert("Solid".to_string(), || {
            Box::new(crate::effects::solid::SolidEffect::new())
        });
        effect_factories.insert("LocatePixel".to_string(), || {
            Box::new(crate::effects::pixel_locate::PixelLocateEffect::new())
        });
        controller.set_effect(Box::new(crate::effects::replicate::ReplicateEffect::new()));

        Self {
            state: UiState::default(),
            controller,
            effect_factories,
        }
    }

    fn set_selected_effect(&mut self) {
        if let Some(factory) = self.effect_factories.get(&self.state.selected_effect) {
            self.controller.set_effect(factory());
        }
    }

    fn render_effect_dropdown(&mut self, ui: &mut egui::Ui) {
        let mut selection_changed = false;
        ui.horizontal(|ui| {
            ui.label("Effect:");
            ComboBox::from_id_salt("effect_selector")
                .selected_text(&self.state.selected_effect)
                .show_ui(ui, |ui| {
                    for effect in self.effect_factories.keys() {
                        if ui
                            .selectable_label(effect == &self.state.selected_effect, effect)
                            .clicked()
                        {
                            self.state.selected_effect = effect.clone();
                            selection_changed = true;
                        }
                    }
                });
        });

        if selection_changed {
            self.set_selected_effect();
        }
    }

    fn render_effect_parameters(ui: &mut egui::Ui, effect: &mut dyn Effect) {
        let params = effect.get_parameters();
        for (key, param) in params {
            match param {
                Parameter::Float(mut f) => {
                    if ui.add(Slider::new(&mut f, 0.0..=5.0).text(&key)).changed() {
                        effect.set_parameter(&key, Parameter::Float(f));
                    }
                }
                Parameter::Int(mut i) => {
                    if ui.add(Slider::new(&mut i, 0..=255).text(&key)).changed() {
                        effect.set_parameter(&key, Parameter::Int(i));
                    }
                }
                Parameter::Color(mut rgb) => {
                    ui.horizontal(|ui| {
                        ui.label(&key);
                        if ui.color_edit_button_srgb(&mut rgb).changed() {
                            effect.set_parameter(&key, Parameter::Color(rgb));
                        }
                    });
                }
            }
        }
    }

    fn render_reset_button(&mut self, ui: &mut egui::Ui) {
        if ui.button("Reset Parameters").clicked() {
            if let Some(factory) = self.effect_factories.get(&self.state.selected_effect) {
                self.controller.set_effect(factory());
            }
        }
    }

    fn render_stop_button(&mut self, ui: &mut egui::Ui) {
        egui::Frame::NONE
            .inner_margin(Margin::symmetric(8, 8))
            .show(ui, |ui| {
                ui.with_layout(
                    egui::Layout::centered_and_justified(egui::Direction::LeftToRight),
                    |ui| {
                        let stop_button = egui::Button::new("⏹ Stop");
                        if ui.add_sized(ui.available_size(), stop_button).clicked() {
                            self.controller.stop_effect();
                        }
                    },
                );
            });
    }
}

impl eframe::App for AppUI {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        egui::TopBottomPanel::top("Title bar").show(ctx, |ui| {
            ui.vertical_centered(|ui| {
                ui.add_space(8.0);
                ui.heading("LED Controller");
                ui.add_space(8.0);
            });
        });

        egui::CentralPanel::default()
            .show(ctx, |ui| {
                ui.vertical_centered(|ui| {
                    self.render_effect_dropdown(ui);

                    ui.add_space(12.0);
                    ui.separator();

                    let effect_ptr = self.controller.effect_mut();
                    if let Some(effect) = effect_ptr {
                        ui.collapsing("Effect Parameters", |ui| {
                            Self::render_effect_parameters(ui, effect);
                        });

                        ui.add_space(8.0);
                        self.render_reset_button(ui);
                    } else {
                        ui.label("Effect stopped");
                    }
                });
            });

        egui::TopBottomPanel::bottom("Stop")
            .exact_height(50.0)
            .show(ctx, |ui| {
                self.render_stop_button(ui);
            });

        // Simulate FPS + controller tick
        let fps = 60.0;
        self.controller.tick(100.0 / fps);

        ctx.request_repaint();
    }
}
