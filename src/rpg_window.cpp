#include "rpg_window.hpp"
#include "obs_display_widget.hpp"

#include <obs-module.h>
#include <obs.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QImage>
#include <QPainter>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStatusBar>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include <dlfcn.h>
#include <algorithm>
#include <cmath>

extern "C" {
#include <obs-frontend-api.h>
}

extern "C" {
#include <graphics/vec2.h>
}

// Compile-time stamp for this translation unit only
static const char *kCompileStamp = __DATE__ " " __TIME__;

// Anchor symbol so dladdr can locate the plugin binary on disk
static void rpg_me_anchor_symbol() {}

static QString GetPluginBinaryPath()
{
    Dl_info info {};
    if (dladdr((void *)&rpg_me_anchor_symbol, &info) && info.dli_fname)
        return QString::fromUtf8(info.dli_fname);
    return {};
}

static QString GetPluginMTimeString()
{
    const QString path = GetPluginBinaryPath();
    if (path.isEmpty())
        return "unknown";

    QFileInfo fi(path);
    if (!fi.exists())
        return "missing";

    return fi.lastModified().toString("yyyy-MM-dd HH:mm:ss");
}

static bool IsFxTemplateSceneName(const QString &name)
{
    // Accept FX:, fx:, Fx:, fX: etc.
    return name.startsWith("fx:", Qt::CaseInsensitive);
}

static QString GetDefaultsConfigPath()
{
    char *path = obs_module_config_path("rpg-map-effects-defaults.json");
    QString qpath = path ? QString::fromUtf8(path) : QString();
    bfree(path);
    return qpath;
}

struct TemplateDefaults {
    int fadeMs = 600;
    int lifetimeSec = 0;
    bool sequence = true;
    bool showLabel = false;
};

static TemplateDefaults LoadTemplateDefaults(const QString &templateUuid)
{
    TemplateDefaults def;
    const QString path = GetDefaultsConfigPath();
    if (path.isEmpty())
        return def;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return def;

    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject())
        return def;

    QJsonObject root = doc.object();
    QJsonObject tmpl = root.value(templateUuid).toObject();
    if (tmpl.isEmpty())
        return def;

    def.fadeMs = tmpl.value("fadeMs").toInt(def.fadeMs);
    def.lifetimeSec = tmpl.value("lifetimeSec").toInt(def.lifetimeSec);
    def.sequence = tmpl.value("sequence").toBool(def.sequence);
    def.showLabel = tmpl.value("showLabel").toBool(def.showLabel);
    return def;
}

static void SaveTemplateDefaults(const QString &templateUuid, int fadeMs, int lifetimeSec,
                                 bool sequence, bool showLabel)
{
    const QString path = GetDefaultsConfigPath();
    if (path.isEmpty())
        return;

    QJsonObject root;
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        f.close();
        if (doc.isObject())
            root = doc.object();
    }

    QJsonObject tmpl;
    tmpl.insert("fadeMs", fadeMs);
    tmpl.insert("lifetimeSec", lifetimeSec);
    tmpl.insert("sequence", sequence);
    tmpl.insert("showLabel", showLabel);
    root.insert(templateUuid, tmpl);

    QFile out(path);
    if (out.open(QIODevice::WriteOnly)) {
        out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }
}

static void AddBattlemapScenesToCombo(QComboBox *combo)
{
    combo->clear();

    obs_frontend_source_list scenes = {};
    obs_frontend_get_scenes(&scenes);

    for (size_t i = 0; i < scenes.sources.num; i++) {
        obs_source_t *src = scenes.sources.array[i];
        const char *name = obs_source_get_name(src);
        const QString qname = QString::fromUtf8(name);
        if (!IsFxTemplateSceneName(qname))
            combo->addItem(qname);
    }

    obs_frontend_source_list_free(&scenes);
}

static void AddFxTemplateScenesToCombo(QComboBox *combo)
{
    combo->clear();

    obs_frontend_source_list scenes = {};
    obs_frontend_get_scenes(&scenes);

    for (size_t i = 0; i < scenes.sources.num; i++) {
        obs_source_t *src = scenes.sources.array[i];
        const char *name = obs_source_get_name(src);
        const QString qname = QString::fromUtf8(name);
        if (IsFxTemplateSceneName(qname))
            combo->addItem(qname);
    }

    obs_frontend_source_list_free(&scenes);
}

static int ParseDelayMsFromItemName(const char *name)
{
    if (!name)
        return 0;

    // Convention: "[250] Smoke" means show after 250ms.
    // If no prefix, delay = 0.
    const char *p = name;
    if (*p != '[')
        return 0;
    p++;

    int value = 0;
    int digits = 0;
    while (*p >= '0' && *p <= '9') {
        value = value * 10 + (*p - '0');
        digits++;
        p++;
        if (digits > 9)
            return 0;
    }

    if (digits == 0 || *p != ']')
        return 0;

    return value;
}

struct ChildItemSchedule {
    int delayMs = 0;
    int64_t itemId = 0;
};

static std::vector<ChildItemSchedule> BuildChildSchedule(obs_scene_t *scene)
{
    std::vector<ChildItemSchedule> out;
    if (!scene)
        return out;

    obs_scene_enum_items(
        scene,
        [](obs_scene_t *, obs_sceneitem_t *item, void *param) {
            auto *vec = static_cast<std::vector<ChildItemSchedule> *>(param);
            if (!item)
                return true;

            obs_source_t *src = obs_sceneitem_get_source(item);
            const char *nm = src ? obs_source_get_name(src) : nullptr;

            ChildItemSchedule s;
            s.delayMs = ParseDelayMsFromItemName(nm);
            s.itemId = obs_sceneitem_get_id(item);
            vec->push_back(s);
            return true;
        },
        &out);

    std::sort(out.begin(), out.end(),
              [](const ChildItemSchedule &a, const ChildItemSchedule &b) {
                  if (a.delayMs != b.delayMs)
                      return a.delayMs < b.delayMs;
                  return a.itemId < b.itemId;
              });
    return out;
}

static void SetAllSceneItemsVisible(obs_scene_t *scene, bool visible)
{
    if (!scene)
        return;
    obs_scene_enum_items(
        scene,
        [](obs_scene_t *, obs_sceneitem_t *item, void *param) {
            const bool vis = *static_cast<bool *>(param);
            obs_sceneitem_set_visible(item, vis);
            return true;
        },
        &visible);
}

