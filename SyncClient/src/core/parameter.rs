#[derive(Clone, Debug)]
pub enum Parameter {
    Int(i32),
    Float(f32),
    Color([u8; 3]),
    Bool(bool),
    Enum {
        options: Vec<String>,
        selected: usize,
    },
}
