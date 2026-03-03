#include "rpg_window.hpp"
#include "obs_display_widget.hpp"

#include <cstdlib>
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
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyle>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QPixmap>
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

static QIcon iconFromColor(const QColor &c)
{
    QPixmap pm(20, 20);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    // Palette shape: rounded rect (paint well) + small circle (thumb)
    p.setBrush(c);
    p.setPen(QColor(100, 100, 100));
    p.drawRoundedRect(2, 2, 14, 14, 3, 3);
    p.setBrush(Qt::lightGray);
    p.drawEllipse(10, 10, 5, 5);
    p.end();
    return QIcon(pm);
}

static QIcon iconTarget()
{
    QPixmap pm(20, 20);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(Qt::darkGray, 2));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(4, 4, 12, 12);
    p.drawEllipse(7, 7, 6, 6);
    p.setBrush(Qt::darkGray);
    p.drawEllipse(9, 9, 2, 2);
    p.end();
    return QIcon(pm);
}

static const int kToolButtonSize = 24;

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

static const char *kCursorSourceName = "RPG Map Cursor";

static QString GetCursorPngPath()
{
    return QDir::temp().absoluteFilePath(QStringLiteral("rpg_map_effects_cursor.png"));
}

/** Generate a simple dot (circle) cursor PNG. Size is diameter in pixels; image is size*2 square. */
static bool GenerateCursorPng(const QString &path, int sizePx, const QColor &color)
{
    if (sizePx < 4 || sizePx > 128)
        return false;
    const int side = sizePx * 2;
    QImage img(side, side, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    const double left = (side - sizePx) / 2.0;
    p.drawEllipse(QRectF(left, left, double(sizePx), double(sizePx)));
    p.end();
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

struct CursorSceneItemCtx {
    obs_source_t *cursorSource = nullptr;
    obs_sceneitem_t *item = nullptr;
};

static bool findCursorItemInScene(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
    auto *ctx = static_cast<CursorSceneItemCtx *>(param);
    if (!ctx->cursorSource)
        return true;
    obs_source_t *src = obs_sceneitem_get_source(item);
    if (src != ctx->cursorSource)
        return true;
    ctx->item = item;
    obs_sceneitem_addref(item);
    return false;
}

struct AddCursorToSceneData {
    obs_source_t *cursorSource = nullptr;
    bool visible = false;
    float posX = 0.0f;
    float posY = 0.0f;
};

static void AddOrShowCursorInSceneHelper(void *param, obs_scene_t *scene)
{
    auto *data = static_cast<AddCursorToSceneData *>(param);
    if (!data || !data->cursorSource || !scene)
        return;

    CursorSceneItemCtx ctx;
    ctx.cursorSource = data->cursorSource;
    obs_scene_enum_items(scene, findCursorItemInScene, &ctx);

    struct vec2 pos;
    vec2_set(&pos, data->posX, data->posY);

    if (ctx.item) {
        obs_sceneitem_set_visible(ctx.item, data->visible);
        obs_sceneitem_set_pos(ctx.item, &pos);
        obs_sceneitem_release(ctx.item);
        return;
    }

    obs_sceneitem_t *item = obs_scene_add(scene, data->cursorSource);
    if (item) {
        obs_sceneitem_set_visible(item, data->visible);
        obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);
        obs_sceneitem_set_alignment(item, OBS_ALIGN_CENTER);
        obs_sceneitem_set_blending_mode(item, OBS_BLEND_SCREEN);
        obs_sceneitem_set_pos(item, &pos);
        obs_sceneitem_release(item);
    }
}

struct UpdateCursorPosData {
    obs_source_t *cursorSource = nullptr;
    float posX = 0.0f;
    float posY = 0.0f;
};

static void UpdateCursorPosInSceneHelper(void *param, obs_scene_t *scene)
{
    auto *data = static_cast<UpdateCursorPosData *>(param);
    if (!data || !data->cursorSource || !scene)
        return;

    CursorSceneItemCtx ctx;
    ctx.cursorSource = data->cursorSource;
    obs_scene_enum_items(scene, findCursorItemInScene, &ctx);
    if (!ctx.item)
        return;

    struct vec2 pos;
    vec2_set(&pos, data->posX, data->posY);
    obs_sceneitem_set_pos(ctx.item, &pos);
    obs_sceneitem_release(ctx.item);
}

/** Param for graphics-thread cursor position task; task frees it. */
struct CursorPosTaskParam {
    char *sceneName = nullptr;
    obs_source_t *cursorSource = nullptr;
    float posX = 0.0f;
    float posY = 0.0f;
};

static void ApplyCursorPositionTask(void *param)
{
    auto *p = static_cast<CursorPosTaskParam *>(param);
    if (!p || !p->sceneName || !p->cursorSource) {
        if (p) {
            if (p->cursorSource)
                obs_source_release(p->cursorSource);
            free(p->sceneName);
            free(p);
        }
        return;
    }
    OBSSourceAutoRelease sceneSrc(obs_get_source_by_name(p->sceneName));
    if (sceneSrc.Get() && obs_source_is_scene(sceneSrc.Get())) {
        obs_scene_t *scene = obs_scene_from_source(sceneSrc.Get());
        if (scene) {
            UpdateCursorPosData data;
            data.cursorSource = p->cursorSource;
            data.posX = p->posX;
            data.posY = p->posY;
            obs_scene_atomic_update(scene, UpdateCursorPosInSceneHelper, &data);
        }
    }
    obs_source_release(p->cursorSource);
    free(p->sceneName);
    free(p);
}

/** Queue cursor position update to run on the graphics thread (avoids main-thread obs_enter_graphics crash). */
static void queueCursorPositionUpdate(const QString &sceneName, obs_source_t *cursorSource, float sceneX, float sceneY)
{
    if (sceneName.isEmpty() || !cursorSource)
        return;
    QByteArray nameBytes = sceneName.toUtf8();
    char *nameCopy = strdup(nameBytes.constData());
    if (!nameCopy)
        return;
    auto *p = static_cast<CursorPosTaskParam *>(malloc(sizeof(CursorPosTaskParam)));
    if (!p) {
        free(nameCopy);
        return;
    }
    p->sceneName = nameCopy;
    p->cursorSource = obs_source_get_ref(cursorSource);
    p->posX = sceneX;
    p->posY = sceneY;
    obs_queue_task(OBS_TASK_GRAPHICS, ApplyCursorPositionTask, p, false);
}

/** Param for graphics-thread "add/show cursor in scene" task; task frees it. */
struct AddCursorToSceneTaskParam {
    char *sceneName = nullptr;
    obs_source_t *cursorSource = nullptr;
    bool visible = false;
    float posX = 0.0f;
    float posY = 0.0f;
};

static void AddCursorToSceneTask(void *param)
{
    auto *p = static_cast<AddCursorToSceneTaskParam *>(param);
    if (!p || !p->sceneName || !p->cursorSource) {
        if (p) {
            if (p->cursorSource)
                obs_source_release(p->cursorSource);
            free(p->sceneName);
            free(p);
        }
        return;
    }
    OBSSourceAutoRelease sceneSrc(obs_get_source_by_name(p->sceneName));
    if (sceneSrc.Get() && obs_source_is_scene(sceneSrc.Get())) {
        obs_scene_t *scene = obs_scene_from_source(sceneSrc.Get());
        if (scene) {
            AddCursorToSceneData data;
            data.cursorSource = p->cursorSource;
            data.visible = p->visible;
            data.posX = p->posX;
            data.posY = p->posY;
            obs_scene_atomic_update(scene, AddOrShowCursorInSceneHelper, &data);
        }
    }
    obs_source_release(p->cursorSource);
    free(p->sceneName);
    free(p);
}

static void queueAddCursorToScene(const QString &sceneName, obs_source_t *cursorSource,
                                   bool visible, float posX, float posY)
{
    if (sceneName.isEmpty() || !cursorSource)
        return;
    QByteArray nameBytes = sceneName.toUtf8();
    char *nameCopy = strdup(nameBytes.constData());
    if (!nameCopy)
        return;
    auto *p = static_cast<AddCursorToSceneTaskParam *>(malloc(sizeof(AddCursorToSceneTaskParam)));
    if (!p) {
        free(nameCopy);
        return;
    }
    p->sceneName = nameCopy;
    p->cursorSource = obs_source_get_ref(cursorSource);
    p->visible = visible;
    p->posX = posX;
    p->posY = posY;
    obs_queue_task(OBS_TASK_GRAPHICS, AddCursorToSceneTask, p, false);
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

    // Default facing: up (top of screen). OBS rotation 0 = right, -90 = up.
    obs_sceneitem_set_rot(fxItem, -90.0f);

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

    // Clear any direction arrow when this FX is being removed, to avoid stale arrows.
    if (display)
        display->clearDirectionArrow();
    if (setDirectionCheck_)
        setDirectionCheck_->setChecked(false);

    // After fadeMs, actually remove the scene item and FX source.
    QTimer::singleShot(fadeMs, qApp, [inst]() {
        ClearLastFx(inst.mapSceneUuid, inst.effectUuid, inst.sceneItemId);
    });
}

int RPGWindow::findNearestFxInstanceIndex(float sceneX, float sceneY) const
{
    if (activeFx.empty())
        return -1;

    obs_video_info ovi = {};
    if (!obs_get_video_info(&ovi) || ovi.base_width == 0 || ovi.base_height == 0)
        return -1;
    const float baseW = float(ovi.base_width);
    const float baseH = float(ovi.base_height);

    const float maxRadius = 200.0f; // pixels in canvas space
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

        const uint32_t sceneW = obs_source_get_width(mapSrc.Get());
        const uint32_t sceneH = obs_source_get_height(mapSrc.Get());
        if (sceneW == 0 || sceneH == 0)
            continue;

        obs_scene_t *scene = obs_scene_from_source(mapSrc.Get());
        if (!scene)
            continue;

        obs_sceneitem_t *item = obs_scene_find_sceneitem_by_id(scene, inst.sceneItemId);
        if (!item)
            continue;

        vec2 pos;
        obs_sceneitem_get_pos(item, &pos);
        const float cx = pos.x * baseW / float(sceneW);
        const float cy = pos.y * baseH / float(sceneH);

        const float dx = cx - sceneX;
        const float dy = cy - sceneY;
        const float d2 = dx * dx + dy * dy;
        if (d2 < bestDist2) {
            bestDist2 = d2;
            bestIndex = static_cast<int>(i);
        }
    }

    return bestIndex;
}

