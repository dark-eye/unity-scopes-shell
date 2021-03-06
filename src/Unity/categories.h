/*
 * Copyright (C) 2013 Canonical, Ltd.
 *
 * Authors:
 *  Michał Sawicz <michal.sawicz@canonical.com>
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


#ifndef NG_CATEGORIES_H
#define NG_CATEGORIES_H

#include <unity/shell/scopes/CategoriesInterface.h>

#include <QSharedPointer>
#include <QJsonValue>
#include <set>

#include <unity/scopes/Category.h>

#include "resultsmodel.h"

namespace scopes_ng
{

class CategoryData;

class Q_DECL_EXPORT Categories : public unity::shell::scopes::CategoriesInterface
{
    Q_OBJECT

public:
    explicit Categories(QObject* parent = 0);

    enum ExtraRoles
    {
        RoleResultsSPtr = 999998,
        RoleCategorySPtr = 999999
    };

    ~Categories();

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;

    Q_INVOKABLE bool overrideCategoryJson(QString const& categoryId, QString const& json) override;

    QSharedPointer<ResultsModel> lookupCategory(std::string const& category_id);
    void registerCategory(const unity::scopes::Category::SCPtr& category, QSharedPointer<ResultsModel> model);
    void updateResultCount(const QSharedPointer<ResultsModel>& resultsModel);
    void clearAll();
    void markNewSearch();
    void purgeResults();
    void updateResult(unity::scopes::Result const& result, QString const& categoryId, unity::scopes::Result const& updated_result);

    static bool parseTemplate(std::string const& raw_template, QJsonValue* renderer, QJsonValue* components);

private Q_SLOTS:
    void countChanged();

private:
    int getCategoryIndex(QString const& categoryId) const;

    QList<QSharedPointer<CategoryData>> m_categories;
    QMap<std::string, QSharedPointer<ResultsModel>> m_categoryResults;
    QMap<QObject*, QString> m_countObjects;
    std::set<std::string> m_registeredCategories;
    int m_categoryIndex;
};

} // namespace scopes_ng

Q_DECLARE_METATYPE(scopes_ng::Categories*)
Q_DECLARE_METATYPE(std::shared_ptr<const unity::scopes::Category>)

#endif // NG_CATEGORIES_H
