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

    struct FxInstance {
        QString mapSceneUuid;
        QString effectUuid;
        int64_t sceneItemId = 0;
        QString label;        // user-visible label text on the map
        QString templateName; // template scene name used for this instance
    };
    static bool sameFx(const FxInstance &a, const FxInstance &b);

private:
    OBSDisplayWidget *display = nullptr;

    QColor labelColor_ = Qt::white;

    float lastClickX = 0.0f;
    float lastClickY = 0.0f;
    bool lastClickInside = false;

    /** FX for the currently selected battlemap only (what we show in the sidebar). */
    std::vector<FxInstance> activeFx;
    /** All FX keyed by battlemap scene UUID; switching maps shows only that map's FX. */
    QMap<QString, std::vector<FxInstance>> fxByMapSceneUuid_;
    QString currentMapSceneUuid_;

    /** Right-click menu lock: move or rotate the selected FX until left-click releases. */
    enum class FxLockMode { None, Move, Rotate };
    FxLockMode fxLockMode_ = FxLockMode::None;

    QListWidget *fxList = nullptr;
    QLineEdit *labelEdit = nullptr;
    QComboBox *sceneCombo_ = nullptr;
    QCheckBox *setDirectionCheck_ = nullptr;

    OBSSource gridSource_;

    void fadeOutAndRemoveInstance(const FxInstance &inst, int fadeMs);
    int findNearestFxInstanceIndex(float sceneX, float sceneY) const;
    /** Returns the scene source UUID for the given battlemap scene name, or empty if not found. */
    QString getMapSceneUuidFromName(const QString &sceneName) const;
    /** Reload activeFx and fxList from fxByMapSceneUuid_ for the current map. */
    void refreshFxListForCurrentMap();
    /** Remove the given FX from fxByMapSceneUuid_ (by effectUuid + sceneItemId). */
    void removeFxFromPerMapStore(const FxInstance &inst);
    /** Update the stored copy of this FX's label in fxByMapSceneUuid_. */
    void syncStoredLabel(const FxInstance &inst, const QString &newLabel);
    /** Clear lock, arrow, and Set direction checkbox. */
    void releaseFxLockAndClearArrow();
    /** Get scene item for this instance (non-owning; do not use after scene changes). */
    obs_sceneitem_t *getSceneItemForInstance(const FxInstance &inst) const;
    /** Convert canvas (base) coords to scene space. Returns false if sizes invalid. */
    bool canvasToScene(obs_source_t *mapSource, float canvasX, float canvasY, float &outSceneX, float &outSceneY) const;
    /** Convert scene position to canvas (base) space. Returns false if sizes invalid. */
    bool scenePosToCanvas(obs_source_t *mapSource, float posX, float posY, float &outCX, float &outCY) const;
    /** Clear the FX at the given list row: fade out, remove from store/list, release lock. */
    void clearFxAtRow(int row, int fadeMs);
};
