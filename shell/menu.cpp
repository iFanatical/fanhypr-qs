#include "launcher.h"
#include "wallpaper.h"

#include <QProcess>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUrl>
#include <csignal>

// Loaded when the Websites page is built, so reopening picks up edits.
static bool websiteItems(QVector<LauncherItem> &items)
{
    const QString folder = QStandardPaths::writableLocation(
        QStandardPaths::GenericConfigLocation) + QStringLiteral("/fanhypr-qs");
    QFile file(folder + QStringLiteral("/websites.json"));
    if (!file.exists())
        return false;
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("fanhypr-qs: cannot read websites.json; using defaults");
        return false;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) {
        qWarning("fanhypr-qs: websites.json must be a JSON array; using defaults");
        return false;
    }
    QVector<LauncherItem> result;
    for (const QJsonValue &value : document.array()) {
        const QJsonObject obj = value.toObject();
        const QString name = obj.value(QStringLiteral("name")).toString().trimmed();
        const QString address = obj.value(QStringLiteral("url")).toString().trimmed();
        const QUrl url(address);
        if (name.isEmpty() || !url.isValid() || url.host().isEmpty()
                || (url.scheme() != QLatin1String("https")
                    && url.scheme() != QLatin1String("http"))) {
            qWarning("fanhypr-qs: invalid website entry; using defaults");
            return false;
        }
        LauncherItem item;
        item.name = name;
        item.sub = obj.value(QStringLiteral("description")).toString(address);
        item.icon = obj.value(QStringLiteral("icon")).toString(QStringLiteral("web-browser"));
        if (item.icon.startsWith(QLatin1String("~/")))
            item.icon = QDir::homePath() + item.icon.mid(1);
        else if (item.icon.contains(QLatin1Char('/')) && QDir::isRelativePath(item.icon))
            item.icon = QDir(folder).filePath(item.icon);
        item.menuAction = QStringLiteral("url:") + address;
        item.cmd = item.menuAction;
        result.push_back(item);
    }
    items += result;
    return true;
}

QString AppLauncher::menuTitle() const
{
    if (menuPage == QLatin1String("main"))
        return QStringLiteral("Fanos");
    if (menuPage.startsWith(QLatin1String("confirm:")))
        return QStringLiteral("Confirm action");
    QString title = menuPage;
    title[0] = title[0].toUpper();
    return title;
}

