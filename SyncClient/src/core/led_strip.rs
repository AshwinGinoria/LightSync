pub struct LedStrip {
    pub pixels: Vec<[u8; 3]>,
    pub length: usize,
}

impl LedStrip {
    pub fn new(length: usize) -> Self {
        Self {
            pixels: vec![[0, 0, 0]; length],
            length: length,
        }
    }

    pub fn set(&mut self, i: usize, r: u8, g: u8, b: u8) {
        if let Some(pixel) = self.pixels.get_mut(i) {
            *pixel = [r, g, b];
        }
    }

    pub fn clear(&mut self) {
        for pixel in self.pixels.iter_mut() {
            *pixel = [0, 0, 0];
        }
    }

    pub fn serialize(&self) -> Vec<u8> {
        self.pixels
            .iter()
            .flat_map(|[r, g, b]| vec![*r, *g, *b])
            .collect()
    }
}
