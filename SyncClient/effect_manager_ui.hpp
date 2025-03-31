#pragma once

#include "parameter.hpp"
#include <QComboBox>
#include <QFormLayout>
#include <QMainWindow>
#include <map>
#include <string>

class EffectManagerUI : public QMainWindow {
    Q_OBJECT

  public:
    explicit EffectManagerUI(QWidget *parent = nullptr);

  private slots:
    void loadEffectParameters(const QString &effectName);
    void startEffect();
    void stopEffect();

  private:
    QComboBox *effectList;
    QFormLayout *parameterForm;
    std::map<std::string, QWidget *> inputWidgets;
    void populateEffects();
    void applyEffectParameters(std::string);
    void clearParameterForm();
};
