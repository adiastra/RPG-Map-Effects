#pragma once
#include <QColor>
#include <QMap>
#include <QMainWindow>
#include <QString>
#include <vector>

#include <obs.hpp>

class OBSDisplayWidget;
class QCheckBox;
class QComboBox;
class QListWidget;
class QLineEdit;
class QTimer;

class RPGWindow : public QMainWindow {
    Q_OBJECT
public:
    RPGWindow();
    ~RPGWindow() override;

private:
    OBSSource ensureGridSource();
    void updateGridSourceSettings(obs_source_t *gridSource, int cellSize, int lineWidth, const QColor &color);
    void syncGridOutputToScene(const QString &sceneName, bool showOnOutput);

    OBSSource ensureCursorSource();
    void updateCursorSourceSettings(obs_source_t *cursorSource, int sizePx, const QColor &color);
    void syncCursorToScene(const QString &sceneName, bool showCursor, float cursorX, float cursorY);
    void updateCursorPosition(const QString &sceneName, float x, float y);

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
    QColor cursorColor_ = Qt::white;

    float lastClickX = 0.0f;
    float lastClickY = 0.0f;
    bool lastClickInside = false;

    /** FX for the currently selected battlemap only (what we show in the sidebar). */
    std::vector<FxInstance> activeFx;
    /** All FX keyed by battlemap scene UUID; switching maps shows only that map's FX. */
    QMap<QString, std::vector<FxInstance>> fxByMapSceneUuid_;
    QString currentMapSceneUuid_;

    QListWidget *fxList = nullptr;
    QLineEdit *labelEdit = nullptr;
    QComboBox *sceneCombo_ = nullptr;
    QCheckBox *setDirectionCheck_ = nullptr;

    OBSSource gridSource_;
    OBSSource cursorSource_;

    float lastCursorX = 0.0f;
    float lastCursorY = 0.0f;

    QTimer *cursorUpdateTimer_ = nullptr;

    void fadeOutAndRemoveInstance(const FxInstance &inst, int fadeMs);
    int findNearestFxInstanceIndex(float sceneX, float sceneY) const;
    /** Returns the scene source UUID for the given battlemap scene name, or empty if not found. */
    QString getMapSceneUuidFromName(const QString &sceneName) const;
    /** Reload activeFx and fxList from fxByMapSceneUuid_ for the current map. */
    void refreshFxListForCurrentMap();
};
