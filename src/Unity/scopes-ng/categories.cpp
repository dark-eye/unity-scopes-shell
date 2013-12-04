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

// self
#include "categories.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

#include <scopes/CategoryRenderer.h>

using namespace unity::api;

namespace scopes_ng {

struct CategoryData
{
    scopes::Category::SCPtr category;
    QJsonDocument renderer_template;

    void setCategory(scopes::Category::SCPtr cat)
    {
        category = cat;
        // FIXME: validate
        renderer_template = QJsonDocument::fromJson(QByteArray(category->renderer_template().data().c_str()));
    }
};

Categories::Categories(QObject* parent)
    : QAbstractListModel(parent)
{
    m_roles[Categories::RoleCategoryId] = "categoryId";
    m_roles[Categories::RoleName] = "name";
    m_roles[Categories::RoleIcon] = "icon";
    m_roles[Categories::RoleRenderer] = "renderer";
    m_roles[Categories::RoleComponents] = "components";
    m_roles[Categories::RoleContentType] = "contentType";
    m_roles[Categories::RoleRendererHint] = "rendererHint";
    m_roles[Categories::RoleProgressSource] = "progressSource";
    m_roles[Categories::RoleResults] = "results";
    m_roles[Categories::RoleCount] = "count";
}

QHash<int, QByteArray>
Categories::roleNames() const
{
    return m_roles;
}

int Categories::rowCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);

    return m_categories.size();
}

ResultsModel* Categories::lookupCategory(std::string const& category_id)
{
    return m_categoryResults[category_id];
}

void Categories::registerCategory(scopes::Category::SCPtr category, ResultsModel* resultsModel)
{
    // do we already have a category with this id?
    int index = -1;
    for (int i = 0; i < m_categories.size(); i++) {
        if (m_categories[i]->category->id() == category->id()) {
            index = i;
            break;
        }
    }
    if (index >= 0) {
        CategoryData* catData = m_categories[index].data();
        // check if any attributes of the category changed
        QVector<int> changedRoles(collectChangedAttributes(catData->category, category));

        catData->setCategory(category);
        if (changedRoles.size() > 0) {
            QModelIndex changedIndex(this->index(index));
            dataChanged(changedIndex, changedIndex, changedRoles);
        }
    } else {
        CategoryData* catData = new CategoryData;
        catData->setCategory(category);

        auto last_index = m_categories.size();
        beginInsertRows(QModelIndex(), last_index, last_index);
        m_categories.append(QSharedPointer<CategoryData>(catData));
        if (resultsModel == nullptr) {
            resultsModel = new ResultsModel(this);
        }
        resultsModel->setCategoryId(QString::fromStdString(category->id()));
        m_categoryResults[category->id()] = resultsModel;
        endInsertRows();
    }
}

QVector<int> Categories::collectChangedAttributes(scopes::Category::SCPtr old_category, scopes::Category::SCPtr category)
{
    QVector<int> roles;

    if (category->title() != old_category->title()) {
        roles.append(RoleName);
    }
    if (category->icon() != old_category->icon()) {
        roles.append(RoleIcon);
    }
    if (category->renderer_template().data() != old_category->renderer_template().data()) {
        roles.append(RoleRenderer);
        roles.append(RoleComponents);
    }

    return roles;
}

void Categories::updateResultCount(ResultsModel* resultsModel)
{
    auto categoryId = resultsModel->categoryId().toStdString();
    int idx = -1;
    for (int i = 0; i < m_categories.count(); i++) {
        if (m_categories[i]->category->id() == categoryId) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        qWarning("unable to update results counts");
        return;
    }

    QVector<int> roles;
    roles.append(RoleCount);

    QModelIndex changedIndex(index(idx));
    dataChanged(changedIndex, changedIndex, roles);
}

void Categories::clearAll()
{
    if (m_categories.count() == 0) return;

    Q_FOREACH(ResultsModel* model, m_categoryResults) {
        model->clearResults();
    }

    QModelIndex changeStart(index(0));
    QModelIndex changeEnd(index(m_categories.count() - 1));
    QVector<int> roles;
    roles.append(RoleCount);
    dataChanged(changeStart, changeEnd, roles);
}

QVariant
Categories::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }

    if (index.row() >= m_categories.size()) {
        return QVariant();
    }

    CategoryData* catData = m_categories[index.row()].data();
    scopes::Category::SCPtr cat(catData->category);
    ResultsModel* resultsModel = m_categoryResults.contains(cat->id()) ? m_categoryResults[cat->id()] : nullptr;

    switch (role) {
        case RoleCategoryId:
            return QVariant(QString::fromStdString(cat->id()));
        case RoleName:
            return QVariant(QString::fromStdString(cat->title()));
        case RoleIcon:
            return QVariant(QString::fromStdString(cat->icon()));
        case RoleRenderer:
            return catData->renderer_template.toVariant();
        case RoleComponents:
             return QVariant();
        case RoleContentType:
            return QVariant(QString("default"));
        case RoleRendererHint:
            return QVariant();
        case RoleProgressSource:
            return QVariant();
        case RoleResults:
            return QVariant::fromValue(resultsModel);
        case RoleCount:
            return QVariant(resultsModel ? resultsModel->rowCount(QModelIndex()) : 0);
        default:
            return QVariant();
    }
}

} // namespace scopes_ng
