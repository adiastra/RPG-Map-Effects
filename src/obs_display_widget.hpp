#pragma once
#include <QWidget>
#include <QPaintEngine>
#include <QString>
#include <QColor>

class QMouseEvent;

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

signals:
    void sceneClicked(float sceneX, float sceneY, float normX, float normY, bool inside);

protected:
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;

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
};
