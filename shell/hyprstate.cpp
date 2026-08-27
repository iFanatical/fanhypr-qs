#include "hyprstate.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>
#include <limits>

#include "theme.h"

namespace {

QString normalizedAddress(QString address)
{
    address = address.trimmed().toLower();
    if (!address.isEmpty() && !address.startsWith(QLatin1String("0x")))
        address.prepend(QStringLiteral("0x"));
    return address;
}

bool sameMonitors(const QVector<HyprState::Monitor> &a,
                  const QVector<HyprState::Monitor> &b)
{
    if (a.size() != b.size())
        return false;
    for (int i = 0; i < a.size(); ++i) {
        const auto &x = a[i];
        const auto &y = b[i];
        if (x.id != y.id || x.name != y.name
                || x.description != y.description || x.x != y.x || x.y != y.y
                || x.w != y.w || x.h != y.h || x.focused != y.focused
                || x.activeWorkspace != y.activeWorkspace
                || x.title != y.title || x.workspaces != y.workspaces)
            return false;
    }
    return true;
}

bool sameWorkspaces(const QHash<int, HyprState::Workspace> &a,
                    const QHash<int, HyprState::Workspace> &b)
{
    if (a.size() != b.size())
        return false;
    for (auto it = a.constBegin(); it != a.constEnd(); ++it) {
        const auto other = b.constFind(it.key());
        if (other == b.constEnd() || it->id != other->id
                || it->name != other->name || it->monitor != other->monitor
                || it->windows != other->windows)
            return false;
    }
    return true;
}

} /* namespace */

HyprState::HyprState(QObject *parent) : QObject(parent)
{
    /* Zero-delay single shot: collapses the burst of events one user action
     * produces into a single re-query, without adding latency. */
    m_coalesce.setSingleShot(true);
    m_coalesce.setInterval(0);
    connect(&m_coalesce, &QTimer::timeout, this, &HyprState::refresh);

    /* Animated application titles can generate legacy + v2 title/focus
     * events around ten times per second. Keep their trailing edge separate
     * from structural compositor changes, which must remain immediate. */
    m_titleDebounce.setSingleShot(true);
    m_titleDebounce.setInterval(300);
    connect(&m_titleDebounce, &QTimer::timeout, this,
            &HyprState::applyTitleUpdates);

    connect(&m_events, &HyprEventStream::hyprEvent, this,
            [this](const QString &name, const QString &data) {
                /* configreloaded can move workspaces between monitors;
                 * everything else only changes state we re-read anyway. */
                if (name == QLatin1String("configreloaded"))
                    refreshConfig();
                /* The submap event is self-contained, so take it at its word
                 * instead of paying for another round trip. */
                if (name == QLatin1String("submap")) {
                    const QString next = normalizeSubmap(data);
                    if (submap != next) {
                        submap = next;
                        emit changed();
                    }
                    return;
                }
                /* Hyprland 0.56 emits both generations. The v2 title payload
                 * contains the stable window address, so legacy duplicates
                 * carry no useful information here. */
                if (name == QLatin1String("windowtitle")
                        || name == QLatin1String("activewindow"))
                    return;
                if (name == QLatin1String("windowtitlev2")) {
                    queueTitleUpdate(data);
                    return;
                }
                if (name == QLatin1String("activewindowv2")) {
                    const QString address = normalizedAddress(data);
                    if (address == m_activeClient)
                        return; /* duplicate emitted for a title-only change */
                    m_titleDebounce.stop();
                    m_pendingTitles.clear();
                    scheduleRefresh(); /* real focus change: no debounce */
                    return;
                }
                m_titleDebounce.stop();
                m_pendingTitles.clear();
                scheduleRefresh();
            });
    connect(&m_events, &HyprEventStream::connected, this, [this]() {
        connected = true;
        refreshConfig();
        scheduleRefresh();
    });

    m_events.start();

    /* Don't wait for the first event to paint something. */
    if (HyprIpc::available()) {
        refreshConfig();
        refresh();
    }
}

