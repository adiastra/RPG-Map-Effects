#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QAction>
#include <QObject>
#include <QWidget>

#include "rpg_window.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("rpg-map-effects", "en-US")

static RPGWindow *g_window = nullptr;
static QAction *g_action = nullptr;

static void open_window()
{
    if (!g_window) {
        g_window = new RPGWindow();
        g_window->setAttribute(Qt::WA_DeleteOnClose, false);
    }
    g_window->show();
    g_window->raise();
    g_window->activateWindow();
}

static void on_frontend_event(enum obs_frontend_event event, void *)
{
    if (event != OBS_FRONTEND_EVENT_FINISHED_LOADING)
        return;

    if (!g_action) {
        g_action = (QAction *)obs_frontend_add_tools_menu_qaction("RPG Map Effects");
        QObject::connect(g_action, &QAction::triggered, []() { open_window(); });
        blog(LOG_INFO, "[rpg-map-effects] Tools menu action added");
    }
}

bool obs_module_load(void)
{
    obs_frontend_add_event_callback(on_frontend_event, nullptr);
    blog(LOG_INFO, "[rpg-map-effects] loaded");
    return true;
}

void obs_module_unload(void)
{
    obs_frontend_remove_event_callback(on_frontend_event, nullptr);

    if (g_window) {
        g_window->close();
        delete g_window;
        g_window = nullptr;
    }
    g_action = nullptr;

    blog(LOG_INFO, "[rpg-map-effects] unloaded");
}