QString RPGWindow::getMapSceneUuidFromName(const QString &sceneName) const
{
    if (sceneName.isEmpty())
        return {};
    OBSSourceAutoRelease src(obs_get_source_by_name(sceneName.toUtf8().constData()));
    if (!src.Get() || !obs_source_is_scene(src.Get()))
        return {};
    const char *uuid = obs_source_get_uuid(src.Get());
    return uuid ? QString::fromUtf8(uuid) : QString();
}

void RPGWindow::refreshFxListForCurrentMap()
{
    activeFx.clear();
    if (fxList)
        fxList->clear();
    if (currentMapSceneUuid_.isEmpty())
        return;
    auto it = fxByMapSceneUuid_.find(currentMapSceneUuid_);
    if (it == fxByMapSceneUuid_.end())
        return;
    for (const FxInstance &inst : it.value()) {
        activeFx.push_back(inst);
        if (fxList) {
            const QString listText = inst.label.isEmpty() ? inst.templateName : inst.label;
            fxList->addItem(new QListWidgetItem(listText));
        }
    }
    if (fxList) {
        for (int i = 0; i < fxList->count(); ++i)
            fxList->item(i)->setFlags(fxList->item(i)->flags() | Qt::ItemIsEditable);
    }
}

RPGWindow::RPGWindow()
{
    setWindowTitle("RPG Map Effects");
    resize(1200, 720);

    // Compact style: smaller font and controls so everything fits
    setStyleSheet(
        "QWidget { font-size: 11px; } "
        "QToolBar { spacing: 2px; padding: 1px 2px; } "
        "QComboBox, QSpinBox, QLineEdit { min-height: 22px; max-height: 22px; } "
        "QComboBox { min-width: 120px; } "
        "QToolButton { min-width: 24px; max-width: 24px; min-height: 24px; max-height: 24px; padding: 0; border: none; } "
        "QCheckBox { spacing: 4px; } "
        "QGroupBox { font-weight: bold; margin-top: 4px; padding-top: 4px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 6px; padding: 0 4px; } "
    );

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
    fxLayout->setContentsMargins(4, 4, 4, 4);
    fxLayout->setSpacing(3);
    hLayout->addWidget(fxPanel, /*stretch*/ 1);

    setCentralWidget(central);

    auto *tb = addToolBar("Controls");
    tb->setMovable(false);
    tb->setIconSize(QSize(16, 16));

    tb->addWidget(new QLabel("Scene:", this));

    sceneCombo_ = new QComboBox(this);
    sceneCombo_->setMinimumWidth(160);
    sceneCombo_->setMaximumWidth(220);
    tb->addWidget(sceneCombo_);

    AddBattlemapScenesToCombo(sceneCombo_);

    QObject::connect(sceneCombo_, &QComboBox::currentTextChanged, this,
                     [this](const QString &sceneName) {
                         display->setSceneByName(sceneName);
                         currentMapSceneUuid_ = getMapSceneUuidFromName(sceneName);
                         refreshFxListForCurrentMap();
                         if (setDirectionCheck_)
                             setDirectionCheck_->setChecked(false);
                         if (display)
                             display->clearDirectionArrow();
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

    auto *gridColorBtn = new QToolButton(this);
    gridColorBtn->setIcon(iconFromColor(display->gridColor()));
    gridColorBtn->setToolTip("Grid color");
    gridColorBtn->setFixedSize(kToolButtonSize, kToolButtonSize);
    gridColorBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
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

    auto *gridOnOutputCheck = new QCheckBox("Grid on output", this);
    gridOnOutputCheck->setChecked(false);
    gridOnOutputCheck->setToolTip(QStringLiteral("Adds the grid to your selected battlemap scene. Check to show on output, uncheck to hide. After switching battlemaps, uncheck then check again to move the grid to the new scene."));
    tb->addWidget(gridOnOutputCheck);

    tb->addSeparator();

    auto *cursorOnOutputCheck = new QCheckBox("Cursor on output", this);
    cursorOnOutputCheck->setChecked(false);
    cursorOnOutputCheck->setToolTip(QStringLiteral("Shows a dot cursor on the battlemap that follows the mouse. Uses Screen blend. Size and color below."));
    tb->addWidget(cursorOnOutputCheck);

    auto *cursorSizeSpin = new QSpinBox(this);
    cursorSizeSpin->setRange(8, 64);
    cursorSizeSpin->setValue(24);
    cursorSizeSpin->setSuffix(" px");
    tb->addWidget(new QLabel("Size:", this));
    tb->addWidget(cursorSizeSpin);

    auto *cursorColorBtn = new QToolButton(this);
    cursorColorBtn->setIcon(iconFromColor(cursorColor_));
    cursorColorBtn->setToolTip("Cursor color");
    cursorColorBtn->setFixedSize(kToolButtonSize, kToolButtonSize);
    cursorColorBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    tb->addWidget(cursorColorBtn);

    tb->addSeparator();

    // Status bar for live mouse / last click coordinates + version info.
    auto *mouseLabel = new QLabel(this);
    mouseLabel->setText("Mouse: —");
    auto *clickLabel = new QLabel(this);
    clickLabel->setText("Click: —");

    auto *verLabel = new QLabel(this);
    statusBar()->addPermanentWidget(mouseLabel, 1);
    statusBar()->addPermanentWidget(clickLabel, 1);
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
    QObject::connect(gridColorBtn, &QAbstractButton::clicked, this, [this, gridColorBtn, syncGridOutput]() {
        const QColor current = display->gridColor();
        QColor chosen = QColorDialog::getColor(current, this, "Choose grid color");
        if (chosen.isValid()) {
            display->setGridColor(chosen);
            gridColorBtn->setIcon(iconFromColor(chosen));
            syncGridOutput();
        }
    });

    // Cursor on output: add to scene (with delay to avoid crash), live position via timer.
    auto colorToArgb = [](const QColor &c) -> uint32_t {
        return (uint32_t(c.alpha()) << 24) | (uint32_t(c.red()) << 16) | (uint32_t(c.green()) << 8) | uint32_t(c.blue());
    };
    auto syncCursorOutput = [this, cursorOnOutputCheck, cursorSizeSpin, colorToArgb]() {
        if (!cursorOnOutputCheck->isChecked())
            return;
        ensureCursorSource();
        if (cursorSource_.Get()) {
            updateCursorSourceSettings(cursorSource_.Get(), cursorSizeSpin->value(), cursorColor_);
            float cx = lastCursorX;
            float cy = lastCursorY;
            if (cx == 0.0f && cy == 0.0f) {
                obs_video_info ovi = {};
                if (obs_get_video_info(&ovi) && ovi.base_width > 0 && ovi.base_height > 0) {
                    cx = float(ovi.base_width) * 0.5f;
                    cy = float(ovi.base_height) * 0.5f;
                }
            }
            syncCursorToScene(sceneCombo_->currentText(), true, cx, cy);
        }
    };

    QObject::connect(cursorOnOutputCheck, &QCheckBox::toggled, this,
                     [this, cursorOnOutputCheck, cursorSizeSpin, colorToArgb, syncCursorOutput](bool checked) {
                         const QString sceneName = sceneCombo_->currentText();
                         if (cursorUpdateTimer_)
                             cursorUpdateTimer_->stop();
                         if (!checked) {
                             syncCursorToScene(sceneName, false, 0.0f, 0.0f);
                             if (display)
                                 display->setCursorOverlay(false, 0.0f, 0.0f);
                             return;
                         }
                         ensureCursorSource();
                         if (!cursorSource_.Get())
                             return;
                         updateCursorSourceSettings(cursorSource_.Get(), cursorSizeSpin->value(), cursorColor_);
                         float cx = lastCursorX;
                         float cy = lastCursorY;
                         if (cx == 0.0f && cy == 0.0f) {
                             obs_video_info ovi = {};
                             if (obs_get_video_info(&ovi) && ovi.base_width > 0 && ovi.base_height > 0) {
                                 cx = float(ovi.base_width) * 0.5f;
                                 cy = float(ovi.base_height) * 0.5f;
                             }
                         }
                         if (display) {
                             display->setCursorOverlayStyle(cursorSizeSpin->value(), colorToArgb(cursorColor_));
                             display->setCursorOverlay(true, cx, cy);
                         }
                         lastCursorX = cx;
                         lastCursorY = cy;
                         // Add cursor to scene after delay to avoid add-during-render crash.
                         QTimer::singleShot(500, this, [this, sceneName, cx, cy]() {
                             if (!cursorSource_.Get())
                                 return;
                             syncCursorToScene(sceneName, true, lastCursorX, lastCursorY);
                         });
                         if (!cursorUpdateTimer_) {
                             cursorUpdateTimer_ = new QTimer(this);
                             cursorUpdateTimer_->setSingleShot(false);
                             QObject::connect(cursorUpdateTimer_, &QTimer::timeout, this, [this, cursorOnOutputCheck]() {
                                 if (!cursorOnOutputCheck->isChecked() || !cursorSource_.Get())
                                     return;
                                 updateCursorPosition(sceneCombo_->currentText(), lastCursorX, lastCursorY);
                                 if (display)
                                     display->setCursorOverlay(true, lastCursorX, lastCursorY);
                             });
                         }
                         cursorUpdateTimer_->start(33);
                     });

    QObject::connect(cursorSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
                     [syncCursorOutput](int) { syncCursorOutput(); });
    QObject::connect(cursorColorBtn, &QAbstractButton::clicked, this, [this, cursorColorBtn, cursorOnOutputCheck, cursorSizeSpin, syncCursorOutput]() {
        QColor chosen = QColorDialog::getColor(cursorColor_, this, "Choose cursor color");
        if (chosen.isValid()) {
            cursorColor_ = chosen;
            cursorColorBtn->setIcon(iconFromColor(chosen));
            syncCursorOutput();
        }
    });

    // Convert effect position from scene space to canvas (base) space for display/arrow.
    auto scenePosToCanvas = [](obs_source_t *mapSceneSource, float posX, float posY, float &outCX, float &outCY) -> bool {
        obs_video_info ovi = {};
        if (!obs_get_video_info(&ovi) || ovi.base_width == 0 || ovi.base_height == 0)
            return false;
        const uint32_t baseW = ovi.base_width;
        const uint32_t baseH = ovi.base_height;
        const uint32_t sceneW = obs_source_get_width(mapSceneSource);
        const uint32_t sceneH = obs_source_get_height(mapSceneSource);
        if (sceneW == 0 || sceneH == 0)
            return false;
        outCX = posX * float(baseW) / float(sceneW);
        outCY = posY * float(baseH) / float(sceneH);
        return true;
    };

    QObject::connect(display, &OBSDisplayWidget::sceneMouseMoved, this,
                     [this, cursorOnOutputCheck, mouseLabel, gridCheck, gridCellSpin, scenePosToCanvas](float x, float y,
                                                                                                        float nx,
                                                                                                        float ny,
                                                                                                        bool inside,
                                                                                                        bool leftButtonDown) {
                         if (cursorOnOutputCheck->isChecked()) {
                             lastCursorX = x;
                             lastCursorY = y;
                             if (display)
                                 display->setCursorOverlay(true, x, y);
                         }
                         // Live mouse tracking in status bar (similar to click/grid readout).
                         {
                             QString text;
                             if (!inside) {
                                 text = QStringLiteral("Mouse: (outside map)");
                             } else {
                                 // Derive normalized coords if not provided (fallback), then cell if grid enabled.
                                 obs_video_info ovi = {};
                                 float nnx = nx;
                                 float nny = ny;
                                 if ((!std::isfinite(nnx) || !std::isfinite(nny)) &&
                                     obs_get_video_info(&ovi) && ovi.base_width > 0 && ovi.base_height > 0) {
                                     nnx = x / float(ovi.base_width);
                                     nny = y / float(ovi.base_height);
                                 }
                                 text = QString("Mouse: x=%1 y=%2  |  n=(%3, %4)")
                                            .arg(x, 0, 'f', 1)
                                            .arg(y, 0, 'f', 1)
                                            .arg(nnx, 0, 'f', 4)
                                            .arg(nny, 0, 'f', 4);
                                 if (gridCheck->isChecked() && gridCellSpin->value() > 0) {
                                     const int cell = gridCellSpin->value();
                                     const int col = static_cast<int>(std::floor(x / cell));
                                     const int row = static_cast<int>(std::floor(y / cell));
                                     text += QString("  |  cell (%1, %2)").arg(col).arg(row);
                                 }
                             }
                             mouseLabel->setText(text);
                         }
                         // Direction arrow / rotation logic.
                         if (!display || !setDirectionCheck_ || !setDirectionCheck_->isChecked() || !fxList) {
                             // Not in rotate mode or no display/selection: ensure arrow is off.
                             if (display)
                                 display->clearDirectionArrow();
                             return;
                         }

                         if (!leftButtonDown) {
                             // Rotate mode on but not dragging: arrow must not be visible.
                             display->clearDirectionArrow();
                             return;
                         }

                         // Rotate mode + left button down + FX selected: show single live arrow and rotate FX.
                         const int row = fxList->currentRow();
                         if (row < 0 || row >= (int)activeFx.size()) {
                             display->clearDirectionArrow();
                             return;
                         }

                         const FxInstance &inst = activeFx[static_cast<size_t>(row)];
                         OBSSourceAutoRelease mapSrc(obs_get_source_by_uuid(inst.mapSceneUuid.toUtf8().constData()));
                         if (!mapSrc.Get()) {
                             display->clearDirectionArrow();
                             return;
                         }

                         obs_scene_t *scene = obs_scene_from_source(mapSrc.Get());
                         obs_sceneitem_t *item = scene ? obs_scene_find_sceneitem_by_id(scene, inst.sceneItemId) : nullptr;
                         if (!item) {
                             display->clearDirectionArrow();
                             return;
                         }

                         struct vec2 pos;
                         obs_sceneitem_get_pos(item, &pos);
                         float cx = 0.0f, cy = 0.0f;
                         if (!scenePosToCanvas(mapSrc.Get(), pos.x, pos.y, cx, cy)) {
                             display->clearDirectionArrow();
                             return;
                         }

                         // Live preview: arrow from FX center toward mouse (only while dragging).
                         display->setDirectionArrow(cx, cy, x, y);

                         // Live rotation: make the FX face the arrow direction while moving.
                         const float dx = x - cx;
                         const float dy = y - cy;
                         const float angleRad = std::atan2(dy, dx);
                         const float angleDeg = angleRad * (180.0f / 3.14159265f);
                         obs_sceneitem_set_rot(item, angleDeg);
                     });

    setDirectionCheck_ = new QCheckBox("Set direction", this);
    setDirectionCheck_->setChecked(false);
    setDirectionCheck_->setToolTip(QStringLiteral("Check this, then click on the map to set the selected effect's facing direction. Arrow shows current facing (default: up)."));
    tb->addWidget(setDirectionCheck_);
    QObject::connect(setDirectionCheck_, &QCheckBox::toggled, this, [this, scenePosToCanvas](bool checked) {
        if (!display)
            return;
        if (!checked) {
            display->clearDirectionArrow();
            return;
        }
        // When checked, do not show an arrow until the user actually drags; keeps behavior simple.
        display->clearDirectionArrow();
    });
    tb->addSeparator();

    QObject::connect(display, &OBSDisplayWidget::sceneClicked, this,
                     [this, clickLabel, gridCheck, gridCellSpin, scenePosToCanvas](float x, float y, float nx, float ny,
                                                                                   bool inside) {
                         // Clear any previous direction arrow on new press so we never accumulate arrows.
                         // (Mouse release may not be delivered on macOS with native surface, so clear on press too.)
                         if (display)
                             display->clearDirectionArrow();

                         // Click-to-face: set selected FX rotation to face the click point (x,y in canvas space).
                         if (setDirectionCheck_ && setDirectionCheck_->isChecked() && inside && fxList) {
                             const int row = fxList->currentRow();
                             if (row >= 0 && row < (int)activeFx.size()) {
                                 const FxInstance &inst = activeFx[static_cast<size_t>(row)];
                                 OBSSourceAutoRelease mapSrc(obs_get_source_by_uuid(inst.mapSceneUuid.toUtf8().constData()));
                                 if (mapSrc.Get()) {
                                     obs_scene_t *scene = obs_scene_from_source(mapSrc.Get());
                                     obs_sceneitem_t *item = scene ? obs_scene_find_sceneitem_by_id(scene, inst.sceneItemId) : nullptr;
                                     if (item) {
                                         struct vec2 pos;
                                         obs_sceneitem_get_pos(item, &pos);
                                         float cx = 0.0f, cy = 0.0f;
                                         if (scenePosToCanvas(mapSrc.Get(), pos.x, pos.y, cx, cy)) {
                                             // Angle from entity to click: 0 = right, -90 = up (our default facing).
                                             const float dx = x - cx;
                                             const float dy = y - cy;
                                             const float angleRad = std::atan2(dy, dx);
                                             const float angleDeg = angleRad * (180.0f / 3.14159265f);
                                             obs_sceneitem_set_rot(item, angleDeg);
                                         }
                                         // Keep rotate mode on; arrow will update on next mouse move.
                                     }
                                 }
                             }
                             return;
                         }

                         lastClickX = x;
                         lastClickY = y;
                         lastClickInside = inside;

                         if (!inside) {
                             clickLabel->setText("Click: (outside map)");
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
                         clickLabel->setText(text);

                         // Click-to-select nearest FX instance on the map canvas.
                         const int idx = findNearestFxInstanceIndex(x, y);
                         if (idx >= 0 && idx < fxList->count())
                             fxList->setCurrentRow(idx);
                     });

    // Build FX side panel UI (compact).
    fxPanel->setMaximumWidth(280);
    fxLayout->addWidget(new QLabel("Effects", this));

    auto *fxCombo = new QComboBox(this);
    fxCombo->setMinimumWidth(140);
    fxLayout->addWidget(new QLabel("Template:", this));
    fxLayout->addWidget(fxCombo);

    auto *sequenceBox = new QCheckBox("Sequence [ms] with [123] prefixes", this);
    sequenceBox->setChecked(true);
    fxLayout->addWidget(sequenceBox);

    auto *labelRowWidget = new QWidget(this);
    auto *labelRowLayout = new QHBoxLayout(labelRowWidget);
    labelRowLayout->setContentsMargins(0, 0, 0, 0);
    labelRowLayout->setSpacing(4);

    auto *labelCheck = new QCheckBox(labelRowWidget);
    labelCheck->setChecked(false);
    labelCheck->setToolTip("Show label on map for this instance");
    labelRowLayout->addWidget(labelCheck);

    labelEdit = new QLineEdit(labelRowWidget);
    labelEdit->setPlaceholderText("Optional label");
    labelRowLayout->addWidget(labelEdit, 1);

    auto *labelColorBtn = new QToolButton(labelRowWidget);
    labelColorBtn->setIcon(iconFromColor(labelColor_));
    labelColorBtn->setToolTip("Label color");
    labelColorBtn->setFixedSize(kToolButtonSize, kToolButtonSize);
    labelColorBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    labelRowLayout->addWidget(labelColorBtn);

    fxLayout->addWidget(labelRowWidget);

    QObject::connect(labelColorBtn, &QAbstractButton::clicked, this, [this, labelColorBtn]() {
        QColor chosen = QColorDialog::getColor(labelColor_, this, "Choose label color");
        if (chosen.isValid()) {
            labelColor_ = chosen;
            labelColorBtn->setIcon(iconFromColor(chosen));
        }
    });

    auto *timersRow = new QWidget(this);
    auto *timersLayout = new QHBoxLayout(timersRow);
    timersLayout->setContentsMargins(0, 0, 0, 0);
    timersLayout->setSpacing(8);
    auto *fadeSpin = new QSpinBox(this);
    fadeSpin->setRange(0, 60000);
    fadeSpin->setSingleStep(100);
    fadeSpin->setValue(600);
    fadeSpin->setMaximumWidth(72);
    fadeSpin->setAlignment(Qt::AlignRight);
    auto *fadeLabel = new QLabel("Fade (ms)", this);
    fadeLabel->setAlignment(Qt::AlignCenter);
    auto *fadeCol = new QWidget(this);
    auto *fadeColLayout = new QVBoxLayout(fadeCol);
    fadeColLayout->setContentsMargins(0, 0, 0, 0);
    fadeColLayout->setSpacing(0);
    fadeColLayout->addWidget(fadeSpin);
    fadeColLayout->addWidget(fadeLabel);
    timersLayout->addWidget(fadeCol);
    auto *lifetimeSpin = new QSpinBox(this);
    lifetimeSpin->setRange(0, 3600);
    lifetimeSpin->setSingleStep(5);
    lifetimeSpin->setValue(0);
    lifetimeSpin->setMaximumWidth(72);
    lifetimeSpin->setAlignment(Qt::AlignRight);
    auto *lifetimeLabel = new QLabel("Lifetime (s)", this);
    lifetimeLabel->setAlignment(Qt::AlignCenter);
    auto *lifetimeCol = new QWidget(this);
    auto *lifetimeColLayout = new QVBoxLayout(lifetimeCol);
    lifetimeColLayout->setContentsMargins(0, 0, 0, 0);
    lifetimeColLayout->setSpacing(0);
    lifetimeColLayout->addWidget(lifetimeSpin);
    lifetimeColLayout->addWidget(lifetimeLabel);
    timersLayout->addWidget(lifetimeCol);
    timersLayout->addStretch(1);
    fxLayout->addWidget(timersRow);

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

    auto *spawnBtn = new QToolButton(this);
    spawnBtn->setIcon(iconTarget());
    spawnBtn->setToolTip("Spawn at last click");
    spawnBtn->setFixedSize(kToolButtonSize, kToolButtonSize);
    spawnBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    auto *clearAllBtn = new QToolButton(this);
    clearAllBtn->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    clearAllBtn->setToolTip("Clear all effects");
    clearAllBtn->setFixedSize(kToolButtonSize, kToolButtonSize);
    clearAllBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    auto *refreshBtn = new QToolButton(this);
    refreshBtn->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    refreshBtn->setToolTip("Refresh scenes");
    refreshBtn->setFixedSize(kToolButtonSize, kToolButtonSize);
    refreshBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    QIcon copyIcon = QIcon::fromTheme("edit-copy");
    if (copyIcon.isNull())
        copyIcon = style()->standardIcon(QStyle::SP_FileDialogContentsView);
    auto *dupAsTemplateBtn = new QToolButton(this);
    dupAsTemplateBtn->setIcon(copyIcon);
    dupAsTemplateBtn->setToolTip("Duplicate as template");
    dupAsTemplateBtn->setFixedSize(kToolButtonSize, kToolButtonSize);
    dupAsTemplateBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);

    auto *btnRow = new QWidget(this);
    auto *btnRowLayout = new QHBoxLayout(btnRow);
    btnRowLayout->setContentsMargins(0, 0, 0, 0);
    btnRowLayout->setSpacing(6);
    auto addBtnWithLabel = [&btnRowLayout](QToolButton *btn, const QString &labelText) {
        auto *col = new QWidget(btn->parentWidget());
        auto *lay = new QVBoxLayout(col);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(2);
        lay->addWidget(btn, 0, Qt::AlignHCenter);
        auto *lbl = new QLabel(labelText, col);
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setStyleSheet("font-size: 9px;");
        lay->addWidget(lbl);
        btnRowLayout->addWidget(col);
    };
    addBtnWithLabel(spawnBtn, "Spawn");
    addBtnWithLabel(clearAllBtn, "Clear all");
    addBtnWithLabel(refreshBtn, "Refresh");
    addBtnWithLabel(dupAsTemplateBtn, "Duplicate");
    fxLayout->addWidget(btnRow);

    fxList = new QListWidget(this);
    fxList->setSelectionMode(QAbstractItemView::SingleSelection);
    fxList->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked);
    fxList->setMaximumHeight(120);
    fxList->setContextMenuPolicy(Qt::CustomContextMenu);
    fxLayout->addWidget(new QLabel("Active:", this));
    fxLayout->addWidget(fxList, /*stretch*/ 1);

    auto *helpGroup = new QGroupBox("Show help", this);
    helpGroup->setCheckable(true);
    helpGroup->setChecked(false);
    auto *helpText = new QTextEdit(helpGroup);
    helpText->setReadOnly(true);
    helpText->setMaximumHeight(0);
    helpText->setHtml(
        "<p><b>Template scenes:</b> Name scenes <code>FX: Something</code> (any case) to use them as effect templates.</p>"
        "<p><b>Sequencing:</b> Prefix source names with <code>[250]</code> for staged reveal (ms delay).</p>"
        "<p><b>Fade-out:</b> Set fade ms &gt; 0 to enable hide transition when clearing.</p>"
        "<p><b>Lifetime:</b> 0 = infinite; otherwise auto-clear after N seconds.</p>"
        "<p><b>Per-template defaults:</b> Your fade, lifetime, sequence, and label settings are saved per template when you spawn.</p>");
    auto *helpLayout = new QVBoxLayout(helpGroup);
    helpLayout->addWidget(helpText);
    fxLayout->addWidget(helpGroup);

    QObject::connect(helpGroup, &QGroupBox::toggled, this, [helpText](bool checked) {
        helpText->setMaximumHeight(checked ? 120 : 0);
    });

    QObject::connect(fxList, &QListWidget::customContextMenuRequested, this,
                     [this, fxCombo, fadeSpin](const QPoint &pos) {
                         const int row = fxList->row(fxList->itemAt(pos));
                         if (row < 0 || row >= (int)activeFx.size())
                             return;
                         fxList->setCurrentRow(row);
                         QMenu menu(this);
                         auto *clearAct = menu.addAction("Clear selected");
                         auto *dupAct = menu.addAction("Duplicate as template");
                         QAction *chosen = menu.exec(fxList->mapToGlobal(pos));
                         if (chosen == clearAct) {
                             const int fadeMs = fadeSpin ? fadeSpin->value() : 600;
                             const FxInstance &inst = activeFx[static_cast<size_t>(row)];
                             fadeOutAndRemoveInstance(inst, fadeMs);
                             auto it = fxByMapSceneUuid_.find(inst.mapSceneUuid);
                             if (it != fxByMapSceneUuid_.end()) {
                                 std::vector<FxInstance> &vec = it.value();
                                 for (size_t i = 0; i < vec.size(); ++i) {
                                     if (vec[i].effectUuid == inst.effectUuid && vec[i].sceneItemId == inst.sceneItemId) {
                                         vec.erase(vec.begin() + (long)i);
                                         break;
                                     }
                                 }
                             }
                             activeFx.erase(activeFx.begin() + row);
                             delete fxList->takeItem(row);

                             if (display)
                                 display->clearDirectionArrow();
                             if (setDirectionCheck_)
                                 setDirectionCheck_->setChecked(false);
                         } else if (chosen == dupAct) {
                             const FxInstance &inst = activeFx[static_cast<size_t>(row)];
                             OBSSourceAutoRelease fxSrc(obs_get_source_by_uuid(inst.effectUuid.toUtf8().constData()));
                             if (!fxSrc.Get() || !obs_source_is_scene(fxSrc.Get()))
                                 return;
                             const char *origName = obs_source_get_name(fxSrc.Get());
                             QString baseName = QString::fromUtf8(origName);
                             if (!baseName.startsWith("FX", Qt::CaseInsensitive))
                                 baseName = "FX Spawn " + baseName;
                             const QString newName = QString("FX: %1 (copy)").arg(baseName);
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
                         }
                     });

    QObject::connect(fxList, &QListWidget::currentRowChanged, this, [this, scenePosToCanvas](int row) {
        if (!labelEdit)
            return;
        if (row < 0 || row >= (int)activeFx.size()) {
            labelEdit->clear();
            if (display)
                display->clearDirectionArrow();
            return;
        }
        labelEdit->setText(activeFx[static_cast<size_t>(row)].label);

        // If we're in Set direction mode and selection changed, clear any existing arrow;
        // arrow will reappear only while dragging.
        if (setDirectionCheck_ && setDirectionCheck_->isChecked() && display)
            display->clearDirectionArrow();
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
        auto itMap = fxByMapSceneUuid_.find(inst.mapSceneUuid);
        if (itMap != fxByMapSceneUuid_.end()) {
            for (FxInstance &stored : itMap.value()) {
                if (stored.effectUuid == inst.effectUuid && stored.sceneItemId == inst.sceneItemId) {
                    stored.label = newLabel;
                    break;
                }
            }
        }
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

    // Ensure grid and cursor image sources exist for output overlays.
    QTimer::singleShot(0, this, [this]() {
        ensureGridSource();
        ensureCursorSource();
    });

    QObject::connect(refreshBtn, &QAbstractButton::clicked, this, [refreshLists]() { refreshLists(); });

    QObject::connect(spawnBtn, &QAbstractButton::clicked, this,
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
            fxByMapSceneUuid_[mapUuid].push_back(inst);
            if (mapUuid == currentMapSceneUuid_) {
                activeFx.push_back(inst);
                const QString listText = inst.label.isEmpty() ? inst.templateName : inst.label;
                auto *item = new QListWidgetItem(listText);
                item->setFlags(item->flags() | Qt::ItemIsEditable);
                fxList->addItem(item);
                fxList->setCurrentRow(fxList->count() - 1);
            }

            const int lifetimeSec = lifetimeSpin->value();
            if (lifetimeSec > 0) {
                const int fadeMs = fadeSpin->value();
                QTimer::singleShot(lifetimeSec * 1000, qApp, [this, inst, fadeMs]() {
                    auto it = fxByMapSceneUuid_.find(inst.mapSceneUuid);
                    if (it == fxByMapSceneUuid_.end())
                        return;
                    std::vector<FxInstance> &vec = it.value();
                    for (size_t i = 0; i < vec.size(); ++i) {
                        if (vec[i].effectUuid == inst.effectUuid && vec[i].sceneItemId == inst.sceneItemId) {
                            fadeOutAndRemoveInstance(vec[i], fadeMs);
                            vec.erase(vec.begin() + (long)i);
                            if (inst.mapSceneUuid == currentMapSceneUuid_) {
                                for (size_t j = 0; j < activeFx.size(); ++j) {
                                    if (activeFx[j].effectUuid == inst.effectUuid &&
                                        activeFx[j].sceneItemId == inst.sceneItemId) {
                                        activeFx.erase(activeFx.begin() + (long)j);
                                        delete fxList->takeItem((int)j);
                                        break;
                                    }
                                }
                            }
                            break;
                        }
                    }
                });
            }
        }
    });

    QObject::connect(clearAllBtn, &QAbstractButton::clicked, this, [this, fadeSpin]() {
        const int fadeMs = fadeSpin->value();
        for (const FxInstance &inst : activeFx)
            fadeOutAndRemoveInstance(inst, fadeMs);

        if (!currentMapSceneUuid_.isEmpty())
            fxByMapSceneUuid_.remove(currentMapSceneUuid_);
        activeFx.clear();
        fxList->clear();

        if (display)
            display->clearDirectionArrow();
        if (setDirectionCheck_)
            setDirectionCheck_->setChecked(false);
    });

    QObject::connect(dupAsTemplateBtn, &QAbstractButton::clicked, this,
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
        // Keep per-map store in sync so label persists when switching maps.
        auto it = fxByMapSceneUuid_.find(inst.mapSceneUuid);
        if (it != fxByMapSceneUuid_.end()) {
            for (FxInstance &stored : it.value()) {
                if (stored.effectUuid == inst.effectUuid && stored.sceneItemId == inst.sceneItemId) {
                    stored.label = newLabel;
                    break;
                }
            }
        }

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

OBSSource RPGWindow::ensureCursorSource()
{
    if (cursorSource_.Get())
        return cursorSource_;

    obs_source_t *existing = obs_get_source_by_name(kCursorSourceName);
    if (existing) {
        cursorSource_ = OBSSource(existing);
        obs_source_release(existing);
        return cursorSource_;
    }

    const QString path = GetCursorPngPath();
    if (!GenerateCursorPng(path, 24, Qt::white))
        return cursorSource_;

    obs_data_t *settings = obs_data_create();
    obs_data_set_string(settings, "file", path.toUtf8().constData());
    obs_data_set_bool(settings, "unload", false);

    obs_source_t *src = obs_source_create("image_source", kCursorSourceName, settings, nullptr);
    obs_data_release(settings);
    if (src)
        cursorSource_ = OBSSource(src);
    return cursorSource_;
}

void RPGWindow::updateCursorSourceSettings(obs_source_t *cursorSource, int sizePx, const QColor &color)
{
    if (!cursorSource)
        return;

    const QString path = GetCursorPngPath();
    if (!GenerateCursorPng(path, sizePx, color))
        return;

    obs_data_t *settings = obs_source_get_settings(cursorSource);
    if (!settings)
        return;
    obs_data_set_string(settings, "file", path.toUtf8().constData());
    obs_source_update(cursorSource, settings);
    obs_data_release(settings);
}

void RPGWindow::syncCursorToScene(const QString &sceneName, bool showCursor, float cursorX, float cursorY)
{
    if (!cursorSource_.Get() || sceneName.isEmpty())
        return;

    OBSSourceAutoRelease sceneSrc(obs_get_source_by_name(sceneName.toUtf8().constData()));
    if (!sceneSrc.Get() || !obs_source_is_scene(sceneSrc.Get()))
        return;

    // cursorX, cursorY are in canvas space; scene item position is in scene space
    float posX = cursorX;
    float posY = cursorY;
    obs_video_info ovi = {};
    if (obs_get_video_info(&ovi) && ovi.base_width > 0 && ovi.base_height > 0) {
        const uint32_t sceneW = obs_source_get_width(sceneSrc.Get());
        const uint32_t sceneH = obs_source_get_height(sceneSrc.Get());
        if (sceneW > 0 && sceneH > 0) {
            posX = cursorX * float(sceneW) / float(ovi.base_width);
            posY = cursorY * float(sceneH) / float(ovi.base_height);
        }
    }

    queueAddCursorToScene(sceneName, cursorSource_.Get(), showCursor, posX, posY);
}

void RPGWindow::updateCursorPosition(const QString &sceneName, float x, float y)
{
    if (!cursorSource_.Get() || sceneName.isEmpty())
        return;

    lastCursorX = x;
    lastCursorY = y;

    OBSSourceAutoRelease sceneSrc(obs_get_source_by_name(sceneName.toUtf8().constData()));
    if (!sceneSrc.Get() || !obs_source_is_scene(sceneSrc.Get()))
        return;

    // x,y are in canvas (base) space; cursor scene item position is in scene space
    obs_video_info ovi = {};
    if (!obs_get_video_info(&ovi) || ovi.base_width == 0 || ovi.base_height == 0)
        return;
    const uint32_t sceneW = obs_source_get_width(sceneSrc.Get());
    const uint32_t sceneH = obs_source_get_height(sceneSrc.Get());
    if (sceneW == 0 || sceneH == 0)
        return;
    const float sx = x * float(sceneW) / float(ovi.base_width);
    const float sy = y * float(sceneH) / float(ovi.base_height);

    queueCursorPositionUpdate(sceneName, cursorSource_.Get(), sx, sy);
}

RPGWindow::~RPGWindow()
{
    /* Do not add/remove grid from scenes (stays in plugin only). */
}