QVector<LauncherItem> AppLauncher::menuItems() const
{
    QVector<LauncherItem> items;
    auto add = [&items](const char *name, const char *detail,
                        const char *action, const char *icon = "preferences-system") {
        LauncherItem item;
        item.name = QString::fromUtf8(name);
        item.sub = QString::fromUtf8(detail);
        item.icon = QString::fromUtf8(icon);
        item.menuAction = QString::fromUtf8(action);
        item.cmd = item.menuAction; // stable identity while filtering
        items.push_back(item);
    };
    if (menuPage == QLatin1String("main")) {
        add("Apps", "Find and launch applications", "apps", "applications-all");
        add("Websites", "Your Fanos web apps", "page:websites", "web-browser");
        add("Configuration", "Wallpaper, bindings, monitors and theme", "page:configuration");
        add("Troubleshooting", "Fanos service recovery tools", "page:troubleshooting", "tools-check-spelling");
        add("Learn", "Keybindings and documentation", "page:learn", "help-browser");
        add("Update", "Review and run the system package update", "page:confirm:update", "system-software-update");
        add("About", "System and hardware information", "about", "help-about");
        add("System", "Lock, suspend, restart and shut down", "page:system", "system-shutdown");
    } else if (menuPage == QLatin1String("websites")) {
        if (!websiteItems(items)) {
        add("YouTube", "youtube.com", "url:https://youtube.com", "web-browser");
        add("Twitch", "Followed channels", "url:https://www.twitch.tv/directory/following", "web-browser");
        add("Jellyfin", "Home media server", "url:http://jellyfin.bush.local:8096/web/#/home.html", "web-browser");
        add("X", "Home timeline", "url:https://x.com/home", "web-browser");
        add("Plex", "Web player", "url:https://app.plex.tv/desktop/#!/", "web-browser");
        add("LinkedIn", "Feed", "url:https://www.linkedin.com/feed/", "web-browser");
        add("Gmail", "Inbox", "url:https://mail.google.com/mail/u/0/#inbox", "web-browser");
        add("Reminders", "Google Tasks", "url:https://tasks.google.com/tasks/", "web-browser");
        add("Drive", "Google Drive", "url:https://drive.google.com/drive/", "web-browser");
        }
    } else if (menuPage == QLatin1String("configuration")) {
        add("Wallpaper", "Open the shell wallpaper picker", "wallpaper", "preferences-desktop-wallpaper");
        add("Bindings", "Default keybindings (or keybindings.lua)", "edit:bindings");
        add("Monitors", "hypr/settings/monitors.lua", "edit:monitors");
        add("Environment", "hypr/config/environment.lua", "edit:environment");
        add("Settings", "hypr/settings/appearance.lua", "edit:settings");
        add("Shell Style", "shell/theme.h (rebuild required)", "edit:style");
    } else if (menuPage == QLatin1String("learn")) {
        add("Keybindings", "Read your active bindings", "edit:bindings", "input-keyboard");
        add("Grok", "grok.com", "url:https://grok.com/?ref=aiartweekly", "help-browser");
        add("Hyprland", "Hyprland wiki", "url:https://wiki.hypr.land/", "help-browser");
        add("Artix", "Artix Linux wiki", "url:https://wiki.artixlinux.org/", "help-browser");
        add("Neovim", "LazyVim keymaps", "url:https://www.lazyvim.org/keymaps", "help-browser");
        add("Bash", "Bash cheat sheet", "url:https://devhints.io/bash", "help-browser");
    } else if (menuPage == QLatin1String("troubleshooting")) {
        add("dinit User Spawn", "Requires dinit and elevated privileges", "page:confirm:dinit");
        add("GPU Screen Recording", "Restart dinit recording services", "page:confirm:gpu");
        add("Audio", "Restart audio services (dinit or systemd user)", "page:confirm:audio");
    } else if (menuPage == QLatin1String("system")) {
        add("Lock", "Lock the session", "power:lock", "system-lock-screen");
        add("Screensaver", "Lock the session (Fanos had no handler)", "power:lock", "preferences-desktop-screensaver");
        add("Suspend", "Put the machine to sleep", "power:suspend", "system-suspend");
        add("Exit WM", "End the Hyprland session", "page:confirm:logout", "system-log-out");
        add("Restart", "Reboot the machine", "page:confirm:reboot", "system-reboot");
        add("Shutdown", "Power off the machine", "page:confirm:poweroff", "system-shutdown");
    } else if (menuPage.startsWith(QLatin1String("confirm:"))) {
        add("Cancel", "Return to the main menu", "page:main", "dialog-cancel");
        const QString action = menuPage.mid(8);
        const QByteArray label = (QStringLiteral("Confirm ") + action).toUtf8();
        const QByteArray command = (QStringLiteral("execute:") + action).toUtf8();
        add(label.constData(), "Run this action", command.constData(), "dialog-warning");
    }
    if (menuPage != QLatin1String("main")
            && !menuPage.startsWith(QLatin1String("confirm:")))
        add("Back", "Return to Fanos (Escape)", "page:main", "go-previous");
    return items;
}

void AppLauncher::openMenuPage(const QString &page)
{
    mode = QStringLiteral("menu");
    menuPage = page;
    searchText.clear();
    entries.clear();
    selected = 0;
    refilter();
    m_win->openOn(pickScreen());
}

void AppLauncher::activateMenuItem(const QString &action)
{
    if (action.startsWith(QLatin1String("page:"))) {
        openMenuPage(action.mid(5));
        return;
    }
    if (action == QLatin1String("apps")) {
        openMode(QStringLiteral("apps"));
        return;
    }
    hideLauncher();
    if (action == QLatin1String("wallpaper")) {
        WallpaperPicker::instance()->openPicker();
        return;
    }
    QProcess process;
    process.setProgram(QStringLiteral("fanhypr-qs-menu-action"));
    process.setArguments({action});
    process.setChildProcessModifier([]() {
        signal(SIGCHLD, SIG_DFL);
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
    });
    if (!process.startDetached())
        qWarning("fanhypr-qs: could not launch menu action; install fanhypr-qs-menu-action");
}