/** Callback for obs_scene_atomic_update: clear hide transition on each item then set invisible. Used for sequencing so we don't trigger the template's hide (fade-out) transitions on the initial hide. */
static void SetAllSceneItemsHiddenForSequenceHelper(void * /*param*/, obs_scene_t *scene)
{
    if (!scene)
        return;
    obs_scene_enum_items(
        scene,
        [](obs_scene_t *, obs_sceneitem_t *item, void *) {
            obs_sceneitem_set_transition(item, false, nullptr); // clear hide transition → instant hide
            obs_sceneitem_set_visible(item, false);
            return true;
        },
        nullptr);
}

/** Set all scene items hidden on the graphics thread for sequencing: clear hide transitions first so the initial hide is instant (no fade out), then set visible false. Show transitions are left intact so [time] fade-in still works. */
static void SetAllSceneItemsHiddenForSequenceOnGraphicsThread(obs_scene_t *scene)
{
    if (!scene)
        return;
    obs_enter_graphics();
    obs_scene_atomic_update(scene, SetAllSceneItemsHiddenForSequenceHelper, nullptr);
    obs_leave_graphics();
}

static void RestartMediaForSceneItem(obs_sceneitem_t *item)
{
    if (!item)
        return;
    obs_source_t *src = obs_sceneitem_get_source(item);
    if (!src)
        return;
    obs_source_media_restart(src);
}

static const char *kGridSourceName = "RPG Map Grid";

static QString GetGridPngPath()
{
    return QDir::temp().absoluteFilePath(QStringLiteral("rpg_map_effects_grid.png"));
}

static bool GenerateGridPng(const QString &path, int width, int height, const QColor &color,
                            int cellSize, int lineWidth)
{
    if (width < 1 || height < 1 || cellSize < 10 || lineWidth < 1 || lineWidth > 10)
        return false;

    QImage img(width, height, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(color, lineWidth, Qt::SolidLine, Qt::SquareCap));

    for (int x = 0; x <= width; x += cellSize)
        p.drawLine(x, 0, x, height);
    for (int y = 0; y <= height; y += cellSize)
        p.drawLine(0, y, width, y);

    p.end();

    return img.save(path, "PNG");
}

/** Write a 1x1 transparent PNG so the grid source shows nothing when "Show grid on output" is off. */
static bool WriteBlankGridPng(const QString &path)
{
    QImage img(1, 1, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    return img.save(path, "PNG");
}

/** Find or add grid in the given scene and set visibility. Uses atomic update like the frontend. */
struct GridSceneItemCtx {
    obs_source_t *gridSource = nullptr;
    obs_sceneitem_t *item = nullptr;
};

static bool findGridItemInScene(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
    auto *ctx = static_cast<GridSceneItemCtx *>(param);
    if (!ctx->gridSource)
        return true;
    obs_source_t *src = obs_sceneitem_get_source(item);
    if (src != ctx->gridSource)
        return true;
    ctx->item = item;
    obs_sceneitem_addref(item);
    return false;
}

struct AddGridToSceneData {
    obs_source_t *gridSource = nullptr;
    bool visible = false;
};

static void AddOrShowGridInSceneHelper(void *param, obs_scene_t *scene)
{
    auto *data = static_cast<AddGridToSceneData *>(param);
    if (!data || !data->gridSource || !scene)
        return;

    GridSceneItemCtx ctx;
    ctx.gridSource = data->gridSource;
    obs_scene_enum_items(scene, findGridItemInScene, &ctx);

    if (ctx.item) {
        obs_sceneitem_set_visible(ctx.item, data->visible);
        if (data->visible)
            obs_sceneitem_set_order(ctx.item, OBS_ORDER_MOVE_TOP);
        obs_sceneitem_release(ctx.item);
        return;
    }

    obs_sceneitem_t *item = obs_scene_add(scene, data->gridSource);
    if (item) {
        obs_sceneitem_set_visible(item, data->visible);
        obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);
        obs_sceneitem_release(item);
    }
}

/** Add grid to user's battlemap scene and set visibility. Uses obs_scene_atomic_update like OBS frontend. */
static void addGridToSceneAndSetVisible(obs_scene_t *scene, obs_source_t *gridSource, bool visible)
{
    if (!scene || !gridSource)
        return;

    AddGridToSceneData data;
    data.gridSource = gridSource;
    data.visible = visible;

    obs_enter_graphics();
    obs_scene_atomic_update(scene, AddOrShowGridInSceneHelper, &data);
    obs_leave_graphics();
}

