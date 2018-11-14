/*
 * Copyright (c) 2017 Panasonic Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <cstdio>

#include "cpptoml/cpptoml.h"

#include "runxdg.hpp"

#define RUNXDG_CONFIG "runxdg.toml"
#define AREA_NORMAL_FULL "normal.full"

void fatal(const char* format, ...)
{
  va_list va_args;
  va_start(va_args, format);
  vfprintf(stderr, format, va_args);
  va_end(va_args);

  exit(EXIT_FAILURE);
}

void warn(const char* format, ...)
{
  va_list va_args;
  va_start(va_args, format);
  vfprintf(stderr, format, va_args);
  va_end(va_args);
}

void debug(const char* format, ...)
{
  va_list va_args;
  va_start(va_args, format);
  vfprintf(stderr, format, va_args);
  va_end(va_args);
}

void RunXDG::notify_ivi_control_cb (ilmObjectType object, t_ilm_uint id,
                                    t_ilm_bool created)
{
  if (object == ILM_SURFACE) {
    struct ilmSurfaceProperties surf_props;

    ilm_getPropertiesOfSurface(id, &surf_props);
    pid_t surf_pid = surf_props.creatorPid;

    if (!created) {
      AGL_DEBUG("ivi surface (id=%d, pid=%d) destroyed.", id, surf_pid);
      m_launcher->unregister_surfpid(surf_pid);
      m_surfaces.erase(surf_pid);
      return;
    }

    AGL_DEBUG("ivi surface (id=%d, pid=%d) is created.", id, surf_pid);

    m_launcher->register_surfpid(surf_pid);
    if (m_launcher->m_rid &&
        surf_pid == m_launcher->find_surfpid_by_rid(m_launcher->m_rid)) {
      setup_surface(id);
    }
    m_surfaces[surf_pid] = id;
  } else if (object == ILM_LAYER) {
    if (created)
      AGL_DEBUG("ivi layer: %d created.", id);
    else
      AGL_DEBUG("ivi layer: %d destroyed.", id);
  }
}

void RunXDG::notify_ivi_control_cb_static (ilmObjectType object, t_ilm_uint id,
                                           t_ilm_bool created, void *user_data)
{
  RunXDG *runxdg = static_cast<RunXDG*>(user_data);
  runxdg->notify_ivi_control_cb(object, id, created);
}

int POSIXLauncher::launch (std::string& name)
{
  pid_t pid;

  pid = fork();
  if (pid < 0) {
    AGL_DEBUG("cannot fork()");
    return -1;
  }

  if (pid == 0) {
    // child
    const char **argv = new const char * [m_args_v.size() + 1];
    for (unsigned int i = 0; i < m_args_v.size(); ++i) {
      argv[i] = m_args_v[i].c_str();
    }
    argv[m_args_v.size()] = NULL;

    execv(argv[0], (char **)argv);

    AGL_FATAL("fail to execve(%s)", argv[0]);
  }
  // parent

  return pid;
}

void POSIXLauncher::loop (volatile sig_atomic_t& e_flag)
{
  int status;
  pid_t ret;

  while (!e_flag) {
    ret = waitpid(m_rid, &status, 0);
    if (ret < 0) {
      if (errno == EINTR) {
        AGL_DEBUG("catch EINTR while waitpid()");
        continue;
      }
      break;
    }
  }

  if (ret > 0) {
    if (WIFEXITED(status)) {
      AGL_DEBUG("%s terminated, return %d", m_args_v[0].c_str(),
                WEXITSTATUS(status));
    }
    if (WIFSIGNALED(status)) {
      AGL_DEBUG("%s terminated by signal %d", m_args_v[0].c_str(),
                WTERMSIG(status));
    }
  }

  if (e_flag) {
    /* parent killed by someone, so need to kill children */
    AGL_DEBUG("killpg(0, SIGTERM)");
    killpg(0, SIGTERM);
  }
}

int AFMDBusLauncher::get_dbus_message_bus (GBusType bus_type,
                                           GDBusConnection * &conn)
{
  GError* err = NULL;

  conn = g_bus_get_sync(bus_type, NULL, &err);
  if (err) {
    AGL_WARN("Failed to get session bus: %s", err->message);
    g_clear_error (&err);
    return -1;
  }

  return 0;
}

