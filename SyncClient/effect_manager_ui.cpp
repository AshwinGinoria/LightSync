#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <array>
#include <map>
#include <variant>

#include "effect_manager.hpp"
#include "effect_manager_ui.hpp"
#include "logger.hpp"

#define effect_manager EffectManager::getInstance()

EffectManagerUI::EffectManagerUI(QWidget *parent) : QMainWindow(parent) {
    // Set Title
    setWindowTitle(QString::fromUtf8("LED Effect Manager"));

    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    centralWidget->setLayout(mainLayout);

    // Effect List
    effectList = new QComboBox(centralWidget);
    mainLayout->addWidget(effectList);

    // Start Stop Buttons
    QHBoxLayout *buttonLayout = new QHBoxLayout(centralWidget);
    QPushButton *startButton = new QPushButton("Start", centralWidget);
    QPushButton *stopButton = new QPushButton("Stop", centralWidget);
    buttonLayout->addWidget(startButton);
    buttonLayout->addWidget(stopButton);
    mainLayout->addLayout(buttonLayout);

    // Parameter Form
    parameterForm = new QFormLayout(centralWidget);
    mainLayout->addLayout(parameterForm);

    setCentralWidget(centralWidget);

    connect(effectList, &QComboBox::currentTextChanged, this,
            &EffectManagerUI::loadEffectParameters);
    connect(startButton, &QPushButton::clicked, this,
            &EffectManagerUI::startEffect);
    connect(stopButton, &QPushButton::clicked, this,
            &EffectManagerUI::stopEffect);

    populateEffects();
}

void EffectManagerUI::populateEffects() {
    auto effects = effect_manager.getAvailableEffects();
    for (const auto &name : effects) {
        effectList->addItem(QString::fromStdString(name));
    }
}

void EffectManagerUI::loadEffectParameters(const QString &effectName) {
    LOGGER.debug("Changing selected effect to {}", effectName.toStdString());

    auto params = effect_manager.getEffectParameters(effectName.toStdString());

    // Clear parameter form
    clearParameterForm();

    for (const auto &[key, param] : params) {
        LOGGER.debug("Adding Parameter {}", key);
        QWidget *input = nullptr;
        // INT
        if (std::holds_alternative<int>(param.value)) {
            auto *spin = new QSpinBox;
            spin->setMaximum(60000);
            spin->setValue(std::get<int>(param.value));
            input = spin;
        }
        // FLOAT
        else if (std::holds_alternative<float>(param.value)) {
            auto *dspin = new QDoubleSpinBox;
            dspin->setValue(std::get<float>(param.value));
            input = dspin;
        }
        // STRING
        else if (std::holds_alternative<std::string>(param.value)) {
            auto *line = new QLineEdit(
                QString::fromStdString(std::get<std::string>(param.value)));
            input = line;
        }
        // COLOR
        else if (std::holds_alternative<std::array<uint8_t, 3>>(param.value)) {
            QPushButton *colorButton = new QPushButton("Pick Color");
            std::array<uint8_t, 3> color =
                std::get<std::array<uint8_t, 3>>(param.value);

            QObject::connect(
                colorButton, &QPushButton::clicked, [colorButton, &color]() {
                    QColor chosen = QColorDialog::getColor(
                        QColor(color[0], color[1], color[2]));

                    if (chosen.isValid()) {
                        color = {static_cast<uint8_t>(chosen.red()),
                                 static_cast<uint8_t>(chosen.green()),
                                 static_cast<uint8_t>(chosen.blue())};

                        colorButton->setStyleSheet(
                            QString("background-color: rgb(%1, %2, %3);")
                                .arg(color[0])
                                .arg(color[1])
                                .arg(color[2]));
                    }
                });

            input = colorButton;
        }

        if (input) {
            parameterForm->addRow(QString::fromStdString(key), input);
            inputWidgets[key] = input;
        }
    }
}

void EffectManagerUI::clearParameterForm() {
    LOGGER.debug("Clearing Parameter Form");
    inputWidgets.clear();

    while (parameterForm->rowCount() > 0) {
        parameterForm->removeRow(0);
    }
    LOGGER.debug("Cleared Parameter Form");
}

void EffectManagerUI::startEffect() {
    std::string name = effectList->currentText().toStdString();
    applyEffectParameters(name);
    effect_manager.startEffect(name);
}

void EffectManagerUI::stopEffect() {
    effect_manager.stop();
}

void EffectManagerUI::applyEffectParameters(std::string effect_name) {
    LOGGER.debug("Applying Parameters for {}", effect_name);
    std::map<std::string, Parameter> params;
    for (const auto &[key, widget] : inputWidgets) {
        LOGGER.debug("Applying Parameter {}", key);
        if (auto *spin = qobject_cast<QSpinBox *>(widget)) {
            LOGGER.debug("Setting int parameter {}", spin->value());
            params[key] = spin->value();
        } else if (auto *dspin = qobject_cast<QDoubleSpinBox *>(widget)) {
            LOGGER.debug("Setting float parameter {}", dspin->value());
            params[key] = static_cast<float>(dspin->value());
        } else if (auto *line = qobject_cast<QLineEdit *>(widget)) {
            LOGGER.debug("Setting string parameter {}",
                         line->text().toStdString());
            params[key] = line->text().toStdString();
        } else if (auto *colorButton = qobject_cast<QPushButton *>(widget)) {
            std::array<uint8_t, 3> color = {0, 0, 0};
            QColor colorValue = colorButton->palette().button().color();
            color[0] = static_cast<uint8_t>(colorValue.red());
            color[1] = static_cast<uint8_t>(colorValue.green());
            color[2] = static_cast<uint8_t>(colorValue.blue());
            LOGGER.debug("Setting color parameter ({}, {}, {})", color[0],
                         color[1], color[2]);
            params[key] = color;
        }
    }

    effect_manager.setEffectParameters(effect_name, params);
}
