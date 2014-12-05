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

#include <scope-harness/category-matcher.h>
#include <scope-harness/category-list-matcher.h>

#include <boost/optional.hpp>

#include <deque>
#include <unordered_map>

using namespace std;
using namespace boost;

namespace unity
{
namespace scopeharness
{

struct CategoryListMatcher::Priv
{
    Mode m_mode = Mode::all;

    deque<CategoryMatcher> m_categories;

    optional<unsigned int> m_hasAtLeast;

    optional<unsigned int> m_hasExactly;

    void all(MatchResult& matchResult, const Category::List& categoryList)
    {
        if (categoryList.size() != m_categories.size())
        {
            matchResult.failure(
                    "Category list contained " + to_string(categoryList.size())
                            + " expected " + to_string(m_categories.size()));
            return;
        }

        for (size_t row = 0; row < m_categories.size(); ++row)
        {
            const auto& expectedCategory = m_categories[row];
            const auto& actualCategory = categoryList[row];
            expectedCategory.match(matchResult, actualCategory);
        }
    }

    void id(MatchResult& matchResult, const Category::List& categoryList)
    {
        unordered_map<string, Category> categoriesById;
        for (const auto& category : categoryList)
        {
            categoriesById.insert({category.id(), category});
        }

        for (const auto& expectedCategory : m_categories)
        {
            auto it = categoriesById.find(expectedCategory.getId());
            if (it == categoriesById.end())
            {
                matchResult.failure(
                        "Category with ID " + expectedCategory.getId()
                                + " could not be found");
            }
            else
            {
                expectedCategory.match(matchResult, it->second);
            }
        }
    }
};

CategoryListMatcher::CategoryListMatcher() :
        p(new Priv)
{
}

CategoryListMatcher& CategoryListMatcher::mode(CategoryListMatcher::Mode mode)
{
    p->m_mode = mode;
    return *this;
}

CategoryListMatcher& CategoryListMatcher::category(CategoryMatcher&& categoryMatcher)
{
    p->m_categories.emplace_back(move(categoryMatcher));
    return *this;
}

CategoryListMatcher& CategoryListMatcher::category(const CategoryMatcher& categoryMatcher)
{
    p->m_categories.emplace_back(categoryMatcher);
    return *this;
}

CategoryListMatcher& CategoryListMatcher::hasAtLeast(unsigned int minimum)
{
    p->m_hasAtLeast = minimum;
    return *this;
}

CategoryListMatcher& CategoryListMatcher::hasExactly(unsigned int amount)
{
    p->m_hasExactly = amount;
    return *this;
}

MatchResult CategoryListMatcher::match(const Category::List& categoryList) const
{
    MatchResult matchResult;

    if (p->m_hasAtLeast && categoryList.size() < p->m_hasAtLeast.get())
    {
        matchResult.failure(
                "Expected at least " + to_string(p->m_hasAtLeast.get())
                        + " categories");
    }

    if (p->m_hasExactly && categoryList.size() != p->m_hasExactly.get())
    {
        matchResult.failure(
                "Expected exactly " + to_string(p->m_hasExactly.get())
                        + " categories");
    }

    if (!p->m_categories.empty())
    {
        switch (p->m_mode)
        {
            case Mode::all:
                p->all(matchResult, categoryList);
                break;
            case Mode::id:
                p->id(matchResult, categoryList);
                break;
        }
    }

    return matchResult;
}

}
}
