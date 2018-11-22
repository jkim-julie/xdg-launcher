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
#ifndef RUNXDG_HPP
#define RUNXDG_HPP

#include <string>
#include <vector>
#include <map>
#include <algorithm>

#include <gio/gio.h>

#include <libappbridge.h>

#define AGL_FATAL(fmt, ...) fatal("ERROR: " fmt "\n", ##__VA_ARGS__)
#define AGL_WARN(fmt, ...) warn("WARNING: " fmt "\n", ##__VA_ARGS__)
#define AGL_DEBUG(fmt, ...) debug("DEBUG: " fmt "\n", ##__VA_ARGS__)
#define AGL_TRACE(file,line) debug("%s:%d\n", file,line);

void fatal (const char* format, ...);
void warn (const char* format, ...);
void debug (const char* format, ...);

class Launcher
{
  public:
    virtual void register_surfpid(pid_t surf_pid) = 0;
    virtual void unregister_surfpid(pid_t surf_pid) = 0;
    virtual pid_t find_surfpid_by_rid(pid_t app_pid) = 0;

    virtual int launch(std::string& name) = 0;
    virtual void loop(volatile sig_atomic_t& e_flag) = 0;

    int m_rid = 0;
};

class POSIXLauncher : public Launcher
{
  private:
    std::vector<pid_t> m_pid_v;

  public:
    std::vector<std::string> m_args_v;

    void register_surfpid(pid_t surf_pid);
    void unregister_surfpid(pid_t surf_pid);
    pid_t find_surfpid_by_rid(pid_t rid);

    int launch(std::string& name);
    void loop(volatile sig_atomic_t& e_flag);
};

class AFMLauncher : public Launcher
{
  private:
    std::map<int, int> m_pgids;  // pair of <afm:rid, ivi:pid>

  public:
    void register_surfpid(pid_t surf_pid);
    void unregister_surfpid(pid_t surf_pid);
    pid_t find_surfpid_by_rid(pid_t app_pid);
};

class AFMDBusLauncher : public AFMLauncher
{
  public:
    int launch(std::string& name);
    void loop(volatile sig_atomic_t& e_flag) {
      while (!(e_flag)) { sleep(60*60*24); } }

  private:
    int get_dbus_message_bus(GBusType bus_type, GDBusConnection* &conn);

    const char* DBUS_SERVICE   = "org.AGL.afm.user";
    const char* DBUS_PATH      = "/org/AGL/afm/user";
    const char* DBUS_INTERFACE = "org.AGL.afm.user";
};

class AFMWebSocketLauncher : public AFMLauncher
{
  // not implemented yet
  public:
    int launch(std::string& name) { return 0; }
    void loop(volatile sig_atomic_t& e_flag) {
      while (!(e_flag)) { sleep(60*60*24); } }
};

class RunXDG : public AppBridgeDelegate
{
  public:
    RunXDG(int port, const char* token, const char* id);

    void start(void);

    // AppBridgeDelegate:
    void OnActive() override;
    void OnInactive() override;
    void OnVisible() override;
    void OnInvisible() override;
    void OnSyncDraw() override;
    void OnFlushDraw() override;
    void OnTabShortcut() override;
    void OnScreenMessage(const char* message) override;
    void OnSurfaceCreated(int id, pid_t pid) override;
    void OnSurfaceDestroyed(int id, pid_t pid) override;
    void OnRequestedSurfaceID(int id, pid_t* surface_pid_output) override;

  private:
    std::string m_role;
    std::string m_path;

    std::string m_id;

    int m_port;
    std::string m_token;

    Launcher *m_launcher;
    std::unique_ptr<AppBridge> m_app_bridge;

    std::map<int, int> m_surfaces;  // pair of <afm:rid, ivi:id>

    int parse_config(const char *file);
};

#endif  // RUNXDG_HPP
