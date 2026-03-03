#pragma once
#include <QColor>
#include <QMainWindow>
#include <QString>
#include <vector>

#include <obs.hpp>

class OBSDisplayWidget;
class QComboBox;
class QListWidget;
class QLineEdit;

class RPGWindow : public QMainWindow {
    Q_OBJECT
public:
    RPGWindow();
    ~RPGWindow() override;

private:
    OBSSource ensureGridSource();
    void updateGridSourceSettings(obs_source_t *gridSource, int cellSize, int lineWidth, const QColor &color);
    void syncGridOutputToScene(const QString &sceneName, bool showOnOutput);

    struct FxInstance {
        QString mapSceneUuid;
        QString effectUuid;
        int64_t sceneItemId = 0;
        QString label;        // user-visible label text on the map
        QString templateName; // template scene name used for this instance
    };

private:
    OBSDisplayWidget *display = nullptr;

    QColor labelColor_ = Qt::white;

    float lastClickX = 0.0f;
    float lastClickY = 0.0f;
    bool lastClickInside = false;

    std::vector<FxInstance> activeFx;
    QListWidget *fxList = nullptr;
    QLineEdit *labelEdit = nullptr;
    QComboBox *sceneCombo_ = nullptr;

    OBSSource gridSource_;

    void fadeOutAndRemoveInstance(const FxInstance &inst, int fadeMs);
    int findNearestFxInstanceIndex(float sceneX, float sceneY) const;
};
