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

#ifndef NG_SCOPE_H
#define NG_SCOPE_H

// Qt
#include <QObject>
#include <QString>
#include <QTimer>
#include <QMetaType>
#include <QMetaObject>
#include <QNetworkConfigurationManager>
#include <QPointer>
#include <QMultiMap>
#include <QUuid>

// scopes
#include <unity/scopes/ActivationResponse.h>
#include <unity/scopes/Result.h>
#include <unity/scopes/Scope.h>
#include <unity/scopes/ScopeMetadata.h>
#include <unity/shell/scopes/ScopeInterface.h>

#include "filters.h"
#include "collectors.h"
#include "departmentnode.h"
#include "department.h"
#include "ubuntulocationservice.h"
#include "locationaccesshelper.h"

namespace scopes_ng
{

class Categories;
class PushEvent;
class PreviewModel;
class SettingsModel;
class Scopes;

class CollectionController
{
public:
    CollectionController() {}
    ~CollectionController()
    {
        if (m_receiver) {
            m_receiver->invalidate();
        }
        // shouldn't call QueryCtrlProxy->cancel() cause the Runtime might be
        // in the process of being destroyed
    }

    bool isValid()
    {
        return m_listener && m_controller;
    }

    void invalidate()
    {
        if (m_receiver) {
            m_receiver->invalidate();
            m_receiver.reset();
        }
        m_listener.reset();
        if (m_controller) {
            m_controller->cancel();
            m_controller.reset();
        }
    }

    void setListener(unity::scopes::ListenerBase::SPtr const& listener)
    {
        m_listener = listener;
        m_receiver = std::dynamic_pointer_cast<ScopeDataReceiverBase>(listener);
    }

    void setController(unity::scopes::QueryCtrlProxy const& controller)
    {
        m_controller = controller;
    }

private:
    unity::scopes::ListenerBase::SPtr m_listener;
    std::shared_ptr<ScopeDataReceiverBase> m_receiver;
    unity::scopes::QueryCtrlProxy m_controller;
};

class Q_DECL_EXPORT Scope : public unity::shell::scopes::ScopeInterface
{
    Q_OBJECT

public:
    typedef QSharedPointer<Scope> Ptr;

    static Scope::Ptr newInstance(scopes_ng::Scopes* parent, bool favorite = false);

    virtual ~Scope();

    virtual bool event(QEvent* ev) override;

    Q_PROPERTY(bool favorite READ favorite WRITE setFavorite NOTIFY favoriteChanged)

    /* getters */
    QString id() const override;
    QString name() const override;
    QString iconHint() const override;
    QString description() const override;
    QString searchHint() const override;
    bool favorite() const override;
    QString shortcut() const override;
    bool searchInProgress() const override;
    bool activationInProgress() const override;
    unity::shell::scopes::ScopeInterface::Status status() const override;
    unity::shell::scopes::CategoriesInterface* categories() const override;
    unity::shell::scopes::SettingsModelInterface* settings() const override;
    unity::shell::scopes::FiltersInterface* filters() const override;
    unity::shell::scopes::FilterBaseInterface* primaryNavigationFilter() const override;
    QString primaryNavigationTag() const override;
    int activeFiltersCount() const override;

    bool require_child_scopes_refresh() const;
    void update_child_scopes();
    QString searchQuery() const override;
    QString noResultsHint() const override;
    QString formFactor() const override;
    bool isActive() const override;
    QString currentNavigationId() const override;
    bool hasNavigation() const override;
    QVariantMap customizations() const override;

    /* setters */
    void setSearchQuery(const QString& search_query) override;
    void setNoResultsHint(const QString& hint) override;
    void setFormFactor(const QString& form_factor) override;
    void setActive(const bool) override;
    void setFavorite(const bool) override;

    Q_INVOKABLE void activate(QVariant const& result, QString const& categoryId) override;
    Q_INVOKABLE unity::shell::scopes::PreviewModelInterface* preview(QVariant const& result, QString const& categoryId) override;
    Q_INVOKABLE void cancelActivation() override;
    Q_INVOKABLE void closeScope(unity::shell::scopes::ScopeInterface* scope) override;
    Q_INVOKABLE unity::shell::scopes::NavigationInterface* getNavigation(QString const& id) override;
    Q_INVOKABLE void setNavigationState(QString const& navId) override;
    Q_INVOKABLE void performQuery(QString const& cannedQuery) override;
    Q_INVOKABLE void refresh() override;
    Q_INVOKABLE void resetPrimaryNavigationTag() override;
    Q_INVOKABLE void resetFilters() override;

    void setScopeData(unity::scopes::ScopeMetadata const& data);
    void handleActivation(std::shared_ptr<unity::scopes::ActivationResponse> const&, unity::scopes::Result::SPtr const&, QString const& categoryId="");
    void activateUri(QString const& uri);
    void activateAction(QVariant const& result, QString const& categoryId, QString const& actionId) override;

    bool resultsDirty() const;
    virtual unity::scopes::ScopeProxy proxy_for_result(unity::scopes::Result::SPtr const& result) const;