static void SpawnFxAtClick(const QString &templateSceneName, obs_source_t *mapSceneSource, float x, float y,
                           bool sequenceByDelay, int defaultFadeMs, bool showLabel,
                           const QString &labelText, uint32_t labelColorArgb,
                           QString &outMapSceneUuid, QString &outEffectUuid,
                           int64_t &outSceneItemId)
{
    outMapSceneUuid.clear();
    outEffectUuid.clear();
    outSceneItemId = 0;

    if (templateSceneName.isEmpty() || !mapSceneSource)
        return;

    obs_scene_t *mapScene = obs_scene_from_source(mapSceneSource);
    if (!mapScene)
        return;

    obs_source_t *templateSource = obs_get_source_by_name(templateSceneName.toUtf8().constData());
    if (!templateSource)
        return;

    if (!obs_source_is_scene(templateSource)) {
        obs_source_release(templateSource);
        return;
    }

    const QString uniqueName =
        QString("FX Spawn %1 %2")
            .arg(templateSceneName)
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz"));

    obs_source_t *dup = obs_source_duplicate(templateSource, uniqueName.toUtf8().constData(), true);
    obs_source_release(templateSource);
    if (!dup)
        return;

    const char *mapUuid = obs_source_get_uuid(mapSceneSource);
    const char *fxUuid = obs_source_get_uuid(dup);
    if (mapUuid)
        outMapSceneUuid = QString::fromUtf8(mapUuid);
    if (fxUuid)
        outEffectUuid = QString::fromUtf8(fxUuid);

    // If sequencing, hide all children on the graphics thread: clear hide transitions first so we
    // don't trigger the template's fade-out (instant hide), then set invisible. Show transitions
    // stay so [time] fade-in still works. Doing this on the graphics thread avoids a "full on" frame.
    obs_scene_t *fxSceneForChildren = obs_scene_from_source(dup);
    if (sequenceByDelay && fxSceneForChildren)
        SetAllSceneItemsHiddenForSequenceOnGraphicsThread(fxSceneForChildren);

        // Optional: add a text label inside the FX scene so it moves with the effect.
        if (showLabel && !labelText.isEmpty() && fxSceneForChildren) {
            obs_data_t *settings = obs_data_create();
            obs_data_set_string(settings, "text", labelText.toUtf8().constData());

            // Smaller, simple font tuned for map labels
            obs_data_t *font = obs_data_create();
            obs_data_set_string(font, "face", "Arial");
            obs_data_set_string(font, "style", "Regular");
            obs_data_set_int(font, "size", 32); // reasonably small/legible
            obs_data_set_int(font, "flags", 0);
            obs_data_set_obj(settings, "font", font);
            obs_data_release(font);

            // No outline/shadow by default for cleaner labels
            obs_data_set_bool(settings, "outline", false);
            obs_data_set_bool(settings, "drop_shadow", false);

            if (labelColorArgb != 0)
                obs_data_set_int(settings, "color1", (int64_t)labelColorArgb);

            QByteArray nameUtf8 =
                QString("FX Label %1").arg(labelText).toUtf8();
            obs_source_t *labelSrc =
                obs_source_create("text_ft2_source_v2", nameUtf8.constData(), settings, nullptr);
            if (!labelSrc) {
                // Fallback for older builds/plugins
                labelSrc =
                    obs_source_create("text_ft2_source", nameUtf8.constData(), settings, nullptr);
            }
            obs_data_release(settings);

            if (labelSrc) {
                obs_sceneitem_t *labelItem = obs_scene_add(fxSceneForChildren, labelSrc);
                if (labelItem) {
                    obs_video_info ovi = {};
                    if (obs_get_video_info(&ovi) && ovi.base_width > 0 && ovi.base_height > 0) {
                        const float cx = float(ovi.base_width) * 0.5f;
                        const float cy = float(ovi.base_height) * 0.5f;
                        vec2 lpos;
                        vec2_set(&lpos, cx, cy);
                        obs_sceneitem_set_alignment(labelItem, OBS_ALIGN_CENTER);
                        obs_sceneitem_set_pos(labelItem, &lpos);
                    } else {
                        vec2 lpos;
                        vec2_set(&lpos, 0.0f, 0.0f);
                        obs_sceneitem_set_alignment(labelItem, OBS_ALIGN_CENTER);
                        obs_sceneitem_set_pos(labelItem, &lpos);
                    }
                }
                obs_source_release(labelSrc);
            }
        }

    obs_sceneitem_t *fxItem = obs_scene_add(mapScene, dup);

    // The scene now owns it; drop our strong ref.
    obs_source_release(dup);

    if (!fxItem)
        return;

    outSceneItemId = obs_sceneitem_get_id(fxItem);

    vec2 pos;
    vec2_set(&pos, x, y);
    obs_sceneitem_set_alignment(fxItem, OBS_ALIGN_CENTER);
    obs_sceneitem_set_pos(fxItem, &pos);

    // Default: screen blend over the battlemap.
    obs_sceneitem_set_blending_mode(fxItem, OBS_BLEND_SCREEN);

    // Configure an automatic fade-out hide transition on the FX scene item.
    if (defaultFadeMs > 0) {
        // Use OBS's built-in fade transition type if available.
        const char *fadeId = nullptr;
        size_t idx = 0;
        while (obs_enum_transition_types(idx++, &fadeId)) {
            if (fadeId && strcmp(fadeId, "fade_transition") == 0)
                break;
            fadeId = nullptr;
        }

        if (fadeId) {
            obs_data_t *settings = obs_data_create();
            obs_source_t *fadeTr =
                obs_source_create_private(fadeId, "RPG FX Fade Transition", settings);
            obs_data_release(settings);

            if (fadeTr) {
                // false = hide transition
                obs_sceneitem_set_transition(fxItem, false, fadeTr);
                obs_sceneitem_set_transition_duration(fxItem, false,
                                                      (uint32_t)defaultFadeMs);
                obs_source_release(fadeTr);
            }
        }
    }

    // Optional: staged reveal using [ms] prefixes + per-item show transitions.
    if (sequenceByDelay) {
        obs_source_t *fxSource = obs_sceneitem_get_source(fxItem);
        obs_scene_t *fxScene = obs_scene_from_source(fxSource);
        if (fxScene) {
            const std::vector<ChildItemSchedule> sched = BuildChildSchedule(fxScene);
            const QString fxUuidQ = outEffectUuid;

            for (const auto &entry : sched) {
                QTimer::singleShot(entry.delayMs, qApp, [fxUuidQ, entry]() {
                    OBSSourceAutoRelease fxSrc(obs_get_source_by_uuid(fxUuidQ.toUtf8().constData()));
                    if (!fxSrc.Get())
                        return;
                    obs_scene_t *scene = obs_scene_from_source(fxSrc.Get());
                    if (!scene)
                        return;
                    obs_sceneitem_t *it = obs_scene_find_sceneitem_by_id(scene, entry.itemId);
                    if (!it)
                        return;
                    RestartMediaForSceneItem(it);
                    obs_sceneitem_set_visible(it, true);
                });
            }
        }
    }
}

static void ClearLastFx(QString mapSceneUuid, QString fxUuid, int64_t sceneItemId)
{
    if (mapSceneUuid.isEmpty() || fxUuid.isEmpty() || sceneItemId == 0)
        return;

    OBSSourceAutoRelease mapSrc(obs_get_source_by_uuid(mapSceneUuid.toUtf8().constData()));
    if (mapSrc.Get()) {
        obs_scene_t *scene = obs_scene_from_source(mapSrc.Get());
        if (scene) {
            obs_sceneitem_t *item = obs_scene_find_sceneitem_by_id(scene, sceneItemId);
            if (item)
                obs_sceneitem_remove(item);
        }
    }

    OBSSourceAutoRelease fxSrc(obs_get_source_by_uuid(fxUuid.toUtf8().constData()));
    if (fxSrc.Get())
        obs_source_remove(fxSrc.Get());
}

