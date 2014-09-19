/*
 * Copyright (C) 2011 Canonical, Ltd.
 *
 * Authors:
 *  Michal Hruby <michal.hruby@canonical.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// Self
#include "scopes.h"

// Local
#include "scope.h"
#include "overviewscope.h"
#include "ubuntulocationservice.h"

// Qt
#include <QDebug>
#include <QTimer>
#include <QDBusConnection>

#include <unity/scopes/Registry.h>
#include <unity/scopes/Scope.h>
#include <unity/scopes/ScopeProxyFwd.h>
#include <unity/UnityExceptions.h>

namespace scopes_ng
{

using namespace unity;

#define SCOPES_SCOPE_ID "scopes"

void ScopeListWorker::run()
{
    try
    {
        // m_runtimeConfig should be null in most cases, and empty string is for system-wide fallback
        if (!m_scopesRuntime) {
            scopes::Runtime::UPtr runtime_uptr = scopes::Runtime::create(m_runtimeConfig.toStdString());
            m_scopesRuntime = std::move(runtime_uptr);
        }
        auto registry = m_scopesRuntime->registry();
        m_metadataMap = registry->list();
    }
    catch (unity::Exception const& err)
    {
        qWarning("ERROR! Caught %s", err.to_string().c_str());
    }
    Q_EMIT discoveryFinished();
}

void ScopeListWorker::setRuntimeConfig(QString const& config)
{
    m_runtimeConfig = config;
}

void ScopeListWorker::setRuntime(scopes::Runtime::SPtr const& runtime)
{
    m_scopesRuntime = runtime;
}

scopes::Runtime::SPtr ScopeListWorker::getRuntime() const
{
    return m_scopesRuntime;
}

scopes::MetadataMap ScopeListWorker::metadataMap() const
{
    return m_metadataMap;
}

int Scopes::LIST_DELAY = -1;
const int Scopes::SCOPE_DELETE_DELAY = 3;

class Scopes::Priv : public QObject {
    Q_OBJECT
Q_SIGNALS:
    void safeInvalidateScopeResults(const QString& scopeName);
public:
    std::unique_ptr<core::ScopedConnection> m_list_update_callback_connection_;
};

Scopes::Scopes(QObject *parent)
    : unity::shell::scopes::ScopesInterface(parent)
    , m_noFavorites(false)
    , m_overviewScope(nullptr)
    , m_listThread(nullptr)
    , m_loaded(false)
    , m_queryOnStartup(true)
    , m_priv(new Priv())
{
    QByteArray noFav = qgetenv("UNITY_SCOPES_NO_FAVORITES");
    if (!noFav.isNull()) {
        m_noFavorites = true;
    }

    // delaying spawning the worker thread, causes problems with qmlplugindump
    // without it
    if (LIST_DELAY < 0) {
        QByteArray listDelay = qgetenv("UNITY_SCOPES_LIST_DELAY");
        LIST_DELAY = listDelay.isNull() ? 100 : listDelay.toInt();
    }
    QTimer::singleShot(LIST_DELAY, this, SLOT(populateScopes()));

    connect(m_priv.get(), SIGNAL(safeInvalidateScopeResults(const QString&)), this,
            SLOT(invalidateScopeResults(const QString &)), Qt::QueuedConnection);

    QDBusConnection::sessionBus().connect(QString(), QString("/com/canonical/unity/scopes"), QString("com.canonical.unity.scopes"), QString("InvalidateResults"), this, SLOT(invalidateScopeResults(QString)));

    m_dashSettings = QGSettings::isSchemaInstalled("com.canonical.Unity.Dash") ? new QGSettings("com.canonical.Unity.Dash", QByteArray(), this) : nullptr;
    if (m_dashSettings)
    {
        QObject::connect(m_dashSettings, &QGSettings::changed, this, &Scopes::dashSettingsChanged);
    }

    m_overviewScope = new OverviewScope(this);
    m_locationService.reset(new UbuntuLocationService());
}

Scopes::~Scopes()
{
    if (m_listThread && !m_listThread->isFinished()) {
        // libunity-scopes supports timeouts, so this shouldn't block forever
        m_listThread->wait();
    }
}

int Scopes::rowCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent)

    return m_scopes.count();
}

int Scopes::count() const
{
    return m_scopes.count();
}

void Scopes::populateScopes()
{
    auto thread = new ScopeListWorker;
    QByteArray runtimeConfig = qgetenv("UNITY_SCOPES_RUNTIME_PATH");
    thread->setRuntimeConfig(QString::fromLocal8Bit(runtimeConfig));
    QObject::connect(thread, &ScopeListWorker::discoveryFinished, this, &Scopes::discoveryFinished);
    QObject::connect(thread, &ScopeListWorker::finished, thread, &QObject::deleteLater);

    m_listThread = thread;
    m_listThread->start();
}

void Scopes::discoveryFinished()
{
    ScopeListWorker* thread = qobject_cast<ScopeListWorker*>(sender());

    m_scopesRuntime = thread->getRuntime();
    auto scopes = thread->metadataMap();
    m_priv->m_list_update_callback_connection_.reset(
            new core::ScopedConnection(
                    m_scopesRuntime->registry()->set_list_update_callback(
                            std::bind(&Scopes::Priv::safeInvalidateScopeResults,
                                      m_priv.get(), SCOPES_SCOPE_ID))));

    beginResetModel();

    if (m_noFavorites) {
        // add all visible scopes
        for (auto it = scopes.begin(); it != scopes.end(); ++it) {
            if (!it->second.invisible()) {
                auto scope = new Scope(this);
                scope->setScopeData(it->second);
                m_scopes.append(scope);
            }
        }
    }

    // HACK! deal with the overview scope
    {
        auto it = scopes.find(SCOPES_SCOPE_ID);
        if (it != scopes.end()) {
            m_overviewScope->setScopeData(it->second);
        } else {
            qWarning("Unable to add overview scope, can't find with ID: \"%s\"", SCOPES_SCOPE_ID);
        }
    }

    // cache all the metadata
    for (auto it = scopes.begin(); it != scopes.end(); ++it) {
        m_cachedMetadata[QString::fromStdString(it->first)] = std::make_shared<unity::scopes::ScopeMetadata>(it->second);
    }

    processFavoriteScopes();
    endResetModel();

    m_loaded = true;
    Q_EMIT loadedChanged();
    Q_EMIT countChanged();
    Q_EMIT overviewScopeChanged();
    Q_EMIT metadataRefreshed();

    m_listThread = nullptr;

    if (m_queryOnStartup)
    {
        m_queryOnStartup = false;
        queryScopesOnStartup();
    }
}

void Scopes::queryScopesOnStartup()
{
    for (auto scope: m_scopes) {
        if (!scope->isActive()) {
            scope->setSearchQuery("");
            // must dispatch search explicitly since setSearchQuery will not do that for inactive scope
            scope->dispatchSearch();
        }
    }
}

void Scopes::processFavoriteScopes()
{
    if (m_noFavorites) {
        return;
    }

    //
    // read the favoriteScopes array value from gsettings.
    // process it and turn its values into scope ids.
    // create new Scope objects or remove existing according to the list of favorities.
    // notify about scopes model changes accordingly.
    if (m_dashSettings) {
        QStringList newFavorites;
        QSet<QString> favScopesLut;
        for (auto const& fv: m_dashSettings->get("favoriteScopes").toList())
        {
            try
            {
                auto const query = unity::scopes::CannedQuery::from_uri(fv.toString().toStdString());
                const QString id = QString::fromStdString(query.scope_id());
                newFavorites.push_back(id);
                favScopesLut.insert(id);
            }
            catch (const InvalidArgumentException &e)
            {
                qWarning() << "Invalid canned query '" << fv.toString() << "'" << QString::fromStdString(e.what());
            }
        }

        // this prevents further processing if we get called back when calling scope->setFavorite() below
        if (m_favoriteScopes == newFavorites)
            return;

        m_favoriteScopes = newFavorites;

        QSet<QString> oldScopes;
        int row = 0;
        // remove un-favorited scopes
        for (auto it = m_scopes.begin(); it != m_scopes.end();)
        {
            if (!favScopesLut.contains((*it)->id()))
            {
                beginRemoveRows(QModelIndex(), row, row);
                (*it)->setFavorite(false);
                //
                // we need to delay actual deletion of Scope object so that shell can animate it
                QTimer::singleShot(1000 * SCOPE_DELETE_DELAY, (*it), SLOT(deleteLater));
                it = m_scopes.erase(it);
                endRemoveRows();
            }
            else
            {
                oldScopes.insert((*it)->id());
                ++it;
                ++row;
            }
        }

        // add new favorites
        row = 0;
        for (auto favIt = m_favoriteScopes.begin(); favIt != m_favoriteScopes.end(); )
        {
            auto const fav = *favIt;
            if (!oldScopes.contains(fav))
            {
                auto it = m_cachedMetadata.find(fav);
                if (it != m_cachedMetadata.end())
                {
                    auto scope = new Scope(this);
                    scope->setScopeData(*(it.value()));
                    scope->setFavorite(true);
                    beginInsertRows(QModelIndex(), row, row);
                    m_scopes.insert(row, scope);
                    endInsertRows();
                }
                else
                {
                    qWarning() << "No such scope:" << fav;
                    favIt = m_favoriteScopes.erase(favIt);
                    continue;
                }
            }
            ++row;
            ++favIt;
        }
    }
}

void Scopes::dashSettingsChanged(QString const& key)
{
    if (key != "favoriteScopes") {
        return;
    }

    processFavoriteScopes();

    if (m_overviewScope)
    {
        m_overviewScope->updateFavorites(m_favoriteScopes);
    }
}

void Scopes::refreshFinished()
{
    ScopeListWorker* thread = qobject_cast<ScopeListWorker*>(sender());

    auto scopes = thread->metadataMap();

    // cache all the metadata
    for (auto it = scopes.begin(); it != scopes.end(); ++it) {
        m_cachedMetadata[QString::fromStdString(it->first)] = std::make_shared<unity::scopes::ScopeMetadata>(it->second);
    }

    Q_EMIT metadataRefreshed();

    m_listThread = nullptr;
}

void Scopes::invalidateScopeResults(QString const& scopeName)
{
    // HACK! mediascanner invalidates local media scopes, but those are aggregated, so let's "forward" the call
    if (scopeName == "mediascanner-music") {
        invalidateScopeResults("musicaggregator");
    } else if (scopeName == "mediascanner-video") {
        invalidateScopeResults("videoaggregator");
    } else if (scopeName == "smart-scopes") {
        // emitted when smart scopes proxy discovers new scopes
        Q_FOREACH(Scope* scope, m_scopes) {
            scope->invalidateResults();
        }
    }

    Scope* scope = getScopeById(scopeName);
    if (scope == nullptr) return;

    scope->invalidateResults();
}

QVariant Scopes::data(const QModelIndex& index, int role) const
{
    Scope* scope = m_scopes.at(index.row());

    switch (role) {
        case Scopes::RoleScope:
            return QVariant::fromValue(scope);
        case Scopes::RoleId:
            return QString(scope->id());
        case Scopes::RoleTitle:
            return QString(scope->name());
        default:
            return QVariant();
    }
}

unity::shell::scopes::ScopeInterface* Scopes::getScope(int row) const
{
    if (row >= m_scopes.size() || row < 0) {
        return nullptr;
    }
    return m_scopes[row];
}

unity::shell::scopes::ScopeInterface* Scopes::getScope(const QString& scopeId) const
{
    return getScopeById(scopeId);
}

Scope* Scopes::getScopeById(QString const& scopeId) const
{
    Q_FOREACH(Scope* scope, m_scopes) {
        if (scope->id() == scopeId) {
            return scope;
        }
    }

    return nullptr;
}

QStringList Scopes::getFavoriteIds() const
{
    return m_favoriteScopes;
}

void Scopes::setFavorite(QString const& scopeId, bool value)
{
    if (m_dashSettings)
    {
        QStringList cannedQueries;
        bool changed = false;

        for (auto const& fav: m_favoriteScopes)
        {
            if (value == false && fav == scopeId) {
                changed = true;
                continue; // skip it
            }
            // TODO: use CannedQuery::to_uri() when we really support them
            const QString query = "scope://" + fav;
            cannedQueries.push_back(query);
        }

        if (value && !m_favoriteScopes.contains(scopeId)) {
            const QString query = "scope://" + scopeId;
            cannedQueries.push_back(query);
            changed = true;
        }

        if (changed) {
            // update gsettings entry
            // note: this will trigger notification, so that new favorites are processed by processFavoriteScopes
            m_dashSettings->set("favoriteScopes", QVariant(cannedQueries));
        }
    }
}

void Scopes::moveFavoriteTo(QString const& scopeId, int index)
{
    if (m_dashSettings)
    {
        QStringList cannedQueries;
        bool found = false;

        int i = 0;
        for (auto const& fav: m_favoriteScopes)
        {
            if (fav == scopeId) {
                if (index == i)
                    return; // same position

                // we are removing existing favorite and inserting new one with higher index,
                // so need to decrease it to compensate for removed item
                if (index > i) {
                    --index;
                }
                found = true;
            } else {
                const QString query = "scope://" + fav;
                cannedQueries.push_back(query);
            }

            ++i;
        }

        if (found) {
            // insert scopeId at new position
            const QString query = "scope://" + scopeId;
            cannedQueries.insert(index, query);
            // update gsettings entry
            // note: this will trigger notification, so that new favorites are processed by processFavoriteScopes
            m_dashSettings->set("favoriteScopes", QVariant(cannedQueries));
        }
    }
}

QMap<QString, unity::scopes::ScopeMetadata::SPtr> Scopes::getAllMetadata() const
{
    return m_cachedMetadata;
}

scopes::ScopeMetadata::SPtr Scopes::getCachedMetadata(QString const& scopeId) const
{
    auto it = m_cachedMetadata.constFind(scopeId);
    if (it != m_cachedMetadata.constEnd()) {
        return it.value();
    }

    return scopes::ScopeMetadata::SPtr();
}

void Scopes::refreshScopeMetadata()
{
    // make sure there's just one listing in-progress at any given time
    if (m_listThread == nullptr && m_scopesRuntime) {
        auto thread = new ScopeListWorker;
        thread->setRuntime(m_scopesRuntime);
        QObject::connect(thread, &ScopeListWorker::discoveryFinished, this, &Scopes::refreshFinished);
        QObject::connect(thread, &ScopeListWorker::finished, thread, &QObject::deleteLater);

        m_listThread = thread;
        m_listThread->start();
    }
}

unity::shell::scopes::ScopeInterface* Scopes::overviewScope() const
{
    return m_loaded ? m_overviewScope : nullptr;
}

bool Scopes::loaded() const
{
    return m_loaded;
}

LocationService::Ptr Scopes::locationService() const
{
    return m_locationService;
}

} // namespace scopes_ng

#include <scopes.moc>