    QString sessionId() const;
    int queryId() const;
    bool initialQueryDone() const;

    Scope::Ptr findTempScope(QString const& id) const;

    void setSearchQueryString(const QString& search_query);

    const QNetworkConfigurationManager& networkManager() const;

public Q_SLOTS:
    void invalidateChildScopes();
    void invalidateResults(bool programmaticSearch = false);
    virtual void dispatchSearch(bool programmaticSearch = false);
    void setSearchInProgress(bool searchInProgress);
    void setActivationInProgress(bool activationInProgress);

Q_SIGNALS:
    void resultsDirtyChanged();
    void favoriteChanged(bool);
    void activationFailed(QString const& id);
    void updateResultRequested();

private Q_SLOTS:
    void typingFinished();
    void flushUpdates(bool finalize = false);
    void metadataRefreshed();
    void departmentModelDestroyed(QObject* obj);
    void locationAccessChanged();
    void filterStateChanged();
    void previewModelDestroyed(QObject *obj);

protected:
    explicit Scope(scopes_ng::Scopes* parent, bool favorite = false);

    void setStatus(unity::shell::scopes::ScopeInterface::Status status);
    void invalidateLastSearch();
    void createSettingsModel();

    unity::scopes::ScopeProxy proxy() const;

    QScopedPointer<Categories> m_categories;
    QPointer<Scopes> m_scopesInstance;

private:
    static void updateNavigationModels(DepartmentNode* rootNode, QMultiMap<QString, Department*>& navigationModels, QString const& activeNavigation);
    static QString buildQuery(QString const& scopeId, QString const& searchQuery, QString const& departmentId, unity::scopes::FilterState const& filterState);
    void setScopesInstance(Scopes*);
    void startTtlTimer();
    void setCurrentNavigationId(QString const& id);
    void setFilterState(unity::scopes::FilterState const& filterState);
    void processSearchChunk(PushEvent* pushEvent);
    void processPrimaryNavigationTag(QString const &targetDepartmentId);
    void processActiveFiltersCount();
    void setCannedQuery(unity::scopes::CannedQuery const& query);
    void executeCannedQuery(unity::scopes::CannedQuery const& query, bool allowDelayedActivation);
    void handlePreviewUpdate(unity::scopes::Result::SPtr const& result, unity::scopes::PreviewWidgetList const& widgets);

    void processResultSet(QList<std::shared_ptr<unity::scopes::CategorisedResult>>& result_set);

    static unity::scopes::Department::SCPtr findDepartmentById(unity::scopes::Department::SCPtr const& root, std::string const& id);
    unity::scopes::Department::SCPtr findUpdateNode(DepartmentNode* node, unity::scopes::Department::SCPtr const& scopeNode);

    QUuid m_session_id;
    int m_query_id;
    QString m_searchQuery;
    QString m_noResultsHint;
    QString m_formFactor;
    QString m_currentNavigationId;
    QString m_primaryNavigationTag;
    QVariantMap m_customizations;
    std::unique_ptr<unity::scopes::Variant> m_queryUserData;
    int m_activeFiltersCount;
    bool m_isActive;
    bool m_searchInProgress;
    bool m_activationInProgress;
    bool m_resultsDirty;
    bool m_delayedSearchProcessing;
    bool m_hasNavigation;
    bool m_favorite;
    bool m_initialQueryDone;
    int m_cardinality;

    bool m_childScopesDirty;

    QMap<std::string, QList<std::shared_ptr<unity::scopes::CategorisedResult>>> m_category_results;
    std::unique_ptr<CollectionController> m_searchController;
    std::unique_ptr<CollectionController> m_activationController;
    unity::scopes::ScopeProxy m_proxy;
    unity::scopes::ScopeMetadata::SPtr m_scopeMetadata;
    std::shared_ptr<unity::scopes::ActivationResponse> m_delayedActivation;
    unity::scopes::Department::SCPtr m_rootDepartment;
    unity::scopes::Department::SCPtr m_lastRootDepartment;
    unity::scopes::FilterState m_filterState;
    unity::shell::scopes::ScopeInterface::Status m_status;
    QList<unity::scopes::FilterBase::SCPtr> m_receivedFilters;
    QScopedPointer<Filters> m_filters;

    QScopedPointer<SettingsModel> m_settingsModel;
    QSharedPointer<DepartmentNode> m_departmentTree;
    QTimer m_typingTimer;
    QTimer m_searchProcessingDelayTimer;
    QTimer m_invalidateTimer;
    QList<std::shared_ptr<unity::scopes::CategorisedResult>> m_cachedResults;
    QMultiMap<QString, Department*> m_departmentModels;
    QMap<Department*, QString> m_inverseDepartments;
    QMetaObject::Connection m_metadataConnection;
    QSharedPointer<UbuntuLocationService> m_locationService;
    QSharedPointer<UbuntuLocationService::Token> m_locationToken;
    QNetworkConfigurationManager m_network_manager;
    QList<PreviewModel*> m_previewModels;
};

} // namespace scopes_ng

Q_DECLARE_METATYPE(scopes_ng::Scope*)

#endif // NG_SCOPE_H