void RPGWindow::fadeOutAndRemoveInstance(const FxInstance &inst, int fadeMs)
{
    if (inst.mapSceneUuid.isEmpty() || inst.effectUuid.isEmpty() || inst.sceneItemId == 0)
        return;

    // Start hide (triggers any configured hide transitions).
    {
        OBSSourceAutoRelease mapSrc(obs_get_source_by_uuid(inst.mapSceneUuid.toUtf8().constData()));
        if (mapSrc.Get()) {
            obs_scene_t *scene = obs_scene_from_source(mapSrc.Get());
            if (scene) {
                obs_sceneitem_t *item = obs_scene_find_sceneitem_by_id(scene, inst.sceneItemId);
                if (item)
                    obs_sceneitem_set_visible(item, false);
            }
        }
    }

    // After fadeMs, actually remove the scene item and FX source.
    QTimer::singleShot(fadeMs, qApp, [inst]() {
        ClearLastFx(inst.mapSceneUuid, inst.effectUuid, inst.sceneItemId);
    });
}

int RPGWindow::findNearestFxInstanceIndex(float sceneX, float sceneY) const
{
    if (activeFx.empty())
        return -1;

    const float maxRadius = 200.0f; // pixels in scene space
    const float maxDist2 = maxRadius * maxRadius;

    int bestIndex = -1;
    float bestDist2 = maxDist2;

    for (size_t i = 0; i < activeFx.size(); ++i) {
        const FxInstance &inst = activeFx[i];
        if (inst.mapSceneUuid.isEmpty() || inst.sceneItemId == 0)
            continue;

        OBSSourceAutoRelease mapSrc(obs_get_source_by_uuid(inst.mapSceneUuid.toUtf8().constData()));
        if (!mapSrc.Get())
            continue;

        obs_scene_t *scene = obs_scene_from_source(mapSrc.Get());
        if (!scene)
            continue;

        obs_sceneitem_t *item = obs_scene_find_sceneitem_by_id(scene, inst.sceneItemId);
        if (!item)
            continue;

        vec2 pos;
        obs_sceneitem_get_pos(item, &pos);

        const float dx = pos.x - sceneX;
        const float dy = pos.y - sceneY;
        const float d2 = dx * dx + dy * dy;
        if (d2 < bestDist2) {
            bestDist2 = d2;
            bestIndex = static_cast<int>(i);
        }
    }

    return bestIndex;
}