int AFMDBusLauncher::launch (std::string &name)
{
  GDBusMessage*       msg;
  GDBusMessage*       re;
  GDBusConnection*    conn;
  GError*             err = NULL;
  GVariant*           body;
  char*               val;
  const char*         xdg_app = name.c_str();

  if (get_dbus_message_bus(G_BUS_TYPE_SESSION, conn)) {
    return -1;
  }

  msg = g_dbus_message_new_method_call (
      DBUS_SERVICE,
      DBUS_PATH,
      DBUS_INTERFACE,
      "start");

  if (msg == NULL) {
    AGL_WARN("Failed to allocate the dbus message");
    g_object_unref(conn);
    return -1;
  }

  g_dbus_message_set_body(msg, g_variant_new("(s)", xdg_app));

  re = g_dbus_connection_send_message_with_reply_sync (
      conn, msg, G_DBUS_SEND_MESSAGE_FLAGS_NONE, -1, NULL, NULL, &err);

  if (err != NULL) {
    AGL_WARN("unable to send message: %s", err->message);
    g_clear_error(&err);
    g_object_unref(conn);
    g_object_unref(msg);
    return -1;
  }

  g_dbus_connection_flush_sync(conn, NULL, &err);
  if (err != NULL) {
    AGL_WARN("unable to flush message queue: %s", err->message);
    g_object_unref(conn);
    g_object_unref(msg);
    g_object_unref(re);
    return -1;
  }

  body = g_dbus_message_get_body(re);
  g_variant_get(body, "(&s)", &val);

  AGL_DEBUG("dbus message get (%s)", val);

  pid_t rid = std::stol(std::string(val));
  AGL_DEBUG("RID = %d", rid);

  g_object_unref(conn);
  g_object_unref(msg);
  g_object_unref(re);

  return rid;
}

volatile sig_atomic_t e_flag = 0;

static void sigterm_handler (int signum)
{
  e_flag = 1;
}

static void init_signal (void)
{
  struct sigaction act, info;

  /* Setup signal for SIGTERM */
  if (!sigaction(SIGTERM, NULL, &info)) {
    if (info.sa_handler == SIG_IGN) {
      AGL_DEBUG("SIGTERM being ignored.");
    } else if (info.sa_handler == SIG_DFL) {
      AGL_DEBUG("SIGTERM being defaulted.");
    }
  }

  act.sa_handler = &sigterm_handler;
  if (sigemptyset(&act.sa_mask) != 0) {
    AGL_FATAL("Cannot initialize sigaction");
  }
  act.sa_flags = 0;

  if (sigaction(SIGTERM, &act, &info) != 0) {
    AGL_FATAL("Cannot register signal handler for SIGTERM");
  }
}

int RunXDG::init_wm (void)
{
  m_wm = new LibWindowmanager();
  if (m_wm->init(m_port, m_token.c_str())) {
    AGL_DEBUG("cannot initialize windowmanager");
    return -1;
  }

  std::function< void(json_object*) > h_active = [](json_object* object) {
    AGL_DEBUG("Got Event_Active");
  };

  std::function< void(json_object*) > h_inactive = [](json_object* object) {
    AGL_DEBUG("Got Event_Inactive");
  };

  std::function< void(json_object*) > h_visible = [](json_object* object) {
    AGL_DEBUG("Got Event_Visible");
  };

  std::function< void(json_object*) > h_invisible = [](json_object* object) {
    AGL_DEBUG("Got Event_Invisible");
  };

  std::function< void(json_object*) > h_syncdraw =
      [this](json_object* object) {
    AGL_DEBUG("Got Event_SyncDraw");
    this->m_wm->endDraw(this->m_role.c_str());
  };

  std::function< void(json_object*) > h_flushdraw= [](json_object* object) {
    AGL_DEBUG("Got Event_FlushDraw");
  };

  m_wm->set_event_handler(LibWindowmanager::Event_Active, h_active);
  m_wm->set_event_handler(LibWindowmanager::Event_Inactive, h_inactive);
  m_wm->set_event_handler(LibWindowmanager::Event_Visible, h_visible);
  m_wm->set_event_handler(LibWindowmanager::Event_Invisible, h_invisible);
  m_wm->set_event_handler(LibWindowmanager::Event_SyncDraw, h_syncdraw);
  m_wm->set_event_handler(LibWindowmanager::Event_FlushDraw, h_flushdraw);

  return 0;
}

