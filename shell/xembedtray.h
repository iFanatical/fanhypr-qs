/* Optional XEmbed system-tray manager for legacy X11/Wine applications.
 * Built only when X11/XComposite/XDamage are available; runtime activation
 * additionally requires a reachable XWayland DISPLAY. */
#ifndef FANHYPR_QS_XEMBEDTRAY_H
#define FANHYPR_QS_XEMBEDTRAY_H

#include <QHash>
#include <QObject>

class QSocketNotifier;
class SniItem;
struct _XDisplay;

class XEmbedTray : public QObject {
    Q_OBJECT
public:
    static XEmbedTray *instance();
    ~XEmbedTray() override;

    bool active() const { return m_display != nullptr; }
    void sendButton(unsigned long window, int button);

private:
    struct Entry {
        SniItem *item = nullptr;
        unsigned long damage = 0;
    };

    explicit XEmbedTray(QObject *parent = nullptr);
    void processEvents();
    void dock(unsigned long window);
    void remove(unsigned long window, bool windowDestroyed = false);
    void refresh(unsigned long window);
    void announceManager();
    unsigned long atom(const char *name);

    _XDisplay *m_display = nullptr;
    unsigned long m_root = 0;
    unsigned long m_manager = 0;
    unsigned long m_selection = 0;
    unsigned long m_managerAtom = 0;
    unsigned long m_opcodeAtom = 0;
    unsigned long m_xembedAtom = 0;
    unsigned long m_netWmNameAtom = 0;
    unsigned long m_utf8Atom = 0;
    int m_damageEvent = 0;
    QSocketNotifier *m_notifier = nullptr;
    QHash<unsigned long, Entry> m_entries;
};

#endif
