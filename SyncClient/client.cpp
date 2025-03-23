#include <QApplication>
#include "effect_manager.hpp"
#include "effect_manager_ui.hpp"

std::string SERVER_IP = "192.168.0.244";
int PORT = 5005;
float SCALE = 0.1;

int main(int argc, char *argv[]) {
    LEDStrip lights(288, SERVER_IP, PORT, SCALE);

    EffectManager::get_instance().set_lights(&lights);

    QApplication app(argc, argv);
    EffectManagerUI window;
    window.show();
    return app.exec();
}