void HyprState::scheduleRefresh()
{
    if (!m_coalesce.isActive())
        m_coalesce.start();
}

void HyprState::queueTitleUpdate(const QString &data)
{
    const int comma = data.indexOf(QLatin1Char(','));
    if (comma <= 0) {
        /* Unexpected payload: preserve correctness through the existing full
         * snapshot path, still debounced against an event storm. */
        m_pendingTitles.insert(QString(), QString());
        m_titleDebounce.start();
        return;
    }

    const QString address = normalizedAddress(data.left(comma));
    const QString title = data.mid(comma + 1);
    if (m_knownClients.contains(address)) {
        const QString monitorName = m_displayedClientMonitor.value(address);
        if (monitorName.isEmpty())
            return; /* known client, but not displayed on a panel */
        for (Monitor &monitor : monitors) {
            if (monitor.name != monitorName || monitor.title == title)
                continue;
            monitor.title = title;
            emit titleChanged(monitorName);
            break;
        }
        return;
    }

    /* A just-created client may emit its title before the structural event's
     * snapshot has populated the address map. Debounce only that race. */
    m_pendingTitles.insert(address, title);
    m_titleDebounce.start();
}

void HyprState::applyTitleUpdates()
{
    bool fallback = false;
    QStringList changedMonitors;
    for (auto it = m_pendingTitles.constBegin();
         it != m_pendingTitles.constEnd(); ++it) {
        if (it.key().isEmpty() || !m_knownClients.contains(it.key())) {
            fallback = true;
            break;
        }
        const QString monitorName = m_displayedClientMonitor.value(it.key());
        if (monitorName.isEmpty())
            continue; /* known client, but not displayed on a panel */
        for (Monitor &monitor : monitors) {
            if (monitor.name != monitorName || monitor.title == it.value())
                continue;
            monitor.title = it.value();
            changedMonitors.push_back(monitorName);
            break;
        }
    }
    m_pendingTitles.clear();
    if (fallback) {
        refresh();
    } else
        for (const QString &monitorName : changedMonitors)
            emit titleChanged(monitorName);
}

HyprState *HyprState::instance()
{
    static HyprState *s = new HyprState();
    return s;
}

QString HyprState::primaryMonitor() const
{
    const Monitor *best = nullptr;
    int bestWs = 0;
    for (const Monitor &m : monitors) {
        if (m.workspaces.isEmpty())
            continue;
        const int lowest = m.workspaces.first(); /* assigned ascending */
        if (!best || lowest < bestWs) {
            best = &m;
            bestWs = lowest;
        }
    }
    if (best)
        return best->name;
    for (const Monitor &m : monitors)
        if (m.focused)
            return m.name;
    const Monitor *lowestId = nullptr;
    for (const Monitor &m : monitors)
        if (!lowestId || m.id < lowestId->id)
            lowestId = &m;
    return lowestId ? lowestId->name : QString();
}

QString HyprState::focusedMonitor() const
{
    for (const Monitor &m : monitors)
        if (m.focused)
            return m.name;
    return primaryMonitor();
}

const HyprState::Monitor *HyprState::monitorByName(const QString &name) const
{
    for (const Monitor &m : monitors)
        if (m.name == name)
            return &m;
    return nullptr;
}

bool HyprState::workspaceOccupied(int id) const
{
    const auto it = workspaces.constFind(id);
    return it != workspaces.constEnd() && it->windows > 0;
}

QString HyprState::workspaceLabel(int id) const
{
    const auto it = workspaces.constFind(id);
    if (it != workspaces.constEnd() && !it->name.isEmpty()
            && it->name != QString::number(id))
        return it->name;
    return QString::number(id);
}

/* Hyprland spells "no submap" two ways depending on where you ask: the
 * `submap` command answers with the literal string "default", while the event
 * stream sends an empty payload. Both mean the same thing here. */
