#pragma once
#include <QWidget>
#include <QPaintEngine>
#include <QString>
#include <QColor>

#include <mutex>

class QMouseEvent;
class ArrowOverlay;
class QTimer;

extern "C" {
#include <obs.h>
}

class OBSDisplayWidget : public QWidget {
    Q_OBJECT
public:
    explicit OBSDisplayWidget(QWidget *parent = nullptr);
    ~OBSDisplayWidget() override;

    void setSceneByName(const QString &sceneName);
    obs_source_t *getSceneSourceRef() const;

    void setGridOverlay(bool show, int cellSizePx);
    void setGridColor(const QColor &color) { gridColor_ = color; }
    void setGridLineWidth(int pixels) { gridLineWidth_ = (pixels < 1) ? 1 : ((pixels > 10) ? 10 : pixels); }
    bool gridVisible() const { return gridShow_; }
    int gridCellSize() const { return gridCellSize_; }
    int gridLineWidth() const { return gridLineWidth_; }
    QColor gridColor() const { return gridColor_; }

    void setDirectionArrow(float fromX, float fromY, float toX, float toY);
    void clearDirectionArrow();

    /** Cursor overlay (preview only): show a dot at canvas position. Avoids adding cursor to scene (crash). */
    void setCursorOverlay(bool show, float canvasX, float canvasY);
    void setCursorOverlayStyle(int sizePx, uint32_t colorArgb);

    /** Convert canvas (scene) coordinates to widget coordinates for overlay drawing. */
    bool canvasToWidget(float canvasX, float canvasY, int widgetW, int widgetH,
                        int &outX, int &outY) const;

    /** For overlay: get current arrow state (returns false if not showing). */
    bool getDirectionArrow(float &fromX, float &fromY, float &toX, float &toY) const;

signals:
    void sceneClicked(float sceneX, float sceneY, float normX, float normY, bool inside);
    void sceneMouseMoved(float sceneX, float sceneY, float normX, float normY, bool inside, bool leftButtonDown);

protected:
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

    // Prevent Qt from painting over the native surface.
    QPaintEngine *paintEngine() const override { return nullptr; }

private:
    void createDisplay();
    void destroyDisplay();

    static void DrawCallback(void *data, uint32_t cx, uint32_t cy);

    obs_display_t *display = nullptr;
    obs_source_t *sceneSource = nullptr;
    bool showing = false;

    bool gridShow_ = false;
    int gridCellSize_ = 50;
    int gridLineWidth_ = 1;
    QColor gridColor_ = QColor(96, 96, 96, 255);

    mutable std::mutex directionArrowMutex_;
    bool directionArrowShow_ = false;
    float directionArrowFromX_ = 0.0f;
    float directionArrowFromY_ = 0.0f;
    float directionArrowToX_ = 0.0f;
    float directionArrowToY_ = 0.0f;

    ArrowOverlay *arrowOverlay_ = nullptr;

    mutable std::mutex cursorOverlayMutex_;
    bool cursorOverlayShow_ = false;
    float cursorOverlayX_ = 0.0f;
    float cursorOverlayY_ = 0.0f;
    int cursorOverlaySizePx_ = 16;
    uint32_t cursorOverlayColorArgb_ = 0xFFFFFFFFu;

    QTimer *mousePollTimer_ = nullptr;
    bool mouseLeftDown_ = false;
};