RPGWindow::RPGWindow()
{
    setWindowTitle("RPG Map Effects");
    resize(1400, 900);

    // Central layout: map display on the left, FX controls on the right.
    auto *central = new QWidget(this);
    auto *hLayout = new QHBoxLayout(central);
    hLayout->setContentsMargins(0, 0, 0, 0);
    hLayout->setSpacing(0);

    display = new OBSDisplayWidget(central);
    display->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    hLayout->addWidget(display, /*stretch*/ 3);

    auto *fxPanel = new QWidget(central);
    auto *fxLayout = new QVBoxLayout(fxPanel);
    fxLayout->setContentsMargins(6, 6, 6, 6);
    fxLayout->setSpacing(6);
    hLayout->addWidget(fxPanel, /*stretch*/ 1);

    setCentralWidget(central);

    auto *tb = addToolBar("Controls");
    tb->setMovable(false);

    tb->addWidget(new QLabel("Battlemap Scene:", this));

    sceneCombo_ = new QComboBox(this);
    sceneCombo_->setMinimumWidth(260);
    tb->addWidget(sceneCombo_);

    AddBattlemapScenesToCombo(sceneCombo_);

    QObject::connect(sceneCombo_, &QComboBox::currentTextChanged, this,
                     [this](const QString &sceneName) {
                         display->setSceneByName(sceneName);
                         OBSSourceAutoRelease sceneSrc(
                             obs_get_source_by_name(sceneName.toUtf8().constData()));
                         if (sceneSrc.Get() && obs_source_is_scene(sceneSrc.Get()))
                             obs_frontend_set_current_scene(sceneSrc.Get());
                     });

    tb->addSeparator();

    auto *gridCheck = new QCheckBox("Show grid", this);
    gridCheck->setChecked(false);
    tb->addWidget(gridCheck);

    auto *gridCellSpin = new QSpinBox(this);
    gridCellSpin->setRange(10, 500);
    gridCellSpin->setSingleStep(10);
    gridCellSpin->setValue(50);
    gridCellSpin->setSuffix(" px");
    tb->addWidget(gridCellSpin);

    auto *gridColorBtn = new QPushButton("Grid color…", this);
    tb->addWidget(gridColorBtn);

    auto *gridWidthSpin = new QSpinBox(this);
    gridWidthSpin->setRange(1, 10);
    gridWidthSpin->setValue(1);
    gridWidthSpin->setSuffix(" px");
    tb->addWidget(new QLabel("Line:", this));
    tb->addWidget(gridWidthSpin);

    auto *snapCheck = new QCheckBox("Snap to grid", this);
    snapCheck->setChecked(false);
    tb->addWidget(snapCheck);

    auto *gridOnOutputCheck = new QCheckBox("Show grid on output", this);
    gridOnOutputCheck->setChecked(false);
    gridOnOutputCheck->setToolTip(QStringLiteral("Adds the grid to your selected battlemap scene. Check to show on output, uncheck to hide. After switching battlemaps, uncheck then check again to move the grid to the new scene."));
    tb->addWidget(gridOnOutputCheck);

    tb->addSeparator();

    // Status bar for coordinates + version info.
    auto *coordLabel = new QLabel(this);
    coordLabel->setText("Click: —");

    auto *verLabel = new QLabel(this);
    statusBar()->addPermanentWidget(coordLabel, 1);
    statusBar()->addPermanentWidget(verLabel, 0);

    auto updateGridOverlay = [this, gridCheck, gridCellSpin, gridWidthSpin]() {
        display->setGridOverlay(gridCheck->isChecked(), gridCellSpin->value());
        display->setGridLineWidth(gridWidthSpin->value());
    };
    QObject::connect(gridCheck, &QCheckBox::toggled, this, [updateGridOverlay](bool) { updateGridOverlay(); });
    QObject::connect(gridCellSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [updateGridOverlay](int) { updateGridOverlay(); });
    QObject::connect(gridWidthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [updateGridOverlay](int) { updateGridOverlay(); });

    auto syncGridOutput = [this, gridOnOutputCheck, gridCellSpin, gridWidthSpin]() {
        if (!gridOnOutputCheck->isChecked())
            return;
        ensureGridSource();
        if (gridSource_.Get()) {
            updateGridSourceSettings(gridSource_.Get(), gridCellSpin->value(),
                                     gridWidthSpin->value(), display->gridColor());
            syncGridOutputToScene(sceneCombo_->currentText(), true);
        }
    };

    QObject::connect(gridOnOutputCheck, &QCheckBox::toggled, this,
                     [this, gridOnOutputCheck, gridCellSpin, gridWidthSpin](bool checked) {
                         const QString sceneName = sceneCombo_->currentText();
                         QTimer::singleShot(0, this, [this, checked, sceneName, gridCellSpin,
                                                       gridWidthSpin]() {
                             if (checked) {
                                 obs_video_info ovi = {};
                                 const bool haveVideo = obs_get_video_info(&ovi) && ovi.base_width > 0 && ovi.base_height > 0;
                                 const int w = haveVideo ? (int)ovi.base_width : 1920;
                                 const int h = haveVideo ? (int)ovi.base_height : 1080;

                                 const QString path = GetGridPngPath();
                                 if (GenerateGridPng(path, w, h, display->gridColor(),
                                                     gridCellSpin->value(),
                                                     gridWidthSpin->value())) {
                                     ensureGridSource();
                                     if (gridSource_.Get()) {
                                         updateGridSourceSettings(gridSource_.Get(),
                                                                  gridCellSpin->value(),
                                                                  gridWidthSpin->value(),
                                                                  display->gridColor());
                                         syncGridOutputToScene(sceneName, true);
                                         blog(LOG_INFO, "[rpg-map-effects] Grid on output: added to scene \"%s\".", sceneName.toUtf8().constData());
                                     }
                                 }
                             } else {
                                 syncGridOutputToScene(sceneName, false);
                             }
                         });
                     });

    // Do NOT sync grid when switching battlemaps: it can crash when scenes share sources
    // or use move transitions (graphics thread can hit detach_sceneitem with invalid state).
    // Grid is only added/updated when the user checks "Show grid on output" or changes grid settings.
    // To get the grid on a different battlemap, uncheck then check "Show grid on output" after switching.

    QObject::connect(gridCellSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
                     [syncGridOutput](int) { syncGridOutput(); });
    QObject::connect(gridWidthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
                     [syncGridOutput](int) { syncGridOutput(); });
    QObject::connect(gridColorBtn, &QPushButton::clicked, this, [this, syncGridOutput]() {
        const QColor current = display->gridColor();
        QColor chosen = QColorDialog::getColor(current, this, "Choose grid color");
        if (chosen.isValid()) {
            display->setGridColor(chosen);
            syncGridOutput();
        }
    });

    QObject::connect(display, &OBSDisplayWidget::sceneClicked, this,
                     [this, coordLabel, gridCheck, gridCellSpin](float x, float y, float nx, float ny, bool inside) {
                         lastClickX = x;
                         lastClickY = y;
                         lastClickInside = inside;

                         if (!inside) {
                             coordLabel->setText("Click: (outside map)");
                             return;
                         }

                         QString text = QString("Click: x=%1 y=%2  |  n=(%3, %4)")
                                            .arg(x, 0, 'f', 1)
                                            .arg(y, 0, 'f', 1)
                                            .arg(nx, 0, 'f', 4)
                                            .arg(ny, 0, 'f', 4);
                         if (gridCheck->isChecked() && gridCellSpin->value() > 0) {
                             const int cell = gridCellSpin->value();
                             const int col = static_cast<int>(std::floor(x / cell));
                             const int row = static_cast<int>(std::floor(y / cell));
                             text += QString("  |  cell (%1, %2)").arg(col).arg(row);
                         }
                         coordLabel->setText(text);

                         // Click-to-select nearest FX instance on the map canvas.
                         const int idx = findNearestFxInstanceIndex(x, y);
                         if (idx >= 0 && idx < fxList->count())
                             fxList->setCurrentRow(idx);
                     });

    // Build FX side panel UI.
    fxLayout->addWidget(new QLabel("Effects", this));

    auto *fxCombo = new QComboBox(this);
    fxCombo->setMinimumWidth(200);
    fxLayout->addWidget(new QLabel("Template Scene:", this));
    fxLayout->addWidget(fxCombo);

    auto *sequenceBox = new QCheckBox("Sequence [ms] using [123] name prefixes", this);
    sequenceBox->setChecked(true);
    fxLayout->addWidget(sequenceBox);

    auto *labelRowWidget = new QWidget(this);
    auto *labelRowLayout = new QHBoxLayout(labelRowWidget);
    labelRowLayout->setContentsMargins(0, 0, 0, 0);
    labelRowLayout->setSpacing(4);

    auto *labelCheck = new QCheckBox("Show label on map", labelRowWidget);
    labelCheck->setChecked(false);
    labelRowLayout->addWidget(labelCheck);

    labelEdit = new QLineEdit(labelRowWidget);
    labelEdit->setPlaceholderText("Label (per instance, optional)");
    labelRowLayout->addWidget(labelEdit, 1);

    auto *labelColorBtn = new QPushButton("Label color…", labelRowWidget);
    labelRowLayout->addWidget(labelColorBtn);

    fxLayout->addWidget(labelRowWidget);

    QObject::connect(labelColorBtn, &QPushButton::clicked, this, [this]() {
        QColor chosen = QColorDialog::getColor(labelColor_, this, "Choose label color");
        if (chosen.isValid())
            labelColor_ = chosen;
    });

    auto *fadeSpin = new QSpinBox(this);
    fadeSpin->setRange(0, 60000);
    fadeSpin->setSingleStep(100);
    fadeSpin->setValue(600);
    fadeSpin->setSuffix(" ms fade-out");
    fxLayout->addWidget(fadeSpin);

    auto *lifetimeSpin = new QSpinBox(this);
    lifetimeSpin->setRange(0, 3600);
    lifetimeSpin->setSingleStep(5);
    lifetimeSpin->setValue(0);
    lifetimeSpin->setSuffix(" s lifetime (0 = infinite)");
    fxLayout->addWidget(lifetimeSpin);

    auto applyTemplateDefaults = [fxCombo, sequenceBox, labelCheck, fadeSpin, lifetimeSpin]() {
        const QString name = fxCombo->currentText();
        if (name.isEmpty())
            return;
        obs_source_t *src = obs_get_source_by_name(name.toUtf8().constData());
        if (!src)
            return;
        const char *uuid = obs_source_get_uuid(src);
        obs_source_release(src);
        if (!uuid)
            return;
        TemplateDefaults def = LoadTemplateDefaults(QString::fromUtf8(uuid));
        sequenceBox->setChecked(def.sequence);
        labelCheck->setChecked(def.showLabel);
        fadeSpin->setValue(def.fadeMs);
        lifetimeSpin->setValue(def.lifetimeSec);
    };

    QObject::connect(fxCombo, &QComboBox::currentTextChanged, this, applyTemplateDefaults);

    auto *spawnBtn = new QPushButton("Spawn at last click", this);
    fxLayout->addWidget(spawnBtn);

    auto *clearSelectedBtn = new QPushButton("Clear selected", this);
    auto *clearLastBtn = new QPushButton("Clear last", this);
    auto *clearAllBtn = new QPushButton("Clear all", this);

    auto *btnRow = new QWidget(this);
    auto *btnRowLayout = new QHBoxLayout(btnRow);
    btnRowLayout->setContentsMargins(0, 0, 0, 0);
    btnRowLayout->setSpacing(4);
    btnRowLayout->addWidget(clearSelectedBtn);
    btnRowLayout->addWidget(clearLastBtn);
    btnRowLayout->addWidget(clearAllBtn);
    fxLayout->addWidget(btnRow);

    auto *refreshBtn = new QPushButton("Refresh scenes", this);
    fxLayout->addWidget(refreshBtn);

    auto *dupAsTemplateBtn = new QPushButton("Duplicate selected as template", this);
    fxLayout->addWidget(dupAsTemplateBtn);

    fxList = new QListWidget(this);
    fxList->setSelectionMode(QAbstractItemView::SingleSelection);
    fxList->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked);
    fxLayout->addWidget(new QLabel("Active Effects:", this));
    fxLayout->addWidget(fxList, /*stretch*/ 1);

    auto *helpGroup = new QGroupBox("FX template help", this);
    helpGroup->setCheckable(true);
    helpGroup->setChecked(false);
    auto *helpText = new QTextEdit(helpGroup);
    helpText->setReadOnly(true);
    helpText->setMaximumHeight(120);
    helpText->setHtml(
        "<p><b>Template scenes:</b> Name scenes <code>FX: Something</code> (any case) to use them as effect templates.</p>"
        "<p><b>Sequencing:</b> Prefix source names with <code>[250]</code> for staged reveal (ms delay).</p>"
        "<p><b>Fade-out:</b> Set fade ms &gt; 0 to enable hide transition when clearing.</p>"
        "<p><b>Lifetime:</b> 0 = infinite; otherwise auto-clear after N seconds.</p>"
        "<p><b>Per-template defaults:</b> Your fade, lifetime, sequence, and label settings are saved per template when you spawn.</p>");
    auto *helpLayout = new QVBoxLayout(helpGroup);
    helpLayout->addWidget(helpText);
    fxLayout->addWidget(helpGroup);

    QObject::connect(fxList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!labelEdit)
            return;
        if (row < 0 || row >= (int)activeFx.size()) {
            labelEdit->clear();
            return;
        }
        labelEdit->setText(activeFx[static_cast<size_t>(row)].label);
    });

    QObject::connect(fxList, &QListWidget::itemChanged, this,
                     [this](QListWidgetItem *item) {
        if (!item)
            return;
        const int row = fxList->row(item);
        if (row < 0 || row >= (int)activeFx.size())
            return;

        FxInstance &inst = activeFx[static_cast<size_t>(row)];
        const QString newLabel = item->text().trimmed();
        inst.label = newLabel;
        if (labelEdit && fxList->currentRow() == row)
            labelEdit->setText(newLabel);

        // Update text and color of any FX Label source inside this FX scene instance.
        struct LabelUpdate {
            QString *label;
            uint32_t colorArgb;
        } lu = {&inst.label,
                (uint32_t(labelColor_.alpha()) << 24) | (uint32_t(labelColor_.red()) << 16) |
                    (uint32_t(labelColor_.green()) << 8) | uint32_t(labelColor_.blue())};
        OBSSourceAutoRelease fxSrc(obs_get_source_by_uuid(inst.effectUuid.toUtf8().constData()));
        if (fxSrc.Get()) {
            obs_scene_t *scene = obs_scene_from_source(fxSrc.Get());
            if (scene) {
                obs_scene_enum_items(
                    scene,
                    [](obs_scene_t *, obs_sceneitem_t *it, void *param) {
                        LabelUpdate *lu = static_cast<LabelUpdate *>(param);
                        obs_source_t *src = obs_sceneitem_get_source(it);
                        if (!src)
                            return true;
                        const char *nm = obs_source_get_name(src);
                        if (!nm)
                            return true;
                        QString name = QString::fromUtf8(nm);
                        if (!name.startsWith("FX Label "))
                            return true;
                        obs_data_t *settings = obs_source_get_settings(src);
                        if (!settings)
                            return true;
                        obs_data_set_string(settings, "text", lu->label->toUtf8().constData());
                        if (lu->colorArgb != 0)
                            obs_data_set_int(settings, "color1", (int64_t)lu->colorArgb);
                        obs_source_update(src, settings);
                        obs_data_release(settings);
                        return true;
                    },
                    &lu);
            }
        }
    });

    auto refreshLists = [this, fxCombo, applyTemplateDefaults]() {
        const QString prevScene = sceneCombo_->currentText();
        const QString prevFx = fxCombo->currentText();

        AddBattlemapScenesToCombo(sceneCombo_);
        AddFxTemplateScenesToCombo(fxCombo);

        if (!prevScene.isEmpty()) {
            const int idx = sceneCombo_->findText(prevScene);
            if (idx >= 0)
                sceneCombo_->setCurrentIndex(idx);
        }
        if (!prevFx.isEmpty()) {
            const int idx = fxCombo->findText(prevFx);
            if (idx >= 0)
                fxCombo->setCurrentIndex(idx);
        }
        applyTemplateDefaults();
    };

    refreshLists();

    // Ensure grid image source exists so "Show grid on output" can add it to the battlemap.
    QTimer::singleShot(0, this, [this]() { ensureGridSource(); });

    QObject::connect(refreshBtn, &QPushButton::clicked, this, [refreshLists]() { refreshLists(); });

    QObject::connect(spawnBtn, &QPushButton::clicked, this,
                     [this, fxCombo, sequenceBox, labelCheck, fadeSpin, lifetimeSpin, snapCheck, gridCellSpin]() {
        if (!lastClickInside)
            return;

        OBSSourceAutoRelease mapSrc(display->getSceneSourceRef());
        if (!mapSrc.Get())
            return;

        float spawnX = lastClickX;
        float spawnY = lastClickY;
        if (snapCheck->isChecked() && gridCellSpin->value() > 0) {
            const float cell = static_cast<float>(gridCellSpin->value());
            spawnX = (std::floor(spawnX / cell) + 0.5f) * cell;
            spawnY = (std::floor(spawnY / cell) + 0.5f) * cell;
        }

        QString mapUuid;
        QString fxUuid;
        int64_t sceneItemId = 0;

        const bool seq = sequenceBox->isChecked();

        QString labelText;
        if (labelCheck->isChecked()) {
            // Default label is template scene name without the "fx:" prefix.
            labelText = fxCombo->currentText().trimmed();
            int colon = labelText.indexOf(':');
            if (colon >= 0)
                labelText = labelText.mid(colon + 1).trimmed();

            if (labelEdit) {
                const QString custom = labelEdit->text().trimmed();
                if (!custom.isEmpty())
                    labelText = custom;
            }
        }

        obs_source_t *tmplSrc = obs_get_source_by_name(fxCombo->currentText().toUtf8().constData());
        if (tmplSrc) {
            const char *uuid = obs_source_get_uuid(tmplSrc);
            if (uuid) {
                SaveTemplateDefaults(QString::fromUtf8(uuid), fadeSpin->value(), lifetimeSpin->value(),
                                    seq, labelCheck->isChecked());
            }
            obs_source_release(tmplSrc);
        }

        const uint32_t labelArgb = (uint32_t(labelColor_.alpha()) << 24) |
                                   (uint32_t(labelColor_.red()) << 16) |
                                   (uint32_t(labelColor_.green()) << 8) |
                                   uint32_t(labelColor_.blue());

        SpawnFxAtClick(fxCombo->currentText(), mapSrc.Get(), spawnX, spawnY, seq,
                       fadeSpin->value(), labelCheck->isChecked(), labelText, labelArgb,
                       mapUuid, fxUuid, sceneItemId);

        if (!mapUuid.isEmpty() && !fxUuid.isEmpty() && sceneItemId != 0) {
            FxInstance inst;
            inst.mapSceneUuid = mapUuid;
            inst.effectUuid = fxUuid;
            inst.sceneItemId = sceneItemId;
            inst.templateName = fxCombo->currentText();
            inst.label = labelText;
            activeFx.push_back(inst);
            const QString listText = inst.label.isEmpty() ? inst.templateName : inst.label;
            auto *item = new QListWidgetItem(listText);
            item->setFlags(item->flags() | Qt::ItemIsEditable);
            fxList->addItem(item);
            fxList->setCurrentRow(fxList->count() - 1);

            const int lifetimeSec = lifetimeSpin->value();
            if (lifetimeSec > 0) {
                const int fadeMs = fadeSpin->value();
                QTimer::singleShot(lifetimeSec * 1000, qApp, [this, inst, fadeMs]() {
                    // If instance was already cleared, this will no-op.
                    for (size_t i = 0; i < activeFx.size(); ++i) {
                        const FxInstance &cur = activeFx[i];
                        if (cur.effectUuid == inst.effectUuid &&
                            cur.mapSceneUuid == inst.mapSceneUuid &&
                            cur.sceneItemId == inst.sceneItemId) {
                            fadeOutAndRemoveInstance(cur, fadeMs);
                            activeFx.erase(activeFx.begin() + (long)i);
                            delete fxList->takeItem((int)i);
                            break;
                        }
                    }
                });
            }
        }
    });

    QObject::connect(clearLastBtn, &QPushButton::clicked, this, [this, fadeSpin]() {
        if (activeFx.empty())
            return;

        const FxInstance inst = activeFx.back();
        const int fadeMs = fadeSpin->value();
        fadeOutAndRemoveInstance(inst, fadeMs);
        activeFx.pop_back();

        if (fxList->count() > 0)
            delete fxList->takeItem(fxList->count() - 1);
    });

    QObject::connect(clearSelectedBtn, &QPushButton::clicked, this, [this, fadeSpin]() {
        const int row = fxList->currentRow();
        if (row < 0 || row >= (int)activeFx.size())
            return;

        const FxInstance inst = activeFx[static_cast<size_t>(row)];
        const int fadeMs = fadeSpin->value();
        fadeOutAndRemoveInstance(inst, fadeMs);

        activeFx.erase(activeFx.begin() + row);
        delete fxList->takeItem(row);
    });

    QObject::connect(clearAllBtn, &QPushButton::clicked, this, [this, fadeSpin]() {
        const int fadeMs = fadeSpin->value();
        for (const FxInstance &inst : activeFx)
            fadeOutAndRemoveInstance(inst, fadeMs);

        activeFx.clear();
        fxList->clear();
    });

    QObject::connect(dupAsTemplateBtn, &QPushButton::clicked, this,
                     [this, fxCombo](void) {
                         const int row = fxList->currentRow();
                         if (row < 0 || row >= (int)activeFx.size())
                             return;

                         const FxInstance &inst = activeFx[static_cast<size_t>(row)];
                         OBSSourceAutoRelease fxSrc(obs_get_source_by_uuid(inst.effectUuid.toUtf8().constData()));
                         if (!fxSrc.Get() || !obs_source_is_scene(fxSrc.Get()))
                             return;

                         const char *origName = obs_source_get_name(fxSrc.Get());
                         QString baseName = QString::fromUtf8(origName);
                         if (!baseName.startsWith("FX", Qt::CaseInsensitive))
                             baseName = "FX Spawn " + baseName;
                         const QString newName =
                             QString("FX: %1 (copy)").arg(baseName);

                         obs_source_t *dup = obs_source_duplicate(fxSrc.Get(), newName.toUtf8().constData(), true);
                         if (!dup)
                             return;

                         OBSSourceAutoRelease curSrc(obs_frontend_get_current_scene());
                         if (curSrc.Get()) {
                             obs_scene_t *curScene = obs_scene_from_source(curSrc.Get());
                             if (curScene) {
                                 obs_sceneitem_t *item = obs_scene_add(curScene, dup);
                                 obs_source_release(dup);
                                 if (item)
                                     obs_sceneitem_set_visible(item, false);
                            } else {
                                obs_source_release(dup);
                            }
                         } else {
                             obs_source_release(dup);
                             return;
                         }

                         AddFxTemplateScenesToCombo(fxCombo);
                         const int idx = fxCombo->findText(newName);
                         if (idx >= 0)
                             fxCombo->setCurrentIndex(idx);
                     });

    // Version indicator (in the status bar).
    verLabel->setText(QString("Build (compile): %1 | Plugin mtime: %2")
                          .arg(kCompileStamp)
                          .arg(GetPluginMTimeString()));

    // Default to current scene
    obs_source_t *cur = obs_frontend_get_current_scene();
    if (cur) {
        const char *curName = obs_source_get_name(cur);
        int idx = sceneCombo_->findText(QString::fromUtf8(curName));
        if (idx >= 0) {
            sceneCombo_->setCurrentIndex(idx);
            display->setSceneByName(QString::fromUtf8(curName));
        }
        obs_source_release(cur);
    }

    // Allow editing label of selected instance to update on-map text.
    QObject::connect(labelEdit, &QLineEdit::editingFinished, this, [this]() {
        if (!fxList)
            return;
        const int row = fxList->currentRow();
        if (row < 0 || row >= (int)activeFx.size())
            return;

        FxInstance &inst = activeFx[static_cast<size_t>(row)];
        const QString newLabel = labelEdit->text().trimmed();
        inst.label = newLabel;

        // Update text and color of any FX Label source inside this FX scene instance.
        struct LabelUpdateEdit {
            QString *label;
            uint32_t colorArgb;
        } luEdit = {&inst.label,
                    (uint32_t(labelColor_.alpha()) << 24) | (uint32_t(labelColor_.red()) << 16) |
                        (uint32_t(labelColor_.green()) << 8) | uint32_t(labelColor_.blue())};
        OBSSourceAutoRelease fxSrc(obs_get_source_by_uuid(inst.effectUuid.toUtf8().constData()));
        if (fxSrc.Get()) {
            obs_scene_t *scene = obs_scene_from_source(fxSrc.Get());
            if (scene) {
                obs_scene_enum_items(
                    scene,
                    [](obs_scene_t *, obs_sceneitem_t *item, void *param) {
                        LabelUpdateEdit *lu = static_cast<LabelUpdateEdit *>(param);
                        obs_source_t *src = obs_sceneitem_get_source(item);
                        if (!src)
                            return true;
                        const char *nm = obs_source_get_name(src);
                        if (!nm)
                            return true;
                        QString name = QString::fromUtf8(nm);
                        if (!name.startsWith("FX Label "))
                            return true;
                        obs_data_t *settings = obs_source_get_settings(src);
                        if (!settings)
                            return true;
                        obs_data_set_string(settings, "text", lu->label->toUtf8().constData());
                        if (lu->colorArgb != 0)
                            obs_data_set_int(settings, "color1", (int64_t)lu->colorArgb);
                        obs_source_update(src, settings);
                        obs_data_release(settings);
                        return true;
                    },
                    &luEdit);
            }
        }

        // Update list text: show only the label (or template name if no custom label).
        const QString listText = inst.label.isEmpty() ? inst.templateName : inst.label;
        if (QListWidgetItem *item = fxList->item(row))
            item->setText(listText);
    });
}