QString HyprState::normalizeSubmap(const QString &raw)
{
    const QString t = raw.trimmed();
    return t == QLatin1String("default") ? QString() : t;
}

void HyprState::resetSubmap()
{
    if (submap.isEmpty())
        return;
    HyprIpc::dispatch(QStringLiteral("hl.dsp.submap(\"reset\")"),
                      QStringLiteral("submap reset"));
}

void HyprState::switchWorkspace(int id)
{
    /* 0.56+ spelling first, then the pre-0.56 one -- see HyprIpc::dispatch.
     * `focus{workspace=N}` also moves focus to whichever output owns N, which
     * is what clicking another monitor's block in the bar should do. */
    HyprIpc::dispatch(QStringLiteral("hl.dsp.focus{workspace=%1}").arg(id),
                      QStringLiteral("workspace %1").arg(id));
}

void HyprState::refreshConfig()
{
    /* Workspace rules: `workspace = 3, monitor:DP-1` shows up here as
     * {"workspaceString": "3", "monitor": "DP-1"}. Only plain numbered
     * workspaces pinned to a named output take part in the fixed row --
     * name:foo and special: rules have no button. */
    /* Seed the submap; from here on the event stream keeps it current. */
    submap = normalizeSubmap(QString::fromUtf8(HyprIpc::request("submap")));

    m_rules.clear();
    const QJsonArray rules = HyprIpc::requestArray("workspacerules");
    for (const QJsonValue &v : rules) {
        const QJsonObject o = v.toObject();
        const QString monitor = o.value(QStringLiteral("monitor")).toString();
        if (monitor.isEmpty())
            continue;
        bool ok = false;
        const int id =
            o.value(QStringLiteral("workspaceString")).toString().toInt(&ok);
        if (!ok || id <= 0)
            continue;
        QVector<int> &ids = m_rules[monitor];
        if (!ids.contains(id))
            ids.push_back(id);
    }
    for (auto it = m_rules.begin(); it != m_rules.end(); ++it)
        std::sort(it->begin(), it->end());
}

void HyprState::assignWorkspaces()
{
    /* Hyprland reports monitors in connection order, which is not stable
     * across hotplug; ordering by id keeps the fallback blocks stable. */
    QVector<int> order(monitors.size());
    for (int i = 0; i < monitors.size(); i++)
        order[i] = i;
    std::sort(order.begin(), order.end(), [this](int a, int b) {
        return monitors[a].id < monitors[b].id;
    });

    for (int slot = 0; slot < order.size(); slot++) {
        Monitor &m = monitors[order[slot]];
        const auto rule = m_rules.constFind(m.name);
        if (rule != m_rules.constEnd() && !rule->isEmpty()) {
            m.workspaces = *rule;
        } else {
            /* No pinning rule for this output: hand it the next contiguous
             * block, so a two-monitor setup at the default block size reads
             * 1-5 on the first output and 6-10 on the second. */
            const int n = Theme::workspacesPerMonitor;
            m.workspaces.clear();
            for (int i = 0; i < n; i++)
                m.workspaces.push_back(slot * n + i + 1);
        }
        /* A workspace dragged onto this output that the fixed row doesn't
         * cover would otherwise leave the bar with nothing highlighted. */
        if (m.activeWorkspace > 0 && !m.workspaces.contains(m.activeWorkspace)) {
            m.workspaces.push_back(m.activeWorkspace);
            std::sort(m.workspaces.begin(), m.workspaces.end());
        }
    }
}