int RunXDG::init_hs (void)
{
  m_hs = new LibHomeScreen();
  if (m_hs->init(m_port, m_token.c_str())) {
    AGL_DEBUG("cannot initialize homescreen");
    return -1;
  }

  std::function< void(json_object*) > handler = [this] (json_object* object) {
    AGL_DEBUG("Activesurface %s ", this->m_role.c_str());
    this->m_wm->activateWindow(this->m_role.c_str(), AREA_NORMAL_FULL);
  };
  m_hs->set_event_handler(LibHomeScreen::Event_TapShortcut, handler);

  std::function< void(json_object*) > h_default= [](json_object* object) {
    const char *j_str = json_object_to_json_string(object);
    AGL_DEBUG("Got event [%s]", j_str);
  };
  m_hs->set_event_handler(LibHomeScreen::Event_OnScreenMessage, h_default);

  return 0;
}

int RunXDG::parse_config (const char *path_to_config)
{
  auto config = cpptoml::parse_file(path_to_config);

  if (config == nullptr) {
    AGL_DEBUG("cannot parse %s", path_to_config);
    return -1;
  }

  AGL_DEBUG("[%s] parsed", path_to_config);

  auto app = config->get_table("application");
  if (app == nullptr) {
    AGL_DEBUG("cannto find [application]");
    return -1;
  }

  m_role = *(app->get_as<std::string>("role"));
  m_path = *(app->get_as<std::string>("path"));
  if (m_role.empty() || m_path.empty()) {
    AGL_FATAL("No name or path defined in config");
  }

  std::string method = *(app->get_as<std::string>("method"));
  if (method.empty()) {
    method = std::string("POSIX");
  }

  POSIXLauncher *pl;

  /* Setup API of launcher */
  if (method == "POSIX") {
    pl = new POSIXLauncher();
    m_launcher = pl;
  } else if (method == "AFM_DBUS") {
    m_launcher = new AFMDBusLauncher();
    return 0;
  } else if (method == "AFM_WEBSOCKET") {
    m_launcher = new AFMWebSocketLauncher();
    return 0;
  } else {
    AGL_FATAL("Unknown type of launcher");
  }

  // setup argv[0]
  pl->m_args_v.push_back(m_path);

  // setup argv[1..n]
  auto params = app->get_array_of<std::string>("params");
  for (const auto& param : *params)
  {
    // replace special string "@port@" and "@token@"
    size_t found = param.find("@port@");
    if (found != std::string::npos) {
      std::string sub1 = param.substr(0, found);
      std::string sub2 = param.substr(found + 6, param.size() - found);
      std::string str = sub1 + std::to_string(m_port) + sub2;
      pl->m_args_v.push_back(str);
      AGL_DEBUG("params[%s] (match @port@)", str.c_str());
      continue;
    }

    found = param.find("@token@");
    if (found != std::string::npos) {
      std::string sub1 = param.substr(0, found);
      std::string sub2 = param.substr(found + 7, param.size() - found);
      std::string str = sub1 + m_token + sub2;
      pl->m_args_v.push_back(str);
      AGL_DEBUG("params[%s] (match @token@)", str.c_str());
      continue;
    }

    pl->m_args_v.push_back(param);

    AGL_DEBUG("params[%s]", param.c_str());
  }

  return 0;
}

RunXDG::RunXDG (int port, const char* token, const char* id)
{
  m_id = std::string(id);
  m_port = port;
  m_token = std::string(token);


  auto path = std::string(getenv("AFM_APP_INSTALL_DIR"));
  path = path + "/" + RUNXDG_CONFIG;

  // Parse config file of runxdg
  if (parse_config(path.c_str())) {
    AGL_FATAL("Error in config");
  }

  AGL_DEBUG("id=[%s], name=[%s], path=[%s], port=%lu, token=[%s]",
            m_id.c_str(), m_role.c_str(), m_path.c_str(),
            m_port, m_token.c_str());

  // Setup HomeScreen/WindowManager API
  if (init_wm())
    AGL_FATAL("cannot setup wm API");

  if (init_hs())
    AGL_FATAL("cannot setup hs API");

  // Setup ilmController API
  m_ic = new ILMControl(notify_ivi_control_cb_static, this);

  AGL_DEBUG("RunXDG created.");
}