OBSSource RPGWindow::ensureGridSource()
{
    if (gridSource_.Get())
        return gridSource_;

    obs_source_t *existing = obs_get_source_by_name(kGridSourceName);
    if (existing) {
        gridSource_ = OBSSource(existing);
        obs_source_release(existing);
        return gridSource_;
    }

    obs_data_t *settings = obs_data_create();
    obs_data_set_string(settings, "file", GetGridPngPath().toUtf8().constData());
    obs_data_set_bool(settings, "unload", false);

    obs_source_t *src = obs_source_create("image_source", kGridSourceName, settings, nullptr);
    obs_data_release(settings);
    if (src)
        gridSource_ = OBSSource(src);
    return gridSource_;
}

void RPGWindow::updateGridSourceSettings(obs_source_t *gridSource, int cellSize, int lineWidth,
                                        const QColor &color)
{
    if (!gridSource)
        return;

    obs_video_info ovi = {};
    const bool haveVideo = obs_get_video_info(&ovi) && ovi.base_width > 0 && ovi.base_height > 0;
    const int w = haveVideo ? (int)ovi.base_width : 1920;
    const int h = haveVideo ? (int)ovi.base_height : 1080;

    const QString path = GetGridPngPath();
    if (!GenerateGridPng(path, w, h, color, cellSize, lineWidth))
        return;

    obs_data_t *settings = obs_source_get_settings(gridSource);
    if (!settings)
        return;
    obs_data_set_string(settings, "file", path.toUtf8().constData());
    obs_source_update(gridSource, settings);
    obs_data_release(settings);
}

void RPGWindow::syncGridOutputToScene(const QString &sceneName, bool showOnOutput)
{
    if (!gridSource_.Get() || sceneName.isEmpty())
        return;

    OBSSourceAutoRelease sceneSrc(obs_get_source_by_name(sceneName.toUtf8().constData()));
    if (!sceneSrc.Get() || !obs_source_is_scene(sceneSrc.Get()))
        return;

    obs_scene_t *scene = obs_scene_from_source(sceneSrc.Get());
    if (!scene)
        return;

    addGridToSceneAndSetVisible(scene, gridSource_.Get(), showOnOutput);

    if (!showOnOutput) {
        const QString path = GetGridPngPath();
        if (WriteBlankGridPng(path)) {
            obs_data_t *settings = obs_source_get_settings(gridSource_.Get());
            if (settings) {
                obs_data_set_string(settings, "file", path.toUtf8().constData());
                obs_source_update(gridSource_.Get(), settings);
                obs_data_release(settings);
            }
        }
    }
}

RPGWindow::~RPGWindow()
{
    /* Do not add/remove grid from scenes (stays in plugin only). */
}