void HyprState::refresh()
{
    if (!HyprIpc::available()) {
        if (connected) {
            connected = false;
            emit changed();
        }
        return;
    }
    const bool oldConnected = connected;
    const QVector<Monitor> oldMonitors = monitors;
    const QHash<int, Workspace> oldWorkspaces = workspaces;
    connected = true;

    /* --- monitors --- */
    QVector<Monitor> mons;
    const QJsonArray monArr = HyprIpc::requestArray("monitors");
    for (const QJsonValue &v : monArr) {
        const QJsonObject o = v.toObject();
        Monitor m;
        m.id = o.value(QStringLiteral("id")).toInt();
        m.name = o.value(QStringLiteral("name")).toString();
        m.description = o.value(QStringLiteral("description")).toString();
        m.x = o.value(QStringLiteral("x")).toInt();
        m.y = o.value(QStringLiteral("y")).toInt();
        m.w = o.value(QStringLiteral("width")).toInt();
        m.h = o.value(QStringLiteral("height")).toInt();
        m.focused = o.value(QStringLiteral("focused")).toBool();
        m.activeWorkspace = o.value(QStringLiteral("activeWorkspace"))
                                .toObject()
                                .value(QStringLiteral("id"))
                                .toInt();
        mons.push_back(m);
    }
    /* A transient empty reply (compositor mid-restart) must not blank the
     * bar -- keep the last good monitor list, as the dwm build did. */
    if (mons.isEmpty() && !monitors.isEmpty())
        return;
    monitors = mons;

    /* --- workspaces (existence + occupancy) --- */
    workspaces.clear();
    const QJsonArray wsArr = HyprIpc::requestArray("workspaces");
    for (const QJsonValue &v : wsArr) {
        const QJsonObject o = v.toObject();
        Workspace w;
        w.id = o.value(QStringLiteral("id")).toInt();
        if (w.id <= 0)
            continue; /* special workspaces have negative ids; no button */
        w.name = o.value(QStringLiteral("name")).toString();
        w.monitor = o.value(QStringLiteral("monitor")).toString();
        w.windows = o.value(QStringLiteral("windows")).toInt();
        workspaces.insert(w.id, w);
    }

    assignWorkspaces();

    /* --- per-monitor focused window title ---
     * Hyprland's `activewindow` is global; the panel wants the title of the
     * window each output is showing. The client with the lowest
     * focusHistoryID on that output's visible workspace is the one it would
     * focus, which is what dwm's per-monitor `sel` meant. */
    QVector<int> bestFocus(monitors.size(), std::numeric_limits<int>::max());
    QVector<QString> bestAddress(monitors.size());
    QSet<QString> knownClients;
    QString activeClient;
    int activeFocus = std::numeric_limits<int>::max();
    const QJsonArray clients = HyprIpc::requestArray("clients");
    for (const QJsonValue &v : clients) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("hidden")).toBool())
            continue;
        const QString address =
            normalizedAddress(o.value(QStringLiteral("address")).toString());
        if (!address.isEmpty())
            knownClients.insert(address);
        const int ws = o.value(QStringLiteral("workspace"))
                           .toObject()
                           .value(QStringLiteral("id"))
                           .toInt();
        const int monId = o.value(QStringLiteral("monitor")).toInt();
        const int focus = o.value(QStringLiteral("focusHistoryID")).toInt();
        if (!address.isEmpty() && focus < activeFocus) {
            activeFocus = focus;
            activeClient = address;
        }
        for (int i = 0; i < monitors.size(); i++) {
            if (monitors[i].id != monId || monitors[i].activeWorkspace != ws)
                continue;
            if (focus < bestFocus[i]) {
                bestFocus[i] = focus;
                bestAddress[i] = address;
                monitors[i].title = o.value(QStringLiteral("title")).toString();
            }
        }
    }

    QHash<QString, QString> displayedClientMonitor;
    for (int i = 0; i < monitors.size(); ++i)
        if (!bestAddress[i].isEmpty())
            displayedClientMonitor.insert(bestAddress[i], monitors[i].name);
    m_knownClients = knownClients;
    m_displayedClientMonitor = displayedClientMonitor;
    m_activeClient = activeClient;

    if (oldConnected != connected || !sameMonitors(oldMonitors, monitors)
            || !sameWorkspaces(oldWorkspaces, workspaces))
        emit changed();
}