void RunXDG::setup_surface (int id)
{
  std::string sid = std::to_string(id);

  // This surface is mine, register pair app_name and ivi id.
  AGL_DEBUG("requestSurfaceXDG(%s,%d)", m_role.c_str(), id);
  m_wm->requestSurfaceXDG(this->m_role.c_str(), id);

  if (m_pending_create) {
    // Recovering 1st time tap_shortcut is dropped because
    // the application has not been run yet (1st time launch)
    m_pending_create = false;
    m_wm->activateWindow(this->m_role.c_str(), AREA_NORMAL_FULL);
  }
}

void POSIXLauncher::register_surfpid (pid_t surf_pid)
{
  if (surf_pid == m_rid) {
    if (!std::count(m_pid_v.begin(), m_pid_v.end(), surf_pid)) {
      AGL_DEBUG("surface creator(pid=%d) registered", surf_pid);
      m_pid_v.push_back(surf_pid);
      AGL_DEBUG("m_pid_v.count(%d) = %d", surf_pid,
                std::count(m_pid_v.begin(), m_pid_v.end(), surf_pid));
    }
  }
}

void POSIXLauncher::unregister_surfpid (pid_t surf_pid)
{
  auto itr = m_pid_v.begin();
  while (itr != m_pid_v.end()) {
    if (*itr == surf_pid) {
      m_pid_v.erase(itr++);
    } else {
      ++itr;
    }
  }
}

pid_t POSIXLauncher::find_surfpid_by_rid (pid_t rid)
{
  AGL_DEBUG("find surfpid by rid(%d)", rid);
  if (std::count(m_pid_v.begin(), m_pid_v.end(), rid)) {
    AGL_DEBUG("found return(%d)", rid);
    return rid;
  }

  return -1;
}

void AFMLauncher::register_surfpid (pid_t surf_pid)
{
  pid_t pgid = 0;

  pgid = getpgid(surf_pid);

  if (pgid < 0) {
    AGL_DEBUG("fail to get process group id");
    return;
  }

  AGL_DEBUG("Surface creator is pid=%d, pgid=%d", surf_pid, pgid);

  if (!m_pgids.count(pgid)) {
    m_pgids[pgid] = surf_pid;
  }
}

void AFMLauncher::unregister_surfpid (pid_t surf_pid)
{
  auto itr = m_pgids.begin();
  while (itr != m_pgids.end()) {
    if (itr->second == surf_pid) {
      m_pgids.erase(itr++);
    } else {
      ++itr;
    }
  }
}

pid_t AFMLauncher::find_surfpid_by_rid (pid_t rid)
{
  auto itr = m_pgids.find(rid);
  if (itr != m_pgids.end())
    return itr->second;

  return -1;
}

void RunXDG::start (void)
{
  // Initialize SIGTERM handler
  init_signal();

  /* Launch XDG application */
  m_launcher->m_rid = m_launcher->launch(m_id);
  if (m_launcher->m_rid < 0) {
    AGL_FATAL("cannot launch XDG app (%s)", m_id);
  }

  // take care 1st time launch
  AGL_DEBUG("waiting for notification: surafce created");
  m_pending_create = true;

  // in case, target app has already run
  if (m_launcher->m_rid) {
    pid_t surf_pid = m_launcher->find_surfpid_by_rid(m_launcher->m_rid);
    if (surf_pid > 0) {
      AGL_DEBUG("match: surf:pid=%d, afm:rid=%d", surf_pid,
                m_launcher->m_rid);
      auto itr = m_surfaces.find(surf_pid);
      if (itr != m_surfaces.end()) {
        int id = itr->second;
        AGL_DEBUG("surface %d for <%s> already exists", id,
                  m_role.c_str());
        setup_surface(id);
      }
    }
  }
  m_launcher->loop(e_flag);
}

int main (int argc, const char* argv[])
{
  // Set debug flags
  // setenv("USE_HMI_DEBUG", "5", 1);
  // setenv("WAYLAND_DEBUG", "1", 1);

  // Parse args
  int port;
  const char *token;

  if (argc < 3) {
    AGL_FATAL("Missing port and token");
  }

  // Get app id
  const char *afm_id = getenv("AFM_ID");
  if (afm_id == NULL || !afm_id[0]) {
    afm_id = argv[0];
  }

  try {
    port = std::stol(argv[1]);
    token = argv[2];
  } catch (const std::invalid_argument& e) {
    AGL_FATAL("Invalid argument");
  } catch (const std::out_of_range& e) {
    AGL_FATAL("Out of range");
  }

  RunXDG runxdg(port, token, afm_id);

  runxdg.start();

  return 0;
}
