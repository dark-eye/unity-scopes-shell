/*
 * Copyright (C) 2014 Canonical, Ltd.
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of version 3 of the GNU Lesser General Public License as published
 * by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Pete Woods <pete.woods@canonical.com>
 */

#include <QSignalSpy>

#include <Unity/scopes.h>
#include <Unity/categories.h>
#include <Unity/utils.h>

#include <scope-harness/internal/category-arguments.h>
#include <scope-harness/internal/result-arguments.h>
#include <scope-harness/internal/results-view-arguments.h>
#include <scope-harness/view/preview-view.h>
#include <scope-harness/view/results-view.h>
#include <scope-harness/test-utils.h>

using namespace std;
namespace ng = scopes_ng;
namespace sc = unity::scopes;
namespace ss = unity::shell::scopes;

namespace unity
{
namespace scopeharness
{
namespace view
{

struct ResultsView::Priv
{
    void checkActiveScope()
    {
        throwIfNot(m_active_scope, "There is no active scope");
    }

    std::shared_ptr<ng::Scopes> m_scopes;

    weak_ptr<PreviewView> m_previewView;

    ng::Scope::Ptr m_active_scope;
};

ResultsView::ResultsView(const internal::ResultsViewArguments& arguments) :
        p(new Priv)
{
    p->m_scopes = arguments.scopes;
}

void ResultsView::setPreviewView(PreviewView::SPtr previewView)
{
    p->m_previewView = previewView;
}

void ResultsView::setActiveScope(const std::string &id_)
{
    QString id = QString::fromStdString(id_);

    // Deactivate the previous scopes first
    for (int row = 0; row < p->m_scopes->rowCount(); ++row)
    {
        ng::Scope::Ptr scope = p->m_scopes->getScopeByRow(row);
        if (scope->id() != id)
        {
            scope->setActive(false);
        }
    }

    p->m_active_scope.reset();

    // Activate the new scope
    for (int row = 0; row < p->m_scopes->rowCount(); ++row)
    {
        ng::Scope::Ptr scope = p->m_scopes->getScopeByRow(row);
        if (scope->id() == id)
        {
            p->m_active_scope = scope;
            QSignalSpy spy(scope.data(), SIGNAL(searchInProgressChanged()));

            scope->setSearchQuery("");
            scope->setActive(true);

            if (!scope->searchInProgress())
            {
                spy.wait(100);
            }
            if (scope->searchInProgress())
            {
                throwIfNot(spy.wait(), "Active scope didn't finish searching");
            }

            break;
        }
    }
}

void ResultsView::setQuery(const std::string& searchString_)
{
    p->checkActiveScope();

    QString searchString = QString::fromStdString(searchString_);

    // If we've already got a completed search for this string then do nothing
    if (p->m_active_scope->searchQuery() == searchString)
    {
        return;
    }

    throwIf(p->m_active_scope->searchInProgress(), "Search is already in progress");

    QSignalSpy spy(p->m_active_scope.data(), SIGNAL(searchInProgressChanged()));
    // perform a search
    p->m_active_scope->setSearchQuery(searchString);
    // search should not be happening yet
    throwIf(p->m_active_scope->searchInProgress(), "Search was in progress too soon");
    throwIfNot(spy.wait(), "Search spy received no events");
    if (p->m_active_scope->searchInProgress())
    {
        // wait for the search to finish
        throwIfNot(spy.wait(), "Search spy received no events");
    }
    throwIf(p->m_active_scope->searchInProgress(), "Search did not complete");
}

void ResultsView::waitForResultsChange()
{
    p->checkActiveScope();

    throwIf(p->m_active_scope->searchInProgress(), "Search is already in progress");
    // wait for the search to finish
    QSignalSpy spy(p->m_active_scope.data(), SIGNAL(searchInProgressChanged()));
    throwIfNot(spy.wait(), "Search status didn't change");
    if(spy.size() == 1) {
        throwIfNot(spy.wait(), "Search status didn't change");
    }
    throwIf(p->m_active_scope->searchInProgress(), "");
}

bool ResultsView::overrideCategoryJson(std::string const& categoryId, std::string const& json)
{
    p->checkActiveScope();

    return p->m_active_scope->categories()->overrideCategoryJson(
            QString::fromStdString(categoryId), QString::fromStdString(json));
}

results::Category::List ResultsView::categories()
{
    auto cats = raw_categories();

    results::Category::List result;
    for (int i = 0; i < cats->rowCount(); ++i)
    {
        try
        {
            result.emplace_back(category(i));
        }
        catch (range_error& e)
        {
        }
    }

    return result;
}

results::Category ResultsView::category(unsigned int row)
{
    auto cats = raw_categories();
    auto categoryIndex = cats->index(row);

    QVariant variant = cats->data(categoryIndex, ng::Categories::RoleCategorySPtr);
    if (!variant.canConvert<sc::Category::SCPtr>())
    {
        throw std::range_error("Invalid category data at index " + to_string(row));
    }
    auto rawCategory = variant.value<sc::Category::SCPtr>();

    QVariant resultsVariant = cats->data(
            cats->index(row), ng::Categories::RoleResultsSPtr);
    QSharedPointer<ss::ResultsModelInterface> resultModel = resultsVariant.value<
            QSharedPointer<ss::ResultsModelInterface>>();
    results::Result::List results;
    if (resultModel)
    {
        for (int i = 0; i < resultModel->rowCount(); ++i)
        {
            auto idx = resultModel->index(i);
            results.emplace_back(results::Result(internal::ResultArguments
                { resultModel, p->m_active_scope, idx,
                  dynamic_pointer_cast<ResultsView>(shared_from_this()),
                  p->m_previewView.lock() }));
        }
    }

    return results::Category(internal::CategoryArguments{cats, categoryIndex, results});
}

results::Category ResultsView::category(const string& categoryId_)
{
    auto cats = raw_categories();

    QString categoryId = QString::fromStdString(categoryId_);
    int row = -1;

    for (int i = 0; i < cats->rowCount(); ++i)
    {
        QVariant variant = cats->data(cats->index(i),
                                      ss::CategoriesInterface::RoleCategoryId);
        if (variant.toString() == categoryId)
        {
            row = i;
            break;
        }
    }

    throwIf(row == -1, "Could not find category");
    return category(row);
}

ss::CategoriesInterface* ResultsView::raw_categories() const
{
    p->checkActiveScope();
    return p->m_active_scope->categories();
}

QSharedPointer<ss::ScopeInterface> ResultsView::activeScope() const
{
    p->checkActiveScope();
    return p->m_active_scope;
}

std::string ResultsView::scopeId() const
{
    p->checkActiveScope();
    return p->m_active_scope->id().toStdString();
}

std::string ResultsView::displayName() const
{
    p->checkActiveScope();
    return p->m_active_scope->name().toStdString();
}

std::string ResultsView::iconHint() const
{
    p->checkActiveScope();
    return p->m_active_scope->iconHint().toStdString();
}

std::string ResultsView::description() const
{
    p->checkActiveScope();
    return p->m_active_scope->description().toStdString();
}

std::string ResultsView::searchHint() const
{
    p->checkActiveScope();
    return p->m_active_scope->searchHint().toStdString();
}

std::string ResultsView::shortcut() const
{
    p->checkActiveScope();
    return p->m_active_scope->shortcut().toStdString();
}

std::string ResultsView::searchQuery() const
{
    p->checkActiveScope();
    return p->m_active_scope->searchQuery().toStdString();
}

sc::Variant ResultsView::customizations() const
{
    p->checkActiveScope();
    return ng::qVariantToScopeVariant(p->m_active_scope->customizations());
}

std::string ResultsView::sessionId() const
{
    p->checkActiveScope();
    return p->m_active_scope->sessionId().toStdString();
}

int ResultsView::queryId() const
{
    p->checkActiveScope();
    return p->m_active_scope->queryId();
}

unity::shell::scopes::ScopeInterface::Status ResultsView::status() const
{
    p->checkActiveScope();
    return p->m_active_scope->status();
}

}
}
}
